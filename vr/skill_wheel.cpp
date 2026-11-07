// skill_wheel.cpp - the wiring of the skill wheel (vr/skill_wheel.h): one listener on direct input's per-frame callback, the
// controller (vr/wheel_controller.h) stepped with the newest game snapshot, and what it says done through the input layer's own
// calls - Take, PadPulse, Haptic - and the game's UI thread (vr/wheel_game.cpp). Nothing here presses a key or moves the mouse.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <string>
#include <thread>

#include <D2RLPlugin/api.h>

#include "log_async.h"   // [debug] log_async: the plugin's log lines through a ring and a writer thread (vr/log_queue.h); off = ctx->LogInfo as ever
#include "bow_draw_wire.h"
#include "calibration.h"
#include "direct_input.h"
#include "game_ui.h"   // [openxr] skill_panel_game_look: the Settings tab's flip arrows go up with the game look
#include "first_start_wire.h"   // the Settings tab's Guide page: the first-start guide asks for the window, and is told when it is over
#include "hset_store.h"
#include "hset_view.h"
#include "hset_life.h"
#include "low_hp_game.h"   // the Settings tab's vignette pages show the vignette as a preview
#include "hset_stats_feed.h"   // the Settings tab's Stats page: the strike statistics ([stats] strike_stats)
#include "bodywalk_start.h"    // [openxr] gesture_overlay: the tab's START BODYWALK
#include "gesture_engine_wire.h"   // [gestures] engine_in_game: the tab's list and commands from the gesture engine in the game
#include "link.h"              // [openxr] gesture_overlay: BodyWalk's Gestures block in, the GestureCmds ring out (Game Link)
#include "skill_link_wire.h"   // ... which BodyWalk writes for a game that sends it the skills ([gestures] skill_link)
#include "party_cmd.h"     // the Settings tab's Party page: the game's roster and its Party panel's buttons ([openxr] headset_party)
#include "players_cmd.h"   // the Settings tab's Players row: the game's own /players N ([openxr] headset_players)
#include "skill_wheel.h"
#include "tiles_labels.h"
#include "view_gate.h"
#include "weight_wire.h"   // [weight] weapon_grip_no_chord: GripOnWeapon
#include "wheel_controller.h"
#include "wheel_game.h"
#include "wheel_icons.h"
#include "wheel_paint.h"
#include "xr.h"

namespace skillwheel {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false};
std::atomic<bool> g_open{false};
std::atomic<bool> g_triggerSkill{false};   // TriggerCastsSkill
bool g_registered = false;

std::mutex g_lock;               // the controller, the snapshot copy, what the layer reads
wheel::Controller g_ctl;
wheel::Settings g_cfg;            // what g_ctl was configured with (the profiles change the hints without the rest)
tiles::Profile g_profile[2] = {tiles::Profile::None, tiles::Profile::None};
bool g_showHints = false;         // direct input is on and its layout is the default one
wheel::Snapshot g_snap;
uint32_t g_snapRevision = 0;
wheel::Layer g_layer;
wheel::View g_view;
uint32_t g_openRevision = 0;
wheel::Style g_openStyle = wheel::Style::Wheel;
bool g_wasOpen = false;
uint32_t g_lastTake[2] = {0, 0};   // what this panel took from each hand last frame (so TakenLastFrame's rest is the others')
unsigned g_slotPresses = 0;        // the slot controls' presses told in the log so far
// The painter is made on first use and never destroyed: its thread is joined by Release (Configure off, the session's end, the
// plugin's unload), never by a static destructor - a join under the loader lock at process exit can hang.
wheelpaint::Async& Painter() {
    static wheelpaint::Async* painter = new wheelpaint::Async;
    return *painter;
}
bool g_everOn = false;           // switched on at some time: there may be threads and state to let go

// The Settings tab (vr/wheel_controller.h): the ini's thread (vr/hset_store.h) runs while the window is up, comes back or its chord arms;
// the sizes the Video page shows come from xr.cpp (SetSettingsHost).
std::atomic<bool> g_settingsTab{false};
std::atomic<bool> g_twoGripChord{false};   // [debug] settings_two_grip_chord: the old both-grips chord (off: two grips held mean nothing)
bool g_storeOn = false;          // under g_lock
std::wstring g_iniPath;          // under g_lock
hset::video::Env g_env;          // under g_lock

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }

// The Gestures tab ([openxr] gesture_overlay): BodyWalk's block as read this frame (36 KB: not on the stack; under g_lock).
BW_LinkGestures g_gestures;
bool g_gesturesHave = false;
bool g_gestureRecording = false;   // told to the link (BodyWalk gets the controls while it records for the tab)
std::atomic<bool> g_gestureRecordingNow{false};   // ... and to xr.cpp's layers (the trail over the panel): GestureRecording
std::atomic<int> g_gestureHand{BW_LINK_GH_RIGHT};   // ... with this hand (GestureRecordHand: the engine's pen)
std::atomic<bool> g_gestureTrace{false};   // [debug] gesture_trace: the whole chain in the log (view only)

