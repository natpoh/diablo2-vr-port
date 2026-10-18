// shield_smite.h - [gameplay] shield_smite: a hard bash of the left shield through a monster casts one Smite at it.
// The pure part: no game calls, no globals. shield_smite.cpp is the game-facing half (monsters, the Smite slot, the
// skeleton's shield, the pad press, the game's target lookups); this file decides WHEN and AT WHOM, and is
// unit-tested on synthetic poses (cleanroom/tests/shield_smite_test.cpp).
//
// What it does, and what it leaves to the game: the hand picks the target and the moment, the game's own Smite does
// everything else. The cast is the game's own pad press on the slot Smite sits in (65 ms, as a finger would), the
// monster the shield touched is what the game's own target lookups hand to that cast (the same two lookups spell_aim
// already answers), so reach, mana, cast rate, animation, damage, stun and knockback are the game's. A resting
// shield does nothing, a stroke with the right trigger down does nothing, one bash casts one Smite.
//
// After bearstonem/d2r-pcvr (MIT): its ShieldContactTick (the capsule: the attachment's centre +-0.20 m along the
// shield axis, radius 0.15). The swing engine (stroke_contact.h) and the monster's body from its MonStats row
// (melee_bodies.h) are the melee-by-touch tab's, shared. pcvr's server-side Smite hook and Target redirect are not
// taken: the cast goes through the game's own click instead, see shield_smite.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "melee_bodies.h"
#include "skill_triggers.h"   // [debug] skill_triggers draws the bash: Why, Verdict (the pure part only)
#include "stroke_contact.h"
#include "stroke_tuning.h"

