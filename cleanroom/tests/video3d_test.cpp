// [cinema] video_3d (vr/video3d): the steady depth map's rules, the runtime loader with no runtime there, and - when a folder with
// onnxruntime.dll, DirectML.dll and zipdepth_base_512x288.onnx is given - the whole pass on a D3D12 device, driven as xr.cpp drives it.
//
//   video3d_test.exe                                   the rules and the loader only (no GPU, no runtime needed)
//   video3d_test.exe <runtime folder>                  + the pass on a made-up picture: the eyes must differ once the depth is ready
//   video3d_test.exe <runtime folder> in.rgba w h out.rgba   + those frames (raw RGBA, w x h) through it, the 2w x h pairs written out

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "video3d/depth_net.h"
#include "video3d/steady_depth.h"
#include "video3d/video3d.h"

using Microsoft::WRL::ComPtr;

namespace {

int g_checks = 0, g_fails = 0;
std::string g_log;

#define EXPECT(c, what)                                                                         \
    do {                                                                                        \
        ++g_checks;                                                                             \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s - %s\n", __LINE__, #c, what); } \
    } while (0)

void Log(const char* s) {
    g_log += s;
    g_log += "\n";
    std::printf("  [log] %s\n", s);
}

void Rules() {
    using video3d::SteadyDepth;
    SteadyDepth sd;
    std::vector<float> a(64 * 36, 0.3f), b = a, c = a;
    for (size_t i = 0; i < b.size(); ++i) b[i] += (i % 3) * 0.0005f;   // a tiny change (video noise): the same frame again
    for (size_t i = 0; i < c.size(); ++i) c[i] = 0.3f + 0.05f * (float)((i / 64) % 2);   // a small change: the same shot
    EXPECT(sd.Look(a, false) == SteadyDepth::Verdict::Cut, "the first picture starts afresh");
    std::vector<float> raw(a.size());
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = 10.0f + 0.01f * (float)(i % 64);   // a ramp across: far left, near right
    const std::vector<float>& m1 = sd.Apply(raw, true);
    EXPECT(m1[0] < 0.05f && m1[63] > 0.95f, "the range is the 2nd..98th percentile: the ramp spans 0..1");
    EXPECT(sd.Look(b, false) == SteadyDepth::Verdict::Repeat, "the same picture again is not worked on");
    EXPECT(sd.Look(c, false) == SteadyDepth::Verdict::Same, "a small change is the same shot");
    std::vector<float> raw2(raw.size());
    for (size_t i = 0; i < raw2.size(); ++i) raw2[i] = 10.0f + 0.01f * (float)(63 - i % 64);   // the ramp turned round
    const float before = sd.map[0];
    const std::vector<float>& m2 = sd.Apply(raw2, false);
    EXPECT(m2[0] > before + 0.1f && m2[0] < 0.9f, "within a shot each pixel is eased, not jumped");
    std::vector<float> d(a.size(), 0.9f);
    EXPECT(sd.Look(d, false) == SteadyDepth::Verdict::Cut, "a big change is a new shot");
    const std::vector<float>& m3 = sd.Apply(raw2, true);
    EXPECT(m3[0] > 0.95f, "a new shot takes the new map as it is");
    EXPECT(sd.Look(d, true) == SteadyDepth::Verdict::Cut, "a reset (a new film) starts afresh even on the same picture");
    std::vector<float> flat(a.size(), 5.0f);
    const std::vector<float>& m4 = sd.Apply(flat, true);
    EXPECT(std::isfinite(m4[0]) && m4[0] >= 0.0f && m4[0] <= 1.0f, "a flat answer stays a number in 0..1");
}

void LoaderWithoutRuntime() {
    video3d::DepthNet net;
    g_log.clear();
    EXPECT(!net.Load(L"C:\\no\\such\\folder\\", L"C:\\no\\such\\folder\\zipdepth_base_512x288.onnx", 0, &Log), "no runtime, no network: not ready");
    EXPECT(!net.Ready(), "not ready");
    EXPECT(g_log.find("the videos stay flat") != std::string::npos, "the log says the videos stay flat");
    std::vector<float> in(10), out;
    EXPECT(!net.Run(in, out), "Run does nothing without a network");
}

struct Gpu {
    ComPtr<ID3D12Device> dev;
    ComPtr<ID3D12CommandQueue> q;
    ComPtr<ID3D12CommandAllocator> al;
    ComPtr<ID3D12GraphicsCommandList> l;
    ComPtr<ID3D12Fence> fence;
    UINT64 fv = 0;
    HANDLE ev = nullptr;
    void Idle() {
        q->Signal(fence.Get(), ++fv);
        fence->SetEventOnCompletion(fv, ev);
        WaitForSingleObject(ev, INFINITE);
    }
};

// Frames through the pass, three presents each (a 24 fps film in a 72 Hz headset), the last one read back. Returns how many frames
// came back in 3D; each pair into `pairs` (2w x h RGBA rows) when it is not null.
int Drive(Gpu& g, const std::vector<std::vector<uint8_t>>& frames, int W, int H, FILE* pairs, double* lastEyeDiff) {
    D3D12_HEAP_PROPERTIES def{D3D12_HEAP_TYPE_DEFAULT}, up{D3D12_HEAP_TYPE_UPLOAD}, rb{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = W;
    td.Height = H;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    td.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> bb, outTex, upBuf, rbBuf;
    g.dev->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&bb));
    const UINT outW = (UINT)W * 2, outH = (UINT)H;
    D3D12_RESOURCE_DESC od = td;
    od.Width = outW;
    od.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    od.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    g.dev->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &od, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&outTex));
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1};
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    g.dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap));
    g.dev->CreateRenderTargetView(outTex.Get(), nullptr, rtvHeap->GetCPUDescriptorHandleForHeapStart());
    const UINT inPitch = ((UINT)W * 4 + 255) & ~255u, outPitch = (outW * 4 + 255) & ~255u;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bd.Width = (UINT64)inPitch * H;
    g.dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upBuf));
    bd.Width = (UINT64)outPitch * outH;
    g.dev->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rbBuf));
    uint8_t* upPtr = nullptr;
    uint8_t* rbPtr = nullptr;
    upBuf->Map(0, nullptr, (void**)&upPtr);
    rbBuf->Map(0, nullptr, (void**)&rbPtr);
    auto barrier = [&](ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER x{};
        x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        x.Transition.pResource = r;
        x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        x.Transition.StateBefore = a;
        x.Transition.StateAfter = b;
        g.l->ResourceBarrier(1, &x);
    };
    int threeD = 0;
    for (const auto& frame : frames) {
        g.Idle();
        for (int y = 0; y < H; ++y) memcpy(upPtr + (size_t)y * inPitch, &frame[(size_t)y * W * 4], (size_t)W * 4);
        int r = -1;
        for (int rep = 0; rep < 3; ++rep) {
            g.Idle();
            g.al->Reset();
            g.l->Reset(g.al.Get(), nullptr);
            if (rep == 0) {
                barrier(bb.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                D3D12_TEXTURE_COPY_LOCATION to{bb.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}, from{upBuf.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
                from.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, (UINT)W, (UINT)H, 1, inPitch};
                g.l->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
                barrier(bb.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
            }
            video3d::Frame f;
            f.src = bb.Get();
            f.srcState = D3D12_RESOURCE_STATE_COMMON;
            f.srcViewFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            f.srcEncoding = video3d::kSrcLinear;
            f.filmW = (uint32_t)W;
            f.filmH = (uint32_t)H;
            f.rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
            f.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            f.outW = outW;
            f.outH = outH;
            r = video3d::Record(g.l.Get(), f);
            if (rep == 2) {
                barrier(outTex.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION to{rbBuf.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT}, from{outTex.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
                to.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, outW, outH, 1, outPitch};
                g.l->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
                barrier(outTex.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
            g.l->Close();
            ID3D12CommandList* ls[] = {g.l.Get()};
            g.q->ExecuteCommandLists(1, ls);
            if (r >= 0) video3d::Submitted();
            Sleep(4);   // the headset's frame time, roughly: the worker gets to run
        }
        g.Idle();
        threeD += r == 1;
        double diff = 0;
        for (UINT y = 0; y < outH; y += 4)
            for (UINT x = 0; x < (UINT)W; x += 4)
                diff += std::abs((int)rbPtr[(size_t)y * outPitch + x * 4] - (int)rbPtr[(size_t)y * outPitch + ((size_t)x + W) * 4]);
        if (lastEyeDiff) *lastEyeDiff = diff / ((outH / 4.0) * (W / 4.0));
        if (pairs)
            for (UINT y = 0; y < outH; ++y) fwrite(rbPtr + (size_t)y * outPitch, 1, (size_t)outW * 4, pairs);
    }
    return threeD;
}

int WithRuntime(int argc, char** argv) {
    std::wstring dir;
    for (const char* p = argv[1]; *p; ++p) dir += (wchar_t)*p;
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
    wchar_t full[MAX_PATH * 2] = {};
    GetFullPathNameW(dir.c_str(), (DWORD)(sizeof full / sizeof full[0]), full, nullptr);   // (the runtime is loaded by its full path, as in the game)
    video3d::SetRuntimeFolder(full);
    Gpu g;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g.dev)))) { std::printf("  no D3D12 device: the pass is not tried\n"); return 0; }
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
    g.dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g.q));
    g.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g.al));
    g.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.al.Get(), nullptr, IID_PPV_ARGS(&g.l));
    g.l->Close();
    g.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g.fence));
    g.ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    EXPECT(video3d::Start(g.dev.Get(), g.q.Get()), "the pipeline starts");

    std::vector<std::vector<uint8_t>> frames;
    int W = 960, H = 540;
    FILE* out = nullptr;
    if (argc >= 6) {
        W = atoi(argv[3]);
        H = atoi(argv[4]);
        FILE* in = fopen(argv[2], "rb");
        std::vector<uint8_t> f((size_t)W * H * 4);
        while (in && fread(f.data(), 1, f.size(), in) == f.size()) frames.push_back(f);
        if (in) fclose(in);
        out = fopen(argv[5], "wb");
    } else {
        // A made-up film: a floor going away under a sky, a box standing near in the middle. 40 frames, the box moving.
        for (int n = 0; n < 40; ++n) {
            std::vector<uint8_t> f((size_t)W * H * 4);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    uint8_t* p = &f[((size_t)y * W + x) * 4];
                    const bool floor = y > H / 2;
                    const int t = floor ? 60 + 150 * (y - H / 2) / (H / 2) : 200 - 80 * y / (H / 2);
                    const bool box = x > W / 3 + n * 2 && x < 2 * W / 3 + n * 2 && y > H / 4 && y < 3 * H / 4;
                    p[0] = (uint8_t)(box ? 200 : (floor ? t : t / 2));
                    p[1] = (uint8_t)(box ? 60 : (floor ? t * 3 / 4 : t * 3 / 4));
                    p[2] = (uint8_t)(box ? 40 : (floor ? t / 2 : 255));
                    p[3] = 255;
                    if (((x / 32) + (y / 32)) % 2 && floor) { p[0] /= 2; p[1] /= 2; }
                }
            frames.push_back(f);
        }
    }
    double eyeDiff = 0;
    // The network loads on the worker: the first frames may come back flat while it does.
    const int threeD = Drive(g, frames, W, H, out, &eyeDiff);
    if (out) fclose(out);
    std::printf("  %zu frames, %d in 3D; the last pair's eyes differ by %.2f levels on average\n", frames.size(), threeD, eyeDiff);
    EXPECT(threeD > 0, "the depth arrives and the eyes are drawn in 3D");
    EXPECT(eyeDiff > 0.5, "the two eyes are not the same picture");
    video3d::Stop();
    EXPECT(video3d::Record(g.l.Get(), video3d::Frame{}) == -1, "nothing is recorded after Stop");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    video3d::SetLog(&Log);
    std::printf("the steady depth map's rules\n");
    Rules();
    std::printf("the runtime loader with no runtime\n");
    LoaderWithoutRuntime();
    if (argc >= 2) {
        std::printf("the whole pass on a D3D12 device, runtime from %s\n", argv[1]);
        WithRuntime(argc, argv);
    }
    std::printf("video3d_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