// [debug] gesture_trace: a line of the chain, "gesture trace" + the time + what.
void TraceF(const char* fmt, ...) {
    char what[400];
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(what, sizeof what, fmt, a);
    va_end(a);
    char b[460];
    std::snprintf(b, sizeof b, "vrcam: gesture trace %.3f - %s", (double)GetTickCount64() / 1000.0, what);
    Log(b);
}
// Why there are no gestures, looked at once a second on vrcam's timer (Tick: a process list is not for the headset's frame) while the tab is on.
std::atomic<int> g_bwRunning{0};          // BodyWalkVR.exe processes
std::atomic<bool> g_bwHost{false};        // its Game Link Host block moves
std::atomic<bool> g_bwFound{false};       // a BodyWalkVR.exe to start was found
std::atomic<uint64_t> g_bwStartedAt{0};   // START BODYWALK's tick (GetTickCount64), 0 none: "starting" until its Game Link is up, 45 s at most
std::atomic<bool> g_fixBusy{false};       // a fix's worker is on its way

// The tab's facts for the controller: why the Gestures block is missing.
gov::Missing MissingNow() {
    const uint64_t at = g_bwStartedAt.load(), now = GetTickCount64();
    if (at && now - at < 45000 && !g_bwHost.load()) return gov::Avail::Starting;
    if (g_bwRunning.load() == 0 && !g_bwHost.load()) return gov::Avail::NotRunning;
    if (!g_bwHost.load()) return gov::Avail::NoGameLink;
    if (!link::SkillLinkUp()) return gov::Avail::NoSkills;
    return gov::Avail::NoGestures;
}

// The tab's fix, off the headset's frame (an ini write, a process start): ENABLE writes [gestures] skill_link=1 (the plugin takes the ini live,
// as the headset settings' writes); START BODYWALK starts the copy D2R VR Setup put in - never a second one.
void DoFix(gov::Fix fix, std::wstring ini) {
    if (g_fixBusy.exchange(true)) return;
    std::thread([fix, ini] {
        if (fix == gov::Fix::EnableLink) {
            const bool ok = !ini.empty() && WritePrivateProfileStringW(L"gestures", L"skill_link", L"1", ini.c_str()) != FALSE;
            Log(ok ? "vrcam: skill panel, Gestures - Enable: [gestures] skill_link=1 written to d2r_vr.ini (taken live: the skills go to BodyWalk)"
                   : "vrcam: skill panel, Gestures - Enable: d2r_vr.ini could not be written ([gestures] skill_link stays 0)");
        } else if (fix == gov::Fix::StartBodyWalk) {
            const std::wstring dir = ini.substr(0, ini.find_last_of(L'\\') + 1);
            const std::wstring exe = bwstart::FindExe(dir);
            const bwstart::Result r = bwstart::Start(exe);
            if (r == bwstart::Result::Started) g_bwStartedAt.store(GetTickCount64());
            char b[600];
            std::snprintf(b, sizeof b, "vrcam: skill panel, Gestures - Start BodyWalk: %s (%ls)",
                          r == bwstart::Result::Started ? "started" : r == bwstart::Result::AlreadyRunning ? "one runs already - not a second" :
                          r == bwstart::Result::NotFound ? "no BodyWalkVR.exe found" : "it could not be started", exe.empty() ? L"-" : exe.c_str());
            Log(b);
        }
        g_fixBusy.store(false);
    }).detach();
}

// The tiles' hints say which control of the player's own controllers presses a slot's pad button in direct input's DEFAULT layout:
// true only while direct input is on and none of the controls involved is rebound in [direct] (then the hint could be false).
bool DefaultLayoutIn(const wchar_t* ini) {
    if (!ini || GetPrivateProfileIntW(L"openxr", L"direct_input", 0, ini) == 0) return false;
    for (const wchar_t* key : {L"right_b", L"left_x", L"left_y", L"right_grip+right_b", L"left_trigger", L"right_trigger"}) {
        wchar_t b[16];
        GetPrivateProfileStringW(L"direct", key, L"~unset~", b, 16, ini);
        if (wcscmp(b, L"~unset~") != 0) return false;   // set (even to "none"): not the default any more
    }
    return true;
}

// The input layer can send the long press's request: long presses are on ([direct] hold_enabled, default on) and the right stick's hold line
// is the skill panel's - left out (its default is action:skillpanel) or naming it. Otherwise the request can never come and the panel is
// opened by the chord, whatever [openxr] skill_wheel_open says.
bool LongPressCanCome(const wchar_t* ini) {
    if (!ini || GetPrivateProfileIntW(L"direct", L"hold_enabled", 1, ini) == 0) return false;
    wchar_t b[96];
    GetPrivateProfileStringW(L"direct", L"right_stick_hold", L"~unset~", b, 96, ini);
    if (wcscmp(b, L"~unset~") == 0) return true;
    _wcslwr_s(b);
    return wcsstr(b, L"skillpanel") != nullptr;
}

