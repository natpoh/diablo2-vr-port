// Offline checks of vr/xr_profiles.h: Steam Frame's own layout ([openxr] frame_profile) and the Index grip by force
// ([openxr] index_grip_force). No OpenXR, no headset. Run: xr_profiles_test.exe (exit 0 = all passed).
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "xr_profiles.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using xrprofiles::Act;

static const char* PathOf(Act act, bool left) {
    for (const auto& b : xrprofiles::kFrameBindings)
        if (b.act == act && std::strncmp(b.path, left ? "/user/hand/left/" : "/user/hand/right/", left ? 16 : 17) == 0) return b.path;
    return nullptr;
}

static void TestFrameTable() {
    // every action has exactly one path per hand, every path is a hand's input or output, no path twice
    std::set<std::string> seen;
    for (const auto& b : xrprofiles::kFrameBindings) {
        const std::string p = b.path;
        CHECK(seen.insert(p).second);
        const bool left = p.rfind("/user/hand/left/", 0) == 0, right = p.rfind("/user/hand/right/", 0) == 0;
        CHECK(left != right);
        const std::string rest = p.substr(left ? 16 : 17);
        CHECK(b.act == Act::Haptic ? rest == "output/haptic" : rest.rfind("input/", 0) == 0);
        CHECK(p.find(' ') == std::string::npos && p.back() != '/');
    }
    for (Act a : {Act::Grip, Act::Aim, Act::Trigger, Act::Squeeze, Act::Stick, Act::StickClick, Act::Primary, Act::Secondary, Act::Menu, Act::Haptic}) {
        int n[2] = {};
        for (const auto& b : xrprofiles::kFrameBindings)
            if (b.act == a) ++n[std::strncmp(b.path, "/user/hand/left/", 16) == 0 ? 0 : 1];
        CHECK(n[0] == 1 && n[1] == 1);
    }
    // the Frame's own buttons: the right A / B, the left D-pad down / up; the menu: the left View, the right Menu
    CHECK(std::strcmp(PathOf(Act::Primary, false), "/user/hand/right/input/a/click") == 0);
    CHECK(std::strcmp(PathOf(Act::Secondary, false), "/user/hand/right/input/b/click") == 0);
    CHECK(std::strcmp(PathOf(Act::Primary, true), "/user/hand/left/input/dpad_down/click") == 0);
    CHECK(std::strcmp(PathOf(Act::Secondary, true), "/user/hand/left/input/dpad_up/click") == 0);
    CHECK(std::strcmp(PathOf(Act::Menu, true), "/user/hand/left/input/view/click") == 0);
    CHECK(std::strcmp(PathOf(Act::Menu, false), "/user/hand/right/input/menu/click") == 0);
    CHECK(std::strcmp(PathOf(Act::Squeeze, true), "/user/hand/left/input/squeeze/value") == 0);
    CHECK(std::strcmp(xrprofiles::kFrameProfile, "/interaction_profiles/valve/frame_controller_valve") == 0);
    CHECK(std::strcmp(xrprofiles::kFrameExtension, "XR_VALVE_frame_controller_interaction") == 0);
    // taken only when both the box and the runtime say so
    CHECK(xrprofiles::WantFrame(true, true) && !xrprofiles::WantFrame(false, true) && !xrprofiles::WantFrame(true, false));
    CHECK(xrprofiles::IsFrame("/interaction_profiles/valve/frame_controller_valve") && !xrprofiles::IsFrame("/interaction_profiles/valve/index_controller"));
    CHECK(!xrprofiles::IsFrame(nullptr));
}

