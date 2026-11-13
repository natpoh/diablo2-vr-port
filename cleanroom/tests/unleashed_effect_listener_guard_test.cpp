// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/game_patch_plan_test.cpp - commit b1078de (EffectEnded and the four fingerprints in Signatures), v0.3.34.
// Its own test here; asserts kept on in a Release build.
// This file stays MPL 2.0.
//
#undef NDEBUG   // the checks are asserts: kept in a Release build
// [game_patches] effect_listener_guard (vr/unleashed_effect_listener_guard.h): what the fingerprints prove, and when the game's listener is called.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "unleashed_effect_listener_guard.h"

using namespace gamepatch;

static int32_t At(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }

// The four fingerprints against themselves, and with any one byte changed: each byte is compared.
static void Signatures() {
    using namespace gamepatches::abi;
    const struct { const uint16_t* sig; size_t size; } all[] = {
        {EndedSig, sizeof EndedSig / 2}, {EndedSiblingSig, sizeof EndedSiblingSig / 2}, {EndedTellSig, sizeof EndedTellSig / 2},
        {EndedCallerSig, sizeof EndedCallerSig / 2},
    };
    for (const auto& s : all) {
        uint8_t bytes[64];
        assert(s.size <= sizeof bytes);
        for (size_t i = 0; i < s.size; ++i) {
            assert(s.sig[i] <= 0xFF);   // none left out
            bytes[i] = uint8_t(s.sig[i]);
        }
        assert(SigMatches(bytes, s.sig, s.size));
        for (size_t i = 0; i < s.size; ++i) {
            bytes[i] ^= 0x01;
            assert(!SigMatches(bytes, s.sig, s.size));
            bytes[i] ^= 0x01;
        }
    }
}

static void EffectEnded() {
    using namespace gamepatches::abi;
    const size_t n = sizeof EndedSig / 2;
    const auto bytes = [](const uint16_t* sig, size_t count, uint8_t* out) { for (size_t i = 0; i < count; ++i) out[i] = uint8_t(sig[i]); };
    uint8_t ended[sizeof EndedSig / 2], sibling[sizeof EndedSiblingSig / 2], caller[sizeof EndedCallerSig / 2];
    bytes(EndedSig, n, ended);
    bytes(EndedSiblingSig, sizeof sibling, sibling);
    bytes(EndedCallerSig, sizeof caller, caller);
    // The function names itself where the listener looks for its own entry on the list: the fingerprint begins at its first byte.
    assert(LeaSelf(EndedSig, n, EndedSelfAt));
    assert(!LeaSelf(EndedSig, n, EndedSelfAt - 1) && !LeaSelf(EndedSig, n, n - 6) && !LeaSelf(nullptr, n, EndedSelfAt));
    uint16_t moved[sizeof EndedSig / 2];
    std::memcpy(moved, EndedSig, sizeof moved);
    moved[EndedSelfAt + 3] = 0xD0;                                          // another distance: another place
    assert(!LeaSelf(moved, n, EndedSelfAt));
    moved[EndedSelfAt + 3] = Any;                                           // a byte not compared proves nothing
    assert(!LeaSelf(moved, n, EndedSelfAt));
    // The hooked bytes: mov [rsp+10],rbx / push rdi / sub rsp,20 / mov rdi,[rcx+EndedEffectAt] - whole, with no branch or address in them,
    // and nothing in the proven bytes jumps into them.
    const uint8_t prologue[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x79, uint8_t(EndedEffectAt)};
    assert(EndedHookBytes == sizeof prologue && !std::memcmp(ended, prologue, sizeof prologue));
    assert(BranchesInto(ended, n, EndedRva, EndedRva + 1, EndedRva + EndedHookBytes - 1) == 0);
    // ... then the lock guard on effect + 0x20: lea rdx,[rdi+0x20] and the call - address 0x20 when the handle holds none.
    assert(ended[22] == 0x48 && ended[23] == 0x8D && ended[24] == 0x57 && ended[25] == 0x20 && ended[26] == 0xE8);
    assert(EndedRva + 31 + int64_t(At(ended + 27)) == EndedLockRva);
    assert(EndedLockRva < 0xa4696a && 0xa4696a - EndedLockRva < 0x40);    // the crash's RIP lies inside the lock helper
    assert(EndedRva < EndedEnd && EndedEnd - EndedRva > n);
    // The sibling tests the same pointer and returns without one: mov rcx,[rcx+0x10] / test rcx,rcx / jne / ret.
    assert(sibling[3] == EndedEffectAt && sibling[4] == 0x48 && sibling[5] == 0x85 && sibling[6] == 0xC9 && sibling[13] == 0xC3);
    assert(EndedSiblingRva + 2 + sizeof sibling == EndedRva);               // right before it: two int3 between
    // The tell hands rcx = the handle and rdx = r13; the caller's call reaches the tell's function with rdx = &effect.
    assert(EndedTellSig[0] == 0x49 && EndedTellSig[1] == 0x8B && EndedTellSig[2] == 0xD5);
    assert(EndedTellSig[7] == 0xFF && EndedTellSig[8] == 0x55);
    assert(caller[18] == 0x48 && caller[19] == 0x8D && caller[20] == 0x54 && caller[21] == 0x24 && caller[22] == 0x28 && caller[23] == 0xE8);
    const uint64_t tell = EndedCallerRva + 28 + int64_t(At(caller + 24));
    assert(tell == EndedTellFunctionRva && tell < EndedTellRva && EndedTellRva - tell < 0x300);
    // Called only for a handle read that holds an effect; the log's "another effect" never changes that.
    assert(EndedCall(true, 0x1234) == Ended::Call);
    assert(EndedCall(true, 0) == Ended::Skip);
    assert(EndedCall(false, 0x1234) == Ended::Skip && EndedCall(false, 0) == Ended::Skip);
    assert(!EndedOther(0x1234, 0x1234) && EndedOther(0x1234, 0x5678));
    assert(!EndedOther(0, 0x5678) && !EndedOther(0x1234, 0));
}

int main() {
    Signatures();
    EffectEnded();
    std::puts("effect listener guard: the four fingerprints, the hooked prologue, the lock at + 0x20, the sibling's test, the tell and the caller, "
              "and the listener called only for a handle that holds an effect passed");
    return 0;
}
