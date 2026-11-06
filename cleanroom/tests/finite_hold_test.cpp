// Nothing non-finite to FlatVR (vr/finite_hold.h): a pose with a NaN or an infinity in it is replaced by the last good one, or by a
// neutral one when there never was a good one - what FlatVR places stands still. No game, no headset:
//
//   finite_hold_test      every case; exit code 0 = all passed
//
// good      finite sets go through as they are, zeros too (a zero pose is a pose), and become the last good
// nan       a NaN anywhere in the set: the whole last good set comes back, the output is finite
// inf       +inf / -inf: the same
// neutral   no good set yet: the neutral one given, else zeros - finite either way
// flicker   NaN, good, NaN, good ...: the output never leaves the good values (the spin: a NaN read as 0 jumped the camera)
// log       Due: the first miss at once, then at most once every period, and only with misses since; TakeCount counts them
// or        Or: one value, its fallback when not finite

#include "finite_hold.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {

int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
bool Same(const float* a, const float* b, int n) {
    for (int i = 0; i < n; ++i)
        if (a[i] != b[i]) return false;
    return true;
}

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

void Good() {
    finitehold::Hold<4> h;
    float v[4] = {10.0f, -2.5f, 0.0f, 1.0f};
    const float want[4] = {10.0f, -2.5f, 0.0f, 1.0f};
    Check(h.Take(v), "good: a finite set is taken");
    Check(Same(v, want, 4), "good: a finite set goes through unchanged");
    Check(h.HadGood() && Same(h.good, want, 4), "good: it is the last good");
    float z[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float zeros[4] = {};
    Check(h.Take(z) && Same(z, zeros, 4), "good: zeros are a pose, not a miss");
    Check(h.misses == 0, "good: no misses counted");
}

void NanAndInf() {
    finitehold::Hold<3> h;
    float a[3] = {45.0f, 1.6f, -10.0f};
    const float good[3] = {45.0f, 1.6f, -10.0f};
    h.Take(a);
    const float bad[][3] = {{kNan, 1.6f, -10.0f}, {45.0f, kNan, -10.0f}, {45.0f, 1.6f, kNan}, {kInf, 0.0f, 0.0f}, {0.0f, -kInf, 0.0f},
                            {kNan, kInf, -kInf}};
    for (const auto& b : bad) {
        float v[3] = {b[0], b[1], b[2]};
        Check(!h.Take(v), "nan/inf: a set with a non-finite value is a miss");
        Check(finitehold::Finite(v, 3), "nan/inf: the output is finite");
        Check(Same(v, good, 3), "nan/inf: the output is the last good set");
    }
    Check(h.misses == 6, "nan/inf: every miss counted");
    Check(Same(h.good, good, 3), "nan/inf: a miss does not change the last good");
}

void Neutral() {
    finitehold::Hold<4> h;
    float v[4] = {kNan, kNan, kNan, kNan};
    const float onScreen[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    Check(!h.Take(v, onScreen), "neutral: a miss before any good set");
    Check(Same(v, onScreen, 4), "neutral: the neutral set given comes out");
    Check(!h.HadGood(), "neutral: still no good set");
    float w[4] = {kInf, 1.0f, 2.0f, 3.0f};
    const float zeros[4] = {};
    h.Take(w);
    Check(Same(w, zeros, 4), "neutral: none given - zeros, finite");
}

void Flicker() {
    finitehold::Hold<2> h;   // a yaw and a pitch
    bool stood = true;
    for (int i = 0; i < 200; ++i) {
        const float yaw = 30.0f + 0.01f * (float)i;   // a slow real turn
        float v[2] = {(i % 2) ? kNan : yaw, -5.0f};
        h.Take(v);
        if (!finitehold::Finite(v, 2)) stood = false;
        // a miss gives the sample before, a good one itself: never 0 (the jump to straight ahead), never far from the real turn
        if (std::fabs(v[0] - yaw) > 0.011f || v[1] != -5.0f) stood = false;
    }
    Check(stood, "flicker: NaN between good heads never leaves the good values");
}

void LogPacing() {
    finitehold::Hold<1> h;
    Check(!h.Due(0), "log: nothing due without a miss");
    float v = kNan;
    h.Take(&v);
    Check(h.Due(1000), "log: the first miss is told at once");
    Check(h.TakeCount() == 1, "log: one miss counted");
    for (int i = 0; i < 500; ++i) { float n = kNan; h.Take(&n); }   // a frame each, for a few seconds
    Check(!h.Due(1000 + 9999), "log: not again within the period");
    Check(h.Due(1000 + 10000), "log: again once the period is gone");
    Check(h.TakeCount() == 500, "log: the line counts the misses since the last");
    Check(!h.Due(1000 + 30000), "log: nothing due with no misses since, however long");
    float g = 1.0f;
    h.Take(&g);
    Check(!h.Due(1000 + 40000), "log: a good set is not a miss");
}

void OrOne() {
    Check(finitehold::Or(2.0f, 0.0f) == 2.0f, "or: a finite value as it is");
    Check(finitehold::Or(kNan, 0.0f) == 0.0f, "or: NaN -> the fallback");
    Check(finitehold::Or(-kInf, 1.5f) == 1.5f, "or: -inf -> the fallback");
}

}  // namespace

int main() {
    Good();
    NanAndInf();
    Neutral();
    Flicker();
    LogPacing();
    OrOne();
    std::printf("finite_hold_test: %d of %d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
