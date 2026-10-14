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

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "melee_bodies.h"
#include "stroke_contact.h"

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

class Detector {
    stroke::Engine engine;
    stroke::HitSet hits;
    double lastRequest = -1e9;

public:
    void Reset() {
        engine.Reset();
        hits.Clear();
    }
    stroke::Diagnostics TakeDiagnostics() { return engine.TakeDiagnostics(); }

    // This frame: false with the engine forgotten when the gate is shut; true when the shield is in an energetic
    // stroke (the caller then looks for monsters and calls Pick).
    bool Step(const Gate& gate, const stroke::Sample& sample, stroke::Sweep& sweep) {
        stroke::Sample s = sample;
        if (!Open(gate)) s.active = false;
        if (!engine.Step(s, sweep)) return false;
        return true;
    }

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

}  // namespace shieldsmite
