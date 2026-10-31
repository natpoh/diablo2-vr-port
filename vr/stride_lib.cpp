// stride_lib.cpp - see stride_lib.h.

#include "stride_lib.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace stridelib {

namespace {

constexpr wchar_t kFile[] = L"d2r_vr_weight.dll";   // (the weight library: the gait is its D2rs* exports, stride 2.0 on)

SRWLOCK g_lock = SRWLOCK_INIT;   // Ensure / SetPath (the plugin's thread); Step reads the function pointer alone
LogFn g_log = nullptr;
std::wstring g_path;
Status g_status = Status::NotLooked;
HMODULE g_module = nullptr;
std::atomic<D2rsGaitFn> g_step{nullptr};
std::atomic<D2rsStepFn> g_feet{nullptr};   // (D2rsStep, optional)
std::string g_name;
ULONGLONG g_lastTry = 0;
int g_told = -1;   // the status the log was last told

void Say(const char* fmt, const char* a = "") {
    if (!g_log) return;
    char buf[400];
    snprintf(buf, sizeof buf, fmt, a);
    g_log(buf);
}

void Anchor() {}   // an address in this module

std::wstring PathOfFile() {
    if (!g_path.empty()) return g_path;
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Anchor, &mod)) return kFile;
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(mod, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    if (!n || n >= sizeof buf / sizeof buf[0]) return kFile;
    std::wstring p(buf, n);
    const size_t slash = p.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1);
    const std::wstring libs = dir + L"..\\d2r_vr_libs\\" + kFile;   // d2rloader\d2r_vr_libs, as the weight library's other users
    if (GetFileAttributesW(libs.c_str()) != INVALID_FILE_ATTRIBUTES) return libs;
    return dir + kFile;
}

void Tell(Status s, const char* why) {   // (under g_lock)
    g_status = s;
    if (g_told == (int)s) return;
    g_told = (int)s;
    Say("vrcam: ragdoll gait - %s", why);
}

}  // namespace

void SetLog(LogFn fn) { g_log = fn; }

void SetPath(const wchar_t* path) {
    AcquireSRWLockExclusive(&g_lock);
    g_path = path ? path : L"";
    g_lastTry = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

Status GetStatus() {
    AcquireSRWLockShared(&g_lock);
    const Status s = g_status;
    ReleaseSRWLockShared(&g_lock);
    return s;
}

bool Ensure() {
    if (g_step.load()) return true;
    AcquireSRWLockExclusive(&g_lock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_lock); } } unlock;
    if (g_step.load()) return true;
    const ULONGLONG now = GetTickCount64();
    if (g_lastTry && now - g_lastTry < 2000) return false;
    g_lastTry = now;
    const std::wstring path = PathOfFile();
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        Tell(Status::Missing, "d2r_vr_weight.dll is not there (d2rloader\\d2r_vr_libs): [body] legs_side_ragdoll holds the legs going sideways, as before");
        return false;
    }
    HMODULE m = LoadLibraryW(path.c_str());
    if (!m) { Tell(Status::BrokenExports, "d2r_vr_weight.dll did not load: the legs are held going sideways"); return false; }
    const auto abi = (D2rsAbiFn)GetProcAddress(m, "D2rsAbi");
    const auto name = (D2rsNameFn)GetProcAddress(m, "D2rsName");
    const auto step = (D2rsGaitFn)GetProcAddress(m, "D2rsGait");
    if (!abi || !name || !step) {
        FreeLibrary(m);
        Tell(Status::BrokenExports, "d2r_vr_weight.dll has no gait (D2rs*: an older weight library): the legs are held going sideways");
        return false;
    }
    if (abi() != D2RS_ABI_VERSION) {
        FreeLibrary(m);
        Tell(Status::BadAbi, "d2r_vr_weight.dll's gait speaks another interface version (stride 1.x: an older weight library): the legs are held going sideways");
        return false;
    }
    g_feet.store((D2rsStepFn)GetProcAddress(m, "D2rsStep"));
    g_module = m;
    const char* n = name();
    g_name = n ? n : "?";
    g_step.store(step);
    g_status = Status::Loaded;
    g_told = (int)Status::Loaded;
    Say("vrcam: gait %s loaded - going sideways the hero's pelvis and legs are the ragdoll's gait ([body] legs_side_ragdoll)", g_name.c_str());
    return true;
}

bool Loaded() { return g_step.load() != nullptr; }

const char* Name() { return Loaded() ? g_name.c_str() : ""; }

bool Gait(const D2rsGaitIn& in, D2rsGaitState* state, D2rsGaitOut* out) {
    const D2rsGaitFn fn = g_step.load();
    if (!fn || !state || !out) return false;
    __try {
        return fn(&in, state, out) == 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_step.store(nullptr);   // a library that faults is not called again this run
        return false;
    }
}

bool HasSteps() { return Loaded() && g_feet.load() != nullptr; }

bool Step(const D2rsLegs& in, D2rsState* state, D2rsFeet* out) {
    const D2rsStepFn fn = g_feet.load();
    if (!fn || !state || !out) return false;
    __try {
        return fn(&in, state, out) == 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_feet.store(nullptr);
        return false;
    }
}

}  // namespace stridelib
