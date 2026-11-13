// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed vr/game_patch_signatures.h and steamvr/game_patch_plan.h - commit b1078de ("Guard the game's effect-ended
// listener"), v0.3.34. Ported to D2R VR Authentic: the effect_listener_guard part only, in a file of its own (the dev Tundra repro left out).
// This file stays MPL 2.0.
//
#pragma once
// [game_patches] effect_listener_guard: the fingerprints it proves before it hooks, and when the game's listener is called. The hook itself
// is in vr/unleashed_game_patches.cpp (it goes in and out like the six render patches); tests: cleanroom/tests/unleashed_effect_listener_guard_test.cpp
// (pure) and cleanroom/tests/game_patches_image_test.cpp (against the game's image).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "unleashed_game_patch_plan.h"   // gamepatch::Any, SigMatches, BranchesInto

namespace gamepatches::abi {

// Not a render change: a guard against a crash in the game's own effect code (Unleashed: two crash reports from a player in the Frozen
// Tundra, 2026-11-08 and 11-11: an access violation writing address 0x20 at RVA 0xA4696A, every return address of the two stacks the same).
// When a PopcornFX effect ends (EndedCallerRva, and RVA 0xA4CAF6 likewise) the game tells each listener on the effect's list - {handle,
// function} pairs at effect + 0x10, the list's lock at effect + 0x20 - one at a time (EndedTellRva: rcx the handle, rdx the address of the
// ending effect's pointer). The game's own effect handles listen with the function at EndedRva: it reads the effect the handle holds at
// handle + 0x10 with no check, takes that effect's list lock (a lock at + 0x20 of it: address 0x20 when the handle holds none - the lock
// helper at RVA 0xA46950, its lock cmpxchg at 0xA4696A), takes itself off the list and lets the effect go. A handle that holds no effect any
// more but is still on an ending effect's list is the crash. The game's other listener there (EndedSiblingRva) tests the same pointer first
// and does nothing without one; the guard does the same for this one, and only that: a handle that holds an effect is called as the game
// calls it. All four fingerprints hold in D2R 3.3.93787 under D2RLoader (dump_loader/D2R_image.bin) at these same addresses.
//
// The function's first 49 bytes: the handle's effect read (+ 0x10), the lock guard called on effect + 0x20, the list's count read, and
// lea r9,[rip-0x31] - the function's own address, which it looks for on the list.
inline constexpr uint64_t EndedRva = 0xf6a030, EndedEnd = 0xf6a147;
inline constexpr uint16_t EndedSig[] = {
    0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x79,0x10,0x48,0x8b,0xd9,0x48,0x8d,0x4c,0x24,0x30,0x48,0x8d,0x57,0x20,0xe8,0x01,
    0xc9,0xad,0xff,0x8b,0x57,0x18,0x85,0xd2,0x74,0x41,0x48,0x8b,0x4f,0x10,0x4c,0x8d,0x0d,0xcf,0xff,0xff,0xff};
inline constexpr uint32_t EndedHookBytes = 14;    // four whole instructions, no branch and no address in them
inline constexpr uint32_t EndedSelfAt = 42;       // in EndedSig: lea r9,[rip-0x31], the function's own address
inline constexpr uint64_t EndedEffectAt = 0x10;   // of the handle: the effect it holds, 0 = none
inline constexpr uint64_t EndedLockRva = 0xa46950;   // the lock helper the function calls on effect + 0x20 (the crash's RIP is inside it)
// The game's other effect-ended listener: rcx = [rcx + 0x10]; none -> return; else on to the effect.
inline constexpr uint64_t EndedSiblingRva = 0xf6a020;
inline constexpr uint16_t EndedSiblingSig[] = {0x48,0x8b,0x49,0x10,0x48,0x85,0xc9,0x0f,0x85,0x63,0xee,0xad,0xff,0xc3};
// The tell, one listener at a time: rdx = r13 (the address of the ending effect's pointer), rcx = the handle, call the function.
inline constexpr uint64_t EndedTellRva = 0xa4737a;
inline constexpr uint16_t EndedTellSig[] = {0x49,0x8b,0xd5,0x48,0x8b,0x4d,0x38,0xff,0x55,0x40};
// The caller on both crash reports' stacks: the effect's pointer kept at rsp + 0x28 (one reference more), the list at effect + 0x10, and the
// tell called with that list and the pointer's address.
inline constexpr uint64_t EndedCallerRva = 0xa4b3aa;
inline constexpr uint16_t EndedCallerSig[] = {
    0x48,0x89,0x7c,0x24,0x28,0x48,0x8d,0x4f,0x10,0x48,0x85,0xff,0x74,0x04,0xf0,0xff,0x47,0x08,0x48,0x8d,0x54,0x24,0x28,0xe8,0x2a,0xbd,0xff,0xff};
inline constexpr uint64_t EndedTellFunctionRva = 0xa470f0;   // what the caller's call reaches: the function the tell is in

}  // namespace gamepatches::abi

namespace gamepatch {

// The game's effect-ended listener is called for a handle only when the handle could be read and holds an effect; with none it would take a
// lock at address 0x20.
enum class Ended : uint8_t { Call, Skip };
inline Ended EndedCall(bool handleRead, uint64_t heldEffect) noexcept { return handleRead && heldEffect ? Ended::Call : Ended::Skip; }
// For the log only: a handle called for an effect it does not hold (it is called all the same, as the game does).
inline bool EndedOther(uint64_t heldEffect, uint64_t endingEffect) noexcept { return heldEffect && endingEffect && heldEffect != endingEffect; }
// lea r9,[rip+d32] (4C 8D 0D d32) at `at` of a fingerprint whose first byte is a function's: true when the instruction's address is that
// first byte - the function names itself there.
inline bool LeaSelf(const uint16_t* sig, size_t size, size_t at) noexcept {
    if (!sig || at + 7 > size || sig[at] != 0x4C || sig[at + 1] != 0x8D || sig[at + 2] != 0x0D) return false;
    uint8_t d[4];
    for (size_t i = 0; i < 4; ++i) {
        if (sig[at + 3 + i] > 0xFF) return false;
        d[i] = uint8_t(sig[at + 3 + i]);
    }
    int32_t disp = 0;
    std::memcpy(&disp, d, 4);
    return int64_t(at) + 7 + disp == 0;
}

}  // namespace gamepatch
