// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/game_patch_plan_test.cpp - commits 853607f (transparent_filter), 133465a (profiler_accounting), 30083a1 (draw_debug_names), 9b9e28e (selection_walk, shadows_once, cull_once), 1289b9a (cull volume), at tag v0.3.33-windows-preview.
// Its test, present_once's part left out; asserts kept on in a Release build.
// This file stays MPL 2.0.
//
#undef NDEBUG   // the checks are asserts: kept in a Release build
// [game_patches]: fingerprints with bytes left out, and the bytes the transparent filter patch writes.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include "unleashed_game_patch_plan.h"
#include "unleashed_game_patch_signatures.h"

using namespace gamepatch;

static int32_t At(const uint8_t* p) { int32_t v; std::memcpy(&v, p, 4); return v; }

static void Signatures() {
    const uint8_t live[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00, 0x48};
    const uint16_t same[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00, 0x48};
    const uint16_t holes[] = {0x48, Any, 0x86, Any, Any, 0x00, 0x00, 0x48};
    const uint16_t other[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00, 0x49};   // the last byte differs
    const uint16_t first[] = {0x49, Any, Any, Any, Any, Any, Any, Any};          // only the first is compared, and differs
    const uint16_t bad[] = {0x48, 0x8B, 0x200, 0xA8, 0x00, 0x00, 0x00, 0x48};    // not a byte and not Any
    assert(SigMatches(live, same, 8));
    assert(SigMatches(live, holes, 8));
    assert(!SigMatches(live, other, 8));
    assert(SigMatches(live, other, 7));      // the differing byte is past what was asked
    assert(!SigMatches(live, first, 8));
    assert(!SigMatches(live, bad, 8));
    assert(!SigMatches(live, same, 0));      // nothing compared proves nothing
    assert(!SigMatches(nullptr, same, 8));
    assert(!SigMatches(live, nullptr, 8));

    // The game's fingerprints against themselves, and with any one byte changed: each byte is compared.
    using namespace gamepatches::abi;
    const struct { const uint16_t* sig; size_t size; } all[] = {
        {FilterHeadSig, sizeof FilterHeadSig / 2}, {FilterLoopSig, sizeof FilterLoopSig / 2}, {FilterSkipSig, sizeof FilterSkipSig / 2},
        {AccountCallSig, sizeof AccountCallSig / 2}, {AccountSig, sizeof AccountSig / 2},
        {LevelGetSig, sizeof LevelGetSig / 2}, {LevelSetSig, sizeof LevelSetSig / 2}, {DrawNameBeginSig, sizeof DrawNameBeginSig / 2},
        {DrawNameEndSig, sizeof DrawNameEndSig / 2}, {EventBeginSig, sizeof EventBeginSig / 2}, {EventEndSig, sizeof EventEndSig / 2},
        {SelectionCallSig, sizeof SelectionCallSig / 2},
        {ShadowUpdateSig, sizeof ShadowUpdateSig / 2}, {ShadowSpotCallSig, sizeof ShadowSpotCallSig / 2}, {ShadowSpotSig, sizeof ShadowSpotSig / 2},
        {ShadowFitCentreSig, sizeof ShadowFitCentreSig / 2}, {ShadowFitExtentSig, sizeof ShadowFitExtentSig / 2},
        {ShadowCollectSig, sizeof ShadowCollectSig / 2}, {ShadowBucketsSig, sizeof ShadowBucketsSig / 2}, {ShadowBucketLookupSig, sizeof ShadowBucketLookupSig / 2},
        {ShadowRenderSig, sizeof ShadowRenderSig / 2}, {ShadowRenderPassSig, sizeof ShadowRenderPassSig / 2}, {ShadowRenderEndSig, sizeof ShadowRenderEndSig / 2},
        {ShadowRenderSunCallSig, sizeof ShadowRenderSunCallSig / 2}, {ShadowRenderNegativeCallSig, sizeof ShadowRenderNegativeCallSig / 2},
        {ShadowKickCallSig, sizeof ShadowKickCallSig / 2}, {ShadowWaitSig, sizeof ShadowWaitSig / 2},
        {CullKickSig, sizeof CullKickSig / 2}, {CullKickCallSig, sizeof CullKickCallSig / 2}, {CullWaitSig, sizeof CullWaitSig / 2},
        {CullJobSig, sizeof CullJobSig / 2}, {CullJobCopySig, sizeof CullJobCopySig / 2}, {CullFrustumCopySig, sizeof CullFrustumCopySig / 2},
        {CullJobTestSig, sizeof CullJobTestSig / 2}, {CullBoxSig, sizeof CullBoxSig / 2}, {CullBoxFrontSig, sizeof CullBoxFrontSig / 2},
        {CullBoxEndSig, sizeof CullBoxEndSig / 2},
    };
    for (const auto& s : all) {
        uint8_t bytes[160];
        assert(s.size <= sizeof bytes);
        for (size_t i = 0; i < s.size; ++i) { assert(s.sig[i] <= 0xFF); bytes[i] = uint8_t(s.sig[i]); }
        assert(SigMatches(bytes, s.sig, s.size));
        for (size_t i = 0; i < s.size; ++i) {
            bytes[i] ^= 0x01;
            assert(!SigMatches(bytes, s.sig, s.size));
            bytes[i] ^= 0x01;
        }
    }
    // The loop's fingerprint begins with the load the jump replaces, then the byte that is kept.
    const uint8_t load[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00};
    for (size_t i = 0; i < sizeof load; ++i) assert(FilterLoopSig[i] == load[i]);
    assert(FilterLoopRva - FilterHeadRva == sizeof FilterHeadSig / 2);   // the head runs up to the loop: one stretch
    assert(FilterLoopRva % 8 == 0);                                      // the 8 bytes written are one aligned word
}

