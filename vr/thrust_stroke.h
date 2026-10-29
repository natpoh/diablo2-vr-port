// thrust_stroke.h - hit by touch's THRUST (a poke): a movement of the blade along its OWN axis, point first, judged with numbers of its own - a least speed and a
// least distance - apart from the swing's (melee_min_speed, melee_stroke_start_cm, the swing amplitude), so a short quick stab of a spear or a dagger counts while a
// swing still needs its usual size. Pure maths, no game calls, no globals (vr/melee_touch.cpp runs it; cleanroom/tests/thrust_stroke_test.cpp tests it).
//
// How it runs, per hand (vr/melee_touch.cpp):
//   a second swing engine (stroke_contact.h) on the SAME samples, with the thrust's numbers (ParamsFor): it may begin a stroke at the thrust's least speed, after a short
//   onset - a poke at ~1 m/s (the author's log: pokes about 1 m/s, swings 2-6) is a stroke for it, where the swing's engine at 6 m/s / 15 cm sees nothing;
//   Path, fed every pose with the blade's two ends in the room (stroke::Sample::room): how far the tip has gone FORWARD along the blade's axis in that stroke, against its
//   whole path - a thrust is a stroke whose path is mostly along the axis, point first (alongPct), and whose tip still goes forward at the contact;
//   Judge: a contact of the thrust engine's sweep counts as a thrust when the path is a thrust, the tip has gone at least the thrust's distance and (when asked,
//   melee_thrust_min_accel) the point's peak acceleration along the blade reached the least - a jab, not a slow push; the strike rules of
//   vr/strike_direction.h (toward the monster, the head) judge it as they judge a swing, the swing's amplitude does not (the thrust's distance stands for it).
// What is "piercing": the weapon's own attack in the game's data - Weapons.txt wclass, the animation class the game swings it with: 1ht (one-hand thrust: daggers, javelins,
// throwing knives) and 2ht (two-hand thrust: spears, tridents, pikes) - read from the item's row (FindClassField finds the field); where the field cannot be read, the kind:
// spear, dagger, javelin. Every kind may be set to thrust always (a sword's tip, a polearm) or never (melee_thrust_<kind>).
// Only whether a contact counts: the game's own attack is pressed as for a swing - its animation, reach, rolls and damage are the game's, nothing is scaled.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "stroke_contact.h"
#include "strike_direction.h"

