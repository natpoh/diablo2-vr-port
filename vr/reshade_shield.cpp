// ReShade's DXGI factory and an interface that is no adapter (2026-10-10).
//
// ReShade wraps every DXGI factory made in the process - also the ones native OpenXR's runtime
// makes, once the loader has brought it into the game. Its IDXGIFactory4::EnumAdapterByLuid and
// IDXGIFactory6::EnumAdapterByGpuPreference hand what the real factory returned for any riid to
// check_and_proxy_adapter_interface, which builds its adapter proxy first and asks whether riid is
// an adapter's only after: the proxy's constructor calls SetPrivateData (IDXGIObject's slot 3) on
// whatever came back, and the destructor once more. A player's runtime (its d3d11.dll, D3D11 or
// 11on12 beside the game's D3D12) asked EnumAdapterByLuid for one of dxgi's own interfaces, whose
// slot 3 is an int3: EXCEPTION_BREAKPOINT in dxgi.dll under ReShade64.dll at every native start
// (ReShade 6.8.0.2155 - its +0x17EF19, +0x17D889, +0x17CE5F; ReShade's main has the same code).
//
// An interface that is no adapter's never keeps a proxy - ReShade deletes it and returns what the
// factory gave - so for those the real factory is asked straight, past ReShade: the same answer,
// without the call that kills the game. Adapters still go through ReShade.

#include "reshade_shield.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>
#include <psapi.h>

#include <cstdarg>
#include <cstdio>
#include <vector>

#include "MinHook.h"

namespace reshade_shield {
namespace {

void (*g_log)(const char*) = nullptr;
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    g_log(b);
}

MODULEINFO g_dxgi{};
bool Inside(const void* p, const MODULEINFO& m) {
    const char* b = static_cast<const char*>(m.lpBaseOfDll);
    return p >= b && p < b + m.SizeOfImage;
}
bool ModuleOf(const wchar_t* name, MODULEINFO* out) {
    const HMODULE h = GetModuleHandleW(name);
    return h && K32GetModuleInformation(GetCurrentProcess(), h, out, sizeof *out);
}

// What ReShade's adapter proxy takes (DXGIAdapter::check_and_upgrade_interface): IUnknown,
// IDXGIObject, IDXGIAdapter to IDXGIAdapter4 and its own uuid
const GUID kReShadeAdapter = {0xF978E25F, 0x2217, 0x49E0, {0xA8, 0x93, 0xCD, 0xAF, 0xD6, 0xEE, 0x48, 0xB5}};
bool AdapterIid(REFIID riid) {
    return riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIAdapter) ||
           riid == __uuidof(IDXGIAdapter1) || riid == __uuidof(IDXGIAdapter2) || riid == __uuidof(IDXGIAdapter3) ||
           riid == __uuidof(IDXGIAdapter4) || riid == kReShadeAdapter;
}

// The factory behind ReShade's proxy: its DXGIFactory has the vtable, then _orig
// (dxgi_factory.hpp) - taken only when that is dxgi's own object, else ReShade is called as before.
template <typename F> F* Real(F* proxy) {
    F* const real = *reinterpret_cast<F* const*>(reinterpret_cast<const char*>(proxy) + sizeof(void*));
    return real && Inside(*reinterpret_cast<void* const*>(real), g_dxgi) ? real : nullptr;
}

// each foreign interface once in the log
SRWLOCK g_toldLock = SRWLOCK_INIT;
std::vector<GUID> g_told;
void Tell(const char* method, REFIID riid) {
    AcquireSRWLockExclusive(&g_toldLock);
    bool seen = false;
    for (const GUID& g : g_told) seen |= g == riid;
    if (!seen && g_told.size() < 32) g_told.push_back(riid);
    ReleaseSRWLockExclusive(&g_toldLock);
    if (seen) return;
    LogF("vrcam: openxr - ReShade shield: %s asked for {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}, no adapter's interface - "
         "answered by dxgi itself, past ReShade's proxy", method, riid.Data1, riid.Data2, riid.Data3, riid.Data4[0], riid.Data4[1],
         riid.Data4[2], riid.Data4[3], riid.Data4[4], riid.Data4[5], riid.Data4[6], riid.Data4[7]);
}

