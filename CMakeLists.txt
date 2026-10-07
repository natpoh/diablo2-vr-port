cmake_minimum_required(VERSION 3.29)
project(d2r_vr LANGUAGES C CXX RC)

#   d2rl-vrcam.dll    game side, D2RLoader plugin: the VR layer (vr/) on our own
#                     camera and render distance (cleanroom/) - no D2R-3D code
#   d2r_bridge.dll    BodyWalk side, output plugin (head yaw -> shared memory)
# D2R-3D/ is kept for reference only; nothing here builds from it.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")   # no VC runtime to ship next to the game

# First person (shared/d2r_vr_build.h): off = the public build's views only (F1 - F3).
option(D2RVR_FIRST_PERSON "Build first person: arms, hands, held weapons, VR F4 and flat F3" ON)
add_compile_definitions(D2RVR_FIRST_PERSON=$<IF:$<BOOL:${D2RVR_FIRST_PERSON}>,1,0>)

# Headers from BodyWalk and FlatVR (the shared-memory blocks, the plugin API) and the
# ReShade SDK: in external/ when this is the public tree (tools/make_public.py), else
# from the BodyWalk source tree beside it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external")
    set(_ext "${CMAKE_CURRENT_SOURCE_DIR}/external")
    set(BODYWALK_SRC "${_ext}/bodywalk" CACHE PATH "Dir holding plugin_api.h and pad_mirror_shared.h")
    set(FLATVR_SHARED_DIR "${_ext}/flatvr" CACHE PATH "Dir holding FlatVR's shared-memory headers")
    set(RESHADE_SDK_INCLUDE "${_ext}/reshade/include" CACHE PATH "ReShade SDK include dir")
else()
    set(BODYWALK_SRC "C:/wsl/vjoy/cpp_src/src" CACHE PATH "Dir holding plugin_api.h and pad_mirror_shared.h")
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

set(MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook")   # BSD, Tsuda Kageyu
add_library(minhook STATIC
    ${MINHOOK_DIR}/src/buffer.c ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c ${MINHOOK_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)

# Our camera and render distance, written clean-room from the game (cleanroom/).
add_library(d2rcam STATIC cleanroom/camera/d2rcam.cpp cleanroom/drawdist/drawdist.cpp cleanroom/sigscan/sigscan.cpp)
target_include_directories(d2rcam PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/camera ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/sigscan)
target_link_libraries(d2rcam PUBLIC D2RLPlugin::D2RLPlugin)
target_compile_definitions(d2rcam PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(d2rcam PRIVATE /utf-8 /W4 /EHsc /permissive-)

add_library(d2rl-vrcam SHARED vr/vrcam.cpp vr/skeletons.cpp vr/gamestate.cpp vr/gamecmd.cpp vr/probe.cpp vr/uitrace.cpp vr/hud.cpp vr/memdiff.cpp vr/writewatch.cpp vr/vrcam.rc)
target_include_directories(d2rl-vrcam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${FLATVR_SHARED_DIR} ${RESHADE_SDK_INCLUDE} ${BODYWALK_SRC})
target_link_libraries(d2rl-vrcam PRIVATE d2rcam minhook user32)
target_compile_options(d2rl-vrcam PRIVATE /utf-8 /W3 /EHsc /permissive-)

add_library(d2r_bridge SHARED bodywalk_bridge/d2r_bridge.cpp)
target_include_directories(d2r_bridge PRIVATE ${BODYWALK_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/shared ${FLATVR_SHARED_DIR})
target_link_libraries(d2r_bridge PRIVATE advapi32)
target_compile_options(d2r_bridge PRIVATE /utf-8 /W3 /EHsc)

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
target_include_directories(d2r_vr_settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared ${FLATVR_SHARED_DIR} ${D2RVR_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cleanroom/sigscan)   # d2r_vr_state.h: the weapon in hand; afr_eye_shared.h: is FlatVR running
# Our own debugging tools (not in the public tree).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/probe_test.cpp")
    # The skeleton probe against a fake Granny skeleton in its own memory.
    add_executable(probe_test tools/probe_test.cpp vr/probe.cpp)
    target_compile_options(probe_test PRIVATE /utf-8 /W3 /EHsc)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dump_loader/D2R_image.bin")
    # The address resolver on the game's snapshot: sigscan_test dump_loader/D2R_image.bin
    add_executable(sigscan_test cleanroom/tests/sigscan_test.cpp)
    target_link_libraries(sigscan_test PRIVATE d2rcam)
    target_compile_options(sigscan_test PRIVATE /utf-8 /W4 /EHsc /permissive-)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/heapdiff/heapdiff.cpp")
    add_executable(heapdiff tools/heapdiff/heapdiff.cpp)   # find a value in the running game's heap by changing it
    target_compile_options(heapdiff PRIVATE /utf-8 /W3 /EHsc)
    target_link_libraries(heapdiff PRIVATE psapi)
endif()
