// first_start.cpp - see first_start_wire.h. The first-start guide's wiring: the ini in, the input layer's frame in (read only - nothing is
// taken), the measurement asked for, [onboarding] done written, the page out. The flow is vr/first_start.h (pure, tested).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "first_start_wire.h"

#include "body_calibration.h"
#include "calibration.h"
#include "direct_input.h"
#include "first_start.h"

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
std::atomic<bool> g_show{true}, g_done{false}, g_calibrated{false}, g_stickOurs{true};
std::atomic<uint32_t> g_cfgSerial{0};   // + 1 with every read that changed something
std::atomic<uint32_t> g_reopen{0};      // + 1 with every press of the panel's button
std::atomic<int> g_page{-1};            // the page shown now, -1 none

// The draw thread's own (the Listen callback).
Guide g_guide;
uint32_t g_cfgSeen = ~0u, g_reopenSeen = 0;
int g_pageWas = -1;
bool g_whyTold = false;

float ReadFloat(const wchar_t* sec, const wchar_t* key, float def) {
    wchar_t b[64], d[64];
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, b, 64, g_ini);
    wchar_t* end = nullptr;
    const float v = wcstof(b, &end);
    return end != b && std::isfinite(v) ? v : def;
}

const char* PageName(int page) {
    static const char* const n[kPageCount] = {"the measurement's offer", "walk, turn, fight", "open things", "the skill panel and the settings",
                                              "belt, weapons, bow", "views"};
    return page >= 0 && page < kPageCount ? n[page] : "?";
}

void OnFrame(const directinput::Frame& f, void*) {
    if (g_cfgSerial.load() == 0) return;   // the ini not read yet: nothing to go by
    if (const uint32_t s = g_cfgSerial.load(); s != g_cfgSeen) {
        g_cfgSeen = s;
        g_guide.Configure(g_show.load(), g_done.load(), g_calibrated.load());
    }
    if (const uint32_t r = g_reopen.load(); r != g_reopenSeen) {
        g_reopenSeen = r;
        g_guide.Reopen();
        LogF("vrcam: first-start guide - opened again from the headset panel");
    }
    Context c;
    c.time = f.time;
    c.focused = (f.flags & directinput::kFocused) != 0;
    c.f4 = (f.flags & directinput::kOtherView) == 0;
    c.direct = (f.flags & directinput::kDirect) != 0;
    c.stickOurs = g_stickOurs.load();
    c.menu = (f.flags & directinput::kInMenu) != 0;
    c.uiUp = (f.flags & directinput::kUiUp) != 0;
    c.measuring = calib::Running();
    c.stick[0] = f.hand[1].stick[0];
    c.stick[1] = f.hand[1].stick[1];
    const Phase before = g_guide.phase();
    const Out o = g_guide.Step(c);
    if (!g_whyTold && before != Phase::Closed && before != Phase::Idle && c.f4 && !c.menu && (!c.direct || !c.stickOurs)) {
        g_whyTold = true;   // once: why it waits
        LogF("vrcam: first-start guide - waiting: it needs %s", !c.direct ? "the plugin's own controls ([openxr] direct_input=1)"
                                                                          : "the right stick to be the plugin's ([turn] right_stick=1)");
    }
    if (o.measure) {
        calib::RequestMeasure();
        LogF("vrcam: first-start guide - the player asked for the T-pose measurement");
    }
    if (o.end != End::None) {
        const bool ok = g_ini[0] && WritePrivateProfileStringW(L"onboarding", L"done", L"1", g_ini);
        LogF("vrcam: first-start guide - %s; [onboarding] done=1 %s", o.end == End::Finished ? "finished" : "closed (the right stick held up)",
             ok ? "written" : "could NOT be written");
    }
    const int page = o.show ? o.page : -1;
    if (page != g_pageWas) {
        if (page >= 0) LogF("vrcam: first-start guide - page %d of %d: %s", page, kGuidePages, PageName(page));
        g_pageWas = page;
    }
    g_page.store(page);
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
    const bool stickOurs = ReadFloat(L"turn", L"right_stick", 1.0f) != 0.0f;   // vrcam's own default ([turn] right_stick, the pad hook)
    if (show != g_show.load() || done != g_done.load() || calibrated != g_calibrated.load() || stickOurs != g_stickOurs.load() || g_cfgSerial.load() == 0) {
        g_show.store(show);
        g_done.store(done);
        g_calibrated.store(calibrated);
        g_stickOurs.store(stickOurs);
        g_cfgSerial.fetch_add(1);
        LogF("vrcam: first-start guide - [onboarding] show=%d, done=%d%s, a measurement %s", show ? 1 : 0, done ? 1 : 0,
             doneKey < 0 ? " (no key: done when measured)" : "", calibrated ? "stored" : "not stored");
    }
}

void RegisterInput() { directinput::Listen(&OnFrame, nullptr); }

void Reopen() { g_reopen.fetch_add(1); }

Panel PanelNow() {
    Panel p;
    p.page = g_page.load();
    p.show = p.page >= 0;
    return p;
}

}  // namespace firststart
