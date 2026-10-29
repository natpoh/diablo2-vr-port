// The bow draw (vr/bow_draw.h) on synthetic poses, without the game and without a headset:
// the nock, the pull, the release, and everything that must NOT shoot.
//
//   bow_draw_test
//
// The bow hand holds the bow at the origin of the scene, pointing along -Z (a grip turned to
// the identity: its knuckles point down, so the controller's forward is the bow's); the string
// hand starts at the nock point 0.16 m behind it and is pulled back along +Z.

#include "../../vr/bow_draw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace bowdraw;

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

constexpr double kDt = 1.0 / 90.0;

struct Sim {
    Detector d;
    Params p;
    Input in;
    Output last;
    int shots = 0, pressFrames = 0, ownedFrames = 0, cancels = 0, nocks = 0, charges = 0, lastWhy = 0;
    Vec lastAim;
    Sim() {
        in.allowed = in.valid = true;
        in.bowHand = {0.0f, 1.2f, -0.3f};
        in.stringHand = Nock();
        in.time = 10.0;
    }
    Vec Nock() const { return in.bowHand - Vec{0, 0, -1} * 0.16f; }
    // One frame, `dt` later, with whatever is in `in` now.
    Output Step(double dt = kDt) {
        in.time += dt;
        last = d.Step(in, p);
        shots += last.shoot;
        pressFrames += last.pressing;
        ownedFrames += last.owned;
        cancels += last.evCancel;
        nocks += last.evNock;
        charges += last.evCharged;
        if (last.shoot) lastAim = last.aim;
        if (last.evCancel) lastWhy = last.why;
        in.cancel = false;
        return last;
    }
    void Run(double seconds) { for (double t = 0; t < seconds - 1e-9; t += kDt) Step(); }
    // The string hand to z (metres behind the bow hand's own place), over `seconds`, stepping every frame.
    void Pull(float behind, double seconds) {
        const Vec from = in.stringHand, to = in.bowHand + Vec{0, 0, behind};
        const int n = (int)std::lround(seconds / kDt);
        for (int i = 1; i <= n; ++i) {
            in.stringHand = from + (to - from) * ((float)i / (float)n);
            Step();
        }
    }
    // The string hand to `to`, over `seconds`.
    void MoveTo(Vec to, double seconds) {
        const Vec from = in.stringHand;
        const int n = std::max(1, (int)std::lround(seconds / kDt));
        for (int i = 1; i <= n; ++i) {
            in.stringHand = from + (to - from) * ((float)i / (float)n);
            Step();
        }
    }
    void Trigger(float v) { in.trigger = v; Step(); }
    // A full draw to 0.6 m and a hold of 0.2 s, the trigger down.
    void Draw(float behind = 0.6f) {
        in.stringHand = Nock();
        Step();
        Trigger(1.0f);
        Pull(behind, 0.4);
        Run(0.2);
    }
};

void TestForward() {
    std::printf("bow forward\n");
    const Vec f = BowForward(Quat{});
    EXPECT(std::fabs(f.x) < 1e-5f && std::fabs(f.y) < 1e-5f && std::fabs(f.z + 1.0f) < 1e-5f, "identity: the controller's -Z, got %.2f %.2f %.2f", f.x, f.y, f.z);
    // the grip turned a quarter about +X so that the knuckles (-Y) point forward (-Z): then it is the knuckles
    const float s = std::sqrt(0.5f);
    const Vec g = BowForward(Quat{s, 0, 0, s});   // +90 deg about X: -Y -> -Z
    EXPECT(std::fabs(g.z + 1.0f) < 1e-4f, "knuckles forward: %.2f %.2f %.2f", g.x, g.y, g.z);
    // the knuckles level to the right (+X), then they are the bow's way
    const Vec h = BowForward(Quat{0, 0, -s, s});   // -90 deg about Z: -Y -> +X ... or -X; the sign of x is what the rule gives
    EXPECT(std::fabs(h.y) < 1e-4f && std::fabs(std::fabs(h.x) - 1.0f) < 1e-4f, "knuckles level: %.2f %.2f %.2f", h.x, h.y, h.z);
}

void TestSkills() {
    std::printf("skills\n");
    for (int s : {6, 7, 11, 12, 16, 21, 22, 27, 31}) EXPECT(Classify(s) == SkillKind::Arrow, "skill %d is an arrow skill", s);
    EXPECT(Classify(0) == SkillKind::Basic, "the attack");
    EXPECT(Classify(26) == SkillKind::Volley, "Strafe keeps its volley");
    for (int s : {1, 2, 8, 9, 10, 13, 14, 15, 17, 18, 19, 20, 23, 24, 25, 28, 29, 30, 32, 33, 34, 35, 36, 97, -1, 1000})
        EXPECT(Classify(s) == SkillKind::Native && !DrawsWith(s), "skill %d keeps the trigger", s);
}

void TestShot() {
    std::printf("a draw and a release\n");
    Sim s;
    s.Draw();
    EXPECT(s.nocks == 1, "one nock, got %d", s.nocks);
    EXPECT(s.charges == 1, "charged once, got %d", s.charges);
    EXPECT(s.last.drawing && s.last.charged && s.last.owned, "held and charged");
    EXPECT(s.last.draw > 0.9f, "a long pull feels full: %.2f", s.last.draw);
    EXPECT(s.shots == 0, "nothing shot while held");
    EXPECT(s.last.aimOwned && std::fabs(s.last.aim.z + 1.0f) < 1e-3f, "the aim while drawing is the line to the bow: %.2f %.2f %.2f", s.last.aim.x, s.last.aim.y, s.last.aim.z);
    s.Trigger(0.0f);
    EXPECT(s.shots == 1, "one shot at the release, got %d", s.shots);
    EXPECT(s.last.shoot && s.last.pressing, "the frame of the shot has the key down");
    EXPECT(std::fabs(s.lastAim.z + 1.0f) < 1e-3f, "along the draw line, got %.2f %.2f %.2f", s.lastAim.x, s.lastAim.y, s.lastAim.z);
    EXPECT(!s.last.owned && !s.last.drawing, "the trigger is free again");
    // the press: 65 ms, 6 frames at 90 Hz (the shoot frame included)
    s.Run(0.5);
    EXPECT(s.pressFrames >= 5 && s.pressFrames <= 7, "the press lasts about 65 ms: %d frames", s.pressFrames);
    EXPECT(s.shots == 1, "still one shot after a while");
    EXPECT(!s.last.aimOwned, "the aim is let go after 0.4 s");
}

