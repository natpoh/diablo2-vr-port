// [mods] restore_mod_data (shared/mod_data_check.h) on a made-up mod folder in %TEMP%: the receipts' files and Setup's kept copies found
// there; D2RMM's wipe (the .mpq folder deleted whole, its own files and a modinfo.json of its own written - barti4033, 2026-11-17) found
// as such, the line naming D2RMM; Setup's files copied back (never over a file that is there); the receipts' parsing; modinfo's savepath.
//
//   mod_data_check_test   (exit code 0 = all passed)

#include <windows.h>

#include <cstdio>
#include <string>

#include "mod_data_check.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

static std::wstring g_mod;   // <scratch>\mods\D2R-VR\

static void Put(const std::wstring& rel, const std::string& text) {
    const std::wstring p = g_mod + rel;
    moddata::detail::MakeDirs(p.substr(0, p.find_last_of(L'\\')));
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD put = 0;
    WriteFile(f, text.data(), (DWORD)text.size(), &put, nullptr);
    CloseHandle(f);
}
static std::string Get(const std::wstring& rel) {
    std::string s;
    return moddata::detail::Read(g_mod + rel, &s) ? s : std::string("<none>");
}
static void Wipe(const std::wstring& dir) {   // a folder whole, as D2RMM does with mods\<name>\<name>.mpq
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) Wipe(dir + n + L"\\");
            else DeleteFileW((dir + n).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

int main() {
    // --- pure
    {
        const std::string r = "{\n  \"version\": 1,\n  \"game_build\":\"93847\",\n  \"files\": {\n"
                              "    \"mods/D2R-VR/D2R-VR.mpq/data/hd/env/model/d2r_vr/sky/sky_dome_act1_unit.json\":\"0123456789abcdef\",\n"
                              "    \"mods/D2R-VR/reshade/reshade-shaders/Textures/D2R_Arcane/stars.png\":\"fedcba9876543210\",\n"
                              "    \"mods/D2R-VR/D2R-VR.mpq/data/hd/env/model/Tests/pillar01_lod0.model\":\"00000000000000ff\"\n  }\n}\n";
        const std::vector<std::string> f = moddata::ReceiptFiles(r);
        CHECK(f.size() == 2);   // the .mpq's two; the picture beside it is not the .mpq's
        CHECK(f.size() == 2 && f[0] == "data/hd/env/model/d2r_vr/sky/sky_dome_act1_unit.json");
        CHECK(f.size() == 2 && f[1] == "data/hd/env/model/Tests/pillar01_lod0.model");
        CHECK(moddata::ReceiptFiles("").empty());
        CHECK(moddata::ReceiptFiles("{\"mods/D2R-VR/D2R-VR.mpq/broken").empty());   // cut short: nothing made up
        CHECK(moddata::ReceiptFiles("{\"made_from\":\"mods/D2R-VR/D2R-VR.mpq/x\"}").empty());   // a value, not a name: left out

        CHECK(moddata::ModInfoOurs("{ \"name\": \"D2R-VR\", \"savepath\": \"../\", \"description\": \"D2R VR Authentic\" }"));
        CHECK(!moddata::ModInfoOurs("{\"name\":\"D2R-VR\",\"savepath\":\"D2R-VR/\"}"));   // D2RMM's (barti4033's D2RLoader log)
        CHECK(!moddata::ModInfoOurs("{\"name\":\"D2R-VR\"}"));
        CHECK(moddata::SavePathOf("{\"name\":\"D2R-VR\",\"savepath\":\"D2R-VR/\"}") == "D2R-VR/");
        bool found = true;
        CHECK(moddata::SavePathOf("{}", &found).empty() && !found);
    }

    // --- a mod folder
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring root = std::wstring(tmp) + L"d2r_vr_mod_data_check_test\\";
    Wipe(root);
    g_mod = root + L"mods\\D2R-VR\\";
    moddata::detail::MakeDirs(g_mod.substr(0, g_mod.size() - 1));

    // not the mod's layout yet (no .mpq folder): nothing looked at, nothing restored
    {
        const moddata::Check c = moddata::Look(g_mod);
        CHECK(!c.mod && !c.Gone());
        CHECK(moddata::Line(c).find("not the mod's layout") != std::string::npos);
        CHECK(moddata::RestoreShipped(g_mod) == 0);
    }

    // as Setup and Settings leave it
    const std::string dds1 = "DDS act1", dds3 = "DDS act3";
    Put(L"D2R-VR.mpq\\modinfo.json", "{ \"name\": \"D2R-VR\", \"savepath\": \"../\", \"description\": \"D2R VR Authentic\" }\r\n");
    Put(L"D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\sky_act1_day_HDR.dds", dds1);
    Put(L"D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\sky_act3_day_HDR.dds", dds3);
    Put(L"shipped_data\\data\\hd\\env\\texture\\d2r_vr\\sky\\sky_act1_day_HDR.dds", dds1);
    Put(L"shipped_data\\data\\hd\\env\\texture\\d2r_vr\\sky\\sky_act3_day_HDR.dds", dds3);
    Put(L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\sky\\sky_dome_act3_unit.json", "{unit}");
    Put(L"D2R-VR.mpq\\data\\hd\\env\\model\\Tests\\Pillar_02_lod0.model", "model");
    Put(L"D2R-VR.mpq\\data\\hd\\env\\preset\\act3\\docktown\\docktown3.json", "boat");
    Put(L"sky_dome.json", "{\"version\": 1,\"game_build\":\"1\",\"files\": {\n"
                          "\"mods/D2R-VR/D2R-VR.mpq/data/hd/env/model/d2r_vr/sky/sky_dome_act3_unit.json\":\"1\",\n"
                          "\"mods/D2R-VR/D2R-VR.mpq/data/hd/env/model/Tests/Pillar_02_lod0.model\":\"2\"}}");
    Put(L"kurast_boat.json", "{\"files\": {\"mods/D2R-VR/D2R-VR.mpq/data/hd/env/preset/act3/docktown/docktown3.json\":\"3\"}}");
    Put(L"reshade\\reshade-shaders\\Textures\\D2R_Arcane\\made.json",
        "{\"files\": {\"mods/D2R-VR/reshade/reshade-shaders/Textures/D2R_Arcane/stars.png\":\"4\"}}");   // (beside the .mpq: not looked at)
    {
        const moddata::Check c = moddata::Look(g_mod);
        CHECK(c.mod && !c.Gone());
        CHECK(c.named == 3 && c.shipped == 2);
        CHECK(c.modinfo && c.modinfoOurs && c.savepath == "../");
        const std::string line = moddata::Line(c);
        CHECK(line.find("all there (3 made from the game by Settings, 2 of Setup's)") != std::string::npos);
        CHECK(line.find("D2RMM") == std::string::npos);
        CHECK(moddata::RestoreShipped(g_mod) == 0);   // nothing to put back
    }

    // D2RMM with its output named D2R-VR builds: the .mpq deleted whole, its files and its modinfo.json written
    Wipe(g_mod + L"D2R-VR.mpq\\");
    Put(L"D2R-VR.mpq\\modinfo.json", "{\"name\":\"D2R-VR\",\"savepath\":\"D2R-VR/\"}");
    Put(L"D2R-VR.mpq\\data\\global\\excel\\cubemain.txt", "D2RMM's");
    {
        const moddata::Check c = moddata::Look(g_mod);
        CHECK(c.mod && c.Gone());
        CHECK(c.named == 3 && c.missing.size() == 3);
        CHECK(c.shipped == 2 && c.shippedMissing.size() == 2);
        CHECK(c.modinfo && !c.modinfoOurs && c.savepath == "D2R-VR/");
        const std::string line = moddata::Line(c);
        CHECK(line.find("GONE: 3 of the 3 Settings made from the game, 2 of Setup's 2") != std::string::npos);
        CHECK(line.find("savepath \"D2R-VR/\"") != std::string::npos && line.find("D2RMM") != std::string::npos);
    }

    // a file the player (or D2RMM) put where one of Setup's goes stays as it is; the others come back byte for byte
    Put(L"D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\sky_act3_day_HDR.dds", "theirs");
    {
        std::vector<std::string> back;
        int failed = -1;
        CHECK(moddata::RestoreShipped(g_mod, &back, &failed) == 1);
        CHECK(failed == 0 && back.size() == 1 && back[0] == "data/hd/env/texture/d2r_vr/sky/sky_act1_day_HDR.dds");
        CHECK(Get(L"D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\sky_act1_day_HDR.dds") == dds1);
        CHECK(Get(L"D2R-VR.mpq\\data\\hd\\env\\texture\\d2r_vr\\sky\\sky_act3_day_HDR.dds") == "theirs");
        CHECK(Get(L"D2R-VR.mpq\\data\\global\\excel\\cubemain.txt") == "D2RMM's");   // D2RMM's own files untouched
        CHECK(Get(L"D2R-VR.mpq\\modinfo.json") == "{\"name\":\"D2R-VR\",\"savepath\":\"D2R-VR/\"}");   // never rewritten
        const moddata::Check c = moddata::Look(g_mod);
        CHECK(c.shippedMissing.empty() && c.missing.size() == 3);   // the made ones are Settings' makers' to make again
    }

    // an empty file is as good as none (a maker's write cut short)
    Put(L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\sky\\sky_dome_act3_unit.json", "");
    CHECK(moddata::Look(g_mod).missing.size() == 3);
    Put(L"D2R-VR.mpq\\data\\hd\\env\\model\\d2r_vr\\sky\\sky_dome_act3_unit.json", "{unit}");
    CHECK(moddata::Look(g_mod).missing.size() == 2);

    Wipe(root);
    std::printf("mod_data_check_test: %d of %d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
