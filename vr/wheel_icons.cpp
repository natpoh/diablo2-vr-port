// wheel_icons.cpp - the skill wheel's icon cache reader (vr/wheel_icons.h). One worker thread decodes a PNG at a time with WIC
// when the painter asks for it; file reads and decoding never run on the headset's frame thread.
//
// The cache layout and the way of showing icons off a local copy of the player's own game follow bearstonem/d2r-pcvr
// (MIT, steamvr/interaction_icons.cpp); written again here.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_map>

#include "wheel_icons.h"
#include "game_ui_sprite.h"                // the game's 'SpA1' sheets (pure)
#include "../shared/game_ui_files.h"       // the game-art folder beside the plugin
#include "../shared/skill_icon_files.h"    // which sheet holds which class's icons

namespace wheelicons {
namespace {

std::mutex g_lock;
std::condition_variable g_wake;
// The worker's thread object is made once and never destroyed: Shutdown joins it; a joinable std::thread in a static at process
// exit would call terminate, and joining there (under the loader lock) can hang.
std::thread& Worker() {
    static std::thread* t = new std::thread;
    return *t;
}
bool g_stop = false;
std::wstring g_dir;
bool g_manifestRead = false;
std::unordered_map<int32_t, Entry> g_entries;
std::unordered_map<int, TreeLine> g_trees;   // by class
bool g_treeData = false;                     // some manifest row names a class: the cache was made with the trees in it
std::unordered_map<int32_t, std::shared_ptr<const Image>> g_images;   // a null entry: asked for, not there (yet)
std::deque<int32_t> g_pending;
std::atomic<uint32_t> g_revision{0};
// the running game's tables (SetGameTables) and its icon sheets beside the plugin
std::shared_ptr<const GameTables> g_game;
std::wstring g_sheetDir;
int g_sheetsHere = -1;          // how many of the sheets are there (-1: not looked yet)
ULONGLONG g_sheetsLookedAt = 0; // none there: looked again after a while (Settings may copy them while the game runs)

constexpr size_t kMaxIcons = 512;

bool Hex64Png(const std::string& f) {
    if (f.size() != 68 || f.compare(64, 4, ".png") != 0) return false;
    for (int i = 0; i < 64; ++i) {
        const char c = f[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool KeyOk(const std::string& k) {
    if (k.empty() || k.size() > 64) return false;
    for (const char c : k)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) return false;
    return true;
}

std::wstring DefaultDir() {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH] = L"";
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&DefaultDir), &module);
    GetModuleFileNameW(module, path, MAX_PATH);
    return (std::filesystem::path(path).parent_path() / L"d2r_vr_skill_icons").wstring();
}

std::wstring DefaultSheetDir() {
    return (std::filesystem::path(DefaultDir()).parent_path() / gameuifiles::kFolder).wstring();
}

// under g_lock: the sheet a skill's icon is cut from and its frame, from the game's tables (false: none)
bool GameIconLocked(int32_t id, int* sheet, int* frame) {
    if (!g_game || id < 0 || (size_t)id >= g_game->skill.size()) return false;
    const GameSkill& s = g_game->skill[(size_t)id];
    if (s.iconCel < 0) return false;
    const int idx = s.cls < 0 ? skilliconfiles::kShared : s.cls < g_game->classes ? skilliconfiles::SheetOf(g_game->code[s.cls]) : -1;
    if (idx < 0) return false;
    *sheet = idx;
    *frame = s.iconCel;
    return true;
}

// under g_lock: how many of the game's icon sheets are beside the plugin (looked again every 10 s while none is)
int SheetsHereLocked() {
    const ULONGLONG now = GetTickCount64();
    if (g_sheetsHere > 0 || (g_sheetsHere == 0 && now - g_sheetsLookedAt < 10000)) return g_sheetsHere;
    if (g_sheetDir.empty()) g_sheetDir = DefaultSheetDir();
    int n = 0;
    for (const auto& s : skilliconfiles::kSheets)
        if (GetFileAttributesW((std::filesystem::path(g_sheetDir) / s.leaf).c_str()) != INVALID_FILE_ATTRIBUTES) ++n;
    g_sheetsHere = n;
    g_sheetsLookedAt = now;
    return n;
}

bool ReadAll(const std::filesystem::path& path, std::string* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff n = in.tellg();
    if (n <= 0 || n > (64ll << 20)) return false;
    out->resize((size_t)n);
    in.seekg(0);
    return (bool)in.read(out->data(), n);
}

// under g_lock
void ReadManifestLocked() {
    if (g_manifestRead) return;
    g_manifestRead = true;
    if (g_dir.empty()) g_dir = DefaultDir();
    try {
        std::ifstream in(std::filesystem::path(g_dir) / L"manifest.tsv");
        std::string line;
        while (g_entries.size() < 8192 && std::getline(in, line)) {
            Entry e;
            if (ParseLine(line, &e)) {
                g_entries[e.id] = e;
                if (e.cls >= 0) g_treeData = true;
            }
        }
        std::ifstream trees(std::filesystem::path(g_dir) / L"trees.tsv");
        while (g_trees.size() < 32 && std::getline(trees, line)) {
            TreeLine t;
            if (ParseTreeLine(line, &t)) g_trees[t.cls] = t;
        }
    } catch (...) {}   // no cache, or one that cannot be read: names only
}

std::shared_ptr<const Image> Decode(IWICImagingFactory* wic, const std::filesystem::path& path) {
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    auto result = std::make_shared<Image>();
    UINT w = 0, h = 0;
    if (wic && SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w && h && w <= 1024 && h <= 1024 &&
        SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        result->width = w;
        result->height = h;
        result->rgba.resize((size_t)w * h * 4);
        if (FAILED(converter->CopyPixels(nullptr, w * 4, (UINT)result->rgba.size(), result->rgba.data()))) result->rgba.clear();
    }
    for (IUnknown* p : {static_cast<IUnknown*>(converter), static_cast<IUnknown*>(frame), static_cast<IUnknown*>(decoder)})
        if (p) p->Release();
    return result->rgba.empty() ? nullptr : result;
}

void Run() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    // the game's sheets decoded for the icons asked together (a sheet is some 4 MB): dropped when nothing more is waiting
    std::unordered_map<int, std::shared_ptr<gameui::Sheet>> sheets;
    for (;;) {
        int32_t id;
        std::filesystem::path file;
        int sheet = -1, frame = -1;
        std::shared_ptr<const GameTables> tables;
        {
            std::unique_lock<std::mutex> lock(g_lock);
            if (g_pending.empty()) sheets.clear();
            g_wake.wait(lock, [] { return g_stop || !g_pending.empty(); });
            if (g_stop) break;
            id = g_pending.front();
            g_pending.pop_front();
            const auto it = g_entries.find(id);
            if (it != g_entries.end()) file = std::filesystem::path(g_dir) / it->second.file;
            else if (GameIconLocked(id, &sheet, &frame)) {
                if (g_sheetDir.empty()) g_sheetDir = DefaultSheetDir();
                file = std::filesystem::path(g_sheetDir) / skilliconfiles::kSheets[sheet].leaf;
                tables = g_game;
            }
        }
        std::shared_ptr<const Image> image;
        if (!file.empty() && sheet < 0) {
            try { image = Decode(wic, file); } catch (...) {}
        } else if (!file.empty()) {
            try {
                auto& s = sheets[sheet];
                if (!s) {
                    s = std::make_shared<gameui::Sheet>();
                    std::string bytes;
                    if (!ReadAll(file, &bytes) || !gameui::Decode(bytes, s.get())) s->frames = 0;   // (not there or not a sheet: no icons from it this round)
                }
                auto cut = std::make_shared<Image>();
                if (s->Ok() && CutFrame(*s, frame, cut.get())) image = std::move(cut);
            } catch (...) {}
        }
        {
            std::lock_guard<std::mutex> lock(g_lock);
            if (sheet < 0 || tables == g_game) g_images[id] = std::move(image);   // (tables changed meanwhile: the entry was dropped, asked again)
        }
        g_revision.fetch_add(1);
    }
    if (wic) wic->Release();
    if (SUCCEEDED(co)) CoUninitialize();
}

}  // namespace

