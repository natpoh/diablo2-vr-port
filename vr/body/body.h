// vr/body/body.h - what vrcam.cpp uses of the hero's facing and arms (vr/body/*.cpp, docs/plan_modules.md M12).
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace vrc {

// vr/body/hero_facing.cpp: where the hero's model faces, the body under the camera
bool FreshYawInModel(float* yawInModel, float* eyeModel, bool* eyeOk);
bool CameraYawInModel(float* out, float* eyeModel, bool* eyeOk);
bool FreshRoomShift(float shift[3]);
// vr/body/arms.cpp: the head in BodyWalk's room, the hands, the hero's arms (skel::Input, every round of the update thread)
void PushArms();

}  // namespace vrc
