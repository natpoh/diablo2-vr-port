// first_start.cpp - see first_start_wire.h. The first-start guide's wiring: the ini in, the input layer's frame in (read only - nothing is
// taken), the request to the settings window out, [onboarding] done written when the window says the guide is over. The flow is
// vr/first_start.h (pure, tested).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "first_start_wire.h"

#include "body_calibration.h"
#include "calibration.h"
#include "direct_input.h"
#include "first_start.h"
#include "skill_wheel.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace firststart {
namespace {

void (*g_log)(const char*) = nullptr;
void LogF(const char* fmt, ...) {
    if (!g_log) return;
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    g_log(b);
}

wchar_t g_ini[MAX_PATH] = L"";
// What the ini says (LoadSettings, vrcam's timer); the guide takes it on the draw thread.
std::atomic<bool> g_show{true}, g_done{false}, g_calibrated{false};
std::atomic<uint32_t> g_cfgSerial{0};   // + 1 with every read that changed something
std::atomic<int> g_request{-1};         // the page the guide asks the window to come up on, -1 none
std::atomic<int> g_ended{0};            // the window's report: 1 finished, 2 closed (taken by the frame)
std::atomic<bool> g_standalone{false}, g_standaloneOpen{false};

// The draw thread's own (the Listen callback).
Guide g_guide;
uint32_t g_cfgSeen = ~0u;
bool g_wantDone = false;       // [onboarding] done=1 is to be in the file: written until the ini says so (a save of the settings' own thread may replace the file)
int g_doneTries = 0;
double g_doneAt = -1e9;
bool g_whyTold = false;

float ReadFloat(const wchar_t* sec, const wchar_t* key, float def) {
    wchar_t b[64], d[64];
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, b, 64, g_ini);
    wchar_t* end = nullptr;
    const float v = wcstof(b, &end);
    return end != b && std::isfinite(v) ? v : def;
}

void OnFrame(const directinput::Frame& f, void*) {
    if (g_cfgSerial.load() == 0) return;   // the ini not read yet: nothing to go by
    if (const uint32_t s = g_cfgSerial.load(); s != g_cfgSeen) {
        g_cfgSeen = s;
        g_guide.Configure(g_show.load(), g_done.load(), g_calibrated.load());
    }
    if (const int e = g_ended.exchange(0); e != 0) {   // the guide page was on show and the window closed (or Done was pressed)
        g_guide.Ended(e == 1 ? End::Finished : End::Skipped);
        g_wantDone = !g_done.load();
        g_doneTries = 0;
        LogF("vrcam: first-start guide - %s", e == 1 ? "finished (Done)" : "closed");
    }
    if (g_wantDone && g_done.load()) g_wantDone = false;   // the file says so now
    if (g_wantDone && f.time - g_doneAt >= 1.0 && g_doneTries < 8) {   // (the settings' own thread may replace the ini under a save: write again until it reads back)
        g_doneAt = f.time;
        ++g_doneTries;
        const bool ok = g_ini[0] && WritePrivateProfileStringW(L"onboarding", L"done", L"1", g_ini);
        LogF("vrcam: first-start guide - [onboarding] done=1 %s", ok ? "written" : "could NOT be written");
    }
    Context c;
    c.time = f.time;
    c.focused = (f.flags & directinput::kFocused) != 0;
    c.f4 = (f.flags & directinput::kOtherView) == 0;
    c.direct = (f.flags & directinput::kDirect) != 0;
    c.menu = (f.flags & directinput::kInMenu) != 0;
    c.uiUp = (f.flags & directinput::kUiUp) != 0 || g_standaloneOpen.load();
    c.measuring = calib::Running();
    c.host = skillwheel::SettingsTabOn() || g_standalone.load();
    const Phase before = g_guide.phase();
    const Out o = g_guide.Step(c);
    if (!g_whyTold && before == Phase::Waiting && c.f4 && !c.menu && (!c.direct || !c.host)) {
        g_whyTold = true;   // once: why it waits
        LogF("vrcam: first-start guide - waiting: it needs %s", !c.direct ? "the plugin's own controls ([openxr] direct_input=1)"
                                                                          : "a settings window to show it in (the skill panel's Settings tab, [openxr] headset_settings=1)");
    }
    if (o.open) {
        g_request.store(o.page);
        LogF("vrcam: first-start guide - asking the settings window to come up on its Guide tab (%s), try %d of %d",
             o.page == kOfferPage ? "the measurement's offer first" : "page 1", g_guide.attempts(), kMaxAttempts);
    }
    if (o.gaveUp) {
        g_request.store(-1);
        LogF("vrcam: first-start guide - the window did not come up %d times: not again this session (Settings > Guide shows it any time)", kMaxAttempts);
    }
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void LoadSettings(const wchar_t* iniPath) {
    if (!g_ini[0] && iniPath) wcscpy_s(g_ini, iniPath);
    if (!g_ini[0]) return;
    const bodycal::Measurement m{ReadFloat(L"calibration", L"eye_height_m", 0.0f), ReadFloat(L"calibration", L"arm_span_m", 0.0f)};
    const bool calibrated = ReadFloat(L"calibration", L"version", 0.0f) == 1.0f && bodycal::Valid(m);
    const bool show = ReadFloat(L"onboarding", L"show", 1.0f) != 0.0f;
    const int doneKey = (int)GetPrivateProfileIntW(L"onboarding", L"done", -1, g_ini);
    const bool done = DoneOf(doneKey, calibrated);
    if (show != g_show.load() || done != g_done.load() || calibrated != g_calibrated.load() || g_cfgSerial.load() == 0) {
        g_show.store(show);
        g_done.store(done);
        g_calibrated.store(calibrated);
        g_cfgSerial.fetch_add(1);
        LogF("vrcam: first-start guide - [onboarding] show=%d, done=%d%s, a measurement %s", show ? 1 : 0, done ? 1 : 0,
             doneKey < 0 ? " (no key: done when measured)" : "", calibrated ? "stored" : "not stored");
    }
}

void RegisterInput() { directinput::Listen(&OnFrame, nullptr); }

int OpenRequest() { return g_request.load(); }
void OpenRequestTaken() { g_request.store(-1); }
void PanelEnded(bool finished) { g_ended.store(finished ? 1 : 2); }
void SetStandalone(bool on, bool open) {
    g_standalone.store(on);
    g_standaloneOpen.store(on && open);
}

}  // namespace firststart
