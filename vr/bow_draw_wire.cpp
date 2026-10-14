// bow_draw_wire.cpp - the two-hand bow draw: [hands] bow_draw.
//
// A bow in one hand, the string taken with the trigger (or the grip: [hands] bow_draw_start) of the other, pulled back along
// the line to the bow and let go: ONE short press of the game's own attack (or the pad button of a skill), aimed along the draw
// line. The detector is bow_draw.h (pure, with a test on synthetic poses); here it is fed from direct_input.h's per-frame
// hands - native OpenXR, with the plugin's own controls ([openxr] direct_input) or with BodyWalk's - and what it decides goes
// out through the game's own buttons: the attack (direct_input's Action) or a skill slot's pad button (PadPulse), 65 ms, as a
// thumb would press it. The game does the rest - its animation, its attack rate, its mana and its arrows. The pull adds
// nothing: no damage, no extra arrows, no speed, no change to the missile. Nothing here shoots anything itself.
//
// The aim: the press resolves its point through vrcam's aim hooks (HookSpellResolve, HookAttackPoint, HookAttackTarget,
// HookUnitFacing), which ask AimLive() / AimDir() for the line for 0.4 s after the release - the point is whole cells out
// along it, inside the game's 50-cell command check as the aim-range work sets it.
//
// The control: while the string is held the control that took it (the trigger or the grip) is the draw's - direct_input's Take
// keeps it from the layout (so no attack starts at the nock), and Owned() keeps BodyWalk's pad and attack action from vrcam's pad
// hook. A control pressed away from the string is the plain one, as before. Unticked: nothing here does anything.
//
// The touch point: the string of the posed bow, read by skeletons.cpp off the bow's own rig (skel::BowString), when it reads
// sanely (bow_touch.h ChooseNock); else 16 cm behind the grip along the bow.
//
// The arrow is the game's own, never a model of ours: the nocked one is the bow's own arrow bone, posed on the string by
// vr/bow_rig.h ([bow] rig_pose, fed below), and the flying one is the missile the game makes of the attack or skill pressed
// here. Nothing in this file or in bow_draw.h hides, replaces or draws an arrow, and none reaches into a missile, its speed,
// damage or the ammunition.
//
// Idea and numbers: bearstonem/d2r-pcvr (MIT), steamvr/gameplay.h and weapon_skills.h; written afresh.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "bow_draw.h"
#include "bow_draw_wire.h"
#include "bow_touch.h"
#include "d2r_vr_shared.h"
#include "d2r_vr_state.h"
#include "direct_input.h"
#if D2RVR_FIRST_PERSON && __has_include("bow_rig.h")
#include "skeletons.h"   // bow-rig-pose's feed (skel::SetBowDraw) and the bow's string (skel::BowString)
#define BOWDRAW_RIG_FEED 1
#if __has_include("bow_layouts.h")
#define BOWDRAW_RIG_CANCEL 1   // bow-rigs: skel::BowDraw::cancelled, a pull that ended with no shot (present once that port is merged)
#endif
#endif

namespace gamestate { uint32_t WeaponClass(); uint32_t WeaponType(); uint32_t WeaponSet(); uint32_t HandsKey(); bool MenuOpen(); }