namespace {

// A line split at its tabs (the trailing CR / LF dropped).
std::vector<std::string> Fields(const std::string& line) {
    std::string s = line;
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    std::vector<std::string> out;
    size_t from = 0;
    for (;;) {
        const size_t t = s.find('\t', from);
        out.push_back(s.substr(from, t == std::string::npos ? std::string::npos : t - from));
        if (t == std::string::npos || out.size() >= 8) break;
        from = t + 1;
    }
    return out;
}

// A small non-negative number, or `bad`.
int Small(const std::string& s, int most, int bad) {
    if (s.empty() || s.size() > 3) return bad;
    int v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return bad;
        v = v * 10 + (c - '0');
    }
    return v <= most ? v : bad;
}

}  // namespace

bool ParseLine(const std::string& line, Entry* out) {
    const std::vector<std::string> f = Fields(line);
    if (f.size() < 2 || f[0].size() < 2 || f[0].size() > 6 || f[0][0] != 's') return false;
    int32_t id = 0;
    for (size_t i = 1; i < f[0].size(); ++i) {
        if (f[0][i] < '0' || f[0][i] > '9') return false;
        id = id * 10 + (f[0][i] - '0');
    }
    if (id < 0 || id > 4095 || !Hex64Png(f[1])) return false;
    out->id = id;
    out->file = f[1];
    out->key = f.size() > 2 && KeyOk(f[2]) ? f[2] : std::string();
    out->cls = f.size() > 3 ? Small(f[3], 15, -1) : -1;
    out->page = f.size() > 4 && out->cls >= 0 ? Small(f[4], 3, 0) : 0;
    out->aims = f.size() > 5 ? Small(f[5], 1, -1) : -1;
    return true;
}