void TestAimHold() {
    std::printf("the aim after the shot\n");
    Sim s;
    // pulled back and to the right (+X): the line runs from the string hand through the bow hand, to the left-forward
    s.in.stringHand = s.Nock();
    s.Step();
    s.Trigger(1.0f);
    s.in.bowHand = {0.0f, 1.2f, -0.3f};
    const Vec to = s.in.bowHand + Vec{0.3f, 0.0f, 0.5f};
    const Vec from = s.in.stringHand;
    for (int i = 1; i <= 40; ++i) { s.in.stringHand = from + (to - from) * (i / 40.0f); s.Step(); }
    s.Run(0.2);
    const Vec expect = Normalized(s.in.bowHand - s.in.stringHand);
    s.Trigger(0.0f);
    EXPECT(s.shots == 1, "shot");
    EXPECT(Dot(s.lastAim, expect) > 0.999f, "the line is the aim: %.2f %.2f %.2f", s.lastAim.x, s.lastAim.y, s.lastAim.z);
    // the hands move on after the shot: the aim stays the line for 0.4 s
    s.MoveTo(s.in.bowHand + Vec{-0.4f, 0.0f, 0.0f}, 0.1);
    EXPECT(s.last.aimOwned && Dot(s.last.aim, expect) > 0.999f, "the aim stays the line after the hands moved");
    s.Run(0.2);
    EXPECT(s.last.aimOwned, "still held at 0.32 s");
    s.Run(0.2);
    EXPECT(!s.last.aimOwned, "gone at 0.52 s");
}

// [bow] shot_line_hold: the shot's own line stays its aim (shotAimLive) for kShotHoldSeconds - a new draw begun meanwhile does not take it - and only
// aimSeconds without the box, as before.
void TestShotHold() {
    std::printf("the shot's line held until the game takes it\n");
    for (int hold = 0; hold < 2; ++hold) {
        Sim s;
        if (hold) s.p.shotHoldSeconds = kShotHoldSeconds;
        s.in.stringHand = s.Nock();
        s.Step();
        s.Trigger(1.0f);
        const Vec to = s.in.bowHand + Vec{0.3f, 0.0f, 0.5f};
        const Vec from = s.in.stringHand;
        for (int i = 1; i <= 40; ++i) { s.in.stringHand = from + (to - from) * (i / 40.0f); s.Step(); }
        s.Run(0.2);
        const Vec first = Normalized(s.in.bowHand - s.in.stringHand);
        s.Trigger(0.0f);
        EXPECT(s.shots == 1 && s.last.shotAimLive && Dot(s.last.shotAim, first) > 0.999f, "hold %d: the shot's line is its aim at the release", hold);
        // the next draw, straight back, begun 0.5 s after: the old shot's line is still its aim with the box (the game may not have taken the press yet)
        s.MoveTo(s.Nock(), 0.3);   // (the string hand back to the bow)
        s.Draw(0.6f);   // (0.6 s more: about 0.9 s after the shot)
        EXPECT(s.last.drawing, "hold %d: the next draw is under way", hold);
        if (hold) EXPECT(s.last.shotAimLive && Dot(s.last.shotAim, first) > 0.999f, "hold: the first shot's line, not the new draw's, through the new draw");
        else EXPECT(!s.last.shotAimLive, "no hold: the shot's line is gone after 0.4 s, as before");
        s.Run(0.7);   // about 1.6 s after the shot, the string still held
        EXPECT(!s.last.shotAimLive, "hold %d: gone after kShotHoldSeconds", hold);
        s.Trigger(0.0f);   // the second shot: its own line, at once
        EXPECT(s.shots == 2 && s.last.shotAimLive && Dot(s.last.shotAim, Vec{0, 0, -1}) > 0.999f, "hold %d: the second shot's own line", hold);
    }
}

void TestShort() {
    std::printf("what does not shoot: a pull too short, too quick, loosed forward\n");
    {   // 0.25 m apart: under 0.28
        Sim s;
        s.in.stringHand = s.Nock(); s.Step();
        s.Trigger(1.0f);
        s.Pull(0.0f + 0.09f, 0.3);   // 0.09 behind the bow hand's place: gap 0.09 <- nock gap was 0.16, so not even that
        s.Run(0.3);
        s.Trigger(0.0f);
        EXPECT(s.shots == 0, "a pull that gets nowhere does not shoot");
    }
    {   // gap 0.26: under 0.28
        Sim s;
        s.in.stringHand = s.Nock(); s.Step();
        s.Trigger(1.0f);
        s.Pull(0.26f, 0.3);
        s.Run(0.3);
        EXPECT(std::fabs(Length(s.in.bowHand - s.in.stringHand) - 0.26f) < 1e-3f, "setup");
        s.Trigger(0.0f);
        EXPECT(s.shots == 0, "0.26 m is not drawn");
    }
    {   // nocked 0.25 m out, pulled to 0.36: over 0.28 but only 0.11 beyond the nock
        Sim s;
        s.in.stringHand = s.in.bowHand + Vec{0, 0, 0.30f} ;   // 0.14 behind the nock point: within 0.28
        s.Step();
        s.Trigger(1.0f);
        EXPECT(s.nocks == 1, "taken at 0.30 m");
        s.Pull(0.41f, 0.3);
        s.Run(0.3);
        s.Trigger(0.0f);
        EXPECT(s.shots == 0, "0.11 m beyond the nock is not drawn");
    }
    {   // the same nock, pulled to 0.43: 0.13 beyond
        Sim s;
        s.in.stringHand = s.in.bowHand + Vec{0, 0, 0.30f};
        s.Step();
        s.Trigger(1.0f);
        s.Pull(0.43f, 0.3);
        s.Run(0.3);
        s.Trigger(0.0f);
        EXPECT(s.shots == 1, "0.13 m beyond the nock is");
    }
    {   // fast: the pull is there but the hold is under 0.12 s
        Sim s;
        s.in.stringHand = s.Nock(); s.Step();
        s.Trigger(1.0f);
        s.Pull(0.6f, 0.05);   // 0.05 s
        s.Run(0.04);
        s.Trigger(0.0f);
        EXPECT(s.shots == 0, "released 0.1 s after the nock: not held 0.12 s");
    }
    {   // the same, held a little longer
        Sim s;
        s.in.stringHand = s.Nock(); s.Step();
        s.Trigger(1.0f);
        s.Pull(0.6f, 0.05);
        s.Run(0.09);
        s.Trigger(0.0f);
        EXPECT(s.shots == 1, "released 0.15 s after the nock: shoots");
    }
    {   // the string hand ahead of the bow hand (a push), line against the bow
        Sim s;
        s.in.stringHand = s.Nock(); s.Step();
        s.Trigger(1.0f);
        s.Pull(-0.6f, 0.4);   // 0.6 m IN FRONT of the bow hand: the line to the bow points backwards
        s.Run(0.3);
        EXPECT(s.last.charged, "far enough, held long enough");
        s.Trigger(0.0f);
        EXPECT(s.shots == 0, "pushed forward: the line goes against the bow");
    }
    {   // sideways: the line is across the bow, dot 0 -> under 0.15
        Sim s;
        s.in.stringHand = s.Nock(); s.Step();
        s.Trigger(1.0f);
        const Vec from = s.in.stringHand, to = s.in.bowHand + Vec{0.6f, 0, 0};
        for (int i = 1; i <= 36; ++i) { s.in.stringHand = from + (to - from) * (i / 36.0f); s.Step(); }
        s.Run(0.3);
        s.Trigger(0.0f);
        EXPECT(s.shots == 0, "across the bow: no shot");
    }
    {   // 60 degrees off the bow: dot 0.5 -> shoots, along the line, not along the bow
        Sim s;
        s.in.stringHand = s.Nock(); s.Step();
        s.Trigger(1.0f);
        const float c = std::cos(1.0472f), sn = std::sin(1.0472f);   // 60 deg
        const Vec from = s.in.stringHand, to = s.in.bowHand + Vec{0.6f * sn, 0, 0.6f * c};
        for (int i = 1; i <= 36; ++i) { s.in.stringHand = from + (to - from) * (i / 36.0f); s.Step(); }
        s.Run(0.3);
        s.Trigger(0.0f);
        EXPECT(s.shots == 1, "60 degrees off the bow still shoots");
        EXPECT(s.lastAim.x < -0.8f, "and along the line, to the left: %.2f", s.lastAim.x);
    }
}

