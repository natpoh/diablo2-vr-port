// fence_gate.cpp - see fence_gate.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>

#include <algorithm>
#include <atomic>

#include "MinHook.h"
#include "fence_gate.h"

namespace fencegate {
namespace {

using GetCompletedFn = UINT64(STDMETHODCALLTYPE*)(ID3D12Fence*);
using SetEventFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Fence*, UINT64, HANDLE);
using SetEventMultiFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device1*, ID3D12Fence* const*, const UINT64*, UINT, D3D12_MULTIPLE_FENCE_WAIT_FLAGS, HANDLE);

GetCompletedFn g_origGet = nullptr;
SetEventFn g_origSet = nullptr;
SetEventMultiFn g_origMulti = nullptr;
std::atomic<bool> g_installed{false}, g_on{false};
std::atomic<double> g_giveUpMs{1000.0};
std::atomic<int> g_tracked{0};   // fences ever noted: 0 = every call passes at once, no lock
SRWLOCK g_lock = SRWLOCK_INIT;
Gate g_gate;

std::atomic<uint32_t> g_looks{0}, g_heldLooks{0}, g_events{0}, g_heldEvents{0}, g_multi{0}, g_gaveUpTold{0};
std::atomic<double> g_maxHeldMs{0.0};

double NowMs() { return (double)GetTickCount64(); }
uint64_t Real(ID3D12Fence* f) { return g_origGet ? g_origGet(f) : f->GetCompletedValue(); }
auto DoneOf = [](const void* ours) { return Real((ID3D12Fence*)ours); };

void NoteHeldMs(double ms) {
    double was = g_maxHeldMs.load(std::memory_order_relaxed);
    while (ms > was && !g_maxHeldMs.compare_exchange_weak(was, ms)) {}
}
bool Gated(ID3D12Fence* f) {
    if (!g_on.load(std::memory_order_relaxed) || !g_tracked.load(std::memory_order_relaxed)) return false;
    AcquireSRWLockShared(&g_lock);
    const bool t = g_gate.Tracked(f);
    ReleaseSRWLockShared(&g_lock);
    return t;
}
Gate::Need NeedFor(ID3D12Fence* f, UINT64 v) {
    AcquireSRWLockExclusive(&g_lock);
    const Gate::Need n = g_gate.NeedFor(f, v, NowMs(), g_giveUpMs.load(), DoneOf);
    ReleaseSRWLockExclusive(&g_lock);
    return n;
}
// Our fence at r, waited for here up to what is left of give-up ms. True = reached.
bool WaitOurs(const Gate::Need& n) {
    ID3D12Fence* ours = (ID3D12Fence*)n.ours;
    if (Real(ours) >= n.r) return true;
    const double left = std::max(0.0, g_giveUpMs.load() - (NowMs() - n.bornMs));
    HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!e) return false;
    const double t0 = NowMs();
    if (SUCCEEDED(g_origSet(ours, n.r, e))) WaitForSingleObject(e, (DWORD)left);
    CloseHandle(e);
    NoteHeldMs(NowMs() - t0);
    return Real(ours) >= n.r;
}

// The game's event, registered on its fence only once our replay is done (a thread-pool wait on an event of our own on our fence).
struct Deferred {
    ID3D12Fence* fence;   // the game's (held: AddRef)
    UINT64 v;
    HANDLE gameEvent;
    HANDLE ourEvent;
    PTP_WAIT wait;
    double t0;
};
VOID CALLBACK OnReplayDone(PTP_CALLBACK_INSTANCE, PVOID ctx, PTP_WAIT wait, TP_WAIT_RESULT) {
    Deferred* d = (Deferred*)ctx;
    g_origSet(d->fence, d->v, d->gameEvent);   // (fires at once when the fence is already there, as D3D12 does)
    NoteHeldMs(NowMs() - d->t0);
    d->fence->Release();
    CloseHandle(d->ourEvent);
    CloseThreadpoolWait(wait);   // (allowed from its own callback: freed once the callback returns)
    delete d;
}

UINT64 STDMETHODCALLTYPE HookGetCompleted(ID3D12Fence* f) {
    const UINT64 real = g_origGet(f);
    if (!Gated(f)) return real;
    g_looks.fetch_add(1, std::memory_order_relaxed);
    AcquireSRWLockExclusive(&g_lock);
    const UINT64 v = g_gate.View(f, real, NowMs(), g_giveUpMs.load(), DoneOf);
    ReleaseSRWLockExclusive(&g_lock);
    if (v < real) g_heldLooks.fetch_add(1, std::memory_order_relaxed);
    return v;
}

