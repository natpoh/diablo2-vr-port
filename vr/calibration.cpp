// calibration.cpp - see calibration.h. The T-pose measurement of the standing eye height and the controller span: the
// flow is vr/body_calibration.h (pure, tested); this is the ini in and out, the headset frame in, the prompt's state out.
//
// The idea is bearstonem/d2r-pcvr's (MIT, steamvr/body_calibration.h): a T-pose held 1.2 s saves two numbers that set the
// world scale and the hero's arm proportions. The file handling, the panel and the wiring are ours.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "calibration.h"

#include "body_calibration.h"
#include "ini_patch.h"
#include "view_gate.h"

#include <windows.h>

#include <atomic>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace calib {
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

// What the ini says (LoadSettings) and what is in force.
std::atomic<bool> g_use{false}, g_seated{false};
std::atomic<float> g_eye{0.0f}, g_span{0.0f};   // in force: 0 = not (unticked, nothing measured, not valid)
std::atomic<float> g_storedEye{0.0f};           // the stored eye height, in force or not (a seated measurement keeps it)
std::atomic<int> g_measure{INT_MIN};            // the Settings button's counter; the first value seen is only remembered
std::atomic<bool> g_startAsked{false};
std::atomic<ULONGLONG> g_startAt{0};
wchar_t g_ini[MAX_PATH] = L"";
constexpr ULONGLONG kAskedForMs = 60000;        // a start asked for while no headset frames come is let go after this

// The flow lives on the headset frame's thread; what others read of it is mirrored.
bodycal::Flow g_flow;
bool g_flowSeated = false;   // the measurement under way reads the arm span only (seated when it began)
std::atomic<bool> g_owns{false};
std::atomic<int> g_image{-1};
std::atomic<float> g_progress{0.0f};

float ReadFloat(const wchar_t* key, float def) {
    wchar_t b[64], d[64];
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(L"calibration", key, d, b, 64, g_ini);
    wchar_t* end = nullptr;
    const float v = wcstof(b, &end);
    return end != b && std::isfinite(v) ? v : def;
}

bool ReadAll(const wchar_t* path, std::string* out, FILETIME* written) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileTime(h, nullptr, nullptr, written) && GetFileSizeEx(h, &size) && size.QuadPart < 16 * 1024 * 1024;
    if (ok) {
        out->resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = ReadFile(h, out->data(), (DWORD)out->size(), &got, nullptr) && got == out->size();
    }
    CloseHandle(h);
    return ok;
}

bool WriteAll(const wchar_t* path, const std::string& data) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size();
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    return ok;
}

bool WriteTimeOf(const wchar_t* path, FILETIME* t) {
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return false;
    *t = fa.ftLastWriteTime;
    return true;
}

// The measurement into d2r_vr.ini: its text patched in memory, written beside it and swapped in with one replace - a
// reader sees the old file or the new one, never half of one, and a crash leaves one of the two. The settings program
// writes the same file; if it did between our read and our replace, we read again. The file as it was before the first
// measurement is kept once as d2r_vr.ini.before-calibration. A file the patch cannot read (UTF-16) goes through
// WritePrivateProfileString instead.
// Seated (m.eyeHeight 0): the arm span only - the stored eye height is left as it is.
bool SaveToIni(bodycal::Measurement m) {
    if (!g_ini[0]) return false;
    const bool withEye = m.eyeHeight > 0.0f;
    char eye[32], span[32];
    snprintf(eye, sizeof eye, "%.4f", m.eyeHeight);
    snprintf(span, sizeof span, "%.4f", m.armSpan);
    std::wstring tmp = std::wstring(g_ini) + L".calibration.tmp", bak = std::wstring(g_ini) + L".before-calibration";
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::string text;
        FILETIME read{}, now{};
        if (!ReadAll(g_ini, &text, &read)) break;
        if ((withEye && !inipatch::Set(text, "calibration", "eye_height_m", eye)) || !inipatch::Set(text, "calibration", "arm_span_m", span) ||
            !inipatch::Set(text, "calibration", "version", "1") || !inipatch::Set(text, "calibration", "use", "1"))
            break;
        if (!WriteAll(tmp.c_str(), text)) { DeleteFileW(tmp.c_str()); return false; }
        if (!WriteTimeOf(g_ini, &now) || CompareFileTime(&read, &now) != 0) {   // written by someone meanwhile
            DeleteFileW(tmp.c_str());
            continue;
        }
        if (GetFileAttributesW(bak.c_str()) == INVALID_FILE_ATTRIBUTES) CopyFileW(g_ini, bak.c_str(), TRUE);
        if (ReplaceFileW(g_ini, tmp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) return true;
        LogF("vrcam: calibration - could not replace d2r_vr.ini (error %lu)", GetLastError());
        DeleteFileW(tmp.c_str());
        break;
    }
    wchar_t w1[32], w2[32];
    swprintf_s(w1, L"%S", eye);
    swprintf_s(w2, L"%S", span);
    return (!withEye || WritePrivateProfileStringW(L"calibration", L"eye_height_m", w1, g_ini)) && WritePrivateProfileStringW(L"calibration", L"arm_span_m", w2, g_ini) &&
           WritePrivateProfileStringW(L"calibration", L"version", L"1", g_ini) && WritePrivateProfileStringW(L"calibration", L"use", L"1", g_ini);
}

