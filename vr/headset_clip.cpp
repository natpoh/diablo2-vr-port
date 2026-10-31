// [headset_shot] video: a 10-second video of what reaches the headset - see vr/headset_clip.h. The draw thread records a shrink per eye and
// a copy into a read-back ring; a thread of its own makes everything first, then encodes (Media Foundation, H.264) and writes the files.
// Media Foundation is found at run time: a Windows "N" without the Media Feature Pack has no mfplat.dll, and the plugin must load there.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "headset_clip.h"
#include "headset_shot_pure.h"

#pragma comment(lib, "mfuuid.lib")   // (the GUIDs only: no DLL)

namespace headclip {
namespace {

using namespace headshot;   // (the pure parts: names, sizes, formats, NV12)

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

std::wstring g_folder;
std::atomic<bool> g_on{false};
std::atomic<int> g_key{0};
int g_take = INT_MIN;
double g_seconds = 10.0;
constexpr double kFrameMs = 1000.0 / 30.0;   // about 30 frames a second kept
constexpr int kSlots = 6;                    // the read-back ring: 200 ms of frames in flight

// kArming: from the next frame on; kMeasuring: the first frame's picture size; kMaking: the thread makes the shader, the ring, the encoder;
// kReadying: from the next frame on; kRecording: 10 s; kFinishing: the thread encodes what is left and writes the files.
enum Phase { kIdle, kArming, kMeasuring, kMaking, kReadying, kRecording, kFinishing };
std::atomic<int> g_phase{kIdle};

double MsNow() {
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / f.QuadPart;
}

// -- the shader: one eye into its half, 4 samples a pixel, black bars where its shape leaves room; written B G R A (the encoder's order) --
const char kShader[] = R"(
Texture2D<float4> Src : register(t0);
RWTexture2D<float4> Dst : register(u0);
cbuffer K : register(b0) { uint4 SrcRect; uint4 Cell; uint4 Inner; uint Flags; };
float3 Srgb(float3 c) { c = saturate(c); return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }
float4 At(float2 p) {
    int2 q = int2(SrcRect.xy) + clamp(int2(p), int2(0, 0), int2(SrcRect.zw) - 1);
    return Src.Load(int3(q, 0));
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= Cell.z || id.y >= Cell.w) return;
    float3 c = float3(0, 0, 0);
    int2 d = int2(id.xy) - int2(Inner.xy);
    if (d.x >= 0 && d.y >= 0 && d.x < (int)Inner.z && d.y < (int)Inner.w) {
        float2 k = float2(SrcRect.zw) / float2(Inner.zw);
        float2 p = (float2(d) + 0.5) * k;
        float2 o = 0.25 * k;
        c = 0.25 * (At(p + float2(-o.x, -o.y)).rgb + At(p + float2(o.x, -o.y)).rgb + At(p + float2(-o.x, o.y)).rgb + At(p + float2(o.x, o.y)).rgb);
        if (Flags & 1) c = Srgb(c);
    }
    Dst[Cell.xy + id.xy] = float4(c.b, c.g, c.r, 1.0);
}
)";
struct Consts { uint32_t src[4], cell[4], inner[4], flags; };

// -- Media Foundation, found at run time ------------------------------------------------------------------------------------------------
struct Mf {
    HMODULE plat = nullptr, rw = nullptr;
    HRESULT(WINAPI* Startup)(ULONG, DWORD) = nullptr;
    HRESULT(WINAPI* Shutdown)() = nullptr;
    HRESULT(WINAPI* CreateMediaType)(IMFMediaType**) = nullptr;
    HRESULT(WINAPI* CreateAttributes)(IMFAttributes**, UINT32) = nullptr;
    HRESULT(WINAPI* CreateMemoryBuffer)(DWORD, IMFMediaBuffer**) = nullptr;
    HRESULT(WINAPI* CreateSample)(IMFSample**) = nullptr;
    HRESULT(WINAPI* CreateSinkWriterFromURL)(LPCWSTR, IMFByteStream*, IMFAttributes*, IMFSinkWriter**) = nullptr;
    bool Load() {
        plat = LoadLibraryW(L"mfplat.dll");
        rw = LoadLibraryW(L"mfreadwrite.dll");
        if (!plat || !rw) return false;
        Startup = (decltype(Startup))GetProcAddress(plat, "MFStartup");
        Shutdown = (decltype(Shutdown))GetProcAddress(plat, "MFShutdown");
        CreateMediaType = (decltype(CreateMediaType))GetProcAddress(plat, "MFCreateMediaType");
        CreateAttributes = (decltype(CreateAttributes))GetProcAddress(plat, "MFCreateAttributes");
        CreateMemoryBuffer = (decltype(CreateMemoryBuffer))GetProcAddress(plat, "MFCreateMemoryBuffer");
        CreateSample = (decltype(CreateSample))GetProcAddress(plat, "MFCreateSample");
        CreateSinkWriterFromURL = (decltype(CreateSinkWriterFromURL))GetProcAddress(rw, "MFCreateSinkWriterFromURL");
        return Startup && Shutdown && CreateMediaType && CreateAttributes && CreateMemoryBuffer && CreateSample && CreateSinkWriterFromURL;
    }
    void Free() {
        if (rw) FreeLibrary(rw);
        if (plat) FreeLibrary(plat);
        *this = Mf{};
    }
};

