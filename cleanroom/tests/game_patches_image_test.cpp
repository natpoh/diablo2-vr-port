// game_patches_image_test.cpp - [game_patches] (vr/unleashed_game_patches.h): every check the six patches and effect_listener_guard make in the running game before
// they write, made here against a snapshot of the game's image in memory (tools/dump_d2r.py with D2RLoader.exe: dump_loader/D2R_image.bin,
// RVA = file offset). It proves, without starting the game, that on the build the mod knows (D2R 3.3.93787 under D2RLoader) each patch would
// go in: every fingerprint at its place, no jump into the filter's or the selection call's replaced bytes, the debug level's reader and
// writer pointing at the int, the level a lowerable one, the filter's new code within reach.
//
//   game_patches_image_test.exe [D2R_image.bin]     (default C:/wsl/d2r_vr/dump_loader/D2R_image.bin; no file = skipped, exit 0)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "unleashed_game_patch_plan.h"
#include "unleashed_game_patch_signatures.h"
#include "unleashed_effect_listener_guard.h"

namespace {
int g_fail = 0, g_pass = 0;
void Check(bool ok, const char* what) {
    if (ok) ++g_pass; else { ++g_fail; std::printf("FAIL: %s\n", what); }
}
std::vector<uint8_t> g_img;

template <size_t N>
bool At(uint64_t rva, const uint16_t (&sig)[N]) {
    return rva + N <= g_img.size() && gamepatch::SigMatches(g_img.data() + rva, sig, N);
}
int32_t Rel(uint64_t rva) { int32_t v; std::memcpy(&v, g_img.data() + rva, 4); return v; }
}  // namespace

