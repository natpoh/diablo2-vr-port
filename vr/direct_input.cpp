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

#include "belt_flick.h"
#include "direct_feed.h"
#include "direct_input.h"
#include "direct_layout.h"
#include "direct_remap.h"
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
std::atomic<bool> g_flatVrCtl{true};   // [direct] flatvr_controls: in FlatVR the layout runs on BodyWalk's raw controllers (flat_raw.h)
std::atomic<bool> g_fedByLink{false};  // the frames come from Game Link (FlatVR), not from a native session
std::atomic<bool> g_walkFlat{true}; // [direct] walk_flat_views: the left stick walks in VR F1-F3 (movement only, vr/walk_flat.h)
std::atomic<bool> g_beltFlat{true}; // [direct] belt_flick_flat_views: left trigger + a flick of a stick drinks a belt column in VR F1-F3 (vr/belt_flick.h)
std::atomic<BeltSink> g_beltSink{nullptr};
std::atomic<PressSink> g_pressSink{nullptr};
ULONGLONG g_pressHintAt = 0;   // FeedWalk's thread only
// The stick of the last frame in a flat view (walkflat::Pack: the stick and the frame's number) and when it came. 0 = none: F4, the key off,
// the session over - the pad poll gets no walk from here then.
std::atomic<uint64_t> g_walkWord{0};
std::atomic<ULONGLONG> g_walkAt{0};
std::atomic<ULONGLONG> g_frameAt{0};   // the newest headset frame in a flat view that FeedWalk saw (whatever the keys): fresh within kStaleMs
// The plugin's own controller in VR F1-F3 ([direct] flat_controller): its layout's newest outputs (pad, actions, commands), when they were made and in which layer.
std::atomic<bool> g_flatCtl{true};
std::atomic<FlatGate> g_flatGateFn{nullptr};
SRWLOCK g_flatLock = SRWLOCK_INIT;
Outputs g_flatOut;                      // under g_flatLock
uint32_t g_flatCounter = 0;             // under g_flatLock
int g_flatOutMenu = 0;                  // under g_flatLock: the layer of g_flatOut
std::atomic<ULONGLONG> g_flatAt{0};
SRWLOCK g_cfgLock = SRWLOCK_INIT;
Table g_table = DefaultTable();   // the world layer; the menu layer is the game's own
bool g_tableChanged = true;       // Feed hands it to the layout once
Table g_flatTable = WithoutPanels(DefaultTable());   // the same without the skill panel's long press: VR F1-F3's controller
bool g_flatTableChanged = true;

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
std::atomic<ULONGLONG> g_pauseAt{0};     // a press asked for the pause menu (Esc) at this tick (TakePauseToggle)
std::atomic<ULONGLONG> g_quiverAt{0};    // a button asked for a step of the javelin quiver at this tick (TakeQuiverStep) ...
std::atomic<int> g_quiverDir{0};         // ... in this direction, +1 next, -1 previous

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
    bool pauseWas = false;   // Outputs.pause the frame before (the same)
    uint8_t quiverWas = 0;   // Outputs.quiver the frame before: a step is its rising edge
    // the walk in VR F1-F3 (walkflat): the left stick through a release gate of its own, the frame number the pad poll sees
    ReleaseGate walkGate;
    bool walkWas = false;
    uint32_t walkCounter = 0;
    beltflick::Machine belt;   // the left trigger + a flick: a belt column (VR F1-F3)
    // the plugin's own controller in VR F1-F3: a layout of its own, its release gate, what FeedWalk made of the left stick for its pad
    Layout flatLayout;
    ReleaseGate flatGate;
    bool flatWas = false;
    uint32_t flatCtx = ~0u;
    int16_t walkLX = 0, walkLY = 0;
    bool beltHolding = false;
};
FeedState g_fs;

