// keyboard_game_look_test.cpp - [openxr] keyboard_game_look (vr/keyboard_game_look.cpp): the VR keyboard's four pictures painted from the
// game's own art. Here from made-up sheets (no game needed): no art - nothing painted; with it - every key solid, the lit pictures
// brighter, the waiting Shift the "chosen" button. With two arguments (the cache folder, a file) it paints from the real art and
// writes the atlas as raw RGBA, to look at: keyboard_game_look_test.exe <d2r_vr_game_ui> <out.rgba>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "game_ui.h"
#include "game_ui_files.h"
#include "keyboard_game_look.h"
#include "vr_keyboard_unleashed.h"

namespace kb = framevr::keyboard;

static void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// A sheet as the game's own are ('SpA1', version 31, 4 components): `frames` frames side by side, each one flat grey of its own shade.
static std::string MadeSheet(int frames, int frameWidth, int height, const int* shades) {
    const uint32_t width = uint32_t(frames * frameWidth), size = width * uint32_t(height) * 4;
    std::string d(40 + size, char(0));
    std::memcpy(&d[0], "SpA1", 4);
    const uint16_t version = 31, fw = uint16_t(frameWidth);
    const uint32_t h = uint32_t(height), count = uint32_t(frames), components = 4;
    std::memcpy(&d[4], &version, 2); std::memcpy(&d[6], &fw, 2);
    std::memcpy(&d[8], &width, 4); std::memcpy(&d[12], &h, 4);
    std::memcpy(&d[20], &count, 4); std::memcpy(&d[32], &size, 4); std::memcpy(&d[36], &components, 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            unsigned char* p = reinterpret_cast<unsigned char*>(&d[40 + (size_t(y) * width + x) * 4]);
            p[0] = p[1] = p[2] = (unsigned char)shades[x / uint32_t(frameWidth)];
            p[3] = 255;
        }
    return d;
}

static void WriteFile(const std::filesystem::path& p, const std::string& bytes) {
    std::ofstream out(p, std::ios::binary);
    out.write(bytes.data(), (std::streamsize)bytes.size());
}

static const uint8_t* At(const std::vector<uint8_t>& atlas, int picture, int x, int y) {
    return &atlas[(size_t(picture) * kb::TexelHeight + y) * kb::TexelWidth * 4 + size_t(x) * 4];
}

int main(int argc, char** argv) {
    if (argc == 3) {   // the real art: the picture written out to look at
        gameui::SetDir(std::filesystem::path(argv[1]).wstring());
        const std::vector<uint8_t> atlas = kblook::PaintAtlas();
        if (atlas.empty()) { std::fprintf(stderr, "nothing painted: the game's art is not in %s\n", argv[1]); return 1; }
        WriteFile(argv[2], std::string(atlas.begin(), atlas.end()));
        std::printf("%d x %d RGBA (premultiplied) written to %s\n", kb::TexelWidth, kb::TexelHeight * 4, argv[2]);
        return 0;
    }
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::filesystem::path dir = std::filesystem::path(temp) / (L"d2r_vr_keyboard_look_test_" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(dir);
    gameui::SetDir(dir.wstring());
    Check(kblook::PaintAtlas().empty(), "no art beside the plugin: nothing painted (the keyboard keeps its own look)");

    const int option[5] = {60, 140, 30, 140, 60}, chosen[4] = {100, 200, 30, 200}, slab[1] = {20};
    WriteFile(dir / gameuifiles::kFiles[(int)gameui::Sprite::Option].leaf, MadeSheet(5, 248, 90, option));
    WriteFile(dir / gameuifiles::kFiles[(int)gameui::Sprite::OptionChosen].leaf, MadeSheet(4, 248, 90, chosen));
    WriteFile(dir / gameuifiles::kFiles[(int)gameui::Sprite::Slab].leaf, MadeSheet(1, 1245, 453, slab));
    gameui::SetDir(dir.wstring());   // (asked anew)
    const std::vector<uint8_t> atlas = kblook::PaintAtlas();
    Check(atlas.size() == size_t(kb::TexelWidth) * kb::TexelHeight * 4 * 4, "four pictures, each the keyboard's size");
    int shift = -1;
    for (int i = 0; i < kb::KeyCount; ++i) {
        if (kb::Keys[i].action == kb::Action::Shift) shift = i;
        const kb::Rect r = kb::KeyTexelRect(i);
        const int x = r.x + 3, y = r.y + 3;   // the key's corner: clear of its label
        for (int p = 0; p < 4; ++p) Check(At(atlas, p, x, y)[3] == 255, "every key solid in every picture");
        Check(At(atlas, 1, x, y)[0] > At(atlas, 0, x, y)[0] && At(atlas, 3, x, y)[0] > At(atlas, 2, x, y)[0], "the lit pictures' keys brighter (the button's lit frame)");
        if (kb::Keys[i].action != kb::Action::Shift) Check(At(atlas, 2, x, y)[0] == At(atlas, 0, x, y)[0], "with capitals the other keys' faces stay");
        Check(At(atlas, 0, x, y)[0] == 60 && At(atlas, 1, x, y)[0] == 140, "the key is the option button's frame, plain and lit");
    }
    Check(shift >= 0, "a Shift key");
    {
        const kb::Rect r = kb::KeyTexelRect(shift);
        Check(At(atlas, 2, r.x + 3, r.y + 3)[0] == 100 && At(atlas, 3, r.x + 3, r.y + 3)[0] == 200, "the waiting Shift is the chosen button");
    }
    {   // the ground between the keys: the slab's dark stone, nearly solid
        const uint8_t* g = At(atlas, 0, 1, 1);
        Check(g[3] > 200 && g[3] < 255 && g[0] <= 20, "between the keys the slab's dark stone, not quite solid");
    }
    {   // a label is on its key: somewhere inside it the letters' gold (redder than the grey key)
        const kb::Rect r = kb::KeyTexelRect(0);
        bool gold = false;
        for (int y = r.y; y < r.y + r.h && !gold; ++y)
            for (int x = r.x; x < r.x + r.w && !gold; ++x) {
                const uint8_t* p = At(atlas, 0, x, y);
                gold = p[0] > p[2] + 40 && p[0] > 120;
            }
        Check(gold, "the key's label in the game's gold");
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::puts("VR keyboard game look passed");
    return 0;
}
