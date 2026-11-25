// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 09c3093ae312eeb6549a2302f76fa461e3dff35e by Berk (bearstonem), "Whirlwind: the walk stick steers each whirl"
// (https://github.com/bearstonem/d2r-pcvr/commit/09c3093ae312eeb6549a2302f76fa461e3dff35e), its steamvr/whirl_steer.h, taken into
// D2R VR Authentic for [gameplay] whirlwind_stick_steer: the game's walk dead zone (DeadZone, Pushed), the whirl's length (AheadCells) and
// the stick turned onto the ground the way the walk takes it (GroundWay: Unleashed's walkdir::OnGround + GridWay's level way), kept as they
// are. Changed: the namespace; plain floats for Unleashed's math::Vec; the grid's turn left to Authentic's own aimpoint::GridDir (the walk's
// calibration) and the spot to aimpoint::Point (the map's edge), so GridWay and Spot are not taken; the T's spot (MotionSpot) is not taken -
// Authentic has no T motion. Fresh added (the stick's age, the input thread vs the game's). The rest of D2R VR Authentic is MIT; this file
// stays MPL 2.0.
#pragma once
#include <cmath>
#include <cstdint>

// Whirlwind steered by the walk stick. A whirl cannot be turned once it has begun: the game's server makes the hero
// uninterruptable until the whirl reaches its spot and refuses every new command meanwhile; as it ends, a held button's next
// repeat starts the next whirl at once. So the stick steers BETWEEN whirls: while it is pushed past the game's own dead zone,
// each Whirlwind cast and each repeat of the held button aims AheadCells cells where that push would walk. Nothing is pressed
// or held - only where the game's own cast aims is chosen; each whirl costs its mana as ever. At rest everything aims as before.
// Value-only: cleanroom/tests/unleashed_whirl_steer_test.cpp.
namespace unleashed::whirlsteer {
constexpr int kWhirlwind = 151;
// The game's own dead zone for its walk stick (0x1210DA0: 10349 of 32767, not rescaled): steering begins where walking does.
// x right, y ahead, each -1..1.
constexpr float DeadZone = 10349.f / 32767.f;
inline bool Pushed(float x, float y) { return std::isfinite(x) && std::isfinite(y) && x * x + y * y > DeadZone * DeadZone; }
// Cells along the stick for a button's whirl. A whirl is one cast's mana however far it goes, and the stick can turn the chain
// only as each one ends: 10 cells, about 0.8 s at the 12 cells a second a whirl goes, turns it often enough without spending a
// cast on every few steps; shorter would turn sooner and cost more mana for the ground.
constexpr float AheadCells = 10.f;
// The stick (the view's frame: x right, y ahead) as a unit way on the ground in the world (x, z): right and ahead are the view's
// level right and ahead there (vrcam's StickFrame camR / camF), as the walk turns its stick (ViewStickToGame). The world's y is
// left out: a level way. False with the stick at rest or nonsense; the outputs then untouched.
inline bool GroundWay(float x, float y, const float right[3], const float ahead[3], float* wx, float* wz) {
    if (!wx || !wz || !right || !ahead || !Pushed(x, y)) return false;
    const float gx = right[0] * x + ahead[0] * y, gz = right[2] * x + ahead[2] * y;
    const float n = std::hypot(gx, gz);
    if (!std::isfinite(n) || n < 1e-4f) return false;
    *wx = gx / n; *wz = gz / n;
    return true;
}
// The stick read at `at` (ms) is still the stick at `now`: within maxAgeMs, never from the future, never unread (at 0).
inline bool Fresh(uint64_t at, uint64_t now, uint64_t maxAgeMs) { return at != 0 && now >= at && now - at <= maxAgeMs; }
}  // namespace unleashed::whirlsteer
