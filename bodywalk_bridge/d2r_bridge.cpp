// D2R Bridge - BodyWalkVR output plugin.
//
// Publishes the head's yaw for the vrcam plugin inside Diablo II: Resurrected.
// It is not an output mode: walking, sticks and gestures keep going out as
// BodyWalk's own virtual Xbox pad, so nothing about the user's mapping changes.
//
// Needs "Universal tracking output" switched on in BodyWalk: that is what
// makes the host call BW_Plugin_ReceiveTracking at all.

#define WIN32_LEAN_AND_MEAN
#include "d2r_vr_build.h"
#include <windows.h>
#include <sddl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "plugin_api.h"
#include "d2r_vr_shared.h"
#include "afr_eye_shared.h"
#include "d2r_vr_state.h"

namespace {

BW_HostCallbacks g_host{};
// How far down BW_HostCallbacks the host fills in (BW_Plugin_SetHostApiVersion);
// never called = version 1. Fields past it must not even be copied.
uint32_t g_hostVer = 1;
int g_headLockSent = -1;   // the Head Lock wish last sent to FlatVR, -1 none
float g_distanceSent = 0.0f;   // the screen distance last asked of FlatVR, 0 none
HANDLE g_map = nullptr;
D2RVR_Shared* g_shared = nullptr;
std::atomic<uint32_t> g_frames{0};
ULONGLONG g_lastFrameTick = 0;

void Info(const char* msg) { if (g_host.log_info) g_host.log_info(msg); }

// The game's state (shared/d2r_vr_state.h, written by vrcam inside D2R) picks
// BodyWalk's mapping category: one tab per weapon kind ("D2R Bow", "D2R Sword"
// ...), whichever weapon set it is in, or the menu's, each on top of "main".
// Like the Cyberpunk bridge's weapon/vehicle tabs. (Until 0.4: a tab per set.)
const char* const kCatMenu = "D2R Menu";
const char* const kCatThird = "D2R Third Person";
const char* const kCatTop = "D2R Top Down";
const char* const kCatFloor = "D2R Floor";   // VR F3, the game on the floor: its own buttons, nothing of first person
// First person with no panel open, whatever the weapon - on top of the weapon's own tab.
const char* const kCatFirst = "D2R First Person";
HANDLE g_stateMap = nullptr;
const D2RVR_State* g_state = nullptr;
uint32_t g_stateCounter = 0;
ULONGLONG g_stateSeen = 0;
std::string g_activeCats;

// The tabs are made when BodyWalk starts: at Initialize, and once more a few
// seconds in, in case the profile's gestures load after the plugins (that load
// resets the list to "main"). After that the user owns them - "Clear" in the
// mapping removes them until the next start, as it does any other tab.
void RegisterCategories() {
    if (!g_host.register_mapping_category) return;
#if D2RVR_FIRST_PERSON
    for (uint32_t t = D2RVR_TYPE_UNARMED; t < D2RVR_TYPE_COUNT; ++t) {
        char name[48], desc[120];
        snprintf(name, sizeof name, "D2R %s", kD2RVRWeaponTypeNames[t]);
        snprintf(desc, sizeof desc, "D2R: %s in the hands (F4)", kD2RVRWeaponTypeNames[t]);
        g_host.register_mapping_category(name, desc);
    }
#endif
    g_host.register_mapping_category(kCatMenu, "D2R: a trade, inventory or other panel is open");
    g_host.register_mapping_category(kCatThird, "D2R: the third-person view (F2)");
    g_host.register_mapping_category(kCatTop, "D2R: the view from above (F1)");
    g_host.register_mapping_category(kCatFloor, "D2R: the game on the floor (F3)");
#if D2RVR_FIRST_PERSON
    g_host.register_mapping_category(kCatFirst, "D2R: first person (F4), no panel open, any weapon");
#endif
}

void SetCategories(const char* cats) {
    if (g_activeCats == cats || !g_host.set_active_mapping_category) return;
    g_activeCats = cats;
    g_host.set_active_mapping_category(cats);
    char b[128]; snprintf(b, sizeof b, "D2R Bridge: mapping categories now '%s'", cats); Info(b);
}

#if D2RVR_FIRST_PERSON
// [screen] menu_distance_m in the game's d2r_vr.ini (D2R VR Settings > UI: F4
// body): how far FlatVR is to push its screen while a panel is open in first
// person, 0 = leave it. This plugin lives in BodyWalk, not beside the ini, so
// the ini is found through the game's process: <game folder>\d2rloader\plugins
// (the game runs as D2RLoader.exe, or D2R.exe, in its own folder). Re-read every
// 2 s while the game runs - the settings program's slider counts at once.
constexpr float kMenuDistanceDefaultM = 1.6f;

bool FindGameIni(wchar_t* out, size_t cap) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof pe};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"D2RLoader.exe") != 0 && _wcsicmp(pe.szExeFile, L"D2R.exe") != 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t path[MAX_PATH];
        DWORD n = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &n)) {
            while (n && path[n - 1] != L'\\') --n;
            path[n] = 0;
            for (const wchar_t* rel : {L"d2rloader\\plugins\\d2r_vr.ini", L"d2r_vr.ini"}) {
                if (wcslen(path) + wcslen(rel) + 1 > cap) continue;
                wcscpy_s(out, cap, path);
                wcscat_s(out, cap, rel);
                if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) { found = true; break; }
            }
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    return found;
}

