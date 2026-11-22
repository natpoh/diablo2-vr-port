// walk_in_place.h - [walk_in_place]: walk in place with the headset, BodyWalk's walking engine as a library in the game (shared/walk_engine_abi.h,
// vr/walk_engine_lib.h). The pure part: the settings, the trims handed to the library, the library's speed made into the game's own walking stick,
// the game's Run key past the run point, and where that stick joins the pad. No game, no Windows: the unit test is
// cleanroom/tests/walk_in_place_test.cpp, the wiring vr/walk_in_place_wire.cpp. Design: docs/notes/walk_in_place.md.
//
// The stick is the game's own walking vector (as the room walk's): ahead in the view (in F4 = where the headset looks), its length from the speed.
// The game decides walk or run itself (vr/lean_gait.h: longer than its stickRunToggleThreshold AND its run on = a run, else a walk at length /
// threshold): up to the run point the length lands at or under that threshold (a walk, at full walk strength right at the run point), past it
// above, and the game's Run key is held while its own Run / walk switch is on walk. Every length is past the pad's dead zone (10349 of 32767), so
// the slowest step still moves the hero, whatever path the game reads the stick by.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "lean_gait.h"

namespace wip {

// Serg 2026-11-22 (0.222.0): "отключи пока ходьбу и скрой я её не проверил" - walking in place is closed until he has tried it: the headset's Walk
// tab is hidden in every view (vr/hset_defs.h PageShown), the wiring takes on=0 whatever the ini says (vr/walk_in_place_wire.cpp Configure), and
// D2R VR Settings' START plan reads it as off (tools/settings/settings.cpp). true = open again, as before.
constexpr bool kOpen = false;

// [walk_in_place] source (Serg 2026-11-22: "встроенная библиотека или бодивалк"): what walks the hero by stepping in place.
//   kSourceLibrary  - the built-in library d2r_vr_walk.dll (BodyWalk's step engine in the game; its profile walk.json, the four rows' trims on top)
//   kSourceBodyWalk - the BodyWalk app with its own Steps settings: the library is not loaded, and BodyWalk's own walking stick (its pad report or
//                     Game Link's Walk block) is mixed into the pad, as with on=0 - in direct mode its walking passes whatever [openxr]
//                     direct_bodywalk_walk says (BodyWalkPasses). The four rows (the trims, the run point) are the library's: idle then.
enum Source : int { kSourceLibrary = 0, kSourceBodyWalk = 1 };
inline int SourceOf(float v) { return std::isfinite(v) && std::lround(v) == kSourceBodyWalk ? kSourceBodyWalk : kSourceLibrary; }

// d2r_vr.ini [walk_in_place] (the headset panel's Walk tab, vr/hset_defs.h): percent, as the rows show them.
struct Config {
    bool on = false;            // on=0: nothing loaded, nothing fed, every walk as before
    int source = kSourceLibrary;  // source=0 the built-in library (as before), 1 BodyWalk (its Steps settings)
    float sensitivity = 50.0f;  // 0..100: higher, lighter steps count
    float speed = 50.0f;        // 0..100: higher, full speed with gentler steps
    float smoothing = 50.0f;    // 0..100: higher, steadier, slower to start and stop
    float runAt = 80.0f;        // 20..100: the speed share past which the hero runs; 100 = never
};
constexpr float kRunAtMin = 20.0f, kRunAtMax = 100.0f;

inline float Pct(float v, float def, float lo = 0.0f, float hi = 100.0f) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; }
inline Config Clamped(Config c) {
    c.source = c.source == kSourceBodyWalk ? kSourceBodyWalk : kSourceLibrary;
    c.sensitivity = Pct(c.sensitivity, 50.0f);
    c.speed = Pct(c.speed, 50.0f);
    c.smoothing = Pct(c.smoothing, 50.0f);
    c.runAt = Pct(c.runAt, 80.0f, kRunAtMin, kRunAtMax);
    return c;
}
// The library is loaded and fed (on, the library chosen) - in F4 only, the wiring adds.
inline bool LibraryOn(const Config& c) { return c.on && c.source != kSourceBodyWalk; }
// BodyWalk walks by stepping in place (on, BodyWalk chosen): the library closed, BodyWalk's stick mixed in.
inline bool BodyWalkOn(const Config& c) { return c.on && c.source == kSourceBodyWalk; }
// Direct mode ([openxr] direct_input): BodyWalk's walking passes (it is given the head and hands, controls zeroed, and its walking stick is mixed in) when
// [openxr] direct_bodywalk_walk says so, or - in VR F4 - when BodyWalk is the walk source chosen here.
inline bool BodyWalkPasses(bool directBodyWalkWalk, const Config& c, bool f4) { return directBodyWalkWalk || (f4 && BodyWalkOn(c)); }
// The Walk tab's line while BodyWalk is the source (F4): what is known of BodyWalk.
struct BodyWalkFacts {
    bool f4 = false;
    bool running = false;   // BodyWalk's Game Link is alive or its pad report moves
    bool stickIn = false;   // its walking stick comes in: the pad report moves or Game Link's Walk block is active
    bool walking = false;   // ... and is pushed past the pad's dead zone
};
enum class BodyWalkLine { NotF4, NotRunning, NoStick, Standing, Walking };
inline BodyWalkLine BodyWalkLineOf(const BodyWalkFacts& f) {
    if (!f.f4) return BodyWalkLine::NotF4;
    if (!f.running && !f.stickIn) return BodyWalkLine::NotRunning;
    if (!f.stickIn) return BodyWalkLine::NoStick;
    return f.walking ? BodyWalkLine::Walking : BodyWalkLine::Standing;
}

