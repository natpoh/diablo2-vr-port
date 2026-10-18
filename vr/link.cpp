// link.cpp - D2R VR's end of BodyWalk Game Link: see link.h.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "direct_feed.h"
#include "link.h"
#include "rune_geom.h"
#include "view_gate.h"
#include "walk_path.h"

namespace link {
namespace {

void (*g_log)(const char*) = nullptr;
wchar_t g_ini[MAX_PATH] = L"";

void Log(const char* s) { if (g_log) g_log(s); }
void LogF(const char* fmt, ...) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    Log(b);
}

// A block, made or opened on first use; tried again once a second while it cannot be had.
// Kept for the life of the game, as vrcam's other views.
template <class T> struct Block {
    const wchar_t* name;
    std::atomic<T*> p{nullptr};
    HANDLE map = nullptr;
    ULONGLONG lastTry = 0;
    SRWLOCK lock = SRWLOCK_INIT;
    T* Get() {
        if (T* q = p.load()) return q;
        if (!TryAcquireSRWLockExclusive(&lock)) return nullptr;
        T* q = p.load();
        if (!q && (!lastTry || GetTickCount64() - lastTry >= 1000)) {
            lastTry = GetTickCount64();
            q = (T*)bw::link::Open(name, &map);
            p.store(q);
        }
        ReleaseSRWLockExclusive(&lock);
        return q;
    }
};
Block<BW_LinkManifest> g_manifest{BW_LINK_MANIFEST_NAME};
Block<BW_LinkGameState> g_gameState{BW_LINK_GAMESTATE_NAME};
Block<BW_LinkTracking> g_tracking{BW_LINK_TRACKING_NAME};
Block<BW_LinkActions> g_actions{BW_LINK_ACTIONS_NAME};
Block<BW_LinkRequests> g_requests{BW_LINK_REQUESTS_NAME};
Block<BW_LinkHost> g_host{BW_LINK_HOST_NAME};
Block<BW_LinkXrInput> g_xrInput{BW_LINK_XRINPUT_NAME};
Block<BW_LinkVrZones> g_vrZones{BW_LINK_VRZONES_NAME};
Block<BW_LinkVrRunes> g_vrRunes{BW_LINK_VRRUNES_NAME};
Block<BW_LinkWalk> g_walk{BW_LINK_WALK_NAME};

std::atomic<uint32_t> g_manifestSeq{0};   // our Manifest's sequence: Actions of any other are not ours

void WriteManifest(const char* version) {
    BW_LinkManifest* blk = g_manifest.Get();
    if (!blk) { Log("vrcam: Game Link - the Manifest could not be made: BodyWalk will not know the game's actions"); return; }
    static BW_LinkManifest m;   // 28 KB: not on the stack
    memset(&m, 0, sizeof m);
    strncpy_s(m.gameId, D2RVR_GAME_ID, _TRUNCATE);
    strncpy_s(m.displayName, D2RVR_GAME_NAME, _TRUNCATE);
    strncpy_s(m.modName, D2RVR_MOD_NAME, _TRUNCATE);
    strncpy_s(m.modVersion, version, _TRUNCATE);
    uint32_t n = 0;
    for (const D2RVRActionName& a : kD2RVRActions) strncpy_s(m.actions[n++], a.name, _TRUNCATE);
    for (const D2RVRCommand& c : kD2RVRCommands) strncpy_s(m.actions[n++], c.action, _TRUNCATE);
    m.actionCount = n;
    uint32_t t = 0;
    D2RVRTabs([&](const char* name, const char* desc) {
        strncpy_s(m.tabs[t].name, name, _TRUNCATE);
        strncpy_s(m.tabs[t].description, desc, _TRUNCATE);
        ++t;
    });
    m.tabCount = t;
    bw::link::Write(blk, m, BW_LINK_MANIFEST_VERSION);
    g_manifestSeq.store(blk->header.sequence);
    LogF("vrcam: Game Link - the game's manifest out: %u actions, %u Mapping tabs", n, t);
}

#if D2RVR_FIRST_PERSON
// [screen] menu_distance_m (D2R VR Settings > UI: F4 body): how far FlatVR is to push its
// screen while a panel is open in first person, 0 = leave it. Re-read every 2 s - the settings
// program's slider counts at once.
constexpr float kMenuDistanceDefaultM = 1.6f;

float MenuDistanceM() {
    static ULONGLONG lastRead = 0;
    static float metres = kMenuDistanceDefaultM;
    if (lastRead && GetTickCount64() - lastRead < 2000) return metres;
    lastRead = GetTickCount64();
    wchar_t v[32];
    GetPrivateProfileStringW(L"screen", L"menu_distance_m", L"", v, 32, g_ini);
    const float m = v[0] ? wcstof(v, nullptr) : kMenuDistanceDefaultM;
    metres = std::isfinite(m) ? std::clamp(m, 0.0f, 20.0f) : kMenuDistanceDefaultM;
    return metres;
}
#endif

// Head(): a ring of converted copies, so a reader's pointer outlives a few more of BodyWalk's.
constexpr uint32_t kRing = 8;
D2RVR_Shared g_ring[kRing];
uint32_t g_ringNext = 0;
std::atomic<const D2RVR_Shared*> g_head{nullptr};
std::atomic<uint32_t> g_headSeq{0};
SRWLOCK g_headLock = SRWLOCK_INIT;

void ToShared(const BW_LinkTracking& t, D2RVR_Shared* o) {
    memset(o, 0, sizeof *o);
    o->version = D2RVR_SHARED_VERSION;
    o->counter = t.header.sequence >> 1;
    o->headValid = (t.flags & BW_LINK_TRACK_HEAD) && t.head.valid ? 1u : 0u;
    o->headYawDeg = t.head.yawDeg;
    o->headHeightM = t.head.pos[1];
    o->pitchValid = (t.flags & BW_LINK_TRACK_PITCH) ? 1u : 0u;
    o->headPitchDeg = o->pitchValid ? t.head.pitchDeg : 0.0f;
    o->rollValid = (t.flags & BW_LINK_TRACK_ROLL) ? 1u : 0u;
    o->headRollDeg = o->rollValid ? t.head.rollDeg : 0.0f;
    // The hands in the frame of the head's turn: only with the room yaw that turns them.
    const bool room = (t.flags & BW_LINK_TRACK_ROOM) != 0;
    uint32_t hands = 0;
    if (room && t.right.valid) { memcpy(o->rightHand, t.right.posAhead, sizeof o->rightHand); memcpy(o->rightRot, t.right.rotAhead, sizeof o->rightRot); hands |= 1u; }
    if (room && t.left.valid) { memcpy(o->leftHand, t.left.posAhead, sizeof o->leftHand); memcpy(o->leftRot, t.left.rotAhead, sizeof o->leftRot); hands |= 2u; }
    o->handsValid = hands;
    // Grips: vrcam's "left hand takes the staff".
    o->rightGrip = (hands & 1u) ? std::clamp(t.right.grip, 0.0f, 1.0f) : 0.0f;
    o->leftGrip = (hands & 2u) ? std::clamp(t.left.grip, 0.0f, 1.0f) : 0.0f;
    o->gripMagic = D2RVR_GRIP_MAGIC;
    // The head in the room (the table view).
    if (room) {
        memcpy(o->headRoom, t.head.pos, sizeof o->headRoom);
        o->headYawRoomDeg = t.head.yawRoomDeg;
        o->roomMagic = D2RVR_ROOM_MAGIC;
    }
    o->sampleStamp = t.stamp;
    o->sampleStampMagic = D2RVR_SAMPLE_STAMP_MAGIC;
    o->userHeightMm = t.userHeightMm;
    o->flatVrStamped = (t.flags & BW_LINK_TRACK_FLATVR_STAMP) ? 1u : 0u;
}

// Actions of our Manifest, consistent; false when there are none.
bool ReadActions(BW_LinkActions* a) {
    const BW_LinkActions* blk = g_actions.Get();
    if (!blk || !bw::link::Read(blk, a)) return false;
    const uint32_t ours = g_manifestSeq.load();
    return ours && a->manifestSequence == ours;
}

// Bit n held, or pressed lately: a press this reader saw reads as held for kTapMs, so one
// that came and went between two of its looks still presses (and lets go) in the game.
bool HeldOrPressed(const BW_LinkActions& a, Presses& p, uint32_t n, ULONGLONG now) {
    if (a.presses[n] != p.seen[n]) { p.seen[n] = a.presses[n]; p.until[n] = now + kTapMs; }
    return bw::link::Bit(a.held, n) || now < p.until[n];
}

void Sync(const BW_LinkActions& a, Presses& p) {
    if (p.manifest == a.manifestSequence) return;
    p.manifest = a.manifestSequence;   // a new set of counters: what is there now is no press
    memcpy(p.seen, a.presses, sizeof p.seen);
    memset(p.until, 0, sizeof p.until);
}

SRWLOCK g_stateLock = SRWLOCK_INIT;
bw::link::Pulse g_hostPulse;
SRWLOCK g_hostLock = SRWLOCK_INIT;

}  // namespace

