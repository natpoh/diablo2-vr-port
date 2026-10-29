// lean_gait.h - [direct] walk_by_lean / walk_by_lean_pct: walk or run by how far the left stick leans (the author, 2026-10-28: "up to
// 80 % a walk, past it a run" - walking spends no stamina, and small steps get easier). Pure: no game, no Windows; the unit test is
// cleanroom/tests/lean_gait_test.cpp, the wiring is vr/vrcam.cpp (the walking-stick hook).
//
// The game decides it itself (docs/move_recon.md, ControllerManager 0x14AA10, read 2026-10-28): it runs only while the stick is longer
// than its stickRunToggleThreshold (controllersettings.json, the ControllerManager's float at +0x1F94; the switch waits stickRunStartTime /
// stickRunStopTime) AND its run is on - the Run key (command 0x22) held, or the Run / walk switch (Always run, command 0x23) on - AND the
// hero has stamina. Under the threshold it forces the walk and walks at the strength length / threshold. Nothing here changes a speed:
//   - Shape: the stick's length handed to the game - up to our limit it lands at or under the game's threshold (the game walks, at full walk
//     strength right at the limit), past it above (the game runs); the direction is the stick's own.
//   - RunKey: past the limit the game's own Run key is held while its switch is on walk, so a lean past the limit runs whatever the switch
//     says; the switch on run needs nothing more. Out of stamina the game walks on its own, as ever.
#pragma once

#include <algorithm>
#include <cmath>

namespace leangait {

constexpr float kDefaultLimit = 0.80f;               // [direct] walk_by_lean_pct (80)
constexpr float kMinLimit = 0.20f, kMaxLimit = 0.95f;
constexpr float kRunKeyBand = 0.03f;                 // the Run key let go only this far back under the limit: no key flutter at the edge
constexpr unsigned kRunCommand = 0x22;               // the game's "Run (hold)" key command (shared/d2r_vr_shared.h)

inline float ClampLimit(float limit) { return std::isfinite(limit) ? std::clamp(limit, kMinLimit, kMaxLimit) : kDefaultLimit; }
// The game's stickRunToggleThreshold as read: a length strictly inside the stick's range, or nothing to steer by.
inline bool SaneThreshold(float t) { return std::isfinite(t) && t > 0.05f && t < 1.0f; }

// The stick's length handed to the game for a lean `lean` (0..1), the limit `limit`, the game's threshold `t`. Lean <= limit: at most t (the
// game's "longer than t" is false: it walks, at lean / limit of a full walk); past the limit: above t, the rim staying the rim.
inline float Shape(float lean, float limit, float t) {
    if (!std::isfinite(lean) || lean <= 0.0f) return 0.0f;
    lean = std::min(lean, 1.0f);
    limit = ClampLimit(limit);
    if (!SaneThreshold(t)) return lean;
    if (lean <= limit) return std::min(t, lean * t / limit);
    return std::min(1.0f, t + (lean - limit) * (1.0f - t) / (1.0f - limit));
}

// The Run key held past the limit (let go kRunKeyBand under it), only while the game's own switch is on walk.
struct RunKey {
    bool past = false;
    // lean: the stick's length now (0 = not walking); switchRuns: the game's Run / walk switch is on run. True: hold the game's Run key.
    bool Step(float lean, float limit, bool switchRuns) {
        limit = ClampLimit(limit);
        const float l = std::isfinite(lean) ? lean : 0.0f;
        past = past ? l > limit - kRunKeyBand : l > limit;
        return past && !switchRuns;
    }
};

}  // namespace leangait
