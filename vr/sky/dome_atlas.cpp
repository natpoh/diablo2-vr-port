// vr/sky/dome_atlas.cpp - [sky] dome_in_game_colour: the game samples a texture of ours for the dome, baked by the time of day - see
// vr/sky/dome_atlas.h. Self-contained (the device, the game's queue and a list of ours given by the caller): tools/dome_atlas_test.cpp runs it.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "dome_atlas.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace domeatlas {
namespace {

constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr int kCave = 5;            // the cave dome's band (shared/sky_atlas.h kBands): black, cleared once its tiles are mapped
constexpr int kBands = 6;
constexpr int kGainEvery = 12;      // presents between bakes while the effect measures the gain (it eases slowly)
constexpr int kSets = 16, kSetSize = 4;   // the shader-visible views: a set a bake, kSets in turn (a bake still on the card keeps its own)
constexpr int kRetireFrames = 300;
constexpr ULONGLONG kStandInMs = 3000;   // our bake ran this lately: the next view of the atlas made on ours
constexpr DXGI_FORMAT kOurs = DXGI_FORMAT_R11G11B10_FLOAT;

LogFn g_log = nullptr;
void Line(const char* fmt, ...) {
    if (!g_log) return;
    char b[640];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}
template <class T> void Free(T*& p) { if (p) { p->Release(); p = nullptr; } }

// The bake: one texel of ours from the game's own (Day, the same layout and mip), the act's night pictures laid round as
// tools/game_sky/make_sky_dome.py lays the day ones (panorama(): column = azimuth, row = elevation 90 .. -12; the band two turns round, 65
// degrees up to 8 under; the cap one tile overhead from 40 to 62 degrees; under the horizon into its 0 degree row's mean, all of it from -6)
// in its scene units (sRGB -> linear -> Rec.2020: scene_values), mixed by the night's share, times the brightness and the measured gain.
const char kShader[] = R"(
cbuffer C : register(b0) { float4 c0; float4 c1; float4 c2; };   // c0: band, its rows, width, mip; c1: night, mul, cap on, gain on; c2: zenith
Texture2D<float4> Day : register(t0);
Texture2D<float4> NightBand : register(t1);
Texture2D<float4> NightCap : register(t2);
Texture2D<float4> Gain : register(t3);
SamplerState BandSmp : register(s0);
SamplerState CapSmp : register(s1);
float3 Scene(float3 srgb)
{
    const float3 c = saturate(srgb);
    const float3 lin = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
    return float3(dot(float3(0.6274040, 0.3292820, 0.0433136), lin), dot(float3(0.0690970, 0.9195400, 0.0113612), lin),
                  dot(float3(0.0163916, 0.0880132, 0.8955950), lin));
}
float3 NightAt(float az, float el)
{
    const float ce = cos(radians(el));
    const float3 d = float3(ce * sin(az), sin(radians(el)), ce * cos(az));
    float3 c = NightBand.SampleLevel(BandSmp, float2(frac(atan2(d.x, d.z) / 3.14159265), (65.0 - el) / 73.0), 0).rgb;
    const float w = smoothstep(40.0, 62.0, el);
    if (w > 0.0) {
        float3 z = c2.xyz;
        if (c1.z > 0.5) {
            const float py = max(d.y, 0.2);
            z = NightCap.SampleLevel(CapSmp, float2(d.x / py * 0.4, d.z / py * 0.4), 0).rgb;
        }
        c = lerp(c, z, w);
    }
    return Scene(c);
}
float3 GainNow()
{
    if (c1.w < 0.5) return 1.0;
    const float4 g = Gain.Load(int3(0, 0, 0));
    return g.a > 0.5 ? g.rgb : 1.0;
}
void VS(uint id : SV_VertexID, out float4 pos : SV_Position)
{
    const float2 t = float2((id << 1) & 2, id & 2);
    pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float4 PS(float4 pos : SV_Position) : SV_Target
{
    float3 c = Day.Load(int3((int2)pos.xy, (int)c0.w)).rgb;
    [branch] if (c1.x > 0.0) {
        const float az = pos.x / c0.z * 6.28318531;
        const float el = 90.0 - (pos.y - c0.x * c0.y) / c0.y * 102.0;
        float3 n = NightAt(az, el);
        [branch] if (el < 0.0) {
            float3 m = 0.0;
            [loop] for (int i = 0; i < 16; ++i) m += NightAt((i + 0.5) / 16.0 * 6.28318531, 0.0);
            n = lerp(n, m / 16.0, smoothstep(0.0, -6.0, el));
        }
        c = lerp(c, n, saturate(c1.x));
    }
    return float4(c * c1.y * GainNow(), 1.0);
}
float4 PSGain(float4 pos : SV_Position) : SV_Target { return float4(GainNow(), 1.0); }
)";

struct Region { D3D12_TILED_RESOURCE_COORDINATE at; D3D12_TILE_REGION_SIZE size; };

std::mutex g_mx;
ID3D12Device* g_dev = nullptr;   // the game's (the presents'); no reference of ours
bool g_on = false;
ULONGLONG g_lastPresent = 0;
// The game's last view of the picture made on ours. Only then, in its own call: a slot it made long ago may since hold another view, or
// its heap be gone - nothing of ours is ever written there later. So the key ticked live counts from the game's next view of the picture
// (its next start); unticked live, ours is baked as the game's own picture, texel for texel, and our effect recolours as before.
bool g_ours = false;
ID3D12Resource* g_atlas = nullptr;   // the game's picture (a reference of ours: the bake's day)
// ours
ID3D12Resource* g_tex = nullptr;
bool g_reserved = false;
uint32_t g_w = 0, g_h = 0, g_mips = 0, g_bandRows = 0;
ID3D12Heap* g_heap = nullptr;
UINT g_heapTiles = 0;
int g_mapped = -2;                   // the act's band the tiles are mapped for (-2: none yet)
bool g_clear = false;                // mapped (or made) since the last clear: what is backed cleared to black first
D3D12_PACKED_MIP_INFO g_packed{};
D3D12_TILE_SHAPE g_shape{};
D3D12_SUBRESOURCE_TILING g_tiling[16] = {};
ID3D12Resource* g_gain = nullptr;    // 1 x 1 RGBA16F: the gain the last bake used
bool g_gainCleared = false;
ID3D12DescriptorHeap* g_rtvs = nullptr;   // our texture's mips, then the gain's
ID3D12DescriptorHeap* g_srvs = nullptr;   // shader-visible, kSets sets of kSetSize
UINT g_rtvInc = 0, g_srvInc = 0, g_set = 0;
// the pipeline (made once)
bool g_pipeTried = false, g_pipeOk = false;
ID3D12RootSignature* g_root = nullptr;
ID3D12PipelineState* g_pso = nullptr;
ID3D12PipelineState* g_psoGain = nullptr;
// the last bake
bool g_baked = false;
int g_sinceBake = 0;
BakeIn g_last{};
std::vector<std::pair<IUnknown*, int>> g_retired;

void Retire(IUnknown* p) { if (p) g_retired.push_back({p, kRetireFrames}); }

bool MakePipeline(ID3D12Device* dev) {
    if (g_pipeTried) return g_pipeOk;
    g_pipeTried = true;
    const HMODULE dc = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const pD3DCompile compile = dc ? (pD3DCompile)GetProcAddress(dc, "D3DCompile") : nullptr;
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    const HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    const SerializeFn serialize = d12 ? (SerializeFn)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
    if (!compile || !serialize) {
        Line("vrcam: game sky - the dome's colours in the game: no %s - the game's own picture, our effect recolours it as before",
             !compile ? "shader compiler (d3dcompiler_47.dll)" : "D3D12SerializeRootSignature");
        if (dc) FreeLibrary(dc);
        return false;
    }
    std::vector<uint8_t> code[3];
    std::string errors;
    const char* entries[3] = {"VS", "PS", "PSGain"};
    for (int i = 0; i < 3; ++i) {
        ID3DBlob* b = nullptr;
        ID3DBlob* e = nullptr;
        if (SUCCEEDED(compile(kShader, sizeof kShader - 1, "d2rvr_dome_atlas", nullptr, nullptr, entries[i], i ? "ps_5_0" : "vs_5_0",
                              D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &e))) {
            const uint8_t* p = (const uint8_t*)b->GetBufferPointer();
            code[i].assign(p, p + b->GetBufferSize());
        } else if (errors.empty()) {
            errors = e ? (const char*)e->GetBufferPointer() : "?";
        }
        Free(b);
        Free(e);
    }
    FreeLibrary(dc);
    if (!errors.empty()) {
        Line("vrcam: game sky - the dome's colours in the game: the bake's shader did not compile (%s) - the game's own picture", errors.c_str());
        return false;
    }
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kSetSize, 0, 0, 0};
    D3D12_ROOT_PARAMETER rp[2] = {};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[0].DescriptorTable = {1, &range};
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[1].Constants = {0, 0, 12};
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC smp[2] = {};
    for (int i = 0; i < 2; ++i) {
        smp[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        smp[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        smp[i].AddressV = i ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;   // (the band: two turns round, clamped up and down)
        smp[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        smp[i].MaxLOD = D3D12_FLOAT32_MAX;
        smp[i].ShaderRegister = (UINT)i;
        smp[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    const D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 2, smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    if (SUCCEEDED(serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)))
        dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_root));
    Free(blob);
    Free(err);
    auto pso = [&](const std::vector<uint8_t>& ps, DXGI_FORMAT f, ID3D12PipelineState** out) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = g_root;
        d.VS = {code[0].data(), code[0].size()};
        d.PS = {ps.data(), ps.size()};
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.SampleMask = UINT_MAX;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = f;
        d.SampleDesc.Count = 1;
        return g_root && SUCCEEDED(dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(out)));
    };
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kSets * kSetSize;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC gd{};
    gd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    gd.Width = gd.Height = 1;
    gd.DepthOrArraySize = gd.MipLevels = 1;
    gd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    gd.SampleDesc.Count = 1;
    gd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    g_pipeOk = pso(code[1], kOurs, &g_pso) && pso(code[2], DXGI_FORMAT_R16G16B16A16_FLOAT, &g_psoGain) &&
               SUCCEEDED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_srvs))) &&
               SUCCEEDED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &gd, kRead, nullptr, IID_PPV_ARGS(&g_gain)));
    if (!g_pipeOk) {
        Line("vrcam: game sky - the dome's colours in the game: the bake's pipeline could not be made - the game's own picture");
        return false;
    }
    g_gain->SetName(L"d2rvr dome baked gain");
    g_srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    return true;
}

