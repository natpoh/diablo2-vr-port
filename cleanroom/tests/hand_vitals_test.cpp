// The hand vitals panel (vr/hand_vitals.h, vr/hand_vitals_paint.cpp) without the game or a headset:
//
//   - the hero's numbers: the game's stat values with their fractional bits, a maximum of 0 (mana), nonsense, the largest ints,
//     the fill levels and the text for edge values (0, the maximum, mana 0, maximum mana 0, huge numbers),
//   - the settings' clamps,
//   - the palm test: a hand turned toward the face / away / sideways, the cone with its hysteresis, the distance band, a hand hanging at
//     the side, behind the head, a fist, the left and the right hand mirrored, the flip switch,
//   - the reveal: the dwell, the fade in and out, the hysteresis at the edge of the cone, noise round it, the choice of the hand
//     ("either"), tracking lost while it is up, a long frame, and the off states (the setting off, not F4, a menu, a panel of the
//     plugin's, dead, unknown numbers) producing nothing,
//   - the panel's pose: over the hand, toward the head, upright, the height setting, the size, not locked to the head,
//   - the picture: drawn at several fill levels, checked for what it must be (clear corners, red / blue where full, dark where empty,
//     premultiplied, the opacity, the numbers inside the plate), and, given a folder, written as PNG files to look at.
//
//   hand_vitals_test [<folder for the pictures>]
//
// What it cannot say: how it looks in the lenses, how it feels to turn the wrist, whether a runtime's grip pose agrees with the axes the
// header assumes (+X out of the left palm, into the right one).

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "hand_vitals.h"
#include "hand_vitals_paint.h"

using namespace handvitals;
using hset::Q;
using hset::V3;

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
    printf("  FAIL: %s\n", b);
}
bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

constexpr float kPi = 3.14159265358979f;
constexpr float kDt = 1.0f / 90.0f;
float Rad(float deg) { return deg * kPi / 180.0f; }

// -- small maths for building poses ------------------------------------------------------------------------------------------------------------

