#pragma once
// stab_debug.h - [debug] stab_debug: what the stab ([weight] stab_in, cleanroom/heft/stab.h) is doing, in the headset. Pure: the weight model hands a Note per
// step that has something to tell (a strike judged, going in, in, out); Board keeps the last of them with the numbers of the strike that was judged; Lines
// writes the readout for the plate over the weapon hand (the thrust debug's, vr/thrust_debug_wire.h: its colours - kPass blue, kFail orange); Marks the
// capsules drawn with [debug] melee_boxes (D2R_DepthFog.fx Hitboxes, melee_boxes.h TouchKind colours): the entry a ball, the joint's line to the depth
// limit a thin grey rod, the depth reached a bar - green sliding, blue locked, red when it came out - and a refused strike an orange ball where it met.
// The author (2026-10-22, the first headset test): «надо какой то дебаг для втыкания ... я пока не вижу чтобы втыкалось».

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "melee_boxes.h"
#include "thrust_debug.h"

namespace stabdbg {

enum class State : uint8_t { None, Refused, In, Locked, Out };

// One step's word from the weight model. Distances in metres, the 3D points in the render world (metres; RenderToHead's frame).
struct Note {
    State state = State::None;
    char why[64] = {};        // the stab's own words (stab::Report::why)
    uint32_t id = 0;
    int side = 0;             // the weapon's hand (0 right, 1 left)
    uint64_t atMs = 0;
    // the strike judged (judged): what Enters looked at, against the settings
    bool judged = false, thrusting = false;
    float speed = 0, minSpeed = 0, angleDeg = 0, maxAngleDeg = 0, fromTip = 0, tipM = 0;
    // the wound
    float depth = 0, limit = 0, deepest = 0, across = 0, tearM = 0, forceN = 0, unlockN = 0;
    // where (render metres): the entry, the line's way in, the contact point of a judged strike
    bool have3d = false, havePoint = false;
    float entry[3] = {}, axis[3] = {}, point[3] = {};
    float upm = 0;            // world units a metre (the view the points went through)
};

constexpr uint64_t kShowMs = 5000;   // the last word is shown this long (an "in" every step it is in)
constexpr uint64_t kMarkMs = 1500;   // a refused or out mark stays this long

// The last word, with the last strike's numbers kept while it is in (the steps that only hold the wound do not judge).
class Board {
public:
    void Take(const Note& n) {
        Note m = n;
        if (!n.judged && last_.judged && (n.state == State::In || n.state == State::Locked || n.state == State::Out)) {
            m.judged = true;
            m.thrusting = last_.thrusting;
            m.speed = last_.speed; m.minSpeed = last_.minSpeed; m.angleDeg = last_.angleDeg; m.maxAngleDeg = last_.maxAngleDeg;
            m.fromTip = last_.fromTip; m.tipM = last_.tipM;
        }
        if (n.state == State::Out && !n.have3d && last_.have3d) {   // (out: drawn where it was)
            m.have3d = true;
            for (int k = 0; k < 3; ++k) { m.entry[k] = last_.entry[k]; m.axis[k] = last_.axis[k]; }
            if (m.limit <= 0) m.limit = last_.limit;
        }
        last_ = m;
        has_ = n.state != State::None;
    }
    bool Has(uint64_t nowMs) const { return has_ && nowMs - last_.atMs < kShowMs; }
    const Note& Last() const { return last_; }

private:
    Note last_;
    bool has_ = false;
};

// The readout: a head line and the numbers, each kPass / kFail against its setting.
inline size_t Lines(const Note& n, thrustdbg::Line* out, size_t max) {
    if (!out || max == 0 || n.state == State::None) return 0;
    size_t k = 0;
    const auto add = [&](thrustdbg::Colour c, const char* fmt, auto... a) {
        if (k >= max) return;
        std::snprintf(out[k].text, thrustdbg::kLineChars, fmt, a...);
        out[k].colour = c;
        ++k;
    };
    using thrustdbg::kFail;
    using thrustdbg::kGrey;
    using thrustdbg::kPass;
    using thrustdbg::kWhite;
    switch (n.state) {
    case State::Refused: add(kFail, "STAB - refused: %s", n.why); break;
    case State::In: add(kPass, "STAB - in, %.1f / %.0f cm", n.depth * 100.0f, n.limit * 100.0f); break;
    case State::Locked: add(kPass, "STAB - locked, %.1f / %.0f cm", n.depth * 100.0f, n.limit * 100.0f); break;
    case State::Out: add(kWhite, "STAB - out: %s", n.why); break;
    default: break;
    }
    if (n.judged) {
        add(n.thrusting ? kPass : kFail, "thrust now: %s", n.thrusting ? "yes" : "no (hit by touch)");
        add(n.speed >= n.minSpeed ? kPass : kFail, "speed %.2f / %.2f m/s", n.speed, n.minSpeed);
        add(n.angleDeg <= n.maxAngleDeg ? kPass : kFail, "angle %.0f / %.0f deg", n.angleDeg, n.maxAngleDeg);
        add(n.fromTip <= n.tipM ? kPass : kFail, "point %.0f / %.0f cm from the tip", n.fromTip * 100.0f, n.tipM * 100.0f);
    }
    if (n.state == State::In || n.state == State::Locked) {
        add(n.across <= n.tearM ? kPass : kFail, "hand across %.0f / %.0f cm", n.across * 100.0f, n.tearM * 100.0f);
        if (n.state == State::Locked) add(kGrey, "pull %.0f / %.0f N to unlock", n.forceN, n.unlockN);
    }
    if (n.state == State::Out) add(kGrey, "deepest %.1f cm", n.deepest * 100.0f);
    return k;
}

// The marks, in world units from where the hands hang (eyeWorld), as monsterbodies::Pack; upm: world units a metre. How many were packed (up to max).
inline int Marks(const Note& n, uint64_t nowMs, float upm, const float eyeWorld[3], float (*a)[4], float (*b)[4], int max) {
    if (max <= 0 || !(upm > 1e-3f) || n.state == State::None || nowMs - n.atMs > kShowMs) return 0;
    const bool fresh = nowMs - n.atMs <= kMarkMs;
    int k = 0;
    const auto put = [&](const float p[3], const float q[3], float radiusM, int colour, int style) {
        if (k >= max) return;
        for (int j = 0; j < 3; ++j) {
            a[k][j] = p[j] * upm - eyeWorld[j];
            b[k][j] = q[j] * upm - eyeWorld[j];
        }
        a[k][3] = radiusM * upm;
        b[k][3] = static_cast<float>(meleeboxes::TouchKind(colour, style));
        ++k;
    };
    // (melee_boxes.h's colours: 0 blue, 1 amber, 2 grey, 3 green, 4 red, 6 orange; style 0 solid, 1 glass)
    if (n.state == State::Refused) {
        if (fresh && n.havePoint) put(n.point, n.point, 0.03f, 6, 0);
        return k;
    }
    if (!n.have3d || (n.state == State::Out && !fresh)) return k;
    float back[3], end[3], reached[3];
    const float depth = std::clamp(n.state == State::Out ? n.deepest : n.depth, 0.0f, std::max(n.limit, 0.0f));
    for (int j = 0; j < 3; ++j) {
        back[j] = n.entry[j] - n.axis[j] * 0.05f;
        end[j] = n.entry[j] + n.axis[j] * std::max(n.limit, 0.0f);
        reached[j] = n.entry[j] + n.axis[j] * depth;
    }
    put(back, end, meleeboxes::kTouchLineRadiusM, 2, 1);                                                    // the joint's line, to the limit
    put(n.entry, reached, meleeboxes::kTouchArrowRadiusM, n.state == State::Locked ? 0 : n.state == State::Out ? 4 : 3, 0);   // how deep
    put(n.entry, n.entry, 0.025f, 1, 0);                                                                     // the entry
    return k;
}

}  // namespace stabdbg
