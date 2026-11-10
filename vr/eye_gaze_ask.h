// eye_gaze_ask.h - [render] foveated + foveated_gaze: whether a new headset session asks the runtime for the eyes' gaze
// (XR_EXT_eye_gaze_interaction, vr/xr.cpp StartSession).
//
// Serg, 2026-11-10 (0.211.1, PS VR2 on SteamVR, which offers the gaze): "foveated rendering does not follow the eyes" - every session logged
// "eye gaze offered - not asked for". The ask was decided from g_cfg.eyeGaze, read once when the plugin loaded its settings; the session
// (the instance, where an extension can be enabled) starts later, at the first present, and the rate image itself is switched live. An ini
// that said foveated=1 only after the plugin's first read (a session started with it off and switched on live, or the file written while
// the game started - that session's foveated hooks saw foveated=1 two seconds after xr.cpp's read) left the gaze un-asked for the whole
// session, and the log did not say which key or when. Now the keys are read from the file at the moment the session starts, the log says
// what was read, and switching foveated on in a session that did not ask says that the gaze waits for the next session (a game restart).
// Pure but for the file read: cleanroom/tests/eye_gaze_ask_test.cpp.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>

namespace eyegazeask {

struct Keys {
    int foveated = 0;   // [render] foveated (default 0)
    int gaze = 1;       // [render] foveated_gaze (default 1)
};

// The keys as the file has them now.
inline Keys Read(const wchar_t* ini) {
    Keys k;
    if (!ini || !ini[0]) return k;
    k.foveated = (int)GetPrivateProfileIntW(L"render", L"foveated", 0, ini);
    k.gaze = (int)GetPrivateProfileIntW(L"render", L"foveated_gaze", 1, ini);
    return k;
}

// The session asks for the gaze: both keys on (and the runtime offers the extension - the caller's part).
inline bool Wanted(const Keys& k) { return k.foveated != 0 && k.gaze != 0; }

// The log's words for this session's ask: `offered` the runtime lists XR_EXT_eye_gaze_interaction.
inline void Describe(const Keys& k, bool offered, char* out, size_t n) {
    if (!offered) { snprintf(out, n, "not offered"); return; }
    if (Wanted(k)) { snprintf(out, n, "offered - asked for ([render] foveated=%d, foveated_gaze=%d at this session's start)", k.foveated, k.gaze); return; }
    snprintf(out, n, "offered - NOT asked for: [render] foveated=%d, foveated_gaze=%d in d2r_vr.ini when this headset session started (both 1 "
                     "for the gaze; switched on later, it is asked at the next session - restart the game)", k.foveated, k.gaze);
}

// A live settings read during a session: the keys now want the gaze, the runtime offers it, but this session did not ask - say so once
// (true: the line is due). `told` is the caller's (reset when a session starts).
inline bool RestartNeeded(const Keys& now, bool offered, bool asked, bool* told) {
    if (!offered || asked || !Wanted(now) || *told) return false;
    *told = true;
    return true;
}

}  // namespace eyegazeask
