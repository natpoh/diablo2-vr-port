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

# ReShade's effect compiler (reshadefx, BSD-3-Clause, Patrick Mours; crosire/reshade 18deaa5, third_party/reshadefx/README.md): [render] no_reshade
# compiles reshade/D2R_DepthFog.fx itself and runs it on its own D3D12 (vr/fx_native.cpp) - HLSL from the effect, D3DCompile per entry point
set(RESHADEFX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/reshadefx")
add_library(reshadefx STATIC
    ${RESHADEFX_DIR}/effect_codegen_hlsl.cpp ${RESHADEFX_DIR}/effect_expression.cpp ${RESHADEFX_DIR}/effect_lexer.cpp
    ${RESHADEFX_DIR}/effect_parser_exp.cpp ${RESHADEFX_DIR}/effect_parser_stmt.cpp ${RESHADEFX_DIR}/effect_preprocessor.cpp
    ${RESHADEFX_DIR}/effect_symbol_table.cpp)
target_include_directories(reshadefx PUBLIC ${RESHADEFX_DIR})
target_compile_definitions(reshadefx PRIVATE _UNICODE UNICODE NOMINMAX)
target_compile_options(reshadefx PRIVATE /utf-8 /Zc:char8_t- /EHsc /W0)
# (D3DCompile is its user's: vr/fx_native.cpp finds it at run time, the plugin imports no d3dcompiler_47.dll)

# Our camera, render distance and memory pools, written clean-room from the game (cleanroom/).
add_library(d2rcam STATIC cleanroom/camera/d2rcam.cpp cleanroom/drawdist/drawdist.cpp cleanroom/drawdist/unleashed_big_objects.cpp cleanroom/pools/pools.cpp cleanroom/sigscan/sigscan.cpp)
target_include_directories(d2rcam PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/camera ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/sigscan)
target_link_libraries(d2rcam PUBLIC D2RLPlugin::D2RLPlugin)
target_compile_definitions(d2rcam PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(d2rcam PRIVATE /utf-8 /W4 /EHsc /permissive-)

# The bow's own rig drawn by the pull (vr/bow_rig.h): pure math, tested on a synthetic bow.
add_executable(bow_rig_test cleanroom/tests/bow_rig_test.cpp)
# [bow] arrow_fx (vr/granny_mesh.h): the arrow mesh's box off the game's Granny model, on synthetic memory laid out as the game's: granny_mesh_test
add_executable(granny_mesh_test cleanroom/tests/granny_mesh_test.cpp)
target_include_directories(granny_mesh_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(granny_mesh_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
# A flail's head swinging free on its chain (vr/flail_chain.h): the chains of the three flail skeletons, the pendulum, the pose, the gate.
add_executable(flail_chain_test cleanroom/tests/flail_chain_test.cpp)
target_include_directories(flail_chain_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(flail_chain_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The Necromancer's head hanging from its cord (vr/head_hang.h): where the five head models hang from, the pendulum and its twist, the frame hook.
add_executable(head_hang_test cleanroom/tests/head_hang_test.cpp)
target_include_directories(head_hang_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(head_hang_test PRIVATE /utf-8 /W4 /EHsc /permissive-)

# A held weapon's weight (cleanroom/heft, docs/WEAPON_WEIGHT.md): the model, its
# synthetic tests, and heft_bench.dll for the Unity bench (tools/heft_bench_unity); hold.cpp,
# the hands that take it, is in the mod too (vr/weight.cpp).
add_library(heft STATIC cleanroom/heft/heft.cpp cleanroom/heft/hold.cpp cleanroom/heft/contact.cpp cleanroom/heft/stab.cpp)
target_include_directories(heft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/heft)
target_compile_options(heft PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_library(heft_benchdefs STATIC cleanroom/heft/bench.cpp)
target_link_libraries(heft_benchdefs PUBLIC heft)
target_compile_options(heft_benchdefs PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(heft_test cleanroom/tests/heft_test.cpp)
target_link_libraries(heft_test PRIVATE heft_benchdefs)
target_compile_options(heft_test PRIVATE /utf-8 /W4 /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS)
# [weight] monster_bodies: the held weapon against the monsters' bodies (cleanroom/heft/contact.h, vr/monster_bodies.h) on synthetic poses.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/monster_bodies_test.cpp")
    add_executable(monster_bodies_test cleanroom/tests/monster_bodies_test.cpp)
    target_link_libraries(monster_bodies_test PRIVATE heft)
    target_include_directories(monster_bodies_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(monster_bodies_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/strike_measure_test.cpp")   # ... what a strike delivers into them ([debug] strike_measure, vr/strike_measure.h)
    add_executable(strike_measure_test cleanroom/tests/strike_measure_test.cpp)
    target_link_libraries(strike_measure_test PRIVATE heft)
    target_include_directories(strike_measure_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(strike_measure_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # [weight] cleave: a strong strike through the bodies into the next; and a body stopping the weapon never makes a hand let go
    add_executable(cleave_test cleanroom/tests/cleave_test.cpp)
    target_link_libraries(cleave_test PRIVATE heft)
    target_include_directories(cleave_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(cleave_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/blade_push_test.cpp")   # ... and a weapon in each hand, or in the left alone, pushing them (vr/blade_push.h)
    add_executable(blade_push_test cleanroom/tests/blade_push_test.cpp)
    target_include_directories(blade_push_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(blade_push_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/stab_test.cpp")   # ... a thrusting weapon sticking into them ([weight] stab_in, cleanroom/heft/stab.h)
    # (the stab itself is the weight library's: `stab_test dll=<path>` runs it through the mod's loader)
    add_executable(stab_test cleanroom/tests/stab_test.cpp vr/weight_lib.cpp)
    target_link_libraries(stab_test PRIVATE heft)
    target_include_directories(stab_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(stab_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/bone_segments_test.cpp")   # [weight] monster_ragdoll_all: bodies from bone segments, no-limb names, the corpse watch, the eased pose
    add_executable(bone_segments_test cleanroom/tests/bone_segments_test.cpp)
    target_include_directories(bone_segments_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(bone_segments_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/doll_pose_test.cpp")   # [weight] monster_ragdoll_pose: the doll's offsets as turns of the monster's own bones (vr/doll_pose.h)
    add_executable(doll_pose_test cleanroom/tests/doll_pose_test.cpp)
    target_compile_options(doll_pose_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/doll_view_test.cpp")   # [weight] monster_ragdoll (probe): what goes to the doll library and what is drawn of it (vr/doll_view.h)
    add_executable(doll_view_test cleanroom/tests/doll_view_test.cpp)
    target_include_directories(doll_view_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(doll_view_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/monster_humanoid_test.cpp")   # ... on humanoids only, judged from the skeleton (vr/monster_humanoid.h)
    add_executable(monster_humanoid_test cleanroom/tests/monster_humanoid_test.cpp)
    target_include_directories(monster_humanoid_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(monster_humanoid_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/monster_rig_test.cpp")   # ... their bodies from their own skeletons (vr/monster_rig.h) on a made-up game memory
    add_executable(monster_rig_test cleanroom/tests/monster_rig_test.cpp)
    target_link_libraries(monster_rig_test PRIVATE heft)
    target_include_directories(monster_rig_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(monster_rig_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/quiver_test.cpp")   # the quiver: what a thrown weapon allows, the list offered, the cursor (vr/quiver.h)
    add_executable(quiver_test cleanroom/tests/quiver_test.cpp)
    target_compile_options(quiver_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/quiver_ctl_test.cpp")   # the quiver's hands behind the shoulder, taps and steps (vr/quiver_ctl.h)
    add_executable(quiver_ctl_test cleanroom/tests/quiver_ctl_test.cpp)
    target_compile_options(quiver_ctl_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/throw_cast_test.cpp")   # which command a throw presses and what the game answered (vr/throw_cast.h)
    add_executable(throw_cast_test cleanroom/tests/throw_cast_test.cpp)
    target_compile_options(throw_cast_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/throw_born_test.cpp")   # [hands] throw_direct: the potion born where the model lands (vr/throw_born.h, pure)
    add_executable(throw_born_test cleanroom/tests/throw_born_test.cpp)
    target_compile_options(throw_born_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/grab_bodies_test.cpp")   # [weight] grab_bodies: the glue to the library's grab (vr/grab_bodies.h, vr/grab_lib.cpp; dll=<d2r_vr_weight.dll> runs the library's grab through the loader)
    add_executable(grab_bodies_test cleanroom/tests/grab_bodies_test.cpp vr/grab_lib.cpp)
    target_include_directories(grab_bodies_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(grab_bodies_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/grab_priority_test.cpp")   # [hands] grab_weapon_first: the weapon before a belt potion (vr/grab_priority.h, pure)
    add_executable(grab_priority_test cleanroom/tests/grab_priority_test.cpp)
    target_compile_options(grab_priority_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/potion_hide_test.cpp")   # [hands] potion_hide_game: the game's own thrown potion not drawn (vr/potion_hide.h, pure)
    add_executable(potion_hide_test cleanroom/tests/potion_hide_test.cpp)
    target_compile_options(potion_hide_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/head_shadow_test.cpp")   # [arms] head_shadow: the head shrunk in the eyes' skinning, whole in the shadows' (vr/head_shadow.h, pure)
    add_executable(head_shadow_test cleanroom/tests/head_shadow_test.cpp)
    target_compile_options(head_shadow_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/own_effects_test.cpp")   # [render] dim_own_effects: the hero's own effects fainter (vr/own_effects.h, pure; the table is generated from the game's tables)
    add_executable(own_effects_test cleanroom/tests/own_effects_test.cpp)
    target_compile_options(own_effects_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
# Settings' "Direct hit" selector over melee_direct / melee_direct_client (shared/direct_hit_mode.h), pure.
add_executable(direct_hit_mode_test cleanroom/tests/direct_hit_mode_test.cpp)
target_include_directories(direct_hit_mode_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(direct_hit_mode_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [gameplay] melee_direct_client (vr/melee_direct_client.h): a joined player's route, the role and the units' verdict, pure.
add_executable(melee_direct_client_test cleanroom/tests/melee_direct_client_test.cpp)
target_include_directories(melee_direct_client_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(melee_direct_client_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(melee_touch_test cleanroom/tests/melee_touch_test.cpp)
target_include_directories(melee_touch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(melee_touch_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Hit by touch: the upswing of a blunt weapon does not count (vr/strike_direction.h), and the engine's two numbers per weapon kind - on synthetic swings, with the real engine.
# [weight] parry_*: a monster's attack and the hero's block by their modes, the clash once per meeting, the pulse, the knock (vr/parry.h).
# [weight] strike_by_force: the force judge (vr/strike_force.h) - thresholds, the cut, no hand, a shared blow, the stuck repeat, and the author's recorded blows.
add_executable(strike_force_test cleanroom/tests/strike_force_test.cpp)
target_include_directories(strike_force_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(strike_force_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(parry_test cleanroom/tests/parry_test.cpp)
target_include_directories(parry_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(parry_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(strike_direction_test cleanroom/tests/strike_direction_test.cpp)
target_include_directories(strike_direction_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(strike_direction_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Hit by touch: the thrust (vr/thrust_stroke.h) - a poke along the blade with its own speed and distance, the game's weapon class, both engines on synthetic pokes and swings.
add_executable(thrust_stroke_test cleanroom/tests/thrust_stroke_test.cpp)
target_include_directories(thrust_stroke_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(thrust_stroke_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [debug] thrust_debug: the readout's picture (vr/thrust_debug_paint.cpp) from the readings - green where a line passes, red where it fails; --dump <file.bmp> to look at it.
add_executable(thrust_debug_paint_test cleanroom/tests/thrust_debug_paint_test.cpp vr/thrust_debug_paint.cpp vr/wheel_icons.cpp)
target_include_directories(thrust_debug_paint_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr ${BODYWALK_SDK_INCLUDE})
target_link_libraries(thrust_debug_paint_test PRIVATE gdi32 user32 ole32 uuid)
target_compile_options(thrust_debug_paint_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Hit by touch with a thrown weapon (vr/thrown_touch.h, pure): the grip's held state and the rules that keep the touch and the throw by gesture
# from both firing for one swing.
add_executable(thrown_touch_test cleanroom/tests/thrown_touch_test.cpp)
target_include_directories(thrown_touch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(thrown_touch_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Which controller throws a thrown weapon ([hands] throw_left_hand, vr/throw_hand.h, pure): the right as ever, or the hand that holds it.
add_executable(throw_hand_test cleanroom/tests/throw_hand_test.cpp)
target_compile_options(throw_hand_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
# The crossbow's support hand (vr/xbow_grip.h): the grip latch, the wide grip capture, the barrel from the model's bolt_helper, pure.
add_executable(xbow_grip_test cleanroom/tests/xbow_grip_test.cpp)
target_compile_options(xbow_grip_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hands] spell_aim_model (vr/spell_aim_model.h): which part of the hero as drawn aims a spell - the staff, wand or orb, the weight model's grip, the fingers, the palm.
add_executable(spell_aim_model_test cleanroom/tests/spell_aim_model_test.cpp)
target_compile_options(spell_aim_model_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hands] staff_upright_arm (vr/staff_upright.h): a staff held upright aims along the arm, blended with its own axis as it tilts, pure.
add_executable(staff_upright_test cleanroom/tests/staff_upright_test.cpp)
target_compile_options(staff_upright_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The aim point's distance from the stretch of the aiming arm ([bow] aim_distance_by_arm, vr/arm_reach.h): the extension, the cells, the smoothing, pure.
add_executable(arm_reach_test cleanroom/tests/arm_reach_test.cpp)
target_compile_options(arm_reach_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [arms] full_reach (vr/full_reach.h): the hand always reaches its controller - the collarbone's least turn, then the stretch, pure.
add_executable(full_reach_test cleanroom/tests/full_reach_test.cpp)
target_compile_options(full_reach_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
# A belt potion bottle let go of falls with its weight: bounce, friction, roll, rest (vr/bottle_fall.h), pure.
add_executable(bottle_fall_test cleanroom/tests/bottle_fall_test.cpp)
target_compile_options(bottle_fall_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hands] belt_empty_bottle, belt_drop_ground: the bottles' sizes, back or onto the ground, the game's word by the item's id, the emptied bottle's meshes (vr/belt_bottle.h), pure.
add_executable(belt_bottle_test cleanroom/tests/belt_bottle_test.cpp)
target_compile_options(belt_bottle_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hands] belt_drop_ground: the steps of the game's own drop - the belt slot's click, the cursor's drop, the way back (vr/belt_drop.h), pure.
add_executable(belt_drop_test cleanroom/tests/belt_drop_test.cpp)
target_compile_options(belt_drop_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
add_executable(staff_flat_test cleanroom/tests/staff_flat_test.cpp vr/weight.cpp vr/shield_arm_wire.cpp vr/ranged_hand_wire.cpp cleanroom/tests/head_shadow_stub.cpp vr/gait_lib.cpp)
target_include_directories(staff_flat_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_link_libraries(staff_flat_test PRIVATE d2rcam heft user32)
target_compile_definitions(staff_flat_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(staff_flat_test PRIVATE /utf-8 /W3 /EHsc /permissive-)
# The skeleton hook's discovery (vr/skeletons.cpp, included whole) on a toy hero rig in fake game memory: names read a page at a time (vr/guarded_text.h) as the letter-at-a-time
# loop read them, next to a page that does not read; the bones Learn finds; Arms' hero cache (by pointer, a full one keeps the hero); the bow / flail / crossbow caches.
add_executable(skeleton_discovery_test cleanroom/tests/skeleton_discovery_test.cpp vr/weight.cpp vr/shield_arm_wire.cpp vr/ranged_hand_wire.cpp cleanroom/tests/head_shadow_stub.cpp vr/gait_lib.cpp)
target_include_directories(skeleton_discovery_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_link_libraries(skeleton_discovery_test PRIVATE d2rcam heft user32)
target_compile_definitions(skeleton_discovery_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(skeleton_discovery_test PRIVATE /utf-8 /W3 /EHsc /permissive-)
# ... the shield on it: strapped to the left forearm as before, to the RIGHT forearm mirrored, put away ([hands] shoulder_draw)
add_executable(shield_arm_rig_test cleanroom/tests/shield_arm_rig_test.cpp vr/weight.cpp vr/shield_arm_wire.cpp vr/ranged_hand_wire.cpp cleanroom/tests/head_shadow_stub.cpp vr/gait_lib.cpp)
target_include_directories(shield_arm_rig_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_link_libraries(shield_arm_rig_test PRIVATE d2rcam heft user32)
target_compile_definitions(shield_arm_rig_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(shield_arm_rig_test PRIVATE /utf-8 /W3 /EHsc /permissive-)
# ... the arm's IK on a toy rig: [arms] full_reach puts the wrist on a target past the arm's reach (the collarbone turned, then the arm lengthened), no pile-up, off as before
add_executable(full_reach_rig_test cleanroom/tests/full_reach_rig_test.cpp vr/weight.cpp vr/shield_arm_wire.cpp vr/ranged_hand_wire.cpp cleanroom/tests/head_shadow_stub.cpp vr/gait_lib.cpp)
target_include_directories(full_reach_rig_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_link_libraries(full_reach_rig_test PRIVATE d2rcam heft user32)
target_compile_definitions(full_reach_rig_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(full_reach_rig_test PRIVATE /utf-8 /W3 /EHsc /permissive-)
# ... on a toy rig with real hands (fingers and thumb, the left hand a relabelled mirror): the hand that takes the weapon gets the game's own grip pose of the fingers ([weight] grip_fit_fingers)
add_executable(grip_fingers_test cleanroom/tests/grip_fingers_test.cpp vr/weight.cpp vr/shield_arm_wire.cpp vr/ranged_hand_wire.cpp cleanroom/tests/head_shadow_stub.cpp vr/gait_lib.cpp)
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
# ... a weapon in each hand, each a body of its own, stopped by the monsters' bodies and pushing the doll (vr/weight.cpp StepSecond)
#   (weight_dual_test dll=<d2r_vr_weight.dll> grip=<d2r_vr_grip.dll>: the same through the libraries, as the game runs it with hands_model 1, grip_model 1)
add_executable(weight_dual_test cleanroom/tests/weight_dual_test.cpp vr/weight.cpp vr/weight_lib.cpp vr/grip_lib.cpp)
target_include_directories(weight_dual_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_link_libraries(weight_dual_test PRIVATE heft)
target_compile_definitions(weight_dual_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(weight_dual_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hands] shoulder_draw through the same glue: it lies where it fell, the shoulder draws it, the cross draw (vr/shoulder_draw.h)
add_executable(weight_draw_wire_test cleanroom/tests/weight_draw_wire_test.cpp vr/weight.cpp vr/shield_arm_wire.cpp)
target_include_directories(weight_draw_wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_link_libraries(weight_draw_wire_test PRIVATE heft)
target_compile_definitions(weight_draw_wire_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(weight_draw_wire_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# the zones and the choice (vr/shoulder_draw.h), pure
add_executable(shoulder_draw_test cleanroom/tests/shoulder_draw_test.cpp)
target_compile_options(shoulder_draw_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# the shield's arm: the holster and the right forearm's mirrored strap (vr/shield_arm.h), pure; and its wiring (vr/shield_arm_wire.cpp) through skel::Input
add_executable(shield_arm_test cleanroom/tests/shield_arm_test.cpp)
target_compile_options(shield_arm_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hands] shield_stow_no_block: the shield put away does not block - the pure rule (vr/stowed_block.h); the hook (vr/stowed_block.cpp) is in d2rl-vrcam below
# The Kurast sewer stairs: their single-sided walls two-sided (vr/roof_sides.h) and A taking the stairs standing (vr/sewer_interaction.h), both pure
add_executable(roof_sides_sewer_test cleanroom/tests/roof_sides_sewer_test.cpp)
target_include_directories(roof_sides_sewer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(roof_sides_sewer_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(sewer_interaction_test cleanroom/tests/sewer_interaction_test.cpp)
target_include_directories(sewer_interaction_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(sewer_interaction_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(stowed_block_test cleanroom/tests/stowed_block_test.cpp)
target_include_directories(stowed_block_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(stowed_block_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [audio] head_follow: a sound's screen offset as a ground direction (vr/spatial_audio.h); the FMOD hooks (vr/spatial_audio.cpp) are in d2rl-vrcam below
add_executable(spatial_audio_test cleanroom/tests/spatial_audio_test.cpp)
target_include_directories(spatial_audio_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(spatial_audio_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hud] labels_whole_eye: the label layout's height check given the whole picture (vr/label_area.h); the hook is vrcam.cpp InstallLabelArea
add_executable(label_area_test cleanroom/tests/label_area_test.cpp)
target_include_directories(label_area_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(label_area_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(shield_arm_wire_test cleanroom/tests/shield_arm_wire_test.cpp vr/shield_arm_wire.cpp)
target_include_directories(shield_arm_wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_definitions(shield_arm_wire_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(shield_arm_wire_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
    # [belt] hold_takes_trigger: the triggers while a belt bottle is in a hand (vr/belt_trigger.h, pure, and its wiring vr/belt_trigger_wire.cpp), with stand-ins for Listen / Take
    add_executable(belt_trigger_test cleanroom/tests/belt_trigger_test.cpp vr/belt_trigger_wire.cpp)
    target_include_directories(belt_trigger_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(belt_trigger_test PRIVATE D2RLPlugin::D2RLPlugin)
    target_compile_definitions(belt_trigger_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(belt_trigger_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# vr/world_anchor.h: a weapon that lies on the floor stays where it is in the game's WORLD while the hero walks or turns (pure maths)
# vr/ranged_hand.h: a bow or crossbow put away behind a shoulder and drawn from it, into the hand that reached (pure)
add_executable(ranged_hand_test cleanroom/tests/ranged_hand_test.cpp)
target_include_directories(ranged_hand_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(ranged_hand_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(ranged_hand_wire_test cleanroom/tests/ranged_hand_wire_test.cpp vr/ranged_hand_wire.cpp)
target_include_directories(ranged_hand_wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(ranged_hand_wire_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(world_anchor_test cleanroom/tests/world_anchor_test.cpp)
target_include_directories(world_anchor_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(world_anchor_test PRIVATE /utf-8 /W4 /EHsc /permissive-)


add_library(d2rl-vrcam SHARED vr/vrcam.cpp vr/ui_lang.cpp vr/xr.cpp vr/link.cpp vr/reshade_shield.cpp vr/dlss_mv.cpp vr/skeletons.cpp vr/weight.cpp vr/gamestate.cpp vr/gamecmd.cpp vr/direct_input.cpp vr/panel_input.cpp vr/throw_gesture_wire.cpp vr/throw_born.cpp vr/potion_hide.cpp vr/probe.cpp vr/uitrace.cpp vr/light_bounds.cpp vr/fog_spare.cpp vr/fog_in_frame.cpp vr/hud.cpp vr/memdiff.cpp vr/writewatch.cpp vr/hset_paint.cpp vr/hset_store.cpp shared/ini_txn.cpp vr/render_resolution.cpp vr/melee_touch.cpp vr/melee_direct.cpp vr/melee_direct_client.cpp vr/skill_wheel.cpp vr/wheel_game.cpp vr/wheel_paint.cpp vr/tiles_paint.cpp vr/wheel_icons.cpp vr/stand_still_wire.cpp vr/move_attack_wire.cpp vr/room_walk_wire.cpp vr/stray_walk_wire.cpp vr/shot_trace_wire.cpp vr/hset_view.cpp vr/gpu_fault.cpp vr/ecl_hook.cpp vr/native_d3d12.cpp vr/native_rsapi.cpp vr/fx_native.cpp vr/vrcam.rc vr/quiver_wire.cpp vr/vcache.cpp vr/fog_height.cpp vr/fog_before_fx.cpp)
target_include_directories(d2rl-vrcam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${FLATVR_SHARED_DIR} ${RESHADE_SDK_INCLUDE} ${BODYWALK_SRC} ${BODYWALK_SDK_INCLUDE})
target_sources(d2rl-vrcam PRIVATE vr/shoulder_swap_wire.cpp vr/shield_arm_wire.cpp vr/ranged_hand_wire.cpp)   # [hands] shoulder_swap (wired to vr/direct_input.h)
target_sources(d2rl-vrcam PRIVATE vr/turn_warp.cpp vr/effects_layer.cpp)   # [stereo] turn_warp: eyes by turns, the eye not drawn made on the GPU from pictures already drawn (bearstonem/d2r-pcvr's, MIT)
target_sources(d2rl-vrcam PRIVATE vr/belt_glow.cpp vr/belt_drop.cpp)   # [hands] belt_inner_glow (the game's own highlight on a bottle in reach), belt_drop_ground (the game's own drop of a belt potion)
target_sources(d2rl-vrcam PRIVATE vr/own_effects.cpp)   # [render] dim_own_effects: the hero's own effects (Bone Armor ...) fainter, visual only (hooks the game's overlay creation to look)
target_sources(d2rl-vrcam PRIVATE vr/head_shadow.cpp)   # [arms] head_shadow: the hidden head still casts its shadow (the eyes' skinning only has it shrunk), visual only
target_sources(d2rl-vrcam PRIVATE vr/monitor_eye.cpp)   # [render] monitor_eye: the game's window shows one eye, steady - to capture the screen (bearstonem/d2r-pcvr's desktop_one_eye, MIT)
target_sources(d2rl-vrcam PRIVATE vr/weight_wire.cpp)   # [weight]: a grip on the weapon is not a chord's modifier (wired to vr/direct_input.h)
target_sources(d2rl-vrcam PRIVATE vr/belt_trigger_wire.cpp)   # [belt] hold_takes_trigger: the triggers while a belt bottle is in a hand (the game reads the left one as Shift)
target_sources(d2rl-vrcam PRIVATE vr/weight_lib.cpp)    # [weight] hands_model 1: the weight-physics library d2r_vr_weight.dll, when it is beside the plugin (shared/d2r_vr_weight.h)
target_sources(d2rl-vrcam PRIVATE vr/doll_lib.cpp)      # [weight] monster_ragdoll: the doll library d2r_vr_doll.dll, when it is in d2r_vr_libs (shared/d2r_vr_doll.h)
target_sources(d2rl-vrcam PRIVATE vr/grab_wire.cpp vr/grab_lib.cpp)   # [weight] grab_bodies: an empty hand's grip holds a body - the weight library's grab (d2r_vr_weight.dll 1.4, shared/d2r_vr_grab.h)
target_sources(d2rl-vrcam PRIVATE vr/props_lib.cpp vr/knock_props.cpp)   # [weight] knock_props: the weight library's props world (shared/d2r_vr_props.h), the level's props near the hero, the physics thread
target_sources(d2rl-vrcam PRIVATE vr/api_layers_lib.cpp)   # [openxr] block_foreign_layers: other programs' implicit OpenXR layers off in this process (shared/api_layers.h)
target_sources(d2rl-vrcam PRIVATE vr/frame_gen_lib.cpp)   # [openxr] frame_generation: OFXR Bridge for this process only (shared/frame_gen.h)
target_sources(d2rl-vrcam PRIVATE vr/grip_lib.cpp)      # [weight] grip_model 1: the grip library d2r_vr_grip.dll, when it is beside the plugin (shared/d2r_vr_grip.h)
target_sources(d2rl-vrcam PRIVATE vr/pose_share_wire.cpp)
target_sources(d2rl-vrcam PRIVATE vr/gait_lib.cpp)   # [multiplayer] pose_share_legs: the optional gait library d2r_vr_gait.dll (shared/d2r_vr_gait.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/gait_lib_test.cpp")   # the loader against stand-ins (not in the public tree's allow-list)
    add_library(gait_lib_standin SHARED cleanroom/tests/gait_lib_standin.cpp)
    target_include_directories(gait_lib_standin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(gait_lib_standin PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_library(gait_lib_standin_bad SHARED cleanroom/tests/gait_lib_standin.cpp)
    target_compile_definitions(gait_lib_standin_bad PRIVATE STANDIN_BAD_ABI=1)
    target_include_directories(gait_lib_standin_bad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(gait_lib_standin_bad PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_executable(gait_lib_test cleanroom/tests/gait_lib_test.cpp vr/gait_lib.cpp)
    target_include_directories(gait_lib_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_definitions(gait_lib_test PRIVATE GAIT_STANDIN_GOOD="$<TARGET_FILE:gait_lib_standin>" GAIT_STANDIN_BAD="$<TARGET_FILE:gait_lib_standin_bad>")
    target_compile_options(gait_lib_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_dependencies(gait_lib_test gait_lib_standin gait_lib_standin_bad)
endif()   # [multiplayer] pose_share: the VR players' poses through D2RLoader's plugin network (vr/pose_share.h)
# [multiplayer] pose_share's pure part (vr/pose_share.h: the wire format, the host's relay table, the playback, the frames): pose_share_test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/pose_share_test.cpp")   # (not in the public tree's allow-list)
    add_executable(pose_share_test cleanroom/tests/pose_share_test.cpp)
    target_include_directories(pose_share_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(pose_share_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # [multiplayer] pose_share_bodies (vr/player_bodies.h): the other players' capsules, into the weight model's bodies, the draw, the echo ahead, steel on steel
    # the gait's knee and leg bones' turns from the rest pose and the bend plane (vr/leg_ik.h: never a knee bent the wrong way)
    add_executable(leg_ik_test cleanroom/tests/leg_ik_test.cpp)
    target_include_directories(leg_ik_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(leg_ik_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # [body] walk_back_reverse: the game's own walk played backwards while the hero walks back (vr/walk_back.h: the store, the cursor's seamless jumps, the bone blend)
    add_executable(walk_back_test cleanroom/tests/walk_back_test.cpp)
    target_include_directories(walk_back_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(walk_back_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # ... and its Reverser on the game's own walk, for tools/walk_legs/walk_legs.py --stream / --played (the stick-figure pictures; not a test)
    add_executable(walk_back_sim tools/walk_legs/walk_back_sim.cpp)
    target_include_directories(walk_back_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(walk_back_sim PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # a hero rig's legs by its hierarchy and bind pose where its bones' names say nothing (vr/leg_chain.h: the barbarian's rig)
    add_executable(leg_chain_test cleanroom/tests/leg_chain_test.cpp)
    target_include_directories(leg_chain_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(leg_chain_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # [multiplayer] pose_share_hero (vr/remote_pose.h): another player's hero posed from his pose - the target, which rig, the turn into the model, the hands, the head
    add_executable(remote_pose_test cleanroom/tests/remote_pose_test.cpp)
    target_include_directories(remote_pose_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(remote_pose_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_executable(player_bodies_test cleanroom/tests/player_bodies_test.cpp)
    target_include_directories(player_bodies_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(player_bodies_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# The transport probe for [multiplayer] pose_share (tools/pose_netprobe): vrcam's own wire in a plugin of its own, a made-up pose, no game
# calls - D2RLoader's plugin network measured without a headset; a Client and a Shared build (may a Client plugin use the network?)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/pose_netprobe/pose_netprobe.cpp")
    foreach(_role client shared)
        add_library(d2rl-pose-netprobe-${_role} SHARED tools/pose_netprobe/pose_netprobe.cpp vr/pose_share_wire.cpp)
        target_include_directories(d2rl-pose-netprobe-${_role} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
        target_link_libraries(d2rl-pose-netprobe-${_role} PRIVATE D2RLPlugin::D2RLPlugin)
        target_compile_definitions(d2rl-pose-netprobe-${_role} PRIVATE POSE_NETPROBE_SHARED=$<STREQUAL:${_role},shared>)
        target_compile_options(d2rl-pose-netprobe-${_role} PRIVATE /utf-8 /W4 /EHsc /permissive-)
    endforeach()
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/doll_lib_test.cpp")   # [weight] monster_ragdoll: the doll library's loader against stand-in DLLs (one right, one of another version)
    add_library(doll_lib_standin SHARED cleanroom/tests/doll_lib_standin.cpp)
    target_include_directories(doll_lib_standin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(doll_lib_standin PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_library(doll_lib_standin_bad SHARED cleanroom/tests/doll_lib_standin.cpp)
    target_compile_definitions(doll_lib_standin_bad PRIVATE STANDIN_BAD_ABI=1)
    target_include_directories(doll_lib_standin_bad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(doll_lib_standin_bad PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_executable(doll_lib_test cleanroom/tests/doll_lib_test.cpp vr/doll_lib.cpp)
    target_include_directories(doll_lib_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_definitions(doll_lib_test PRIVATE STANDIN_GOOD="$<TARGET_FILE:doll_lib_standin>" STANDIN_BAD="$<TARGET_FILE:doll_lib_standin_bad>")
    target_compile_options(doll_lib_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_dependencies(doll_lib_test doll_lib_standin doll_lib_standin_bad)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/weight_lib_test.cpp")   # (not in the public tree's allow-list)
    # The loader and the engine adapter, tested against a stand-in DLL that implements shared/d2r_vr_weight.h with heft (two builds of it: one right, one of another version).
    add_library(weight_lib_standin SHARED cleanroom/tests/weight_lib_standin.cpp)
    target_include_directories(weight_lib_standin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(weight_lib_standin PRIVATE heft)
    target_compile_options(weight_lib_standin PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_library(weight_lib_standin_bad SHARED cleanroom/tests/weight_lib_standin.cpp)
    target_compile_definitions(weight_lib_standin_bad PRIVATE STANDIN_BAD_ABI=1)
    target_include_directories(weight_lib_standin_bad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(weight_lib_standin_bad PRIVATE heft)
    target_compile_options(weight_lib_standin_bad PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_executable(weight_lib_test cleanroom/tests/weight_lib_test.cpp vr/weight_lib.cpp)
    target_include_directories(weight_lib_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(weight_lib_test PRIVATE heft)
    target_compile_definitions(weight_lib_test PRIVATE STANDIN_GOOD="$<TARGET_FILE:weight_lib_standin>" STANDIN_BAD="$<TARGET_FILE:weight_lib_standin_bad>")
    target_compile_options(weight_lib_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_dependencies(weight_lib_test weight_lib_standin weight_lib_standin_bad)
    # The face-to-face table of our own physics and a weight-physics library's, on the same motions (the library's path as the argument).
    add_executable(weight_lib_compare cleanroom/tests/weight_lib_compare.cpp vr/weight_lib.cpp)
    target_include_directories(weight_lib_compare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(weight_lib_compare PRIVATE heft)
    target_compile_options(weight_lib_compare PRIVATE /utf-8 /W4 /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/hero_strength_test.cpp")   # (not in the public tree's allow-list)
    # [weight] hero_strength: the maths (vr/hero_strength.h), and the weight model end to end with the weight library's stand-in (it records the steel's mass and the strength it is given)
    add_executable(hero_strength_test cleanroom/tests/hero_strength_test.cpp)
    target_compile_options(hero_strength_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_executable(weight_strength_test cleanroom/tests/weight_strength_test.cpp vr/weight.cpp vr/weight_lib.cpp)
    target_include_directories(weight_strength_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(weight_strength_test PRIVATE heft)
    target_compile_definitions(weight_strength_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX STANDIN_GOOD="$<TARGET_FILE:weight_lib_standin>")
    target_compile_options(weight_strength_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_dependencies(weight_strength_test weight_lib_standin)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/slide_kinds_test.cpp")   # (not in the public tree's allow-list)
    # [weight] slide_second_hand_<kind>: which weapons' second hand slides (vr/slide_kinds.h), against the weight model's shapes and the hold itself
    add_executable(slide_kinds_test cleanroom/tests/slide_kinds_test.cpp)
    target_include_directories(slide_kinds_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(slide_kinds_test PRIVATE heft)
    target_compile_options(slide_kinds_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/grip_zone_test.cpp")   # (not in the public tree's allow-list)
    # [weight] grip_hilt_only: where along the shaft the second hand takes a sword, an axe, a mace (vr/grip_zone.h, heft::hold::Hold::HiltEnd), on the hold itself
    add_executable(grip_zone_test cleanroom/tests/grip_zone_test.cpp)
    target_include_directories(grip_zone_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(grip_zone_test PRIVATE heft)
    target_compile_options(grip_zone_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/grip_lib_test.cpp")   # (not in the public tree's allow-list)
    # The grip library: the loader, the hold's grip engine and the skeleton hook's library path, tested against a stand-in DLL that implements shared/d2r_vr_grip.h (two builds: one right, one of another version).
    add_library(grip_lib_standin SHARED cleanroom/tests/grip_lib_standin.cpp)
    target_include_directories(grip_lib_standin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(grip_lib_standin PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_library(grip_lib_standin_bad SHARED cleanroom/tests/grip_lib_standin.cpp)
    target_compile_definitions(grip_lib_standin_bad PRIVATE STANDIN_BAD_ABI=1)
    target_include_directories(grip_lib_standin_bad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(grip_lib_standin_bad PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_library(grip_lib_standin_v1 SHARED cleanroom/tests/grip_lib_standin.cpp)   # one that speaks interface version 1 (an older d2r_vr_grip.dll)
    target_compile_definitions(grip_lib_standin_v1 PRIVATE STANDIN_ABI_1=1)
    target_include_directories(grip_lib_standin_v1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(grip_lib_standin_v1 PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_executable(grip_lib_test cleanroom/tests/grip_lib_test.cpp vr/weight.cpp vr/grip_lib.cpp vr/shield_arm_wire.cpp vr/ranged_hand_wire.cpp cleanroom/tests/head_shadow_stub.cpp vr/gait_lib.cpp)
    target_include_directories(grip_lib_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(grip_lib_test PRIVATE d2rcam heft user32)
    target_compile_definitions(grip_lib_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX GRIP_STANDIN_GOOD="$<TARGET_FILE:grip_lib_standin>" GRIP_STANDIN_BAD="$<TARGET_FILE:grip_lib_standin_bad>" GRIP_STANDIN_V1="$<TARGET_FILE:grip_lib_standin_v1>")
    target_compile_options(grip_lib_test PRIVATE /utf-8 /W3 /EHsc /permissive-)
    add_dependencies(grip_lib_test grip_lib_standin grip_lib_standin_bad grip_lib_standin_v1)
    # Our own way of holding and a grip library's, face to face on the same rig (the library's path as the argument).
    add_executable(grip_lib_compare cleanroom/tests/grip_lib_compare.cpp vr/weight.cpp vr/grip_lib.cpp vr/shield_arm_wire.cpp vr/ranged_hand_wire.cpp cleanroom/tests/head_shadow_stub.cpp vr/gait_lib.cpp)
    target_include_directories(grip_lib_compare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(grip_lib_compare PRIVATE d2rcam heft user32)
    target_compile_definitions(grip_lib_compare PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(grip_lib_compare PRIVATE /utf-8 /W3 /EHsc /permissive-)
endif()
target_link_libraries(d2rl-vrcam PRIVATE d2rcam heft minhook user32 gdi32 ole32 uuid OpenXR::openxr_loader reshadefx windowscodecs)
target_compile_options(d2rl-vrcam PRIVATE /utf-8 /W3 /EHsc /permissive-)
# [gameplay] shield_smite: a shield bash casts the game's own Smite (vr/shield_smite.h decides, shield_smite.cpp talks to the game).
target_sources(d2rl-vrcam PRIVATE vr/shield_smite.cpp)
target_sources(d2rl-vrcam PRIVATE vr/parry_wire.cpp)   # [weight] parry_*: the monster's blow met by the weapon or the shield (vr/parry.h decides)
add_executable(shield_smite_test cleanroom/tests/shield_smite_test.cpp)
target_include_directories(shield_smite_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(shield_smite_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hands] double_swing_by_hands: with a weapon in each hand the second hand's touch on the monster the first hand just struck casts the game's own Double Swing / Frenzy
# (vr/double_swing.h decides: pairs, window, skill, mana; double_swing_game.cpp reads the pad slots, the skills' levels and the mana, and tells the log): double_swing_test
target_sources(d2rl-vrcam PRIVATE vr/double_swing_game.cpp)
add_executable(double_swing_test cleanroom/tests/double_swing_test.cpp)
target_compile_options(double_swing_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hands] double_swing_combo (vr/double_swing.h Tracker: the combo - every blow after the first on a monster is the dual skill - and the pairs as they were): double_swing_combo_test
add_executable(double_swing_combo_test cleanroom/tests/double_swing_combo_test.cpp)
target_compile_options(double_swing_combo_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [gameplay] melee_skill_armed: a melee attack skill's pad button arms the skill instead of casting it; every hit by touch is then the game's attack with it, the plain attack when the mana is short
# (vr/melee_arm.h decides: the skills, the press, the pad buttons taken, the hit; melee_arm_game.cpp reads the pad slots, the level and the mana, vrcam's slot-skill hook answers): melee_arm_test
target_sources(d2rl-vrcam PRIVATE vr/melee_arm_game.cpp)
add_executable(melee_arm_test cleanroom/tests/melee_arm_test.cpp)
target_compile_options(melee_arm_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [gameplay] melee_skill_armed_icon: the armed skill's icon on the bottom of the mana medallion (vr/armed_icon.h decides what is drawn and how strong, the circle it is cut to and the
# bake of the game's icon; vrcam uploads it into D2R_DepthFog.fx OrbIconTex and the effect lays it under the liquid): armed_icon_test
add_executable(armed_icon_test cleanroom/tests/armed_icon_test.cpp)
target_compile_options(armed_icon_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [gameplay] melee_touch_only: with hit by touch live the trigger does not attack - the plugin's own layout leaves the trigger's attack out (vr/melee_trigger.h decides, melee_trigger_wire.cpp
# feeds it direct_input.h's frames and reads the pad's A slot): melee_trigger_test
target_sources(d2rl-vrcam PRIVATE vr/melee_trigger_wire.cpp)
add_executable(melee_trigger_test cleanroom/tests/melee_trigger_test.cpp)
target_include_directories(melee_trigger_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(melee_trigger_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [openxr] skill_rt_a: the right trigger and the right A as the game's RT and A skill slots (vr/slot_controls.h, pure; skill_wheel.cpp wires it): which slot each control
# presses, the left trigger as the second layer, the plain slots left to the layout, the slot mapping against the game's table: slot_controls_test
add_executable(slot_controls_test cleanroom/tests/slot_controls_test.cpp)
target_include_directories(slot_controls_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(slot_controls_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The monsters round the hero from the client's unit hash (vr/unit_scan.h, pure): ONE scan for hit by touch and the shield bash.
add_executable(unit_scan_test cleanroom/tests/unit_scan_test.cpp)
target_include_directories(unit_scan_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(unit_scan_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Hit by touch's verdict from the client's own units when the game's server is not in this process (vr/client_verdict.h, pure).
add_executable(client_verdict_test cleanroom/tests/client_verdict_test.cpp)
target_compile_options(client_verdict_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [debug] skill_triggers: what casts a skill, drawn in the headset (vr/skill_triggers.h pure, skill_triggers.cpp its state): the shield bash so far.
target_sources(d2rl-vrcam PRIVATE vr/skill_triggers.cpp)
add_executable(skill_triggers_test cleanroom/tests/skill_triggers_test.cpp vr/skill_triggers.cpp)
target_include_directories(skill_triggers_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(skill_triggers_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hands] shield_fist (vr/shield_fist.h: the hand with a shield closes with the grip) and the weight model's contract for that hand.
add_executable(shield_fist_test cleanroom/tests/shield_fist_test.cpp)
target_link_libraries(shield_fist_test PRIVATE heft)
target_compile_options(shield_fist_test PRIVATE /utf-8 /W4 /EHsc /permissive-)

# [hands] forearm_twist: the forearm turns with the wrist (vr/forearm_twist.h pure, the skeleton hook in skeletons.cpp).
add_executable(forearm_twist_test cleanroom/tests/forearm_twist_test.cpp)
target_compile_options(forearm_twist_test PRIVATE /utf-8 /W4 /EHsc /permissive-)

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
# D2R VR Settings' list of controller buttons (vr/direct_remap.h, pure): what each row shows from the ini and what it writes back, against the
# plugin's own parser (vr/direct_layout.h TableFromIni). Run: direct_remap_test.exe
add_executable(direct_remap_test cleanroom/tests/direct_remap_test.cpp)
target_include_directories(direct_remap_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr ${BODYWALK_SDK_INCLUDE})
target_compile_options(direct_remap_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# A game key (Show items, Alt) on a long press or a touch line is held as long as the control (vr/direct_layout.h, vr/hold_tap.h, pure).
add_executable(hold_line_test cleanroom/tests/hold_line_test.cpp)
target_include_directories(hold_line_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(hold_line_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The game must never think its pad left (vr/pad_presence.h): when pad 0 is answered present, and what the log says.
add_executable(pad_presence_test cleanroom/tests/pad_presence_test.cpp)
target_include_directories(pad_presence_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(pad_presence_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Attacks that never walk the hero ([gameplay] attack_in_place, vr/stand_still.h): which presses, and the wait for the game's Stand Still.
add_executable(stand_still_test cleanroom/tests/stand_still_test.cpp)
target_include_directories(stand_still_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(stand_still_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Moving on once the blow is out ([gameplay] move_while_attacking, vr/move_attack.h): which weapons and modes, the server gate, the walk, the rate.
add_executable(move_attack_test cleanroom/tests/move_attack_test.cpp)
target_include_directories(move_attack_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(move_attack_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# What the game does with a shot of ours, told in the log ([debug] shot_trace, vr/shot_trace.h): the window of a shot and the texts.
add_executable(shot_trace_test cleanroom/tests/shot_trace_test.cpp)
target_include_directories(shot_trace_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(shot_trace_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# No stray walk, no stray mouse button in the F4 world ([gameplay] block_stray_walk / block_stray_clicks, vr/stray_walk.h): the decisions.
add_executable(stray_walk_test cleanroom/tests/stray_walk_test.cpp)
target_include_directories(stray_walk_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(stray_walk_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The game's depth range at its source ([render] depth_fix_mode 2; vr/depth_source.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/depth_source_test.cpp")   # (not in the public tree's allow-list)
    add_executable(depth_source_test cleanroom/tests/depth_source_test.cpp)
    target_include_directories(depth_source_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(depth_source_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# Steam Frame's own controller layout and the Index grip by force ([openxr] frame_profile, index_grip_force; vr/xr_profiles.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/xr_profiles_test.cpp")   # (not in the public tree's allow-list)
    add_executable(xr_profiles_test cleanroom/tests/xr_profiles_test.cpp)
    target_include_directories(xr_profiles_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(xr_profiles_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# Is the thumbstick bound by the OpenXR runtime? (vr/xr_stick_check.h: the verdict from the counts of bound sources, the spare action, the state-block bits, the words)
add_executable(xr_stick_check_test cleanroom/tests/xr_stick_check_test.cpp)
target_include_directories(xr_stick_check_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(xr_stick_check_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [weight] knock_props: the catalogue of knockable props, a preset's props, the renderer's matrices taken apart, what a prop stands on (shared/props_scene.h, shared/props_presets.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/props_scene_test.cpp")   # (not in the public tree's allow-list)
    add_executable(props_scene_test cleanroom/tests/props_scene_test.cpp)
    target_include_directories(props_scene_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(props_scene_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [weight] grab_items: the pure picker of an item on the ground (vr/item_grab.h). Run: item_grab_test.exe
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/item_grab_test.cpp")
    add_executable(item_grab_test cleanroom/tests/item_grab_test.cpp)
    target_include_directories(item_grab_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(item_grab_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# ... and the loader of the weight library's props world (vr/props_lib.h). Run: props_lib_test.exe [<a d2r_vr_weight.dll 1.3 built from the private repository>]
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/props_lib_test.cpp")
    add_executable(props_lib_test cleanroom/tests/props_lib_test.cpp vr/props_lib.cpp)
    target_include_directories(props_lib_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(props_lib_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [openxr] block_foreign_layers: the manifest reading, the allow-list, what is turned off and the Status line (shared/api_layers.h) - and
# vr/api_layers_lib.cpp against the real OpenXR loader and this PC's registry (read only): a layer's disable_environment set in the test's own process
# takes it out of the loader's list. Run: api_layers_test.exe
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/api_layers_test.cpp")
    add_executable(api_layers_test cleanroom/tests/api_layers_test.cpp vr/api_layers_lib.cpp)
    target_include_directories(api_layers_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(api_layers_test PRIVATE OpenXR::openxr_loader advapi32)
    target_compile_definitions(api_layers_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(api_layers_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [openxr] runtime_fov / symmetric_fov / fov_crop: the fov each eye is drawn with and the off-axis projection from it (vr/eye_fov.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/eye_fov_test.cpp")
    add_executable(eye_fov_test cleanroom/tests/eye_fov_test.cpp)
    target_include_directories(eye_fov_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(eye_fov_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [openxr] frame_generation: OFXR Bridge's settings, control ABI, manifest and state lines (shared/frame_gen.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/frame_gen_test.cpp")   # (not in the public tree's allow-list)
    add_executable(frame_gen_test cleanroom/tests/frame_gen_test.cpp)
    target_include_directories(frame_gen_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(frame_gen_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    # ... and vr/frame_gen_lib.cpp's Offer against the real OpenXR loader, no headset. Run: frame_gen_loader_test.exe <an empty scratch folder>
    add_executable(frame_gen_loader_test cleanroom/tests/frame_gen_loader_test.cpp vr/frame_gen_lib.cpp)
    target_include_directories(frame_gen_loader_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(frame_gen_loader_test PRIVATE OpenXR::openxr_loader)
    target_compile_definitions(frame_gen_loader_test PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(frame_gen_loader_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# The right grip + the right stick bring a game panel nearer / farther in VR F4 ([openxr] menu_zoom; vr/menu_zoom.h: the readable limits, a step, the grip's hold)
add_executable(menu_zoom_test cleanroom/tests/menu_zoom_test.cpp)
target_include_directories(menu_zoom_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared ${BODYWALK_SDK_INCLUDE})
target_compile_options(menu_zoom_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# In which views the aiming hand may direct the hero's attack (vr/attack_view.h): VR F2 and F4 look where the player looks, VR F1 and F3 (top-down) keep the hero's own facing.
# One target per press of "pick up / interact" ([input] interact_one_press; vr/interact_press.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/interact_press_test.cpp")   # (not in the public tree's allow-list)
    add_executable(interact_press_test cleanroom/tests/interact_press_test.cpp)
    target_include_directories(interact_press_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(interact_press_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# A skill slot's press in a pad report, the right trigger's RT slot too: the cast goes along the hand ([hands] spell_aim; vr/pad_skill.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/pad_skill_test.cpp")   # (not in the public tree's allow-list)
    add_executable(pad_skill_test cleanroom/tests/pad_skill_test.cpp)
    target_include_directories(pad_skill_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(pad_skill_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# A grip the laser took, tapped over a menu, is the game's bumper - the tabbed menu's page ([openxr] laser_grip_tap; vr/laser_grip_tap.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/laser_grip_tap_test.cpp")   # (not in the public tree's allow-list)
    add_executable(laser_grip_tap_test cleanroom/tests/laser_grip_tap_test.cpp)
    target_include_directories(laser_grip_tap_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(laser_grip_tap_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# The laser reaches the whole picture: its way swept over monitors and windows as players have them ([openxr] laser_fit_window; vr/laser_reach.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/laser_reach_test.cpp")   # (not in the public tree's allow-list)
    add_executable(laser_reach_test cleanroom/tests/laser_reach_test.cpp)
    target_include_directories(laser_reach_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(laser_reach_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# The timer that stops each controller pulse when its time is up ([openxr] haptics_stop; vr/haptic_stop.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/haptic_stop_test.cpp")   # (not in the public tree's allow-list)
    add_executable(haptic_stop_test cleanroom/tests/haptic_stop_test.cpp)
    target_include_directories(haptic_stop_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(haptic_stop_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# The flat picture and a menu's panel placed only with the head tracked ([openxr] menus_wait_tracking; vr/anchor_gate.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/anchor_gate_test.cpp")   # (not in the public tree's allow-list)
    add_executable(anchor_gate_test cleanroom/tests/anchor_gate_test.cpp)
    target_include_directories(anchor_gate_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(anchor_gate_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# D2R VR as the D2RLoader mod D2R-VR: its folders (shared/mod_layout.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/mod_layout_test.cpp")   # (not in the public tree's allow-list)
    add_executable(mod_layout_test cleanroom/tests/mod_layout_test.cpp)
    target_include_directories(mod_layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(mod_layout_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
add_executable(attack_view_test cleanroom/tests/attack_view_test.cpp)
target_include_directories(attack_view_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(attack_view_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# BodyWalk's walking through the plugin into the game ([openxr] direct_bodywalk_walk, vr/walk_path.h): a fake pad in, the game's stick and
# command out, in direct mode and not, with and without the stray-walk guard.
# FlatVR's raw controllers through Game Link made into the native frame (vr/flat_raw.h, [direct] flatvr_controls): the head, the hands, the buttons, the time
# D2R VR Settings' Status row "BodyWalk's path to the game" (tools/settings/bw_path.h): Game Link direct, or BodyWalk's gamepad and Mapping
add_executable(bw_path_test cleanroom/tests/bw_path_test.cpp)
target_include_directories(bw_path_test PRIVATE ${BODYWALK_SDK_INCLUDE})
target_compile_options(bw_path_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(flat_raw_test cleanroom/tests/flat_raw_test.cpp)
target_include_directories(flat_raw_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared ${BODYWALK_SDK_INCLUDE})
target_compile_options(flat_raw_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(walk_path_test cleanroom/tests/walk_path_test.cpp)
target_include_directories(walk_path_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(walk_path_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [debug] walk_start_log (vr/walk_latency.h): one line per walk start of BodyWalk's - the stick leaving rest, past the dead zone, the game's walk, the hero moving
add_executable(walk_latency_test cleanroom/tests/walk_latency_test.cpp)
target_compile_options(walk_latency_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# BodyWalk's Output as D2R VR Settings' Status row reads it (tools/settings/bw_output.h): Keyboard = no BodyWalk button in FlatVR
add_executable(bw_output_test cleanroom/tests/bw_output_test.cpp)
target_compile_options(bw_output_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# D2R VR Settings' language files (tools/settings/lang.h): UTF-8 with and without a BOM, UTF-16, comments, escapes, {placeholders}, the file for Windows' language
add_executable(settings_lang_test cleanroom/tests/settings_lang_test.cpp)
target_compile_options(settings_lang_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# D2R VR Settings' Draw distance slider (shared/draw_distance.h): every stop a consistent set (fog full before the models' radius, the dome
# past both, rooms enough), near to far, the author's set a stop, one value by hand Custom
add_executable(draw_distance_test cleanroom/tests/draw_distance_test.cpp)
target_compile_options(draw_distance_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Room-scale walking in VR first person ([openxr] roomscale_walk, vr/room_walk.h): a synthetic room and game - the hero walks under the head,
# the view stays with the head, turning or nodding walks nothing, the cap at a wall, the controller's stick wins, a teleport, a recentre.
add_executable(room_walk_test cleanroom/tests/room_walk_test.cpp)
target_include_directories(room_walk_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(room_walk_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
# The thumbsticks' dead zones against drift ([direct] stick_deadzone_*_pct, stick_outer_*_pct, vr/stick_shape.h): the defaults change nothing, a drift reads as
# centred, radial not per axis, no jump at either edge, the outer saturation, and the readers after it (the belt flick, the pad's stick) keep their thresholds.
add_executable(stick_shape_test cleanroom/tests/stick_shape_test.cpp)
target_include_directories(stick_shape_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(stick_shape_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Walk or run by the left stick's lean ([direct] walk_by_lean, vr/lean_gait.h): up to the limit the length lands at or under the game's run threshold
# (the game walks, full walk strength at the limit), past it above (the game runs), any threshold, the rim kept; the Run key held past the limit only
# while the game's switch is on walk, let go a little under it; a threshold that cannot be read leaves the stick as it is.
add_executable(lean_gait_test cleanroom/tests/lean_gait_test.cpp)
target_include_directories(lean_gait_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(lean_gait_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The right stick's up / down tilt in VR F2 ([camera] stick_pitch_f2, vr/stick_pitch.h): nothing at rest or inside the dead zone, a turn never tilts,
# the speed and its square past the dead zone, the sign, the limits (a [camera] pitch outside them never pulled in), the ini's ranges.
add_executable(stick_pitch_test cleanroom/tests/stick_pitch_test.cpp)
target_include_directories(stick_pitch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(stick_pitch_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The settings panel in the headset ([openxr] headset_settings, vr/hset_*.h, vr/xr_headset_settings.h): its model,
# the chord and the lasers, the ini transaction (backup, conflict) and the picture, tested without a headset.
# [hud] headset_language: the headset's words in the player's language (vr/ui_text.h, vr/ui_text_tables.h, vr/ui_lang.cpp): English without a table, the
# translation with one, every table text in lang/en.txt, the pages composed both ways, a scratch mod folder read. Run: ui_text_test.exe
add_executable(ui_text_test cleanroom/tests/ui_text_test.cpp vr/ui_lang.cpp)
target_include_directories(ui_text_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared ${BODYWALK_SDK_INCLUDE})
target_compile_definitions(ui_text_test PRIVATE D2RVR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ui_text_test PRIVATE user32 gdi32)
target_compile_options(ui_text_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(hset_test tools/hset_test.cpp vr/hset_paint.cpp vr/hset_store.cpp shared/ini_txn.cpp)
target_include_directories(hset_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared ${BODYWALK_SDK_INCLUDE})
target_link_libraries(hset_test PRIVATE user32 gdi32)
target_compile_options(hset_test PRIVATE /utf-8 /W4 /EHsc /permissive- /D_CRT_SECURE_NO_WARNINGS)
# The T-pose measurement of the eye height and arm span (vr/body_calibration.h is its pure detector, tested below)
target_sources(d2rl-vrcam PRIVATE vr/calibration.cpp vr/calibration_panel.cpp vr/calibration_input.cpp)
# [onboarding] the first-start guide in the headset: when it asks the settings window for its Guide tab (the flow vr/first_start.h, tested: first_start_test; the page
# is vr/hset_scene.h's, tested in hset_test)
target_sources(d2rl-vrcam PRIVATE vr/first_start.cpp)
# Controller haptics ([openxr] haptics): the mailbox, the hit observer and the flush; the logic is vr/haptics_pattern.h, tested without a headset.
target_sources(d2rl-vrcam PRIVATE vr/haptics.cpp)
# [hands] shield_stow_no_block: the hook on the game's block check (the rule is vr/stowed_block.h, stowed_block_test above)
target_sources(d2rl-vrcam PRIVATE vr/stowed_block.cpp)
# [audio] head_follow: the game's 3D sounds turn with the view - the two FMOD hooks (the rule is vr/spatial_audio.h, spatial_audio_test above)
target_sources(d2rl-vrcam PRIVATE vr/spatial_audio.cpp)
target_sources(d2rl-vrcam PRIVATE vr/sound_trace.cpp)   # [debug] sound_trace: the game's sounds by name as it makes and plays them (two reading hooks on its sound engine)
# [weight] clash_sound: a knock of steel on steel plays the game's block sound through its own SoundPlay / SoundSetVolume, as loud as it was hard (the rule:
# vr/clash_loud.h, tested in clash_loud_test)
target_sources(d2rl-vrcam PRIVATE vr/clash_sound.cpp)
add_executable(clash_loud_test cleanroom/tests/clash_loud_test.cpp)
target_include_directories(clash_loud_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(clash_loud_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(haptics_test cleanroom/tests/haptics_test.cpp)
target_include_directories(haptics_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(haptics_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# Whose blow just landed (vr/hit_claim.h) and the controller it buzzes (haptics::HitHand): every source of a blow - touch with either hand, a Barbarian's two blades, a weapon in the left hand,
# the shield bash with the follower off / left / right, a thrown weapon, the game's own attack.
add_executable(hit_claim_test cleanroom/tests/hit_claim_test.cpp)
target_include_directories(hit_claim_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(hit_claim_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The one predicate behind "F4 only" (vr/view_gate.h), and the pulses' use of it (haptics.cpp itself, the output played by the test).
# Our own tests: not in the public tree.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/view_gate_test.cpp")
    add_executable(view_gate_test cleanroom/tests/view_gate_test.cpp)
    target_include_directories(view_gate_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(view_gate_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/fx_build_order_test.cpp")
    add_executable(fx_build_order_test cleanroom/tests/fx_build_order_test.cpp)
    target_include_directories(fx_build_order_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(fx_build_order_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/sky_paths_test.cpp")
    add_executable(sky_paths_test cleanroom/tests/sky_paths_test.cpp)
    target_include_directories(sky_paths_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(sky_paths_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/native_fx_depth_test.cpp")
    add_executable(native_fx_depth_test cleanroom/tests/native_fx_depth_test.cpp)
    target_include_directories(native_fx_depth_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(native_fx_depth_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/depth_pick_test.cpp")
    add_executable(depth_pick_test cleanroom/tests/depth_pick_test.cpp)
    target_include_directories(depth_pick_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(depth_pick_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/flatvr_pose_test.cpp")
    add_executable(flatvr_pose_test cleanroom/tests/flatvr_pose_test.cpp)
    target_include_directories(flatvr_pose_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(flatvr_pose_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/vcache_test.cpp")
    add_executable(vcache_test cleanroom/tests/vcache_test.cpp vr/vcache.cpp)
    target_include_directories(vcache_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(vcache_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [fog] game_fog (vr/game_fog_vr.h): our fog's numbers as the game's own distance fog, and the layout check before a write
# What is taken from D2R VR Unleashed (bearstonem; MPL 2.0 files of their own, shared/unleashed_*.h, cleanroom/drawdist/unleashed_big_objects.cpp,
# reshade/D2R_FogEase_Unleashed.fxh): Big objects' reach and the game's test's site (unleashed_far_objects_test, itself MPL, from Unleashed's
# far_objects_test), the fog's ease and no fog in the River of Flame (unleashed_fog_test). Run: unleashed_far_objects_test.exe, unleashed_fog_test.exe
add_executable(unleashed_far_objects_test cleanroom/tests/unleashed_far_objects_test.cpp)
target_compile_options(unleashed_far_objects_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(unleashed_fog_test cleanroom/tests/unleashed_fog_test.cpp)
target_compile_definitions(unleashed_fog_test PRIVATE UNLEASHED_FXH_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_FogEase_Unleashed.fxh"
                           DEPTHFOG_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx" _CRT_SECURE_NO_WARNINGS)
target_compile_options(unleashed_fog_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/game_fog_vr_test.cpp")
    add_executable(game_fog_vr_test cleanroom/tests/game_fog_vr_test.cpp)
    target_include_directories(game_fog_vr_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(game_fog_vr_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/replay_pause_test.cpp")
    add_executable(replay_pause_test cleanroom/tests/replay_pause_test.cpp)
    target_include_directories(replay_pause_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(replay_pause_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# vr/sky_view_eyes.h: which eye's sky camera a build writes in a pair, against a model of ReShade's one constant buffer
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/sky_view_eyes_test.cpp")
    add_executable(sky_view_eyes_test cleanroom/tests/sky_view_eyes_test.cpp)
    target_include_directories(sky_view_eyes_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(sky_view_eyes_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
# The eye distance of native OpenXR's stereo pair (vr/eye_gap.h): the runtime's, a fallback for eyes in one place, or the player's own number.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/ipd_test.cpp")
    add_executable(ipd_test tools/ipd_test.cpp)
    target_include_directories(ipd_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(ipd_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# The frame both eyes are drawn and submitted in, for canted displays (vr/eye_frame.h)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/eye_frame_test.cpp")
    add_executable(eye_frame_test tools/eye_frame_test.cpp)
    target_include_directories(eye_frame_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(eye_frame_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [stereo] slow_pair_world (vr/pair_world.h): a pair after a pass of over a second is still the world's - no flat picture between stereo pairs on a slow card.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/pair_world_test.cpp")
    add_executable(pair_world_test cleanroom/tests/pair_world_test.cpp)
    target_include_directories(pair_world_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(pair_world_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [openxr] hand_trackers (vr/hand_trackers.h): no hand trackers on Meta's runtime - they took the Touch controllers' places away (Quest 2).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/hand_trackers_test.cpp")
    add_executable(hand_trackers_test cleanroom/tests/hand_trackers_test.cpp)
    target_include_directories(hand_trackers_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(hand_trackers_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [openxr] hand_space (vr/hand_space.h): a grip with no whole pose in the room taken from LOCAL / the aim (Quest 2 over Link: P--r, the arms at the belt).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/hand_space_test.cpp")
    add_executable(hand_space_test cleanroom/tests/hand_space_test.cpp)
    target_include_directories(hand_space_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(hand_space_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [gameplay] melee_touch_barrels (vr/barrel_touch.h): a swing through a barrel / pot presses the game's own interact on it (after d2r-pcvr 9b75aa2, without its server call).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/barrel_touch_test.cpp")
    add_executable(barrel_touch_test cleanroom/tests/barrel_touch_test.cpp)
    target_include_directories(barrel_touch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(barrel_touch_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [debug] flat_pick (vr/flat_pick.h): the game's pixel against the middle, where the crosshair's ray meets the ground, the slip per unit of height.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/flat_pick_test.cpp")
    add_executable(flat_pick_test cleanroom/tests/flat_pick_test.cpp)
    target_include_directories(flat_pick_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(flat_pick_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [input] flat_aim_ground (vr/flat_ground.h): the flat mouse look's point on raised ground - the floor's cell with the drawn shear taken off.
add_executable(flat_ground_test cleanroom/tests/flat_ground_test.cpp)
target_include_directories(flat_ground_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(flat_ground_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [camera] flat_shift_x / _y (vr/flat_shift.h): the flat picture moved by a share of its size; the mouse's ray still meets what is drawn.
add_executable(flat_shift_test cleanroom/tests/flat_shift_test.cpp)
target_include_directories(flat_shift_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/camera)
target_compile_options(flat_shift_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# From sajmonsoft's flat fork: [input] flat_body_pick (vr/flat_body.h) - a unit or object clicked by its body; [input] flat_portal_no_oval
# (vr/portal_oval.h) - the town portal's black oval, which meshes and when a remembered render mesh may be written.
add_executable(flat_body_test cleanroom/tests/flat_body_test.cpp)
target_include_directories(flat_body_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(flat_body_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(portal_oval_test cleanroom/tests/portal_oval_test.cpp)
target_include_directories(portal_oval_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(portal_oval_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [body] lock (F4) and flat_hold_trunk (flat F3), vr/trunk_hold.h: the trunk's axis, and which pose each bone above the pelvis takes.
add_executable(trunk_hold_test cleanroom/tests/trunk_hold_test.cpp)
target_include_directories(trunk_hold_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(trunk_hold_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# From sajmonsoft's flat fork: [input] portal_look_horizon and portal_auto (vr/portal_auto.h) - an arrival told apart, the walk into a portal.
add_executable(portal_auto_test cleanroom/tests/portal_auto_test.cpp)
target_include_directories(portal_auto_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(portal_auto_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [render] keep_room_heights (vr/room_heights.h): a room keeps the height the game first drew it at - the game's own stitching modelled.
# [sky] weather_first_person (vr/weather_place.h): where the game's rain and snow go in first person, the pure part
add_executable(weather_place_test cleanroom/tests/weather_place_test.cpp)
target_compile_options(weather_place_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(room_heights_test cleanroom/tests/room_heights_test.cpp)
target_include_directories(room_heights_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(room_heights_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
# [debug] log_async_auto (vr/log_slow.h): is the host's log file slow on this PC - the window, the edge, the player's 29 ms lines, the switch.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/log_slow_test.cpp")
    add_executable(log_slow_test cleanroom/tests/log_slow_test.cpp)
    target_include_directories(log_slow_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(log_slow_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# [hud] ordinary_mob_bars (vr/mob_bars.h): the plate of an ordinary monster out. Its logic is pure and has a test
# against a fake game (mob_bars_test); the hook is vr/mob_bars.cpp.
target_sources(d2rl-vrcam PRIVATE vr/mob_bars.cpp)
# [render] roofs_two_sided (vr/roof_sides.h): roof materials built double-sided. The name rule, the answer to change and the
# expected bytes (and on the game's snapshot when it is here). Run: roof_sides_test [dump_loader/D2R_image.bin]
target_sources(d2rl-vrcam PRIVATE vr/roof_sides.cpp)
add_executable(roof_sides_test cleanroom/tests/roof_sides_test.cpp)
target_include_directories(roof_sides_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(roof_sides_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [render] keep_roofs (vr/roof_keep.h) and [stereo] props_once_a_pair (vr/prop_hold.h): their expected bytes and decisions,
# and the bytes on the game's snapshot when it is here. Run: scene_holds_test [dump_loader/D2R_image.bin]
add_executable(scene_holds_test cleanroom/tests/scene_holds_test.cpp)
target_include_directories(scene_holds_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(scene_holds_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
add_executable(mob_bars_test cleanroom/tests/mob_bars_test.cpp)
target_compile_options(mob_bars_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The monster's health bar in the world (vr/plate_world.h): the maths of the head's place, and the state the parts share
add_executable(plate_world_test cleanroom/tests/plate_world_test.cpp)
target_include_directories(plate_world_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(plate_world_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The item-condition figure (vr/durability_figure.h): where it is, and the effect's moved / sized / hidden branch
add_executable(durability_figure_test cleanroom/tests/durability_figure_test.cpp)
target_compile_options(durability_figure_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The quest / level-up notice on a card in the room (vr/notice_card.h): its box in the interface layer, the card's timing
add_executable(notice_card_test cleanroom/tests/notice_card_test.cpp)
target_compile_options(notice_card_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The monster's plate and the item labels kept apart in the interface layer's effect (vr/plate_look.h)
add_executable(plate_look_test cleanroom/tests/plate_look_test.cpp)
target_compile_options(plate_look_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The places in xr.cpp's render-target heap (vr/rtv_slots.h): taken, given back, never somebody else's - the monster bar's chain made again and again wrapped it
add_executable(rtv_slots_test cleanroom/tests/rtv_slots_test.cpp)
target_include_directories(rtv_slots_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(rtv_slots_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The toolbar's type (shared/toolbar_type.h): one choice over [hud] hide and bar_place, for D2R VR Settings and the headset panel
add_executable(toolbar_type_test cleanroom/tests/toolbar_type_test.cpp)
target_compile_options(toolbar_type_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dump_loader/D2R_image.bin")
    # The hook on the game's snapshot, without the game: mob_bars_hook_test dump_loader/D2R_image.bin
    add_executable(mob_bars_hook_test cleanroom/tests/mob_bars_hook_test.cpp vr/mob_bars.cpp)
    target_include_directories(mob_bars_hook_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)   # (plate_world.h: blade_shape.h, d2r_vr_state.h)
    target_link_libraries(mob_bars_hook_test PRIVATE d2rcam)
    target_compile_options(mob_bars_hook_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# [hud] low_hp_vignette (vr/low_hp.h): the low-life heartbeat vignette. Its logic is pure (the beat, the fade, the hysteresis, the
# life maths, the vignette's shape) and tested; low_hp_test also compiles the effect's own HLSL (reshade/D2R_DepthFog.fx, LowHp) and
# runs it on WARP against the C++ mirror, and draws pictures of the vignette. Run: low_hp_test.exe [<folder for the pictures>]
target_sources(d2rl-vrcam PRIVATE vr/low_hp.cpp)
# [hud] merc_tint (vr/merc_tint.h): the wounded merc tinted red by the effect. The decisions and the effect's own HLSL (the MERC
# marks in reshade/D2R_DepthFog.fx, run on WARP against the C++ mirror) are tested. Run: merc_tint_test.exe
target_sources(d2rl-vrcam PRIVATE vr/merc_tint.cpp)
# [sky] arcane_sky (shared/arcane_sky.h, after bearstonem/d2r-pcvr, MIT): the BC4 decoder, the visual files' one change, the PNG
# (read back by WIC), the receipt, and the effect's own HLSL (the ARCANE marks in reshade/D2R_DepthFog.fx) on WARP against a C++
# mirror; the game's own files too when releases/art/bear_arcane holds them (never in the repository). Run: arcane_sky_test.exe
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/arcane_sky_test.cpp")   # (not in the public tree's allow-list)
    add_executable(arcane_sky_test cleanroom/tests/arcane_sky_test.cpp)
    target_include_directories(arcane_sky_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_definitions(arcane_sky_test PRIVATE ARCANE_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx"
                               ARCANE_ART_DIR="${CMAKE_CURRENT_SOURCE_DIR}/releases/art/bear_arcane")
    target_link_libraries(arcane_sky_test PRIVATE d3d11 d3dcompiler windowscodecs ole32)
    target_compile_options(arcane_sky_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# ... and D2R_VR_Settings' maker of those pictures (tools/settings/arcane_assets.cpp) against the game's own archives, read only,
# writing into a folder of the test's own. Run: arcane_assets_test.exe <game folder> <empty scratch folder> [<releases/art/bear_arcane>]
# [fog] spare_effects (vr/fog_spare.h): the opaque share of a pixel from the game's transparent resolve (vr/fog_spare_math.h), and the
# effect's own HLSL (the FOGSPARE marks in reshade/D2R_DepthFog.fx) on WARP against it. Run: fog_spare_test.exe
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/fog_spare_test.cpp")   # (not in the public tree's allow-list)
    add_executable(fog_spare_test cleanroom/tests/fog_spare_test.cpp)
    target_include_directories(fog_spare_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_definitions(fog_spare_test PRIVATE FOGSPARE_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
    target_link_libraries(fog_spare_test PRIVATE d3d11 d3dcompiler)
    target_compile_options(fog_spare_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [render] fog_in_frame (vr/fog_in_frame.h): the resolve's match, the game's resolve against our three steps, the fog's colour in the
# game's scene units, the stencil test, and our shaders (vr/fog_in_frame_shader.h) on WARP against vr/fog_in_frame_math.h; with
# --game-shader <ps_transparent_resolve DXBC from the player's own game> the game's own resolve on WARP too. Run: fog_in_frame_test.exe
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/fog_in_frame_test.cpp")   # (not in the public tree's allow-list)
    add_executable(fog_in_frame_test cleanroom/tests/fog_in_frame_test.cpp)
    target_include_directories(fog_in_frame_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_definitions(fog_in_frame_test PRIVATE FOGFRAME_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
    target_link_libraries(fog_in_frame_test PRIVATE d3d11 d3dcompiler)
    target_compile_options(fog_in_frame_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# ... and its maker of the knockable props' catalogue ([weight] knock_props, tools/settings/props_assets.cpp), the same way. Run: props_assets_test.exe <game folder> <empty scratch folder>
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/props_assets_test.cpp")
    add_executable(props_assets_test cleanroom/tests/props_assets_test.cpp tools/settings/props_assets.cpp)
    target_include_directories(props_assets_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/tools/settings)
    target_compile_definitions(props_assets_test PRIVATE UNICODE _UNICODE)
    target_link_libraries(props_assets_test PRIVATE casc_static)
    target_compile_options(props_assets_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [world] kurast_boat (shared/kurast_boat.h, after bearstonem/d2r-pcvr bed674b, MIT): the town preset's one change on a made-up preset,
# and D2R_VR_Settings' maker (tools/settings/boat_assets.cpp) the way props_assets_test and arcane_assets_test run, when given the game: its own docktown3.json,
# the canoe at y 0 and every other byte the game's. Run: kurast_boat_test.exe [<game folder> <empty scratch folder>]
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/kurast_boat_test.cpp")
    add_executable(kurast_boat_test cleanroom/tests/kurast_boat_test.cpp tools/settings/boat_assets.cpp)
    target_include_directories(kurast_boat_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/tools/settings)
    target_compile_definitions(kurast_boat_test PRIVATE UNICODE _UNICODE)
    target_link_libraries(kurast_boat_test PRIVATE casc_static)
    target_compile_options(kurast_boat_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# [sky] game_sky: D2R_VR_Settings' maker of the dome's models from the game's own Summit sky card (tools/settings/dome_assets.cpp,
# shared/granny_remesh.h). Run: dome_assets_test.exe <game folder> <empty scratch folder>
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/dome_assets_test.cpp")
    add_executable(dome_assets_test cleanroom/tests/dome_assets_test.cpp tools/settings/dome_assets.cpp)
    target_include_directories(dome_assets_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/tools/settings ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pybg3)
    target_compile_definitions(dome_assets_test PRIVATE UNICODE _UNICODE)
    target_link_libraries(dome_assets_test PRIVATE casc_static)
    target_compile_options(dome_assets_test PRIVATE /utf-8 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/arcane_assets_test.cpp")   # (not in the public tree's allow-list)
    add_executable(arcane_assets_test cleanroom/tests/arcane_assets_test.cpp tools/settings/arcane_assets.cpp)
    target_include_directories(arcane_assets_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/tools/settings)
    target_compile_definitions(arcane_assets_test PRIVATE UNICODE _UNICODE)
    target_link_libraries(arcane_assets_test PRIVATE casc_static windowscodecs ole32)
    target_compile_options(arcane_assets_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
add_executable(merc_tint_test cleanroom/tests/merc_tint_test.cpp)
target_include_directories(merc_tint_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_definitions(merc_tint_test PRIVATE MERC_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
target_link_libraries(merc_tint_test PRIVATE d3d11 d3dcompiler)
target_compile_options(merc_tint_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
# [debug] thrust_debug: the thrust's readout over the weapon hand (vr/thrust_debug.h, its layer vr/xr_thrust_debug.h)
target_sources(d2rl-vrcam PRIVATE vr/thrust_debug_wire.cpp vr/thrust_debug_paint.cpp)
# [hud] wrist_status (vr/wrist_status.h): the hero's companions (its pet lists) and the effects on it (its states) as small icons and counters over a wrist turned to the
# eyes. The tables, the reads of the game's lists (against a made-up heap), the cells, the settings and the hang are pure and tested (wrist_status_test, which also draws the panels:
# wrist_status_test.exe [<folder for the pictures>]); the wiring - the frames from direct_input, the painter thread, the layers asked for - is tested against stand-ins for
# direct_input and the game's read (wrist_status_wire_test). After bearstonem/d2r-pcvr (MIT).
target_sources(d2rl-vrcam PRIVATE vr/wrist_status_wire.cpp vr/wrist_status_game.cpp vr/wrist_status_paint.cpp)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/wrist_status_test.cpp")   # (not in the public tree's allow-list)
    add_executable(wrist_status_test cleanroom/tests/wrist_status_test.cpp vr/wrist_status_paint.cpp vr/wheel_icons.cpp)
    target_include_directories(wrist_status_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_definitions(wrist_status_test PRIVATE WRISTSTATUS_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vr")   # (the test scans the game side's file)
    target_link_libraries(wrist_status_test PRIVATE gdi32 user32 ole32 uuid)
    target_compile_options(wrist_status_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/wrist_status_wire_test.cpp")
    add_executable(wrist_status_wire_test cleanroom/tests/wrist_status_wire_test.cpp vr/wrist_status_wire.cpp vr/wrist_status_paint.cpp vr/wheel_icons.cpp)
    target_include_directories(wrist_status_wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(wrist_status_wire_test PRIVATE D2RLPlugin::D2RLPlugin gdi32 user32 ole32 uuid)
    target_compile_options(wrist_status_wire_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
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
    target_include_directories(skill_wheel_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr ${BODYWALK_SDK_INCLUDE})
    target_link_libraries(skill_wheel_test PRIVATE gdi32 user32 ole32 uuid)
    target_compile_options(skill_wheel_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()

# The skill panel's tab strip and its Settings tab ([openxr] headset_settings with the tiles' look: vr/tiles_geom.h's strip and sheet, vr/wheel_model.h's
# tabs, vr/wheel_controller.h hosting vr/hset_*.h's pages): the strip and sheet geometry, the tabs, the chord, the long press, the pages' clicks, Apply, the
# view buttons, Calibrate body and the window's return, the slider's drag, and the window's pictures. Run: headset_tabs_test.exe [--dump <folder>]
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/headset_tabs_test.cpp")   # (not in the public tree's allow-list)
    add_executable(headset_tabs_test cleanroom/tests/headset_tabs_test.cpp vr/wheel_paint.cpp vr/tiles_paint.cpp vr/wheel_icons.cpp)
    target_include_directories(headset_tabs_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr ${BODYWALK_SDK_INCLUDE})
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
    target_include_directories(skill_tiles_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr ${BODYWALK_SDK_INCLUDE})
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
            "-DSTAMP=${D2RVR_GEN_DIR}/d2r_vr_version.stamp" "-DD2RVR_FIRST_PERSON=${D2RVR_FIRST_PERSON}" -P "${D2RVR_VERSION_SCRIPT}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vr/vrcam.cpp" "${D2RVR_VERSION_SCRIPT}"
    COMMENT "D2R VR version from vrcam.cpp" VERBATIM)
# One target owns the generated header; the plugin (vr/vrcam.rc: its VERSIONINFO) and the settings program (its Home page and
# VERSIONINFO) both wait for it.
add_custom_target(d2r_vr_version_h DEPENDS "${D2RVR_GEN_DIR}/d2r_vr_version.stamp")
add_dependencies(d2rl-vrcam d2r_vr_version_h)
target_include_directories(d2rl-vrcam PRIVATE ${D2RVR_GEN_DIR})
# CascLib (MIT, Ladislav Zezula): D2R_VR_Settings reads the Arcane Sanctuary's star layers out of the
# player's own game, read only ([sky] arcane_sky, tools/settings/arcane_assets.cpp). Settings only - never
# the plugin. A checkout in third_party/CascLib, else the commit tools/casc_extract was built with
# (FETCHCONTENT_SOURCE_DIR_CASCLIB points it at another checkout).
set(CASC_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(CASC_UNICODE ON CACHE BOOL "" FORCE)   # Settings is a UNICODE program: CascLib's paths are wide too
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)      # CascLib's own CMakeLists asks for a CMake older than 3.5, which CMake 4 refuses
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CascLib/CMakeLists.txt")
    add_subdirectory(third_party/CascLib casclib EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(CascLib GIT_REPOSITORY https://github.com/ladislav-zezula/CascLib.git
        GIT_TAG 38a34665624b8775bb875274b36191b21c38d97b EXCLUDE_FROM_ALL)   # the CascLib of tools/casc_extract (2026-10)
    FetchContent_MakeAvailable(CascLib)
endif()
unset(CMAKE_POLICY_VERSION_MINIMUM)
add_executable(d2r_vr_settings WIN32 tools/settings/settings.cpp tools/settings/arcane_assets.cpp tools/settings/props_assets.cpp tools/settings/boat_assets.cpp tools/settings/dome_assets.cpp tools/settings/settings.rc)
add_dependencies(d2r_vr_settings d2r_vr_version_h)
target_link_libraries(d2r_vr_settings PRIVATE casc_static)
target_compile_options(d2r_vr_settings PRIVATE /utf-8 /W3 /EHsc)
# The file players see next to D2R.exe: capitals, like the game's own (2026-10-06). Its window state stays in
# d2r_vr_settings.ini.
set_target_properties(d2r_vr_settings PROPERTIES OUTPUT_NAME "D2R_VR_Settings")
# 8 MB of stack: the item table (g_items) is built at start from one initializer list of ~2700 Items on the main thread's stack - past
# the default 1 MB it died before main (0xC00000FD) once the table and Item grew (the languages, 2026-10-26)
target_link_options(d2r_vr_settings PRIVATE /STACK:8388608)
# its language files beside the built program (it looks in lang\ beside itself first; installed, they are in mods\D2R-VR\lang)
add_custom_command(TARGET d2r_vr_settings POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/lang" "$<TARGET_FILE_DIR:d2r_vr_settings>/lang")
target_compile_definitions(d2r_vr_settings PRIVATE UNICODE _UNICODE)
target_include_directories(d2r_vr_settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pybg3 ${FLATVR_SHARED_DIR} ${D2RVR_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/sigscan ${BODYWALK_SDK_INCLUDE})   # d2r_vr_state.h: the weapon in hand; afr_eye_shared.h: is FlatVR running; game_link.h: BodyWalk
# Our own debugging tools (not in the public tree).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/settings/fake_loader.cpp")
    # Stands in for D2RLoader.exe in tools/start_helper_test.py (D2R_VR_Settings.exe --start): records its arguments and environment, starts nothing.
    add_executable(fake_loader tools/settings/fake_loader.cpp)
    target_compile_options(fake_loader PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/installer/tests/fake_launcher_setup.cpp")
    # Stand-ins for the launcher's Setup that D2R VR's Setup carries (installer/tests/mod_layout_test.py): they record how they were started, and exit 0 or 5.
    add_executable(fake_launcher_setup installer/tests/fake_launcher_setup.cpp)
    target_compile_options(fake_launcher_setup PRIVATE /utf-8 /W4 /EHsc /permissive-)
    add_executable(fake_launcher_setup_fail installer/tests/fake_launcher_setup.cpp)
    target_compile_definitions(fake_launcher_setup_fail PRIVATE FAKE_EXIT=5)
    target_compile_options(fake_launcher_setup_fail PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/frame_cap_test.cpp")
    # The game's frame cap in Settings.json and what D2R VR does to it (shared/frame_cap.h): VR's rate is VR's, flat mode has the player's own.
    add_executable(frame_cap_test tools/frame_cap_test.cpp)
    target_include_directories(frame_cap_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(frame_cap_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/reshade_env_test.cpp")
    # Another mod's RESHADE_* variables the plugin clears in the game's process before ReShade64.dll loads (shared/reshade_env.h).
    add_executable(reshade_env_test tools/reshade_env_test.cpp)
    target_include_directories(reshade_env_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(reshade_env_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
    target_compile_definitions(reshade_env_test PRIVATE VRCAM_CPP="${CMAKE_CURRENT_SOURCE_DIR}/vr/vrcam.cpp")   # (the order in LoadReShade is checked in the source)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/data_mods_test.cpp")
    # Other data mods (D2RMM's output) merged into D2R VR's mod at START (shared/data_mods.h, [mods] merge_data_mods), on real files in %TEMP%.
    add_executable(data_mods_test tools/data_mods_test.cpp)
    target_include_directories(data_mods_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_definitions(data_mods_test PRIVATE UNICODE _UNICODE)
    target_compile_options(data_mods_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/reshade_as_test.cpp")
    # Home > "Keep ReShade as dxgi.dll" / "ReShade for D2R VR only" (shared/reshade_as.h): the files they move, in the mod layout and before it, on real files in %TEMP%.
    add_executable(reshade_as_test tools/reshade_as_test.cpp)
    target_include_directories(reshade_as_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(reshade_as_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/reshade_paths_test.cpp")
    # D2R VR's folders in the search paths of the ReShade a player has as dxgi.dll (shared/reshade_paths.h): append-only, idempotent, byte-exact on the rest.
    add_executable(reshade_paths_test tools/reshade_paths_test.cpp)
    target_include_directories(reshade_paths_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(reshade_paths_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/safe_screen_test.cpp")
    # The game's interface size in Settings.json ("Safe Screen Percent") and what D2R VR does to it (shared/safe_screen.h): VR starts it at 80 where the player chose none, flat mode has his own.
    add_executable(safe_screen_test tools/safe_screen_test.cpp)
    target_include_directories(safe_screen_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(safe_screen_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/gpu_fault_test.cpp")
    # The device-lost report (vr/gpu_fault.cpp) against a real device taken away with RemoveDevice: gpu_fault_test (WARP, no game, no headset)
    add_executable(gpu_fault_test tools/gpu_fault_test.cpp vr/gpu_fault.cpp)
    target_include_directories(gpu_fault_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(gpu_fault_test PRIVATE d3d12 dxgi)
    target_compile_options(gpu_fault_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/stall_guard_test.cpp")
    # The stall guard's logic (vr/stall_guard.h): a queue that stands still told from one that is busy, the levels, the fences to signal from the CPU
    add_executable(stall_guard_test tools/stall_guard_test.cpp)
    target_include_directories(stall_guard_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(stall_guard_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/replay_groups_test.cpp")
    # The replay's submissions (vr/replay_groups.h) on synthetic captures: which lists were one call of the game, how each mode submits them
    add_executable(replay_groups_test tools/replay_groups_test.cpp)
    target_include_directories(replay_groups_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(replay_groups_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/ecl_hook_test.cpp")
    # The ExecuteCommandLists hook (vr/ecl_hook.cpp) on a WARP device: the game's calls seen with their lists, the call goes on, the lists numbered as the game's calls
    add_executable(ecl_hook_test tools/ecl_hook_test.cpp vr/ecl_hook.cpp)
    target_include_directories(ecl_hook_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(ecl_hook_test PRIVATE minhook d3d12 dxgi)
    target_compile_options(ecl_hook_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/fx_compile_test.cpp")
    # reshade/D2R_DepthFog.fx through the vendored ReShade FX compiler and D3DCompile, as [render] no_reshade compiles it: every entry point, timed
    add_executable(fx_compile_test tools/fx_compile_test.cpp)
    target_link_libraries(fx_compile_test PRIVATE reshadefx d3dcompiler)
    target_compile_definitions(fx_compile_test PRIVATE FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
    target_compile_options(fx_compile_test PRIVATE /utf-8 /W4 /EHsc /permissive- /Zc:char8_t-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/fx_native_test.cpp")
    # [render] no_reshade's own runtime for D2R_DepthFog.fx (vr/fx_native.cpp) on the graphics card: built in the background, uniforms as ReShade
    # lays them out, the fog drawn by the real effect (full, half, off), a rebuild on a definition. fx_native_test.exe <cache folder>
    add_executable(fx_native_test tools/fx_native_test.cpp vr/fx_native.cpp)
    target_include_directories(fx_native_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${RESHADE_SDK_INCLUDE})
    target_link_libraries(fx_native_test PRIVATE reshadefx d3d12 dxgi d3dcompiler windowscodecs ole32)
    target_compile_definitions(fx_native_test PRIVATE FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx" _CRT_SECURE_NO_WARNINGS)
    target_link_options(fx_native_test PRIVATE /STACK:268435456)   # (the debug layer's GPU validation goes deep on the effect's biggest shaders: FXN_GBV=1)
    target_compile_options(fx_native_test PRIVATE /utf-8 /W4 /EHsc /permissive- /Zc:char8_t-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/sky_dome_test.cpp")
    # [sky] game_sky (vr/sky_dome.h): when the game's dome is shown, its matrix, whether the fog and our passes leave it alone
    add_executable(sky_dome_test cleanroom/tests/sky_dome_test.cpp)
    target_include_directories(sky_dome_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(sky_dome_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/dxbc_fog_height_test.cpp")
    # [fog] game_fog_top / game_fog_soft: the game's fog shader's bytecode patched to thin the fog with height (shared/dxbc_fog_height.h);
    # drawn on WARP. Run: dxbc_fog_height_test.exe [<folder of the game's ps_atmosphere_apply permutations permNN.dxbc>]
    add_executable(dxbc_fog_height_test cleanroom/tests/dxbc_fog_height_test.cpp)
    target_include_directories(dxbc_fog_height_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_link_libraries(dxbc_fog_height_test PRIVATE d3d11 d3dcompiler)
    target_compile_options(dxbc_fog_height_test PRIVATE /utf-8 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/effects_layer_native_test.cpp")
    # [stereo] turns_keep_own 3 / 4 without ReShade: vr/effects_layer.cpp fed from the plugin's own hooks on a WARP device - the layer found,
    # its states from the barriers, held while it is the layer
    add_executable(effects_layer_native_test cleanroom/tests/effects_layer_native_test.cpp vr/effects_layer.cpp)
    target_include_directories(effects_layer_native_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr ${RESHADE_SDK_INCLUDE})
    target_link_libraries(effects_layer_native_test PRIVATE d3d12 dxgi)
    target_compile_options(effects_layer_native_test PRIVATE /utf-8 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/fog_before_fx_test.cpp")
    # [fog] game_fog_before_fx: vr/fog_before_fx.cpp on a WARP D3D12 device, inside a list recorded as the game's frame end - the scene fogged
    # before the resolve by the game's formula, the void and off left alone, the game's heap, table and compute state given back
    add_executable(fog_before_fx_test cleanroom/tests/fog_before_fx_test.cpp vr/fog_before_fx.cpp vr/fog_height.cpp)
    target_include_directories(fog_before_fx_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(fog_before_fx_test PRIVATE d3d12 d3dcompiler dxgi)
    target_compile_options(fog_before_fx_test PRIVATE /utf-8 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/fog_height_test.cpp")
    # vr/fog_height.cpp on a WARP D3D12 device: the fog pass's pipeline kept, a variant made and bound in its place while on
    add_executable(fog_height_test cleanroom/tests/fog_height_test.cpp vr/fog_height.cpp)
    target_include_directories(fog_height_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(fog_height_test PRIVATE d3d12 d3dcompiler dxgi)
    target_compile_options(fog_height_test PRIVATE /utf-8 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/game_ceiling_test.cpp")
    # [ceiling] game_ceiling (shared/game_ceiling.h, granny_remesh.h CeilingMesh): when the game's rock slab shows, where it is laid, the slab
    # the same in every tile, its unit; [ceiling] height_from_walls, game_ceiling_beams and game_ceiling_dome (shared/ceiling_measures.h). Run:
    # game_ceiling_test.exe [<the game's rocks03_lod1.model> [<out .model> [<the game's wood_ceiling_frame01_lod0.model>]]]
    add_executable(game_ceiling_test cleanroom/tests/game_ceiling_test.cpp)
    target_include_directories(game_ceiling_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pybg3)
    target_compile_options(game_ceiling_test PRIVATE /utf-8 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/granny_remesh_test.cpp")
    # [sky] game_sky: the dome made from the game's own Summit sky card (shared/granny_remesh.h; BitKnit2 by third_party/pybg3, MIT).
    # Run: granny_remesh_test.exe [<the game's skybox01_lod1.model> [<out .model>]]
    add_executable(granny_remesh_test cleanroom/tests/granny_remesh_test.cpp)
    target_include_directories(granny_remesh_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pybg3)
    target_compile_options(granny_remesh_test PRIVATE /utf-8 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/game_sky/gr2dump.cpp")
    # Reads a Granny 2 file - the game's own .model (BitKnit2 sections: third_party/pybg3/rans.h, MIT) or ours - as a tree (tools/game_sky)
    add_executable(gr2dump tools/game_sky/gr2dump.cpp)
    target_include_directories(gr2dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pybg3)
    target_compile_features(gr2dump PRIVATE cxx_std_20)
    target_compile_options(gr2dump PRIVATE /utf-8 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/game_sky/bc6h_gpu_check.cpp")
    # [sky] game_sky: the GPU's own decode of a BC6H .dds (D3D11 on WARP) - tools/game_sky/bc6h_test.py compares it with bc6h.py's
    add_executable(bc6h_gpu_check tools/game_sky/bc6h_gpu_check.cpp)
    target_link_libraries(bc6h_gpu_check PRIVATE d3d11 d3dcompiler)
    target_compile_options(bc6h_gpu_check PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/fx_picture_keep_test.cpp")
    # [render] no_reshade: only the act's sky and the ceiling's stone on the graphics card (vr/fx_picture_keep.h) - the rule, and against the
    # effect's own pictures (reshade/D2R_DepthFog.fx)
    add_executable(fx_picture_keep_test tools/fx_picture_keep_test.cpp)
    target_include_directories(fx_picture_keep_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_definitions(fx_picture_keep_test PRIVATE PICTURE_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
    target_compile_options(fx_picture_keep_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/overlay_work_test.cpp")
    # Eyes by turns: the overlay techniques rendered only with something on (vr/overlay_work.h) - the decision on fake reads, and every switch the
    # effect's overlay passes branch on (reshade/D2R_DepthFog.fx) in the gate
    add_executable(overlay_work_test tools/overlay_work_test.cpp)
    target_include_directories(overlay_work_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_definitions(overlay_work_test PRIVATE OVERLAY_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
    target_compile_options(overlay_work_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/no_reshade_test.cpp")
    # [render] no_reshade's decisions (vr/no_reshade.h): ReShade loaded or not, the mode it is for, what the log says is off
    add_executable(no_reshade_test tools/no_reshade_test.cpp)
    target_include_directories(no_reshade_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(no_reshade_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/monitor_eye_test.cpp")
    # [render] monitor_eye (vr/monitor_eye.h): the window one eye - the maths played through the stereo modes, and vr/monitor_eye.cpp on a WARP
    # device: each present read back is the wanted eye, the crop stretched over a wide window (MONITOR_EYE_DEBUG=1: the debug layer, 0 errors)
    add_executable(monitor_eye_test tools/monitor_eye_test.cpp vr/monitor_eye.cpp)
    target_include_directories(monitor_eye_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(monitor_eye_test PRIVATE d3d12 dxgi)
    target_compile_options(monitor_eye_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/hud_native_test.cpp")
    # [render] no_reshade: vr/hud.cpp on the plugin's own D3D12 hooks through the stand-in for ReShade's API (vr/native_rsapi.h), on WARP
    add_executable(hud_native_test tools/hud_native_test.cpp vr/hud.cpp vr/native_d3d12.cpp vr/native_rsapi.cpp)
    target_include_directories(hud_native_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared ${FLATVR_SHARED_DIR} ${RESHADE_SDK_INCLUDE})
    target_link_libraries(hud_native_test PRIVATE minhook d3d12 dxgi)
    target_compile_options(hud_native_test PRIVATE /utf-8 /W3 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/native_d3d12_test.cpp")
    # [render] no_reshade's own hooks (vr/native_d3d12.cpp) on a WARP device and a hidden window: the device, the swap chain, its presents and resizes,
    # the depth-stencil binds, barriers and viewports reach the sinks; the plugin's own ("d2rvr ...") depth-stencils and our own list do not
    add_executable(native_d3d12_test tools/native_d3d12_test.cpp vr/native_d3d12.cpp)
    target_include_directories(native_d3d12_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(native_d3d12_test PRIVATE minhook d3d12 dxgi)
    target_compile_options(native_d3d12_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/xr_busy_test.cpp")
    # A runtime call that never returns (xrWaitFrame under a headset another app holds) does not hold the draw thread (vr/bounded_call.h); FlatVR over the headset (vr/flatvr_gate.h)
    add_executable(xr_busy_test tools/xr_busy_test.cpp)
    target_include_directories(xr_busy_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(xr_busy_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/pointer_cache_test.cpp")
    # The mouse pointer's images kept by what they show: the game's pointer made anew under a new handle makes no swapchain (vr/pointer_cache.h)
    add_executable(pointer_cache_test tools/pointer_cache_test.cpp)
    target_include_directories(pointer_cache_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(pointer_cache_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/chain_reaper_test.cpp")
    # A swapchain made again in a session is destroyed only once the game's queue was drained, never inside a present that would wait for it (vr/chain_reaper.h)
    add_executable(chain_reaper_test tools/chain_reaper_test.cpp)
    target_include_directories(chain_reaper_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(chain_reaper_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
# The light-bounds replacement shader on a software D3D11 device against a scan of every pixel; with the game's own
# shader (extracted on your machine) as the argument it also shows the bug and that square tiles are the same bit for bit.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/light_bounds_test.cpp")
    add_executable(light_bounds_test cleanroom/tests/light_bounds_test.cpp)
    target_link_libraries(light_bounds_test PRIVATE d3d11 d3dcompiler)
    target_compile_options(light_bounds_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/light_bounds_native_test.cpp")
    # [render] light_bounds_fix without ReShade: the replacement made and bound without ReShade, on a software D3D12 device (WARP)
    add_executable(light_bounds_native_test cleanroom/tests/light_bounds_native_test.cpp vr/light_bounds.cpp)
    target_include_directories(light_bounds_native_test PRIVATE ${RESHADE_SDK_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(light_bounds_native_test PRIVATE d3d12 dxgi d3dcompiler)
    target_compile_options(light_bounds_native_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
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
# [bow] pitch_aim (vr/bow_pitch.h): the shot aimed by its 3D line - the ray against the bodies, the ground's shear, the stairs: bow_pitch_test
add_executable(bow_pitch_test cleanroom/tests/bow_pitch_test.cpp)
target_include_directories(bow_pitch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
target_compile_options(bow_pitch_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [gameplay] leap_by_gaze (vr/leap_gaze.h): Leap and Leap Attack where the head looks - the gaze to the ground, the range clamp, the monster under it: leap_gaze_test
add_executable(leap_gaze_test cleanroom/tests/leap_gaze_test.cpp)
target_compile_options(leap_gaze_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [body] under_camera (vr/under_camera.h): the drawn body under the camera while the game's camera trails the hero on its spring - a leap, a knockback: under_camera_test
add_executable(under_camera_test cleanroom/tests/under_camera_test.cpp)
target_compile_options(under_camera_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [gameplay] teleport_circle (vr/point_cast.h): the tap against the hold, the arc's landing from the hand's pose, the cap, the skill list, the ring: point_cast_test
add_executable(point_cast_test cleanroom/tests/point_cast_test.cpp)
target_compile_options(point_cast_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# ... and the effect's own beam and ring (reshade/D2R_DepthFog.fx, the PARC marks) compiled and run on WARP: point_arc_fx_test
add_executable(point_arc_fx_test cleanroom/tests/point_arc_fx_test.cpp)
target_compile_definitions(point_arc_fx_test PRIVATE POINTARC_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
target_link_libraries(point_arc_fx_test PRIVATE d3d11 d3dcompiler)
target_compile_options(point_arc_fx_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [bow] arrow_flight (vr/arrow_flight.h): the shot arrow drawn by us - which missiles, where ours is as the game's flies, the matrices: arrow_flight_test
add_executable(arrow_flight_test cleanroom/tests/arrow_flight_test.cpp)
target_compile_options(arrow_flight_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The string hand's closing (vr/bow_hand.h): what asks the fingers to close, the amount, the easing: bow_hand_test
add_executable(bow_hand_test cleanroom/tests/bow_hand_test.cpp)
target_compile_options(bow_hand_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [debug] bow_latency (vr/bow_latency.h): the split of the second between a bow's release and its arrow: bow_latency_test
add_executable(bow_latency_test cleanroom/tests/bow_latency_test.cpp)
target_compile_options(bow_latency_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
target_sources(d2rl-vrcam PRIVATE vr/bow_draw_wire.cpp)   # [hands] bow_draw (wired to vr/direct_input.h)
target_sources(d2rl-vrcam PRIVATE vr/point_cast_wire.cpp)   # [gameplay] teleport_circle: a point skill's press held for an arc and a circle (vr/point_cast.h)
target_sources(d2rl-vrcam PRIVATE vr/bow_pitch_wire.cpp)   # [bow] pitch_aim: the scene a bow's shot is decided in (vr/bow_pitch.h), the heading the aim hooks give it
target_sources(d2rl-vrcam PRIVATE vr/arrow_flight_wire.cpp)   # [bow] arrow_flight: the hero's shot arrow drawn by us (vr/arrow_flight.h), on the potion hide's renderer hooks
# [bow] quiver (vr/bow_quiver.h): the bow skill table, the glow and the arrow's element: bow_quiver_test (its old request-machine checks gave way to bow_quiver_armed_test)
add_executable(bow_quiver_test cleanroom/tests/bow_quiver_test.cpp)
target_compile_options(bow_quiver_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# ... and the weapon-neutral part (vr/quiver_input.h): the controls' names: quiver_input_test
add_executable(quiver_input_test cleanroom/tests/quiver_input_test.cpp)
target_compile_options(quiver_input_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [bow] quiver_orbs (vr/bow_orbs.h): the orbs along the bow's stave and the grip that takes one, on synthetic numbers: bow_orbs_test
add_executable(bow_orbs_test cleanroom/tests/bow_orbs_test.cpp)
target_compile_options(bow_orbs_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [bow] arrow_fire_cut (vr/arrow_fire_cut.h): the arrow's effect out at the release that shoots, at once or after the set delay, back on the next arrow: arrow_fire_cut_test
add_executable(arrow_fire_cut_test cleanroom/tests/arrow_fire_cut_test.cpp)
target_compile_options(arrow_fire_cut_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [bow] quiver_plain_no_mana (vr/bow_mana.h): what a bow skill costs by the game's own row, when the hero cannot pay it, the mana at the release: bow_mana_test
add_executable(bow_mana_test cleanroom/tests/bow_mana_test.cpp)
target_compile_options(bow_mana_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# ... and the effect's own HLSL for the orbs (the QUIVER-ORBS block of reshade/D2R_DepthFog.fx), compiled and run on a software D3D11 device: bow_orbs_fx_test
add_executable(bow_orbs_fx_test cleanroom/tests/bow_orbs_fx_test.cpp)
target_compile_definitions(bow_orbs_fx_test PRIVATE QUIVER_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
target_link_libraries(bow_orbs_fx_test PRIVATE d3d11 d3dcompiler)
target_compile_options(bow_orbs_fx_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# ... and the nocked arrow's fire, frost and arcane glow (the ARROWFX block of the same effect), run the same way; --bench times it on the real GPU: bow_arrow_fx_test
add_executable(bow_arrow_fx_test cleanroom/tests/bow_arrow_fx_test.cpp)
target_compile_definitions(bow_arrow_fx_test PRIVATE ARROWFX_FX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/reshade/D2R_DepthFog.fx")
target_link_libraries(bow_arrow_fx_test PRIVATE d3d11 d3dcompiler dxgi)
target_compile_options(bow_arrow_fx_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
target_sources(d2rl-vrcam PRIVATE vr/bow_quiver_wire.cpp)   # ... and its wire: the draw's hook, the wheel's reads and setter, the glow for the effect
target_sources(d2rl-vrcam PRIVATE vr/anim_cut.cpp)        # [bow] quick_release and [hands] quick_throw (the action-frame flag of the hero's bow attack and throw rows; the pure part vr/anim_cut.h)
# ... its pure part: the row's shape check, the move and the way back, the walk over a block (anim_cut_test)
add_executable(anim_cut_test cleanroom/tests/anim_cut_test.cpp)
target_compile_options(anim_cut_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/bow_quiver_armed_test.cpp")
    # [bow] quiver, armed (vr/bow_quiver.h Machine / DecideShot / Cleanup): arm, disarm, the shot with a bow (the skill, the plain arrow when it cannot be paid), melee with an
    # armed bow skill untouched, the weapon swap, the old quiver's arrow on A given back as the plain attack
    add_executable(bow_quiver_armed_test cleanroom/tests/bow_quiver_armed_test.cpp)
    target_include_directories(bow_quiver_armed_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(bow_quiver_armed_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/melee_arm_fit_test.cpp")
    # [gameplay] melee_skill_armed (vr/melee_arm.h NeedOf / Fits): an armed skill the weapon in hand cannot use is the plain attack
    add_executable(melee_arm_fit_test cleanroom/tests/melee_arm_fit_test.cpp)
    target_include_directories(melee_arm_fit_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(melee_arm_fit_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/press_trace_test.cpp")
    # [debug] melee_press_trace (vr/press_trace.h): one line per attack press of hit by touch - the pad polls, the slot reads, the target lookups, the hero's mode, the hit -
    # and the first step that did not happen
    add_executable(press_trace_test cleanroom/tests/press_trace_test.cpp)
    target_include_directories(press_trace_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_options(press_trace_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/crouch_test.cpp")
    # [body] crouch (vr/crouch.h): when the player crouches (threshold, hysteresis, standing again, seated, lost tracking) and the legs' bend, on synthetic numbers
    add_executable(crouch_test cleanroom/tests/crouch_test.cpp)
    target_include_directories(crouch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(crouch_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/typed_keys_test.cpp")
    # [openxr] laser_typing (vr/typed_keys.h, after bearstonem/d2r-pcvr 34f20a8): a real key lets the keyboard layout in, the plugin's own never
    add_executable(typed_keys_test cleanroom/tests/typed_keys_test.cpp)
    target_include_directories(typed_keys_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(typed_keys_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/turn_math_test.cpp")
    # [stereo] turn_warp (vr/turn_math.h): eyes by turns - a point moved with its depth lands where the other eye sees it, the turn, what the hero carries, the edge cut
    add_executable(turn_math_test cleanroom/tests/turn_math_test.cpp)
    target_include_directories(turn_math_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_compile_options(turn_math_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/tests/turn_warp_gpu_test.cpp")
    # [stereo] turn_warp (vr/turn_warp.cpp): the eye made's passes on D3D12's software device - moved with the depth, the drawn eye's edges
    # to the pixel, its own picture kept where the same ([stereo] turns_keep_own: everywhere, near things, the game's effects), the warm-up
    add_executable(turn_warp_gpu_test cleanroom/tests/turn_warp_gpu_test.cpp vr/turn_warp.cpp)
    target_include_directories(turn_warp_gpu_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
    target_link_libraries(turn_warp_gpu_test PRIVATE d3d12 dxgi)
    target_compile_options(turn_warp_gpu_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
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
# The first-start guide's flow (vr/first_start.h: when it asks the settings window for its Guide tab, how it ends) and its pages' words
# (vr/first_start_pages.h), on synthetic frames: first_start_test
add_executable(first_start_test cleanroom/tests/first_start_test.cpp)
target_include_directories(first_start_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(first_start_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The corner map for the hand kept in a corner (vr/minimap_side.h: the game's map-mode setter's globals from its own bytes, when it is asked for): minimap_side_test
add_executable(minimap_side_test cleanroom/tests/minimap_side_test.cpp)
target_include_directories(minimap_side_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(minimap_side_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# The corner map's scissor box found at any window size, shape and interface size (vr/minimap_box.h, hud.cpp IsMapScissor): minimap_box_test
add_executable(minimap_box_test cleanroom/tests/minimap_box_test.cpp)
target_include_directories(minimap_box_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(minimap_box_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [openxr] hand_inventory: away from town the inventory on the left hand, the left stick walking on (vr/hand_inventory.h, direct_layout.h's two rules): hand_inventory_test
add_executable(hand_inventory_test cleanroom/tests/hand_inventory_test.cpp)
target_include_directories(hand_inventory_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr ${CMAKE_CURRENT_SOURCE_DIR}/shared ${BODYWALK_SDK_INCLUDE})
target_compile_options(hand_inventory_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hud] map_direction: the way our camera looks on the game's isometric map, and the arrow as rows of pixels (vr/map_direction.h, hud.cpp): map_direction_test
add_executable(map_direction_test cleanroom/tests/map_direction_test.cpp)
target_include_directories(map_direction_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(map_direction_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hud] map_top_down / map_rotate / map_compass: the map in the hand from above, turned with the look, and the compass's window (vr/map_turn.h, xr.cpp): map_turn_test
add_executable(map_turn_test cleanroom/tests/map_turn_test.cpp)
target_include_directories(map_turn_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(map_turn_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hud] map_click_big: the laser on the map in the hand, the click that makes it big and the button that turns it (vr/map_laser.h, xr.cpp): map_laser_test
add_executable(map_laser_test cleanroom/tests/map_laser_test.cpp)
target_include_directories(map_laser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(map_laser_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
# [hud] map_stick_zoom: the right stick zooms the map the laser is on (vr/map_zoom.h, xr.cpp, gamestate.cpp): map_zoom_test
add_executable(map_zoom_test cleanroom/tests/map_zoom_test.cpp)
target_include_directories(map_zoom_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vr)
target_compile_options(map_zoom_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