void Init(void (*log)(const char*), const wchar_t* iniPath, const char* modVersion) {
    g_log = log;
    wcscpy_s(g_ini, iniPath);
    WriteManifest(modVersion);
    // the rest made now too, so whichever side comes first finds them
    g_gameState.Get(); g_tracking.Get(); g_actions.Get(); g_requests.Get(); g_host.Get();
}

const D2RVR_Shared* Head() {
    // [openxr] direct_input with a native session: the game's own headset, not BodyWalk's. Fed by FlatVR's raw controllers instead
    // ([direct] flatvr_controls): the controls are the layout's, the head and hands stay BodyWalk's - its recentred yaw and FlatVR's stamps
    // are what the FlatVR camera and the arms are made from.
    if (directinput::Active() && !directinput::FedByLink()) return directinput::HeadShared();
    const BW_LinkTracking* t = g_tracking.Get();
    if (!t) return g_head.load();
    const uint32_t seq = ((const volatile BW_LinkHeader*)&t->header)->sequence;
    if (seq == g_headSeq.load() || (seq & 1u) || t->header.magic != BW_LINK_MAGIC) return g_head.load();
    if (!TryAcquireSRWLockExclusive(&g_headLock)) return g_head.load();   // another thread is at it
    if (seq != g_headSeq.load()) {
        BW_LinkTracking c;
        if (bw::link::Read(t, &c)) {
            D2RVR_Shared* o = &g_ring[g_ringNext++ % kRing];
            ToShared(c, o);
            if (!g_head.load()) Log("vrcam: Game Link - head tracking from BodyWalk");
            g_head.store(o);
            g_headSeq.store(c.header.sequence);
        }
    }
    ReleaseSRWLockExclusive(&g_headLock);
    return g_head.load();
}