using ByLuid = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory4*, LUID, REFIID, void**);
using ByPreference = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory6*, UINT, DXGI_GPU_PREFERENCE, REFIID, void**);
ByLuid g_byLuid = nullptr;
ByPreference g_byPreference = nullptr;

HRESULT STDMETHODCALLTYPE HookByLuid(IDXGIFactory4* self, LUID luid, REFIID riid, void** out) {
    if (!AdapterIid(riid))
        if (IDXGIFactory4* real = Real(self)) {
            Tell("EnumAdapterByLuid", riid);
            return real->EnumAdapterByLuid(luid, riid, out);
        }
    return g_byLuid(self, luid, riid, out);
}
HRESULT STDMETHODCALLTYPE HookByPreference(IDXGIFactory6* self, UINT n, DXGI_GPU_PREFERENCE pref, REFIID riid, void** out) {
    if (!AdapterIid(riid))
        if (IDXGIFactory6* real = Real(self)) {
            Tell("EnumAdapterByGpuPreference", riid);
            return real->EnumAdapterByGpuPreference(n, pref, riid, out);
        }
    return g_byPreference(self, n, pref, riid, out);
}

}  // namespace

void Install(void (*log)(const char*)) {
    static bool tried = false;
    if (tried) return;
    tried = true;
    g_log = log;
    MODULEINFO rs{};
    if (!ModuleOf(L"ReShade64.dll", &rs) || !ModuleOf(L"dxgi.dll", &g_dxgi)) {
        LogF("vrcam: openxr - ReShade shield: no ReShade64.dll or dxgi.dll in the game - nothing to shield");
        return;
    }
    // A factory of our own: ReShade's hook of the export hands back its proxy - the class every
    // factory in the process gets, the runtime's too, one vtable for all of them.
    using Create2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
    const auto create = reinterpret_cast<Create2>(GetProcAddress(GetModuleHandleW(L"dxgi.dll"), "CreateDXGIFactory2"));
    IDXGIFactory6* f = nullptr;
    if (!create || FAILED(create(0, __uuidof(IDXGIFactory6), reinterpret_cast<void**>(&f))) || !f) {
        LogF("vrcam: openxr - ReShade shield: no IDXGIFactory6 here - not put in");
        return;
    }
    void* const* const vt = *reinterpret_cast<void* const* const*>(f);
    void* const byLuid = vt[26];         // IDXGIFactory4::EnumAdapterByLuid (0xD0 - ReShade's own call of it)
    void* const byPreference = vt[29];   // IDXGIFactory6::EnumAdapterByGpuPreference
    const bool proxy = Inside(vt, rs) && Inside(byLuid, rs) && Inside(byPreference, rs) && Real(f);
    f->Release();
    if (!proxy) {
        LogF("vrcam: openxr - ReShade shield: the DXGI factory is not ReShade's proxy (vtable %p) - nothing to shield", (const void*)vt);
        return;
    }
    const bool ok = MH_CreateHook(byLuid, reinterpret_cast<void*>(&HookByLuid), reinterpret_cast<void**>(&g_byLuid)) == MH_OK &&
                    MH_CreateHook(byPreference, reinterpret_cast<void*>(&HookByPreference), reinterpret_cast<void**>(&g_byPreference)) == MH_OK &&
                    MH_EnableHook(byLuid) == MH_OK && MH_EnableHook(byPreference) == MH_OK;
    LogF(ok ? "vrcam: openxr - ReShade shield in: its factory's EnumAdapterByLuid / EnumAdapterByGpuPreference (ReShade64.dll+0x%llX / +0x%llX) "
              "send an interface that is no adapter's straight to dxgi"
            : "vrcam: openxr - ReShade shield NOT in (MinHook failed at ReShade64.dll+0x%llX / +0x%llX) - a runtime that asks it for "
              "no adapter may crash the game",
         (unsigned long long)(static_cast<const char*>(byLuid) - static_cast<const char*>(rs.lpBaseOfDll)),
         (unsigned long long)(static_cast<const char*>(byPreference) - static_cast<const char*>(rs.lpBaseOfDll)));
}

}  // namespace reshade_shield