// VR F1, F2, F3 with [direct] walk_flat_views: the left stick, and nothing else of the controllers, for the game's walking code. The
// stick is held back (the gate) while the view, the focus or a panel has just changed, until it is let go - a stick held on the way in
// walks nothing - and it is zero while a panel is up (the stick is the menu's).
// A control pressed that nothing of ours takes in VR F1-F3 (the walk is the left stick, the belt the left trigger and a flick).
const char* PressedElsewhere(const D2RVR_XrInput& in) {
    const D2RVR_XrHand& l = in.hand[0];
    const D2RVR_XrHand& r = in.hand[1];
    constexpr uint32_t kBtn = kPrimary | kSecondary | kStick | kMenuButton;
    if (std::isfinite(r.trigger) && r.trigger >= 0.6f) return "right trigger";
    if (std::isfinite(r.squeeze) && r.squeeze >= 0.6f) return "right grip";
    if (std::isfinite(l.squeeze) && l.squeeze >= 0.6f) return "left grip";
    if (r.buttons & kBtn) return "a right-hand button";
    if (l.buttons & kBtn) return "a left-hand button";
    return nullptr;
}

void FeedWalk(const D2RVR_XrInput& in, bool focused, bool menu) {
    FeedState& s = g_fs;
    g_frameAt.store(GetTickCount64(), std::memory_order_release);
    s.walkLX = s.walkLY = 0;
    if (!s.walkWas) s.walkGate.Suspend();   // coming in from another view (or the first frame): start masked
    s.walkWas = true;
    const D2RVR_XrHand& h = in.hand[0];
    const bool live = focused && !menu;
    auto stickOf = [&](const D2RVR_XrHand& hand) {
        beltflick::Stick st;
        if (focused && hand.active && std::isfinite(hand.stickX) && std::isfinite(hand.stickY)) { st.x = hand.stickX; st.y = hand.stickY; }
        return st;
    };
    const beltflick::Stick left = stickOf(h), right = stickOf(in.hand[1]);
    // The belt: the left trigger held and a flick of a stick (vr/belt_flick.h). Its own key; the sink (vrcam) has the last word. While the
    // trigger is held the left stick does not walk - and is masked after it until it has been let go (the gate's context moves with the trigger).
    const float trigger = focused && h.active && std::isfinite(h.trigger) ? h.trigger : 0.0f;
    const beltflick::Machine::Out b = s.belt.Step(trigger, left, right, live && g_beltFlat.load());
    if (b.column >= 0) {
        if (const BeltSink sink = g_beltSink.load()) sink((uint32_t)b.column, b.stick, (int)b.dir);
    }
    s.beltHolding = b.holding;
    if (live && !g_flatCtl.load()) {   // the controller is off: a control pressed that nothing of ours takes here is said (rarely) in the log
        if (const char* what = PressedElsewhere(in)) {
            const ULONGLONG now = GetTickCount64();
            if (!g_pressHintAt || now - g_pressHintAt >= walkflat::kHintEveryMs) {
                if (const PressSink sink = g_pressSink.load()) { if (sink(what)) g_pressHintAt = now; }
            }
        }
    }
    if (!g_walkFlat.load()) {   // the walk's key is off (the belt's is on): no stick is published, and it starts masked when the walk comes back
        g_walkAt.store(0, std::memory_order_release);
        g_walkWord.store(0, std::memory_order_release);
        s.walkWas = false;
        return;
    }
    Controls c;
    c.stick[0][0] = left.x; c.stick[0][1] = left.y;
    c = s.walkGate.Step((focused ? 1u : 0u) | (menu ? 2u : 0u) | (b.holding ? 4u : 0u), c);
    int16_t lx = 0, ly = 0;
    if (!menu && !b.holding) walkflat::StickToPad(c.stick[0][0], c.stick[0][1], &lx, &ly);
    s.walkLX = lx; s.walkLY = ly;
    g_walkWord.store(walkflat::Pack(lx, ly, ++s.walkCounter), std::memory_order_release);
    g_walkAt.store(GetTickCount64(), std::memory_order_release);
}

