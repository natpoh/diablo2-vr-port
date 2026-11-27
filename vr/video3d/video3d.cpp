// video3d.cpp - see video3d.h.

#include "video3d.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "depth_net.h"
#include "steady_depth.h"
#include "video3d_shader.h"

using Microsoft::WRL::ComPtr;

namespace video3d {

namespace {

constexpr wchar_t kModelFile[] = L"zipdepth_base_512x288.onnx";
constexpr int kReadback = 2, kUpload = 2;
constexpr int kSrvPairs = 48;        // (src, depth) descriptor pairs, a ring
constexpr int kSearchSteps = 24;

LogFn g_log = nullptr;
void Say(const char* fmt, ...) {
    if (!g_log) return;
    char b[600];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER x{};
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition.pResource = r;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    x.Transition.StateBefore = a;
    x.Transition.StateAfter = b;
    return x;
}

bool IsSrgb(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}

struct Constants {
    float rect[4];
    float eyeSign, sep, conv, srcGamma;
    float steps, hasDepth, tap[2];
    float outGamma, pad[3];
};
static_assert(sizeof(Constants) == 64, "16 root constants, as the shader's cbuffer");

struct State {
    ComPtr<ID3D12Device> dev;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12RootSignature> rs;
    ComPtr<ID3DBlob> vs, psEye;
    ComPtr<ID3D12PipelineState> downPso;
    struct EyePso { DXGI_FORMAT fmt; ComPtr<ID3D12PipelineState> pso; };
    std::vector<EyePso> eyePsos;
    ComPtr<ID3D12DescriptorHeap> srvHeap, rtvHeap;
    UINT srvInc = 0;
    uint32_t srvAt = 0;

    int netW = 512, netH = 288;       // the network's input; read from it once loaded, the resources made for this size
    UINT inPitch = 0, outPitch = 0;   // readback rows (RGBA8) and upload rows (float), each a multiple of 256
    ComPtr<ID3D12Resource> down;      // netW x netH RGBA8, render target / copy source
    ComPtr<ID3D12Resource> readback[kReadback];
    uint8_t* readbackPtr[kReadback] = {};
    ComPtr<ID3D12Resource> depth;     // netW x netH R32_FLOAT
    D3D12_RESOURCE_STATES depthState = D3D12_RESOURCE_STATE_COPY_DEST;
    bool depthValid = false;
    ComPtr<ID3D12Resource> upload[kUpload];
    uint8_t* uploadPtr[kUpload] = {};

    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    uint64_t ticket = 0;              // the last value handed out
    uint64_t toSignal = 0;            // Record's list, for Submitted
    int nextReadback = 0;
    int adapter = 0;

