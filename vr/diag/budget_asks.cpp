// vr/diag/budget_asks.cpp - the game's asks of its video memory budget, counted for the 10 s line (cut from vr/vrcam.cpp, docs/plan_modules.md M1)
#include "diag.h"
#include "../vrcam_internal.h"

#include <dxgi1_4.h>

#include "MinHook.h"

namespace vrc {

// The game's own asks of its video memory budget (IDXGIAdapter3::QueryVideoMemoryInfo), counted only - nothing changed: the 10 s line tells
// whether it sizes anything (its textures) by the budget Windows gives it, the foreground window's the bigger. A Quest 2 player on a 4 GB card
// over Link (2026-10-26): 1-10 frames a second for the first 30-80 s in town in both modes, better at once with D2R VR Settings in front of
// the game - in his three logs the recovery came with the budget falling (2780 -> 2218, 2756 -> 2140 MiB), Link's compositor then left room.
namespace budgetasks {
std::atomic<uint32_t> g_gameAsks{0}, g_frontPresents{0};
std::atomic<DWORD> g_ourThread{0};
std::atomic<bool> g_in{false};
typedef HRESULT(STDMETHODCALLTYPE* QvmiFn)(IDXGIAdapter3*, UINT, DXGI_MEMORY_SEGMENT_GROUP, DXGI_QUERY_VIDEO_MEMORY_INFO*);
static QvmiFn g_orig = nullptr;
HRESULT STDMETHODCALLTYPE Hook(IDXGIAdapter3* a, UINT node, DXGI_MEMORY_SEGMENT_GROUP group, DXGI_QUERY_VIDEO_MEMORY_INFO* info) {
    if (GetCurrentThreadId() != g_ourThread.load(std::memory_order_relaxed)) g_gameAsks.fetch_add(1, std::memory_order_relaxed);
    return g_orig(a, node, group, info);
}
// Once (the 10 s line's thread): dxgi.dll's own QueryVideoMemoryInfo, from an adapter of our own (every adapter's is the same code)
void Install() {
    static bool tried = false;
    if (tried) return;
    tried = true;
    typedef HRESULT(WINAPI * Fn)(REFIID, void**);
    HMODULE m = GetModuleHandleW(L"dxgi.dll");
    Fn fn = m ? (Fn)GetProcAddress(m, "CreateDXGIFactory1") : nullptr;
    IDXGIFactory1* f = nullptr;
    if (!fn || FAILED(fn(__uuidof(IDXGIFactory1), (void**)&f)) || !f) return;
    IDXGIAdapter1* a = nullptr;
    IDXGIAdapter3* a3 = nullptr;
    if (SUCCEEDED(f->EnumAdapters1(0, &a)) && a && SUCCEEDED(a->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3)) && a3) {
        void* target = (*(void***)a3)[14];   // IUnknown 3, IDXGIObject 4, IDXGIAdapter 3, Adapter1 1, Adapter2 1, Adapter3: two before it
        const bool ok = MH_CreateHook(target, (void*)&Hook, (void**)&g_orig) == MH_OK && MH_EnableHook(target) == MH_OK;
        g_in.store(ok);
        Log(ok ? "vrcam: video memory - the game's asks of its budget (QueryVideoMemoryInfo) counted in the 10 s line, nothing changed"
               : "vrcam: video memory - QueryVideoMemoryInfo could not be watched");
    }
    if (a3) a3->Release();
    if (a) a->Release();
    f->Release();
}
}  // namespace budgetasks

}  // namespace vrc