// -- one clip ---------------------------------------------------------------------------------------------------------------------------
struct Slot {
    ID3D12CommandAllocator* alloc[3] = {};   // the left half, the right half (and a flat picture's both), the copy out
    ID3D12GraphicsCommandList* list[3] = {};
    ID3D12Resource* buf = nullptr;           // read-back
    std::atomic<bool> busy{false};           // its copy queued and not encoded yet
    UINT64 fenceV = 0;
    uint32_t timed = 0;                      // the GPU timestamps of this frame written: bit 0 the left half's pair, 1 the right's, 2 the copy's
};
struct Item { int slot; double tMs; int kept; };
struct Row { double tMs; std::string kind, eyes; double us; int kept; };   // kept: its number among the frames kept (-1: left out)
constexpr int kKeptMax = 4096;   // (the GPU times of the frames kept: 10 s at 120 Hz is far less)
struct Clip {
    std::string stamp, by;
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* queue = nullptr;   // the last eye's: the copy goes on it
    uint32_t srcW = 0, srcH = 0;           // the first picture's (the clip's shape)
    bool deny = false;                     // ... it refused a shader view: each eye copied into our own texture first
    DXGI_FORMAT srcFormat = DXGI_FORMAT_UNKNOWN;
    uint32_t halfW = 0, h = 0;
    ID3D12RootSignature* root = nullptr;
    ID3D12PipelineState* pso = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    UINT inc = 0;
    ID3D12Resource* sbs = nullptr;         // both eyes side by side (UNORDERED_ACCESS between copies)
    ID3D12Resource* inter[2] = {};         // the eyes' own copies (deny only)
    Slot slots[kSlots];
    ID3D12Fence* fence = nullptr;
    UINT64 fenceV = 0;
    ID3D12QueryHeap* stamps = nullptr;     // the GPU's own clock around each shrink and copy: 6 a slot
    ID3D12Resource* stampBuf = nullptr;    // ... resolved into this (read-back)
    UINT64 stampHz = 0;
    std::vector<float> gpuUs;              // each frame kept: its GPU time, the thread's (kKeptMax)
    double gpuSum = 0.0, gpuMax = 0.0;
    int gpuN = 0;
    Mf mf;
    IMFSinkWriter* writer = nullptr;
    DWORD stream = 0;
    // the draw thread's
    int frames = 0, measureFrames = 0, cur = 0;
    bool take = false;                     // this frame is kept
    bool used[2] = {};                     // ... and its halves recorded
    std::string eyes;
    double t0 = 0.0, lastTake = -1e9, lastFrame = 0.0, frameGapSum = 0.0, frameGapMax = 0.0, us = 0.0, usSum = 0.0, usMax = 0.0;
    int gaps = 0, kept = 0, dropped = 0;
    std::vector<Row> rows;
    std::string notes;
};
Clip* g_clip = nullptr;   // (the draw thread and the clip's thread; made by Request, gone at the clip's end)
std::mutex g_lock;
std::condition_variable g_wake;
std::deque<Item> g_items;
std::vector<std::wstring> g_lastFiles;
std::thread g_worker;