    // Between the game's thread and the worker, under mu.
    std::mutex mu;
    std::condition_variable cv;
    bool stop = false;
    int pendingSlot = -1;             // a readback slot the GPU fills, behind pendingTicket
    uint64_t pendingTicket = 0;
    bool reset = false;
    int readySlot = -1;               // the upload slot with a map not yet copied
    uint64_t uploadTicket[kUpload] = {};   // the list that last copied from each upload slot
    std::atomic<bool> readbackBusy{false};
    std::atomic<int> net{0};          // 0 loading, 1 ready, -1 none (missing / failed): flat for good
    std::thread worker;
};

State* g = nullptr;
ID3D12Device* g_failedFor = nullptr;
std::wstring g_folder;   // SetRuntimeFolder (tests); empty: RuntimeFolder()

// ── the worker ──────────────────────────────────────────────────────────────

void WorkerLoop(State* st) {
    DepthNet net;
    const std::wstring dir = g_folder.empty() ? RuntimeFolder() : g_folder;
    if (dir.empty()) {
        Say("vrcam: video3d - no onnxruntime.dll in d2rloader\\d2r_vr_libs (or beside the plugin): the videos stay flat");
        st->net.store(-1);
        return;
    }
    if (!net.Load(dir, dir + kModelFile, st->adapter, g_log) || net.Width() != st->netW || net.Height() != st->netH) {
        if (net.Ready()) Say("vrcam: video3d - the depth network takes %dx%d, not %dx%d: the videos stay flat", net.Width(), net.Height(), st->netW, st->netH);
        st->net.store(-1);
        return;
    }
    st->net.store(1);
    const int w = st->netW, h = st->netH;
    SteadyDepth sd;
    std::vector<float> input((size_t)w * h * 3), luma((size_t)w * h), raw;
    for (;;) {
        int slot;
        uint64_t ticket;
        bool reset;
        {
            std::unique_lock<std::mutex> lk(st->mu);
            st->cv.wait(lk, [&] { return st->stop || st->pendingSlot >= 0; });
            if (st->stop) return;
            slot = st->pendingSlot;
            ticket = st->pendingTicket;
            reset = st->reset;
            st->reset = false;
        }
        // The GPU fills the readback behind the list it was recorded on: that list, waited for a while at most.
        bool ready = st->fence->GetCompletedValue() >= ticket;
        for (int tries = 0; !ready && tries < 40; ++tries) {
            st->fence->SetEventOnCompletion(ticket, st->fenceEvent);
            WaitForSingleObject(st->fenceEvent, 50);
            ready = st->fence->GetCompletedValue() >= ticket;
            std::lock_guard<std::mutex> lk(st->mu);
            if (st->stop) return;
        }
        if (ready) {
            const uint8_t* px = st->readbackPtr[slot];
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const uint8_t* p = px + (size_t)y * st->inPitch + (size_t)x * 4;
                    float* q = &input[((size_t)y * w + x) * 3];
                    q[0] = p[0] / 255.0f;
                    q[1] = p[1] / 255.0f;
                    q[2] = p[2] / 255.0f;
                    luma[(size_t)y * w + x] = 0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2];
                }
        }
        {
            std::lock_guard<std::mutex> lk(st->mu);
            st->pendingSlot = -1;
        }
        st->readbackBusy.store(false);
        if (!ready) continue;
        if (reset) sd.Clear();
        const SteadyDepth::Verdict v = sd.Look(luma, reset);
        if (v == SteadyDepth::Verdict::Repeat) continue;   // the film's frame shown again at the headset's rate
        if (!net.Run(input, raw)) continue;
        const std::vector<float>& map = sd.Apply(raw, v == SteadyDepth::Verdict::Cut);

        // Into an upload buffer the GPU no longer reads from, and not the one waiting to be copied (a map not copied yet is simply
        // replaced by this newer one: it goes into the other buffer and takes its place).
        const uint64_t done = st->fence->GetCompletedValue();
        int u = -1;
        {
            std::lock_guard<std::mutex> lk(st->mu);
            for (int k = 0; k < kUpload; ++k)
                if (k != st->readySlot && st->uploadTicket[k] <= done) { u = k; break; }
        }
        if (u < 0) continue;   // both busy: this map is skipped, the next one goes
        for (int y = 0; y < h; ++y) std::memcpy(st->uploadPtr[u] + (size_t)y * st->outPitch, &map[(size_t)y * w], (size_t)w * sizeof(float));
        std::lock_guard<std::mutex> lk(st->mu);
        st->readySlot = u;
    }
}

// ── setup ───────────────────────────────────────────────────────────────────

int AdapterIndexOf(ID3D12Device* dev) {
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    const auto create = dxgi ? (CreateFactoryFn)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
    ComPtr<IDXGIFactory1> f;
    if (!create || FAILED(create(IID_PPV_ARGS(&f)))) return 0;
    const LUID want = dev->GetAdapterLuid();
    ComPtr<IDXGIAdapter1> a;
    for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i, a.Reset()) {
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        if (d.AdapterLuid.LowPart == want.LowPart && d.AdapterLuid.HighPart == want.HighPart) return (int)i;
    }
    return 0;
}

bool MakeBuffer(ID3D12Device* dev, D3D12_HEAP_TYPE heap, UINT64 bytes, D3D12_RESOURCE_STATES state, ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES hp{heap};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&out)));
}

bool MakeTexture(ID3D12Device* dev, UINT w, UINT h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = fmt;
    d.SampleDesc.Count = 1;
    d.Flags = flags;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = fmt;
    return SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, (flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ? &cv : nullptr,
                                                  IID_PPV_ARGS(&out)));
}

