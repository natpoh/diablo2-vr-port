// dome_backing.h - [sky] dome_atlas_tiled: how the dome's baked texture (vr/sky/dome_atlas.cpp, ours for the game's atlas) is backed by memory.
//
// Crash batch 2026-11-24, group A (rows 375, 383, 413, 414): four players on RTX 2060 / 2060 SUPER / 2080 SUPER / 3050 6 GB - three Turing,
// one Ampere, 6-8 GB - got DXGI_ERROR_DEVICE_HUNG with DRED standing in our ring list ("d2rvr no-reshade depth copy list", DrawInstanced or
// ClearRTV), no page fault, three of four within ~3 s of the dome's work. All four had the texture reserved (tiled resources tier 2+): each
// change of the act's band UpdateTileMappings'd the whole texture to NULL and the band back in, on the game's queue, and the bake cleared and
// drew into it on that list. Serg's big card never hangs. So by default the texture is a committed one, all of it backed (~134 MiB at 4096 x
// 6144, 9 mips, R11G11B10 - what cards without tier 2 always had), no tile mappings at all; the bake and the picture are the same either way
// (only the backed part differs: the game samples the shown band only). 1 = the reserved one on tier 2+ cards, as up to 0.222.5 (~22 MiB).
//
// Pure: tested in cleanroom/tests/dome_backing_test.cpp.
#pragma once

namespace domebacking {

// The way taken and why, for the log line (vrcam: game sky - ... our texture made, ... - <why>)
struct Choice {
    bool reserved = false;     // try the reserved (tiled) texture first; false: committed, whole
    const char* why = "";
};

// Before the texture is made: `tiledKey` [sky] dome_atlas_tiled, `tier2` the card's D3D12_TILED_RESOURCES_TIER >= 2
inline Choice Choose(bool tiledKey, bool tier2) {
    if (!tiledKey)
        return {false, "all of it backed: a committed texture, no tile mappings ([sky] dome_atlas_tiled=0, the default - the tiled one is "
                       "suspected in GPU hangs on 6-8 GB cards)"};
    if (!tier2) return {false, "all of it backed (the card has no tiled resources tier 2; [sky] dome_atlas_tiled=1 wants it)"};
    return {true, "only the shown act's band and the cave's backed (reserved, tiled resources tier 2+, [sky] dome_atlas_tiled=1)"};
}

// After: the reserved texture (or its heap) could not be made - committed, whole, instead
inline Choice Fallback() {
    return {false, "all of it backed (the reserved texture or its heap could not be made; [sky] dome_atlas_tiled=1 wanted it)"};
}

// Ours kept as made: the same size and mips, made under the same key. The key changes take effect only where ours is made - at the game's
// next view of its picture (dome_atlas.cpp StandIn), never under a view the game holds.
inline bool Keep(bool made, bool sameSize, bool madeTiledKey, bool tiledKeyNow) { return made && sameSize && madeTiledKey == tiledKeyNow; }

}  // namespace domebacking
