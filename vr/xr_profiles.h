#pragma once
// xr_profiles.h - controller profiles of native OpenXR beyond the ones every runtime knows (vr/xr.cpp MakeActions). Pure: the
// paths and the choices, no OpenXR call; cleanroom/tests/xr_profiles_test.cpp checks them.
//
//  - Steam Frame ([openxr] frame_profile, on): with XR_VALVE_frame_controller_interaction offered by the runtime the Frame's
//    controllers get their own layout instead of the runtime's Touch / Index stand-in. Its right hand has A and B; its LEFT hand's
//    two face buttons are a D-pad's down and up (the first and the second face button here); the left hand's View button and the
//    right hand's Menu button are the menu. The paths are bearstonem/d2r-pcvr's (MIT; steamvr/controller_labels.h FacePath and
//    steamvr/transport.cpp SetupActions, commit d05c286), confirmed on a Frame by its owner. Without the extension: nothing changes.
//  - Valve Index, grip by force ([openxr] index_grip_force, off): Index reads its grip from the fingers round the handle, so a
//    relaxed hand that only holds the controller counts as squeezed; d2r-pcvr binds the grip to squeeze/force instead (how hard it
//    is squeezed). With the box ticked an Index hand's grip is that force; other controllers and the box unticked: as before.

#include <cstring>

namespace xrprofiles {

constexpr const char* kFrameExtension = "XR_VALVE_frame_controller_interaction";
constexpr const char* kFrameProfile = "/interaction_profiles/valve/frame_controller_valve";

// The actions of xr.cpp, by name, so the binding table below stays data.
enum class Act { Grip, Aim, Trigger, Squeeze, Stick, StickClick, Primary, Secondary, Menu, Haptic };
struct Binding { Act act; const char* path; };

// Steam Frame's bindings (both hands spelled out).
constexpr Binding kFrameBindings[] = {
    {Act::Grip, "/user/hand/left/input/grip/pose"},          {Act::Grip, "/user/hand/right/input/grip/pose"},
    {Act::Aim, "/user/hand/left/input/aim/pose"},            {Act::Aim, "/user/hand/right/input/aim/pose"},
    {Act::Trigger, "/user/hand/left/input/trigger/value"},   {Act::Trigger, "/user/hand/right/input/trigger/value"},
    {Act::Squeeze, "/user/hand/left/input/squeeze/value"},   {Act::Squeeze, "/user/hand/right/input/squeeze/value"},
    {Act::Stick, "/user/hand/left/input/thumbstick"},        {Act::Stick, "/user/hand/right/input/thumbstick"},
    {Act::StickClick, "/user/hand/left/input/thumbstick/click"}, {Act::StickClick, "/user/hand/right/input/thumbstick/click"},
    {Act::Primary, "/user/hand/left/input/dpad_down/click"}, {Act::Primary, "/user/hand/right/input/a/click"},
    {Act::Secondary, "/user/hand/left/input/dpad_up/click"}, {Act::Secondary, "/user/hand/right/input/b/click"},
    {Act::Menu, "/user/hand/left/input/view/click"},         {Act::Menu, "/user/hand/right/input/menu/click"},
    {Act::Haptic, "/user/hand/left/output/haptic"},          {Act::Haptic, "/user/hand/right/output/haptic"},
};

// Does the runtime's list of extensions (names) offer the Frame's? `has` is called with a name.
inline bool WantFrame(bool option, bool offered) { return option && offered; }

inline bool IsIndex(const char* profile) { return profile && std::strstr(profile, "/valve/index_controller") != nullptr; }
inline bool IsFrame(const char* profile) { return profile && std::strcmp(profile, kFrameProfile) == 0; }

// A hand's grip, 0..1: its squeeze value, or - Index with index_grip_force ticked and the force action live - the force.
inline float Grip(bool forceOption, const char* profile, float value, float force, bool forceActive) {
    return forceOption && forceActive && IsIndex(profile) ? force : value;
}

}  // namespace xrprofiles