ID3D12PipelineState* MakePso(State* st, ID3DBlob* ps, DXGI_FORMAT rtv) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = st->rs.Get();
    d.VS = {st->vs->GetBufferPointer(), st->vs->GetBufferSize()};
    d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = rtv;
    d.SampleDesc.Count = 1;
    ID3D12PipelineState* p = nullptr;
    return SUCCEEDED(st->dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&p))) ? p : nullptr;
}

ID3D12PipelineState* EyePso(State* st, DXGI_FORMAT fmt) {
    for (auto& e : st->eyePsos)
        if (e.fmt == fmt) return e.pso.Get();
    ID3D12PipelineState* p = MakePso(st, st->psEye.Get(), fmt);
    if (!p) return nullptr;
    st->eyePsos.push_back({fmt, nullptr});
    st->eyePsos.back().pso.Attach(p);
    return p;
}

bool Compile(pD3DCompile compile, const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
    ComPtr<ID3DBlob> err;
    if (SUCCEEDED(compile(kShader, sizeof kShader - 1, "d2rvr_video3d", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &err)))
        return true;
    Say("vrcam: video3d - %s did not compile: %s", entry, err ? (const char*)err->GetBufferPointer() : "?");
    return false;
}

bool MakeGpu(State* st) {
    ID3D12Device* dev = st->dev.Get();
    HMODULE dc = LoadLibraryW(L"d3dcompiler_47.dll");
    const auto compile = dc ? (pD3DCompile)GetProcAddress(dc, "D3DCompile") : nullptr;
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    const auto serialize = d12 ? (SerializeFn)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
    if (!compile || !serialize) { Say("vrcam: video3d - no shader compiler (d3dcompiler_47.dll): the videos stay flat"); return false; }
    ComPtr<ID3DBlob> psDown;
    if (!Compile(compile, "VSMain", "vs_5_0", st->vs) || !Compile(compile, "PSDown", "ps_5_0", psDown) || !Compile(compile, "PSEye", "ps_5_0", st->psEye))
        return false;

    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0, 0};
    D3D12_ROOT_PARAMETER rp[2] = {};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[0].Constants = {0, 0, sizeof(Constants) / 4};
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable = {1, &range};
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC smp{};
    smp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    smp.MaxLOD = D3D12_FLOAT32_MAX;
    smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    const D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 1, &smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&st->rs)))) {
        Say("vrcam: video3d - the root signature could not be made: the videos stay flat");
        return false;
    }
    st->downPso.Attach(MakePso(st, psDown.Get(), DXGI_FORMAT_R8G8B8A8_UNORM));
    if (!st->downPso) { Say("vrcam: video3d - the pipeline could not be made: the videos stay flat"); return false; }

    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvPairs * 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&st->srvHeap))) || FAILED(dev->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&st->rtvHeap)))) return false;
    st->srvHeap->SetName(L"d2rvr video3d srv ring");
    st->srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    const UINT w = (UINT)st->netW, h = (UINT)st->netH;
    st->inPitch = (w * 4 + 255) & ~255u;
    st->outPitch = (w * 4 + 255) & ~255u;
    if (!MakeTexture(dev, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, st->down) ||
        !MakeTexture(dev, w, h, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, st->depth))
        return false;
    st->depthState = D3D12_RESOURCE_STATE_COPY_DEST;
    dev->CreateRenderTargetView(st->down.Get(), nullptr, st->rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (int i = 0; i < kReadback; ++i) {
        if (!MakeBuffer(dev, D3D12_HEAP_TYPE_READBACK, (UINT64)st->inPitch * h, D3D12_RESOURCE_STATE_COPY_DEST, st->readback[i])) return false;
        void* p = nullptr;
        if (FAILED(st->readback[i]->Map(0, nullptr, &p))) return false;
        st->readbackPtr[i] = (uint8_t*)p;
    }
    for (int i = 0; i < kUpload; ++i) {
        if (!MakeBuffer(dev, D3D12_HEAP_TYPE_UPLOAD, (UINT64)st->outPitch * h, D3D12_RESOURCE_STATE_GENERIC_READ, st->upload[i])) return false;
        void* p = nullptr;
        D3D12_RANGE none{0, 0};
        if (FAILED(st->upload[i]->Map(0, &none, &p))) return false;
        st->uploadPtr[i] = (uint8_t*)p;
    }
    if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&st->fence)))) return false;
    st->fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return st->fenceEvent != nullptr;
}