// g_cfg + the profiles -> the controller (under g_lock).
void ApplyLocked() {
    g_cfg.hints = tiles::BuildHints(g_profile[0], g_profile[1], g_showHints, g_cfg.slotControls);
    g_ctl.Configure(g_cfg);
}

// The ini's thread: started when the Settings tab wants it (the window up, back from a measurement, or the chord arming), stopped when it does not.
void StoreFor(bool want) {
    if (want && !g_storeOn && !g_iniPath.empty()) {
        hset::store::SetLogger(&Log);
        hset::store::Start(g_iniPath.c_str());
        g_storeOn = true;
    } else if (!want && g_storeOn) {
        hset::store::Stop();
        g_storeOn = false;
    }
}

// What the Settings tab asks of the plugin (the controller has closed the window itself where it should).
void SettingsAct(const wheel::Step& s) {
    switch (s.settings.what) {
    case hset::Do::Save:
        if (!hset::store::Submit(s.settings.request)) {
            g_ctl.SettingsSubmitFailed(s.settings.request.sequence);
            Log("vrcam: openxr - skill panel, Settings: Apply could not be handed to the ini's thread");
        }
        break;
    case hset::Do::Coop:   // the Co-op page: a [relay] line of D2R Rooms' d2r-rooms.ini, written by the ini's thread (it logs the write)
        if (!hset::store::SubmitCoop(s.settings.coop)) {
            g_ctl.SettingsCoopFailed(s.settings.coop.sequence);
            Log("vrcam: openxr - skill panel, Settings: Co-op: the change could not be handed to the ini's thread");
        }
        break;
    case hset::Do::Recentre:
        xr::Recenter();
        Log("vrcam: openxr - skill panel, Settings: Recentre view");
        break;
    case hset::Do::Calibrate:   // the window has closed; the plugin's own prompt leads the player, and the window comes back when it is over
        calib::RequestMeasure();
        Log("vrcam: openxr - skill panel, Settings: Calibrate body (the window is back when the measurement is over)");
        break;
    case hset::Do::GuideDone:   // the guide's last page: the window has closed, the guide is over (the controller told it: Step::guideEnd)
        Log("vrcam: openxr - skill panel, Settings: the first-start guide's Done");
        break;
    case hset::Do::Players:     // the Options page's Players row: "/players N" into the game's own chat commands, on the UI thread (vr/players_cmd.cpp logs it)
        playerscmd::Ask(s.settings.players);
        break;
    case hset::Do::Party:       // the Party page: the game's Party panel's own button (or its P key command; then the window has closed), on the UI thread
        partycmd::Ask(s.settings.party);
        break;
    case hset::Do::MapShow:     // the Co-op page's map row: the game's own key command 10 / 11 flips its Show Party / Show Party Names (vr/party_cmd.cpp logs it)
        partycmd::MapToggle(s.settings.mapShow);
        break;
    case hset::Do::Preview:     // "Preview 5 s" on the Life and Look pages: the vignette and the beat for five seconds with the rows as they stand (vr/low_hp.h PreviewWindow)
        lowhp::PlayPreview();
        break;
    case hset::Do::View:        // F1 - F4 by the controller's own view path (vr/hset_view.h); the view leaves F4, so the window has closed
        if (hsetview::Press(s.settings.view))
            Log("vrcam: openxr - skill panel, Settings: game view changed (F4 is the way back: the F4 key, D2R VR Authentic Settings > Home, or a view action bound in BodyWalk / [direct])");
        break;
    default: break;
    }
}

void Haptic(wheel::Feedback f, int hand) {
    using wheel::Feedback;
    switch (f) {
    case Feedback::Hover: directinput::Haptic(hand, 0.12f, 6.0f); break;
    case Feedback::Selected: directinput::Haptic(hand, 0.55f, 35.0f); break;
    case Feedback::Applied: directinput::Haptic(hand, 0.6f, 60.0f); break;
    case Feedback::Refused: directinput::Haptic(hand, 0.8f, 90.0f, 80.0f); break;
    case Feedback::Opened: directinput::Haptic(hand, 0.3f, 30.0f); break;
    case Feedback::Closed: directinput::Haptic(hand, 0.2f, 20.0f); break;
    default: break;
    }
}

