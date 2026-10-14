// direct_input.cpp - direct input for native OpenXR: see direct_input.h (the API) and direct_feed.h (the plumbing).
//
// One headset frame in, once, from xr.cpp's Publish: Feed() makes the Frame the gesture listeners see, then (with
// [openxr] direct_input=1) runs the controls through the release gate and the layout and publishes what BodyWalk
// used to supply - the pad, the D2R actions, the game's commands, the head and hands. The readers (the game's XInput
// poll, link.cpp for the actions and commands, gamecmd's timer) take copies under a lock; a result older than a
// second is no result, so a stopped game holds nothing down.
//
// Nothing here calls into the game. The commands go out through link::CommandsHeld to vr/gamecmd.cpp, which presses
// them through the game's own command executor on its UI thread.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

#include "direct_feed.h"
#include "direct_input.h"
#include "direct_layout.h"
#include "view_gate.h"

namespace directinput {
namespace {

using namespace layout;

static_assert(act::kAttack == (uint32_t)D2RVR_ACT_ATTACK && act::kInteract == (uint32_t)D2RVR_ACT_INTERACT &&
                  act::kInventory == (uint32_t)D2RVR_ACT_INVENTORY && act::kMap == (uint32_t)D2RVR_ACT_MAP &&
                  act::kSwap == (uint32_t)D2RVR_ACT_SWAP && act::kView1 == (uint32_t)D2RVR_ACT_VIEW_1 &&
                  act::kView2 == (uint32_t)D2RVR_ACT_VIEW_2 && act::kView3 == (uint32_t)D2RVR_ACT_VIEW_3 &&
                  act::kView4 == (uint32_t)D2RVR_ACT_VIEW_4 && act::kView5 == (uint32_t)D2RVR_ACT_VIEW_5 &&
                  act::kViewNext == (uint32_t)D2RVR_ACT_VIEW_NEXT && act::kRecenter == (uint32_t)D2RVR_ACT_RECENTER,
              "the layout's action bits are the mod's");
static_assert((uint32_t)kPrimary == (uint32_t)BW_LINK_XRB_PRIMARY && (uint32_t)kSecondary == (uint32_t)BW_LINK_XRB_SECONDARY &&
                  (uint32_t)kStick == (uint32_t)BW_LINK_XRB_STICK && (uint32_t)kMenuButton == (uint32_t)BW_LINK_XRB_MENU &&
                  (uint32_t)kTrigger == (uint32_t)BW_LINK_XRB_TRIGGER && (uint32_t)kSqueeze == (uint32_t)BW_LINK_XRB_SQUEEZE,
              "the frame's button bits are Game Link's");
static_assert(layout::kCommands == D2RVR_GAME_COMMANDS, "the game's command table");

constexpr ULONGLONG kStaleMs = 1000;      // frames older than this: direct mode holds nothing

// F4 only (vr/view_gate.h): in every other view nothing of this layer acts - the key is as good as 0 there.
bool ViewOk() { return viewgate::F4(); }
constexpr ULONGLONG kTapMs = 65;          // Tap(): held this long
constexpr int kMaxListeners = 16;
constexpr int kPulses = 8;
constexpr int kActionBits = 12;

void (*g_log)(const char*) = nullptr;
void Log(const char* s) { if (g_log) g_log(s); }
void LogF(const char* fmt, ...) {
    char b[320];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    Log(b);
}

// -- the settings ---------------------------------------------------------------------------------------------------
std::atomic<bool> g_on{false};
std::atomic<bool> g_bwWalk{true};   // [openxr] direct_bodywalk_walk: BodyWalk's walking passes in direct mode
SRWLOCK g_cfgLock = SRWLOCK_INIT;
Table g_table = DefaultTable();   // the world layer; the menu layer is the game's own
bool g_tableChanged = true;       // Feed hands it to the layout once

// -- the sources of the gestures --------------------------------------------------------------------------------------
std::atomic<uint16_t> g_cmdSrc[layout::kCommands];   // bit per Source: who holds the command
std::atomic<ULONGLONG> g_tapUntil[layout::kCommands];
std::atomic<uint16_t> g_actSrc[kActionBits];
std::atomic<uint32_t> g_take[2];                      // this frame's takes: Button bits | kTakeTrigger | kTakeSqueeze

struct Pulse { uint16_t buttons; uint8_t lt, rt; ULONGLONG until; };
SRWLOCK g_pulseLock = SRWLOCK_INIT;
Pulse g_pulses[kPulses];
std::atomic<ULONGLONG> g_pulseLatest{0};   // the latest expiry: the XInput detour's poll costs one load when no gesture pulses

std::atomic<HapticSink> g_hapticSink{nullptr};
std::atomic<ULONGLONG> g_uiUpUntil{0};   // SetUiUp: a panel of ours is up until then
std::atomic<ULONGLONG> g_panelAt{0};     // a long press asked for the skill panel at this tick (TakeSkillPanelToggle)

// -- the listeners ------------------------------------------------------------------------------------------------------
struct Listener { FrameFn fn; void* user; };
SRWLOCK g_listenLock = SRWLOCK_INIT;
Listener g_listeners[kMaxListeners];

// -- the newest frame and what it made ------------------------------------------------------------------------------
SRWLOCK g_pubLock = SRWLOCK_INIT;
Frame g_frame;
bool g_haveFrame = false;
Outputs g_out;
uint32_t g_padCounter = 0;
constexpr uint32_t kRing = 8;
D2RVR_Shared g_ring[kRing];
uint32_t g_ringNext = 0;
std::atomic<const D2RVR_Shared*> g_head{nullptr};

// vrcam's view of the game (SetMenuNow) against the layer the published outputs were made in
std::atomic<int> g_menuNow{-1};      // -1 not told: no check
std::atomic<int> g_outMenu{0};       // the layer of g_out
bool InStep() {
    const int now = g_menuNow.load();
    return now < 0 || now == g_outMenu.load();
}
std::atomic<uint32_t> g_session{0};
std::atomic<ULONGLONG> g_lastFeed{0};
// Feed's own state (the draw thread only)
struct FeedState {
    uint32_t counter = 0, epoch = 0, ctx = ~0u, session = 0;
    viewgate::Edge view;   // the view changed: what is held is let go
    ReleaseGate gate;
    Layout layout;
    Mode mode = Mode::World;
    uint32_t sharedCounter = 0;
    bool told = false;
    bool panelWas = false;   // Outputs.skillPanel the frame before: the request is its rising edge
};
FeedState g_fs;

// A command's number from its name in the ini ("Swap weapons", "D2R key: Swap weapons").
int CmdByName(const char* name) {
    static const char kPrefix[] = "D2R key: ";
    for (const D2RVRCommand& c : kD2RVRCommands) {
        const char* n = c.action;
        if (EqNoCase(name, n)) return c.cmd;
        if (!std::strncmp(n, kPrefix, sizeof kPrefix - 1) && EqNoCase(name, n + sizeof kPrefix - 1)) return c.cmd;
    }
    return -1;
}

std::string NarrowAscii(const wchar_t* w) {
    std::string s;
    for (; *w; ++w) s.push_back(*w < 128 ? (char)*w : '?');
    return s;
}

// Dropped, not an error: the source's commands, pad pulses, takes (the session is over or direct mode went off).
void ClearHeld() {
    for (auto& c : g_cmdSrc) c.store(0);
    for (auto& t : g_tapUntil) t.store(0);
    for (auto& a : g_actSrc) a.store(0);
    g_take[0].store(0); g_take[1].store(0);
    AcquireSRWLockExclusive(&g_pulseLock);
    std::memset(g_pulses, 0, sizeof g_pulses);
    ReleaseSRWLockExclusive(&g_pulseLock);
}

Frame MakeFrame(const D2RVR_XrInput& in, bool menu, bool direct, uint32_t counter, uint32_t session, uint32_t epoch, bool otherView = false) {
    Frame f{};
    f.version = kVersion;
    f.counter = counter;
    f.session = session;
    f.epoch = epoch;
    // (another view: kInMenu too - a detector that only knows the menu flag stands down as well)
    f.flags = (in.focused ? kFocused : 0u) | (menu || otherView ? kInMenu : 0u) | (direct ? kDirect : 0u) | (otherView ? kOtherView : 0u);
    f.displayTime = in.displayTime;
    f.time = (double)in.displayTime * 1e-9;
    auto pose = [](const D2RVR_XrPose& p, Pose* o) {
        std::memcpy(o->pos, p.pos, sizeof o->pos);
        std::memcpy(o->rot, p.rot, sizeof o->rot);
        o->valid = p.valid;
    };
    pose(in.head, &f.head);
    for (int h = 0; h < 2; ++h) {
        const D2RVR_XrHand& s = in.hand[h];
        Hand& o = f.hand[h];
        pose(s.grip, &o.grip);
        pose(s.aim, &o.aim);
        o.trigger = s.trigger; o.squeeze = s.squeeze;
        o.stick[0] = s.stickX; o.stick[1] = s.stickY;
        o.buttons = s.buttons;
        o.active = s.active;
    }
    return f;
}

// The controls the layout sees: the hand's analog values and buttons.
Controls ControlsOf(const D2RVR_XrInput& in) {
    Controls c;
    auto clamp01 = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; };
    for (int h = 0; h < 2; ++h) {
        const D2RVR_XrHand& s = in.hand[h];
        const bool left = h == 0;
        c.v[left ? kLTrigger : kRTrigger] = clamp01(s.trigger);
        c.v[left ? kLGrip : kRGrip] = clamp01(s.squeeze);
        c.v[left ? kLX : kRA] = (s.buttons & kPrimary) ? 1.0f : 0.0f;
        c.v[left ? kLY : kRB] = (s.buttons & kSecondary) ? 1.0f : 0.0f;
        c.v[left ? kLStick : kRStick] = (s.buttons & kStick) ? 1.0f : 0.0f;
        c.v[left ? kLMenu : kRMenu] = (s.buttons & kMenuButton) ? 1.0f : 0.0f;
        c.stick[h][0] = std::isfinite(s.stickX) ? s.stickX : 0.0f;
        c.stick[h][1] = std::isfinite(s.stickY) ? s.stickY : 0.0f;
    }
    return c;
}

// What a take hides: the Button bits and the analog values, as controls of the layout.
void TakenControls(const uint32_t take[2], bool out[kCtlCount]) {
    for (int h = 0; h < 2; ++h) {
        const bool left = h == 0;
        const uint32_t t = take[h];
        if (t & (kPrimary)) out[left ? kLX : kRA] = true;
        if (t & (kSecondary)) out[left ? kLY : kRB] = true;
        if (t & (kStick)) out[left ? kLStick : kRStick] = true;
        if (t & (kMenuButton)) out[left ? kLMenu : kRMenu] = true;
        if (t & (kTrigger | kTakeTrigger)) out[left ? kLTrigger : kRTrigger] = true;
        if (t & (kSqueeze | kTakeSqueeze)) out[left ? kLGrip : kRGrip] = true;
    }
}

// A control a gesture took reads as let go for BodyWalk and the Frame's later readers alike.
void HideTaken(D2RVR_XrInput* in, const uint32_t take[2]) {
    for (int h = 0; h < 2; ++h) {
        D2RVR_XrHand& o = in->hand[h];
        const uint32_t t = take[h];
        o.buttons &= ~(t & (kPrimary | kSecondary | kStick | kMenuButton | kTrigger | kSqueeze));
        if (t & (kTrigger | kTakeTrigger)) { o.trigger = 0.0f; o.buttons &= ~(uint32_t)kTrigger; }
        if (t & (kSqueeze | kTakeSqueeze)) { o.squeeze = 0.0f; o.buttons &= ~(uint32_t)kSqueeze; }
        if (t & kTakeStick) o.stickX = o.stickY = 0.0f;
    }
}

}  // namespace