namespace shieldsmite {

constexpr int kSmite = 97;   // Skills.txt: Smite

// -- the shield as a capsule ------------------------------------------------------------------------------------
constexpr float kAxisHalf = 0.20f, kShieldRadius = 0.15f;
inline stroke::Blade ShieldBlade(stroke::Vec centre, stroke::Vec axis) {
    const stroke::Vec a = stroke::Normalized(axis);
    return {centre - a * kAxisHalf, centre + a * kAxisHalf, kShieldRadius};
}

// -- a monster as a capsule -------------------------------------------------------------------------------------
// meleetouch::BodyFor (melee_bodies.h) turns a MonStats row's bytes and the monster's place into a capsule in render
// metres. This is one of the monsters near the shield.
struct Monster {
    uint32_t id = 0;   // the unit's id, for one hit per stroke and to find it again
    stroke::Body body;
};

// -- the pad slot Smite sits in ---------------------------------------------------------------------------------
// The controller's ten skill slots as the game lays them out: B X Y RB, the same four with LT held, A, LT + A.
// Slot -> XInput bits (the Windows header's, so this needs none).
struct PadPress {
    uint16_t buttons = 0;
    uint8_t lt = 0;
};
constexpr int kSlotCount = 10;
constexpr uint16_t kPadB = 0x2000, kPadX = 0x4000, kPadY = 0x8000, kPadRB = 0x0200, kPadA = 0x1000;
constexpr PadPress SlotPress(int slot) {
    constexpr uint16_t buttons[4] = {kPadB, kPadX, kPadY, kPadRB};
    if (slot < 0 || slot >= kSlotCount) return {};
    if (slot >= 8) return {kPadA, uint8_t(slot == 9 ? 255 : 0)};
    return {buttons[slot % 4], uint8_t(slot >= 4 ? 255 : 0)};
}
// How long the slot's button is held. A slot of the left trigger's row (LT + B / X / Y / RB / A) is held 180 ms, the others 65:
// with LT the game's pad code first prepares the second bank (about 50 ms at the game's own poll, 25 a second) before the button
// counts, and a 65 ms pulse could end between two polls - the bash then cast nothing. bearstonem/d2r-pcvr (MIT) found it and
// measured it (steamvr/weapon_skills.h, commit b7190a2: 160 timings of XR frames against 25 Hz polls, 65 ms lost the press in all,
// 180 in none). The press is let go at once when the game's click has asked for the target (the cast is on its way).
constexpr uint32_t kPulseMs = 65, kAltBankPulseMs = 180;
constexpr uint32_t PulseMs(PadPress p) { return p.lt ? kAltBankPulseMs : kPulseMs; }
// The order the slots are looked at in when Smite sits in more than one: the plain face buttons first.
constexpr int kSlotOrder[kSlotCount] = {0, 1, 2, 3, 8, 4, 5, 6, 7, 9};
// skillOf(slot) is the skill the game has on the slot (-1 none); returns the first slot holding Smite, -1 none.
template <class F>
int SmiteSlot(F skillOf) {
    for (int slot : kSlotOrder)
        if (skillOf(slot) == kSmite) return slot;
    return -1;
}

// -- the gate and the detector ----------------------------------------------------------------------------------
struct Gate {
    bool enabled = false;      // [gameplay] shield_smite
    bool shield = false;       // a shield on the left arm
    bool smiteSlot = false;    // Smite sits on a pad slot the press can reach
    bool playing = false;      // in a game, no menu or panel, the headset has the focus
    float rightTrigger = 0;    // the weapon hand's trigger, 0..1
    bool f4 = true;            // the view is VR first person (viewgate::F4()): F4 only, shut in every other view
};
constexpr float kTriggerDown = 0.2f;
inline bool Open(const Gate& g) { return g.enabled && g.f4 && g.shield && g.smiteSlot && g.playing && g.rightTrigger < kTriggerDown; }

constexpr double kMinGap = 0.45;   // seconds between two requests: a bash is not a machine gun (and the game's own cast has its rate)

// The bash's own two numbers, apart from hit by touch's melee_min_speed / melee_stroke_start_cm: [gameplay] shield_smite_min_speed (m/s, 0.3 .. 8,
// default 0.8) and shield_smite_stroke_cm (cm, 2 .. 30, default 8) - what the shield always used. The swing engine's parameters for them; at the
// defaults exactly stroke::Params{}. (The shield's speed is the left HAND's: its movement plus its turn times the shield's length - the shield has
// no drawn blade of its own in the room, so hit by touch's blade speed does not apply.)
// The bash's least speed stays 0.3 .. 8 m/s (its own slider's range) although hit by touch's goes up to 20: the shield's speed is the hand's, a bash is no whip.
inline constexpr float kBashSpeedCeil = 8.0f;
inline stroke::StrokeTuning BashTuning(float minSpeed, float startCm) {
    return stroke::Clamp({std::isfinite(minSpeed) ? std::min(minSpeed, kBashSpeedCeil) : minSpeed, startCm});
}
inline stroke::Params BashParams(float minSpeed = stroke::kDefaultMinSpeed, float startCm = stroke::kDefaultStartCm) { return stroke::ParamsFor(BashTuning(minSpeed, startCm)); }

class Detector {
    stroke::Engine engine;     // the one that decides: gated (it forgets the stroke while the gate is shut)
    stroke::Engine observer;   // the same samples and numbers WITHOUT the gate - for the debug drawing and log only, it decides nothing
    stroke::HitSet hits;
    double lastRequest = -1e9;

public:
    Detector() : engine(BashParams()), observer(BashParams()) {}
    void Reset() {
        engine.Reset();
        observer.Reset();
        hits.Clear();
    }
    void SetParams(const stroke::Params& p) {
        engine.SetParams(p);
        observer.SetParams(p);
    }
    const stroke::Params& Params() const { return engine.GetParams(); }
    stroke::Diagnostics TakeDiagnostics() { return engine.TakeDiagnostics(); }

    // This frame: false with the engine forgotten when the gate is shut; true when the shield is in an energetic
    // stroke (the caller then looks for monsters and calls Pick).
    bool Step(const Gate& gate, const stroke::Sample& sample, stroke::Sweep& sweep) {
        stroke::Sweep unused;
        observer.Step(sample, unused);   // (a drawing's numbers: the shield's speed whatever the trigger does)
        stroke::Sample s = sample;
        if (!Open(gate)) s.active = false;
        if (!engine.Step(s, sweep)) return false;
        return true;
    }

    // What the debug drawing shows (melee_boxes): the observer's speed and stroke - the movement as it is, gate or none - and whether a bash
    // would count NOW (the deciding engine's latest step swept something, which it only does with the gate open).
    float Speed() const { return observer.LastSpeed(); }
    bool InStroke() const { return observer.InStroke(); }
    bool Counting() const { return engine.Counting(); }
    double SinceRequest(double now) const { return now - lastRequest; }

