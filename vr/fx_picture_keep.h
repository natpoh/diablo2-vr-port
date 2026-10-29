// fx_picture_keep.h - [render] no_reshade's own runtime (vr/fx_native.h Options::pictureWanted): which of D2R_DepthFog.fx's pictures (its
// `source =` textures) are wanted on the graphics card now. The effect samples one act's sky at a time (SkyTex: its day and night band and cap,
// SkyBand<n>Tex / SkyNBand<n>Tex / SkyCap<n>Tex / SkyNCap<n>Tex) and one cave ceiling's stone (CeilTexSlot: CeilTex<n>) - the other 20 sky
// pictures and 7 stones are never read there. ReShade keeps every one, 166 MiB at 1536 x 1024 and 1024 x 1024: on a 4 GB card over Link
// (GTX 1650 SUPER, Quest 2 player, 2026-10-26) the game ran at 1-10 frames a second for the first 30-80 s in the town while its video memory
// stood within 150 MiB of the budget. Every other picture (the Arcane Sanctuary's, small) is always wanted.
// skySlot: the act's painted sky (1..6; 0 = none yet: no sky picture wanted); ceilSlot: the ceiling's stone (1..8; 0 = the ceiling is off).
// vrcam keeps the last slot seen (a cave between two visits to the same act reads no picture again). tools/fx_picture_keep_test checks the
// rule against the effect's own texture names.
#pragma once

#include <cstdlib>
#include <cstring>

namespace fxpicturekeep {

// the slot a name of the form <prefix><digits><suffix> carries; -1: not of that form
inline int SlotOf(const char* name, const char* prefix, const char* suffix) {
    const size_t p = strlen(prefix), s = strlen(suffix), n = strlen(name);
    if (n <= p + s || strncmp(name, prefix, p) != 0 || strcmp(name + n - s, suffix) != 0) return -1;
    int v = 0;
    for (size_t i = p; i < n - s; ++i) {
        if (name[i] < '0' || name[i] > '9') return -1;
        v = v * 10 + (name[i] - '0');
    }
    return v;
}

inline bool Wanted(const char* name, int skySlot, int ceilSlot) {
    if (!name) return true;
    for (const char* pre : {"SkyBand", "SkyNBand", "SkyCap", "SkyNCap"})
        if (const int k = SlotOf(name, pre, "Tex"); k >= 0) return k == skySlot;
    if (const int k = SlotOf(name, "CeilTex", ""); k >= 0) return k == ceilSlot;
    return true;
}

}  // namespace fxpicturekeep
