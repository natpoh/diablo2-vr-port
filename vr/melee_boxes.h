// melee_boxes.h - [debug] melee_boxes: the capsules hit by touch (melee_touch.h) tests, drawn in the headset so they can be
// seen against the monsters they stand for. Pure part: the list of what to draw, the frame conversion to what the shader
// takes, the speed peak and the text of the 5 Hz log line. No game calls, no globals (the state is melee_boxes.cpp's).
//
// What is drawn (D2R_DepthFog.fx Hitboxes, see-through, rimmed):
//   cyan     the blade, exactly as the stroke engine sweeps it (the weapon bone as drawn, vr/blade_shape.h)
//   teal     the second blade, when a melee weapon is held in each hand (a Barbarian's two weapons, an Assassin's two claws)
//   green    a blade (either) while a contact would COUNT: the stroke engine's latest step swept something - a stroke is on and the blade
//            is moving at or above [gameplay] melee_min_speed, the very speed value the engine gates on. Back to cyan / teal when it slows
//   yellow   the capsule of every monster in the scan (the ones melee_touch looks at: within 24 game units of the hero)
//   pale     a monster the game's own checks refuse (not hostile, or a wall between): a touch on it does nothing
//   red      the monster a touch was accepted on, for half a second - the game's own attack key was pressed on it
//   magenta  the same, when the Direct hit route took it ([gameplay] melee_direct)
//   white ball   where the blade's speed REACHED [gameplay] melee_min_speed in a swing (vr/melee_marks.h): the tip's place at the moment
//                a contact started to count; orange ball: where it fell below that again. Each stays [debug] melee_mark_ms (800)
//   trail        the tip's path over the same time as a thin tube: cyan where a contact would not count, green where it would
// The nearest three monsters of the scan are always drawn, whatever the game says of them (pale if refused); with more than fit
// (kMaxCapsules) the farthest are left out - and the marks and the trail take their places before the farther monsters do.
// It is a drawing only: nothing here feeds back into the detector.
//
// Frames. melee_touch works in the render world over the units per metre (upm): the blade comes from the weapon bone as drawn
// (vr/blade_shape.h: the bone in the head's frame, hung off where the hands hang), the monsters from the game's grid by
// meleetouch::BodyFor (grid / upm, sheared up by the hero's floor), so a blade and a body are in ONE frame and are tested against
// each other there. The shader works in world units relative to where the hands hang (vrcam's g_eyeWorld), because the camera's
// axes and each eye's offset are given there. Pack() is the whole conversion: metres * upm - eyeWorld.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "melee_marks.h"
#include "stroke_contact.h"