static void Displacements() {
    int32_t d = 0;
    assert(Rel32(0x1000, 0x1000, d) && d == 0);
    assert(Rel32(0x1000, 0x0FFB, d) && d == -5);
    assert(Rel32(0x100000000ull, 0x100000000ull + 0x7FFFFFFF, d) && d == INT32_MAX);
    assert(!Rel32(0x100000000ull, 0x100000000ull + 0x80000000ull, d));
    assert(Rel32(0x100000000ull, 0x100000000ull - 0x80000000ull, d) && d == INT32_MIN);
    assert(!Rel32(0x100000000ull, 0x100000000ull - 0x80000001ull, d));
}

static void Branches() {
    // 0x1000: jne back to 0x1000 (near) ; jmp +2 (short) ; call 0x2000 ; nops
    uint8_t code[32] = {};
    std::memset(code, 0x90, sizeof code);
    code[0] = 0x0F; code[1] = 0x85; const int32_t back = -6; std::memcpy(code + 2, &back, 4);   // -> 0x1000
    code[6] = 0xEB; code[7] = 0x02;                                                             // -> 0x100A
    code[8] = 0xE8; const int32_t call = 0x2000 - (0x1008 + 5); std::memcpy(code + 9, &call, 4);   // -> 0x2000
    code[13] = 0x74; code[14] = 0xF8;                                                           // je -> 0x1007
    assert(BranchesInto(code, sizeof code, 0x1000, 0x1000, 0x1000) == 1);
    assert(BranchesInto(code, sizeof code, 0x1000, 0x100A, 0x100A) == 1);
    assert(BranchesInto(code, sizeof code, 0x1000, 0x2000, 0x2000) == 1);
    assert(BranchesInto(code, sizeof code, 0x1000, 0x1001, 0x1006) == 0);   // nothing lands inside the first instruction
    assert(BranchesInto(code, sizeof code, 0x1000, 0x1001, 0x1007) == 1);   // the je does, into the short jump's second byte
    assert(BranchesInto(code, sizeof code, 0x1000, 0x1000, 0x2000) == 4);
    assert(BranchesInto(code, 6, 0x1000, 0x100A, 0x100A) == 0);             // only what is given is read
    assert(BranchesInto(code, 7, 0x1000, 0x100A, 0x100A) == 0);             // a short jump cut off after its first byte
    assert(BranchesInto(nullptr, 8, 0x1000, 0, ~0ull) == 0);
    // The game's loop: from the skip stretch, the way back lands on the load's first byte and nowhere inside it.
    using namespace gamepatches::abi;
    uint8_t skip[sizeof FilterSkipSig / 2];
    for (size_t i = 0; i < sizeof skip; ++i) skip[i] = uint8_t(FilterSkipSig[i]);
    assert(BranchesInto(skip, sizeof skip, FilterSkipRva, FilterLoopRva, FilterLoopRva) == 1);
    assert(BranchesInto(skip, sizeof skip, FilterSkipRva, FilterLoopRva + 1, FilterLoopRva + 6) == 0);
    // ... and the loop's own flag test leaves for the skip stretch's first byte.
    uint8_t loop[sizeof FilterLoopSig / 2];
    for (size_t i = 0; i < sizeof loop; ++i) loop[i] = uint8_t(FilterLoopSig[i]);
    assert(BranchesInto(loop, sizeof loop, FilterLoopRva, FilterSkipRva, FilterSkipRva) == 1);
    assert(BranchesInto(loop, sizeof loop, FilterLoopRva, FilterLoopRva + 1, FilterLoopRva + 6) == 0);
    assert(FilterFunctionRva < FilterHeadRva && FilterSkipRva + sizeof skip < FilterFunctionEnd);
}