bool RawTracking(BW_LinkTracking* out) {
    const BW_LinkTracking* t = g_tracking.Get();
    if (!t || t->header.magic != BW_LINK_MAGIC) return false;
    return bw::link::Read(t, out);
}

// BodyWalk's, or - [openxr] direct_input with a native session - the plugin's own layout; the gestures' (directinput::Action
// and Command) go over either of them.
uint32_t ActionsHeld(Presses& p) {
    uint32_t held = directinput::InjectedActions();
    if (directinput::Active()) return held | directinput::LayoutActions();
    if (directinput::FlatControllerLive()) return held | directinput::FlatActions();   // VR F1-F3, no BodyWalk running: the plugin's own controller (BodyWalk's Mapping would be silent)
    BW_LinkActions a;
    if (!ReadActions(&a)) return held;
    Sync(a, p);
    const ULONGLONG now = GetTickCount64();
    for (uint32_t i = 0; i < kD2RVRActionCount && i < a.count; ++i)
        if (HeldOrPressed(a, p, i, now)) held |= kD2RVRActions[i].bit;
    return held;
}

void CommandsHeld(uint64_t out[2], Presses& p) {
    directinput::InjectedCommands(out);
    if (directinput::Active()) {
        uint64_t mine[2];
        directinput::LayoutCommands(mine);
        out[0] |= mine[0]; out[1] |= mine[1];
        return;
    }
    if (directinput::FlatControllerLive()) {   // VR F1-F3, no BodyWalk running: the plugin's own controller
        uint64_t mine[2];
        directinput::FlatCommands(mine);
        out[0] |= mine[0]; out[1] |= mine[1];
        return;
    }
    BW_LinkActions a;
    if (!ReadActions(&a)) return;
    Sync(a, p);
    const ULONGLONG now = GetTickCount64();
    for (uint32_t j = 0; j < kD2RVRCommandCount; ++j) {
        const uint32_t n = kD2RVRActionCount + j, cmd = kD2RVRCommands[j].cmd;
        if (n < a.count && cmd < D2RVR_GAME_COMMANDS && HeldOrPressed(a, p, n, now)) out[cmd / 64] |= 1ull << (cmd % 64);
    }
}

std::atomic<bool> g_menuPanel{false};
void SetMenuPanel(bool on) { g_menuPanel.store(on); }

