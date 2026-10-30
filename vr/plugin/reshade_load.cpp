// vr/plugin/reshade_load.cpp - ReShade64.dll loaded from the game's (or the mod's) folder at the plugin's load, or the player's own
// dxgi.dll ReShade used as it is with D2R VR's add-on beside it (cut from vr/vrcam.cpp, docs/plan_modules.md M3; tools/reshade_env_test
// checks the order in LoadReShade from this file)
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;

// ReShade from the game's folder as ReShade64.dll, loaded here - only ever
// under D2RLoader. As dxgi.dll it broke the plain game: D2R.exe checks the
// signature of the dxgi.dll it loads, ReShade's has none, and the game stopped
// with "Failed to initialize graphics system" (blz-log: "LoadLibrary(dxgi.dll)
// Failed", 0x80090006). Loaded under any other name ReShade hooks dxgi and
// D3D12 itself; plugins load before the game builds its renderer, so it is in
// time. With a dxgi.dll in the folder (the game ships none: it is the player's
// own ReShade) the game loads that one itself, before any plugin, and it is
// USED as it is - never renamed, moved or deleted by anything of ours; a
// second ReShade (ReShade64.dll as well) would refuse to start (error 1114),
// so this returns before it loads ours.
// What a dxgi.dll ReShade cannot find by itself is D2R VR's FlatVR depth
// add-on in the mod's reshade\ folder (ReShade has ONE add-on folder, the
// game's; D2R_DepthFog.fx and the pictures reach it through two search paths
// that Setup adds to its ReShade.ini, shared/reshade_paths.h): the add-on is
// loaded from here, the way ReShade loads it from its own folder (it registers
// itself with the ReShade of the process; the plugin joins ReShade the same
// way, at its load, before the game builds its renderer).
void LoadOurAddonForDxgiReShade(const wchar_t* gameDir) {
    const std::wstring files = OurFilesDir();
    if (_wcsicmp(files.c_str(), gameDir) == 0) return;   // not the mod: the files are in the game's folder, where ReShade looks
    const std::wstring addon = files + L"FlatVR_DepthProvider.addon64";
    if (GetFileAttributesW(addon.c_str()) == INVALID_FILE_ATTRIBUTES) { Log("vrcam: no FlatVR_DepthProvider.addon64 in the mod's reshade folder - no FlatVR depth"); return; }
    if (GetFileAttributesW((std::wstring(gameDir) + L"FlatVR_DepthProvider.addon64").c_str()) != INVALID_FILE_ATTRIBUTES) {
        Log("vrcam: FlatVR_DepthProvider.addon64 is beside the game too - the player's ReShade loads that one");
        return;
    }
    const HMODULE m = LoadLibraryW(addon.c_str());
    char utf8[MAX_PATH * 3] = "";
    WideCharToMultiByte(CP_UTF8, 0, addon.c_str(), -1, utf8, sizeof utf8, nullptr, nullptr);
    if (m) LogF("vrcam: FlatVR depth add-on loaded from the mod's reshade folder for the dxgi.dll ReShade: %s", utf8);
    else LogF("vrcam: FlatVR depth add-on could not be loaded from %s (error %lu)", utf8, GetLastError());
}