float MenuDistanceM() {
    static wchar_t ini[MAX_PATH] = L"";
    static ULONGLONG lastRead = 0;
    static float metres = kMenuDistanceDefaultM;
    if (lastRead && GetTickCount64() - lastRead < 2000) return metres;
    lastRead = GetTickCount64();
    if (!ini[0] || GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES) {
        ini[0] = 0;
        if (!FindGameIni(ini, MAX_PATH)) { ini[0] = 0; return metres; }
        char b[MAX_PATH + 64];
        snprintf(b, sizeof b, "D2R Bridge: screen distance for panels from %ls", ini);
        Info(b);
    }
    wchar_t v[32];
    GetPrivateProfileStringW(L"screen", L"menu_distance_m", L"", v, 32, ini);
    const float m = v[0] ? wcstof(v, nullptr) : kMenuDistanceDefaultM;
    metres = std::isfinite(m) ? std::clamp(m, 0.0f, 20.0f) : kMenuDistanceDefaultM;
    return metres;
}
#endif

// FlatVR's screen distance (host API 6): pushed back while a panel is open in
// first person, so the whole inventory or trade window is in view; 0 (the
// user's own distance) otherwise. Sent when it changes; FlatVR eases there and
// back, and follows it only while "Screen distance follows the game" is ticked.
void SendScreenDistance(float metres) {
    if (metres == g_distanceSent || g_hostVer < 6 || !g_host.request_flatvr_screen_distance) return;
    g_distanceSent = metres;
    g_host.request_flatvr_screen_distance(metres);
    char b[96];
    if (metres > 0.0f) snprintf(b, sizeof b, "D2R Bridge: panel open - FlatVR screen to %.2f m", metres);
    else snprintf(b, sizeof b, "D2R Bridge: FlatVR screen back to its own distance");
    Info(b);
}

