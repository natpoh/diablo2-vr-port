cmake_minimum_required(VERSION 3.29)
project(d2r_vr LANGUAGES C CXX RC)

#   d2rl-vrcam.dll    game side, D2RLoader plugin: the VR layer (vr/) on our own
#                     camera and render distance (cleanroom/) - no D2R-3D code
# BodyWalk's side is its own Game Link plugin (game_link.dll, built with BodyWalk);
# the mod speaks bodywalk/game_link.h to it (vr/link.cpp).
# D2R-3D/ is kept for reference only; nothing here builds from it.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")   # no VC runtime to ship next to the game

# First person (shared/d2r_vr_build.h): off = the public build's views only (F1 - F3).
option(D2RVR_FIRST_PERSON "Build first person: arms, hands, held weapons, VR F4 and flat F3" ON)
add_compile_definitions(D2RVR_FIRST_PERSON=$<IF:$<BOOL:${D2RVR_FIRST_PERSON}>,1,0>)

# Headers from BodyWalk and FlatVR (the shared-memory blocks, BodyWalk Game Link) and the
# ReShade SDK: in external/ when this is the public tree (tools/make_public.py), else
# from the BodyWalk source tree beside it. Game Link (bodywalk/game_link.h, MIT) moves to
# BodyWalk's public plugin SDK (integralab-am/bodywalkvr-plugin-sdk) and comes in through
# FetchContent once that is out.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external")
    set(_ext "${CMAKE_CURRENT_SOURCE_DIR}/external")
    set(BODYWALK_SRC "${_ext}/bodywalk" CACHE PATH "Dir holding pad_mirror_shared.h")
    set(BODYWALK_SDK_INCLUDE "${_ext}" CACHE PATH "Dir holding bodywalk/game_link.h")
    set(FLATVR_SHARED_DIR "${_ext}/flatvr" CACHE PATH "Dir holding FlatVR's shared-memory headers")
    set(RESHADE_SDK_INCLUDE "${_ext}/reshade/include" CACHE PATH "ReShade SDK include dir")
else()
    set(BODYWALK_SRC "C:/wsl/vjoy/cpp_src/src" CACHE PATH "Dir holding pad_mirror_shared.h")
    set(BODYWALK_SDK_INCLUDE "C:/wsl/vjoy/sdk/include" CACHE PATH "Dir holding bodywalk/game_link.h")
    set(FLATVR_SHARED_DIR "C:/wsl/vjoy/cpp_src/flatvr/reshade_depth_addon" CACHE PATH "Dir holding FlatVR's shared-memory headers")
    set(RESHADE_SDK_INCLUDE "C:/wsl/vjoy/cpp_src/flatvr/reshade_sdk/include" CACHE PATH "ReShade SDK include dir")
endif()

# D2RLoader's plugin SDK (MIT): a checkout beside this file, else fetched from GitHub.
set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PluginSDK/CMakeLists.txt")
    add_subdirectory(PluginSDK)
else()
    include(FetchContent)
    FetchContent_Declare(D2RLPluginSDK GIT_REPOSITORY https://github.com/D2RLoader/PluginSDK.git
        GIT_TAG 717f727a0ec52912d1558764345f8fa3453a2bd6)   # the SDK this was built against (2026-09-24)
    FetchContent_MakeAvailable(D2RLPluginSDK)
endif()

