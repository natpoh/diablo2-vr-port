// ally_portraits_paint.cpp - the allies' portraits' picture (vr/ally_portraits_paint.h). Our own drawing with the paint kit; the portraits are the copies
// D2R_VR_Settings made out of the player's own game (shared/ally_portrait_files.h), never shipped.

#include "ally_portraits_paint.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

#include "ally_portrait_files.h"
#include "game_ui_files.h"
#include "game_ui_sprite.h"
#include "paint_kit.h"

namespace allyportraitspaint {
namespace {

using wheelpaint::kit::Canvas;
using wheelpaint::kit::Col;
using wheelpaint::kit::Fonts;
using wheelpaint::kit::IconTone;
using wheel::Rect;
using namespace allyportraits;

// The game's own colours: $FontColorGold for a player's name, $FontColorVeryLightGray for the rest; the health bar's green, yellow and red.
constexpr Col kGold{199.0f, 179.0f, 119.0f}, kLightGray{210.0f, 210.0f, 210.0f}, kRim{120.0f, 100.0f, 62.0f}, kRimNpc{96.0f, 92.0f, 86.0f};
constexpr Col kGreen{38.0f, 168.0f, 44.0f}, kYellow{214.0f, 186.0f, 40.0f}, kRed{206.0f, 40.0f, 30.0f}, kBack{14.0f, 12.0f, 11.0f}, kText{240.0f, 232.0f, 214.0f};

std::mutex g_lock;
std::wstring g_dir;
bool g_tried = false;
int g_found = 0;
std::shared_ptr<const wheelicons::Image> g_art[kIconCount];

static_assert(allyportraitfiles::kCount == kIconCount - 1, "a file for every portrait of vr/ally_portraits.h Icon (but None)");

std::wstring DefaultDir() {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"";
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&DefaultDir), &module);
    GetModuleFileNameW(module, path, MAX_PATH);
    return (std::filesystem::path(path).parent_path() / gameuifiles::kFolder).wstring();
}

// under g_lock: every portrait that is there, each on its own
void LoadLocked() {
    if (g_tried) return;
    g_tried = true;
    if (g_dir.empty()) g_dir = DefaultDir();
    for (int i = 0; i < allyportraitfiles::kCount; ++i) {
        const std::filesystem::path file = std::filesystem::path(g_dir) / (std::string(allyportraitfiles::kPrefix) + allyportraitfiles::kLeaves[i] + ".sprite");
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        std::ostringstream s;
        s << in.rdbuf();
        gameui::Sheet sheet;
        if (!gameui::Decode(s.str(), &sheet) || !sheet.Ok()) continue;
        auto img = std::make_shared<wheelicons::Image>();
        img->width = (unsigned)sheet.frameWidth;
        img->height = (unsigned)sheet.height;
        img->rgba.resize((size_t)img->width * img->height * 4);
        for (int y = 0; y < sheet.height; ++y)
            std::memcpy(&img->rgba[(size_t)y * img->width * 4], sheet.At(0, 0, y), (size_t)img->width * 4);
        g_art[i + 1] = img;
        ++g_found;
    }
}

std::shared_ptr<const wheelicons::Image> Art(Icon icon) {
    std::lock_guard<std::mutex> lock(g_lock);
    LoadLocked();
    const int i = (int)icon;
    return i > 0 && i < kIconCount ? g_art[i] : nullptr;
}

// The initials standing in for a portrait that is not there: the first letters of the name's words (up to two), or its first two letters.
void Initials(const char* name, char out[4]) {
    int n = 0;
    bool start = true;
    for (const char* p = name; p && *p && n < 2; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (std::isalpha(c)) {
            if (start) out[n++] = (char)std::toupper(c);
            start = false;
        } else {
            start = c < 0x80;   // (a letter of another alphabet: not a word's start here)
        }
    }
    if (n == 1 && name[1] && std::isalpha((unsigned char)name[1])) out[n++] = (char)std::tolower((unsigned char)name[1]);
    out[n] = 0;
}

Col ToneCol(Tone t) {
    switch (t) {
        case Tone::Green: return kGreen;
        case Tone::Yellow: return kYellow;
        case Tone::Red: return kRed;
        default: return kBack;
    }
}