// The profiler accounting call: what the fingerprint itself proves about the jump that is changed.
static void Accounting() {
    using namespace gamepatches::abi;
    uint8_t call[sizeof AccountCallSig / 2];
    for (size_t i = 0; i < sizeof call; ++i) call[i] = uint8_t(AccountCallSig[i]);
    assert(call[AccountJumpAt] == 0x74);                                   // je, one byte of distance
    const uint64_t target = AccountCallRva + AccountJumpAt + 2 + call[AccountJumpAt + 1];
    assert(target == AccountCallRva + 25);                                 // past both calls ...
    const uint8_t end[] = {0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3};   // ... at the function's end: restore, return
    assert(sizeof call == 25 + sizeof end && !std::memcmp(call + 25, end, sizeof end));
    assert(call[AccountSecondCallAt] == 0xE8);
    assert(AccountCallRva + AccountSecondCallAt + 5 + At(call + AccountSecondCallAt + 1) == AccountRva);   // the call skipped is the function proven
    assert(AccountJumpAlways == 0xEB);                                     // jmp rel8: same length, same distance
}

// The debug event level: the fingerprints' own distances lead to the one int and to its reader.
static void DebugNames() {
    using namespace gamepatches::abi;
    const auto bytes = [](const uint16_t* sig, size_t n, uint8_t* out) { for (size_t i = 0; i < n; ++i) out[i] = uint8_t(sig[i]); };
    uint8_t get[7], set[7], begin[14], end[27], eventBegin[9], eventEnd[9];
    bytes(LevelGetSig, 7, get); bytes(LevelSetSig, 7, set); bytes(DrawNameBeginSig, 14, begin); bytes(DrawNameEndSig, 27, end);
    bytes(EventBeginSig, 9, eventBegin); bytes(EventEndSig, 9, eventEnd);
    // mov eax, [rip + d] ; ret   and   mov [rip + d], ecx ; ret: both at the level's int
    assert(get[0] == 0x8B && get[1] == 0x05 && get[6] == 0xC3 && LevelGetRva + 6 + At(get + 2) == LevelRva);
    assert(set[0] == 0x89 && set[1] == 0x0D && set[6] == 0xC3 && LevelSetRva + 6 + At(set + 2) == LevelRva);
    // the four tests call that reader ...
    assert(DrawNameBeginRva + 5 + At(begin + 1) == LevelGetRva && DrawNameEndRva + 5 + At(end + 1) == LevelGetRva);
    assert(EventBeginRva + 5 + At(eventBegin + 1) == LevelGetRva && EventEndRva + 5 + At(eventEnd + 1) == LevelGetRva);
    // ... a draw's name on both sides against 1 (cmp eax, 1 ; jle), an event's begin and end against 0 (test eax, eax ; jle)
    const uint8_t above1[] = {0x83, 0xF8, 0x01}, above0[] = {0x85, 0xC0};
    assert(!std::memcmp(begin + 5, above1, 3) && begin[8] == 0x0F && begin[9] == 0x8E);
    assert(!std::memcmp(end + 5, above1, 3) && end[8] == 0x7E);
    assert(!std::memcmp(eventBegin + 5, above0, 2) && eventBegin[7] == 0x7E && !std::memcmp(eventEnd + 5, above0, 2) && eventEnd[7] == 0x7E);
    // the draw's end event is the event end proven (its test is 9 bytes into that function)
    assert(end[13] == 0xE8 && DrawNameEndRva + 18 + At(end + 14) == EventEndRva - 9);
    // so the level written keeps the passes' events and drops the draws' names
    assert(LevelWithoutDrawNames > 0 && !(LevelWithoutDrawNames > 1));
}

