// capture.cpp - see capture.h. The press (update thread) makes the folder and arms the taps; the taps copy what they see while the window is
// open (under one lock, a few kilobytes); the back buffer is copied on the present thread into a read-back buffer (one list of ours on the
// game's queue and a signal); a thread of its own waits for the window and the copy, writes the files, keeps the newest folders, logs one line.
#include "capture.h"

#include <objbase.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <D2RLPlugin/api.h>

#include "banner.h"           // the result, big on the flat picture and before the face in VR
#include "capture_rule.h"
#include "d2rcam.h"
#include "headset_shot.h"     // in VR: the headset's own pictures too, when [headset_shot] is on
#include "native_d3d12.h"     // natived3d::OpenList / Submit: our list on the game's queue
#include "patch_hunt_wire.h"  // the game's graphics options now, the line on the picture
#include "pipe_hunt.h"        // one frame's pipelines

namespace capture {
namespace {

void (*g_log)(const char*) = nullptr;
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[700];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}
bool Peek(void* d, uintptr_t s, size_t n) noexcept {
    __try { memcpy(d, (const void*)s, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::wstring g_ini, g_logs;
std::string g_version;
std::string (*g_stateText)() = nullptr;   // the plugin's state in words (vr/frame/frame_order.cpp)
int (*g_eyeNow)() = nullptr;               // the eye being drawn
std::atomic<unsigned> g_vk{0};
std::atomic<int> g_mods{0};   // 1 ctrl, 2 alt, 4 shift
std::atomic<bool> g_collecting{false}, g_busy{false};
std::atomic<ULONGLONG> g_lastPress{0};   // the key's or the console's last capture
std::atomic<bool> g_keyTargets{false};   // [debug] capture_targets: the key takes the intermediate targets too

constexpr ULONGLONG kWindowMs = 1200;   // the taps stay open this long after the press ...
constexpr size_t kPresents = 60;        // ... or this many presents
constexpr size_t kBuilds = 4, kViews = 12;
constexpr size_t kCamBytes = 0x400, kFrameBytes = 0x800;

struct Build {
    ULONGLONG ms = 0;
    int eye = 0;
    uintptr_t renderer = 0, camera = 0, camConst = 0, frameConst = 0;
    bool camOk = false, frameOk = false;
    float cam[kCamBytes / 4] = {}, frame[kFrameBytes / 4] = {};
};
struct View {
    ULONGLONG ms = 0;
    int eye = 0;
    bool proj = false, ours = false, lookOk = false;
    float game[16] = {}, out[16] = {}, look[3] = {};
    float vpW = 0, vpH = 0;
};
std::mutex g_lock;   // what the taps collect (the window's)
std::vector<Build> g_builds;
std::vector<View> g_views;
std::vector<double> g_presentMs;
ULONGLONG g_startMs = 0;
std::wstring g_dir;
std::string g_state, g_notes;   // the state at the press; what failed along the way
// the back buffer's copy
ID3D12Resource* g_rb = nullptr;
ID3D12Fence* g_fence = nullptr;
D3D12_PLACED_SUBRESOURCE_FOOTPRINT g_fp{};
UINT g_rows = 0;
DXGI_FORMAT g_fmt = DXGI_FORMAT_UNKNOWN;
bool g_picAsked = false;

// The intermediate targets ("vrcapture targets", [debug] capture_targets): over one frame, each 2D texture the game moves OUT of a writing state
// (render target, depth write, unordered access - its pass done) is copied right there on the game's own list, before its barrier: our barrier into
// copy source, a copy into a read-back buffer, our barrier back. Armed at the press, recording from the next present to the one after, the fence
// signalled at the present after that (the frame's lists ran by then).
constexpr size_t kMaxTargets = 24;
constexpr UINT64 kTargetBytes = 384ull << 20;
enum : int { kTgtOff = 0, kTgtArmed, kTgtRecording, kTgtClosing, kTgtSignalled };
std::atomic<int> g_tgtPhase{kTgtOff};
struct Target {
    ID3D12Resource* rb = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0;
    D3D12_RESOURCE_DESC desc{};
    D3D12_RESOURCE_STATES from{}, to{};
    D3D12_COMMAND_LIST_TYPE list{};
};
std::vector<Target> g_tgts;
std::vector<ID3D12Resource*> g_tgtSeen;
UINT64 g_tgtBytes = 0;
ID3D12Fence* g_tgtFence = nullptr;
bool g_tgtFull = false, g_wantTargets = false;

// The console ("vrcapture"): its answer when the capture is written, on the UI thread
const D2RL::PluginContext* g_ctx = nullptr;
const D2RL::ThreadService* g_threads = nullptr;
std::mutex g_replyLock;
std::vector<std::string> g_reply;        // lines waiting for the UI thread
bool g_replyWanted = false;              // this capture came from the console
std::string g_lastName, g_lastSummary;   // the last capture (status)

double MsNow() {
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return 1000.0 * (double)t.QuadPart / (double)f.QuadPart;
}
void Note(const char* fmt, ...) {   // (g_lock held)
    char b[400];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_notes += "  ! ";
    g_notes += b;
    g_notes += "\n";
}

// ---- the picture --------------------------------------------------------------------------------------------------------------------------
float Half(uint16_t h) {
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v = e == 0 ? std::ldexp((float)m, -24) : e == 31 ? 1e9f : std::ldexp((float)(m | 1024), (int)e - 25);
    return s ? -v : v;
}
uint8_t Unit(float v) { return (uint8_t)std::clamp((int)std::lround(v * 255.0f), 0, 255); }
// BGRA8 from a row of the read-back; false: a format not read.
bool ToBgra(DXGI_FORMAT f, const uint8_t* src, uint8_t* dst, uint32_t w) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        for (uint32_t x = 0; x < w; ++x) { dst[4 * x] = src[4 * x + 2]; dst[4 * x + 1] = src[4 * x + 1]; dst[4 * x + 2] = src[4 * x]; dst[4 * x + 3] = 255; }
        return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        for (uint32_t x = 0; x < w; ++x) { memcpy(dst + 4 * x, src + 4 * x, 3); dst[4 * x + 3] = 255; }
        return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t p;
            memcpy(&p, src + 4 * x, 4);
            dst[4 * x + 2] = (uint8_t)((p & 1023) >> 2);
            dst[4 * x + 1] = (uint8_t)(((p >> 10) & 1023) >> 2);
            dst[4 * x] = (uint8_t)(((p >> 20) & 1023) >> 2);
            dst[4 * x + 3] = 255;
        }
        return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        for (uint32_t x = 0; x < w; ++x) {
            uint16_t c[4];
            memcpy(c, src + 8 * x, 8);
            dst[4 * x + 2] = Unit(Half(c[0]));
            dst[4 * x + 1] = Unit(Half(c[1]));
            dst[4 * x] = Unit(Half(c[2]));
            dst[4 * x + 3] = 255;
        }
        return true;
    default: return false;
    }
}
float F11(uint32_t v) { const uint32_t e = (v >> 6) & 31, m = v & 63; return e == 0 ? std::ldexp((float)m, -20) : e == 31 ? 1e9f : std::ldexp((float)(m | 64), (int)e - 21); }
float F10(uint32_t v) { const uint32_t e = (v >> 5) & 31, m = v & 31; return e == 0 ? std::ldexp((float)m, -19) : e == 31 ? 1e9f : std::ldexp((float)(m | 32), (int)e - 20); }
// One value a texel for the one-channel formats (depth, AO, a mask): false when f is not one of them.
bool OneValue(DXGI_FORMAT f, const uint8_t* p, uint32_t x, float* v) {
    switch (f) {
    case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_TYPELESS: memcpy(v, p + 4 * x, 4); return true;
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: memcpy(v, p + 8 * x, 4); return true;
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: {
        uint32_t u; memcpy(&u, p + 4 * x, 4); *v = (float)(u & 0xFFFFFF) / 16777215.0f; return true;
    }
    case DXGI_FORMAT_R16_FLOAT: { uint16_t h; memcpy(&h, p + 2 * x, 2); *v = Half(h); return true; }
    case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: { uint16_t h; memcpy(&h, p + 2 * x, 2); *v = h / 65535.0f; return true; }
    case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_TYPELESS: *v = p[x] / 255.0f; return true;
    default: return false;
    }
}
// A whole image to BGRA8. *oneChannel: the values were spread over min..max (grey; the raw copy keeps them - *lo / *hi said).
bool Convert(DXGI_FORMAT f, const uint8_t* data, uint32_t pitch, uint32_t w, uint32_t h, std::vector<uint8_t>& out, bool* oneChannel, float* lo, float* hi) {
    out.assign((size_t)w * h * 4, 255);
    *oneChannel = false;
    bool ok = true;
    for (uint32_t y = 0; y < h && ok; ++y) ok = ToBgra(f, data + (size_t)y * pitch, out.data() + (size_t)y * w * 4, w);
    if (ok) return true;
    if (f == DXGI_FORMAT_R11G11B10_FLOAT || f == DXGI_FORMAT_R16G16_FLOAT || f == DXGI_FORMAT_R8G8_UNORM || f == DXGI_FORMAT_R16G16_UNORM) {
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* r = data + (size_t)y * pitch;
            uint8_t* d = out.data() + (size_t)y * w * 4;
            for (uint32_t x = 0; x < w; ++x) {
                float c[3] = {0, 0, 0};
                if (f == DXGI_FORMAT_R11G11B10_FLOAT) { uint32_t u; memcpy(&u, r + 4 * x, 4); c[0] = F11(u & 2047); c[1] = F11((u >> 11) & 2047); c[2] = F10(u >> 22); }
                else if (f == DXGI_FORMAT_R16G16_FLOAT) { uint16_t q[2]; memcpy(q, r + 4 * x, 4); c[0] = Half(q[0]); c[1] = Half(q[1]); }
                else if (f == DXGI_FORMAT_R16G16_UNORM) { uint16_t q[2]; memcpy(q, r + 4 * x, 4); c[0] = q[0] / 65535.0f; c[1] = q[1] / 65535.0f; }
                else { c[0] = r[2 * x] / 255.0f; c[1] = r[2 * x + 1] / 255.0f; }
                d[4 * x + 2] = Unit(c[0]); d[4 * x + 1] = Unit(c[1]); d[4 * x] = Unit(c[2]); d[4 * x + 3] = 255;
            }
        }
        return true;
    }
    float v = 0;
    if (!OneValue(f, data, 0, &v)) return false;
    *oneChannel = true;
    float mn = 1e30f, mx = -1e30f;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            if (OneValue(f, data + (size_t)y * pitch, x, &v) && std::isfinite(v)) { mn = std::min(mn, v); mx = std::max(mx, v); }
    if (!(mx >= mn)) { mn = 0; mx = 1; }
    const float k = mx > mn ? 1.0f / (mx - mn) : 0.0f;
    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* d = out.data() + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; ++x) {
            OneValue(f, data + (size_t)y * pitch, x, &v);
            const uint8_t g = Unit(std::isfinite(v) ? (v - mn) * k : 0.0f);
            d[4 * x] = d[4 * x + 1] = d[4 * x + 2] = g;
            d[4 * x + 3] = 255;
        }
    }
    *lo = mn;
    *hi = mx;
    return true;
}
// The bytes as read back, behind a 24-byte header ("D2RVRRAW", width, height, row pitch, DXGI format) - for values a PNG cannot keep.
bool WriteRaw(const std::wstring& path, const uint8_t* data, uint32_t pitch, uint32_t w, uint32_t h, DXGI_FORMAT f) {
    FILE* o = nullptr;
    if (_wfopen_s(&o, path.c_str(), L"wb") || !o) return false;
    const uint32_t head[4] = {w, h, pitch, (uint32_t)f};
    bool ok = fwrite("D2RVRRAW", 1, 8, o) == 8 && fwrite(head, 4, 4, o) == 4 && fwrite(data, 1, (size_t)pitch * h, o) == (size_t)pitch * h;
    fclose(o);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}
