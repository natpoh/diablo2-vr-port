// The T-pose measurement (vr/body_calibration.h) and the ini text patch it saves through (vr/ini_patch.h), on synthetic
// poses - no headset, no game, no files:
//
//   body_calibration_test        every case; exit code 0 = all passed
//
// pose      CheckPose on a good T-pose (any heading), and on each way to get it wrong
// flow      the release / ready / hold flow: one capture, the averages, Still, a gap, cancel, the save and its end
// gate      ReleaseGate: the controls stay the measurement's until let go once (a cancel with the triggers down)
// ranges    Valid(): the acceptance limits at their edges
// arms      UnitsPerMetre / ArmScale / ScaleAbout against numbers worked out by hand
// stand     StandingEyeY: the F4 camera's standing height - measured and standing up after a seated recentre, the hero's
//           eyes are the player's (not "small under me"), in a STAGE space and a LOCAL one; no floor or no measurement: the
//           recentre's, as before
// seated    [calibration] seated=1: the arm span only (any space, any head height, eyeHeight 0 out), InForce (the stored eye
//           height kept), StandingEyeY from the recentre ("recenter must set the hero's height", the author 2026-10-20)
// ini       inipatch::Set: replace, keep a comment, add a key, add a section, keep CRLF

#include "body_calibration.h"
#include "ini_patch.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace bodycal;