static void Filter() {
    // The game at 0x7FF6'0000'0000, the cave 0x10000 below it.
    const uint64_t base = 0x7FF600000000ull, site = base + 0xFDC740, skip = base + 0xFDCAAD, cave = base - 0x10000;
    const FilterPlan p = PlanFilter(site, skip, cave, 0x48);
    assert(p.ok);
    // cave: test dword ptr [rsi+8], 0x21800000 / jz skip / mov rax,[rsi+0xA8] / jmp site+7
    const uint8_t test[] = {0xF7, 0x46, 0x08, 0x00, 0x00, 0x80, 0x21};
    assert(!std::memcmp(p.cave, test, 7));
    assert(p.cave[7] == 0x0F && p.cave[8] == 0x84);
    assert(cave + 13 + At(p.cave + 9) == skip);
    assert(At(p.cave + 9) == int32_t(0xFDCAAD + 0x10000 - 13));
    const uint8_t load[] = {0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00};
    assert(!std::memcmp(p.cave + 13, load, 7));
    assert(p.cave[20] == 0xE9);
    assert(cave + 25 + At(p.cave + 21) == site + 7);
    // site: jmp cave, a two-byte nop, the eighth byte as it was
    assert(p.site[0] == 0xE9);
    assert(site + 5 + At(p.site + 1) == cave);
    assert(At(p.site + 1) == -int32_t(0xFDC740 + 0x10000 + 5));
    assert(p.site[5] == 0x66 && p.site[6] == 0x90 && p.site[7] == 0x48);
    // The exact bytes for these addresses.
    const uint8_t wantCave[25] = {0xF7, 0x46, 0x08, 0x00, 0x00, 0x80, 0x21, 0x0F, 0x84, 0xA0, 0xCA, 0xFE, 0x00,
                                  0x48, 0x8B, 0x86, 0xA8, 0x00, 0x00, 0x00, 0xE9, 0x2E, 0xC7, 0xFE, 0x00};
    const uint8_t wantSite[8] = {0xE9, 0xBB, 0x38, 0x01, 0xFF, 0x66, 0x90, 0x48};
    assert(!std::memcmp(p.cave, wantCave, sizeof wantCave));
    assert(!std::memcmp(p.site, wantSite, sizeof wantSite));

    // A cave above the game works the same.
    const FilterPlan up = PlanFilter(site, skip, base + 0x40000000, 0x48);
    assert(up.ok && site + 5 + At(up.site + 1) == base + 0x40000000 && base + 0x40000000 + 13 + At(up.cave + 9) == skip);

    // Out of a relative jump's reach, either way: no plan, nothing to write.
    const FilterPlan farUp = PlanFilter(site, skip, site + 0x80000010ull, 0x48);
    const FilterPlan farDown = PlanFilter(site, skip, site - 0x80000010ull, 0x48);
    assert(!farUp.ok && !farDown.ok);
    for (uint8_t b : farUp.site) assert(b == 0);
    for (uint8_t b : farUp.cave) assert(b == 0);
    // The very edge: the jump to the cave reaches, the way back does not (the cave's end is 20 bytes farther).
    const FilterPlan edge = PlanFilter(site, skip, site + 5 + 0x7FFFFFFFull, 0x48);
    assert(!edge.ok);
    // No address, no plan.
    assert(!PlanFilter(0, skip, cave, 0x48).ok && !PlanFilter(site, 0, cave, 0x48).ok && !PlanFilter(site, skip, 0, 0x48).ok);
}

static void Levels() {
    assert(LevelDecision(0) == Level::Leave && LevelDecision(1) == Level::Leave);
    assert(LevelDecision(2) == Level::Lower && LevelDecision(3) == Level::Lower && LevelDecision(16) == Level::Lower);
    assert(LevelDecision(17) == Level::Refuse && LevelDecision(-1) == Level::Refuse && LevelDecision(0x3F800000) == Level::Refuse);
    assert(LevelWithoutDrawNames == 1);
}

