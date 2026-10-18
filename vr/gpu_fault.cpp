// gpu_fault.cpp - see gpu_fault.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "gpu_fault.h"

// The numbers below are D3D12's own; if the SDK's header disagrees, this stops here instead of naming things wrongly in a report.
static_assert(D3D12_AUTO_BREADCRUMB_OP_PRESENT == 17, "D3D12_AUTO_BREADCRUMB_OP numbering");
static_assert(D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE == 26, "D3D12_AUTO_BREADCRUMB_OP numbering");
static_assert(D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE == 19, "D3D12_DRED_ALLOCATION_TYPE numbering");
static_assert(D3D12_DRED_ALLOCATION_TYPE_RESOURCE == 34, "D3D12_DRED_ALLOCATION_TYPE numbering");

namespace gpufault {
namespace {

void (*g_log)(const char*) = nullptr;
wchar_t g_dir[MAX_PATH] = L"";             // ..\logs\ beside d2r_vr.ini, "" before Init
HANDLE g_file = INVALID_HANDLE_VALUE;      // the report file, open while a report is written
std::atomic<bool> g_removed{false}, g_dumped{false}, g_dredAsked{false}, g_validationAsked{false};
int g_validation = 0;                      // [debug] d3d12_validation, as asked for
ULONGLONG g_t0 = 0;                        // the plugin's load, for the report's times

// -- the report's lines: the log and the file --------------------------------------------------------------------------------
// 2026-10-18 (the first DEVICE_HUNG dump): the game ends ~40 ms after the removal is seen, and a log line costs ~1.2 ms: the report stopped after
// its fifth command list. So the body of a report is collected in memory first (microseconds), written to the file in one call and flushed, and
// only then handed to the log line by line. `t_collect`: this thread's lines go to the buffer (the debug layer's callback may speak on any thread).
constexpr size_t kReportBytes = 96 * 1024;
char g_report[kReportBytes];
size_t g_reportLen = 0;
thread_local bool t_collect = false;

void Say(const char* fmt, ...) {
    char b[600];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    const size_t n = strlen(b);
    if (t_collect) {
        if (g_reportLen + n + 2 <= kReportBytes) {
            memcpy(g_report + g_reportLen, b, n);
            g_reportLen += n;
            g_report[g_reportLen++] = '\r';
            g_report[g_reportLen++] = '\n';
        }
        return;
    }
    if (g_log) g_log(b);
    if (g_file != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(g_file, b, (DWORD)n, &w, nullptr);
        WriteFile(g_file, "\r\n", 2, &w, nullptr);
    }
}

void CollectBegin() { g_reportLen = 0; t_collect = true; }
// The collected lines: the file first (one write, flushed), then the log.
void CollectEnd() {
    t_collect = false;
    if (g_file != INVALID_HANDLE_VALUE && g_reportLen) {
        DWORD w = 0;
        WriteFile(g_file, g_report, (DWORD)g_reportLen, &w, nullptr);
        FlushFileBuffers(g_file);
    }
    if (!g_log) return;
    char line[600];
    for (size_t i = 0; i < g_reportLen;) {
        size_t e = i;
        while (e + 1 < g_reportLen && !(g_report[e] == '\r' && g_report[e + 1] == '\n')) ++e;
        const size_t len = e - i < sizeof line - 1 ? e - i : sizeof line - 1;
        memcpy(line, g_report + i, len);
        line[len] = 0;
        g_log(line);
        i = e + 2;
    }
    g_reportLen = 0;
}

// -- the flight recorder -----------------------------------------------------------------------------------------------------
struct Rec { ULONGLONG at; const char* what; uint64_t a, b; };
constexpr int kRec = 128;
Rec g_rec[kRec];
std::atomic<uint32_t> g_recAt{0};

void DumpRecorder(ULONGLONG now) {
    const uint32_t end = g_recAt.load();
    const uint32_t n = end < (uint32_t)kRec ? end : (uint32_t)kRec;
    if (!n) { Say("vrcam: gpu fault - the flight recorder is empty (nothing of ours touched the GPU's memory since the plugin loaded)"); return; }
    const uint32_t shown = n < 48 ? n : 48;
    Say("vrcam: gpu fault - the last %u of our events that touch the GPU's memory (oldest first, ms before the removal):", shown);
    for (uint32_t i = end - shown; i != end; ++i) {
        const Rec& r = g_rec[i % kRec];
        if (!r.what) continue;
        Say("vrcam: gpu fault -   -%llu ms  %s  %llX %llX", (unsigned long long)(now - r.at), r.what, (unsigned long long)r.a, (unsigned long long)r.b);
    }
}

// -- DRED ----------------------------------------------------------------------------------------------------------------------
// SEH, so no object with a destructor in these: the GPU's own notes are read from memory the driver filled.
// One unfinished list: its queue (the address the hang watchdog prints), its name, how far it got, and the commands around where it stopped.
void SayBreadcrumbNode(unsigned at, const D3D12_AUTO_BREADCRUMB_NODE* n, UINT done) {
    Say("vrcam: gpu fault -   #%u queue %p \"%ls\", list \"%ls\": %u of %u commands done", at, (const void*)n->pCommandQueue,
        n->pCommandQueueDebugNameW ? n->pCommandQueueDebugNameW : L"?", n->pCommandListDebugNameW ? n->pCommandListDebugNameW : L"(unnamed)", done, n->BreadcrumbCount);
    if (!n->pCommandHistory) return;
    const UINT from = done > 3 ? done - 3 : 0, to = done + 3 < n->BreadcrumbCount ? done + 3 : n->BreadcrumbCount - 1;
    char ops[400] = "";
    size_t len = 0;
    for (UINT i = from; i <= to && len < sizeof ops - 60; ++i)
        len += (size_t)snprintf(ops + len, sizeof ops - len, "%s[%u]%s%s%s", i == from ? "" : " ", i, i == done ? " >>" : " ",
                                OpName((int)n->pCommandHistory[i]), i == done ? "<<" : "");
    Say("vrcam: gpu fault -       %s", ops);
}

// The whole list is summed first, per queue (which lists are not finished: the first and the last of them - which end of the list is the oldest
// is D3D12's to say, and it was not clear which the 2026-10-18 hang's five printed lists were), then the first eight and the last four are told.
void WalkBreadcrumbs(const D3D12_AUTO_BREADCRUMB_NODE* head) {
    constexpr unsigned kNodes = 8192, kOpen = 512, kQueues = 8;
    struct Q { const void* q; const wchar_t* name; unsigned nodes, open, first, last; };
    Q qs[kQueues] = {};
    unsigned nq = 0, nodes = 0, open = 0;
    unsigned openAt[kOpen];
    const D3D12_AUTO_BREADCRUMB_NODE* openNode[kOpen];
    UINT openDone[kOpen];
    bool cut = false;
    __try {
        for (const D3D12_AUTO_BREADCRUMB_NODE* n = head; n; n = n->pNext, ++nodes) {
            if (nodes >= kNodes) { cut = true; break; }
            const void* q = (const void*)n->pCommandQueue;
            unsigned k = 0;
            while (k < nq && qs[k].q != q) ++k;
            if (k == nq && nq < kQueues) { qs[nq] = Q{q, n->pCommandQueueDebugNameW, 0, 0, 0, 0}; ++nq; }
            if (k >= nq) k = nq - 1;   // (more queues than there is room for: the last one's)
            ++qs[k].nodes;
            const UINT done = n->pLastBreadcrumbValue ? *n->pLastBreadcrumbValue : n->BreadcrumbCount;
            if (done >= n->BreadcrumbCount) continue;   // finished: not where it stopped
            if (!qs[k].open) qs[k].first = nodes;
            qs[k].last = nodes;
            ++qs[k].open;
            if (open < kOpen) { openAt[open] = nodes; openNode[open] = n; openDone[open] = done; }
            ++open;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cut = true;
    }
    Say("vrcam: gpu fault - auto-breadcrumbs: %u command list(s) recorded%s, %u not finished (#n = the list's place in D3D12's own order)", nodes,
        cut ? " (read as far as it could be)" : "", open);
    for (unsigned k = 0; k < nq; ++k)
        Say("vrcam: gpu fault -   queue %p \"%ls\": %u lists, %u not finished%s", qs[k].q, qs[k].name ? qs[k].name : L"?", qs[k].nodes, qs[k].open,
            qs[k].open ? "" : " - it was idle or waiting, not in a list");
    const unsigned held = open < kOpen ? open : kOpen;
    __try {
        for (unsigned i = 0; i < held; ++i) {
            if (i >= 8 && i + 4 < held) { if (i == 8) Say("vrcam: gpu fault -   ... %u more not finished ...", held - 12); continue; }
            SayBreadcrumbNode(openAt[i], openNode[i], openDone[i]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Say("vrcam: gpu fault -   (a list could not be read)");
    }
    if (open > held) Say("vrcam: gpu fault -   (%u not finished in all; the first %u kept)", open, held);
}

void WalkAllocations(const char* title, const D3D12_DRED_ALLOCATION_NODE* head) {
    unsigned n = 0;
    __try {
        for (const D3D12_DRED_ALLOCATION_NODE* a = head; a && n < 64; a = a->pNext, ++n) {
            if (n < 12)
                Say("vrcam: gpu fault -   %s: \"%ls\" (%s)", title, a->ObjectNameW ? a->ObjectNameW : L"(unnamed)", AllocTypeName((int)a->AllocationType));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Say("vrcam: gpu fault -   (the allocation list could not be read to its end)");
    }
    if (!n) Say("vrcam: gpu fault -   %s: none", title);
    else if (n > 12) Say("vrcam: gpu fault -   %s: %u in all, the first 12 shown", title, n);
}

void DumpDred(ID3D12Device* dev) {
    ID3D12DeviceRemovedExtendedData* dred = nullptr;
    if (FAILED(dev->QueryInterface(__uuidof(ID3D12DeviceRemovedExtendedData), (void**)&dred)) || !dred) {
        Say("vrcam: gpu fault - DRED is not on this device (the runtime has no ID3D12DeviceRemovedExtendedData)");
        return;
    }
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT ab{};
    const HRESULT h1 = dred->GetAutoBreadcrumbsOutput(&ab);
    if (SUCCEEDED(h1)) {
        Say("vrcam: gpu fault - auto-breadcrumbs asked: 0x%08lX, first list %p", (unsigned long)h1, (const void*)ab.pHeadAutoBreadcrumbNode);
        WalkBreadcrumbs(ab.pHeadAutoBreadcrumbNode);
    }
    else Say("vrcam: gpu fault - no auto-breadcrumbs (0x%08lX%s): DRED was not on when the device was made", (unsigned long)h1,
             h1 == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE ? ", not currently available" : "");
    D3D12_DRED_PAGE_FAULT_OUTPUT pf{};
    const HRESULT h2 = dred->GetPageFaultAllocationOutput(&pf);
    if (SUCCEEDED(h2) && pf.PageFaultVA) {
        Say("vrcam: gpu fault - A PAGE FAULT at GPU address 0x%llX", (unsigned long long)pf.PageFaultVA);
        WalkAllocations("allocated there", pf.pHeadExistingAllocationNode);
        WalkAllocations("freed just before", pf.pHeadRecentFreedAllocationNode);
    } else if (SUCCEEDED(h2)) {
        Say("vrcam: gpu fault - no page fault was reported (a hang, a reset or a driver error, not a bad address)");
    } else {
        Say("vrcam: gpu fault - no page-fault report (0x%08lX)", (unsigned long)h2);
    }
    dred->Release();
}

void Dump(ID3D12Device* dev, const char* how) {
    g_removed.store(true);
    if (g_dumped.exchange(true)) return;
    const ULONGLONG now = GetTickCount64();
    if (g_dir[0]) {
        wchar_t path[MAX_PATH];
        wcscpy_s(path, g_dir);
        wcscat_s(path, L"d2r_vr_gpu_fault.txt");
        g_file = CreateFileW(path, FILE_APPEND_DATA | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    Say("==== %04d-%02d-%02d %02d:%02d:%02d - D2R VR - the D3D12 device was removed (%s) ====", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, how);
    const HRESULT reason = dev ? dev->GetDeviceRemovedReason() : E_POINTER;
    Say("vrcam: gpu fault - the device's removal reason: 0x%08lX %s; %.1f s after the plugin loaded", (unsigned long)reason, ReasonName((int32_t)reason),
        (double)(now - g_t0) / 1000.0);
    // the rest in memory, DRED's answer first (what the GPU was doing - the part the game's end must not take), written in one go
    CollectBegin();
    if (dev) DumpDred(dev);
    DumpRecorder(now);
    Say("vrcam: gpu fault - the game's own DX12 backend asserts on this (dx12_backend.cpp, 'SUCCEEDED(hr)') and its next line reads a null pointer: the game ends "
        "a moment after this report");
    CollectEnd();
    if (g_file != INVALID_HANDLE_VALUE) { CloseHandle(g_file); g_file = INVALID_HANDLE_VALUE; }
}

// -- the debug layer's messages ------------------------------------------------------------------------------------------------
// Any thread, the one that made the call: each message id told the first few times, then every 200th (a bad call is made every frame).
void CALLBACK OnMessage(D3D12_MESSAGE_CATEGORY cat, D3D12_MESSAGE_SEVERITY sev, D3D12_MESSAGE_ID id, LPCSTR text, void*) {
    if (sev > D3D12_MESSAGE_SEVERITY_WARNING) return;   // info and plain messages: far too many, and not what is hunted
    static std::atomic<uint32_t> seen[256];
    const uint32_t n = seen[(uint32_t)id % 256].fetch_add(1) + 1;
    if (n > 5 && n % 200 != 0) return;
    static const char* const kSev[] = {"CORRUPTION", "ERROR", "WARNING"};
    Say("vrcam: d3d12 debug layer - %s (category %d, id %d, %u time(s) so far): %.480s", kSev[(int)sev < 3 ? (int)sev : 2], (int)cat, (int)id, n, text ? text : "?");
    Note("d3d12 debug layer message", (uint64_t)id, (uint64_t)sev);
}
ID3D12InfoQueue1* g_infoQueue = nullptr;
DWORD g_infoCookie = 0;

// -- the watcher ---------------------------------------------------------------------------------------------------------------
ID3D12Device* g_dev = nullptr;
LUID g_luid{};                              // the watched device's adapter, kept so that VideoMemory never touches a device that may be going
std::atomic<bool> g_haveLuid{false};
ID3D12Fence* g_fence = nullptr;
HANDLE g_evt = nullptr, g_wait = nullptr;

VOID CALLBACK OnSignal(PVOID, BOOLEAN) {
    ID3D12Device* d = g_dev;
    if (d && d->GetDeviceRemovedReason() != S_OK) Dump(d, "the device's own fence went to its end: the runtime signals every fence when it removes the device");
}

}  // namespace

// -- the words --------------------------------------------------------------------------------------------------------------
const char* ReasonName(int32_t hr) {
    switch ((uint32_t)hr) {
        case 0x00000000: return "S_OK (not removed)";
        case 0x887A0005: return "DXGI_ERROR_DEVICE_REMOVED (the adapter went away or was reset)";
        case 0x887A0006: return "DXGI_ERROR_DEVICE_HUNG (the GPU stopped answering: a hang the driver reset)";
        case 0x887A0007: return "DXGI_ERROR_DEVICE_RESET (the driver reset the device)";
        case 0x887A0020: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR (the driver failed)";
        case 0x887A0001: return "DXGI_ERROR_INVALID_CALL (a call the runtime could not take)";
        case 0x8007000E: return "E_OUTOFMEMORY";
        default: return "an unknown reason";
    }
}

const char* OpName(int op) {
    static const char* const k[] = {
        "SetMarker", "BeginEvent", "EndEvent", "DrawInstanced", "DrawIndexedInstanced", "ExecuteIndirect", "Dispatch", "CopyBufferRegion",
        "CopyTextureRegion", "CopyResource", "CopyTiles", "ResolveSubresource", "ClearRenderTargetView", "ClearUnorderedAccessView",
        "ClearDepthStencilView", "ResourceBarrier", "ExecuteBundle", "Present", "ResolveQueryData", "BeginSubmission", "EndSubmission",
        "DecodeFrame", "ProcessFrames", "AtomicCopyBufferUint", "AtomicCopyBufferUint64", "ResolveSubresourceRegion", "WriteBufferImmediate",
        "DecodeFrame1", "SetProtectedResourceSession", "DecodeFrame2", "ProcessFrames1", "BuildRaytracingAccelerationStructure",
        "EmitRaytracingAccelerationStructurePostbuildInfo", "CopyRaytracingAccelerationStructure", "DispatchRays", "InitializeMetaCommand",
        "ExecuteMetaCommand", "EstimateMotion", "ResolveMotionVectorHeap", "SetPipelineState1", "InitializeExtensionCommand",
        "ExecuteExtensionCommand", "DispatchMesh", "EncodeFrame", "ResolveEncoderOutputMetadata"};
    if (op >= 0 && op < (int)(sizeof k / sizeof *k)) return k[op];
    static char b[24];
    snprintf(b, sizeof b, "op %d", op);
    return b;
}

const char* AllocTypeName(int type) {
    switch (type) {
        case 19: return "command queue";
        case 20: return "command allocator";
        case 21: return "pipeline state";
        case 22: return "command list";
        case 23: return "fence";
        case 24: return "descriptor heap";
        case 25: return "heap";
        case 27: return "query heap";
        case 28: return "command signature";
        case 29: return "pipeline library";
        case 34: return "resource";
        case 35: return "pass";
        case 40: return "command pool";
        case 41: return "command recorder";
        case 42: return "state object";
        default: return "other";
    }
}

// -- the calls --------------------------------------------------------------------------------------------------------------
void Init(void (*log)(const char*), const wchar_t* iniPath) {
    g_log = log;
    g_t0 = GetTickCount64();
    if (!iniPath) return;
    wcscpy_s(g_dir, iniPath);
    if (wchar_t* slash = wcsrchr(g_dir, L'\\')) slash[1] = 0;
    wcscat_s(g_dir, L"..\\logs\\");
}

void EnableDred(bool on) {
    if (!on || g_dredAsked.exchange(true)) return;
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (!n || n > MAX_PATH - 16) return;
    wcscat_s(sys, L"\\d3d12.dll");   // the system's own, by its path: the game's folder holds a copy of an older one that is not what runs
    HMODULE m = LoadLibraryW(sys);
    typedef HRESULT(WINAPI * GetDebugInterfaceFn)(REFIID, void**);
    GetDebugInterfaceFn fn = m ? (GetDebugInterfaceFn)GetProcAddress(m, "D3D12GetDebugInterface") : nullptr;
    ID3D12DeviceRemovedExtendedDataSettings* s = nullptr;
    HRESULT hr = fn ? fn(__uuidof(ID3D12DeviceRemovedExtendedDataSettings), (void**)&s) : E_NOINTERFACE;
    if (SUCCEEDED(hr) && s) {
        s->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        s->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        s->Release();
        if (g_log) g_log("vrcam: gpu fault - DRED on for the game's device ([debug] dred): if the device is removed, the log and d2rloader\\logs\\d2r_vr_gpu_fault.txt say what each command list had done and where a page fault was");
    } else if (g_log) {
        char b[200];
        snprintf(b, sizeof b, "vrcam: gpu fault - DRED could not be asked for (0x%08lX): a device loss will be told with its reason only", (unsigned long)hr);
        g_log(b);
    }
}

void EnableValidation(int level) {
    if (level <= 0 || g_validationAsked.exchange(true)) return;
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (!n || n > MAX_PATH - 16) return;
    wcscat_s(sys, L"\\d3d12.dll");
    HMODULE m = LoadLibraryW(sys);
    typedef HRESULT(WINAPI * GetDebugInterfaceFn)(REFIID, void**);
    GetDebugInterfaceFn fn = m ? (GetDebugInterfaceFn)GetProcAddress(m, "D3D12GetDebugInterface") : nullptr;
    ID3D12Debug* dbg = nullptr;
    const HRESULT hr = fn ? fn(__uuidof(ID3D12Debug), (void**)&dbg) : E_NOINTERFACE;
    if (FAILED(hr) || !dbg) {
        char b[220];
        snprintf(b, sizeof b, "vrcam: gpu fault - the D3D12 debug layer could not be asked for (0x%08lX): Windows' Graphics Tools (D3D12SDKLayers.dll) missing?", (unsigned long)hr);
        if (g_log) g_log(b);
        return;
    }
    dbg->EnableDebugLayer();
    g_validation = 1;
    if (level >= 2) {
        ID3D12Debug1* d1 = nullptr;
        if (SUCCEEDED(dbg->QueryInterface(__uuidof(ID3D12Debug1), (void**)&d1)) && d1) {
            d1->SetEnableGPUBasedValidation(TRUE);
            d1->SetEnableSynchronizedCommandQueueValidation(TRUE);
            d1->Release();
            g_validation = 2;
        }
    }
    dbg->Release();
    if (g_log) {
        char b[260];
        snprintf(b, sizeof b, "vrcam: gpu fault - the D3D12 debug layer is ON%s ([debug] d3d12_validation=%d): its errors and warnings go to this log; the game runs slower", g_validation >= 2 ? " with GPU-based validation (about 10 times slower)" : "", level);
        g_log(b);
    }
}

void Watch(ID3D12Device* dev) {
    if (!dev || dev == g_dev) return;
    if (g_dev) Unwatch(g_dev);
    ID3D12Fence* f = nullptr;
    if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&f)) || !f) return;
    f->SetName(L"d2rvr device-removal watcher");
    HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!e) { f->Release(); return; }
    if (FAILED(f->SetEventOnCompletion(UINT64_MAX, e))) { CloseHandle(e); f->Release(); return; }
    g_dev = dev;
    g_luid = dev->GetAdapterLuid();
    g_haveLuid.store(true);
    g_fence = f;
    g_evt = e;
    if (!RegisterWaitForSingleObject(&g_wait, e, &OnSignal, nullptr, INFINITE, WT_EXECUTEONLYONCE)) {
        g_wait = nullptr;
        g_dev = nullptr; g_fence = nullptr; g_evt = nullptr;
        CloseHandle(e);
        f->Release();
        return;
    }
    if (g_log) {
        char b[160];
        snprintf(b, sizeof b, "vrcam: gpu fault - watching the game's D3D12 device %p: its removal is told the moment it happens", (void*)dev);
        g_log(b);
    }
    if (g_validation > 0) {   // the debug layer's messages for this device
        ID3D12InfoQueue1* iq = nullptr;
        if (SUCCEEDED(dev->QueryInterface(__uuidof(ID3D12InfoQueue1), (void**)&iq)) && iq) {
            DWORD cookie = 0;
            if (SUCCEEDED(iq->RegisterMessageCallback(&OnMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie))) {
                g_infoQueue = iq;
                g_infoCookie = cookie;
                if (g_log) g_log("vrcam: gpu fault - the debug layer's messages for this device go to the log");
            } else {
                iq->Release();
            }
        } else if (g_log) {
            g_log("vrcam: gpu fault - the device has no info queue (the debug layer was not on when it was made?): no debug layer messages");
        }
    }
}

void Unwatch(ID3D12Device* dev) {
    if (!dev || dev != g_dev) return;
    if (g_infoQueue) { g_infoQueue->UnregisterMessageCallback(g_infoCookie); g_infoQueue->Release(); g_infoQueue = nullptr; }
    if (g_wait) { UnregisterWaitEx(g_wait, INVALID_HANDLE_VALUE); g_wait = nullptr; }   // (waits for a callback that runs)
    if (g_evt) { CloseHandle(g_evt); g_evt = nullptr; }
    if (g_fence) { g_fence->Release(); g_fence = nullptr; }
    g_dev = nullptr;
}

void DescribeBreadcrumbs(const D3D12_AUTO_BREADCRUMB_NODE* head) {
    CollectBegin();
    WalkBreadcrumbs(head);
    CollectEnd();
}

bool VideoMemory(uint64_t* usage, uint64_t* budget) {
    if (!g_haveLuid.load()) return false;
    static SRWLOCK lock = SRWLOCK_INIT;
    static IDXGIAdapter3* a3 = nullptr;
    static LUID forLuid{};
    bool ok = false;
    AcquireSRWLockExclusive(&lock);
    if (a3 && (forLuid.LowPart != g_luid.LowPart || forLuid.HighPart != g_luid.HighPart)) { a3->Release(); a3 = nullptr; }
    if (!a3) {
        typedef HRESULT(WINAPI * Fn)(REFIID, void**);
        HMODULE m = GetModuleHandleW(L"dxgi.dll");   // (the game has it loaded; no import of ours)
        Fn fn = m ? (Fn)GetProcAddress(m, "CreateDXGIFactory1") : nullptr;
        IDXGIFactory4* f = nullptr;
        if (fn && SUCCEEDED(fn(__uuidof(IDXGIFactory4), (void**)&f)) && f) {
            IDXGIAdapter3* a = nullptr;
            if (SUCCEEDED(f->EnumAdapterByLuid(g_luid, __uuidof(IDXGIAdapter3), (void**)&a)) && a) { a3 = a; forLuid = g_luid; }
            f->Release();
        }
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (a3 && SUCCEEDED(a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        *usage = info.CurrentUsage;
        *budget = info.Budget;
        ok = true;
    }
    ReleaseSRWLockExclusive(&lock);
    return ok;
}

bool Removed() { return g_removed.load(std::memory_order_relaxed); }

bool Check(ID3D12Device* dev, const char* where) {
    if (g_removed.load(std::memory_order_relaxed)) return true;
    if (!dev) return false;
    if (dev->GetDeviceRemovedReason() == S_OK) return false;
    Dump(dev, where);
    return true;
}

void Note(const char* what, uint64_t a, uint64_t b) {
    const uint32_t i = g_recAt.fetch_add(1);
    g_rec[i % kRec] = Rec{GetTickCount64(), what, a, b};
}

}  // namespace gpufault