namespace {

int g_failed = 0, g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

Quat AboutY(float a) { return {0, std::sin(a * 0.5f), 0, std::cos(a * 0.5f)}; }
Quat AboutX(float a) { return {std::sin(a * 0.5f), 0, 0, std::cos(a * 0.5f)}; }
Quat Mul(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// A good T-pose: eyes at `eye`, the hands `reach` out each side, a shoulder's drop under the eyes, in line with them.
// `heading` turns the whole player about the vertical (the room's axes mean nothing).
Sample TPose(float eye = 1.62f, float reach = 0.80f, float heading = 0.0f) {
    Sample s;
    s.time = 100.0;
    s.floor = true;
    s.headTracked = s.handTracked[kLeft] = s.handTracked[kRight] = true;
    s.headRot = AboutY(heading);
    const Vec at{0.3f, eye, -0.7f};   // somewhere in the room
    s.head = at;
    s.hand[kRight] = at + TurnAboutY(heading, {reach, -0.20f, 0.0f});
    s.hand[kLeft] = at + TurnAboutY(heading, {-reach, -0.20f, 0.0f});
    return s;
}

void PoseCases() {
    Check(CheckPose(TPose()) == Hint::Pose, "pose: a good T-pose is accepted");
    for (float h : {0.5f, 1.0f, 2.0f, -1.2f, 3.1f, -3.1f, 3.14159f})
        Check(CheckPose(TPose(1.62f, 0.8f, h)) == Hint::Pose, "pose: accepted at any heading");

    Sample s = TPose();
    s.headTracked = false;
    Check(CheckPose(s) == Hint::Tracking, "pose: head not tracked");
    s = TPose(); s.handTracked[kRight] = false;
    Check(CheckPose(s) == Hint::Tracking, "pose: a hand not tracked");
    s = TPose(); s.hand[kLeft].x = std::nanf("");
    Check(CheckPose(s) == Hint::Tracking, "pose: a hand not finite");
    s = TPose(); s.floor = false;
    Check(CheckPose(s) == Hint::Floor, "pose: LOCAL space (no floor) refused");
    s = TPose(0.6f);
    Check(CheckPose(s) == Hint::Height, "pose: eyes at 0.6 m");
    s = TPose(2.4f);
    Check(CheckPose(s) == Hint::Height, "pose: eyes at 2.4 m");
    s = TPose(); s.headRot = AboutX(0.5f);   // looking down ~29 deg
    Check(CheckPose(s) == Hint::Upright, "pose: head tilted 29 degrees");
    s = TPose(); s.headRot = AboutX(0.2f);   // ~11 deg: within the limit (cos 11 = 0.98)
    Check(CheckPose(s) == Hint::Pose, "pose: head tilted 11 degrees is fine");
    s = TPose(); s.headRot = {0, 0, 0, 3.0f};
    Check(CheckPose(s) == Hint::Upright, "pose: a quaternion not of unit length");

    s = TPose(1.62f, 0.2f);   // arms down at the sides
    Check(CheckPose(s) == Hint::Arms, "pose: arms down");
    s = TPose(); s.hand[kRight].z -= 0.40f;   // forward
    Check(CheckPose(s) == Hint::Arms, "pose: one hand 40 cm ahead");
    s = TPose(); s.hand[kRight].y += 0.12f;   // uneven
    Check(CheckPose(s) == Hint::Arms, "pose: one hand 12 cm higher");
    s = TPose(); s.hand[kRight].y += 0.30f; s.hand[kLeft].y += 0.30f;   // drop 0 - hands at the eyes
    Check(CheckPose(s) == Hint::Arms, "pose: hands at the height of the eyes");
    s = TPose(); s.hand[kRight].y -= 0.45f; s.hand[kLeft].y -= 0.45f;   // drop 0.65
    Check(CheckPose(s) == Hint::Arms, "pose: hands 65 cm under the eyes");
    s = TPose();
    { const Vec t = s.hand[kRight]; s.hand[kRight] = s.hand[kLeft]; s.hand[kLeft] = t; }
    Check(CheckPose(s) == Hint::Arms, "pose: the hands crossed over (right on the left)");
    s = TPose(); s.hand[kRight].x += 0.25f;   // lopsided: x sum 0.25
    Check(CheckPose(s) == Hint::Arms, "pose: not symmetric about the head");
    Check(CheckPose(TPose(1.62f, 0.36f)) == Hint::Arms, "pose: hands 36 cm out - span 0.72 is short");
    Check(CheckPose(TPose(1.62f, 0.5f)) == Hint::Arms, "pose: hands 50 cm out - span 1.0, ratio 0.62");
    Check(CheckPose(TPose(1.62f, 0.55f)) == Hint::Pose, "pose: hands 55 cm out - a short-armed child");
}

void RangeCases() {
    Check(Valid({1.6f, 1.7f}), "ranges: a normal adult");
    Check(!Valid({0.79f, 1.0f}) && Valid({0.8f, 1.0f}), "ranges: eye height 0.8 lower edge");
    Check(Valid({2.3f, 2.3f}) && !Valid({2.31f, 2.3f}), "ranges: eye height 2.3 upper edge");
    Check(!Valid({1.6f, 0.84f}) && !Valid({1.0f, 0.84f}), "ranges: span under 0.85");
    Check(!Valid({2.0f, 2.61f}), "ranges: span over 2.6");
    Check(!Valid({1.6f, 1.6f * 0.64f}) && Valid({1.6f, 1.6f * 0.66f}), "ranges: span / eye 0.65 lower edge");
    Check(!Valid({1.2f, 1.2f * 1.46f}) && Valid({1.2f, 1.2f * 1.44f}), "ranges: span / eye 1.45 upper edge");
    Check(!Valid({std::nanf(""), 1.7f}) && !Valid({1.6f, std::nanf("")}) && !Valid({1.6f, INFINITY}), "ranges: not finite");
}

double g_clock = 100.0;   // the headset's clock: one run follows the last without a gap

// Frames at 90 Hz: runs the flow with a callback shaping each sample (t = seconds into this run); the time it took to
// the capture, or -1. The clock goes on from where the last run stopped.
template <class F>
double Run(Flow& flow, double seconds, F shape, bool* captured = nullptr) {
    const double dt = 1.0 / 90.0;
    for (double t = 0; t <= seconds; t += dt) {
        Sample s = TPose();
        s.time = g_clock += dt;
        shape(s, t);
        if (flow.Step(s)) {
            if (captured) *captured = true;
            return t;
        }
    }
    return -1;
}

// One frame `after` seconds on from the last (a gap, or the next frame).
template <class F>
bool One(Flow& flow, F shape, double after = 1.0 / 90.0) {
    Sample s = TPose();
    s.time = g_clock += after;
    shape(s);
    return flow.Step(s);
}
const auto Both = [](Sample& s) { s.trigger[0] = s.trigger[1] = 1.0f; };
const auto Both2 = [](Sample& s, double) { s.trigger[0] = s.trigger[1] = 1.0f; };

void FlowCases() {
    Flow flow;
    Check(!flow.Active() && !flow.Step(TPose()), "flow: idle until begun");

    flow.Begin();
    Check(flow.phase == Phase::Release && flow.OwnsTriggers(), "flow: begun - waiting for the triggers to be let go");

    // triggers held from the start: nothing happens until they are let go
    bool got = false;
    Run(flow, 1.0, [](Sample& s, double) { s.trigger[0] = s.trigger[1] = 1.0f; }, &got);
    Check(!got && flow.phase == Phase::Release && flow.hint == Hint::Release, "flow: triggers held from the start do not count");
    Run(flow, 0.2, [](Sample&, double) {});
    Check(flow.phase == Phase::Ready, "flow: let go in the pose -> ready");

    // press both and hold: one capture at 1.2 s, with the averages
    got = false;
    const double took = Run(flow, 3.0, [](Sample& s, double t) {
        s.trigger[0] = s.trigger[1] = 0.9f;
        s.head.y = 1.62f + (std::fmod(t * 90, 2) < 1 ? 0.004f : -0.004f);   // a little jitter
    }, &got);
    Check(got && took >= 1.19 && took < 1.3, "flow: captured after the hold (1.2 s)");
    Check(flow.phase == Phase::Saving, "flow: saving after the capture");
    const Measurement m = flow.Get();
    Check(Near(m.eyeHeight, 1.62f, 0.003f), "flow: the averaged eye height");
    Check(Near(m.armSpan, 1.60f, 0.003f), "flow: the averaged span (2 x 0.8)");
    Check(!One(flow, Both), "flow: saving gives no second capture");

    flow.Saved(g_clock);
    Check(flow.phase == Phase::Saved && flow.hint == Hint::Saved, "flow: saved");
    One(flow, Both, 0.5);
    Check(flow.phase == Phase::Saved, "flow: triggers still held - it stays up");
    One(flow, [](Sample&) {}, 0.1);
    Check(flow.phase == Phase::Saved, "flow: released but under a second - it stays up");
    One(flow, [](Sample&) {}, 0.5);
    One(flow, [](Sample&) {}, 0.1);
    Check(flow.phase == Phase::Idle && !flow.OwnsTriggers(), "flow: released a second after - over");

    // a hand moves during the hold: starts over, needs a release
    flow.Begin();
    Run(flow, 0.2, [](Sample&, double) {});
    Run(flow, 0.5, Both2);
    Check(flow.phase == Phase::Hold, "flow: holding");
    One(flow, [](Sample& s) { Both(s); s.hand[kRight].x += 0.08f; });   // 8 cm
    Check(flow.phase == Phase::Release && flow.hint == Hint::Still, "flow: a hand moved 8 cm -> Still");

    // the head moves 4 cm
    flow.Begin();
    Run(flow, 0.2, [](Sample&, double) {});
    Run(flow, 0.5, Both2);
    One(flow, [](Sample& s) { Both(s); s.head.x += 0.04f; });
    Check(flow.phase == Phase::Release && flow.hint == Hint::Still, "flow: the head moved 4 cm -> Still");
    // 2 cm of head sway is fine
    flow.Begin();
    Run(flow, 0.2, [](Sample&, double) {});
    got = false;
    Run(flow, 2.0, [](Sample& s, double t) { s.trigger[0] = s.trigger[1] = 1.0f; s.head.x += 0.02f * (std::fmod(t, 0.5) < 0.25 ? 1 : 0); }, &got);
    Check(got, "flow: 2 cm of head sway is within 3 cm");

    // one trigger let go half way: starts over
    flow.Begin();
    Run(flow, 0.2, [](Sample&, double) {});
    got = false;
    Run(flow, 2.0, [](Sample& s, double t) {
        s.trigger[0] = 1.0f;
        s.trigger[1] = t < 0.6 ? 1.0f : 0.0f;
    }, &got);
    Check(!got && flow.phase != Phase::Hold, "flow: a trigger let go half way -> no capture");

    // only one trigger ever pressed
    flow.Begin();
    Run(flow, 0.2, [](Sample&, double) {});
    got = false;
    Run(flow, 2.0, [](Sample& s, double) { s.trigger[0] = 1.0f; }, &got);
    Check(!got && flow.phase == Phase::Ready, "flow: one trigger alone is not a capture");

    // a gap in the frames (the session paused 1 s) while holding: starts over
    flow.Begin();
    Run(flow, 0.2, [](Sample&, double) {});
    for (int i = 0; i < 40; ++i) One(flow, Both);
    Check(flow.phase == Phase::Hold, "flow: holding");
    One(flow, Both, 1.0);
    Check(flow.phase == Phase::Release, "flow: a 1 s gap in the frames -> starts over");

    // the pose breaks (arms down) while holding
    flow.Begin();
    Run(flow, 0.2, [](Sample&, double) {});
    Run(flow, 0.5, [](Sample& s, double) { s.trigger[0] = s.trigger[1] = 1.0f; });
    Check(flow.phase == Phase::Hold, "flow: holding again");
    One(flow, [](Sample& s) { s.hand[kRight].x = s.head.x + 0.15f; s.hand[kLeft].x = s.head.x - 0.15f; s.trigger[0] = s.trigger[1] = 1.0f; });
    Check(flow.phase == Phase::Release && flow.hint == Hint::Arms, "flow: the arms dropped -> Release, Arms");

    // cancel: an edge, not a level held at the start
    flow.Begin();
    One(flow, [](Sample& s) { s.cancel = true; });
    Check(flow.Active(), "flow: a cancel button already down at the start is not a press");
    One(flow, [](Sample& s) { s.cancel = false; });
    One(flow, [](Sample& s) { s.cancel = true; });
    Check(!flow.Active(), "flow: the cancel button ends it");

    // the save fails: Failed, then ready again after the triggers are let go
    flow.Begin();
    Run(flow, 0.2, [](Sample&, double) {});
    got = false;
    Run(flow, 3.0, [](Sample& s, double) { s.trigger[0] = s.trigger[1] = 1.0f; }, &got);
    Check(got, "flow: captured for the failing save");
    flow.SaveFailed();
    Check(flow.phase == Phase::Failed && flow.hint == Hint::Failed, "flow: the save failed");
    One(flow, Both);
    Check(flow.phase == Phase::Failed, "flow: failed, triggers still held");
    One(flow, [](Sample&) {});
    Check(flow.phase == Phase::Ready, "flow: failed, then ready for another go");

    // floor lost during the flow: Release with Floor
    flow.Begin();
    One(flow, [](Sample& s) { s.floor = false; });
    Check(flow.phase == Phase::Release && flow.hint == Hint::Floor, "flow: no floor -> Floor");

    // a capture that falls outside the limits is not taken (a 2.31 m head would be refused at the pose already)
    flow.Begin();
    Run(flow, 0.2, [](Sample&, double) {});
    got = false;
    Run(flow, 3.0, [](Sample& s, double) { s.trigger[0] = s.trigger[1] = 1.0f; s.head.y = 2.25f; s.hand[0].y = s.hand[1].y = 2.05f; s.hand[0].x -= 0.4f; s.hand[1].x += 0.4f; }, &got);
    Check(got, "flow: a tall player with long arms is measured");
    Check(flow.Get().eyeHeight > 2.2f && Valid(flow.Get()), "flow: and the result is valid");
}

void GateCases() {
    ReleaseGate g;
    Check(!g.Step(false, 1.0f, 1.0f, true), "gate: idle, nothing owned (a trigger held means nothing to it)");
    Check(g.Step(true, 1.0f, 1.0f, false), "gate: the measurement owns the controls");
    Check(g.Step(false, 1.0f, 1.0f, false), "gate: ended with both triggers down (a cancel): still owned");
    Check(g.Step(false, 0.0f, 1.0f, false), "gate: one trigger let go: still owned");
    Check(g.Step(false, 0.1f, 0.1f, true), "gate: B still down: still owned");
    Check(g.Step(false, 0.1f, 0.1f, false), "gate: all let go: owned for this frame more");
    Check(!g.Step(false, 0.1f, 0.1f, false), "gate: then given back");
    Check(!g.Step(false, 1.0f, 1.0f, true), "gate: a press after that is the game's again");
    Check(g.Step(true, 0.0f, 0.0f, false) && g.Step(false, 0.0f, 0.0f, false) && !g.Step(false, 0.0f, 0.0f, false),
          "gate: owned, ended with everything let go: one frame more, then given back");
    // with the flow: the cancel with both triggers down
    Flow flow;
    ReleaseGate gate;
    flow.Begin();
    Run(flow, 0.2, [](Sample&, double) {});
    Run(flow, 0.5, Both2);
    bool owned = gate.Step(flow.OwnsTriggers(), 1, 1, false);
    Check(flow.phase == Phase::Hold && owned, "gate: holding - owned");
    One(flow, [](Sample& s) { Both(s); s.cancel = true; });
    owned = gate.Step(flow.OwnsTriggers(), 1, 1, true);
    Check(!flow.Active() && owned, "gate: cancelled with both triggers down - the flow is over, the gate still owns them");
    owned = gate.Step(flow.OwnsTriggers(), 0, 0, false);
    owned = gate.Step(flow.OwnsTriggers(), 0, 0, false);
    Check(!owned, "gate: let go - the controls are the game's again");
}

void ArmCases() {
    Check(Near(UnitsPerMetre(6.8f, 1.7f), 4.0f), "arms: units per metre");
    Check(Near(UnitsPerMetre(6.8f, 1.7f, 1.5f), 6.0f), "arms: with the manual reach on top");
    Check(Near(UnitsPerMetre(3.2f, 0.2f), 4.0f), "arms: the user's height floored at 0.8 m");
    Check(Near(UnitsPerMetre(30.0f, 1.0f), 8.0f) && Near(UnitsPerMetre(1.0f, 1.7f), 2.0f), "arms: kept between 2 and 8");

    // span 1.6 m at 4 units/m = 6.4; less shoulders 1.6 = 4.8; each arm 2.4; less the palm 0.2 = 2.2 = the arm: unchanged
    Check(Near(ArmScale(6.8f, 1.7f, 1.6f, 1.6f, 2.2f, 0.2f), 1.0f, 1e-5f), "arms: the authored reach is kept when it matches");
    Check(Near(ArmScale(6.8f, 1.7f, 1.82f, 1.6f, 2.2f, 0.2f), 1.2f, 1e-5f), "arms: grows to a longer span");
    Check(Near(ArmScale(6.8f, 1.7f, 1.2f, 1.6f, 2.2f, 0.2f), 0.65f, 1e-5f), "arms: bounded below (0.65)");
    Check(Near(ArmScale(6.8f, 1.7f, 2.5f, 1.6f, 2.2f, 0.2f), 1.6f, 1e-5f), "arms: bounded above (1.6)");
    Check(ArmScale(6.8f, 1.7f, std::nanf(""), 1.6f, 2.2f, 0.2f) == 1.0f, "arms: not finite -> 1");
    Check(ArmScale(6.8f, 1.7f, 0.0f, 1.6f, 2.2f, 0.2f) == 1.0f, "arms: not measured (span 0) -> 1");
    Check(ArmScale(0.0f, 1.7f, 1.6f, 1.6f, 2.2f, 0.2f) == 1.0f, "arms: no model eye height -> 1");
    Check(ArmScale(6.8f, 1.7f, 1.6f, 1.6f, 0.0f, 0.2f) == 1.0f, "arms: no arm bones -> 1");

    // ScaleAbout: row-vector matrix, position in m[12..14]
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, 5, 7, 1};
    ScaleAbout(m, {1, 1, 1}, 2.0f);
    Check(Near(m[0], 2) && Near(m[5], 2) && Near(m[10], 2) && Near(m[15], 1), "arms: the axes scaled, w kept");
    Check(Near(m[12], 5) && Near(m[13], 9) && Near(m[14], 13), "arms: the place moved along the line from the pivot");
    float n[16] = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1};
    ScaleAbout(n, {1, 1, 1}, 0.5f);
    Check(Near(n[1], 0.5f) && Near(n[4], -0.5f) && Near(n[12], 1) && Near(n[13], 1), "arms: a place at the pivot stays");
}

