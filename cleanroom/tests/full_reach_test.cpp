// vr/full_reach.h: [arms] full_reach - the collarbone's least turn that brings a target into the arm's reach, bounded, and the
// stretch for what is still missing. Synthetic arms in the head's frame (x right, y up, z back), metres.
#include <cmath>
#include <cstdio>

#include "../../vr/full_reach.h"

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

using fullreach::V;

// A wide-shouldered hero's right arm: the collarbone's joint by the breastbone, the shoulder joint 0.22 m out from it, the arm
// (upper arm + forearm) 0.55 m.
static const V kP{0.04f, -0.24f, 0.06f};
static const V kS{0.26f, -0.24f, 0.08f};
static const float kL = 0.55f;

static float Dist(V a, V b) { return fullreach::Len(fullreach::Sub(a, b)); }

int main() {
    using namespace fullreach;

    // -- in reach: nothing turns
    {
        const V t{0.30f, -0.45f, -0.25f};   // ahead, low, on its own side
        Check(Dist(t, kS) < kL, "(the case is in reach)");
        const Turned r = TurnShoulder(kP, kS, t, kL);
        Check(!r.turn && r.rad == 0.0f && Near(r.shoulder.x, kS.x) && Near(r.shoulder.z, kS.z), "in reach: the shoulder stays");
        Check(Stretch(Dist(t, kS), Dist(t, kS)) == 1.0f, "in reach: no stretch");
    }
    // -- a slash across the body: the right hand at the left hip, out of reach of the right shoulder - the collarbone turns it forward
    //    and in, just enough, and the arm then reaches without a stretch
    {
        const V t{-0.22f, -0.42f, -0.30f};
        const float d0 = Dist(t, kS);
        Check(d0 > kL * kReach, "(the case is out of reach)");
        const Turned r = TurnShoulder(kP, kS, t, kL);
        Check(r.turn && r.rad > 0.0f && r.rad <= kShoulderTurnDeg * 0.0174533f + 1e-5f, "across the body: a turn, within the breastbone's");
        Check(Near(Dist(r.shoulder, kP), Dist(kS, kP), 1e-4f), "the collarbone keeps its length");
        Check(r.shoulder.z < kS.z && r.shoulder.x < kS.x, "the shoulder comes forward and in, toward the hand");
        const float d1 = Dist(t, r.shoulder);
        Check(d1 < d0, "the target is nearer the turned shoulder");
        if (r.rad < kShoulderTurnDeg * 0.0174533f - 1e-4f) {
            Check(Near(d1, kL * kReach, 2e-4f), "not at the bound: the least turn - the target just in reach");
        }
        // the least: a smaller turn leaves it out of reach
        const V less = Add(kP, Turn(Sub(kS, kP), r.axis, r.rad * 0.9f));
        Check(Dist(t, less) > kL * kReach, "a smaller turn does not reach: the least turn");
    }
    // -- far across: the turn stops at the bound, the stretch does the rest (and the two put the wrist on the target)
    {
        const V t{-0.45f, -0.40f, -0.30f};
        const Turned r = TurnShoulder(kP, kS, t, kL);
        Check(r.turn && Near(r.rad, kShoulderTurnDeg * 0.0174533f, 1e-4f), "far across: the turn stops at the breastbone's bound");
        const float d1 = Dist(t, r.shoulder);
        Check(d1 > kL * 0.999f, "(still out of reach after the turn)");
        const float wrist = kL * 0.999f;   // the IK's straight arm
        const float k = Stretch(d1, wrist);
        Check(k > 1.0f && k <= kMaxStretch, "the rest is a stretch, within its bound");
        if (k < kMaxStretch) Check(Near(wrist * k, d1, 1e-4f), "the stretched arm's wrist is on the target");
        // the turn alone, with a smaller bound: the same direction, less
        const Turned r5 = TurnShoulder(kP, kS, t, kL, 5.0f);
        Check(r5.turn && Near(r5.rad, 5.0f * 0.0174533f, 1e-4f) && Dist(t, r5.shoulder) > d1, "a smaller bound turns less");
    }
    // -- straight ahead past the arm's length (an arm too short for the player's): the shoulder comes forward, then the stretch
    {
        const V t{0.26f, -0.30f, -0.62f};
        const Turned r = TurnShoulder(kP, kS, t, kL);
        Check(r.turn && r.shoulder.z < kS.z, "too far ahead: the shoulder comes forward");
        Check(Dist(t, r.shoulder) < Dist(t, kS), "and nearer");
    }
    // -- reaching up past the arm: the shoulder comes up (a shrug), never down
    {
        const V t{0.30f, 0.38f, 0.05f};
        const Turned r = TurnShoulder(kP, kS, t, kL);
        Check(r.turn && r.shoulder.y > kS.y, "reaching up: the shoulder rises");
    }
    // -- the stretch's bound: a controller that jumped a metre away stays short
    Check(Near(Stretch(2.0f, 0.55f), kMaxStretch), "a jump: the stretch is bounded");
    Check(Stretch(0.5501f, 0.55f) == 1.0f, "a hair past the arm (the IK's 0.999): no stretch");
    Check(Near(Stretch(0.66f, 0.55f), 1.2f, 1e-4f), "a target 20 % past the wrist: 20 % longer");
    Check(Stretch(NAN, 0.55f) == 1.0f && Stretch(0.7f, 0.0f) == 1.0f && Stretch(0.7f, NAN) == 1.0f, "nothing to go by: no stretch");
    // -- degenerate: no collarbone length, no arm, not finite, the shoulder pointing straight at the target or straight away
    {
        const V t{-0.4f, -0.4f, -0.4f};
        Check(!TurnShoulder(kS, kS, t, kL).turn, "no collarbone (its joint on the shoulder): no turn");
        Check(!TurnShoulder(kP, kS, t, 0.0f).turn, "no arm: no turn");
        Check(!TurnShoulder(kP, kS, V{NAN, 0, 0}, kL).turn, "a target not finite: no turn");
        const V ahead = Add(kS, Mul(Sub(kS, kP), 5.0f));   // on the collarbone's line, past the shoulder
        Check(!TurnShoulder(kP, kS, ahead, kL).turn, "the collarbone already points at it: no turn (it would only take the shoulder away)");
        const V behind = Sub(kP, Mul(Sub(kS, kP), 4.0f));   // on the line, the far side of the breastbone
        const Turned r = TurnShoulder(kP, kS, behind, kL);
        Check(r.turn && Dist(behind, r.shoulder) < Dist(behind, kS), "straight away: turned about some square axis, nearer");
    }
    // -- the left arm, mirrored: the same turn mirrored
    {
        const V pL{-kP.x, kP.y, kP.z}, sL{-kS.x, kS.y, kS.z}, t{0.22f, -0.42f, -0.30f}, tR{-0.22f, -0.42f, -0.30f};
        const Turned l = TurnShoulder(pL, sL, t, kL), r = TurnShoulder(kP, kS, tR, kL);
        Check(l.turn && r.turn && Near(l.rad, r.rad, 1e-5f) && Near(l.shoulder.x, -r.shoulder.x, 1e-5f) && Near(l.shoulder.z, r.shoulder.z, 1e-5f),
              "the left arm: the right one's turn mirrored");
    }
    // -- Turn is right-handed (x about z by 90 deg is y)
    {
        const V v = Turn(V{1, 0, 0}, V{0, 0, 1}, 1.5707963f);
        Check(Near(v.x, 0.0f, 1e-5f) && Near(v.y, 1.0f, 1e-5f), "Turn: right-handed");
    }

    if (g_fail) { std::fprintf(stderr, "full_reach_test: %d failed\n", g_fail); return 1; }
    std::printf("full_reach_test: all passed\n");
    return 0;
}
