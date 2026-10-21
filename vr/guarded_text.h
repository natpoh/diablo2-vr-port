// guarded_text.h - a NUL-terminated name in game memory read a page at a time, not a letter at a time. Pure (the copy is handed in).
//
// The skeleton hook reads names out of the game's memory - a model's path, every bone's name - through a guarded copy (skeletons.cpp SafeCopy: memcpy under __try, so a
// pointer that went stale reads as nothing instead of killing the game). Str did one guarded copy per letter: hundreds of calls for one rig's bone names. This copies up to
// the end of each page at once - a page is the unit of protection, so no copy reaches into a page the string does not - and stops at the NUL, at the first letter that is
// not printable, or at cap - 1 letters: the same bytes touched and the same answers as the letter-at-a-time loop.
//
// From bearstonem/d2r-pcvr (MIT), steamvr/guarded_text.h, his commits bd99126 / be02ea3 (PR #209, "Skeleton hook: crossbow and shield discovery without a call per letter").
//
// Tested in cleanroom/tests/skeleton_discovery_test.cpp (against the letter-at-a-time loop at every place near an unreadable page).
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace guardedtext {

constexpr uintptr_t kPage = 0x1000;
constexpr uintptr_t kLowest = 0x10000;   // at or below: never read (as skeletons.cpp Get)

// out holds cap bytes. The length, the string NUL-terminated in out; with no NUL in the first cap - 1 letters, those. -1 at a letter that is not printable ASCII (32..126),
// at a page before the end that does not read, or for an address in the low 64 KB. copy(dst, src, n) -> bool is the guarded copy.
template <class Copy> int ReadText(uintptr_t at, char* out, int cap, Copy&& copy) {
    if (cap < 1) return -1;
    int n = 0;
    while (n < cap - 1) {
        const uintptr_t from = at + (uintptr_t)n;
        if (from <= kLowest) return -1;
        const int take = (int)std::min<uintptr_t>(kPage - (from & (kPage - 1)), (uintptr_t)(cap - 1 - n));
        if (!copy(out + n, reinterpret_cast<const void*>(from), (size_t)take)) return -1;
        for (const int end = n + take; n < end; ++n) {
            if (!out[n]) return n;
            if (out[n] < 32 || out[n] > 126) return -1;
        }
    }
    out[n] = 0;
    return n;
}

}  // namespace guardedtext