void IniCases() {
    using inipatch::Set;
    std::string t = "; head\r\n[arms]\r\nmode=2\r\nscale=1 ; reach\r\n\r\n[calibration]\r\nuse=0\r\nversion=0\r\n; tail\r\n[table]\r\nplace=0\r\n";

    std::string a = t;
    Check(Set(a, "calibration", "use", "1") && a.find("use=1\r\n") != std::string::npos && a.find("use=0") == std::string::npos,
          "ini: a value replaced");
    Check(a.find("[arms]\r\nmode=2\r\nscale=1 ; reach\r\n") != std::string::npos && a.find("; tail\r\n[table]\r\nplace=0\r\n") != std::string::npos,
          "ini: the rest left as it was");
    Check(a.size() == t.size(), "ini: same length for a same-length value");

    a = t;
    Set(a, "arms", "scale", "1.25");
    Check(a.find("scale=1.25 ; reach\r\n") != std::string::npos, "ini: an inline comment kept");

    a = t;
    Set(a, "CALIBRATION", "USE", "1");
    Check(a.find("use=1") != std::string::npos, "ini: names compared without case");

    a = t;
    Check(Set(a, "calibration", "eye_height_m", "1.623"), "ini: a new key");
    Check(a.find("version=0\r\neye_height_m=1.623\r\n; tail\r\n") != std::string::npos, "ini: a new key goes after the section's last key, before its trailing comment");

    a = t;
    Set(a, "arms", "up", "3");
    Check(a.find("scale=1 ; reach\r\nup=3\r\n\r\n[calibration]") != std::string::npos, "ini: a new key in an earlier section");

    a = t;
    Check(Set(a, "newsec", "k", "v"), "ini: a new section");
    Check(a.size() > t.size() && a.compare(a.size() - 17, 17, "\r\n[newsec]\r\nk=v\r\n") == 0 && a.compare(0, t.size(), t) == 0,
          "ini: a new section at the end, CRLF, the old text intact");

    a = "[x]\na=1\n";
    Set(a, "x", "b", "2");
    Check(a == "[x]\na=1\nb=2\n", "ini: LF files stay LF");
    a = "[x]\na=1";
    Set(a, "x", "b", "2");
    Check(a == "[x]\na=1\nb=2\n", "ini: a last line without an ending");
    a = "";
    Set(a, "x", "b", "2");
    Check(a == "[x]\nb=2\n", "ini: an empty file");
    a = "[x]\n";
    Set(a, "x", "b", "2");
    Check(a == "[x]\nb=2\n", "ini: an empty section");
    a = "[x]\nb=1\n[x]\nb=2\n";
    Set(a, "x", "b", "9");
    Check(a == "[x]\nb=9\n[x]\nb=2\n", "ini: the first duplicate is the one read (as the Windows API does)");
    a = "[x]\n;b=1\nb=2\n";
    Set(a, "x", "b", "9");
    Check(a == "[x]\n;b=1\nb=9\n", "ini: a commented key is not the key");
    a = std::string("[x]\0\0", 5);
    Check(!Set(a, "x", "b", "2"), "ini: text with NULs (UTF-16) is left alone");

    // the three keys the measurement writes, as vrcam's reader will find them
    a = t;
    Set(a, "calibration", "eye_height_m", "1.6234");
    Set(a, "calibration", "arm_span_m", "1.7012");
    Set(a, "calibration", "version", "1");
    Check(a.find("version=1\r\n") != std::string::npos && a.find("eye_height_m=1.6234\r\n") != std::string::npos &&
              a.find("arm_span_m=1.7012\r\n") != std::string::npos,
          "ini: the three keys of a measurement");
}

