// grip_lib.cpp - see grip_lib.h.

#include "grip_lib.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "hold.h"

namespace griplib {

namespace {

namespace hh = heft::hold;

constexpr wchar_t kFile[] = L"d2r_vr_grip.dll";

SRWLOCK g_lock = SRWLOCK_INIT;
LogFn g_log = nullptr;
std::wstring g_path;                 // an override (tests); empty: beside this module
Status g_status = Status::NotLooked;
HMODULE g_module = nullptr;
D2rgLineStepFn g_line = nullptr;
D2rgHandStepFn g_hand = nullptr;
std::string g_name;
int g_abi = 0;                       // the interface version the loaded library speaks
ULONGLONG g_lastTry = 0;
int g_toldMissing = -1;              // what the log was last told about the file: 0 missing, 1 there
std::atomic<bool> g_lineErrorTold{false}, g_handErrorTold{false};

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
    return (slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1)) + kFile;
}

bool FileThere(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static_assert(sizeof(D2rgLineState) <= sizeof(hh::LineState::blob), "the library's line state must fit the hold's blob");

// The library behind heft::hold::GripEngine.
class LibEngine final : public hh::GripEngine {
public:
    bool Line(hh::LineState& st, const hh::LineIn& in, double dt, hh::LineOut* out) const override {
        D2rgLineStepFn fn = g_line;
        if (!fn || !out) return false;
        D2rgLineIn li {};
        li.size    = sizeof li;
        li.dt      = std::clamp(dt, 0.0, 0.1);
        li.grip    = in.grip;
        li.trigger = in.trigger;
        li.holds   = in.holds ? 1 : 0;
        li.second  = in.second ? 1 : 0;
        li.inReach = in.inReach ? 1 : 0;
        li.t       = in.t;
        li.raw     = in.raw;
        li.rollRaw = in.rollRaw;
        li.butt    = in.butt;
        li.tip     = in.tip;
        D2rgLineOut lo {};
        const int32_t r = fn(&li, reinterpret_cast<D2rgLineState*>(st.blob), &lo);
        if (r < 0) {
            if (!g_lineErrorTold.exchange(true)) Say("vrcam: grip library - D2rgLineStep refused a step: our own rule holds that hand");
            return false;
        }
        out->hold      = lo.hold != 0;
        out->mode      = lo.mode;
        out->t         = lo.t;
        out->roll      = lo.roll;
        out->events    = lo.events;
        out->hapticAmp = lo.hapticAmp;
        out->hapticMs  = lo.hapticMs;
        return true;
    }
};

LibEngine g_engine;

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

int Abi() { return Loaded() ? g_abi : 0; }

bool Probe() {
    AcquireSRWLockExclusive(&g_lock);
    const bool there = FileThere(PathOfFile());
    if (g_status != Status::Loaded && g_toldMissing != (there ? 1 : 0)) {
        g_toldMissing = there ? 1 : 0;
        if (there) Say("vrcam: grip library - d2r_vr_grip.dll is beside the plugin; it is loaded when [weight] grip_model 1 asks for it");
        else Say("vrcam: grip library - there is no d2r_vr_grip.dll beside the plugin: [weight] grip_model 1 keeps our own way of holding");
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
            Say("vrcam: grip library - there is no d2r_vr_grip.dll beside the plugin: [weight] grip_model 1 keeps our own way of holding");
        }
        g_status = Status::Missing;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        char why[64];
        snprintf(why, sizeof why, "error %lu", GetLastError());
        Say("vrcam: grip library - d2r_vr_grip.dll could not be loaded (%s): our own way of holding", why);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    const D2rgAbiFn abi = (D2rgAbiFn)GetProcAddress(mod, "D2rgAbi");
    const D2rgNameFn name = (D2rgNameFn)GetProcAddress(mod, "D2rgName");
    const D2rgLineStepFn line = (D2rgLineStepFn)GetProcAddress(mod, "D2rgLineStep");
    const D2rgHandStepFn hand = (D2rgHandStepFn)GetProcAddress(mod, "D2rgHandStep");
    if (!abi || !line || !hand) {
        Say("vrcam: grip library - d2r_vr_grip.dll lacks D2rgAbi / D2rgLineStep / D2rgHandStep: not used");
        FreeLibrary(mod);
        g_status = Status::BrokenExports;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    const int32_t v = abi();
    if (v < D2RG_ABI_OLDEST || v > D2RG_ABI_VERSION) {
        char b[32];
        snprintf(b, sizeof b, "%d", (int)v);
        char w[32];
        snprintf(w, sizeof w, "%d to %d", (int)D2RG_ABI_OLDEST, (int)D2RG_ABI_VERSION);
        Say("vrcam: grip library - d2r_vr_grip.dll speaks interface version %s, the mod needs %s: not used", b, w);
        FreeLibrary(mod);
        g_status = Status::BadAbi;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_module = mod;
    g_line   = line;
    g_hand   = hand;
    g_abi    = (int)v;
    g_name   = name && name() ? name() : "unnamed";
    g_status = Status::Loaded;
    hh::SetGripEngine(&g_engine);
    Say("vrcam: grip library - %s loaded from beside the plugin: [weight] grip_model 1 holds the weapon the library's way", g_name.c_str());
    if (v < D2RG_ABI_VERSION) Say("vrcam: grip library - it speaks interface version 1: the hand is laid on the wood by its palm's surface, not where the game's own grip has the shaft (a newer d2r_vr_grip.dll does)");
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

int32_t HandStep(const D2rgHandIn& in, const D2rgRig& rig, D2rgHandState* state, D2rgHandOut* out) {
    D2rgHandStepFn fn = g_hand;
    if (!fn || !state || !out) return -1000;
    int32_t r;
    if (g_abi >= 2) {
        r = fn(&in, &rig, state, out);
    } else {   // interface version 1: its struct ends before the version-2 fields, and it knows no lean of the line
        D2rgHandIn v1 = in;
        v1.hasAxisInHand = 0;
        v1.axisInHand = {};
        v1.lineInHand = {};
        r = fn(&v1, &rig, state, out);
    }
    if (r < 0 && !g_handErrorTold.exchange(true)) Say("vrcam: grip library - D2rgHandStep refused a step: our own hand is drawn");
    return r;
}

}  // namespace griplib