int main(int argc, char** argv) {
    using namespace gamepatches::abi;
    const char* path = argc > 1 ? argv[1] : "C:/wsl/d2r_vr/dump_loader/D2R_image.bin";
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") == 0 && f) {
        std::fseek(f, 0, SEEK_END);
        g_img.resize((size_t)std::ftell(f));
        std::fseek(f, 0, SEEK_SET);
        g_img.resize(std::fread(g_img.data(), 1, g_img.size(), f));
        std::fclose(f);
    }
    if (g_img.size() < 0x3000000) { std::printf("game patches image: SKIPPED - no image at %s\n", path); return 0; }

    // Every fingerprint where the patch looks for it.
    Check(At(FilterHeadRva, FilterHeadSig), "transparent_filter: the loop's set-up");
    Check(At(FilterLoopRva, FilterLoopSig), "transparent_filter: the loop's first instructions");
    Check(At(FilterSkipRva, FilterSkipSig), "transparent_filter: the loop's end");
    Check(At(AccountCallRva, AccountCallSig), "profiler_accounting: the test and the calls");
    Check(At(AccountRva, AccountSig), "profiler_accounting: the function no longer called");
    Check(At(LevelGetRva, LevelGetSig) && At(LevelSetRva, LevelSetSig), "draw_debug_names: the level's reader and writer");
    Check(At(DrawNameBeginRva, DrawNameBeginSig) && At(DrawNameEndRva, DrawNameEndSig), "draw_debug_names: the draw submit's two tests");
    Check(At(EventBeginRva, EventBeginSig) && At(EventEndRva, EventEndSig), "draw_debug_names: the event begin's and end's tests");
    Check(At(SelectionCallRva, SelectionCallSig), "selection_walk: the call and what follows it");
    Check(At(ShadowUpdateRva, ShadowUpdateSig) && At(ShadowSpotCallRva, ShadowSpotCallSig) && At(ShadowSpotRva, ShadowSpotSig) &&
          At(ShadowFitCentreRva, ShadowFitCentreSig) && At(ShadowFitExtentRva, ShadowFitExtentSig) && At(ShadowCollectRva, ShadowCollectSig) &&
          At(ShadowBucketsRva, ShadowBucketsSig) && At(ShadowBucketLookupRva, ShadowBucketLookupSig) && At(ShadowRenderRva, ShadowRenderSig) &&
          At(ShadowRenderPassRva, ShadowRenderPassSig) && At(ShadowRenderEndRva, ShadowRenderEndSig) &&
          At(ShadowRenderSunCallRva, ShadowRenderSunCallSig) && At(ShadowRenderNegativeCallRva, ShadowRenderNegativeCallSig) &&
          At(ShadowKickRva, CullKickSig) && At(ShadowKickCallRva, ShadowKickCallSig) && At(ShadowWaitRva, ShadowWaitSig),
          "shadows_once: its 16 stretches");
    Check(At(CullKickRva, CullKickSig) && At(CullKickCallRva, CullKickCallSig) && At(CullWaitRva, CullWaitSig) && At(CullJobRva, CullJobSig) &&
          At(CullJobCopyRva, CullJobCopySig) && At(CullFrustumCopyRva, CullFrustumCopySig) && At(CullJobTestRva, CullJobTestSig) &&
          At(CullBoxRva, CullBoxSig) && At(CullBoxFrontRva, CullBoxFrontSig) && At(CullBoxEndRva, CullBoxEndSig) &&
          At(ShadowFitCentreRva, ShadowFitCentreSig) && At(ShadowFitExtentRva, ShadowFitExtentSig),
          "cull_once: its 12 stretches");

    // transparent_filter: nothing in the function lands inside the 7-byte load; the new code is planned for a page near the game.
    {
        const uint8_t* fn = g_img.data() + FilterFunctionRva;
        const size_t size = size_t(FilterFunctionEnd - FilterFunctionRva);
        const uint64_t site = FilterLoopRva;
        Check(gamepatch::BranchesInto(fn, size, FilterFunctionRva, site + 1, site + gamepatch::FilterLoadBytes - 1) == 0,
              "transparent_filter: no jump into the replaced load");
        Check(gamepatch::BranchesInto(fn, size, FilterFunctionRva, site, site) >= 1, "transparent_filter: the loop's way back to its first instruction is seen");
        const gamepatch::FilterPlan plan = gamepatch::PlanFilter(0x140000000ull + site, 0x140000000ull + FilterSkipRva, 0x140000000ull + 0x5700000ull,
                                                                 g_img[site + gamepatch::FilterLoadBytes]);
        Check(plan.ok && plan.site[0] == 0xE9 && plan.site[7] == g_img[site + 7], "transparent_filter: the jump and the new code planned");
    }
    // profiler_accounting: the byte made unconditional is a short je.
    Check(AccountCallSig[AccountJumpAt] == 0x74 && g_img[AccountCallRva + AccountJumpAt] == 0x74, "profiler_accounting: a short je at the jump");
    // draw_debug_names: both instructions reach the int, and the int holds a level the patch lowers.
    {
        const uint64_t byGet = LevelGetRva + 6 + int64_t(Rel(LevelGetRva + 2)), bySet = LevelSetRva + 6 + int64_t(Rel(LevelSetRva + 2));
        Check(byGet == LevelRva && bySet == LevelRva, "draw_debug_names: the reader and the writer reach the level's int");
        int32_t level = 0;
        std::memcpy(&level, g_img.data() + LevelRva, 4);
        std::printf("game patches image: the debug event level in this snapshot is %d\n", level);
        Check(gamepatch::LevelDecision(level) != gamepatch::Level::Refuse, "draw_debug_names: the int holds a debug level");
    }
    // selection_walk: nothing in the function lands inside the call; the call is a relative call.
    {
        const uint64_t call = SelectionCallRva + SelectionCallAt;
        const uint8_t* fn = g_img.data() + SelectionFunctionRva;
        Check(gamepatch::BranchesIntoInstruction(fn, size_t(SelectionFunctionEnd - SelectionFunctionRva), SelectionFunctionRva,
                                                 size_t(call - SelectionFunctionRva), gamepatch::CallBytes) == 0,
              "selection_walk: no jump into the call");
        Check(gamepatch::PlanCallNop(g_img.data() + call).ok, "selection_walk: a relative call, made a five-byte no-op");
    }
    // effect_listener_guard: its four stretches, nothing in the listener jumps into the hooked prologue, the listener's lock call reaches the
    // helper the crash's RIP (0xA4696A) is in, the function names itself, and the caller reaches the tell's function.
    Check(At(EndedRva, EndedSig) && At(EndedSiblingRva, EndedSiblingSig) && At(EndedTellRva, EndedTellSig) && At(EndedCallerRva, EndedCallerSig),
          "effect_listener_guard: its 4 stretches");
    {
        const uint8_t* fn = g_img.data() + EndedRva;
        Check(gamepatch::BranchesInto(fn, size_t(EndedEnd - EndedRva), EndedRva, EndedRva + 1, EndedRva + EndedHookBytes - 1) == 0,
              "effect_listener_guard: no jump into the hooked prologue");
        Check(EndedRva + 31 + int64_t(Rel(EndedRva + 27)) == EndedLockRva, "effect_listener_guard: the lock call on effect + 0x20 reaches the lock helper");
        // The crash: lock cmpxchg [rdx],r13d at 0xA4696A, rdx = effect + 0x20 = 0x20 when the handle holds none.
        const uint8_t cmpxchg[] = {0xF0, 0x44, 0x0F, 0xB1, 0x2A};
        Check(!std::memcmp(g_img.data() + 0xa4696a, cmpxchg, sizeof cmpxchg), "effect_listener_guard: the crash's RIP is the lock helper's lock cmpxchg [rdx]");
        Check(gamepatch::LeaSelf(EndedSig, sizeof EndedSig / 2, EndedSelfAt), "effect_listener_guard: the function names itself");
        Check(EndedCallerRva + 28 + int64_t(Rel(EndedCallerRva + 24)) == EndedTellFunctionRva, "effect_listener_guard: the caller reaches the tell's function");
        Check(EndedHookBytes <= sizeof EndedSig / 2, "effect_listener_guard: the hook's bytes inside its fingerprint");
    }
    // The hooks' byte counts lie inside the proven prologues.
    Check(ShadowUpdateHookBytes <= sizeof ShadowUpdateSig / 2 && ShadowCollectHookBytes <= sizeof ShadowCollectSig / 2 &&
          ShadowRenderHookBytes <= sizeof ShadowRenderSig / 2 && CullKickHookBytes <= sizeof CullKickSig / 2,
          "the hooks' bytes inside their fingerprints");

    std::printf("game patches image (%s): %d passed, %d failed\n", path, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
