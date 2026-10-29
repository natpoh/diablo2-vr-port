// native_fx_depth.h - the scene's depth on the no-ReShade path ([render] no_reshade, native_effects): when vrcam copies it, and when
// D2R_DepthFog.fx may draw what is placed by it (2026-10-29).
//
// The copy (fx::scenedepth::BindNative) was made only while own depth was in force, and on the no-ReShade path that is VR alone
// ([mode] platform=1: eyes by turns move the eye made with it). Flat ([mode] platform=0) with "Sky and fog drawn by: Our effect (old)"
// (game_sky=0, game_fog=0) then drew the sky, the fog and the ceiling with no depth at all: every pixel the void, the act's sky and
// its fog over the whole picture - PC1 2026-10-29 17:07, 0.188.3, the Arreat Summit: a black screen ("the scene's depth not picked
// yet" in every 10 s line until VR started at 17:08:31 and the pick was made at once).
//
// So: the copy is also made whenever the effect draws something that needs it, in any mode; and whatever needs it is not drawn on a
// depth that is missing or stale - nothing of ours rather than the void over everything.
#pragma once

namespace nativefxdepth {

// The copy is wanted: no ReShade (with ReShade its own DEPTH is there), and VR or an effect that is placed by the depth (the sky in the
// void, the fog by distance, the cave ceiling).
inline bool CopyWanted(bool noReShade, bool vr, bool depthEffects) { return noReShade && (vr || depthEffects); }

// The copy the effect reads is good to draw by: one there, made within the last few presents (a present the picked buffer was not
// bound in keeps the last copy; more than this in a row and it is someone else's frame - another size, another mode).
constexpr unsigned kStalePresents = 8;
inline bool Usable(bool haveCopy, unsigned presentsSinceCopy) { return haveCopy && presentsSinceCopy <= kStalePresents; }

// Presents since the last copy, held at a ceiling (no wrap round to "fresh")
inline unsigned NextSince(unsigned since, bool copiedNow) { return copiedNow ? 0u : since < 1000000u ? since + 1u : since; }

}  // namespace nativefxdepth
