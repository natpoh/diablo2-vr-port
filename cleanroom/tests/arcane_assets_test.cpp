// arcane_assets_test - D2R_VR_Settings' Arcane sky maker (tools/settings/arcane_assets.cpp) against the game's own archives,
// read only, writing into a folder of the test's own - never the game's. The layout is the mod's (the game's archives are read from the
// game folder given, everything is written under the scratch folder):
//   <scratch>\mods\D2R-VR\reshade   = ReShade's folder in the mod layout (Prepare's reshadeFolder)
//   <scratch>\mods\D2R-VR           = the mod's folder (Prepare's modRoot), with D2R-VR.mpq once Setup's WriteModInfo made it
//   - no reshade-shaders\Textures in ReShade's folder (or no folder): nothing made, and said so;
//   - with one: stars.png (1024 x 512) and clouds.png (512 x 512) made and read back by Windows (WIC), the receipt beside them;
//     with releases/art/bear_arcane at hand, stars.png = an independent decoder's picture of the three layers, texel for texel;
//   - again: nothing written (up to date);
//   - not the mod (modRoot empty), or the mod without its D2R-VR.mpq: the game's flat star cards stay, nothing written in the mod's data folder;
//   - with D2R-VR.mpq: the seven visual files, each with its stars taken out;
//   - a visual file the player changed: left as it is, and no longer in the receipt;
//   - everything went into mods\D2R-VR (reshade\ and D2R-VR.mpq\) and nowhere else in the scratch;
//   - an install that is not the mod (ReShade in the game's own folder, modRoot empty): the pictures in <that folder>\reshade-shaders.
//   arcane_assets_test <game folder> <empty scratch folder> [<releases/art/bear_arcane>]

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "arcane_assets.h"
#include "arcane_sky.h"

using Microsoft::WRL::ComPtr;

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