namespace meleeboxes {

constexpr int kMaxCapsules = 32;          // the shader's HitboxA / HitboxB
constexpr size_t kMaxMonsters = 48;       // melee_touch's scan holds this many
constexpr size_t kMaxStrikes = 4;
constexpr int kNearestAlways = 3;         // the nearest monsters that are drawn whatever the game says of them
constexpr uint64_t kStruckMs = 500;       // a struck monster stays red / magenta this long
constexpr uint64_t kStaleMs = 300;        // nothing is drawn from a scene older than this (melee_touch stopped feeding it)
constexpr uint64_t kLogMs = 200;          // the log line: 5 Hz
constexpr int kLogMonsters = 3;
static_assert(kMaxCapsules - 2 - static_cast<int>(kMaxStrikes) >= kNearestAlways, "two blades and every strike must leave room for the nearest monsters");

// The shader's HitboxB.w: the colour.
enum Kind : int { kBlade = 0, kMonster = 1, kStruckKey = 2, kStruckDirect = 3, kRefused = 4, kBlade2 = 5, kBladeHot = 6,
                  kMarkReached = 7, kMarkSlowed = 8, kTrailCold = 9, kTrailHot = 10 };

// What the server's checks said of a monster (melee_touch's Verdicted): not asked yet / refused / hostile with nothing between.
enum class Verdict : uint8_t { Unknown, Refused, Hostile };

struct Monster {
    uint32_t id = 0, cls = 0;
    float d2 = 0.0f;                 // squared distance to the hero on the grid, game units (as the scan sorts)
    stroke::Body body;               // melee_touch's frame, metres
    Verdict verdict = Verdict::Unknown;
};

struct Capsule {
    stroke::Vec a, b;                // melee_touch's frame, metres
    float radius = 0.0f;
    Kind kind = kMonster;
};

// ---- the highest speed since the last look ----------------------------------------------------------------------------------
// What a 5 Hz log line can show of a 90 Hz motion. The speeds fed to it are the stroke engine's own (BladeIn::speed): there is no
// second estimate of the blade's speed here.
class PeakTracker {
public:
    void Note(float speed) { if (std::isfinite(speed)) peak_ = std::max(peak_, speed); }
    float Take(float current) { const float p = peak_; peak_ = std::isfinite(current) ? current : 0.0f; return p; }

private:
    float peak_ = 0.0f;
};

// ---- what melee_touch hands over, once per pose ------------------------------------------------------------------------------
// One blade of the pose: [0] the primary (the hand slot PosedWeapon names), [1] the other hand's, when two melee weapons are held.
struct BladeIn {
    bool have = false;
    int side = 0;                    // the hand slot: 0 right, 1 left (for the log)
    stroke::Blade blade;             // metres
    bool inStroke = false;           // the engine says a stroke is on
    float speed = 0.0f;              // the engine's speed of this pose's step, m/s: stroke::Engine::LastSpeed - the value its gate is on
    bool counting = false;           // the engine's step swept something: a contact would count right now (stroke::Engine::Counting)
    float sweepSpeed = 0.0f;         // the speed of the last sweep the engine made, m/s ...
    uint64_t sweepMs = 0;            // ... and when (0 = none yet)
};
struct Frame {
    uint64_t nowMs = 0;
    float upm = 4.0f;                // world units per metre of the frame below
    float floorY = 0.0f;             // metres: the hero's floor in that frame (the heights in the log are over it)
    float minSpeed = 0.8f;           // the speed a contact needs ([gameplay] melee_min_speed), for the log
    uint32_t markMs = kDefaultMarkMs;   // how long the speed marks and the trail stay ([debug] melee_mark_ms; 0 = none); the module fills it
    BladeIn blades[2];
    const Monster* monsters = nullptr;
    size_t count = 0;
};

// ---- the scene -------------------------------------------------------------------------------------------------------------
class Scene {
public:
    void Set(const Frame& f) {
        at_ = f.nowMs;
        upm_ = f.upm;
        floorY_ = f.floorY;
        markMs_ = std::min(f.markMs, kMaxMarkMs);
        for (int i = 0; i < 2; ++i) {
            const BladeIn& in = f.blades[i];
            Blade& b = blades_[i];
            const bool sameHand = b.have && b.side == in.side;   // the slot's blade is still the same hand's: its marks and trail go on
            if (in.have && markMs_) {
                if (!sameHand) tracks_[i].Clear();
                tracks_[i].Feed(f.nowMs, in.blade.tip, in.counting);
            } else {
                tracks_[i].Clear();
            }
            b.have = in.have;
            b.side = in.side;
            b.blade = in.blade;
            b.inStroke = in.inStroke;
            b.speed = in.speed;
            b.counting = in.have && in.counting;
            b.sweepSpeed = in.sweepSpeed;
            b.sweepMs = in.sweepMs;
            if (in.have) b.peak.Note(in.speed);
        }
        minSpeed_ = f.minSpeed;
        count_ = std::min(f.count, monsters_.size());
        if (count_) std::memcpy(monsters_.data(), f.monsters, count_ * sizeof(Monster));
    }

    // A touch was accepted on monster `id` (its capsule is copied now, so the flash outlives the monster's death).
    void Struck(uint32_t id, Kind how, uint64_t nowMs) {
        Strike s;
        s.id = id;
        s.kind = how;
        s.at = nowMs;
        for (size_t i = 0; i < count_; ++i)
            if (monsters_[i].id == id) { s.body = monsters_[i].body; s.haveBody = true; break; }
        for (Strike& t : strikes_)
            if (t.at && t.id == id) { t = s; return; }
        strikes_[nextStrike_++ % strikes_.size()] = s;
    }