static void TestIndexGrip() {
    const char* index = "/interaction_profiles/valve/index_controller";
    const char* touch = "/interaction_profiles/oculus/touch_controller";
    CHECK(xrprofiles::IsIndex(index) && !xrprofiles::IsIndex(touch) && !xrprofiles::IsIndex(nullptr) && !xrprofiles::IsIndex(""));
    // a relaxed hand round an Index handle: value high, force none
    CHECK(xrprofiles::Grip(false, index, 0.95f, 0.0f, true) == 0.95f);   // box unticked: as before
    CHECK(xrprofiles::Grip(true, index, 0.95f, 0.0f, true) == 0.0f);     // ticked: the force - not squeezed
    CHECK(xrprofiles::Grip(true, index, 1.0f, 0.7f, true) == 0.7f);
    CHECK(xrprofiles::Grip(true, index, 0.95f, 0.0f, false) == 0.95f);   // the force action not live: the value, never nothing
    CHECK(xrprofiles::Grip(true, touch, 0.6f, 0.0f, true) == 0.6f);      // another controller: its own grip
    CHECK(xrprofiles::Grip(true, nullptr, 0.6f, 0.9f, true) == 0.6f);
}

// PS VR2 on SteamVR runs as a Touch controller and its Options button (right) arrives as the Touch LEFT menu button; the Create button never arrives.
static void TestPs2Menu() {
    const char* touch = "/interaction_profiles/oculus/touch_controller";
    const char* index = "/interaction_profiles/valve/index_controller";
    const char* ps2 = "SteamVR/OpenXR : playstation_vr2";   // the headset's name in the log of 2026-10-17
    CHECK(xrprofiles::IsPs2Headset(ps2) && xrprofiles::IsPs2Headset("SteamVR/OpenXR : PlayStation_VR2") && xrprofiles::IsPs2Headset("playstation_vr2"));
    CHECK(!xrprofiles::IsPs2Headset("SteamVR/OpenXR : oculus") && !xrprofiles::IsPs2Headset("SteamVR/OpenXR : lighthouse") &&
          !xrprofiles::IsPs2Headset("") && !xrprofiles::IsPs2Headset(nullptr) && !xrprofiles::IsPs2Headset("playstation_vr"));
    CHECK(xrprofiles::IsTouch(touch) && !xrprofiles::IsTouch(index) && !xrprofiles::IsTouch(nullptr) && !xrprofiles::IsTouch(""));
    // only PS VR2 under the Touch profile: the Options button is the right menu button
    CHECK(xrprofiles::MenuIsRightOptions(ps2, touch));
    CHECK(!xrprofiles::MenuIsRightOptions(ps2, index) && !xrprofiles::MenuIsRightOptions(ps2, nullptr) && !xrprofiles::MenuIsRightOptions(ps2, ""));   // its own profile, if SteamVR ever gives one: as it says
    CHECK(!xrprofiles::MenuIsRightOptions("SteamVR/OpenXR : oculus", touch) && !xrprofiles::MenuIsRightOptions(nullptr, touch));                       // a real Touch: the left menu is the left one
    CHECK(!xrprofiles::MenuIsRightOptions("Oculus Quest", touch) && !xrprofiles::MenuIsRightOptions("Index", index));
}

// Which controllers have a thumbstick that vr/xr.cpp suggested a binding for (vr/xr_stick_check.h does not call a missing stick a fault elsewhere).
static void TestHasThumbstick() {
    CHECK(xrprofiles::HasThumbstick("/interaction_profiles/oculus/touch_controller") && xrprofiles::HasThumbstick("/interaction_profiles/valve/index_controller"));
    CHECK(xrprofiles::HasThumbstick("/interaction_profiles/htc/vive_controller") && xrprofiles::HasThumbstick("/interaction_profiles/microsoft/motion_controller"));
    CHECK(xrprofiles::HasThumbstick(xrprofiles::kFrameProfile));
    CHECK(!xrprofiles::HasThumbstick("/interaction_profiles/khr/simple_controller") && !xrprofiles::HasThumbstick("") && !xrprofiles::HasThumbstick(nullptr));
    CHECK(!xrprofiles::HasThumbstick("/interaction_profiles/hp/mixed_reality_controller"));   // not suggested: no binding, no verdict
}

int main() {
    TestFrameTable();
    TestIndexGrip();
    TestPs2Menu();
    TestHasThumbstick();
    std::printf("xr_profiles_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