void LoadReShade() {
    wchar_t dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    while (n > 0 && dir[n - 1] != L'\\') --n;
    dir[n] = 0;
    std::wstring proxy = std::wstring(dir) + L"dxgi.dll";
    // [render] no_reshade (docs/plan_no_reshade.md): no ReShade64.dll - the plugin hooks D3D12 itself (noreshade::Install, after MinHook's start).
    // Not with the player's own ReShade as dxgi.dll beside the game: the game loads that one itself, so it is joined as before.
    switch (nors::Decide(IniB(L"render", L"no_reshade", false), GetFileAttributesW(proxy.c_str()) != INVALID_FILE_ATTRIBUTES)) {
        case nors::Start::PlayerDxgi:
            Log("vrcam: [render] no_reshade is on, but a dxgi.dll is beside the game (the player's own ReShade): the game loads it anyway - "
                "joined as before, no_reshade does nothing");
            break;
        case nors::Start::None:
            g_noReShade = true;
            LogF("vrcam: no ReShade ([render] no_reshade) - ReShade64.dll is not loaded; the plugin hooks D3D12 itself for native OpenXR eyes by "
                 "turns. Off in this mode: %s", nors::kOff);
            return;
        case nors::Start::ReShade: break;
    }
    if (GetFileAttributesW(proxy.c_str()) != INVALID_FILE_ATTRIBUTES) {
        Log("vrcam: ReShade as dxgi.dll beside the game (the player's own) - the game loads it itself, vrcam joins it and uses it as it is; "
            "ReShade64.dll is not loaded as well (a second ReShade would refuse to start). Its ReShade.ini needs D2R VR's two search paths "
            "(D2R VR Setup / Settings > Home add them)");
        LoadOurAddonForDxgiReShade(dir);
        return;
    }
    // ReShade's own folder (ReShadeDir): the mod's reshade\ (D2RLoader mod D2R-VR) or the game's. For the mod's, ReShade is told
    // that folder is its base (RESHADE_BASE_PATH_OVERRIDE, read once by ReShade at its start): its ReShade.ini, presets,
    // shaders and add-ons are the mod's own, and another VR mod's ReShade setup is never seen.
    const std::wstring base = ReShadeDir();
    const bool fromMod = _wcsicmp(base.c_str(), dir) != 0;
    wchar_t path[MAX_PATH];
    wcscpy_s(path, base.c_str());
    wcscat_s(path, L"ReShade64.dll");
    char where[MAX_PATH * 3] = "";
    WideCharToMultiByte(CP_UTF8, 0, path, -1, where, sizeof where, nullptr, nullptr);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        if (!fromMod) { Log("vrcam: no ReShade64.dll beside the game - no fog, sky or FlatVR picture"); return; }
        // the mod's ReShade only: a ReShade64.dll beside the game is not ours in the mod layout (mod_layout.h ReShadeDir) - not loaded
        const bool stray = GetFileAttributesW((std::wstring(dir) + L"ReShade64.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
        LogF("vrcam: no ReShade64.dll in the mod's reshade folder (%s) - no fog, sky (black) or FlatVR picture: press Install ReShade in "
             "D2R VR Settings > Home%s", where,
             stray ? ". The ReShade64.dll beside D2R.exe is not D2R VR's (another mod's, or one left from an install before 0.170) - "
                     "not loaded, and left where it is" : "");
        return;
    }
    // The order is the point (shared/reshade_env.h, PrepareProcess; pinned by tools/reshade_env_test.cpp): FIRST every RESHADE_*
    // variable this process inherited is cleared - another mod's (d2r-pcvr's RESHADE_BASE_PATH_OVERRIDE sent our ReShade to its folder
    // and it refused to start, 2026-10-18) - and only THEN our own RESHADE_BASE_PATH_OVERRIDE is set, for the mod's reshade\ (the
    // mod layout); in the game's own folder none is set. ReShade reads these once, at its start (LoadLibraryW below).
    for (const reshadeenv::Step& st : reshadeenv::PrepareProcess(fromMod ? base : std::wstring()))
        if (st.kind == reshadeenv::Step::Clear)
            LogF("vrcam: ReShade - %s=%s was set for this game (left by another mod?): cleared for the game's process only, so our "
                 "ReShade uses its own folder", Utf8(st.name.c_str()).c_str(), Utf8(st.value.c_str()).c_str());
    if (LoadLibraryW(path)) {
        char utf8[MAX_PATH * 3] = "";
        WideCharToMultiByte(CP_UTF8, 0, base.c_str(), -1, utf8, sizeof utf8, nullptr, nullptr);
        LogF("vrcam: ReShade loaded from ReShade64.dll%s%s", fromMod ? " - the D2RLoader mod's own, its base " : "", fromMod ? utf8 : "");
        return;
    }
    const DWORD err = GetLastError();
    LogF("vrcam: ReShade64.dll did not load (error %lu): %s", err, where);
    // Who is ReShade here already (ReShade refuses a second copy in a process: error 1114, its own
    // start saying no before it writes a line to ReShade.log - a player's game had no ReShade at all
    // then, and native OpenXR, which starts from ReShade's frames, never came up, 2026-10-11): every
    // module that exports ReShade's entry points or has "reshade" in its name, with its path.
    HMODULE mods[1024];
    DWORD need = 0;
    int found = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) {
        for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 1024; ++i) {
            wchar_t name[MAX_PATH] = L"";
            GetModuleFileNameW(mods[i], name, MAX_PATH);
            std::wstring low = name;
            for (wchar_t& c : low) c = (wchar_t)towlower(c);
            const bool exports = GetProcAddress(mods[i], "ReShadeVersion") || GetProcAddress(mods[i], "ReShadeRegisterAddon");
            if (!exports && low.find(L"reshade") == std::wstring::npos) continue;
            ++found;
            char utf8[MAX_PATH * 3] = "";
            WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof utf8, nullptr, nullptr);
            LogF("vrcam: ReShade-like module in the game: %s%s", utf8, exports ? " (exports ReShade's entry points: a ReShade already loaded)" : "");
        }
    }
    if (!found)
        LogF("vrcam: no other ReShade in the game - ReShade64.dll itself refused (%s): reinstall D2R VR over the game; "
             "an antivirus may also block it", err == ERROR_DLL_INIT_FAILED ? "its start said no" : "it could not be loaded");
}
