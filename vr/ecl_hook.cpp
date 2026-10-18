// ecl_hook.cpp - see ecl_hook.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "MinHook.h"
#include "ecl_hook.h"

namespace eclhook {
namespace {

using ExecFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
std::atomic<Observer> g_observer{nullptr};
ExecFn g_orig[3] = {};
std::mutex g_mx;
std::vector<void*> g_hooked;   // the functions hooked
bool g_slotDone[3] = {};

// One trampoline for each queue type: a type may have a function of its own, and the original of one is not the original of another.
template <int S> void STDMETHODCALLTYPE Hook(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (Observer o = g_observer.load(std::memory_order_relaxed)) o(q, n, lists);
    g_orig[S](q, n, lists);
}

template <int S> bool InstallSlot(void** vt) {
    void* fn = vt[10];   // ID3D12CommandQueue: IUnknown 0-2, ID3D12Object 3-6, GetDevice 7, UpdateTileMappings 8, CopyTileMappings 9, ExecuteCommandLists 10
    if (std::find(g_hooked.begin(), g_hooked.end(), fn) != g_hooked.end()) return true;   // already: through another type's slot
    const bool ok = MH_CreateHook(fn, (void*)&Hook<S>, (void**)&g_orig[S]) == MH_OK && MH_EnableHook(fn) == MH_OK;
    if (ok) g_hooked.push_back(fn);
    return ok;
}

}  // namespace

bool Install(ID3D12CommandQueue* q, Observer obs) {
    if (!q) return false;
    g_observer.store(obs);
    const D3D12_COMMAND_LIST_TYPE t = q->GetDesc().Type;
    const int slot = t == D3D12_COMMAND_LIST_TYPE_DIRECT ? 0 : t == D3D12_COMMAND_LIST_TYPE_COMPUTE ? 1 : 2;
    std::lock_guard<std::mutex> l(g_mx);
    if (g_slotDone[slot]) return true;
    void** vt = *(void***)q;
    const bool ok = slot == 0 ? InstallSlot<0>(vt) : slot == 1 ? InstallSlot<1>(vt) : InstallSlot<2>(vt);
    g_slotDone[slot] = ok;
    return ok;
}

}  // namespace eclhook