// The F4 camera (vrcam VrViewInner, native): the hero's bind-pose eyes at the standing eyes' height, moved from there by the
// head at `upm` world units a metre (his eyes in world units over the measured eye height). How tall his eyes look to the
// player: his eyes' height over the game's ground over the camera's, times the player's own eye height now.
float HeroEyesSeenM(float heroEyeUnits, float measuredEye, float headY, float standY) {
    const float upm = heroEyeUnits / measuredEye;
    const float camera = heroEyeUnits + (headY - standY) * upm;   // world units over his feet
    return heroEyeUnits / camera * measuredEye;
}

void StandCases() {
    // the author's numbers (2026-10-19): eye_height_m=1.6917, arm_span_m=1.5101 - measured standing in a STAGE space (the floor
    // at 0); the session's recentre was taken seated, the head at 1.18 m
    const float eye = 1.6917f, seatedRecentre = 1.18f, heroEye = 6.2f;
    Check(Valid({eye, 1.5101f}), "stand: the author's measurement is valid");
    Check(StandsOnFloor(true, 0.0f, eye), "stand: a floor and a measurement - the standing height is the measured one");
    const float before = HeroEyesSeenM(heroEye, eye, eye, seatedRecentre);   // the old way: from the recentre's height
    Check(before < 0.8f * eye, "stand: (the bug) from a seated recentre, the hero's eyes stood far under the player's - small, under them");
    const float standY = StandingEyeY(seatedRecentre, true, 0.0f, eye);
    Check(Near(standY, eye), "stand: STAGE - the floor plus the measured eye height");
    Check(Near(HeroEyesSeenM(heroEye, eye, eye, standY), eye), "stand: standing upright the hero's eyes are the player's, whatever the recentre");
    Check(Near(HeroEyesSeenM(heroEye, eye, eye, StandingEyeY(1.95f, true, 0.0f, eye)), eye), "stand: ... a recentre taken high (on tiptoe) too");
    // a crouch: the camera goes down by as much as the head, at the world's scale - from the standing eyes, not from the recentre
    Check(Near(eye - 0.40f - standY, -0.40f), "stand: a 40 cm crouch is 40 cm under the standing eyes (it was 11 cm over the seated recentre)");
    // LOCAL (no STAGE for the eyes): the floor in that space as F3 has it (the eyes' middle less the head's STAGE height)
    const float localFloor = -1.30f;
    const float localStand = StandingEyeY(0.0f, true, localFloor, eye);
    Check(Near(localStand, localFloor + eye), "stand: LOCAL - the floor there plus the measured eye height");
    Check(Near(HeroEyesSeenM(heroEye, eye, localFloor + eye, localStand), eye), "stand: LOCAL - standing, the hero's eyes are the player's");
    // nothing to stand on: as before
    Check(!StandsOnFloor(false, 0.0f, eye) && StandingEyeY(seatedRecentre, false, 0.0f, eye) == seatedRecentre, "stand: no floor seen - the recentre's");
    Check(!StandsOnFloor(true, 0.0f, 0.0f) && StandingEyeY(seatedRecentre, true, 0.0f, 0.0f) == seatedRecentre,
          "stand: no measurement in use (unticked, another view) - the recentre's");
    Check(StandingEyeY(seatedRecentre, true, 0.0f, 5.2f) == seatedRecentre, "stand: an eye height out of range - the recentre's");
    Check(StandingEyeY(seatedRecentre, true, NAN, eye) == seatedRecentre, "stand: a floor not finite - the recentre's");
}