bool WritePng(const std::wstring& path, const std::vector<uint8_t>& bgra, uint32_t w, uint32_t h) {
    IWICImagingFactory* wic = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) || !wic) return false;
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
    wic->Release();
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}
// (the writer) the copy waited for and written; the line for the index
std::string WritePicture() {
    if (!g_picAsked) return "backbuffer.png - NOT written: no present in the window";
    if (!g_rb || !g_fence) return "backbuffer.png - NOT written: the copy could not be made (see below)";
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    bool done = g_fence->GetCompletedValue() >= 1;
    if (!done && ev && SUCCEEDED(g_fence->SetEventOnCompletion(1, ev))) done = WaitForSingleObject(ev, 5000) == WAIT_OBJECT_0;
    if (ev) CloseHandle(ev);
    if (!done) return "backbuffer.png - NOT written: the graphics card did not finish the copy in 5 s (the buffer is left, not freed)";
    const uint32_t w = g_fp.Footprint.Width, h = g_rows;
    std::string line;
    void* p = nullptr;
    const D3D12_RANGE all{0, (SIZE_T)g_fp.Footprint.RowPitch * h};
    if (SUCCEEDED(g_rb->Map(0, &all, &p)) && p) {
        std::vector<uint8_t> px((size_t)w * h * 4);
        bool read = true;
        for (uint32_t y = 0; y < h && read; ++y) read = ToBgra(g_fmt, (const uint8_t*)p + (size_t)y * g_fp.Footprint.RowPitch, px.data() + (size_t)y * w * 4, w);
        const D3D12_RANGE none{0, 0};
        g_rb->Unmap(0, &none);
        char b[200];
        if (!read) snprintf(b, sizeof b, "backbuffer.png - NOT written: format %d is not read here", (int)g_fmt);
        else if (WritePng(g_dir + L"\\backbuffer.png", px, w, h))
            snprintf(b, sizeof b, "backbuffer.png - the picture as presented, %ux%u (format %d%s)", w, h, (int)g_fmt,
                     g_fmt == DXGI_FORMAT_R16G16B16A16_FLOAT ? ", linear values clamped, not tone-mapped" : "");
        else snprintf(b, sizeof b, "backbuffer.png - NOT written: the PNG encoder failed");
        line = b;
    } else {
        line = "backbuffer.png - NOT written: the read-back could not be mapped";
    }
    g_rb->Release();
    g_rb = nullptr;
    g_fence->Release();
    g_fence = nullptr;
    return line;
}