// Once per headset frame on the draw thread, inside xr.cpp's Publish (vr/direct_input.h Listen).
void OnFrame(const directinput::Frame& f, void*) {
    hsetview::Tick();   // a view button's press is let go when it is due
    if (!g_on.load()) return;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_on.load()) return;
    const bool have = wheelgame::Latest(&g_snap, &g_snapRevision);
    if (!have && (g_snap.ready || g_snap.session)) g_snap = wheel::Snapshot{};   // (no game state: nothing to show)
    // F4 has all of it; VR F1 - F3 the window alone (vr/wheel_controller.h, vr/view_gate.h Number); in another view the controller stands down. The
    // long press's request is taken (and, outside the long-press mode or those views, dropped) every frame so that it cannot open the panel late.
    const bool f4 = viewgate::F4();
    const int viewNo = f4 ? 4 : viewgate::Flat() ? viewgate::Number() : 0;
    g_ctl.SetView(viewNo);
    const bool request = directinput::TakeSkillPanelToggle();
    // a bow wielded in F4 with the draw on shoots by a draw only: the pointing hand's trigger is the bow draw's (kept from the layout), so
    // the picked slot is not cast with it either (the draw's own release still presses its skill slot)
    g_ctl.SetCastBlocked(bowdrawwire::KeepsTrigger(g_cfg.pointerHand));
    g_ctl.SetSecondRow(directinput::SecondRowOn());   // the left trigger bound to its own action: the second row of buttons is off (greyed)
    {   // the right trigger / A as slot controls ([openxr] skill_rt_a): another gesture that holds them (a bottle, a javelin, the string) keeps them
        const uint32_t others = directinput::TakenLastFrame(1) & ~g_lastTake[1];
        g_ctl.SetControlsTaken((others & (directinput::kTrigger | directinput::kTakeTrigger)) != 0, (others & directinput::kPrimary) != 0);
    }
    {   // [weight] weapon_grip_no_chord: a grip that holds the weapon (or another gesture's) is no modifier of the chords and the slot controls
        bool taken[2] = {false, false};
        if (weightwire::GripNoChord())
            for (int h = 0; h < 2; ++h)
                taken[h] = weightwire::GripOnWeapon(h) || (directinput::TakenLastFrame(h) & ~g_lastTake[h] & (directinput::kSqueeze | directinput::kTakeSqueeze)) != 0;
        g_ctl.SetGripsTaken(taken[0], taken[1]);
    }
    const bool tabs = wheel::TabsOn(g_cfg);
    wheel::SettingsIn host;
    if (tabs) {   // the Settings tab's side: the ini as its thread reads it, the picture's sizes, the T-pose measurement
        if (g_storeOn) host.ini = hset::store::Read();
        host.env = g_env;
        host.view = viewNo ? viewNo : 4;   // (the pages shown are the view's: VR F1 - F3 not F4's body)
        host.spanInUse = calib::SpanInUseM();   // the Body page's Arm span, while nothing is stored
        host.players = playerscmd::Now();       // the Options page's Players row: the game's count (read, cheap)
        if (g_ctl.SettingsModel().open && g_ctl.SettingsModel().page == hset::kPageParty) partycmd::Watch();   // the Party page: the roster read on
        host.party = partycmd::Now();
        if (g_ctl.SettingsModel().open && g_ctl.SettingsModel().page == hset::kPageCoop) host.mapShow = partycmd::MapNow();
        if (g_ctl.SettingsModel().open && g_ctl.SettingsModel().page == hset::kPageStats) {   // [stats] strike_stats: the Stats page (a copy when they changed)
            static hset::stats::View statsView;
            hset::stats::Feed(statsView);
            host.stats = statsView;
        }   // the Co-op page's map row (read, cheap)
        host.calibrating = calib::Running();
        host.openGuide = f4 ? firststart::OpenRequest() : -1;   // the first-start guide asks for the window on its Guide tab (vr/first_start.cpp; F4's)
        host.chordOn = g_twoGripChord.load();         // the author: "no combinations with the two grips" - off unless [debug] settings_two_grip_chord
    }
    // the Gestures tab: BodyWalk's block, every frame while the tab is on (a consistent copy; false = no BodyWalk writes it)
    wheel::GesturesIn gestures;
    const bool gestureTab = tabs && g_cfg.gestureTab;
    const bool engine = gengine::On();   // [gestures] engine_in_game: the engine in the game, not BodyWalk over Game Link
    if (gestureTab || g_ctl.GesturesModel().Recording()) {
        gestures.link = engine || skilllinkwire::On();
        if (engine) g_gesturesHave = gengine::List(&g_gestures);
        else g_gesturesHave = gestures.link && link::Gestures(&g_gestures);   // (the skill link off: BodyWalk writes none - the block is not even opened)
        gestures.g = g_gesturesHave ? &g_gestures : nullptr;
        if (g_gesturesHave) {
            {   // BodyWalk's side in the log too: each answer and each change of its recording's state (rare: a few lines a take)
                static uint32_t seenDone = 0, seenState = ~0u, seenGesture = ~0u;
                if (g_gestures.cmdDone != seenDone) {
                    seenDone = g_gestures.cmdDone;
                    static const char* const kResult[] = {"ok", "no such gesture", "refused (bad)", "busy", "no list for the class"};
                    char b[200];
                    std::snprintf(b, sizeof b, "vrcam: skill panel, Gestures - BodyWalk acted on command #%u: %s", g_gestures.cmdDone,
                                  g_gestures.cmdResult < 5 ? kResult[g_gestures.cmdResult] : "?");
                    Log(b);
                }
                if (g_gestures.recordState != seenState || g_gestures.recordGesture != seenGesture) {
                    seenState = g_gestures.recordState;
                    seenGesture = g_gestures.recordGesture;
                    static const char* const kState[] = {"none", "ready", "drawing", "saved", "failed", "cancelled"};
                    char b[200];
                    std::snprintf(b, sizeof b, "vrcam: skill panel, Gestures - BodyWalk's recording: %s (gesture %u, hand %u)",
                                  g_gestures.recordState < 6 ? kState[g_gestures.recordState] : "?", g_gestures.recordGesture, (unsigned)g_gestures.recordHand);
                    Log(b);
                }
            }
            g_bwStartedAt.store(0);
            if (!engine && link::PrimeGestureCmds(g_gestures.cmdDone)) {   // (once: BodyWalk's reader primes on an empty ring, not on our first command)
                static bool told = false;
                if (!told) {
                    told = true;
                    Log("vrcam: skill panel, Gestures - BodyWalk's Gestures are there: the command ring written empty for it to start from");
                }
            }
        }
        gestures.missing = engine ? gov::Avail::NoEngine : MissingNow();
        gestures.canStart = !engine && g_bwFound.load();
    }
    const wheel::Step s = g_ctl.Frame(f, g_snap, GetTickCount64(), wheelgame::ReplyNow(), f4, request, tabs ? &host : nullptr,
                                      gestureTab || g_ctl.GesturesModel().Recording() ? &gestures : nullptr);
    if (g_gestureTrace.load()) {   // [debug] gesture_trace: the tab's model's events, the block's changes, the pen handed to BodyWalk
        for (int i = 0; i < s.gestureTraces; ++i) TraceF("model: %s", s.gestureTrace[i]);
        static uint32_t seenCount = ~0u, seenRev = ~0u, beats = 0, lastStamp = 0;
        static uint64_t beatAt = 0, penAt = 0;
        static bool seenHave = false;
        const uint64_t nowMs = GetTickCount64();
        if (g_gesturesHave != seenHave) {
            seenHave = g_gesturesHave;
            TraceF("block: BodyWalk's Gestures %s", g_gesturesHave ? "there" : "GONE (none written for 2 s, or the skill link off)");
        }
        if (g_gesturesHave) {
            if (g_gestures.count != seenCount || g_gestures.revision != seenRev) {
                seenCount = g_gestures.count;
                seenRev = g_gestures.revision;
                TraceF("block: %u gestures, revision %u, set '%.31s'", g_gestures.count, g_gestures.revision, g_gestures.setKey);
            }
            if (g_gestures.stamp != lastStamp) { lastStamp = g_gestures.stamp; ++beats; }
            if (!beatAt) beatAt = nowMs;
            if (nowMs - beatAt >= 1000) {
                TraceF("block: BodyWalk's heartbeat %u writes in %.1f s (cmdDone %u, result %u, recording %s, gesture %u)", beats, (nowMs - beatAt) / 1000.0,
                       g_gestures.cmdDone, g_gestures.cmdResult, gov::StateName(g_gestures.recordState), g_gestures.recordGesture);
                beats = 0;
                beatAt = nowMs;
            }
        }
        if (s.gestureRecording && nowMs - penAt >= 100) {   // the pen: what BodyWalk is handed, 10 times a second while it records for the tab
            penAt = nowMs;
            TraceF("pen: triggers L %.2f R %.2f, grips L %.2f R %.2f (BodyWalk is given the controls: %s)", f.hand[0].trigger, f.hand[1].trigger,
                   f.hand[0].squeeze, f.hand[1].squeeze, link::SkillLinkUp() ? "yes" : "NO - its skill link is not up");
        }
    }
    if (s.gestureSend) {   // a command to BodyWalk's gestures: into the ring, its sequence back to the tab (it waits for BodyWalk's cmdDone)
        const uint32_t seq = engine ? gengine::Push(s.gestureCmd) : link::PushGestureCmd(s.gestureCmd, g_gesturesHave ? g_gestures.cmdDone : 0u);
        g_ctl.GestureSent(seq, f.time);
        char b[240];
        std::snprintf(b, sizeof b, "vrcam: skill panel, Gestures - %s #%u to %s (gesture %u, skill %d, type %u, hand %u, value %.1f)%s",
                      gesturecmd::OpName(s.gestureCmd.op), seq, engine ? "the gesture engine" : "BodyWalk", s.gestureCmd.gesture, (int)s.gestureCmd.skill, (unsigned)s.gestureCmd.type,
                      (unsigned)s.gestureCmd.hand, s.gestureCmd.value, seq ? "" : " - NOT sent: the GestureCmds block could not be had");
        Log(b);
    }
    if (s.gestureFix != gov::Fix::None) DoFix(s.gestureFix, g_iniPath);
    g_gestureHand.store(g_ctl.GesturesModel().Current().recHand);   // (the engine's pen while the tab's take runs)
    if (s.gestureRecording != g_gestureRecording) {   // BodyWalk records for the tab: its pen is the trigger, it gets the controls meanwhile
        g_gestureRecording = s.gestureRecording;
        g_gestureRecordingNow.store(s.gestureRecording);
        link::SetGestureRecording(s.gestureRecording && !engine);   // (BodyWalk is given the controls only when it records; the engine gets the frame itself)
        Log(s.gestureRecording ? "vrcam: skill panel, Gestures - BodyWalk records: the trigger is its pen, the window stands aside (B / Y stop)"
                               : "vrcam: skill panel, Gestures - the recording is over");
    }
    StoreFor(tabs && s.wantStore);
    if (tabs) SettingsAct(s);
    if (s.guideOpened) {
        firststart::OpenRequestTaken();
        Log("vrcam: openxr - skill panel: the window is up on Settings > Guide (the first-start guide asked for it)");
    }
    if (s.guideEnd) firststart::PanelEnded(s.guideEnd == 1);   // the guide's page was on show and the window closed
    for (int h = 0; h < 2; ++h) {
        if (s.take[h]) directinput::Take(h, s.take[h], directinput::kSrcPanel);   // (the overlay's source: it takes in VR F1 - F3 too)
        g_lastTake[h] = s.take[h];
    }
    // the cast: the game's own pad buttons, pulsed while the trigger is down (the modifier and the button each held as the game wants); with the slot
    // controls the right trigger is the pad's RT, held by its pulse the same way
    if (s.casting && (s.buttons || s.lt || s.rt)) directinput::PadPulse(directinput::PadPress{s.buttons, s.lt, s.rt}, wheel::kHoldMs);
    static bool toldSlots = false;
    if (g_ctl.Config().slotControls && !toldSlots) {
        toldSlots = true;
        Log("vrcam: skill panel - slot controls on ([openxr] skill_rt_a=1): in first person with direct input the right trigger casts the game's RT slot and the right A its A slot "
            "(the left trigger held: LT + RT, LT + A) when that slot holds a skill; a slot with nothing or the plain Attack on it leaves the control to the layout");
    }
    for (int c = 0; c < 2; ++c) {   // (the first few presses of the slot controls, told: which control pressed which slot holding what)
        const int slot = s.slotStarted[c];
        if (slot < 0 || g_slotPresses >= 12) continue;
        ++g_slotPresses;
        char b[260];
        std::snprintf(b, sizeof b, "vrcam: skill panel - the right %s pressed the %s slot, which holds %s (the game casts it)%s", c == 0 ? "A" : "trigger",
                      wheel::kSlotDefs[slot].label, g_snap.slot[slot].name, g_slotPresses == 12 ? " - no more of these lines" : "");
        Log(b);
    }
    if (s.request) wheelgame::Submit(s.req);
    // RUN / WALK: the game's own key command, through the game's own executor (vr/gamecmd.cpp), pressed once - as its key would
    if (s.runWalk) directinput::Tap((uint32_t)wheel::kRunWalkCommand, directinput::kSrcPanel);
    Haptic(s.feedback, g_ctl.Config().pointerHand);
    g_layer = s.layer;
    g_view = g_ctl.ViewNow();
    const bool open = g_ctl.Open();
    {   // each time the editor's grid comes up: whether the category strip is there, and if not why (the one line that tells a missing strip's cause)
        static bool wasGrid = false;
        const bool grid = open && g_view.page == wheel::Page::PickSkill;
        if (grid && !wasGrid) {
            char b[260];
            std::snprintf(b, sizeof b, "vrcam: skill panel - the editor on slot %d: category strip %s ([openxr] skill_categories %d, the snapshot knows the trees %d, class %d, %u skills)",
                          g_view.editSlot, g_view.catStrip ? "shown" : "NOT shown", g_ctl.Config().categories ? 1 : 0, g_snap.treesKnown ? 1 : 0, (int)g_snap.playerClass, (unsigned)g_snap.count);
            Log(b);
        }
        wasGrid = grid;
    }
    lowhp::SetPreview(open && tabs && g_ctl.FlipPlace() > 0 && hset::IsLifePage(g_ctl.FlipPlace() - 1), hset::LifeConfig(g_ctl.SettingsModel().Get()),
                      g_ctl.FlipPlace() - 1);   // the Settings tab is on a vignette page: its rows are the preview's draft; nothing shows until "Preview 5 s"
    if (open && (!g_wasOpen || g_view.style != g_openStyle)) g_openRevision = g_view.revision;   // (another look: its picture must be painted before it is shown)
    g_openStyle = g_view.style;
    // the gesture detectors read the same raw frame: while the wheel is up they treat it like a menu (a 250 ms lease, set every frame)
    if (open) directinput::SetUiUp(true);
    else if (g_wasOpen) directinput::SetUiUp(false);
    g_wasOpen = open;
    g_open.store(open);
    g_triggerSkill.store(g_ctl.TriggerCastsSkill());
}

}  // namespace

