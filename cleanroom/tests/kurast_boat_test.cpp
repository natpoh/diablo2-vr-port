// kurast_boat_test - [world] kurast_boat: the Kurast Docks' flying canoe put on the water (shared/kurast_boat.h, after bearstonem/d2r-pcvr
// bed674b, MIT) and D2R_VR_Settings' maker of that preset (tools/settings/boat_assets.cpp).
//   Always, on made-up presets: the one change and every other byte kept; a boat already low or gone is no work; a preset not as known is
//   refused (the entity twice, another prefab, another place, not a preset); the boat's entity last in the list.
//   With the game (its archives read only, everything written under the scratch folder in the mod's layout <scratch>\mods\D2R-VR):
//   - not the mod (no modRoot), or the mod without its D2R-VR.mpq: nothing made, and said so;
//   - with D2R-VR.mpq: the game's own docktown3.json in mods\D2R-VR\D2R-VR.mpq\data\hd\env\preset\act3\docktown, read back and parsed: the canoe
//     (entity 3418903870, pf_canoe01_07_bob) at (0, 0, 0) with the game's orientation and scale, every other entity as the game has it, and the
//     file the game's own but for that one number; the receipt beside the mod;
//   - again: nothing written; a preset the player changed: left as it is; off: ours taken out, the receipt too; off with the player's own: it stays;
//   - everything went into mods\D2R-VR and nowhere else in the scratch.
//   kurast_boat_test [<game folder> <empty scratch folder>]

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "CascLib.h"
#include "arcane_sky.h"
#include "boat_assets.h"
#include "kurast_boat.h"
#include "props_presets.h"

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

static std::string Replace(std::string s, const std::string& from, const std::string& to) {
    const size_t at = s.find(from);
    return at == std::string::npos ? s : s.replace(at, from.size(), to);
}

// ---------------------------------------------------------------- made-up presets

static std::string Entity(const char* name, const char* id, const char* pos, const char* prefab) {
    return std::string("{\"type\":\"Entity\",\"name\":\"") + name + "\",\"id\":" + id + ",\"components\":[{\"type\":\"TransformDefinitionComponent\",\"name\":\"" + name +
           "_TransformDefinitionComponent\",\"position\":" + pos +
           ",\"orientation\":{\"x\":0.0,\"y\":0.0,\"z\":0.0,\"w\":1.0},\"scale\":{\"x\":1.0,\"y\":1.0,\"z\":1.0},\"inheritOnlyPosition\":false},"
           "{\"type\":\"PrefabPlacementDefinitionComponent\",\"name\":\"" + name + "_PrefabPlacementDefinitionComponent\",\"prefab\":\"" + prefab + "\"}]}";
}

static std::string Preset(const std::vector<std::string>& entities) {
    std::string s = "{\"dependencies\":{\"models\":[]},\"type\":\"Preset\",\"name\":\"preset\",\"entities\":[";
    for (size_t i = 0; i < entities.size(); ++i) s += (i ? "," : "") + entities[i];
    return s + "],\"terrain\":{\"type\":\"Entity\",\"name\":\"terrain\",\"id\":2693599502,\"components\":[]},\"biomeFilename\":\"x\",\"specialTiles\":{}}";
}

static const char* kCanoe06 = "data/hd/env/model/act3/docktown/act3_docktown_boats/prefab/pf_canoe01_06_bob.json";
static const char* kSky = "{\"x\":0.0,\"y\":50.0,\"z\":0.0}";

