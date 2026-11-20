// walk_engine_lib.cpp - see walk_engine_lib.h.

#include "walk_engine_lib.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <string>

namespace welib {

namespace {

SRWLOCK g_lock = SRWLOCK_INIT;   // the status and the module (the calls themselves are one thread's)
LogFn g_log = nullptr;
std::wstring g_path;
std::string g_gameId = "d2r";
Status g_status = Status::NotLooked;
HMODULE g_module = nullptr;
void* g_engine = nullptr;
ULONGLONG g_lastTry = 0;
int g_toldMissing = -1;
char g_file[64] = "";

struct Fns {
    BWWE_AbiFn abi = nullptr;
    BWWE_OpenFn open = nullptr;
    BWWE_CloseFn close = nullptr;
    BWWE_SetTuningFn tuning = nullptr;
    BWWE_FeedFn feed = nullptr;
    BWWE_ResetFn reset = nullptr;
} g_fn;

void Say(const char* fmt, const char* a = "", const char* b = "") {
    if (!g_log) return;
    char buf[400];
    snprintf(buf, sizeof buf, fmt, a, b);
    g_log(buf);
}
void __cdecl EngineLog(const char* line) {   // the engine's own lines, marked
    if (!g_log || !line) return;
    char buf[480];
    snprintf(buf, sizeof buf, "vrcam: walk engine - %s", line);
    g_log(buf);
}

void Anchor() {}

bool FileThere(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// The places to look, in order: d2r_vr_walk.dll in d2r_vr_libs, then beside the plugin.
int Candidates(std::wstring out[2]) {
    if (!g_path.empty()) {
        out[0] = g_path;
        return 1;
    }
    HMODULE mod = nullptr;
    std::wstring dir;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Anchor, &mod)) {
        wchar_t buf[MAX_PATH * 2] = {};
        const DWORD n = GetModuleFileNameW(mod, buf, (DWORD)(sizeof buf / sizeof buf[0]));
        if (n && n < sizeof buf / sizeof buf[0]) {
            std::wstring p(buf, n);
            const size_t slash = p.find_last_of(L"\\/");
            if (slash != std::wstring::npos) dir = p.substr(0, slash + 1);
        }
    }
    const std::wstring libs = dir + L"..\\d2r_vr_libs\\";   // (out of d2rloader\plugins, where D2RLoader takes any DLL for a plugin)
    out[0] = libs + BWWE_FILE;
    out[1] = dir + BWWE_FILE;
    return 2;
}

template <class T> T Get(HMODULE m, const char* name) { return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(m, name))); }

void Drop() {   // (under the lock)
    if (g_module) FreeLibrary(g_module);
    g_module = nullptr;
    g_fn = Fns{};
    g_file[0] = 0;
}

void Narrow(const std::wstring& path, char* out, size_t cap) {
    const size_t slash = path.find_last_of(L"\\/");
    const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const int n = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1, out, (int)cap, nullptr, nullptr);
    if (n <= 0 && cap) out[0] = 0;
}

}  // namespace

void SetLog(LogFn fn) { g_log = fn; }

void SetGameId(const char* id) {
    AcquireSRWLockExclusive(&g_lock);
    g_gameId = id && *id ? id : "d2r";
    ReleaseSRWLockExclusive(&g_lock);
}

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

const char* StatusText(Status s) {
    switch (s) {
    case Status::NotLooked: return "not looked for yet";
    case Status::Missing: return "not there (d2rloader\\d2r_vr_libs\\d2r_vr_walk.dll)";
    case Status::BadAbi: return "another interface version";
    case Status::BrokenExports: return "not loadable or without its exports";
    case Status::OpenFailed: return "it would not open";
    case Status::Loaded: return "loaded";
    }
    return "?";
}

const char* FileName() { return g_file; }

bool Loaded() { return GetStatus() == Status::Loaded; }