int MessageFor(const bodycal::Flow& f) {
    using bodycal::Hint;
    using bodycal::Phase;
    switch (f.phase) {
    case Phase::Idle: return -1;
    case Phase::Ready: return 1;
    case Phase::Hold:
    case Phase::Saving: return 2;
    case Phase::Saved: return 9;
    case Phase::Failed: return 10;
    case Phase::Release:
        switch (f.hint) {
        case Hint::Floor: return 3;
        case Hint::Height: return 4;
        case Hint::Tracking: return 5;
        case Hint::Upright: return 6;
        case Hint::Arms: return 7;
        case Hint::Still: return 8;
        default: return 0;
        }
    }
    return 0;
}

// The prompt's picture: the message, from the seated set while a seated measurement runs.
int ImageFor(const bodycal::Flow& f) {
    const int m = MessageFor(f);
    return m >= 0 && g_flowSeated ? m + kPanelMessages : m;
}

void Mirror() {
    g_owns.store(g_flow.OwnsTriggers());
    g_image.store(ImageFor(g_flow));
    g_progress.store(g_flow.phase == bodycal::Phase::Saving || g_flow.phase == bodycal::Phase::Saved ? 1.0f : g_flow.progress);
}

const char* PhaseName(bodycal::Phase p) {
    static const char* const n[] = {"idle", "release", "ready", "hold", "saving", "saved", "failed"};
    return n[(int)p];
}
const char* HintName(bodycal::Hint h) {
    static const char* const n[] = {"pose ok", "let go of the triggers", "no floor (LOCAL space)", "eye height out of range", "not tracked",
                                    "head tilted", "arms not out level", "moved", "saved", "could not save"};
    return n[(int)h];
}

}  // namespace

void SetLogger(void (*log)(const char*)) { g_log = log; }

void LoadSettings(const wchar_t* iniPath) {
    if (!g_ini[0] && iniPath) wcscpy_s(g_ini, iniPath);
    if (!g_ini[0]) return;
    g_use.store(ReadFloat(L"use", 0.0f) != 0.0f);
    const bool seated = ReadFloat(L"seated", 0.0f) != 0.0f;
    if (seated != g_seated.exchange(seated))
        LogF("vrcam: calibration - played %s ([calibration] seated=%d): %s", seated ? "seated" : "standing", (int)seated,
             seated ? "the hero's eyes are at the head's height at the recenter (F11), a measurement reads the arm span only"
                    : "the hero's eyes at the floor plus the measured eye height, a measurement reads both");
    const bodycal::Measurement m{ReadFloat(L"eye_height_m", 0.0f), ReadFloat(L"arm_span_m", 0.0f)};
    g_storedEye.store(m.eyeHeight);
    const bodycal::Measurement f = ReadFloat(L"version", 0.0f) == 1.0f && g_use.load() ? bodycal::InForce(seated, m) : bodycal::Measurement{};
    const bool inForce = f.armSpan > 0.0f;
    const float eye = f.eyeHeight, span = f.armSpan;
    if (eye != g_eye.load() || span != g_span.load()) {
        g_eye.store(eye);
        g_span.store(span);
        if (inForce && eye > 0.0f)
            LogF("vrcam: calibration - the measured eye height %.3f m and arm span %.3f m are in use ([calibration] use=1)", eye, span);
        else if (inForce)
            LogF("vrcam: calibration - seated: the measured arm span %.3f m is in use; no stored standing eye height to go with it, so the "
                 "world's scale is the height from BodyWalk / [stereo] user_height_m, as before", span);
        else
            LogF("vrcam: calibration - %s: the height from BodyWalk / [stereo] user_height_m and [arms] scale, as before",
                 !g_use.load() ? "not used (unticked)" : "nothing valid stored");
    }
    const int measure = (int)ReadFloat(L"measure", 0.0f);
    const int before = g_measure.exchange(measure);
    if (before != INT_MIN && before != measure) {
        LogF("vrcam: calibration - D2R VR Authentic Settings asked for a measurement");
        RequestMeasure();
    }
}