void DrawTile(Canvas& cv, Fonts& fonts, const Tile& t, float x, float y) {
    const bool player = t.kind == Kind::Player;
    wheelpaint::kit::RoundRect(cv, Rect{x + 4.0f, y + 4.0f, x + 156.0f, y + 156.0f}, 14.0f, kBack, 0.86f, player ? kRim : kRimNpc, 0.95f, 2.5f);
    if (t.kind == Kind::More) {
        char more[12];
        snprintf(more, sizeof more, "+%u", (unsigned)std::min<uint16_t>(t.count, 99));
        wheelpaint::kit::Text(cv, fonts, more, Rect{x + 8.0f, y + 40.0f, x + 152.0f, y + 120.0f}, 46, true, kText);
        return;
    }
    // the life bar over the portrait, as the game's (HireablesPanelHD.json: Health over the picture)
    const Tone tone = LifeTone(t.life);
    const Rect bar{x + 18.0f, y + 12.0f, x + 142.0f, y + 26.0f};
    wheelpaint::kit::RoundRect(cv, bar, 3.0f, Col{4.0f, 4.0f, 4.0f}, 0.95f, Col{70.0f, 62.0f, 50.0f}, 0.9f, 1.5f);
    const float fill = LifeFill(t.life);
    if (fill > 0.0f) wheelpaint::kit::RoundRect(cv, Rect{bar.l + 2.0f, bar.t + 2.0f, bar.l + 2.0f + (bar.r - bar.l - 4.0f) * fill, bar.b - 2.0f}, 2.0f, ToneCol(tone), 1.0f, kBack, 0.0f, 0.0f);
    // the portrait (or the initials)
    const Rect pic{x + 30.0f, y + 30.0f, x + 130.0f, y + 130.0f};
    const bool dead = tone == Tone::Dead;
    if (const auto art = Art(t.icon)) {
        wheelpaint::kit::Icon(cv, art, pic, dead ? IconTone::Dim : IconTone::Normal);
    } else {
        char ini[4] = {};
        Initials(t.name, ini);
        wheelpaint::kit::RoundRect(cv, pic, 8.0f, Col{40.0f, 34.0f, 28.0f}, 0.95f, player ? kGold : kLightGray, 0.6f, 1.5f);
        wheelpaint::kit::Text(cv, fonts, ini[0] ? ini : "?", pic, 44, true, dead ? Col{120.0f, 120.0f, 120.0f} : player ? kGold : kLightGray);
    }
    if (t.count > 1) {   // a group's counter (the game's Counter: on the picture)
        const Rect c{x + 92.0f, y + 96.0f, x + 132.0f, y + 130.0f};
        wheelpaint::kit::RoundRect(cv, c, 9.0f, Col{10.0f, 8.0f, 8.0f}, 0.9f, kRimNpc, 0.9f, 1.5f);
        char num[8];
        snprintf(num, sizeof num, "%u", (unsigned)std::min<uint16_t>(t.count, 99));
        wheelpaint::kit::Text(cv, fonts, num, c, 24, true, kText);
    }
    if (player && t.level > 0) {   // a player's level, top left of the picture
        const Rect c{x + 26.0f, y + 30.0f, x + 70.0f, y + 58.0f};
        wheelpaint::kit::RoundRect(cv, c, 8.0f, Col{10.0f, 8.0f, 8.0f}, 0.88f, kRim, 0.9f, 1.5f);
        char lv[8];
        snprintf(lv, sizeof lv, "%u", (unsigned)std::min<uint16_t>(t.level, 999));
        wheelpaint::kit::Text(cv, fonts, lv, c, 20, true, kGold);
    }
    wheelpaint::kit::Text(cv, fonts, t.name, Rect{x + 8.0f, y + 130.0f, x + 152.0f, y + 154.0f}, 19, true, player ? kGold : kLightGray);
}

}  // namespace

void Render(const Look& look, std::vector<uint32_t>& px) {
    const int w = PicW(look.cols), h = PicH(look.n, look.cols);
    px.assign((size_t)w * h, 0u);
    Canvas cv{px, w, h};
    Fonts fonts;
    for (int i = 0; i < (int)look.n && i < kMaxTiles; ++i) {
        const Cell c = CellOf(i, look.n, look.cols);
        DrawTile(cv, fonts, look.tile[(size_t)i], (float)c.x, (float)c.y);
    }
}

int PortraitsFound() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_found;
}

void SetDir(const std::wstring& dir) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_dir = dir;
    g_tried = false;
    g_found = 0;
    for (auto& a : g_art) a.reset();
}

}  // namespace allyportraitspaint