// Back in F4 (or the keys off, the session over): the walk's stick is gone, the next flat view starts masked, the belt's machine and the controller's
// layout let go.
void EndWalk() {
    g_walkAt.store(0, std::memory_order_release);
    g_walkWord.store(0, std::memory_order_release);
    g_frameAt.store(0, std::memory_order_release);
    g_flatAt.store(0, std::memory_order_release);
    AcquireSRWLockExclusive(&g_flatLock);
    g_flatOut = Outputs{};
    ReleaseSRWLockExclusive(&g_flatLock);
    g_fs.walkWas = false;
    g_fs.flatWas = false;
    g_fs.belt.Reset();
    g_fs.flatLayout.Reset();
}

// A command's number from its name in the ini ("Swap weapons", "D2R key: Swap weapons"): vr/direct_remap.h, the same as D2R VR Settings' button list.
int CmdByName(const char* name) { return remap::CmdByName(name); }

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

Frame MakeFrame(const D2RVR_XrInput& in, bool menu, bool direct, uint32_t counter, uint32_t session, uint32_t epoch, bool otherView = false,
                const uint32_t* touches = nullptr) {
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
        o.touches = touches ? touches[h] : 0u;
    }
    return f;
}

// The controls the layout sees: the hand's analog values and buttons, and (touches: the TouchBit bits of each hand) the finger on the controls that report it.
Controls ControlsOf(const D2RVR_XrInput& in, const uint32_t* touches = nullptr) {
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
        const uint32_t t = touches ? touches[h] : 0u;
        c.touch[left ? kLTrigger : kRTrigger] = (t & kTouchTrigger) != 0;
        c.touch[left ? kLX : kRA] = (t & kTouchPrimary) != 0;
        c.touch[left ? kLY : kRB] = (t & kTouchSecondary) != 0;
        c.touch[left ? kLStick : kRStick] = (t & kTouchStick) != 0;
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

// The plugin's own controller in VR F1-F3 ([direct] flat_controller, vr/walk_flat.h): the F4 layout over the frame's controls - through a release gate of its
// own, in the menu layer while a panel is open - minus the skill panel's long press. Its pad takes the left stick from FeedWalk (the walk's own gate and the
// belt's hold: while the left trigger is held for a flick the sticks are the flick's, not the game's); the actions and the game's key commands it holds go the
// way BodyWalk's would (link::ActionsHeld / CommandsHeld). Called by Feed in a flat view, after FeedWalk; nothing of the F4 world is touched.
void FeedFlatController(const D2RVR_XrInput& in, bool focused, bool menu) {
    FeedState& s = g_fs;
    if (!g_flatCtl.load()) {   // off: nothing published, and it starts fresh when it comes back
        if (s.flatWas || g_flatAt.load()) { g_flatAt.store(0, std::memory_order_release); s.flatWas = false; s.flatLayout.Reset(); }
        return;
    }
    AcquireSRWLockExclusive(&g_cfgLock);
    if (g_flatTableChanged) { s.flatLayout.SetWorld(g_flatTable); g_flatTableChanged = false; }
    ReleaseSRWLockExclusive(&g_cfgLock);
    if (!s.flatWas) { s.flatGate.Suspend(); s.flatLayout.Reset(); s.flatCtx = ~0u; }   // coming in from another view: the controls held on the way in do nothing until let go
    s.flatWas = true;
    const uint32_t ctx = (focused ? 1u : 0u) | (menu ? 2u : 0u);
    if (ctx != s.flatCtx) { s.flatCtx = ctx; s.flatLayout.Cancel(); }   // (a press under way is not a tap, nor a hold)
    const Controls c = s.flatGate.Step(ctx, ControlsOf(in));
    Outputs o = s.flatLayout.Step(menu ? Mode::Menu : Mode::World, c, GetTickCount64(), false, 0);
    if (!menu) {   // the world: the left stick is the walk's (or nothing, with the walk off)
        o.pad.lx = s.walkLX;
        o.pad.ly = s.walkLY;
    }
    if (s.beltHolding) { o.pad.lx = o.pad.ly = o.pad.rx = o.pad.ry = 0; }   // a flick's trigger is held: the sticks are the belt's
    o.skillPanel = false;
    o.pause = false;
    o.quiver = 0;
    AcquireSRWLockExclusive(&g_flatLock);
    g_flatOut = o;
    g_flatOutMenu = menu ? 1 : 0;
    ++g_flatCounter;
    ReleaseSRWLockExclusive(&g_flatLock);
    g_flatAt.store(GetTickCount64(), std::memory_order_release);
}

}  // namespace