// -- the public API (direct_input.h) ----------------------------------------------------------------------------------

bool On() { return g_on.load(); }
bool BodyWalkWalks() { return g_on.load() && g_bwWalk.load(); }
bool Governing() { return g_on.load() && ViewOk(); }
bool Active() {
    const ULONGLONG at = g_lastFeed.load();
    return g_on.load() && ViewOk() && at && GetTickCount64() - at < kStaleMs;
}
uint32_t Session() { return g_session.load(); }

bool Latest(Frame* out) {
    AcquireSRWLockShared(&g_pubLock);
    const bool have = g_haveFrame;
    if (have) *out = g_frame;
    ReleaseSRWLockShared(&g_pubLock);
    return have;
}

bool Listen(FrameFn fn, void* user) {
    if (!fn) return false;
    bool ok = false;
    AcquireSRWLockExclusive(&g_listenLock);
    int free = -1;
    bool dup = false;
    for (int i = 0; i < kMaxListeners; ++i) {
        if (g_listeners[i].fn == fn && g_listeners[i].user == user) dup = true;
        if (!g_listeners[i].fn && free < 0) free = i;
    }
    if (!dup && free >= 0) { g_listeners[free] = {fn, user}; ok = true; }
    ReleaseSRWLockExclusive(&g_listenLock);
    return ok;
}

