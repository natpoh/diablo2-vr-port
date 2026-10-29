// gait_lib.cpp - see gait_lib.h.

#include "gait_lib.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace gaitlib {

namespace {

constexpr wchar_t kFile[] = L"d2r_vr_gait.dll";

SRWLOCK g_lock = SRWLOCK_INIT;   // Ensure / SetPath (the plugin's thread); Step reads the function pointer alone
LogFn g_log = nullptr;
std::wstring g_path;
Status g_status = Status::NotLooked;
HMODULE g_module = nullptr;
std::atomic<D2rfLegsStepFn> g_step{nullptr};
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
    const std::wstring libs = dir + L"..\\d2r_vr_libs\\" + kFile;   // d2rloader\d2r_vr_libs, as the grip and weight libraries
    if (GetFileAttributesW(libs.c_str()) != INVALID_FILE_ATTRIBUTES) return libs;
    return dir + kFile;
}

void Tell(Status s, const char* why) {   // (under g_lock)
    g_status = s;
    if (g_told == (int)s) return;
    g_told = (int)s;
    Say("vrcam: gait library - %s", why);
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
        Tell(Status::Missing, "d2r_vr_gait.dll is not there (d2rloader\\d2r_vr_libs): the other heroes' legs keep the game's own walk");
        return false;
    }
    HMODULE m = LoadLibraryW(path.c_str());
    if (!m) { Tell(Status::BrokenExports, "d2r_vr_gait.dll did not load: the legs keep the game's own walk"); return false; }
    const auto abi = (D2rfAbiFn)GetProcAddress(m, "D2rfAbi");
    const auto name = (D2rfNameFn)GetProcAddress(m, "D2rfName");
    const auto step = (D2rfLegsStepFn)GetProcAddress(m, "D2rfLegsStep");
    if (!abi || !name || !step) { FreeLibrary(m); Tell(Status::BrokenExports, "d2r_vr_gait.dll lacks its functions: not used"); return false; }
    if (abi() != D2RF_ABI_VERSION) { FreeLibrary(m); Tell(Status::BadAbi, "d2r_vr_gait.dll speaks another interface version: not used"); return false; }
    g_module = m;
    const char* n = name();
    g_name = n ? n : "?";
    g_step.store(step);
    g_status = Status::Loaded;
    g_told = (int)Status::Loaded;
    Say("vrcam: gait library %s loaded - the other heroes' legs step along their travel ([multiplayer] pose_share_legs)", g_name.c_str());
    return true;
}

bool Loaded() { return g_step.load() != nullptr; }

const char* Name() { return Loaded() ? g_name.c_str() : ""; }

bool Step(const D2rfLegsIn& in, D2rfState* state, D2rfLegsOut* out) {
    const D2rfLegsStepFn fn = g_step.load();
    if (!fn || !state || !out) return false;
    __try {
        return fn(&in, state, out) == 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_step.store(nullptr);   // a library that faults is not called again this run
        return false;
    }
}

}  // namespace gaitlib