// ---- the text files -----------------------------------------------------------------------------------------------------------------------
void Rows(FILE* f, const float* v, size_t rows, const char* name, int annotateFrom, int annotateTo, const char* annotation) {
    for (size_t r = 0; r < rows; ++r) {
        fprintf(f, "    %s[%3zu] +0x%03zX  % 14.7g % 14.7g % 14.7g % 14.7g", name, r, r * 16, v[4 * r], v[4 * r + 1], v[4 * r + 2], v[4 * r + 3]);
        if ((int)r >= annotateFrom && (int)r <= annotateTo) fprintf(f, "   <- %s row %d", annotation, (int)r - annotateFrom);
        if (std::string(name) == "frame" && r == 53) fprintf(f, "   <- b10[53]: the linear depth range {range, 1/range, near, -}");
        fprintf(f, "\n");
    }
}
void Mat(FILE* f, const char* what, const float* m) {
    fprintf(f, "    %s:\n", what);
    for (int r = 0; r < 4; ++r) fprintf(f, "      % 12.6f % 12.6f % 12.6f % 12.6f\n", m[4 * r], m[4 * r + 1], m[4 * r + 2], m[4 * r + 3]);
}
std::string WriteCamera() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, (g_dir + L"\\camera.txt").c_str(), L"w") || !f) return "camera.txt - NOT written";
    fprintf(f, "# D2R VR capture - the game's camera and frame constants as its frame builder (0xE3B590) filled them, in float4 rows\n");
    fprintf(f, "# camera block: renderer+0x11A08 -> its projection at +0xD0 (rows 13-16; [8] / [9] = the off-axis terms; reversed-Z infinite: [10] 0, [11] -1,\n");
    fprintf(f, "#   [14] the near plane). The light-bin notes' CB3[17] / [18] are rows 17 / 18 if this block is the CB3 the passes bind.\n");
    fprintf(f, "# frame block: renderer+0x11BB8 -> word 53 (+0x350) the linear depth range, after [render] depth_fix_mode 2 wrote it\n\n");
    for (size_t i = 0; i < g_builds.size(); ++i) {
        const Build& b = g_builds[i];
        fprintf(f, "build %zu: +%llu ms, eye %d, renderer %p, camera %p, camera block %p%s, frame block %p%s\n", i, b.ms, b.eye, (void*)b.renderer,
                (void*)b.camera, (void*)b.camConst, b.camOk ? "" : " (NOT read)", (void*)b.frameConst, b.frameOk ? "" : " (NOT read)");
        if (b.camOk) Rows(f, b.cam, kCamBytes / 16, "cam", 13, 16, "projection");
        if (b.frameOk) Rows(f, b.frame, kFrameBytes / 16, "frame", -1, -2, "");
        fprintf(f, "\n");
    }
    if (g_builds.empty()) fprintf(f, "no build of the frame seen: its hook goes in with [render] depth_fix_mode 2 (on at some point since the game's start)\n\n");
    fprintf(f, "# the camera hooks (cleanroom/camera/d2rcam.h): the game's own view / projection this frame, and ours when ours replaced it\n\n");
    for (size_t i = 0; i < g_views.size(); ++i) {
        const View& v = g_views[i];
        fprintf(f, "%s %zu: +%llu ms, eye %d, viewport %.0fx%.0f, look-at (the hero) %s %.3f %.3f %.3f, ours %s\n", v.proj ? "projection" : "view", i, v.ms,
                v.eye, v.vpW, v.vpH, v.lookOk ? "" : "(not valid)", v.look[0], v.look[1], v.look[2], v.ours ? "yes" : "no (the game's kept)");
        Mat(f, v.proj ? "the game's projection" : "the game's view", v.game);
        if (v.ours) Mat(f, v.proj ? "our projection" : "our view", v.out);
        if (v.proj && v.ours) fprintf(f, "    our projection's off-axis terms [8] %.6f [9] %.6f\n", v.out[8], v.out[9]);
    }
    fclose(f);
    char b[160];
    snprintf(b, sizeof b, "camera.txt - %zu frame builds (camera and frame constants), %zu camera hook calls", g_builds.size(), g_views.size());
    return b;
}
std::string WriteState() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, (g_dir + L"\\state.txt").c_str(), L"w") || !f) return "state.txt - NOT written";
    fprintf(f, "# D2R VR capture - the state at the press\nplugin %s\n%s\n", g_version.c_str(), g_state.c_str());
    int v[6];
    patchhunt::GameValues(v);
    fprintf(f, "the game's graphics options (-1 not known): Ambient Occlusion Quality %d, Shadow Quality %d, Light Quality %d, Anti Aliasing %d, "
               "NVIDIA DLSS %d, Resolution Scale %d\n", v[0], v[1], v[2], v[3], v[4], v[5]);
    fprintf(f, "patch_hunt step %d\n", patchhunt::Step());
    if (g_presentMs.size() >= 2) {
        const double span = g_presentMs.back() - g_presentMs.front();
        double worst = 0.0;
        for (size_t i = 1; i < g_presentMs.size(); ++i) worst = std::max(worst, g_presentMs[i] - g_presentMs[i - 1]);
        fprintf(f, "presents in the window: %zu over %.0f ms = %.1f a second, the longest gap %.1f ms\n", g_presentMs.size(), span,
                span > 0 ? 1000.0 * (g_presentMs.size() - 1) / span : 0.0, worst);
    } else {
        fprintf(f, "presents in the window: %zu\n", g_presentMs.size());
    }
    fclose(f);
    return "state.txt - the view, platform, area, the game's options, fps, our camera's numbers";
}
bool CopyIn(const std::wstring& from, const wchar_t* name) { return CopyFileW(from.c_str(), (g_dir + L"\\" + name).c_str(), FALSE) != 0; }

