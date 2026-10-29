// scene_holds_test - [render] keep_roofs (vr/roof_keep.h) and [stereo] props_once_a_pair (vr/prop_hold.h) without the game:
// the expected bytes agree with each other (the calls reach the functions hooked), with the game's snapshot when there is one
// (dump_loader/D2R_image.bin, an image laid out at its RVAs), and the two decisions do what they say.
//   scene_holds_test [<D2R_image.bin>]

#include <cstdio>
#include <cstring>
#include <vector>

#include "prop_hold.h"
#include "roof_keep.h"

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { ++g_fail; printf("FAIL: %s\n", what); }
}

static bool SameAt(const std::vector<unsigned char>& img, uint64_t rva, const uint8_t* sig, size_t n) {
    return rva + n <= img.size() && memcmp(img.data() + rva, sig, n) == 0;
}

int main(int argc, char** argv) {
    // The bytes agree with each other: the hide branch calls the setter; Step's calls reach Prepare (through the thunk) and Simulate.
    Expect(roofkeep::HideCallsSetter(), "the roof hide branch calls the opacity setter");
    Expect(prophold::CallsReach(), "Step's two calls reach Prepare (through the thunk) and Simulate");

    // Roofs: only the hide branch's 0, only while kept.
    const uintptr_t hideRet = 0x140000000ull + roofkeep::kHideRva + roofkeep::kHideReturnOffset;
    Expect(roofkeep::TargetAlpha(true, hideRet, hideRet, 0.0f) == 1.0f, "kept: the hide request becomes alpha 1");
    Expect(roofkeep::TargetAlpha(false, hideRet, hideRet, 0.0f) == 0.0f, "off (or not F4): the game's alpha 0");
    Expect(roofkeep::TargetAlpha(true, hideRet + 1, hideRet, 0.0f) == 0.0f, "another caller (a creature's fade) keeps its 0");
    Expect(roofkeep::TargetAlpha(true, hideRet, 0, 0.0f) == 0.0f, "no hook address known: the game's own");
    Expect(roofkeep::TargetAlpha(true, hideRet, hideRet, 0.35f) == 0.35f, "a value other than 0 is passed on as it is");
    Expect(roofkeep::TargetAlpha(true, hideRet, hideRet, 1.0f) == 1.0f, "the show request stays 1");

    // Props: only Step's own call, only in a pair's right pass, only with both hooks in and wanted.
    const uintptr_t prepRet = 0x140000000ull + prophold::kCallsRva + prophold::kPrepareReturnOffset;
    Expect(prophold::Hold(true, true, true, 1, prepRet, prepRet), "the right pass of a pair: held");
    Expect(!prophold::Hold(true, true, true, 0, prepRet, prepRet), "the left pass runs");
    Expect(!prophold::Hold(true, true, false, 1, prepRet, prepRet), "a frame drawn once (no pair) runs");
    Expect(!prophold::Hold(false, true, true, 1, prepRet, prepRet), "not both hooks in: runs");
    Expect(!prophold::Hold(true, false, true, 1, prepRet, prepRet), "off (or not F4): runs");
    Expect(!prophold::Hold(true, true, true, 1, prepRet + 7, prepRet), "another caller of Prepare runs");
    Expect(!prophold::Hold(true, true, true, 1, prepRet, 0), "no return address known: runs");

    // The game's snapshot, when it is here: every expected byte at its RVA.
    const char* path = argc > 1 ? argv[1] : nullptr;
    if (path) {
        std::vector<unsigned char> img;
        FILE* f = nullptr;
        if (fopen_s(&f, path, "rb") == 0 && f) {
            fseek(f, 0, SEEK_END); img.resize((size_t)ftell(f)); fseek(f, 0, SEEK_SET);
            img.resize(fread(img.data(), 1, img.size(), f));
            fclose(f);
        }
        Expect(!img.empty(), "the snapshot reads");
        if (!img.empty()) {
            Expect(SameAt(img, roofkeep::kSetterRva, roofkeep::kSetterSig, sizeof roofkeep::kSetterSig), "snapshot: the opacity setter's bytes");
            Expect(SameAt(img, roofkeep::kHideRva, roofkeep::kHideSig, sizeof roofkeep::kHideSig), "snapshot: the hide branch's bytes");
            Expect(SameAt(img, prophold::kPrepareRva, prophold::kPrepareSig, sizeof prophold::kPrepareSig), "snapshot: Prepare's bytes");
            Expect(SameAt(img, prophold::kSimulateRva, prophold::kSimulateSig, sizeof prophold::kSimulateSig), "snapshot: Simulate's bytes");
            Expect(SameAt(img, prophold::kThunkRva, prophold::kThunkSig, sizeof prophold::kThunkSig), "snapshot: the thunk's bytes");
            Expect(SameAt(img, prophold::kCallsRva, prophold::kCallsSig, sizeof prophold::kCallsSig), "snapshot: Step's calls");
        }
        printf("snapshot %s checked\n", path);
    }
    if (g_fail) { printf("%d FAILED\n", g_fail); return 1; }
    printf("scene holds: all checks passed\n");
    return 0;
}
