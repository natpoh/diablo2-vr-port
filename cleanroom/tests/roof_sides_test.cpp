// roof_sides_test - [render] roofs_two_sided (vr/roof_sides.h) without the game: which materials are built double-sided (a roof
// model's roof materials: yes; every other: no; the tick off: none), which answer of the member lookup is changed, the nested
// build's scope, and the expected bytes - against each other and, when there is one, the game's snapshot (an image at its RVAs).
//   roof_sides_test [<D2R_image.bin>]

#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "roof_sides.h"

using namespace roofsides;

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { ++g_fail; printf("FAIL: %s\n", what); }
}

// As the hook sees them: the game's own spelling, made plain first.
static bool Pick(bool wanted, const char* model, const char* material) {
    char m[512], n[128];
    std::string_view mv, nv;
    return Canonical(std::string_view(model), m, &mv) && Canonical(std::string_view(material), n, &nv) && Chosen(wanted, mv, nv);
}

int main(int argc, char** argv) {
    // The word "roof".
    Expect(RoofToken("roof") && RoofToken("ext_roof_tile01") && RoofToken("slum_roof_b") && RoofToken("roof01") && RoofToken("a/roof/b"),
           "roof as a word: alone, between underscores, before a digit, between slashes");
    Expect(!RoofToken("roofless") && !RoofToken("proof") && !RoofToken("rooftop") && !RoofToken("waterproof_wall") && !RoofToken(""),
           "not inside another word");

    // Roof materials of roof models: yes.
    Expect(Pick(true, "data/hd/env/model/act3/docktown/act3_docktown_hut_roof01.model", "slum_roof_b"), "a Kurast hut roof");
    Expect(Pick(true, "DATA\\HD\\ENV\\MODEL\\ACT1\\TOWN\\Act1_Town_Roof_02.model", "Ext_Roof_Tile01"), "the game's own spelling (case, backslashes)");
    Expect(Pick(true, "data:hd/env/model/act2/town/act2_town_building_roof.model", "roof"), "a data: path");
    Expect(Pick(true, "data/hd/env/model/act5/town/act5_roof_lod1.model", "ext_roof_trim"), "a level of detail of a roof");

    // Every other: no.
    Expect(!Pick(true, "data/hd/env/model/act3/docktown/act3_docktown_hut_wall01.model", "slum_roof_b"),
           "a roof material on a model that is not a roof's (a wall's trim): no - the name alone is not enough");
    Expect(!Pick(true, "data/hd/env/model/act3/docktown/act3_docktown_hut_roof01.model", "slum_wall_a"), "a wall material on a roof model: no");
    Expect(!Pick(true, "data/hd/env/model/act2/town/act2_town_building_tavern/level01.model", "ext_wall01a"),
           "the tavern's walls (d2r-pcvr's list by hand): no");
    Expect(!Pick(true, "data/hd/env/model/act6/x_roof.model", "roof"), "not act 1 - 5: no");
    Expect(!Pick(true, "data/hd/objects/act1/roof01.model", "roof"), "not the environment's (an object): no");
    Expect(!Pick(true, "data/hd/character/monster/roof.model", "roof"), "a monster's: no");
    Expect(!Pick(true, "data/hd/env/model/act1/../roof.model", "roof"), "a path with ..: no");
    Expect(!Pick(true, "data/hd/env/model/act1/town/roof01.texture", "roof"), "not a .model: no");

    // The tick off: none at all.
    Expect(!Pick(false, "data/hd/env/model/act3/docktown/act3_docktown_hut_roof01.model", "slum_roof_b"), "the tick off: the game's own");

    // Not a plain path: no.
    char tiny[8];
    std::string_view v;
    Expect(!Canonical(std::string_view("data/hd/env/model/act1/long.model"), tiny, &v), "a name longer than the buffer is refused");
    Expect(!Canonical(std::string_view("data/\x01" "bad"), tiny, &v), "a control character is refused");

    // The answer to change: that material's IsDoubleSided, a boolean, 0.
    int t = 0, o = 0, other = 0;
    Context ctx;
    ctx.type = &t;
    ctx.object = &o;
    Expect(Override(&ctx, &t, &o, "IsDoubleSided", 19, 0, 0) && Override(&ctx, &t, &o, "IsDoubleSided", 20, 1, 0), "its IsDoubleSided 0: changed");
    Expect(!Override(&ctx, &t, &o, "IsDoubleSided", 19, 0, 1), "already double-sided: left");
    Expect(!Override(&ctx, &t, &o, "IsTransparent", 19, 0, 0), "another member: left");
    Expect(!Override(&ctx, &other, &o, "IsDoubleSided", 19, 0, 0), "another material's type: left");
    Expect(!Override(&ctx, &t, &other, "IsDoubleSided", 19, 0, 0), "another material's object: left");
    Expect(!Override(&ctx, &t, &o, "IsDoubleSided", 3, 0, 0), "not a boolean member: left");
    Expect(!Override(nullptr, &t, &o, "IsDoubleSided", 19, 0, 0), "outside a chosen build: left");

    // A nested build of another material clears the scope; the outer one gets it back.
    Context* slot = nullptr;
    {
        Scope outer(slot, &ctx);
        Expect(slot == &ctx, "inside a roof's build: its scope");
        {
            Scope inner(slot, nullptr);
            Expect(slot == nullptr, "a nested build of another material: none");
        }
        Expect(slot == &ctx, "back in the roof's build: its scope again");
    }
    Expect(slot == nullptr, "after the build: none");

    // The bytes agree with each other: the model's call reaches the builder, the builder's flag call the lookup.
    Expect(CallsReach(), "the build call reaches the builder and the flag call the member lookup");
    const char* path = argc > 1 ? argv[1] : nullptr;
    if (path) {
        std::vector<unsigned char> img;
        FILE* f = nullptr;
        if (fopen_s(&f, path, "rb") == 0 && f) {
            fseek(f, 0, SEEK_END);
            img.resize((size_t)ftell(f));
            fseek(f, 0, SEEK_SET);
            img.resize(fread(img.data(), 1, img.size(), f));
            fclose(f);
        }
        auto at = [&](uint64_t rva, const uint8_t* sig, size_t n) { return rva + n <= img.size() && memcmp(img.data() + rva, sig, n) == 0; };
        Expect(!img.empty(), "the snapshot reads");
        if (!img.empty()) {
            Expect(at(kBuildRva, kBuildSig, sizeof kBuildSig), "snapshot: the material builder");
            Expect(at(kFindRva, kFindSig, sizeof kFindSig), "snapshot: the member lookup");
            Expect(at(kBuildCallRva, kBuildCallSig, sizeof kBuildCallSig), "snapshot: the model's build call");
            Expect(at(kFlagCallRva, kFlagCallSig, sizeof kFlagCallSig), "snapshot: the builder's flag call");
            Expect(at(kFindResultRva, kFindResultSig, sizeof kFindResultSig), "snapshot: the lookup's result");
        }
        printf("snapshot %s checked\n", path);
    }
    if (g_fail) { printf("%d FAILED\n", g_fail); return 1; }
    printf("roof sides: all checks passed\n");
    return 0;
}
