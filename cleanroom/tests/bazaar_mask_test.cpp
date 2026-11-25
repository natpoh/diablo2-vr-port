// bazaar_mask_test - [holes] bazaar_mask_hidden: Kurast Bazaar's stray terrain-mask plane cut out of burbs08x16_1.json (shared/bazaar_mask.h)
// and D2R_VR_Settings' maker (tools/settings/bazaar_assets.cpp).
//   Always, on made-up presets: the entity cut with one comma (first, middle, last, alone in the list), every other byte kept; gone is no work;
//   refused: twice, another model, at the ground (not deep), not a preset, a brace inside a string kept whole.
//   With the game (its archives read only, everything written under the scratch folder in the mod's layout <scratch>\mods\D2R-VR): the real
//   preset cut and parsed back (one entity less, the rest as the game has them); the maker: not the mod / no .mpq: nothing; made; again:
//   nothing written; a preset the player changed stays; off: ours out, the receipt too; nothing outside mods\D2R-VR.
//   bazaar_mask_test [<game folder> <empty scratch folder>]

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"
#include "bazaar_assets.h"
#include "bazaar_mask.h"
#include "props_presets.h"

using namespace bazaarmask;

static int g_fail = 0;
static void Expect(bool ok, const char* what) {
    if (!ok) { ++g_fail; printf("FAIL: %s\n", what); }
}
static std::string ReadAll(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static void WriteAll(const std::wstring& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << data;
}
static std::wstring Slash(std::wstring p) {
    for (wchar_t& c : p) if (c == L'/') c = L'\\';
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    return p;
}
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static std::string Plane(const char* y = "-27.39841", const char* model = kModel, const char* id = "1197355067") {
    return std::string("{\"type\":\"Entity\",\"name\":\"terrainmask_plane_01\",\"id\":") + id +
           ",\"components\":[{\"type\":\"TransformDefinitionComponent\",\"name\":\"terrainmask_plane_01_TransformDefinitionComponent\",\"position\":{\"x\":-24.67274,\"y\":" + y +
           ",\"z\":-8.852175},\"orientation\":{\"x\":0.0,\"y\":0.0,\"z\":0.0,\"w\":1.0},\"scale\":{\"x\":2.150304,\"y\":1.0,\"z\":3.992939},\"inheritOnlyPosition\":false},"
           "{\"type\":\"ModelDefinitionComponent\",\"name\":\"terrainmask_plane_ModelDefinitionComponent\",\"filename\":\"" + model +
           "\",\"visibleLayers\":1,\"lightMask\":19,\"shadowMask\":3}]}";
}
static std::string Other(const char* name) {
    return std::string("{\"type\":\"Entity\",\"name\":\"") + name + "\",\"id\":5,\"components\":[{\"type\":\"ModelDefinitionComponent\",\"name\":\"m {not a brace]\",\"filename\":\"x.model\"}]}";
}
static std::string Preset(const std::vector<std::string>& ents) {
    std::string s = std::string("{\"dependencies\":{\"models\":[{\"path\":\"") + kModel + "\"}]},\"type\":\"Preset\",\"name\":\"p\",\"entities\":[";
    for (size_t i = 0; i < ents.size(); ++i) s += (i ? "," : "") + ents[i];
    return s + "],\"terrain\":{\"type\":\"Entity\",\"name\":\"terrain\",\"id\":2,\"components\":[]}}";
}

static void MadeUp() {
    std::string out, why;
    const std::string a = Other("a"), b = Other("b");
    Expect(Remove(Preset({a, Plane(), b}), &out, &why) == kRemoved && out == Preset({a, b}), "in the middle: cut with its comma, every other byte kept");
    Expect(Remove(Preset({Plane(), a, b}), &out, &why) == kRemoved && out == Preset({a, b}), "first: cut with the comma after it");
    Expect(Remove(Preset({a, b, Plane()}), &out, &why) == kRemoved && out == Preset({a, b}), "last: cut with the comma before it");
    Expect(Remove(Preset({Plane()}), &out, &why) == kRemoved && out == Preset({}), "alone: the list left empty");
    {
        propspresets::Value doc;
        Expect(Remove(Preset({a, Plane(), b}), &out, &why) == kRemoved && propspresets::Parser(out.data(), out.size()).Parse(doc) &&
                   doc.Get("entities")->items.size() == 2,
               "... still JSON, two entities");
    }
    Expect(Remove(Preset({a, b}), &out, &why) == kNoNeed && out.empty(), "no plane (taken out): no work");
    Expect(Remove(Preset({Plane(), a, Plane()}), &out, &why) == kRefused, "twice: refused");
    Expect(Remove(Preset({a, Plane("-27.4", "data/hd/env/model/x/other.model")}), &out, &why) == kRefused, "another model: refused");
    Expect(Remove(Preset({a, Plane("-4.9")}), &out, &why) == kRefused && out.empty(), "at the ground (as the Upper Kurast one): refused, never a mask in use");
    printf("  %s\n", why.c_str());
    std::string notPreset = Preset({Plane()});
    notPreset.replace(notPreset.find("\"Preset\""), 8, "\"Prefab\"");
    Expect(Remove(notPreset, &out, &why) == kRefused, "not a preset: refused");
    Expect(Remove("", &out, &why) == kRefused, "nothing: refused");
    std::string open = Preset({a, Plane()});
    open.resize(open.find("\"shadowMask\":3"));
    Expect(Remove(open, &out, &why) == kRefused, "cut short: refused");
}

static void Game(const std::wstring& game, const std::wstring& into) {
    std::wstring root = game;
    root.pop_back();
    HANDLE storage = nullptr;
    std::string original;
    if (CascOpenStorage(root.c_str(), 0, &storage)) {
        HANDLE f = nullptr;
        if (CascOpenFile(storage, (std::string("data:") + kPreset).c_str(), 0, CASC_OPEN_BY_NAME, &f)) {
            ULONGLONG size = 0;
            CascGetFileSize64(f, &size);
            original.resize((size_t)size);
            DWORD got = 0;
            if (!CascReadFile(f, original.data(), (DWORD)size, &got) || got != size) original.clear();
            CascCloseFile(f);
        }
        CascCloseStorage(storage);
    }
    Expect(!original.empty(), "the game's own burbs08x16_1.json read from its archives");
    if (original.empty()) return;
    std::string cut, why;
    const State s = Remove(original, &cut, &why);
    Expect(s == kRemoved, "the game's preset: the plane cut out");
    if (s != kRemoved) { printf("  why: %s\n", why.c_str()); return; }
    propspresets::Value a, b;
    Expect(propspresets::Parser(original.data(), original.size()).Parse(a) && propspresets::Parser(cut.data(), cut.size()).Parse(b), "both parse");
    const auto& ea = a.Get("entities")->items;
    const auto& eb = b.Get("entities")->items;
    printf("  the game's: %zu bytes, %zu entities; ours: %zu bytes, %zu entities\n", original.size(), ea.size(), cut.size(), eb.size());
    Expect(eb.size() + 1 == ea.size(), "one entity less");
    int planes = 0;
    for (const auto& e : eb) planes += e.Get("name")->s.find("terrainmask") != std::string::npos;
    Expect(planes == 0 && cut.find(kId) == std::string::npos, "no terrain-mask plane left in it");
    Expect(cut.size() < original.size() && original.find(cut.substr(0, 200)) == 0 && original.compare(original.size() - 200, 200, cut, cut.size() - 200, 200) == 0,
           "the rest as the game has it (its start and end the game's bytes)");

    const std::wstring modRoot = into + L"mods\\D2R-VR\\", mpq = modRoot + L"D2R-VR.mpq\\";
    CreateDirectoryW(into.c_str(), nullptr);
    CreateDirectoryW((into + L"mods").c_str(), nullptr);
    CreateDirectoryW(modRoot.c_str(), nullptr);
    std::wstring file = mpq + std::wstring(kPreset, kPreset + strlen(kPreset));
    for (wchar_t& c : file) if (c == L'/') c = L'\\';
    const std::wstring receipt = modRoot + L"bazaar_mask.json";
    bazaarassets::Result r = bazaarassets::Prepare(game, L"", true);
    Expect(r.state == bazaarassets::kNotHere && !r.wrote, "not the mod: nothing made");
    r = bazaarassets::Prepare(game, modRoot, true);
    Expect(r.state == bazaarassets::kNotHere && !r.wrote && !Exists(mpq), "the mod without D2R-VR.mpq: nothing made");
    CreateDirectoryW(mpq.c_str(), nullptr);
    r = bazaarassets::Prepare(game, modRoot, true);
    printf("  %ls\n", r.text.c_str());
    Expect(r.state == bazaarassets::kMade && r.wrote && ReadAll(file) == cut, "with the mod's D2R-VR.mpq: made, the cut preset");
    Expect(arcanesky::ReceiptValue(ReadAll(receipt), std::string("mods/D2R-VR/D2R-VR.mpq/") + kPreset).size() == 16, "the receipt names it");
    r = bazaarassets::Prepare(game, modRoot, true);
    Expect(r.state == bazaarassets::kMade && !r.wrote, "again: up to date, nothing written");
    WriteAll(file, cut + " ");
    r = bazaarassets::Prepare(game, modRoot, true);
    Expect(r.state == bazaarassets::kMade && !r.wrote && ReadAll(file) == cut + " ", "a preset the player changed: left as it is");
    r = bazaarassets::Prepare(game, modRoot, false);
    Expect(r.state == bazaarassets::kOff && !r.wrote && Exists(file), "off with the player's own preset: it stays");
    DeleteFileW(file.c_str());
    r = bazaarassets::Prepare(game, modRoot, true);
    Expect(r.state == bazaarassets::kMade && r.wrote && ReadAll(file) == cut, "deleted: made again");
    r = bazaarassets::Prepare(game, modRoot, false);
    Expect(r.state == bazaarassets::kOff && r.wrote && !Exists(file) && !Exists(receipt), "off: ours taken out, the receipt too");
    r = bazaarassets::Prepare(game, modRoot, false);
    Expect(r.state == bazaarassets::kOff && !r.wrote, "off again: nothing to do");

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((into + L"*").c_str(), &fd);
    int others = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n != L"." && n != L".." && n != L"mods") ++others;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    Expect(others == 0, "nothing written outside mods\\D2R-VR");
}

int wmain(int argc, wchar_t** argv) {
    MadeUp();
    if (argc >= 3) {
        const std::wstring game = Slash(argv[1]), into = Slash(argv[2]);
        if (_wcsnicmp(game.c_str(), into.c_str(), game.size()) == 0) { printf("the scratch folder must not be in the game's\n"); return 2; }
        Game(game, into);
    } else {
        printf("  (no game given: the made-up presets only)\n");
    }
    printf(g_fail ? "bazaar_mask_test: %d FAILED\n" : "bazaar_mask_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
