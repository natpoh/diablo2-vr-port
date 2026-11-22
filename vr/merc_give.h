// merc_give.h - [hands] merc_give_weapon: the weapon in the VR hand given to the hero's mercenary (Serg, 2026-11-22: «віддати найманцю зброю прямо з руки»).
//
// The gesture: let the weapon go (both grips open - the weight model's let-go, vr/weight.h) while it touches the mercenary, or throw / drop it so that it reaches
// him in the air or lands at his feet. Then the GAME moves it, by its own controller-layout item commands - nothing is written into an inventory, no packet is made:
//   1. the hand slot's own "to the inventory" (what the pad's quick move on an equipped item does):  0x15C600(0, bodyloc, 1, -1, 0)  -> C->S 0x58 BODYTOGRID
//      (into a free cell of the inventory; no room: the game says no and nothing moves - the weapon stays in the hand);
//   2. the inventory item's own "equip on the hireling" (the pad's quick move with the hireling's panel up): 0x15C220(item, 1, {0, 0}) -> C->S 0x5B
//      GRIDHIREABLETRANSACTION - the game picks his body location for it and puts what he held there into a free cell of the hero's inventory: his old weapon is
//      never lost (no room for it: the game refuses the whole move);
//   3. the mercenary did not take it (refused, no room for his old one): the same item's own "equip" back into the hand it came from (what the pad's quick move on
//      an inventory item does, 0x15C600(item, bodyloc, 1, -1, 0) -> C->S 0x56 GRIDTOBODY).
// Nothing goes through the cursor: with the controller layout the game drops a cursor item on the ground by itself the moment no panel is open (belt_drop.h).
// The game's names for its client commands are its own table (CCMD_*, "CCMD_GIVETOHIREABLE" is 0x51 ... "CCMD_GRIDHIREABLETRANSACTION" 0x5B at 0x1D9A5C8 on).
// Before anything is sent: the game's own "can the hireling wear it" (0x159D20(item, 1): his class may use the kind, he meets its requirements) - a weapon he
// cannot use is not moved at all (it lies where it fell; a grip calls it back as ever).
//
// The pure part below - the watch of a release and the steps of the sequence - is tested in cleanroom/tests/merc_give_test.cpp; vr/merc_give.cpp calls the game.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace D2RL { struct PluginContext; }