void SeatedCases() {
    // the pose: no floor and no eye height asked for - a seated head in a LOCAL space (y 0 at the recentre) or 1.2 m over a STAGE floor
    Sample s = TPose(1.20f);
    s.seated = true;
    Check(CheckPose(s) == Hint::Pose, "seated: a T-pose sitting, the head 1.2 m over the floor");
    s.floor = false;
    Check(CheckPose(s) == Hint::Pose, "seated: in a LOCAL space too (no floor needed)");
    s = TPose(0.0f); s.seated = true; s.floor = false;
    Check(CheckPose(s) == Hint::Pose, "seated: the head at y 0 (LOCAL, recentred there)");
    s = TPose(0.6f); s.seated = true;
    Check(CheckPose(s) == Hint::Pose && CheckPose(TPose(0.6f)) == Hint::Height, "seated: the head's height is not an eye height (standing: refused)");
    s = TPose(1.20f, 0.36f); s.seated = true;
    Check(CheckPose(s) == Hint::Arms, "seated: a span of 0.72 m is still refused");
    s = TPose(1.20f, 1.40f); s.seated = true;
    Check(CheckPose(s) == Hint::Arms, "seated: a span of 2.8 m is still refused");
    s = TPose(0.95f, 0.80f);   // a short seated head with an adult's span: the ratio would refuse it standing
    Check(CheckPose(s) == Hint::Arms, "seated: (standing) span / head 1.68 - refused as not a person");
    s.seated = true;
    Check(CheckPose(s) == Hint::Pose, "seated: no ratio to the head's height - accepted");
    s = TPose(1.20f); s.seated = true; s.hand[kRight].z -= 0.40f;
    Check(CheckPose(s) == Hint::Arms, "seated: the arms' pose is still checked (a hand ahead)");
    s = TPose(1.20f); s.seated = true; s.headRot = AboutX(0.5f);
    Check(CheckPose(s) == Hint::Upright, "seated: and the head upright");

    // the flow: a capture is the span alone (eyeHeight 0 = not measured)
    {
        Flow flow;
        flow.Begin();
        bool captured = false;
        const auto local = [](Sample& x) { x.seated = true; x.floor = false; x.head.y = 0.0f; x.hand[0].y = x.hand[1].y = -0.20f; };   // LOCAL: the head at 0
        Run(flow, 0.2, [&](Sample& x, double) { local(x); });
        Run(flow, 2.0, [&](Sample& x, double) { local(x); Both(x); }, &captured);
        const Measurement m = flow.Get();
        Check(captured && flow.phase == Phase::Saving, "seated: the flow captures in a LOCAL space");
        Check(m.eyeHeight == 0.0f && Near(m.armSpan, 1.60f, 1e-3f), "seated: the arm span only, the eye height not measured");
    }

    // what is in force: standing both or nothing; seated the span, and a stored (standing) eye height with it
    const Measurement author{1.6917f, 1.5101f};
    Check(InForce(false, author).eyeHeight == author.eyeHeight && InForce(false, author).armSpan == author.armSpan, "seated: standing - both");
    Check(InForce(true, author).eyeHeight == author.eyeHeight && InForce(true, author).armSpan == author.armSpan,
          "seated: a standing measurement stored - its eye height is kept for the world's scale");
    Check(InForce(true, {0.0f, 1.5101f}).eyeHeight == 0.0f && InForce(true, {0.0f, 1.5101f}).armSpan == 1.5101f,
          "seated: only a span stored - the span, the eye height the old one (0: BodyWalk / user_height_m)");
    Check(InForce(false, {0.0f, 1.5101f}).armSpan == 1.5101f && InForce(false, {0.0f, 1.5101f}).eyeHeight == 0.0f,
          "only a span stored (seated, or set by hand), played standing - the span alone, the old height");
    Check(InForce(false, {0.0f, 0.5f}).armSpan == 0.0f && InForce(false, {0.5f, 1.6f}).armSpan == 1.6f && InForce(false, {0.5f, 1.6f}).eyeHeight == 0.0f,
          "standing: a span not a person's - nothing; an eye height not a person's - the span alone");
    Check(InForce(true, {1.6917f, 0.5f}).armSpan == 0.0f && InForce(true, {1.6917f, 0.5f}).eyeHeight == 0.0f, "seated: a span not a person's - nothing");
    Check(InForce(true, {1.0f, 2.0f}).armSpan == 2.0f && InForce(true, {1.0f, 2.0f}).eyeHeight == 0.0f,
          "seated: an eye height that does not go with the span - the span alone");

    // the camera: the recentre sets the standing height, whatever the floor and the measurement
    const float eye = author.eyeHeight, heroEye = 6.2f, seatedHead = 1.18f;
    Check(!StandsOnFloor(true, 0.0f, eye, true), "seated: never on the floor");
    const float standY = StandingEyeY(seatedHead, true, 0.0f, eye, true);
    Check(standY == seatedHead, "seated: the recentre's height (sitting) is the hero's eyes");
    // (the hero's eyes seen from a camera under them look higher than the player's own: the view is in his middle)
    Check(HeroEyesSeenM(heroEye, eye, seatedHead, StandingEyeY(seatedHead, true, 0.0f, eye, false)) > 1.2f * eye,
          "seated: (the bug) sitting with the floor's standing height, the view is in the middle of the hero");
    Check(Near(HeroEyesSeenM(heroEye, eye, seatedHead, standY), eye), "seated: sitting after the recentre, the camera is the hero's eyes");
    // sat with the recentre taken standing (the session's first frame): a recentre seated lifts the view back to the eyes
    const float before = StandingEyeY(eye, true, 0.0f, eye, true);
    Check(HeroEyesSeenM(heroEye, eye, seatedHead, before) > 1.2f * eye, "seated: recentred standing, then sat - in his middle (until a recentre)");
    Check(Near(HeroEyesSeenM(heroEye, eye, seatedHead, StandingEyeY(seatedHead, true, 0.0f, eye, true)), eye), "seated: recentre sitting - the hero's eyes again");
    Check(StandingEyeY(-0.02f, false, 0.0f, 0.0f, true) == -0.02f, "seated: LOCAL, nothing measured - the recentre's too");
    // standing is as it was
    Check(StandingEyeY(seatedHead, true, 0.0f, eye, false) == StandingEyeY(seatedHead, true, 0.0f, eye), "seated: standing is the 4-argument call");
}