    // The capsules to draw at nowMs: the blades first, ordinary monsters nearest the hero first, struck monsters last (drawn on top).
    int Capsules(uint64_t nowMs, Capsule* out, int max) const {
        const bool anyBlade = blades_[0].have || blades_[1].have;
        if (!count_ && !anyBlade) return 0;
        if (nowMs < at_ || nowMs - at_ > kStaleMs || max <= 0) return 0;
        int n = 0;
        const int cap = std::min(max, kMaxCapsules);
        for (int i = 0; i < 2; ++i)
            if (blades_[i].have && n < cap)   // green while a contact would count (the engine's step swept something), else cyan / teal
                out[n++] = {blades_[i].blade.base, blades_[i].blade.tip, blades_[i].blade.radius, blades_[i].counting ? kBladeHot : i == 0 ? kBlade : kBlade2};
        // the monsters in a struck window are drawn last, in their flash's colour
        int struck = 0;
        Capsule hit[kMaxStrikes];
        for (const Strike& t : strikes_) {
            if (!Active(t, nowMs)) continue;
            stroke::Body body = t.body;
            bool have = t.haveBody;
            for (size_t i = 0; i < count_; ++i) if (monsters_[i].id == t.id) { body = monsters_[i].body; have = true; break; }   // alive: where it is now
            if (have) hit[struck++] = {body.bottom, body.top, body.radius, t.kind};
        }
        std::array<uint8_t, kMaxMonsters> order{};
        size_t m = 0;
        for (size_t i = 0; i < count_; ++i) {
            bool isStruck = false;
            for (const Strike& t : strikes_) isStruck = isStruck || (Active(t, nowMs) && t.id == monsters_[i].id);
            if (!isStruck) order[m++] = static_cast<uint8_t>(i);
        }
        std::sort(order.begin(), order.begin() + m, [&](uint8_t x, uint8_t y) { return monsters_[x].d2 < monsters_[y].d2; });
        // The speed marks and the trail (vr/melee_marks.h) come before the farther monsters but never before the nearest ones: what is
        // left after the blades, the struck ones and the nearest kNearestAlways monsters is theirs; the marks first, then the trail
        // (its segments shared between the blades).
        Capsule marks[2 * kMarkSlots];
        int nMarks = 0;
        Capsule trail[2 * kTrailSegments];
        int nTrail = 0;
        if (markMs_) {
            const int room = std::max(0, cap - n - struck - static_cast<int>(std::min<size_t>(m, kNearestAlways)));
            for (int i = 0; i < 2; ++i) {
                if (!blades_[i].have) continue;
                Mark mk[kMarkSlots];
                const size_t k = tracks_[i].Marks(nowMs, markMs_, mk, kMarkSlots);
                for (size_t j = 0; j < k && nMarks < room; ++j) {
                    const stroke::Vec up{0.0f, kMarkHalfM, 0.0f};
                    marks[nMarks++] = {mk[j].at - up, mk[j].at + up, kMarkRadiusM, mk[j].reached ? kMarkReached : kMarkSlowed};
                }
            }
            const int tracks = BladeCount();
            const int each = tracks ? std::min(kTrailSegments, (room - nMarks) / tracks) : 0;
            for (int i = 0; i < 2 && each > 0; ++i) {
                if (!blades_[i].have) continue;
                TrailSegment seg[kTrailSegments];
                const size_t k = tracks_[i].Trail(nowMs, markMs_, seg, static_cast<size_t>(each));
                for (size_t j = 0; j < k; ++j) trail[nTrail++] = {seg[j].a, seg[j].b, kTrailRadiusM, seg[j].hot ? kTrailHot : kTrailCold};
            }
        }
        // Room for the struck ones and the marks, then the nearest monsters first: with two blades and four strikes there are still
        // kNearestAlways places (static_assert above), so the nearest monsters are never the ones left out.
        const int reserved = struck + nMarks + nTrail;
        for (size_t k = 0; k < m && n < cap - reserved; ++k) {
            const Monster& mo = monsters_[order[k]];
            out[n++] = {mo.body.bottom, mo.body.top, mo.body.radius, mo.verdict == Verdict::Refused ? kRefused : kMonster};
        }
        for (int k = 0; k < struck && n < cap; ++k) out[n++] = hit[k];
        for (int k = 0; k < nTrail && n < cap; ++k) out[n++] = trail[k];
        for (int k = 0; k < nMarks && n < cap; ++k) out[n++] = marks[k];   // on top
        return n;
    }

