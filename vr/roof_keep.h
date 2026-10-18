// roof_keep.h - [render] keep_roofs: in VR F4 the roof of a building you walk into stays (vr/vrcam.cpp, namespace roofkeep).
//
// The game hides what is between its camera high above and the hero: a FadeTargetComponent whose name matches one of the
// active cut-away boxes asks the generic opacity animator (FadeTarget, 0xEEA920: component, target alpha, seconds) for alpha
// 0 - the roof of Asheara's hut in Kurast Docks, and the like elsewhere. From our camera at the hero's eyes that only takes the
// ceiling away and shows the sky. The detour on the setter answers alpha 1 instead of 0, and only for that one call (its return
// address, RoofHideCall + 14); creature fades and every other caller of the setter keep their own arguments, and the animator
// still moves the value over its own duration. Nothing of the game's data is written.
// F4 only: from above (F1 - F3) the game's cut-aways stay as they are.
//
// Found by bearstonem/d2r-pcvr (MIT, v0.3.17: vr/environment_cutaway.h, docs/KURAST_ROOF_PREVIEW.md); written again here on
// our signatures (game_sigs.h, area "Roofs, hanging props").
#pragma once

#include <cstddef>
#include <cstdint>

namespace roofkeep {

// Addresses in 3.3.93787 (the keys of game_sigs.h) and the bytes each starts with there.
constexpr uint64_t kSetterRva = 0xEEA920, kHideRva = 0xF5220D;
constexpr uint64_t kHideReturnOffset = 14;   // the hide branch's call of the setter returns here, from kHideRva
// movss xmm3, [rcx]; ucomiss xmm1, xmm3; jp +2; je +2Eh; movss xmm0, [rcx+10h]
constexpr uint8_t kSetterSig[16] = {0xF3,0x0F,0x10,0x19,0x0F,0x2E,0xCB,0x7A,0x02,0x74,0x2E,0xF3,0x0F,0x10,0x41,0x10};
// movaps xmm2, xmm6 (the seconds); xorps xmm1, xmm1 (alpha 0); mov rcx, rbp (the component); call FadeTarget; xor r15b, r15b
constexpr uint8_t kHideSig[17] = {0x0F,0x28,0xD6,0x0F,0x57,0xC9,0x48,0x8B,0xCD,0xE8,0x05,0x87,0xF9,0xFF,0x45,0x32,0xFF};

// The call in the hide branch reaches the setter (from the sig's own rel32, as the game has it).
inline bool HideCallsSetter() {
    const int32_t rel = (int32_t)((uint32_t)kHideSig[10] | (uint32_t)kHideSig[11] << 8 | (uint32_t)kHideSig[12] << 16 | (uint32_t)kHideSig[13] << 24);
    return (int64_t)(kHideRva + kHideReturnOffset) + rel == (int64_t)kSetterRva;
}

// The alpha the setter gets: the game's own, except the hide branch's 0 while the roofs are to stay (on, VR F4, in the world).
inline float TargetAlpha(bool keep, uintptr_t caller, uintptr_t hideReturn, float alpha) {
    return keep && hideReturn != 0 && caller == hideReturn && alpha == 0.0f ? 1.0f : alpha;
}

}  // namespace roofkeep
