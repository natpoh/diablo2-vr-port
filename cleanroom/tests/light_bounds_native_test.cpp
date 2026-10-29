// [render] light_bounds_fix without ReShade (vr/light_bounds.h): the light bins' replacement, on a software D3D12 device (WARP).
// A shader of the test's own stands for the game's (cs_light_bin_depth_bounds_c is Blizzard's, never in the repository): the same
// bindings, and the game's mistake - a tile's row taken from its block height - so at the eyes' size (2496 x 2688) it misses rows as the
// game's does. Its pipeline is made as the game makes one, from a compute description and from a pipeline stream, and handed to
// lightbounds::OnComputePso / OnStreamPso as the plugin's D3D12 hooks hand it over; then
//   - Swap gives our copy for it (and nothing else for any other pipeline), and our copy, run in the game's root signature, gives every
//     tile's depth range as a scan of its pixels does, while the stand-in does not;
//   - with [render] light_bounds_fix off (at the creation, or now) or not bound (flat without light_bounds_flat), the game's own is bound;
//   - another shader of the same bindings is left alone.
//
//   light_bounds_native_test

#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../../vr/light_bounds.h"
#include "../../vr/light_bounds_shader.h"
#include "light_bounds_scene.h"

// The plugin's D3D12 hooks are not in this test: our own calls are not told to them anyway.
namespace natived3d {
void SetOurs(bool) {}
bool Ours() { return false; }
}  // namespace natived3d

using Microsoft::WRL::ComPtr;

namespace {

using namespace lbscene;

int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; printf("  FAIL: %s\n", what); }
}
void Must(HRESULT hr, const char* what) {
    if (FAILED(hr)) { printf("%s failed: 0x%08lX\n", what, (unsigned long)hr); exit(2); }
}
void Log(const char* s) { printf("  log: %s\n", s); }

uint64_t Fnv(const void* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= ((const uint8_t*)p)[i]; h *= 1099511628211ull; }
    return h;
}

ComPtr<ID3DBlob> Compile(const std::string& src, const char* what) {
    ComPtr<ID3DBlob> blob, errors;
    if (FAILED(D3DCompile(src.data(), src.size(), what, nullptr, nullptr, "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors))) {
        printf("%s did not compile: %s\n", what, errors ? (const char*)errors->GetBufferPointer() : "?");
        exit(2);
    }
    return blob;
}
// Our shader with one line changed
std::string Changed(const char* from, const char* to) {
    std::string s(lightbounds::kShader);
    const size_t at = s.find(from);
    if (at == std::string::npos) { printf("the shader has no '%s'\n", from); exit(2); }
    return s.replace(at, strlen(from), to);
}

struct Gpu {
    ComPtr<ID3D12Device> dev;
    ComPtr<ID3D12CommandQueue> q;
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue = 0;
    HANDLE event = nullptr;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12DescriptorHeap> heap;
    ComPtr<ID3D12Resource> depth, cb3, cb12, bounds, zeros, readback, upload;

    void Wait() {
        Must(q->Signal(fence.Get(), ++fenceValue), "signal");
        if (fence->GetCompletedValue() < fenceValue) {
            Must(fence->SetEventOnCompletion(fenceValue, event), "fence event");
            WaitForSingleObject(event, INFINITE);
        }
    }
    void Submit() {
        Must(list->Close(), "close");
        ID3D12CommandList* lists[] = {list.Get()};
        q->ExecuteCommandLists(1, lists);
        Wait();
        Must(alloc->Reset(), "allocator reset");
        Must(list->Reset(alloc.Get(), nullptr), "list reset");
    }
    ComPtr<ID3D12Resource> Buffer(D3D12_HEAP_TYPE type, UINT64 bytes, D3D12_RESOURCE_STATES state, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = type;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = bytes;
        rd.Height = rd.DepthOrArraySize = rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        rd.Flags = flags;
        ComPtr<ID3D12Resource> r;
        Must(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr, IID_PPV_ARGS(&r)), "buffer");
        return r;
    }
    void Fill(ID3D12Resource* r, const void* data, size_t bytes) {
        void* p = nullptr;
        Must(r->Map(0, nullptr, &p), "map");
        memcpy(p, data, bytes);
        r->Unmap(0, nullptr);
    }
};

// The game's root signature as our shader needs it: b3, b12 (root views), t10 (a table), u0 (a root view), s1 (static, point, clamp)
void MakeRoot(Gpu& g) {
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 10, 0, 0};
    D3D12_ROOT_PARAMETER p[4] = {};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[0].Descriptor = {3, 0};
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; p[1].Descriptor = {12, 0};
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; p[2].DescriptorTable = {1, &range};
    p[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; p[3].Descriptor = {0, 0};
    D3D12_STATIC_SAMPLER_DESC s{};
    s.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.MaxLOD = D3D12_FLOAT32_MAX;
    s.ShaderRegister = 1;
    D3D12_ROOT_SIGNATURE_DESC rd{4, p, 1, &s, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> blob, errors;
    Must(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors), "root signature serialized");
    Must(g.dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g.root)), "root signature");
}

