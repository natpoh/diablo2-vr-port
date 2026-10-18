# The mod's version for d2r_vr_settings.exe: vrcam's own g_info_version in
# vr/vrcam.cpp, the one number (installer/build_installer.ps1 reads it there too,
# with the same pattern). Writes OUT only when the number changed, so editing
# vrcam.cpp does not rebuild the settings program; touches STAMP if given.
#   cmake -DSRC=vr/vrcam.cpp -DOUT=<header> [-DSTAMP=<file>] -P version.cmake
file(STRINGS "${SRC}" _line REGEX "g_info_version\\[\\] = \"[0-9.]+\"" LIMIT_COUNT 1 ENCODING UTF-8)
if(NOT _line MATCHES "g_info_version\\[\\] = \"([0-9.]+)\"")
    message(FATAL_ERROR "No g_info_version[] = \"x.y.z\" in ${SRC}: the settings program would not know its version")
endif()
# "0.166.0" -> 0,166,0,0: the numeric form of a Windows VERSIONINFO (the plugin's and the settings program's .rc)
set(_ver "${CMAKE_MATCH_1}")
string(REPLACE "." "," _rc "${_ver}")
string(REGEX MATCHALL "," _commas "${_rc}")
list(LENGTH _commas _ncommas)
while(_ncommas LESS 3)
    string(APPEND _rc ",0")
    math(EXPR _ncommas "${_ncommas} + 1")
endwhile()
# The edition: the public tree's build (D2RVR_FIRST_PERSON off, tools/make_public.py) has no first person.
set(_edition "full")
if(DEFINED D2RVR_FIRST_PERSON AND NOT D2RVR_FIRST_PERSON)
    set(_edition "public")
endif()
string(CONCAT _text
    "// Made from vr/vrcam.cpp (g_info_version) by tools/settings/version.cmake - do not edit.\n"
    "#pragma once\n"
    "#define D2RVR_VERSION \"${_ver}\"\n"
    "#define D2RVR_VERSION_W L\"${_ver}\"\n"
    "#define D2RVR_VERSION_RC ${_rc}\n"
    "// who this is, for the VERSIONINFO of the plugin and the settings program (the launcher and a support log read it)\n"
    "#define D2RVR_MOD_ID \"d2r-vr\"\n"
    "#define D2RVR_EDITION \"${_edition}\"\n"
    "#define D2RVR_COMMENTS \"mod-id=d2r-vr edition=${_edition}\"\n")
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