bool Ensure(const char* dataDir) {
    AcquireSRWLockExclusive(&g_lock);
    if (g_status == Status::Loaded) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_lastTry && now - g_lastTry < 2000) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_lastTry = now;
    std::wstring paths[2];
    const int count = Candidates(paths);
    Status worst = Status::Missing;
    bool anyFile = false;
    for (int i = 0; i < count; ++i) {
        if (!FileThere(paths[i])) continue;
        anyFile = true;
        HMODULE mod = LoadLibraryExW(paths[i].c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!mod) {
            if (worst == Status::Missing) worst = Status::BrokenExports;
            continue;
        }
        Fns f;
        f.abi = Get<BWWE_AbiFn>(mod, "BWWE_Abi");
        f.open = Get<BWWE_OpenFn>(mod, "BWWE_Open");
        f.close = Get<BWWE_CloseFn>(mod, "BWWE_Close");
        f.tuning = Get<BWWE_SetTuningFn>(mod, "BWWE_SetTuning");
        f.feed = Get<BWWE_FeedFn>(mod, "BWWE_Feed");
        f.reset = Get<BWWE_ResetFn>(mod, "BWWE_Reset");
        if (!f.abi || !f.open || !f.close || !f.tuning || !f.feed || !f.reset) {   // (another DLL of that name without the walk: the next place)
            FreeLibrary(mod);
            if (worst == Status::Missing) worst = Status::BrokenExports;
            continue;
        }
        char name[64];
        Narrow(paths[i], name, sizeof name);
        if (f.abi() != BWWE_ABI) {
            char b[32], w[32];
            snprintf(b, sizeof b, "%u", f.abi());
            snprintf(w, sizeof w, "%u", BWWE_ABI);
            Say("vrcam: walk engine - %s speaks interface version %s, the mod needs another: not used", name, b);
            (void)w;
            FreeLibrary(mod);
            worst = Status::BadAbi;
            continue;
        }
        BWWE_Init init{};
        init.size = sizeof init;
        init.abi = BWWE_ABI;
        init.dataDir = dataDir ? dataDir : "";
        init.gameId = g_gameId.c_str();
        init.log = &EngineLog;
        void* e = f.open(&init);
        if (!e) {
            Say("vrcam: walk engine - %s would not open (its log line above says why)", name);
            FreeLibrary(mod);
            worst = Status::OpenFailed;
            continue;
        }
        g_module = mod;
        g_fn = f;
        g_engine = e;
        g_status = Status::Loaded;
        snprintf(g_file, sizeof g_file, "%s", name);
        g_toldMissing = 1;
        Say("vrcam: walk engine - %s loaded and open (the walk profile from %s\\games\\...\\walk.json)", name, init.dataDir);
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    if (!anyFile || worst == Status::BrokenExports) {
        if (g_toldMissing != (int)worst) {
            g_toldMissing = (int)worst;
            Say(anyFile ? "vrcam: walk engine - no library with the walk's BWWE_* exports in d2rloader\\d2r_vr_libs (or beside the plugin): no walk in place"
                        : "vrcam: walk engine - there is no d2r_vr_walk.dll in d2rloader\\d2r_vr_libs (or beside the plugin): no walk in place");
        }
        worst = anyFile ? Status::BrokenExports : Status::Missing;
    }
    g_status = worst;
    ReleaseSRWLockExclusive(&g_lock);
    return false;
}

void Shutdown() {
    AcquireSRWLockExclusive(&g_lock);
    if (g_engine && g_fn.close) g_fn.close(g_engine);
    g_engine = nullptr;
    if (g_status == Status::Loaded) Say("vrcam: walk engine - closed");
    Drop();
    g_status = Status::NotLooked;
    g_lastTry = 0;
    g_toldMissing = -1;
    ReleaseSRWLockExclusive(&g_lock);
}

// (one thread calls these: the lock above guards the load / unload, which happen on that thread too)
void SetTuning(const BWWE_Tuning& t) {
    if (g_engine) g_fn.tuning(g_engine, &t);
}
void Feed(const BWWE_Frame& frame, BWWE_State* out) {
    if (!out) return;
    *out = BWWE_State{};
    out->size = sizeof *out;
    if (g_engine) g_fn.feed(g_engine, &frame, out);
}
void Reset() {
    if (g_engine) g_fn.reset(g_engine);
}

}  // namespace welib
