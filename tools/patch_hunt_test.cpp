// patch_hunt_test - [debug] patch_hunt's steps (vr/patch_hunt.h) and - against a snapshot of the game's image (dump_loader/D2R_image.bin,
// tools/dump_d2r.py) - that the game's graphics settings are where it says: each accessor's first bytes, the name its first call gives, the object
// it returns, the range and default it gives every call, and the setter they share. Nothing here runs the game's code.
//
//   patch_hunt_test.exe [<snapshot>]     (default: dump_loader\D2R_image.bin beside the source tree; without one only the pure part runs)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "patch_hunt.h"

using namespace patchhunt;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, what)                                                     \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, what); } \
    } while (0)

static void TestPure() {
    CHECK(Next(0) == 1 && Next(kCount - 1) == 0 && Prev(0) == kCount - 1 && Prev(1) == 0, "the steps go round");
    CHECK(Clamp(-1) == 0 && Clamp(kCount) == 0 && Clamp(5) == 5, "a step out of range is 0");
    int touched[kGfxCount] = {};
    for (int s = 0; s < kCount; ++s)
        for (int g = 0; g < kGfxCount; ++g) touched[g] += Touches(s, g) && s != kAllAtOnce;
    for (int g = 0; g < kGfxCount; ++g) CHECK(touched[g] == 1, "every game setting belongs to one step (and to 9)");
    for (int g = 0; g < kGfxCount; ++g) CHECK(!Touches(kNormal, g) && !Touches(kDepthFix, g) && !Touches(kPipeline, g) && Touches(kAllAtOnce, g), "steps 0, 5, 8 leave the game's settings");
    CHECK(DepthFixOff(kDepthFix) && LightBoundsOff(kLightBounds) && OwnEffectsOff(kOwnEffects) && PipelineSkip(kPipeline), "each step its part");
    CHECK(DepthFixOff(kAllAtOnce) && LightBoundsOff(kAllAtOnce) && OwnEffectsOff(kAllAtOnce) && !PipelineSkip(kAllAtOnce), "9 is 1 - 7, not 8");
    CHECK(!DepthFixOff(kNormal) && !LightBoundsOff(kNormal) && !OwnEffectsOff(kNormal) && !PipelineSkip(kNormal), "0 leaves everything");
    for (int g = 0; g < kGfxCount; ++g) CHECK(kGfx[g].hunt >= kGfx[g].lo && kGfx[g].hunt <= kGfx[g].hi, "the step's value is in the setting's range");
    CHECK(Sane(kAo, 3) == 3 && Sane(kAo, 5) == -1 && Sane(kShadow, 0) == -1 && Sane(kResScale, 100) == 100, "values out of range are not known");
    for (int s = 0; s < kCount; ++s) CHECK(What(s) && *What(s), "every step has words");
}

static std::vector<uint8_t> g_img;
static bool At(uint64_t rva, const uint8_t* bytes, size_t n) { return rva + n <= g_img.size() && std::memcmp(&g_img[rva], bytes, n) == 0; }
static int32_t I32(uint64_t rva) { int32_t v = 0; std::memcpy(&v, &g_img[rva], 4); return v; }
static std::string Str(uint64_t rva) { return rva < g_img.size() ? std::string((const char*)&g_img[rva]) : std::string(); }

static void TestSnapshot() {
    CHECK(At(RVA_SETTING_SET, kSigSet, sizeof kSigSet), "the setter's first bytes");
    CHECK(RipTarget(RVA_SETTING_SET + 9, I32(RVA_SETTING_SET + 10), 5) == 0xD2EC90, "it stores through 0xD2EC90 ...");
    static const uint8_t kStore[] = {0x89, 0x46, 0x0C};   // mov [rsi+0xC], eax
    CHECK(At(0xD2ED5C, kStore, 3) && kValueAt == 0xC, "... which keeps the value at +0xC");
    static const uint8_t kListeners[] = {0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x10};   // mov rax, [rbx]; mov rcx, rbx; call [rax+0x10]
    CHECK(At(RVA_SETTING_SET + 0xE, kListeners, sizeof kListeners), "then calls the object's listeners");
    // the defaults and ranges each accessor gives every call: (default, lo, hi), read off the image
    struct Seen { int def, lo, hi; } seen[kGfxCount] = {{3, 0, 4}, {3, 1, 4}, {2, 0, 2}, {3, 0, 3}, {0, 0, 5}, {100, 50, 200}};
    for (int g = 0; g < kGfxCount; ++g) {
        const uint64_t a = kGfx[g].accessor;
        CHECK(At(a, kSigAccessorHead, 3) && (g_img[a + 3] == 0x58 || g_img[a + 3] == 0x78) && At(a + 4, kSigAccessorGs, sizeof kSigAccessorGs), kGfx[g].name);
        uint64_t object = 0;
        bool named = false;
        for (uint32_t i = 0; i + 10 <= kAccessorScan; ++i) {
            const uint64_t t = RipTarget(a + i, I32(a + i + 3), 7);
            if (!object && At(a + i, (const uint8_t*)"\x48\x8D\x05", 3) && At(a + i + 7, (const uint8_t*)"\x48\x83\xC4", 3)) object = t;
            if (!named && At(a + i, (const uint8_t*)"\x48\x8D\x15", 3)) named = Str(t) == kGfx[g].name;
        }
        CHECK(named, "the accessor's first call gives it the name");
        CHECK(object > 0x228F000 && object < 0x3BFF000, "it returns an object in .data");
        CHECK(kGfx[g].lo == seen[g].lo && kGfx[g].hi == seen[g].hi, "the range as the accessor gives it");
        // the default: mov edx, imm32 / xor edx, edx before lea rcx, [object]; call 0xD2EF30 - the first one in the accessor
        int def = -1;
        for (uint32_t i = 0x10; i + 12 <= kAccessorScan && def < 0; ++i) {
            if (!At(a + i, (const uint8_t*)"\x48\x8D\x0D", 3) || RipTarget(a + i, I32(a + i + 3), 7) != object || g_img[a + i + 7] != 0xE8) continue;
            if (RipTarget(a + i + 7, I32(a + i + 8), 5) != 0xD2EF30) continue;
            if (g_img[a + i - 5] == 0xBA) def = I32(a + i - 4);
            else if (g_img[a + i - 2] == 0x33 && g_img[a + i - 1] == 0xD2) def = 0;
        }
        CHECK(def == seen[g].def, "its default as read");
    }
}

int main(int argc, char** argv) {
    TestPure();
    std::string path = argc > 1 ? argv[1] : std::string(D2RVR_SOURCE_DIR) + "/dump_loader/D2R_image.bin";
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        g_img.resize((size_t)std::ftell(f));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(g_img.data(), 1, g_img.size(), f) != g_img.size()) g_img.clear();
        std::fclose(f);
    }
    if (g_img.size() > 0x3000000) TestSnapshot();
    else std::printf("  no snapshot at %s: the image checks are skipped\n", path.c_str());
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