bool ParseTreeLine(const std::string& line, TreeLine* out) {
    const std::vector<std::string> f = Fields(line);
    if (f.size() < 2) return false;
    const int cls = Small(f[0], 15, -1);
    if (cls < 0) return false;
    out->cls = cls;
    for (int t = 0; t < 3; ++t) out->key[t] = (size_t)t + 1 < f.size() && KeyOk(f[t + 1]) ? f[t + 1] : std::string();
    return true;
}

void SetDirectory(const std::wstring& dir) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_manifestRead && !Worker().joinable()) g_dir = dir;
}

void SetSheetDirectory(const std::wstring& dir) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_sheetDir = dir;
    g_sheetsHere = -1;
}

bool Available() {
    std::lock_guard<std::mutex> lock(g_lock);
    ReadManifestLocked();
    return !g_entries.empty() || (g_game && SheetsHereLocked() > 0);
}

std::string NameKey(int32_t skillId) {
    std::lock_guard<std::mutex> lock(g_lock);
    ReadManifestLocked();
    const auto it = g_entries.find(skillId);
    return it == g_entries.end() ? std::string() : it->second.key;
}

uint32_t NameId(int32_t skillId) {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_game && skillId >= 0 && (size_t)skillId < g_game->skill.size() ? g_game->skill[(size_t)skillId].nameId : 0;
}

bool HasTrees() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_game) return true;
    ReadManifestLocked();
    return g_treeData && !g_trees.empty();
}