// The scene: the eyes' size, the depth picture, the constants
void MakeScene(Gpu& g, const Case& c, const std::vector<float>& depth) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = c.w; td.Height = c.h; td.DepthOrArraySize = td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1;
    Must(g.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&g.depth)), "depth");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 total = 0;
    g.dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &total);
    g.upload = g.Buffer(D3D12_HEAP_TYPE_UPLOAD, total, D3D12_RESOURCE_STATE_GENERIC_READ);
    {
        uint8_t* p = nullptr;
        Must(g.upload->Map(0, nullptr, (void**)&p), "map upload");
        for (uint32_t y = 0; y < c.h; ++y) memcpy(p + fp.Offset + (size_t)y * fp.Footprint.RowPitch, &depth[(size_t)y * c.w], c.w * sizeof(float));
        g.upload->Unmap(0, nullptr);
    }
    D3D12_TEXTURE_COPY_LOCATION dst{g.depth.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{g.upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
    src.PlacedFootprint = fp;
    g.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {g.depth.Get(), 0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE};
    g.list->ResourceBarrier(1, &b);
    g.Submit();

    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    Must(g.dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g.heap)), "heap");
    g.dev->CreateShaderResourceView(g.depth.Get(), nullptr, g.heap->GetCPUDescriptorHandleForHeapStart());

    // b3: the lit area's size [1].xy, its uv -> the texture's: scale [3].xy, offset [2].zw (the whole texture here)
    float pass[64] = {};
    pass[4] = (float)c.w; pass[5] = (float)c.h;
    pass[12] = 1.0f; pass[13] = 1.0f;
    uint32_t grid[64] = {kCols, kRows, 0, 0, 0, kPitch, 0, 0};
    g.cb3 = g.Buffer(D3D12_HEAP_TYPE_UPLOAD, 256, D3D12_RESOURCE_STATE_GENERIC_READ);
    g.cb12 = g.Buffer(D3D12_HEAP_TYPE_UPLOAD, 256, D3D12_RESOURCE_STATE_GENERIC_READ);
    g.Fill(g.cb3.Get(), pass, sizeof pass);
    g.Fill(g.cb12.Get(), grid, sizeof grid);
    const UINT64 bytes = (UINT64)kPitch * kRows * sizeof(Pair);
    g.bounds = g.Buffer(D3D12_HEAP_TYPE_DEFAULT, bytes, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    g.zeros = g.Buffer(D3D12_HEAP_TYPE_UPLOAD, bytes, D3D12_RESOURCE_STATE_GENERIC_READ);
    std::vector<uint8_t> z((size_t)bytes, 0);
    g.Fill(g.zeros.Get(), z.data(), z.size());
    g.readback = g.Buffer(D3D12_HEAP_TYPE_READBACK, bytes, D3D12_RESOURCE_STATE_COPY_DEST);
}

