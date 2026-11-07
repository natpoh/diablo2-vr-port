// gesture_engine_wire.cpp - [gestures] engine_in_game, the wiring: see gesture_engine_wire.h. The decisions are vr/gesture_engine.h's (pure, tested).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>

#include <D2RLPlugin/api.h>

#include "direct_input.h"
#include "gesture_engine.h"
#include "gesture_engine_lib.h"
#include "gesture_engine_wire.h"
#include "log_async.h"
#include "skill_link_wire.h"
#include "skill_wheel.h"
#include "view_gate.h"

namespace gengine {
namespace {

const D2RL::PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false};
std::atomic<int> g_playPens{kPenBoth};
std::atomic<bool> g_active{false};    // on and open (the frame thread sets it)
bool g_registered = false;

// the frame thread's own
Answers g_answers;
uint32_t g_setRev = 0;               // the skills' revision the engine was told
bool g_setTold = false;
BW_LinkSkills g_skills;              // (12 KB: not on the stack)
BW_LinkVrRune g_trailNow[BW_LINK_VRRUNES_MAX];
std::string g_dataDir;

SRWLOCK g_trailLock = SRWLOCK_INIT;  // the trail's copy for the runes layer
BW_LinkVrRune g_trail[BW_LINK_VRRUNES_MAX];
uint32_t g_trailCount = 0;

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

// BodyWalk's settings folder (the portable and the Steam BodyWalk both keep theirs there): %LOCALAPPDATA%\BodyWalkVR, UTF-8.
std::string DataDir() {
    wchar_t w[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", w, MAX_PATH);
    if (!n || n >= MAX_PATH) return std::string();
    std::wstring dir = std::wstring(w, n) + L"\\BodyWalkVR";
    char u[MAX_PATH * 3] = {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, u, (int)sizeof u, nullptr, nullptr);
    return len > 0 ? std::string(u) : std::string();
}

void ClearTrail() {
    AcquireSRWLockExclusive(&g_trailLock);
    g_trailCount = 0;
    ReleaseSRWLockExclusive(&g_trailLock);
}

struct Calls {   // gelib as gengine::Do's engine
    int Record(uint32_t g, uint8_t t, uint8_t h, int32_t s) { return gelib::Record(g, t, h, s); }
    int Stop() { return gelib::Stop(); }
    int Keep() { return gelib::Keep(); }
    int Discard() { return gelib::Discard(); }
    int Clear(uint32_t g) { return gelib::Clear(g); }
    int Bind(uint32_t g, int32_t s) { return gelib::Bind(g, s); }
    int SetColor(uint32_t g, uint32_t c) { return gelib::SetColor(g, c); }
    int SetThreshold(uint32_t g, float v) { return gelib::SetThreshold(g, v); }
};

// Once per headset frame on the draw thread.
void OnFrame(const directinput::Frame& f, void*) {
    if (!g_on.load()) {
        if (gelib::Loaded()) {
            gelib::Shutdown();
            g_active.store(false);
            g_setTold = false;
            ClearTrail();
        }
        return;
    }
    if (!viewgate::F4()) {   // F4 only: in any other view nothing is fed (a take the tab started is stopped by the tab itself)
        ClearTrail();
        return;
    }
    if (g_dataDir.empty()) g_dataDir = DataDir();
    const bool open = gelib::Ensure(g_dataDir.c_str());
    g_active.store(open);
    if (!open) return;
    // the hero's class and skills, when they changed
    uint32_t rev = 0;
    if (skilllinkwire::LatestSkills(nullptr, &rev) && (!g_setTold || rev != g_setRev) && skilllinkwire::LatestSkills(&g_skills, &rev)) {
        static BWGE_Skill skills[BW_LINK_SKILLS_MAX];
        const uint32_t n = std::min<uint32_t>(g_skills.count, BW_LINK_SKILLS_MAX);
        for (uint32_t i = 0; i < n; ++i) {
            skills[i].id = g_skills.skill[i].id;
            std::snprintf(skills[i].name, sizeof skills[i].name, "%.47s", g_skills.skill[i].name);
        }
        gelib::SetSet(g_skills.setKey, g_skills.setName, skills, n);
        g_setRev = rev;
        g_setTold = true;
        LogF("vrcam: gesture engine - the hero's class '%.31s' and %u skills given to the engine (revision %u)", g_skills.setKey, n, rev);
    }
    // the pens of this frame, the frame fed
    const bool focused = (f.flags & directinput::kFocused) != 0, inMenu = (f.flags & directinput::kInMenu) != 0;
    PenIn pi;
    pi.play = focused && !inMenu;
    pi.panelOpen = skillwheel::IsOpen();
    pi.recording = skillwheel::GestureRecording();
    pi.recHand = skillwheel::GestureRecordHand();
    pi.playPens = g_playPens.load();
    uint8_t pen[2];
    Pens(pi, pen);
    gelib::Feed(FrameOf(f, pi.play && !pi.panelOpen, pen));
    // what it recognised: the game's own cast (skill_link_wire's caster, F4 / play only)
    BWGE_Cast casts[8];
    const int n = gelib::PollCasts(casts, 8);
    for (int i = 0; i < n; ++i) skilllinkwire::CastFromEngine(casts[i].skill);
    // the trail, for the runes layer
    const uint32_t t = gelib::Trail(g_trailNow, BW_LINK_VRRUNES_MAX);
    AcquireSRWLockExclusive(&g_trailLock);
    g_trailCount = std::min<uint32_t>(t, BW_LINK_VRRUNES_MAX);
    for (uint32_t i = 0; i < g_trailCount; ++i) g_trail[i] = g_trailNow[i];
    ReleaseSRWLockExclusive(&g_trailLock);
}

}  // namespace

void Init(const D2RL::PluginContext* ctx) {
    g_ctx = ctx;
    gelib::SetLog(&Log);
}

void Configure(bool on, int playPens) {
    const bool was = g_on.exchange(on);
    g_playPens.store(playPens >= kPenNone && playPens <= kPenBoth ? playPens : kPenBoth);
    skilllinkwire::SetEngine(on);
    if (on && !g_registered) {
        if (!directinput::Listen(&OnFrame, nullptr)) {
            Log("vrcam: gesture engine - the input layer's listener table is full: no engine");
            return;
        }
        g_registered = true;   // (kept: off is handled in the frame - the engine is closed on the thread that calls it)
    }
    if (on != was) {
        static const char* const kPens[] = {"none (only the Gestures tab's takes)", "the left trigger", "the right trigger", "both triggers"};
        LogF("vrcam: gesture engine - %s ([gestures] engine_in_game; pens in play: %s)",
             on ? "on: the gestures are recorded and recognised in the game (d2r_vr_gestures.dll), F4" : "off", kPens[g_playPens.load()]);
    }
}

bool On() { return g_on.load(); }
bool Active() { return g_on.load() && g_active.load(); }
bool Missing() {
    if (!g_on.load()) return false;
    const gelib::Status s = gelib::GetStatus();
    return s != gelib::Status::Loaded && s != gelib::Status::NotLooked;
}

bool List(BW_LinkGestures* out) {
    if (!Active() || !out) return false;
    gelib::List(out);
    g_answers.Stamp(*out);
    return true;
}

uint32_t Push(const gesturecmd::Cmd& c) {
    if (!Active()) return 0;
    Calls calls;
    return g_answers.Push(calls, c);
}

uint32_t Trail(BW_LinkVrRune* out, uint32_t max) {
    if (!out || !max) return 0;
    AcquireSRWLockShared(&g_trailLock);
    const uint32_t n = std::min(max, g_trailCount);
    for (uint32_t i = 0; i < n; ++i) out[i] = g_trail[i];
    ReleaseSRWLockShared(&g_trailLock);
    return n;
}

void Shutdown() {
    g_on.store(false);
    if (g_registered) {
        directinput::Unlisten(&OnFrame, nullptr);
        g_registered = false;
    }
    gelib::Shutdown();
    g_active.store(false);
    ClearTrail();
}

}  // namespace gengine