void KeepNewest() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_logs + L"capture_*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> names;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (const std::wstring& n : capturerule::ToDelete(names)) {   // (only capture_YYYYMMDD-HHMMSS folders; only the files in them)
        const std::wstring dir = g_logs + n;
        HANDLE g = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (g != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + L"\\" + fd.cFileName).c_str());
            } while (FindNextFileW(g, &fd));
            FindClose(g);
        }
        RemoveDirectoryW(dir.c_str());
    }
}

std::string Narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += c < 128 ? (char)c : '?';
    return s;
}
size_t CountFiles(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    size_t n = 0;
    do n += !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}
const char* StateName(D3D12_RESOURCE_STATES s) {
    return s == D3D12_RESOURCE_STATE_RENDER_TARGET ? "rt" : s == D3D12_RESOURCE_STATE_DEPTH_WRITE ? "depth" : s == D3D12_RESOURCE_STATE_UNORDERED_ACCESS ? "uav" : "other";
}
// (the writer) the intermediate targets: the fence waited for, each read back to a PNG (and the raw bytes for one-channel values), targets.txt.
std::string WriteTargets() {
    char b[240];
    if (g_tgtPhase.load() != kTgtSignalled || !g_tgtFence) {
        snprintf(b, sizeof b, "targets - NOT written: the frame's lists were not seen through (%zu copies asked)", g_tgts.size());
        for (auto& t : g_tgts) t.rb = nullptr;   // (left, not freed: the GPU may still write them)
        g_tgts.clear();
        g_tgtPhase.store(kTgtOff);
        return b;
    }
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    bool done = g_tgtFence->GetCompletedValue() >= 1;
    if (!done && ev && SUCCEEDED(g_tgtFence->SetEventOnCompletion(1, ev))) done = WaitForSingleObject(ev, 5000) == WAIT_OBJECT_0;
    if (ev) CloseHandle(ev);
    if (!done) {
        g_tgts.clear();
        g_tgtPhase.store(kTgtOff);
        return "targets - NOT written: the graphics card did not finish the copies in 5 s";
    }
    FILE* f = nullptr;
    _wfopen_s(&f, (g_dir + L"\\targets.txt").c_str(), L"w");
    if (f) fprintf(f, "# D2R VR capture - the game's render targets, depth and UAV textures of one frame, each copied as its pass ended (moved out of\n"
                      "# a writing state), in that order. One-channel values (depth, AO) are spread over min..max in the PNG; the .raw keeps them\n"
                      "# (24-byte header: \"D2RVRRAW\", width, height, row pitch, DXGI format; then the rows).\n\n");
    size_t pngs = 0;
    for (size_t i = 0; i < g_tgts.size(); ++i) {
        Target& t = g_tgts[i];
        const uint32_t w = t.fp.Footprint.Width, h = t.rows;
        wchar_t base[96];
        swprintf_s(base, L"\\t%02zu_%hs_%dfmt_%ux%u", i, StateName(t.from), (int)t.desc.Format, w, h);
        void* p = nullptr;
        const D3D12_RANGE all{0, (SIZE_T)t.fp.Footprint.RowPitch * h};
        std::string what = "NOT read back";
        if (SUCCEEDED(t.rb->Map(0, &all, &p)) && p) {
            std::vector<uint8_t> px;
            bool one = false;
            float lo = 0, hi = 0;
            const uint8_t* data = (const uint8_t*)p;
            if (Convert(t.desc.Format, data, t.fp.Footprint.RowPitch, w, h, px, &one, &lo, &hi) && WritePng(g_dir + base + L".png", px, w, h)) {
                ++pngs;
                char s[96];
                if (one) snprintf(s, sizeof s, "png (values %.6g .. %.6g spread to grey)", lo, hi);
                else snprintf(s, sizeof s, "png");
                what = s;
                if (one) what += WriteRaw(g_dir + base + L".raw", data, t.fp.Footprint.RowPitch, w, h, t.desc.Format) ? " + raw" : " (raw NOT written)";
            } else {
                what = WriteRaw(g_dir + base + L".raw", data, t.fp.Footprint.RowPitch, w, h, t.desc.Format) ? "raw only (a format the PNG does not take)" : "NOT written";
            }
            const D3D12_RANGE none{0, 0};
            t.rb->Unmap(0, &none);
        }
        if (f) fprintf(f, "t%02zu  %-5s -> state 0x%X  format %d  %ux%u (mips %u, array %u)  %s list  %s\n", i, StateName(t.from), (unsigned)t.to,
                       (int)t.desc.Format, w, h, t.desc.MipLevels, t.desc.DepthOrArraySize,
                       t.list == D3D12_COMMAND_LIST_TYPE_COMPUTE ? "compute" : "direct", what.c_str());
        t.rb->Release();
        t.rb = nullptr;
    }
    if (f) {
        if (g_tgtFull) fprintf(f, "\n(more were written in that frame: the first %zu kept, at most %llu MB)\n", kMaxTargets, (unsigned long long)(kTargetBytes >> 20));
        fclose(f);
    }
    const size_t n = g_tgts.size();
    g_tgts.clear();
    g_tgtFence->Release();
    g_tgtFence = nullptr;
    g_tgtPhase.store(kTgtOff);
    snprintf(b, sizeof b, "targets.txt + t*.png - %zu intermediate targets of one frame (%zu as PNG)%s", n, pngs, g_tgtFull ? ", more not kept" : "");
    return b;
}
// The console's answer on the UI thread (where its commands run).
void __cdecl ReplyRun(const D2RL::PluginContext* ctx, void*) noexcept {
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> l(g_replyLock);
        lines.swap(g_reply);
    }
    for (const auto& s : lines) ctx->WriteConsoleMessage(s.c_str());
}
void QueueReply() {
    {
        std::lock_guard<std::mutex> l(g_replyLock);
        if (g_reply.empty()) return;
    }
    if (g_ctx && g_threads && g_threads->runOnUiThread) g_threads->runOnUiThread(g_ctx, &ReplyRun, nullptr);
}

