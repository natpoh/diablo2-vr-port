// fx_pass_keep.h - [render] no_reshade's own runtime (vr/fx_native.h): which of D2R_DepthFog.fx's passes into the back buffer are worth drawing this
// frame. Each of these returns the picture as it is while its switches are off - a full-screen pass and a copy of the picture (COLOR) for nothing:
//   PS_Hud(R)       Hud: the picture unless HudShift != 0 (and the overlays not apart: then always the picture)
//   PS_GameHud(R)   GameHud: the picture unless LabelsOn / GameBarOn / GameMapOn (the same with the overlays apart)
//   PS_Phantom(R)   LowHp(Phantom): the picture unless one of Phantom's switches or LowHpAlpha (the same with the overlays apart)
//   PS_TableKey(R)  TableKey: the picture unless TableKeyOn
//   PS_Stamp(R)     Stamp: the picture unless StampOn
//   PS_Ov*          the overlays drawn apart (eyes by turns): PS_OvHud(R) as PS_Hud, PS_Ov(Made)GameHud(R) as PS_GameHud, PS_Ov(Made)Phantom(R) as
//                   PS_Phantom - on their own picture, OverlaysApart not asked
// With FogLight ([fog] light: the fog's short way, D2R_DepthFog.fx LightFog) the passes only the full fog, the cave ceiling and the glow over
// the void read are left out too (kLight) - the interface's copies and mask (PS_HudCopy / Mask / Store / MaskStore) only while PS_Hud is not
// drawn (it moves the interface by that mask).
// With no sky, no fog and no ceiling (SkyOn, FogOn, CeilOn all off - [fog] game_fog drawing the fog in the game's own frame, PC1 2026-10-27:
// "sky off, fog off, ceiling off; 0.66-0.69 ms a present, 26 passes" for the interface's switches alone) PS_DepthFog(R) returns the picture
// as it is: it is left out with everything only it reads (kLight, and kLightHud as above) - the interface's own passes stay as they are.
// Every other pass is kept. The switches are read back from the effect as vrcam set them this frame. tools/fx_pass_keep_test checks the rules
// against the effect's own code and on fake reads; tools/fx_native_test draws the fog with and without them alike.
#pragma once

#include <cstring>

namespace fxpasskeep {

inline constexpr const char* kPhantom[] = {"HitboxOn", "PaOn", "OrbOn", "MercOn", "PhantomOn", "BoneOn", "DbgOn"};   // bools
inline constexpr const char* kPhantomF[] = {"QvN", "AfOn", "LowHpAlpha"};                                         // 0 draws nothing
// what FogLight leaves unread: the fog over the void (VoidFog*), the torches and the glow for the ceiling (Fire, Torch*, VoidGlow*), the
// ceiling's height map and domes (the rest)
inline constexpr const char* kLight[] = {"PS_VoidFog", "PS_VoidFogBlurH", "PS_VoidFogBlurV", "PS_Fire", "PS_TorchCands", "PS_TorchCandsR",
                                         "PS_TorchMerge", "PS_TorchMergeR", "PS_TorchStore", "PS_VoidGlow8", "PS_VoidGlow32", "CS_RecordHeights",
                                         "CS_RecordHeightsR", "PS_HMapMerge", "PS_HMapStore", "PS_SupMerge", "PS_SupStore", "PS_HMapGenStore",
                                         "CS_ClearHeights", "PS_Peaks", "PS_DomeSeeds"};
inline constexpr const char* kLightHud[] = {"PS_HudCopy", "PS_HudMask", "PS_HudStore", "PS_HudMaskStore"};

// entry: the pass's pixel shader's entry point as the compiler names it ("F__PS_Hud") or plain ("PS_Hud"). readBool(name, &v) / readFloat(name, &v):
// false when the effect has no such uniform (read as off).
template <class ReadBool, class ReadFloat>
bool Keep(const char* entry, ReadBool readBool, ReadFloat readFloat) {
    if (!entry) return true;
    const char* n = (entry[0] == 'F' || entry[0] == 'E') && entry[1] == '_' && entry[2] == '_' ? entry + 3 : entry;
    auto on = [&](const char* u) { bool v = false; return readBool(u, &v) && v; };
    auto nonzero = [&](const char* u) { float v = 0.0f; return readFloat(u, &v) && v != 0.0f; };
    auto is = [&](const char* a) { return strcmp(n, a) == 0; };
    const bool none = !on("SkyOn") && !on("FogOn") && !on("CeilOn");   // the world's pass gives the picture as it is
    if (none && (is("PS_DepthFog") || is("PS_DepthFogR"))) return false;
    if (none || on("FogLight")) {
        // (a compute entry point carries its template's numbers: "CS_RecordHeights_16_16_1")
        auto named = [&](const char* a) { const size_t k = strlen(a); return strncmp(n, a, k) == 0 && (n[k] == 0 || n[k] == '_'); };
        for (const char* a : kLight) if (named(a)) return false;
        for (const char* a : kLightHud) if (is(a)) return !on("OverlaysApart") && nonzero("HudShift");
    }
    if (is("PS_Hud") || is("PS_HudR")) return !on("OverlaysApart") && nonzero("HudShift");
    if (is("PS_GameHud") || is("PS_GameHudR")) return !on("OverlaysApart") && (on("LabelsOn") || on("GameBarOn") || on("GameMapOn"));
    if (is("PS_Phantom") || is("PS_PhantomR")) {
        if (on("OverlaysApart")) return false;
        for (const char* u : kPhantom) if (on(u)) return true;
        for (const char* u : kPhantomF) if (nonzero(u)) return true;
        return false;
    }
    // the overlays drawn apart (D2R_Overlay*, eyes by turns): the same, on their own picture
    if (is("PS_OvHud") || is("PS_OvHudR")) return nonzero("HudShift");
    if (is("PS_OvGameHud") || is("PS_OvGameHudR") || is("PS_OvMadeGameHud") || is("PS_OvMadeGameHudR"))
        return on("LabelsOn") || on("GameBarOn") || on("GameMapOn");
    if (is("PS_OvPhantom") || is("PS_OvPhantomR") || is("PS_OvMadePhantom") || is("PS_OvMadePhantomR")) {
        for (const char* u : kPhantom) if (on(u)) return true;
        for (const char* u : kPhantomF) if (nonzero(u)) return true;
        return false;
    }
    if (is("PS_TableKey") || is("PS_TableKeyR")) return on("TableKeyOn");
    if (is("PS_Stamp") || is("PS_StampR")) return on("StampOn");
    return true;
}

}  // namespace fxpasskeep