void PublishState(const D2RVR_State& s) {
    BW_LinkGameState* blk = g_gameState.Get();
    if (!blk) return;
    BW_LinkGameState g{};
    strncpy_s(g.gameId, D2RVR_GAME_ID, _TRUNCATE);
    // FlatVR's Head Lock: on in first person, off while a panel is open (so it can be looked
    // around) and in the other views - from above and third person the screen stays put. FlatVR
    // follows it only if the user ticked "Head Lock follows the game". Views 3 and 4, our camera
    // from above (F1 in perspective, the game on the floor), follow the head as first person does.
    // The game's main menu (character select: no hero, so no weapon read) is a menu as a panel is -
    // the screen stays put and moves back, not on the eyes (2026-10-06).
    const bool menu = s.menuOpen || s.weaponClass == D2RVR_WEAPON_UNKNOWN;
    // [openxr] menu_3d (SetMenuPanel): a menu in a game goes to FlatVR as a panel of its own - the
    // screen and Head Lock as with none, the world in stereo round it (the Mapping's "D2R Menu" stays)
    const bool flatMenu = (s.menuOpen && !g_menuPanel.load()) || s.weaponClass == D2RVR_WEAPON_UNKNOWN;
    g.headLock = !flatMenu && (s.viewMode == 1 || s.viewMode == 3 || s.viewMode == 4) ? 1 : 0;
#if D2RVR_FIRST_PERSON
    // FlatVR's screen pushed back while a panel is open in first person (view 1, VR F4), so the
    // whole inventory or trade window is in view: from above, behind and on the floor the panel
    // is already far enough away. FlatVR eases there and back, and follows it only while "Screen
    // distance follows the game" is ticked.
    g.screenDistanceM = flatMenu && s.viewMode == 1 ? MenuDistanceM() : 0.0f;
#endif
    // The Mapping tabs, each on top of "main".
    if (menu) strcpy_s(g.categories, "main, D2R Menu");
    else if (s.viewMode == 2) strcpy_s(g.categories, "main, D2R Third Person");
    // From above: the game's own camera, vrcam's off. Its own tab, as the view from behind has:
    // the same buttons want different things from up there.
    else if (s.viewMode == 0 || s.viewMode == 3) strcpy_s(g.categories, "main, D2R Top Down");
    else if (s.viewMode == 4) strcpy_s(g.categories, "main, D2R Floor");
#if D2RVR_FIRST_PERSON
    else if (s.weaponType > D2RVR_TYPE_UNKNOWN && s.weaponType < D2RVR_TYPE_COUNT)
        snprintf(g.categories, sizeof g.categories, "main, D2R First Person, D2R %s%s", kD2RVRWeaponTypeNames[s.weaponType],
                 (s.twoHanded & D2RVR_LEFT_WEAPON) ? ", D2R Left Hand" : "");
    else strcpy_s(g.categories, "main, D2R First Person");
#else
    else strcpy_s(g.categories, "main");
#endif
    AcquireSRWLockExclusive(&g_stateLock);   // the UI thread and vrcam's own both publish
    bw::link::Write(blk, g, BW_LINK_GAMESTATE_VERSION);
    ReleaseSRWLockExclusive(&g_stateLock);
}

void RequestFlatVR(bool on) {
    if (BW_LinkRequests* r = g_requests.Get()) bw::link::Request(r, &r->flatvrRunning, on ? 1 : 0);
}
void RequestStereoSource(int source) {
    if (BW_LinkRequests* r = g_requests.Get()) bw::link::Request(r, &r->stereoSource, source);
}
void RequestPresenter(bool game) {
    if (BW_LinkRequests* r = g_requests.Get()) bw::link::Request(r, &r->presenter, game ? BW_LINK_PRESENTER_GAME : BW_LINK_PRESENTER_FLATVR);
}

bool HostAlive(uint32_t* caps) {
    if (caps) *caps = 0;
    const BW_LinkHost* h = g_host.Get();
    if (!h) return false;
    AcquireSRWLockExclusive(&g_hostLock);
    const bool alive = g_hostPulse.Alive(&h->header, 2000);
    ReleaseSRWLockExclusive(&g_hostLock);
    if (alive && caps) {
        BW_LinkHost c;
        if (bw::link::Read(h, &c)) *caps = c.caps;
    }
    return alive;
}