V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Len(V3 a) { return std::sqrt(hset::Dot(a, a)); }
Q QMul(Q a, Q b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Q AxisAngle(V3 axis, float rad) {
    const V3 u = hset::Unit(axis);
    const float s = std::sin(0.5f * rad);
    return {u.x * s, u.y * s, u.z * s, std::cos(0.5f * rad)};
}
// The turn that takes unit vector a to unit vector b by the shortest way.
Q FromTo(V3 a, V3 b) {
    const float d = hset::Dot(a, b);
    if (d > 0.999999f) return {};
    if (d < -0.999999f) {
        V3 p = Cross(a, {1, 0, 0});
        if (Len(p) < 1e-3f) p = Cross(a, {0, 1, 0});
        return AxisAngle(p, kPi);
    }
    const V3 c = Cross(a, b);
    Q q{c.x, c.y, c.z, 1.0f + d};
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}
V3 Perp(V3 v) {
    V3 p = Cross(v, {0, 1, 0});
    if (Len(p) < 0.1f) p = Cross(v, {1, 0, 0});
    return hset::Unit(p);
}
Q Mirror(Q q) { return {q.x, -q.y, -q.z, q.w}; }   // the turn reflected across the plane x = 0
V3 Mirror(V3 v) { return {-v.x, v.y, v.z}; }

// The head at (0, 1.6, 0) looking down -z. A hand's pose is made so that its palm's normal (local +X for the left, -X for the right) points
// `angleDeg` off the direction to the head; a roll about the normal is added, which must not matter.
HeadPose MakeHead() {
    HeadPose h;
    h.valid = true;
    h.pos = {0.0f, 1.6f, 0.0f};
    h.rot = {0, 0, 0, 1};
    return h;
}
HandPose MakeHand(const HeadPose& head, int side, V3 pos, float angleDeg, float rollDeg = 0.0f, bool flip = false) {
    HandPose h;
    h.valid = true;
    h.pos = pos;
    const V3 dir = hset::Unit(hset::Sub(head.pos, pos));
    const float a = Rad(angleDeg);
    const V3 n = hset::Unit(hset::Add(hset::Mul(dir, std::cos(a)), hset::Mul(Perp(dir), std::sin(a))));
    const bool leftLike = (side == 0) != flip;
    h.rot = QMul(AxisAngle(n, Rad(rollDeg)), FromTo({leftLike ? 1.0f : -1.0f, 0.0f, 0.0f}, n));
    return h;
}
const V3 kFront{0.0f, 1.45f, -0.38f};   // a hand held in front of the face, a little low

Config On() {
    Config c;
    c.on = true;
    return c;
}

// ---- the hero's numbers -------------------------------------------------------------------------------------------------------------------------

void TestNumbers() {
    printf("numbers\n");
    const int32_t S = 256;
    Vitals v = FromStats(100 * S, 200 * S, 50 * S, 80 * S);
    Expect(v.known && !v.dead && Near(v.life, 100, 1e-4f) && Near(v.maxLife, 200, 1e-4f) && Near(v.mana, 50, 1e-4f) && Near(v.maxMana, 80, 1e-4f), "plain numbers");
    v = FromStats(100 * S + 128, 200 * S, 0, 80 * S);
    Expect(v.known && Near(v.life, 100.5f, 1e-4f), "the fractional bits are kept: 100.5");
    v = FromStats(0, 200 * S, 0, 80 * S);
    Expect(v.known && v.dead && v.life == 0, "life 0 is dead (and known)");
    v = FromStats(1, 200 * S, 0, 80 * S);
    Expect(v.known && !v.dead, "life 1/256 is not dead");
    Expect(!FromStats(5, 0, 0, 0).known, "maximum life 0: unknown");
    Expect(!FromStats(5, 255, 0, 0).known, "maximum life under one point: unknown");
    Expect(FromStats(S, S, 0, 0).known, "maximum life of exactly one point: known");
    Expect(!FromStats(-1, 200 * S, 0, 80 * S).known, "negative life: unknown");
    Expect(FromStats(400 * S, 200 * S, 0, 80 * S).known, "life at twice the maximum: known");
    Expect(!FromStats(400 * S + 1, 200 * S, 0, 80 * S).known, "life past twice the maximum: unknown");
    Expect(!FromStats(100 * S, 200 * S, -1, 80 * S).known, "negative mana: unknown");
    Expect(!FromStats(100 * S, 200 * S, 0, -5).known, "negative maximum mana: unknown");
    Expect(!FromStats(100 * S, 200 * S, 161 * S, 80 * S).known, "mana past twice the maximum: unknown");
    v = FromStats(100 * S, 200 * S, 0, 0);
    Expect(v.known && v.maxMana == 0 && v.mana == 0, "maximum mana 0 with mana 0 (a hero with no pool): known");
    Expect(!FromStats(100 * S, 200 * S, 1, 0).known, "mana above a maximum of 0: unknown");
    Expect(FromStats(1, 1000000 * S - 1, 0, 0).known, "a maximum of nearly a million points: known");
    Expect(!FromStats(1, 1000000 * S, 0, 0).known, "a maximum of a million points: unknown");
    Expect(!FromStats(std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max(), 0, 0).known, "the largest ints: unknown, no overflow");
    Expect(!FromStats(0, 200 * S, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max()).known, "the largest mana ints: unknown");
    Expect(!FromStats(std::numeric_limits<int32_t>::min(), 200 * S, 0, 0).known, "the smallest int life: unknown");

    Expect(Fresh(0.0) && Fresh(1500.0) && !Fresh(1500.1) && !Fresh(-1.0) && !Fresh(std::nan("")), "a read stays fresh for 1.5 s");

    Expect(Fraction(0, 100) == 0.0f && Fraction(100, 100) == 1.0f && Near(Fraction(25, 100), 0.25f, 1e-6f), "fractions 0, 1, 0.25");
    Expect(Fraction(200, 100) == 1.0f, "more than the maximum is full");
    Expect(Fraction(50, 0) == 0.0f && Fraction(0, 0) == 0.0f, "a maximum of 0 is empty, not a division by zero");
    Expect(Fraction(-5, 100) == 0.0f && Fraction(std::nanf(""), 100) == 0.0f && Fraction(5, std::nanf("")) == 0.0f, "negative / NaN are empty");
    Expect(Fraction(5, std::numeric_limits<float>::infinity()) == 0.0f, "an infinite maximum is empty");
    Expect(FillStep(0.0f) == 0 && FillStep(1.0f) == kFillSteps && FillStep(0.5f) == kFillSteps / 2 && FillStep(2.0f) == kFillSteps && FillStep(-1.0f) == 0 &&
               FillStep(std::nanf("")) == 0,
           "fill steps 0, full, half, clamped, NaN");
    Expect(FillStep(0.004f) == 1 && FillStep(0.0001f) == 0, "a sliver of life shows one step; the dust of a step does not");

    char b[32];
    FormatPoints(0, b, sizeof b);
    Expect(!strcmp(b, "0"), "0 -> '%s'", b);
    FormatPoints(999.9f, b, sizeof b);
    Expect(!strcmp(b, "999"), "999.9 -> '%s' (fractions dropped, like the game)", b);
    FormatPoints(9999.0f, b, sizeof b);
    Expect(!strcmp(b, "9999"), "9999 -> '%s'", b);
    FormatPoints(10000.0f, b, sizeof b);
    Expect(!strcmp(b, "10.0k"), "10000 -> '%s'", b);
    FormatPoints(12345.0f, b, sizeof b);
    Expect(!strcmp(b, "12.3k"), "12345 -> '%s'", b);
    FormatPoints(99999.0f, b, sizeof b);
    Expect(!strcmp(b, "99.9k"), "99999 -> '%s'", b);
    FormatPoints(100000.0f, b, sizeof b);
    Expect(!strcmp(b, "100k"), "100000 -> '%s'", b);
    FormatPoints(999999.0f, b, sizeof b);
    Expect(!strcmp(b, "999k"), "999999 -> '%s'", b);
    FormatPoints(1000000.0f, b, sizeof b);
    Expect(!strcmp(b, "1.0M"), "1000000 -> '%s'", b);
    FormatPoints(1999999.0f, b, sizeof b);
    Expect(!strcmp(b, "1.9M"), "1999999 -> '%s'", b);
    FormatPoints(-5.0f, b, sizeof b);
    Expect(!strcmp(b, "0"), "negative -> '%s'", b);
    FormatPoints(std::nanf(""), b, sizeof b);
    Expect(!strcmp(b, "0"), "NaN -> '%s'", b);
    FormatPoints(1e12f, b, sizeof b);
    Expect(!strcmp(b, "999.9M"), "1e12 -> '%s' (capped)", b);
    FormatPoints(5.0f, b, 1);
    Expect(b[0] == 0, "a one-byte buffer holds an empty string");
    char tiny[4] = {'x', 'x', 'x', 'x'};
    FormatPoints(123456.0f, tiny, sizeof tiny);
    Expect(tiny[3] == 0 || tiny[3] == 'x', "a short buffer is not overrun");
    FormatPoints(5.0f, nullptr, 0);   // (no write, no crash)
    FormatPair(123.9f, 456.2f, b, sizeof b);
    Expect(!strcmp(b, "123 / 456"), "pair -> '%s'", b);
    char big[24];
    FormatPair(999999999.0f, 999999999.0f, big, sizeof big);
    Expect(!strcmp(big, "999.9M / 999.9M") && strlen(big) < sizeof big - 1, "the longest pair '%s' fits a Look's text", big);

    // The looks
    Config c = On();
    v = FromStats(1000 * S, 1000 * S, 300 * S, 300 * S);
    Look l = MakeLook(v, c);
    Expect(l.lifeFill == kFillSteps && l.manaFill == kFillSteps && !strcmp(l.lifeText, "1000 / 1000") && !strcmp(l.manaText, "300 / 300"), "full: '%s' '%s'", l.lifeText, l.manaText);
    v = FromStats(0, 1000 * S, 0, 300 * S);
    l = MakeLook(v, c);
    Expect(l.lifeFill == 0 && l.manaFill == 0 && !strcmp(l.lifeText, "0 / 1000") && !strcmp(l.manaText, "0 / 300"), "empty: '%s' '%s'", l.lifeText, l.manaText);
    v = FromStats(500 * S, 1000 * S, 0, 0);
    l = MakeLook(v, c);
    Expect(l.manaFill == 0 && !strcmp(l.manaText, "0 / 0"), "mana with no pool: empty globe, '%s'", l.manaText);
    v = FromStats(999999 * S, 999999 * S, 999999 * S, 999999 * S);
    l = MakeLook(v, c);
    Expect(!strcmp(l.lifeText, "999k / 999k") && l.lifeFill == kFillSteps, "huge: '%s'", l.lifeText);
    v = FromStats(500 * S, 1000 * S, 50 * S, 100 * S);
    const Look a = MakeLook(v, c);
    v = FromStats(500 * S + 70, 1000 * S, 50 * S + 70, 100 * S);   // 0.27 of a point more: the same picture
    const Look b2 = MakeLook(v, c);
    Expect(a == b2, "a quarter of a point more life and mana is the same look (no repaint)");
    v = FromStats(560 * S, 1000 * S, 50 * S, 100 * S);
    Expect(MakeLook(v, c) != a, "60 points more life is a new look");
    Config co = c;
    co.opacity = 0.5f;
    Expect(MakeLook(v, co).opacity == 10 && MakeLook(v, c).opacity == 20, "the opacity is in the look");
    co.opacity = 0.0f;   // (unsanitised)
    Expect(MakeLook(v, co).opacity == 6, "an opacity under the least is the least (0.3 -> 6)");

    // The alert (the pulse)
    Config p = On();
    p.pulse = true;
    p.lowFrac = 0.30f;
    Expect(!Alert(FromStats(5 * S, 100 * S, 0, 0), On()), "pulse off: no alert at 5% life");
    Expect(Alert(FromStats(29 * S, 100 * S, 0, 0), p), "pulse on, 29% < 30%: alert");
    Expect(!Alert(FromStats(30 * S, 100 * S, 0, 0), p), "30% is not under 30%");
    Expect(!Alert(FromStats(0, 100 * S, 0, 0), p), "dead: no alert");
    Expect(!Alert(Vitals{}, p), "unknown: no alert");
    Expect(Breath(false, 0.3) == 1.0f && Breath(false, 123.4) == 1.0f, "no alert: the panel does not breathe");
    float lo = 2, hi = -1;
    for (double t = 0; t < 2.0; t += 0.01) {
        const float k = Breath(true, t);
        lo = std::min(lo, k);
        hi = std::max(hi, k);
    }
    Expect(lo >= 0.799f && hi <= 1.0001f && lo < 0.81f && hi > 0.99f, "the breath goes between 80%% and 100%% (%.3f .. %.3f)", lo, hi);
    Expect(Near(Breath(true, 0.25), 1.0f, 1e-4f) && Near(Breath(true, 0.75), 0.8f, 1e-4f) && Near(Breath(true, 1.25), Breath(true, 0.25), 1e-4f), "once a second");
    Expect(Breath(true, std::nan("")) == 1.0f, "NaN time: no breath");
}

void TestSettings() {
    printf("settings\n");
    Config c;
    c.sizeCm = 1.0f;
    c.heightCm = -50.0f;
    c.opacity = 0.0f;
    c.coneDeg = 5.0f;
    c.hand = 7;
    c.lowFrac = 0.0f;
    c = Sanitized(c);
    Expect(c.sizeCm == 5.0f && c.heightCm == -5.0f && c.opacity == 0.3f && c.coneDeg == 20.0f && c.hand == 0 && c.lowFrac == 0.10f, "the low ends clamp");
    c.sizeCm = 100.0f;
    c.heightCm = 50.0f;
    c.opacity = 5.0f;
    c.coneDeg = 90.0f;
    c.hand = 2;
    c.lowFrac = 0.9f;
    c = Sanitized(c);
    Expect(c.sizeCm == 14.0f && c.heightCm == 15.0f && c.opacity == 1.0f && c.coneDeg == 60.0f && c.hand == 2 && c.lowFrac == 0.60f, "the high ends clamp");
    c.sizeCm = std::nanf("");
    c.heightCm = std::nanf("");
    c.opacity = std::nanf("");
    c.coneDeg = std::nanf("");
    c.lowFrac = std::nanf("");
    c.hand = -1;
    c = Sanitized(c);
    Expect(c.sizeCm == 8.0f && c.heightCm == 6.0f && c.opacity == 1.0f && c.coneDeg == 35.0f && c.lowFrac == 0.30f && c.hand == 0, "NaN takes the defaults");
    const Config d;
    Expect(!d.on && d.hand == 0 && !d.pulse && !d.flip && d.sizeCm == 8.0f && d.coneDeg == 35.0f, "defaults: off, left hand, 8 cm, cone 35");
}

// ---- the palm ------------------------------------------------------------------------------------------------------------------------------------

void TestPalm() {
    printf("palm\n");
    const HeadPose head = MakeHead();
    const Config cfg = On();

    // Which way the palm faces: +X of the grip for the left hand, -X for the right
    Expect(Near(PalmNormal({0, 0, 0, 1}, 0).x, 1.0f, 1e-6f) && Near(PalmNormal({0, 0, 0, 1}, 1).x, -1.0f, 1e-6f), "palm normal: left +X, right -X");
    Expect(Near(PalmNormal({0, 0, 0, 1}, 0, true).x, -1.0f, 1e-6f) && Near(PalmNormal({0, 0, 0, 1}, 1, true).x, 1.0f, 1e-6f), "flip turns them round");

    for (int side = 0; side < 2; ++side) {
        const char* nm = side == 0 ? "left" : "right";
        auto at = [&](float angle, float roll = 0.0f) { return Measure(MakeHand(head, side, kFront, angle, roll), side, head, cfg); };
        Look3 m = at(0.0f);
        Expect(m.valid && m.enter && m.keep && Near(m.angleDeg, 0.0f, 0.1f), "%s: palm to the face: shown (angle %.2f)", nm, m.angleDeg);
        for (float roll : {0.0f, 45.0f, 90.0f, 180.0f, 270.0f}) {
            m = at(20.0f, roll);
            Expect(m.enter && Near(m.angleDeg, 20.0f, 0.2f), "%s: 20 degrees off, rolled %.0f: angle %.2f", nm, roll, m.angleDeg);
        }
        m = at(34.0f);
        Expect(m.enter && m.keep, "%s: 34 degrees: in", nm);
        m = at(36.0f);
        Expect(!m.enter && m.keep, "%s: 36 degrees: not far enough in to start, still kept", nm);
        m = at(49.0f);
        Expect(!m.enter && m.keep, "%s: 49 degrees: kept", nm);
        m = at(51.0f);
        Expect(!m.enter && !m.keep, "%s: 51 degrees: gone", nm);
        m = at(90.0f);
        Expect(!m.enter && !m.keep && Near(m.angleDeg, 90.0f, 0.3f), "%s: sideways (90): no", nm);
        m = at(180.0f);
        Expect(!m.enter && !m.keep && Near(m.angleDeg, 180.0f, 0.3f), "%s: the back of the hand to the face: no", nm);

        // distance (a hand straight ahead of the eyes)
        auto ahead = [&](float d) { return Measure(MakeHand(head, side, {0.0f, 1.6f, -d}, 0.0f), side, head, cfg); };
        m = ahead(0.10f);
        Expect(!m.enter && !m.keep, "%s: 10 cm: too near", nm);
        m = ahead(0.13f);
        Expect(!m.enter && m.keep, "%s: 13 cm: kept, not started", nm);
        m = ahead(0.16f);
        Expect(m.enter, "%s: 16 cm: in", nm);
        m = ahead(0.69f);
        Expect(m.enter, "%s: 69 cm: in", nm);
        m = ahead(0.72f);
        Expect(!m.enter && m.keep, "%s: 72 cm: kept, not started", nm);
        m = ahead(0.79f);
        Expect(m.keep, "%s: 79 cm: kept", nm);
        m = ahead(0.82f);
        Expect(!m.keep, "%s: 82 cm: gone", nm);

        // not hanging at the side, not behind the head, not off to the side of the gaze
        m = Measure(MakeHand(head, side, {0.25f, 0.95f, -0.1f}, 0.0f), side, head, cfg);
        Expect(!m.enter && !m.keep, "%s: a hand low at the side (65 cm under the eyes, 70 cm away): no", nm);
        m = Measure(MakeHand(head, side, {0.25f, 1.05f, -0.1f}, 0.0f), side, head, cfg);
        Expect(!m.enter && m.keep, "%s: 55 cm under the eyes: kept, not started", nm);
        m = Measure(MakeHand(head, side, {0.0f, 1.5f, 0.35f}, 0.0f), side, head, cfg);
        Expect(!m.enter && !m.keep && m.gazeDeg > 150.0f, "%s: behind the head: no (gaze %.0f)", nm, m.gazeDeg);
        m = Measure(MakeHand(head, side, {std::sin(Rad(80.0f)) * 0.4f, 1.6f, -std::cos(Rad(80.0f)) * 0.4f}, 0.0f), side, head, cfg);
        Expect(!m.enter && m.keep && Near(m.gazeDeg, 80.0f, 0.5f), "%s: 80 degrees off the gaze: kept, not started (gaze %.1f)", nm, m.gazeDeg);

        // a fist
        HandPose h = MakeHand(head, side, kFront, 0.0f);
        h.squeeze = 0.5f;
        m = Measure(h, side, head, cfg);
        Expect(m.enter && m.keep, "%s: half a squeeze: still a palm", nm);
        h.squeeze = 0.7f;
        m = Measure(h, side, head, cfg);
        Expect(!m.enter && m.keep, "%s: 0.7: kept, not started", nm);
        h.squeeze = 0.9f;
        m = Measure(h, side, head, cfg);
        Expect(!m.enter && !m.keep, "%s: a fist: no", nm);

        // not located
        h = MakeHand(head, side, kFront, 0.0f);
        h.valid = false;
        Expect(!Measure(h, side, head, cfg).valid && !Measure(h, side, head, cfg).enter, "%s: a hand that is not located: nothing", nm);
        h.valid = true;
        h.pos.x = std::nanf("");
        Expect(!Measure(h, side, head, cfg).valid, "%s: a NaN position: nothing", nm);
        h.pos = kFront;
        h.rot = {0, 0, 0, std::nanf("")};
        Expect(!Measure(h, side, head, cfg).enter, "%s: a NaN turn: nothing", nm);
        HeadPose nohead = head;
        nohead.valid = false;
        Expect(!Measure(MakeHand(head, side, kFront, 0.0f), side, nohead, cfg).valid, "%s: a head that is not located: nothing", nm);
        Expect(!Measure(MakeHand(head, side, head.pos, 0.0f), side, head, cfg).valid, "%s: a hand on the head: nothing (no direction)", nm);

        // the cone setting
        Config narrow = cfg;
        narrow.coneDeg = 20.0f;
        Expect(!Measure(MakeHand(head, side, kFront, 25.0f), side, head, narrow).enter && Measure(MakeHand(head, side, kFront, 34.0f), side, head, narrow).keep &&
                   !Measure(MakeHand(head, side, kFront, 36.0f), side, head, narrow).keep,
               "%s: cone 20: starts under 20, kept to 35", nm);
        Config wide = cfg;
        wide.coneDeg = 60.0f;
        Expect(Measure(MakeHand(head, side, kFront, 59.0f), side, head, wide).enter && Measure(MakeHand(head, side, kFront, 74.0f), side, head, wide).keep &&
                   !Measure(MakeHand(head, side, kFront, 76.0f), side, head, wide).keep,
               "%s: cone 60: starts under 60, kept to 75", nm);

        // flip: the back of the hand
        Config flip = cfg;
        flip.flip = true;
        Expect(Measure(MakeHand(head, side, kFront, 0.0f, 0.0f, true), side, head, flip).enter, "%s: flipped: the back of the hand shows it", nm);
        Expect(!Measure(MakeHand(head, side, kFront, 0.0f), side, head, flip).enter, "%s: flipped: the palm does not", nm);
    }

    // Left and right are mirrors of each other: the same palm test on a mirrored scene
    int checked = 0;
    for (float angle : {0.0f, 10.0f, 30.0f, 40.0f, 55.0f, 100.0f, 160.0f})
        for (V3 pos : {V3{-0.12f, 1.5f, -0.35f}, V3{0.2f, 1.7f, -0.5f}, V3{-0.3f, 1.45f, -0.2f}, V3{0.0f, 1.55f, -0.62f}}) {
            const HandPose left = MakeHand(head, 0, pos, angle, 33.0f);
            HandPose right;
            right.valid = true;
            right.pos = Mirror(left.pos);
            right.rot = Mirror(left.rot);
            const Look3 a = Measure(left, 0, head, cfg), b = Measure(right, 1, head, cfg);
            Expect(a.enter == b.enter && a.keep == b.keep && Near(a.angleDeg, b.angleDeg, 0.01f) && Near(a.distM, b.distM, 1e-5f) && Near(a.gazeDeg, b.gazeDeg, 0.01f),
                   "mirror: angle %.0f at (%.2f %.2f %.2f): %.3f vs %.3f", angle, pos.x, pos.y, pos.z, a.angleDeg, b.angleDeg);
            ++checked;
        }
    printf("  %d mirrored scenes\n", checked);
}

// ---- the reveal -----------------------------------------------------------------------------------------------------------------------------------

struct Sim {
    Reveal r;
    In in;
    Out out;
    float t = 0.0f;
    Sim() {
        in.cfg = On();
        in.head = MakeHead();
        in.gate = Gate{};
        in.dt = kDt;
    }
    void Palm(int side, float angle, V3 pos = kFront) { in.hand[side] = MakeHand(in.head, side, pos, angle); }
    Out Step() {
        out = r.Step(in);
        t += kDt;
        return out;
    }
    Out Run(float seconds) {
        for (float s = 0.0f; s < seconds - 1e-6f; s += kDt) Step();
        return out;
    }
};

void TestReveal() {
    printf("reveal\n");

    // off: nothing, whatever the hand does
    {
        Sim s;
        s.in.cfg.on = false;
        s.Palm(0, 0.0f);
        float most = 0.0f;
        for (int i = 0; i < 180; ++i) most = std::max(most, s.Step().alpha);
        Expect(most == 0.0f && s.out.side == -1 && !s.r.Active(), "off: a perfect palm shows nothing for two seconds");
    }
    // the palm turned to the face: a dwell, then a fade in; the left hand
    for (int side = 0; side < 2; ++side) {
        Sim s;
        s.in.cfg.hand = side;
        s.Palm(side, 5.0f);
        Expect(s.Step().alpha == 0.0f, "side %d: the first frame shows nothing", side);
        s.Run(0.10f);
        Expect(s.out.alpha == 0.0f && s.r.state() == Reveal::State::Dwell, "side %d: 0.1 s in: still the dwell", side);
        float prev = 0.0f, worstJump = 0.0f;
        bool monotone = true;
        for (int i = 0; i < 90; ++i) {
            const float a = s.Step().alpha;
            monotone = monotone && a >= prev - 1e-6f;
            worstJump = std::max(worstJump, a - prev);
            prev = a;
        }
        Expect(monotone && worstJump < 0.12f, "side %d: it comes up smoothly (largest step %.3f)", side, worstJump);
        Expect(s.out.alpha == 1.0f && s.out.side == side && s.out.pose.valid, "side %d: up after a second", side);
        Expect(s.r.state() == Reveal::State::Shown, "side %d: shown", side);
        // the other hand does not matter with the one chosen
        s.Palm(1 - side, 0.0f);
        s.Run(0.5f);
        Expect(s.out.side == side, "side %d: a palm of the other hand does not take it over", side);
    }
    // the right hand turned, the left chosen: nothing
    {
        Sim s;
        s.in.cfg.hand = 0;
        s.Palm(1, 0.0f);
        s.Run(1.0f);
        Expect(s.out.alpha == 0.0f, "left only: a right palm shows nothing");
        s.in.cfg.hand = 1;
        s.Run(1.0f);
        Expect(s.out.alpha == 1.0f && s.out.side == 1, "right only: it does");
    }
    // away: down in a quarter second, no jump
    {
        Sim s;
        s.Palm(0, 10.0f);
        s.Run(1.0f);
        Expect(s.out.alpha == 1.0f, "up");
        s.Palm(0, 180.0f);
        float prev = 1.0f, worst = 0.0f;
        int frames = 0;
        while (s.Step().alpha > 0.0f && frames < 200) {
            worst = std::max(worst, prev - s.out.alpha);
            prev = s.out.alpha;
            ++frames;
        }
        Expect(frames > 15 && frames < 30, "away: gone in %d frames (about 0.28 s)", frames);
        Expect(worst < 0.14f, "away: no jump (largest step %.3f)", worst);
        Expect(s.out.side == -1 && s.r.state() == Reveal::State::Idle && !s.r.Active(), "away: free again");
    }
    // the hysteresis at the cone's edge
    {
        Sim s;
        s.Palm(0, 20.0f);
        s.Run(1.0f);
        s.Palm(0, 45.0f);
        s.Run(2.0f);
        Expect(s.out.alpha == 1.0f, "shown at 20, held at 45 for two seconds (between the cones)");
        s.Palm(0, 55.0f);
        s.Run(0.6f);
        Expect(s.out.alpha == 0.0f, "gone at 55");
        s.Palm(0, 45.0f);
        s.Run(2.0f);
        Expect(s.out.alpha == 0.0f && s.out.side == -1, "from gone, 45 does not bring it back");
        s.Palm(0, 30.0f);
        s.Run(1.0f);
        Expect(s.out.alpha == 1.0f, "30 does");
    }
    // noise round the edge
    {
        Sim s;   // from idle: 36 / 38 never starts it
        for (int i = 0; i < 360; ++i) {
            s.Palm(0, i % 2 ? 38.0f : 36.0f);
            s.Step();
        }
        Expect(s.out.alpha == 0.0f && s.r.state() == Reveal::State::Idle, "36 / 38 degrees alternating from idle: never starts");
        Sim q;   // 30 / 36: starts at 30, kept at 36
        float prev = 0.0f;
        bool monotone = true;
        for (int i = 0; i < 180; ++i) {
            q.Palm(0, i % 2 ? 36.0f : 30.0f);
            const float a = q.Step().alpha;
            monotone = monotone && a >= prev - 1e-6f;
            prev = a;
        }
        Expect(monotone && q.out.alpha == 1.0f, "30 / 36 alternating: comes up once and stays");
        Sim w;   // up, then 49 / 51 alternating: goes once, does not flicker back
        w.Palm(0, 10.0f);
        w.Run(1.0f);
        int rises = 0;
        float last = 1.0f;
        for (int i = 0; i < 180; ++i) {
            w.Palm(0, i % 2 ? 49.0f : 51.0f);
            const float a = w.Step().alpha;
            if (a > last + 1e-6f) ++rises;
            last = a;
        }
        Expect(rises == 0 && w.out.alpha == 0.0f, "49 / 51 alternating: fades out once, no flicker (%d rises)", rises);
    }
    // the palm back while it fades
    {
        Sim s;
        s.Palm(0, 10.0f);
        s.Run(1.0f);
        s.Palm(0, 70.0f);
        s.Run(0.12f);
        const float mid = s.out.alpha;
        Expect(mid > 0.0f && mid < 1.0f, "fading (%.2f)", mid);
        s.Palm(0, 20.0f);
        s.Run(0.05f);
        Expect(s.out.alpha > mid, "back inside the cone while fading: it rises again (%.2f -> %.2f)", mid, s.out.alpha);
        s.Run(0.5f);
        Expect(s.out.alpha == 1.0f, "and is up");
    }
    // a hand swept past the face
    {
        Sim s;
        s.Palm(0, 5.0f);
        float most = 0.0f;
        for (int i = 0; i < 5; ++i) most = std::max(most, s.Step().alpha);   // 0.055 s
        s.Palm(0, 120.0f);
        for (int i = 0; i < 90; ++i) most = std::max(most, s.Step().alpha);
        Expect(most == 0.0f && s.r.state() == Reveal::State::Idle, "a palm that is there for 55 ms shows nothing");
    }
    // the gates
    {
        Sim s;
        s.Palm(0, 5.0f);
        s.Run(1.0f);
        Expect(s.out.alpha == 1.0f, "up");
        s.in.gate.hide = true;   // a menu, a panel, dead, unknown numbers
        int frames = 0;
        while (s.Step().alpha > 0.0f && frames < 100) ++frames;
        Expect(frames > 5 && frames <= 12, "hide: gone in %d frames (0.12 s)", frames);
        s.Run(2.0f);
        Expect(s.out.alpha == 0.0f && !s.r.Active(), "hide: stays gone while it holds, palm or not");
        s.in.gate.hide = false;
        Expect(s.Step().alpha == 0.0f, "hide lifted: not at once");
        s.Run(0.6f);
        Expect(s.out.alpha == 1.0f, "hide lifted: it comes up again by the dwell and the fade");
    }
    {
        Sim s;   // not F4: nothing at the very step, and a fresh dwell when back
        s.Palm(0, 5.0f);
        s.Run(1.0f);
        s.in.gate.hideNow = true;
        const Out o = s.Step();
        Expect(o.alpha == 0.0f && o.side == -1 && !s.r.Active(), "not F4: alpha 0 in the very step");
        s.Run(1.0f);
        Expect(s.out.alpha == 0.0f, "not F4: nothing, a perfect palm and all");
        s.in.gate.hideNow = false;
        Expect(s.Step().alpha == 0.0f, "back in F4: it does not jump to full");
        Expect(s.r.state() == Reveal::State::Dwell, "back in F4: starts by the dwell");
        s.Run(0.6f);
        Expect(s.out.alpha == 1.0f, "back in F4: comes up");
    }
    {   // the gate's makers
        Flags f;
        f.vitalsFresh = true;
        const Vitals ok = FromStats(50 * 256, 100 * 256, 10 * 256, 20 * 256);
        Gate g = MakeGate(f, ok);
        Expect(!g.hideNow && !g.hide, "gate: F4, focus, numbers: open");
        f.f4 = false;
        g = MakeGate(f, ok);
        Expect(g.hideNow, "gate: not F4: hide now");
        f.f4 = true;
        f.otherView = true;
        Expect(MakeGate(f, ok).hideNow, "gate: another view: hide now");
        f.otherView = false;
        f.inMenu = true;
        g = MakeGate(f, ok);
        Expect(!g.hideNow && g.hide, "gate: a menu: fade out");
        f.inMenu = false;
        f.uiUp = true;
        Expect(MakeGate(f, ok).hide, "gate: a plugin panel up: fade out");
        f.uiUp = false;
        f.focused = false;
        Expect(MakeGate(f, ok).hide, "gate: no input focus: fade out");
        f.focused = true;
        f.vitalsFresh = false;
        Expect(MakeGate(f, ok).hide, "gate: a stale read: fade out");
        f.vitalsFresh = true;
        Expect(MakeGate(f, Vitals{}).hide, "gate: unknown numbers: fade out");
        Expect(MakeGate(f, FromStats(0, 100 * 256, 0, 0)).hide, "gate: dead: fade out");
    }
    // either hand
    {
        Sim s;
        s.in.cfg.hand = 2;
        s.Palm(0, 25.0f, {-0.1f, 1.45f, -0.38f});
        s.Palm(1, 10.0f, {0.1f, 1.45f, -0.38f});
        s.Run(1.0f);
        Expect(s.out.side == 1 && s.out.alpha == 1.0f, "either: the palm turned more to the face wins (right, 10 degrees)");
        s.Palm(1, 20.0f, {0.1f, 1.45f, -0.38f});
        s.Palm(0, 2.0f, {-0.1f, 1.45f, -0.38f});
        s.Run(1.0f);
        Expect(s.out.side == 1, "either: the one up stays up when the other gets better");
        s.Palm(1, 180.0f, {0.1f, 1.45f, -0.38f});
        bool free = false;
        for (int i = 0; i < 40 && !free; ++i) free = s.Step().side == -1 && s.out.alpha == 0.0f;
        Expect(free, "either: the right hand's panel is gone");
        s.Run(0.7f);
        Expect(s.out.side == 0 && s.out.alpha == 1.0f, "either: then the left takes over");
        Expect(s.out.pose.valid && s.out.pose.p.x < 0.0f, "either: and hangs at the left hand (x %.2f)", s.out.pose.p.x);
    }
    // tracking lost while up
    {
        Sim s;
        s.Palm(0, 10.0f);
        s.Run(1.0f);
        const hset::Pose before = s.out.pose;
        s.in.hand[0].valid = false;
        s.in.hand[0].pos = {0, 0, 0};
        s.Step();
        Expect(s.out.alpha < 1.0f && s.out.pose.valid && Near(s.out.pose.p.x, before.p.x, 1e-6f) && Near(s.out.pose.p.y, before.p.y, 1e-6f),
               "tracking lost: it fades out where it was, not at the origin");
        s.Run(0.4f);
        Expect(s.out.alpha == 0.0f, "tracking lost: gone");
    }
    // the head lost
    {
        Sim s;
        s.Palm(0, 10.0f);
        s.Run(1.0f);
        s.in.head.valid = false;
        const Out o = s.Step();
        Expect(o.alpha == 0.0f && !s.r.Active(), "no head: nothing");
    }
    // long frames and bad numbers
    {
        Sim s;
        s.Palm(0, 5.0f);
        s.in.dt = 5.0f;   // a load hitch
        float most = 0.0f;
        for (int i = 0; i < 3; ++i) most = std::max(most, s.Step().alpha);
        Expect(most < 0.9f && most > 0.0f && s.r.state() == Reveal::State::Shown, "a 5 s frame counts as 0.1 s: the dwell is not skipped, no jump to full (%.2f)", most);
        s.in.dt = std::nanf("");
        Expect(s.Step().alpha == 0.0f && !s.r.Active(), "a NaN step: reset, nothing");
        s.in.dt = -1.0f;
        s.Step();
        s.in.dt = kDt;
        s.in.hand[0].pos = {std::nanf(""), 0, 0};
        s.Step();
        Expect(s.out.alpha == 0.0f, "a NaN hand: nothing");
    }
    // the mirror: the right hand on the mirrored scene follows the left hand's sequence
    {
        Sim l, r;
        r.in.cfg.hand = 1;
        const float angles[] = {10.0f, 10.0f, 44.0f, 44.0f, 70.0f, 20.0f, 20.0f, 160.0f};
        bool same = true;
        for (float a : angles)
            for (int i = 0; i < 40; ++i) {
                l.in.hand[0] = MakeHand(l.in.head, 0, {-0.1f, 1.45f, -0.38f}, a, 17.0f);
                r.in.hand[1].valid = true;
                r.in.hand[1].pos = Mirror(l.in.hand[0].pos);
                r.in.hand[1].rot = Mirror(l.in.hand[0].rot);
                l.Step();
                r.Step();
                same = same && l.out.alpha == r.out.alpha && (l.out.alpha == 0.0f || (l.out.side == 0 && r.out.side == 1));
            }
        Expect(same, "the right hand on the mirrored scene does what the left one does");
    }
}

// ---- where the panel hangs --------------------------------------------------------------------------------------------------------------------------

void TestPose() {
    printf("pose\n");
    const V3 head{0.0f, 1.6f, 0.0f}, hand{0.1f, 1.4f, -0.4f};
    const V3 up{0.0f, 1.0f, 0.0f};
    hset::Pose p = PanelPose(hand, head, 6.0f);
    Expect(p.valid, "a pose");
    const V3 z = hset::Rot(p.q, {0, 0, 1}), x = hset::Rot(p.q, {1, 0, 0}), y = hset::Rot(p.q, {0, 1, 0});
    Expect(hset::Dot(z, hset::Unit(hset::Sub(head, p.p))) > 0.9999f, "the front faces the head (%.5f)", hset::Dot(z, hset::Unit(hset::Sub(head, p.p))));
    Expect(Near(x.y, 0.0f, 1e-5f), "the panel is level (its x axis has y = %.6f)", x.y);
    Expect(y.y > 0.9f, "its top is up (%.3f)", y.y);
    Expect(Near(hset::Dot(x, y), 0.0f, 1e-5f) && Near(hset::Dot(x, z), 0.0f, 1e-5f) && Near(hset::Dot(y, z), 0.0f, 1e-5f) && Near(Len(x), 1.0f, 1e-5f) &&
               Near(Len(y), 1.0f, 1e-5f) && Near(Len(z), 1.0f, 1e-5f), "an orthonormal set");
    Expect(Near(p.q.x * p.q.x + p.q.y * p.q.y + p.q.z * p.q.z + p.q.w * p.q.w, 1.0f, 1e-5f), "a unit quaternion");
    // right = forward x up (looking at the panel from the head's side)
    const V3 viewFwd = hset::Mul(z, -1.0f);
    const V3 right = hset::Unit(Cross(viewFwd, up));
    Expect(hset::Dot(x, right) > 0.99f, "x is to the viewer's right (%.3f)", hset::Dot(x, right));
    const V3 d = hset::Sub(p.p, hand);
    Expect(Near(d.y, 0.06f + 0.03f * (head.y - hand.y) / Len(hset::Sub(head, hand)), 1e-5f), "6 cm up and a little toward the head (dy %.4f)", d.y);
    Expect(Len(d) < 0.09f, "right at the hand: %.1f cm away", Len(d) * 100.0f);

    // the height setting
    const hset::Pose p0 = PanelPose(hand, head, 0.0f), p10 = PanelPose(hand, head, 10.0f), pm = PanelPose(hand, head, -5.0f);
    Expect(Near(p10.p.y - p0.p.y, 0.10f, 1e-5f) && Near(p0.p.y - pm.p.y, 0.05f, 1e-5f), "the height setting moves the panel straight up");
    Expect(Near(p10.p.x, p0.p.x, 1e-6f) && Near(p10.p.z, p0.p.z, 1e-6f), "and only up");
    Expect(Near(PanelPose(hand, head, 99.0f).p.y - p0.p.y, 0.15f, 1e-5f) && Near(PanelPose(hand, head, -99.0f).p.y - p0.p.y, -0.05f, 1e-5f), "clamped to -5 .. +15");
    Expect(Near(PanelPose(hand, head, std::nanf("")).p.y - p0.p.y, 0.06f, 1e-5f), "NaN takes the default");

    // the size
    Config c;
    c.sizeCm = 8.0f;
    Expect(Near(PanelWidthM(c), 0.08f, 1e-6f) && Near(PanelHeightM(c), 0.08f * 2.0f / 3.0f, 1e-6f), "8 cm wide, two thirds of that high");
    c.sizeCm = 14.0f;
    Expect(Near(PanelWidthM(c), 0.14f, 1e-6f), "14 cm");
    Expect(Near((float)handvitalspaint::kH / (float)handvitalspaint::kW, kPanelAspect, 1e-6f), "the picture has the panel's proportions");

    // it follows the hand, not the head
    const hset::Pose a = PanelPose(hand, head, 6.0f), b = PanelPose({hand.x + 0.2f, hand.y, hand.z}, head, 6.0f);
    Expect(Near(b.p.x - a.p.x, 0.2f, 0.03f), "the hand 20 cm to the right: the panel about 20 cm to the right");
    const hset::Pose c1 = PanelPose(hand, {0.5f, 1.6f, 0.3f}, 6.0f);
    Expect(Len(hset::Sub(c1.p, a.p)) < 0.04f, "the head elsewhere: the panel stays at the hand (moved %.1f cm, only its hover)", Len(hset::Sub(c1.p, a.p)) * 100.0f);
    Expect(hset::Dot(hset::Rot(c1.q, {0, 0, 1}), hset::Unit(hset::Sub({0.5f, 1.6f, 0.3f}, c1.p))) > 0.9999f, "and turns to the new head position");

    // degenerate
    const hset::Pose below = PanelPose({0.0f, 1.2f, 0.0f}, head, 6.0f);
    Expect(below.valid && Near(Len(hset::Rot(below.q, {1, 0, 0})), 1.0f, 1e-5f), "a hand straight under the head: still a pose (any level direction)");
    Expect(!PanelPose(head, head, 6.0f).valid, "a hand on the head: no pose");
    Expect(!PanelPose({std::nanf(""), 0, 0}, head, 6.0f).valid, "a NaN hand: no pose");
    Expect(!PanelPose(hand, {0, std::nanf(""), 0}, 6.0f).valid, "a NaN head: no pose");

    // FromAxes against known turns
    const Q q = FromAxes({1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    Expect(Near(q.w, 1.0f, 1e-6f) && Near(q.x, 0.0f, 1e-6f), "identity axes -> identity");
    const Q q90 = FromAxes({0, 0, -1}, {0, 1, 0}, {1, 0, 0});   // a quarter turn about +y: x -> -z
    const V3 r = hset::Rot(q90, {1, 0, 0});
    Expect(Near(r.z, -1.0f, 1e-5f) && Near(r.x, 0.0f, 1e-5f), "a quarter turn about y turns x to -z");
    for (V3 zz : {V3{0, 0, 1}, V3{1, 0, 0}, V3{-1, 0, 0}, V3{0, 0, -1}, V3{0.6f, 0.0f, -0.8f}, V3{-0.3f, 0.2f, 0.93f}}) {   // every branch of the conversion
        const V3 zu = hset::Unit(zz);
        V3 xx = Cross(up, zu);
        xx = hset::Unit(xx);
        const V3 yy = Cross(zu, xx);
        const Q qq = FromAxes(xx, yy, zu);
        const V3 rx = hset::Rot(qq, {1, 0, 0}), rz = hset::Rot(qq, {0, 0, 1});
        Expect(Near(hset::Dot(rx, xx), 1.0f, 1e-4f) && Near(hset::Dot(rz, zu), 1.0f, 1e-4f), "FromAxes round trip for z (%.2f %.2f %.2f)", zu.x, zu.y, zu.z);
    }
}

// ---- the picture --------------------------------------------------------------------------------------------------------------------------------------

struct Rgb {
    int w, h;
    std::vector<uint8_t> px;
    Rgb(int ww, int hh) : w(ww), h(hh), px((size_t)ww * hh * 3, 0) {}
};
uint32_t Crc(const uint8_t* d, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}
void Be32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s)); }
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    Be32(out, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    Be32(out, ~Crc(td.data(), td.size()));
}
bool WritePng(const std::string& path, const Rgb& img) {
    std::vector<uint8_t> raw;
    for (int y = 0; y < img.h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), &img.px[(size_t)y * img.w * 3], &img.px[(size_t)y * img.w * 3] + (size_t)img.w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};   // zlib, stored blocks: simple, and every viewer reads it
    for (size_t at = 0; at < raw.size(); at += 65535) {
        const size_t len = std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + len >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)(len & 0xFF));
        z.push_back((uint8_t)(len >> 8));
        z.push_back((uint8_t)(~len & 0xFF));
        z.push_back((uint8_t)((~len >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + len);
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    Be32(z, (b << 16) | a);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, ihdr;
    Be32(ihdr, (uint32_t)img.w);
    Be32(ihdr, (uint32_t)img.h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") || !f) return false;
    fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return true;
}
// The panel (premultiplied) laid over a flat backdrop at (x0, y0).
void Composite(Rgb& dst, const std::vector<uint32_t>& px, int x0, int y0, const uint8_t bg[3]) {
    for (int y = 0; y < handvitalspaint::kH; ++y)
        for (int x = 0; x < handvitalspaint::kW; ++x) {
            const uint32_t p = px[(size_t)y * handvitalspaint::kW + x];
            const float a = (p >> 24) / 255.0f;
            const int dx = x0 + x, dy = y0 + y;
            if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h) continue;
            uint8_t* o = &dst.px[((size_t)dy * dst.w + dx) * 3];
            const float c[3] = {(float)(p & 255), (float)((p >> 8) & 255), (float)((p >> 16) & 255)};
            for (int k = 0; k < 3; ++k) o[k] = (uint8_t)std::clamp((int)std::lround(c[k] + bg[k] * (1.0f - a)), 0, 255);
        }
}

struct Px {
    int r, g, b, a;
};
Px At(const std::vector<uint32_t>& px, int x, int y) {
    const uint32_t p = px[(size_t)y * handvitalspaint::kW + x];
    return {(int)(p & 255), (int)((p >> 8) & 255), (int)((p >> 16) & 255), (int)(p >> 24)};
}

Look LookOf(float life, float maxLife, float mana, float maxMana, bool alert = false, float opacity = 1.0f) {
    Vitals v;
    v.known = true;
    v.life = life;
    v.maxLife = maxLife;
    v.mana = mana;
    v.maxMana = maxMana;
    Config c = On();
    c.pulse = alert;
    c.lowFrac = 0.30f;
    c.opacity = opacity;
    return MakeLook(v, c);
}

void TestPicture(const std::string& dir) {
    printf("picture\n");
    using namespace handvitalspaint;
    std::vector<uint32_t> full, empty, half, alert, dim;
    Render(LookOf(1000, 1000, 400, 400), full);
    Render(LookOf(0, 1000, 0, 400), empty);
    Render(LookOf(700, 1000, 100, 400), half);
    Render(LookOf(80, 1000, 400, 400, true), alert);
    Render(LookOf(1000, 1000, 400, 400, false, 0.5f), dim);
    Expect(full.size() == (size_t)kW * kH, "the picture is %d x %d", kW, kH);

    // premultiplied, nothing out of range
    bool premult = true;
    for (const std::vector<uint32_t>* img : {&full, &empty, &half, &alert, &dim})
        for (uint32_t p : *img) {
            const int a = (int)(p >> 24);
            if ((int)(p & 255) > a + 1 || (int)((p >> 8) & 255) > a + 1 || (int)((p >> 16) & 255) > a + 1) premult = false;
        }
    Expect(premult, "every pixel is premultiplied (no channel above its alpha)");
    Expect(At(full, 0, 0).a == 0 && At(full, kW - 1, 0).a == 0 && At(full, 0, kH - 1).a == 0 && At(full, kW - 1, kH - 1).a == 0, "the four corners are clear");

    constexpr int lx = 96, mx = 288, top = 100 - 70, bottom = 100 + 70;
    // full: red life, blue mana, at the bottom of the globes
    Px p = At(full, lx, bottom - 8);
    Expect(p.r > 100 && p.r > 2 * p.g && p.r > 2 * p.b && p.a == 255, "full life globe is red at the bottom (%d %d %d)", p.r, p.g, p.b);
    p = At(full, lx, top + 12);
    Expect(p.r > 100 && p.r > 2 * p.g, "and at the top (%d %d %d)", p.r, p.g, p.b);
    p = At(full, mx, bottom - 8);
    Expect(p.b > 90 && p.b > 2 * p.r && p.b > p.g, "full mana globe is blue (%d %d %d)", p.r, p.g, p.b);
    // empty: dark glass
    p = At(empty, lx, bottom - 8);
    Expect(p.r < 70 && p.g < 50 && p.b < 60 && p.a > 200, "empty life globe is dark glass (%d %d %d)", p.r, p.g, p.b);
    p = At(empty, mx, bottom - 8);
    Expect(p.b < 70 && p.r < 70, "empty mana globe is dark glass (%d %d %d)", p.r, p.g, p.b);
    // 70% life: red below the line, dark above; mana 25%
    p = At(half, lx, bottom - 20);
    Expect(p.r > 90 && p.r > 2 * p.g, "70%% life: red low (%d %d %d)", p.r, p.g, p.b);
    p = At(half, lx + 30, top + 18);
    Expect(p.r < 70 && p.a > 200, "70%% life: dark high (%d %d %d)", p.r, p.g, p.b);
    p = At(half, mx + 20, bottom - 10);
    Expect(p.b > 60 && p.b > 2 * p.r, "25%% mana: blue at the bottom (%d %d %d)", p.r, p.g, p.b);
    p = At(half, mx + 20, 100);
    Expect(p.b < 70, "25%% mana: not up at the middle (%d %d %d)", p.r, p.g, p.b);
    // the fill line rises with the level: count red rows in the life column
    auto redRows = [&](const std::vector<uint32_t>& img) {
        int n = 0;
        for (int y = top; y <= bottom; ++y) {
            const Px q = At(img, lx + 25, y);   // off the highlight
            if (q.r > 90 && q.r > 2 * q.g && q.r > 2 * q.b) ++n;
        }
        return n;
    };
    std::vector<uint32_t> l25, l50, l75;
    Render(LookOf(250, 1000, 0, 1), l25);
    Render(LookOf(500, 1000, 0, 1), l50);
    Render(LookOf(750, 1000, 0, 1), l75);
    const int r0 = redRows(empty), r25 = redRows(l25), r50 = redRows(l50), r75 = redRows(l75), r100 = redRows(full);
    Expect(r0 < 3 && r25 < r50 && r50 < r75 && r75 < r100, "the red column rises with the level: %d %d %d %d %d rows", r0, r25, r50, r75, r100);
    Expect(std::abs(r50 - (int)(2 * 70 * 0.5f)) < 22, "half life is about half the glass (%d of ~%d rows, off the rim's curve)", r50, 70);
    // the low-life glow: red outside the rim only with the alert
    const Px g1 = At(alert, lx - 78 - 5, 100), g0 = At(half, lx - 78 - 5, 100);
    Expect(g1.r > g0.r + 25 && g1.a > g0.a, "alert: a red glow round the life globe (%d vs %d)", g1.r, g0.r);
    const Px m1 = At(alert, mx - 78 - 5, 100), m0 = At(half, mx - 78 - 5, 100);
    Expect(m1.r == m0.r && m1.a == m0.a, "alert: the mana globe has none");
    // the rim is metal (warm, lighter than the glass), not clear
    p = At(full, lx - 74, 100);
    Expect(p.a > 240 && p.r > 40, "the rim is solid metal (%d %d %d %d)", p.r, p.g, p.b, p.a);
    // the opacity
    p = At(dim, lx, bottom - 8);
    const Px pf = At(full, lx, bottom - 8);
    Expect(Near((float)p.a, pf.a * 0.5f, 2.0f) && Near((float)p.r, pf.r * 0.5f, 2.0f), "opacity 0.5 halves every channel (%d vs %d)", p.r, pf.r);

    // the numbers: cream pixels in the box under each globe, inside the plate
    auto lit = [&](const std::vector<uint32_t>& img, int x0, int x1, int y0, int y1, int* minX, int* maxX) {
        int n = 0;
        *minX = 9999;
        *maxX = -1;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const Px q = At(img, x, y);
                if (q.r > 200 && q.g > 190 && q.b > 170) {
                    ++n;
                    *minX = std::min(*minX, x);
                    *maxX = std::max(*maxX, x);
                }
            }
        return n;
    };
    int mn, mxx;
    int n = lit(full, 0, kW / 2, 180, kH, &mn, &mxx);
    Expect(n > 150 && mn > 12 && mxx < kW / 2, "the life numbers are drawn under the life globe (%d lit pixels, x %d..%d)", n, mn, mxx);
    n = lit(full, kW / 2, kW, 180, kH, &mn, &mxx);
    Expect(n > 100 && mn > kW / 2 && mxx < kW - 12, "the mana numbers under the mana globe (%d lit pixels, x %d..%d)", n, mn, mxx);
    n = lit(full, 0, kW, 0, 170, &mn, &mxx);
    Expect(n < 600, "no numbers in the globes' area (%d bright pixels: only highlights)", n);
    std::vector<uint32_t> huge, zero, wide;
    Render(LookOf(999999, 999999, 999999, 999999), huge);
    n = lit(huge, 0, kW / 2, 180, kH, &mn, &mxx);
    Expect(n > 100 && mn > 12 && mxx < kW / 2 - 6, "huge numbers (999k / 999k) are shrunk to fit (x %d..%d of the half 0..%d)", mn, mxx, kW / 2);
    Look hl = LookOf(1, 1, 1, 1);
    strcpy_s(hl.lifeText, "999.9M / 999.9M");
    strcpy_s(hl.manaText, "999.9M / 999.9M");
    Render(hl, wide);
    n = lit(wide, 0, kW / 2, 180, kH, &mn, &mxx);
    Expect(n > 100 && mn > 12 && mxx < kW / 2 - 6, "the longest text (%s) fits (x %d..%d)", hl.lifeText, mn, mxx);
    Render(LookOf(0, 1000, 0, 0), zero);
    n = lit(zero, kW / 2, kW, 180, kH, &mn, &mxx);
    Expect(n > 40, "mana with no pool shows '0 / 0' (%d lit pixels)", n);
    p = At(zero, mx, bottom - 8);
    Expect(p.b < 70 && p.r < 70, "and its globe is empty");
    Look none;   // empty texts: nothing drawn, no crash
    Render(none, wide);
    Expect(wide.size() == (size_t)kW * kH, "an empty look draws (globes, no numbers)");

    // deterministic
    std::vector<uint32_t> again;
    Render(LookOf(700, 1000, 100, 400), again);
    Expect(again == half, "the same look paints the same picture");

    if (dir.empty()) return;
    // The sheet: the panel over a hand-ish backdrop at several levels (life / mana): full, 70 / 25, 35 / 0, 8 low-life alert / 100, dead, no mana pool; and the dim one
    const uint8_t bg[3] = {74, 58, 50};
    std::vector<Look> items = {LookOf(1000, 1000, 400, 400), LookOf(700, 1000, 100, 400), LookOf(350, 1000, 0, 400), LookOf(80, 1000, 400, 400, true),
                               LookOf(0, 1000, 0, 400), LookOf(1000, 1000, 0, 0), LookOf(12345, 12345, 999, 999), LookOf(1000, 1000, 400, 400, false, 0.5f)};
    Rgb sheet(kW * 2 + 24, (kH + 8) * 4 + 8);
    for (size_t i = 0; i < sheet.px.size(); i += 3) { sheet.px[i] = bg[0]; sheet.px[i + 1] = bg[1]; sheet.px[i + 2] = bg[2]; }
    for (size_t i = 0; i < items.size(); ++i) {
        std::vector<uint32_t> img;
        Render(items[i], img);
        Composite(sheet, img, 8 + (int)(i % 2) * (kW + 8), 8 + (int)(i / 2) * (kH + 8), bg);
    }
    Expect(WritePng(dir + "\\hand_vitals_sheet.png", sheet), "wrote hand_vitals_sheet.png");
    // Two single panels on a dark and a bright backdrop, at the size they have in the headset when looked at from arm's length
    const uint8_t dark[3] = {18, 20, 28}, bright[3] = {190, 178, 150};
    Rgb two(kW * 2 + 24, kH + 16);
    for (size_t i = 0; i < two.px.size(); i += 3) { two.px[i] = dark[0]; two.px[i + 1] = dark[1]; two.px[i + 2] = dark[2]; }
    for (int y = 0; y < two.h; ++y)
        for (int x = two.w / 2; x < two.w; ++x) { uint8_t* o = &two.px[((size_t)y * two.w + x) * 3]; o[0] = bright[0]; o[1] = bright[1]; o[2] = bright[2]; }
    std::vector<uint32_t> img;
    Render(LookOf(700, 1000, 300, 400), img);
    Composite(two, img, 8, 8, dark);
    Composite(two, img, kW + 16, 8, bright);
    Expect(WritePng(dir + "\\hand_vitals_backdrops.png", two), "wrote hand_vitals_backdrops.png");
    // Full size, one panel
    Rgb one(kW, kH);
    for (size_t i = 0; i < one.px.size(); i += 3) { one.px[i] = bg[0]; one.px[i + 1] = bg[1]; one.px[i + 2] = bg[2]; }
    Render(LookOf(820, 1000, 260, 400), img);
    Composite(one, img, 0, 0, bg);
    Expect(WritePng(dir + "\\hand_vitals_panel.png", one), "wrote hand_vitals_panel.png");
    printf("  hand_vitals_sheet.png, hand_vitals_backdrops.png, hand_vitals_panel.png in %s\n", dir.c_str());
}

}  // namespace