// [calibration] strict_tpose: the arms straight out to the sides. The author's numbers: eye height 1.75 m; standing 1.63 m one day,
// 1.50 m another, seated 1.68 m - the 1.50 one taken with the arms swung forward (a span 15 cm short), and it was let through.
// A pose of an arm `out` from a shoulder 0.18 m out, 0.08 m behind the eyes, swung `aheadDeg` forward from straight out sideways.
Sample Swung(float eye, float arm, float aheadDeg, float heading = 0.0f) {
    Sample s = TPose(eye, 0.8f, heading);
    const float a = aheadDeg * 0.0174532925f;
    const Vec r{0.18f + arm * std::cos(a), -0.20f, 0.08f - arm * std::sin(a)}, l{-r.x, r.y, r.z};
    s.hand[kRight] = s.head + TurnAboutY(heading, r);
    s.hand[kLeft] = s.head + TurnAboutY(heading, l);
    return s;
}

void StrictCases() {
    // the arm the 1.68 seated span gives: 0.66 m each from a shoulder 0.18 m out
    const float eye = 1.7484f, arm = 0.66f;
    Sample s = Swung(eye, arm, 0.0f);
    const float spanOut = Length(s.hand[kRight] - s.hand[kLeft]);
    Check(Near(spanOut, 1.68f, 1e-3f), "strict: (straight out sideways: the 1.68 span)");
    s.strict = true;
    Check(CheckPose(s) == Hint::Pose, "strict: arms straight out sideways - taken");
    for (float h : {1.0f, -2.0f, 3.1f}) {
        Sample t = Swung(eye, arm, 0.0f, h);
        t.strict = true;
        Check(CheckPose(t) == Hint::Pose, "strict: straight out sideways at any heading - taken");
    }
    // swung 25 degrees forward: the hands 20 cm ahead of the eyes, the span 1.56 - the old pose let it through, strict does not
    Sample f = Swung(eye, arm, 25.0f);
    const float spanAhead = Length(f.hand[kRight] - f.hand[kLeft]);
    Check(spanAhead < spanOut - 0.10f, "strict: (arms swung 25 degrees forward read a span over 10 cm short)");
    Check(CheckPose(f) == Hint::Pose, "strict off: the arms swung forward are taken, as before");
    f.strict = true;
    Check(CheckPose(f) == Hint::Arms, "strict: the arms swung forward - refused (Arms)");
    for (float h : {1.0f, -2.0f, 3.1f}) {
        Sample t = Swung(eye, arm, 25.0f, h);
        t.strict = true;
        Check(CheckPose(t) == Hint::Arms, "strict: swung forward at any heading - refused");
    }
    // a little forward (10 degrees: the hands 3 cm ahead of the eyes) - still taken, the span 1 % short
    Sample g = Swung(eye, arm, 10.0f);
    g.strict = true;
    Check(CheckPose(g) == Hint::Pose, "strict: 10 degrees forward - taken");
    // the elbows bent (the arm 0.55 m to the grip, straight out): span 1.46, 0.84 of the eye height - refused
    Sample b = Swung(eye, 0.55f, 0.0f);
    Check(CheckPose(b) == Hint::Pose, "strict off: elbows bent, taken, as before");
    b.strict = true;
    Check(CheckPose(b) == Hint::Arms, "strict: a span under 0.88 of the eye height - refused");
    // the bound itself
    Check(StrictArms({0.8f, -0.2f, -0.059f}, {-0.8f, -0.2f, 0.0f}, 1.6f, 1.7f), "StrictArms: a hand 5.9 cm ahead - taken");
    Check(!StrictArms({0.8f, -0.2f, -0.061f}, {-0.8f, -0.2f, 0.0f}, 1.6f, 1.7f), "StrictArms: a hand 6.1 cm ahead - refused");
    Check(!StrictArms({0.8f, -0.2f, 0.0f}, {-0.8f, -0.2f, -0.07f}, 1.6f, 1.7f), "StrictArms: the left hand ahead - refused");
    Check(StrictArms({0.8f, -0.2f, 0.15f}, {-0.8f, -0.2f, 0.15f}, 1.6f, 1.7f), "StrictArms: hands behind the eyes - taken");
    Check(StrictArms({}, {}, 0.881f * 1.7f, 1.7f) && !StrictArms({}, {}, 0.879f * 1.7f, 1.7f), "StrictArms: the span's bound, 0.88 of the eye height");
    Check(StrictArms({}, {}, 1.0f, 0.0f), "StrictArms: no eye height to hold it against - the span not judged");
    // seated: the stored standing eye height is the yardstick; without one, only the hands' place
    Sample sb = Swung(eye, 0.55f, 0.0f);
    sb.seated = true;
    sb.strict = true;
    sb.storedEye = eye;
    Check(CheckPose(sb) == Hint::Arms, "strict seated: elbows bent against the stored eye height - refused");
    sb.storedEye = 0.0f;
    Check(CheckPose(sb) == Hint::Pose, "strict seated, no stored eye height: the span not judged - taken");
    Sample sf = Swung(eye, arm, 25.0f);
    sf.seated = true;
    sf.strict = true;
    Check(CheckPose(sf) == Hint::Arms, "strict seated: arms swung forward - refused");
    // the flow with strict: a whole measurement of arms straight out sideways reads the 1.68
    {
        Flow flow;
        flow.Begin();
        Sample t = Swung(eye, arm, 0.0f);
        t.strict = true;
        bool done = false;
        for (int i = 0; i < 200 && !done; ++i) {
            t.time = 10.0 + i * 0.011;
            t.trigger[kLeft] = t.trigger[kRight] = i < 5 ? 0.0f : 1.0f;
            done = flow.Step(t);
        }
        Check(done && Near(flow.Get().armSpan, 1.68f, 2e-3f) && Near(flow.Get().eyeHeight, eye, 1e-3f), "strict: the flow reads the straight-out span");
    }
}