void Release(State* st) {
    if (!st) return;
    {
        std::lock_guard<std::mutex> lk(st->mu);
        st->stop = true;
    }
    st->cv.notify_all();
    if (st->worker.joinable()) st->worker.join();
    if (st->fence && st->ticket && st->fence->GetCompletedValue() < st->ticket) {
        st->fence->SetEventOnCompletion(st->ticket, st->fenceEvent);
        WaitForSingleObject(st->fenceEvent, 2000);
    }
    if (st->fenceEvent) CloseHandle(st->fenceEvent);
    delete st;
}

}  // namespace

void SetLog(LogFn fn) { g_log = fn; }
void SetRuntimeFolder(const wchar_t* dir) { g_folder = dir ? dir : L""; }

bool Start(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (!device || !queue) return false;
    if (g) {
        if (g->dev.Get() == device) return true;
        Stop();
    }
    if (g_failedFor == device) return false;
    auto* st = new State;
    st->dev = device;
    st->queue = queue;
    if (!MakeGpu(st)) {
        Release(st);
        g_failedFor = device;
        return false;
    }
    st->adapter = AdapterIndexOf(device);
    st->worker = std::thread(WorkerLoop, st);   // the network loads there: the first film starts flat, never with a hitch
    g = st;
    return true;
}