void Unlisten(FrameFn fn, void* user) {
    AcquireSRWLockExclusive(&g_listenLock);
    for (Listener& l : g_listeners) if (l.fn == fn && l.user == user) l = {};
    ReleaseSRWLockExclusive(&g_listenLock);
}

void Command(uint32_t cmd, bool down, Source src) {
    if (cmd >= layout::kCommands || src >= kSrcCount || (down && !ViewOk())) return;
    const uint16_t bit = (uint16_t)(1u << src);
    if (down) g_cmdSrc[cmd].fetch_or(bit); else g_cmdSrc[cmd].fetch_and((uint16_t)~bit);
}

void Tap(uint32_t cmd) {
    if (cmd < layout::kCommands && ViewOk()) g_tapUntil[cmd].store(GetTickCount64() + kTapMs);
}

void Action(uint32_t actBits, bool down, Source src) {
    if (src >= kSrcCount || (down && !ViewOk())) return;
    const uint16_t bit = (uint16_t)(1u << src);
    for (int i = 0; i < kActionBits; ++i)
        if (actBits & (1u << i)) { if (down) g_actSrc[i].fetch_or(bit); else g_actSrc[i].fetch_and((uint16_t)~bit); }
}

void PadPulse(const PadPress& p, uint32_t holdMs) {
    if ((!p.buttons && !p.lt && !p.rt) || !ViewOk()) return;
    const ULONGLONG now = GetTickCount64(), until = now + std::max<uint32_t>(holdMs, 30);
    AcquireSRWLockExclusive(&g_pulseLock);
    int slot = 0;
    for (int i = 0; i < kPulses; ++i) {   // an expired slot, else the oldest
        if (g_pulses[i].until <= now) { slot = i; break; }
        if (g_pulses[i].until < g_pulses[slot].until) slot = i;
    }
    g_pulses[slot] = {p.buttons, p.lt, p.rt, until};
    if (until > g_pulseLatest.load()) g_pulseLatest.store(until);
    ReleaseSRWLockExclusive(&g_pulseLock);
}