void TestTrigger() {
    std::printf("the trigger: hysteresis, a press away from the string\n");
    {   // 0.55 on / 0.20 off
        Sim s;
        s.in.stringHand = s.Nock(); s.Step();
        s.Trigger(0.50f);
        EXPECT(s.nocks == 0 && !s.last.owned, "0.50 does not take the string");
        s.Trigger(0.56f);
        EXPECT(s.nocks == 1 && s.last.drawing, "0.56 does");
        s.Pull(0.6f, 0.4);
        s.Run(0.2);
        s.Trigger(0.30f);
        EXPECT(s.last.drawing && s.shots == 0, "0.30 keeps it: a small change of the squeeze does not let go");
        s.Trigger(0.21f);
        EXPECT(s.last.drawing && s.shots == 0, "0.21 too");
        s.Trigger(0.19f);
        EXPECT(s.shots == 1, "0.19 lets go and shoots");
    }
    {   // pressed away from the string: plain, whatever happens after
        Sim s;
        s.in.stringHand = s.in.bowHand + Vec{0.8f, 0.0f, 0.0f};   // far from the nock point
        s.Step();
        s.Trigger(1.0f);
        EXPECT(s.nocks == 0 && !s.last.owned && !s.last.drawing, "away from the string: the plain trigger");
        s.Run(0.2);
        s.MoveTo(s.Nock(), 0.3);   // the hand comes to the string with the trigger still down
        s.Run(0.2);
        EXPECT(s.nocks == 0 && !s.last.owned, "coming to the string with it down does not nock");
        s.Pull(0.6f, 0.4);
        s.Run(0.3);
        s.Trigger(0.0f);
        EXPECT(s.shots == 0 && s.ownedFrames == 0, "and no shot: it stayed the host's plain trigger");
        // the next press at the string does take it
        s.MoveTo(s.Nock(), 0.3);
        s.Trigger(1.0f);
        EXPECT(s.nocks == 1 && s.last.owned, "a new press at the string takes it");
    }
    {   // a trigger down when the detector first sees it (the game began with it held)
        Sim s;
        s.in.stringHand = s.Nock();
        s.in.trigger = 1.0f;
        s.Step();
        EXPECT(s.nocks == 0 && !s.last.owned, "down at the start: plain");
    }
    {   // the near edge
        Sim s;
        s.in.stringHand = s.in.bowHand + Vec{0.8f, 0, 0};
        s.Step();
        EXPECT(!s.last.atString, "far");
        int told = 0;
        for (int i = 1; i <= 30; ++i) { s.in.stringHand = s.in.bowHand + Vec{0.8f, 0, 0} + (s.Nock() - s.in.bowHand - Vec{0.8f, 0, 0}) * (i / 30.0f); told += s.Step().evNear; }
        EXPECT(s.last.atString && told == 1, "near, and told once: %d", told);
        // reach: 0.28
        s.MoveTo(s.Nock() + Vec{0.27f, 0, 0}, 0.1);
        EXPECT(s.last.atString, "0.27 m from the nock point is near");
        s.MoveTo(s.Nock() + Vec{0.29f, 0, 0}, 0.1);
        EXPECT(!s.last.atString, "0.29 m is not");
    }
}