// The panel does not depend on the game's render size or window size ([render] vr_resolution, the square window, the safe-screen setting): it is drawn
// from the hero's numbers on a canvas of its own (384 x 256) and hung in the room in metres. Nothing in its files may ask for a size of the game's picture
// or take a crop of it (d2r-pcvr's palm vitals crop the game's own HUD, which comes out wrong at a square render size).
void TestNoRenderSize() {
    printf("independent of the render size\n");
#ifdef HANDVITALS_SRC_DIR
    const char* files[] = {"hand_vitals.h", "hand_vitals_paint.h", "hand_vitals_paint.cpp", "hand_vitals_wire.h", "hand_vitals_wire.cpp",
                           "hand_vitals_game.h", "hand_vitals_game.cpp", "xr_hand_vitals.h"};
    const char* banned[] = {"render_resolution", "vrres", "vr_resolution", "GetClientRect", "GetWindowRect", "GetSystemMetrics", "g_wnd", "hud.h", "hud_native",
                            "StretchBlt", "BitBlt", "d2rcam", "ClientToScreen", "backbuffer", "back buffer", "UnitToScreen"};
    int scanned = 0;
    for (const char* f : files) {
        FILE* fp = nullptr;
        if (fopen_s(&fp, (std::string(HANDVITALS_SRC_DIR) + "\\" + f).c_str(), "rb") || !fp) {
            Expect(false, "could not read %s", f);
            continue;
        }
        std::string text;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, fp)) > 0) text.append(buf, n);
        fclose(fp);
        ++scanned;
        for (const char* b : banned) Expect(text.find(b) == std::string::npos, "%s mentions '%s': the panel must not depend on a size of the game's picture", f, b);
    }
    Expect(scanned == (int)(sizeof files / sizeof files[0]), "scanned every file of the panel (%d)", scanned);
#else
    printf("  (no source folder given: scan skipped)\n");
#endif
    // What fixes the panel's size: the setting (metres) and one canvas - the same whatever size the game renders at.
    using namespace handvitalspaint;
    Config c;
    c.sizeCm = 8.0f;
    Expect(Near(PanelWidthM(c), 0.08f, 1e-6f) && kW == 384 && kH == 256, "the panel is 8 cm wide on a 384 x 256 canvas");
    std::vector<uint32_t> a, b;
    Render(LookOf(700, 1000, 100, 400), a);
    Render(LookOf(700, 1000, 100, 400), b);
    Expect(a == b, "the same look paints the same pixels (nothing else is an input)");
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "";
    TestNumbers();
    TestSettings();
    TestPalm();
    TestReveal();
    TestPose();
    TestPicture(dir);
    TestNoRenderSize();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