// selection_walk: the call's five bytes become one instruction that does nothing; anything but a call is refused.
static void SelectionWalk() {
    using namespace gamepatches::abi;
    uint8_t live[sizeof SelectionCallSig / 2];
    for (size_t i = 0; i < sizeof live; ++i) live[i] = uint8_t(SelectionCallSig[i]);
    assert(SelectionCallAt + CallBytes <= sizeof live);
    assert(live[SelectionCallAt] == 0xE8);
    // the call goes to the walk, 0xFA8EE0, and what follows it is the test of the window's byte through r15
    const uint64_t next = SelectionCallRva + SelectionCallAt + CallBytes;
    assert(next + int64_t(At(live + SelectionCallAt + 1)) == 0xfa8ee0);
    assert(live[SelectionCallAt + 5] == 0x41 && live[SelectionCallAt + 6] == 0x80 && live[SelectionCallAt + 7] == 0x3F && live[SelectionCallAt + 8] == 0x00);
    assert(SelectionCallRva >= SelectionFunctionRva && next < SelectionFunctionEnd);
    const CallNop plan = PlanCallNop(live + SelectionCallAt);
    const uint8_t nop[] = {0x0F, 0x1F, 0x44, 0x00, 0x00};
    assert(plan.ok && !std::memcmp(plan.bytes, nop, sizeof nop) && sizeof plan.bytes == CallBytes);
    assert(!PlanCallNop(live).ok);           // not a call
    assert(!PlanCallNop(nullptr).ok);
    const uint8_t jump[] = {0xE9, 0, 0, 0, 0};
    assert(!PlanCallNop(jump).ok);
    // The stretch holds no jump into the middle of the call. Read as jumps, the call's own distance (76 FF) is
    // one that lands in the call: its bytes are left out of the search, and a jump before or after is still found.
    assert(BranchesInto(live, sizeof live, SelectionCallRva, SelectionCallRva + SelectionCallAt + 1, SelectionCallRva + SelectionCallAt + 4) == 1);
    assert(BranchesIntoInstruction(live, sizeof live, SelectionCallRva, SelectionCallAt, CallBytes) == 0);
    uint8_t jumps[sizeof live];
    std::memcpy(jumps, live, sizeof live);
    jumps[0] = 0xEB; jumps[1] = uint8_t(SelectionCallAt + 2 - 2);          // jmp to the call's third byte, from the stretch's start
    assert(BranchesIntoInstruction(jumps, sizeof jumps, SelectionCallRva, SelectionCallAt, CallBytes) == 1);
    jumps[1] = uint8_t(SelectionCallAt - 2);                                // to the call's first byte: that is no jump into its middle
    assert(BranchesIntoInstruction(jumps, sizeof jumps, SelectionCallRva, SelectionCallAt, CallBytes) == 0);
    std::memcpy(jumps, live, sizeof live);
    jumps[sizeof live - 2] = 0xEB; jumps[sizeof live - 1] = uint8_t(-int(sizeof live - SelectionCallAt - 1));   // back to the call's second byte, from the stretch's end
    assert(BranchesIntoInstruction(jumps, sizeof jumps, SelectionCallRva, SelectionCallAt, CallBytes) == 1);
    assert(BranchesIntoInstruction(live, sizeof live, SelectionCallRva, sizeof live, CallBytes) == 0);      // an instruction past the code: nothing to find
    assert(BranchesIntoInstruction(nullptr, sizeof live, SelectionCallRva, SelectionCallAt, CallBytes) == 0);
}

