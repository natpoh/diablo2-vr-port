// rune_geom.h - [openxr] runes_draw: what the shared block's numbers mean in the room. Pure: no game, no Windows calls, no
// OpenXR, no D3D - only bodywalk/game_link.h's structs for the block's layout.
//
// A gesture engine outside the game recognises the shape the player draws in the air with a hand and writes a finished description
// every frame (BW_LinkVrRunes: for each rune on show its picture on a panel, how much of it is matched, the hand's trail, colours,
// fades). The plugin only DRAWS it; this header holds the maths between the block and the picture:
//   - the colours (the block's R-in-the-low-byte bytes into linear, the fade and the flash folded in),
//   - the picture's panel (an upright quad at the block's place, turned by its right/up) and which segments are lit,
//   - the trail: the hand's path in the room projected from the head onto ONE billboard that faces the head (a quad layer cannot
//     hold a 3-D ribbon; flat at the billboard's depth is fine for a glowing line),
//   - the layer budget (pictures before trails) and the 0.2 s local ease-out of a rune that vanishes while it was still showing.
// The look itself (the glow, the halo, the dark rim) is the pixel shader's, vr/rune_shader.h; this decides where and in what colour.
//
// Space: metres, the STAGE space of the game's own head (the space the headset poses the game sends the engine are in), x right,
// y up, z back (OpenXR). A quad layer takes these numbers as they are, in that space.
#pragma once

#include <bodywalk/game_link.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace runes {

constexpr uint32_t kMax = BW_LINK_VRRUNES_MAX;           // runes at once
constexpr uint32_t kPoints = BW_LINK_RUNE_POINTS;        // a picture's points
constexpr uint32_t kTrailPoints = BW_LINK_RUNE_TRAIL;    // a trail's points
constexpr uint32_t kPictureSegs = kPoints - 1;
constexpr uint32_t kTrailSegs = kTrailPoints - 1;
constexpr uint32_t kLayers = kMax * 2;                   // a picture and a trail each, at most

// -- small vectors and turns (x y z w) -------------------------------------------------------------------------------------------
struct V3 { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };

inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(V3 a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline V3 From3(const float* p) { return {p[0], p[1], p[2]}; }
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
// A number in [lo, hi]; a NaN or infinity is `def`.
inline float Num(float v, float lo, float hi, float def) { return std::isfinite(v) ? std::min(std::max(v, lo), hi) : def; }

// The turn whose columns are x, y, z (the basis of a quad: x its right, y its up, z its front - the way it faces).
inline Quat FromAxes(V3 x, V3 y, V3 z) {
    const float m00 = x.x, m01 = y.x, m02 = z.x, m10 = x.y, m11 = y.y, m12 = z.y, m20 = x.z, m21 = y.z, m22 = z.z;
    const float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-9f ? Quat{q.x / n, q.y / n, q.z / n, q.w / n} : Quat{};
}
inline V3 Rotate(const Quat& q, V3 v) {
    const V3 u{q.y * v.z - q.z * v.y + q.w * v.x, q.z * v.x - q.x * v.z + q.w * v.y, q.x * v.y - q.y * v.x + q.w * v.z};
    return {v.x + 2.0f * (q.y * u.z - q.z * u.y), v.y + 2.0f * (q.z * u.x - q.x * u.z), v.z + 2.0f * (q.x * u.y - q.y * u.x)};
}

// -- colours -----------------------------------------------------------------------------------------------------------------------
// The block's colours are IM_COL32-like: R in the low byte, alpha in the top one, the three colour bytes sRGB. The picture is
// composed in linear light and the swapchain is an sRGB format, so the colours go in as exact sRGB -> linear and come out through the
// hardware's encode: a colour the engine picks reaches the headset as the same colour at full opacity. (A plain power of 2.2, as the
// ball shader's constants use, is within a code or two of this everywhere that matters.)
inline float SrgbToLinear(float c) {
    c = Clamp01(c);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float LinearToSrgb(float l) {
    l = Clamp01(l);
    return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}
struct Rgba { float r = 0, g = 0, b = 0, a = 0; };   // linear light, NOT premultiplied
inline Rgba Unpack(uint32_t c) {
    return {SrgbToLinear((float)(c & 0xFFu) / 255.0f), SrgbToLinear((float)((c >> 8) & 0xFFu) / 255.0f),
            SrgbToLinear((float)((c >> 16) & 0xFFu) / 255.0f), (float)((c >> 24) & 0xFFu) / 255.0f};
}
// The colour with its alpha times `fade`, its rgb mixed toward white by `flash` (the shape was completed: the spell went off).
inline Rgba Shown(uint32_t packed, float fade, float flash) {
    Rgba c = Unpack(packed);
    c.r += (1.0f - c.r) * flash;
    c.g += (1.0f - c.g) * flash;
    c.b += (1.0f - c.b) * flash;
    c.a *= fade;
    return c;
}

// -- one rune's look, sanitised --------------------------------------------------------------------------------------------------
constexpr float kMinWidth = 0.002f, kMaxWidth = 0.08f;   // a line's width, metres: a number the eye can see and a panel can hold
struct Look {
    Rgba rune, lit, trail;     // linear, straight; alpha = opacity x fade; mixed toward white by the flash
    float lineW = 0.012f, trailW = 0.010f;   // metres
    float glow = 0.0f;         // 0..1
    float fade = 0.0f;         // 0..1 as sent (kept: the ease-out and the tests read it)
    bool shown = false;        // anything to see at all
};
inline Look LookOf(const BW_LinkVrRune& r) {
    Look k;
    k.fade = Num(r.fade, 0.0f, 1.0f, 0.0f);
    const float flash = Num(r.flash, 0.0f, 1.0f, 0.0f);
    k.rune = Shown(r.runeRgba, k.fade, flash);
    k.lit = Shown(r.litRgba, k.fade, flash);
    k.trail = Shown(r.trailRgba, k.fade, flash);
    k.lineW = Num(r.lineWidth, kMinWidth, kMaxWidth, 0.012f);
    k.trailW = Num(r.trailWidth, kMinWidth, kMaxWidth, 0.010f);
    k.glow = Num(r.glow, 0.0f, 1.0f, 0.0f);
    k.shown = k.fade > 0.003f && (k.rune.a > 0.003f || k.lit.a > 0.003f || k.trail.a > 0.003f);
    return k;
}

// -- the picture: which segments are lit -----------------------------------------------------------------------------------------
// Segment i (pts[i] -> pts[i+1]) is LIT when i < progress * (pointCount - 1); the number of lit ones is therefore the ceiling.
inline uint32_t LitSegments(float progress, uint32_t pointCount) {
    if (pointCount < 2 || !std::isfinite(progress) || progress <= 0.0f) return 0;
    const float x = std::min(progress, 1.0f) * (float)(pointCount - 1);
    return std::min<uint32_t>(pointCount - 1, (uint32_t)std::ceil(x));
}
inline bool SegmentLit(uint32_t i, float progress, uint32_t pointCount) { return i < LitSegments(progress, pointCount); }

// -- how far a line's glow reaches (the shader's halo; the panel is padded by it so the halo is gone before the quad's edge) ------
inline float HaloScale(float width, float glow) { return width * (0.6f + 1.2f * glow); }
inline float Reach(float width, float glow) { return 0.5f * width + 4.0f * HaloScale(width, glow); }
constexpr float kBlobSize = 1.1f;      // the trail's blob: solid out to this many line widths from the hand
constexpr float kBlobHalo = 0.45f;     // ... its halo's scale, in line widths
inline float BlobReach(float width) { return (kBlobSize + 4.0f * kBlobHalo) * width; }

// -- the picture's panel ------------------------------------------------------------------------------------------------------------
// A square quad centred on the block's `centre`, turned by its right/up (made orthonormal), large enough for the points (halfW,
// halfH) and the halo. cross(right, up) is the quad's front: the engine puts the panel facing the player.
struct Panel {
    V3 centre;
    Quat q;
    float side = 0.0f;   // metres
};
constexpr float kMaxSide = 6.0f;
inline bool PicturePanel(const BW_LinkVrRune& r, const Look& look, Panel* out) {
    if (r.pointCount < 2) return false;
    const V3 c = From3(r.centre), ru = From3(r.right), up = From3(r.up);
    if (!Finite(c) || !Finite(ru) || !Finite(up)) return false;
    const float lr = Len(ru);
    if (!(lr > 1e-3f)) return false;
    const V3 x = ru * (1.0f / lr);
    const V3 u = up - x * Dot(up, x);
    const float lu = Len(u);
    if (!(lu > 1e-3f)) return false;
    const V3 y = u * (1.0f / lu);
    const float hw = Num(r.halfW, 0.0f, 100.0f, -1.0f), hh = Num(r.halfH, 0.0f, 100.0f, -1.0f);
    if (!(hw > 0.005f) || !(hh > 0.005f)) return false;
    const float side = 2.0f * std::max(hw, hh) + 2.0f * Reach(look.lineW, look.glow);
    if (!(side <= kMaxSide)) return false;
    out->centre = c;
    out->q = FromAxes(x, y, Cross(x, y));
    out->side = side;
    return true;
}

// -- the trail: the hand's path as one billboard facing the head --------------------------------------------------------------------
// Each trail point is projected onto a plane through the trail's middle, square to the line from the head to it, along the ray from
// the head through the point: the line stays where the hand drew it as seen from the head, however the head has moved since. The
// board is as large as the projected line, a blob's reach and the halo need, and re-centred on the line (in its own plane: its
// normal stays the one toward the middle of the trail, within a few degrees of the one toward the board's centre).
struct TrailBoard {
    V3 centre;
    Quat q;
    float side = 0.0f;
    uint32_t segs = 0;                 // segments, oldest first; a point that cannot be projected (nearly level with the head or
                                       // behind it) leaves a gap, and so does a point that is not a number
    float seg[kTrailSegs][4] = {};     // x0 y0 x1 y1, metres from the board's centre, x along its right, y along its up
    bool blob = false;                 // the last point (the hand) is on the board
    float blobAt[2] = {0.0f, 0.0f};
};
constexpr float kMinBoardDistance = 0.15f, kMaxBoardDistance = 30.0f;
constexpr float kMaxMagnify = 5.0f;   // a point nearer the head than this fraction of the board's distance is not drawn
constexpr float kMinSegment = 1e-4f;  // metres: a shorter step is the hand standing still

// `forward` is the way the head looks (any length but zero): a point that is not ahead of the head has no place on a board that faces
// it, and one such point must not drag the board's middle toward the head.
constexpr float kMinAhead = 0.05f;   // metres ahead of the head, at least
inline bool ProjectTrail(const BW_LinkVrRune& r, const Look& look, V3 head, V3 forward, TrailBoard* out) {
    if (!Finite(head) || !Finite(forward)) return false;
    const float fl = Len(forward);
    if (!(fl > 0.5f)) return false;
    forward = forward * (1.0f / fl);
    const uint32_t n = std::min<uint32_t>(r.trailCount, kTrailPoints);
    if (n == 0) return false;
    V3 p[kTrailPoints];
    bool ok[kTrailPoints];
    V3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    uint32_t good = 0;
    for (uint32_t i = 0; i < n; ++i) {
        p[i] = From3(r.trail[i]);
        ok[i] = Finite(p[i]) && Dot(p[i] - head, forward) >= kMinAhead;
        if (!ok[i]) continue;
        ++good;
        lo = {std::min(lo.x, p[i].x), std::min(lo.y, p[i].y), std::min(lo.z, p[i].z)};
        hi = {std::max(hi.x, p[i].x), std::max(hi.y, p[i].y), std::max(hi.z, p[i].z)};
    }
    if (!good) return false;
    const V3 mid = (lo + hi) * 0.5f;
    const V3 toHead = head - mid;
    const float dist = Len(toHead);
    if (!(dist >= kMinBoardDistance) || !(dist <= kMaxBoardDistance)) return false;
    const V3 nrm = toHead * (1.0f / dist);   // the board's front
    const V3 hint = std::fabs(nrm.y) > 0.98f ? V3{0.0f, 0.0f, -1.0f} : V3{0.0f, 1.0f, 0.0f};
    const V3 xa = Cross(hint, nrm) * (1.0f / Len(Cross(hint, nrm)));
    const V3 ya = Cross(nrm, xa);
    float uv[kTrailPoints][2];
    float ulo = 1e30f, uhi = -1e30f, vlo = 1e30f, vhi = -1e30f;
    uint32_t seen = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (!ok[i]) continue;
        const V3 rel = p[i] - head;
        const float depth = -Dot(rel, nrm);   // along the way the head looks at the board
        if (!(depth * kMaxMagnify >= dist)) { ok[i] = false; continue; }
        const V3 onBoard = rel * (dist / depth) + head - mid;   // in the board's plane (its normal part is zero)
        uv[i][0] = Dot(onBoard, xa);
        uv[i][1] = Dot(onBoard, ya);
        ulo = std::min(ulo, uv[i][0]); uhi = std::max(uhi, uv[i][0]);
        vlo = std::min(vlo, uv[i][1]); vhi = std::max(vhi, uv[i][1]);
        ++seen;
    }
    if (!seen) return false;
    const float cu = 0.5f * (ulo + uhi), cv = 0.5f * (vlo + vhi);
    const float pad = std::max(Reach(look.trailW, look.glow), BlobReach(look.trailW));
    const float side = std::max(uhi - ulo, vhi - vlo) + 2.0f * pad;
    if (!(side <= kMaxSide)) return false;
    out->centre = mid + xa * cu + ya * cv;
    out->q = FromAxes(xa, ya, nrm);
    out->side = side;
    out->segs = 0;
    out->blob = false;
    for (uint32_t i = 0; i + 1 < n; ++i) {
        if (!ok[i] || !ok[i + 1]) continue;
        const float dx = uv[i + 1][0] - uv[i][0], dy = uv[i + 1][1] - uv[i][1];
        if (!(dx * dx + dy * dy >= kMinSegment * kMinSegment)) continue;
        float* s = out->seg[out->segs++];
        s[0] = uv[i][0] - cu; s[1] = uv[i][1] - cv; s[2] = uv[i + 1][0] - cu; s[3] = uv[i + 1][1] - cv;
    }
    if (ok[n - 1]) {   // the last point is the hand
        out->blob = true;
        out->blobAt[0] = uv[n - 1][0] - cu;
        out->blobAt[1] = uv[n - 1][1] - cv;
    }
    return out->segs > 0 || out->blob;
}

// -- the layer budget ---------------------------------------------------------------------------------------------------------------
// `room` layers are free for the runes: the pictures first, the trails with what is left.
inline void Budget(uint32_t pictures, uint32_t trails, uint32_t room, uint32_t* keepPictures, uint32_t* keepTrails) {
    *keepPictures = std::min(pictures, room);
    *keepTrails = std::min(trails, room - *keepPictures);
}

// -- the local ease-out --------------------------------------------------------------------------------------------------------------
// The engine fades a rune out itself (its fade reaches 0 before it drops one). If it vanishes while it was still plainly showing
// (it stopped, died, or dropped a rune in a hurry) the rune is held for kEaseMs more, fading to nothing, rather than popping. Keyed by
// the rune's id; at most kMax runes in all. Everything is dropped at once by Reset (another view, the feature off, the session over).
constexpr uint64_t kEaseMs = 200;
constexpr float kEaseFrom = 0.05f;   // a rune whose last fade was under this just goes
class Ease {
 public:
    // `cur[0..n)` as read this frame, `nowMs` a monotonic clock; fills out[] (room for kMax) with what to draw: the current ones as
    // they are, then those easing out with their fade scaled. Returns how many.
    uint32_t Step(const BW_LinkVrRune* cur, uint32_t n, uint64_t nowMs, BW_LinkVrRune* out) {
        n = std::min(n, kMax);
        bool seen[kMax] = {};
        uint32_t outN = 0;
        for (uint32_t i = 0; i < n; ++i) {   // the current ones: kept as the last look
            Slot* s = Find(cur[i].id);
            if (!s) s = Free();
            if (s) { s->rune = cur[i]; s->used = true; s->goneAt = 0; seen[s - slot_] = true; }
            out[outN++] = cur[i];
        }
        for (uint32_t k = 0; k < kMax; ++k) {   // the others: easing out, or let go
            Slot& s = slot_[k];
            if (!s.used || seen[k]) continue;
            if (!s.goneAt) {
                if (Num(s.rune.fade, 0.0f, 1.0f, 0.0f) < kEaseFrom) { s.used = false; continue; }
                s.goneAt = nowMs ? nowMs : 1;
            }
            const uint64_t age = nowMs - s.goneAt;
            if (age >= kEaseMs) { s.used = false; continue; }
            if (outN >= kMax) continue;
            const float left = 1.0f - (float)age / (float)kEaseMs;
            out[outN] = s.rune;
            out[outN].fade = Num(s.rune.fade, 0.0f, 1.0f, 0.0f) * left * left;
            ++outN;
        }
        return outN;
    }
    void Reset() { for (Slot& s : slot_) { s.used = false; s.goneAt = 0; } }

 private:
    struct Slot { BW_LinkVrRune rune{}; bool used = false; uint64_t goneAt = 0; };
    Slot slot_[kMax];
    Slot* Find(uint32_t id) { for (Slot& s : slot_) if (s.used && s.rune.id == id) return &s; return nullptr; }
    // A free slot, else the one that has been easing out longest (a new rune comes before an old one's ghost).
    Slot* Free() {
        Slot* ghost = nullptr;
        for (Slot& s : slot_) {
            if (!s.used) return &s;
            if (s.goneAt && (!ghost || s.goneAt < ghost->goneAt)) ghost = &s;
        }
        return ghost;
    }
};

// -- what the log says about the block -------------------------------------------------------------------------------------------
// One line when a live block first appears (once per run), one when it goes stale with runes still on show (the engine stopped writing
// mid-gesture); the ordinary end - the engine writes a block with no runes in it, then nothing for as long as nothing is drawn - is
// silent, so a player who draws a hundred runes gets one line, not two hundred.
enum class Event { None, Appeared, WentStale };
class Watch {
 public:
    // `fresh`: the block was written within half a second; `count`: the runes it says are on show.
    Event Step(bool fresh, uint32_t count) {
        const bool onShow = fresh && count > 0;
        if (onShow && !live_) {
            live_ = true;
            if (told_) return Event::None;
            told_ = true;
            return Event::Appeared;
        }
        if (!onShow && live_) {
            live_ = false;
            return !fresh && count > 0 ? Event::WentStale : Event::None;
        }
        return Event::None;
    }

 private:
    bool live_ = false, told_ = false;
};

}  // namespace runes