// One dispatch of a pipeline in that root signature: every tile's (smallest, largest)
std::vector<Pair> Run(Gpu& g, ID3D12PipelineState* pso) {
    const UINT64 bytes = (UINT64)kPitch * kRows * sizeof(Pair);
    g.list->CopyBufferRegion(g.bounds.Get(), 0, g.zeros.Get(), 0, bytes);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {g.bounds.Get(), 0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
    g.list->ResourceBarrier(1, &b);
    ID3D12DescriptorHeap* heaps[] = {g.heap.Get()};
    g.list->SetDescriptorHeaps(1, heaps);
    g.list->SetComputeRootSignature(g.root.Get());
    g.list->SetPipelineState(pso);
    g.list->SetComputeRootConstantBufferView(0, g.cb3->GetGPUVirtualAddress());
    g.list->SetComputeRootConstantBufferView(1, g.cb12->GetGPUVirtualAddress());
    g.list->SetComputeRootDescriptorTable(2, g.heap->GetGPUDescriptorHandleForHeapStart());
    g.list->SetComputeRootUnorderedAccessView(3, g.bounds->GetGPUVirtualAddress());
    g.list->Dispatch(kCols, kRows, 1);
    b.Transition = {g.bounds.Get(), 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE};
    g.list->ResourceBarrier(1, &b);
    g.list->CopyBufferRegion(g.readback.Get(), 0, g.bounds.Get(), 0, bytes);
    b.Transition = {g.bounds.Get(), 0, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST};
    g.list->ResourceBarrier(1, &b);
    g.Submit();
    std::vector<Pair> r((size_t)kPitch * kRows);
    void* p = nullptr;
    const D3D12_RANGE whole{0, (SIZE_T)bytes};
    Must(g.readback->Map(0, &whole, &p), "map readback");
    memcpy(r.data(), p, (size_t)bytes);
    const D3D12_RANGE none{0, 0};
    g.readback->Unmap(0, &none);
    return r;
}

size_t Differences(const std::vector<Pair>& a, const std::vector<Pair>& b) {
    size_t n = 0;
    for (uint32_t ty = 0; ty < kRows; ++ty)
        for (uint32_t tx = 0; tx < kCols; ++tx) {
            const size_t i = (size_t)ty * kPitch + tx;
            if (memcmp(&a[i], &b[i], sizeof(Pair)) != 0) ++n;
        }
    return n;
}

ComPtr<ID3D12PipelineState> FromDesc(Gpu& g, ID3DBlob* cs, D3D12_COMPUTE_PIPELINE_STATE_DESC* keep) {
    *keep = D3D12_COMPUTE_PIPELINE_STATE_DESC{};
    keep->pRootSignature = g.root.Get();
    keep->CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    ComPtr<ID3D12PipelineState> p;
    Must(g.dev->CreateComputePipelineState(keep, IID_PPV_ARGS(&p)), "compute pipeline");
    return p;
}

// A stream as a game makes one: the root signature, the compute shader, the flags
struct alignas(void*) RootSub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; ID3D12RootSignature* value; };
struct alignas(void*) CsSub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; D3D12_SHADER_BYTECODE value; };
struct alignas(void*) FlagsSub { D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type; D3D12_PIPELINE_STATE_FLAGS value; };
struct Stream { RootSub root; CsSub cs; FlagsSub flags; };

ComPtr<ID3D12PipelineState> FromStream(Gpu& g, ID3DBlob* cs, Stream* s, D3D12_PIPELINE_STATE_STREAM_DESC* keep) {
    s->root = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, g.root.Get()};
    s->cs = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, {cs->GetBufferPointer(), cs->GetBufferSize()}};
    s->flags = {D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAG_NONE};
    *keep = D3D12_PIPELINE_STATE_STREAM_DESC{sizeof *s, s};
    ComPtr<ID3D12Device2> d2;
    Must(g.dev.As(&d2), "Device2");
    ComPtr<ID3D12PipelineState> p;
    Must(d2->CreatePipelineState(keep, IID_PPV_ARGS(&p)), "pipeline from a stream");
    return p;
}

}  // namespace

