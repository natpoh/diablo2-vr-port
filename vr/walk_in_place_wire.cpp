// walk_in_place_wire.cpp - [walk_in_place], the wiring: see walk_in_place_wire.h. The decisions are vr/walk_in_place.h's (pure, tested).
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
#include <string>

#include <D2RLPlugin/api.h>

#include "direct_feed.h"   // (direct_input.h, and SetBodyWalkWalkWanted: [walk_in_place] source=1)
#include "log_async.h"
#include "ui_text.h"
#include "view_gate.h"
#include "walk_engine_lib.h"
#include "walk_in_place_wire.h"
#include "walk_line.h"

namespace wipwire {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
Readers g_readers;
bool g_registered = false;

SRWLOCK g_cfgLock = SRWLOCK_INIT;
wip::Config g_cfg;
std::atomic<bool> g_on{false};
std::atomic<bool> g_walks{false};       // on, F4, the library open (the frame thread sets it)
std::atomic<int> g_source{wip::kSourceLibrary};   // [walk_in_place] source
std::atomic<uint64_t> g_stick{0};       // x | y << 16 | the tick it was made << 32 (low 32 bits of GetTickCount64)

// the line's facts (the frame thread writes, the panel reads)
std::atomic<int> g_lineState{0};        // 0 off, 1 not F4, 2 library not usable, 3 standing, 4 walking, 5 BodyWalk walks (its own line)
std::atomic<int> g_libStatus{0};        // welib::Status
std::atomic<float> g_speed{0.0f}, g_cadence{0.0f};
std::atomic<bool> g_directLive{true};

// the frame thread's own
wip::Trims g_told{-1.0f, -1.0f, -1.0f};
wip::RunKey g_runKey;
bool g_runHeld = false;
bool g_needReset = true;
uint32_t g_session = 0;
std::string g_dataDir;
bool g_walkingTold = false;
constexpr directinput::Source kRunSrc = directinput::kSrcOther6;   // (only this file holds command 0x22 under this source)

void Log(const char* text) {
    if (g_ctx) logasync::Info(g_ctx, text);
}
void LogF(const char* fmt, ...) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    Log(b);
}

