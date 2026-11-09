// vr/mp/pose_share.h - what vrcam.cpp uses of vr/mp/pose_share.cpp (docs/plan_modules.md M11).
#pragma once

#include <atomic>
#include <cstdint>

namespace monsterbodies { struct Snapshot; }
namespace arrowflight { struct Shot; }
namespace throwborn { struct AimAsk; }

namespace vrc {
// A bow shot of ours to the other players (vr/shot_share.h): its line (world units), the press it belongs to, whether this is the press's own message.
void ShareShot(const arrowflight::Shot& s, uint16_t seq, bool press);
// At the start: the others' shots that come go to arrow_flight (their arrows drawn along them) and, as the host, to the create's table.
void ShotShareInit();
// The host, at the create of another player's arrow: turned to his line of that moment (his press's heading kept: a fan turns as a whole).
bool RemoteReleaseAim(const throwborn::AimAsk& a, int* tx, int* ty);
namespace fx {
void PoseShareTick();                         // our pose to the others, their places for the markers (the update thread)
bool BodiesWithPlayers(monsterbodies::Snapshot* out);   // the weight model's bodies with the other players' ([multiplayer] pose_share_bodies)
int PoseShareMarkers(const float eyeAt[3], float (*a)[4], float (*b)[4], int max);   // [debug] pose_share_draw
}  // namespace fx
}  // namespace vrc