void TestCancel() {
    std::printf("cancel: no shot, and the trigger stays the draw's until it is let go\n");
    {   // too far a pull
        Sim s;
        s.in.stringHand = s.Nock(); s.Step();
        s.Trigger(1.0f);
        s.Pull(1.0f, 0.4);
        s.Run(0.2);
        EXPECT(s.last.drawing && s.last.charged, "1.0 m is a draw");
        s.Pull(1.15f, 0.2);
        EXPECT(!s.last.drawing && s.cancels == 1, "1.15 m is none: cancelled");
        EXPECT(s.last.owned, "the trigger is still held back");
        s.Pull(0.5f, 0.3);   // back in range with the trigger still down
        s.Run(0.2);
        EXPECT(!s.last.drawing && s.last.owned, "back in range: nothing starts");
        s.Trigger(0.0f);
        EXPECT(s.shots == 0, "the release shoots nothing");
        EXPECT(!s.last.owned, "and frees the trigger");
    }
    {   // the host says no (a menu, the weapon, the skill)
        Sim s;
        s.Draw();
        s.in.allowed = false;
        s.Step();
        EXPECT(s.cancels == 1 && s.last.owned, "not allowed: cancelled, held back");
        s.in.allowed = true;
        s.Run(0.1);
        EXPECT(s.last.owned && !s.last.drawing, "allowed again with the trigger down: still not the draw's");
        s.Trigger(0.0f);
        EXPECT(s.shots == 0 && !s.last.owned, "no shot");
    }
    {   // the trigger let go while the host said no: free at once
        Sim s;
        s.Draw();
        s.in.allowed = false;
        s.Step();
        s.Trigger(0.0f);
        EXPECT(!s.last.owned && s.shots == 0, "let go during the break: free, no shot");
        s.in.allowed = true;
        s.in.trigger = 0.0f;
        s.Step();
        EXPECT(!s.last.owned, "idle");
    }
    {   // a tracking loss that lasts (over the 0.25 s grace; shorter ones are held, see TestLoss)
        Sim s;
        s.Draw();
        s.in.valid = false;
        s.Run(0.4);
        s.in.valid = true;
        s.Run(0.1);
        s.Trigger(0.0f);
        EXPECT(s.shots == 0 && s.cancels == 1 && s.lastWhy == kWhyTracking, "pose lost: no shot, one cancel, named");
    }
    {   // an event: a recentre or a swap of weapons
        Sim s;
        s.Draw();
        s.in.cancel = true;
        s.Step();
        s.Trigger(0.0f);
        EXPECT(s.shots == 0 && s.cancels == 1, "an event: no shot");
    }
    {   // a hitch
        Sim s;
        s.Draw();
        s.Step(0.25);
        s.Trigger(0.0f);
        EXPECT(s.shots == 0 && s.cancels == 1, "a quarter of a second between two frames: no shot");
    }
    {   // a hand that jumped (a recentre moves the room under it)
        Sim s;
        s.Draw();
        s.in.bowHand = s.in.bowHand + Vec{1.0f, 0, 0};
        s.in.stringHand = s.in.stringHand + Vec{1.0f, 0, 0};
        s.Step();
        s.Trigger(0.0f);
        EXPECT(s.shots == 0 && s.cancels == 1, "both hands 1 m off in a frame: no shot");
    }
    {   // the weapon or the skill changed under the string
        Sim s;
        s.in.context = 7;
        s.Draw();
        s.in.context = 8;
        s.Step();
        s.Trigger(0.0f);
        EXPECT(s.shots == 0 && s.cancels == 1, "another context: no shot");
    }
    {   // an event while only a plain trigger is down: the host's business, the detector takes nothing
        Sim s;
        s.in.stringHand = s.in.bowHand + Vec{0.8f, 0, 0};
        s.Step();
        s.Trigger(1.0f);
        s.in.cancel = true;
        s.Step();
        EXPECT(!s.last.owned && s.cancels == 0, "a plain press is not the draw's to cancel");
        s.Trigger(0.0f);
        EXPECT(s.shots == 0, "no shot");
    }
}

void TestPulse() {
    std::printf("the press of the key and the next draw\n");
    Sim s;
    s.Draw();
    s.Trigger(0.0f);
    EXPECT(s.shots == 1, "shot");
    // the string hand back to the string and the trigger at once (a fast re-nock): not within the press
    s.MoveTo(s.Nock(), 0.03);
    s.Trigger(1.0f);
    EXPECT(s.nocks == 1 && s.last.owned && !s.last.drawing, "a press at the string inside the key's 65 ms takes nothing: %d nocks", s.nocks);
    s.Trigger(0.0f);
    // after the press a draw starts as usual
    s.Run(0.2);
    s.Draw();
    s.Trigger(0.0f);
    EXPECT(s.shots == 2, "the second draw shoots: %d", s.shots);
    // nonfinite input never shoots and never throws
    Sim n;
    n.Draw();
    n.in.stringHand.x = NAN;
    n.Run(0.4);   // a pose that stays NaN is a lost one
    n.in.stringHand.x = 0.0f;
    n.Trigger(0.0f);
    EXPECT(n.shots == 0, "a NaN pose: no shot");
}

void TestFrames() {
    std::printf("the head's turn frame and a turned scene\n");
    const float pi = 3.14159265f, s2 = std::sqrt(0.5f);
    // the head looks straight ahead (-Z): no turn, a line forward stays forward
    EXPECT(std::fabs(HeadYaw(Quat{})) < 1e-6f, "no turn");
    Vec f = ToHeadTurn({0, 0, -1}, 0.0f);
    EXPECT(std::fabs(f.z + 1.0f) < 1e-6f, "forward is forward");
    // turned 90 degrees to the left (about +Y): + yaw, and what lies where the head looks comes out as (0, 0, -1)
    const Quat left{0, s2, 0, s2};
    EXPECT(std::fabs(HeadYaw(left) - pi / 2) < 1e-5f, "turned left is +90 deg, got %.3f", HeadYaw(left) * 180.0f / pi);
    const Vec ahead = Rotate(left, {0, 0, -1});   // the room's -X
    f = ToHeadTurn(ahead, HeadYaw(left));
    EXPECT(std::fabs(f.x) < 1e-5f && std::fabs(f.z + 1.0f) < 1e-5f, "where the head looks is ahead: %.2f %.2f %.2f", f.x, f.y, f.z);
    // what lies to its right comes out to the right (+X), up stays up
    f = ToHeadTurn(Rotate(left, {1, 0, 0}), HeadYaw(left));
    EXPECT(std::fabs(f.x - 1.0f) < 1e-5f && std::fabs(f.z) < 1e-5f, "to the right is +X: %.2f %.2f %.2f", f.x, f.y, f.z);
    f = ToHeadTurn({0, 1, 0}, 1.0f);
    EXPECT(std::fabs(f.y - 1.0f) < 1e-6f, "up stays up");
    // the head pitched down does not turn the frame (the yaw is about the vertical only)
    const Quat down{-std::sin(0.3f), 0, 0, std::cos(0.3f)};
    EXPECT(std::fabs(HeadYaw(down)) < 1e-5f, "a nod is no turn");

    // the whole scene turned 90 degrees to the left about the vertical: the same draw, the same shot, turned
    Sim a, b;
    b.in.bowRot = left;
    b.in.bowHand = Rotate(left, a.in.bowHand);
    b.in.stringHand = b.Nock();
    // Nock() above assumes the bow along -Z: in this scene it lies along -X, the nock point is behind it
    b.in.stringHand = b.in.bowHand - Rotate(left, {0, 0, -1}) * 0.16f;
    a.Draw();
    b.Step();
    b.Trigger(1.0f);
    const Vec to = b.in.bowHand - Rotate(left, {0, 0, -1}) * 0.6f;
    b.MoveTo(to, 0.4);
    b.Run(0.2);
    b.Trigger(0.0f);
    a.Trigger(0.0f);
    EXPECT(a.shots == 1 && b.shots == 1, "both shoot: %d %d", a.shots, b.shots);
    const Vec expect = Rotate(left, a.lastAim);
    EXPECT(Dot(b.lastAim, expect) > 0.9999f, "the turned scene shoots the turned way: %.2f %.2f %.2f", b.lastAim.x, b.lastAim.y, b.lastAim.z);
    // and in the head's frame (the head turned with the scene) it is the same as the first scene's, in its own
    const Vec ha = ToHeadTurn(a.lastAim, HeadYaw(Quat{})), hb = ToHeadTurn(b.lastAim, HeadYaw(left));
    EXPECT(Dot(ha, hb) > 0.9999f, "the head-turn frame does not care how the room is turned: %.2f %.2f %.2f / %.2f %.2f %.2f", ha.x, ha.y, ha.z, hb.x, hb.y, hb.z);
}

