// gesture_engine_lib.cpp - see gesture_engine_lib.h.

#include "gesture_engine_lib.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace gelib {

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
bool g_errorTold = false;

struct Fns {
    BWGE_AbiFn abi = nullptr;
    BWGE_OpenFn open = nullptr;
    BWGE_CloseFn close = nullptr;
    BWGE_SetSetFn setSet = nullptr;
    BWGE_FeedFn feed = nullptr;
    BWGE_RecordFn record = nullptr;
    BWGE_PlainFn stop = nullptr, keep = nullptr, discard = nullptr;
    BWGE_GestureFn clear = nullptr;
    BWGE_BindFn bind = nullptr;
    BWGE_ColorFn color = nullptr;
    BWGE_ThresholdFn threshold = nullptr;
    BWGE_ListFn list = nullptr;
    BWGE_PollCastsFn poll = nullptr;
    BWGE_TrailFn trail = nullptr;
    BWGE_GetPoseZoneFn getZone = nullptr;   // (optional)
    BWGE_SetPoseZoneFn setZone = nullptr;
    BWGE_GetTakeFn getTake = nullptr;       // (optional)
    BWGE_BindActionFn bindAction = nullptr; // (optional, the three together)
    BWGE_GetActionFn getAction = nullptr;
    BWGE_PollCasts2Fn poll2 = nullptr;
    BWGE_SetHandFn setHand = nullptr;       // (optional)
    BWGE_SetModeFn setMode = nullptr;       // (optional, the two together)
    BWGE_GetModeFn getMode = nullptr;
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
    snprintf(buf, sizeof buf, "vrcam: gesture engine - %s", line);
    g_log(buf);
}

void Anchor() {}

std::wstring PathOfFile() {
    if (!g_path.empty()) return g_path;
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&Anchor, &mod)) return BWGE_FILE;
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(mod, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    if (!n || n >= sizeof buf / sizeof buf[0]) return BWGE_FILE;
    std::wstring p(buf, n);
    const size_t slash = p.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? std::wstring() : p.substr(0, slash + 1);
    const std::wstring libs = dir + L"..\\d2r_vr_libs\\" + BWGE_FILE;   // (out of d2rloader\plugins, where D2RLoader takes any DLL for a plugin)
    if (GetFileAttributesW(libs.c_str()) != INVALID_FILE_ATTRIBUTES) return libs;
    return dir + BWGE_FILE;
}

bool FileThere(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

template <class T> T Get(HMODULE m, const char* name) { return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(m, name))); }

void Fail(Status s) {   // (under the lock)
    if (g_module) FreeLibrary(g_module);
    g_module = nullptr;
    g_fn = Fns{};
    g_status = s;
}

