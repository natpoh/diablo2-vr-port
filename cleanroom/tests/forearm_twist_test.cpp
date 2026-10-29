// Synthetic checks of vr/forearm_twist.h: the hand's roll about the forearm read back, a bend of the wrist not counted, mirrored
// bones, the twist kept continuous past half a turn, the share along the forearm. No game, no headset.
// Build: the `forearm_twist_test` target; the exit code is 0 when everything passes.
#include "../../vr/forearm_twist.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace forearmtwist;

namespace {
void Check(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}

void Unit3(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int i = 0; i < 3; ++i) v[i] /= l;
}

// A random rotation (rows), optionally mirrored (one row negated) and scaled like a bone (row lengths 0.8 .. 1.2).
void RandomBone(std::mt19937& rng, bool mirrored, float out[9]) {
    std::uniform_real_distribution<float> u(-1.0f, 1.0f), deg(-180.0f, 180.0f), len(0.8f, 1.2f);
    float k1[3] = {u(rng), u(rng), u(rng)}, k2[3] = {u(rng), u(rng), u(rng)};
    Unit3(k1); Unit3(k2);
    float a[9], b[9];
    Turn(k1, deg(rng), a);
    Turn(k2, deg(rng), b);
    Mul3(a, b, out);
    if (mirrored) for (int c = 0; c < 3; ++c) out[c] = -out[c];
    for (int r = 0; r < 3; ++r) { const float l = len(rng); for (int c = 0; c < 3; ++c) out[r * 3 + c] *= l; }
}

float AngleDiff(float a, float b) {
    float d = a - b;
    while (d > 180.0f) d -= 360.0f;
    while (d <= -180.0f) d += 360.0f;
    return std::fabs(d);
}
}  // namespace

int main() {
    std::mt19937 rng(20261015);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f), twist(-170.0f, 170.0f), bend(-70.0f, 70.0f);

    // 1. A pure roll about the forearm: read back exactly, proper and mirrored bones alike.
    for (int i = 0; i < 400; ++i) {
        float ref[9], axis[3] = {u(rng), u(rng), u(rng)}, t[9], now[9];
        RandomBone(rng, (i & 1) != 0, ref);
        Unit3(axis);
        const float d = twist(rng);
        Turn(axis, d, t);
        Mul3(ref, t, now);
        Check(AngleDiff(TwistDeg(ref, now, axis), d) < 0.05f, "a pure roll about the forearm reads back as itself");
    }

    // 2. A roll, then the wrist bent (a turn about an axis square to the forearm): still the roll alone.
    for (int i = 0; i < 400; ++i) {
        float ref[9], axis[3] = {u(rng), u(rng), u(rng)}, r[3] = {u(rng), u(rng), u(rng)};
        RandomBone(rng, (i & 1) != 0, ref);
        Unit3(axis);
        const float dot = r[0] * axis[0] + r[1] * axis[1] + r[2] * axis[2];
        float perp[3] = {r[0] - axis[0] * dot, r[1] - axis[1] * dot, r[2] - axis[2] * dot};
        Unit3(perp);
        const float d = twist(rng);
        float t[9], s[9], ts[9], now[9];
        Turn(axis, d, t);
        Turn(perp, bend(rng), s);
        Mul3(t, s, ts);
        Mul3(ref, ts, now);
        Check(AngleDiff(TwistDeg(ref, now, axis), d) < 0.05f, "a bent wrist does not count as a roll");
    }

    // 3. The same turn either way round reads with opposite signs; no turn reads 0.
    {
        float ref[9], axis[3] = {0.0f, 1.0f, 0.0f}, t[9], now[9];
        RandomBone(rng, false, ref);
        Turn(axis, 30.0f, t); Mul3(ref, t, now);
        Check(std::fabs(TwistDeg(ref, now, axis) - 30.0f) < 0.05f, "+30 reads +30");
        Turn(axis, -30.0f, t); Mul3(ref, t, now);
        Check(std::fabs(TwistDeg(ref, now, axis) + 30.0f) < 0.05f, "-30 reads -30");
        Check(std::fabs(TwistDeg(ref, ref, axis)) < 0.01f, "no turn reads 0");
        const float zero[9] = {};
        Check(std::isnan(TwistDeg(zero, ref, axis)), "a matrix of no size reads nothing");
    }

    // 4. Kept continuous past half a turn, and held to the limit.
    Check(std::fabs(Unwrap(170.0f, -175.0f) - 185.0f) < 1e-3f, "a roll past 180 keeps going, no flip to -175");
    Check(std::fabs(Unwrap(-170.0f, 175.0f) + 185.0f) < 1e-3f, "and the other way");
    Check(std::fabs(Unwrap(10.0f, 20.0f) - 20.0f) < 1e-3f, "a small step is the step");
    Check(std::fabs(Unwrap(230.0f, -110.0f) - (-110.0f)) < 1e-3f, "past the limit (250) it starts afresh");
    Check(std::fabs(Unwrap(NAN, 42.0f) - 42.0f) < 1e-3f, "no last value: this one");
    Check(std::fabs(Unwrap(42.0f, NAN) - 42.0f) < 1e-3f, "nothing read: the last one stays");

    // 5. The share along the forearm.
    Check(std::fabs(Share(0.5f, 0.0f) - 0.5f) < 1e-6f, "the forearm bone takes the share");
    Check(std::fabs(Share(0.5f, 1.0f) - 1.0f) < 1e-6f, "the wrist takes it all");
    Check(std::fabs(Share(0.5f, 0.5f) - 0.75f) < 1e-6f, "halfway: in between");
    Check(std::fabs(Share(2.0f, 0.0f) - 1.0f) < 1e-6f && std::fabs(Share(-1.0f, -1.0f)) < 1e-6f, "held to 0..1");

    // 6. The forearm turned by its share leaves the hand the rest: twist(ref*T(share*d), now) = (1 - share) d.
    for (int i = 0; i < 200; ++i) {
        float ref[9], axis[3] = {u(rng), u(rng), u(rng)}, t[9], now[9], ts[9], refTurned[9];
        RandomBone(rng, (i & 1) != 0, ref);
        Unit3(axis);
        const float d = twist(rng), share = 0.6f;
        Turn(axis, d, t); Mul3(ref, t, now);
        Turn(axis, share * d, ts); Mul3(ref, ts, refTurned);
        Check(AngleDiff(TwistDeg(refTurned, now, axis), (1.0f - share) * d) < 0.05f, "the forearm's share is taken off the hand's");
    }

    std::printf("forearm_twist_test: all passed\n");
    return 0;
}