void Take(int hand, uint32_t buttons, Source) {
    if (hand < 0 || hand > 1 || !ViewOk()) return;
    g_take[hand].fetch_or(buttons);
}

void SetUiUp(bool up) { g_uiUpUntil.store(up ? GetTickCount64() + 250 : 0); }

bool TakeSkillPanelToggle() {
    const ULONGLONG at = g_panelAt.exchange(0);
    return at && GetTickCount64() - at < 1000;
}

void SetHapticSink(HapticSink sink) { g_hapticSink.store(sink); }
void Haptic(int hand, float amplitude, float durationMs, float frequencyHz) {
    if (hand < 0 || hand > 1 || !(amplitude > 0.0f) || !(durationMs > 0.0f) || !ViewOk()) return;
    if (const HapticSink s = g_hapticSink.load()) s(hand, std::min(amplitude, 1.0f), durationMs, std::isfinite(frequencyHz) ? frequencyHz : 0.0f);
}

// -- the plumbing (direct_feed.h) -------------------------------------------------------------------------------------

void SetLogger(void (*log)(const char*)) { g_log = log; }

void LoadSettings(const wchar_t* ini) {
    wchar_t b[320];
    const bool on = GetPrivateProfileIntW(L"openxr", L"direct_input", 0, ini) != 0;
    // the [direct] table: a key unset keeps the default, empty or "none" unbinds
    const bool stickMap = GetPrivateProfileIntW(L"direct", L"left_stick_map", 1, ini) != 0;   // a quick click of the left stick: the map
    Table t = DefaultTable(stickMap);
    int rebound = 0, bad = 0;
    auto read = [&](const char* key, Binding* into) {
        wchar_t wkey[64];
        MultiByteToWideChar(CP_ACP, 0, key, -1, wkey, 64);
        GetPrivateProfileStringW(L"direct", wkey, L"\x01", b, 320, ini);
        if (b[0] == L'\x01' && !b[1]) return;
        const std::string text = NarrowAscii(b);
        Binding parsed;
        if (ParseBinding(text.c_str(), &CmdByName, &parsed)) { *into = parsed; ++rebound; }
        else { ++bad; LogF("vrcam: direct input - [direct] %s = %s: not understood, the default stays", key, text.c_str()); }
    };
    for (int i = 0; i < kCtlCount; ++i) {
        const Ctl c = (Ctl)i;
        read(Name(c), &t.plain[i]);
        if (c == kLGrip || c == kRGrip) continue;
        char chord[64];
        snprintf(chord, sizeof chord, "%s+%s", Name(GripOf(HandOf(c))), Name(c));
        read(chord, &t.chord[i]);
    }
    // hold-or-tap (vr/hold_tap.h): a "<control>_hold" line gives a control a long press
    t.holdEnabled = GetPrivateProfileIntW(L"direct", L"hold_enabled", 1, ini) != 0;
    t.holdMs = holdtap::ClampHoldMs((long)GetPrivateProfileIntW(L"direct", L"hold_ms", (int)holdtap::kDefaultHoldMs, ini));
    for (int i = 0; i < kCtlCount; ++i) {
        char hold[64];
        snprintf(hold, sizeof hold, "%s_hold", Name((Ctl)i));
        read(hold, &t.hold[i]);
    }
    AcquireSRWLockExclusive(&g_cfgLock);
    g_table = t;
    g_tableChanged = true;
    ReleaseSRWLockExclusive(&g_cfgLock);
    static int was = -1;
    if ((int)on != was) {
        if (on) Log("vrcam: direct input - [openxr] direct_input=1: with a native OpenXR session the plugin reads the controllers itself "
                    "(BodyWalk's pad buttons, actions, commands and tracking are ignored then; only its walking stick passes, [openxr] direct_bodywalk_walk)");
        else if (was == 1) Log("vrcam: direct input - [openxr] direct_input=0: BodyWalk's input again");
        was = on;
    }
    const bool wasOn = g_on.exchange(on);
    if (wasOn && !on) { g_lastFeed.store(0); ClearHeld(); }   // (switched off: what the layout held is let go; the gestures' holds with it)
    const bool bwWalk = GetPrivateProfileIntW(L"openxr", L"direct_bodywalk_walk", 1, ini) != 0;
    g_bwWalk.store(bwWalk);
    static int toldWalk = -1;
    if (on && (int)bwWalk != toldWalk) {
        toldWalk = (int)bwWalk;
        Log(bwWalk ? "vrcam: direct input - [openxr] direct_bodywalk_walk=1: BodyWalk is still given the head and hands (its controls zeroed) and ITS LEFT STICK "
                     "walks the hero, mixed into the controller's own (nothing else of its pad, actions or commands is read)"
                   : "vrcam: direct input - [openxr] direct_bodywalk_walk=0: BodyWalk's walking is ignored with the rest of its input (it is given no headset)");
    }
    static int toldRebinds = -1;
    if (on && rebound + bad != toldRebinds) {
        toldRebinds = rebound + bad;
        LogF("vrcam: direct input - the layout: %d control(s) rebound in [direct], %d line(s) not understood", rebound, bad);
    }
    static int toldMap = -1;
    if (on && (int)stickMap != toldMap) {
        toldMap = (int)stickMap;
        Log(stickMap ? "vrcam: direct input - a quick click of the left stick shows / hides the map ([direct] left_stick_map=1; held it is still Alt)"
                     : "vrcam: direct input - the left stick's click is Alt only ([direct] left_stick_map=0)");
    }
    static int toldHold = -1;
    if (on && (int)(t.holdEnabled ? t.holdMs : 0) != toldHold) {
        toldHold = (int)(t.holdEnabled ? t.holdMs : 0);
        if (t.holdEnabled) LogF("vrcam: direct input - a long press (%u ms) of a stick click: the right one asks for the skill panel, the left one is the "
                                "inventory ([direct] hold_enabled, hold_ms)", t.holdMs);
        else Log("vrcam: direct input - no long presses ([direct] hold_enabled=0): a stick click acts at once");
    }
}