void FollowGameState() {
    static const ULONGLONG started = GetTickCount64();
    static bool again = false;
    if (!again && GetTickCount64() - started > 3000) {
        again = true;
        RegisterCategories();
        if (!g_activeCats.empty() && g_host.set_active_mapping_category) g_host.set_active_mapping_category(g_activeCats.c_str());
    }
    if (!g_state) {
        if (!g_stateMap) g_stateMap = OpenFileMappingW(FILE_MAP_READ, FALSE, D2RVR_STATE_NAME);
        if (g_stateMap) g_state = (const D2RVR_State*)MapViewOfFile(g_stateMap, FILE_MAP_READ, 0, 0, sizeof(D2RVR_State));
        if (!g_state) return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_state->counter != g_stateCounter) { g_stateCounter = g_state->counter; g_stateSeen = now; }
    // A game that stopped writing for 3 s is gone (or between games): main only.
    if (g_state->version != D2RVR_STATE_VERSION || now - g_stateSeen > 3000) { SetCategories("main"); g_headLockSent = -1; SendScreenDistance(0.0f); return; }
    // FlatVR's Head Lock (host API 5): on in first person, off while a panel is
    // open (so it can be looked around) and in the other views - from above and
    // third person the screen stays put. Sent when it changes; FlatVR follows it
    // only if the user ticked "Head Lock follows the game".
    // Views 3 and 4, our camera from above (F1 in perspective, the game on the floor), follow the head as first person does.
    // The game's main menu (character select: no hero, so no weapon read) is a menu as a
    // panel is - the screen stays put and moves back, not on the eyes (2026-10-06).
    const bool menu = g_state->menuOpen || g_state->weaponClass == D2RVR_WEAPON_UNKNOWN;
    const int lock = !menu && (g_state->viewMode == 1 || g_state->viewMode == 3 || g_state->viewMode == 4) ? 1 : 0;
    if (lock != g_headLockSent && g_hostVer >= 5 && g_host.request_flatvr_head_lock) {
        g_headLockSent = lock;
        g_host.request_flatvr_head_lock(lock);
    }
#if D2RVR_FIRST_PERSON
    // Only in first person (view 1, VR F4): from above, behind and on the floor
    // the panel is already far enough away.
    SendScreenDistance(menu && g_state->viewMode == 1 ? MenuDistanceM() : 0.0f);
#endif
    if (menu) SetCategories("main, D2R Menu");
    else if (g_state->viewMode == 2) SetCategories("main, D2R Third Person");
    // From above: the game's own camera, vrcam's off. Its own tab, as the view
    // from behind has: the same buttons want different things from up there.
    else if (g_state->viewMode == 0 || g_state->viewMode == 3) SetCategories("main, D2R Top Down");
    else if (g_state->viewMode == 4) SetCategories("main, D2R Floor");
#if D2RVR_FIRST_PERSON
    else if (g_state->weaponType > D2RVR_TYPE_UNKNOWN && g_state->weaponType < D2RVR_TYPE_COUNT) {
        char cats[96];
        snprintf(cats, sizeof cats, "main, D2R First Person, D2R %s", kD2RVRWeaponTypeNames[g_state->weaponType]);
        SetCategories(cats);
    } else SetCategories("main, D2R First Person");
#else
    else SetCategories("main");
#endif
}

bool OpenShared() {
    if (g_shared) return true;
    // Low integrity may write, so the game reads it whatever level it runs at.
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, FALSE};
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)", SDDL_REVISION_1, &sd, nullptr))
        sa.lpSecurityDescriptor = sd;
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(D2RVR_Shared), D2RVR_SHARED_NAME);
    if (sd) LocalFree(sd);
    if (!g_map) return false;
    g_shared = (D2RVR_Shared*)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(D2RVR_Shared));
    if (!g_shared) { CloseHandle(g_map); g_map = nullptr; return false; }
    memset(g_shared, 0, sizeof *g_shared);
    g_shared->version = D2RVR_SHARED_VERSION;
    return true;
}

// BodyWalk actions for the game (d2r_vr_shared.h, D2RVR_Actions): mapped
// like any button in BodyWalk's Mapping tab, acted on by vrcam. Pad A does
// attack, pick-up and interact in one; these two keep them apart.
struct ActionName { const char* name; uint32_t bit; };
const ActionName kActions[] = {
    {"D2R: Attack (never picks up)", D2RVR_ACT_ATTACK},
    {"D2R: Pick up / interact", D2RVR_ACT_INTERACT},
    {"D2R: Inventory", D2RVR_ACT_INVENTORY},
    {"D2R: Map (show / hide)", D2RVR_ACT_MAP},
    {"D2R: Swap weapons", D2RVR_ACT_SWAP},
    {"D2R: View - from above", D2RVR_ACT_VIEW_1},
    {"D2R: View - third person", D2RVR_ACT_VIEW_2},
#if D2RVR_FIRST_PERSON
    {"D2R: View - first person (body)", D2RVR_ACT_VIEW_4},
#endif
    {"D2R: View - table", D2RVR_ACT_VIEW_5},
    {"D2R: View - next", D2RVR_ACT_VIEW_NEXT},
    {"D2R: Recenter / put the game in front of me (F11)", D2RVR_ACT_RECENTER},
};
D2RVR_Actions* g_actions = nullptr;