int Record(ID3D12GraphicsCommandList* l, const Frame& f) {
    State* st = g;
    if (!st || !l || !f.src || !f.rtv || !f.filmW || !f.filmH || f.outW < 2 || !f.outH) return -1;
    auto* src = (ID3D12Resource*)f.src;
    const D3D12_RESOURCE_DESC sd = src->GetDesc();
    if (f.filmX + f.filmW > sd.Width || f.filmY + f.filmH > sd.Height) return -1;
    ID3D12PipelineState* eyePso = EyePso(st, (DXGI_FORMAT)f.rtvFormat);
    if (!eyePso) return -1;
    const uint64_t ticket = ++st->ticket;
    st->toSignal = ticket;

    // Views: the film's source and the depth, a pair in the ring.
    const uint32_t pair = st->srvAt++ % kSrvPairs;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = st->srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += (SIZE_T)pair * 2 * st->srvInc;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = (DXGI_FORMAT)f.srcViewFormat;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    st->dev->CreateShaderResourceView(src, &sv, cpu);
    cpu.ptr += st->srvInc;
    D3D12_SHADER_RESOURCE_VIEW_DESC dv = sv;
    dv.Format = DXGI_FORMAT_R32_FLOAT;
    st->dev->CreateShaderResourceView(st->depth.Get(), &dv, cpu);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = st->srvHeap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += (UINT64)pair * 2 * st->srvInc;

    const D3D12_RESOURCE_STATES srcState = (D3D12_RESOURCE_STATES)f.srcState;
    const D3D12_RESOURCE_STATES readable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const bool srcMove = (srcState & readable) != readable;
    if (srcMove) { auto b = Tr(src, srcState, readable); l->ResourceBarrier(1, &b); }

    // A finished depth map in.
    int u = -1;
    {
        std::lock_guard<std::mutex> lk(st->mu);
        u = st->readySlot;
        if (u >= 0) {
            st->readySlot = -1;
            st->uploadTicket[u] = ticket;
        }
    }
    if (u >= 0) {
        if (st->depthState != D3D12_RESOURCE_STATE_COPY_DEST) {
            auto b = Tr(st->depth.Get(), st->depthState, D3D12_RESOURCE_STATE_COPY_DEST);
            l->ResourceBarrier(1, &b);
            st->depthState = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        D3D12_TEXTURE_COPY_LOCATION to{}, from{};
        to.pResource = st->depth.Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        from.pResource = st->upload[u].Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_FLOAT, (UINT)st->netW, (UINT)st->netH, 1, st->outPitch};
        l->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        st->depthValid = true;
    }
    if (st->depthState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        auto b = Tr(st->depth.Get(), st->depthState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        l->ResourceBarrier(1, &b);
        st->depthState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    Constants c{};
    c.rect[0] = (float)f.filmX / (float)sd.Width;
    c.rect[1] = (float)f.filmY / (float)sd.Height;
    c.rect[2] = (float)f.filmW / (float)sd.Width;
    c.rect[3] = (float)f.filmH / (float)sd.Height;
    c.srcGamma = f.srcEncoding == kSrcGamma ? 1.0f : 0.0f;
    c.sep = std::min(std::max(f.strengthPct, 0.0f), 6.0f) * 0.01f;
    c.conv = std::min(std::max(f.convergence, 0.0f), 1.0f);
    c.steps = (float)kSearchSteps;
    c.hasDepth = st->depthValid ? 1.0f : 0.0f;
    c.tap[0] = c.rect[2] / (float)st->netW;
    c.tap[1] = c.rect[3] / (float)st->netH;
    c.outGamma = IsSrgb((DXGI_FORMAT)f.rtvFormat) ? 0.0f : 1.0f;

    ID3D12DescriptorHeap* heaps[] = {st->srvHeap.Get()};
    l->SetDescriptorHeaps(1, heaps);
    l->SetGraphicsRootSignature(st->rs.Get());
    l->SetGraphicsRootDescriptorTable(1, gpu);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // The film down to the network's size and out to a readback, when the network is there and the worker can take one.
    bool expected = false;
    if (st->net.load() == 1 && st->readbackBusy.compare_exchange_strong(expected, true)) {
        const int r = st->nextReadback;
        st->nextReadback = (r + 1) % kReadback;
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = st->rtvHeap->GetCPUDescriptorHandleForHeapStart();
        l->SetPipelineState(st->downPso.Get());
        l->SetGraphicsRoot32BitConstants(0, sizeof(Constants) / 4, &c, 0);
        l->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const D3D12_VIEWPORT vp{0, 0, (float)st->netW, (float)st->netH, 0, 1};
        const D3D12_RECT sc{0, 0, st->netW, st->netH};
        l->RSSetViewports(1, &vp);
        l->RSSetScissorRects(1, &sc);
        l->DrawInstanced(3, 1, 0, 0);
        auto b0 = Tr(st->down.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        l->ResourceBarrier(1, &b0);
        D3D12_TEXTURE_COPY_LOCATION to{}, from{};
        to.pResource = st->readback[r].Get();
        to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, (UINT)st->netW, (UINT)st->netH, 1, st->inPitch};
        from.pResource = st->down.Get();
        from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        l->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        auto b1 = Tr(st->down.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        l->ResourceBarrier(1, &b1);
        {
            std::lock_guard<std::mutex> lk(st->mu);
            st->pendingSlot = r;
            st->pendingTicket = ticket;
        }
        st->cv.notify_one();
    }

    // The two eyes.
    D3D12_CPU_DESCRIPTOR_HANDLE out{(SIZE_T)f.rtv};
    l->SetPipelineState(eyePso);
    l->OMSetRenderTargets(1, &out, FALSE, nullptr);
    const UINT half = f.outW / 2;
    for (int e = 0; e < 2; ++e) {
        c.eyeSign = e == 0 ? 1.0f : -1.0f;
        l->SetGraphicsRoot32BitConstants(0, sizeof(Constants) / 4, &c, 0);
        const D3D12_VIEWPORT vp{(float)(e * half), 0, (float)half, (float)f.outH, 0, 1};
        const D3D12_RECT sc{(LONG)(e * half), 0, (LONG)((e + 1) * half), (LONG)f.outH};
        l->RSSetViewports(1, &vp);
        l->RSSetScissorRects(1, &sc);
        l->DrawInstanced(3, 1, 0, 0);
    }

    if (srcMove) { auto b = Tr(src, readable, srcState); l->ResourceBarrier(1, &b); }
    return st->depthValid ? 1 : 0;
}

void Submitted() {
    State* st = g;
    if (st && st->toSignal) {
        st->queue->Signal(st->fence.Get(), st->toSignal);
        st->toSignal = 0;
    }
}

void Reset() {
    State* st = g;
    if (!st) return;
    std::lock_guard<std::mutex> lk(st->mu);
    st->reset = true;
    st->depthValid = false;   // the last film's depth is not shown over the new one
}

void Stop() {
    State* st = g;
    g = nullptr;
    Release(st);
    g_failedFor = nullptr;
}

}  // namespace video3d