// The tile regions backing `band` and the cave's, every mip (packed mips whole); their tiles
UINT RegionsFor(int band, std::vector<Region>* out) {
    out->clear();
    UINT tiles = 0;
    for (UINT m = 0; m < g_packed.NumStandardMips && m < 16; ++m) {
        uint32_t a0 = 0, a1 = 0, b0 = 0, b1 = 0;
        BandTileRows(band, g_bandRows, m, g_shape.HeightInTexels, &a0, &a1);
        BandTileRows(kCave, g_bandRows, m, g_shape.HeightInTexels, &b0, &b1);
        a1 = std::min<uint32_t>(a1, g_tiling[m].HeightInTiles);
        b1 = std::min<uint32_t>(b1, g_tiling[m].HeightInTiles);
        std::pair<uint32_t, uint32_t> r[2] = {{a0, a1}, {b0, b1}};
        int n = 2;
        if (r[1].first <= r[0].second && r[0].first <= r[1].second) {   // (touching or shared: one)
            r[0] = {std::min(a0, b0), std::max(a1, b1)};
            n = 1;
        }
        for (int i = 0; i < n; ++i) {
            if (r[i].second <= r[i].first) continue;
            Region g{};
            g.at.X = 0;
            g.at.Y = r[i].first;
            g.at.Subresource = m;
            g.size.UseBox = TRUE;
            g.size.Width = g_tiling[m].WidthInTiles;
            g.size.Height = (UINT16)(r[i].second - r[i].first);
            g.size.Depth = 1;
            g.size.NumTiles = g.size.Width * g.size.Height;
            tiles += g.size.NumTiles;
            out->push_back(g);
        }
    }
    if (g_packed.NumPackedMips) {
        Region g{};
        g.at.Subresource = g_packed.NumStandardMips;
        g.size.NumTiles = g_packed.NumTilesForPackedMips;
        tiles += g.size.NumTiles;
        out->push_back(g);
    }
    return tiles;
}