void SetMenuNow(bool menu) { g_menuNow.store(menu ? 1 : 0, std::memory_order_relaxed); }

void SessionBegin() {
    ClearHeld();
    ++g_fs.session;
    g_session.store(g_fs.session);
    g_fs.ctx = ~0u;   // the next frame is a new context: the gate holds everything held until it is let go
    g_fs.layout.Reset();
    g_fs.told = false;
}

void SessionEnd() {
    g_lastFeed.store(0);
    ClearHeld();
    AcquireSRWLockExclusive(&g_pubLock);
    g_haveFrame = false;
    g_out = Outputs{};
    ReleaseSRWLockExclusive(&g_pubLock);
    g_head.store(nullptr);
    g_fs.layout.Reset();
    g_fs.ctx = ~0u;
}

void SharedFromXr(const D2RVR_XrInput& in, uint32_t counter, D2RVR_Shared* out) {
    std::memset(out, 0, sizeof *out);
    out->version = D2RVR_SHARED_VERSION;
    out->counter = counter;
    if (!in.head.valid) return;
    using namespace layout::math;
    const HeadAngles a = Angles(in.head.rot);
    out->headValid = 1;
    out->headYawDeg = a.yawDeg;
    out->headHeightM = in.head.pos[1];
    out->headPitchDeg = a.pitchDeg; out->pitchValid = 1;
    out->headRollDeg = a.rollDeg; out->rollValid = 1;
    uint32_t hands = 0;
    // a hand only with its place too (valid 1)
    if (in.hand[1].grip.valid == 1) { IntoHeading(a.yawRad, in.head.pos, in.hand[1].grip.pos, in.hand[1].grip.rot, out->rightHand, out->rightRot); hands |= 1u; }
    if (in.hand[0].grip.valid == 1) { IntoHeading(a.yawRad, in.head.pos, in.hand[0].grip.pos, in.hand[0].grip.rot, out->leftHand, out->leftRot); hands |= 2u; }
    out->handsValid = hands;
    out->rightGrip = (hands & 1u) ? std::clamp(in.hand[1].squeeze, 0.0f, 1.0f) : 0.0f;
    out->leftGrip = (hands & 2u) ? std::clamp(in.hand[0].squeeze, 0.0f, 1.0f) : 0.0f;
    out->gripMagic = D2RVR_GRIP_MAGIC;
    for (int i = 0; i < 3; ++i) out->headRoom[i] = in.head.pos[i];
    out->headYawRoomDeg = a.yawDeg;
    out->roomMagic = D2RVR_ROOM_MAGIC;
    out->sampleStamp = D2RVRStampNow();
    out->sampleStampMagic = D2RVR_SAMPLE_STAMP_MAGIC;
}

