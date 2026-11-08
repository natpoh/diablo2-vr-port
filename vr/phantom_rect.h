// phantom_rect.h - where D2R_DepthFog.fx's vignette pass (PS_Phantom / PS_PhantomR and the eyes-by-turns PS_Ov(Made)Phantom(R)) can change the
// picture, as screen rectangles of each eye, worked out on the processor from the uniforms the pass itself reads - so our effect's runtime
// (vr/fx_native.cpp Options::passRects) draws it only there. Serg 2026-11-08 ("давай пофиксим отрисовку амулетов"): at 2880 x 2880 the pass cost
// 0.93 ms a present to draw two wrist orbs a few hundred pixels wide - every pixel of the eye ray-cast both orbs and read the depth.
//
// Why the picture stays the same, pixel for pixel: the pass draws COLOR (the picture copied before it) back with its carriers over it; a
// pixel no carrier reaches comes back exactly as it was. Each carrier below has a hard edge in the shader past which it adds nothing - not a
// fade that only gets small:
//   - the wrist orbs (OrbOn; OneOrb / OneMedal): the halo's smoothstep reaches 0 at 2.6 radii out from the ball's radius (3.6 R from the
//     middle; the medallion's 2.6 past 0.92 R), the ball and its base inside 1.03 R; OrbLook.y <= 0.001 draws nothing;
//   - the quiver orbs (QvN; QuiverOrbs): nothing past 3.2 radii, the radius at most 1.85 QvPos.w (the current orb 1.25x, the hovered 1.60x more);
//   - the nocked arrow's glow (AfOn; QuiverArrowFx): only within 3.8 R of its head or 0.9 R of its shaft (R = min(AfA.w, the arrow's length));
//   - the merc tint (MercOn; MercTint): only where the drawn point lies inside the merc's capsule (MercA - MercB, radius MercA.w).
// Each is bounded by an axis-aligned box in the eye's camera frame, projected the way PixelRayDir un-projects (the eye's CamRight / CamUp /
// CamBack, SkyProj, the picture's size), widened by a few pixels. A box reaching behind the eye is cut at the eye's plane first (every carrier
// above reads only the ray ahead of the eye, t > 0: a hand hanging at the side, half behind the eye, gives the bottom of the picture or nothing -
// it made the pass full screen in 50-80 % of the presents of 0.207.9 while just standing in camp); only an eye inside a box goes full screen.
//   - the point skill's arc (PaOn; PointArcLight), since 2026-11-08: the beam adds nothing past 14 of its widths (the shader's q < 14), the
//     ring is under 3e-6 of light past 24 of its line widths out of its circle (below one step of an 8- or 10-bit picture, sRGB too);
// Whatever is not one of them makes the pass full screen, as before: the low-life and low-mana vignettes (the edges of the picture; only
// while their tint is on, LowHpAlpha / LowMpAlpha > 0), the hitboxes (and [multiplayer] pose_share's markers), the debug lines and balls.
// Result::why says which one, for the 10 s log line.
//
// Pure (no D3D12, no Windows): tested by cleanroom/tests/phantom_rect_test.cpp, and on WARP by tools/phantom_rect_gpu_test.cpp (the effect's own
// pass drawn both ways through vr/fx_native, compared byte for byte).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace phantomrect {

struct V3 { float x = 0, y = 0, z = 0; };
inline V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

// One eye's camera as the pass has it: PhantomEye<e> (the ray's origin, world units relative to where the hands hang), CamRight / CamUp /
// CamBack<e>, SkyProj<e> (x, y the scales, z, w the off-centre shifts: ndc = view.xy / depth * scale - shift)
struct Eye { V3 o, r, u, b; float sp[4] = {1, 1, 0, 0}; };
struct Rect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };   // pixels, [x0, x1) x [y0, y1)
inline int Area(const Rect& r) { return std::max(0, r.x1 - r.x0) * std::max(0, r.y1 - r.y0); }

constexpr int kMarginPx = 3;          // round the projected box: the shader's pixel-wide soft edges, the processor's and the card's rounding
constexpr float kNearMin = 1e-3f;     // a box corner closer than this along the view (or behind the eye): the pass full screen
constexpr int kMaxRects = 8;

