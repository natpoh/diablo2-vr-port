// [headset_shot]: a screenshot of what reaches the headset - see vr/headset_shot.h. The draw thread only records copies; a thread of
// its own makes the read-back buffers before and reads them after, writes the PNGs (WIC) and the text.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wincodec.h>

#include <atomic>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "headset_shot.h"
#include "headset_shot_pure.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace headshot {
namespace {

void (*g_log)(const char*) = nullptr;
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[768];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

std::string g_version = "?";
std::wstring g_folder;               // d2rloader\logs\ with its slash
std::atomic<bool> g_on{false};
std::atomic<int> g_key{0};           // 0 none, 1 Scroll Lock, 2 Pause, 3 F8
int g_take = INT_MIN;                // Settings' button: the counter as last read (INT_MIN: not read yet)

// kArmed: the frame says what it would hand over (the sizes); kMaking: the buffers being made; kReady: the next frame's pictures copied;
// kWriting: read back and written. kArming / kReadying: the same, from the next frame's start on - a press (the update thread) or the buffers
// made (their thread) can come between a pair's eyes (FlatVR: the left at its hold, the right at its present), and half a pair is no shot.
// Wanted() in kArming, kArmed, kReadying and kReady.
enum Phase { kIdle, kArming, kArmed, kMaking, kReadying, kReady, kWriting };
std::atomic<int> g_phase{kIdle};

struct Slot {
    std::string part;
    uint32_t x = 0, y = 0, w = 0, h = 0;
    DXGI_FORMAT texFormat = DXGI_FORMAT_UNKNOWN, format = DXGI_FORMAT_UNKNOWN;
    Layout layout = Layout::None;
    bool opaque = true;
    ID3D12Resource* buf = nullptr;
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    UINT64 fenceV = 0;   // copied: the fence's value after the copy (0 not copied)
    std::string note;    // why it was not read
};
struct Job {
    std::string stamp, by, info;
    std::vector<Slot> slots;
    ID3D12Device* dev = nullptr;   // (a reference of ours)
    ID3D12Fence* fence = nullptr;
    UINT64 fenceV = 0;
    int emptyFrames = 0, readyFrames = 0;
    double usSizes = 0.0, usCopy = 0.0;   // the draw thread's time in Offer / FrameDone: the frame that said the sizes, the frame copied
    std::vector<std::string> notes;
};
std::mutex g_lock;
Job g_job;
std::vector<std::wstring> g_lastFiles;
std::thread g_worker;

double UsNow() {
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1e6 / f.QuadPart;
}
// The draw thread's time in a call, added to the shot's cost at its end (under g_lock: the job is alive while it runs).
struct CostScope {
    double t0 = UsNow();
    double* to = nullptr;
    ~CostScope() { if (to) *to += UsNow() - t0; }
};

Slot* FindSlot(const std::string& part) {
    for (Slot& s : g_job.slots) if (s.part == part) return &s;
    return nullptr;
}

void ReleaseJob(Job& j, bool gpuDone) {
    for (Slot& s : j.slots) {
        if (!gpuDone && s.fenceV) continue;   // a copy that never came back: its buffer and list left as they are, not freed under the GPU
        if (s.list) s.list->Release();
        if (s.alloc) s.alloc->Release();
        if (s.buf) s.buf->Release();
    }
    if (j.fence && gpuDone) j.fence->Release();
    if (j.dev) j.dev->Release();
    j = Job{};
}

D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

// -- the thread of its own ----------------------------------------------------------------------------------------------------------

void Start(void (*fn)()) {
    if (g_worker.joinable()) g_worker.join();   // (finished: the phases follow each other)
    try {
        g_worker = std::thread(fn);
    } catch (...) {
        LogF("vrcam: headset shot - could not start its thread: no shot");
        ReleaseJob(g_job, true);
        g_phase.store(kIdle);
    }
}

// The buffers for the sizes the frame asked for, a list each, the fence; then the next frame copies.
void MakeBuffers() {
    std::lock_guard<std::mutex> hold(g_lock);
    Job& j = g_job;
    if (j.dev && FAILED(j.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&j.fence))) j.fence = nullptr;
    for (Slot& s : j.slots) {
        if (!j.dev || !j.fence) break;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = (UINT64)RowPitch(s.w, s.layout) * s.h;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(j.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
                                                  (void**)&s.buf)) ||
            FAILED(j.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&s.alloc)) ||
            FAILED(j.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&s.list))) {
            s.note = "no read-back buffer could be made for it";
            continue;
        }
        s.list->Close();
    }
    if (!j.fence) j.notes.push_back("no fence could be made: nothing read back");
    g_phase.store(kReadying);
}