static void MadeUp() {
    using namespace kurastboat;
    const std::string before = Entity("prefab013", "1182335812", "{\"x\":377.2785,\"y\":-1.22609,\"z\":335.979}", kCanoe06);
    const std::string boat = Entity("prefab014", "3418903870", kSky, kPrefab);
    const std::string decoy = Entity("prefab099", "845336702", kSky, kCanoe06);   // another entity at (0, 50, 0): not ours, stays
    const std::string in = Preset({before, boat, decoy});
    std::string out, why;
    Expect(Lower(in, &out, &why) == kLowered, "the boat in the sky: lowered");
    Expect(out == Replace(in, boat, Replace(boat, kSky, "{\"x\":0.0,\"y\":0.0,\"z\":0.0}")), "... only its y changed, 50.0 -> 0.0; every other byte as it was");
    Expect(out.size() + 1 == in.size() && out.find(decoy) != std::string::npos, "... one character shorter; the other entity at (0, 50, 0) untouched");
    const std::string low = out;
    Expect(Lower(low, &out, &why) == kNoNeed, "already on the water: no work");

    Expect(Lower(Preset({before, decoy, boat}), &out, &why) == kLowered && out.find("\"y\":0.0,\"z\":0.0},\"orientation\"") != std::string::npos &&
               out.find("],\"terrain\"") != std::string::npos,
           "the boat's entity last in the list: lowered, the list's end kept");
    Expect(Lower(Preset({before, decoy}), &out, &why) == kNoNeed && out.empty(), "no boat (taken out): no work");
    Expect(Lower(Preset({before, Entity("prefab014", "3418903870", "{\"x\":0.0,\"y\":-2.5,\"z\":0.0}", kPrefab)}), &out, &why) == kNoNeed,
           "a boat already low (another value): no work");
    Expect(Lower(Preset({boat, before, boat}), &out, &why) == kRefused && out.empty(), "the entity twice: refused");
    printf("  %s\n", why.c_str());
    Expect(Lower(Preset({Entity("prefab014", "3418903870", kSky, kCanoe06)}), &out, &why) == kRefused, "another prefab: refused");
    Expect(Lower(Preset({Entity("prefab014", "3418903870", "{\"x\":10.0,\"y\":50.0,\"z\":3.0}", kPrefab)}), &out, &why) == kRefused,
           "high, but elsewhere than (0, 50, 0): refused");
    Expect(Lower(Replace(Preset({boat}), "\"type\":\"Preset\"", "\"type\":\"Prefab\""), &out, &why) == kRefused, "not a preset: refused");
    Expect(Lower("", &out, &why) == kRefused, "nothing: refused");
}

// ---------------------------------------------------------------- the game's own

static const propspresets::Value* Find(const propspresets::Value& entities, double id) {
    for (const auto& e : entities.items) {
        const propspresets::Value* v = e.Get("id");
        if (v && v->type == propspresets::Value::Number && v->n == id) return &e;
    }
    return nullptr;
}

static const propspresets::Value* Component(const propspresets::Value& entity, const char* type) {
    const propspresets::Value* cs = entity.Get("components");
    for (size_t i = 0; cs && i < cs->items.size(); ++i) {
        const propspresets::Value* t = cs->items[i].Get("type");
        if (t && t->s == type) return &cs->items[i];
    }
    return nullptr;
}

static double Num(const propspresets::Value* obj, const char* k1, const char* k2) {
    const propspresets::Value* a = obj ? obj->Get(k1) : nullptr;
    const propspresets::Value* b = a ? a->Get(k2) : nullptr;
    return b ? b->n : 1e30;
}

