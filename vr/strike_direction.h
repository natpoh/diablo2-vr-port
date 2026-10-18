// strike_direction.h - when does a contact of the blade count as a STRIKE: where the blade goes, which part touches, how big the swing is. Pure maths, no game calls (hit by touch,
// vr/melee_touch.cpp). Only whether a contact counts as an attack is decided here - the game's attack, damage, rolls and reach stay the game's, nothing is scaled.
//
// The swing engine (stroke_contact.h) counts any fast movement of the blade through a monster's body. Three rules sit on top, per weapon kind (the 15 the touch swings):
//   toward     (every kind, on by default) at the contact the touching POINT of the blade moves TOWARD the monster: the angle between its velocity and the direction from the point to the
//              nearest point of the monster capsule's axis is under `towardDeg` (60: the dot product above 0.5). An overhead blow down, an uppercut from below, a sideways sweep into it, a
//              thrust all go toward it; the wind-up (the head goes up and back, away from a monster in front), the pull-back after a blow, a graze along the side do not.
//   head       (maces and scepters by default) the contact is with the HEAD of the weapon: the outer `headPct` of the blade's length (the end where the mass is), not the handle or the shaft.
//   amplitude  (every kind, on by default) the head has travelled at least this far in this stroke before the contact: a real swing, not a flick - the PIERCING amplitude in cm is the base
//              (a dagger's or a spear's short jab can count), the edged kinds need it times `edgedX`, the blunt ones `bluntX` (2), the staffs `staffX`, each kind may carry a factor of its
//              own (0 = its family's). The least speed is the engine's own ([gameplay] melee_min_speed, and a kind's own: melee_min_speed_<kind>).
// A refused contact does not use the monster up (the HitSet takes only what is pressed): a later contact of the same stroke - or the blow after a wind-up, a stroke of its own - counts.
//
// The debug drawing (vr/melee_boxes.h) asks the same questions of the blade as it moves (Look), against the monster nearest it, so green means "a contact would count now".
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "d2r_vr_state.h"
#include "stroke_contact.h"
#include "stroke_tuning.h"