// Ours for the atlas as it is now (its size and mips): made again only for another size
bool EnsureTexture(ID3D12Device* dev) {
    if (!g_atlas) return false;
    const D3D12_RESOURCE_DESC ad = g_atlas->GetDesc();
    if (g_tex && g_w == ad.Width && g_h == ad.Height && g_mips == ad.MipLevels) return true;
    Retire(g_tex);
    g_tex = nullptr;
    Retire(g_heap);
    g_heap = nullptr;
    Retire(g_rtvs);
    g_rtvs = nullptr;
    g_w = (uint32_t)ad.Width;
    g_h = ad.Height;
    g_mips = std::min<uint32_t>(ad.MipLevels, 16);
    g_bandRows = g_h / kBands;
    g_mapped = -2;
    g_baked = false;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = g_w;
    rd.Height = g_h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = (UINT16)g_mips;
    rd.Format = kOurs;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_FEATURE_DATA_D3D12_OPTIONS o{};
    const bool tier2 = SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof o)) &&
                       o.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_2;
    g_reserved = false;
    if (tier2) {
        rd.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
        if (SUCCEEDED(dev->CreateReservedResource(&rd, kRead, nullptr, IID_PPV_ARGS(&g_tex)))) {
            UINT n = 0, subs = g_mips;
            dev->GetResourceTiling(g_tex, &n, &g_packed, &g_shape, &subs, 0, g_tiling);
            std::vector<Region> r;
            UINT most = 0;
            for (int b = 0; b < kBands - 1; ++b) most = std::max(most, RegionsFor(b, &r));
            D3D12_HEAP_DESC hd{};
            hd.SizeInBytes = (UINT64)most * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
            hd.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
            hd.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
            if (most && SUCCEEDED(dev->CreateHeap(&hd, IID_PPV_ARGS(&g_heap)))) {
                g_heapTiles = most;
                g_reserved = true;
            } else {
                Free(g_tex);
            }
        }
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    }
    if (!g_tex) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, kRead, nullptr, IID_PPV_ARGS(&g_tex)))) {
            g_tex = nullptr;
            Line("vrcam: game sky - the dome's colours in the game: our texture (%ux%u, %u mips) could not be made - the game's own picture", g_w, g_h, g_mips);
            return false;
        }
    }
    g_tex->SetName(L"d2rvr dome atlas");
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = g_mips + 1;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_rtvs)))) {
        g_rtvs = nullptr;
        Free(g_tex);
        return false;
    }
    for (uint32_t m = 0; m <= g_mips; ++m) {
        D3D12_RENDER_TARGET_VIEW_DESC r{};
        r.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        r.Format = m < g_mips ? kOurs : DXGI_FORMAT_R16G16B16A16_FLOAT;
        r.Texture2D.MipSlice = m < g_mips ? m : 0;
        D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvs->GetCPUDescriptorHandleForHeapStart();
        h.ptr += (SIZE_T)m * g_rtvInc;
        dev->CreateRenderTargetView(m < g_mips ? g_tex : g_gain, &r, h);
    }
    g_clear = !g_reserved;   // (made whole: black before the first bake; reserved: once its tiles are mapped)
    Line("vrcam: game sky - the dome's colours in the game: our texture made, %ux%u, %u mips, R11G11B10 - %s", g_w, g_h, g_mips,
         g_reserved ? "only the shown act's band and the cave's backed (reserved, tiled resources tier 2+)"
                    : "all of it backed (the card has no tiled resources tier 2)");
    return true;
}