static bool WicDecode(const std::string& png, int* w, int* h, std::vector<uint8_t>* rgba) {
    ComPtr<IWICImagingFactory> f;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    ComPtr<IWICStream> s;
    if (FAILED(f->CreateStream(&s)) || FAILED(s->InitializeFromMemory((BYTE*)png.data(), (DWORD)png.size()))) return false;
    ComPtr<IWICBitmapDecoder> d;
    if (FAILED(f->CreateDecoderFromStream(s.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &d))) return false;
    ComPtr<IWICBitmapFrameDecode> fr;
    if (FAILED(d->GetFrame(0, &fr))) return false;
    ComPtr<IWICFormatConverter> c;
    if (FAILED(f->CreateFormatConverter(&c)) ||
        FAILED(c->Initialize(fr.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return false;
    UINT uw = 0, uh = 0;
    c->GetSize(&uw, &uh);
    *w = (int)uw;
    *h = (int)uh;
    rgba->resize((size_t)uw * uh * 4);
    return SUCCEEDED(c->CopyPixels(nullptr, uw * 4, (UINT)rgba->size(), rgba->data()));
}

static std::wstring Slash(std::wstring p) {
    for (wchar_t& c : p) if (c == L'/') c = L'\\';
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    return p;
}

static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { printf("arcane_assets_test <game folder> <empty scratch folder> [<releases/art/bear_arcane>]\n"); return 2; }
    const std::wstring game = Slash(argv[1]), into = Slash(argv[2]), art = argc > 3 ? Slash(argv[3]) : L"";
    if (_wcsnicmp(game.c_str(), into.c_str(), game.size()) == 0) { printf("the scratch folder must not be in the game's\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CreateDirectoryW(into.c_str(), nullptr);

    const std::wstring modRoot = into + L"mods\\D2R-VR\\";
    const std::wstring rs = modRoot + L"reshade\\";                     // ReShade's folder in the mod layout
    CreateDirectoryW((into + L"mods").c_str(), nullptr);
    CreateDirectoryW(modRoot.c_str(), nullptr);
    CreateDirectoryW(rs.c_str(), nullptr);

    arcane::Result r = arcane::Prepare(game, rs, modRoot);
    Expect(r.state == arcane::kNotHere && !r.wrote, "no reshade-shaders\\Textures in the mod's reshade folder: nothing made");
    printf("  %ls\n", r.text.c_str());
    Expect(arcane::Prepare(game, L"", modRoot).state == arcane::kNotHere, "no ReShade folder at all: nothing made");

    CreateDirectoryW((rs + L"reshade-shaders").c_str(), nullptr);
    CreateDirectoryW((rs + L"reshade-shaders\\Textures").c_str(), nullptr);
    const ULONGLONG t0 = GetTickCount64();
    r = arcane::Prepare(game, rs, L"");                                  // D2R VR not installed as the mod: no modRoot
    const ULONGLONG ms = GetTickCount64() - t0;
    Expect(r.state == arcane::kMade && r.wrote, "with ReShade's folder: made");
    printf("  %ls (%llu ms)\n", r.text.c_str(), (unsigned long long)ms);
    Expect(r.text.find(L"reshade-shaders\\Textures\\D2R_Arcane") != std::wstring::npos && r.text.find(L"flat star cards stay") != std::wstring::npos,
           "... told by the folder it went to, and that the flat star cards stay (no mod)");
    const std::wstring dir = rs + L"reshade-shaders\\Textures\\D2R_Arcane\\";
    int w = 0, h = 0;
    std::vector<uint8_t> stars, clouds;
    Expect(WicDecode(ReadAll(dir + L"stars.png"), &w, &h, &stars) && w == 1024 && h == 512, "stars.png: 1024 x 512, Windows reads it");
    Expect(WicDecode(ReadAll(dir + L"clouds.png"), &w, &h, &clouds) && w == 512 && h == 512, "clouds.png: 512 x 512, Windows reads it");
    bool grey = clouds.size() == 512u * 512 * 4, opaque = true;
    for (size_t i = 0; grey && i < clouds.size(); i += 4) grey = clouds[i] == clouds[i + 1] && clouds[i] == clouds[i + 2] && clouds[i + 3] == 255;
    for (size_t i = 3; i < stars.size(); i += 4) opaque = opaque && stars[i] == 255;
    Expect(grey && opaque, "the clouds grey, both pictures opaque");
    const std::string receipt = ReadAll(dir + L"made.json");
    Expect(!arcanesky::ReceiptValue(receipt, "reshade-shaders/Textures/D2R_Arcane/stars.png").empty() &&
               !arcanesky::ReceiptValue(receipt, "reshade-shaders/Textures/D2R_Arcane/clouds.png").empty(),
           "the receipt names both pictures");
    printf("  the game's build in the receipt: %s\n", arcanesky::ReceiptValue(receipt, "game_build").c_str());
    if (!art.empty()) {
        std::vector<uint8_t> ref;
        if (WicDecode(ReadAll(art + L"3_bear_stars_png_far_mid_near_as_rgb.png"), &w, &h, &ref) && w == 1024 && h == 512) {
            Expect(ref == stars, "stars.png = an independent decoder's picture of far / mid / near as red / green / blue, texel for texel");
            printf("  stars.png compared with the independent decoder's picture\n");
        }
    }

    r = arcane::Prepare(game, rs, L"");
    Expect(r.state == arcane::kMade && !r.wrote, "again: up to date, nothing written");

    // the mod, but without its D2R-VR.mpq (Setup's WriteModInfo makes it): the flat star cards stay, nothing written in the mod's data folder
    const std::wstring mpq = modRoot + L"D2R-VR.mpq";
    r = arcane::Prepare(game, rs, modRoot);
    Expect(r.state == arcane::kMade && !r.wrote && !Exists(mpq), "the mod without D2R-VR.mpq: nothing written there");

    CreateDirectoryW(mpq.c_str(), nullptr);
    r = arcane::Prepare(game, rs, modRoot);
    Expect(r.state == arcane::kMade && r.wrote, "with the mod's D2R-VR.mpq: the visual files written");
    printf("  %ls\n", r.text.c_str());
    int stripped = 0;
    for (const char* p : arcanesky::kPeriods) {
        const std::string name = std::string("data\\hd\\env\\vis\\act2_arcane_") + p + ".json";
        const std::string j = ReadAll(mpq + L"\\" + std::wstring(name.begin(), name.end()));
        if (j.find("\"screenspaceVfx\":\"\"") != std::string::npos && j.find("\"VisualDataDefinition\"") != std::string::npos) ++stripped;
    }
    Expect(stripped == 7, "seven visual files, each with its stars taken out");

    const std::wstring day = mpq + L"\\data\\hd\\env\\vis\\act2_arcane_day.json";
    {
        std::ofstream f(day, std::ios::binary | std::ios::trunc);
        f << "{\"the player's own\":1}";
    }
    r = arcane::Prepare(game, rs, modRoot);
    Expect(r.state == arcane::kMade && ReadAll(day) == "{\"the player's own\":1}", "a visual file the player changed: left as it is");
    Expect(r.text.find(L"left as they are") != std::wstring::npos, "... and said so");
    Expect(arcanesky::ReceiptValue(ReadAll(dir + L"made.json"), "mods/D2R-VR/D2R-VR.mpq/data/hd/env/vis/act2_arcane_day.json").empty() &&
               !arcanesky::ReceiptValue(ReadAll(dir + L"made.json"), "mods/D2R-VR/D2R-VR.mpq/data/hd/env/vis/act2_arcane_night.json").empty(),
           "... and no longer ours in the receipt (the others still are)");
    printf("  %ls\n", r.text.c_str());

    // everything went into mods\D2R-VR (reshade\ and D2R-VR.mpq\) and nowhere else in the scratch
    Expect(!Exists(into + L"reshade-shaders") && !Exists(into + L"data") && !Exists(modRoot + L"reshade-shaders") && !Exists(modRoot + L"data"),
           "nothing written outside mods\\D2R-VR\\reshade and mods\\D2R-VR\\D2R-VR.mpq");

    // an install that is not the mod: ReShade in the game's own folder (here a folder of the test's own), no modRoot - the pictures there under
    // reshade-shaders\Textures, the flat star cards stay, no mod folder is made
    const std::wstring plain = into + L"plain_game\\";
    CreateDirectoryW(plain.c_str(), nullptr);
    CreateDirectoryW((plain + L"reshade-shaders").c_str(), nullptr);
    CreateDirectoryW((plain + L"reshade-shaders\\Textures").c_str(), nullptr);
    r = arcane::Prepare(game, plain, L"");
    Expect(r.state == arcane::kMade && r.wrote && Exists(plain + L"reshade-shaders\\Textures\\D2R_Arcane\\stars.png") && !Exists(plain + L"mods"),
           "not the mod: the pictures in the game's own ReShade folder, no mod folder made");

    if (g_fail) { printf("%d FAILED\n", g_fail); return 1; }
    printf("arcane assets: all checks passed\n");
    return 0;
}
