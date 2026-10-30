// vr/mp/pose_share.h - what vrcam.cpp uses of vr/mp/pose_share.cpp (docs/plan_modules.md M11).
#pragma once

#include <atomic>
#include <cstdint>

namespace monsterbodies { struct Snapshot; }

namespace vrc {
namespace fx {
void PoseShareTick();                         // our pose to the others, their places for the markers (the update thread)
bool BodiesWithPlayers(monsterbodies::Snapshot* out);   // the weight model's bodies with the other players' ([multiplayer] pose_share_bodies)
int PoseShareMarkers(const float eyeAt[3], float (*a)[4], float (*b)[4], int max);   // [debug] pose_share_draw
}  // namespace fx
}  // namespace vrc