void PublishXr(const D2RVR_XrInput& in, bool ended) {
    // The plugin is the input: BodyWalk gets no controllers (and makes no second pad) - unless [openxr] direct_bodywalk_walk lets its walking
    // through: it walks by the body's motion, which it reads from the head and hand poses, so it is given those, and no control (it presses
    // nothing a second time: its pad's buttons, actions and commands are ignored anyway, and the controller's own thumbstick is the plugin's).
    const bool direct = directinput::Active() && !ended;
    if (direct && !directinput::BodyWalkWalks()) return;
    BW_LinkXrInput* blk = g_xrInput.Get();
    if (!blk) return;
    BW_LinkXrInput x{};
    x.stamp = in.stamp;
    x.state = (in.focused ? BW_LINK_XR_FOCUSED : 0u) | (ended ? BW_LINK_XR_ENDED : 0u);
    x.displayTime = in.displayTime;
    x.head = in.head;
    x.hand[0] = in.hand[0];
    x.hand[1] = in.hand[1];
    if (direct) { walkpath::ZeroControls(x.hand[0]); walkpath::ZeroControls(x.hand[1]); }
    memcpy(x.profile, in.profile, sizeof x.profile);
    bw::link::Write(blk, x, BW_LINK_XRINPUT_VERSION);
    if (direct) {
        static bool told = false;
        if (!told) { told = true; Log("vrcam: Game Link - direct input: BodyWalk is given the head and hands, controls zeroed, so its body walking can run ([openxr] direct_bodywalk_walk=1)"); }
    }
}

bool Walk(float* x, float* y) {
    *x = *y = 0.0f;
    const BW_LinkWalk* blk = g_walk.Get();
    if (!blk || blk->header.magic != BW_LINK_MAGIC) return false;
    BW_LinkWalk w;   // a copy each: read from every pad poll, any thread
    if (!bw::link::Read(blk, &w)) return false;
    if (w.header.version != BW_LINK_WALK_VERSION || !(w.flags & BW_LINK_WALK_ACTIVE)) return false;
    if (D2RVRStampNow() - w.stamp > 5000u) return false;   // 0.1 ms units: half a second
    if (!std::isfinite(w.stickX) || !std::isfinite(w.stickY)) return false;
    *x = std::clamp(w.stickX, -1.0f, 1.0f);
    *y = std::clamp(w.stickY, -1.0f, 1.0f);
    return true;
}

uint32_t VrZones(BW_LinkVrZone* out, uint32_t max) {
    if (directinput::Active()) return 0;   // BodyWalk's zones are not in the picture: it has no hands to place them by
    const BW_LinkVrZones* blk = g_vrZones.Get();
    if (!blk || blk->header.magic != BW_LINK_MAGIC) return 0;
    static BW_LinkVrZones z;   // one reader: xr.cpp's draw, under its lock
    if (!bw::link::Read(blk, &z)) return 0;
    if (z.header.version != BW_LINK_VRZONES_VERSION || D2RVRStampNow() - z.stamp > 5000u) return 0;   // 0.1 ms units: half a second
    const uint32_t n = std::min(std::min<uint32_t>(z.count, BW_LINK_VRZONES_MAX), max);
    memcpy(out, z.zone, n * sizeof *out);
    return n;
}

uint32_t VrRunes(BW_LinkVrRune* out, uint32_t max) {
    // F4 only (vr/view_gate.h); and with the plugin as the input BodyWalk has no hands, so no gestures: none, as the zones
    if (!viewgate::F4() || directinput::Active()) return 0;
    const BW_LinkVrRunes* blk = g_vrRunes.Get();
    if (!blk || blk->header.magic != BW_LINK_MAGIC) return 0;
    // One reader: xr.cpp's draw, under its lock. A read that cannot get a whole copy (the writer was mid-write all through its tries -
    // a writer that is preempted there holds the seqlock for a time slice) leaves the last whole copy to be used: it is as fresh as its
    // own stamp says, and a writer that died mid-write makes it stale in half a second like any other.
    static BW_LinkVrRunes r, got;
    static bool have = false;
    if (bw::link::Read(blk, &got)) { r = got; have = true; }
    if (!have || r.header.version != BW_LINK_VRRUNES_VERSION) return 0;
    // 0.1 ms units: half a second. While no rune is on show the stamp stands still (the engine writes once more with count 0 and then
    // nothing): that is the normal end, not a fault. A block that still said "runes" and went quiet is the engine gone mid-gesture.
    const bool fresh = D2RVRStampNow() - r.stamp <= 5000u;
    static runes::Watch watch;
    switch (watch.Step(fresh, r.count)) {
        case runes::Event::Appeared: LogF("vrcam: Game Link - BodyWalk's gesture runes come (%u on show)", r.count); break;
        case runes::Event::WentStale: Log("vrcam: Game Link - BodyWalk's gesture runes went stale (no write for half a second with runes still on show): drawn no more"); break;
        default: break;
    }
    const uint32_t n = fresh ? std::min(std::min<uint32_t>(r.count, BW_LINK_VRRUNES_MAX), max) : 0;
    if (!n) return 0;
    memcpy(out, r.rune, n * sizeof *out);
    return n;
}

}  // namespace link