// The box [lo, hi] in the world's axes (relative to where the hands hang) as pixels of this eye - the part of it ahead of the eye (the
// carriers read only the ray ahead, t > 0): the box cut at the plane kNearMin ahead of the eye, its corners there and where its edges cross
// that plane projected (a convex body ahead of the eye projects into the hull of its corners' projections). A point of the box closer than
// kNearMin along the view and still inside the picture lies within kNearMin * (1 + 2 * the picture's widest tangent) of the eye: an eye that
// far from the box loses nothing to the cut; nearer: false (the pass full screen). A box all behind the eye: an empty rectangle (no pixel).
inline bool Project(const Eye& e, V3 lo, V3 hi, int w, int h, Rect* out) {
    if (!(e.sp[0] > 0.0f) || !(e.sp[1] > 0.0f)) return false;
    const float k = std::max((1.0f + std::fabs(e.sp[2])) / e.sp[0], (1.0f + std::fabs(e.sp[3])) / e.sp[1]);
    const float m = kNearMin * (1.0f + 2.0f * k);
    if (!std::isfinite(m) || (e.o.x > lo.x - m && e.o.x < hi.x + m && e.o.y > lo.y - m && e.o.y < hi.y + m && e.o.z > lo.z - m && e.o.z < hi.z + m))
        return false;   // (the eye at the box, or numbers that are not numbers)
    V3 c[8];
    float d[8];
    for (int i = 0; i < 8; ++i) {
        c[i] = {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
        d[i] = -Dot(Sub(c[i], e.o), e.b);   // along the view (the view looks down -CamBack)
        if (!std::isfinite(d[i])) return false;
    }
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    int n = 0;
    auto add = [&](V3 p, float depth) {
        const V3 v = Sub(p, e.o);
        depth = std::max(depth, kNearMin);
        const float nx = Dot(v, e.r) / depth * e.sp[0] - e.sp[2];
        const float ny = Dot(v, e.u) / depth * e.sp[1] - e.sp[3];
        const float px = (nx + 1.0f) * 0.5f * (float)w, py = (1.0f - ny) * 0.5f * (float)h;
        x0 = std::min(x0, px); x1 = std::max(x1, px);
        y0 = std::min(y0, py); y1 = std::max(y1, py);
        ++n;
    };
    for (int i = 0; i < 8; ++i)
        if (d[i] >= kNearMin) add(c[i], d[i]);
    for (int i = 0; i < 8; ++i)   // the 12 edges (corners one bit apart) that cross the plane
        for (int bit = 1; bit < 8; bit <<= 1) {
            const int j = i | bit;
            if (j == i || ((d[i] >= kNearMin) == (d[j] >= kNearMin))) continue;
            const float t = (kNearMin - d[i]) / (d[j] - d[i]);
            add(Add(c[i], Mul(Sub(c[j], c[i]), t)), kNearMin);
        }
    if (!n) { *out = Rect{}; return true; }   // all behind the eye: no pixel
    if (!std::isfinite(x0) || !std::isfinite(x1) || !std::isfinite(y0) || !std::isfinite(y1)) return false;
    Rect r;
    r.x0 = std::max(0, (int)std::floor(std::max(-1e6f, x0)) - kMarginPx);
    r.y0 = std::max(0, (int)std::floor(std::max(-1e6f, y0)) - kMarginPx);
    r.x1 = std::min(w, (int)std::ceil(std::min(1e6f, x1)) + kMarginPx);
    r.y1 = std::min(h, (int)std::ceil(std::min(1e6f, y1)) + kMarginPx);
    *out = r;
    return true;
}
// A sphere's box
inline bool ProjectSphere(const Eye& e, V3 c, float radius, int w, int h, Rect* out) {
    return Project(e, {c.x - radius, c.y - radius, c.z - radius}, {c.x + radius, c.y + radius, c.z + radius}, w, h, out);
}
// The box round two points, widened by `radius`
inline bool ProjectCapsule(const Eye& e, V3 a, V3 b, float radius, int w, int h, Rect* out) {
    return Project(e, {std::min(a.x, b.x) - radius, std::min(a.y, b.y) - radius, std::min(a.z, b.z) - radius},
                   {std::max(a.x, b.x) + radius, std::max(a.y, b.y) + radius, std::max(a.z, b.z) + radius}, w, h, out);
}

// What the pass reads, as the effect holds it now (the effect code sets these before the Render; the reader fills them)
struct Inputs {
    bool overlaysApart = false;
    bool hitbox = false, phantom = false, bone = false, dbg = false;   // full screen whenever on
    float lowHp = 0.0f, lowMp = 0.0f;                                      // the vignettes: full screen while > 0
    bool pointArc = false;                                                 // the point skill's arc: its beam's segments and its ring
    float paPts[17][4] = {}, paRing[4] = {}, paSize[4] = {};
    bool orbs = false;
    float orbPos[2][4] = {}, orbLook[2][4] = {};
    float qvN = 0.0f;
    float qvPos[12][4] = {};
    float afOn = 0.0f, afA[4] = {}, afB[4] = {};
    bool merc = false;
    float mercA[4] = {}, mercB[4] = {};
};

// Why a pass went full screen (Result::why), for the log
enum Why { kWhyNone = 0, kWhySize, kWhyHitbox, kWhyPhantom, kWhyBone, kWhyDbg, kWhyLowHp, kWhyLowMp, kWhyEyeOrb, kWhyEyeQuiver, kWhyEyeArrow,
           kWhyEyeMerc, kWhyEyeArc, kWhyNumbers, kWhyCount };
inline const char* WhyName(int why) {
    static const char* const n[kWhyCount] = {"-", "no size", "hitboxes/pose_share", "phantom_ray", "bone_axes", "debug lines", "low-life vignette",
                                             "low-mana vignette", "eye at a wrist orb", "eye at a quiver orb", "eye at the arrow", "eye at the merc",
                                             "eye at the point arc", "numbers not numbers"};
    return why >= 0 && why < kWhyCount ? n[why] : "?";
}

struct Result {
    bool full = true;           // draw the whole picture, as before
    int why = kWhySize;         // when full: which carrier asked for it (Why)
    int n = 0;                  // else these rectangles (n 0: nothing of the pass reaches the picture this present)
    Rect r[kMaxRects];
};

// Overlapping (or touching) rectangles joined, until no two meet; more than kMaxRects: their one box
inline void Merge(std::vector<Rect>& v) {
    bool joined = true;
    while (joined) {
        joined = false;
        for (size_t i = 0; i < v.size() && !joined; ++i)
            for (size_t j = i + 1; j < v.size(); ++j) {
                const Rect& a = v[i];
                const Rect& b = v[j];
                if (a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1) {
                    v[i] = {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
                    v.erase(v.begin() + (long)j);
                    joined = true;
                    break;
                }
            }
    }
    if ((int)v.size() > kMaxRects) {
        Rect u = v[0];
        for (const Rect& r : v) u = {std::min(u.x0, r.x0), std::min(u.y0, r.y0), std::max(u.x1, r.x1), std::max(u.y1, r.y1)};
        v.assign(1, u);
    }
}

// The rectangles of one eye's pass. A whole picture of w x h.
// The point skill's arc (PointArcLight): how far out of its circle the ring can still light a pixel, in its line widths (the tail
// 0.35 exp(-edge / 2) at 24 is 2.2e-6; the line's core and the running wave far less), and the beam's hard edge (the shader's q < 14)
constexpr float kArcRingWidths = 24.0f;
constexpr float kArcBeamWidths = 14.5f;

inline Result Compute(const Inputs& in, const Eye& e, int w, int h) {
    auto full = [](int why) { Result f; f.why = why; return f; };
    if (w <= 0 || h <= 0) return full(kWhySize);
    if (in.hitbox) return full(kWhyHitbox);
    if (in.phantom) return full(kWhyPhantom);
    if (in.bone) return full(kWhyBone);
    if (in.dbg) return full(kWhyDbg);
    if (in.lowHp != 0.0f) return full(kWhyLowHp);
    if (in.lowMp != 0.0f) return full(kWhyLowMp);
    std::vector<Rect> v;
    Rect r;
    if (in.orbs)
        for (int i = 0; i < 2; ++i) {
            const float R = in.orbPos[i][3];
            if (!(in.orbLook[i][1] > 0.001f) || !(R > 0.0f)) continue;   // (OneOrb / OneMedal: nothing drawn)
            if (!ProjectSphere(e, {in.orbPos[i][0], in.orbPos[i][1], in.orbPos[i][2]}, 3.7f * R, w, h, &r)) return full(kWhyEyeOrb);
            v.push_back(r);
        }
    const int nq = std::min(12, (int)in.qvN);
    for (int i = 0; i < nq; ++i) {
        const float R = in.qvPos[i][3] * 1.85f;
        if (!(R > 0.0f)) continue;
        if (!ProjectSphere(e, {in.qvPos[i][0], in.qvPos[i][1], in.qvPos[i][2]}, 3.3f * R, w, h, &r)) return full(kWhyEyeQuiver);
        v.push_back(r);
    }
    if (in.afOn > 0.5f) {
        const V3 a{in.afA[0], in.afA[1], in.afA[2]}, b{in.afB[0], in.afB[1], in.afB[2]};
        const V3 ab = Sub(b, a);
        const float L = std::sqrt(Dot(ab, ab));
        const float R = std::min(in.afA[3], L);
        if (std::isfinite(L) && std::isfinite(R) && R > 0.0f && L >= 1e-3f && in.afB[3] > 0.0f) {
            if (!ProjectCapsule(e, a, b, 3.9f * R, w, h, &r)) return full(kWhyEyeArrow);
            v.push_back(r);
        } else if (!std::isfinite(L) || !std::isfinite(in.afA[3])) {
            return full(kWhyNumbers);   // (numbers that are not numbers: the shader draws nothing, but no box can say so - as before)
        }
    }
    if (in.merc && in.mercB[3] > 0.0f && in.mercA[3] > 0.0f) {
        if (!ProjectCapsule(e, {in.mercA[0], in.mercA[1], in.mercA[2]}, {in.mercB[0], in.mercB[1], in.mercB[2]}, 1.02f * in.mercA[3], w, h, &r))
            return full(kWhyEyeMerc);
        v.push_back(r);
    }
    if (in.pointArc) {
        // PaSize: x the beam's core radius, y the ring's line radius (world units), w the thinnest a line may get (radians from the eye)
        const float sx = in.paSize[0], sy = in.paSize[1], sw = in.paSize[3];
        bool ok = std::isfinite(sx) && std::isfinite(sy) && std::isfinite(sw) && sx >= 0.0f && sy >= 0.0f && sw >= 0.0f && sw * kArcRingWidths < 0.5f;
        for (int i = 0; i < 17 && ok; ++i)
            for (int k = 0; k < 4; ++k) ok = ok && std::isfinite(in.paPts[i][k]);
        for (int k = 0; k < 4 && ok; ++k) ok = ok && std::isfinite(in.paRing[k]);
        if (!ok) return full(kWhyNumbers);
        // the beam: a pixel's ray lights only where it passes within 14 r of a segment, r = max(sx, at * sw), `at` the ray's way to that
        // point - no more than the far end's distance D over (1 - 14 sw): each segment's capsule of 14.5 such r
        for (int i = 0; i < 16; ++i) {
            const V3 a{in.paPts[i][0], in.paPts[i][1], in.paPts[i][2]}, b{in.paPts[i + 1][0], in.paPts[i + 1][1], in.paPts[i + 1][2]};
            const float D = std::max(std::sqrt(Dot(Sub(a, e.o), Sub(a, e.o))), std::sqrt(Dot(Sub(b, e.o), Sub(b, e.o))));
            const float rr = kArcBeamWidths * std::max(sx, sw * D / (1.0f - kArcBeamWidths * sw));
            if (!ProjectCapsule(e, a, b, rr, w, h, &r)) return full(kWhyEyeArc);
            v.push_back(r);
        }
        // the ring: where the ray meets its ground (y = PaRing.y), only within kArcRingWidths line widths w = max(sy, t sw) out of the circle
        // (t, the ray's way there, no more than the middle's distance d0 plus the way out): a flat disc of that reach
        const V3 c{in.paRing[0], in.paRing[1], in.paRing[2]};
        const float R = std::max(0.0f, in.paRing[3]);
        const float d0 = std::sqrt(Dot(Sub(c, e.o), Sub(c, e.o)));
        const float reach = std::max(R + kArcRingWidths * sy, (R + kArcRingWidths * sw * d0) / (1.0f - kArcRingWidths * sw));
        if (!Project(e, {c.x - reach, c.y, c.z - reach}, {c.x + reach, c.y, c.z + reach}, w, h, &r)) return full(kWhyEyeArc);
        v.push_back(r);
    }
    v.erase(std::remove_if(v.begin(), v.end(), [](const Rect& x) { return Area(x) <= 0; }), v.end());
    Merge(v);
    Result out;
    out.full = false;
    out.why = kWhyNone;
    out.n = (int)v.size();
    for (int i = 0; i < out.n; ++i) out.r[i] = v[(size_t)i];
    return out;
}

// The pass's eye by its entry point ("F__PS_PhantomR", "PS_OvMadePhantom"...): 0 left, 1 right, -1 not the vignette pass
inline int EyeOfEntry(const char* entry) {
    if (!entry) return -1;
    const char* n = (entry[0] == 'F' || entry[0] == 'E') && entry[1] == '_' && entry[2] == '_' ? entry + 3 : entry;
    static const char* const left[] = {"PS_Phantom", "PS_OvPhantom", "PS_OvMadePhantom"};
    static const char* const right[] = {"PS_PhantomR", "PS_OvPhantomR", "PS_OvMadePhantomR"};
    for (const char* s : left) if (!strcmp(n, s)) return 0;
    for (const char* s : right) if (!strcmp(n, s)) return 1;
    return -1;
}

// Read every input through the effect's uniforms: readF(name, values, count) / readB(name, &value) - false when the effect has none (left off)
template <class ReadF, class ReadB>
inline void Read(ReadF readF, ReadB readB, Inputs* in, Eye eyes[2]) {
    auto b = [&](const char* n) { bool v = false; return readB(n, &v) && v; };
    in->overlaysApart = b("OverlaysApart");
    in->hitbox = b("HitboxOn"); in->pointArc = b("PaOn"); in->phantom = b("PhantomOn"); in->bone = b("BoneOn"); in->dbg = b("DbgOn");
    readF("LowHpAlpha", &in->lowHp, 1);
    readF("LowMpAlpha", &in->lowMp, 1);
    in->orbs = b("OrbOn");
    if (in->orbs) { readF("OrbPos", &in->orbPos[0][0], 8); readF("OrbLook", &in->orbLook[0][0], 8); }
    readF("QvN", &in->qvN, 1);
    if (in->qvN > 0.5f) readF("QvPos", &in->qvPos[0][0], 48);
    readF("AfOn", &in->afOn, 1);
    if (in->afOn > 0.5f) { readF("AfA", in->afA, 4); readF("AfB", in->afB, 4); }
    in->merc = b("MercOn");
    if (in->merc) { readF("MercA", in->mercA, 4); readF("MercB", in->mercB, 4); }
    if (in->pointArc) { readF("PaPts", &in->paPts[0][0], 68); readF("PaRing", in->paRing, 4); readF("PaSize", in->paSize, 4); }
    const char* const names[2][5] = {{"PhantomEye0", "CamRight0", "CamUp0", "CamBack0", "SkyProj0"},
                                     {"PhantomEye1", "CamRight1", "CamUp1", "CamBack1", "SkyProj1"}};
    for (int e = 0; e < 2; ++e) {
        readF(names[e][0], &eyes[e].o.x, 3);
        readF(names[e][1], &eyes[e].r.x, 3);
        readF(names[e][2], &eyes[e].u.x, 3);
        readF(names[e][3], &eyes[e].b.x, 3);
        readF(names[e][4], eyes[e].sp, 4);
    }
}

}  // namespace phantomrect
