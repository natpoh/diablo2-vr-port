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

}  // namespace

int main() {
    PoseCases();
    RangeCases();
    FlowCases();
    GateCases();
    ArmCases();
    IniCases();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