// -- the public API (direct_input.h) ----------------------------------------------------------------------------------

bool On() { return g_on.load(); }
bool BodyWalkWalks() { return g_on.load() && g_bwWalk.load(); }
bool FlatVrControlsOn() { return g_on.load() && g_flatVrCtl.load(); }
bool FedByLink() { return g_fedByLink.load(); }
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

void PadPulseEnd(const PadPress& p) {
    const ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&g_pulseLock);
    for (Pulse& q : g_pulses)
        if (q.until > now && q.buttons == p.buttons && q.lt == p.lt && q.rt == p.rt) q.until = now;
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

bool TakePauseToggle() {
    if (!ViewOk()) { g_pauseAt.store(0); return false; }   // F4 only
    const ULONGLONG at = g_pauseAt.exchange(0);
    return at && GetTickCount64() - at < 1000;
}

int TakeQuiverStep() {
    if (!ViewOk()) { g_quiverAt.store(0); g_quiverDir.store(0); return 0; }   // F4 only
    const ULONGLONG at = g_quiverAt.exchange(0);
    const int dir = g_quiverDir.exchange(0);
    return at && GetTickCount64() - at < 1000 ? dir : 0;
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
    const bool pauseButton = GetPrivateProfileIntW(L"direct", L"pause_button", 1, ini) != 0;  // a long press of the left menu button: the pause menu
    // (vr/direct_layout.h TableFromIni - the same function D2R VR Settings' button list reads the table with)
    const IniGet get = [&](const char* key, std::string* text) {
        wchar_t wkey[64];
        MultiByteToWideChar(CP_ACP, 0, key, -1, wkey, 64);
        GetPrivateProfileStringW(L"direct", wkey, L"\x01", b, 320, ini);
        if (b[0] == L'\x01' && !b[1]) return false;
        *text = NarrowAscii(b);
        return true;
    };
    int rebound = 0, bad = 0;
    const Table t = TableFromIni(get, &CmdByName, &rebound, &bad, [](const char* key, const std::string& text) {
        LogF("vrcam: direct input - [direct] %s = %s: not understood, the default stays", key, text.c_str());
    });
    AcquireSRWLockExclusive(&g_cfgLock);
    g_table = t;
    g_tableChanged = true;
    g_flatTable = WithoutPanels(t);   // VR F1-F3's controller: the same rebinding, without the skill panel's long press
    g_flatTableChanged = true;
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
    const bool flatVrCtl = GetPrivateProfileIntW(L"direct", L"flatvr_controls", 1, ini) != 0;
    g_flatVrCtl.store(flatVrCtl);
    static int toldFlatVr = -1;
    if (on && (int)flatVrCtl != toldFlatVr) {
        toldFlatVr = (int)flatVrCtl;
        Log(flatVrCtl ? "vrcam: direct input - [direct] flatvr_controls=1: in FlatVR the plugin's own layout runs on BodyWalk's raw controllers (Game Link, "
                        "BodyWalk with raw controls): BodyWalk's Mapping, pad buttons, actions and commands are ignored there, as in native OpenXR"
                      : "vrcam: direct input - [direct] flatvr_controls=0: in FlatVR BodyWalk's Mapping and pad are the controls, as before");
    }
    const bool walkFlat = GetPrivateProfileIntW(L"direct", L"walk_flat_views", 1, ini) != 0;
    g_walkFlat.store(walkFlat);
    static int toldFlat = -1;
    if (on && (int)walkFlat != toldFlat) {
        toldFlat = (int)walkFlat;
        Log(walkFlat ? "vrcam: direct input - [direct] walk_flat_views=1: in VR F1, F2, F3 the left stick walks the hero when BodyWalk is not running "
                       "(movement only: no attack, button, gesture or panel there; BodyWalk running = its pad as before)"
                     : "vrcam: direct input - [direct] walk_flat_views=0: VR F1, F2, F3 keep BodyWalk's input alone (no walking without it)");
    }
    const bool flatCtl = GetPrivateProfileIntW(L"direct", L"flat_controller", 1, ini) != 0;
    g_flatCtl.store(flatCtl);
    static int toldCtl = -1;
    if (on && (int)flatCtl != toldCtl) {
        toldCtl = (int)flatCtl;
        Log(flatCtl ? "vrcam: direct input - [direct] flat_controller=1: in VR F1, F2, F3, when BodyWalk is not running, the plugin's own controller is the game's pad - the F4 layout "
                      "(the right trigger attacks, A picks up, B / X / Y the skills, the triggers, the sticks, the menu button ...) without what only F4 has; BodyWalk running = its pad as before"
                    : "vrcam: direct input - [direct] flat_controller=0: VR F1, F2, F3 without BodyWalk have no controller buttons (the walk and the belt potions only)");
    }
    const bool beltFlat = GetPrivateProfileIntW(L"direct", L"belt_flick_flat_views", 1, ini) != 0;
    g_beltFlat.store(beltFlat);
    static int toldBelt = -1;
    if (on && (int)beltFlat != toldBelt) {
        toldBelt = (int)beltFlat;
        Log(beltFlat ? "vrcam: direct input - [direct] belt_flick_flat_views=1: in VR F1, F2, F3 (no BodyWalk running) the left trigger held + a flick of a thumbstick "
                       "drinks a belt potion: left = column 1, up = 2, right = 3, down = 4 (the game's own belt keys); the left stick does not walk meanwhile"
                     : "vrcam: direct input - [direct] belt_flick_flat_views=0: no belt potions from the thumbsticks in VR F1, F2, F3");
    }
    static int toldRebinds = -1;
    if (on && rebound + bad != toldRebinds) {
        toldRebinds = rebound + bad;
        LogF("vrcam: direct input - the layout: %d control(s) rebound in [direct], %d line(s) not understood", rebound, bad);
    }
    {   // the touch lines, and where the game's run / walk switch (command 0x23) was put - which trigger it is on (a press, a long press, the grip held, a touch): said once per change
        static std::string toldTouch, toldRunWalk;
        std::string touch, runWalk;
        auto add = [](std::string& s, const std::string& what) { if (!s.empty()) s += "; "; s += what; };
        auto runs = [](const Binding& b) { for (const Out& o : b.out) if (o.kind == kOutCommand && o.value == 0x23) return true; return false; };
        for (int i = 0; i < layout::kCtlCount; ++i) {
            const layout::Ctl c = (layout::Ctl)i;
            const std::string n = layout::Name(c);
            if (runs(t.plain[i])) add(runWalk, "a press of " + n);
            if (runs(t.hold[i])) add(runWalk, "a long press of " + n);
            if (c != layout::kLGrip && c != layout::kRGrip && runs(t.chord[i])) add(runWalk, "a press of " + n + " with the same hand's grip held");
            if (runs(t.touch[i])) add(runWalk, "a touch of " + n);
            if (layout::HasTouch(c) && layout::FormatBinding(t.touch[i]) != "none") add(touch, n + "_touch = " + layout::FormatBinding(t.touch[i]));
        }
        if (on && touch != toldTouch) {
            toldTouch = touch;
            if (touch.empty()) Log("vrcam: direct input - no touch lines in [direct] now");
            else LogF("vrcam: direct input - touch lines (the finger arriving on the control, once per touch, F4 only; a Touch, PS VR2 or Index controller reports it): %s", touch.c_str());
        }
        if (on && runWalk != toldRunWalk) {
            toldRunWalk = runWalk;
            LogF("vrcam: direct input - Run / walk switch (the game's command 0x23) is on: %s", runWalk.empty() ? "no button now" : runWalk.c_str());
        }
    }
    static int toldMap = -1;
    if (on && (int)stickMap != toldMap) {
        toldMap = (int)stickMap;
        Log(stickMap ? "vrcam: direct input - a quick click of the left stick shows / hides the map ([direct] left_stick_map=1; held it is still Alt)"
                     : "vrcam: direct input - the left stick's click is Alt only ([direct] left_stick_map=0)");
    }
    static int toldPause = -1;
    if (on && (int)pauseButton != toldPause) {
        toldPause = (int)pauseButton;
        Log(pauseButton ? "vrcam: direct input - a long press of the left menu button (PS VR2: the Options button of the RIGHT controller - SteamVR gives a Touch game that one as the left menu button and no Create button) is the pause menu (Esc) ([direct] pause_button=1, left_menu_hold); a tap is the inventory"
                        : "vrcam: direct input - no pause button on the left menu button ([direct] pause_button=0): the menu button is the inventory at once");
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

void SessionBegin(bool fromLink) {
    g_fedByLink.store(fromLink);
    ClearHeld();
    ++g_fs.session;
    g_session.store(g_fs.session);
    g_fs.ctx = ~0u;   // the next frame is a new context: the gate holds everything held until it is let go
    g_fs.layout.Reset();
    g_fs.told = false;
}

void SessionEnd() {
    g_fedByLink.store(false);
    g_lastFeed.store(0);
    EndWalk();
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

void Feed(D2RVR_XrInput* in, bool menu, const uint32_t touches[2]) {
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
    Frame f = MakeFrame(*in, menu, direct, s.counter, s.session, s.epoch, !f4, touches);
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

    // VR F1-F3: the one thing that acts outside F4 - the left stick's walk (walkflat). Nothing of the layout runs there.
    if (!f4 && g_on.load() && (g_walkFlat.load() || g_beltFlat.load() || g_flatCtl.load()) && viewgate::Flat()) {
        FeedWalk(*in, focused, menu);
        FeedFlatController(*in, focused, menu);
    } else if (s.walkWas || s.flatWas || g_walkAt.load() || g_flatAt.load() || g_frameAt.load()) EndWalk();

    if (!direct) return;

    AcquireSRWLockExclusive(&g_cfgLock);
    if (g_tableChanged) { s.layout.SetWorld(g_table); g_tableChanged = false; }
    ReleaseSRWLockExclusive(&g_cfgLock);

    bool taken[kCtlCount] = {};
    TakenControls(take, taken);
    // (the controls after HideTaken: a taken one is zero already; the gate keeps it masked once the gesture lets go)
    const Controls c = s.gate.Step(s.epoch, ControlsOf(*in, touches), taken);
    const Mode mode = menu ? Mode::Menu : Mode::World;
    const Outputs o = s.layout.Step(mode, c, GetTickCount64(), (f.flags & kUiUp) != 0, NoShotMask(take));   // (a bow's draw-only mode: shots dropped, the rest as bound)
    if (o.skillPanel && !s.panelWas) g_panelAt.store(GetTickCount64());   // a long press asks for the skill panel, once
    s.panelWas = o.skillPanel;
    if (o.pause && !s.pauseWas) {   // a press asks for the pause menu, once
        g_pauseAt.store(GetTickCount64());
        static int told = 0;   // (a deliberate press, so a line each; vrcam's "Esc sent" or "NOT sent" follows it - which of the two never came is the answer to "nothing happens")
        if (told++ < 60) Log("vrcam: direct input - the pause button was held: Esc is asked for");
    }
    s.pauseWas = o.pause;
    if (const uint8_t edge = (uint8_t)(o.quiver & ~s.quiverWas)) {   // a button bound to the javelin quiver's step: once per press
        g_quiverDir.store(edge & 1 ? +1 : -1);
        g_quiverAt.store(GetTickCount64());
    }
    s.quiverWas = o.quiver;

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

void FlatFacts(walkflat::Inputs* in) {
    in->keyOn = g_walkFlat.load();
    in->directOn = g_on.load();
    in->flatView = viewgate::Flat();
    const ULONGLONG at = g_frameAt.load(std::memory_order_acquire);
    in->framesFresh = in->directOn && in->flatView && at && GetTickCount64() - at < kStaleMs;
}

bool WalkRead(WalkOut* out, walkflat::Inputs* in) {
    in->keyOn = g_walkFlat.load();
    in->directOn = g_on.load();
    in->flatView = viewgate::Flat();
    in->framesFresh = false;
    if (!in->keyOn || !in->directOn || !in->flatView) return false;
    const ULONGLONG at = g_walkAt.load(std::memory_order_acquire);
    in->framesFresh = at && GetTickCount64() - at < kStaleMs;
    const uint64_t w = g_walkWord.load(std::memory_order_acquire);
    walkflat::Unpack(w, &out->lx, &out->ly, &out->counter);
    if (!in->framesFresh) out->lx = out->ly = 0;
    return true;
}

// -- the plugin's own controller in VR F1-F3 --------------------------------------------------------------------------
void SetFlatGate(FlatGate gate) { g_flatGateFn.store(gate); }
bool FlatControllerOn() { return g_flatCtl.load(); }

bool FlatControllerLive() {
    if (!g_flatCtl.load() || !g_on.load() || !viewgate::Flat()) return false;
    const ULONGLONG at = g_flatAt.load(std::memory_order_acquire);
    if (!at || GetTickCount64() - at >= kStaleMs) return false;
    const FlatGate gate = g_flatGateFn.load();
    return gate && gate();
}

namespace {
// The other layer's outputs are let go until the next frame (a panel has just opened or closed, vrcam's SetMenuNow knows before the headset frame does).
bool FlatInStep() {
    const int now = g_menuNow.load();
    return now < 0 || now == g_flatOutMenu;
}
}  // namespace

bool FlatPadRead(FlatPad* out) {
    if (!FlatControllerLive()) return false;
    AcquireSRWLockShared(&g_flatLock);
    const Pad p = FlatInStep() ? g_flatOut.pad : Pad{};
    const uint32_t counter = g_flatCounter;
    ReleaseSRWLockShared(&g_flatLock);
    *out = {p.buttons, p.lt, p.rt, p.lx, p.ly, p.rx, p.ry, counter};
    return true;
}

uint32_t FlatActions() {
    if (!FlatControllerLive()) return 0;
    AcquireSRWLockShared(&g_flatLock);
    const uint32_t a = FlatInStep() ? g_flatOut.actions : 0u;
    ReleaseSRWLockShared(&g_flatLock);
    return a;
}

void FlatCommands(uint64_t out[2]) {
    out[0] = out[1] = 0;
    if (!FlatControllerLive()) return;
    AcquireSRWLockShared(&g_flatLock);
    if (FlatInStep()) { out[0] = g_flatOut.commands[0]; out[1] = g_flatOut.commands[1]; }
    ReleaseSRWLockShared(&g_flatLock);
}

void SetBeltSink(BeltSink sink) { g_beltSink.store(sink); }
void SetPressSink(PressSink sink) { g_pressSink.store(sink); g_pressHintAt = 0; }
bool BeltFlickOn() { return g_beltFlat.load(); }

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