// The writer: the window closed, everything written.
void Writer() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        Sleep(50);
        std::lock_guard<std::mutex> l(g_lock);
        const ULONGLONG age = GetTickCount64() - g_startMs;
        const bool window = age >= kWindowMs || g_presentMs.size() >= kPresents;
        const bool targets = !g_wantTargets || g_tgtPhase.load() == kTgtSignalled || age >= 4000;   // (they need three presents)
        if (window && targets) { g_collecting.store(false); break; }
    }
    // the pipe trace is written at the present after the one it started at: a little longer for it
    std::wstring tracePath;
    bool traceOk = false;
    for (int i = 0; i < 40; ++i) {
        traceOk = pipehunt::LastWritten(&tracePath) && tracePath == g_dir + L"\\pipetrace.txt";
        if (traceOk || !pipehunt::State().recording) break;
        Sleep(50);
    }
    std::vector<std::string> lines;
    std::lock_guard<std::mutex> l(g_lock);   // (the taps are closed; nothing else takes it now)
    lines.push_back(traceOk ? "pipetrace.txt - one frame's pipelines: shaders' hashes, draws, render targets and formats, depth, viewport"
                            : "pipetrace.txt - NOT written (a trace already under way, or no present after the press)");
    lines.push_back(WritePicture());
    if (g_wantTargets) lines.push_back(WriteTargets());
    lines.push_back(WriteCamera());
    lines.push_back(WriteState());
    lines.push_back(CopyIn(g_ini, L"d2r_vr.ini") ? "d2r_vr.ini - the mod's settings as they are" : "d2r_vr.ini - NOT copied");
    {
        wchar_t* saved = nullptr;
        std::wstring json;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &saved)) && saved) json = std::wstring(saved) + L"\\Diablo II Resurrected\\Settings.json";
        CoTaskMemFree(saved);
        lines.push_back(!json.empty() && CopyIn(json, L"Settings.json") ? "Settings.json - the game's settings file (written by the game at its exit: "
                                                                          "the options as they were at the last exit, state.txt has them now)"
                                                                        : "Settings.json - NOT copied");
    }
    if (headshot::On()) lines.push_back("headset_shot_*.png in the logs folder - the headset's pictures of the same moment ([headset_shot] on)");
    FILE* f = nullptr;
    if (!_wfopen_s(&f, (g_dir + L"\\capture.txt").c_str(), L"w") && f) {
        fprintf(f, "D2R VR capture (plugin %s) - for a picture fault seen at the press; send this folder (Collect logs for support packs the newest two)\n\n", g_version.c_str());
        for (const auto& s : lines) fprintf(f, "%s\n", s.c_str());
        if (!g_notes.empty()) fprintf(f, "\nalong the way:\n%s", g_notes.c_str());
        fclose(f);
    }
    KeepNewest();
    char dir[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, g_dir.c_str(), -1, dir, sizeof dir, nullptr, nullptr);
    size_t failed = 0;
    for (const auto& s : lines) failed += s.find(" NOT ") != std::string::npos;
    const size_t files = CountFiles(g_dir);
    const std::string name = Narrow(g_dir.substr(g_dir.find_last_of(L'\\') + 1));
    char head[300];
    if (failed) snprintf(head, sizeof head, "D2R VR capture saved: %s (%zu files, %zu part%s failed - see capture.txt)", name.c_str(), files, failed, failed == 1 ? "" : "s");
    else snprintf(head, sizeof head, "D2R VR capture saved: %s (%zu files)", name.c_str(), files);
    LogF("vrcam: capture - saved to %s (%zu files; %zu builds, %zu camera calls, %zu presents%s%s)", dir, files, g_builds.size(), g_views.size(),
         g_presentMs.size(), g_wantTargets ? ", targets" : "", failed || !g_notes.empty() ? "; something failed, capture.txt says what" : "");
    banner::Show(head, failed == 0, GetTickCount64(), 3000);
    {
        std::lock_guard<std::mutex> r(g_replyLock);
        g_lastName = name;
        g_lastSummary = head;
        if (g_replyWanted) {
            g_reply.push_back(head);
            for (const auto& s : lines) g_reply.push_back("  " + s.substr(0, 180));
            size_t at = 0;
            while (at < g_notes.size()) {   // what failed along the way, a line each
                const size_t nl = g_notes.find('\n', at);
                g_reply.push_back(g_notes.substr(at, std::min<size_t>(180, (nl == std::string::npos ? g_notes.size() : nl) - at)));
                if (nl == std::string::npos) break;
                at = nl + 1;
            }
            g_replyWanted = false;
        }
    }
    QueueReply();
    CoUninitialize();
    g_busy.store(false);
}