void Feed(D2RVR_XrInput* in, bool menu) {
    if (!in) return;
    FeedState& s = g_fs;
    const bool f4 = ViewOk();
    const bool direct = g_on.load() && f4;
    const bool focused = in->focused != 0;
    if (s.view.Step(f4) && !f4) {   // F4 -> another view: whatever the layer or a gesture holds is let go, BodyWalk's input is the input again
        ClearHeld();
        s.layout.Reset();
        AcquireSRWLockExclusive(&g_pubLock);
        g_out = Outputs{};
        ReleaseSRWLockExclusive(&g_pubLock);
        g_head.store(nullptr);
        g_lastFeed.store(0);
        s.told = false;
    }
    // the context: any change of it is an epoch, and the gate holds every control held until it is let go
    // (the view is a part of it: back in F4 the hands start fresh)
    const uint32_t ctx = (focused ? 1u : 0u) | (menu ? 2u : 0u) | (direct ? 4u : 0u) | (f4 ? 0u : 8u);
    if (ctx != s.ctx) { s.ctx = ctx; ++s.epoch; s.layout.Cancel(); }   // (a press under way is not a tap, nor a hold)
    ++s.counter;
    Frame f = MakeFrame(*in, menu, direct, s.counter, s.session, s.epoch, !f4);
    if (GetTickCount64() < g_uiUpUntil.load()) f.flags |= kUiUp;
    AcquireSRWLockExclusive(&g_pubLock);
    g_frame = f;
    g_haveFrame = true;
    ReleaseSRWLockExclusive(&g_pubLock);

    // the listeners: they may take controls for this frame
    g_take[0].store(0); g_take[1].store(0);
    Listener ls[kMaxListeners];
    AcquireSRWLockShared(&g_listenLock);
    std::memcpy(ls, g_listeners, sizeof ls);
    ReleaseSRWLockShared(&g_listenLock);
    for (const Listener& l : ls) if (l.fn) l.fn(f, l.user);
    const uint32_t take[2] = {g_take[0].load(), g_take[1].load()};
    if (f4) HideTaken(in, take);   // (another view: nothing is taken from BodyWalk's hands either)

    if (!direct) return;

    AcquireSRWLockExclusive(&g_cfgLock);
    if (g_tableChanged) { s.layout.SetWorld(g_table); g_tableChanged = false; }
    ReleaseSRWLockExclusive(&g_cfgLock);

    bool taken[kCtlCount] = {};
    TakenControls(take, taken);
    // (the controls after HideTaken: a taken one is zero already; the gate keeps it masked once the gesture lets go)
    const Controls c = s.gate.Step(s.epoch, ControlsOf(*in), taken);
    const Mode mode = menu ? Mode::Menu : Mode::World;
    const Outputs o = s.layout.Step(mode, c, GetTickCount64(), (f.flags & kUiUp) != 0, NoShotMask(take));   // (a bow's draw-only mode: shots dropped, the rest as bound)
    if (o.skillPanel && !s.panelWas) g_panelAt.store(GetTickCount64());   // a long press asks for the skill panel, once
    s.panelWas = o.skillPanel;

    D2RVR_Shared* sh = &g_ring[g_ringNext++ % kRing];
    s.sharedCounter++;
    SharedFromXr(*in, s.sharedCounter, sh);

    AcquireSRWLockExclusive(&g_pubLock);
    g_out = o;
    g_outMenu.store(menu ? 1 : 0);
    ++g_padCounter;
    ReleaseSRWLockExclusive(&g_pubLock);
    g_head.store(sh);
    g_lastFeed.store(GetTickCount64());
    if (!s.told) {
        s.told = true;
        Log("vrcam: direct input - the plugin's own layout is feeding the game (pad, actions, commands, head and hands)");
    }
}