// BodyWalk's settings folder (as the gesture engine's): %LOCALAPPDATA%\BodyWalkVR, UTF-8.
std::string DataDir() {
    wchar_t w[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", w, MAX_PATH);
    if (!n || n >= MAX_PATH) return std::string();
    std::wstring dir = std::wstring(w, n) + L"\\BodyWalkVR";
    char u[MAX_PATH * 3] = {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, u, (int)sizeof u, nullptr, nullptr);
    return len > 0 ? std::string(u) : std::string();
}

wip::Config Cfg() {
    AcquireSRWLockShared(&g_cfgLock);
    const wip::Config c = g_cfg;
    ReleaseSRWLockShared(&g_cfgLock);
    return c;
}

void PutStick(const wip::Stick& s) {
    const uint64_t at = (uint64_t)(uint32_t)GetTickCount64();
    g_stick.store((uint64_t)(uint16_t)s.lx | ((uint64_t)(uint16_t)s.ly << 16) | (at << 32), std::memory_order_release);
}

void LetGoRun() {
    if (g_runHeld) directinput::Command(0x22, false, kRunSrc);
    g_runHeld = false;
    g_runKey = wip::RunKey{};
}

// Nothing walks this frame: no stick, the Run key let go, the walking line said once.
void Stand(int lineState) {
    PutStick({});
    LetGoRun();
    g_speed.store(0.0f);
    g_lineState.store(lineState);
    if (g_walkingTold) {
        g_walkingTold = false;
        Log("vrcam: walk in place - stopped");
    }
}

// BodyWalk walks ([walk_in_place] source=1): the line from what BodyWalk sends now (any thread).
const char* BodyWalkLineText() {
    thread_local char buf[256];
    wip::BodyWalkFacts f;
    f.f4 = viewgate::F4();
    f.running = g_readers.bodyWalkRunning && g_readers.bodyWalkRunning();
    int16_t x = 0, y = 0;
    f.stickIn = g_readers.bodyWalkStick && g_readers.bodyWalkStick(&x, &y);
    f.walking = f.stickIn && wip::PushedRaw(x, y);
    switch (wip::BodyWalkLineOf(f)) {
    case wip::BodyWalkLine::NotF4: return uitext::Tr("vr.walk.status.not_f4", "Walking in place works in first person (F4).");
    case wip::BodyWalkLine::NotRunning:
        return uitext::Tr("vr.walk.status.bw_not_running", "BodyWalk walks, but no BodyWalk is running: start it (START in D2R VR Settings starts it with the game).");
    case wip::BodyWalkLine::NoStick:
        return uitext::Tr("vr.walk.status.bw_no_stick", "BodyWalk runs but sends no walking stick: in BodyWalk, Steps on, Output Xbox or Game Link.");
    case wip::BodyWalkLine::Walking: {
        const float len = std::sqrt((float)x * x + (float)y * y) / 32767.0f;
        char pct[16];
        snprintf(pct, sizeof pct, "%.0f", std::min(len, 1.0f) * 100.0);
        const std::string s = uitext::TrF("vr.walk.status.bw_walking", "BodyWalk walks the hero: its stick at {stick}%. Its Steps settings set the walk.", {{"stick", pct}});
        snprintf(buf, sizeof buf, "%s", s.c_str());
        return buf;
    }
    case wip::BodyWalkLine::Standing:
    default: return uitext::Tr("vr.walk.status.bw_standing", "BodyWalk walks: step in place. Its Steps settings set the walk; the rows below are the library's.");
    }
}

// The Walk tab's line (any thread: a copy per thread).
const char* Line() {
    thread_local char buf[256];
    const int st = g_lineState.load();
    if (st == 5) return BodyWalkLineText();
    if (st == 0) return uitext::Tr("vr.walk.status.off", "Tick Move by walking: step in place and the hero walks where you look (F4).");
    if (st == 2) {
        const auto ls = (welib::Status)g_libStatus.load();
        if (ls == welib::Status::BadAbi) return uitext::Tr("vr.walk.status.bad_abi", "The walk library is another version: run D2R VR Setup again.");
        if (ls == welib::Status::OpenFailed) return uitext::Tr("vr.walk.status.open_failed", "The walk library would not start: the plugin's log says why.");
        return uitext::Tr("vr.walk.status.missing", "The walk library is not installed (d2r_vr_walk.dll): run D2R VR Setup again.");
    }
    if (st == 1) return uitext::Tr("vr.walk.status.not_f4", "Walking in place works in first person (F4).");
    if (!g_directLive.load()) return uitext::Tr("vr.walk.status.no_direct", "Needs Controls without BodyWalk (D2R VR Settings): then the walk needs no BodyWalk at all.");
    if (st == 4) {
        char pct[16], hz[16];
        snprintf(pct, sizeof pct, "%.0f", g_speed.load() * 100.0);
        snprintf(hz, sizeof hz, "%.1f", (double)g_cadence.load());
        const std::string s = uitext::TrF("vr.walk.status.walking", "Walking: {speed}% speed, {steps} steps a second. Each change applies by itself.",
                                          {{"speed", pct}, {"steps", hz}});
        snprintf(buf, sizeof buf, "%s", s.c_str());
        return buf;
    }
    return uitext::Tr("vr.walk.status.standing", "Standing. Step in place to walk; each change applies by itself.");
}

void OnFrame(const directinput::Frame& f, void*) {
    const bool bodyWalk = g_on.load() && g_source.load() == wip::kSourceBodyWalk;
    directinput::SetBodyWalkWalkWanted(bodyWalk && viewgate::F4());   // ([walk_in_place] source=1: BodyWalk's walking passes in direct mode, F4 only)
    if (!g_on.load() || bodyWalk) {   // off, or BodyWalk walks: the library closed, no stick of ours
        if (welib::Loaded() || g_runHeld) {
            Stand(0);
            welib::Shutdown();
            g_walks.store(false);
            g_told = {-1.0f, -1.0f, -1.0f};
            g_needReset = true;
        }
        g_walks.store(false);
        g_lineState.store(bodyWalk ? 5 : 0);
        return;
    }
    if (!viewgate::F4()) {   // F4 only: in any other view nothing is fed, nothing walks
        g_walks.store(false);
        g_needReset = true;
        Stand(1);
        return;
    }
    if (g_dataDir.empty()) g_dataDir = DataDir();
    const bool open = welib::Ensure(g_dataDir.c_str());
    g_libStatus.store((int)welib::GetStatus());
    g_walks.store(open);
    if (!open) {
        Stand(2);
        return;
    }
    const wip::Config c = Cfg();
    const wip::Trims trims = wip::TrimsOf(c);
    if (trims != g_told) {
        BWWE_Tuning t{};
        t.size = sizeof t;
        t.sensitivity = trims.sensitivity;
        t.speed = trims.speed;
        t.smoothing = trims.smoothing;
        welib::SetTuning(t);
        g_told = trims;
        LogF("vrcam: walk in place - trims to the library: sensitivity %.2f, speed %.2f, smoothing %.2f (0.5 = BodyWalk's walk profile as it is); run point %.0f %%",
             trims.sensitivity, trims.speed, trims.smoothing, c.runAt);
    }
    if (g_needReset || f.session != g_session) {
        welib::Reset();
        g_needReset = false;
        g_session = f.session;
    }
    wip::Gate g;
    g.f4 = true;
    g.inWorld = g_readers.inWorld && g_readers.inWorld();
    g.focused = (f.flags & directinput::kFocused) != 0;
    g.menu = (f.flags & directinput::kInMenu) != 0;
    g.panelUp = (f.flags & directinput::kUiUp) != 0;
    g.chat = g_readers.chatOpen && g_readers.chatOpen();
    const bool play = wip::Play(g);
    BWWE_Frame fr{};
    fr.size = sizeof fr;
    fr.flags = play ? BWWE_FRAME_PLAY : 0u;
    fr.timeS = f.time;
    auto pose = [](BWWE_Pose& o, const directinput::Pose& p) {
        for (int i = 0; i < 3; ++i) o.pos[i] = p.pos[i];
        for (int i = 0; i < 4; ++i) o.rot[i] = p.rot[i];
        o.valid = p.valid;
    };
    pose(fr.head, f.head);
    for (int h = 0; h < 2; ++h) pose(fr.hand[h], f.hand[h].grip);
    BWWE_State st{};
    welib::Feed(fr, &st);
    g_cadence.store(st.cadenceHz);
    g_directLive.store(directinput::Active());
    const float speed = play ? st.speed : 0.0f;
    if (speed < wip::kStanding) {
        Stand(3);
        return;
    }
    const float t = g_readers.runThreshold ? g_readers.runThreshold() : 0.0f;
    PutStick(wip::StickOf(wip::StickLength(speed, c.runAt, t)));
    const bool want = g_runKey.Step(speed, c.runAt, g_readers.runSwitchOn && g_readers.runSwitchOn());
    if (want != g_runHeld) {
        g_runHeld = want;
        directinput::Command(0x22, want, kRunSrc);   // the game's own Run key (hold), every gate of the game's
    }
    g_speed.store(speed);
    g_lineState.store(4);
    if (!g_walkingTold) {
        g_walkingTold = true;
        LogF("vrcam: walk in place - walking (speed %.2f, the game's run threshold %.2f, run point %.0f %%%s)", speed, t, c.runAt,
             wip::Runs(c.runAt) ? "" : ": never runs");
    }
}

}  // namespace