// A capture now (by: "the key", "the console"). "" = started (*name its folder); else why not - said on the picture and in the log too.
std::string Start(bool targets, const char* by, bool console, std::string* name) {
    char why[200];
    const ULONGLONG t = GetTickCount64();
    if (g_busy.load()) snprintf(why, sizeof why, "D2R VR capture NOT taken: the last one is still being written");
    else if (!capturerule::MayCapture(t, g_lastPress.load())) snprintf(why, sizeof why, "D2R VR capture NOT taken: one a 3 s at most - press again in a moment");
    else why[0] = 0;
    if (why[0]) {
        LogF("vrcam: capture - %s (%s)", why, by);
        banner::Show(why, false, t, 3000);
        return why;
    }
    g_lastPress.store(t);
    g_busy.store(true);
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::lock_guard<std::mutex> l(g_lock);
    g_dir = g_logs + capturerule::FolderName(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    CreateDirectoryW(g_logs.c_str(), nullptr);
    if (!CreateDirectoryW(g_dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        snprintf(why, sizeof why, "D2R VR capture FAILED: its folder could not be made in the logs folder (error %lu)", GetLastError());
        LogF("vrcam: capture - %s", why);
        banner::Show(why, false, t, 3000);
        g_busy.store(false);
        return why;
    }
    if (name) *name = Narrow(g_dir.substr(g_dir.find_last_of(L'\\') + 1));
    {
        std::lock_guard<std::mutex> r(g_replyLock);
        g_replyWanted = console;
    }
    g_wantTargets = targets;
    g_tgts.clear();
    g_tgtSeen.clear();
    g_tgtBytes = 0;
    g_tgtFull = false;
    g_tgtPhase.store(targets ? kTgtArmed : kTgtOff);
    g_builds.clear();
    g_views.clear();
    g_presentMs.clear();
    g_notes.clear();
    g_picAsked = false;
    g_startMs = GetTickCount64();
    g_state = g_stateText ? g_stateText() : std::string("(no state source)");
    if (!pipehunt::Arm(g_dir + L"\\pipetrace.txt")) Note("a pipe trace was already under way (patch_hunt 8 / Ctrl+F10): pipetrace.txt left out");
    if (headshot::On() && !headshot::Request("the capture key")) Note("[headset_shot]: one is still being written - no headset pictures this time");
    g_collecting.store(true);
    LogF("vrcam: capture - started by %s%s", by, targets ? ", with the intermediate targets" : "");
    banner::Show(targets ? "D2R VR capture: recording (with the targets)..." : "D2R VR capture: recording...", true, t, 1500);
    std::thread(Writer).detach();
    return "";
}

// "vrcapture", "vrcapture targets", "vrcapture status" in the game's console.
D2RL::ConsoleCommandResult __cdecl CmdCapture(D2R::Game::Client*, const D2RL::ConsoleCommandContext* c, void*) noexcept {
    if (!g_ctx) return D2RL::ConsoleCommandResult::Failed;
    std::string a = c && c->args ? std::string(c->args, c->argsLength) : std::string();
    while (!a.empty() && a.front() == ' ') a.erase(a.begin());
    while (!a.empty() && a.back() == ' ') a.pop_back();
    for (auto& ch : a) ch = (char)tolower((unsigned char)ch);
    if (a == "status") {
        capturerule::Key k;
        k.vk = g_vk.load();
        const int m = g_mods.load();
        k.ctrl = (m & 1) != 0; k.alt = (m & 2) != 0; k.shift = (m & 4) != 0;
        char b[400];
        snprintf(b, sizeof b, "D2R VR capture: key %s ([debug] capture_key), the key takes the targets: %s ([debug] capture_targets)", capturerule::Name(k).c_str(),
                 g_keyTargets.load() ? "yes" : "no");
        g_ctx->WriteConsoleMessage(b);
        snprintf(b, sizeof b, "  folder: %s (the newest %zu kept; Collect logs packs the newest two)", Narrow(g_logs).c_str(), capturerule::kKept);
        g_ctx->WriteConsoleMessage(b);
        std::string last;
        {
            std::lock_guard<std::mutex> l(g_replyLock);
            last = g_lastSummary;
        }
        g_ctx->WriteConsoleMessage(g_busy.load() ? "  a capture is being written now" : last.empty() ? "  no capture this session" : ("  last: " + last).c_str());
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (!a.empty() && a != "targets") {
        g_ctx->WriteConsoleMessage("usage: vrcapture  |  vrcapture targets (with the game's intermediate render targets)  |  vrcapture status");
        return D2RL::ConsoleCommandResult::Handled;
    }
    std::string name;
    const std::string why = Start(a == "targets", "the console", true, &name);
    if (!why.empty()) g_ctx->WriteConsoleMessage(why.c_str());
    else g_ctx->WriteConsoleMessage(("D2R VR capture: recording into " + name + (a == "targets" ? " with the intermediate targets" : "") + " - about a second...").c_str());
    return D2RL::ConsoleCommandResult::Handled;
}

}  // namespace

void SetSources(std::string (*state)(), int (*eye)()) {
    g_stateText = state;
    g_eyeNow = eye;
}

void Init(void (*log)(const char*), const wchar_t* iniPath, const char* version) {
    g_log = log;
    g_ini = iniPath ? iniPath : L"";
    g_version = version ? version : "?";
    std::wstring dir = g_ini;
    dir.resize(dir.find_last_of(L"\\/") + 1);
    dir += L"..\\logs\\";
    wchar_t full[MAX_PATH];
    if (GetFullPathNameW(dir.c_str(), MAX_PATH, full, nullptr)) dir = full;
    g_logs = dir;
}

void InitConsole(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    if (!ctx) return;
    const D2RL::ThreadService* t = nullptr;
    g_threads = ctx->QueryService(&t) == D2RL::ServiceQueryResult::Success ? t : nullptr;
    if (!ctx->RegisterConsoleCommand("vrcapture", &CmdCapture, "vrcapture [targets | status] - D2R VR: save what a picture fault needs (logs\\capture_*)"))
        ctx->LogError("vrcam: the console command vrcapture did not register");
}

void Configure(const std::wstring& keySpec, bool targets) {
    g_keyTargets.store(targets);
    capturerule::Key k;
    const bool ok = capturerule::Parse(keySpec, &k);
    if (!ok) k = capturerule::Default();
    const int mods = (k.ctrl ? 1 : 0) | (k.alt ? 2 : 0) | (k.shift ? 4 : 0);
    if (g_vk.exchange(k.vk) != k.vk || g_mods.exchange(mods) != mods || !ok)
        LogF("vrcam: capture - key %s%s", capturerule::Name(k).c_str(), ok ? "" : " ([debug] capture_key not understood: the default)");
}

void KeyTick(bool focus) {
    static bool was = false;
    const unsigned vk = g_vk.load(std::memory_order_relaxed);
    if (!vk) return;
    const bool now = focus && (GetAsyncKeyState((int)vk) & 0x8000) != 0;
    const bool edge = now && !was;
    was = now;
    if (!edge) return;
    capturerule::Key k;
    k.vk = vk;
    const int m = g_mods.load();
    k.ctrl = (m & 1) != 0; k.alt = (m & 2) != 0; k.shift = (m & 4) != 0;
    if (!capturerule::Matches(k, (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0, (GetAsyncKeyState(VK_MENU) & 0x8000) != 0,
                              (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0))
        return;
    Start(g_keyTargets.load(), "the key", false, nullptr);
}

bool Collecting() { return g_collecting.load(std::memory_order_relaxed); }

void OnFrameBuilt(void* renderer, void* camera) {
    if (!g_collecting.load(std::memory_order_relaxed) || !renderer) return;
    std::lock_guard<std::mutex> l(g_lock);
    if (!g_collecting.load() || g_builds.size() >= kBuilds) return;
    Build b;
    b.ms = GetTickCount64() - g_startMs;
    b.eye = g_eyeNow ? g_eyeNow() : 0;
    b.renderer = (uintptr_t)renderer;
    b.camera = (uintptr_t)camera;
    const auto* r = static_cast<const uint8_t*>(renderer);
    if (Peek(&b.camConst, (uintptr_t)(r + 0x11A08), sizeof b.camConst) && b.camConst > 0x10000) b.camOk = Peek(b.cam, b.camConst, kCamBytes);
    if (Peek(&b.frameConst, (uintptr_t)(r + 0x11BB8), sizeof b.frameConst) && b.frameConst > 0x10000) b.frameOk = Peek(b.frame, b.frameConst, kFrameBytes);
    g_builds.push_back(b);
}

static void Keep(const d2rcam::WorldView& in, const float out[16], bool ours, int eye, bool proj) {
    std::lock_guard<std::mutex> l(g_lock);
    if (!g_collecting.load() || g_views.size() >= kViews) return;
    View v;
    v.ms = GetTickCount64() - g_startMs;
    v.eye = eye;
    v.proj = proj;
    v.ours = ours;
    memcpy(v.game, proj ? in.gameProj : in.gameView, sizeof v.game);
    if (out) memcpy(v.out, out, sizeof v.out);
    memcpy(v.look, in.lookAt, sizeof v.look);
    v.lookOk = in.lookAtValid;
    v.vpW = in.viewportW;
    v.vpH = in.viewportH;
    g_views.push_back(v);
}
void OnView(const d2rcam::WorldView& in, const float out[16], bool ours, int eye) {
    if (g_collecting.load(std::memory_order_relaxed)) Keep(in, out, ours, eye, false);
}
void OnProj(const d2rcam::WorldView& in, const float out[16], bool ours, int eye) {
    if (g_collecting.load(std::memory_order_relaxed)) Keep(in, out, ours, eye, true);
}

void OnPresent(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12Resource* bb) {
    if (!g_collecting.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> l(g_lock);
    if (!g_collecting.load()) return;
    if (g_presentMs.size() < kPresents * 2) g_presentMs.push_back(MsNow());
    switch (g_tgtPhase.load()) {   // the intermediate targets: one frame recorded, then one more for its lists to run, then the fence
    case kTgtArmed:
        if (dev && !g_tgtFence && FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_tgtFence)))) g_tgtFence = nullptr;
        g_tgtPhase.store(g_tgtFence ? kTgtRecording : kTgtOff);
        if (!g_tgtFence) Note("no fence for the targets' copies: none taken");
        break;
    case kTgtRecording: g_tgtPhase.store(kTgtClosing); break;
    case kTgtClosing:
        if (q && g_tgtFence) q->Signal(g_tgtFence, 1);
        g_tgtPhase.store(kTgtSignalled);
        break;
    default: break;
    }
    if (g_picAsked || !dev || !q || !bb) return;
    g_picAsked = true;
    const D3D12_RESOURCE_DESC d = bb->GetDesc();
    UINT64 rowBytes = 0, total = 0;
    dev->GetCopyableFootprints(&d, 0, 1, 0, &g_fp, &g_rows, &rowBytes, &total);
    g_fmt = d.Format;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (!total || FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g_rb)))) {
        Note("the back buffer's read-back buffer could not be made (%llu bytes)", (unsigned long long)total);
        g_rb = nullptr;
        return;
    }
    if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) {
        Note("no fence for the back buffer's copy");
        g_fence = nullptr;
        return;
    }
    ID3D12GraphicsCommandList* cl = natived3d::OpenList(dev);
    if (!cl) {
        Note("no list of ours for the back buffer's copy (no allocator free)");
        g_fence->Release();
        g_fence = nullptr;
        return;
    }
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = bb;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.pResource = g_rb;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = g_fp;
    src.pResource = bb;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    cl->ResourceBarrier(1, &b);
    natived3d::Submit(q);
    q->Signal(g_fence, 1);
}

