// [body] crouch (vr/crouch.h): when the player crouches, and how the hero's legs bend for it, on synthetic numbers - no
// headset, no game:
//
//   crouch_test        every case; exit code 0 = all passed
//
// detect    the threshold, a dip under it, the eased way in, following the head once in
// back      hysteresis: standing again only within half the threshold, eased out
// seated    recentred seated: the head at the recentre's height, leaning a little, rising above it - never a crouch
// lost      tracking lost: the pose held a moment, then standing; a bad step (NaN, a long gap) changes nothing it should not
// legs      LegRoom, KneePole and Knee: lengths kept, the foot kept, the knee ahead, nothing past the folded leg

#include "crouch.h"

#include <cstdio>
#include <limits>

using namespace crouch;

namespace {

int g_failed = 0, g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool Near(V a, V b, float eps = 1e-4f) { return Length(a - b) <= eps; }

constexpr float kTick = 0.01f;   // PushArms' 10 ms

// `seconds` of steps with the head held `downM` below the recentre; the last drop returned
float Hold(Tracker& t, float downM, float seconds, const Params& p, bool valid = true) {
    float out = 0.0f;
    for (float s = 0.0f; s < seconds - 1e-6f; s += kTick) out = t.Step(valid, downM, kTick, p);
    return out;
}

void DetectCases() {
    Params p;   // 20 cm, no floor
    Tracker t;
    Check(t.Step(true, 0.0f, kTick, p) == 0.0f && !t.Crouched(), "detect: standing at the recentre");
    Check(Hold(t, 0.19f, 1.0f, p) == 0.0f && !t.Crouched(), "detect: a dip just under the threshold stays standing");
    Check(Hold(t, 0.12f, 1.0f, p) == 0.0f, "detect: a nod or a bend of the neck: nothing");
    // in: the first step past the threshold starts it, eased over kBlendS
    float d = t.Step(true, 0.30f, kTick, p);
    Check(t.Crouched(), "detect: past the threshold: a crouch");
    Check(d > 0.0f && d < 0.30f * 0.1f, "detect: no jump of the hips on the first step");
    float last = d;
    bool rising = true;
    for (int i = 0; i < 14; ++i) { const float n = t.Step(true, 0.30f, kTick, p); rising = rising && n > last; last = n; }
    Check(rising && last < 0.30f, "detect: eased in, step by step");
    Check(Near(Hold(t, 0.30f, kBlendS, p), 0.30f), "detect: once in, the body takes the whole drop");
    Check(Near(t.Step(true, 0.55f, kTick, p), 0.55f), "detect: deeper - followed at once, the neck stays under the camera");
    Check(Near(t.Step(true, 0.25f, kTick, p), 0.25f), "detect: shallower but still crouched - followed at once");
    // the threshold is the slider's
    Params low;
    low.enterM = 0.10f;
    Tracker u;
    Hold(u, 0.12f, 0.5f, low);
    Check(u.Crouched(), "detect: a 10 cm threshold takes a 12 cm dip");
    // a threshold of 0 (or NaN) is not every millimetre
    Params zero;
    zero.enterM = 0.0f;
    Tracker z;
    Hold(z, 0.005f, 0.5f, zero);
    Check(!z.Crouched(), "detect: threshold 0 is held to a centimetre");
    zero.enterM = std::numeric_limits<float>::quiet_NaN();
    Hold(z, 0.15f, 0.5f, zero);
    Check(!z.Crouched(), "detect: a NaN threshold falls back to 20 cm");
    // the floor: never deeper than the standing eye height
    Params floor;
    floor.floorM = 1.6f;
    Tracker f;
    Check(Near(Hold(f, 2.5f, 0.5f, floor), 1.6f), "detect: no deeper than the floor");
}

void BackCases() {
    Params p;
    Tracker t;
    Hold(t, 0.40f, 0.5f, p);
    Check(t.Crouched() && Near(t.Weight(), 1.0f), "back: crouched");
    // between the two edges: still crouched, following the head
    Check(Near(Hold(t, 0.15f, 0.5f, p), 0.15f) && t.Crouched(), "back: 15 cm (inside the hysteresis): still a crouch");
    Check(Near(Hold(t, 0.11f, 0.5f, p), 0.11f) && t.Crouched(), "back: 11 cm: still a crouch");
    // within half the threshold: standing, eased out
    float d = t.Step(true, 0.09f, kTick, p);
    Check(!t.Crouched(), "back: 9 cm (under half of 20): standing again");
    Check(d > 0.0f && d < 0.09f, "back: eased out, no snap up");
    Check(Hold(t, 0.09f, kBlendS, p) == 0.0f && Near(t.Weight(), 0.0f), "back: standing after kBlendS");
    // the way up does not re-enter at the threshold's half
    Check(Hold(t, 0.15f, 0.5f, p) == 0.0f, "back: 15 cm from standing: not a crouch (needs 20 again)");
    // straight up from deep down: out at once, eased
    Hold(t, 0.60f, 0.5f, p);
    d = t.Step(true, 0.0f, kTick, p);
    Check(!t.Crouched() && d == 0.0f, "back: up to standing in one step: the drop follows the head (0)");
    Check(Hold(t, 0.0f, 0.3f, p) == 0.0f, "back: and stays 0");
    // flicker at the threshold does not toggle
    Tracker f;
    int changes = 0;
    bool was = false;
    for (int i = 0; i < 200; ++i) {
        f.Step(true, (i & 1) ? 0.205f : 0.195f, kTick, p);
        if (f.Crouched() != was) { ++changes; was = f.Crouched(); }
    }
    Check(changes == 1, "back: noise about the threshold: one change, not one a step");
}

void SeatedCases() {
    // Recentred seated: the recentre's height is the seated head's, so the drop is how far the head goes below that.
    Params p;
    p.floorM = 1.62f;
    Tracker t;
    Check(Hold(t, 0.0f, 2.0f, p) == 0.0f && !t.Crouched(), "seated: at the recentre: standing pose");
    Check(Hold(t, 0.08f, 1.0f, p) == 0.0f, "seated: leaning a little forward: nothing");
    Check(Hold(t, -0.40f, 1.0f, p) == 0.0f && !t.Crouched(), "seated: the head above the recentre (stood up): never a lift");
    // Standing recentre, then sitting down: the camera has gone 40 cm down too, so the body goes with it.
    Tracker s;
    Check(Near(Hold(s, 0.40f, 1.0f, p), 0.40f), "seated: sat down after a standing recentre: the body follows the camera down");
    // ... and stood up past the recentre in one step while still eased in: never lifted above standing
    const float up = s.Step(true, -0.30f, kTick, p);
    Check(!s.Crouched() && up >= 0.0f, "seated: up above the recentre from a crouch: no drop below 0");
}

void LostCases() {
    Params p;
    Tracker t;
    Hold(t, 0.40f, 0.5f, p);
    // lost for less than kLostHoldS: held
    Check(Near(Hold(t, 0.0f, kLostHoldS - 0.05f, p, false), 0.40f) && t.Crouched(), "lost: held a moment");
    // NaN is lost too
    Check(Near(t.Step(true, std::numeric_limits<float>::quiet_NaN(), kTick, p), 0.40f), "lost: NaN is not a height");
    // longer: stands up, eased, from where it was
    Hold(t, 0.0f, 0.1f, p, false);
    Check(!t.Crouched(), "lost: longer than kLostHoldS: standing");
    Check(Hold(t, 0.0f, kBlendS + 0.02f, p, false) == 0.0f, "lost: stood up after kBlendS");
    // back: from the head as it is
    Check(Near(Hold(t, 0.35f, 0.5f, p), 0.35f), "lost: tracking back, crouched again from the head");
    // a long gap counts as kMaxStepS: never a jump in one step
    Tracker g;
    const float d = g.Step(true, 0.50f, 5.0f, p);
    Check(d > 0.0f && d <= 0.50f * (kMaxStepS / kBlendS) + 1e-5f, "lost: a 5 s gap is one kMaxStepS of easing");
    Check(g.Step(true, 0.50f, -1.0f, p) == d && g.Step(true, 0.50f, std::numeric_limits<float>::quiet_NaN(), p) == d,
          "lost: a negative or NaN dt does not move it");
    // off: at once
    g.Reset();
    Check(!g.Crouched() && g.Weight() == 0.0f && g.Step(true, 0.0f, kTick, p) == 0.0f, "lost: Reset stands at once");
}

void LegCases() {
    const V ahead{0, 0, 1};
    // A straight standing leg, thigh 2, shin 2, the hip 3.9 over the foot.
    const V hip{1, 4, 0}, foot{1, 0.1f, 0}, knee{1, 2.05f, 0.02f};
    // room: the hip can come down until the leg is folded shut
    const float room = LegRoom(hip, foot, 2.0f, 2.0f);
    Check(Near(room, 3.9f - FoldedReach(2.0f, 2.0f), 1e-4f), "legs: room = height over the foot less the folded leg");
    Check(LegRoom(hip, foot, 0.0f, 2.0f) == 0.0f && LegRoom({0, NAN, 0}, foot, 2, 2) == 0.0f, "legs: a bad leg has no room");
    // a foot far out to the side: the hip may come down to its own height over it (less than the folded reach across)
    Check(Near(LegRoom({0, 2, 0}, {1.5f, 0, 0}, 2.0f, 1.0f), 2.0f - std::sqrt(std::max(0.0f, FoldedReach(2, 1) * FoldedReach(2, 1) - 2.25f))),
          "legs: a stride wide apart");
    // the pole: the animated knee ahead is kept; behind, or on the line, it is straight ahead
    const V pole = KneePole(hip, knee, foot, ahead);
    Check(pole.z > 0.0f && Near(pole.x, 0.0f) && Near(Dot(pole, foot - hip), 0.0f, 1e-4f), "legs: the animation's knee, across the leg");
    Check(Near(KneePole(hip, {1, 2.05f, -0.3f}, foot, ahead), ahead), "legs: a knee bent back is not taken - knees bend forward");
    Check(Near(KneePole(hip, {1, 2.05f, 0}, foot, ahead), ahead), "legs: a straight leg bends ahead");
    // crouches of every depth: the foot kept, the lengths kept, the knee ahead of the hip-foot line
    for (float down : {0.2f, 0.8f, 1.5f, 2.5f, 3.5f}) {
        V f = foot, k{};
        const V h{hip.x, hip.y - down, hip.z};
        Check(Knee(h, 2.0f, 2.0f, f, KneePole(hip, knee, foot, ahead), k), "legs: a solution");
        Check(Near(f, foot), "legs: the foot stays on the floor");
        Check(Near(Length(k - h), 2.0f, 1e-3f) && Near(Length(f - k), 2.0f, 1e-3f), "legs: thigh and shin keep their lengths");
        Check(k.z > 0.0f, "legs: the knee comes forward");
    }
    // unequal bones (a shin longer than the thigh), and a deeper drop than the leg allows: pulled to the folded reach
    {
        V f = foot, k{};
        const V h{hip.x, foot.y + 0.1f, hip.z};
        Check(Knee(h, 1.8f, 2.2f, f, ahead, k), "legs: past the folded leg - still a solution");
        Check(Near(Length(f - h), FoldedReach(1.8f, 2.2f), 1e-3f), "legs: the foot pulled to the folded reach, not into the hip");
    }
    // out of reach (the hip lifted): the foot comes up, the leg straight
    {
        V f = foot, k{};
        Check(Knee({1, 9, 0}, 2.0f, 2.0f, f, ahead, k) && f.y > 4.9f && f.y < 5.01f, "legs: out of reach - the foot pulled up");
    }
    // the pole along the leg: still a finite knee
    {
        V f = foot, k{};
        Check(Knee(hip, 2.0f, 2.0f, f, {0, -1, 0}, k) && Finite(k), "legs: a pole along the leg");
    }
    // bad input
    {
        V f = foot, k{};
        Check(!Knee(hip, 0.0f, 2.0f, f, ahead, k), "legs: no thigh, no leg");
        Check(!Knee(hip, 2.0f, 2.0f, f = {0, NAN, 0}, ahead, k), "legs: NaN foot");
        Check(!Knee({NAN, 0, 0}, 2.0f, 2.0f, f = foot, ahead, k), "legs: NaN hip");
    }
}

}  // namespace

int main() {
    DetectCases();
    BackCases();
    SeatedCases();
    LostCases();
    LegCases();
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