# Khronos' OpenXR loader (Apache-2.0) for native OpenXR ([openxr] on, vr/xr.cpp): linked in
# statically, so there is no openxr_loader.dll to ship beside the game.
include(FetchContent)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(OpenXR GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
    GIT_TAG release-1.1.54 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(OpenXR)
# its static loader takes the DLL C runtime; ours is the static one
set_target_properties(openxr_loader PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook")   # BSD, Tsuda Kageyu
add_library(minhook STATIC
    ${MINHOOK_DIR}/src/buffer.c ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c ${MINHOOK_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)

# Our camera, render distance and memory pools, written clean-room from the game (cleanroom/).
add_library(d2rcam STATIC cleanroom/camera/d2rcam.cpp cleanroom/drawdist/drawdist.cpp cleanroom/pools/pools.cpp cleanroom/sigscan/sigscan.cpp)
target_include_directories(d2rcam PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/camera ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/sigscan)
target_link_libraries(d2rcam PUBLIC D2RLPlugin::D2RLPlugin)
target_compile_definitions(d2rcam PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(d2rcam PRIVATE /utf-8 /W4 /EHsc /permissive-)

# The bow's own rig drawn by the pull (vr/bow_rig.h): pure math, tested on a synthetic bow.
add_executable(bow_rig_test cleanroom/tests/bow_rig_test.cpp)
target_include_directories(bow_rig_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(bow_rig_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The bow along the hands (vr/bow_aim.h): the turn onto the aim line, the roll, the blend; pure maths on synthetic poses.
add_executable(bow_aim_test cleanroom/tests/bow_aim_test.cpp)
target_include_directories(bow_aim_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(bow_aim_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Which bow skeletons the game poses and the hero's among them (vr/bow_census.h): each keeps its own status, the hero's speaks for the rig.
add_executable(bow_census_test cleanroom/tests/bow_census_test.cpp)
target_include_directories(bow_census_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(bow_census_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The string hand's fingers as the game's own draw animation holds them (vr/bow_hook.h): the table, the row-vector matrices, the blend.
add_executable(bow_hook_test cleanroom/tests/bow_hook_test.cpp)
target_include_directories(bow_hook_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(bow_hook_test PRIVATE /utf-8 /W4 /EHsc /permissive-)

# A held weapon's weight (cleanroom/heft, docs/WEAPON_WEIGHT.md): the model, its
# synthetic tests, and heft_bench.dll for the Unity bench (tools/heft_bench_unity); hold.cpp,
# the hands that take it, is in the mod too (vr/weight.cpp).
add_library(heft STATIC cleanroom/heft/heft.cpp cleanroom/heft/hold.cpp)
target_include_directories(heft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/heft)
target_compile_options(heft PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_library(heft_benchdefs STATIC cleanroom/heft/bench.cpp)
target_link_libraries(heft_benchdefs PUBLIC heft)
target_compile_options(heft_benchdefs PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(heft_test cleanroom/tests/heft_test.cpp)
target_link_libraries(heft_test PRIVATE heft_benchdefs)
target_compile_options(heft_test PRIVATE /utf-8 /W4 /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS)
# The javelin throw gesture (vr/throw_gesture.h), a pure detector, on synthetic hand poses - and its wiring to the headset
# frames (vr/throw_gesture_wire.cpp) with stand-ins for direct_input and the game's weapon state. Our own tests: not in the public tree.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/throw_gesture_test.cpp")
    add_executable(throw_gesture_test cleanroom/tests/throw_gesture_test.cpp)
    target_compile_options(throw_gesture_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/throw_aim_test.cpp")   # the auto-aim cone and a thrown potion's ballistics (vr/throw_aim.h)
    add_executable(throw_aim_test cleanroom/tests/throw_aim_test.cpp)
    target_compile_options(throw_aim_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/throw_source_test.cpp")   # the weight model's account of a throw against the hand's own (vr/throw_source.h)
    add_executable(throw_source_test cleanroom/tests/throw_source_test.cpp)
    target_compile_options(throw_source_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/throw_land_test.cpp")   # when a potion's game throw is pressed, and what it costs (vr/throw_land.h)
    add_executable(throw_land_test cleanroom/tests/throw_land_test.cpp)
    target_compile_options(throw_land_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/throw_trace_test.cpp")   # the throw tracer's lines (vr/throw_trace.h)
    add_executable(throw_trace_test cleanroom/tests/throw_trace_test.cpp)
    target_compile_options(throw_trace_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/throw_cast_test.cpp")   # which command a throw presses and what the game answered (vr/throw_cast.h)
    add_executable(throw_cast_test cleanroom/tests/throw_cast_test.cpp)
    target_compile_options(throw_cast_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/throw_born_test.cpp")   # [hands] throw_direct: the potion born where the model lands (vr/throw_born.h, pure)
    add_executable(throw_born_test cleanroom/tests/throw_born_test.cpp)
    target_compile_options(throw_born_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/potion_hide_test.cpp")   # [hands] potion_hide_game: the game's own thrown potion not drawn (vr/potion_hide.h, pure)
    add_executable(potion_hide_test cleanroom/tests/potion_hide_test.cpp)
    target_compile_options(potion_hide_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/throw_gesture_wire_test.cpp" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vr/direct_input.h")
    add_executable(throw_gesture_wire_test cleanroom/tests/throw_gesture_wire_test.cpp vr/throw_gesture_wire.cpp)
    target_include_directories(throw_gesture_wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared ${FLATVR_SHARED_DIR} ${BODYWALK_SRC} ${BODYWALK_SDK_INCLUDE})
    target_link_libraries(throw_gesture_wire_test PRIVATE D2RLPlugin::D2RLPlugin)
    target_compile_options(throw_gesture_wire_test PRIVATE /utf-8 /W3 /EHsc /permissive-)
endif()
# The swing engine (vr/stroke_contact.h, pure maths): the hand's strokes and the blade's sweeps on synthetic poses, no game.
add_executable(stroke_contact_test cleanroom/tests/stroke_contact_test.cpp)
target_compile_options(stroke_contact_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Melee by touch (vr/melee_bodies.h: the weapon's and the monsters' capsules) with the engine, on synthetic swings.
# [gameplay] melee_direct (vr/melee_direct.h): the route, the units' verdict and the hooks' windows, pure.
add_executable(melee_direct_test cleanroom/tests/melee_direct_test.cpp)
target_include_directories(melee_direct_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(melee_direct_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(melee_touch_test cleanroom/tests/melee_touch_test.cpp)
target_include_directories(melee_touch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(melee_touch_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Hit by touch with a thrown weapon (vr/thrown_touch.h, pure): the grip's held state and the rules that keep the touch and the throw by gesture
# from both firing for one swing.
add_executable(thrown_touch_test cleanroom/tests/thrown_touch_test.cpp)
target_include_directories(thrown_touch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(thrown_touch_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Which hand swings a weapon that has weight ([weight], vr/weight_drive.h) with the swing engine, on synthetic hands.
add_executable(weight_drive_test cleanroom/tests/weight_drive_test.cpp)
target_compile_options(weight_drive_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# A grip release that is a throw does not drop a thrown weapon ([weight] with [hands] throw_by_grip, vr/weight_throw.h), on synthetic timelines.
add_executable(weight_throw_test cleanroom/tests/weight_throw_test.cpp)
target_compile_options(weight_throw_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# A let-go weapon slung on the hero's back ([weight] to_back): where it is drawn (vr/weapon_back.h) and what a grip behind a shoulder is (vr/shoulder_grip.h), pure.
add_executable(weapon_back_test cleanroom/tests/weapon_back_test.cpp)
target_compile_options(weapon_back_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(shoulder_grip_test cleanroom/tests/shoulder_grip_test.cpp)
target_compile_options(shoulder_grip_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Where a staff's aim comes from (vr/aim_source.h): the controllers, never the weapon as the weight model draws it, pure.
add_executable(aim_source_test cleanroom/tests/aim_source_test.cpp)
target_compile_options(aim_source_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The weight model's shaft lies on the hit-by-touch blade's axis: the same bone as drawn, the same ends (vr/weapon_shaft.h, blade_shape.h, Hold::SetDrawnShaft), pure.
add_executable(weapon_shaft_test cleanroom/tests/weapon_shaft_test.cpp)
target_include_directories(weapon_shaft_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_link_libraries(weapon_shaft_test PRIVATE heft)
target_compile_options(weapon_shaft_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Where a thrown weapon lands (vr/weight_flight.h): the flight, the reach scale, the cap, pure.
add_executable(weight_flight_test cleanroom/tests/weight_flight_test.cpp)
target_compile_options(weight_flight_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The floor a let-go weapon falls to: the game's ground under the hero, in the room (vr/weight_floor.h), pure.
add_executable(weight_floor_test cleanroom/tests/weight_floor_test.cpp)
target_compile_options(weight_floor_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [weight] grip_fit: the hand that takes a weapon laid on its shaft, the corrections in the shaft's own frame (vr/grip_fit.h), pure.
add_executable(grip_fit_test cleanroom/tests/grip_fit_test.cpp)
target_compile_options(grip_fit_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The weight model's shaft is the drawn bone's axis in every state, two hands included (vr/weight.cpp against a stand-in of the skeleton hook's weapon placement).
add_executable(weight_two_hands_test cleanroom/tests/weight_two_hands_test.cpp vr/weight.cpp)
target_include_directories(weight_two_hands_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_link_libraries(weight_two_hands_test PRIVATE heft)
target_compile_definitions(weight_two_hands_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(weight_two_hands_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# A weapon's bone matrix that came out shrunk (the game hides a bone by shrinking it; the staff flattened into a plane) is brought back (vr/bone_guard.h), pure.
add_executable(bone_guard_test cleanroom/tests/bone_guard_test.cpp)
target_compile_options(bone_guard_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The hand that takes a weapon gets the game's own grip pose of the fingers and the thumb (vr/grip_pose.h): crossing a pose from one hand to the other, pure.
add_executable(grip_pose_test cleanroom/tests/grip_pose_test.cpp)
target_compile_options(grip_pose_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The skeleton hook ITSELF (vr/skeletons.cpp, included whole) on a toy rig in fake game memory, with the real weight model: every matrix it makes for the weapon and the hands is
# a proper one (orthonormal, the scale it had, the handedness it had), whatever way the staff points - the whole sphere, the hand's roll, one hand, two, a taken-over hand.
add_executable(staff_flat_test cleanroom/tests/staff_flat_test.cpp vr/weight.cpp)
target_include_directories(staff_flat_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_link_libraries(staff_flat_test PRIVATE d2rcam heft user32)
target_compile_definitions(staff_flat_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(staff_flat_test PRIVATE /utf-8 /W3 /EHsc /permissive-)
# ... on a toy rig with real hands (fingers and thumb, the left hand a relabelled mirror): the hand that takes the weapon gets the game's own grip pose of the fingers ([weight] grip_fit_fingers)
add_executable(grip_fingers_test cleanroom/tests/grip_fingers_test.cpp vr/weight.cpp)
target_include_directories(grip_fingers_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_link_libraries(grip_fingers_test PRIVATE d2rcam heft user32)
target_compile_definitions(grip_fingers_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(grip_fingers_test PRIVATE /utf-8 /W3 /EHsc /permissive-)
# ... and the glue of it (vr/weight.cpp, the real clock): a weapon let go falls, lies, goes to the back, is taken from there
add_executable(weight_back_wire_test cleanroom/tests/weight_back_wire_test.cpp vr/weight.cpp)
target_include_directories(weight_back_wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_link_libraries(weight_back_wire_test PRIVATE heft)
target_compile_definitions(weight_back_wire_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(weight_back_wire_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_library(heft_bench SHARED cleanroom/heft/heft_capi.cpp)
target_link_libraries(heft_bench PRIVATE heft_benchdefs)
target_compile_options(heft_bench PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The shoulder swap's detector on synthetic poses (vr/shoulder_swap.h): shoulder_swap_test
add_executable(shoulder_swap_test cleanroom/tests/shoulder_swap_test.cpp)
target_compile_options(shoulder_swap_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# ... and its wiring (vr/shoulder_swap_wire.cpp) fed direct_input.h's frames, with stand-ins for Listen / Take / Tap and the weapon set
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vr/direct_input.h")
    add_executable(shoulder_swap_wire_test cleanroom/tests/shoulder_swap_wire_test.cpp vr/shoulder_swap_wire.cpp)
    target_link_libraries(shoulder_swap_wire_test PRIVATE D2RLPlugin::D2RLPlugin)
    target_compile_definitions(shoulder_swap_wire_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(shoulder_swap_wire_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # [weight]: the grips of a hand on the weapon taken from the layout (vr/weight_wire.cpp), with stand-ins for Listen / Take / Latest and the weight model
    add_executable(weight_wire_test cleanroom/tests/weight_wire_test.cpp vr/weight_wire.cpp)
    target_include_directories(weight_wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(weight_wire_test PRIVATE D2RLPlugin::D2RLPlugin)
    target_compile_definitions(weight_wire_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(weight_wire_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

add_library(d2rl-vrcam SHARED vr/vrcam.cpp vr/xr.cpp vr/link.cpp vr/reshade_shield.cpp vr/dlss_mv.cpp vr/skeletons.cpp vr/weight.cpp vr/gamestate.cpp vr/gamecmd.cpp vr/direct_input.cpp vr/panel_input.cpp vr/throw_gesture_wire.cpp vr/throw_born.cpp vr/potion_hide.cpp vr/probe.cpp vr/uitrace.cpp vr/light_bounds.cpp vr/hud.cpp vr/memdiff.cpp vr/writewatch.cpp vr/hset_paint.cpp vr/hset_store.cpp shared/ini_txn.cpp vr/render_resolution.cpp vr/melee_touch.cpp vr/melee_direct.cpp vr/skill_wheel.cpp vr/wheel_game.cpp vr/wheel_paint.cpp vr/tiles_paint.cpp vr/wheel_icons.cpp vr/stand_still_wire.cpp vr/stray_walk_wire.cpp vr/shot_trace_wire.cpp vr/hset_view.cpp vr/vrcam.rc)
target_include_directories(d2rl-vrcam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${FLATVR_SHARED_DIR} ${RESHADE_SDK_INCLUDE} ${BODYWALK_SRC} ${BODYWALK_SDK_INCLUDE})
target_sources(d2rl-vrcam PRIVATE vr/shoulder_swap_wire.cpp)   # [hands] shoulder_swap (wired to vr/direct_input.h)
target_sources(d2rl-vrcam PRIVATE vr/weight_wire.cpp)   # [weight]: a grip on the weapon is not a chord's modifier (wired to vr/direct_input.h)
target_link_libraries(d2rl-vrcam PRIVATE d2rcam heft minhook user32 gdi32 ole32 uuid OpenXR::openxr_loader)
target_compile_options(d2rl-vrcam PRIVATE /utf-8 /W3 /EHsc /permissive-)
# [gameplay] shield_smite: a shield bash casts the game's own Smite (vr/shield_smite.h decides, shield_smite.cpp talks to the game).
target_sources(d2rl-vrcam PRIVATE vr/shield_smite.cpp)
add_executable(shield_smite_test cleanroom/tests/shield_smite_test.cpp)
target_include_directories(shield_smite_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(shield_smite_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hands] shield_fist (vr/shield_fist.h: the hand with a shield closes with the grip) and the weight model's contract for that hand.
add_executable(shield_fist_test cleanroom/tests/shield_fist_test.cpp)
target_link_libraries(shield_fist_test PRIVATE heft)
target_compile_options(shield_fist_test PRIVATE /utf-8 /W4 /EHsc /permissive-)

# [debug] melee_boxes: the capsules hit by touch tests, drawn in the headset (vr/melee_boxes.h pure, melee_boxes.cpp its state). The test
# checks the list, the frame conversion to the shader's numbers (a CPU copy of the effect's projection) and the off state; with --dump
# <file.png> it draws a synthetic scene.
target_sources(d2rl-vrcam PRIVATE vr/melee_boxes.cpp)
add_executable(melee_boxes_test cleanroom/tests/melee_boxes_test.cpp vr/melee_boxes.cpp)
target_include_directories(melee_boxes_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(melee_boxes_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The speed marks of melee_boxes (vr/melee_marks.h, pure): where the engine's "a contact would count" flag turned on and off, the tip's trail,
# the places they share with the monsters, and [debug] melee_mark_ms.
add_executable(melee_marks_test cleanroom/tests/melee_marks_test.cpp vr/melee_boxes.cpp)
target_include_directories(melee_marks_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(melee_marks_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The touch's blade built from the weapon as drawn (vr/blade_shape.h, pure): the shape by kind, the live tuning, the dual-wield rule and the
# frame of the red line, with zero sideways offset against the weapon bone.
add_executable(blade_shape_test cleanroom/tests/blade_shape_test.cpp)
target_include_directories(blade_shape_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(blade_shape_test PRIVATE /utf-8 /W4 /EHsc /permissive-)

# Direct input ([openxr] direct_input, vr/direct_input.h): the layout, the release gates, the ini's [direct] table and the head and
# hand maths, offline - no game, no OpenXR, no headset. Run: direct_input_test.exe
add_executable(direct_input_test cleanroom/tests/direct_input_test.cpp vr/direct_input.cpp)
target_include_directories(direct_input_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr ${BODYWALK_SDK_INCLUDE})
target_compile_options(direct_input_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The game must never think its pad left (vr/pad_presence.h): when pad 0 is answered present, and what the log says.
add_executable(pad_presence_test cleanroom/tests/pad_presence_test.cpp)
target_include_directories(pad_presence_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(pad_presence_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Attacks that never walk the hero ([gameplay] attack_in_place, vr/stand_still.h): which presses, and the wait for the game's Stand Still.
add_executable(stand_still_test cleanroom/tests/stand_still_test.cpp)
target_include_directories(stand_still_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(stand_still_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# What the game does with a shot of ours, told in the log ([debug] shot_trace, vr/shot_trace.h): the window of a shot and the texts.
add_executable(shot_trace_test cleanroom/tests/shot_trace_test.cpp)
target_include_directories(shot_trace_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(shot_trace_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# No stray walk, no stray mouse button in the F4 world ([gameplay] block_stray_walk / block_stray_clicks, vr/stray_walk.h): the decisions.
add_executable(stray_walk_test cleanroom/tests/stray_walk_test.cpp)
target_include_directories(stray_walk_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(stray_walk_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# In which views the aiming hand may direct the hero's attack (vr/attack_view.h): VR F2 and F4 look where the player looks, VR F1 and F3 (top-down) keep the hero's own facing.
add_executable(attack_view_test cleanroom/tests/attack_view_test.cpp)
target_include_directories(attack_view_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(attack_view_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# BodyWalk's walking through the plugin into the game ([openxr] direct_bodywalk_walk, vr/walk_path.h): a fake pad in, the game's stick and
# command out, in direct mode and not, with and without the stray-walk guard.
add_executable(walk_path_test cleanroom/tests/walk_path_test.cpp)
target_include_directories(walk_path_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(walk_path_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Walking with the controller's left stick in VR F1, F2, F3 without BodyWalk ([direct] walk_flat_views, vr/walk_flat.h): the verdict, the
# stick as a pad stick, the log, and the view gate's flat-views predicate.
add_executable(walk_flat_test cleanroom/tests/walk_flat_test.cpp)
target_include_directories(walk_flat_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(walk_flat_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Belt potions in VR F1, F2, F3 ([direct] belt_flick_flat_views, vr/belt_flick.h): the left trigger held and a flick of a thumbstick - the directions,
# one potion per flick, the middle in between, the trigger, both sticks, a diagonal.
add_executable(belt_flick_test cleanroom/tests/belt_flick_test.cpp)
target_include_directories(belt_flick_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(belt_flick_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The settings panel in the headset ([openxr] headset_settings, vr/hset_*.h, vr/xr_headset_settings.h): its model,
# the chord and the lasers, the ini transaction (backup, conflict) and the picture, tested without a headset.
add_executable(hset_test tools/hset_test.cpp vr/hset_paint.cpp vr/hset_store.cpp shared/ini_txn.cpp)
target_include_directories(hset_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_link_libraries(hset_test PRIVATE user32 gdi32)
target_compile_options(hset_test PRIVATE /utf-8 /W4 /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS)
# The T-pose measurement of the eye height and arm span (vr/body_calibration.h is its pure detector, tested below)
target_sources(d2rl-vrcam PRIVATE vr/calibration.cpp vr/calibration_panel.cpp vr/calibration_input.cpp)
# Controller haptics ([openxr] haptics): the mailbox, the hit observer and the flush; the logic is vr/haptics_pattern.h, tested without a headset.
target_sources(d2rl-vrcam PRIVATE vr/haptics.cpp)
add_executable(haptics_test cleanroom/tests/haptics_test.cpp)
target_include_directories(haptics_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(haptics_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The one predicate behind "F4 only" (vr/view_gate.h), and the pulses' use of it (haptics.cpp itself, the output played by the test).
# Our own tests: not in the public tree.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/view_gate_test.cpp")
    add_executable(view_gate_test cleanroom/tests/view_gate_test.cpp)
    target_include_directories(view_gate_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(view_gate_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/haptics_view_test.cpp")
    add_executable(haptics_view_test cleanroom/tests/haptics_view_test.cpp vr/haptics.cpp vr/direct_input.cpp)
    target_include_directories(haptics_view_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared ${BODYWALK_SDK_INCLUDE})
    target_link_libraries(haptics_view_test PRIVATE d2rcam)
    target_compile_options(haptics_view_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# Native OpenXR, F1 and F3 in real 3D ([openxr] views_3d, vr/views3d.h): the window's frusta, its anchor, the diorama's map.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/views3d_test.cpp")
    add_executable(views3d_test cleanroom/tests/views3d_test.cpp)
    target_include_directories(views3d_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(views3d_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# The laser as the game's mouse in F1 and F3 in 3D (vr/pointer3d.h): the ray against the game's ground and the pixel the game's own ScreenToRay turns back.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/pointer3d_test.cpp")
    add_executable(pointer3d_test cleanroom/tests/pointer3d_test.cpp)
    target_include_directories(pointer3d_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/camera)
    target_compile_options(pointer3d_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# The press -> hit time of hit by touch and the shield's Smite (vr/hit_latency.h): the pairing and the statistics line, on made-up clocks.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/hit_latency_test.cpp")
    add_executable(hit_latency_test cleanroom/tests/hit_latency_test.cpp)
    target_include_directories(hit_latency_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(hit_latency_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# [debug] frame_trace (vr/frame_trace.h): where the time of a slow pair went and what part of it is the plugin's - the meter, the
# probes, the situation of a pair (idle, walking, swinging, a hit) and the lines, on made-up pairs.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/frame_trace_test.cpp")
    add_executable(frame_trace_test cleanroom/tests/frame_trace_test.cpp)
    target_include_directories(frame_trace_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(frame_trace_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# tools/xinput_bench.cpp: times the Xbox driver's answers to the calls the game makes (XInputGetState on four pads, XInputSetState for its rumble) on
# a machine without the game or the plugin - for a stutter that might be the pad's driver. Not a test (it needs the machine's pads, so it is not named *_test).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/xinput_bench.cpp")
    add_executable(xinput_bench tools/xinput_bench.cpp)
    target_compile_options(xinput_bench PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# [debug] log_async (vr/log_queue.h): the plugin's log lines through a ring and a writer thread - the ring, the pump, the order, the
# lines a full ring refuses, the crash drain's waiting, on a made-up sink.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/log_queue_test.cpp")
    add_executable(log_queue_test cleanroom/tests/log_queue_test.cpp)
    target_include_directories(log_queue_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(log_queue_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# [hud] ordinary_mob_bars (vr/mob_bars.h): the plate of an ordinary monster out. Its logic is pure and has a test
# against a fake game (mob_bars_test); the hook is vr/mob_bars.cpp.
target_sources(d2rl-vrcam PRIVATE vr/mob_bars.cpp)
add_executable(mob_bars_test cleanroom/tests/mob_bars_test.cpp)
target_compile_options(mob_bars_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dump_loader/D2R_image.bin")
    # The hook on the game's snapshot, without the game: mob_bars_hook_test dump_loader/D2R_image.bin
    add_executable(mob_bars_hook_test cleanroom/tests/mob_bars_hook_test.cpp vr/mob_bars.cpp)
    target_link_libraries(mob_bars_hook_test PRIVATE d2rcam)
    target_compile_options(mob_bars_hook_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# [hud] low_hp_vignette (vr/low_hp.h): the low-life heartbeat vignette. Its logic is pure (the beat, the fade, the hysteresis, the
# life maths, the vignette's shape) and tested; low_hp_test also compiles the effect's own HLSL (reshade/D2R_DepthFog.fx, LowHp) and
# runs it on WARP against the C++ mirror, and draws pictures of the vignette. Run: low_hp_test.exe [<folder for the pictures>]
target_sources(d2rl-vrcam PRIVATE vr/low_hp.cpp)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/low_hp_test.cpp")   # (not in the public tree's allow-list)
    add_executable(low_hp_test cleanroom/tests/low_hp_test.cpp)
    target_include_directories(low_hp_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_definitions(low_hp_test PRIVATE LOWHP_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
    target_link_libraries(low_hp_test PRIVATE d3d11 d3dcompiler)
    target_compile_options(low_hp_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# [hud] hand_vitals (vr/hand_vitals.h): life and mana on the hand when the palm turns to the face. The palm test, the fade, the panel's pose
# and the numbers' maths are pure and tested (hand_vitals_test, which also draws the panel at several levels: hand_vitals_test.exe
# [<folder for the pictures>]); the wiring - the frames from direct_input, the painter thread, the layer asked for - is tested against
# stand-ins for direct_input and the game's read (hand_vitals_wire_test).
target_sources(d2rl-vrcam PRIVATE vr/hand_vitals_wire.cpp vr/hand_vitals_game.cpp vr/hand_vitals_paint.cpp)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/hand_vitals_test.cpp")   # (not in the public tree's allow-list)
    add_executable(hand_vitals_test cleanroom/tests/hand_vitals_test.cpp vr/hand_vitals_paint.cpp)
    target_include_directories(hand_vitals_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_definitions(hand_vitals_test PRIVATE HANDVITALS_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vr")   # (the test scans the panel's files)
    target_link_libraries(hand_vitals_test PRIVATE gdi32 user32)
    target_compile_options(hand_vitals_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [hud] hand_vitals_style orbs (vr/hand_orbs.h): life and mana as 3D balls on the wrists, ray-cast by the effect (the ORBS block of
# reshade/D2R_DepthFog.fx). The placement, the eased levels, the heartbeat and the uniforms are pure and tested; the test also compiles the
# effect's own HLSL and runs it on WARP over a synthetic scene (a forearm and a wall as the game's depth) and draws pictures of the balls.
# Run: hand_orbs_test.exe [<folder for the pictures>]
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/hand_orbs_test.cpp")   # (not in the public tree's allow-list)
    add_executable(hand_orbs_test cleanroom/tests/hand_orbs_test.cpp)
    target_include_directories(hand_orbs_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_definitions(hand_orbs_test PRIVATE ORBS_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
    target_link_libraries(hand_orbs_test PRIVATE d3d11 d3dcompiler)
    target_compile_options(hand_orbs_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [hud] hide = 1 (vr/hud_rects.h): the toolbar's strip in the game's interface layer, with the hide_up_pct / hide_down_pct sliders. Pure and tested.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/hud_rects_test.cpp")   # (not in the public tree's allow-list)
    add_executable(hud_rects_test cleanroom/tests/hud_rects_test.cpp)
    target_include_directories(hud_rects_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(hud_rects_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/hand_vitals_wire_test.cpp")
    add_executable(hand_vitals_wire_test cleanroom/tests/hand_vitals_wire_test.cpp vr/hand_vitals_wire.cpp vr/hand_vitals_paint.cpp)
    target_include_directories(hand_vitals_wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(hand_vitals_wire_test PRIVATE D2RLPlugin::D2RLPlugin gdi32 user32)
    target_compile_options(hand_vitals_wire_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# The skill wheel ([openxr] skill_wheel, vr/wheel_*.h): the wedges' hit test, the bank logic against a fake game, the model, the trigger's
# cast and the controller on synthetic frames, offline - no game, no OpenXR, no headset. Run: skill_wheel_test.exe
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/skill_wheel_test.cpp")   # (not in the public tree's allow-list)
    add_executable(skill_wheel_test cleanroom/tests/skill_wheel_test.cpp vr/wheel_paint.cpp vr/tiles_paint.cpp vr/wheel_icons.cpp)
    target_include_directories(skill_wheel_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(skill_wheel_test PRIVATE gdi32 user32 ole32 uuid)
    target_compile_options(skill_wheel_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# The skill panel's tab strip and its Settings tab ([openxr] headset_settings with the tiles' look: vr/tiles_geom.h's strip and sheet, vr/wheel_model.h's
# tabs, vr/wheel_controller.h hosting vr/hset_*.h's pages): the strip and sheet geometry, the tabs, the chord, the long press, the pages' clicks, Apply, the
# view buttons, Calibrate body and the window's return, the slider's drag, and the window's pictures. Run: headset_tabs_test.exe [--dump <folder>]
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/headset_tabs_test.cpp")   # (not in the public tree's allow-list)
    add_executable(headset_tabs_test cleanroom/tests/headset_tabs_test.cpp vr/wheel_paint.cpp vr/tiles_paint.cpp vr/wheel_icons.cpp)
    target_include_directories(headset_tabs_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(headset_tabs_test PRIVATE gdi32 user32 ole32 uuid)
    target_compile_options(headset_tabs_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# BodyWalk's gesture runes in the headset ([openxr] runes_draw, vr/rune_geom.h, rune_shader.h, rune_mirror.h; vr/xr_runes.h hangs them):
# the maths (colours, lit segments, panel, trail projected onto a billboard facing the head, layer budget, ease-out), the shader's HLSL
# compiled and run on WARP against its CPU mirror, and pictures of it (--dump <folder>). Offline: no game, no OpenXR, no headset, no BodyWalk.
# Run: rune_test.exe [--dump <folder>]. rune_link_test: vr/link.cpp's VrRunes against a fake writer of the shared block (stale, version, F4).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/rune_test.cpp")   # (not in the public tree's allow-list)
    add_executable(rune_test cleanroom/tests/rune_test.cpp)
    target_include_directories(rune_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${BODYWALK_SDK_INCLUDE})
    target_link_libraries(rune_test PRIVATE d3d11 d3dcompiler)
    target_compile_options(rune_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/rune_link_test.cpp")
    add_executable(rune_link_test cleanroom/tests/rune_link_test.cpp vr/link.cpp vr/direct_input.cpp)
    target_include_directories(rune_link_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr ${BODYWALK_SDK_INCLUDE} ${BODYWALK_SRC})
    target_compile_options(rune_link_test PRIVATE /utf-8 /W3 /EHsc)
endif()

# A stand-in for the gesture engine to look at runes_draw in the headset without BodyWalk (tools/rune_fake_writer.cpp, header comment): it writes
# the shared block at 60 Hz - a rune that completes and one that strays, the trail scripted or on your right controller (--hand).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/rune_fake_writer.cpp")
    add_executable(rune_fake_writer tools/rune_fake_writer.cpp)
    target_include_directories(rune_fake_writer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${BODYWALK_SDK_INCLUDE})
    target_compile_options(rune_fake_writer PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# The skill panel's second look, TILES ([openxr] skill_panel_style=1, vr/tiles_*.h, vr/tiles_paint.cpp): the hit test on synthetic rays, both
# layouts, the button labels per controller profile, a change of look while the panel is up, and the window's picture (--dump <folder>
# writes it as BMP files to look at). Offline: no game, no OpenXR, no headset. Run: skill_tiles_test.exe
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/skill_tiles_test.cpp")   # (not in the public tree's allow-list)
    add_executable(skill_tiles_test cleanroom/tests/skill_tiles_test.cpp vr/wheel_paint.cpp vr/tiles_paint.cpp vr/wheel_icons.cpp)
    target_include_directories(skill_tiles_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(skill_tiles_test PRIVATE gdi32 user32 ole32 uuid)
    target_compile_options(skill_tiles_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# Sliders for d2r_vr.ini, dropped next to it in <game>\d2rloader\plugins.
# Its version (Home page, update check) is vrcam's g_info_version: d2r_vr_version.h is made from
# vr/vrcam.cpp now and again whenever it changes (rewritten only when the number does).
set(D2RVR_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(D2RVR_VERSION_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tools/settings/version.cmake")
set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/vr/vrcam.cpp")
set(OUT "${D2RVR_GEN_DIR}/d2r_vr_version.h")
set(STAMP "")
include(${D2RVR_VERSION_SCRIPT})   # at configure time too: the header is there before the first build
unset(SRC)
unset(OUT)
unset(STAMP)
add_custom_command(OUTPUT "${D2RVR_GEN_DIR}/d2r_vr_version.stamp"
    BYPRODUCTS "${D2RVR_GEN_DIR}/d2r_vr_version.h"
    COMMAND ${CMAKE_COMMAND} "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}/vr/vrcam.cpp" "-DOUT=${D2RVR_GEN_DIR}/d2r_vr_version.h"
            "-DSTAMP=${D2RVR_GEN_DIR}/d2r_vr_version.stamp" -P "${D2RVR_VERSION_SCRIPT}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vr/vrcam.cpp" "${D2RVR_VERSION_SCRIPT}"
    COMMENT "D2R VR version from vrcam.cpp" VERBATIM)
add_executable(d2r_vr_settings WIN32 tools/settings/settings.cpp tools/settings/settings.rc "${D2RVR_GEN_DIR}/d2r_vr_version.stamp")
target_compile_options(d2r_vr_settings PRIVATE /utf-8 /W3 /EHsc)
# The file players see next to D2R.exe: capitals, like the game's own (2026-10-06). Its window state stays in
# d2r_vr_settings.ini.
set_target_properties(d2r_vr_settings PROPERTIES OUTPUT_NAME "D2R_VR_Settings")
target_compile_definitions(d2r_vr_settings PRIVATE UNICODE _UNICODE)
target_include_directories(d2r_vr_settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${FLATVR_SHARED_DIR} ${D2RVR_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/sigscan ${BODYWALK_SDK_INCLUDE})   # d2r_vr_state.h: the weapon in hand; afr_eye_shared.h: is FlatVR running; game_link.h: BodyWalk
# Our own debugging tools (not in the public tree).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/probe_test.cpp")
    # The skeleton probe against a fake Granny skeleton in its own memory.
    add_executable(probe_test tools/probe_test.cpp vr/probe.cpp)
    target_compile_options(probe_test PRIVATE /utf-8 /W3 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/aim_point_test.cpp")
    # The aim point of a cast along the hand and the hand's grid direction (vr/aim_point.h) over all headings.
    add_executable(aim_point_test tools/aim_point_test.cpp)
    target_include_directories(aim_point_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(aim_point_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/window_json_test.cpp")
    # The game's windowed size in Settings.json and what each platform does to it (shared/window_json.h): flat mode has no square window.
    add_executable(window_json_test tools/window_json_test.cpp)
    target_include_directories(window_json_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(window_json_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/vr_resolution_test.cpp")
    # [render] vr_resolution (vr/vr_resolution.h, render_window.h): size selection, when to ask, the pointer's map - fake windows, no game.
    add_executable(vr_resolution_test tools/vr_resolution_test.cpp)
    target_include_directories(vr_resolution_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(vr_resolution_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # The same against the real user32 calls (vr/render_resolution.cpp's hooks) on a window of the test's own.
    add_executable(render_pointer_test tools/render_pointer_test.cpp vr/render_resolution.cpp)
    target_include_directories(render_pointer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(render_pointer_test PRIVATE minhook user32)
    target_compile_options(render_pointer_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/game_link_test.cpp")
    # BodyWalk Game Link end to end: BodyWalk's game_link.dll and vr/link.cpp in one process.
    add_executable(game_link_test tools/game_link_test.cpp vr/link.cpp vr/direct_input.cpp)
    target_include_directories(game_link_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr ${BODYWALK_SDK_INCLUDE} ${BODYWALK_SRC})   # plugin_api.h: BodyWalk's
    target_compile_options(game_link_test PRIVATE /utf-8 /W3 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/xr_hardening_test.cpp")
    # Native OpenXR's hardening helpers (vr/xr_layers.h and the like) on synthetic data, no headset: xr_hardening_test
    add_executable(xr_hardening_test tools/xr_hardening_test.cpp)
    target_include_directories(xr_hardening_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(xr_hardening_test PRIVATE OpenXR::headers)
    target_compile_options(xr_hardening_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# The light-bounds replacement shader on a software D3D11 device against a scan of every pixel; with the game's own
# shader (extracted on your machine) as the argument it also shows the bug and that square tiles are the same bit for bit.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/light_bounds_test.cpp")
    add_executable(light_bounds_test cleanroom/tests/light_bounds_test.cpp)
    target_link_libraries(light_bounds_test PRIVATE d3d11 d3dcompiler)
    target_compile_options(light_bounds_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/light_bounds_reshade_test.cpp")
    # The same through the real ReShade on the GPU, with the game's shader (extracted on your machine) as its pipeline:
    # light_bounds_reshade_test <container> <ReShade64.dll>. Our own debugging tool (not in the public tree).
    add_executable(light_bounds_reshade_test tools/light_bounds_reshade_test.cpp vr/light_bounds.cpp)
    target_include_directories(light_bounds_reshade_test PRIVATE ${RESHADE_SDK_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(light_bounds_reshade_test PRIVATE d3d12 dxgi)
    target_compile_options(light_bounds_reshade_test PRIVATE /utf-8 /W3 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/laser_tooltip_test.cpp")
    # The laser's item tooltips at once, the pure part (vr/laser_tooltip.h): synthetic frames, no game.
    add_executable(laser_tooltip_test tools/laser_tooltip_test.cpp)
    target_compile_options(laser_tooltip_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dump_loader/D2R_image.bin")
    # The address resolver on the game's snapshot: sigscan_test dump_loader/D2R_image.bin
    add_executable(sigscan_test cleanroom/tests/sigscan_test.cpp)
    target_link_libraries(sigscan_test PRIVATE d2rcam)
    target_compile_options(sigscan_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # The memory pools grown, against the game's own TLSF code: pools_test dump_loader/D2R_image.bin
    add_executable(pools_test cleanroom/tests/pools_test.cpp)
    target_link_libraries(pools_test PRIVATE d2rcam)
    target_compile_options(pools_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/heapdiff/heapdiff.cpp")
    add_executable(heapdiff tools/heapdiff/heapdiff.cpp)   # find a value in the running game's heap by changing it
    target_compile_options(heapdiff PRIVATE /utf-8 /W3 /EHsc)
    target_link_libraries(heapdiff PRIVATE psapi)
endif()

# The bow draw gesture (vr/bow_draw.h): pure logic, tested on synthetic poses. bow_draw_test
add_executable(bow_draw_test cleanroom/tests/bow_draw_test.cpp)
target_compile_options(bow_draw_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# ... and its wiring (vr/bow_draw_wire.cpp) fed direct_input.h's frames, with stand-ins for Listen / Take / Action / PadPulse, the weapon and the
# skeleton's feed - and no BodyWalk (vr/link.cpp is not linked): bow_draw_wire_test
add_executable(bow_draw_wire_test cleanroom/tests/bow_draw_wire_test.cpp vr/bow_draw_wire.cpp)
target_link_libraries(bow_draw_wire_test PRIVATE D2RLPlugin::D2RLPlugin)
target_include_directories(bow_draw_wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${FLATVR_SHARED_DIR} ${BODYWALK_SRC} ${BODYWALK_SDK_INCLUDE})
target_compile_definitions(bow_draw_wire_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(bow_draw_wire_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The bow's touch point and debug vectors (vr/bow_touch.h): bow_touch_test
add_executable(bow_touch_test cleanroom/tests/bow_touch_test.cpp)
target_compile_options(bow_touch_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The string hand's closing (vr/bow_hand.h): what asks the fingers to close, the amount, the easing: bow_hand_test
add_executable(bow_hand_test cleanroom/tests/bow_hand_test.cpp)
target_compile_options(bow_hand_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [debug] bow_latency (vr/bow_latency.h): the split of the second between a bow's release and its arrow: bow_latency_test
add_executable(bow_latency_test cleanroom/tests/bow_latency_test.cpp)
target_compile_options(bow_latency_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
target_sources(d2rl-vrcam PRIVATE vr/bow_draw_wire.cpp)   # [hands] bow_draw (wired to vr/direct_input.h)
target_sources(d2rl-vrcam PRIVATE vr/anim_cut.cpp)        # [bow] quick_release and [hands] quick_throw (the action-frame flag of the hero's bow attack and throw rows; the pure part vr/anim_cut.h)
# ... its pure part: the row's shape check, the move and the way back, the walk over a block (anim_cut_test)
add_executable(anim_cut_test cleanroom/tests/anim_cut_test.cpp)
target_compile_options(anim_cut_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/body_calibration_test.cpp")
    # The T-pose measurement (vr/body_calibration.h) and the ini patch it saves through (vr/ini_patch.h), on synthetic poses
    add_executable(body_calibration_test cleanroom/tests/body_calibration_test.cpp)
    target_include_directories(body_calibration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(body_calibration_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # ... and the measurement end to end on synthetic frames and a real ini in a temporary folder (vr/calibration.cpp)
    add_executable(calibration_test cleanroom/tests/calibration_test.cpp vr/calibration.cpp)
    target_include_directories(calibration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(calibration_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