D2RVR_Actions* Actions() {
    if (g_actions) return g_actions;
    static HANDLE map = nullptr;
    if (!map) map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(D2RVR_Actions), D2RVR_ACTIONS_NAME);
    if (map) g_actions = (D2RVR_Actions*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(D2RVR_Actions));
    if (g_actions) { g_actions->held = 0; g_actions->version = D2RVR_ACTIONS_VERSION; }
    return g_actions;
}

}  // namespace

extern "C" {

BW_EXPORT void BW_CALLBACK BW_Plugin_SetHostApiVersion(uint32_t version) { g_hostVer = version; }

BW_EXPORT bool BW_CALLBACK BW_Plugin_Initialize(const BW_HostCallbacks* callbacks, BW_PluginInfo* out_info) {
    // Only the part this host has: a whole-struct copy from an older BodyWalk
    // reads past the end of its smaller struct.
    if (callbacks) {
        const size_t have = g_hostVer >= 7 ? sizeof(BW_HostCallbacks)
                          : g_hostVer >= 6 ? offsetof(BW_HostCallbacks, request_flatvr_running)
                          : g_hostVer >= 5 ? offsetof(BW_HostCallbacks, request_flatvr_screen_distance)
                          : g_hostVer >= 3 ? offsetof(BW_HostCallbacks, request_flatvr_head_lock)
                          : g_hostVer >= 2 ? offsetof(BW_HostCallbacks, register_input_device)
                                           : offsetof(BW_HostCallbacks, send_acceleration);
        memcpy(&g_host, callbacks, have);
    }
    out_info->name = "D2R Bridge";
    out_info->version = "0.22.0";
    out_info->author = "BodyWalkVR";
    out_info->type = BW_PLUGIN_TYPE_OUTPUT;
    out_info->output_mode_name = nullptr;   // not a mode: the Xbox pad stays the output
    out_info->input_source_name = nullptr;
    RegisterCategories();
    if (g_host.register_action) for (const ActionName& a : kActions) g_host.register_action(a.name);
    Actions();
    if (!OpenShared()) Info("D2R Bridge: could not create the shared memory");
    else Info("D2R Bridge: ready, head yaw goes to Diablo II: Resurrected");
    return true;
}

// D2R VR Settings' Start FlatVR / Stop FlatVR (D2RVR_FLATVR_START_NAME): passed
// on to BodyWalk, host API 7. The events are made here, so they exist exactly
// while a BodyWalk with this bridge runs.
HANDLE g_flatVrStart = nullptr, g_flatVrStop = nullptr;

void FollowFlatVrButtons() {
    if (!g_flatVrStart) g_flatVrStart = CreateEventW(nullptr, FALSE, FALSE, D2RVR_FLATVR_START_NAME);
    if (!g_flatVrStop) g_flatVrStop = CreateEventW(nullptr, FALSE, FALSE, D2RVR_FLATVR_STOP_NAME);
    const bool can = g_hostVer >= 7 && g_host.request_flatvr_running;
    if (g_flatVrStart && WaitForSingleObject(g_flatVrStart, 0) == WAIT_OBJECT_0) {
        if (can) g_host.request_flatvr_running(1);
        Info(can ? "D2R Bridge: Start FlatVR from D2R VR Settings" : "D2R Bridge: Start FlatVR asked, but this BodyWalk is older than 1.74");
    }
    if (g_flatVrStop && WaitForSingleObject(g_flatVrStop, 0) == WAIT_OBJECT_0) {
        if (can) g_host.request_flatvr_running(0);
        Info(can ? "D2R Bridge: Stop FlatVR from D2R VR Settings" : "D2R Bridge: Stop FlatVR asked, but this BodyWalk is older than 1.74");
    }
}

BW_EXPORT void BW_CALLBACK BW_Plugin_Update() {
    FollowGameState();
    FollowFlatVrButtons();
}

BW_EXPORT void BW_CALLBACK BW_Plugin_ReceiveAction(const char* action_name, bool active) {
    if (!action_name) return;
    for (const ActionName& a : kActions) {
        if (strcmp(a.name, action_name) != 0) continue;
        if (D2RVR_Actions* b = Actions()) {
            const uint32_t before = b->held;
            b->held = active ? (before | a.bit) : (before & ~a.bit);
            if (b->held != before) b->counter++;
        }
        return;
    }
}

BW_EXPORT void BW_CALLBACK BW_Plugin_Shutdown() {
    SendScreenDistance(0.0f);   // a screen left pushed back would stay there until FlatVR restarts
    if (g_shared) { g_shared->headValid = 0; UnmapViewOfFile(g_shared); g_shared = nullptr; }
    if (g_map) { CloseHandle(g_map); g_map = nullptr; }
    if (g_state) { UnmapViewOfFile(g_state); g_state = nullptr; }
    if (g_stateMap) { CloseHandle(g_stateMap); g_stateMap = nullptr; }
    if (g_flatVrStart) { CloseHandle(g_flatVrStart); g_flatVrStart = nullptr; }
    if (g_flatVrStop) { CloseHandle(g_flatVrStop); g_flatVrStop = nullptr; }
}

// The user's height as BodyWalk keeps it (Avatar > Body Height): config.json,
// {"userHeight": metres}, written by VRService::saveConfig - in its settings
// folder %LOCALAPPDATA%\BodyWalkVR since 1.74, beside BodyWalk before. Read
// every 2 s; millimetres, 0 if not found.
uint32_t UserHeightMm() {
    static ULONGLONG lastRead = 0;
    static uint32_t mm = 0;
    if (lastRead && GetTickCount64() - lastRead < 2000) return mm;
    lastRead = GetTickCount64();
    wchar_t exeDir[MAX_PATH] = L"";
    DWORD n = GetModuleFileNameW(nullptr, exeDir, MAX_PATH);
    while (n && exeDir[n - 1] != L'\\') --n;
    exeDir[n] = 0;
    wchar_t local[MAX_PATH] = L"";
    const DWORD ln = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    const std::wstring settings = ln && ln < MAX_PATH - 40 ? std::wstring(local) + L"\\BodyWalkVR\\config.json" : std::wstring();
    for (const std::wstring& path : {settings, std::wstring(L"config.json"), std::wstring(exeDir) + L"config.json"}) {
        if (path.empty()) continue;
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"rb") || !f) continue;
        char buf[2048] = {};
        const size_t got = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[got] = 0;
        const char* k = strstr(buf, "\"userHeight\"");
        if (!k || !(k = strchr(k, ':'))) continue;
        const double v = atof(k + 1);
        if (v > 1.0 && v < 2.5) { mm = (uint32_t)(v * 1000.0 + 0.5); return mm; }
    }
    return mm;
}

