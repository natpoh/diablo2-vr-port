// menu_assets_test - [openxr] skill_panel_game_look: D2R_VR_Settings' maker of the game's own interface art beside the plugin
// (tools/settings/menu_assets.cpp, shared/game_ui_files.h), against the game's archives (read only), writing under a scratch folder.
//   - off: nothing read, nothing written;
//   - no plugin folder: nothing made, and said so;
//   - on: every piece in <scratch>\d2r_vr_game_ui, each sprite a sheet as the plugin reads it (vr/game_ui_sprite.h) with the frames the painter
//     counts on (the gold corners 9, the check box 7, the buttons and arrows 4, the tabs 2 or more), the two fonts there;
//   - again: nothing written; a piece gone: made again, only that one;
//   - nothing else written in the scratch.
//   menu_assets_test <game folder> <empty scratch folder>

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "game_ui_files.h"
#include "game_ui_sprite.h"
#include "menu_assets.h"

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

static int CountFiles(const std::wstring& dir) {
    int n = 0;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file()) ++n;
    return n;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        printf("menu_assets_test <game folder> <empty scratch folder>\n");
        return 2;
    }
    std::wstring game = argv[1], scratch = argv[2];
    if (game.back() != L'\\') game += L'\\';
    if (scratch.back() != L'\\') scratch += L'\\';
    CreateDirectoryW(scratch.c_str(), nullptr);
    Expect(CountFiles(scratch) == 0, "the scratch folder is empty");

    menuassets::Result r = menuassets::Prepare(game, scratch, false);
    Expect(r.ok && !r.wrote && CountFiles(scratch) == 0, "off: nothing read, nothing written");
    r = menuassets::Prepare(game, scratch + L"no such folder\\", true);
    Expect(!r.ok && !r.wrote && !r.text.empty() && CountFiles(scratch) == 0, "no plugin folder: nothing made, and said so");

    const ULONGLONG t0 = GetTickCount64();
    r = menuassets::Prepare(game, scratch, true);
    printf("made: %ls (%llu ms)\n", r.text.c_str(), GetTickCount64() - t0);
    Expect(r.ok && r.wrote, "on: made");
    const std::wstring dir = scratch + gameuifiles::kFolder + L"\\";
    Expect(CountFiles(scratch) == gameuifiles::kFileCount, "every piece and nothing else");
    const int want[gameuifiles::kSpriteCount] = {1, 9, 4, 2, 4, 7, 4, 4, 1, 1, 1, 1, 5, 4};
    size_t bytes = 0;
    for (int i = 0; i < gameuifiles::kFileCount; ++i) {
        const std::string data = ReadAll(dir + gameuifiles::kFiles[i].leaf);
        bytes += data.size();
        if (i >= gameuifiles::kSpriteCount) {
            Expect(data.size() > 1000, "a font");
            continue;
        }
        gameui::Sheet s;
        const char* why = "";
        const bool ok = gameui::Decode(data, &s, &why);
        printf("  %-40ls %s %d x %d, %d frame(s)%s%s\n", gameuifiles::kFiles[i].leaf, ok ? "a sheet" : "NOT a sheet", s.frameWidth, s.height, s.frames, ok ? "" : ": ", ok ? "" : why);
        Expect(ok && s.frames == want[i], "a sheet with the frames the painter counts on");
    }
    printf("  %d files, %.1f MB\n", gameuifiles::kFileCount, bytes / 1048576.0);

    r = menuassets::Prepare(game, scratch, true);
    Expect(r.ok && !r.wrote, "again: nothing to do");
    DeleteFileW((dir + gameuifiles::kFiles[5].leaf).c_str());
    r = menuassets::Prepare(game, scratch, true);
    Expect(r.ok && r.wrote && CountFiles(scratch) == gameuifiles::kFileCount, "a piece gone: made again");

    printf(g_fail ? "%d failed\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
