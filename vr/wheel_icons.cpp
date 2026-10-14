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
#include <thread>
#include <unordered_map>

#include "wheel_icons.h"

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
std::unordered_map<int32_t, std::shared_ptr<const Image>> g_images;   // a null entry: asked for, not there (yet)
std::deque<int32_t> g_pending;
std::atomic<uint32_t> g_revision{0};

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
            if (ParseLine(line, &e)) g_entries[e.id] = e;
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
    for (;;) {
        int32_t id;
        std::filesystem::path file;
        {
            std::unique_lock<std::mutex> lock(g_lock);
            g_wake.wait(lock, [] { return g_stop || !g_pending.empty(); });
            if (g_stop) break;
            id = g_pending.front();
            g_pending.pop_front();
            const auto it = g_entries.find(id);
            if (it != g_entries.end()) file = std::filesystem::path(g_dir) / it->second.file;
        }
        std::shared_ptr<const Image> image;
        if (!file.empty()) {
            try { image = Decode(wic, file); } catch (...) {}
        }
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g_images[id] = std::move(image);
        }
        g_revision.fetch_add(1);
    }
    if (wic) wic->Release();
    if (SUCCEEDED(co)) CoUninitialize();
}

}  // namespace

bool ParseLine(const std::string& line, Entry* out) {
    if (line.size() < 4 || line[0] != 's') return false;
    const size_t t1 = line.find('\t');
    if (t1 == std::string::npos || t1 < 2 || t1 > 6) return false;
    int32_t id = 0;
    for (size_t i = 1; i < t1; ++i) {
        if (line[i] < '0' || line[i] > '9') return false;
        id = id * 10 + (line[i] - '0');
    }
    if (id < 0 || id > 4095) return false;
    size_t t2 = line.find('\t', t1 + 1);
    std::string file = line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
    while (!file.empty() && (file.back() == '\r' || file.back() == '\n')) file.pop_back();
    if (!Hex64Png(file)) return false;
    std::string key;
    if (t2 != std::string::npos) {
        key = line.substr(t2 + 1);
        while (!key.empty() && (key.back() == '\r' || key.back() == '\n')) key.pop_back();
        if (!KeyOk(key)) key.clear();
    }
    out->id = id;
    out->file = file;
    out->key = key;
    return true;
}

void SetDirectory(const std::wstring& dir) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_manifestRead && !Worker().joinable()) g_dir = dir;
}

bool Available() {
    std::lock_guard<std::mutex> lock(g_lock);
    ReadManifestLocked();
    return !g_entries.empty();
}

std::string NameKey(int32_t skillId) {
    std::lock_guard<std::mutex> lock(g_lock);
    ReadManifestLocked();
    const auto it = g_entries.find(skillId);
    return it == g_entries.end() ? std::string() : it->second.key;
}

std::shared_ptr<const Image> Get(int32_t skillId) {
    std::lock_guard<std::mutex> lock(g_lock);
    ReadManifestLocked();
    if (g_entries.empty()) return {};
    const auto it = g_images.find(skillId);
    if (it != g_images.end()) return it->second;
    if (g_entries.find(skillId) == g_entries.end() || g_images.size() >= kMaxIcons) return {};
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
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_stop = true;
        g_wake.notify_all();
    }
    if (Worker().joinable()) Worker().join();
    std::lock_guard<std::mutex> lock(g_lock);
    g_pending.clear();
    g_images.clear();
    g_entries.clear();
    g_manifestRead = false;   // (read again at the next use: a cache made since is found)
    g_stop = false;
    g_revision.fetch_add(1);
}

}  // namespace wheelicons