static int Game(const std::wstring& game, const std::wstring& into) {
    std::wstring root = game;
    root.pop_back();
    HANDLE storage = nullptr;
    std::string original;
    if (CascOpenStorage(root.c_str(), 0, &storage)) {
        HANDLE f = nullptr;
        if (CascOpenFile(storage, (std::string("data:") + kurastboat::kPreset).c_str(), 0, CASC_OPEN_BY_NAME, &f)) {
            ULONGLONG size = 0;
            CascGetFileSize64(f, &size);
            original.resize((size_t)size);
            DWORD got = 0;
            if (!CascReadFile(f, original.data(), (DWORD)size, &got) || got != size) original.clear();
            CascCloseFile(f);
        }
        CascCloseStorage(storage);
    }
    Expect(!original.empty(), "the game's own docktown3.json read from its archives");
    if (original.empty()) return 1;
    printf("  the game's docktown3.json: %zu bytes\n", original.size());

    CreateDirectoryW(into.c_str(), nullptr);
    const std::wstring modRoot = into + L"mods\\D2R-VR\\";
    CreateDirectoryW((into + L"mods").c_str(), nullptr);
    CreateDirectoryW(modRoot.c_str(), nullptr);
    const std::wstring mpq = modRoot + L"D2R-VR.mpq\\";
    const std::wstring file = mpq + L"data\\hd\\env\\preset\\act3\\docktown\\docktown3.json";
    const std::wstring receipt = modRoot + L"kurast_boat.json";

    boatassets::Result r = boatassets::Prepare(game, L"", true);
    Expect(r.state == boatassets::kNotHere && !r.wrote, "not the mod: nothing made");
    r = boatassets::Prepare(game, modRoot, true);
    Expect(r.state == boatassets::kNotHere && !r.wrote && !Exists(mpq), "the mod without D2R-VR.mpq: nothing made");
    printf("  %ls\n", r.text.c_str());

    CreateDirectoryW(mpq.c_str(), nullptr);
    const ULONGLONG t0 = GetTickCount64();
    r = boatassets::Prepare(game, modRoot, true);
    const ULONGLONG ms = GetTickCount64() - t0;
    Expect(r.state == boatassets::kMade && r.wrote, "with the mod's D2R-VR.mpq: made");
    printf("  %ls (%llu ms)\n", r.text.c_str(), (unsigned long long)ms);
    const std::string made = ReadAll(file);
    Expect(made == Replace(original, std::string(kurastboat::kInSky), kurastboat::kOnWater) && made.size() + 1 == original.size(),
           "the file is the game's own but for the canoe's one number");

    // Read back as the game would: parsed whole, the canoe and its neighbours looked up by id.
    propspresets::Value was, now;
    Expect(propspresets::Parser(original.data(), original.size()).Parse(was) && propspresets::Parser(made.data(), made.size()).Parse(now),
           "both presets parse");
    const propspresets::Value* ew = was.Get("entities");
    const propspresets::Value* en = now.Get("entities");
    Expect(ew && en && ew->items.size() == en->items.size() && en->items.size() > 4000, "the same number of entities (the boat kept)");
    if (!ew || !en) return 1;
    const propspresets::Value* boatWas = Find(*ew, 3418903870.0);
    const propspresets::Value* boatNow = Find(*en, 3418903870.0);
    Expect(boatWas && boatNow, "the canoe's entity 3418903870 is there");
    if (!boatWas || !boatNow) return 1;
    const propspresets::Value* tw = Component(*boatWas, "TransformDefinitionComponent");
    const propspresets::Value* tn = Component(*boatNow, "TransformDefinitionComponent");
    const propspresets::Value* pf = Component(*boatNow, "PrefabPlacementDefinitionComponent");
    Expect(pf && pf->Get("prefab") && pf->Get("prefab")->s == kurastboat::kPrefab, "... the canoe pf_canoe01_07_bob");
    printf("  the canoe: y %.3f -> %.3f\n", Num(tw, "position", "y"), Num(tn, "position", "y"));
    Expect(Num(tw, "position", "y") == 50.0, "the game's own places it at y 50");
    Expect(Num(tn, "position", "y") == kurastboat::kWaterY, "ours: at y 0, on the water (shared/kurast_boat.h)");
    Expect(Num(tn, "position", "x") == Num(tw, "position", "x") && Num(tn, "position", "z") == Num(tw, "position", "z"), "... x and z as the game's");
    bool same = true;
    for (const char* k : {"x", "y", "z", "w"}) same = same && Num(tn, "orientation", k) == Num(tw, "orientation", k);
    for (const char* k : {"x", "y", "z"}) same = same && Num(tn, "scale", k) == Num(tw, "scale", k);
    Expect(same, "... its orientation and scale as the game's");
    int moved = 0;
    for (size_t i = 0; i < en->items.size(); ++i) {
        const propspresets::Value* a = Component(ew->items[i], "TransformDefinitionComponent");
        const propspresets::Value* b = Component(en->items[i], "TransformDefinitionComponent");
        if (Num(a, "position", "x") != Num(b, "position", "x") || Num(a, "position", "y") != Num(b, "position", "y") ||
            Num(a, "position", "z") != Num(b, "position", "z"))
            ++moved;
    }
    Expect(moved == 1, "one entity moved, the canoe; every other where the game has it");

    const std::string rc = ReadAll(receipt);
    Expect(arcanesky::ReceiptValue(rc, "mods/D2R-VR/D2R-VR.mpq/data/hd/env/preset/act3/docktown/docktown3.json") == arcanesky::Hash(made.data(), made.size()),
           "the receipt names the file by its hash");
    printf("  the game's build in the receipt: %s\n", arcanesky::ReceiptValue(rc, "game_build").c_str());

    r = boatassets::Prepare(game, modRoot, true);
    Expect(r.state == boatassets::kMade && !r.wrote, "again: up to date, nothing written");

    WriteAll(file, made + " ");
    r = boatassets::Prepare(game, modRoot, true);
    Expect(r.state == boatassets::kMade && !r.wrote && ReadAll(file) == made + " ", "a preset the player changed: left as it is");
    printf("  %ls\n", r.text.c_str());
    r = boatassets::Prepare(game, modRoot, false);
    Expect(r.state == boatassets::kOff && !r.wrote && Exists(file), "off with the player's own preset: it stays");

    DeleteFileW(file.c_str());
    r = boatassets::Prepare(game, modRoot, true);
    Expect(r.state == boatassets::kMade && r.wrote && ReadAll(file) == made, "deleted: made again");
    r = boatassets::Prepare(game, modRoot, false);
    Expect(r.state == boatassets::kOff && r.wrote && !Exists(file) && !Exists(receipt), "off: ours taken out, the receipt too");
    printf("  %ls\n", r.text.c_str());
    r = boatassets::Prepare(game, modRoot, false);
    Expect(r.state == boatassets::kOff && !r.wrote, "off again: nothing to do");

    // only mods\D2R-VR was written in
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
    return 0;
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
    printf(g_fail ? "kurast_boat_test: %d FAILED\n" : "kurast_boat_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