// The grip as the start (Params::start): the natural grab. Same draw, the grip down at the string and let go.
void TestGripStart() {
    std::printf("the grip takes the string\n");
    {   // start = grip: the grip draws, the trigger is the plain trigger
        Sim s;
        s.p.start = kStartGrip;
        s.in.stringHand = s.Nock(); s.Step();
        s.in.trigger = 1.0f;
        s.Step();
        EXPECT(s.nocks == 0 && !s.last.owned, "the trigger does not take it when the grip is the start");
        s.in.trigger = 0.0f;
        s.Step();
        s.in.grip = 0.50f;
        s.Step();
        EXPECT(s.nocks == 0, "a grip of 0.50 is under 0.60");
        s.in.grip = 0.65f;
        s.Step();
        EXPECT(s.nocks == 1 && s.last.drawing && s.last.owned && s.last.hold == 1, "the grip at 0.65 takes it, and owns the grip: hold %d", s.last.hold);
        s.Pull(0.6f, 0.4);
        s.Run(0.2);
        EXPECT(s.last.charged, "charged");
        s.in.grip = 0.40f;
        s.Step();
        EXPECT(s.last.drawing && s.shots == 0, "0.40 keeps it (off at 0.35)");
        s.in.grip = 0.30f;
        s.Step();
        EXPECT(s.shots == 1 && std::fabs(s.lastAim.z + 1.0f) < 1e-3f, "0.30 lets go and shoots along the line: %d", s.shots);
    }
    {   // the trigger squeezed while the grip holds the string is nobody's business
        Sim s;
        s.p.start = kStartGrip;
        s.in.stringHand = s.Nock(); s.Step();
        s.in.grip = 1.0f; s.Step();
        s.Pull(0.6f, 0.4);
        s.Run(0.2);
        s.in.trigger = 1.0f; s.Step();
        s.in.trigger = 0.0f; s.Step();
        EXPECT(s.last.drawing && s.shots == 0, "the trigger neither shoots nor cancels the grip's draw");
        s.in.grip = 0.0f; s.Step();
        EXPECT(s.shots == 1, "the grip's release shoots");
    }
    {   // a grip pressed away from the string is the grip's other duties (belt, shoulder...): plain, and the string is not taken by moving in
        Sim s;
        s.p.start = kStartGrip;
        s.in.stringHand = s.in.bowHand + Vec{0.8f, 0, 0};
        s.Step();
        s.in.grip = 1.0f; s.Step();
        s.MoveTo(s.Nock(), 0.3);
        s.Run(0.2);
        EXPECT(s.nocks == 0 && !s.last.owned, "a grip pressed away stays plain");
        s.in.grip = 0.0f; s.Step();
        s.in.grip = 1.0f; s.Step();
        EXPECT(s.nocks == 1, "a new press at the string takes it");
    }
    {   // either: whichever is pressed; the draw is that control's
        Sim s;
        s.p.start = kStartEither;
        s.in.stringHand = s.Nock(); s.Step();
        s.in.trigger = 1.0f; s.Step();
        EXPECT(s.nocks == 1 && s.last.hold == 0, "the trigger takes it with either: hold %d", s.last.hold);
        s.Pull(0.6f, 0.4);
        s.Run(0.2);
        s.in.grip = 1.0f; s.Step();
        s.in.grip = 0.0f; s.Step();
        EXPECT(s.last.drawing && s.shots == 0, "its grip does not let go of a trigger's draw");
        s.in.trigger = 0.0f; s.Step();
        EXPECT(s.shots == 1, "the trigger's release shoots");
        Sim g;
        g.p.start = kStartEither;
        g.in.stringHand = g.Nock(); g.Step();
        g.in.grip = 1.0f; g.Step();
        EXPECT(g.nocks == 1 && g.last.hold == 1, "the grip takes it with either: hold %d", g.last.hold);
        g.Pull(0.6f, 0.4);
        g.Run(0.2);
        g.in.trigger = 1.0f; g.Step();
        g.in.trigger = 0.0f; g.Step();
        EXPECT(g.last.drawing && g.shots == 0, "its trigger does not let go of a grip's draw");
        g.in.grip = 0.0f; g.Step();
        EXPECT(g.shots == 1, "the grip's release shoots");
    }
    {   // the default mode is the trigger: the grip alone does nothing, as it always did
        Sim s;
        s.in.stringHand = s.Nock(); s.Step();
        s.in.grip = 1.0f; s.Step();
        s.Run(0.2);
        EXPECT(s.nocks == 0 && !s.last.owned, "grip alone, start = trigger: nothing");
    }
    {   // a cancel while the grip owns: held back until the grip is let go
        Sim s;
        s.p.start = kStartGrip;
        s.in.stringHand = s.Nock(); s.Step();
        s.in.grip = 1.0f; s.Step();
        s.Pull(0.6f, 0.4);
        s.in.allowed = false; s.Step();
        EXPECT(s.last.owned && s.last.hold == 1, "owned by the grip after a break");
        s.in.allowed = true; s.in.trigger = 1.0f; s.Step();
        EXPECT(s.last.owned, "a trigger does not free it");
        s.in.trigger = 0.0f; s.in.grip = 0.0f; s.Step();
        EXPECT(!s.last.owned && s.shots == 0, "the grip's release frees it, no shot");
    }
    {   // the touch point from the model: the draw takes the string there, not at the constant
        Sim s;
        s.in.stringHand = s.in.bowHand + Vec{0, 0, 0.40f};   // 0.40 m behind the bow hand: 0.24 from the constant's point (reach 0.28)
        s.Step();
        EXPECT(s.last.atString, "inside the constant's reach");
        s.MoveTo(s.in.bowHand + Vec{0, 0, 0.55f}, 0.1);      // 0.39 from it
        EXPECT(!s.last.atString, "outside it");
        s.in.hasNock = true;
        s.in.nock = s.in.bowHand + Vec{0, 0, 0.55f};         // the model's string is there
        s.Step();
        EXPECT(s.last.atString && std::fabs(s.last.nock.z - s.in.nock.z) < 1e-6f, "inside the model's reach");
        s.Trigger(1.0f);
        EXPECT(s.nocks == 1, "the string is taken there");
    }
}

