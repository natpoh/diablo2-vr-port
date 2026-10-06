# The mod's version for d2r_vr_settings.exe: vrcam's own g_info_version in
# vr/vrcam.cpp, the one number (installer/build_installer.ps1 reads it there too,
# with the same pattern). Writes OUT only when the number changed, so editing
# vrcam.cpp does not rebuild the settings program; touches STAMP if given.
#   cmake -DSRC=vr/vrcam.cpp -DOUT=<header> [-DSTAMP=<file>] -P version.cmake
file(STRINGS "${SRC}" _line REGEX "g_info_version\\[\\] = \"[0-9.]+\"" LIMIT_COUNT 1 ENCODING UTF-8)
if(NOT _line MATCHES "g_info_version\\[\\] = \"([0-9.]+)\"")
    message(FATAL_ERROR "No g_info_version[] = \"x.y.z\" in ${SRC}: the settings program would not know its version")
endif()
string(CONCAT _text
    "// Made from vr/vrcam.cpp (g_info_version) by tools/settings/version.cmake - do not edit.\n"
    "#pragma once\n"
    "#define D2RVR_VERSION \"${CMAKE_MATCH_1}\"\n"
    "#define D2RVR_VERSION_W L\"${CMAKE_MATCH_1}\"\n")
set(_old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" _old)
endif()
if(NOT _old STREQUAL _text)
    file(WRITE "${OUT}" "${_text}")
endif()
if(STAMP)
    file(TOUCH "${STAMP}")
endif()
