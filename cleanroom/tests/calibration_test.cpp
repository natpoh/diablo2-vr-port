// The T-pose measurement end to end without a headset (vr/calibration.cpp): synthetic headset frames through Feed, the
// Settings program's button as a changed counter in a real d2r_vr.ini in a temporary folder, the measurement written into
// that file (atomically, with the backup), read back by LoadSettings, and the tick box and "Forget" as the player has them.
//
//   calibration_test        exit code 0 = all passed
//
// What it cannot say: whether a real runtime's STAGE floor, the controllers' grip poses and the headset's height read
// like these frames, or how the panel looks in the headset. Those are the headset's.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "calibration.h"
#include "view_gate.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

std::string ReadFile(const std::wstring& p) {
    std::string s;
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"rb") || !f) return s;
    char b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    std::fclose(f);
    return s;
}
void WriteFileText(const std::wstring& p, const std::string& s) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.c_str(), L"wb") || !f) return;
    std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
}
bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string g_log;
void Log(const char* s) { g_log += s; g_log += '\n'; }

double g_clock = 1000.0;

bool g_f4 = true;
bool TheView() { return g_f4; }

// One headset frame of a good T-pose: eyes 1.63 m up, the grips 0.82 m out each side and 0.2 m under the eyes.
calib::Frame Frame(bool bothTriggers, bool focused = true) {
    calib::Frame f;
    f.time = g_clock += 1.0 / 90.0;
    f.floor = true;
    f.focused = focused;
    f.headTracked = f.handTracked[0] = f.handTracked[1] = true;
    f.head[0] = 0.2f; f.head[1] = 1.63f; f.head[2] = -0.4f;
    f.headRot[0] = 0; f.headRot[1] = 0; f.headRot[2] = 0; f.headRot[3] = 1;
    f.hand[0][0] = f.head[0] - 0.82f; f.hand[0][1] = 1.43f; f.hand[0][2] = f.head[2];   // left
    f.hand[1][0] = f.head[0] + 0.82f; f.hand[1][1] = 1.43f; f.hand[1][2] = f.head[2];   // right
    f.trigger[0] = f.trigger[1] = bothTriggers ? 1.0f : 0.0f;
    return f;
}

}  // namespace

