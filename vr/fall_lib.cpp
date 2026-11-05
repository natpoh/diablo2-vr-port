// fall_lib.cpp - see fall_lib.h.

#include "fall_lib.h"
#include "weight_worlds.h"   // one lock round the library's calls that make or drop a Box3D world

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>

namespace falllib {

namespace {

constexpr wchar_t kFile[] = L"d2r_vr_weight.dll";   // (the weight library: the knock-down is its D2rf* exports)

SRWLOCK g_lock = SRWLOCK_INIT;
LogFn g_log = nullptr;
std::wstring g_path;
bool g_loaded = false;
HMODULE g_module = nullptr;
D2rfCreateFn g_create = nullptr;
D2rfDestroyFn g_destroy = nullptr;
D2rfKnockDownFn g_knock = nullptr;
D2rfStepFn g_step = nullptr;
D2rfPosesFn g_poses = nullptr;
D2rfLetGoFn g_letGo = nullptr;
D2rfHomeFn g_home = nullptr;
D2rfGrabFn g_grab = nullptr;
D2rfGrabPartFn g_grabPart = nullptr;   // (optional: fall_physics 1.7)
D2rfSetStrengthFn g_setStrength = nullptr;   // (optional: fall_physics 1.12)
D2rfSetHandStrengthFn g_setHandStrength = nullptr;
D2rfSetBalanceFn g_setBalance = nullptr;   // (optional: fall_physics 1.13)
bool g_balance = true;
float g_strength = 1.0f;
D2rfHandFn g_hand = nullptr;
D2rfReleaseFn g_release = nullptr;
D2rfHeldFn g_held = nullptr;
void* g_world = nullptr;
std::string g_name;
ULONGLONG g_lastTry = 0;
int g_toldMissing = -1;
std::atomic<bool> g_errorTold{false};

void Say(const char* fmt, const char* a = "", const char* b = "") {
    if (!g_log) return;
    char buf[400];
    snprintf(buf, sizeof buf, fmt, a, b);
    g_log(buf);
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

void Refused(int32_t r) {
    if (r < 0 && !g_errorTold.exchange(true)) Say("vrcam: knock-down library - a call was refused: the bodies stand as the game poses them");
}

void* World() {
    if (!g_world && g_create) {
        g_world = g_create();
        if (g_world && g_setStrength) g_setStrength(g_world, g_strength);
        if (g_world && g_setBalance) g_setBalance(g_world, g_balance ? 1 : 0);
    }
    return g_world;
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

bool Loaded() {
    AcquireSRWLockShared(&g_lock);
    const bool l = g_loaded;
    ReleaseSRWLockShared(&g_lock);
    return l;
}

const char* Name() { return Loaded() ? g_name.c_str() : ""; }

bool Probe() {
    AcquireSRWLockExclusive(&g_lock);
    const bool there = FileThere(PathOfFile());
    if (!g_loaded && g_toldMissing != (there ? 1 : 0)) {
        g_toldMissing = there ? 1 : 0;
        if (there) Say("vrcam: knock-down library - d2r_vr_weight.dll is in d2rloader\\d2r_vr_libs (or beside the plugin); it is loaded when [weight] knock_down 1 asks for it");
        else Say("vrcam: knock-down library - there is no d2r_vr_weight.dll in d2rloader\\d2r_vr_libs (or beside the plugin): [weight] knock_down does nothing");
    }
    ReleaseSRWLockExclusive(&g_lock);
    return there;
}

bool Ensure() {
    AcquireSRWLockExclusive(&g_lock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_lock); } } unlock;
    if (g_loaded) return true;
    const ULONGLONG now = GetTickCount64();
    if (g_lastTry && now - g_lastTry < 2000) return false;
    g_lastTry = now;
    const std::wstring path = PathOfFile();
    if (!FileThere(path)) {
        if (g_toldMissing != 0) {
            g_toldMissing = 0;
            Say("vrcam: knock-down library - there is no d2r_vr_weight.dll in d2rloader\\d2r_vr_libs (or beside the plugin): [weight] knock_down does nothing");
        }
        return false;
    }
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        char why[64];
        snprintf(why, sizeof why, "error %lu", GetLastError());
        Say("vrcam: knock-down library - d2r_vr_weight.dll could not be loaded (%s): the bodies stand as the game poses them", why);
        return false;
    }
    const D2rfAbiFn abi = (D2rfAbiFn)GetProcAddress(mod, "D2rfAbi");
    const D2rfNameFn name = (D2rfNameFn)GetProcAddress(mod, "D2rfName");
    g_create = (D2rfCreateFn)GetProcAddress(mod, "D2rfCreate");
    g_destroy = (D2rfDestroyFn)GetProcAddress(mod, "D2rfDestroy");
    g_knock = (D2rfKnockDownFn)GetProcAddress(mod, "D2rfKnockDown");
    g_step = (D2rfStepFn)GetProcAddress(mod, "D2rfStep");
    g_poses = (D2rfPosesFn)GetProcAddress(mod, "D2rfPoses");
    g_letGo = (D2rfLetGoFn)GetProcAddress(mod, "D2rfLetGo");
    g_home = (D2rfHomeFn)GetProcAddress(mod, "D2rfHome");
    g_grab = (D2rfGrabFn)GetProcAddress(mod, "D2rfGrab");
    g_grabPart = (D2rfGrabPartFn)GetProcAddress(mod, "D2rfGrabPart");
    g_setStrength = (D2rfSetStrengthFn)GetProcAddress(mod, "D2rfSetStrength");
    g_setHandStrength = (D2rfSetHandStrengthFn)GetProcAddress(mod, "D2rfSetHandStrength");
    g_setBalance = (D2rfSetBalanceFn)GetProcAddress(mod, "D2rfSetBalance");
    g_hand = (D2rfHandFn)GetProcAddress(mod, "D2rfHand");
    g_release = (D2rfReleaseFn)GetProcAddress(mod, "D2rfRelease");
    g_held = (D2rfHeldFn)GetProcAddress(mod, "D2rfHeld");
    if (!abi || !g_create || !g_destroy || !g_knock || !g_step || !g_poses || !g_letGo || !g_home || !g_grab || !g_hand || !g_release || !g_held) {
        Say("vrcam: knock-down library - d2r_vr_weight.dll has no knock-down (no D2rf* exports: an older weight library) - [weight] knock_down does nothing");
        FreeLibrary(mod);
        g_create = nullptr; g_destroy = nullptr; g_knock = nullptr; g_step = nullptr; g_poses = nullptr; g_letGo = nullptr;
        return false;
    }
    if (abi() != D2RF_ABI_VERSION) {
        char b[32], w[32];
        snprintf(b, sizeof b, "%d", (int)abi());
        snprintf(w, sizeof w, "%d", (int)D2RF_ABI_VERSION);
        Say("vrcam: knock-down library - d2r_vr_weight.dll speaks interface version %s, the mod needs %s: not used", b, w);
        FreeLibrary(mod);
        g_create = nullptr; g_destroy = nullptr; g_knock = nullptr; g_step = nullptr; g_poses = nullptr; g_letGo = nullptr;
        return false;
    }
    g_module = mod;
    g_name = name && name() ? name() : "unnamed";
    g_loaded = true;
    Say("vrcam: knock-down library - %s loaded from d2rloader\\d2r_vr_libs (or beside the plugin): [weight] knock_down draws struck bodies falling and getting up", g_name.c_str());
    return true;
}

int KnockDown(const D2rfKnock& k) {
    const weightworlds::Hold hold;   // (a world may be made or dropped: vr/weight_worlds.h)
    if (!Loaded()) return 0;
    void* w = World();
    if (!w) return 0;
    const int32_t r = g_knock(w, &k);
    Refused(r);
    return r;
}

int Step(float dt) {
    const weightworlds::Hold hold;   // (a world may be made or dropped: vr/weight_worlds.h)
    if (!Loaded() || !g_world) return 0;
    const int32_t r = g_step(g_world, dt);
    Refused(r);
    return r;
}

int Poses(D2rfPose* out, int max) {
    if (!Loaded() || !g_world) return 0;
    const int32_t r = g_poses(g_world, out, max);
    Refused(r);
    return r > 0 ? r : 0;
}

int LetGo(uint32_t id, int how) {
    const weightworlds::Hold hold;   // (a world may be made or dropped: vr/weight_worlds.h)
    if (!Loaded() || !g_world) return 0;
    const int32_t r = g_letGo(g_world, id, how);
    Refused(r);
    return r;
}

int Home(uint32_t id, const float joint[D2RF_JOINTS][3]) {
    const weightworlds::Hold hold;   // (a world may be made or dropped: vr/weight_worlds.h)
    if (!Loaded() || !g_world) return 0;
    const int32_t r = g_home(g_world, id, joint);
    Refused(r);
    return r;
}

int Grab(const D2rfKnock& body, int hand, const float point[3], int joint) {
    const weightworlds::Hold hold;   // (a world may be made or dropped: vr/weight_worlds.h)
    if (!Loaded()) return 0;
    void* w = World();
    if (!w) return 0;
    const int32_t r = joint >= 0 && g_grabPart ? g_grabPart(w, &body, hand, point, joint) : g_grab(w, &body, hand, point);
    Refused(r);
    return r;
}

int Hand(uint32_t id, int hand, const float pos[3]) {
    const weightworlds::Hold hold;   // (a world may be made or dropped: vr/weight_worlds.h)
    if (!Loaded() || !g_world) return 0;
    const int32_t r = g_hand(g_world, id, hand, pos);
    Refused(r);
    return r;
}

int Release(uint32_t id, int hand) {
    const weightworlds::Hold hold;   // (a world may be made or dropped: vr/weight_worlds.h)
    if (!Loaded() || !g_world) return 0;
    const int32_t r = g_release(g_world, id, hand);
    Refused(r);
    return r;
}

int Held(uint32_t id, int hand, float a[3], float b[3], float* r) {
    if (!Loaded() || !g_world) return 0;
    const int32_t k = g_held(g_world, id, hand, a, b, r);
    Refused(k);
    return k > 0 ? k : 0;
}

void Reset() {
    const weightworlds::Hold hold;   // (a world may be made or dropped: vr/weight_worlds.h)
    if (!Loaded() || !g_world) return;
    g_destroy(g_world);
    g_world = nullptr;
}

void SetBalance(bool on) {
    const weightworlds::Hold hold;   // (a world may be made or dropped: vr/weight_worlds.h)
    g_balance = on;
    if (Loaded() && g_setBalance)
        if (void* w = World()) g_setBalance(w, on ? 1 : 0);
}

int SetHandStrength(uint32_t id, int hand, float strength) {
    const weightworlds::Hold hold;   // (a world may be made or dropped: vr/weight_worlds.h)
    if (!Loaded() || !g_setHandStrength) return 0;
    void* w = World();
    return w ? g_setHandStrength(w, id, hand, strength) : 0;
}

void SetStrength(float strength) {
    if (!std::isfinite(strength)) return;
    g_strength = strength;
    const weightworlds::Hold hold;   // (a world may be made: vr/weight_worlds.h)
    if (Loaded() && g_setStrength)
        if (void* w = World()) g_setStrength(w, strength);
}

}  // namespace falllib
