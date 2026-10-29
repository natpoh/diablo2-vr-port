// props_lib.cpp - see props_lib.h.

#include "props_lib.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace propslib {

namespace {

constexpr wchar_t kFile[] = L"d2r_vr_weight.dll";   // (the weight library: the props world is its D2rp* exports, 1.3 on)

SRWLOCK g_lock = SRWLOCK_INIT;
LogFn g_log = nullptr;
std::wstring g_path;
Status g_status = Status::NotLooked;
ULONGLONG g_lastTry = 0;
int g_toldMissing = -1;

struct Fns {
    D2rpCreateFn create = nullptr;
    D2rpDestroyFn destroy = nullptr;
    D2rpClearFn clear = nullptr;
    D2rpSetPropsFn setProps = nullptr;
    D2rpSetStaticsFn setStatics = nullptr;
    D2rpSetMoversFn setMovers = nullptr;
    D2rpStepFn step = nullptr;
    D2rpPosesFn poses = nullptr;
    D2rpKnocksFn knocks = nullptr;
    D2rpMoverAtFn moverAt = nullptr;
    D2rpSetHandsFn setHands = nullptr;   // optional: [weight] grab_props (none: no hand takes a prop)
    D2rpHandAtFn handAt = nullptr;
};
Fns g_fns;                       // set once, before g_loaded
std::atomic<bool> g_loaded{false};

void Say(const char* text) {
    if (g_log) g_log(text);
}

void Anchor() {}

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
    const std::wstring libs = dir + L"..\\d2r_vr_libs\\" + kFile;
    if (GetFileAttributesW(libs.c_str()) != INVALID_FILE_ATTRIBUTES) return libs;
    return dir + kFile;
}

bool FileThere(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
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

bool Loaded() { return g_loaded.load(); }

bool Ensure() {
    if (g_loaded.load()) return true;
    AcquireSRWLockExclusive(&g_lock);
    const ULONGLONG now = GetTickCount64();
    if (g_status == Status::Loaded || (g_lastTry && now - g_lastTry < 2000)) {
        ReleaseSRWLockExclusive(&g_lock);
        return g_status == Status::Loaded;
    }
    g_lastTry = now;
    const std::wstring path = PathOfFile();
    if (!FileThere(path)) {
        if (g_toldMissing != 0) {
            g_toldMissing = 0;
            Say("vrcam: props world - there is no d2r_vr_weight.dll in d2rloader\\d2r_vr_libs (or beside the plugin): [weight] knock_props does nothing");
        }
        g_status = Status::Missing;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        char b[160];
        snprintf(b, sizeof b, "vrcam: props world - d2r_vr_weight.dll could not be loaded (error %lu): the props stand where the game placed them", GetLastError());
        Say(b);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    const D2rpAbiFn abi = (D2rpAbiFn)GetProcAddress(mod, "D2rpAbi");
    Fns f;
    f.create = (D2rpCreateFn)GetProcAddress(mod, "D2rpCreate");
    f.destroy = (D2rpDestroyFn)GetProcAddress(mod, "D2rpDestroy");
    f.clear = (D2rpClearFn)GetProcAddress(mod, "D2rpClear");
    f.setProps = (D2rpSetPropsFn)GetProcAddress(mod, "D2rpSetProps");
    f.setStatics = (D2rpSetStaticsFn)GetProcAddress(mod, "D2rpSetStatics");
    f.setMovers = (D2rpSetMoversFn)GetProcAddress(mod, "D2rpSetMovers");
    f.step = (D2rpStepFn)GetProcAddress(mod, "D2rpStep");
    f.poses = (D2rpPosesFn)GetProcAddress(mod, "D2rpPoses");
    f.knocks = (D2rpKnocksFn)GetProcAddress(mod, "D2rpKnocks");
    f.moverAt = (D2rpMoverAtFn)GetProcAddress(mod, "D2rpMoverAt");
    f.setHands = (D2rpSetHandsFn)GetProcAddress(mod, "D2rpSetHands");
    f.handAt = (D2rpHandAtFn)GetProcAddress(mod, "D2rpHandAt");
    if (!f.setHands || !f.handAt) f.setHands = nullptr, f.handAt = nullptr;
    if (!abi || !f.create || !f.destroy || !f.clear || !f.setProps || !f.setStatics || !f.setMovers || !f.step || !f.poses || !f.knocks || !f.moverAt) {
        Say("vrcam: props world - d2r_vr_weight.dll has no props world (no D2rp* exports: a weight library older than 1.3) - [weight] knock_props does nothing");
        FreeLibrary(mod);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    if (abi() != D2RP_ABI_VERSION) {
        char b[160];
        snprintf(b, sizeof b, "vrcam: props world - d2r_vr_weight.dll speaks props interface version %d, the mod needs %d: not used", (int)abi(), (int)D2RP_ABI_VERSION);
        Say(b);
        FreeLibrary(mod);
        g_status = Status::BadAbi;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_fns = f;   // (the module stays loaded for the process: its functions never go away)
    g_status = Status::Loaded;
    g_loaded.store(true);
    Say(f.setHands ? "vrcam: props world - the weight library's props world is loaded (d2r_vr_weight.dll, D2rp* exports): [weight] knock_props knocks the level's props about, "
                     "[weight] grab_props takes them in the hand"
                   : "vrcam: props world - the weight library's props world is loaded (d2r_vr_weight.dll, D2rp* exports): [weight] knock_props knocks the level's props about "
                     "(no hands in it - D2rpSetHands: [weight] grab_props takes nothing with this library)");
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

void* Create(const D2rpParams* params) { return g_loaded.load() ? g_fns.create(params) : nullptr; }
void Destroy(void* world) { if (g_loaded.load() && world) g_fns.destroy(world); }
int32_t Clear(void* world, float floorY) { return g_loaded.load() && world ? g_fns.clear(world, floorY) : -1; }
int32_t SetProps(void* world, const D2rpProp* props, int32_t n) { return g_loaded.load() && world ? g_fns.setProps(world, props, n) : -1; }
int32_t SetStatics(void* world, const D2rpBox* boxes, int32_t n) { return g_loaded.load() && world ? g_fns.setStatics(world, boxes, n) : -1; }
int32_t SetMovers(void* world, const D2rpMover* movers, int32_t n) { return g_loaded.load() && world ? g_fns.setMovers(world, movers, n) : -1; }
int32_t Step(void* world, double now, float dt) { return g_loaded.load() && world ? g_fns.step(world, now, dt) : -1; }
int32_t Poses(void* world, D2rpPose* out, int32_t max) { return g_loaded.load() && world ? g_fns.poses(world, out, max) : 0; }
int32_t Knocks(void* world, D2rpKnock* out, int32_t max) { return g_loaded.load() && world ? g_fns.knocks(world, out, max) : 0; }
int32_t MoverAt(void* world, int32_t slot, D2rpMoverOut* out) { return g_loaded.load() && world ? g_fns.moverAt(world, slot, out) : 0; }
bool HasHands() { return g_loaded.load() && g_fns.setHands; }
int32_t SetHands(void* world, const D2rpHand* hands, int32_t n) { return HasHands() && world ? g_fns.setHands(world, hands, n) : -1; }
int32_t HandAt(void* world, int32_t hand, D2rpHandOut* out) { return HasHands() && world ? g_fns.handAt(world, hand, out) : 0; }

}  // namespace propslib