BW_EXPORT void BW_CALLBACK BW_Plugin_ReceiveTracking(const BW_TrackingData* data) {
    if (!data || !OpenShared()) return;
    // UniversalOutputService sends the head as a pure yaw quaternion,
    // y = sin(-yaw/2), w = cos(-yaw/2), yaw positive to the left.
    const BW_Pose& h = data->head;
    float yaw = -2.0f * std::atan2(h.rot[1], h.rot[3]) * 57.2957795f;
    yaw -= 360.0f * std::floor((yaw + 180.0f) / 360.0f);
    g_shared->headYawDeg = yaw;
    g_shared->headHeightM = h.pos[1];
    // headPitchDeg exists from BW_TrackingData version 2 on; an older host's
    // struct ends before it, so it must not be read there.
    const bool hasPitch = data->version >= 2;
    g_shared->headPitchDeg = hasPitch ? data->headPitchDeg : 0.0f;
    g_shared->pitchValid = hasPitch ? 1u : 0u;
    const bool hasRoll = data->version >= 3;
    g_shared->headRollDeg = hasRoll ? data->headRollDeg : 0.0f;
    g_shared->rollValid = hasRoll ? 1u : 0u;

    // Hands: room axes from the head, turned by the head's room yaw so they
    // read as "ahead of me / to my right". Only a v4 host sends that yaw.
    uint32_t hands = 0;
    if (data->version >= 4) {
        const float th = data->headYawRoomDeg * 0.0174532925f;
        const float c = std::cos(th), s = std::sin(th);
        const float qs = std::sin(-th * 0.5f), qc = std::cos(-th * 0.5f);   // undo the yaw: about +y by -th
        auto put = [&](const BW_Pose& p, float* pos, float* rot) {
            pos[0] = p.pos[0] * c - p.pos[2] * s;
            pos[1] = p.pos[1];
            pos[2] = p.pos[0] * s + p.pos[2] * c;
            // q = qyaw^-1 * p.rot, with qyaw^-1 = (0, qs, 0, qc)
            const float x = p.rot[0], y = p.rot[1], z = p.rot[2], w = p.rot[3];
            rot[0] = qc * x + qs * z;
            rot[1] = qc * y + qs * w;
            rot[2] = qc * z - qs * x;
            rot[3] = qc * w - qs * y;
        };
        if (data->rightHand.valid) { put(data->rightHand, g_shared->rightHand, g_shared->rightRot); hands |= 1u; }
        if (data->leftHand.valid) { put(data->leftHand, g_shared->leftHand, g_shared->leftRot); hands |= 2u; }
    }
    g_shared->handsValid = hands;
    // Grips: vrcam's "left hand takes the staff" (BW_Pose has had grip from v1).
    g_shared->rightGrip = (hands & 1u) ? std::clamp(data->rightHand.grip, 0.0f, 1.0f) : 0.0f;
    g_shared->leftGrip = (hands & 2u) ? std::clamp(data->leftHand.grip, 0.0f, 1.0f) : 0.0f;
    g_shared->gripMagic = D2RVR_GRIP_MAGIC;
    // The head in the room (vrcam's table view): position as BodyWalk has it,
    // and its room yaw (a v4 host's) - v4 is what sends that yaw.
    if (data->version >= 4) {
        for (int i = 0; i < 3; ++i) g_shared->headRoom[i] = h.pos[i];
        g_shared->headYawRoomDeg = data->headYawRoomDeg;
        g_shared->roomMagic = D2RVR_ROOM_MAGIC;
    }
    g_shared->headValid = h.valid ? 1u : 0u;
    // FlatVR's name for this head when FlatVR handed it over (its prediction
    // for the display, so "now" would be the wrong moment), else now.
    static const FlatVRHeadSample* named = nullptr;
    if (!named) {
        static ULONGLONG lastLook = 0;
        if (GetTickCount64() - lastLook > 1000) {
            lastLook = GetTickCount64();
            if (HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, FLATVR_HEAD_SAMPLE_NAME)) {
                named = (const FlatVRHeadSample*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(FlatVRHeadSample));
                CloseHandle(m);
            }
        }
    }
    g_shared->sampleStamp = named && named->version == FLATVR_HEAD_SAMPLE_VERSION &&
                                    (int32_t)(D2RVRStampNow() - named->stamp) >= 0 &&
                                    (int32_t)(D2RVRStampNow() - named->stamp) < 300
                                ? named->stamp : D2RVRStampNow();
    g_shared->sampleStampMagic = D2RVR_SAMPLE_STAMP_MAGIC;
    g_shared->userHeightMm = UserHeightMm();
    g_shared->counter++;
    g_frames++;
    g_lastFrameTick = GetTickCount64();
}

BW_EXPORT void BW_CALLBACK BW_Plugin_GetStatus(BW_PluginStatus* out_status) {
    if (!out_status) return;
    out_status->has_action = false;
    out_status->action_name[0] = '\0';
    const bool live = g_frames.load() && GetTickCount64() - g_lastFrameTick < 1000;
    if (!g_shared) {
        out_status->code = BW_STATUS_ERROR;
        strncpy_s(out_status->message, "Shared memory unavailable", _TRUNCATE);
    } else if (!live) {
        out_status->code = BW_STATUS_WARNING;
        strncpy_s(out_status->message, "No head tracking - switch on Universal tracking output", _TRUNCATE);
    } else {
        out_status->code = BW_STATUS_OK;
        if (g_shared->pitchValid)
            snprintf(out_status->message, sizeof out_status->message, "Head yaw %.0f deg, pitch %.0f deg", g_shared->headYawDeg, g_shared->headPitchDeg);
        else
            snprintf(out_status->message, sizeof out_status->message, "Head yaw %.0f deg (no pitch - update BodyWalk)", g_shared->headYawDeg);
    }
}

}  // extern "C"