    // The monster to cast at: of those the stroke touched, the one nearest the shield; one per stroke, and not
    // within kMinGap of the last request. `session` changes with the game (a new hero, an area): hits start again.
    bool Pick(const stroke::Sweep& sweep, uint64_t session, double now, const Monster* monsters, size_t count, size_t& index) {
        if (!sweep.stroke || now - lastRequest < kMinGap) return false;
        if (hits.Contains(session, sweep.stroke, UINT32_MAX)) return false;
        bool found = false;
        float best = 0;
        for (size_t i = 0; i < count; ++i) {
            const stroke::Body& body = monsters[i].body;
            if (!stroke::Touches(sweep.before, sweep.after, body)) continue;
            const float d = stroke::SegmentDistanceSquared(sweep.after.base, sweep.after.tip, body.bottom, body.top);
            if (!found || d < best) {
                found = true;
                best = d;
                index = i;
            }
        }
        if (!found) return false;
        hits.Insert(session, sweep.stroke, UINT32_MAX);   // any monster: the stroke is spent
        lastRequest = now;
        return true;
    }
};

// -- the cast on its way ----------------------------------------------------------------------------------------
// From the request to the game's own click: the game's target lookups answer with the monster until the click has
// asked once (the click is what carries the target to the game's server), or kWindow has passed - a press the game
// refuses (no mana, a menu) must not leave the monster waiting for some other cast.
constexpr double kWindow = 0.35;
class Cast {
    bool open = false;
    uint32_t id = 0;
    float gx = 0, gy = 0;
    double begun = 0;

public:
    void Begin(uint32_t monster, float cellX, float cellY, double now) {
        open = true;
        id = monster;
        gx = cellX;
        gy = cellY;
        begun = now;
    }
    bool Open(double now) {
        if (open && now - begun > kWindow) open = false;
        return open;
    }
    void Finish() { open = false; }
    uint32_t Monster() const { return id; }
    float CellX() const { return gx; }
    float CellY() const { return gy; }
};

// -- the blow a bash makes ---------------------------------------------------------------------------------------------
// The game rolls the bash's hit AFTER the click has asked for the target - 25 to 53 ms after our press in the user's log - when the cast above is already finished (Finish at the
// click), so "a cast of ours is on its way" cannot tell the hit observer whose blow it is. The press says so instead: it claims the blow for the arm that wears the shield
// (vr/hit_claim.h), which the observer reads when the blow lands. `wearArm` is shieldarmwire::Arm(): 0 the right forearm, 1 the left, -1 not handled ([hands] shoulder_draw off) -
// then the shield is on the LEFT arm, as the game has it. Returns the skeleton side, 0 right / 1 left.
inline int BashSide(int wearArm) { return wearArm == 0 ? 0 : 1; }

// -- why a bash does not count (the debug line of melee_boxes) -------------------------------------------------------
// Smite costs this much mana at every level (Skills.txt); the game's own number decides the cast, this only words the log.
constexpr float kSmiteMana = 2.0f;
struct Facts {
    bool enabled = false;          // [gameplay] shield_smite
    bool playing = false;          // gate.playing && gate.f4 (a game, no menu, focus, F4, the hand tracked)
    float rightTrigger = 0.0f;
    bool slot = false;             // Smite sits on a pad slot
    float speed = 0.0f, minSpeed = 0.8f;   // the shield's speed (the observer's) and the bash speed
    bool inStroke = false;         // the observer says a stroke is on
    double sinceRequest = 1e9;     // seconds since the last bash was asked for
    bool heroKnown = false;        // the hero's unit is known, so monsters can be looked for
    bool haveNear = false;         // a monster in the scan
    skilltriggers::Verdict nearVerdict = skilltriggers::Verdict::Unknown;
    float mana = -1.0f, manaNeed = kSmiteMana;   // the hero's mana (< 0 not read)
};
// The skilltriggers::Why bits that apply. 0 = a bash would count (and be asked for). Off: only that (nothing else is ever evaluated).
inline uint32_t Explain(const Facts& f) {
    using namespace skilltriggers;
    if (!f.enabled) return kWhyOff;
    uint32_t why = 0;
    if (!f.playing) why |= kWhyNotPlaying;
    if (f.rightTrigger >= kTriggerDown) why |= kWhyTrigger;
    if (!f.slot) why |= kWhyNoSlot;
    if (!(f.speed >= f.minSpeed)) why |= kWhySlow;
    else if (!f.inStroke) why |= kWhyNoStroke;
    if (f.sinceRequest < kMinGap) why |= kWhyTooSoon;
    if (!f.heroKnown) why |= kWhyNoHero;
    else if (!f.haveNear) why |= kWhyNoMonster;
    if (f.mana >= 0.0f && f.mana < f.manaNeed) why |= kWhyNoMana;
    if (f.haveNear && f.nearVerdict == Verdict::Refused) why |= kWhyNotHostile;
    return why;
}

}  // namespace shieldsmite