void Init(const D2RL::PluginContext* ctx, const Readers& r) {
    g_ctx = ctx;
    g_readers = r;
    welib::SetLog(&Log);
    walkline::Set(&Line);
    if (!g_registered) g_registered = directinput::Listen(&OnFrame, nullptr);
}

void Configure(const wip::Config& raw) {
    wip::Config c = wip::Clamped(raw);
    if (!wip::kOpen) c.on = false;   // (closed until Serg has tried it: walk_in_place.h kOpen)
    AcquireSRWLockExclusive(&g_cfgLock);
    const bool was = g_cfg.on;
    const int wasSource = g_cfg.source;
    g_cfg = c;
    ReleaseSRWLockExclusive(&g_cfgLock);
    g_source.store(c.source);
    g_on.store(c.on);
    if (!wip::BodyWalkOn(c)) directinput::SetBodyWalkWalkWanted(false);   // (the frames set it again while BodyWalk walks in F4)
    if (c.on) {   // (on, no headset frame yet: the line says standing - or BodyWalk's - until the first)
        const int st = g_lineState.load();
        if (wip::BodyWalkOn(c)) g_lineState.store(5);
        else if (st == 0 || st == 5) g_lineState.store(3);
    }
    if (c.on && (was != c.on || wasSource != c.source))
        Log(wip::BodyWalkOn(c) ? "vrcam: walk in place - by BodyWalk ([walk_in_place] source=1): the library is not loaded; BodyWalk's own walking stick (its pad "
                                 "report or Game Link's Walk block) walks the hero with its Steps settings, in direct mode too (F4)"
                               : "vrcam: walk in place - by the built-in library ([walk_in_place] source=0): d2r_vr_walk.dll, BodyWalk's walking stick not mixed in while it walks (F4)");
    if (was != c.on)
        LogF(c.on ? "vrcam: walk in place - on ([walk_in_place] on=1): BodyWalk's walk library walks the hero in F4 (sensitivity %.0f, speed %.0f, smoothing %.0f, run at %.0f %%)"
                  : "vrcam: walk in place - off: every walk as before",
             c.sensitivity, c.speed, c.smoothing, c.runAt);
}

bool On() { return g_on.load(); }
bool Walks() { return g_on.load() && g_source.load() != wip::kSourceBodyWalk && g_walks.load() && viewgate::F4(); }
bool BodyWalkWalks() { return g_on.load() && g_source.load() == wip::kSourceBodyWalk && viewgate::F4(); }

bool Stick(int16_t* lx, int16_t* ly) {
    if (!g_on.load() || g_source.load() == wip::kSourceBodyWalk) return false;
    const uint64_t w = g_stick.load(std::memory_order_acquire);
    const uint32_t at = (uint32_t)(w >> 32);
    if ((uint32_t)GetTickCount64() - at > 150) return false;
    const int16_t x = (int16_t)(uint16_t)(w & 0xFFFF), y = (int16_t)(uint16_t)((w >> 16) & 0xFFFF);
    if (x == 0 && y == 0) return false;
    *lx = x;
    *ly = y;
    return true;
}

bool IsOurStick(int16_t lx, int16_t ly) {
    int16_t x = 0, y = 0;
    return (lx != 0 || ly != 0) && Stick(&x, &y) && x == lx && y == ly;
}

void Shutdown() {
    if (g_registered) directinput::Unlisten(&OnFrame, nullptr);
    g_registered = false;
    if (g_runHeld) directinput::Command(0x22, false, kRunSrc);
    g_runHeld = false;
    PutStick({});
    welib::Shutdown();
    walkline::Set(nullptr);
}

}  // namespace wipwire
