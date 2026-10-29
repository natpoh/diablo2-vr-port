// [weight] knock_props: D2R_VR_Settings' maker of the props' catalogue (tools/settings/props_assets.cpp) against the game's own archives, read only, writing into a folder
// of the test's own. Run: props_assets_test.exe <game folder> <empty scratch folder>
// Checks: it opens the game, finds the level presets, catalogues props of many kinds, writes d2r_vr_props\props.bin that reads back, and a second run finds it made.

#include <windows.h>

#include <cstdio>
#include <string>

#include "props_assets.h"
#include "props_scene.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::printf("usage: props_assets_test <game folder> <empty scratch folder>\n");
        return 2;
    }
    int fails = 0;
    const DWORD t0 = GetTickCount();
    const propsassets::Result r = propsassets::Prepare(argv[1], argv[2], true);
    std::printf("first run (%lu ms): state %d, %d props, %d kinds, %d presets - %ls\n", GetTickCount() - t0, (int)r.state, r.props, r.kinds, r.presets, r.text.c_str());
    if (r.state != propsassets::kMade || !r.wrote) { std::printf("  FAIL: not made\n"); ++fails; }
    if (r.presets < 500) { std::printf("  FAIL: too few presets (%d)\n", r.presets); ++fails; }
    if (r.props < 1000 || r.kinds < 30) { std::printf("  FAIL: too few props or kinds\n"); ++fails; }
    // the Rogue Encampment's props are in its prefabs (pf_town_tent01..03, wagons, campfire, smithy: 300-odd)
    std::printf("  prefabs with props %d, their props %d\n", r.prefabs, r.prefabProps);
    if (r.prefabs < 10 || r.prefabProps < 300) { std::printf("  FAIL: the prefabs' props are not read\n"); ++fails; }
    std::wstring bin = std::wstring(argv[2]) + L"\\d2r_vr_props\\props.bin";
    HANDLE f = CreateFileW(bin.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    std::string data;
    if (f != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        GetFileSizeEx(f, &size);
        data.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr);
        CloseHandle(f);
    }
    propscene::Catalog c;
    if (!c.Parse((const uint8_t*)data.data(), data.size()) || (int)c.Size() != r.props) { std::printf("  FAIL: props.bin does not read back\n"); ++fails; }
    else {
        std::printf("  kinds:");
        for (unsigned i = 0; i < c.KindCount(); ++i) std::printf(" %s", c.KindAt(i).name);
        std::printf("\n");
    }
    const propsassets::Result again = propsassets::Prepare(argv[1], argv[2], false);
    std::printf("second run: state %d, wrote %d - %ls\n", (int)again.state, (int)again.wrote, again.text.c_str());
    if (again.state != propsassets::kMade || again.wrote) { std::printf("  FAIL: the second run should find it made\n"); ++fails; }
    // [weight] grab_items: each item code's own model (d2r_vr_props\item_models.txt)
    const propsassets::Result m = propsassets::PrepareItemModels(argv[1], argv[2], true);
    std::printf("item models: state %d, %d codes - %ls\n", (int)m.state, m.props, m.text.c_str());
    if (m.state != propsassets::kMade || m.props < 300) { std::printf("  FAIL: too few item models\n"); ++fails; }
    {
        HANDLE g = CreateFileW((std::wstring(argv[2]) + L"\\d2r_vr_props\\item_models.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        std::string t;
        if (g != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER size{};
            GetFileSizeEx(g, &size);
            t.resize((size_t)size.QuadPart);
            DWORD got = 0;
            ReadFile(g, t.data(), (DWORD)t.size(), &got, nullptr);
            CloseHandle(g);
        }
        for (const char* want : {"hp1\tdata/hd/items/misc/potion/lesser_healing_potion.json\n", "hax\tdata/hd/items/weapon/axe/hand_axe.json\n", "cap\tdata/hd/items/armor/"})
            if (t.find(want) == std::string::npos) { std::printf("  FAIL: item_models.txt has no line '%s'\n", want); ++fails; }
    }
    const propsassets::Result m2 = propsassets::PrepareItemModels(argv[1], argv[2], false);
    if (m2.state != propsassets::kMade || m2.wrote || m2.props != m.props) { std::printf("  FAIL: the second make should find it made (%d, wrote %d, %d)\n", (int)m2.state, (int)m2.wrote, m2.props); ++fails; }
    std::printf(fails ? "props_assets_test: %d check(s) failed\n" : "props_assets_test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