namespace mercgive {

// ---- the game's side (vr/merc_give.cpp) ----
void Init(const D2RL::PluginContext* ctx);
// From the settings' (re)load: the tick ([hands] merc_give_weapon, default on), how near his feet a dropped weapon still counts ([hands] merc_give_reach_cm,
// default 30: beyond his body's own radius), [debug] merc_give_trace (a log line for every watched sample).
void Configure(bool on, float reachM, bool trace);
// vrcam's timer (the frame order's, every few ms): the weight model's let-go weapon against the mercenary; a sequence under way goes to the UI thread.
void Tick(bool inWorld);

// ---- the geometry (pure) ----
// The squared distance from the segment a-b to the segment c-d.
inline float SegSegDist2(const float a[3], const float b[3], const float c[3], const float d[3]) {
    float u[3], v[3], w[3];
    for (int i = 0; i < 3; ++i) { u[i] = b[i] - a[i]; v[i] = d[i] - c[i]; w[i] = a[i] - c[i]; }
    auto dot = [](const float* x, const float* y) { return x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; };
    const float A = dot(u, u), B = dot(u, v), C = dot(v, v), D = dot(u, w), E = dot(v, w);
    const float den = A * C - B * B;
    float s = 0.0f, t = 0.0f;
    if (A < 1e-9f && C < 1e-9f) { s = 0.0f; t = 0.0f; }
    else if (A < 1e-9f) { s = 0.0f; t = std::clamp(E / C, 0.0f, 1.0f); }
    else if (C < 1e-9f) { t = 0.0f; s = std::clamp(-D / A, 0.0f, 1.0f); }
    else {
        s = den > 1e-9f ? std::clamp((B * E - C * D) / den, 0.0f, 1.0f) : 0.0f;
        t = (B * s + E) / C;
        if (t < 0.0f) { t = 0.0f; s = std::clamp(-D / A, 0.0f, 1.0f); }
        else if (t > 1.0f) { t = 1.0f; s = std::clamp((B - D) / A, 0.0f, 1.0f); }
    }
    float r2 = 0.0f;
    for (int i = 0; i < 3; ++i) { const float k = w[i] + s * u[i] - t * v[i]; r2 += k * k; }
    return r2;
}
// The mercenary as a capsule (bottom, top, radius), render metres; the weapon's shaft (butt, tip). How far the shaft is outside his body (< 0 inside).
struct Capsule { float bottom[3] = {}, top[3] = {}; float radius = 0.0f; };
inline float ShaftOutside(const Capsule& m, const float butt[3], const float tip[3]) {
    const float d2 = SegSegDist2(butt, tip, m.bottom, m.top);
    return std::isfinite(d2) ? std::sqrt(d2) - m.radius : 1e9f;
}

// ---- the watch of a release (pure) ----
constexpr double kWindowS = 3.0;      // s after the release: past it a weapon still in the air or lying is no longer watched
constexpr double kLandWindowS = 0.5;  // s after it first touched the floor: landing at his feet counts only now (not his walking over a weapon that lies)
struct Sample {
    int state = 0;            // weight::FreeNote::state: 0 in a hand / back / flying back, 1 in the air, 2 on the floor
    uint32_t release = 0;
    bool thrown = false;
    bool mercSeen = false;    // his capsule is known on this sample
    float outside = 1e9f;     // ShaftOutside: metres the shaft is outside his capsule
    double nowS = 0;
};
enum class Verdict { None, Give, Missed };
struct Watch {
    uint32_t release = 0;
    bool armed = false;
    double releasedAt = 0, landedAt = -1;
    const char* why = "";
    // reachM: [hands] merc_give_reach_cm - on the floor, this far outside his body still counts (it fell at his feet).
    Verdict Step(const Sample& s, float reachM) {
        why = "";
        if (s.release != release) {   // a new release: watched if it is let go (not a throw of a throwing weapon)
            release = s.release;
            armed = s.state != 0 && !s.thrown;
            releasedAt = s.nowS;
            landedAt = -1;
            if (!armed) return Verdict::None;
        }
        if (!armed) return Verdict::None;
        if (s.state == 0 || s.thrown) { armed = false; why = s.thrown ? "it was a throw" : "back in a hand"; return Verdict::None; }
        if (s.nowS - releasedAt > kWindowS) { armed = false; why = "it did not reach him"; return Verdict::Missed; }
        if (s.state == 1) {
            if (s.mercSeen && s.outside <= 0.0f) { armed = false; why = "it touched him"; return Verdict::Give; }
            return Verdict::None;
        }
        if (landedAt < 0) landedAt = s.nowS;
        if (s.mercSeen && s.outside <= std::max(0.0f, reachM)) { armed = false; why = s.outside <= 0.0f ? "it lies against him" : "it fell at his feet"; return Verdict::Give; }
        if (s.nowS - landedAt > kLandWindowS) { armed = false; why = "it fell away from him"; return Verdict::Missed; }
        return Verdict::None;
    }
};

// ---- the steps of the game's moves (pure) ----
enum class Phase { Idle, Asked, ToGrid, Giving, Back, Done, Stopped };
enum class Act { None, ToGrid, Give, PutBack };
struct See {
    bool gatesOpen = false;   // the game takes item commands now (not busy, input not locked, no trade-like UI state, the hireling gate down, the cursor empty)
    bool inHand = false;      // the hand slot holds our item
    bool itemFound = false;   // our item is a unit the client knows
    bool itemBusy = false;    // ... with a request of the game's under way
    bool mercCan = false;     // the game's own "the hireling can wear it" (asked only while it is in the hand)
    bool onMerc = false;      // the mercenary wears it
    double sinceS = 0;        // since this phase began
};
struct Next { Phase phase; Act act; const char* why; };

constexpr double kGatesWaitS = 0.5;   // the game not taking item commands this long at the start: given up (nothing sent)
constexpr double kAnswerS = 1.5;      // the game's answer to one move
constexpr double kLongS = 4.0;        // a move whose item stays busy this long: given up (the game keeps it where it is)

inline Next Step(Phase p, const See& s) {
    switch (p) {
        case Phase::Asked:
            if (!s.inHand) return {Phase::Stopped, Act::None, "the weapon is no longer in its hand slot: nothing sent"};
            if (!s.gatesOpen || s.itemBusy) {
                if (s.sinceS > kGatesWaitS) return {Phase::Stopped, Act::None, "the game does not take item commands now: nothing sent"};
                return {Phase::Asked, Act::None, ""};
            }
            if (!s.mercCan) return {Phase::Stopped, Act::None, "the game says the mercenary cannot use it (his class, its kind or its requirements): nothing sent"};
            return {Phase::ToGrid, Act::ToGrid, "the hand slot's own quick move: the weapon into the inventory"};
        case Phase::ToGrid:
            if (s.onMerc) return {Phase::Done, Act::None, "the mercenary wears it"};
            if (s.inHand) {
                if (s.sinceS > kAnswerS) return {Phase::Stopped, Act::None, "the game did not move it into the inventory (no room?): it stays in the hand"};
                return {Phase::ToGrid, Act::None, ""};
            }
            if (!s.itemFound || s.itemBusy || !s.gatesOpen) {
                if (s.sinceS > kLongS) return {Phase::Stopped, Act::None, "the weapon left the hand but is not free in the inventory: left where the game has it"};
                return {Phase::ToGrid, Act::None, ""};
            }
            return {Phase::Giving, Act::Give, "the inventory item's own equip on the hireling (his old one into the inventory)"};
        case Phase::Giving:
            if (s.onMerc) return {Phase::Done, Act::None, "the mercenary wears it"};
            if (s.sinceS > kAnswerS) {
                if (s.itemFound && !s.itemBusy && s.gatesOpen && !s.inHand) return {Phase::Back, Act::PutBack, "the mercenary did not take it: back into the hand"};
                if (s.inHand) return {Phase::Done, Act::None, "it is back in the hand"};
                if (s.sinceS > kLongS) return {Phase::Stopped, Act::None, "the mercenary did not take it and it is not free to go back: left in the inventory"};
            }
            return {Phase::Giving, Act::None, ""};
        case Phase::Back:
            if (s.inHand) return {Phase::Done, Act::None, "back in the hand"};
            if (s.onMerc) return {Phase::Done, Act::None, "the mercenary wears it after all"};
            if (s.sinceS > kAnswerS) return {Phase::Stopped, Act::None, "the game did not put it back: it is in the inventory"};
            return {Phase::Back, Act::None, ""};
        default:
            return {p, Act::None, ""};
    }
}

}  // namespace mercgive