int main() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"d2r_calibration_test_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring ini = dir + L"\\d2r_vr.ini", bak = ini + L".before-calibration", tmpFile = ini + L".calibration.tmp";

    const std::string original =
        "; D2R VR\r\n[arms]\r\nmode=2\r\nscale=1.1 ; reach\r\n\r\n[calibration]\r\n; the T-pose\r\nuse=0\r\nmeasure=0\r\nversion=0\r\neye_height_m=0\r\narm_span_m=0\r\n\r\n"
        "[table]\r\nplace=0\r\n";
    WriteFileText(ini, original);

    calib::SetLogger(Log);
    calib::LoadSettings(ini.c_str());
    Check(calib::MeasuredEyeM() == 0.0f && calib::MeasuredSpanM() == 0.0f, "nothing measured: the old numbers (0)");
    Check(!calib::PanelNow().show && !calib::OwnsTriggers(), "no prompt and no triggers owned before a start");

    // frames with no measurement asked for do nothing
    for (int i = 0; i < 100; ++i) calib::Feed(Frame(true));
    Check(!calib::PanelNow().show && !calib::OwnsTriggers(), "frames without a start: no flow");

    // the Settings button: its counter goes up
    WritePrivateProfileStringW(L"calibration", L"measure", L"1", ini.c_str());
    calib::LoadSettings(ini.c_str());
    calib::Feed(Frame(true));
    Check(calib::OwnsTriggers() && calib::PanelNow().show && calib::PanelNow().image == 0, "the button: the prompt is up, a trigger held from before - waiting for them to be let go");
    calib::Feed(Frame(false));
    Check(calib::PanelNow().image == 1, "triggers let go in the pose: ready");

    // not focused: nothing counts, and what was under way starts over
    for (int i = 0; i < 20; ++i) calib::Feed(Frame(false, false));
    Check(calib::PanelNow().image == 5, "focus lost: starts over (not tracked)");
    for (int i = 0; i < 30; ++i) calib::Feed(Frame(false));
    Check(calib::PanelNow().image == 1, "focus back, released in the pose: ready");
    int frames = 0;
    for (; frames < 200 && calib::PanelNow().image != 9; ++frames) calib::Feed(Frame(true));
    Check(calib::PanelNow().image == 9, "held 1.2 s: saved");
    Check(frames > 100 && frames < 120, "held for about 1.2 s of 90 Hz frames");
    Check(calib::PanelNow().progress == 1.0f, "the bar full");
    Check(calib::OwnsTriggers(), "the triggers are still the measurement's until they are let go");

    // in force at once
    Check(Near(calib::MeasuredEyeM(), 1.63f, 0.002f) && Near(calib::MeasuredSpanM(), 1.64f, 0.002f), "the measured numbers are in force");

    // the file: the keys written, the rest as it was, a backup of the original, no temporary left
    const std::string now = ReadFile(ini);
    Check(now.find("version=1\r\n") != std::string::npos && now.find("use=1\r\n") != std::string::npos, "ini: version=1 and use=1");
    Check(now.find("eye_height_m=1.63") != std::string::npos && now.find("arm_span_m=1.64") != std::string::npos, "ini: the numbers");
    Check(now.find("scale=1.1 ; reach\r\n") != std::string::npos && now.find("[table]\r\nplace=0\r\n") != std::string::npos && now.find("; the T-pose\r\n") != std::string::npos,
          "ini: the other keys and the comments untouched");
    Check(now.find("measure=1\r\n") != std::string::npos, "ini: the button's counter kept (it is the Settings program's)");
    Check(Exists(bak) && ReadFile(bak).find("version=0") != std::string::npos && ReadFile(bak).find("use=0") != std::string::npos &&
              ReadFile(bak).find("version=1") == std::string::npos,
          "the backup holds the file as it was before the first measurement");
    Check(!Exists(tmpFile), "no temporary file left");

    // the triggers let go and a second on: it ends
    for (int i = 0; i < 200 && calib::OwnsTriggers(); ++i) calib::Feed(Frame(false));
    Check(!calib::OwnsTriggers() && !calib::PanelNow().show, "released: the prompt is gone and the triggers are the game's again");

    // read back as vrcam does it on the file's change
    calib::LoadSettings(ini.c_str());
    Check(Near(calib::MeasuredEyeM(), 1.63f, 0.002f) && Near(calib::MeasuredSpanM(), 1.64f, 0.002f), "LoadSettings reads the same numbers");

    // the tick box
    WritePrivateProfileStringW(L"calibration", L"use", L"0", ini.c_str());
    calib::LoadSettings(ini.c_str());
    Check(calib::MeasuredEyeM() == 0.0f && calib::MeasuredSpanM() == 0.0f, "unticked: the old numbers (0)");
    WritePrivateProfileStringW(L"calibration", L"use", L"1", ini.c_str());
    calib::LoadSettings(ini.c_str());
    Check(calib::MeasuredEyeM() > 0.0f, "ticked again: the measured ones");

    // "Forget the measurement" (the Settings program clears the three keys)
    WritePrivateProfileStringW(L"calibration", L"version", L"0", ini.c_str());
    WritePrivateProfileStringW(L"calibration", L"eye_height_m", L"0", ini.c_str());
    WritePrivateProfileStringW(L"calibration", L"arm_span_m", L"0", ini.c_str());
    calib::LoadSettings(ini.c_str());
    Check(calib::MeasuredEyeM() == 0.0f && calib::MeasuredSpanM() == 0.0f, "forgotten: the old numbers (0), the box still ticked");

    // numbers that are not a person are not taken, whatever the file says
    WritePrivateProfileStringW(L"calibration", L"version", L"1", ini.c_str());
    WritePrivateProfileStringW(L"calibration", L"eye_height_m", L"5.2", ini.c_str());
    WritePrivateProfileStringW(L"calibration", L"arm_span_m", L"1.7", ini.c_str());
    calib::LoadSettings(ini.c_str());
    Check(calib::MeasuredEyeM() == 0.0f, "stored eye height 5.2 m: refused");
    WritePrivateProfileStringW(L"calibration", L"eye_height_m", L"1.6", ini.c_str());
    WritePrivateProfileStringW(L"calibration", L"arm_span_m", L"0.4", ini.c_str());
    calib::LoadSettings(ini.c_str());
    Check(calib::MeasuredEyeM() == 0.0f && calib::MeasuredSpanM() == 0.0f, "stored span 0.4 m: refused");

    // a second measurement does not replace the first backup
    WritePrivateProfileStringW(L"calibration", L"measure", L"2", ini.c_str());
    calib::LoadSettings(ini.c_str());
    for (int i = 0; i < 40; ++i) calib::Feed(Frame(false));
    for (int i = 0; i < 200 && calib::PanelNow().image != 9; ++i) calib::Feed(Frame(true));
    Check(calib::PanelNow().image == 9, "a second measurement");
    Check(ReadFile(bak).find("version=0") != std::string::npos && ReadFile(bak).find("eye_height_m=0\r\n") != std::string::npos, "the backup is still the first one");

    // the session ends in the middle of one
    for (int i = 0; i < 200 && calib::OwnsTriggers(); ++i) calib::Feed(Frame(false));
    WritePrivateProfileStringW(L"calibration", L"measure", L"3", ini.c_str());
    calib::LoadSettings(ini.c_str());
    calib::Feed(Frame(false));
    Check(calib::OwnsTriggers(), "a third one begun");
    calib::Stop();
    Check(!calib::OwnsTriggers() && !calib::PanelNow().show, "the session ended: dropped");

    // the file cannot be written (read-only): the measurement is reported as failed, nothing half-written
    WritePrivateProfileStringW(L"calibration", L"measure", L"4", ini.c_str());
    calib::LoadSettings(ini.c_str());
    SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_READONLY);
    for (int i = 0; i < 40; ++i) calib::Feed(Frame(false));
    for (int i = 0; i < 200 && calib::PanelNow().image != 9 && calib::PanelNow().image != 10; ++i) calib::Feed(Frame(true));
    SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_NORMAL);
    Check(calib::PanelNow().image == 10, "an ini that cannot be written: 'could not save'");
    Check(!Exists(tmpFile), "and no temporary file left");

    // ---- F4 only (vr/view_gate.h) ----
    viewgate::Set(&TheView);
    g_f4 = true;
    WritePrivateProfileStringW(L"calibration", L"version", L"1", ini.c_str());
    WritePrivateProfileStringW(L"calibration", L"eye_height_m", L"1.63", ini.c_str());
    WritePrivateProfileStringW(L"calibration", L"arm_span_m", L"1.64", ini.c_str());
    WritePrivateProfileStringW(L"calibration", L"use", L"1", ini.c_str());
    calib::LoadSettings(ini.c_str());
    Check(Near(calib::MeasuredEyeM(), 1.63f, 0.002f) && Near(calib::MeasuredSpanM(), 1.64f, 0.002f), "view: F4 - the measured numbers are in force");
    g_f4 = false;
    Check(calib::MeasuredEyeM() == 0.0f && calib::MeasuredSpanM() == 0.0f, "view: another view - the old numbers (the world scale and the arms as before)");
    g_f4 = true;
    Check(Near(calib::MeasuredEyeM(), 1.63f, 0.002f), "view: back in F4 - the measured numbers again");

    // a measurement under way in F4, the view changes: it stops, the triggers are the layout's, nothing is saved
    WritePrivateProfileStringW(L"calibration", L"measure", L"5", ini.c_str());
    calib::LoadSettings(ini.c_str());
    const std::string iniAsked = ReadFile(ini);
    calib::Feed(Frame(false));
    Check(calib::PanelNow().show, "view: a measurement is up in F4");
    for (int i = 0; i < 30; ++i) calib::Feed(Frame(false));
    Check(calib::PanelNow().image == 1, "view: ready in F4");
    g_f4 = false;
    calib::Feed(Frame(true));
    Check(!calib::PanelNow().show && !calib::OwnsTriggers(), "view: F4 -> another view: the prompt is gone, the triggers are not the measurement's");
    for (int i = 0; i < 200; ++i) calib::Feed(Frame(true));   // a good T-pose with both triggers, held, in another view
    Check(!calib::PanelNow().show && !calib::OwnsTriggers() && ReadFile(ini) == iniAsked, "view: nothing is measured or saved in another view");
    g_f4 = true;
    for (int i = 0; i < 20; ++i) calib::Feed(Frame(true));
    Check(!calib::PanelNow().show && !calib::OwnsTriggers(), "view: back in F4 the cancelled one does not come back by itself");

    // a measurement asked for while in another view waits for F4 (its minute runs)
    g_f4 = false;
    WritePrivateProfileStringW(L"calibration", L"measure", L"6", ini.c_str());
    calib::LoadSettings(ini.c_str());
    for (int i = 0; i < 20; ++i) calib::Feed(Frame(false));
    Check(!calib::PanelNow().show && !calib::OwnsTriggers(), "view: asked for in another view: no prompt there");
    g_f4 = true;
    calib::Feed(Frame(false));
    Check(calib::PanelNow().show, "view: ... and it begins in F4");
    calib::Stop();
    viewgate::Set(nullptr);

    DeleteFileW(ini.c_str());
    DeleteFileW(bak.c_str());
    DeleteFileW(tmpFile.c_str());
    RemoveDirectoryW(dir.c_str());
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    if (g_failed) std::printf("--- log ---\n%s", g_log.c_str());
    return g_failed ? 1 : 0;
}