D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}
template <class T> void Rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Everything the recording needs, on the clip's thread before it starts. False: what failed is in c.notes.
bool Make(Clip& c) {
    ID3D12Device* dev = c.dev;
    ClipSize(c.srcW, c.srcH, &c.halfW, &c.h);
    HMODULE comp = LoadLibraryW(L"d3dcompiler_47.dll");
    const pD3DCompile compile = comp ? (pD3DCompile)GetProcAddress(comp, "D3DCompile") : nullptr;
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    const PFN_D3D12_SERIALIZE_ROOT_SIGNATURE serialize = d12 ? (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
    if (!compile || !serialize) { c.notes = "no d3dcompiler_47.dll / D3D12SerializeRootSignature"; return false; }
    ID3DBlob *cs = nullptr, *err = nullptr, *sig = nullptr;
    bool ok = SUCCEEDED(compile(kShader, sizeof kShader - 1, "headset_clip", nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &cs, &err));
    Rel(err);
    D3D12_DESCRIPTOR_RANGE ranges[2] = {{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0}, {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 1}};
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants = {0, 0, sizeof(Consts) / 4};
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable = {2, ranges};
    D3D12_ROOT_SIGNATURE_DESC rd{2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ok = ok && SUCCEEDED(serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)) &&
         SUCCEEDED(dev->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&c.root));
    Rel(err);
    Rel(sig);
    if (ok) {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = c.root;
        pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
        ok = SUCCEEDED(dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&c.pso));
    }
    Rel(cs);
    if (!ok) { c.notes = "its shader could not be made"; return false; }
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4 * kSlots, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = 2 * c.halfW; td.Height = c.h; td.DepthOrArraySize = 1; td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&c.heap)) ||
        FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, __uuidof(ID3D12Resource),
                                            (void**)&c.sbs)) ||
        FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&c.fence))) {
        c.notes = "its textures could not be made";
        return false;
    }
    c.inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (int k = 0; k < kSlots; ++k)
        for (int half = 0; half < 2; ++half) {
            D3D12_CPU_DESCRIPTOR_HANDLE hnd = c.heap->GetCPUDescriptorHandleForHeapStart();
            hnd.ptr += (SIZE_T)(4 * k + 2 * half + 1) * c.inc;
            D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
            ud.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            dev->CreateUnorderedAccessView(c.sbs, nullptr, &ud, hnd);
        }
    if (c.deny) {   // the eyes' own copies, the picture's size and family, read by the shader
        td.Width = c.srcW; td.Height = c.srcH; td.Format = TypelessOf(c.srcFormat); td.Flags = D3D12_RESOURCE_FLAG_NONE;
        for (ID3D12Resource*& t : c.inter)
            if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                    __uuidof(ID3D12Resource), (void**)&t))) {
                c.notes = "its copies of the eyes could not be made";
                return false;
            }
    }
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = (UINT64)2 * c.halfW * 4 * c.h; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (Slot& s : c.slots) {
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&s.buf))) {
            c.notes = "its read-back ring could not be made";
            return false;
        }
        for (int i = 0; i < 3; ++i)
            if (FAILED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&s.alloc[i])) ||
                FAILED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s.alloc[i], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&s.list[i]))) {
                c.notes = "its command lists could not be made";
                return false;
            } else s.list[i]->Close();
    }
    {   // the GPU's timestamps (no timing if they cannot be made: the clip goes on)
        const D3D12_QUERY_HEAP_DESC qd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 6 * kSlots, 0};
        D3D12_RESOURCE_DESC sd = bd;
        sd.Width = 6 * kSlots * sizeof(UINT64);
        if (FAILED(dev->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void**)&c.stamps)) ||
            FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &sd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
                                                (void**)&c.stampBuf)) ||
            !c.queue || FAILED(c.queue->GetTimestampFrequency(&c.stampHz)) || !c.stampHz) {
            Rel(c.stamps);
            Rel(c.stampBuf);
            c.stampHz = 0;
        }
        c.gpuUs.assign(kKeptMax, -1.0f);
    }
    // the encoder: NV12 in, H.264 out, 30 frames a second nominal (each frame stamped with its own time), BT.709 video range
    if (!c.mf.Load()) { c.notes = "no Media Foundation on this Windows (an N edition without the Media Feature Pack?)"; return false; }
    if (FAILED(c.mf.Startup(MF_VERSION, MFSTARTUP_LITE))) { c.notes = "Media Foundation did not start"; return false; }
    IMFAttributes* at = nullptr;
    IMFMediaType *out = nullptr, *in = nullptr;
    const UINT32 W = 2 * c.halfW, H = c.h;
    const std::wstring path = g_folder + FileName(c.stamp, "", L".mp4", "headset_clip_");
    CreateDirectoryW(g_folder.c_str(), nullptr);
    ok = SUCCEEDED(c.mf.CreateAttributes(&at, 2)) && SUCCEEDED(at->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE)) &&
         SUCCEEDED(at->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE)) && SUCCEEDED(c.mf.CreateSinkWriterFromURL(path.c_str(), nullptr, at, &c.writer)) &&
         SUCCEEDED(c.mf.CreateMediaType(&out)) && SUCCEEDED(out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
         SUCCEEDED(out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) && SUCCEEDED(out->SetUINT32(MF_MT_AVG_BITRATE, 6000000)) &&
         SUCCEEDED(out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) && SUCCEEDED(MFSetAttributeSize(out, MF_MT_FRAME_SIZE, W, H)) &&
         SUCCEEDED(MFSetAttributeRatio(out, MF_MT_FRAME_RATE, 30, 1)) && SUCCEEDED(MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) &&
         SUCCEEDED(out->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709)) &&
         SUCCEEDED(out->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235)) && SUCCEEDED(c.writer->AddStream(out, &c.stream)) &&
         SUCCEEDED(c.mf.CreateMediaType(&in)) && SUCCEEDED(in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
         SUCCEEDED(in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)) && SUCCEEDED(in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) &&
         SUCCEEDED(MFSetAttributeSize(in, MF_MT_FRAME_SIZE, W, H)) && SUCCEEDED(MFSetAttributeRatio(in, MF_MT_FRAME_RATE, 30, 1)) &&
         SUCCEEDED(MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1)) && SUCCEEDED(in->SetUINT32(MF_MT_DEFAULT_STRIDE, W)) &&
         SUCCEEDED(in->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709)) &&
         SUCCEEDED(in->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235)) && SUCCEEDED(c.writer->SetInputMediaType(c.stream, in, nullptr)) &&
         SUCCEEDED(c.writer->BeginWriting());
    Rel(at);
    Rel(out);
    Rel(in);
    if (!ok) { c.notes = "the H.264 encoder could not be set up"; return false; }
    return true;
}

