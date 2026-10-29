// effects_layer_native_test - vr/effects_layer.cpp fed from the plugin's own hooks ([render] no_reshade): on a WARP device two half-float targets
// of the scene's size bound with a depth-stencil of that size are the game's effects layer, their states from the barriers on them; a pair of
// another format or size is not; not wanted, nothing is looked at; a new pair lets the old one's references go (they are held: no destroy event).
#include "effects_layer.h"
#include "native_d3d12.h"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <windows.h>

#include <cstdio>
#include <unordered_map>

static std::unordered_map<uint64_t, ID3D12Resource*> g_rtvs;
namespace natived3d {
ID3D12Resource* RtvResource(uint64_t h) { const auto it = g_rtvs.find(h); return it != g_rtvs.end() ? it->second : nullptr; }
unsigned RtvIncrement() { return 32; }
}  // namespace natived3d

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
static void Log(const char* s) { std::printf("  %s\n", s); }

static ID3D12Resource* Tex(ID3D12Device* dev, DXGI_FORMAT f, UINT w, UINT h, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES s) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Flags = flags;
    ID3D12Resource* r = nullptr;
    dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, s, nullptr, __uuidof(ID3D12Resource), (void**)&r);
    return r;
}
static ULONG Refs(ID3D12Resource* r) { r->AddRef(); return r->Release(); }

int main() {
    using CreateFactoryFn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    auto createFactory = (CreateFactoryFn)GetProcAddress(LoadLibraryA("dxgi.dll"), "CreateDXGIFactory2");
    IDXGIFactory4* fac = nullptr;
    IDXGIAdapter* warp = nullptr;
    ID3D12Device* dev = nullptr;
    CHECK(createFactory && SUCCEEDED(createFactory(0, __uuidof(IDXGIFactory4), (void**)&fac)) &&
          SUCCEEDED(fac->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void**)&warp)) &&
          SUCCEEDED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev)));
    if (!dev) return 1;
    effectslayer::SetLogNative(&Log);
    const auto rt = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource* a = Tex(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, 64, 32, rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource* b = Tex(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, 64, 32, rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource* other = Tex(dev, DXGI_FORMAT_R11G11B10_FLOAT, 64, 32, rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource* ds = Tex(dev, DXGI_FORMAT_R24G8_TYPELESS, 64, 32, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    ID3D12Resource* tinyDs = Tex(dev, DXGI_FORMAT_R24G8_TYPELESS, 32, 16, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    CHECK(a && b && other && ds && tinyDs);
    g_rtvs[1000] = a; g_rtvs[1032] = b; g_rtvs[2000] = other;
    const D3D12_CPU_DESCRIPTOR_HANDLE pair[2] = {{1000}, {1032}}, mixed[2] = {{1000}, {2000}};
    effectslayer::Layer l;
    // not wanted: nothing looked at
    effectslayer::OnBindNative(2, pair, false, ds);
    CHECK(!effectslayer::Latest(l));
    effectslayer::SetWanted(true);
    // not the layer: another format, another size
    effectslayer::OnBindNative(2, mixed, false, ds);
    effectslayer::OnBindNative(2, pair, false, tinyDs);
    CHECK(!effectslayer::Latest(l) && l.targets[0] == nullptr);
    // the layer (one handle, two in a row too), held; its states unknown until the game's barriers
    const ULONG before = Refs(a);
    effectslayer::OnBindNative(2, pair, true, ds);
    CHECK(Refs(a) == before + 1 && Refs(b) == before + 1);
    CHECK(!effectslayer::Latest(l));
    D3D12_RESOURCE_BARRIER br[2] = {};
    for (int i = 0; i < 2; ++i) {
        br[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        br[i].Transition = {i ? b : a, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    }
    effectslayer::OnBarrierNative(2, br);
    CHECK(effectslayer::Latest(l) && l.targets[0] == a && l.targets[1] == b && l.width == 64 && l.height == 32);
    CHECK(l.states[0] == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE && l.states[1] == D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    br[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    effectslayer::OnBarrierNative(1, br);
    CHECK(effectslayer::Latest(l) && l.states[0] == D3D12_RESOURCE_STATE_COMMON);
    // the same pair again: nothing new held
    effectslayer::OnBindNative(2, pair, false, ds);
    CHECK(Refs(a) == before + 1);
    // a new pair: the old one let go, the new one's states unknown again
    ID3D12Resource* c = Tex(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, 64, 32, rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource* d = Tex(dev, DXGI_FORMAT_R16G16B16A16_FLOAT, 64, 32, rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    g_rtvs[3000] = c; g_rtvs[3032] = d;
    const D3D12_CPU_DESCRIPTOR_HANDLE pair2[2] = {{3000}, {3032}};
    effectslayer::OnBindNative(2, pair2, false, ds);
    CHECK(Refs(a) == before && Refs(b) == before);
    CHECK(!effectslayer::Latest(l) && l.targets[0] == c);
    std::printf(g_fail ? "effects_layer_native_test: %d FAILED\n" : "effects_layer_native_test: the game's effects layer found from the hooks, its states "
                "from the barriers, held while it is the layer: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
