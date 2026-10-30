// game_ui.cpp - the game look's cache reader (vr/game_ui.h).

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#include "game_ui.h"
#include "game_ui_files.h"

namespace gameui {
namespace {

std::atomic<bool> g_on{false};
std::mutex g_lock;
std::wstring g_dir;
bool g_tried = false, g_ready = false;
Sheet g_sheets[(int)Sprite::Count];
const Sheet g_none;
const wchar_t* g_title = nullptr;
const wchar_t* g_text = nullptr;
std::atomic<LogFn> g_log{nullptr};
std::map<std::wstring, std::wstring> g_fontsIn;   // file -> family, taken into the process (kept for its life: GDI holds them)

static_assert((int)Sprite::Count == gameuifiles::kSpriteCount, "a sprite for every file of shared/game_ui_files.h");

std::wstring DefaultDir() {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"";
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&DefaultDir), &module);
    GetModuleFileNameW(module, path, MAX_PATH);
    return (std::filesystem::path(path).parent_path() / gameuifiles::kFolder).wstring();
}

bool ReadAll(const std::filesystem::path& p, std::string* out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream s;
    s << in.rdbuf();
    *out = s.str();
    return true;
}

// The face GDI really gives for `family` (a name it does not know is quietly another font).
bool FaceIs(const wchar_t* family) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    HFONT f = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, family);
    HGDIOBJ old = SelectObject(dc, f);
    wchar_t got[LF_FACESIZE] = L"";
    GetTextFaceW(dc, LF_FACESIZE, got);
    SelectObject(dc, old);
    DeleteObject(f);
    DeleteDC(dc);
    return _wcsicmp(got, family) == 0;
}

const wchar_t* TakeFont(const std::filesystem::path& file, const wchar_t* family) {
    if (FaceIs(family)) return family;   // (already in: installed, or taken in before)
    if (!std::filesystem::exists(file) || AddFontResourceExW(file.c_str(), FR_PRIVATE, nullptr) == 0) return nullptr;
    g_fontsIn[file.wstring()] = family;
    return FaceIs(family) ? family : nullptr;
}

// under g_lock
void LoadLocked() {
    if (g_tried) return;
    g_tried = true;
    if (g_dir.empty()) g_dir = DefaultDir();
    const std::filesystem::path dir(g_dir);
    std::string missing;
    for (int i = 0; i < gameuifiles::kSpriteCount; ++i) {
        std::string bytes;
        const char* why = "not there";
        if (!ReadAll(dir / gameuifiles::kFiles[i].leaf, &bytes) || !Decode(bytes, &g_sheets[i], &why)) {
            char line[160];
            std::snprintf(line, sizeof line, "%s%ls (%s)", missing.empty() ? "" : ", ", gameuifiles::kFiles[i].leaf, why);
            missing += line;
            g_sheets[i] = Sheet{};
        }
    }
    if (!missing.empty()) {
        for (Sheet& s : g_sheets) s = Sheet{};
        const std::string msg = "vrcam: skill panel - the game look needs the game's art beside the plugin (" + std::string(std::filesystem::path(g_dir).filename().string()) +
                                ", made by D2R VR Authentic Settings from your game); missing: " + missing + " - the panel keeps its own look";
        if (LogFn log = g_log.load()) log(msg.c_str());
        return;
    }
    g_title = TakeFont(dir / gameuifiles::kFiles[gameuifiles::kSpriteCount].leaf, L"Exocet Blizzard Mixed Caps");
    g_text = TakeFont(dir / gameuifiles::kFiles[gameuifiles::kSpriteCount + 1].leaf, L"Formal436 BT");
    g_ready = true;
    char line[200];
    std::snprintf(line, sizeof line, "vrcam: skill panel - the game look: %d pieces of the game's art read; its fonts %s / %s", gameuifiles::kSpriteCount,
                  g_title ? "Exocet" : "(ours)", g_text ? "Formal436" : "(ours)");
    if (LogFn log = g_log.load()) log(line);
}

}  // namespace

void SetLog(LogFn fn) { g_log.store(fn); }
void SetOn(bool on) { g_on.store(on); }
bool On() { return g_on.load(); }

bool Ready() {
    if (!g_on.load()) return false;
    std::lock_guard<std::mutex> lock(g_lock);
    LoadLocked();
    return g_ready;
}

bool Present() {
    if (!g_on.load()) return false;
    std::wstring dir;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_dir.empty()) g_dir = DefaultDir();
        if (g_tried) return g_ready;
        dir = g_dir;
    }
    for (int i = 0; i < gameuifiles::kFileCount; ++i)
        if (GetFileAttributesW((std::filesystem::path(dir) / gameuifiles::kFiles[i].leaf).c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    return true;
}

const Sheet& Get(Sprite s) {
    const int i = (int)s;
    return i >= 0 && i < (int)Sprite::Count ? g_sheets[i] : g_none;
}

const wchar_t* TitleFace() { return g_title; }
const wchar_t* TextFace() { return g_text; }

bool Covers(const wchar_t* face, const std::wstring& text) {
    if (!face) return false;
    if (text.empty()) return true;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return false;
    HFONT f = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    HGDIOBJ old = SelectObject(dc, f);
    std::vector<WORD> idx(text.size());
    const DWORD n = GetGlyphIndicesW(dc, text.c_str(), (int)text.size(), idx.data(), GGI_MARK_NONEXISTING_GLYPHS);
    bool all = n != GDI_ERROR;
    for (size_t i = 0; all && i < text.size(); ++i)
        if (idx[i] == 0xFFFF && text[i] != L'\n' && text[i] != L'\r') all = false;
    SelectObject(dc, old);
    DeleteObject(f);
    DeleteDC(dc);
    return all;
}

void SetDir(const std::wstring& dir) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_dir = dir;
    g_tried = g_ready = false;
    for (Sheet& s : g_sheets) s = Sheet{};
}

}  // namespace gameui