    // ---- the log line ----------------------------------------------------------------------------------------------------
    struct Reading {
        uint32_t cls = 0;
        float heroM = 0.0f;          // the monster's place from the hero on the grid, metres
        float radius = 0.0f;         // the capsule's radius, metres
        float lowM = 0.0f, highM = 0.0f;   // the whole capsule over the floor: from its lowest point to its highest, metres
        float tipToAxis = 0.0f;      // a blade's tip to the capsule's axis, metres (the nearest blade's)
        float gap = 0.0f;            // a blade's axis to the capsule's axis less the two radii (the nearest blade's): < 0 is a touch now
        int side = 0;                // which hand's blade that is (0 right, 1 left)
        Verdict verdict = Verdict::Unknown;
    };
    // The monsters nearest a blade's tip first (their distance to the capsule's axis); how many, at most max.
    int Readings(Reading* out, int max) const {
        const bool anyBlade = blades_[0].have || blades_[1].have;
        if (!anyBlade || !count_ || max <= 0) return 0;
        std::array<Reading, kMaxMonsters> all{};
        for (size_t i = 0; i < count_; ++i) {
            const Monster& mo = monsters_[i];
            Reading& r = all[i];
            r.cls = mo.cls;
            r.heroM = std::sqrt(std::max(0.0f, mo.d2)) / std::max(1.0f, upm_);
            r.radius = mo.body.radius;
            r.lowM = mo.body.bottom.y - mo.body.radius - floorY_;
            r.highM = mo.body.top.y + mo.body.radius - floorY_;
            r.tipToAxis = 1e9f;
            r.gap = 1e9f;
            for (const Blade& b : blades_) {
                if (!b.have) continue;
                const float tip = PointSegmentDistance(b.blade.tip, mo.body.bottom, mo.body.top);
                const float gap = std::sqrt(stroke::SegmentDistanceSquared(b.blade.base, b.blade.tip, mo.body.bottom, mo.body.top)) - (mo.body.radius + b.blade.radius);
                if (tip < r.tipToAxis) { r.tipToAxis = tip; r.gap = gap; r.side = b.side; }
            }
            r.verdict = mo.verdict;
        }
        std::sort(all.begin(), all.begin() + count_, [](const Reading& x, const Reading& y) { return x.tipToAxis < y.tipToAxis; });
        const int n = std::min<int>(max, static_cast<int>(count_));
        for (int i = 0; i < n; ++i) out[i] = all[i];
        return n;
    }

    // One line of numbers for the log; false (nothing written) when there is no monster to talk of.
    bool Describe(char* buf, size_t size, uint64_t nowMs) {
        Reading r[kLogMonsters];
        const int n = Readings(r, kLogMonsters);
        if (n <= 0 || !size) return false;
        // One compact line (metres everywhere): per blade the hand, its length and thickness, the hand's speed (the peak since the last
        // line), the stroke and the last sweep; then per monster its class, what the game's checks said, its distance from the hero,
        // the capsule's radius and height over the floor, the nearest blade's tip distance to its axis and the gap between that
        // blade and the capsule (negative: they overlap now).
        int w = std::snprintf(buf, size, "melee boxes:");
        for (Blade& b : blades_) {
            if (!b.have || w <= 0 || static_cast<size_t>(w) >= size) continue;
            const float peak = b.peak.Take(b.speed);
            const double ago = b.sweepMs && nowMs >= b.sweepMs ? static_cast<double>(nowMs - b.sweepMs) / 1000.0 : -1.0;
            // the blade's speed is the engine's own, the one its gate is on: COUNTS = a contact would count now (a stroke on, at or above the
            // speed a contact needs), below = a stroke on but too slow, no stroke = the movement has not become a stroke
            w += std::snprintf(buf + w, size - static_cast<size_t>(w), " %c %.2f m r %.3f: %.1f m/s (peak %.1f, needs %.1f) %s,", b.side ? 'L' : 'R', stroke::Length(b.blade.tip - b.blade.base),
                               b.blade.radius, b.speed, peak, minSpeed_, b.counting ? "COUNTS" : b.inStroke ? "below" : "no stroke");
            if (w > 0 && static_cast<size_t>(w) < size) {
                if (ago >= 0.0 && ago < 60.0) w += std::snprintf(buf + w, size - static_cast<size_t>(w), " sweep %.1f m/s %.1f s ago;", b.sweepSpeed, ago);
                else w += std::snprintf(buf + w, size - static_cast<size_t>(w), " no sweep yet;");
            }
        }
        for (int i = 0; i < n && w > 0 && static_cast<size_t>(w) < size; ++i) {
            const Reading& q = r[i];
            w += std::snprintf(buf + w, size - static_cast<size_t>(w), " | cls %u %s %.1f m: r %.2f, %.2f..%.2f m, %c tip>axis %.2f, gap %+.2f",
                               q.cls, q.verdict == Verdict::Refused ? "refused" : q.verdict == Verdict::Hostile ? "hostile" : "?", q.heroM, q.radius, q.lowM, q.highM,
                               q.side ? 'L' : 'R', q.tipToAxis, q.gap);
        }
        return w > 0;
    }