// The green zone: its radius (Params::reach) and its centre (the nock point the host hands in, moved along the bow).
void TestZone() {
    std::printf("the grab zone: radius and centre\n");
    for (float reach : {0.05f, 0.10f, 0.28f, 0.50f}) {
        Sim s;
        s.p.reach = reach;
        // the string hand `d` behind the touch point, along the bow: inside below the radius, outside above it
        auto at = [&](float d) { s.in.stringHand = s.Nock() + Vec{0, 0, d}; s.in.trigger = 0.0f; s.Step(); return s.last.atString; };
        EXPECT(at(reach - 0.01f), "inside %.2f at %.2f", reach, reach - 0.01f);
        EXPECT(!at(reach + 0.01f), "outside %.2f at %.2f", reach, reach + 0.01f);
        // and a press takes the string only inside
        s.in.stringHand = s.Nock() + Vec{0, 0, reach + 0.02f};
        s.Step();
        s.in.trigger = 1.0f;
        s.Step();
        EXPECT(s.nocks == 0 && !s.last.owned, "a press outside %.2f is the plain trigger", reach);
        s.in.trigger = 0.0f;
        s.Step();
        s.in.stringHand = s.Nock() + Vec{0, 0, reach - 0.02f};
        s.Step();
        s.in.trigger = 1.0f;
        s.Step();
        EXPECT(s.nocks == 1, "a press inside %.2f takes the string", reach);
    }
    // the centre moved along the bow (the host's shift): the zone follows, the old centre is outside a small zone
    for (float shift : {-0.20f, -0.05f, 0.0f, 0.10f, 0.25f}) {
        Sim s;
        s.p.reach = 0.08f;
        s.in.hasNock = true;
        s.in.nock = s.Nock() + Vec{0, 0, shift};   // + = further from the grip (behind it, +Z here)
        s.in.stringHand = s.in.nock;
        s.Step();
        EXPECT(s.last.atString && Length(s.last.nock - s.in.nock) < 1e-6f, "the zone is at the shifted point %+.2f", shift);
        s.in.trigger = 1.0f;
        s.Step();
        EXPECT(s.nocks == 1, "taken at the shifted point %+.2f", shift);
        Sim t;
        t.p.reach = 0.08f;
        t.in.hasNock = true;
        t.in.nock = t.Nock() + Vec{0, 0, shift};
        t.in.stringHand = t.Nock();   // the unshifted place
        t.Step();
        EXPECT(t.last.atString == (std::fabs(shift) < 0.08f), "the old place is %s a zone shifted %+.2f", t.last.atString ? "in" : "out of", shift);
    }
    // a loose release is told once (the rig lets the string back to rest)
    Sim s;
    s.in.stringHand = s.Nock(); s.Step();
    s.Trigger(1.0f);
    s.Run(0.1);
    int loose = 0;
    s.in.trigger = 0.0f;
    for (int i = 0; i < 4; ++i) { s.Step(); loose += s.last.evLoose; }
    EXPECT(loose == 1 && s.shots == 0, "a release with no draw is one loose event: %d", loose);
    Sim d;
    d.Draw();
    d.Trigger(0.0f);
    EXPECT(d.shots == 1 && !d.last.evLoose, "a shot is not a loose release");
}

// The least pull that shoots (Params::minPull, [hands] bow_draw_min_pull_cm): beyond the nock by this, and the hands pullFloor + this apart.
void TestMinPull() {
    std::printf("the least pull that shoots\n");
    // a release at hands `gap` apart after a draw held 0.3 s, the string taken at the default nock (16 cm); did it shoot?
    auto shoots = [](float minPull, float gap, float nockBehind = 0.16f) {
        Sim s;
        s.p.minPull = minPull;
        s.in.stringHand = s.in.bowHand + Vec{0, 0, nockBehind};
        s.Step();
        s.Trigger(1.0f);
        s.Pull(gap, 0.3);
        s.Run(0.3);
        s.Trigger(0.0f);
        return s.shots == 1;
    };
    // the default is the old rule to the last bit: 0.28 m apart and 0.12 m beyond the nock; checked against it over the range
    int differ = 0;
    for (int k = 0; k < 220; ++k) {
        const float gap = 0.17f + 0.001f * k + 0.0005f;   // off the boundaries
        const bool old = gap >= 0.28f && gap - 0.16f >= 0.12f;
        if (shoots(0.12f, gap) != old) ++differ;
    }
    EXPECT(differ == 0, "the default minimum pull shoots exactly where it did: %d of 220 differ", differ);
    EXPECT(!shoots(0.12f, 0.275f) && shoots(0.12f, 0.285f), "the 28 cm edge");
    // less: 5 cm beyond the nock and 21 cm apart
    EXPECT(!shoots(0.05f, 0.205f) && shoots(0.05f, 0.215f), "5 cm: 21 cm apart");
    EXPECT(!shoots(0.08f, 0.235f) && shoots(0.08f, 0.245f), "8 cm: 24 cm apart");
    // more: 40 cm beyond and 56 cm apart
    EXPECT(!shoots(0.40f, 0.55f) && shoots(0.40f, 0.57f), "40 cm: 56 cm apart");
    EXPECT(!shoots(0.25f, 0.40f) && shoots(0.25f, 0.42f), "25 cm: 41 cm apart");
    // the string taken further out: beyond is from where it was taken (nock gap 0.30)
    EXPECT(!shoots(0.12f, 0.41f, 0.30f) && shoots(0.12f, 0.43f, 0.30f), "taken at 30 cm, 12 cm beyond: 42 cm");
    EXPECT(!shoots(0.05f, 0.34f, 0.30f) && shoots(0.05f, 0.36f, 0.30f), "taken at 30 cm, 5 cm beyond: 35 cm");
    // taken near (nock gap 0.10): the floor 16 cm + the pull applies, not just 'beyond'
    EXPECT(!shoots(0.05f, 0.20f, 0.10f) && shoots(0.05f, 0.22f, 0.10f), "taken at 10 cm, 5 cm: the 21 cm floor");
    // the zone it draws: pullNeed is the gap from which a release shoots, pullEnough flips there
    {
        Sim s;
        s.p.minPull = 0.07f;
        s.in.stringHand = s.in.bowHand + Vec{0, 0, 0.20f};   // taken at 20 cm
        s.Step();
        s.Trigger(1.0f);
        EXPECT(std::fabs(s.last.pullNeed - 0.27f) < 1e-5f && !s.last.pullEnough, "need 27 cm, not yet: %.3f", s.last.pullNeed);
        s.MoveTo(s.in.bowHand + Vec{0, 0, 0.265f}, 0.1);
        EXPECT(!s.last.pullEnough, "26.5 cm is short");
        s.MoveTo(s.in.bowHand + Vec{0, 0, 0.275f}, 0.1);
        EXPECT(s.last.pullEnough && std::fabs(s.last.pullNeed - 0.27f) < 1e-5f, "27.5 cm is enough");
        // the same ring with the default pull is 0.32 when taken at 20 cm
        Sim d;
        d.in.stringHand = d.in.bowHand + Vec{0, 0, 0.20f};
        d.Step();
        d.Trigger(1.0f);
        EXPECT(std::fabs(d.last.pullNeed - 0.32f) < 1e-5f, "default: %.3f", d.last.pullNeed);
        // nothing taken yet but the string near: the least it can be (16 cm + the pull)
        Sim n;
        n.p.minPull = 0.05f;
        n.in.stringHand = n.Nock();
        n.Step();
        EXPECT(std::fabs(n.last.pullNeed - 0.21f) < 1e-5f, "near: %.3f", n.last.pullNeed);
    }
    // the draw amount the rig reads never calls a pull that shoots a short one
    {
        Sim s;
        s.p.minPull = 0.05f;
        s.in.stringHand = s.Nock();
        s.Step();
        s.Trigger(1.0f);
        s.Pull(0.225f, 0.3);   // 6.5 cm beyond the nock: enough at 5 cm, 0.14 linear
        EXPECT(s.last.draw >= kRigShortPull, "a pull that shoots reads %.2f to the rig", s.last.draw);
        Sim t;
        t.p.minPull = 0.05f;
        t.in.stringHand = t.Nock();
        t.Step();
        t.Trigger(1.0f);
        t.Pull(0.195f, 0.3);   // 3.5 cm: not enough
        EXPECT(t.last.draw < kRigShortPull, "a pull that does not shoot stays short: %.2f", t.last.draw);
    }
}

