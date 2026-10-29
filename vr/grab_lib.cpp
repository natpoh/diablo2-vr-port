// grab_lib.cpp - see grab_lib.h.

#include "grab_lib.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace grablib {

namespace {

constexpr wchar_t kFile[] = L"d2r_vr_weight.dll";   // (the weight library: the grab is its D2rgb* exports, 1.4 on)

SRWLOCK g_lock = SRWLOCK_INIT;
LogFn g_log = nullptr;
std::wstring g_path;                 // an override (tests); empty: d2r_vr_libs or beside this module
Status g_status = Status::NotLooked;
HMODULE g_module = nullptr;
D2rgbCreateFn g_create = nullptr;
D2rgbDestroyFn g_destroy = nullptr;
D2rgbStepFn g_step = nullptr;
D2rgbSwaysFn g_sways = nullptr;
D2rgbReleaseFn g_release = nullptr;
D2rgbLinksFn g_links = nullptr;
void* g_world = nullptr;
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
    const std::wstring libs = dir + L"..\\d2r_vr_libs\\" + kFile;   // (as the weight, doll and grip libraries: out of d2rloader\plugins)
    if (GetFileAttributesW(libs.c_str()) != INVALID_FILE_ATTRIBUTES) return libs;
    return dir + kFile;
}

bool FileThere(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void Refused(int32_t r) {
    if (r < 0 && !g_errorTold.exchange(true)) Say("vrcam: grab library - a call was refused: the hands hold nothing");
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
            Say("vrcam: grab library - there is no d2r_vr_weight.dll in d2rloader\\d2r_vr_libs (or beside the plugin): [weight] grab_bodies does nothing");
        }
        g_status = Status::Missing;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        char why[64];
        snprintf(why, sizeof why, "error %lu", GetLastError());
        Say("vrcam: grab library - d2r_vr_weight.dll could not be loaded (%s): [weight] grab_bodies does nothing", why);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    const D2rgbAbiFn abi = (D2rgbAbiFn)GetProcAddress(mod, "D2rgbAbi");
    const D2rgbNameFn name = (D2rgbNameFn)GetProcAddress(mod, "D2rgbName");
    const D2rgbCreateFn create = (D2rgbCreateFn)GetProcAddress(mod, "D2rgbCreate");
    const D2rgbDestroyFn destroy = (D2rgbDestroyFn)GetProcAddress(mod, "D2rgbDestroy");
    const D2rgbStepFn step = (D2rgbStepFn)GetProcAddress(mod, "D2rgbStep");
    const D2rgbSwaysFn sways = (D2rgbSwaysFn)GetProcAddress(mod, "D2rgbSways");
    const D2rgbReleaseFn release = (D2rgbReleaseFn)GetProcAddress(mod, "D2rgbReleaseAll");
    const D2rgbLinksFn links = (D2rgbLinksFn)GetProcAddress(mod, "D2rgbLinks");
    if (!abi || !create || !destroy || !step || !sways || !release || !links) {
        Say("vrcam: grab library - d2r_vr_weight.dll has no grab (no D2rgb* exports: a weight library older than 1.4) - [weight] grab_bodies does nothing");
        FreeLibrary(mod);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    const int32_t v = abi();
    if (v != D2RGB_ABI_VERSION) {
        char b[32], w[32];
        snprintf(b, sizeof b, "%d", (int)v);
        snprintf(w, sizeof w, "%d", (int)D2RGB_ABI_VERSION);
        Say("vrcam: grab library - d2r_vr_weight.dll speaks grab interface version %s, the mod needs %s: not used", b, w);
        FreeLibrary(mod);
        g_status = Status::BadAbi;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    void* world = create(nullptr);
    if (!world) {
        Say("vrcam: grab library - d2r_vr_weight.dll made no world: [weight] grab_bodies does nothing");
        FreeLibrary(mod);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_module = mod;
    g_create = create;
    g_destroy = destroy;
    g_step = step;
    g_sways = sways;
    g_release = release;
    g_links = links;
    g_world = world;
    g_name = name && name() ? name() : "unnamed";
    g_status = Status::Loaded;
    Say("vrcam: grab library - %s loaded from d2rloader\\d2r_vr_libs (or beside the plugin): [weight] grab_bodies grabs with it", g_name.c_str());
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

int Step(const D2rgbParams* params, const D2rgbBody* bodies, int n, const D2rgbHand* hands, float dt, D2rgbHandOut* out) {
    if (!Loaded() || !g_step || !g_world) return -1;
    const int32_t r = g_step(g_world, params, bodies, n, hands, dt, out);
    Refused(r);
    return r;
}

int Sways(D2rgbSway* out, int max) {
    if (!Loaded() || !g_sways || !g_world) return 0;
    const int32_t r = g_sways(g_world, out, max);
    Refused(r);
    return r > 0 ? r : 0;
}

int Links(D2rgbLink* out, int max) {
    if (!Loaded() || !g_links || !g_world) return 0;
    const int32_t r = g_links(g_world, out, max);
    Refused(r);
    return r > 0 ? r : 0;
}

int ReleaseAll() {
    if (!Loaded() || !g_release || !g_world) return 0;
    const int32_t r = g_release(g_world);
    return r > 0 ? r : 0;
}

}  // namespace grablib