namespace bowdrawwire {
namespace {

constexpr ULONGLONG kFreshMs = 250;   // a state older than this is stale: no frames are coming
constexpr uint32_t kPressMs = 65;     // a press the game never misses (directinput::PadPulse)

// What the release presses, by the ini's number. 0 is the game's attack (pad A, never picking up: the weapon's own shot);
// the others are the pad buttons of the controller's skill slots - the skill on them is whatever the player put there.
struct Button { uint16_t buttons; uint8_t lt; const char* name; };
constexpr Button kButtons[] = {
    {0, 0, "the attack"},
    {directinput::pad::kB, 0, "B"}, {directinput::pad::kX, 0, "X"}, {directinput::pad::kY, 0, "Y"}, {directinput::pad::kRightBumper, 0, "RB"},
    {directinput::pad::kB, 255, "LT + B"}, {directinput::pad::kX, 255, "LT + X"}, {directinput::pad::kY, 255, "LT + Y"},
    {directinput::pad::kRightBumper, 255, "LT + RB"},
};
constexpr int kButtonCount = (int)(sizeof kButtons / sizeof kButtons[0]);

const D2RL::PluginContext* g_ctx = nullptr;
HandFn g_bowHand = nullptr;
ViewFn g_f4 = nullptr;
std::atomic<bool> g_on{false}, g_model{true}, g_debug{false};
std::atomic<unsigned> g_shotCount{0};      // [debug] bow_latency: the releases so far, and when the last one was and when its button was pressed
std::atomic<double> g_shotReleaseS{0.0}, g_shotPressS{0.0};   // (std::chrono::steady_clock seconds: throwborn::NowS()'s base)
std::atomic<bool> g_only{false};   // [hands] bow_draw_only: a press away from the string shoots nothing (in the zone the trigger is the draw's, away its shots are dropped)
std::atomic<float> g_reach{0.28f};   // [hands] bow_draw_reach_cm: how near the string the free hand has to be (m)
std::atomic<float> g_shift{0.0f};    // [hands] bow_draw_nock_shift_cm: the zone's centre along the bow's axis (m), + away from the grip
std::atomic<float> g_minPull{0.12f}; // [hands] bow_draw_min_pull_cm: how far the string hand is pulled back from the nock for a release to shoot (m)
std::atomic<bool> g_restAims{true};  // [bow] arrow_rest_aims: the shot's line goes through the arrow's rest point too (skel::BowRestPoint), as the arrow lies
constexpr float kNockBehind = 0.16f; // the constant touch point: this far behind the grip along the bow
std::atomic<int> g_button{0}, g_start{0};
std::atomic<bool> g_recentre{false};
std::atomic<ULONGLONG> g_ownedAt{0};
std::atomic<ULONGLONG> g_keepAt{0};       // the string hand's trigger is kept from the layout (the draw's, or bow_draw_only's), when
std::atomic<int> g_keepHand{-1};          // ... and whose (direct_input's: 0 left, 1 right)
std::atomic<ULONGLONG> g_gripAt{0};       // the string hand's grip is the draw's, when
std::atomic<int> g_gripHand{-1};          // ... and whose (vrcam's: 0 right, 1 left)

// F4 only (vrcam's FullBody()): everywhere else nothing of the bow draw exists. No view function = no F4.
bool F4() { return g_f4 && g_f4(); }
// The box is ticked and this is F4: what every answer to the rest of the plugin is gated by.
bool Active() { return g_on.load() && F4(); }

SRWLOCK g_lock = SRWLOCK_INIT;
struct Aim {
    bool live = false;
    bowdraw::Vec room;   // the line, in the room's frame (OpenXR: x right, y up, z back)
    float yaw = 0.0f;    // the head's, now: the line is turned into the head's turn frame with it
    ULONGLONG at = 0;
} g_aim;
bowtouch::VectorSet g_vectors;   // [debug] bow_vectors: the last frame's, in the hands' frame
ULONGLONG g_vectorsAt = 0;

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

// The draw thread's (OnFrame) alone.
struct Run {
    bowdraw::Detector det;
    uint32_t session = 0, epoch = 0;
    bool seen = false, pressHeld = false;
    int told = 0;
    double dbgAt = 0.0;
    double lostAt = 0.0;     // a hand is lost and the draw held since then (frame time), 0 = not
    bool pullEnded = false;  // the last pull ended with no shot and no new nock yet (the rig's "cancelled")
    const char* event = "";  // what the last cancel event was (a recentre, the focus/a menu/the session changing)
};

bowdraw::Vec V(const float p[3]) { return {p[0], p[1], p[2]}; }
bowdraw::Quat Q(const float q[4]) { return {q[0], q[1], q[2], q[3]}; }

double SteadyS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void Publish(bool live, const bowdraw::Vec& room, bool haveYaw, float yaw) {
    AcquireSRWLockExclusive(&g_lock);
    g_aim.live = live;
    if (live) g_aim.room = room;
    if (haveYaw) g_aim.yaw = yaw;
    g_aim.at = GetTickCount64();
    ReleaseSRWLockExclusive(&g_lock);
}

void LetGo(Run& r) {
    if (r.pressHeld) directinput::Action(D2RVR_ACT_ATTACK, false, directinput::kSrcBowDraw);
    r.pressHeld = false;
    Publish(false, {}, false, 0.0f);
    g_ownedAt.store(0);
    g_gripAt.store(0);
    g_keepAt.store(0);
    AcquireSRWLockExclusive(&g_lock);
    g_vectorsAt = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

void Say(Run& r, const char* text) {
    if (r.told < 400) { ++r.told; Log(text); }
}

void OnFrame(const directinput::Frame& f, void*) {
    static Run r;
    if (!Active()) {   // unticked, or any view but F4: nothing is held, nothing is taken, the detector forgets a draw under way
        if (r.seen) {
            LetGo(r);
            const int told = r.told;
            r = Run{};
            r.told = told;
        }
        return;
    }
    if (!r.seen || f.session != r.session) {   // a new native session: forget everything
        LetGo(r);
        const int told = r.told;
        r = Run{};
        r.told = told;
        r.seen = true;
        r.session = f.session;
        r.epoch = f.epoch;
    }
    bool cancel = g_recentre.exchange(false);
    if (cancel) r.event = "a recentre";
    if (f.epoch != r.epoch) { r.epoch = f.epoch; cancel = true; r.event = "the input focus, a menu or the session changed"; }   // a draw under way ends

    const int bowVr = g_bowHand ? g_bowHand() : 1;   // vrcam's: 0 right, 1 left
    const int bowX = bowVr ? 0 : 1;                  // direct_input's: 0 left, 1 right
    const int strX = 1 - bowX;
    const directinput::Hand& bow = f.hand[bowX];
    const directinput::Hand& str = f.hand[strX];
    const bool focused = (f.flags & directinput::kFocused) != 0, menu = (f.flags & directinput::kInMenu) != 0;
    const bool bowInHand = gamestate::WeaponClass() == D2RVR_WEAPON_BOW, gameMenu = gamestate::MenuOpen();
    const int start = bowdraw::ClampStart(g_start.load());
    const bool debug = g_debug.load();

    bowdraw::Input in;
    in.time = f.time;
    // (a panel of the plugin's own up - the skill wheel, the settings panel - is a menu to the draw: its clicks are not a nock)
    const bool panelUp = (f.flags & directinput::kUiUp) != 0;
    in.allowed = focused && !menu && !panelUp && bowInHand && !gameMenu;
    in.valid = f.head.valid != 0 && bow.active && str.active && bow.grip.valid == 1 && str.grip.valid == 1;
    in.cancel = cancel;
    in.bowHand = V(bow.grip.pos);
    in.bowRot = Q(bow.grip.rot);
    in.stringHand = V(str.grip.pos);
    in.trigger = str.trigger;
    in.grip = str.squeeze;
    // a change of weapon, set, hands or bow hand under the string ends the draw
    in.context = ((uint64_t)gamestate::WeaponSet() << 56) ^ ((uint64_t)gamestate::WeaponType() << 48) ^ ((uint64_t)bowVr << 44) ^
                 gamestate::HandsKey();

    // The touch point: where the bow's own string rests, when the skeleton hook read it and it is plausible; else the constant.
    const bool haveHead = f.head.valid != 0;
    const float yaw = haveHead ? bowdraw::HeadYaw(Q(f.head.rot)) : 0.0f;
    const bowdraw::Vec head = V(f.head.pos);
    const bowdraw::Vec forward = bowdraw::BowForward(in.bowRot);
    bowtouch::NockChoice nock{};
    bool haveModel = false;
    bowdraw::Vec modelRoom;
#ifdef BOWDRAW_RIG_FEED
    if (haveHead && (g_model.load() || debug)) {
        float p[3];
        if (skel::BowString(p)) { haveModel = true; modelRoom = bowtouch::HandsToRoom(V(p), head, yaw); }
    }
#endif
    // [hands] bow_draw_nock_shift_cm moves the zone's centre along the bow's axis, for the bow's string and the constant alike.
    const float shift = g_shift.load();
    const bowtouch::NockChoice read = bowtouch::ChooseNock(in.bowHand, forward, haveModel, modelRoom, kNockBehind, 0.05f, 0.45f, shift);
    nock = g_model.load() && read.source == bowtouch::NockSource::Model ? read
         : bowtouch::ChooseNock(in.bowHand, forward, false, {}, kNockBehind, 0.05f, 0.45f, shift);
    nock.modelDist = read.modelDist;
    nock.modelSane = read.modelSane;   // (what was read, for the log, whether it is used or not)
    in.hasNock = true;
    in.nock = nock.point;

    bowdraw::Params params;
    params.start = start;
    params.reach = g_reach.load();
    params.minPull = g_minPull.load();
    params.nockBehind = kNockBehind + shift;   // (the constant, as the detector would place it: the debug view's)
    // [bow] arrow_rest_aims: the arrow lies on the line from the string hand through its rest point on the bow hand (skel::BowRestPoint: a point
    // fixed in the hand's own frame, [bow] arrow_rest_*) - the shot goes along that line too, so that it is the line the player sees. Only the
    // detector's line (the aim and the pull's length) is taken through it; the touch point above and the debug below use the hand's own point.
    const bowdraw::Vec bowGrip = in.bowHand;
    bowdraw::Vec restRoom;
    bool haveRest = false;
#ifdef BOWDRAW_RIG_FEED
    if (haveHead && (g_restAims.load() || debug)) {
        float p[3];
        if (skel::BowRestPoint(p)) { haveRest = true; restRoom = bowtouch::HandsToRoom(V(p), head, yaw); }
    }
#endif
    if (haveRest && g_restAims.load()) {
        // the detector's line is bowHand - stringHand, and its length is the pull: the hand's own point is turned about the string hand onto
        // the line through the rest point, at the distance it was - the aim is the line through the rest point, the pull is the pull
        // ([bow] string_anchor: the arrow lies on the line from where the string is held - the fingers' first joints - so the line starts there)
        bowdraw::Vec lineFrom = in.stringHand;
#ifdef BOWDRAW_RIG_FEED
        {
            float a[3];
            if (haveHead && skel::StringAnchorHands(a)) lineFrom = bowtouch::HandsToRoom(V(a), head, yaw);
        }
#endif
        const bowdraw::Vec toRest = restRoom - lineFrom;
        const float restLen = bowdraw::Length(toRest), gripLen = bowdraw::Length(bowGrip - in.stringHand);
        if (restLen > 0.02f && gripLen > 0.02f) in.bowHand = in.stringHand + toRest * (gripLen / restLen);
    }
    const bowdraw::Output o = r.det.Step(in, params);
    in.bowHand = bowGrip;
    char b[1024];

#ifdef BOWDRAW_RIG_FEED
    {   // [bow] rig_pose: the game's own bow - string, limbs and its arrow bones - posed by the pull; nothing is drawn over it
        skel::BowDraw d;
        d.drawing = o.drawing;
        d.draw = o.draw;
        d.arrowVisible = true;
        d.stringHand = 1 - bowVr;   // vrcam's: 0 right, 1 left; the hand opposite the bow's
#ifdef BOWDRAW_RIG_CANCEL
        // a pull that ended with nothing shot (a short one let go, or any cancel) until the next nock: the rig eases the bow back to rest
        if (o.evNock || o.shoot) r.pullEnded = false;
        if (o.evCancel || o.evLoose) r.pullEnded = true;
        d.cancelled = r.pullEnded && !o.drawing;
#endif
        skel::SetBowDraw(d);
        // [bow] hand_pose: the string hand's own trigger and squeeze, as the controller has them - read here, from the raw frame, before the
        // draw takes the control from the layout (which then sees it let go): the fingers follow the squeeze, and the string taken hooks them
        skel::SetBowHandControls(1 - bowVr, str.trigger, str.squeeze);
    }
#endif

    // The aim before the press: the game reads its point when it takes the button.
    Publish(o.aimOwned && !o.drawing, o.aim, haveHead, yaw);

    // While the string is held the control that took it is the draw's: nothing bound to it fires. With [hands] bow_draw_only the string
    // hand's trigger is the draw's too while the hand is in the grab zone (a press there is the draw's, whatever the table binds to the
    // trigger), and everywhere else the trigger's and the grip's SHOTS are dropped (the layout leaves out action:attack, pad A and RT from
    // what they are bound to) while every other output they are bound to works. The bow hand and the other weapons (allowed needs a bow in
    // hand) are not touched.
    {
        const bowdraw::Keep keep = bowdraw::KeepFromLayout(in.allowed, g_only.load(), o.owned, o.hold, o.atString, start);
        const uint32_t mask = (keep.trigger ? (directinput::kTrigger | directinput::kTakeTrigger) : 0u) |
                              (keep.grip ? (directinput::kSqueeze | directinput::kTakeSqueeze) : 0u) |
                              (keep.noShotTrigger ? directinput::kNoShotTrigger : 0u) | (keep.noShotGrip ? directinput::kNoShotSqueeze : 0u);
        if (mask) directinput::Take(strX, mask, directinput::kSrcBowDraw);
        // (the skill wheel casts its picked slot on a press of the pointing hand's trigger - a shot too: it stands down while this holds)
        if (keep.trigger || keep.noShotTrigger) { g_keepHand.store(strX); g_keepAt.store(GetTickCount64()); }
        else g_keepAt.store(0);
    }
    if (o.owned) {
        g_ownedAt.store(o.hold == 1 ? 0 : GetTickCount64());   // vrcam's BodyWalk-pad mask is the attack's: the trigger's
        if (o.hold == 1) { g_gripHand.store(strX ? 0 : 1); g_gripAt.store(GetTickCount64()); }
        else g_gripAt.store(0);
    } else {
        g_ownedAt.store(0);
        g_gripAt.store(0);
    }

    if (o.evNear) directinput::Haptic(strX, 0.15f, 15.0f);
    if (o.evNock) {
        directinput::Haptic(strX, 0.30f, 25.0f);
        snprintf(b, sizeof b, "vrcam: bow draw - the string taken with the %s (the %s hand has the bow; touch point: %s)",
                 o.hold == 1 ? "grip" : "trigger", bowVr ? "left" : "right", nock.source == bowtouch::NockSource::Model && g_model.load() ? "the bow's own string" : "16 cm behind the grip");
        Say(r, b);
    }
    if (o.evCharged) {
        directinput::Haptic(strX, 0.45f, 35.0f);
        directinput::Haptic(bowX, 0.20f, 25.0f);
    }
    if (o.lost && r.lostAt == 0.0) {   // a controller dropped out of tracking mid-draw: held for up to params.lossSeconds
        r.lostAt = f.time;
        snprintf(b, sizeof b, "vrcam: bow draw - tracking dropped (hands active: bow %u, string %u; grips placed %d/%d) - the draw is held for up to %.0f ms",
                 bow.active, str.active, bow.grip.valid == 1, str.grip.valid == 1, params.lossSeconds * 1000.0f);
        Say(r, b);
    } else if (!o.lost && r.lostAt != 0.0) {
        if (!o.evCancel) {
            snprintf(b, sizeof b, "vrcam: bow draw - tracking came back after %.0f ms, the draw goes on", (f.time - r.lostAt) * 1000.0);
            Say(r, b);
        }
        r.lostAt = 0.0;
    }
    if (o.evCancel) {
        // WHICH reason ended it, as exactly as this side can tell: a menu, the weapon, tracking, a recentre, a pull too long...
        char why[200];
        switch (o.why) {
            case bowdraw::kWhyNotAllowed:
                snprintf(why, sizeof why, "%s%s%s%s%s", !focused ? "the headset lost the input focus" : "", menu || gameMenu ? "a menu or panel is open" : "",
                         panelUp ? "a panel of the plugin's own is up (the skill wheel, the settings)" : "",
                         !bowInHand ? "no bow in hand any more (the weapon changed)" : "",
                         focused && !menu && !gameMenu && !panelUp && bowInHand ? "not allowed" : "");
                break;
            case bowdraw::kWhyEvent: snprintf(why, sizeof why, "an event: %s", r.event); break;
            case bowdraw::kWhyTracking:
                snprintf(why, sizeof why, "tracking lost for more than %.0f ms (hands active: bow %u, string %u; grips placed %d/%d; head %d)",
                         params.lossSeconds * 1000.0f, bow.active, str.active, bow.grip.valid == 1, str.grip.valid == 1, haveHead);
                break;
            default: snprintf(why, sizeof why, "%s", bowdraw::WhyName(o.why)); break;
        }
        snprintf(b, sizeof b, "vrcam: bow draw - ended with no shot: %s; the control is the draw's until it is let go", why);
        Say(r, b);
        r.lostAt = 0.0;
    }

    if (o.shoot) {
        const int idx = g_button.load();
        const Button& bt = kButtons[idx >= 0 && idx < kButtonCount ? idx : 0];
        snprintf(b, sizeof b, "vrcam: bow draw - released: %s pressed, along the line %.2f %.2f %.2f (room), %.2f m drawn",
                 bt.name, o.aim.x, o.aim.y, o.aim.z, bowdraw::Length(in.bowHand - in.stringHand));
        Say(r, b);
        const double releasedS = SteadyS();
        directinput::Haptic(strX, 0.80f, 45.0f);
        directinput::Haptic(bowX, 0.35f, 30.0f);
        if (bt.buttons == 0) {
            directinput::Action(D2RVR_ACT_ATTACK, true, directinput::kSrcBowDraw);
            r.pressHeld = true;
        } else {
            directinput::PadPulse({bt.buttons, bt.lt, 0}, kPressMs);
        }
        g_shotReleaseS.store(releasedS);
        g_shotPressS.store(SteadyS());
        g_shotCount.fetch_add(1);
    }
    if (r.pressHeld && !o.pressing) {
        directinput::Action(D2RVR_ACT_ATTACK, false, directinput::kSrcBowDraw);
        r.pressHeld = false;
    }

    if (debug) {
        // The vectors in the hands' frame (metres from the head, the head's turn): what the effect draws in the headset.
        if (haveHead) {
            const auto h = [&](bowdraw::Vec room) { return bowtouch::RoomToHands(room, head, yaw); };
            const auto dir = [&](bowdraw::Vec d) { return bowdraw::ToHeadTurn(d, yaw); };
            bowtouch::Scene s;
            s.bow = h(in.bowHand);
            s.str = h(in.stringHand);
            s.forward = dir(forward);
            s.nock = h(o.nock);
            s.nockConst = h(in.bowHand - forward * params.nockBehind);
            s.haveModel = haveModel;
            if (haveModel) s.nockModel = h(modelRoom);
            s.reach = params.reach;
            s.atString = o.atString;
            s.drawing = o.drawing;
            s.aimOwned = o.aimOwned;
            s.aim = dir(o.aim);
            s.pullNeed = o.pullNeed;
            s.pullEnough = o.drawing ? o.charged : o.pullEnough;   // taken: the release shoots (distance and the 0.12 s); near: the distance
            if (haveRest) { s.haveRest = true; s.rest = h(restRoom); }   // [bow] arrow_rest_*: the point the arrow rests on, in the bow hand's frame: a dot, to tune the offsets by
#ifdef BOWDRAW_RIG_FEED
            {   // [bow] aim_along_hands: the bow model's real forward axis, from the hero's pose (the lime line)
                float ax[3];
                if (skel::BowAxis(ax)) { s.haveModelAxis = true; s.modelAxis = {ax[0], ax[1], ax[2]}; }
            }
#endif
            const bowtouch::VectorSet v = bowtouch::MakeVectors(s);
            AcquireSRWLockExclusive(&g_lock);
            g_vectors = v;
            g_vectorsAt = GetTickCount64();
            ReleaseSRWLockExclusive(&g_lock);
        }
        // And the numbers, five times a second: why a draw starts or does not.
        // (every 5 s while there is no bow in hand: nothing to explain then)
        if (f.time - r.dbgAt >= (bowInHand ? 0.2 : 5.0) || r.dbgAt > f.time) {
            r.dbgAt = f.time;
            const float toNock = bowdraw::Length(in.stringHand - o.nock), toConst = bowdraw::Length(in.stringHand - (in.bowHand - forward * params.nockBehind));
            const float gap = bowdraw::Length(in.bowHand - in.stringHand);
            char aimText[420] = "";
#ifdef BOWDRAW_RIG_FEED
            {   // [bow] aim_along_hands: how far the bow's axis is from the aim, and how far the turn is in, and which wrist it took the bow to be on
                float blend = 0.0f, angle = 0.0f, raw = 0.0f;
                const char* why = "";
                if (skel::BowAimState(&blend, &angle, &raw, &why)) {
                    const std::string who = skel::BowAimWho();
                    snprintf(aimText, sizeof aimText, " | bow aim: %s; the bow's axis is %.1f deg from the aim (%.1f deg as the controller holds it), turned in %.2f (%s)", why, angle, raw, blend, who.c_str());
                }
            }
#endif
            char model[64];
            if (haveModel) snprintf(model, sizeof model, "%.3f m from the grip, %s", nock.modelDist, nock.modelSane ? "sane" : "REJECTED");
            else snprintf(model, sizeof model, "not read");
            snprintf(b, sizeof b,
                     "bow draw dbg: %s%s | allowed %d (focused %d, menu %d/%d, bow in hand %d) pose %d (head %d, hands active %d/%d, grips placed %d/%d) | "
                     "bow in the %s hand, start: %s, release: %s | trigger %.2f grip %.2f | to the touch point %.3f m (reach %.2f, %s; "
                     "the constant's %.3f, the bow's string: %s) | hands apart %.3f m, bow axis %.2f %.2f %.2f (room)",
                     bowdraw::PhaseName(o.phase), o.lost ? " (a HAND IS LOST, the draw is held)" : "", in.allowed, focused, menu, gameMenu, bowInHand, in.valid, haveHead, bow.active, str.active,
                     bow.grip.valid == 1, str.grip.valid == 1, bowVr ? "left" : "right", bowdraw::StartName(start), kButtons[g_button.load()].name,
                     str.trigger, str.squeeze, toNock, params.reach, o.atString ? "INSIDE" : "outside", toConst, model, gap,
                     forward.x, forward.y, forward.z);
            const size_t used = strlen(b);
            if (used + 1 < sizeof b) snprintf(b + used, sizeof b - used, "%s", aimText);
            Log(b);
        }
    }
}

}  // namespace

void Init(const D2RL::PluginContext* ctx, HandFn bowHand, ViewFn f4) {
    g_ctx = ctx;
    g_bowHand = bowHand;
    g_f4 = f4;
    static bool listening = false;
    if (!listening) listening = directinput::Listen(&OnFrame, nullptr);
    if (!listening) Log("vrcam: bow draw - direct_input's listener table is full: the bow draw does nothing");
    // (the settings are read before the log is there: say now what they are)
    if (g_on.load()) {
        char b[300];
        snprintf(b, sizeof b, "vrcam: bow draw on - the string is taken with the %s at the touch point (%s), the release presses %s%s",
                 bowdraw::StartName(g_start.load()), g_model.load() ? "the bow's own string when it reads sanely, else 16 cm behind the grip" : "16 cm behind the grip",
                 kButtons[g_button.load()].name, g_debug.load() ? "; [debug] bow_vectors is on" : "");
        Log(b);
    }
}

void SetRestAims(bool on) { g_restAims.store(on); }

void Configure(bool on, int button, int start, bool model, bool debug, float reachCm, float nockShiftCm, float minPullCm, bool drawOnly) {
    const bool wasOnly = g_only.exchange(drawOnly);
    if (button < 0 || button >= kButtonCount) button = 0;
    start = bowdraw::ClampStart(start);
    // the zone: a radius of 5..50 cm (28 as it always was) and a shift of its centre along the bow of -25..+25 cm; live, not logged
    // on every step of a slider (the debug view shows both)
    const float reach = (std::isfinite(reachCm) ? std::fmin(std::fmax(reachCm, 5.0f), 50.0f) : 28.0f) * 0.01f;
    const float shift = (std::isfinite(nockShiftCm) ? std::fmin(std::fmax(nockShiftCm, -25.0f), 25.0f) : 0.0f) * 0.01f;
    g_reach.store(reach);
    g_shift.store(shift);
    // the pull that is enough: 5..40 cm beyond where the string was taken (12 as it always was), live, not logged
    g_minPull.store((std::isfinite(minPullCm) ? std::fmin(std::fmax(minPullCm, 5.0f), 40.0f) : 12.0f) * 0.01f);
    const bool was = g_on.exchange(on);
    const int wasButton = g_button.exchange(button), wasStart = g_start.exchange(start);
    const bool wasModel = g_model.exchange(model), wasDebug = g_debug.exchange(debug);
    if (!g_ctx) return;   // before Init: Init tells
    if (was != on || (on && (wasButton != button || wasStart != start || wasModel != model || wasDebug != debug || wasOnly != drawOnly))) {
        char b[480];
        if (on) snprintf(b, sizeof b, "vrcam: bow draw on - the string is taken with the %s within %.0f cm of the touch point, moved %+.0f cm along the bow (%s), the release presses %s%s%s",
                         bowdraw::StartName(start), reach * 100.0f, shift * 100.0f, model ? "the bow's own string when it reads sanely, else 16 cm behind the grip" : "16 cm behind the grip",
                         kButtons[button].name, drawOnly ? "; a press away from the string shoots nothing, what else is bound to the trigger still works ([hands] bow_draw_only)" : "; a trigger press away from the string is the plain trigger",
                         debug ? "; [debug] bow_vectors is on" : "");
        else snprintf(b, sizeof b, "vrcam: bow draw off");
        Log(b);
    }
}

bool On() { return g_on.load(); }

bool WantsModelString() { return Active() && (g_model.load() || g_debug.load()); }

bool Owned() {
    if (!Active()) return false;
    const ULONGLONG at = g_ownedAt.load();
    return at && GetTickCount64() - at < kFreshMs;
}

unsigned ShotCount() { return g_shotCount.load(); }
void ShotTimes(double* releaseS, double* pressS) {
    if (releaseS) *releaseS = g_shotReleaseS.load();
    if (pressS) *pressS = g_shotPressS.load();
}

bool KeepsTrigger(int hand) {
    if (!Active() || hand != g_keepHand.load()) return false;
    const ULONGLONG at = g_keepAt.load();
    return at && GetTickCount64() - at < kFreshMs;
}

bool HoldsGrip(int hand) {
    if (!Active() || hand != g_gripHand.load()) return false;
    const ULONGLONG at = g_gripAt.load();
    return at && GetTickCount64() - at < kFreshMs;
}

bool AimLive() {
    if (!Active()) return false;
    AcquireSRWLockShared(&g_lock);
    const bool live = g_aim.live && GetTickCount64() - g_aim.at < kFreshMs;
    ReleaseSRWLockShared(&g_lock);
    return live;
}

bool AimDir(float out[3]) {
    if (!Active()) return false;
    AcquireSRWLockShared(&g_lock);
    const Aim a = g_aim;
    ReleaseSRWLockShared(&g_lock);
    if (!a.live || GetTickCount64() - a.at >= kFreshMs) return false;
    const bowdraw::Vec h = bowdraw::ToHeadTurn(a.room, a.yaw);   // the head's turn taken off: the same as XrIntoShared does to a hand
    out[0] = h.x; out[1] = h.y; out[2] = h.z;
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

void NoteRecenter() { g_recentre.store(true); }

bool DebugWanted() { return Active() && g_debug.load(); }

bool DebugVectors(bowtouch::VectorSet* out) {
    if (!Active() || !g_debug.load() || !out) return false;
    AcquireSRWLockShared(&g_lock);
    const bool fresh = g_vectorsAt && GetTickCount64() - g_vectorsAt < kFreshMs;
    if (fresh) *out = g_vectors;
    ReleaseSRWLockShared(&g_lock);
    return fresh;
}

}  // namespace bowdrawwire