void Release(Clip& c, bool gpuDone) {
    if (c.writer) c.writer->Release();
    if (c.mf.Shutdown) c.mf.Shutdown();
    c.mf.Free();
    if (!gpuDone) return;   // (a copy that never came back: nothing freed under the GPU)
    for (Slot& s : c.slots) {
        for (int i = 0; i < 3; ++i) { Rel(s.list[i]); Rel(s.alloc[i]); }
        Rel(s.buf);
    }
    Rel(c.inter[0]);
    Rel(c.inter[1]);
    Rel(c.sbs);
    Rel(c.heap);
    Rel(c.pso);
    Rel(c.root);
    Rel(c.fence);
    Rel(c.stamps);
    Rel(c.stampBuf);
    Rel(c.dev);
}

bool WaitFence(Clip& c, UINT64 v, DWORD ms, HANDLE ev) {
    if (c.fence->GetCompletedValue() >= v) return true;
    if (ev && SUCCEEDED(c.fence->SetEventOnCompletion(v, ev))) WaitForSingleObject(ev, ms);
    return c.fence->GetCompletedValue() >= v;
}

// The clip's thread: Make, then each frame kept encoded as it comes back, then the files.
void Run() {
    Clip& c = *g_clip;
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool made = Make(c);
    if (!made) {
        LogF("vrcam: headset clip - could not start: %s", c.notes.c_str());
        Release(c, true);
        if (SUCCEEDED(co)) CoUninitialize();
        { std::lock_guard<std::mutex> hold(g_lock); delete g_clip; g_clip = nullptr; }
        g_phase.store(kIdle);
        return;
    }
    g_phase.store(kReadying);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const UINT32 W = 2 * c.halfW, H = c.h;
    int written = 0, failed = 0;
    bool gpuDone = true;
    double encodeMs = 0.0;
    for (;;) {
        Item it{};
        {
            std::unique_lock<std::mutex> hold(g_lock);
            g_wake.wait(hold, [] { return !g_items.empty() || g_phase.load() == kFinishing; });
            if (g_items.empty()) break;   // (finishing, nothing left)
            it = g_items.front();
            g_items.pop_front();
        }
        Slot& s = c.slots[it.slot];
        if (!WaitFence(c, s.fenceV, 5000, ev)) { gpuDone = false; ++failed; continue; }   // (its slot stays busy: never reused)
        if (c.stampBuf && c.stampHz && s.timed) {   // its GPU time: the shrinks' and the copy's, each between its own two stamps
            UINT64* q = nullptr;
            const D3D12_RANGE r{(SIZE_T)it.slot * 6 * 8, (SIZE_T)(it.slot + 1) * 6 * 8}, none{0, 0};
            if (SUCCEEDED(c.stampBuf->Map(0, &r, (void**)&q)) && q) {
                double ticks = 0.0;
                for (int k = 0; k < 3; ++k)
                    if ((s.timed >> k) & 1u) ticks += (double)(q[it.slot * 6 + 2 * k + 1] - q[it.slot * 6 + 2 * k]);
                c.stampBuf->Unmap(0, &none);
                const double us = ticks * 1e6 / (double)c.stampHz;
                if (it.kept >= 0 && it.kept < kKeptMax) c.gpuUs[it.kept] = (float)us;
                c.gpuSum += us;
                c.gpuMax = std::max(c.gpuMax, us);
                ++c.gpuN;
            }
        }
        const double t = MsNow();
        uint8_t* m = nullptr;
        D3D12_RANGE all{0, (SIZE_T)W * 4 * H}, none{0, 0};
        IMFMediaBuffer* buf = nullptr;
        IMFSample* sample = nullptr;
        BYTE* dst = nullptr;
        bool ok = SUCCEEDED(s.buf->Map(0, &all, (void**)&m)) && m;
        if (ok) {
            ok = SUCCEEDED(c.mf.CreateMemoryBuffer(W * H * 3 / 2, &buf)) && SUCCEEDED(buf->Lock(&dst, nullptr, nullptr));
            if (ok) {
                BgraToNv12(m, W * 4, W, H, dst);
                buf->Unlock();
                ok = SUCCEEDED(buf->SetCurrentLength(W * H * 3 / 2)) && SUCCEEDED(c.mf.CreateSample(&sample)) && SUCCEEDED(sample->AddBuffer(buf)) &&
                     SUCCEEDED(sample->SetSampleTime((LONGLONG)(it.tMs * 10000.0))) && SUCCEEDED(sample->SetSampleDuration((LONGLONG)(kFrameMs * 10000.0))) &&
                     SUCCEEDED(c.writer->WriteSample(c.stream, sample));
            }
            s.buf->Unmap(0, &none);
        }
        Rel(sample);
        Rel(buf);
        s.busy.store(false);
        encodeMs += MsNow() - t;
        ++(ok ? written : failed);
    }
    if (gpuDone && c.fenceV) gpuDone = WaitFence(c, c.fenceV, 5000, ev);
    if (ev) CloseHandle(ev);
    const HRESULT fin = c.writer->Finalize();
    std::vector<std::wstring> files;
    const std::wstring mp4 = g_folder + FileName(c.stamp, "", L".mp4", "headset_clip_");
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    const double mb = GetFileAttributesExW(mp4.c_str(), GetFileExInfoStandard, &fa) ? (fa.nFileSizeLow + ((double)fa.nFileSizeHigh * 4294967296.0)) / 1048576.0 : 0.0;
    if (SUCCEEDED(fin) && written) files.push_back(mp4);
    {   // the frames' rows
        const std::wstring csv = g_folder + FileName(c.stamp, "", L".csv", "headset_clip_");
        std::string t = "frame,t_ms,kind,eyes,draw_thread_us,gpu_us,kept\n";
        char b[192];
        for (size_t i = 0; i < c.rows.size(); ++i) {
            const Row& r = c.rows[i];
            const float gpu = r.kept >= 0 && r.kept < kKeptMax ? c.gpuUs[r.kept] : -1.0f;
            char g[24] = "";
            if (gpu >= 0.0f) snprintf(g, sizeof g, "%.0f", gpu);
            snprintf(b, sizeof b, "%u,%.1f,%s,%s,%.0f,%s,%s\n", (unsigned)i, r.tMs, r.kind.c_str(), r.eyes.c_str(), r.us, g,
                     r.kept >= 0 ? "yes" : "no (the encoder behind)");
            t += b;
        }
        HANDLE f = CreateFileW(csv.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(f, t.data(), (DWORD)t.size(), &w, nullptr);
            CloseHandle(f);
            files.push_back(csv);
        }
    }
    {   // the newest kHeadsetClipsKept clips kept
        std::vector<std::pair<std::string, std::wstring>> found;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((g_folder + L"headset_clip_*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do found.push_back({StampOf(fd.cFileName, L"headset_clip_"), fd.cFileName});
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::vector<std::string> stamps;
        for (const auto& f : found) stamps.push_back(f.first);
        for (const std::string& old : OldShots(stamps, kHeadsetClipsKept))
            for (const auto& f : found)
                if (f.first == old) DeleteFileW((g_folder + f.second).c_str());
    }
    const double secs = c.rows.empty() ? 0.0 : c.rows.back().tMs / 1000.0;
    LogF("vrcam: headset clip - %d frames in %.1f s (%.1f a second), %ux%u, %.1f MB to d2rloader\\logs\\headset_clip_%s.mp4 and .csv (asked by %s)%s; "
         "the draw thread %.3f ms a frame kept (most %.3f), the GPU %.3f ms (most %.3f), the game's frames every %.2f ms (most %.1f) while recording; "
         "%d left out (the encoder behind), %d not encoded; encoding %.1f ms a frame on its own thread",
         written, secs, secs > 0.0 ? written / secs : 0.0, W, H, mb, c.stamp.c_str(), c.by.c_str(), SUCCEEDED(fin) ? "" : " - THE FILE COULD NOT BE CLOSED",
         c.kept ? c.usSum / c.kept / 1000.0 : 0.0, c.usMax / 1000.0, c.gpuN ? c.gpuSum / c.gpuN / 1000.0 : 0.0, c.gpuMax / 1000.0,
         c.gaps ? c.frameGapSum / c.gaps : 0.0, c.frameGapMax, c.dropped, failed, written ? encodeMs / written : 0.0);
    Release(c, gpuDone);
    if (SUCCEEDED(co)) CoUninitialize();
    {
        std::lock_guard<std::mutex> hold(g_lock);
        g_lastFiles = files;
        if (gpuDone) delete g_clip;   // (else left as it is: its buffers may still be written)
        g_clip = nullptr;
    }
    g_phase.store(kIdle);
}

void StartThread() {
    if (g_worker.joinable()) g_worker.join();
    try {
        g_worker = std::thread(&Run);
    } catch (...) {
        LogF("vrcam: headset clip - could not start its thread: no clip");
        delete g_clip;
        g_clip = nullptr;
        g_phase.store(kIdle);
    }
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }
bool On() { return g_on.load(); }
void SetFolderForTest(const wchar_t* folder) {
    g_folder = folder ? folder : L"";
    if (!g_folder.empty() && g_folder.back() != L'\\') g_folder += L'\\';
}
void SetOnForTest(bool on) { g_on.store(on); }
void SetSecondsForTest(double seconds) { g_seconds = seconds; }

void LoadSettings(const wchar_t* iniPath) {
    const bool on = GetPrivateProfileIntW(L"headset_shot", L"video", 0, iniPath) == 1;
    if (on != g_on.load()) LogF("vrcam: headset clip %s ([headset_shot] video)", on ? "on - Settings' button and the key record 10 s" : "off");
    g_on.store(on);
    g_key.store(GetPrivateProfileIntW(L"headset_shot", L"video_key", 0, iniPath));
    if (g_folder.empty()) {
        wchar_t full[MAX_PATH];
        std::wstring dir = iniPath;
        dir.resize(dir.find_last_of(L"\\/") + 1);
        dir += L"..\\logs\\";
        if (GetFullPathNameW(dir.c_str(), MAX_PATH, full, nullptr)) dir = full;
        SetFolderForTest(dir.c_str());
    }
    const int take = GetPrivateProfileIntW(L"headset_shot", L"video_take", 0, iniPath);
    const int before = g_take;
    g_take = take;
    if (before != INT_MIN && before != take && on && !Request("the Settings button"))
        LogF("vrcam: headset clip - the Settings button: one is still being recorded");
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
    g_items.clear();
    g_clip = new Clip;
    SYSTEMTIME t;
    GetLocalTime(&t);
    g_clip->stamp = Stamp(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    g_clip->by = by ? by : "?";
    return true;
}

bool Wanted() {
    const int p = g_phase.load(std::memory_order_relaxed);
    return p == kArming || p == kMeasuring || p == kReadying || p == kRecording;
}

void Eye(ID3D12CommandQueue* queue, ID3D12Resource* tex, D3D12_RESOURCE_STATES state, uint32_t x, uint32_t y, uint32_t w, uint32_t h, int half) {
    const int phase = g_phase.load(std::memory_order_relaxed);
    if ((phase != kMeasuring && phase != kRecording) || !queue || !tex || half < 0 || half > 2) return;
    Clip& c = *g_clip;
    if (phase == kRecording && !c.take) return;
    const double t = MsNow();
    const D3D12_RESOURCE_DESC d = tex->GetDesc();
    if (d.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.SampleDesc.Count != 1) return;
    x = std::min<uint32_t>(x, (uint32_t)d.Width);
    y = std::min<uint32_t>(y, d.Height);
    w = std::min<uint32_t>(w ? w : (uint32_t)d.Width, (uint32_t)d.Width - x);
    h = std::min<uint32_t>(h ? h : d.Height, d.Height - y);
    const ViewOf view = ClipView(d.Format);
    if (!w || !h || view.view == DXGI_FORMAT_UNKNOWN) return;
    const bool deny = (d.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0;
    if (phase == kMeasuring) {
        if (c.srcW) return;   // (the first picture gives the clip its shape)
        ID3D12Device* dev = nullptr;
        if (FAILED(tex->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) || !dev) return;
        c.dev = dev;   // (our reference)
        c.srcW = w; c.srcH = h; c.deny = deny; c.srcFormat = d.Format;
        c.queue = queue;
        return;
    }
    const int li = half == 1 ? 1 : 0;
    if (c.used[li] || (half == 2 && c.used[1])) return;   // (one picture a half a frame)
    ID3D12Device* dev = nullptr;
    if (FAILED(tex->GetDevice(__uuidof(ID3D12Device), (void**)&dev)) || !dev) return;
    dev->Release();
    if (dev != c.dev) return;
    Slot& s = c.slots[c.cur];
    ID3D12GraphicsCommandList* l = s.list[li];
    if (FAILED(s.alloc[li]->Reset()) || FAILED(l->Reset(s.alloc[li], c.pso))) return;
    if (c.stamps) l->EndQuery(c.stamps, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(c.cur * 6 + li * 2));
    ID3D12Resource* src = tex;
    uint32_t sx = x, sy = y;
    ViewOf v = view;
    if (deny) {   // copied into our own texture first (the picture's family; its first size - another is left out)
        ID3D12Resource* in = c.inter[li];
        if (!in || w > c.srcW || h > c.srcH || TypelessOf(d.Format) != TypelessOf(c.srcFormat)) { l->Close(); return; }
        D3D12_RESOURCE_BARRIER b[2] = {Tr(tex, state, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                       Tr(in, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)};
        l->ResourceBarrier(2, b);
        D3D12_TEXTURE_COPY_LOCATION to{}, from{};
        to.pResource = in; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.pResource = tex; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        const D3D12_BOX box{x, y, 0, x + w, y + h, 1};
        l->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
        D3D12_RESOURCE_BARRIER e[2] = {Tr(tex, D3D12_RESOURCE_STATE_COPY_SOURCE, state),
                                       Tr(in, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};
        l->ResourceBarrier(2, e);
        src = in;
        sx = sy = 0;
        v = ClipView(TypelessOf(d.Format));
    } else if (state != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        const D3D12_RESOURCE_BARRIER b = Tr(tex, state, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        l->ResourceBarrier(1, &b);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = c.heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += (SIZE_T)(4 * c.cur + 2 * li) * c.inc;
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = v.view;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    c.dev->CreateShaderResourceView(src, &sd, cpu);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = c.heap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += (UINT64)(4 * c.cur + 2 * li) * c.inc;
    l->SetComputeRootSignature(c.root);
    l->SetDescriptorHeaps(1, &c.heap);
    l->SetComputeRootDescriptorTable(1, gpu);
    const Fit fit = FitInto(c.halfW, c.h, w, h);
    for (int k = half == 2 ? 0 : half; k <= (half == 2 ? 1 : half); ++k) {
        const Consts k32{{sx, sy, w, h}, {(uint32_t)k * c.halfW, 0, c.halfW, c.h}, {fit.x, fit.y, fit.w, fit.h}, v.encodeSrgb ? 1u : 0u};
        l->SetComputeRoot32BitConstants(0, sizeof k32 / 4, &k32, 0);
        l->Dispatch((c.halfW + 7) / 8, (c.h + 7) / 8, 1);
    }
    if (!deny && state != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
        const D3D12_RESOURCE_BARRIER b = Tr(tex, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, state);
        l->ResourceBarrier(1, &b);
    }
    if (c.stamps) l->EndQuery(c.stamps, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(c.cur * 6 + li * 2 + 1));
    if (FAILED(l->Close())) return;
    ID3D12CommandList* one = l;
    queue->ExecuteCommandLists(1, &one);
    c.queue = queue;
    c.used[li] = true;
    if (half == 2) c.used[1] = true;
    if (c.stamps) s.timed |= 1u << li;
    c.eyes += half == 0 ? "L" : half == 1 ? "R" : "flat";
    c.us += (MsNow() - t) * 1000.0;
}

void FrameDone(const char* kind) {
    const int phase = g_phase.load(std::memory_order_relaxed);
    if (phase == kArming) { g_phase.store(kMeasuring); return; }
    if (phase == kReadying) {   // the recording starts with the next frame, which is kept
        Clip& c = *g_clip;
        c.t0 = c.lastFrame = MsNow();
        c.take = true;
        g_phase.store(kRecording);
        return;
    }
    if (phase == kMeasuring) {
        Clip& c = *g_clip;
        if (c.srcW) { g_phase.store(kMaking); StartThread(); return; }
        if (++c.measureFrames >= 90) {   // nothing handed to the headset: said, no file
            LogF("vrcam: headset clip - nothing was handed to the headset in 90 frames: no clip (asked by %s)", c.by.c_str());
            delete g_clip;
            g_clip = nullptr;
            g_phase.store(kIdle);
        }
        return;
    }
    if (phase != kRecording) return;
    Clip& c = *g_clip;
    const double t = MsNow();
    const double gap = t - c.lastFrame;
    c.lastFrame = t;
    if (c.frames++) { c.frameGapSum += gap; c.frameGapMax = std::max(c.frameGapMax, gap); ++c.gaps; }
    if (c.take && (c.used[0] || c.used[1])) {
        Slot& s = c.slots[c.cur];
        ID3D12GraphicsCommandList* l = s.list[2];
        if (SUCCEEDED(s.alloc[2]->Reset()) && SUCCEEDED(l->Reset(s.alloc[2], nullptr))) {
            if (c.stamps) l->EndQuery(c.stamps, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(c.cur * 6 + 4));
            D3D12_RESOURCE_BARRIER b = Tr(c.sbs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
            l->ResourceBarrier(1, &b);
            D3D12_TEXTURE_COPY_LOCATION to{}, from{};
            to.pResource = s.buf;
            to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            to.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 2 * c.halfW, c.h, 1, 2 * c.halfW * 4};
            from.pResource = c.sbs;
            from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            l->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            b = Tr(c.sbs, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            l->ResourceBarrier(1, &b);
            if (c.stamps) {   // this slot's stamps, those written (the shrinks' lists ran before this one on the queue)
                l->EndQuery(c.stamps, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(c.cur * 6 + 5));
                s.timed |= 4u;
                for (UINT k = 0; k < 3; ++k)
                    if ((s.timed >> k) & 1u)
                        l->ResolveQueryData(c.stamps, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)(c.cur * 6) + 2 * k, 2, c.stampBuf,
                                            ((UINT64)c.cur * 6 + 2 * k) * sizeof(UINT64));
            }
            if (SUCCEEDED(l->Close())) {
                ID3D12CommandList* one = l;
                c.queue->ExecuteCommandLists(1, &one);
                c.queue->Signal(c.fence, ++c.fenceV);
                s.fenceV = c.fenceV;
                s.busy.store(true);
                const double tMs = c.lastTake = t;
                {
                    std::lock_guard<std::mutex> hold(g_lock);
                    g_items.push_back(Item{c.cur, tMs - c.t0, c.kept});
                }
                g_wake.notify_one();
                c.us += (MsNow() - t) * 1000.0;
                c.rows.push_back(Row{tMs - c.t0, kind ? kind : "?", c.eyes, c.us, c.kept});
                c.usSum += c.us;
                c.usMax = std::max(c.usMax, c.us);
                ++c.kept;
                c.cur = (c.cur + 1) % kSlots;
            }
        }
    }
    c.take = false;
    c.used[0] = c.used[1] = false;
    c.eyes.clear();
    c.us = 0.0;
    if (t - c.t0 >= g_seconds * 1000.0) {   // done: the thread encodes what is left and writes the files
        g_phase.store(kFinishing);
        g_wake.notify_one();
        return;
    }
    if (t - c.lastTake >= kFrameMs - 4.0) {   // the next frame is due to be kept - unless its slot is still in the encoder's hands
        if (c.slots[c.cur].busy.load()) {
            ++c.dropped;
            c.rows.push_back(Row{t - c.t0, kind ? kind : "?", "", 0.0, -1});
            c.lastTake = t;   // (tried again a frame time on)
        } else {
            c.take = true;
            c.slots[c.cur].timed = 0;
        }
    }
}

bool Busy() { return g_phase.load() != kIdle; }

bool WaitIdle(unsigned ms) {
    const ULONGLONG until = GetTickCount64() + ms;
    while (g_phase.load() == kMaking || g_phase.load() == kFinishing) {
        if (GetTickCount64() >= until) return false;
        Sleep(5);
    }
    std::lock_guard<std::mutex> hold(g_lock);
    if (g_worker.joinable() && g_phase.load() != kMaking && g_phase.load() != kFinishing && g_phase.load() != kReadying &&
        g_phase.load() != kRecording)
        g_worker.join();
    return true;
}

std::vector<std::wstring> LastFiles() {
    std::lock_guard<std::mutex> hold(g_lock);
    return g_lastFiles;
}

}  // namespace headclip
