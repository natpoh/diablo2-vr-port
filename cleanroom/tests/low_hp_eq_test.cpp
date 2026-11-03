// The vignettes' equalizer ([hud] low_hp_cover_eq / low_mp_cover_eq, vr/low_hp_eq.h) without the game or a headset:
//
//   - the points: highest first, every 5% from 60 down to 5, one key each; the life's defaults the size's own (40) at its default
//     threshold, rising toward no life;
//   - the cover at a life: each point's own value on it, a straight line between two, held above the highest and under the lowest,
//     nonsense put right; the strength: full from kFullAt, fainter under it, nothing at 0;
//   - the Heartbeat with it off is the one of before, step for step, and the effect's shape the radius's; on, the shape's reach is the
//     cover at the life, a small cover fainter, 0 nothing at all, and the beat (its rate, its moments for the hands) untouched.
//
//   low_hp_eq_test
//
// What it cannot say: how the cover looks in the lenses.

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "low_hp.h"

using namespace lowhp;

namespace {

int g_failed = 0, g_checks = 0;
void Expect(bool ok, const char* fmt, ...) {
    ++g_checks;
    if (ok) return;
    ++g_failed;
    char b[300];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    printf("FAIL: %s\n", b);
}
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

void TestBands() {
    const Config d;
    Expect(!d.coverEq.on, "the equalizer is off by default");
    for (int k = 0; k < eq::kBands; ++k) {
        Expect(eq::kLifePct[k] == 60 - 5 * k, "point %d is %d%% of the life", k, eq::kLifePct[k]);
        char want[32], got[32] = {};
        snprintf(want, sizeof want, "low_hp_cover_eq_%d", eq::kLifePct[k]);
        for (int i = 0; kCoverKeys[k][i] && i < 31; ++i) got[i] = (char)kCoverKeys[k][i];
        Expect(strcmp(want, got) == 0, "point %d's key is %s (got %s)", k, want, got);
        Expect(d.coverEq.cover[k] == Config::kCoverDefault[k], "point %d starts at its default", k);
        Expect(k == 0 || Config::kCoverDefault[k] >= Config::kCoverDefault[k - 1], "the defaults rise toward no life (%d)", k);
    }
    // at the default threshold (30%) the default cover is the default size: ticking it changes nothing there
    Expect(Near(eq::CoverAt(d.coverEq, d.threshold), d.radius, 1e-6f), "at the default threshold: the size's default (%.2f)", eq::CoverAt(d.coverEq, d.threshold));
}

void TestCover() {
    eq::Bands b;
    for (int k = 0; k < eq::kBands; ++k) b.cover[k] = 8.0f * (float)k;   // 0 at 60% ... 88 at 5%
    for (int k = 0; k < eq::kBands; ++k) Expect(Near(eq::CoverAt(b, eq::kLifePct[k] * 0.01f), 0.08f * (float)k), "on the point %d%% its own value", eq::kLifePct[k]);
    Expect(Near(eq::CoverAt(b, 0.575f), 0.04f), "half way between 60%% and 55%%: half way between their covers");
    Expect(Near(eq::CoverAt(b, 0.06f), 0.864f), "6%%, four fifths of the way from 10%% to 5%% (%.3f)", eq::CoverAt(b, 0.06f));
    Expect(Near(eq::CoverAt(b, 0.9f), 0.0f) && Near(eq::CoverAt(b, 1.0f), 0.0f), "above 60%%: the 60%% point's");
    Expect(Near(eq::CoverAt(b, 0.01f), 0.88f) && Near(eq::CoverAt(b, 0.0f), 0.88f) && Near(eq::CoverAt(b, NAN), 0.88f), "under 5%% (or no number): the 5%% point's");
    eq::Bands n;
    n.cover[0] = NAN;
    n.cover[1] = -30.0f;
    n.cover[2] = 500.0f;
    const eq::Bands s = eq::Sanitized(n, Config::kCoverDefault);
    Expect(s.cover[0] == Config::kCoverDefault[0] && s.cover[1] == 0.0f && s.cover[2] == 100.0f, "not a number: the default; out of range: the end");
    Config c;
    c.coverEq.cover[3] = 999.0f;
    Expect(Sanitized(c).coverEq.cover[3] == 100.0f, "the config's Sanitized puts the points right too");
    // the strength
    Expect(eq::Strength(0.0f) == 0.0f && eq::Strength(-1.0f) == 0.0f && eq::Strength(NAN) == 0.0f, "no cover: no colour");
    Expect(Near(eq::Strength(eq::kFullAt * 0.5f), 0.5f) && eq::Strength(eq::kFullAt) == 1.0f && eq::Strength(1.0f) == 1.0f, "fainter under kFullAt, full from it");
}

void TestShape() {
    Config c;
    c.radius = 0.40f;
    // no cover: the radius, as before (and a cover of -1 the same)
    const Shape s0 = ShapeOf(c), s1 = ShapeOf(c, 0.0f, 0.0f, -1.0f);
    Expect(Near(s0.inner, 0.60f) && s0.inner == s1.inner && s0.outer == s1.outer, "without a cover: the radius's shape");
    // a cover: its reach, past the radius slider's own range
    Expect(Near(ShapeOf(c, 0.0f, 0.0f, 0.75f).inner, 0.25f), "a cover of 75%%: the clear middle a quarter");
    Expect(Near(ShapeOf(c, 0.0f, 0.0f, 1.0f).inner, 0.0f) && Mask(0.6f, 0.5f, ShapeOf(c, 0.0f, 0.0f, 1.0f)) > 0.0f, "100%%: no clear middle left");
    Expect(ShapeOf(c, 0.0f, 0.0f, 0.0f).inner < 1.0f, "0 is still a shape (the strength makes it invisible)");
    const Uniforms u = MakeUniforms(0.5f, c, 0.0f, 0.0f, 0.75f);
    Expect(Near(u.shape[0], 0.25f), "the effect is handed the cover's reach");
}

Heartbeat::In Seen(float frac, float dt) {
    Heartbeat::In in;
    in.inView = true;
    in.lifeKnown = true;
    in.frac = frac;
    in.dt = dt;
    return in;
}

void TestHeartbeat() {
    // off: the beat and the opacity step for step as before, whatever the points; no cover handed on
    Config plain;
    plain.on = true;
    Config moved = plain;
    for (float& v : moved.coverEq.cover) v = 3.0f;
    for (int g = 0; g < 2; ++g) {
        plain.gentle = moved.gentle = g == 1;
        Heartbeat a, b;
        bool same = true, noCover = true;
        for (int i = 0; i < 4000; ++i) {
            const Heartbeat::In in = Seen(0.29f - 0.27f * (float)(i % 1000) / 1000.0f, (i % 37 == 0) ? 0.05f : 1.0f / 90.0f);
            const Heartbeat::Out x = a.Step(plain, in), y = b.Step(moved, in);
            same = same && x.alpha == y.alpha && x.bpm == y.bpm && x.phase == y.phase && x.beat.lub == y.beat.lub && x.beat.dub == y.beat.dub;
            noCover = noCover && x.cover == -1.0f && y.cover == -1.0f;
        }
        Expect(same && noCover, "the equalizer off%s: the beat step for step as before", g ? " (gentle)" : "");
    }
    // on: the cover at the life; the opacity times its strength; the beat itself as without it
    Config on = plain;
    on.gentle = false;
    on.threshold = 0.60f;
    on.coverEq.on = true;
    for (int k = 0; k < eq::kBands; ++k) on.coverEq.cover[k] = 50.0f;
    on.coverEq.cover[6] = 80.0f;   // 30%
    on.coverEq.cover[9] = 0.0f;    // 15%
    on.coverEq.cover[8] = 15.0f;   // 20%: half strength
    Config ref = on;
    ref.coverEq.on = false;
    struct Case { float frac, cover, strength; };
    const Case cases[] = {{0.30f, 0.80f, 1.0f}, {0.275f, 0.65f, 1.0f}, {0.20f, 0.15f, 0.5f}, {0.15f, 0.0f, 0.0f}, {0.40f, 0.50f, 1.0f}};
    for (const Case& k : cases) {
        Heartbeat a, b;
        bool same = true, cover = true, alpha = true;
        int lubs = 0, lubsRef = 0;
        for (int i = 0; i < 90 * 30; ++i) {
            const Heartbeat::In in = Seen(k.frac, 1.0f / 90.0f);
            const Heartbeat::Out x = a.Step(on, in), y = b.Step(ref, in);
            same = same && x.bpm == y.bpm && x.phase == y.phase && x.beat.lub == y.beat.lub && x.beat.dub == y.beat.dub && x.amount == y.amount;
            cover = cover && Near(x.cover, k.cover, 1e-5f);
            alpha = alpha && Near(x.alpha, y.alpha * k.strength, 1e-6f);
            lubs += x.beat.lub;
            lubsRef += y.beat.lub;
        }
        Expect(same && lubs == lubsRef && lubs > 0, "at %.1f%% life: the beat as without the equalizer (%d beats)", k.frac * 100.0f, lubs);
        Expect(cover, "at %.1f%% life: the cover is %.3f", k.frac * 100.0f, k.cover);
        Expect(alpha, "at %.1f%% life: the opacity times the cover's strength (%.2f)", k.frac * 100.0f, k.strength);
    }
    // above the threshold: nothing, whatever the points
    Config t = on;
    t.threshold = 0.20f;
    Heartbeat h;
    float most = 0.0f;
    for (int i = 0; i < 300; ++i) most = std::fmax(most, h.Step(t, Seen(0.30f, 1.0f / 90.0f)).alpha);
    Expect(most == 0.0f, "above the threshold: nothing");
}

}  // namespace

int main() {
    TestBands();
    TestCover();
    TestShape();
    TestHeartbeat();
    printf("%s: %d checks, %d failed\n", g_failed ? "FAILED" : "ok", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
