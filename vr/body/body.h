// vr/body/body.h - what vrcam.cpp uses of the hero's facing and arms (vr/body/*.cpp, docs/plan_modules.md M12).
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace vrc {

// vr/body/hero_facing.cpp: where the hero's model faces, the body under the camera
bool FreshYawInModel(float* yawInModel, float* eyeModel, bool* eyeOk);
bool CameraYawInModel(float* out, float* eyeModel, bool* eyeOk);
// the camera's forward in the hero's model space (unit; y up) as the last CameraYawInModel / FreshYawInModel found it ([debug] body_dump)
bool CameraForwardInModel(float out[3]);
// ... and its right ([body] flat_weapon_adjust: the arms moved along the camera's own axes)
bool CameraRightInModel(float out[3]);
// ... and a world point in the hero's model space by the same matrix ([body] flat_aim_tilt_target: the body under the crosshair)
bool WorldPointInModel(const float world[3], float out[3]);
bool FreshRoomShift(float shift[3]);
// vr/body/arms.cpp: the head in BodyWalk's room, the hands, the hero's arms (skel::Input, every round of the update thread)
void PushArms();

}  // namespace vrc