bool WritePng(IWICImagingFactory* wic, const std::wstring& path, const std::vector<uint8_t>& bgra, uint32_t w, uint32_t h) {
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = false;
    WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(w, h)) &&
        SUCCEEDED(frame->SetPixelFormat(&pf)) && pf == GUID_WICPixelFormat32bppBGRA &&
        SUCCEEDED(frame->WritePixels(h, w * 4, (UINT)bgra.size(), const_cast<BYTE*>(bgra.data()))) && SUCCEEDED(frame->Commit()) &&
        SUCCEEDED(enc->Commit()))
        ok = true;
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

std::string Narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += c < 128 ? (char)c : '?';
    return s;
}

const char* FormatText(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return "R8G8B8A8_TYPELESS";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return "B8G8R8A8_TYPELESS";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8X8_UNORM: return "B8G8R8X8_UNORM";
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return "B8G8R8X8_UNORM_SRGB";
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return "R10G10B10A2_TYPELESS";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return "R16G16B16A16_TYPELESS";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    default: return nullptr;
    }
}
std::string FormatName(DXGI_FORMAT f) {
    if (const char* n = FormatText(f)) return n;
    return "DXGI format " + std::to_string((int)f);
}

// The copies waited for, read, turned into PNGs and the text; the old shots beyond the newest kHeadsetShotsKept deleted; one log line.
void WriteShot() {
    Job j;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        j = std::move(g_job);
        g_job = Job{};
    }
    bool gpuDone = true;
    if (j.fence) {
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        for (Slot& s : j.slots) {
            if (!s.fenceV || j.fence->GetCompletedValue() >= s.fenceV) continue;
            if (ev && SUCCEEDED(j.fence->SetEventOnCompletion(s.fenceV, ev))) WaitForSingleObject(ev, 5000);
            if (j.fence->GetCompletedValue() < s.fenceV) { gpuDone = false; s.note = "its copy did not come back from the GPU within 5 s"; }
        }
        if (ev) CloseHandle(ev);
    }
    struct Pic { std::string part; std::vector<uint8_t> px; uint32_t w, h; };
    std::vector<Pic> pics;
    for (Slot& s : j.slots) {
        if (!s.fenceV || !s.note.empty() || !s.buf) {
            if (s.note.empty()) s.note = "not handed over in the frame read back";
            continue;
        }
        uint8_t* m = nullptr;
        D3D12_RANGE all{0, (SIZE_T)RowPitch(s.w, s.layout) * s.h}, none{0, 0};
        if (FAILED(s.buf->Map(0, &all, (void**)&m)) || !m) { s.note = "its read-back buffer could not be mapped"; continue; }
        Pic p{s.part, {}, s.w, s.h};
        if (!ToBgra8(m, RowPitch(s.w, s.layout), s.w, s.h, s.layout, s.opaque, p.px)) s.note = "its format is not read";
        s.buf->Unmap(0, &none);
        if (s.note.empty()) pics.push_back(std::move(p));
    }
    CreateDirectoryW(g_folder.c_str(), nullptr);
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* wic = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    std::vector<std::wstring> files;
    auto write = [&](const char* part, const std::vector<uint8_t>& px, uint32_t w, uint32_t h) {
        const std::wstring path = g_folder + FileName(j.stamp, part, L".png");
        if (wic && WritePng(wic, path, px, w, h)) { files.push_back(path); return true; }
        return false;
    };
    std::string read;
    const Pic *left = nullptr, *right = nullptr;
    for (const Pic& p : pics) {
        const bool ok = write(p.part.c_str(), p.px, p.w, p.h);
        if (p.part == "left") left = &p;
        if (p.part == "right") right = &p;
        for (const Slot& s : j.slots)
            if (s.part == p.part) {
                char b[320];
                snprintf(b, sizeof b, "  %-14s %ux%u at %u,%u of its image, %s%s -> %s\n", p.part.c_str(), s.w, s.h, s.x, s.y, FormatName(s.format).c_str(),
                         s.format != s.texFormat ? (" (texture " + FormatName(s.texFormat) + ")").c_str() : "",
                         ok ? Narrow(FileName(j.stamp, p.part.c_str(), L".png")).c_str() : "the PNG could not be written");
                read += b;
                if (TakenAsLinear(s.format))
                    read += "                 (UNORM, not sRGB: the runtime takes these values as linear light - the headset shows it brighter than this PNG)\n";
            }
    }
    for (const Slot& s : j.slots)
        if (!s.note.empty()) read += "  " + s.part + ": not read - " + s.note + "\n";
    std::string pair;
    if (left && right) {
        uint32_t w = 0, h = 0;
        const std::vector<uint8_t> sbs = SideBySide(left->px, left->w, left->h, right->px, right->w, right->h, &w, &h);
        write("sbs", sbs, w, h);
        const double differ = DifferentShare(left->px, right->px);
        char b[256];
        if (differ < 0.0) snprintf(b, sizeof b, "left and right: pictures of different sizes (%ux%u, %ux%u)", left->w, left->h, right->w, right->h);
        else if (differ < 0.002) snprintf(b, sizeof b, "left and right: THE SAME PICTURE (%.2f %% of pixels differ) - the headset gets no stereo", differ * 100.0);
        else snprintf(b, sizeof b, "left and right: two pictures (%.1f %% of pixels differ)", differ * 100.0);
        pair = b;
    }
    if (wic) wic->Release();
    if (SUCCEEDED(co)) CoUninitialize();
    {   // the text
        std::string t = "D2R VR Authentic " + g_version + " - headset screenshot " + j.stamp + " (asked by " + j.by + ")\n\n" + j.info;
        if (!t.empty() && t.back() != '\n') t += '\n';
        t += "\nRead back (what the headset was handed):\n" + (read.empty() ? std::string("  nothing\n") : read);
        if (!pair.empty()) t += "  " + pair + "\n";
        {
            char c[200];
            snprintf(c, sizeof c, "\nIts cost on the draw thread: %.3f ms in the frame copied, %.3f ms in the frame before (the sizes); the rest on a thread of its own\n",
                     j.usCopy / 1000.0, j.usSizes / 1000.0);
            t += c;
        }
        for (const std::string& n : j.notes) t += "  " + n + "\n";
        const std::wstring path = g_folder + FileName(j.stamp, "", L".txt");
        HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD wrote = 0;
            WriteFile(f, t.data(), (DWORD)t.size(), &wrote, nullptr);
            CloseHandle(f);
            files.push_back(path);
        }
    }
    {   // the newest kHeadsetShotsKept shots kept
        std::vector<std::pair<std::string, std::wstring>> found;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((g_folder + L"headset_shot_*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do found.push_back({StampOf(fd.cFileName), fd.cFileName});
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::vector<std::string> stamps;
        for (const auto& f : found) stamps.push_back(f.first);
        for (const std::string& old : OldShots(stamps, kHeadsetShotsKept))
            for (const auto& f : found)
                if (f.first == old) DeleteFileW((g_folder + f.second).c_str());
    }
    LogF("vrcam: headset shot - %u picture(s)%s to d2rloader\\logs\\headset_shot_%s_*.png and .txt (asked by %s)%s%s; the draw thread %.3f ms "
         "(the frame copied) + %.3f ms (the frame before)", (unsigned)pics.size(), left && right ? " and both eyes side by side" : "", j.stamp.c_str(),
         j.by.c_str(), pair.empty() ? "" : "; ", pair.c_str(), j.usCopy / 1000.0, j.usSizes / 1000.0);
    ReleaseJob(j, gpuDone);
    {
        std::lock_guard<std::mutex> hold(g_lock);
        g_lastFiles = files;
    }
    g_phase.store(kIdle);
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }
void SetVersion(const char* version) { g_version = version ? version : "?"; }
bool On() { return g_on.load(); }
void SetFolderForTest(const wchar_t* folder) {
    g_folder = folder ? folder : L"";
    if (!g_folder.empty() && g_folder.back() != L'\\') g_folder += L'\\';
}
void SetOnForTest(bool on) { g_on.store(on); }

void LoadSettings(const wchar_t* iniPath) {
    const bool on = GetPrivateProfileIntW(L"headset_shot", L"on", 0, iniPath) == 1;
    if (on != g_on.load()) LogF("vrcam: headset shot %s ([headset_shot] on)", on ? "on - Settings' button and the key take one" : "off");
    g_on.store(on);
    g_key.store(GetPrivateProfileIntW(L"headset_shot", L"key", 0, iniPath));
    if (g_folder.empty()) {
        wchar_t full[MAX_PATH];
        std::wstring dir = iniPath;
        dir.resize(dir.find_last_of(L"\\/") + 1);
        dir += L"..\\logs\\";
        if (GetFullPathNameW(dir.c_str(), MAX_PATH, full, nullptr)) dir = full;
        SetFolderForTest(dir.c_str());
    }
    const int take = GetPrivateProfileIntW(L"headset_shot", L"take", 0, iniPath);
    const int before = g_take;
    g_take = take;
    if (before != INT_MIN && before != take && on && !Request("the Settings button"))
        LogF("vrcam: headset shot - the Settings button: one is still being written");
}

void KeyTick(bool focus) {
    static bool was = false;
    const int k = g_key.load();
    const int vk = k == 1 ? VK_SCROLL : k == 2 ? VK_PAUSE : k == 3 ? VK_F8 : 0;
    const bool now = vk && focus && g_on.load() && (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (now && !was) Request("the key");
    was = now;
}

bool Request(const char* by) {
    if (!g_on.load()) return false;
    int idle = kIdle;
    if (!g_phase.compare_exchange_strong(idle, kArming)) return false;
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_worker.joinable()) g_worker.join();
    g_job = Job{};
    SYSTEMTIME t;
    GetLocalTime(&t);
    g_job.stamp = Stamp(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    g_job.by = by ? by : "?";
    return true;
}

bool Wanted() {
    const int p = g_phase.load(std::memory_order_relaxed);
    return p == kArming || p == kArmed || p == kReadying || p == kReady;
}

void Offer(ID3D12Device* caller, ID3D12CommandQueue* queue, const Picture& p) {
    if (!Wanted() || !caller || !queue || !p.tex || !p.part || !*p.part) return;
    CostScope cost;
    ID3D12Device* dev = nullptr;   // the picture's own device (the caller's may be a wrapper of it): the buffers are made on it
    if (FAILED(p.tex->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) || !dev) return;
    dev->Release();   // (the texture holds it)
    std::lock_guard<std::mutex> hold(g_lock);
    const int phase = g_phase.load();
    if (phase != kArmed && phase != kReady) return;   // (kArming, kReadying: from the next frame on)
    cost.to = phase == kArmed ? &g_job.usSizes : &g_job.usCopy;
    const D3D12_RESOURCE_DESC d = p.tex->GetDesc();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.SampleDesc.Count != 1) return;
    uint32_t x = std::min<uint32_t>(p.x, (uint32_t)d.Width), y = std::min<uint32_t>(p.y, d.Height);
    uint32_t w = p.w ? p.w : (uint32_t)d.Width - x, h = p.h ? p.h : d.Height - y;
    w = std::min<uint32_t>(w, (uint32_t)d.Width - x);
    h = std::min<uint32_t>(h, d.Height - y);
    const Layout layout = LayoutOf(d.Format);
    const std::string part = PartName(p.part);
    if (phase == kArmed) {
        if (!g_job.dev) { g_job.dev = dev; dev->AddRef(); }
        Slot* s = FindSlot(part);
        if (!s) { g_job.slots.push_back(Slot{}); s = &g_job.slots.back(); s->part = part; }
        s->x = x; s->y = y; s->w = w; s->h = h;
        s->texFormat = d.Format;
        s->format = p.format != DXGI_FORMAT_UNKNOWN ? p.format : d.Format;
        s->layout = layout;
        s->opaque = p.opaque;
        if (layout == Layout::None) s->note = "its format (" + FormatName(d.Format) + ") is not read";
        else if (!w || !h) s->note = "an empty part of its image";
        return;
    }
    Slot* s = FindSlot(part);
    if (!s || !s->list || !s->note.empty() || s->fenceV || !g_job.fence || dev != g_job.dev) return;   // (copied once: its list may still run)
    if (s->w != w || s->h != h || s->layout != layout) { s->note = "its size or format changed since the shot was asked - press again"; return; }
    s->x = x; s->y = y;
    if (FAILED(s->alloc->Reset()) || FAILED(s->list->Reset(s->alloc, nullptr))) { s->note = "its copy could not be recorded"; return; }
    if (p.state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        const D3D12_RESOURCE_BARRIER b = Tr(p.tex, p.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        s->list->ResourceBarrier(1, &b);
    }
    D3D12_TEXTURE_COPY_LOCATION to{}, from{};
    to.pResource = s->buf;
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint.Footprint = {d.Format, w, h, 1, RowPitch(w, layout)};
    from.pResource = p.tex;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.SubresourceIndex = 0;
    const D3D12_BOX box{x, y, 0, x + w, y + h, 1};
    s->list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
    if (p.state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        const D3D12_RESOURCE_BARRIER b = Tr(p.tex, D3D12_RESOURCE_STATE_COPY_SOURCE, p.state);
        s->list->ResourceBarrier(1, &b);
    }
    if (FAILED(s->list->Close())) { s->note = "its copy could not be recorded"; return; }
    ID3D12CommandList* l = s->list;
    queue->ExecuteCommandLists(1, &l);
    queue->Signal(g_job.fence, ++g_job.fenceV);
    s->fenceV = g_job.fenceV;
}

void FrameDone(const std::string& info) {
    if (!Wanted()) return;
    CostScope cost;
    std::lock_guard<std::mutex> hold(g_lock);
    Job& j = g_job;
    const int phase = g_phase.load();
    if (phase == kArmed) cost.to = &j.usSizes;
    else if (phase == kReady) cost.to = &j.usCopy;
    if (phase == kArming || phase == kReadying) {   // a frame's start: the pictures from here on
        g_phase.store(phase == kArming ? kArmed : kReady);
        return;
    }
    if (phase == kArmed) {
        bool any = false;
        for (const Slot& s : j.slots) if (s.note.empty()) any = true;
        if (j.slots.empty() && ++j.emptyFrames < 90) return;   // (a frame with pictures none of which can be read: written as it is)
        j.info = info;
        if (!any) {
            j.notes.push_back(j.slots.empty() ? "nothing was handed to the headset in 90 frames (no layer with a picture)"
                                              : "no picture of the frame could be read");
            g_phase.store(kWriting);
            if (cost.to) { *cost.to += UsNow() - cost.t0; cost.to = nullptr; }   // (booked now: the job goes to its thread)
            Start(&WriteShot);
            return;
        }
        g_phase.store(kMaking);
        if (cost.to) { *cost.to += UsNow() - cost.t0; cost.to = nullptr; }   // (booked now: the job goes to its thread)
        Start(&MakeBuffers);
        return;
    }
    bool copied = false;
    for (const Slot& s : j.slots) if (s.fenceV) copied = true;
    if (!copied && ++j.readyFrames < 90) return;   // (this frame handed over something else - a menu between: the next one)
    if (copied) j.info = info;
    else j.notes.push_back("90 frames came with none of the pictures the shot was made for (the view changed?) - press again");
    g_phase.store(kWriting);
    if (cost.to) { *cost.to += UsNow() - cost.t0; cost.to = nullptr; }   // (booked now: the job goes to its thread)
    Start(&WriteShot);
}

bool WaitIdle(unsigned ms) {
    const ULONGLONG until = GetTickCount64() + ms;
    while (g_phase.load() == kMaking || g_phase.load() == kWriting) {
        if (GetTickCount64() >= until) return false;
        Sleep(5);
    }
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_worker.joinable() && g_phase.load() != kMaking && g_phase.load() != kWriting) g_worker.join();
    return true;
}

std::vector<std::wstring> LastFiles() {
    std::lock_guard<std::mutex> hold(g_lock);
    return g_lastFiles;
}

}  // namespace headshot