// The tiles of `band` and the cave's mapped (on q, before our list): the rest unbacked
bool Map(ID3D12CommandQueue* q, int band) {
    std::vector<Region> r;
    const UINT tiles = RegionsFor(band, &r);
    if (!tiles || tiles > g_heapTiles) return false;
    const D3D12_TILE_RANGE_FLAGS none = D3D12_TILE_RANGE_FLAG_NULL;
    q->UpdateTileMappings(g_tex, 1, nullptr, nullptr, nullptr, 1, &none, nullptr, nullptr, D3D12_TILE_MAPPING_FLAG_NONE);
    std::vector<D3D12_TILED_RESOURCE_COORDINATE> at;
    std::vector<D3D12_TILE_REGION_SIZE> size;
    for (const Region& g : r) { at.push_back(g.at); size.push_back(g.size); }
    const D3D12_TILE_RANGE_FLAGS flag = D3D12_TILE_RANGE_FLAG_NONE;
    const UINT start = 0;
    q->UpdateTileMappings(g_tex, (UINT)r.size(), at.data(), size.data(), g_heap, 1, &flag, &start, &tiles, D3D12_TILE_MAPPING_FLAG_NONE);
    return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE Rtv(uint32_t i) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvs->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)i * g_rtvInc;
    return h;
}

