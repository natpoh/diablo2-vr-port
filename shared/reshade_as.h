#pragma once
// reshade_as.h - D2R VR Settings > Home > "The game and ReShade": which ReShade file is there, and the two buttons that act on a dxgi.dll
// (tools/reshade_as_test.cpp tests it on real files, without the window).
//
// The rule (Serg, 2026-10-18): a dxgi.dll that is in the game's folder already is the PLAYER'S own ReShade, and D2R VR uses it - nothing of ours renames,
// moves or deletes it, not Setup, not the uninstaller, not a button that does something else. How the game loads ReShade, which decides the rest:
//  - A dxgi.dll is taken by the game itself, only from D2R.exe's folder, at process start; that ReShade takes the game's folder for its base (its
//    ReShade.ini, its reshade-shaders) and the plugin joins it (vr/vrcam.cpp LoadReShade). D2R VR's files - D2R_DepthFog.fx, the sky / ceiling / Arcane
//    pictures, the FlatVR add-on - are in mods\D2R-VR\reshade; the effect and the pictures reach it through two search paths in its ReShade.ini
//    (shared/reshade_paths.h, D2R_VR_Settings.exe --reshade-paths, run by Setup), the add-on by the plugin loading it from there.
//  - With no dxgi.dll D2R VR's own ReShade is ReShade64.dll in mods\D2R-VR\reshade: the plugin loads it by path with that folder as its base.
// So the buttons are: Install ReShade (there is none), Add D2R VR's folders to my ReShade (a dxgi.dll that does not have them yet), and the one explicit
// choice - put the player's dxgi.dll aside as dxgi.dll.off (never deleted, never replaced) so D2R VR's own ReShade can be installed, and bring it back.

#include <string>

#include <windows.h>

namespace reshadeas {

struct Where {
    std::wstring game;         // D2R.exe's folder, trailing backslash
    std::wstring modReShade;   // <game>\mods\D2R-VR\reshade\ when D2R VR is installed as the D2RLoader mod, else empty
};

inline bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

inline std::wstring Dxgi(const Where& w) { return w.game + L"dxgi.dll"; }
inline std::wstring DxgiOff(const Where& w) { return w.game + L"dxgi.dll.off"; }

// The mod has a ReShade64.dll of its own in its reshade\ (the plugin's ReShadeDir then takes that folder).
inline bool ModOwn(const Where& w) { return !w.modReShade.empty() && Exists(w.modReShade + L"ReShade64.dll"); }

// The folder D2R VR's ReShade is in: the mod's reshade\ once it has a ReShade64.dll, else the game's folder (as the plugin's ReShadeDir decides).
inline std::wstring ReShadeFolder(const Where& w) { return ModOwn(w) ? w.modReShade : w.game; }

struct Buttons {
    bool install;   // "Install ReShade": there is no ReShade at all
    bool paths;     // "Add D2R VR's folders to my ReShade": a dxgi.dll that does not have the two search paths yet (the mod only)
    bool aside;     // "Put my dxgi.dll aside": a dxgi.dll is there, and no dxgi.dll.off in its way
    bool back;      // "Bring my dxgi.dll back": it was put aside
};

inline Buttons ButtonsShown(const Where& w, bool pathsPresent) {
    const bool dx = Exists(Dxgi(w));
    const bool off = Exists(DxgiOff(w));
    const bool r64 = Exists(ReShadeFolder(w) + L"ReShade64.dll");
    Buttons b{};
    b.install = !(r64 || dx);
    b.paths = dx && !w.modReShade.empty() && !pathsPresent;
    b.aside = dx && !off;
    b.back = !dx && off;
    return b;
}

struct Result {
    bool ok = true;
    DWORD error = 0;          // GetLastError() of the move that failed
    std::wstring did;         // what happened, for the log
};

enum class Act { Aside, Back };

// The only two things a button does to the player's dxgi.dll, both a rename that cannot lose anything: nothing is replaced, nothing deleted.
inline Result Apply(Act act, const Where& w) {
    Result r;
    const std::wstring dx = Dxgi(w), off = DxgiOff(w);
    if (act == Act::Aside) {
        r.ok = MoveFileExW(dx.c_str(), off.c_str(), 0) != 0;   // (not MOVEFILE_REPLACE_EXISTING: an older dxgi.dll.off is somebody's file too)
        r.error = r.ok ? 0 : GetLastError();
        r.did = L"dxgi.dll put aside as dxgi.dll.off";
    } else {
        r.ok = MoveFileExW(off.c_str(), dx.c_str(), 0) != 0;
        r.error = r.ok ? 0 : GetLastError();
        r.did = L"dxgi.dll.off brought back as dxgi.dll";
    }
    return r;
}

}  // namespace reshadeas