bool SkillTree(int32_t skillId, int* cls, int* page) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_game) {   // the running game's own tables first: they are this build's
        if (skillId < 0 || (size_t)skillId >= g_game->skill.size()) return false;
        *cls = g_game->skill[(size_t)skillId].cls;
        *page = g_game->skill[(size_t)skillId].page;
        return true;
    }
    ReadManifestLocked();
    const auto it = g_entries.find(skillId);
    if (it == g_entries.end()) return false;
    *cls = it->second.cls;
    *page = it->second.page;
    return true;
}

int Aims(int32_t skillId) {
    std::lock_guard<std::mutex> lock(g_lock);
    ReadManifestLocked();
    const auto it = g_entries.find(skillId);
    return it == g_entries.end() ? -1 : it->second.aims;
}

std::string TreeKey(int cls, int tree) {
    std::lock_guard<std::mutex> lock(g_lock);
    ReadManifestLocked();
    const auto it = g_trees.find(cls);
    return tree >= 1 && tree <= 3 && it != g_trees.end() ? it->second.key[tree - 1] : std::string();
}

uint32_t TreeNameId(int cls, int tree) {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_game && cls >= 0 && cls < g_game->classes && tree >= 1 && tree <= 3 ? g_game->treeName[cls][tree - 1] : 0;
}

std::shared_ptr<const Image> Get(int32_t skillId) {
    std::lock_guard<std::mutex> lock(g_lock);
    ReadManifestLocked();
    const auto it = g_images.find(skillId);
    if (it != g_images.end()) return it->second;
    int sheet = -1, frame = -1;
    const bool cached = g_entries.find(skillId) != g_entries.end();
    if (!cached && !(GameIconLocked(skillId, &sheet, &frame) && SheetsHereLocked() > 0)) return {};
    if (g_images.size() >= kMaxIcons) return {};
    g_images.emplace(skillId, nullptr);
    g_pending.push_back(skillId);
    if (!Worker().joinable()) {
        g_stop = false;
        Worker() = std::thread(Run);
    }
    g_wake.notify_one();
    return {};
}

uint32_t Revision() { return g_revision.load(); }

void Shutdown() {
    // The worker is taken out of Worker() under the lock and joined outside it: Get (any painting thread - the wheel, the tiles, the
    // wrist's medallion) starts one under the lock, and two Shutdowns at once both saw it joinable and the second join threw
    // std::system_error out of an ini reload (0.201.0, Serg's PC: skillwheel::Configure -> Release -> here, the game died).
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_stop = true;
        g_wake.notify_all();
        worker = std::move(Worker());
    }
    if (worker.joinable()) {
        if (worker.get_id() == std::this_thread::get_id()) {
            worker.detach();   // (never from the worker itself; a join there would throw)
        } else {
            try {
                worker.join();
            } catch (const std::system_error&) {
                worker.detach();
            }
        }
    }
    std::lock_guard<std::mutex> lock(g_lock);
    g_pending.clear();
    g_images.clear();
    g_entries.clear();
    g_trees.clear();
    g_treeData = false;
    g_manifestRead = false;   // (read again at the next use: a cache made since is found)
    g_sheetsHere = -1;        // (and sheets copied since)
    g_stop = false;
    g_revision.fetch_add(1);
}

void SetGameTables(std::shared_ptr<const GameTables> tables) {
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_game = std::move(tables);
        g_images.clear();   // (an icon cut by the old tables, or a skill they had none for; the waiting ones stay asked)
        g_sheetsHere = -1;
    }
    g_revision.fetch_add(1);
}

bool HasGameTables() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_game != nullptr;
}

bool CutFrame(const gameui::Sheet& sheet, int frame, Image* out) {
    if (!sheet.Ok() || frame < 0 || frame >= sheet.frames || sheet.frameWidth > 1024 || sheet.height > 1024) return false;
    out->width = (unsigned)sheet.frameWidth;
    out->height = (unsigned)sheet.height;
    out->rgba.resize((size_t)sheet.frameWidth * sheet.height * 4);
    for (int y = 0; y < sheet.height; ++y) std::memcpy(&out->rgba[(size_t)y * sheet.frameWidth * 4], sheet.At(frame, 0, y), (size_t)sheet.frameWidth * 4);
    return true;
}

