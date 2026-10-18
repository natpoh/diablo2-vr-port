// throw_gesture_wire.cpp - [hands] throw_gesture / throw_by_grip: the throwing hand of a headset frame
// (vr/direct_input.h) fed to the detector of vr/throw_gesture.h, and a released throw made the game's own throw.
//
// A javelin or a throwing weapon in the hand and a box ticked: the right trigger (throw_gesture) or the right
// grip (throw_by_grip) belongs to the gesture (directinput::Take: the layout sees it as let go).
//   trigger: a press opens a gesture, the hand's swing and the release of the trigger decide;
//   grip:    the grip is held as long as the weapon is in the hand (the detector's hold mode), the swing at the
//            end of it and the release of the grip decide - a release with the hand at rest throws nothing.
// At the release of a throw
//   1. the game's own attack is pressed once (directinput::Action, D2RVR_ACT_ATTACK: the pad's A, never a
//      pick-up), held 70 ms and let go - the command path the game's controller takes, with its animation,
//      attack rate and ammunition;
//   2. for the few ticks the game takes to read that press, Aim() says where the hand was going, and
//      vrcam's aim hooks (the ones [hands] throw_aim steers) put the attack's point out along it.
// Nothing of the game is written, no server-side call is made: the missile, its speed and its collision are
// the game's. Where the gesture cannot be made a heading (no camera frame yet, the head pose lost) the throw
// is dropped, not thrown at the game's own aim.
//
// The log says what it sees, once per change: the weapon, whether it counts as thrown, whether the play is on
// (focus, menus, F4), what is taken - and why a release was not a throw, and whether the game ever asked for
// the aim of a throw's press.
//
// The gesture and its numbers: bearstonem/d2r-pcvr (MIT), steamvr/gameplay.h, the javelin branch - which
// ends in a direct server-side throw; only the gesture is taken here.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "d2r_vr_shared.h"
#include "d2r_vr_state.h"
#include "direct_input.h"
#include "throw_cast.h"
#include "throw_gesture.h"
#include "throw_gesture_wire.h"
#include "throw_born.h"
#include "potion_hide.h"
#include "throw_land.h"
#include "throw_source.h"
#include "view_gate.h"
#include "wheel_game.h"

namespace gamestate { uint32_t WeaponType(); bool MenuOpen(); uint32_t WeaponHand(); uint32_t HandCode(int side); bool HandIsThrowPotion(int side); }

