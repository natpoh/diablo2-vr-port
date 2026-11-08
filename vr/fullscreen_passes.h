// fullscreen_passes.h - [render] foveated_fullscreen, grass_lighting_coarse (vr/foveation.h): the game's heavy passes that Bear's rule leaves
// whole, known by their pixel shader (docs/notes/frame_cost_study.md sections 2 and 4, cut #3): the grass lighting (a full-screen stencil
// pass, ps_grass_lighting: 2,002 instructions / 83 texture reads a pixel - as heavy as the lit colour pass, no depth test), the reflections
// (ps_ppr_reflection, full screen, stencil) and the hair (ps_hair, weighted OIT: blended). Each permutation of those shaders in the game's
// own archive (data/hd/shaders, 10,847 permutations, read 2026-11-08) by its size and its FNV-1a 64 - the hash vr/uitrace.cpp printed as
// "ps" and [render] skip_shaders takes. ps_grass_lighting and ps_ppr_reflection have ONE permutation each: no lighter grass lighting exists
// for an option to pick. Pure: cleanroom/tests/unleashed_foveation_test.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fullscreenpasses {

enum Kind : uint8_t { kNone = 0, kGrassLighting = 1, kReflections = 2, kHair = 3 };

// vr/uitrace.cpp's and vr/pipe_hunt.cpp's hash: FNV-1a 64 with THEIR offset basis (1469598103934665603 - a digit short of the standard
// 14695981039346656037); every "ps" number in the traces, the study and [render] skip_shaders is this one.
inline uint64_t Fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

struct Known {
    uint64_t hash;
    uint32_t bytes;
    Kind kind;
};
inline const Known* Table(size_t* n) {
    static const Known t[] = {
        {0x5fafbd06ef7f97d7ull, 57580, kGrassLighting},   // ps_grass_lighting #0 (the only one), 2002 instr
        {0x403f6d66029d1d1full, 9120, kReflections},      // ps_ppr_reflection #0 (the only one), 284 instr
        {0xe8a1ccc6a24b3ce3ull, 51204, kHair},            // ps_hair #0 .. #17, 1779 - 2044 instr (#1 is the one in the 2026-10-13 trace)
        {0xace4faac06289d00ull, 51256, kHair},
        {0x8665513ed0afaa05ull, 51040, kHair},
        {0xd37740dfcabaaf59ull, 50924, kHair},
        {0x6c1d35c31fa457e5ull, 51244, kHair},
        {0x424e404b637362beull, 50956, kHair},
        {0xb2a483ce387c7a7aull, 59404, kHair},
        {0x97d275dc3c9d775eull, 57528, kHair},
        {0x9da1d524501f65dbull, 59444, kHair},
        {0x752ab389bb95d7f1ull, 57400, kHair},
        {0x770ed94bb88ba0c4ull, 57520, kHair},
        {0x72df851cd6efb152ull, 57432, kHair},
        {0x9125963a958df9dbull, 54184, kHair},
        {0x454c1f75ee7cc6baull, 53904, kHair},
        {0x2789d153efac86d0ull, 54224, kHair},             // (#14 and #16 are the same bytes)
        {0xc909a82f2faafa8bull, 54088, kHair},
        {0x313e8541e93970c1ull, 54324, kHair},
    };
    *n = sizeof t / sizeof t[0];
    return t;
}

// A pixel shader's kind: its size first (cheap: most of the game's shaders are none of these), the hash only on a size that matches.
inline Kind KindOf(const void* code, size_t bytes) {
    if (!code || !bytes) return kNone;
    size_t n = 0;
    const Known* t = Table(&n);
    bool sized = false;
    for (size_t i = 0; i < n && !sized; ++i) sized = t[i].bytes == bytes;
    if (!sized) return kNone;
    const uint64_t h = Fnv(code, bytes);
    for (size_t i = 0; i < n; ++i)
        if (t[i].bytes == bytes && t[i].hash == h) return t[i].kind;
    return kNone;
}

// [render] foveated_fullscreen: 0 none (Bear's rule alone), 1 the grass lighting and the reflections, 2 the hair too.
inline bool Widened(Kind k, int mode) {
    return (k == kGrassLighting || k == kReflections) ? mode >= 1 : k == kHair ? mode >= 2 : false;
}
inline const char* Name(Kind k) {
    return k == kGrassLighting ? "grass lighting" : k == kReflections ? "reflections" : k == kHair ? "hair" : "-";
}

}  // namespace fullscreenpasses