void Release();   // below

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    wheelgame::Init(ctx);
}

void SetProfiles(const char* left, const char* right) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_profile[0] = tiles::ProfileOf(left);
    g_profile[1] = tiles::ProfileOf(right);
    ApplyLocked();
}

void Configure(bool on, int pointerHand, float distanceM, float widthM, int style, float tilesWidthM, const wchar_t* ini, bool settingsTab) {
    wheel::Settings cfg;
    cfg.pointerHand = pointerHand ? 1 : 0;
    cfg.distanceM = std::isfinite(distanceM) ? std::min(std::max(distanceM, 0.4f), 3.0f) : 0.8f;
    cfg.widthM = std::isfinite(widthM) ? std::min(std::max(widthM, 0.3f), 2.0f) : 0.8f;
    cfg.style = style == 1 ? wheel::Style::Tiles : wheel::Style::Wheel;
    cfg.tilesWidthM = std::isfinite(tilesWidthM) ? std::min(std::max(tilesWidthM, 0.5f), 2.5f) : 0.9f;
    // how it opens: [openxr] skill_wheel_open (1 the long press of the right stick click - the default - 0 the chord), and for how long a
    // long press is: [direct] hold_ms, as the input layer reads it (200 - 1500)
    cfg.openMode = ini && GetPrivateProfileIntW(L"openxr", L"skill_wheel_open", 1, ini) != 0 && LongPressCanCome(ini) ? 1 : 0;
    cfg.holdS = ini ? std::min(1.5f, std::max(0.2f, float(GetPrivateProfileIntW(L"direct", L"hold_ms", 500, ini)) / 1000.0f)) : 0.5f;
    cfg.settingsTab = on && settingsTab && ini && cfg.style == wheel::Style::Tiles;
    // the two additions of the editor and the panel (idea after bearstonem/d2r-pcvr, MIT): the category strip (on unless the ini says 0) and the RUN / WALK
    // control (off unless the ini says 1: the same command is a pick of the button lists - "Run / walk switch" - which is how it is meant to be used)
    cfg.categories = !ini || GetPrivateProfileIntW(L"openxr", L"skill_categories", 1, ini) != 0;
    // the user (2026-10-25): "the skill menu, when I open it, should be in edit mode at once - I tap a tile and can assign" - a tap on a slot opens its
    // skill list; the old tap (the trigger casts that slot) is the trigger held on it (wheel_model.h kPickHoldS). 0 = as before
    cfg.editFirst = !ini || GetPrivateProfileIntW(L"openxr", L"skill_panel_edit_first", 1, ini) != 0;
    cfg.runWalk = ini && GetPrivateProfileIntW(L"openxr", L"skill_run_walk", 0, ini) != 0;
    // the game look (xr.cpp sets it before this): its art there, the Settings tab's flip arrows stand at the page tabs' row (the painter's look and the hit test agree)
    cfg.flipHigh = cfg.style == wheel::Style::Tiles && gameui::On() && gameui::Present();
    // the right trigger and the right A as the game's RT and A slots (off unless the ini says 1: the layout's trigger is the attack and its A interact, as ever);
    // it acts in first person with direct input only (vr/slot_controls.h), and the editor's list then has the basic attack to put a slot back
    cfg.slotControls = on && ini && GetPrivateProfileIntW(L"openxr", L"skill_rt_a", 0, ini) != 0;
    // [openxr] gesture_overlay (off unless the ini says 1): the Gestures tab beside Settings - the player's BodyWalk gestures, F4 only; it needs the tab strip
    cfg.gestureTab = cfg.settingsTab && GetPrivateProfileIntW(L"openxr", L"gesture_overlay", 0, ini) != 0;
    {   // [debug] gesture_trace (view only): the Gestures tab's whole chain in the log
        const bool trace = on && ini && GetPrivateProfileIntW(L"debug", L"gesture_trace", 0, ini) != 0;
        if (trace != g_gestureTrace.exchange(trace))
            Log(trace ? "vrcam: gesture trace on ([debug] gesture_trace=1): the Gestures tab's screens, commands, BodyWalk's answers and states, its heartbeat and "
                        "the pen in the log"
                      : "vrcam: gesture trace off");
    }
    wheelgame::SetOfferAttack(cfg.slotControls);
    const bool hints = on && cfg.style == wheel::Style::Tiles && DefaultLayoutIn(ini);
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_cfg = cfg;
        g_showHints = hints;
        if (ini) g_iniPath = ini;
        ApplyLocked();
    }
    {   // (a word when the tab comes or goes)
        static int said = -1;
        const int now = cfg.settingsTab ? 1 : on && settingsTab ? 2 : 0;
        if (now != said) {
            said = now;
            g_settingsTab.store(cfg.settingsTab);
            if (now == 1)
                Log("vrcam: openxr - skill panel: a Settings tab beside Skills ([openxr] headset_settings=1, the tiles' look): the long press opens it on the tab it was last on, "
                    "flip to it with a stick or a grip");
            else if (now == 2)
                Log("vrcam: openxr - skill panel: no Settings tab - it needs the tiles' look ([openxr] skill_panel_style=1); the settings panel of its own stays");
        }
    }
    {   // (a word when the Gestures tab comes or goes)
        static int said = -1;
        const int now = cfg.gestureTab ? 1 : on && ini && GetPrivateProfileIntW(L"openxr", L"gesture_overlay", 0, ini) != 0 ? 2 : 0;
        if (now != said) {
            said = now;
            if (now == 1)
                Log("vrcam: openxr - skill panel: a Gestures tab beside Settings ([openxr] gesture_overlay=1, F4): your BodyWalk gestures - record, re-record, clear, "
                    "bind - from inside the game (needs [gestures] skill_link=1 and BodyWalk with Game skills)");
            else if (now == 2)
                Log("vrcam: openxr - skill panel: no Gestures tab - it needs the Settings tab ([openxr] headset_settings=1 with skill_panel_style=1)");
        }
    }
    if (on && !g_registered) {
        if (!directinput::Listen(&OnFrame, nullptr)) {
            Log("vrcam: openxr - skill wheel: the input layer's listener table is full - no wheel");
            return;
        }
        g_registered = true;
        g_everOn = true;
        g_on.store(true);
        Painter().Warm();
        Log("vrcam: openxr - skill wheel on ([openxr] skill_wheel=1): hold the grip of the hand that does not point and click the pointing hand's thumbstick; "
            "the pointing hand's trigger then casts the picked slot");
    } else if (!on && g_registered) {
        g_on.store(false);
        directinput::Unlisten(&OnFrame, nullptr);
        g_registered = false;
        Release();
        Log("vrcam: openxr - skill wheel off ([openxr] skill_wheel=0)");
    }
}