int Refused(int r) {
    if (r != BW_LINK_GR_OK && r != BW_LINK_GR_NO_GESTURE && r != BW_LINK_GR_BUSY && !g_errorTold) {
        g_errorTold = true;
        char b[16];
        snprintf(b, sizeof b, "%d", r);
        Say("vrcam: gesture engine - a call was refused (result %s); said once", b);
    }
    return r;
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
    case Status::Missing: return "not there (d2rloader\\d2r_vr_libs\\d2r_vr_gestures.dll)";
    case Status::BadAbi: return "another interface version";
    case Status::BrokenExports: return "not loadable or without its exports";
    case Status::OpenFailed: return "it would not open";
    case Status::Loaded: return "loaded";
    }
    return "?";
}

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
    const std::wstring path = PathOfFile();
    if (!FileThere(path)) {
        if (g_toldMissing != 0) {
            g_toldMissing = 0;
            Say("vrcam: gesture engine - there is no d2r_vr_gestures.dll in d2rloader\\d2r_vr_libs (or beside the plugin): no gestures in the game (run D2R VR Setup again)");
        }
        g_status = Status::Missing;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_toldMissing = 1;
    HMODULE mod = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) {
        char why[64];
        snprintf(why, sizeof why, "error %lu", GetLastError());
        Say("vrcam: gesture engine - d2r_vr_gestures.dll could not be loaded (%s)", why);
        Fail(Status::BrokenExports);
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_module = mod;
    Fns f;
    f.abi = Get<BWGE_AbiFn>(mod, "BWGE_Abi");
    f.open = Get<BWGE_OpenFn>(mod, "BWGE_Open");
    f.close = Get<BWGE_CloseFn>(mod, "BWGE_Close");
    f.setSet = Get<BWGE_SetSetFn>(mod, "BWGE_SetSet");
    f.feed = Get<BWGE_FeedFn>(mod, "BWGE_Feed");
    f.record = Get<BWGE_RecordFn>(mod, "BWGE_Record");
    f.stop = Get<BWGE_PlainFn>(mod, "BWGE_Stop");
    f.keep = Get<BWGE_PlainFn>(mod, "BWGE_Keep");
    f.discard = Get<BWGE_PlainFn>(mod, "BWGE_Discard");
    f.clear = Get<BWGE_GestureFn>(mod, "BWGE_Clear");
    f.bind = Get<BWGE_BindFn>(mod, "BWGE_Bind");
    f.color = Get<BWGE_ColorFn>(mod, "BWGE_SetColor");
    f.threshold = Get<BWGE_ThresholdFn>(mod, "BWGE_SetThreshold");
    f.list = Get<BWGE_ListFn>(mod, "BWGE_List");
    f.poll = Get<BWGE_PollCastsFn>(mod, "BWGE_PollCasts");
    f.trail = Get<BWGE_TrailFn>(mod, "BWGE_Trail");
    f.getZone = Get<BWGE_GetPoseZoneFn>(mod, "BWGE_GetPoseZone");   // (optional: an older library has no zones)
    f.setZone = Get<BWGE_SetPoseZoneFn>(mod, "BWGE_SetPoseZone");
    if (!f.getZone || !f.setZone) f.getZone = nullptr, f.setZone = nullptr;
    f.getTake = Get<BWGE_GetTakeFn>(mod, "BWGE_GetTake");   // (optional: Play makes its take up from the list without it)
    f.bindAction = Get<BWGE_BindActionFn>(mod, "BWGE_BindAction");   // (optional: game actions on gestures)
    f.getAction = Get<BWGE_GetActionFn>(mod, "BWGE_GetAction");
    f.poll2 = Get<BWGE_PollCasts2Fn>(mod, "BWGE_PollCasts2");
    if (!f.bindAction || !f.getAction || !f.poll2) f.bindAction = nullptr, f.getAction = nullptr, f.poll2 = nullptr;
    f.setHand = Get<BWGE_SetHandFn>(mod, "BWGE_SetHand");   // (optional: a glyph's hand changed without a take)
    f.setMode = Get<BWGE_SetModeFn>(mod, "BWGE_SetMode");   // (optional: cast once / select)
    f.getMode = Get<BWGE_GetModeFn>(mod, "BWGE_GetMode");
    if (!f.setMode || !f.getMode) f.setMode = nullptr, f.getMode = nullptr;
    if (!f.abi || !f.open || !f.close || !f.setSet || !f.feed || !f.record || !f.stop || !f.keep || !f.discard || !f.clear || !f.bind || !f.color ||
        !f.threshold || !f.list || !f.poll || !f.trail) {
        Say("vrcam: gesture engine - d2r_vr_gestures.dll lacks some of its BWGE_* exports: not used");
        Fail(Status::BrokenExports);
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    if (f.abi() != BWGE_ABI) {
        char b[32], w[32];
        snprintf(b, sizeof b, "%u", f.abi());
        snprintf(w, sizeof w, "%u", BWGE_ABI);
        Say("vrcam: gesture engine - d2r_vr_gestures.dll speaks interface version %s, the mod needs %s: not used", b, w);
        Fail(Status::BadAbi);
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    BWGE_Init init{};
    init.size = sizeof init;
    init.abi = BWGE_ABI;
    init.dataDir = dataDir ? dataDir : "";
    init.gameId = g_gameId.c_str();
    init.log = &EngineLog;
    init.userHeightM = 0.0f;   // (0: the library reads BodyWalk's own height and arm length - the mod's T-pose measures eye height and grip-to-grip span,
    init.armLengthM = 0.0f;    //  not BodyWalk's measures: a take recorded here and one recorded in BodyWalk then compare alike)
    void* e = f.open(&init);
    if (!e) {
        Say("vrcam: gesture engine - d2r_vr_gestures.dll would not open (its log line above says why)");
        Fail(Status::OpenFailed);
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    g_fn = f;
    g_engine = e;
    g_status = Status::Loaded;
    g_errorTold = false;
    Say("vrcam: gesture engine - d2r_vr_gestures.dll loaded and open (the gestures kept in %s\\games\\%s)", init.dataDir, init.gameId);
    Say(f.getZone ? "vrcam: gesture engine - it has pose zones (BWGE_GetPoseZone / BWGE_SetPoseZone)"
                  : "vrcam: gesture engine - an older library without pose zones: their sizes are kept in the game for the session");
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

void Shutdown() {
    AcquireSRWLockExclusive(&g_lock);
    if (g_engine && g_fn.close) g_fn.close(g_engine);
    g_engine = nullptr;
    if (g_status == Status::Loaded) Say("vrcam: gesture engine - closed");
    Fail(Status::NotLooked);
    g_lastTry = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

// (one thread calls these: the lock above guards the load / unload, which happen on that thread too)
void SetSet(const char* setKey, const char* setName, const BWGE_Skill* skills, uint32_t count) {
    if (g_engine) g_fn.setSet(g_engine, setKey ? setKey : "", setName ? setName : "", skills, count);
}
void Feed(const BWGE_Frame& frame) {
    if (g_engine) g_fn.feed(g_engine, &frame);
}
int Record(uint32_t gesture, uint8_t type, uint8_t hand, int32_t skill) { return g_engine ? Refused(g_fn.record(g_engine, gesture, type, hand, skill)) : BW_LINK_GR_BAD; }
int Stop() { return g_engine ? Refused(g_fn.stop(g_engine)) : BW_LINK_GR_BAD; }
int Keep() { return g_engine ? Refused(g_fn.keep(g_engine)) : BW_LINK_GR_BAD; }
int Discard() { return g_engine ? Refused(g_fn.discard(g_engine)) : BW_LINK_GR_BAD; }
int Clear(uint32_t gesture) { return g_engine ? Refused(g_fn.clear(g_engine, gesture)) : BW_LINK_GR_BAD; }
int Bind(uint32_t gesture, int32_t skill) { return g_engine ? Refused(g_fn.bind(g_engine, gesture, skill)) : BW_LINK_GR_BAD; }
int SetColor(uint32_t gesture, uint32_t rgba) { return g_engine ? Refused(g_fn.color(g_engine, gesture, rgba)) : BW_LINK_GR_BAD; }
int SetThreshold(uint32_t gesture, float percent) { return g_engine ? Refused(g_fn.threshold(g_engine, gesture, percent)) : BW_LINK_GR_BAD; }
uint32_t List(BW_LinkGestures* out) { return g_engine && out ? g_fn.list(g_engine, out) : 0u; }
int PollCasts(BWGE_Cast* out, int max) { return g_engine && out && max > 0 ? g_fn.poll(g_engine, out, max) : 0; }
uint32_t Trail(BW_LinkVrRune* out, uint32_t max) { return g_engine && out && max ? g_fn.trail(g_engine, out, max) : 0u; }
bool HasPoseZones() { return g_engine && g_fn.getZone; }
int GetPoseZone(uint32_t gesture, BWGE_PoseZone* out) {
    if (!g_engine || !g_fn.getZone || !out) return BW_LINK_GR_BAD;
    out->size = sizeof *out;
    return g_fn.getZone(g_engine, gesture, out);
}
bool HasActions() { return g_engine && g_fn.bindAction; }
bool HasSetHand() { return g_engine && g_fn.setHand; }
bool HasModes() { return g_engine && g_fn.setMode; }
int SetMode(uint32_t gesture, uint8_t mode) { return g_engine && g_fn.setMode ? Refused(g_fn.setMode(g_engine, gesture, mode)) : BW_LINK_GR_BAD; }
int GetMode(uint32_t gesture, uint8_t* out) {
    if (out) *out = 0;
    return g_engine && g_fn.getMode && out ? g_fn.getMode(g_engine, gesture, out) : BW_LINK_GR_BAD;
}
int SetHand(uint32_t gesture, uint8_t hand) { return g_engine && g_fn.setHand ? Refused(g_fn.setHand(g_engine, gesture, hand)) : BW_LINK_GR_BAD; }
int BindAction(uint32_t gesture, const char* action) { return g_engine && g_fn.bindAction ? Refused(g_fn.bindAction(g_engine, gesture, action ? action : "")) : BW_LINK_GR_BAD; }
int GetAction(uint32_t gesture, char* out, uint32_t cap) {
    if (out && cap) out[0] = 0;
    return g_engine && g_fn.getAction && out && cap ? g_fn.getAction(g_engine, gesture, out, cap) : BW_LINK_GR_BAD;
}
int PollCasts2(BWGE_Cast2* out, int max) { return g_engine && g_fn.poll2 && out && max > 0 ? g_fn.poll2(g_engine, out, max) : 0; }
int GetTake(uint32_t gesture, BWGE_TakeSample* out, int max) { return g_engine && g_fn.getTake && out && max > 0 ? g_fn.getTake(g_engine, gesture, out, max) : 0; }
int SetPoseZone(uint32_t gesture, float radiusM) { return g_engine && g_fn.setZone ? Refused(g_fn.setZone(g_engine, gesture, radiusM)) : BW_LINK_GR_BAD; }

}  // namespace gelib