HRESULT STDMETHODCALLTYPE HookSetEvent(ID3D12Fence* f, UINT64 v, HANDLE h) {
    if (!Gated(f)) return g_origSet(f, v, h);
    g_events.fetch_add(1, std::memory_order_relaxed);
    const Gate::Need n = NeedFor(f, v);
    if (!n.ours) return g_origSet(f, v, h);
    g_heldEvents.fetch_add(1, std::memory_order_relaxed);
    if (!h) {   // a null event: the call itself blocks until the fence is there - our replay first
        WaitOurs(n);
        return g_origSet(f, v, nullptr);
    }
    Deferred* d = new Deferred{f, v, h, CreateEventW(nullptr, FALSE, FALSE, nullptr), nullptr, NowMs()};
    if (d->ourEvent) d->wait = CreateThreadpoolWait(&OnReplayDone, d, nullptr);
    if (!d->ourEvent || !d->wait || FAILED(g_origSet((ID3D12Fence*)n.ours, n.r, d->ourEvent))) {   // could not defer: blocked here instead
        if (d->wait) CloseThreadpoolWait(d->wait);
        if (d->ourEvent) CloseHandle(d->ourEvent);
        delete d;
        WaitOurs(n);
        return g_origSet(f, v, h);
    }
    f->AddRef();
    const double left = std::max(1.0, g_giveUpMs.load() - (NowMs() - n.bornMs));
    FILETIME due;   // relative, 100 ns units: negative
    const LONGLONG rel = -(LONGLONG)(left * 10000.0);
    due.dwLowDateTime = (DWORD)(rel & 0xFFFFFFFF);
    due.dwHighDateTime = (DWORD)((ULONGLONG)rel >> 32);
    SetThreadpoolWait(d->wait, d->ourEvent, &due);   // (at give-up ms the game's event is registered anyway)
    return S_OK;
}

HRESULT STDMETHODCALLTYPE HookSetEventMulti(ID3D12Device1* dev, ID3D12Fence* const* fences, const UINT64* values, UINT n,
                                            D3D12_MULTIPLE_FENCE_WAIT_FLAGS flags, HANDLE h) {
    if (g_on.load(std::memory_order_relaxed) && g_tracked.load(std::memory_order_relaxed) && fences && values) {
        bool counted = false;
        for (UINT i = 0; i < n; ++i) {
            if (!fences[i] || !Gated(fences[i])) continue;
            const Gate::Need need = NeedFor(fences[i], values[i]);
            if (!need.ours) continue;
            if (!counted) { counted = true; g_multi.fetch_add(1, std::memory_order_relaxed); }
            WaitOurs(need);   // (rare - the game's frame fences are waited one at a time: blocked here, bounded)
        }
    }
    return g_origMulti(dev, fences, values, n, flags, h);
}

bool HookAt(void* target, void* hook, void** orig) {
    if (MH_CreateHook(target, hook, orig) != MH_OK) return false;   // (already hooked by MinHook: left alone)
    return MH_EnableHook(target) == MH_OK;
}

}  // namespace

bool Install(ID3D12Device* dev) {
    static int done = -1;
    if (done >= 0) return done == 1;
    if (!dev) return false;
    ID3D12Fence* f = nullptr;
    if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&f)) || !f) { done = 0; return false; }
    void** vt = *(void***)f;
    // ID3D12Fence: IUnknown 0-2, ID3D12Object 3-6, ID3D12DeviceChild 7, GetCompletedValue 8, SetEventOnCompletion 9, Signal 10
    bool ok = HookAt(vt[8], (void*)&HookGetCompleted, (void**)&g_origGet) && HookAt(vt[9], (void*)&HookSetEvent, (void**)&g_origSet);
    f->Release();
    ID3D12Device1* d1 = nullptr;
    if (ok && SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12Device1), (void**)&d1)) && d1) {
        // ID3D12Device 0-43, ID3D12Device1: CreatePipelineLibrary 44, SetEventOnMultipleFenceCompletion 45, SetResidencyPriority 46
        void** dvt = *(void***)d1;
        if (!HookAt(dvt[45], (void*)&HookSetEventMulti, (void**)&g_origMulti)) g_origMulti = nullptr;   // (without it: only the two above)
        d1->Release();
    }
    done = ok ? 1 : 0;
    g_installed.store(ok);
    return ok;
}
bool Installed() { return g_installed.load(); }
void SetOn(bool on) {
    g_on.store(on);
    if (!on) {
        AcquireSRWLockExclusive(&g_lock);
        g_gate = Gate{};
        g_tracked.store(0);
        ReleaseSRWLockExclusive(&g_lock);
    }
}
bool On() { return g_on.load(); }
void SetGiveUpMs(double ms) { g_giveUpMs.store(std::clamp(ms, 50.0, 5000.0)); }
void NoteSignal(ID3D12Fence* fence, uint64_t v) {
    if (!g_on.load(std::memory_order_relaxed) || !g_installed.load(std::memory_order_relaxed) || !fence) return;
    AcquireSRWLockExclusive(&g_lock);
    g_gate.Note(fence, v);
    ReleaseSRWLockExclusive(&g_lock);
    g_tracked.store(1, std::memory_order_relaxed);
}
void BindPass(ID3D12Fence* ours, uint64_t r) {
    AcquireSRWLockExclusive(&g_lock);
    if (ours && g_on.load(std::memory_order_relaxed)) g_gate.Bind(ours, r, NowMs());
    else g_gate.Drop();
    ReleaseSRWLockExclusive(&g_lock);
}
void DropPass() {
    AcquireSRWLockExclusive(&g_lock);
    g_gate.Drop();
    ReleaseSRWLockExclusive(&g_lock);
}
uint64_t RealCompleted(ID3D12Fence* f) { return Real(f); }

Stats TakeStats() {
    Stats s;
    s.looks = g_looks.exchange(0);
    s.heldLooks = g_heldLooks.exchange(0);
    s.events = g_events.exchange(0);
    s.heldEvents = g_heldEvents.exchange(0);
    s.multi = g_multi.exchange(0);
    s.maxHeldMs = g_maxHeldMs.exchange(0.0);
    AcquireSRWLockShared(&g_lock);
    const uint32_t all = g_gate.GaveUp();
    ReleaseSRWLockShared(&g_lock);
    s.gaveUp = all - g_gaveUpTold.exchange(all);
    return s;
}
}  // namespace fencegate