// The trims the library takes (BWWE_Tuning: 0..1, 0.5 = BodyWalk's walk profile as it is).
struct Trims {
    float sensitivity = 0.5f, speed = 0.5f, smoothing = 0.5f;
    bool operator==(const Trims& o) const { return sensitivity == o.sensitivity && speed == o.speed && smoothing == o.smoothing; }
    bool operator!=(const Trims& o) const { return !(*this == o); }
};
inline Trims TrimsOf(const Config& raw) {
    const Config c = Clamped(raw);
    return {c.sensitivity / 100.0f, c.speed / 100.0f, c.smoothing / 100.0f};
}

// The pad's dead zone as the game has it (raw 10349 of 32767, no rescale) and the shortest stick handed to it: just past it.
constexpr float kDeadZone = 10349.0f / 32767.0f;
constexpr float kShortest = 10400.0f / 32767.0f;
// A speed under this is standing (the library's 0 may come as a crumb of a float).
constexpr float kStanding = 0.02f;
// The run point as a share (1 = never), and whether there is one.
inline float RunShare(float runAtPct) { return Pct(runAtPct, 80.0f, kRunAtMin, kRunAtMax) / 100.0f; }
inline bool Runs(float runAtPct) { return RunShare(runAtPct) < 0.999f; }

// The stick's length for the library's speed (0..1), the run point (percent, 100 never), the game's run threshold `t` (stickRunToggleThreshold,
// 0 = not readable). 0 = standing. Up to the run point: kShortest .. the longest walk (t, or the rim when t is not known); past it: above t up to
// the rim (the game runs, its switch or our Run key permitting). Never a run point: the walk's range over the whole speed.
inline float StickLength(float speed, float runAtPct, float t) {
    if (!std::isfinite(speed) || speed < kStanding) return 0.0f;
    speed = std::min(speed, 1.0f);
    const bool known = leangait::SaneThreshold(t) && t > kShortest;
    const float walkTop = known ? t : 1.0f;
    if (!Runs(runAtPct)) return kShortest + (walkTop - kShortest) * speed;
    const float run = std::clamp(RunShare(runAtPct), leangait::kMinLimit, leangait::kMaxLimit);
    if (speed <= run) return std::min(walkTop, kShortest + (walkTop - kShortest) * speed / run);
    if (!known) return 1.0f;   // (no threshold to steer by: the rim, and the Run key does the rest)
    return std::min(1.0f, t + 1e-3f + (speed - run) * (1.0f - t - 1e-3f) / (1.0f - run));
}

// The game's Run key past the run point (let go a little under it), only while the game's own Run / walk switch is on walk; never with no run point.
struct RunKey {
    leangait::RunKey key;
    bool Step(float speed, float runAtPct, bool switchRuns) {
        if (!Runs(runAtPct)) { key.past = false; return false; }
        const float s = std::isfinite(speed) && speed >= kStanding ? speed : 0.0f;
        return key.Step(s, std::clamp(RunShare(runAtPct), leangait::kMinLimit, leangait::kMaxLimit), switchRuns);
    }
};

// The stick in the view, raw as a pad has it (x right, y ahead, XInput units): straight ahead.
struct Stick {
    int16_t lx = 0, ly = 0;
    bool Any() const { return lx != 0 || ly != 0; }
};
inline Stick StickOf(float length) {
    if (!std::isfinite(length) || length <= 0.0f) return {};
    return {0, (int16_t)std::lround(std::min(length, 1.0f) * 32767.0f)};
}

// When the frame may walk: VR F4, a game area, the session focused, no menu or panel of the game's or ours, no chat line.
struct Gate {
    bool f4 = false, inWorld = false, focused = false, menu = false, panelUp = false, chat = false;
};
inline bool Play(const Gate& g) { return g.f4 && g.inWorld && g.focused && !g.menu && !g.panelUp && !g.chat; }

// Where the stick joins the pad the game's walk reads: only where the controller's own left stick rests (inside the dead zone) - a push of the
// controller's always wins. ownLX / ownLY in and out (raw).
inline bool PushedRaw(int lx, int ly) { return (float)lx * lx + (float)ly * ly > 10349.0f * 10349.0f; }
inline bool Merge(int16_t* ownLX, int16_t* ownLY, const Stick& ours) {
    if (!ours.Any() || PushedRaw(*ownLX, *ownLY)) return false;
    *ownLX = ours.lx;
    *ownLY = ours.ly;
    return true;
}

}  // namespace wip