namespace strikedir {

// The kinds the touch swings, in the order of the ini keys' names (<key>_<name>). Javelins and throwing weapons only with [gameplay] melee_touch_thrown.
inline constexpr int kKinds = 15;
inline constexpr int kSword = 0, kAxe = 1, kMace = 2, kScepter = 3, kWand = 4, kStaff = 5, kPolearm = 6, kSpear = 7, kDagger = 8, kClaw = 9, kOrb = 10, kUnarmed = 11, kOther = 12, kJavelin = 13, kThrowing = 14;
inline int KindIndex(uint32_t type) {   // D2RVR_WeaponType; -1 for what the touch does not swing as a blade (a bow, a crossbow, nothing)
    switch (type) {
        case D2RVR_TYPE_SWORD: return kSword;
        case D2RVR_TYPE_AXE: return kAxe;
        case D2RVR_TYPE_MACE: return kMace;
        case D2RVR_TYPE_SCEPTER: return kScepter;
        case D2RVR_TYPE_WAND: return kWand;
        case D2RVR_TYPE_STAFF: return kStaff;
        case D2RVR_TYPE_POLEARM: return kPolearm;
        case D2RVR_TYPE_SPEAR: return kSpear;
        case D2RVR_TYPE_DAGGER: return kDagger;
        case D2RVR_TYPE_CLAW: return kClaw;
        case D2RVR_TYPE_ORB: return kOrb;
        case D2RVR_TYPE_UNARMED: return kUnarmed;
        case D2RVR_TYPE_OTHER: return kOther;
        case D2RVR_TYPE_JAVELIN: return kJavelin;
        case D2RVR_TYPE_THROWING: return kThrowing;
        default: return -1;
    }
}
inline const char* KindName(int i) {
    static const char* const n[kKinds] = {"sword", "axe", "mace", "scepter", "wand", "staff", "polearm", "spear", "dagger", "claw", "orb", "unarmed", "other", "javelin", "throwing"};
    return i >= 0 && i < kKinds ? n[i] : "?";
}
inline const wchar_t* KindNameW(int i) {
    static const wchar_t* const n[kKinds] = {L"sword", L"axe", L"mace", L"scepter", L"wand", L"staff", L"polearm", L"spear", L"dagger", L"claw", L"orb", L"unarmed", L"other", L"javelin", L"throwing"};
    return i >= 0 && i < kKinds ? n[i] : L"?";
}
// What the player sees in the lists (Settings): maces and hammers, clubs and flails are all the game's Mace.
inline const wchar_t* KindLabel(int i) {
    static const wchar_t* const n[kKinds] = {L"Swords", L"Axes", L"Maces, hammers, clubs", L"Scepters", L"Wands", L"Staffs", L"Polearms", L"Spears", L"Daggers", L"Claws", L"Orbs", L"Bare fists", L"Other",
                                             L"Javelins", L"Throwing weapons"};
    return i >= 0 && i < kKinds ? n[i] : L"?";
}

// The family a kind belongs to for the amplitude (and its default factor).
enum class Family { Piercing, Edged, Blunt, Staff };
inline Family FamilyOf(int kind) {
    switch (kind) {
        case kSword: case kAxe: case kPolearm: return Family::Edged;
        case kMace: case kScepter: return Family::Blunt;
        case kStaff: return Family::Staff;
        default: return Family::Piercing;   // wands, spears, daggers, claws, orbs, bare fists, javelins, throwing weapons, the rest
    }
}

inline constexpr uint32_t Bit(int kind) { return 1u << kind; }
inline constexpr uint32_t kAllKinds = (1u << kKinds) - 1u;
inline constexpr uint32_t kDefaultHeadKinds = (1u << kMace) | (1u << kScepter);   // the blunt ones: only the head strikes
inline constexpr float kDefaultTowardDeg = 60.0f, kDefaultHeadPct = 40.0f;
inline constexpr float kDefaultPierceCm = 8.0f, kDefaultEdgedX = 1.4f, kDefaultBluntX = 2.0f, kDefaultStaffX = 1.75f;

struct Rule {
    bool toward = true;                          // [gameplay] melee_toward
    uint32_t towardKinds = kAllKinds;            // melee_toward_<kind>
    float towardDeg = kDefaultTowardDeg;         // melee_toward_deg: 20 .. 89 (the angle at which a movement is still "toward")
    bool head = true;                            // melee_head_only
    uint32_t headKinds = kDefaultHeadKinds;      // melee_head_only_<kind>
    float headPct = kDefaultHeadPct;             // melee_head_pct: 10 .. 100 (the outer part of the blade that is the head)
    bool amplitude = true;                       // melee_amplitude
    float pierceCm = kDefaultPierceCm;           // melee_amp_pierce_cm: 1 .. 60 (the piercing kinds' least swing amplitude)
    float edgedX = kDefaultEdgedX, bluntX = kDefaultBluntX, staffX = kDefaultStaffX;   // melee_amp_edged_x, _blunt_x, _staff_x: 0.5 .. 6 (times the piercing)
    std::array<float, kKinds> kindX{};           // melee_amp_x_<kind>: 0 = the family's, else 0.25 .. 8 times the piercing
};
inline Rule Clamp(Rule r) {
    const auto fin = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    r.towardDeg = fin(r.towardDeg, 20.0f, 89.0f, kDefaultTowardDeg);
    r.headPct = fin(r.headPct, 10.0f, 100.0f, kDefaultHeadPct);
    r.pierceCm = fin(r.pierceCm, 1.0f, 60.0f, kDefaultPierceCm);
    r.edgedX = fin(r.edgedX, 0.5f, 6.0f, kDefaultEdgedX);
    r.bluntX = fin(r.bluntX, 0.5f, 6.0f, kDefaultBluntX);
    r.staffX = fin(r.staffX, 0.5f, 6.0f, kDefaultStaffX);
    for (float& k : r.kindX) k = std::isfinite(k) && k >= 0.25f ? std::min(k, 8.0f) : 0.0f;
    r.towardKinds &= kAllKinds;
    r.headKinds &= kAllKinds;
    return r;
}
// [flail] chain: the rules for a flail's ball (bladeshape::BallShape) - the whole ball is the head, so the head-only rule has nothing to refuse;
// toward the monster and the amplitude stay, judged by the ball's own motion.
inline Rule ForBall(Rule r) { r.head = false; return r; }
inline bool TowardApplies(const Rule& r, int kind) { return r.toward && kind >= 0 && kind < kKinds && (r.towardKinds & Bit(kind)) != 0; }
inline bool HeadApplies(const Rule& r, int kind) { return r.head && kind >= 0 && kind < kKinds && (r.headKinds & Bit(kind)) != 0; }

// The factor of a kind over the piercing amplitude: its own when set, else its family's.
inline float AmplitudeX(const Rule& r, int kind) {
    if (kind < 0 || kind >= kKinds) return 1.0f;
    if (r.kindX[static_cast<size_t>(kind)] >= 0.25f) return r.kindX[static_cast<size_t>(kind)];
    switch (FamilyOf(kind)) {
        case Family::Edged: return r.edgedX;
        case Family::Blunt: return r.bluntX;
        case Family::Staff: return r.staffX;
        default: return 1.0f;
    }
}
// The least swing amplitude of a kind in cm (what Settings shows beside the factor); 0 with the amplitude rule off.
inline float AmplitudeCm(const Rule& r, int kind) { return r.amplitude && kind >= 0 && kind < kKinds ? r.pierceCm * AmplitudeX(r, kind) : 0.0f; }

// ---- the stroke's travel: how far the head has gone in this stroke ----
// Fed once per pose with the head's place (the blade's tip, in the frame the engine measures speed in) and the engine's stroke number (stroke::Engine::StrokeId, 0 = no stroke on). A new stroke starts
// with the path of the last `onsetS` seconds (the movement that began it - the engine's onset window), then adds every step; no stroke on = nothing.
class StrokeTravel {
public:
    void Reset() { *this = StrokeTravel{}; }
    void Feed(double timeS, stroke::Vec head, uint64_t strokeId, double onsetS = 0.12) {
        if (!stroke::Finite(head)) { Reset(); return; }
        if (n_ && timeS < ring_[(next_ + kRing - 1) % kRing].t) Reset();   // time went back
        if (n_) {
            const Point& last = ring_[(next_ + kRing - 1) % kRing];
            const float step = stroke::Length(head - last.p);
            cumulative_ += step;
        }
        ring_[next_] = {timeS, head, cumulative_};
        next_ = (next_ + 1) % kRing;
        n_ = std::min(n_ + 1, kRing);
        if (!strokeId) {   // no stroke: nothing travelled, and the running total starts again from nothing (it must not grow for ever)
            stroke_ = 0;
            travel_ = 0.0f;
            for (Point& p : ring_) p.c -= cumulative_;
            cumulative_ = 0.0f;
            base_ = 0.0f;
            return;
        }
        if (strokeId != stroke_) {   // a stroke began: the path of its onset window counts
            stroke_ = strokeId;
            float from = cumulative_;
            for (size_t k = 1; k < n_; ++k) {   // oldest point still inside the window
                const Point& p = ring_[(next_ + kRing - 1 - k) % kRing];
                if (timeS - p.t > onsetS) break;
                from = p.c;
            }
            travel_ = cumulative_ - from;
            base_ = cumulative_ - travel_;
        } else {
            travel_ = cumulative_ - base_;
        }
    }
    float Meters() const { return travel_; }
    uint64_t Stroke() const { return stroke_; }

private:
    static constexpr size_t kRing = 24;
    struct Point { double t = 0.0; stroke::Vec p; float c = 0.0f; };
    std::array<Point, kRing> ring_{};
    size_t next_ = 0, n_ = 0;
    float cumulative_ = 0.0f, travel_ = 0.0f, base_ = 0.0f;
    uint64_t stroke_ = 0;
};

// ---- the direction ----

// The way the point of the blade at `along` (0 the grip end, 1 the tip) moves over the sweep, unit; false when it moved under 5 mm (no way to speak of). *at: where that point is at the moment
// of the touch (`t`: 0 at the sweep's first blade, 1 at its last).
inline bool WayOf(const stroke::Sweep& sw, float along, float t, stroke::Vec* dir, stroke::Vec* at = nullptr) {
    const float k = std::clamp(along, 0.0f, 1.0f);
    const stroke::Vec p0 = sw.before.base + (sw.before.tip - sw.before.base) * k, p1 = sw.after.base + (sw.after.tip - sw.after.base) * k;
    const stroke::Vec d = p1 - p0;
    const float len = stroke::Length(d);
    if (!stroke::Finite(d) || !(len > 0.005f)) return false;
    *dir = d * (1.0f / len);
    if (at) *at = p0 + d * std::clamp(t, 0.0f, 1.0f);
    return true;
}

// The nearest point of a monster capsule's axis (the segment bottom-top) to `p`.
inline stroke::Vec NearestOnAxis(const stroke::Body& b, stroke::Vec p) {
    float s = 0.0f;
    stroke::SegmentDistanceSquared(b.bottom, b.top, p, p, &s);   // (the segment bottom-top against a point: s along bottom-top)
    return b.bottom + (b.top - b.bottom) * s;
}

// The cosine between a movement and the direction from its point to the monster's axis: +1 straight at it, 0 across, -1 away; false when the point is on the axis (no direction to it).
inline bool TowardCos(stroke::Vec at, stroke::Vec dir, const stroke::Body& body, float* cosine, stroke::Vec* onAxis = nullptr) {
    const stroke::Vec axis = NearestOnAxis(body, at), w = axis - at;
    const float wl = stroke::Length(w);
    if (onAxis) *onAxis = axis;
    if (!(wl > 1e-4f)) return false;
    *cosine = stroke::Dot(dir, w * (1.0f / wl));
    return true;
}
inline float DegOf(float cosine) { return std::acos(std::clamp(cosine, -1.0f, 1.0f)) * 57.2957795f; }

// ---- the verdict on a contact ----

enum class Why { Counts, NotHead, Away, TooSmall };
inline const char* WhyText(Why w) {
    return w == Why::NotHead ? "not the head (the shaft or the handle touched)" : w == Why::Away ? "the point moves away from the monster, or across it" : w == Why::TooSmall ? "a swing too small for this weapon" : "a strike";
}

struct Judgement {
    Why why = Why::Counts;
    bool haveWay = false;        // the blade moved far enough to have a direction
    float angleDeg = 0.0f;       // between the point's velocity and the direction to the monster's axis (0 straight at it, 90 across, 180 away); 0 when it has none
    float cosine = 0.0f;
    float along = 0.0f;          // where on the blade it touched (0 the grip end, 1 the tip)
    float travelCm = 0.0f, needCm = 0.0f;   // the swing's amplitude so far, and what the kind needs
    stroke::Vec at, dir, onAxis; // with haveWay: the touching point at the moment of the touch, its way (unit), the nearest point of the monster's axis (for the drawing)
    bool haveAxis = false;
};

// Does this contact count, for a weapon of `kind` under `rule`? travelM: the head's travel in this stroke (StrokeTravel). Kinds and rules switched off count everything, as they always did.
inline Judgement Judge(const Rule& rule, int kind, const stroke::Sweep& sw, const stroke::Contact& contact, const stroke::Body& body, float travelM) {
    Judgement j;
    j.along = contact.along;
    j.travelCm = travelM * 100.0f;
    j.needCm = AmplitudeCm(rule, kind);
    stroke::Vec dir, at;
    j.haveWay = kind >= 0 && WayOf(sw, contact.along, contact.t, &dir, &at);
    if (j.haveWay) {
        j.at = at;
        j.dir = dir;
        float c = 0.0f;
        j.haveAxis = TowardCos(at, dir, body, &c, &j.onAxis);
        if (j.haveAxis) { j.cosine = c; j.angleDeg = DegOf(c); }
    }
    if (kind < 0) return j;
    // the head: the outer part of the blade (the contact's place along it)
    if (HeadApplies(rule, kind) && contact.along < 1.0f - rule.headPct * 0.01f) { j.why = Why::NotHead; return j; }
    // toward the monster
    if (TowardApplies(rule, kind) && j.haveWay && j.haveAxis && j.cosine < std::cos(rule.towardDeg * 0.0174532925f)) { j.why = Why::Away; return j; }
    // the amplitude
    if (rule.amplitude && j.needCm > 0.0f && j.travelCm < j.needCm) { j.why = Why::TooSmall; return j; }
    return j;
}

// ---- the debug's look of a blade as it moves ----
// What the drawing shows of a blade each pose: Cold - no contact would count (no stroke, too slow); Hot - one would; Away - the engine counts the movement but it is not toward the monster nearest the
// blade's tip; Small - it is toward it (or there is none) but the swing is not yet as big as the kind needs.
enum class Look : uint8_t { Cold, Hot, Away, Small };

// `counting`: the engine's step swept something. way: the tip's way over the last few poses (unit, or zero); `near`: the monster capsule nearest the tip (or null: nothing within reach).
inline Look LookOf(const Rule& rule, int kind, bool counting, stroke::Vec tip, stroke::Vec way, const stroke::Body* nearest, float travelM) {
    if (!counting) return Look::Cold;
    if (TowardApplies(rule, kind) && nearest && stroke::Length(way) > 0.5f) {
        float c = 0.0f;
        if (TowardCos(tip, way, *nearest, &c) && c < std::cos(rule.towardDeg * 0.0174532925f)) return Look::Away;
    }
    if (rule.amplitude && AmplitudeCm(rule, kind) > 0.0f && travelM * 100.0f < AmplitudeCm(rule, kind)) return Look::Small;
    return Look::Hot;
}

// The tip's way over the last few poses (about 40 ms): a short ring of tips.
class TipWay {
public:
    void Reset() { n_ = 0; }
    void Feed(double timeS, stroke::Vec tip) {
        if (!stroke::Finite(tip)) { Reset(); return; }
        if (n_ && timeS < t_[(next_ + kRing - 1) % kRing]) Reset();
        t_[next_] = timeS;
        p_[next_] = tip;
        next_ = (next_ + 1) % kRing;
        n_ = std::min(n_ + 1, kRing);
    }
    // The unit way from the oldest tip within `windowS` to the newest; zero when it has moved under 1 cm.
    stroke::Vec Way(double windowS = 0.04) const {
        if (n_ < 2) return {};
        const size_t newest = (next_ + kRing - 1) % kRing;
        size_t from = newest;
        for (size_t k = 1; k < n_; ++k) {
            const size_t i = (next_ + kRing - 1 - k) % kRing;
            if (t_[newest] - t_[i] > windowS) break;
            from = i;
        }
        const stroke::Vec d = p_[newest] - p_[from];
        return stroke::Length(d) > 0.01f ? stroke::Normalized(d) : stroke::Vec{};
    }

private:
    static constexpr size_t kRing = 8;
    std::array<double, kRing> t_{};
    std::array<stroke::Vec, kRing> p_{};
    size_t next_ = 0, n_ = 0;
};

// ---- the engine's two numbers for one kind ----

struct KindTuning {
    float minSpeed = 0.0f;   // m/s: 0.3 .. 8; below that = the general number
    float startCm = 0.0f;    // cm: 2 .. 30; below that = the general number
};
inline stroke::StrokeTuning Effective(stroke::StrokeTuning general, KindTuning k) {
    if (std::isfinite(k.minSpeed) && k.minSpeed >= 0.3f) general.minSpeed = k.minSpeed;
    if (std::isfinite(k.startCm) && k.startCm >= 2.0f) general.startCm = k.startCm;
    return stroke::Clamp(general);
}

}  // namespace strikedir