const char* WhyNotActive(uint32_t* ageMs) {
    const ULONGLONG at = g_lastFeed.load();
    const ULONGLONG now = GetTickCount64();
    if (ageMs) *ageMs = at ? (uint32_t)std::min<ULONGLONG>(now - at, 0xFFFFFFFFull) : 0;
    if (!g_on.load()) return "[openxr] direct_input is off";
    if (!ViewOk()) return "not VR first person (F4): direct input stands down";
    if (!at) return "no headset frame yet (the session has not started, or it ended)";
    if (now - at >= kStaleMs) return "no headset frame for a second or more (the game's draw thread stalled - an area change - or the session lost focus or ended)";
    return "";
}

bool PadRead(PadOut* out) {
    if (!Active()) return false;
    AcquireSRWLockShared(&g_pubLock);
    const Pad p = InStep() ? g_out.pad : Pad{};   // (the other layer's outputs are let go until the next frame)
    const uint32_t counter = g_padCounter;
    ReleaseSRWLockShared(&g_pubLock);
    *out = {p.buttons, p.lt, p.rt, p.lx, p.ly, p.rx, p.ry, counter};
    return true;
}

bool PadPulses(uint16_t* buttons, uint8_t* lt, uint8_t* rt) {
    *buttons = 0; *lt = 0; *rt = 0;
    bool any = false;
    const ULONGLONG now = GetTickCount64();
    if (now >= g_pulseLatest.load() || !ViewOk()) return false;
    AcquireSRWLockShared(&g_pulseLock);
    for (const Pulse& p : g_pulses) {
        if (p.until <= now) continue;
        any = true;
        *buttons |= p.buttons;
        *lt = std::max(*lt, p.lt);
        *rt = std::max(*rt, p.rt);
    }
    ReleaseSRWLockShared(&g_pulseLock);
    return any;
}

