// doll_lib.cpp - see doll_lib.h.

#include "doll_lib.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace dolllib {

namespace {

constexpr wchar_t kFile[] = L"d2r_vr_weight.dll";   // (the weight library: the doll is its D2rd* exports, 1.2 on)

SRWLOCK g_lock = SRWLOCK_INIT;
LogFn g_log = nullptr;
std::wstring g_path;                 // an override (tests); empty: d2r_vr_libs or beside this module
Status g_status = Status::NotLooked;
HMODULE g_module = nullptr;
std::atomic<D2rdPushFn> g_push{nullptr};
std::atomic<D2rdStepFn> g_step{nullptr};
std::atomic<D2rdApplyFn> g_apply{nullptr};
std::atomic<D2rdMassFn> g_mass{nullptr};
std::atomic<D2rdBalanceFn> g_balance{nullptr};
std::string g_name;
ULONGLONG g_lastTry = 0;
int g_toldMissing = -1;              // what the log was last told about the file: 0 missing, 1 there
std::atomic<bool> g_errorTold{false};

void Say(const char* fmt, const char* a = "", const char* b = "") {
    if (!g_log) return;
    char buf[400];
    snprintf(buf, sizeof buf, fmt, a, b);
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
    const std::wstring libs = dir + L"..\\d2r_vr_libs\\" + kFile;   // (as the weight and grip libraries: out of d2rloader\plugins, where D2RLoader takes any DLL for a plugin)
    if (GetFileAttributesW(libs.c_str()) != INVALID_FILE_ATTRIBUTES) return libs;
    return dir + kFile;
}

bool FileThere(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void Refused(int32_t r) {
    if (r < 0 && !g_errorTold.exchange(true)) Say("vrcam: doll library - a call was refused: the monsters stand as the game poses them");
}

}  // namespace

void SetLog(LogFn fn) { g_log = fn; }

void SetPath(const wchar_t* path) {
    AcquireSRWLockExclusive(&g_lock);
    g_path = path ? path : L"";
    g_toldMissing = -1;
    g_lastTry = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

Status GetStatus() {
    AcquireSRWLockShared(&g_lock);
    const Status s = g_status;
    ReleaseSRWLockShared(&g_lock);
    return s;
}

bool Loaded() { return GetStatus() == Status::Loaded; }

const char* Name() { return Loaded() ? g_name.c_str() : ""; }

bool Probe() {
    AcquireSRWLockExclusive(&g_lock);
    const bool there = FileThere(PathOfFile());
    if (g_status != Status::Loaded && g_toldMissing != (there ? 1 : 0)) {
        g_toldMissing = there ? 1 : 0;
        if (there) Say("vrcam: doll library - d2r_vr_weight.dll is in d2rloader\\d2r_vr_libs (or beside the plugin); it is loaded when [weight] monster_ragdoll 1 asks for it");
        else Say("vrcam: doll library - there is no d2r_vr_weight.dll in d2rloader\\d2r_vr_libs (or beside the plugin): [weight] monster_ragdoll does nothing");
    }
    ReleaseSRWLockExclusive(&g_lock);
    return there;
}

bool Ensure() {
    AcquireSRWLockExclusive(&g_lock);
    if (g_status == Status::Loaded) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_lastTry && now - g_lastTry < 2000) {   // looked a moment ago
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_lastTry = now;
    const std::wstring path = PathOfFile();
    if (!FileThere(path)) {
        if (g_toldMissing != 0) {
            g_toldMissing = 0;
            Say("vrcam: doll library - there is no d2r_vr_weight.dll in d2rloader\\d2r_vr_libs (or beside the plugin): [weight] monster_ragdoll does nothing");
        }
        g_status = Status::Missing;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        char why[64];
        snprintf(why, sizeof why, "error %lu", GetLastError());
        Say("vrcam: doll library - d2r_vr_weight.dll could not be loaded (%s): the monsters stand as the game poses them", why);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    const D2rdAbiFn abi = (D2rdAbiFn)GetProcAddress(mod, "D2rdAbi");
    const D2rdNameFn name = (D2rdNameFn)GetProcAddress(mod, "D2rdName");
    const D2rdPushFn push = (D2rdPushFn)GetProcAddress(mod, "D2rdPush");
    const D2rdStepFn step = (D2rdStepFn)GetProcAddress(mod, "D2rdStep");
    const D2rdApplyFn apply = (D2rdApplyFn)GetProcAddress(mod, "D2rdApply");
    const D2rdMassFn mass = (D2rdMassFn)GetProcAddress(mod, "D2rdMass");
    const D2rdBalanceFn balance = (D2rdBalanceFn)GetProcAddress(mod, "D2rdBalance");
    if (!abi || !push || !step || !apply || !mass || !balance) {
        Say("vrcam: doll library - d2r_vr_weight.dll has no monsters' weight (no D2rd* exports: a weight library older than 1.2) - [weight] monster_ragdoll does nothing");
        FreeLibrary(mod);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    const int32_t v = abi();
    if (v != D2RD_ABI_VERSION) {
        char b[32], w[32];
        snprintf(b, sizeof b, "%d", (int)v);
        snprintf(w, sizeof w, "%d", (int)D2RD_ABI_VERSION);
        Say("vrcam: doll library - d2r_vr_weight.dll speaks interface version %s, the mod needs %s: not used", b, w);
        FreeLibrary(mod);
        g_status = Status::BadAbi;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_module = mod;
    g_push.store(push);
    g_step.store(step);
    g_apply.store(apply);
    g_mass.store(mass);
    g_balance.store(balance);
    g_name = name && name() ? name() : "unnamed";
    g_status = Status::Loaded;
    Say("vrcam: doll library - %s loaded from d2rloader\\d2r_vr_libs (or beside the plugin): [weight] monster_ragdoll gives the monsters weight", g_name.c_str());
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

bool Push(D2rdState* state, const D2rdParams* params, const D2rdBody* bodies, int n, int i, const float impulse[3], const float point[3]) {
    const D2rdPushFn fn = g_push.load();
    if (!fn) return false;
    const int32_t r = fn(state, params, bodies, n, i, impulse, point);
    Refused(r);
    return r > 0;
}

bool Step(D2rdState* state, const D2rdParams* params, float dt) {
    const D2rdStepFn fn = g_step.load();
    if (!fn) return false;
    const int32_t r = fn(state, params, dt);
    Refused(r);
    return r >= 0;
}

int Apply(const D2rdState* state, const D2rdParams* params, D2rdBody* bodies, int n) {
    const D2rdApplyFn fn = g_apply.load();
    if (!fn) return 0;
    const int32_t r = fn(state, params, bodies, n);
    Refused(r);
    return r > 0 ? r : 0;
}

float Mass(const D2rdParams* params, const D2rdBody* bodies, int n, uint32_t id) {
    const D2rdMassFn fn = g_mass.load();
    return fn ? fn(params, bodies, n, id) : 0.0f;
}

float Balance(const D2rdState* state, const D2rdParams* params, uint32_t id, float off[3]) {
    const D2rdBalanceFn fn = g_balance.load();
    if (!fn) {
        if (off) off[0] = off[1] = off[2] = 0.0f;
        return 0.0f;
    }
    return fn(state, params, id, off);
}

}  // namespace dolllib
