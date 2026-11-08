#pragma once
// vrs_probe.h - does this PC's graphics card have variable rate shading tier 2 (what [render] foveated and grass_lighting_coarse need)?
// Asked of D3D12 itself, before the game makes its device: the plugin at its load (no hooks, no rate images, nothing drawn on a card without
// it - Serg 2026-11-08 on a GTX 1050 Ti: "фоверация забрала fps, а не дала") and D2R VR Settings (its foveation rows greyed with the reason).
// d3d12.dll and dxgi.dll are loaded by name (nothing to link); each hardware adapter gets a device of its own for the question, let go at once.
// Header only. The decision is pure (Decide, tested); Ask needs Windows.

#include <cstdint>
#include <vector>

namespace vrsprobe {

// One adapter's answer: tier 2 with a usable rate-image tile (8, 16 or 32), or not; software adapters (WARP, the Basic Render Driver) do not count.
struct Adapter {
    bool software = false;
    bool deviceMade = false;   // a D3D12 device could be made on it (feature level 11_0)
    int tier = 0;              // D3D12_VARIABLE_SHADING_RATE_TIER: 0 none, 1, 2
    unsigned tile = 0;         // ShadingRateImageTileSize
};
constexpr bool Usable(const Adapter& a) { return a.deviceMade && a.tier >= 2 && (a.tile == 8 || a.tile == 16 || a.tile == 32); }

// 1: some hardware adapter has it (the game may run on it - foveation goes on as before); 0: hardware adapters were asked and none has
// it (a true no-op then); -1: not known (nothing could be asked - foveation goes on as before and its own check at the first present decides).
inline int Decide(const std::vector<Adapter>& adapters) {
    bool asked = false;
    for (const Adapter& a : adapters) {
        if (a.software || !a.deviceMade) continue;
        asked = true;
        if (Usable(a)) return 1;
    }
    return asked ? 0 : -1;
}

}  // namespace vrsprobe

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace vrsprobe {

// The adapters as D3D12 sees them (software ones too, marked), each with its answer
inline std::vector<Adapter> AskAll() {
    std::vector<Adapter> out;
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll"), d3d12 = LoadLibraryW(L"d3d12.dll");
    using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
    auto createFactory = dxgi ? (CreateFactoryFn)(void*)GetProcAddress(dxgi, "CreateDXGIFactory1") : nullptr;
    auto createDevice = d3d12 ? (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(d3d12, "D3D12CreateDevice") : nullptr;
    IDXGIFactory1* factory = nullptr;
    if (createFactory && createDevice && SUCCEEDED(createFactory(__uuidof(IDXGIFactory1), (void**)&factory)) && factory) {
        IDXGIAdapter1* ad = nullptr;
        for (UINT i = 0; i < 16 && factory->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; ++i) {
            if (!ad) continue;
            Adapter a;
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(ad->GetDesc1(&d))) a.software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            ID3D12Device* dev = nullptr;
            if (SUCCEEDED(createDevice(ad, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev)) && dev) {
                a.deviceMade = true;
                D3D12_FEATURE_DATA_D3D12_OPTIONS6 o{};
                if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &o, sizeof o))) {
                    a.tier = (int)o.VariableShadingRateTier;
                    a.tile = o.ShadingRateImageTileSize;
                }
                dev->Release();
            }
            ad->Release();
            ad = nullptr;
            out.push_back(a);
        }
        factory->Release();
    }
    // (the two DLLs stay loaded: the game loads them anyway, and freeing d3d12.dll under a live device elsewhere is not wanted)
    return out;
}
inline int Ask() { return Decide(AskAll()); }

}  // namespace vrsprobe
#endif