bool Enabled() { return g_on.load(); }
bool IsOpen() { return g_on.load() && g_open.load(); }
bool TriggerCastsSkill() { return g_on.load() && g_triggerSkill.load(); }
bool GestureRecording() { return g_on.load() && g_gestureRecordingNow.load(); }
uint8_t GestureRecordHand() { return (uint8_t)g_gestureHand.load(); }
bool SettingsTabOn() { return g_on.load() && g_settingsTab.load(); }
void SetTwoGripChord(bool on) { g_twoGripChord.store(on); }
void SetSettingsHost(const hset::video::Env& env) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_env = env;
}

void Tick() {
    if (!g_on.load() || (!viewgate::F4() && !viewgate::Flat())) return;   // (another view than F4 and VR F1 - F3: nothing of ours reads the game)
    {   // the Gestures tab's look at BodyWalk, once a second while the tab is on and has no gestures (the process list costs a few ms: not the frame's)
        static uint64_t lastLook = 0, lastFind = 0;
        const uint64_t now = GetTickCount64();
        bool tab = false;
        std::wstring ini;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            tab = g_cfg.gestureTab && !g_gesturesHave;
            if (tab && (!lastFind || now - lastFind >= 10000)) ini = g_iniPath;
        }
        if (tab && (!lastLook || now - lastLook >= 1000)) {
            lastLook = now;
            g_bwRunning.store(bwstart::Running());
            g_bwHost.store(link::HostAlive());
            if (!ini.empty()) {
                lastFind = now;
                g_bwFound.store(!bwstart::FindExe(ini.substr(0, ini.find_last_of(L'\\') + 1)).empty());
            }
        }
    }
    wheelgame::Tick(true, g_open.load());
}