void Bake(ID3D12Device* dev, ID3D12GraphicsCommandList* l, const BakeIn& in) {
    // the views: the game's picture (every mip), the night pictures, the gain
    const UINT set = g_set++ % kSets;
    D3D12_CPU_DESCRIPTOR_HANDLE c = g_srvs->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE g = g_srvs->GetGPUDescriptorHandleForHeapStart();
    c.ptr += (SIZE_T)set * kSetSize * g_srvInc;
    g.ptr += (UINT64)set * kSetSize * g_srvInc;
    ID3D12Resource* res[kSetSize] = {g_atlas, in.nightBand, in.nightCap, in.gain};
    for (int i = 0; i < kSetSize; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Texture2D.MipLevels = (UINT)-1;
        v.Format = res[i] ? res[i]->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM;
        if (v.Format == DXGI_FORMAT_BC6H_TYPELESS) v.Format = DXGI_FORMAT_BC6H_UF16;
        else if (v.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        else if (v.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS) v.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (!res[i]) v.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE at = c;
        at.ptr += (SIZE_T)i * g_srvInc;
        dev->CreateShaderResourceView(res[i], &v, at);
    }
    D3D12_RESOURCE_BARRIER br[2] = {};
    for (int i = 0; i < 2; ++i) {
        br[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        br[i].Transition = {i ? g_gain : g_tex, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, kRead, D3D12_RESOURCE_STATE_RENDER_TARGET};
    }
    l->ResourceBarrier(2, br);
    if (!g_gainCleared) {   // (a = 0: no bake's gain yet)
        const float none[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        l->ClearRenderTargetView(Rtv(g_mips), none, 0, nullptr);
        g_gainCleared = true;
    }
    const float black[4] = {};
    if (g_clear) {   // what is backed, black: the cave's band stays so, the act's is baked over
        g_clear = false;
        for (uint32_t m = 0; m < g_mips; ++m) {
            if (!g_reserved || m >= g_packed.NumStandardMips) {
                l->ClearRenderTargetView(Rtv(m), black, 0, nullptr);
                continue;
            }
            std::vector<Region> r;
            RegionsFor(in.band, &r);
            std::vector<D3D12_RECT> rects;
            const LONG mw = (LONG)std::max(1u, g_w >> m), mh = (LONG)std::max(1u, g_h >> m);
            for (const Region& x : r)
                if (x.size.UseBox && x.at.Subresource == m)
                    rects.push_back({0, (LONG)(x.at.Y * g_shape.HeightInTexels), mw,
                                     std::min(mh, (LONG)((x.at.Y + x.size.Height) * g_shape.HeightInTexels))});
            if (!rects.empty()) l->ClearRenderTargetView(Rtv(m), black, (UINT)rects.size(), rects.data());
        }
    }
    ID3D12DescriptorHeap* heaps[] = {g_srvs};
    l->SetDescriptorHeaps(1, heaps);
    l->SetGraphicsRootSignature(g_root);
    l->SetGraphicsRootDescriptorTable(0, g);
    l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const bool night = in.night > 0.0f && in.nightBand;
    float k[12] = {(float)in.band, 0.0f, 0.0f, 0.0f, night ? in.night : 0.0f, in.mul, in.capOn && in.nightCap ? 1.0f : 0.0f, in.gain ? 1.0f : 0.0f,
                   in.zenith[0], in.zenith[1], in.zenith[2], 0.0f};
    l->SetPipelineState(g_pso);
    for (uint32_t m = 0; m < g_mips; ++m) {
        uint32_t from = 0, to = 0;
        BandRows(in.band, g_bandRows, m, &from, &to);
        if (to <= from) break;
        const uint32_t mw = std::max(1u, g_w >> m);
        k[1] = (float)(to - from);
        k[2] = (float)mw;
        k[3] = (float)m;
        l->SetGraphicsRoot32BitConstants(1, 12, k, 0);
        const D3D12_CPU_DESCRIPTOR_HANDLE rt = Rtv(m);
        l->OMSetRenderTargets(1, &rt, FALSE, nullptr);
        const D3D12_VIEWPORT vp{0.0f, (float)from, (float)mw, (float)(to - from), 0.0f, 1.0f};
        const D3D12_RECT sc{0, (LONG)from, (LONG)mw, (LONG)to};
        l->RSSetViewports(1, &vp);
        l->RSSetScissorRects(1, &sc);
        l->DrawInstanced(3, 1, 0, 0);
    }
    {   // the gain it used, for the effect's next measure
        l->SetPipelineState(g_psoGain);
        l->SetGraphicsRoot32BitConstants(1, 12, k, 0);
        const D3D12_CPU_DESCRIPTOR_HANDLE rt = Rtv(g_mips);
        l->OMSetRenderTargets(1, &rt, FALSE, nullptr);
        const D3D12_VIEWPORT vp{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        const D3D12_RECT sc{0, 0, 1, 1};
        l->RSSetViewports(1, &vp);
        l->RSSetScissorRects(1, &sc);
        l->DrawInstanced(3, 1, 0, 0);
    }
    for (D3D12_RESOURCE_BARRIER& b : br) std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    l->ResourceBarrier(2, br);
}

bool Differs(const BakeIn& a, const BakeIn& b) {
    auto off = [](float x, float y) { return std::fabs(x - y) > 0.002f * std::max(1.0f, std::fabs(y)); };
    return a.band != b.band || off(a.night, b.night) || off(a.mul, b.mul) || a.capOn != b.capOn || a.nightBand != b.nightBand ||
           a.nightCap != b.nightCap || (a.gain != nullptr) != (b.gain != nullptr) || off(a.zenith[0], b.zenith[0]) ||
           off(a.zenith[1], b.zenith[1]) || off(a.zenith[2], b.zenith[2]);
}

}  // namespace

void SetLog(LogFn log) { g_log = log; }

bool StandIn(ID3D12Device* dev, ID3D12Resource* atlas, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE slot,
             CreateSrvFn create) {
    if (!dev || !atlas || !create || (desc && desc->ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2D)) return false;
    std::lock_guard<std::mutex> lock(g_mx);
    if (atlas != g_atlas) {
        Retire(g_atlas);
        atlas->AddRef();
        g_atlas = atlas;
    }
    g_ours = false;
    // (the pipeline made by the presents already: no compile on the game's thread)
    const bool running = dev == g_dev && GetTickCount64() - g_lastPresent < kStandInMs;
    if (!g_on || !running || !g_pipeOk || !EnsureTexture(dev)) {
        Line("vrcam: game sky - the game's view of the domes' picture left to it (%s)",
             !running ? "our effects do not run: ReShade's path, or [render] native_effects=0" : !g_on ? "[sky] dome_in_game_colour=0"
             : "nothing of ours to stand in, above");
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC v{};
    v.Format = kOurs;
    v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    v.Shader4ComponentMapping = desc ? desc->Shader4ComponentMapping : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (desc) {
        v.Texture2D = desc->Texture2D;
        v.Texture2D.PlaneSlice = 0;
    } else {
        v.Texture2D.MipLevels = (UINT)-1;
    }
    create(dev, g_tex, &v, slot);
    g_ours = true;
    g_baked = false;   // (black until the next present bakes it: the game draws the dome seconds after it made the view)
    Line("vrcam: game sky - the game's view of the domes' picture made on OURS (slot %zx): the game draws the dome in the colours our bake gives "
         "it ([sky] dome_in_game_colour)", (size_t)slot.ptr);
    return true;
}

void Present(ID3D12Device* dev, ID3D12CommandQueue* q, ID3D12GraphicsCommandList* l, const BakeIn& in) {
    std::lock_guard<std::mutex> lock(g_mx);
    for (auto it = g_retired.begin(); it != g_retired.end();) {
        if (--it->second <= 0) { it->first->Release(); it = g_retired.erase(it); }
        else ++it;
    }
    if (!dev) return;
    if (!g_dev) g_dev = dev;
    if (dev != g_dev) return;
    if (in.on && !g_pipeTried) {
        MakePipeline(dev);
        if (g_pipeOk) Line("vrcam: game sky - the dome's colours in the game: the bake is ready; the game's next view of the domes' picture is made on ours");
    }
    g_on = in.on && g_pipeOk;
    g_lastPresent = GetTickCount64();
    if (!g_ours || !g_tex || !q || !l) return;
    const int band = in.band >= 0 && in.band < kCave ? in.band : g_mapped >= 0 ? g_mapped : -1;
    if (band < 0) return;   // (no act's dome shown yet: nothing to bake for)
    bool must = !g_baked;
    if (band != g_mapped) {
        if (g_reserved && !Map(q, band)) {
            Line("vrcam: game sky - the dome's colours in the game: band %d's tiles could not be mapped - left as it was", band);
            return;
        }
        g_clear = g_clear || g_reserved;
        g_mapped = band;
        must = true;
    }
    BakeIn now = in;
    now.band = band;
    if (!g_on) {   // unticked live: the game's own picture as it is (our effect recolours it again)
        now.night = 0.0f;
        now.mul = 1.0f;
        now.gain = nullptr;
    }
    ++g_sinceBake;
    if (must || Differs(now, g_last) || (now.gain && g_sinceBake >= kGainEvery)) {
        Bake(dev, l, now);
        if (!g_baked)
            Line("vrcam: game sky - the dome's colours in the game: baked, act %d's band, night %.2f, x%.2f, %s", band + 1, now.night, now.mul,
                 !g_on ? "the game's own picture as it is ([sky] dome_in_game_colour unticked)"
                 : now.gain ? "times the effect's measured gain" : "no gain measured ([sky] dome_true_colour=0: the game's own tone)");
        g_baked = true;
        g_last = now;
        g_sinceBake = 0;
    }
}

bool Live() {
    std::lock_guard<std::mutex> lock(g_mx);
    return g_on && g_baked && g_ours;
}

bool Ours() {
    std::lock_guard<std::mutex> lock(g_mx);
    return g_ours;
}

ID3D12Resource* OurTexture() {
    std::lock_guard<std::mutex> lock(g_mx);
    return g_tex;
}

ID3D12Resource* BakedGain() {
    std::lock_guard<std::mutex> lock(g_mx);
    return g_gain;
}

}  // namespace domeatlas