// Once per pair: what a second eye pass may keep of the first's shadows.
static void ShadowPasses() {
    ShadowPair pair;
    const uint64_t passes = 0x1000, other = 0x2000;
    // a pair: the first fits, the second keeps
    assert(ShadowUpdate(true, Pass::First, passes, true, pair) == Shadow::Fit && pair.fitted && pair.passes == passes);
    assert(ShadowUpdate(true, Pass::Second, passes, true, pair) == Shadow::Keep);
    // ... once: a second update of the same pass fits (nothing is left)
    assert(!pair.fitted && ShadowUpdate(true, Pass::Second, passes, true, pair) == Shadow::Fit);
    // off, or a draw that stands alone: always the game's own, and nothing is left for a later pass
    pair = {true, passes};
    assert(ShadowUpdate(false, Pass::Second, passes, true, pair) == Shadow::Fit && !pair.fitted);
    pair = {true, passes};
    assert(ShadowUpdate(true, Pass::Alone, passes, true, pair) == Shadow::Fit && !pair.fitted);
    assert(ShadowUpdate(true, Pass::Second, passes, true, pair) == Shadow::Fit);
    // a second pass with no first seen (the first was not drawn, or drew no world)
    pair = {};
    assert(ShadowUpdate(true, Pass::Second, passes, true, pair) == Shadow::Fit);
    // a first pass whose fit went by the visible set (no look-at point): the second fits its own
    assert(ShadowUpdate(true, Pass::First, passes, false, pair) == Shadow::Fit && !pair.fitted);
    assert(ShadowUpdate(true, Pass::Second, passes, true, pair) == Shadow::Fit);
    // the look-at point gone in the second pass, or another object
    assert(ShadowUpdate(true, Pass::First, passes, true, pair) == Shadow::Fit);
    assert(ShadowUpdate(true, Pass::Second, passes, false, pair) == Shadow::Fit);
    assert(ShadowUpdate(true, Pass::First, passes, true, pair) == Shadow::Fit);
    assert(ShadowUpdate(true, Pass::Second, other, true, pair) == Shadow::Fit);
    assert(ShadowUpdate(true, Pass::First, 0, true, pair) == Shadow::Fit && !pair.fitted);
    // a first pass drawn again (a new pair) replaces what the one before left
    assert(ShadowUpdate(true, Pass::First, other, true, pair) == Shadow::Fit);
    assert(ShadowUpdate(true, Pass::First, passes, true, pair) == Shadow::Fit);
    assert(ShadowUpdate(true, Pass::Second, passes, true, pair) == Shadow::Keep);
    // the offsets the hooks use, against the fingerprints that prove them
    using namespace gamepatches::abi;
    assert(ShadowSpotCallSig[8] == 0x70 && ShadowSpotCallSig[9] == 0x05 && ShadowSpotOffset == 0x570);          // lea rcx, [rbx + 0x570]
    assert(ShadowRenderNegativeCallSig[31] == 0x90 && ShadowRenderNegativeCallSig[32] == 0x02 && ShadowViewSize == 0x290);   // lea rcx, [r13 + 0x290]
    assert(ShadowFitCentreSig[10] == 0x28 && ShadowFitCentreSig[11] == 0x01 && CameraHasLookAt == 0x128);
    assert(ShadowBucketsSig[58] == 0x08 && ShadowBucketsSig[62] == 0x08 && ShadowBucketCount == 8);            // mov [rdi + 8], rax / mov [rbx + 8], rax
    // where the stretches' own displacements go: the two bucket names, the lookup, the spot lights' update, the two kicks
    const auto to = [](uint64_t rva, const uint16_t* sig, size_t at, size_t size) {
        uint8_t d[4];
        for (int i = 0; i < 4; ++i) d[i] = uint8_t(sig[at + size - 4 + i]);
        return rva + at + size + int64_t(At(d));
    };
    assert(to(ShadowBucketsRva, ShadowBucketsSig, 0, 7) == ShadowBucketNameRva[0]);
    assert(to(ShadowBucketsRva, ShadowBucketsSig, 24, 7) == ShadowBucketNameRva[1]);
    assert(to(ShadowBucketsRva, ShadowBucketsSig, 19, 5) == ShadowBucketLookupRva && to(ShadowBucketsRva, ShadowBucketsSig, 37, 5) == ShadowBucketLookupRva);
    assert(to(ShadowSpotCallRva, ShadowSpotCallSig, 18, 5) == ShadowSpotRva);
    assert(to(ShadowKickCallRva, ShadowKickCallSig, 75, 5) == ShadowKickRva);
    assert(to(CullKickCallRva, CullKickCallSig, 67, 5) == CullKickRva);
    assert(to(ShadowRenderSunCallRva, ShadowRenderSunCallSig, 54, 5) == ShadowRenderRva);
    assert(to(ShadowRenderNegativeCallRva, ShadowRenderNegativeCallSig, 58, 5) == ShadowRenderRva);
    assert(to(CullJobTestRva, CullJobTestSig, 26, 5) == CullBoxRva);
    assert(to(CullJobCopyRva, CullJobCopySig, 71, 5) == CullFrustumCopyRva);
    // the hooks sit on whole instructions inside their fingerprints
    assert(ShadowUpdateHookBytes <= sizeof ShadowUpdateSig / 2 && ShadowCollectHookBytes <= sizeof ShadowCollectSig / 2 &&
           ShadowRenderHookBytes <= sizeof ShadowRenderSig / 2 && CullKickHookBytes <= sizeof CullKickSig / 2);
}

int main() {
    Signatures();
    SelectionWalk();
    ShadowPasses();
    Displacements();
    Branches();
    Accounting();
    DebugNames();
    Filter();
    Levels();
    std::puts("game patch plan: fingerprints, displacements, jumps into a place, the filter's cave and jump, out-of-reach refusal, levels, "
              "the call that is no longer made, the shadows a second pass keeps passed");
    return 0;
}
