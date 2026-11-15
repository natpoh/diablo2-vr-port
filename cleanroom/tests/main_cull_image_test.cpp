// main_cull_image_test.cpp - vr/main_cull.h: what [weight] knock_down's hook on the game's main cull relies on, against a snapshot of the game's image in
// memory (tools/dump_d2r.py with D2RLoader.exe: dump_loader/D2R_image.bin, RVA = file offset). Without starting the game it proves, on the build the mod
// knows: the cull's first instructions are the hook's fingerprint; the world draw calls Renderer::UpdateSystems BEFORE the cull, and the cull kicks its
// jobs; a job reads an entity's world box at +0x40 / +0x50 of a 0x60-byte component - the bounds the mod grows.
//
//   main_cull_image_test.exe [D2R_image.bin]     (default C:/wsl/d2r_vr/dump_loader/D2R_image.bin; no file = skipped, exit 0)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "main_cull.h"

namespace {
int g_fail = 0, g_pass = 0;
void Check(bool ok, const char* what) {
    if (ok) ++g_pass; else { ++g_fail; std::printf("FAIL: %s\n", what); }
}
std::vector<uint8_t> g_img;
bool CallAt(const maincull::Call& c) {
    if (c.at + 5 > g_img.size() || g_img[c.at] != 0xE8) return false;
    int32_t rel = 0;
    std::memcpy(&rel, g_img.data() + c.at + 1, 4);
    return static_cast<int64_t>(c.at) + 5 + rel == static_cast<int64_t>(c.to);
}
}  // namespace

int main(int argc, char** argv) {
    using namespace maincull;
    const char* path = argc > 1 ? argv[1] : "C:/wsl/d2r_vr/dump_loader/D2R_image.bin";
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") == 0 && f) {
        std::fseek(f, 0, SEEK_END);
        g_img.resize((size_t)std::ftell(f));
        std::fseek(f, 0, SEEK_SET);
        g_img.resize(std::fread(g_img.data(), 1, g_img.size(), f));
        std::fclose(f);
    }
    if (g_img.size() < 0x3000000) { std::printf("main cull image: SKIPPED - no image at %s\n", path); return 0; }

    Check(!std::memcmp(g_img.data() + kCullRva, kCullSig, sizeof kCullSig), "the cull's first instructions are the hook's fingerprint");
    Check(g_img[kCullRva - 1] == 0xCC, "the fingerprint is a function's start (padding before it)");
    // the hook takes whole instructions only: 3 + 1 + 1 + 1 + 7 bytes, none with an address of its own (the next one reads the stack cookie, RIP-relative)
    Check(sizeof kCullSig == 13 && g_img[kCullRva + 13] == 0x48 && g_img[kCullRva + 14] == 0x8B && g_img[kCullRva + 15] == 0x05,
          "the fingerprint ends where the first RIP-relative instruction begins");
    Check(CallAt(kUpdateSystemsCall), "the world draw calls Renderer::UpdateSystems");
    Check(CallAt(kCullCall), "the world draw calls the cull");
    Check(kUpdateSystemsCall.at < kCullCall.at && kCullCall.at - kUpdateSystemsCall.at < 0x40, "UpdateSystems first, the cull right after it");
    Check(CallAt(kKickCall) && kKickCall.at > kCullRva && kKickCall.at - kCullRva < 0x300, "the cull kicks its jobs inside itself");
    Check(!std::memcmp(g_img.data() + kJobReadRva, kJobReadSig, sizeof kJobReadSig), "a job reads the entity's world box from the storage");
    // ... index x 3 x 32 = 0x60 a component; the box at +0x40 / +0x50: what knockprops::EntityBounds / PutEntityBounds read and write (floats 16 and 20 of 24)
    Check(kBoundsSize == 3 * 32 && kWorldMinAt == 16 * 4 && kWorldMaxAt == 20 * 4 && kBoundsSize == 24 * 4, "the bounds component the mod grows is the one tested");
    Check(g_img[kJobReadRva + 15] == kWorldMinAt && g_img[kJobReadRva + 19] == kWorldMaxAt, "the job's two reads are the world min and max");

    std::printf("main cull image: %d checks, %d failed\n", g_pass + g_fail, g_fail);
    return g_fail ? 1 : 0;
}