bool Draw(wheel::Layer* layer, uint32_t* needRevision) {
    if (!g_on.load()) return false;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_ctl.Open() || !g_layer.show) return false;
    *layer = g_layer;
    *needRevision = g_openRevision;
    Painter().Submit(g_view);
    return true;
}

bool Picture(std::vector<uint32_t>* px, uint32_t* revision, uint32_t* width, uint32_t* height) {
    if (!g_on.load()) return false;
    int w = wheelpaint::kSize, h = wheelpaint::kSize;
    if (!Painter().Fetch(*px, revision, &w, &h)) return false;
    *width = (uint32_t)w;
    *height = (uint32_t)h;
    return true;
}

// Everything let go: the wheel closed, the controller's state and the game's snapshot dropped, the painter's and the icons' threads joined.
void Release() {
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_ctl.Reset();
        g_layer = wheel::Layer{};
        g_view = wheel::View{};
        g_snap = wheel::Snapshot{};
        g_snapRevision = 0;
        g_wasOpen = false;
        g_open.store(false);
        g_triggerSkill.store(false);
        g_lastTake[0] = g_lastTake[1] = 0;
        if (g_gestureRecording) link::SetGestureRecording(false);
        g_gestureRecording = false;
        g_gestureRecordingNow.store(false);
        g_gesturesHave = false;
        if (g_storeOn) {
            hset::store::Stop();
            g_storeOn = false;
        }
    }
    hset::store::Join(1500);   // an Apply in flight is finished first
    hsetview::LetGo();
    directinput::SetUiUp(false);
    Painter().Stop();
    wheelicons::Shutdown();
    wheelgame::Reset();
}

void Teardown() {   // the session's end: the same, for a wheel that has been switched on
    if (g_everOn) Release();
}

void Shutdown() {   // the plugin's unload
    if (g_on.load() || g_registered) {
        g_on.store(false);
        directinput::Unlisten(&OnFrame, nullptr);
        g_registered = false;
    }
    if (g_everOn) Release();
}

}  // namespace skillwheel