// What the draw keeps from the layout, and what it only strips of its shots: with "a press away from the string shoots nothing" the trigger in
// full in the grab zone (a press there is the draw's) and the shots of the trigger and the grip everywhere else.
void TestKeep() {
    std::printf("what the draw keeps from the layout\n");
    // allowed, drawOnly, owned, hold, atString, start -> trigger, grip, noShotTrigger, noShotGrip
    struct Row { bool allowed, only, owned; int hold; bool atString; int start; bool trigger, grip, noShotTrigger, noShotGrip; };
    const Row rows[] = {
        {true, false, false, -1, false, kStartTrigger, false, false, false, false},   // the old plain trigger: nothing kept, nothing stripped
        {true, true, false, -1, false, kStartTrigger, false, false, true, true},      // draw only, away from the string: only the shots go; the rest is as bound
        {true, true, false, -1, true, kStartTrigger, true, false, false, true},       // ... in the zone: the trigger is the draw's, whole
        {true, true, false, -1, true, kStartEither, true, false, false, true},
        {true, true, false, -1, true, kStartGrip, false, false, true, true},          // ... the grip starts the draw: the trigger is not the string's - shots only
        {true, false, false, -1, true, kStartTrigger, false, false, false, false},    // the zone alone keeps nothing without draw-only (the draw's own press takes it)
        {false, true, false, -1, true, kStartTrigger, false, false, false, false},    // not allowed (another weapon, a menu, a panel, another view): nothing
        {false, false, false, -1, false, kStartTrigger, false, false, false, false},
        {true, false, true, 0, true, kStartTrigger, true, false, false, false},       // the trigger took the string: kept
        {true, true, true, 0, false, kStartTrigger, true, false, false, true},        // ... with draw only the grip's shots still go
        {true, false, true, 1, true, kStartGrip, false, true, false, false},          // the grip took it: the grip is kept, the trigger stays the layout's (old behaviour)
        {true, true, true, 1, true, kStartGrip, true, true, false, false},            // ... and with draw only the trigger too
        {false, true, true, 0, false, kStartTrigger, true, false, false, false},      // a draw ended by a menu is still the trigger's until it is let go
        {false, true, true, 1, false, kStartGrip, false, true, false, false},         // ... by the grip: the grip, and no trigger kept where the draw may not act
    };
    for (const Row& r : rows) {
        const Keep k = KeepFromLayout(r.allowed, r.only, r.owned, r.hold, r.atString, r.start);
        EXPECT(k.trigger == r.trigger && k.grip == r.grip && k.noShotTrigger == r.noShotTrigger && k.noShotGrip == r.noShotGrip,
               "allowed %d only %d owned %d hold %d zone %d start %d: trigger %d grip %d noShot %d/%d", r.allowed, r.only, r.owned, r.hold, r.atString, r.start,
               k.trigger, k.grip, k.noShotTrigger, k.noShotGrip);
    }
    // the old call, without the zone and the start: a press anywhere is away from the string
    {
        const Keep k = KeepFromLayout(true, true, false, -1);
        EXPECT(!k.trigger && k.noShotTrigger, "the short form is 'away from the string'");
    }
    // the detector side: a press away from the string is Plain and owns nothing - the host strips its shots
    Sim s;
    s.in.stringHand = s.in.bowHand + Vec{0.8f, 0, 0};
    s.Step();
    s.Trigger(1.0f);
    EXPECT(!s.last.owned && s.last.hold == -1 && s.nocks == 0 && !s.last.atString, "away from the string: no draw, nothing owned, not in the zone");
    const Keep k = KeepFromLayout(true, true, s.last.owned, s.last.hold, s.last.atString);
    EXPECT(!k.trigger && !k.grip && k.noShotTrigger, "and with draw only the host strips its shots, keeping nothing whole");
    // at the string: the draw owns it
    Sim t;
    t.in.stringHand = t.Nock();
    t.Step();
    t.Trigger(1.0f);
    EXPECT(t.last.owned && t.last.hold == 0 && t.last.atString, "at the string the trigger is the draw's");
    const Keep kz = KeepFromLayout(true, true, t.last.owned, t.last.hold, t.last.atString);
    EXPECT(kz.trigger && !kz.noShotTrigger, "and kept whole");
    // in the zone with the trigger not pressed yet: the zone is already the draw's (the first frame of a press needs no owned flag)
    Sim u;
    u.in.stringHand = u.Nock();
    u.Step();
    EXPECT(!u.last.owned && u.last.atString, "in the zone, nothing pressed");
    const Keep ku = KeepFromLayout(true, true, u.last.owned, u.last.hold, u.last.atString);
    EXPECT(ku.trigger, "the trigger is already kept whole there");
}

