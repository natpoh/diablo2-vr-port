// prop_hold.h - [stereo] props_once_a_pair: a hanging lantern, a chain, a sign - the props the game swings with a partial ragdoll
// - are stepped once a stereo pair, not once an eye (vr/vrcam.cpp, namespace prophold).
//
// With [stereo] pair_per_tick the game's whole draw runs twice a game frame: the left eye with the frame's time, the right eye
// with [stereo] right_dt_ms (0.01 ms) - unless the replay ([stereo] replay_right) draws the right eye from the left eye's work:
// then the draw runs once and nothing here happens. The partial ragdoll is stepped inside the draw
// (D2PhysicsPartialRagdollSkeleton::Step 0x828FB0: Prepare 0x994C00 through a thunk, then Simulate 0x998B60, then the pose read
// back and put on the bones), so a second pass gave it a step of a hundredth of a millisecond, and its preparation divides by the
// time: a normal step and a tiny one by turns, and the lantern twitched. In the right pass of a pair the two stages are skipped
// and the physical state stays what the left pass made; Step itself still runs and puts the same pose on the bones in both eyes
// (skipping all of it would leave the right eye a reset pose). Only these two calls of Step's are touched (their return
// addresses); every other caller of Prepare and Simulate, the left eye and a frame drawn once keep the game's own.
// F4 only, like every new behaviour. Nothing of the game's data is written.
//
// Found by bearstonem/d2r-pcvr (MIT, v0.3.17: vr/stereo_prop_physics.h, docs/HANGING_FIXTURE_PHYSICS.md); written again here on
// our signatures (game_sigs.h, area "Roofs, hanging props").
#pragma once

#include <cstddef>
#include <cstdint>

namespace prophold {

// Addresses in 3.3.93787 (the keys of game_sigs.h) and the bytes each starts with there.
constexpr uint64_t kPrepareRva = 0x994C00, kSimulateRva = 0x998B60, kThunkRva = 0x998910, kCallsRva = 0x8290A9;
constexpr uint64_t kPrepareReturnOffset = 5, kSimulateReturnOffset = 17;   // from kCallsRva: where Step's two calls return
// mov rax, rsp; push rbp; push rsi; push r13; lea rbp, [rax-318h]
constexpr uint8_t kPrepareSig[14] = {0x48,0x8B,0xC4,0x55,0x56,0x41,0x55,0x48,0x8D,0xA8,0xE8,0xFC,0xFF,0xFF};
// push rbx; sub rsp, 40h; movaps [rsp+30h], xmm6; xorps xmm0, xmm0; movaps xmm6, xmm1
constexpr uint8_t kSimulateSig[17] = {0x40,0x53,0x48,0x83,0xEC,0x40,0x0F,0x29,0x74,0x24,0x30,0x0F,0x57,0xC0,0x0F,0x28,0xF1};
// call the thunk (-> Prepare); mov rcx, [rsi+48h]; movaps xmm1, xmm6; call Simulate; mov rcx, [rsi+48h]
constexpr uint8_t kCallsSig[21] = {0xE8,0x62,0xF8,0x16,0x00,0x48,0x8B,0x4E,0x48,0x0F,0x28,0xCE,0xE8,0xA6,0xFA,0x16,0x00,0x48,0x8B,0x4E,0x48};
// jmp Prepare
constexpr uint8_t kThunkSig[5] = {0xE9,0xEB,0xC2,0xFF,0xFF};

inline int32_t Rel32(const uint8_t* p) {
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}
// Step's first call reaches Prepare through the thunk and its second reaches Simulate (from the sigs' own rel32s).
inline bool CallsReach() {
    const int64_t first = (int64_t)(kCallsRva + kPrepareReturnOffset) + Rel32(kCallsSig + 1);
    const int64_t thunk = (int64_t)kThunkRva + 5 + Rel32(kThunkSig + 1);
    const int64_t second = (int64_t)(kCallsRva + kSimulateReturnOffset) + Rel32(kCallsSig + 13);
    return first == (int64_t)kThunkRva && thunk == (int64_t)kPrepareRva && second == (int64_t)kSimulateRva;
}

// Skip this stage: both hooks in, wanted (on, VR F4), inside a pair's right pass, called from Step's own call of it.
inline bool Hold(bool ready, bool wanted, bool inPair, int eye, uintptr_t caller, uintptr_t expected) {
    return ready && wanted && inPair && eye == 1 && expected != 0 && caller == expected;
}

}  // namespace prophold