void RequestMeasure() {
    g_startAt.store(GetTickCount64());
    g_startAsked.store(true);   // taken by the next headset frame (Feed)
}

bool Running() { return g_image.load() >= 0 || (g_startAsked.load() && GetTickCount64() - g_startAt.load() <= kAskedForMs); }

// F4 only (vr/view_gate.h): in the other views the world's scale and the arms are the old numbers, as if nothing were measured.
float MeasuredEyeM() { return viewgate::F4() ? g_eye.load() : 0.0f; }
float MeasuredSpanM() { return viewgate::F4() ? g_span.load() : 0.0f; }
bool Seated() { return g_seated.load(); }
bool OwnsTriggers() { return g_owns.load(); }

void Feed(const Frame& f) {
    using namespace bodycal;
    if (!viewgate::F4()) {   // F4 only: a measurement under way ends (its prompt goes, the triggers are let go); one asked for waits for F4 (its minute runs)
        if (g_flow.Active()) {
            g_flow.Cancel();
            LogF("vrcam: calibration - stopped (the view is not F4)");
        }
        if (g_owns.load() || g_image.load() >= 0) Mirror();
        return;
    }
    if (g_startAsked.exchange(false)) {
        if (GetTickCount64() - g_startAt.load() <= kAskedForMs) {
            g_flow.Begin();
            g_flowSeated = g_seated.load();
            LogF(g_flowSeated ? "vrcam: calibration - started, seated (the arm span only): sit upright, arms out level, let go of both triggers, "
                                "then hold both for 1.2 s"
                              : "vrcam: calibration - started: stand upright, arms out level, let go of both triggers, then hold both for 1.2 s");
        }
    }
    if (!g_flow.Active()) {
        if (g_owns.load()) Mirror();
        return;
    }
    const Phase phaseWas = g_flow.phase;
    const Hint hintWas = g_flow.hint;
    if (!f.focused) {
        g_flow.Suspend();   // another app has the input: what was under way starts over
    } else {
        Sample s;
        s.time = f.time;
        s.floor = f.floor;
        s.headTracked = f.headTracked;
        s.handTracked[kLeft] = f.handTracked[0];
        s.handTracked[kRight] = f.handTracked[1];
        s.cancel = f.cancel;
        s.seated = g_flowSeated;
        s.head = {f.head[0], f.head[1], f.head[2]};
        s.headRot = {f.headRot[0], f.headRot[1], f.headRot[2], f.headRot[3]};
        for (int h = 0; h < 2; ++h) {
            s.hand[h] = {f.hand[h][0], f.hand[h][1], f.hand[h][2]};
            s.trigger[h] = f.trigger[h];
        }
        if (g_flow.Step(s)) {
            const Measurement m = g_flow.Get();
            if (SaveToIni(m)) {
                g_flow.Saved(f.time);
                // in force at once (the ini's change reads back the same): a seated one with the eye height stored before
                const Measurement now = g_flowSeated ? Measurement{g_storedEye.load(), m.armSpan} : m;
                if (!g_flowSeated) g_storedEye.store(m.eyeHeight);
                const Measurement in = InForce(g_seated.load(), now);
                g_eye.store(in.eyeHeight);
                g_span.store(in.armSpan);
                g_use.store(true);
                if (g_flowSeated)
                    LogF("vrcam: calibration - measured seated: arm span %.3f m; saved to d2r_vr.ini [calibration] (the stored eye height %.3f m kept%s), in use",
                         m.armSpan, now.eyeHeight, in.eyeHeight > 0.0f ? "" : ": none usable - the height from BodyWalk / [stereo] user_height_m");
                else
                    LogF("vrcam: calibration - measured: eye height %.3f m, arm span %.3f m (ratio %.2f); saved to d2r_vr.ini [calibration], in use", m.eyeHeight,
                         m.armSpan, m.armSpan / m.eyeHeight);
            } else {
                g_flow.SaveFailed();
                LogF("vrcam: calibration - measured eye height %.3f m, arm span %.3f m, but d2r_vr.ini could not be written", m.eyeHeight, m.armSpan);
            }
        }
    }
    if (g_flow.phase != phaseWas || g_flow.hint != hintWas)
        LogF("vrcam: calibration - %s (%s)", PhaseName(g_flow.phase), HintName(g_flow.hint));
    Mirror();
}

void Stop() {
    if (g_flow.Active()) {
        g_flow.Cancel();
        LogF("vrcam: calibration - stopped (the session ended)");
    }
    Mirror();
}

Panel PanelNow() {
    Panel p;
    p.image = g_image.load();
    p.show = p.image >= 0;
    p.progress = g_progress.load();
    return p;
}

}  // namespace calib