int main() {
    Gpu g;
    ComPtr<IDXGIFactory4> factory;
    Must(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
    ComPtr<IDXGIAdapter> warp;
    Must(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "WARP adapter");
    Must(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g.dev)), "D3D12 device (WARP)");
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
    Must(g.dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&g.q)), "queue");
    Must(g.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g.alloc)), "allocator");
    Must(g.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.alloc.Get(), nullptr, IID_PPV_ARGS(&g.list)), "list");
    Must(g.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g.fence)), "fence");
    g.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    MakeRoot(g);

    // The stand-in for the game's shader: the row from the block height, as the game's (vr/light_bounds_shader.h)
    ComPtr<ID3DBlob> game = Compile(Changed("uint2(i % blocks.x, i / blocks.x)", "uint2(i % blocks.y, i / blocks.y)"), "stand-in");
    // another shader of the same bindings: not the game's light bins
    ComPtr<ID3DBlob> other = Compile(Changed("float smallest = 1.0f;", "float smallest = 0.999f;"), "other");
    lightbounds::MatchForTest((uint32_t)game->GetBufferSize(), Fnv(game->GetBufferPointer(), game->GetBufferSize()));
    lightbounds::SetLog(&Log);

    const Case eye{"the eyes' size", 2496, 2688, false, false};
    const std::vector<float> depth = MakeDepth(eye, eye.w, eye.h, 0, 0);
    const std::vector<Pair> want = Scan(eye, depth, eye.w, eye.h, 0, 0);
    MakeScene(g, eye, depth);

    // 1. [render] light_bounds_fix off at the game's start: nothing made, the game's own bound - as before
    lightbounds::Configure(false, false, false, false);
    D3D12_COMPUTE_PIPELINE_STATE_DESC d0;
    ComPtr<ID3D12PipelineState> early = FromDesc(g, game.Get(), &d0);
    lightbounds::OnComputePso(g.dev.Get(), &d0, early.Get());
    lightbounds::Configure(true, true, false, false);
    Check(lightbounds::Swap(early.Get()) == early.Get(), "made with light_bounds_fix off: the game's own pipeline stays bound (as before)");

    // 2. light_bounds_fix on, made from a description
    D3D12_COMPUTE_PIPELINE_STATE_DESC d1;
    ComPtr<ID3D12PipelineState> fromDesc = FromDesc(g, game.Get(), &d1);
    lightbounds::OnComputePso(g.dev.Get(), &d1, fromDesc.Get());
    ID3D12PipelineState* ours = lightbounds::Swap(fromDesc.Get());
    Check(ours && ours != fromDesc.Get(), "a description: our copy bound in the game's place");
    const std::vector<Pair> gameGot = Run(g, fromDesc.Get());
    const size_t gameWrong = Differences(gameGot, want);
    Check(gameWrong > 0, "the stand-in (the game's mistake) misses rows at the eyes' size");
    const size_t oursWrong = ours ? Differences(Run(g, ours), want) : 1;
    Check(oursWrong == 0, "our copy, in the game's root signature: every tile's depth range as the scan's");
    printf("the eyes' size %ux%u (tiles %.1f x %.1f px): the game's way wrong in %zu of %u tiles, ours in %zu\n", eye.w, eye.h,
           eye.w / 32.0, eye.h / 20.0, gameWrong, kCols * kRows, oursWrong);

    // 3. not bound: [render] light_bounds_fix off now, or flat without light_bounds_flat
    lightbounds::Configure(false, true, false, false);
    Check(lightbounds::Swap(fromDesc.Get()) == fromDesc.Get(), "light_bounds_fix off: the game's own bound");
    lightbounds::Configure(true, false, false, true);
    Check(lightbounds::Swap(fromDesc.Get()) == fromDesc.Get(), "not bound (flat, light_bounds_flat off): the game's own bound");
    lightbounds::Configure(true, true, false, false);
    Check(lightbounds::Swap(fromDesc.Get()) == ours, "on again: ours bound again");

    // 4. made from a stream
    Stream s;
    D3D12_PIPELINE_STATE_STREAM_DESC sd;
    ComPtr<ID3D12PipelineState> fromStream = FromStream(g, game.Get(), &s, &sd);
    lightbounds::OnStreamPso(g.dev.Get(), &sd, fromStream.Get());
    ID3D12PipelineState* oursS = lightbounds::Swap(fromStream.Get());
    Check(oursS && oursS != fromStream.Get() && oursS != ours, "a stream: a copy of its own bound in the game's place");
    Check(!oursS || Differences(Run(g, oursS), want) == 0, "the stream's copy: every tile's depth range as the scan's");

    // 5. anything else left alone
    D3D12_COMPUTE_PIPELINE_STATE_DESC d2;
    ComPtr<ID3D12PipelineState> notIt = FromDesc(g, other.Get(), &d2);
    lightbounds::OnComputePso(g.dev.Get(), &d2, notIt.Get());
    Check(lightbounds::Swap(notIt.Get()) == notIt.Get(), "another compute shader: left alone");
    Stream s2;
    ComPtr<ID3D12PipelineState> notItS = FromStream(g, other.Get(), &s2, &sd);
    lightbounds::OnStreamPso(g.dev.Get(), &sd, notItS.Get());
    Check(lightbounds::Swap(notItS.Get()) == notItS.Get(), "another stream: left alone");
    Check(lightbounds::Swap(nullptr) == nullptr, "no pipeline: none");

    // 6. the log's line on the size: found, bound
    lightbounds::NoteScreen(eye.w, eye.h);

    CloseHandle(g.event);
    printf("light_bounds_native_test: %d of %d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