// [calibration] default_span: the span a person of the eye height has
void DefaultSpanCases() {
    Check(Near(DefaultSpan(1.7484f), 0.95f * 1.7484f), "default span: 0.95 of the eye height");
    Check(Near(DefaultSpan(1.6401f), 1.5581f, 1e-3f), "default span: the player's eye height gives his measured 1.55 within 1 cm");
    Check(DefaultSpan(0.0f) == 0.0f && DefaultSpan(0.5f) == 0.0f && DefaultSpan(std::nanf("")) == 0.0f, "default span: no valid eye height - none");
    Check(ValidSpan(DefaultSpan(0.9f)) && ValidSpan(DefaultSpan(2.3f)), "default span: a valid span from a 0.9 m eye height to the top");
    Check(Valid({1.7f, DefaultSpan(1.7f)}), "default span: a valid measurement with its eye height");
    Check(DefaultSpan(1.7f) >= kStrictSpanPerEye * 1.7f, "default span: passes the strict span check");
    // fitted as a measurement: an arm fitted to it with the author's eye height reaches 0.95 * 1.75 / 2 less half the shoulders, less the palm
    const float fit = ArmScale(7.05f, 1.7484f, DefaultSpan(1.7484f), 1.6f, 2.2f, 0.2f);
    const float upm = UnitsPerMetre(7.05f, 1.7484f);
    Check(Near(fit * 2.2f, (DefaultSpan(1.7484f) * upm - 1.6f) * 0.5f - 0.2f, 1e-4f), "default span: fitted the way a measurement is");
}

}  // namespace

int main() {
    PoseCases();
    RangeCases();
    FlowCases();
    GateCases();
    ArmCases();
    IniCases();
    StandCases();
    SeatedCases();
    StrictCases();
    DefaultSpanCases();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
