// vr/plugin/reshade_load.cpp - the plugin's start without ReShade (cut from vr/vrcam.cpp, docs/plan_modules.md M3).
//
// Since 0.203 (Serg, 2026-11-05: "from the new build don't use ReShade at all - we take the depth map without it") D2R VR never uses ReShade:
// ReShade64.dll is not loaded (neither the mod's nor one beside the game), a ReShade already in the game is never joined (no add-on
// registered, no effect defines set, no add-on of ours loaded into it), and [render] no_reshade in an old ini means nothing - the depth and
// every effect come from the plugin's own D3D12 hooks (vr/native_d3d12.h) and its own runtime for D2R_DepthFog.fx (vr/fx_native.h).
// Why: crash reports 231 / 232 / 251 - vrcam set D2R_DepthFog.fx's defines (D2R_FOG_MEASURE, D2R_CEILING_n) and ReShade rebuilt the effect
// synchronously in its present: minutes on an i7-4790, the game killed as hung. 251 went through the PLAYER'S OWN dxgi.dll ReShade, which the
// old no_reshade=1 joined anyway.
// A dxgi.dll beside the game (the game ships none: it is the player's own ReShade) is loaded by the game itself before any plugin. It is left
// exactly as it is - never renamed, moved or deleted - and keeps running the player's own effects; it simply gets nothing of D2R VR's.
// tools/reshade_env_test checks this file: no ReShade64.dll loaded, no ReShade joined.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;

void LoadReShade() {
    wchar_t dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, dir, MAX_PATH);
    while (n > 0 && dir[n - 1] != L'\\') --n;
    dir[n] = 0;
    const bool playerDxgi = GetFileAttributesW((std::wstring(dir) + L"dxgi.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    g_noReShade = true;
    if (IniB(L"render", L"no_reshade", true) == false)
        Log("vrcam: [render] no_reshade=0 in d2r_vr.ini is from an older version - D2R VR no longer uses ReShade, the key means nothing now");
    switch (nors::Decide(playerDxgi)) {
        case nors::Start::BesidePlayers:
            LogF("vrcam: no ReShade - a dxgi.dll is beside the game (the player's own ReShade): the game loads it itself, it is left exactly as it "
                 "is and runs its own effects; D2R VR does not join it (no add-on, no effect of ours in it) and hooks D3D12 itself. Off in this "
                 "mode: %s", nors::kOff);
            break;
        case nors::Start::None:
            LogF("vrcam: no ReShade - the plugin hooks D3D12 itself (the depth and the effects are D2R VR's own). Off in this mode: %s", nors::kOff);
            break;
    }
}