// A controller out of the cameras' view for a moment: the draw is held (its trigger reads as let go meanwhile), not ended.
void TestLoss() {
    std::printf("a short loss of a hand\n");
    {   // 0.1 s lost, the trigger reading 0, then back: still drawing, no cancel, the release shoots
        Sim s;
        s.Draw();
        EXPECT(s.last.drawing && s.last.charged, "drawn");
        s.in.valid = false;
        s.in.trigger = 0.0f;   // an untracked controller reads as let go
        for (int i = 0; i < 9; ++i) {
            s.Step();
            EXPECT(s.last.drawing && s.last.owned && s.last.lost && !s.last.shoot && !s.last.evCancel, "held, frame %d", i);
        }
        s.in.valid = true;
        s.in.trigger = 1.0f;   // and it is back, still held
        s.Step();
        EXPECT(s.last.drawing && !s.last.lost && s.cancels == 0 && s.shots == 0, "back: still drawing");
        s.Run(0.1);
        EXPECT(s.last.drawing && s.cancels == 0, "still drawing");
        s.Trigger(0.0f);
        EXPECT(s.shots == 1, "and the release shoots");
    }
    {   // lost for 0.4 s: over the grace, the draw ends with "tracking lost"
        Sim s;
        s.Draw();
        s.in.valid = false;
        s.in.trigger = 0.0f;
        s.Run(0.4);
        EXPECT(s.cancels == 1, "ended once: %d", s.cancels);
        EXPECT(!s.last.drawing && !s.last.lost, "no longer held");
        s.in.valid = true;
        s.Step();
        s.Trigger(0.0f);
        EXPECT(s.shots == 0, "nothing shot over a loss");
    }
    {   // lost for 0.2 s and the trigger really let go meanwhile: back with it up = the release, evaluated now
        Sim s;
        s.Draw();
        s.in.valid = false;
        s.in.trigger = 0.0f;
        s.Run(0.2);
        s.in.valid = true;
        s.Step();
        EXPECT(s.shots == 1 && s.cancels == 0, "a release on the way back counts: %d shots", s.shots);
    }
    {   // the hands moved a long way while lost: the first frame back is judged by a wider jump
        Sim s;
        s.Draw();
        s.in.valid = false;
        s.Run(0.2);
        s.in.valid = true;
        s.in.bowHand = s.in.bowHand + Vec{0.5f, 0, 0};
        s.in.stringHand = s.in.stringHand + Vec{0.5f, 0, 0};
        s.Step();
        EXPECT(s.last.drawing && s.cancels == 0, "0.5 m moved while lost is a draw going on");
        Sim t;
        t.Draw();
        t.in.valid = false;
        t.Run(0.2);
        t.in.valid = true;
        t.in.bowHand = t.in.bowHand + Vec{1.5f, 0, 0};
        t.in.stringHand = t.in.stringHand + Vec{1.5f, 0, 0};
        t.Step();
        EXPECT(!t.last.drawing && t.cancels == 1 && t.last.why == kWhyJump, "1.5 m is a jump");
    }
    {   // a loss that is also something else (a menu, an event, another weapon) is not held
        Sim s;
        s.Draw();
        s.in.valid = false;
        s.in.allowed = false;
        s.Step();
        EXPECT(s.cancels == 1 && s.last.why == kWhyNotAllowed, "a menu in the loss: ended");
        Sim e;
        e.Draw();
        e.in.valid = false;
        e.in.cancel = true;
        e.Step();
        EXPECT(e.cancels == 1 && e.last.why == kWhyEvent, "an event in the loss: ended");
        Sim c;
        c.in.context = 5;
        c.Draw();
        c.in.valid = false;
        c.in.context = 6;
        c.Step();
        EXPECT(c.cancels == 1, "another weapon in the loss: ended");
    }
    {   // a loss before the string is taken, or with only a plain trigger, is nothing to hold
        Sim s;
        s.in.stringHand = s.Nock();
        s.Step();
        s.in.valid = false;
        s.Step();
        s.in.valid = true;
        s.Trigger(1.0f);
        EXPECT(s.nocks == 1, "a loss before the nock leaves nothing behind");
    }
}

// Why a draw ended: each reason is named.
void TestWhy() {
    std::printf("why a draw ended\n");
    {   Sim s; s.Draw(); s.in.allowed = false; s.Step();
        EXPECT(s.cancels == 1 && s.last.why == kWhyNotAllowed && std::strlen(WhyName(s.last.why)) > 0, "not allowed"); }
    {   Sim s; s.Draw(); s.in.cancel = true; s.Step();
        EXPECT(s.last.why == kWhyEvent, "an event"); }
    {   Sim s; s.Draw(); s.Step(0.25);
        EXPECT(s.last.why == kWhyHitch, "a hitch"); }
    {   Sim s; s.in.context = 1; s.Draw(); s.in.context = 2; s.Step();
        EXPECT(s.last.why == kWhyContext, "the context"); }
    {   Sim s; s.in.stringHand = s.Nock(); s.Step(); s.Trigger(1.0f); s.Pull(1.0f, 0.4);
        s.in.stringHand = s.in.bowHand + Vec{0, 0, 1.2f};
        s.Step();
        EXPECT(s.cancels == 1 && s.last.why == kWhyTooLong, "a pull too long: %d", s.last.why); }
}

void TestUnticked() {
    std::printf("not allowed: the detector takes nothing\n");
    Sim s;
    s.in.allowed = false;
    s.in.stringHand = s.Nock();
    s.Step();
    s.Trigger(1.0f);
    s.Pull(0.6f, 0.4);
    s.Run(0.3);
    s.Trigger(0.0f);
    EXPECT(s.shots == 0 && s.ownedFrames == 0 && s.nocks == 0 && s.pressFrames == 0, "never owns the trigger, never shoots");
}

}  // namespace

int main() {
    TestForward();
    TestSkills();
    TestShot();
    TestAimHold();
    TestShotHold();
    TestShort();
    TestTrigger();
    TestCancel();
    TestPulse();
    TestFrames();
    TestGripStart();
    TestZone();
    TestMinPull();
    TestKeep();
    TestLoss();
    TestWhy();
    TestUnticked();
    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}
