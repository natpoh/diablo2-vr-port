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

// The family GDI knows a font file by: its name table's name 1 (the legacy family), Windows' platform - 0.205.12: Exocet was asked for as
// "Exocet Blizzard Mixed Caps" (the file's typographic family, name 16) while GDI calls it "ExocetBlizzardMixedCapsOTMedium": the face was never
// found and our own font drew every "Exocet" name (gameui::FamilyInFontFile, tested in own_labels_test against the game's own files when present).
std::map<std::wstring, std::wstring> g_families;   // file -> the family taken from it (the pointer TakeFont hands out stays valid)
const wchar_t* TakeFont(const std::filesystem::path& file, const wchar_t* family) {
    std::string bytes;
    std::wstring inFile;
    if (ReadAll(file, &bytes)) inFile = FamilyInFontFile(bytes);
    std::wstring& keep = g_families[file.wstring()];
    keep = inFile.empty() ? std::wstring(family) : inFile;
    const wchar_t* face = keep.c_str();
    const bool before = FaceIs(face);   // (already in: installed, or taken in before)
    const bool added = before || (std::filesystem::exists(file) && AddFontResourceExW(file.c_str(), FR_PRIVATE, nullptr) != 0);
    const bool real = added && FaceIs(face);
    if (added && !before) g_fontsIn[file.wstring()] = face;
    char line[400];
    std::snprintf(line, sizeof line, "vrcam: game font - %ls: its family in the file \"%ls\", %s; GDI %s it as that face%s", file.filename().c_str(),
                  inFile.empty() ? L"(not read)" : inFile.c_str(), added ? (before ? "already in" : "taken in (private)") : "NOT taken in",
                  real ? "knows" : "does NOT know", real ? "" : " - ours is drawn in our own font");
    if (LogFn log = g_log.load()) log(line);
    return real ? face : nullptr;
}

}  // namespace

std::wstring FamilyInFontFile(const std::string& b) {
    auto u16 = [&](size_t at) -> unsigned { return at + 2 <= b.size() ? ((unsigned char)b[at] << 8) | (unsigned char)b[at + 1] : 0u; };
    auto u32 = [&](size_t at) -> uint32_t { return at + 4 <= b.size() ? ((uint32_t)u16(at) << 16) | u16(at + 2) : 0u; };
    const unsigned tables = u16(4);
    size_t name = 0, nameLen = 0;
    for (unsigned i = 0; i < tables && 12 + 16 * (size_t)i + 16 <= b.size(); ++i)
        if (b.compare(12 + 16 * (size_t)i, 4, "name") == 0) { name = u32(12 + 16 * (size_t)i + 8); nameLen = u32(12 + 16 * (size_t)i + 12); }
    if (!name || name + 6 > b.size() || name + nameLen > b.size()) return {};
    const unsigned count = u16(name + 2), strings = u16(name + 4);
    std::wstring best;
    int bestScore = -1;
    for (unsigned i = 0; i < count; ++i) {
        const size_t r = name + 6 + 12 * (size_t)i;
        if (r + 12 > b.size()) break;
        const unsigned platform = u16(r), lang = u16(r + 4), id = u16(r + 6), len = u16(r + 8), off = u16(r + 10);
        if (id != 1 || platform != 3) continue;   // the legacy family, Windows' own names (UTF-16 big-endian)
        const size_t at = name + strings + off;
        if (at + len > b.size()) continue;
        std::wstring s;
        for (unsigned k = 0; k + 1 < len; k += 2) s += (wchar_t)u16(at + k);
        const int score = lang == 0x409 ? 2 : 1;   // US English first
        if (score > bestScore && !s.empty()) { best = s; bestScore = score; }
    }
    return best;
}

namespace {

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
const wchar_t* TextFaceAlone() {
    std::lock_guard<std::mutex> lock(g_lock);
    static bool tried = false;
    if (g_text || tried) return g_text;
    tried = true;
    if (g_dir.empty()) g_dir = DefaultDir();
    g_text = TakeFont(std::filesystem::path(g_dir) / gameuifiles::kFiles[gameuifiles::kSpriteCount + 1].leaf, L"Formal436 BT");
    return g_text;
}

const wchar_t* TitleFaceAlone() {
    std::lock_guard<std::mutex> lock(g_lock);
    static bool tried = false;
    if (g_title || tried) return g_title;
    tried = true;
    if (g_dir.empty()) g_dir = DefaultDir();
    g_title = TakeFont(std::filesystem::path(g_dir) / gameuifiles::kFiles[gameuifiles::kSpriteCount].leaf, L"Exocet Blizzard Mixed Caps");
    return g_title;
}

const Sheet* PlateSprite(int piece) {
    static Sheet sheets[gameuifiles::kPlateFileCount];
    static bool tried[gameuifiles::kPlateFileCount] = {};
    if (piece < 0 || piece >= gameuifiles::kPlateFileCount) return nullptr;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!tried[piece]) {
        tried[piece] = true;
        if (g_dir.empty()) g_dir = DefaultDir();
        std::string bytes;
        const char* why = "not there";
        const bool ok = ReadAll(std::filesystem::path(g_dir) / gameuifiles::kPlateFiles[piece].leaf, &bytes) && Decode(bytes, &sheets[piece], &why);
        if (!ok) sheets[piece] = Sheet{};
        static int told = 0;
        if (told < 3) {
            ++told;
            char line[220];
            std::snprintf(line, sizeof line, "vrcam: our plate - the game's %ls %s", gameuifiles::kPlateFiles[piece].leaf, ok ? "read" : why);
            if (LogFn log = g_log.load()) log(line);
        }
    }
    return sheets[piece].Ok() ? &sheets[piece] : nullptr;
}

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