namespace {
template <class T> T At(const RawTable& t, uint32_t row, uint32_t offset) {
    T v;
    std::memcpy(&v, t.rows + (size_t)row * t.size + offset, sizeof v);
    return v;
}
}  // namespace

bool ParseGameTables(const RawTable& skills, const RawTable& desc, const RawTable& charstats, const RawTable& playerclass, GameTables* out,
                     const char** why) {
    auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    // the rows hold at least what is read, and as many as such tables have
    if (!skills.rows || skills.size < 0x23E || skills.count < 2 || skills.count > 4096) return fail("the skills table is not what this build knows");
    if (!desc.rows || desc.size < 0x0A || desc.count < 2 || desc.count > 4096) return fail("the skill descriptions table is not what this build knows");
    if (!charstats.rows || charstats.size < 0x5C || charstats.count < 1 || charstats.count > 16) return fail("the classes' stats table is not what this build knows");
    if (!playerclass.rows || playerclass.size < 0x34 || playerclass.count != charstats.count) return fail("the classes table is not what this build knows");
    GameTables t;
    t.classes = (int)playerclass.count;
    for (uint32_t c = 0; c < playerclass.count; ++c) {
        char code[4];
        std::memcpy(code, playerclass.rows + (size_t)c * playerclass.size + 0x30, 4);
        for (int i = 0; i < 3; ++i)
            if (code[i] < 'a' || code[i] > 'z') return fail("a class's code does not read as one (another layout)");
        std::memcpy(t.code[c], code, 3);
        for (int k = 0; k < 3; ++k) t.treeName[c][k] = At<uint16_t>(charstats, c, 0x56 + 2u * (uint32_t)k);
    }
    for (uint32_t d = 0; d < desc.count; ++d)
        if (At<uint8_t>(desc, d, 0x02) > 3) return fail("a skill description's tree page is past 3 (another layout)");
    t.skill.resize(skills.count);
    std::vector<int8_t> owner(desc.count, -1);   // a description shared by several skills: the class of the one that has one
    int onTree = 0;
    for (uint32_t i = 0; i < skills.count; ++i) {
        if (At<uint16_t>(skills, i, 0x00) != i) return fail("a skill row's id is not its row (another layout)");
        const int8_t cls = At<int8_t>(skills, i, 0x2C);
        const int16_t d = At<int16_t>(skills, i, 0x23C);
        if (cls < -1 || cls >= t.classes) return fail("a skill's class is past the classes (another layout)");
        if (d < -1 || d >= (int)desc.count) return fail("a skill's description is past the descriptions (another layout)");
        t.skill[i].cls = cls;
        if (d >= 0) {
            t.skill[i].iconCel = At<uint8_t>(desc, (uint32_t)d, 0x06);
            const uint16_t name = At<uint16_t>(desc, (uint32_t)d, 0x08);
            t.skill[i].nameId = name == 0xFFFF ? 0 : name;
            if (cls >= 0) owner[(size_t)d] = cls;
        }
    }
    for (uint32_t i = 0; i < skills.count; ++i) {
        GameSkill& s = t.skill[i];
        const int16_t d = At<int16_t>(skills, i, 0x23C);
        if (d < 0) continue;
        if (s.cls < 0) s.cls = owner[(size_t)d];
        const uint8_t page = At<uint8_t>(desc, (uint32_t)d, 0x02);
        s.page = s.cls >= 0 && page >= 1 && page <= 3 ? page : 0;
        if (s.page) ++onTree;
    }
    if (onTree == 0) return fail("no skill is on a class's tree (another layout)");
    *out = std::move(t);
    return true;
}

}  // namespace wheelicons