uint32_t LayoutActions() {
    if (!Active()) return 0;
    AcquireSRWLockShared(&g_pubLock);
    const uint32_t a = InStep() ? g_out.actions : 0u;
    ReleaseSRWLockShared(&g_pubLock);
    return a;
}

uint32_t InjectedActions() {
    uint32_t a = 0;
    if (!ViewOk()) return 0;
    for (int i = 0; i < kActionBits; ++i) if (g_actSrc[i].load()) a |= 1u << i;
    return a;
}

void LayoutCommands(uint64_t out[2]) {
    out[0] = out[1] = 0;
    if (!Active()) return;
    AcquireSRWLockShared(&g_pubLock);
    if (InStep()) { out[0] = g_out.commands[0]; out[1] = g_out.commands[1]; }
    ReleaseSRWLockShared(&g_pubLock);
}

void InjectedCommands(uint64_t out[2]) {
    out[0] = out[1] = 0;
    if (!ViewOk()) return;
    const ULONGLONG now = GetTickCount64();
    for (uint32_t c = 0; c < layout::kCommands; ++c)
        if (g_cmdSrc[c].load() || now < g_tapUntil[c].load()) out[c / 64] |= 1ull << (c % 64);
}

const D2RVR_Shared* HeadShared() { return g_head.load(); }

}  // namespace directinput
