// overlay_work.h - eyes by turns: whether D2R_DepthFog.fx's overlay techniques (D2R_Overlay(_R), D2R_OverlayMade(_R)) have anything to draw this
// present. Their passes - Hud, GameHud (GamePieces, the labels), Phantom, LowHp - each branch on one of the effect's own switches below; with all
// off each pass copies the picture as it is (~5.5 ms a frame for both eyes on a GTX 1050 Ti, docs/plan_no_reshade.md), so vrcam renders neither
// eye's (replay::OverlaysHaveWork). The switches are read back from the effect as OnBeginEffects left them; one the effect lacks (an older
// effect) reads as off. tools/overlay_work_test checks the decision and that every switch those passes branch on in the effect is listed here.
#pragma once

namespace overlaywork {

inline constexpr const char* kBools[] = {"GameBarOn", "GameMapOn", "LabelsOn", "HitboxOn", "PaOn", "OrbOn", "MercOn", "PhantomOn", "BoneOn", "DbgOn"};
inline constexpr const char* kFloats[] = {"HudShift", "QvN", "AfOn", "LowHpAlpha"};   // (0 draws nothing)

// readBool(name, &on) / readFloat(name, &v): false when the effect has no such uniform.
template <class ReadBool, class ReadFloat>
bool Any(ReadBool readBool, ReadFloat readFloat) {
    for (const char* n : kBools) {
        bool v = false;
        if (readBool(n, &v) && v) return true;
    }
    for (const char* n : kFloats) {
        float v = 0.0f;
        if (readFloat(n, &v) && v != 0.0f) return true;
    }
    return false;
}

}  // namespace overlaywork
