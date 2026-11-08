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

#include "banner.h"
#include "direct_input.h"
#include "gesture_actions.h"
#include "gesture_engine.h"
#include "gesture_engine_lib.h"
#include "gesture_engine_wire.h"
#include "log_async.h"
#include "rune_flash.h"
#include "skill_link_wire.h"
#include "skill_wheel.h"
#include "trigger_roles_wire.h"   // [gestures] trigger_left / trigger_right: the pens are the Glyphs triggers' strokes
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

// A game action a recognised gesture does (vr/gesture_actions.h), the game's own way - F4 and in play only (this listener's gates). An "Act" held
// down is let go 80 ms later (g_actUp).
uint32_t g_actBits = 0;
ULONGLONG g_actUpAt = 0;
void DoAction(const char* id, uint32_t gesture) {
    const gact::Def* d = gact::All().Find(id);
    if (!d) {
        LogF("vrcam: gesture engine - gesture %u does '%.31s', an action this build does not know: nothing done", gesture, id);
        return;
    }
    switch (d->kind) {
    case gact::Kind::Item: {   // the hero's own tome, else scroll: its skill cast as a skill is (the game's own use of the item)
        const int32_t* ids = d->code == gact::kItemTownPortal ? gact::kTownPortalSkills : gact::kIdentifySkills;
        int32_t found = -1;
        for (int k = 0; k < 2 && found < 0; ++k)
            for (uint32_t i = 0; i < std::min<uint32_t>(g_skills.count, BW_LINK_SKILLS_MAX); ++i)
                if (g_skills.skill[i].id == ids[k]) { found = ids[k]; break; }
        if (found < 0) {
            banner::Show(d->code == gact::kItemTownPortal ? "No Town Portal tome or scroll" : "No Identify tome or scroll", false, GetTickCount64(), 2500);
            LogF("vrcam: gesture engine - gesture %u: %s - the hero has no tome or scroll for it (the HUD says so)", gesture, d->name);
            return;
        }
        skilllinkwire::CastFromEngine(found);
        LogF("vrcam: gesture engine - gesture %u: %s - skill %d cast the game's way", gesture, d->name, (int)found);
        return;
    }
    case gact::Kind::Command:
        directinput::Tap(d->code);   // (the game's executor, as its key: every gate of the game)
        break;
    case gact::Kind::Act:
        directinput::Action(d->code, true);
        g_actBits |= d->code;
        g_actUpAt = GetTickCount64() + 80;
        break;
    case gact::Kind::Pad: {
        directinput::PadPress p{};
        p.buttons = (uint16_t)(d->code & 0xFFFFu);
        p.lt = (d->code & gact::kPadLT) ? 255 : 0;
        p.rt = (d->code & gact::kPadRT) ? 255 : 0;
        directinput::PadPulse(p, 65);
        break;
    }
    }
    LogF("vrcam: gesture engine - gesture %u does %s (%.31s)", gesture, d->name, d->id);
}

// The hand that drew a recognised gesture (0 left, 1 right): its own hand, or for an either-hand glyph the trigger that drew the last stroke.
int DrawingHand(uint32_t gesture) {
    static BW_LinkGestures list;   // (large: not on the stack; the listener's thread)
    gelib::List(&list);
    uint8_t hand = BW_LINK_GH_RIGHT;
    for (uint32_t i = 0; i < std::min<uint32_t>(list.count, BW_LINK_GESTURES_MAX); ++i)
        if (list.g[i].id == gesture) hand = list.g[i].hand;
    if (hand == BW_LINK_GH_LEFT) return 0;
    if (hand == BW_LINK_GH_RIGHT) return 1;
    uint64_t at = 0;
    const int last = trigroleswire::LastStrokeHand(&at);
    return last >= 0 && GetTickCount64() - at < 2000 ? last : 1;
}
// (the listener's own thread: the engine is open here)
uint8_t ModeOfLocked(uint32_t gesture) {
    uint8_t m = 0;
    return gelib::GetMode(gesture, &m) == BW_LINK_GR_OK ? m : 0;
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
    pi.roles = trigroleswire::Active();
    pi.rolePen[0] = trigroleswire::Pen(0);
    pi.rolePen[1] = trigroleswire::Pen(1);
    uint8_t pen[2];
    Pens(pi, pen);
    gelib::Feed(FrameOf(f, pi.play && !pi.panelOpen, pen));
    // what it recognised: the game's own cast (skill_link_wire's caster, F4 / play only)
    if (g_actBits && GetTickCount64() >= g_actUpAt) {   // an action held for a tap: let go
        directinput::Action(g_actBits, false);
        g_actBits = 0;
    }
    int n = 0;
    if (gelib::HasActions()) {   // skills and game actions (vr/gesture_actions.h)
        BWGE_Cast2 casts[8];
        n = gelib::PollCasts2(casts, 8);
        for (int i = 0; i < n; ++i) {
            casts[i].action[sizeof casts[i].action - 1] = 0;
            if (casts[i].action[0]) DoAction(casts[i].action, casts[i].gesture);
            else if (casts[i].skill >= 0 && gelib::HasModes() && ModeOfLocked(casts[i].gesture) == 1)   // select: onto the drawing hand's trigger
                skilllinkwire::ArmFromEngine(casts[i].skill, DrawingHand(casts[i].gesture));
            else if (casts[i].skill >= 0) skilllinkwire::CastFromEngine(casts[i].skill);
        }
    } else {
        BWGE_Cast casts[8];
        n = gelib::PollCasts(casts, 8);
        for (int i = 0; i < n; ++i) skilllinkwire::CastFromEngine(casts[i].skill);
    }
    if (n > 0) {
        runeflash::Shared().Trigger(GetTickCount64());   // (recognised: the trail flashes 100 ms, vr/rune_flash.h)
        trigroleswire::Recognised(f.time);               // (a glyph trigger's release then casts nothing: it was a glyph)
    }
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

bool PoseZone(uint32_t gesture, BWGE_PoseZone* out) {
    if (!Active() || !gelib::HasPoseZones() || !out) return false;
    return gelib::GetPoseZone(gesture, out) == BW_LINK_GR_OK;
}
int SetPoseZone(uint32_t gesture, float radiusM) { return Active() ? gelib::SetPoseZone(gesture, radiusM) : BW_LINK_GR_BAD; }
int Take(uint32_t gesture, BWGE_TakeSample* out, int max) { return Active() ? gelib::GetTake(gesture, out, max) : 0; }
bool HasActions() { return Active() && gelib::HasActions(); }
int BindAction(uint32_t gesture, const char* action) { return HasActions() ? gelib::BindAction(gesture, action) : BW_LINK_GR_BAD; }
bool ActionOf(uint32_t gesture, char* out, uint32_t cap) { return HasActions() && gelib::GetAction(gesture, out, cap) == BW_LINK_GR_OK; }
bool HasSetHand() { return Active() && gelib::HasSetHand(); }
int SetHand(uint32_t gesture, uint8_t hand) { return HasSetHand() ? gelib::SetHand(gesture, hand) : BW_LINK_GR_BAD; }
bool HasModes() { return Active() && gelib::HasModes(); }
int SetMode(uint32_t gesture, uint8_t mode) { return HasModes() ? gelib::SetMode(gesture, mode) : BW_LINK_GR_BAD; }
uint8_t ModeOf(uint32_t gesture) {
    uint8_t m = 0;
    return HasModes() && gelib::GetMode(gesture, &m) == BW_LINK_GR_OK ? m : 0;
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