namespace throwwire {
namespace {

enum Mode : int { kOff = 0, kTrigger = 1, kGrip = 2 };

const D2RL::PluginContext* g_ctx = nullptr;
bool (*g_canAim)() = nullptr;

std::atomic<int> g_mode{kOff};
std::atomic<bool> g_listening{false};

// The heading of the last released throw, read by the game's thread (Aim) while the draw thread writes it.
std::atomic<float> g_right{0.0f}, g_fwd{1.0f};
std::atomic<ULONGLONG> g_liveUntil{0};   // GetTickCount64, ms
std::atomic<bool> g_read{false};         // a hook asked for the aim since the last throw's press
std::atomic<bool> g_gripOwns{false};     // a throw_by_grip gesture owns the right grip this frame
std::atomic<bool> g_trigOwns{false};     // a throw_gesture (trigger) gesture owns the right trigger this frame
std::atomic<bool> g_gripDown{false};     // ... and the right grip is squeezed on the weapon (the frame's own value: the gesture's Take hides it from the shared block)
std::atomic<float> g_speed{0.0f}, g_horiz{0.0f}, g_vert{0.0f}, g_below{0.0f}, g_relSpeed{0.0f};   // the last throw's hand, for Info
std::atomic<unsigned> g_seq{0};
std::atomic<ULONGLONG> g_lastThrow{0};   // GetTickCount64 of the last released throw
std::atomic<unsigned> g_shotCount{0};    // [debug] bow_latency: the throws whose game button was pressed so far, and the steady-clock seconds (throwborn::NowS()'s base) of the last one's release and press
std::atomic<double> g_shotReleaseS{0.0}, g_shotPressS{0.0};
double SteadyS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::atomic<ULONGLONG> g_holdOffUntil{0};   // a hit by touch pressed the attack with this thrown weapon: a release before this tick throws nothing
bool (*g_heroMode)(uint32_t*) = nullptr;
bool (*g_autoSkill)() = nullptr;
std::atomic<int> g_potionMode{0};                 // [hands] potion_throw_mode (vr/throw_land.h)
std::atomic<bool> g_pressIsThrow{false};          // the A attack held down now is a throw's
std::atomic<WeightThrowFn> g_weightFn{nullptr};   // the weight model's account of a throw (SetWeightSource)
std::mutex g_wMtx;
struct WeightTaken { unsigned seq = 0; bool ok = false; throwsrc::Choice c; } g_w;   // taken for the throw with this seq
std::atomic<bool> g_wantSlots{false};

constexpr double kPressS = 0.07;         // the attack held this long: past one of the game's polls
constexpr ULONGLONG kLiveMs = 70 + 450;  // the press, and the ticks the game takes to read it and aim

throwgesture::Params HoldParams() {
    throwgesture::Params p;
    p.hold = true;
    return p;
}

// What the draw thread keeps between frames (the listener is the only one to touch it).
struct State {
    throwgesture::Detector trig;
    throwgesture::Detector grip{HoldParams()};
    uint32_t session = 0, epoch = 0, weaponType = 0;
    int lastMode = kOff;
    bool controlDown = false, pressing = false, taking = false;
    double releaseAt = 0.0;
    // waiting to learn whether the game asked for the aim of the last press
    bool awaitingRead = false;
    ULONGLONG readBy = 0;
    // what the log last said about the state (once per change)
    bool diagSeen = false, dThrown = false, dPlay = false, dTaking = false, dDirect = false;
    uint32_t dType = 0;
    int dMode = kOff;
    ULONGLONG diagAt = 0, whyAt = 0, hintAt = 0;
    // which command a throw presses: the pad button of the slot that holds the game's Throw, else the A attack
    wheel::Snapshot snap;
    uint32_t snapRev = 0;
    ULONGLONG snapAt = 0;
    int slotSeen = -2;                 // the slot the log last said (-1 none)
    throwcast::Press press;
    throwcast::AnswerWatch watch;
    throwland::Pending pend;           // a potion's press waiting for the model to land (potion_throw_mode 1)
    bool bornThrow = false;            // this throw's potion is born so that it explodes as the model lands (throw_direct): armed when it is pressed
    uint64_t bornLandMs = 0;           // ...and the model lands at this GetTickCount64
    double bornFlightS = 0.0;
    bool hideThrow = false;            // this throw's potion is OURS: the game's own potion missile is not drawn (potion_hide_game): armed when it is pressed
    double hideFlightS = 0.0;          // ...and the model flies this long
} g;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

bool ThrownType(uint32_t type) { return type == D2RVR_TYPE_JAVELIN || type == D2RVR_TYPE_THROWING; }

const char* WeaponName(uint32_t type) { return type < D2RVR_TYPE_COUNT ? kD2RVRWeaponTypeNames[type] : "?"; }

const char* WhyText(throwgesture::Why w) {
    switch (w) {
        case throwgesture::Why::Quick: return "let go within 0.08 s of being taken";
        case throwgesture::Why::NoFastSample: return "the hand never moved at 1.4 - 12 m/s (at rest, or too slow)";
        case throwgesture::Why::Stale: return "the swing ended more than 80 ms before the release";
        case throwgesture::Why::ShortTravel: return "the swing was shorter than 18 cm";
        case throwgesture::Why::Steep: return "the hand went mostly up or down";
        default: return "?";
    }
}

// The weight model's account of the last released throw: taken once it is valid and belongs to the throw (throwsrc::FromWeight), then kept for that
// throw. False when no source is registered, it is not valid, or it belongs to another moment - the hand's own estimate stays.
bool WeightChoice(throwsrc::Choice* out) {
    const unsigned seq = g_seq.load();
    {
        std::lock_guard<std::mutex> lk(g_wMtx);
        if (g_w.ok && g_w.seq == seq) { *out = g_w.c; return true; }
    }
    const WeightThrowFn fn = g_weightFn.load();
    if (!fn || seq == 0) return false;
    throwsrc::Outcome o;
    throwsrc::Choice c;
    if (!fn(&o) || !throwsrc::FromWeight(o, (uint64_t)g_lastThrow.load(), &c)) return false;
    std::lock_guard<std::mutex> lk(g_wMtx);
    g_w.seq = seq;
    g_w.ok = true;
    g_w.c = c;
    *out = c;
    return true;
}

// The skill table's newest snapshot (the wheel's reads: the controller's slots), at most every 250 ms unless forced.
void RefreshSlots(bool force) {
    const ULONGLONG now = GetTickCount64();
    if (!force && now - g.snapAt < 250) return;
    g.snapAt = now;
    wheelgame::Latest(&g.snap, &g.snapRev);
}

void LetGoOfAttack() {
    if (!g.pressing) return;
    directinput::Action(D2RVR_ACT_ATTACK, false, directinput::kSrcJavelin);
    g.pressing = false;
    g_pressIsThrow.store(false);
}

void Reset() {
    g.trig.Cancel();
    g.grip.Cancel();
    g.controlDown = false;
    g.taking = false;
    g_gripOwns.store(false);
    g_trigOwns.store(false);
    g_gripDown.store(false);
    g_wantSlots.store(false);
    g.press.Stop();
    g.pend.Cancel();
    g.bornThrow = false;
    g.hideThrow = false;
    LetGoOfAttack();
}

// The state the log last said, once per change (at most 6 a second).
void Diag(uint32_t type, int mode, bool thrown, bool play, bool taking, bool direct, uint32_t flags) {
    if (g.diagSeen && g.dType == type && g.dMode == mode && g.dThrown == thrown && g.dPlay == play && g.dTaking == taking && g.dDirect == direct) return;
    const ULONGLONG now = GetTickCount64();
    if (g.diagSeen && now - g.diagAt < 150) return;   // said on a later frame
    g.diagSeen = true;
    g.diagAt = now;
    g.dType = type; g.dMode = mode; g.dThrown = thrown; g.dPlay = play; g.dTaking = taking; g.dDirect = direct;
    const char* why = "";
    if (!play) {
        why = !(flags & directinput::kFocused) ? " (no input focus)" :
              (flags & directinput::kInMenu) ? " (a menu or panel is open)" :
              (flags & directinput::kOtherView) || !viewgate::F4() ? " (not VR first person, F4)" :
              " (the game's menu is open)";
    }
    char m[360];
    snprintf(m, sizeof m, "throw gesture: weapon '%s' (type %u) - %s | play %s%s | control: %s, %s%s",
             WeaponName(type), type, thrown ? "a thrown weapon" : "NOT a thrown weapon", play ? "on" : "OFF", why,
             mode == kGrip ? "the grip" : mode == kTrigger ? "the trigger" : "none", taking ? "taken from the layout" : "NOT taken",
             g_mode.load() == kGrip && !direct ? " - throw_by_grip needs [openxr] direct_input=1: it does nothing" : "");
    Log(m);
}

// Says once per change which pad button holds the game's Throw skill (what a throw presses), when a thrown weapon is in hand.
void DiagSlot(bool thrown) {
    if (!thrown || (g_autoSkill && g_autoSkill())) { g.slotSeen = -2; return; }   // (with the swap hook on no skill slot is needed)
    const throwcast::Pick p = throwcast::FindSlot(g.snap, GetTickCount64());
    if (p.slot == g.slotSeen) return;
    if (g.slotSeen == -2 && !g.snap.ready && GetTickCount64() - g.snapAt < 1500) return;   // the table has not been read yet
    g.slotSeen = p.slot;
    char m[400];
    if (p.slot >= 0)
        snprintf(m, sizeof m, "throw gesture: the game's Throw skill is on the pad's %s button: a throw presses it (a cast at a spot, no monster needed)", p.label);
    else
        snprintf(m, sizeof m, "throw gesture: the game's Throw skill is on NO pad skill button%s: put it on one (the skill wheel, or the game's own skill panel) - "
                 "the A attack (skill 0) cannot throw a missile potion at all and, for a knife, axe or javelin, needs an enemy at the point",
                 g.snap.ready ? "" : " (the controller's skill table has not been read - is the skill wheel's read running?)");
    Log(m);
}

// The game's own throw, pressed: WHICH of its commands - its Throw skill (a cast at a spot) by the pad button of the slot that holds it; failing that
// the A attack - which the game refuses a missile potion altogether, and gives a knife, axe or javelin only for an enemy at the point (see throw_cast.h).
// The aim of the press is live from here (the aim hooks read the throw's heading and point for the next 0.5 s).
void FirePress(const directinput::Frame& f, uint32_t type, bool potion, bool late) {
    g_read.store(false);
    g_liveUntil.store(GetTickCount64() + kLiveMs);
    g.awaitingRead = true;
    g.readBy = GetTickCount64() + kLiveMs;
    RefreshSlots(true);
    const throwcast::Pick pick = throwcast::FindSlot(g.snap, GetTickCount64());
    const uint32_t code = gamestate::HandCode((int)gamestate::WeaponHand());
    char item[8];
    for (int i = 0; i < 4; ++i) item[i] = (char)((code >> (8 * i)) & 0xFF) ? (char)((code >> (8 * i)) & 0xFF) : ' ';
    item[4] = 0;
    const char* route;
    bool pressed = true;
    if (g_autoSkill && g_autoSkill()) {   // the A attack; the hook of the slot's skill read makes the game cast Throw (throw_auto_skill)
        g_pressIsThrow.store(true);
        directinput::Action(D2RVR_ACT_ATTACK, true, directinput::kSrcJavelin);
        g.pressing = true;
        g.releaseAt = f.time + kPressS;
        route = "the A attack, read by the game as its Throw skill (throw_auto_skill: no skill slot needed)";
    } else if (pick.slot >= 0) {
        g.press.Start(f.time, pick);
        route = "the game's Throw skill, by the pad's skill button";
    } else if (potion) {
        pressed = false;   // Attack excludes missile potions: the press would only make the hero say he cannot
        route = "NOTHING - a throwing potion is thrown by the Throw skill, which is on no pad skill button (the A attack cannot throw it)";
    } else {
        g_pressIsThrow.store(true);
        directinput::Action(D2RVR_ACT_ATTACK, true, directinput::kSrcJavelin);
        g.pressing = true;
        g.releaseAt = f.time + kPressS;
        route = "the A attack (Throw is on no pad skill button: the game takes the attack only for an enemy at the point)";
    }
    if (pressed) {
        g_shotPressS.store(SteadyS());
        g_shotCount.fetch_add(1);
        uint32_t m = 0;
        const bool ok = g_heroMode && g_heroMode(&m);
        g.watch.Arm(f.time, ok, m);
        if (potion && g.bornThrow) {   // throw_direct: the game's potion explodes as the model lands (vr/throw_born.h); the window waits for its create
            if (throwborn::Arm(GetTickCount64(), g.bornLandMs, g.bornFlightS)) Log("throw direct: armed - the next potion create of the hero is timed to the model's landing");
            g.bornThrow = false;
        }
        if (potion && g.hideThrow) {   // potion_hide_game: OUR potion flies, the game's own potion missile of this throw is not drawn (vr/potion_hide.h)
            if (potionhide::Arm(GetTickCount64(), g.hideFlightS)) Log("potion hide: armed - the game's potion missile of this throw is not drawn, our potion is the one that flies");
            g.hideThrow = false;
        }
    }
    char b[460];
    snprintf(b, sizeof b, "throw gesture: %s item '%s' (%s) - pressed %s%s%s%s", WeaponName(type), item, potion ? "a throwing potion" : "thrown weapon", route,
             pick.slot >= 0 ? ": " : "", pick.slot >= 0 ? pick.label : "", late ? " - AT THE MODEL'S LANDING (potion_throw_mode 1)" : "");
    Log(b);
}

void OnFrame(const directinput::Frame& f, void*) {
    throwborn::Install();   // [hands] throw_direct: the create hook goes in once the game's code is there (cheap, tried now and then)
    potionhide::Install();  // [hands] potion_hide_game: the renderer's two functions are hooked, and a summary is told after each armed throw (cheap)
    if (g.pressing && f.time >= g.releaseAt) LetGoOfAttack();
    if (g.press.Active()) {   // the pad button of the Throw slot: pulsed into the game's pad poll every frame, as the skill wheel does
        const throwcast::Press::Out o = g.press.Step(f.time);
        if (o.active) directinput::PadPulse(directinput::PadPress{o.buttons, o.lt, 0}, 80);
    }
    if (g.watch.Armed()) {   // what the game answered to the press: the hero's mode after it
        uint32_t m = 0;
        const bool ok = g_heroMode && g_heroMode(&m);
        const throwcast::AnswerWatch::Event e = g.watch.Poll(f.time, ok, m);
        char b[300];
        if (e.kind == throwcast::AnswerWatch::Kind::Accepted) {
            snprintf(b, sizeof b, "throw gesture: the game ACCEPTED the press - the hero went into '%s' (mode %u) %.0f ms after it (he was in '%s')",
                     throwcast::ModeName(e.mode), e.mode, e.afterS * 1000.0, throwcast::ModeName(e.before));
            Log(b);
        } else if (e.kind == throwcast::AnswerWatch::Kind::NoAnswer) {
            snprintf(b, sizeof b, "throw gesture: the game did NOT take the press - the hero stayed in '%s' (mode %u, was '%s') for %.1f s: refused (the skill's weapon check, "
                     "no enemy at the point) or not read", throwcast::ModeName(e.mode), e.mode, throwcast::ModeName(e.before), e.afterS);
            Log(b);
        } else if (e.kind == throwcast::AnswerWatch::Kind::Unreadable) {
            Log("throw gesture: the hero's mode could not be read - what the game answered to the press is not known");
        }
    }
    if (g.awaitingRead && GetTickCount64() >= g.readBy) {   // the press's window is over: did the game ask for the aim?
        g.awaitingRead = false;
        if (!g_read.load())
            Log("throw gesture: the game never asked for the aim of the throw's press within 0.5 s - the attack did not start through "
                "the controller's aim (a menu, the hero busy, or the press was not read)");
    }
    const int want = g_mode.load();
    if (want == kOff) {
        g.pend.Cancel();
        if (g.taking || g.trig.Throwing() || g.grip.Throwing() || g.pressing || g.diagSeen) { Reset(); g.diagSeen = false; }
        return;
    }
    const uint32_t type = gamestate::WeaponType();
    const bool thrown = ThrownType(type);
    // F4 only (vr/view_gate.h): another view is no play - nothing is taken, a stroke is dropped, the press and the throw's aim let go
    const bool f4 = viewgate::F4() && !(f.flags & directinput::kOtherView);
    const bool play = (f.flags & directinput::kFocused) && !(f.flags & directinput::kInMenu) && f4 && !gamestate::MenuOpen();
    if (!f4) g_liveUntil.store(0);   // (a throw's aim does not outlive the view)
    // the grip mode is the plugin's own input: it needs direct_input to be feeding the game (the grip is taken from its layout)
    const bool direct = (f.flags & directinput::kDirect) != 0;
    const int mode = want == kGrip && !direct ? kOff : want;
    if (mode != g.lastMode) {   // the other mode's stroke is dropped; nothing is held over
        g.trig.Cancel();
        g.grip.Cancel();
        g.controlDown = false;
        g.lastMode = mode;
    }
    // anything that changes what the player holds, where he is or what the controls mean drops a stroke
    const bool changed = f.session != g.session || f.epoch != g.epoch || type != g.weaponType;
    g.session = f.session;
    g.epoch = f.epoch;
    g.weaponType = type;
    if (g.pend.Active()) {   // potion_throw_mode 1: the model's flight is over - the game's throw now; a change of play, weapon or view drops it
        const bool stillOk = mode != kOff && thrown && play && !changed;
        const throwland::Pending::Act pa = g.pend.Step(f.time, stillOk);
        if (pa == throwland::Pending::Act::Fire) FirePress(f, type, true, true);
        else if (pa == throwland::Pending::Act::Dropped)
            Log("throw gesture: potion_throw_mode 1 - the potion's throw was dropped before the model landed (the play, the weapon or the view changed, or too long)");
    }

    const directinput::Hand& h = f.hand[1];   // a javelin is thrown from the right hand, as throw_aim has it
    const bool byGrip = mode == kGrip;
    const float value = byGrip ? h.squeeze : h.trigger;
    const float down = byGrip ? 0.60f : 0.55f, up = byGrip ? 0.40f : 0.20f;
    g.controlDown = value > (g.controlDown ? up : down);   // a squeeze wavering does not let go
    const bool take = mode != kOff && thrown && play;
    if (take) {
        if (byGrip) directinput::Take(1, directinput::kSqueeze | directinput::kTakeSqueeze, directinput::kSrcJavelin);
        else directinput::Take(1, directinput::kTrigger | directinput::kTakeTrigger, directinput::kSrcJavelin);
    }
    g.taking = take;
    g_gripOwns.store(take && byGrip);
    g_trigOwns.store(take && !byGrip);
    g_gripDown.store(take && byGrip && g.controlDown);   // (f is the frame as the controllers made it, before the Take above hid the grip from the shared block)
    g_wantSlots.store(take);   // (the controller's skill slots are wanted while a throw could come)
    if (take) { RefreshSlots(false); DiagSlot(thrown); } else g.slotSeen = -2;
    Diag(type, mode, thrown, play, take, direct, f.flags);

    if (mode == kOff) {
        g.trig.Cancel();
        g.grip.Cancel();
        return;
    }
    throwgesture::Sample s;
    s.t = f.time;
    s.hand = {h.grip.pos[0], h.grip.pos[1], h.grip.pos[2]};
    s.tracked = h.active != 0 && h.grip.valid == 1;
    s.trigger = g.controlDown;
    s.cancel = !thrown || !play || changed;
    const throwgesture::Result r = (byGrip ? g.grip : g.trig).Step(s);
    if (r.released && !r.throwNow && GetTickCount64() - g.whyAt > 150) {
        g.whyAt = GetTickCount64();
        char m[220];
        if (r.sinceFast >= 0.0)
            snprintf(m, sizeof m, "throw gesture: %s let go, not a throw - %s (held %.2f s, swing %.0f cm, last fast %.0f ms before)",
                     byGrip ? "the grip" : "the trigger", WhyText(r.why), r.held, r.travel * 100.0f, r.sinceFast * 1000.0);
        else
            snprintf(m, sizeof m, "throw gesture: %s let go, not a throw - %s (held %.2f s, swing %.0f cm)",
                     byGrip ? "the grip" : "the trigger", WhyText(r.why), r.held, r.travel * 100.0f);
        Log(m);
    }
    if (!r.throwNow) return;
    if (GetTickCount64() < g_holdOffUntil.load()) {   // hit by touch already threw this swing at the monster its blade met: one throw per swing
        // (still a THROW's release to whoever asks - the weapon-weight logic: the weapon leaves the hand with the game's throw, it is not put down;
        // nothing is pressed or aimed here)
        g_lastThrow.store(GetTickCount64());
        Log("throw gesture: a throw released, dropped - hit by touch pressed the game's attack at a monster in this swing (melee_touch_thrown): that was the throw");
        return;
    }
    if (g.pend.Active()) {   // the last potion is still in the air (potion_throw_mode 1): one throw at a time
        Log("throw gesture: a throw released while the last potion's flight is not over (potion_throw_mode 1) - ignored");
        return;
    }

    float fx, fz;
    if (!f.head.valid || !throwgesture::HeadGroundForward(f.head.rot, &fx, &fz) || !g_canAim || !g_canAim()) {
        Log("throw gesture: a throw released, but there is no heading to make of it (head pose or camera frame missing) - dropped");
        return;
    }
    // the throw's way on the ground, in the head's frame: right of the head, forward of it
    float right, fwd;
    throwgesture::ToHeadFrame(r.groundX, r.groundZ, fx, fz, &right, &fwd);
    g_right.store(right);
    g_fwd.store(fwd);
    g_speed.store(r.peakSpeed);
    g_horiz.store(std::sqrt(r.peak.x * r.peak.x + r.peak.z * r.peak.z));
    g_vert.store(r.peak.y);
    g_relSpeed.store(r.speed);
    g_below.store(f.head.pos[1] - h.grip.pos[1]);
    g_seq.fetch_add(1);
    g_lastThrow.store(GetTickCount64());
    g_shotReleaseS.store(SteadyS());
    throwsrc::Choice wc;
    const bool haveWeight = WeightChoice(&wc);   // the weight model's account of this throw (it has it by now: it is worked out on every step while the weapon is held)
    if (haveWeight) {
        char m[260];
        snprintf(m, sizeof m, "throw gesture: the weight model has the throw - heading %.0f deg to the right of the head's way (%s), lands %.1f m out, flight %.2f s, %+.0f deg up%s",
                 std::atan2(wc.right, wc.forward) * 57.2957795f, wc.headingFromVelocity ? "from its velocity" : "from its landing", wc.landingM, wc.flightS, wc.elevationDeg,
                 wc.capped ? " (its cell cap held it)" : "");
        Log(m);
    }
    const bool potion = gamestate::HandIsThrowPotion((int)gamestate::WeaponHand());
    throwland::Plan plan = throwland::Decide(g_potionMode.load(), potion, haveWeight, haveWeight ? (double)wc.flightS : 0.0);
    g.bornThrow = false;
    g.hideThrow = false;
    if (potion && potionhide::Wanted()) {   // potion_hide_game: the game's own potion is not drawn - only when OUR potion flies, i.e. the weight model has the throw
        if (haveWeight && std::isfinite((double)wc.flightS) && potionhide::Ready()) {
            g.hideThrow = true;
            g.hideFlightS = (double)wc.flightS;
        } else {
            Log(haveWeight ? "potion hide: not armed - the renderer's hooks are not in (see above); the game's own potion is drawn as ever"
                           : "potion hide: not armed - the weight model has no account of this throw, so no model of ours flies; the game's own potion is drawn as ever");
        }
    }
    if (potion && haveWeight && std::isfinite((double)wc.flightS) && throwborn::Ready()) {   // [hands] throw_direct: the game's throw at the release; its potion is timed to the landing
        plan.deferred = false;
        plan.delayS = 0.0;
        plan.why = "throw_direct: the game's throw is pressed at the release, its potion is born so that it explodes as the model lands";
        g.bornThrow = true;
        g.bornFlightS = (double)wc.flightS;
        g.bornLandMs = GetTickCount64() + (uint64_t)std::llround(std::clamp((double)wc.flightS, 0.0, 10.0) * 1000.0);
    }
    if (plan.deferred) {   // potion_throw_mode 1: the model flies; the game's throw is pressed when it lands, aimed at the landing (the aim is live from that press)
        g_liveUntil.store(0);
        g.pend.Arm(f.time, plan.delayS, g_seq.load());
        char m[300];
        snprintf(m, sizeof m, "throw gesture: potion_throw_mode 1 - the potion's model flies %.2f s; the game's Throw is pressed when it lands %.1f m out",
                 plan.delayS, wc.landingM);
        Log(m);
    } else {
        if (g.bornThrow) {
            char m[300];
            snprintf(m, sizeof m, "throw gesture: throw_direct - the game's Throw is pressed now; the model lands %.1f m out after %.2f s and the game's potion is timed to explode there",
                     wc.landingM, wc.flightS);
            Log(m);
        } else if (g_potionMode.load() == throwland::kAtLanding && potion) {
            char m[300];
            snprintf(m, sizeof m, "throw gesture: potion_throw_mode 1 is on, but %s", plan.why);
            Log(m);
        }
        FirePress(f, type, potion, false);
    }

    static ULONGLONG told = 0;
    if (GetTickCount64() - told > 500) {
        told = GetTickCount64();
        char m[220];
        snprintf(m, sizeof m, "throw gesture: thrown (%s) - hand %.1f m/s over %.2f m, held %.2f s, %.0f deg to the right of the head's way (up %+.0f deg)",
                 byGrip ? "grip" : "trigger", r.speed, r.travel, r.held, std::atan2(right, fwd) * 57.2957795f,
                 std::asin(std::fmax(-1.0f, std::fmin(1.0f, r.dir.y))) * 57.2957795f);
        Log(m);
    }
}

}  // namespace

const char* ModeText(int mode) {
    return mode == kGrip ? "throw gesture: on, by the GRIP ([hands] throw_by_grip) - hold the right grip with a throwing weapon in the hand, "
                           "swing, let go at the end of the swing: the game's own throw"
           : mode == kTrigger ? "throw gesture: on, by the trigger - hold the right trigger, swing, let go: the game's own throw"
                              : "throw gesture: off";
}

void SetHeroModeFn(bool (*fn)(uint32_t* mode)) { g_heroMode = fn; }

void SetAutoSkillFn(bool (*fn)()) { g_autoSkill = fn; }

void SetPotionMode(int mode) { g_potionMode.store(mode == throwland::kAtLanding ? throwland::kAtLanding : throwland::kNow); }

void SetWeightSource(WeightThrowFn fn) { g_weightFn.store(fn); }

bool WantsSlots() { return g_wantSlots.load(); }

bool PressIsThrow() { return g_pressIsThrow.load(); }

void Init(const D2RL::PluginContext* ctx, bool (*canAim)()) {
    g_ctx = ctx;
    g_canAim = canAim;
    throwborn::Init(ctx);
    potionhide::Init(ctx);
    // (the ini is read before the log is there: the mode set then is told now)
    if (g_mode.load() != kOff) Log(ModeText(g_mode.load()));
}

void SetModes(bool byTrigger, bool byGrip) {
    const int mode = byGrip ? kGrip : byTrigger ? kTrigger : kOff;
    const int was = g_mode.exchange(mode);
    if (mode != kOff && !g_listening.exchange(true)) {
        if (!directinput::Listen(&OnFrame, nullptr)) {
            g_listening.store(false);
            Log("throw gesture: the headset frames could not be listened to (the listener table is full) - the gesture does nothing");
        }
    }
    if (mode != was) Log(ModeText(mode));
    if (mode == kOff) {
        g_liveUntil.store(0);
        g_gripOwns.store(false);
        g_trigOwns.store(false);
        g_gripDown.store(false);
    }
}

bool Aim(float* right, float* forward) {
    if (g_mode.load() == kOff || !viewgate::F4() || GetTickCount64() >= g_liveUntil.load()) return false;
    throwsrc::Choice wc;
    if (WeightChoice(&wc)) {   // the weight model's heading for this throw
        *right = wc.right;
        *forward = wc.forward;
        return true;
    }
    *right = g_right.load();
    *forward = g_fwd.load();
    return true;
}

void NoteRead() { g_read.store(true); }

bool Info(ThrowInfo* out) {
    float r, f;
    if (!out || !Aim(&r, &f)) return false;
    out->right = r;
    out->forward = f;
    out->speed = g_speed.load();
    out->horizSpeed = g_horiz.load();
    out->vertSpeed = g_vert.load();
    out->releaseSpeed = g_relSpeed.load();
    out->handBelowHeadM = g_below.load();
    out->seq = g_seq.load();
    throwsrc::Choice wc;
    out->fromWeight = WeightChoice(&wc);
    out->landingM = out->fromWeight ? wc.landingM : 0.0f;
    out->weightSpeed = out->fromWeight ? wc.speed : 0.0f;
    out->flightS = out->fromWeight ? wc.flightS : 0.0f;
    out->elevationDeg = out->fromWeight ? wc.elevationDeg : 0.0f;
    out->weightCapped = out->fromWeight && wc.capped;
    out->headingFromVelocity = out->fromWeight && wc.headingFromVelocity;
    return true;
}

bool GripIsThrows() { return g_gripOwns.load() && viewgate::F4(); }

bool TriggerIsThrows() { return g_trigOwns.load() && viewgate::F4(); }

bool GripHeld() { return g_gripOwns.load() && g_gripDown.load() && viewgate::F4(); }

uint64_t LastThrowTickMs() { return g_lastThrow.load(); }
unsigned ShotCount() { return g_shotCount.load(); }
void ShotTimes(double* releaseS, double* pressS) {
    if (releaseS) *releaseS = g_shotReleaseS.load();
    if (pressS) *pressS = g_shotPressS.load();
}

void HoldOffRelease(uint64_t untilTickMs) { g_holdOffUntil.store(untilTickMs); }

}  // namespace throwwire