namespace thrust {

inline constexpr float kDefaultMinSpeed = 0.8f;   // m/s ([gameplay] melee_thrust_min_speed, 0.3 .. 20): a poke at ~1 m/s counts
inline constexpr float kDefaultCm = 5.0f;         // cm ([gameplay] melee_thrust_cm, 1 .. 30): the tip's least travel forward along the blade
inline constexpr float kDefaultAlongPct = 70.0f;  // % ([gameplay] melee_thrust_along_pct, 50 .. 95): the part of the tip's path that must be along the blade, point first
inline constexpr float kMinSpeedFloor = 0.3f, kMinSpeedCeil = 20.0f, kCmFloor = 1.0f, kCmCeil = 30.0f, kAlongFloor = 50.0f, kAlongCeil = 95.0f;
// The least ACCELERATION of the point along the blade, m/s^2 ([gameplay] melee_thrust_min_accel, 0 .. 300): 0 = not asked (the default). It is the PEAK over the thrust - the
// burst that drove the point forward, from up to kAccelBackS before the thrust began - not the value at the touch: a jab reaches its top speed as the arm extends and meets the
// monster coasting or already slowing, so at the touch a real jab and a slow push both read about nothing; the burst that started it is what tells them apart (a jab tens of
// m/s^2, a slow push a few). Measured over kAccelSpanS (25 ms) of the point's speed along the blade - the tracking's frame-to-frame noise is not an acceleration.
inline constexpr float kDefaultMinAccel = 0.0f, kAccelCeil = 300.0f;
inline constexpr double kAccelSpanS = 0.025, kAccelBackS = 0.25;
// At the contact the tip must still go forward: its step this pose within 60 deg of the blade's axis (a pull-back, or the flick at the end of a poke, is not a thrust).
inline constexpr float kStepCos = 0.5f;

// A kind's mode: kByGame = by the game's weapon class of the item (a thrust class thrusts, any other does not; the kind where the class cannot be read), kAlways, kNever.
// The ini says it with a tick per kind, melee_thrust_on_<kind> (ModeFromIni): ticked = the kind thrusts, unticked = it does not; an ini that has no tick yet keeps the older
// number melee_thrust_<kind> (-1 never, 0 by the game, 1 always), and one with neither is by the game.
inline constexpr int kNever = -1, kByGame = 0, kAlways = 1;

struct Rule {
    bool on = true;                                          // [gameplay] melee_thrust
    float minSpeed = kDefaultMinSpeed;                       // melee_thrust_min_speed
    float cm = kDefaultCm;                                   // melee_thrust_cm
    float alongPct = kDefaultAlongPct;                       // melee_thrust_along_pct
    float minAccel = kDefaultMinAccel;                       // melee_thrust_min_accel: 0 = not asked
    std::array<int8_t, strikedir::kKinds> mode{};            // melee_thrust_<kind>: kNever / kByGame / kAlways
    std::array<float, strikedir::kKinds> kindSpeed{};        // melee_thrust_min_speed_<kind>: 0 (or under 0.3) = the general one
    std::array<float, strikedir::kKinds> kindCm{};           // melee_thrust_cm_<kind>: 0 (or under 1) = the general one
    std::array<float, strikedir::kKinds> kindAccel{};        // melee_thrust_min_accel_<kind>: 0 = the general one
    // [gameplay] melee_thrust_with_run: the thrust is measured in the WORLD, not the room - the hero running (or walking) with the weapon held forward is the
    // thrust's movement and acceleration too, a charge (the author, 2026-10-22: "if I run with the spear that must count as acceleration and movement").
    // Off: the room, as ever (the hero's walking adds nothing). The swing is always the room's.
    bool withRun = false;
};

inline Rule Clamp(Rule r) {
    const auto fin = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    r.minSpeed = fin(r.minSpeed, kMinSpeedFloor, kMinSpeedCeil, kDefaultMinSpeed);
    r.cm = fin(r.cm, kCmFloor, kCmCeil, kDefaultCm);
    r.alongPct = fin(r.alongPct, kAlongFloor, kAlongCeil, kDefaultAlongPct);
    r.minAccel = fin(r.minAccel, 0.0f, kAccelCeil, kDefaultMinAccel);
    for (int8_t& m : r.mode) m = static_cast<int8_t>(std::clamp<int>(m, kNever, kAlways));
    for (float& s : r.kindSpeed) s = std::isfinite(s) && s >= kMinSpeedFloor ? std::min(s, kMinSpeedCeil) : 0.0f;
    for (float& c : r.kindCm) c = std::isfinite(c) && c >= kCmFloor ? std::min(c, kCmCeil) : 0.0f;
    for (float& a : r.kindAccel) a = std::isfinite(a) && a > 0.0f ? std::min(a, kAccelCeil) : 0.0f;
    return r;
}
// An ini number as a mode: -1 never, 1 always, anything else (0, missing, not a number) by the game's class.
inline int8_t ModeOf(float v) { return !std::isfinite(v) ? static_cast<int8_t>(kByGame) : v <= -0.5f ? static_cast<int8_t>(kNever) : v >= 0.5f ? static_cast<int8_t>(kAlways) : static_cast<int8_t>(kByGame); }
// tick: melee_thrust_on_<kind> as read, NAN when the ini has no such line; older: melee_thrust_<kind> (NAN when none). The tick wins; without it the older number; without both, by the game.
inline int8_t ModeFromIni(float tick, float older) {
    if (std::isfinite(tick)) return tick >= 0.5f ? static_cast<int8_t>(kAlways) : static_cast<int8_t>(kNever);
    return ModeOf(older);
}

// ---- what is piercing: the game's weapon class ----

// Do the four bytes of an item row's class field spell `cls` (three characters, any case), padded with a zero or a space?
inline bool Spells(const uint8_t* p, const char* cls) {
    if (!p || !cls) return false;
    for (int i = 0; i < 3; ++i) {
        const uint8_t a = p[i], b = static_cast<uint8_t>(cls[i]);
        const auto lower = [](uint8_t c) { return c >= 'A' && c <= 'Z' ? static_cast<uint8_t>(c + 32) : c; };
        if (!b || lower(a) != lower(b)) return false;
    }
    return p[3] == 0 || p[3] == ' ';
}
// The game's thrust classes: 1ht (one-hand thrust) and 2ht (two-hand thrust).
inline bool ThrustClass(const uint8_t* p) { return Spells(p, "1ht") || Spells(p, "2ht"); }

// Where the item rows keep the class: the offsets at which EVERY probe's row spells the class it is known to have (Weapons.txt wclass of a few base items the game's
// own tables fix - a dagger 1ht, a spear 2ht, a short sword 1hs, a short staff stf, a short bow bow). Up to `max` offsets, lowest first; the field is the first (the
// two-handed class, which follows it, reads the same for these). 0 found: the field cannot be read - the kind decides.
struct ClassProbe {
    const uint8_t* row = nullptr;
    uint32_t size = 0;
    const char* cls = nullptr;
};
inline size_t FindClassField(const ClassProbe* probes, size_t n, uint32_t* out, size_t max) {
    if (!probes || n == 0) return 0;
    uint32_t size = UINT32_MAX;
    for (size_t i = 0; i < n; ++i) {
        if (!probes[i].row || !probes[i].cls) return 0;
        size = std::min(size, probes[i].size);
    }
    size_t found = 0;
    for (uint32_t o = 0; o + 4 <= size && size != UINT32_MAX; ++o) {
        bool all = true;
        for (size_t i = 0; i < n && all; ++i) all = Spells(probes[i].row + o, probes[i].cls);
        if (!all) continue;
        if (out && found < max) out[found] = o;
        ++found;
    }
    return found;
}

// Where the class cannot be read: the kinds the game thrusts with - spears (2ht), daggers and javelins (1ht). A throwing knife (1ht) and a throwing axe (1hs) share a kind:
// without the class neither is taken.
inline bool KindThrusts(int kind) { return kind == strikedir::kSpear || kind == strikedir::kDagger || kind == strikedir::kJavelin; }
// The tick a kind starts with (D2R VR Authentic Settings, the shipped ini): the kinds the game's data thrusts with - spears (2ht), daggers, javelins and throwing weapons (1ht: the
// throwing knives; ticked, a throwing axe of the same kind pokes too) - and swords (the author, 2026-10-24: "a sword must thrust - it is a sword"; the game swings them, but a
// sword's point pokes, and a run in with it held forward is a thrust).
inline bool DefaultTick(int kind) { return KindThrusts(kind) || kind == strikedir::kThrowing || kind == strikedir::kSword; }

// Does a thrust count for this weapon? itemClass: 1 = its class in the game's data is a thrust class, 0 = another class, -1 = not read.
inline bool Applies(const Rule& r, int kind, int itemClass) {
    if (!r.on || kind < 0 || kind >= strikedir::kKinds) return false;
    const int m = r.mode[static_cast<size_t>(kind)];
    if (m == kAlways) return true;
    if (m == kNever) return false;
    return itemClass >= 0 ? itemClass == 1 : KindThrusts(kind);
}

// ---- the numbers of a kind ----

struct Numbers {
    float minSpeed = kDefaultMinSpeed;   // m/s
    float cm = kDefaultCm;               // cm
    float minAccel = kDefaultMinAccel;   // m/s^2, 0 = not asked
};
inline Numbers NumbersFor(const Rule& r, int kind) {
    Numbers n{r.minSpeed, r.cm, r.minAccel};
    if (kind >= 0 && kind < strikedir::kKinds) {
        const float s = r.kindSpeed[static_cast<size_t>(kind)], c = r.kindCm[static_cast<size_t>(kind)], a = r.kindAccel[static_cast<size_t>(kind)];
        if (std::isfinite(s) && s >= kMinSpeedFloor) n.minSpeed = std::min(s, kMinSpeedCeil);
        if (std::isfinite(c) && c >= kCmFloor) n.cm = std::min(c, kCmCeil);
        if (std::isfinite(a) && a > 0.0f) n.minAccel = std::min(a, kAccelCeil);
    }
    n.minAccel = std::isfinite(n.minAccel) ? std::clamp(n.minAccel, 0.0f, kAccelCeil) : kDefaultMinAccel;
    n.minSpeed = std::isfinite(n.minSpeed) ? std::clamp(n.minSpeed, kMinSpeedFloor, kMinSpeedCeil) : kDefaultMinSpeed;
    n.cm = std::isfinite(n.cm) ? std::clamp(n.cm, kCmFloor, kCmCeil) : kDefaultCm;
    return n;
}

// The thrust engine's gates: a contact counts from the thrust's least speed, and a stroke may BEGIN at it too (the swing's engine asks 1.5 x, at most 1.2 - a 1 m/s poke
// would never begin); the rest is the same share of it as the swing's (at most the swing's). The onset is half the thrust's distance, 2 .. 4 cm, within the engine's 0.12 s:
// it only begins the stroke - the distance itself is Path's (a long thrust begins early and counts once its tip has gone the whole way; a slow push of 0.4 m/s covers 4.8 cm
// in 0.12 s, so a whole 5 cm onset would never begin it, and a poke shorter than the distance would not even be seen to be refused).
inline stroke::Params ParamsFor(Numbers n, stroke::Params base = stroke::Params{}) {
    const float speed = std::isfinite(n.minSpeed) ? std::clamp(n.minSpeed, kMinSpeedFloor, kMinSpeedCeil) : kDefaultMinSpeed;
    const float cm = std::isfinite(n.cm) ? std::clamp(n.cm, kCmFloor, kCmCeil) : kDefaultCm;
    base.strokeSpeed = speed;
    base.beginSpeed = speed;
    base.quietSpeed = std::min(base.quietSpeed, 0.5625f * speed);
    base.onsetExcursion = std::clamp(0.5f * cm, 2.0f, 4.0f) * 0.01f;
    return base;
}

// ---- the whole weapon, not its tip ----
// A thrust is the WHOLE weapon going along its own axis without turning much (the author, 2026-10-22: "measure not the tip but the angle of the whole weapon and its
// deviation"). The tip was measured before, and on a spear it sits 2 m out from the hand: the small turn every real poke has swept it sideways more than forward
// (his eleven pokes read 51 - 76 % straight, so a 70 % threshold refused them) and kept its speed over the rest threshold, so a jab hardly ever ended. Now the
// thrust engine and Path follow the GRIP - the hand's place on the weapon's axis - which a turn of the hand barely moves, and a separate number says how far the
// weapon's axis turned during the stroke: a poke turns little, a swing a lot.
inline constexpr float kGripAheadM = 0.5f;    // the thrust engine's lever: its "tip" is this far ahead of the grip (or the real tip, when nearer)
inline constexpr float kMaxTurnDeg = 35.0f;   // a weapon whose axis turned more than this within kTurnWindowS is swinging, not thrusting
// The turn is measured over the last quarter second: a poke is shorter, so this is its whole turn; a run with the weapon ([gameplay] melee_thrust_with_run) is
// one long stroke, and the hero steering toward a monster over seconds must not add up to a "turned" refusal at the touch.
inline constexpr double kTurnWindowS = 0.25;

// Where the hand holds the weapon: the hand projected onto the blade's axis, as the part of the blade from its base (0 the base, 1 the tip). -1: no reading.
inline float GripAt(const stroke::Blade& b, stroke::Vec hand) {
    const stroke::Vec span = b.tip - b.base;
    const float len = stroke::Length(span);
    if (!stroke::Finite(b.base) || !stroke::Finite(b.tip) || !stroke::Finite(hand) || len < 0.05f) return -1.0f;
    return std::clamp(stroke::Dot(hand - b.base, span * (1.0f / len)) / len, 0.0f, 1.0f);
}
// The blade the thrust engine steps on: from the grip (`at` of the blade, GripAt) to kGripAheadM toward the tip. The same `at` serves the blade in another frame
// (the world's, [gameplay] melee_thrust_with_run): a rigid blade has its grip at the same part of its length in every frame. at < 0 keeps the blade as drawn.
inline stroke::Blade GripBladeAt(const stroke::Blade& b, float at) {
    const stroke::Vec span = b.tip - b.base;
    const float len = stroke::Length(span);
    if (at < 0.0f || !stroke::Finite(b.base) || !stroke::Finite(b.tip) || len < 0.05f) return b;
    const stroke::Vec axis = span * (1.0f / len);
    const float from = std::clamp(at, 0.0f, 1.0f) * len;
    stroke::Blade out = b;
    out.base = b.base + axis * from;
    out.tip = out.base + axis * std::min(kGripAheadM, len - from);
    if (len - from < 0.05f) out.tip = b.tip;   // held at the very tip: the blade as drawn
    return out;
}
// The grip blade in the frame the hand is in (the room). A blade with no length, or a hand that does not read, keeps the blade as drawn.
inline stroke::Blade GripBlade(const stroke::Blade& room, stroke::Vec hand) { return GripBladeAt(room, GripAt(room, hand)); }

// ---- the path of a thrust ----
// Fed once per pose with the GRIP (GripBlade's base) and the weapon's tip in the room (the frame the engine measures speed in: the hero walking adds nothing) and the
// THRUST engine's stroke number (0 = none). Like strikedir::StrokeTravel a new stroke starts with the path of its onset window. Along: the grip's steps forward along
// the weapon's axis (grip -> tip), summed (a step back adds nothing); Path: the grip's whole path. The share of the two - how much of the weapon's own movement was
// along its axis - says whether the stroke is a thrust; StepCos is the latest step's own. TurnDeg: how far the weapon's axis turned from where the stroke began (the
// deviation). PeakAccel: the highest forward acceleration of the grip along the weapon (its along-the-axis speed's change over kAccelSpanS) in this stroke, counted
// from kAccelBackS before it began (the burst that drove the point is mostly before the engine calls it a stroke).
class Path {
public:
    void Reset() { *this = Path{}; }
    void Feed(double timeS, stroke::Vec grip, stroke::Vec tip, uint64_t strokeId, double onsetS = 0.12) {
        if (!stroke::Finite(grip) || !stroke::Finite(tip) || !std::isfinite(timeS)) { Reset(); return; }
        if (n_ && timeS < ring_[(next_ + kRing - 1) % kRing].t) Reset();   // time went back
        float vel = 0.0f, acc = 0.0f;
        const stroke::Vec axis = stroke::Normalized(tip - grip);
        if (n_) {
            const Point& last = ring_[(next_ + kRing - 1) % kRing];
            const stroke::Vec step = grip - last.tip;
            const float len = stroke::Length(step);
            const float along = stroke::Dot(step, axis);
            cumAlong_ += std::max(0.0f, along);
            cumPath_ += len;
            if (len > 0.001f) stepCos_ = along / len;   // (a step under a millimetre keeps the last way)
            const double dt = timeS - last.t;
            vel = dt > 1e-4 ? along / static_cast<float>(dt) : last.vel;   // the point's speed along the blade, forward +
            for (size_t k = 1; k < n_; ++k) {   // the newest point at least kAccelSpanS older: the speed's change since
                const Point& p = ring_[(next_ + kRing - k) % kRing];
                if (timeS - p.t < kAccelSpanS) continue;
                if (timeS - p.t < 4.0 * kAccelSpanS) acc = (vel - p.vel) / static_cast<float>(timeS - p.t);   // (a gap in the samples: none)
                break;
            }
        }
        ring_[next_] = {timeS, grip, cumAlong_, cumPath_, vel, acc, axis};
        next_ = (next_ + 1) % kRing;
        n_ = std::min(n_ + 1, kRing);
        if (!strokeId) {   // no stroke: nothing gone, and the totals start again from nothing
            stroke_ = 0;
            along_ = path_ = 0.0f;
            for (Point& p : ring_) { p.along -= cumAlong_; p.path -= cumPath_; }
            cumAlong_ = cumPath_ = 0.0f;
            baseAlong_ = basePath_ = 0.0f;
            peakAccel_ = 0.0f;
            turnDeg_ = 0.0f;
            return;
        }
        if (strokeId != stroke_) {   // a stroke began: the path of its onset window counts
            stroke_ = strokeId;
            float fromAlong = cumAlong_, fromPath = cumPath_;
            for (size_t k = 1; k < n_; ++k) {   // the oldest point still inside the window
                const Point& p = ring_[(next_ + kRing - 1 - k) % kRing];
                if (timeS - p.t > onsetS) break;
                fromAlong = p.along;
                fromPath = p.path;
            }
            baseAlong_ = fromAlong;
            basePath_ = fromPath;
            peakAccel_ = 0.0f;
            for (size_t k = 1; k < n_; ++k) {   // the burst that began it: the points of the last kAccelBackS before this one
                const Point& p = ring_[(next_ + kRing - 1 - k) % kRing];
                if (timeS - p.t > kAccelBackS) break;
                peakAccel_ = std::max(peakAccel_, p.acc);
            }
        }
        peakAccel_ = std::max(peakAccel_, acc);
        turnDeg_ = 0.0f;   // the most the axis turned against any of its places in the last kTurnWindowS
        if (stroke::Length(axis) > 0.5f) {
            for (size_t k = 1; k < n_; ++k) {
                const Point& p = ring_[(next_ + kRing - 1 - k) % kRing];
                if (timeS - p.t > kTurnWindowS) break;
                if (stroke::Length(p.axis) > 0.5f)
                    turnDeg_ = std::max(turnDeg_, std::acos(std::clamp(stroke::Dot(axis, p.axis), -1.0f, 1.0f)) * 57.2957795f);
            }
        }
        along_ = cumAlong_ - baseAlong_;
        path_ = cumPath_ - basePath_;
    }
    float AlongM() const { return along_; }
    float PathM() const { return path_; }
    // The part of the stroke's path that went forward along the blade: 1 a pure thrust, ~0 a swing (the tip goes across the blade), 0 with no path.
    float Share() const { return path_ > 1e-4f ? std::clamp(along_ / path_, 0.0f, 1.0f) : 0.0f; }
    // The latest step's way against the blade's axis: +1 point first, 0 across, -1 drawn back.
    float StepCos() const { return stepCos_; }
    // The highest forward acceleration of the point along the blade in this stroke (with the burst before it began), m/s^2; 0 with no stroke.
    float PeakAccel() const { return stroke_ ? peakAccel_ : 0.0f; }
    // How far the weapon's axis turned within the last kTurnWindowS, degrees (now); 0 with no stroke.
    float TurnDeg() const { return stroke_ ? turnDeg_ : 0.0f; }
    uint64_t Stroke() const { return stroke_; }

private:
    static constexpr size_t kRing = 48;   // (0.4 s at 120 Hz: the acceleration looks kAccelBackS back)
    struct Point { double t = 0.0; stroke::Vec tip; float along = 0.0f, path = 0.0f, vel = 0.0f, acc = 0.0f; stroke::Vec axis; };   // tip: the grip
    std::array<Point, kRing> ring_{};
    size_t next_ = 0, n_ = 0;
    float cumAlong_ = 0.0f, cumPath_ = 0.0f, baseAlong_ = 0.0f, basePath_ = 0.0f, along_ = 0.0f, path_ = 0.0f, stepCos_ = 0.0f, peakAccel_ = 0.0f, turnDeg_ = 0.0f;
    uint64_t stroke_ = 0;
};

// ---- the verdict ----

enum class Why { Counts, NotAlong, Short, Weak, Turned };
inline const char* WhyText(Why w) {
    return w == Why::NotAlong ? "not along the blade (a swing, not a thrust)" : w == Why::Short ? "a thrust too short for this weapon"
         : w == Why::Weak ? "a push, not a jab (too little acceleration)" : w == Why::Turned ? "the weapon turned (a swing, not a thrust)" : "a thrust";
}
struct Verdict {
    Why why = Why::Counts;
    float share = 0.0f;               // Path::Share
    float stepCos = 0.0f;             // Path::StepCos
    float alongCm = 0.0f, needCm = 0.0f;
    float accel = 0.0f, needAccel = 0.0f;   // Path::PeakAccel and the kind's least (0 = not asked), m/s^2
    float turnDeg = 0.0f;                   // Path::TurnDeg, against kMaxTurnDeg
};
// Is the thrust engine's stroke a thrust, long enough and (when asked) sharp enough, for a weapon of `kind`? (The speed - the engine's own gate and the speed where it touched - and the strike rules
// are the caller's, as for a swing: StrikeRule.)
inline Verdict Judge(const Rule& r, int kind, const Path& path) {
    Verdict v;
    v.share = path.Share();
    v.stepCos = path.StepCos();
    v.alongCm = path.AlongM() * 100.0f;
    const Numbers n = NumbersFor(r, kind);
    v.needCm = n.cm;
    v.accel = path.PeakAccel();
    v.needAccel = n.minAccel;
    v.turnDeg = path.TurnDeg();
    const float share = std::clamp(r.alongPct, kAlongFloor, kAlongCeil) * 0.01f;
    if (v.share < share || v.stepCos < kStepCos) v.why = Why::NotAlong;
    else if (v.turnDeg > kMaxTurnDeg) v.why = Why::Turned;
    else if (v.alongCm < v.needCm) v.why = Why::Short;
    else if (v.needAccel > 0.0f && v.accel < v.needAccel) v.why = Why::Weak;
    return v;
}
// [weight] stab_in: is the hand's movement a thrust NOW - the thrust engine's stroke on, mostly along the blade point first, its latest step forward, the weapon not
// turned (Judge without the distance and the jab's sharpness: the point meets the body when it meets it). What the weight model is told (heft Hold::SetStabbing) for
// a weapon that thrusts (Applies) - it lets a point-first strike go into the body (cleanroom/heft/stab.h).
inline bool ThrustingNow(const Rule& r, const Path& path) {
    if (!r.on || !path.Stroke()) return false;
    const float share = std::clamp(r.alongPct, kAlongFloor, kAlongCeil) * 0.01f;
    return path.Share() >= share && path.StepCos() >= kStepCos && path.TurnDeg() <= kMaxTurnDeg;
}
// [weight] stab_in for every pointed blade (the author: «любой меч должен втыкаться»): whatever hit by touch counts as a hit, a weapon with a point - a sword (one- or
// two-handed), a dagger, a spear, a javelin, a throwing weapon, a polearm, a claw or a katar - may stick in when it is driven point first. Maces, scepters, wands, staffs,
// orbs, axes and bare fists never do.
inline bool PointedKind(int kind) {
    return kind == strikedir::kSword || kind == strikedir::kDagger || kind == strikedir::kSpear || kind == strikedir::kJavelin || kind == strikedir::kThrowing ||
           kind == strikedir::kPolearm || kind == strikedir::kClaw;
}
// ... and whether it is moving point first NOW: the stab's own judge, on any pointed blade, apart from hit by touch's thrust (which needs its engine's stroke and only
// runs for the kinds that thrust). Fed every pose with the grip and a point ahead of it along the weapon (GripBlade, the room); over the last kWindowS: the share of the
// grip's path that went forward along the weapon's axis (alongPct), its speed along the axis, the latest step forward, and how far the axis turned (kMaxTurnDeg).
class PointFirst {
public:
    static constexpr double kWindowS = 0.15;
    static constexpr float kMinSpeed = 0.3f;   // m/s along the axis
    void Reset() { *this = PointFirst{}; }
    void Feed(double timeS, stroke::Vec grip, stroke::Vec ahead) {
        if (!stroke::Finite(grip) || !stroke::Finite(ahead) || !std::isfinite(timeS) || (n_ && timeS < ring_[(next_ + kRing - 1) % kRing].t)) { Reset(); return; }
        ring_[next_] = {timeS, grip, stroke::Normalized(ahead - grip)};
        next_ = (next_ + 1) % kRing;
        n_ = std::min(n_ + 1, kRing);
    }
    // Is it moving point first now (alongPct: % of the path that must be along the blade - hit by touch's melee_thrust_along_pct)?
    bool Now(float alongPct, float* speedOut = nullptr) const {
        if (speedOut) *speedOut = 0.0f;
        if (n_ < 3) return false;
        const Point& last = ring_[(next_ + kRing - 1) % kRing];
        float along = 0.0f, path = 0.0f, turn = 0.0f, stepCos = 0.0f;
        double from = last.t;
        bool haveStep = false;
        for (size_t k = 1; k < n_; ++k) {
            const Point& b = ring_[(next_ + kRing - k) % kRing];
            const Point& a = ring_[(next_ + kRing - k - 1) % kRing];
            if (last.t - a.t > kWindowS) break;
            const stroke::Vec step = b.grip - a.grip;
            const float len = stroke::Length(step), al = stroke::Dot(step, b.axis);
            along += std::max(0.0f, al);
            path += len;
            if (!haveStep && len > 0.001f) { stepCos = al / len; haveStep = true; }
            if (stroke::Length(a.axis) > 0.5f && stroke::Length(last.axis) > 0.5f)
                turn = std::max(turn, std::acos(std::clamp(stroke::Dot(a.axis, last.axis), -1.0f, 1.0f)) * 57.2957795f);
            from = a.t;
        }
        const double span = last.t - from;
        if (!(span > 1e-3) || !(path > 1e-4f)) return false;
        const float speed = static_cast<float>(along / span);
        if (speedOut) *speedOut = speed;
        return along / path >= std::clamp(alongPct, kAlongFloor, kAlongCeil) * 0.01f && speed >= kMinSpeed && stepCos >= kStepCos && turn <= kMaxTurnDeg;
    }

private:
    static constexpr size_t kRing = 32;
    struct Point { double t = 0.0; stroke::Vec grip, axis; };
    std::array<Point, kRing> ring_{};
    size_t next_ = 0, n_ = 0;
};
// The strike rules a thrust's contact is judged by: toward the monster and the head as for a swing; not the swing's amplitude - the thrust's distance stands for it.
inline strikedir::Rule StrikeRule(strikedir::Rule r) { r.amplitude = false; return r; }

// The debug's look of a blade the thrust engine counts (vr/melee_boxes.h): Cold - not a thrust (the swing's look decides); Small - a thrust not yet as long as the kind
// needs; else the strike rules' look (toward the monster nearest the tip).
inline strikedir::Look LookOf(const Rule& r, const strikedir::Rule& strike, int kind, bool counting, const Path& path, stroke::Vec tip, stroke::Vec way, const stroke::Body* nearest) {
    if (!counting) return strikedir::Look::Cold;
    const Verdict v = Judge(r, kind, path);
    if (v.why == Why::NotAlong || v.why == Why::Turned) return strikedir::Look::Cold;
    const strikedir::Look l = strikedir::LookOf(StrikeRule(strike), kind, true, tip, way, nearest, 0.0f);
    return l == strikedir::Look::Hot && (v.why == Why::Short || v.why == Why::Weak) ? strikedir::Look::Small : l;
}

}  // namespace thrust