    bool HaveBlade() const { return blades_[0].have || blades_[1].have; }
    int BladeCount() const { return (blades_[0].have ? 1 : 0) + (blades_[1].have ? 1 : 0); }
    size_t Count() const { return count_; }
    float Upm() const { return upm_; }

    static float PointSegmentDistance(stroke::Vec p, stroke::Vec a, stroke::Vec b) {
        const stroke::Vec ab = b - a;
        const float len2 = stroke::Dot(ab, ab);
        const float t = len2 > 1e-12f ? std::clamp(stroke::Dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        return stroke::Length(p - (a + ab * t));
    }

private:
    struct Strike { uint32_t id = 0; Kind kind = kStruckKey; uint64_t at = 0; stroke::Body body; bool haveBody = false; };
    struct Blade { bool have = false; int side = 0; stroke::Blade blade; bool inStroke = false; bool counting = false; float speed = 0.0f; float sweepSpeed = 0.0f; uint64_t sweepMs = 0; PeakTracker peak; };
    static bool Active(const Strike& t, uint64_t nowMs) { return t.at && nowMs >= t.at && nowMs - t.at < kStruckMs; }
    uint64_t at_ = 0;
    float upm_ = 4.0f, floorY_ = 0.0f, minSpeed_ = 0.8f;
    uint32_t markMs_ = kDefaultMarkMs;
    Blade blades_[2];
    Track tracks_[2];
    std::array<Monster, kMaxMonsters> monsters_{};
    size_t count_ = 0;
    std::array<Strike, kMaxStrikes> strikes_{};
    size_t nextStrike_ = 0;
};

// ---- the frame conversion -----------------------------------------------------------------------------------------------------
// melee_touch's metres -> the shader's world units relative to where the hands hang: A = (a * upm - eyeWorld, radius * upm),
// B = (b * upm - eyeWorld, kind). Capsules that are not finite are left out. Returns how many were packed.
inline int Pack(const Capsule* caps, int n, float upm, const float eyeWorld[3], float (*a)[4], float (*b)[4]) {
    int k = 0;
    for (int i = 0; i < n && k < kMaxCapsules; ++i) {
        const Capsule& c = caps[i];
        if (!stroke::Finite(c.a) || !stroke::Finite(c.b) || !std::isfinite(c.radius) || c.radius <= 0.0f) continue;
        a[k][0] = c.a.x * upm - eyeWorld[0]; a[k][1] = c.a.y * upm - eyeWorld[1]; a[k][2] = c.a.z * upm - eyeWorld[2]; a[k][3] = c.radius * upm;
        b[k][0] = c.b.x * upm - eyeWorld[0]; b[k][1] = c.b.y * upm - eyeWorld[1]; b[k][2] = c.b.z * upm - eyeWorld[2]; b[k][3] = static_cast<float>(c.kind);
        ++k;
    }
    return k;
}

// ---- the module (melee_boxes.cpp): one scene behind a lock, off unless asked for ---------------------------------------------------
// Off (the default): Update and Struck return at once, Take gives 0, nothing is kept.
using LogSink = void (*)(const char* line);
void SetLogSink(LogSink sink);
// [debug] melee_boxes, from the ini (every read).
void SetWanted(bool on);
bool Wanted();
// [debug] melee_mark_ms, from the ini (every read): how long the speed marks and the trail stay, ms; 0 = none (clamped to kMaxMarkMs).
void SetMarkMs(uint32_t ms);
// melee_touch's timer, once per pose, only while Wanted(): the scene as it is now; every 200 ms a line of numbers goes to the log sink.
void Update(const Frame& f);
// melee_touch's timer, when it accepted a touch on monster `id`: kStruckKey (the game's attack key) or kStruckDirect.
void Struck(uint32_t id, Kind how, uint64_t nowMs);
// The render thread, for the effect: this frame's capsules as HitboxA / HitboxB (Pack), 0 when there is nothing fresh to draw.
// maxOut: at most that many (the shader's 32 are shared with vr/skill_triggers.h's capsules: that module takes its share first).
int Take(uint64_t nowMs, const float eyeWorld[3], float (*a)[4], float (*b)[4], int maxOut = kMaxCapsules);

}  // namespace meleeboxes