void OnBarrier(ID3D12GraphicsCommandList* cl, unsigned n, const D3D12_RESOURCE_BARRIER* b) {
    if (g_tgtPhase.load(std::memory_order_relaxed) != kTgtRecording || !cl || !b) return;
    const D3D12_COMMAND_LIST_TYPE type = cl->GetType();
    if (type != D3D12_COMMAND_LIST_TYPE_DIRECT && type != D3D12_COMMAND_LIST_TYPE_COMPUTE) return;
    std::lock_guard<std::mutex> l(g_lock);
    if (g_tgtPhase.load() != kTgtRecording) return;
    for (unsigned i = 0; i < n; ++i) {
        const D3D12_RESOURCE_BARRIER& br = b[i];
        if (br.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION || br.Flags != D3D12_RESOURCE_BARRIER_FLAG_NONE) continue;
        const D3D12_RESOURCE_STATES from = br.Transition.StateBefore;
        if (from != D3D12_RESOURCE_STATE_RENDER_TARGET && from != D3D12_RESOURCE_STATE_DEPTH_WRITE && from != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) continue;
        if (type == D3D12_COMMAND_LIST_TYPE_COMPUTE && from != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) continue;   // (a compute list moves no target)
        if (br.Transition.Subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES && br.Transition.Subresource != 0) continue;
        ID3D12Resource* r = br.Transition.pResource;
        if (!r || std::find(g_tgtSeen.begin(), g_tgtSeen.end(), r) != g_tgtSeen.end()) continue;
        g_tgtSeen.push_back(r);
        const D3D12_RESOURCE_DESC d = r->GetDesc();
        if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.SampleDesc.Count != 1) continue;
        if (g_tgts.size() >= kMaxTargets) { g_tgtFull = true; continue; }
        ID3D12Device* dev = nullptr;
        if (FAILED(cl->GetDevice(IID_PPV_ARGS(&dev))) || !dev) continue;
        Target t;
        t.desc = d;
        t.from = from;
        t.to = br.Transition.StateAfter;
        t.list = type;
        UINT64 rowBytes = 0, total = 0;
        dev->GetCopyableFootprints(&d, 0, 1, 0, &t.fp, &t.rows, &rowBytes, &total);
        if (!total || g_tgtBytes + total > kTargetBytes) { g_tgtFull = true; dev->Release(); continue; }
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        const HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&t.rb));
        dev->Release();
        if (FAILED(hr) || !t.rb) continue;
        // on the game's list, before its own barrier: into copy source, the copy, back (the nested barrier calls pass our hook untold)
        D3D12_RESOURCE_BARRIER mine{};
        mine.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        mine.Transition.pResource = r;
        mine.Transition.Subresource = 0;
        mine.Transition.StateBefore = from;
        mine.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cl->ResourceBarrier(1, &mine);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = t.rb;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = t.fp;
        src.pResource = r;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        std::swap(mine.Transition.StateBefore, mine.Transition.StateAfter);
        cl->ResourceBarrier(1, &mine);
        g_tgtBytes += total;
        g_tgts.push_back(t);
    }
}

}  // namespace capture
