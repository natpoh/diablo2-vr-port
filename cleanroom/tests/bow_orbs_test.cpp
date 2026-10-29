// [bow] quiver_orbs (vr/bow_orbs.h) on synthetic numbers, without the game and without a headset: where the orbs sit on the bow's stave, what the player's sliders do to the row, which
// orb the hand is at (and how far from it), and the grip that takes it.
//
//   bow_orbs_test

#include "../../vr/bow_orbs.h"

#include <cmath>
#include <cstdio>

using namespace boworbs;
using bowquiver::Entry;

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

bool Near(Vec a, Vec b, float tol = 1e-4f) { return bowdraw::Length(a - b) < tol; }

// An upright bow held 40 cm ahead of the eyes at chest height: the stave straight up 65 cm, the archer behind it (+z).
Stave UprightStave() {
    Stave s;
    s.grip = {-0.2f, -0.4f, -0.4f};
    s.tip = {-0.2f, 0.25f, -0.4f};
    s.back = {0.0f, 0.0f, 1.0f};
    s.fromModel = true;
    return s;
}
Entry A(int id, int owner = -1) { Entry e; e.id = id; e.owner = owner; return e; }

void TestLayout() {
    std::puts("the orbs along the stave");
    const Stave st = UprightStave();
    const Entry list[4] = {A(0), A(6), A(7), A(11)};
    const Entry cur = A(7);
    const Tuning tn;
    Layout l;
    EXPECT(MakeLayout(st, list, 4, &cur, tn, &l) && l.n == 4, "made");
    // on a line parallel to the stave, above the grip, on the archer's side
    for (int i = 0; i < l.n; ++i) {
        EXPECT(std::fabs(l.orb[i].c.x - st.grip.x) < 1e-5f && l.orb[i].c.z > st.grip.z + l.orb[i].r, "orb %d: on the archer's side of the limb, clear of it", i);
        EXPECT(l.orb[i].c.y > st.grip.y + 0.17f && l.orb[i].c.y < st.tip.y, "orb %d: above the grip by 18 cm, below the tip", i);
        if (i) EXPECT(l.orb[i].c.y > l.orb[i - 1].c.y, "orb %d: higher than the one before", i);
        EXPECT(std::fabs(l.orb[i].r - 0.022f) < 1e-6f, "orb %d: 2.2 cm by default (bigger than it was)", i);
    }
    EXPECT(std::fabs(l.spacing - 0.065f) < 1e-5f && std::fabs((l.orb[1].c.y - l.orb[0].c.y) - 0.065f) < 1e-4f, "few orbs: 6.5 cm apart (%.3f)", l.spacing);
    EXPECT(std::fabs((l.orb[0].c.y - st.grip.y) - 0.18f) < 1e-4f, "the first 18 cm up the stave");
    EXPECT(std::fabs(l.reach - 0.075f) < 1e-6f && l.reach > 3.0f * l.orb[0].r, "the grab radius is 7.5 cm, well over the orb's");
    // the plain shot is the first and pale; the arrows have their skill's colour
    EXPECT(l.orb[0].entry.id == 0 && l.orb[0].rgb[0] == kPlainRgb[0] && l.orb[1].rgb[0] == bowquiver::FindArrow(6)->rgb[0] && l.orb[2].rgb[1] == bowquiver::FindArrow(7)->rgb[1], "colours");
    EXPECT(l.orb[2].current && !l.orb[0].current && !l.orb[1].current && !l.orb[3].current, "the slot's skill is the current one");
    EXPECT(l.orb[0].hover == 0.0f, "nobody is near");
    // no current: none marked
    EXPECT(MakeLayout(st, list, 4, nullptr, tn, &l) && !l.orb[0].current && !l.orb[2].current, "no current skill");
    // a full row squeezes to fit the stave
    Entry many[12];
    for (int i = 0; i < 12; ++i) many[i] = A(i + 100);
    EXPECT(MakeLayout(st, many, 12, nullptr, tn, &l) && l.n == 12, "twelve");
    EXPECT(l.orb[11].c.y < st.tip.y && l.spacing < 0.065f && l.spacing > 0.03f, "twelve fit: spacing %.3f", l.spacing);
    EXPECT(MakeLayout(st, many, 20, nullptr, tn, &l) && l.n == kMaxOrbs, "more than the effect draws are cut");
    // one orb: the first place
    EXPECT(MakeLayout(st, list, 1, nullptr, tn, &l) && l.n == 1 && std::fabs((l.orb[0].c.y - st.grip.y) - 0.18f) < 1e-4f, "one orb");
    // a leaning bow: the line leans with it, the offset stays square to it
    Stave lean = st;
    lean.tip = {-0.2f + 0.3f, -0.4f + 0.5f, -0.4f};
    lean.back = {0.0f, 0.2f, 1.0f};   // not square to the stave: only the part square to it is used
    EXPECT(MakeLayout(lean, list, 4, nullptr, tn, &l), "leaning");
    {
        const Vec u = bowdraw::Normalized(lean.tip - lean.grip);
        const Vec fromLine = l.orb[2].c - lean.grip;
        const Vec across = fromLine - u * bowdraw::Dot(fromLine, u);
        EXPECT(std::fabs(bowdraw::Length(across) - (l.orb[2].r + 0.02f)) < 1e-4f, "the offset square to the stave: %.4f", bowdraw::Length(across));
        EXPECT(bowdraw::Dot(across, lean.back) > 0.0f, "on the archer's side");
    }
    // nonsense: no layout, the output untouched
    Layout keep = l;
    Stave bad = st;
    bad.tip = bad.grip + Vec{0.0f, 0.1f, 0.0f};
    EXPECT(!MakeLayout(bad, list, 4, nullptr, tn, &l) && l.n == keep.n, "a stave of 10 cm is no stave");
    bad = st; bad.tip = {NAN, 0, 0};
    EXPECT(!MakeLayout(bad, list, 4, nullptr, tn, &l), "NaN");
    bad = st; bad.tip = bad.grip + Vec{0.0f, 3.0f, 0.0f};
    EXPECT(!MakeLayout(bad, list, 4, nullptr, tn, &l), "a stave of 3 m");
    bad = st; bad.back = {0.0f, 1.0f, 0.0f};
    EXPECT(!MakeLayout(bad, list, 4, nullptr, tn, &l), "the archer's side along the stave: no side");
    EXPECT(!MakeLayout(st, list, 0, nullptr, tn, &l) && !MakeLayout(st, nullptr, 4, nullptr, tn, &l) && !MakeLayout(st, list, 4, nullptr, tn, nullptr), "no list, no output");
    // the orbs keep clear of the string's resting place (16 cm behind the grip along the bow, at the grip's height): 20 cm
    {
        const Vec nock = st.grip + Vec{0.0f, 0.0f, 0.16f};
        MakeLayout(st, list, 4, nullptr, tn, &l);
        for (int i = 0; i < l.n; ++i) EXPECT(bowdraw::Length(l.orb[i].c - nock) > 0.20f, "orb %d is %.3f m from the string's rest", i, bowdraw::Length(l.orb[i].c - nock));
    }
}

void TestTuning() {
    std::puts("the sliders");
    const Stave st = UprightStave();
    const Entry list[4] = {A(0), A(6), A(7), A(11)};
    Layout base, l;
    MakeLayout(st, list, 4, nullptr, Tuning{}, &base);
    // size
    Tuning t;
    t.radius = 0.04f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && std::fabs(l.orb[0].r - 0.04f) < 1e-6f, "size 4 cm");
    EXPECT(l.orb[0].c.z > base.orb[0].c.z, "a bigger orb stands further off the limb (it stays clear of it): %.3f", l.orb[0].c.z - base.orb[0].c.z);
    // grab radius
    t = Tuning{};
    t.reach = 0.12f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && std::fabs(l.reach - 0.12f) < 1e-6f, "grab radius 12 cm");
    // start along the stave
    t = Tuning{};
    t.start = 0.30f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && std::fabs((l.orb[0].c.y - st.grip.y) - 0.30f) < 1e-4f, "start 30 cm up");
    t.start = 0.0f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && std::fabs(l.orb[0].c.y - st.grip.y) < 1e-4f, "start at the grip");
    t.start = 0.55f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && l.orb[3].c.y <= st.tip.y + 1e-4f && l.spacing < 0.065f, "a start high on the stave squeezes the row into what is left (spacing %.3f)", l.spacing);
    // spacing
    t = Tuning{};
    t.spacing = 0.10f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && std::fabs((l.orb[1].c.y - l.orb[0].c.y) - 0.10f) < 1e-4f, "spacing 10 cm");
    t.spacing = 0.03f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && std::fabs((l.orb[1].c.y - l.orb[0].c.y) - 0.03f) < 1e-4f, "spacing 3 cm");
    // the shift along the bow's axes: x across (to the right), y up the stave, z toward the archer
    t = Tuning{};
    t.offset[0] = 0.05f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[1].c, base.orb[1].c + Vec{0.05f, 0.0f, 0.0f}), "x: 5 cm to the right of an upright bow");
    t = Tuning{};
    t.offset[1] = -0.04f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[1].c, base.orb[1].c + Vec{0.0f, -0.04f, 0.0f}), "y: 4 cm down the stave");
    t = Tuning{};
    t.offset[2] = 0.03f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[1].c, base.orb[1].c + Vec{0.0f, 0.0f, 0.03f}), "z: 3 cm toward the archer");
    // the shift follows a leaning bow (its own axes)
    Stave lean = st;
    lean.tip = {-0.2f + 0.65f, -0.4f, -0.4f};   // lying on its side: the stave points right
    Layout leanBase;
    MakeLayout(lean, list, 4, nullptr, Tuning{}, &leanBase);
    t = Tuning{};
    t.offset[1] = 0.05f;
    EXPECT(MakeLayout(lean, list, 4, nullptr, t, &l) && Near(l.orb[1].c, leanBase.orb[1].c + Vec{0.05f, 0.0f, 0.0f}), "y follows the stave when the bow lies on its side");
    // limits, NaN
    t = Tuning{};
    t.radius = 5.0f; t.reach = 9.0f; t.start = 7.0f; t.spacing = 9.0f; t.offset[0] = 3.0f; t.offset[1] = -3.0f; t.offset[2] = NAN;
    const Tuning c = t.Clamped();
    EXPECT(c.radius == 0.060f && c.reach == 0.250f && c.start == 0.60f && c.spacing == 0.200f && c.offset[0] == 0.30f && c.offset[1] == -0.30f && c.offset[2] == 0.0f, "clamped");
    t = Tuning{};
    t.radius = 0.0f; t.reach = -1.0f; t.start = -1.0f; t.spacing = 0.0f;
    const Tuning d = t.Clamped();
    EXPECT(d.radius == 0.008f && d.reach == 0.020f && d.start == 0.0f && d.spacing == 0.020f, "clamped from below");
    t = Tuning{};
    t.radius = NAN; t.reach = NAN; t.start = NAN; t.spacing = NAN;
    const Tuning e = t.Clamped();
    EXPECT(std::fabs(e.radius - 0.022f) < 1e-6f && std::fabs(e.reach - 0.075f) < 1e-6f && std::fabs(e.start - 0.18f) < 1e-6f && std::fabs(e.spacing - 0.065f) < 1e-6f, "NaN: the defaults");
}

// The row turned about the bow's own axes (x across, y along the stave, z toward the archer), on its first orb.
void TestTurn() {
    std::puts("the row's turn");
    const Stave st = UprightStave();
    const Entry list[4] = {A(0), A(6), A(7), A(11)};
    Layout base, l;
    MakeLayout(st, list, 4, nullptr, Tuning{}, &base);
    const float d = base.spacing;
    // no turn is the row as it was, to the last bit (the defaults, and a Tuning that never heard of the turn)
    Tuning t;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l), "made");
    bool same = true;
    for (int i = 0; i < 4; ++i) same = same && l.orb[i].c.x == base.orb[i].c.x && l.orb[i].c.y == base.orb[i].c.y && l.orb[i].c.z == base.orb[i].c.z;
    EXPECT(same, "0, 0, 0: the row where it always was");
    // x +90: the far end of the row swings toward the archer (+z); the first orb stays
    t = Tuning{};
    t.turnDeg[0] = 90.0f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[0].c, base.orb[0].c), "x: the first orb is the pivot");
    for (int i = 1; i < 4; ++i) EXPECT(Near(l.orb[i].c, base.orb[0].c + Vec{0.0f, 0.0f, d * (float)i}), "x +90: orb %d stands %.1f cm toward the archer of the first", i, d * (float)i * 100.0f);
    t.turnDeg[0] = -90.0f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[2].c, base.orb[0].c + Vec{0.0f, 0.0f, -2.0f * d}), "x -90: away from the archer");
    // z +90: the far end swings to the left of an upright bow (-x); -90 to the right
    t = Tuning{};
    t.turnDeg[2] = 90.0f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[0].c, base.orb[0].c) && Near(l.orb[3].c, base.orb[0].c + Vec{-3.0f * d, 0.0f, 0.0f}), "z +90: the row lies to the left");
    t.turnDeg[2] = -90.0f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[1].c, base.orb[0].c + Vec{d, 0.0f, 0.0f}), "z -90: to the right");
    // y alone: a row along the stave does not move for a turn about the stave
    for (float deg : {-180.0f, -45.0f, 30.0f, 90.0f}) {
        t = Tuning{};
        t.turnDeg[1] = deg;
        bool still = MakeLayout(st, list, 4, nullptr, t, &l);
        for (int i = 0; i < 4; ++i) still = still && Near(l.orb[i].c, base.orb[i].c, 1e-5f);
        EXPECT(still, "y %.0f alone: the row stays where it is", deg);
    }
    // ... but it swings a tilted row round the stave: x +90 puts the row toward the archer, y +90 then takes it to the right (z toward x)
    t = Tuning{};
    t.turnDeg[0] = 90.0f; t.turnDeg[1] = 90.0f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[2].c, base.orb[0].c + Vec{2.0f * d, 0.0f, 0.0f}), "x 90, y 90: the tilted row swung round to the right");
    // the order is x, then y, then z: x 90 then z 90 leaves the row toward the archer (a turn about z does not move a row along z); the other way round it would lie to the left
    t = Tuning{};
    t.turnDeg[0] = 90.0f; t.turnDeg[2] = 90.0f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[1].c, base.orb[0].c + Vec{0.0f, 0.0f, d}), "x then z: toward the archer, not to the left");
    // a slanting turn: every orb is d*i from the first, on one line, and the spacing is the layout's
    t = Tuning{};
    t.turnDeg[0] = 30.0f; t.turnDeg[1] = -40.0f; t.turnDeg[2] = 55.0f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l), "made");
    const Vec line = bowdraw::Normalized(l.orb[3].c - l.orb[0].c);
    for (int i = 0; i < 4; ++i) {
        EXPECT(std::fabs(bowdraw::Length(l.orb[i].c - l.orb[0].c) - d * (float)i) < 1e-4f, "slanting: orb %d is %.1f cm from the first", i, d * (float)i * 100.0f);
        EXPECT(bowdraw::Length(Cross(l.orb[i].c - l.orb[0].c, line)) < 1e-4f, "slanting: orb %d on the line", i);
    }
    EXPECT(Near(l.orb[0].c, base.orb[0].c), "slanting: the first orb is the pivot");
    EXPECT(std::fabs(l.spacing - d) < 1e-6f && l.n == 4, "the layout's spacing and count are not the turn's business");
    // the pivot is the first orb where the shifts put it, and the turn is about the bow's own axes
    t = Tuning{};
    t.offset[0] = 0.05f; t.offset[1] = -0.03f; t.offset[2] = 0.04f;
    Layout shifted;
    MakeLayout(st, list, 4, nullptr, t, &shifted);
    t.turnDeg[0] = 90.0f;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[0].c, shifted.orb[0].c) && Near(l.orb[3].c, shifted.orb[0].c + Vec{0.0f, 0.0f, 3.0f * d}), "the shifts place the pivot, then the row turns on it");
    Stave lean = st;
    lean.tip = {-0.2f + 0.65f, -0.4f, -0.4f};   // lying on its side: the stave points right, across points down
    Layout leanBase;
    MakeLayout(lean, list, 4, nullptr, Tuning{}, &leanBase);
    t = Tuning{};
    t.turnDeg[2] = 90.0f;   // to the bow's left = world up here
    EXPECT(MakeLayout(lean, list, 4, nullptr, t, &l) && Near(l.orb[0].c, leanBase.orb[0].c) && Near(l.orb[2].c, leanBase.orb[0].c + Vec{0.0f, 2.0f * d, 0.0f}), "the turn follows the bow's axes when it lies on its side");
    // one orb: nothing to turn but the pivot
    t = Tuning{};
    t.turnDeg[0] = 70.0f; t.turnDeg[2] = 20.0f;
    EXPECT(MakeLayout(st, list, 1, nullptr, t, &l) && l.n == 1 && Near(l.orb[0].c, base.orb[0].c), "a single orb stays at the pivot");
    // the hand finds the orbs where they stand now
    t = Tuning{};
    t.turnDeg[0] = 90.0f;
    MakeLayout(st, list, 4, nullptr, t, &l);
    EXPECT(Hover(l, l.orb[3].c) == 3, "the hand at the turned fourth orb has it");
    float nearest = 0.0f;
    EXPECT(Hover(l, base.orb[3].c, &nearest) < 0, "the hand at the place it used to hang has nothing (%.1f cm from the nearest)", nearest * 100.0f);
    // limits, NaN
    t = Tuning{};
    t.turnDeg[0] = 400.0f; t.turnDeg[1] = -999.0f; t.turnDeg[2] = NAN;
    const Tuning c = t.Clamped();
    EXPECT(c.turnDeg[0] == 180.0f && c.turnDeg[1] == -180.0f && c.turnDeg[2] == 0.0f, "clamped to +-180, NaN is no turn");
    t = Tuning{};
    t.turnDeg[0] = INFINITY;
    EXPECT(MakeLayout(st, list, 4, nullptr, t, &l) && Near(l.orb[1].c, base.orb[1].c), "an infinite turn is no turn");
}

void TestFallback() {
    std::puts("the stave from the controller");
    // the bow hand upright, its knuckles pointing at the target (-Z): forward -Z, the stave up
    Stave s;
    const Vec forward = bowdraw::BowForward({0, 0, 0, 1});
    EXPECT(StaveFromHand({-0.2f, -0.4f, -0.4f}, forward, &s) && !s.fromModel, "made");
    EXPECT(Near(s.grip, {-0.2f, -0.4f, -0.4f}) && std::fabs(bowdraw::Length(s.tip - s.grip) - 0.65f) < 1e-4f, "65 cm");
    EXPECT(s.tip.y > s.grip.y + 0.6f && std::fabs(s.tip.x - s.grip.x) < 0.2f, "up");
    EXPECT(bowdraw::Dot(s.back, forward) < -0.9f, "the archer's side is behind the forward");
    EXPECT(!StaveFromHand({NAN, 0, 0}, forward, &s) && !StaveFromHand({0, 0, 0}, forward, nullptr) && !StaveFromHand({0, 0, 0}, {NAN, 0, 0}, &s), "NaN, no output");
    EXPECT(!StaveFromHand({0, 0, 0}, {0, 1, 0}, &s) && !StaveFromHand({0, 0, 0}, {0, 0, 0}, &s), "pointing straight up, or nowhere: no stave");
}

void TestGrab() {
    std::puts("the hand and its grip");
    const Stave st = UprightStave();
    const Entry list[4] = {A(0), A(6), A(7), A(11)};
    Layout l;
    MakeLayout(st, list, 4, nullptr, Tuning{}, &l);
    Grab g;
    GrabIn in;
    in.tracked = true;
    in.allowed = true;
    in.press = 0.0f;
    // far from the orbs: nothing
    in.hand = {0.2f, -0.2f, 0.0f};
    GrabOut o = g.Step(in, l);
    EXPECT(o.hover < 0 && !o.claim && o.pick < 0 && o.dist > 0.3f, "far away (%.2f m)", o.dist);
    EXPECT(Hover(l, l.orb[0].c + Vec{0.0f, 0.0f, 0.2f}) == -1, "20 cm off");
    // 6 cm from an orb: inside the 7.5 cm grab radius, though the orb is only 2.2 cm big
    float dist = 0.0f;
    EXPECT(Hover(l, l.orb[2].c + Vec{0.0f, 0.0f, 0.06f}, &dist) == 2 && std::fabs(dist - 0.06f) < 1e-4f, "6 cm from the third orb: it is the one the hand is at (%.3f)", dist);
    EXPECT(Hover(l, l.orb[2].c + Vec{0.0f, 0.0f, 0.09f}, &dist) == -1 && dist > 0.08f && dist < 0.10f, "9 cm: not yet, and the distance says how far (%.3f)", dist);
    // at orb 2: hover, claim, the grip is not down yet
    in.hand = l.orb[2].c + Vec{0.005f, 0.0f, 0.01f};
    o = g.Step(in, l);
    EXPECT(o.hover == 2 && o.claim && o.pick < 0 && o.dist < 0.02f, "at the third orb");
    // the grip: one pick
    in.press = 0.8f;
    o = g.Step(in, l);
    EXPECT(o.pick == 2 && o.claim, "grip pressed: the third orb is taken");
    o = g.Step(in, l);
    EXPECT(o.pick < 0 && o.claim, "held: no second pick");
    // let go (below 0.2), press again: another pick
    in.press = 0.1f;
    g.Step(in, l);
    in.press = 0.6f;
    EXPECT(g.Step(in, l).pick == 2, "pressed again");
    // between 0.2 and 0.55 after a press is still down; 0.55 after a let-go is down
    in.press = 0.3f;
    EXPECT(g.Step(in, l).pick < 0, "0.3 after a press: still down");
    in.press = 0.6f;
    EXPECT(g.Step(in, l).pick < 0, "... and no press from 0.3 to 0.6");
    in.press = 0.0f;
    g.Step(in, l);
    in.press = 0.5f;
    EXPECT(g.Step(in, l).pick < 0, "0.5 is not down");
    // a grip held as the hand comes in takes nothing
    Grab h;
    GrabIn held = in;
    held.hand = {0.3f, -0.2f, 0.0f};
    held.press = 0.9f;
    h.Step(held, l);
    held.hand = l.orb[1].c;
    GrabOut ho = h.Step(held, l);
    EXPECT(ho.hover == 1 && ho.claim && ho.pick < 0, "gripped on the way in: claimed, nothing taken");
    held.press = 0.0f;
    h.Step(held, l);
    held.press = 0.9f;
    EXPECT(h.Step(held, l).pick == 1, "let go and pressed at the orb: taken");
    // the nearest wins where two reaches overlap
    in.press = 0.0f;
    g.Step(in, l);
    in.hand = (l.orb[1].c + l.orb[2].c) * 0.5f + Vec{0.0f, 0.004f, 0.0f};
    o = g.Step(in, l);
    EXPECT(o.hover == 2, "between two orbs: the nearer (the upper by 4 mm)");
    in.hand = (l.orb[1].c + l.orb[2].c) * 0.5f - Vec{0.0f, 0.004f, 0.0f};
    EXPECT(g.Step(in, l).hover == 1, "... and the other way");
    // not allowed (a menu, drawn): nothing, nothing claimed; untracked: the same
    in.allowed = false;
    in.hand = l.orb[2].c;
    in.press = 0.9f;
    o = g.Step(in, l);
    EXPECT(o.hover < 0 && !o.claim && o.pick < 0, "not allowed");
    in.allowed = true;
    in.tracked = false;
    o = g.Step(in, l);
    EXPECT(o.hover < 0 && !o.claim && o.pick < 0, "untracked");
    // ... and a grip pressed while not allowed is no press when it comes back
    in.tracked = true;
    o = g.Step(in, l);
    EXPECT(o.pick < 0, "the grip was already down when it came back");
    // an unknown grip: nothing
    in.press = -1.0f;
    EXPECT(g.Step(in, l).pick < 0, "unknown grip");
    // no orbs: nothing
    Layout none;
    in.press = 0.0f;
    g.Step(in, none);
    in.press = 0.9f;
    EXPECT(g.Step(in, none).pick < 0 && Hover(none, {0, 0, 0}) == -1, "no orbs");
    EXPECT(Hover(l, {NAN, 0, 0}) == -1, "NaN hand");
}

// The trigger as the pick control: a press that took an orb stays the orb's until it is let go, wherever the hand goes (the game's attack must not see it).
void TestLatch() {
    std::puts("a press that took an orb");
    const Stave st = UprightStave();
    const Entry list[4] = {A(0), A(6), A(7), A(11)};
    Layout l;
    MakeLayout(st, list, 4, nullptr, Tuning{}, &l);
    const Vec away{0.3f, -0.2f, 0.0f};
    Grab g;
    GrabIn in;
    in.tracked = true;
    in.allowed = true;
    in.latch = true;
    in.hand = l.orb[1].c;
    in.press = 0.0f;
    g.Step(in, l);
    in.press = 0.9f;
    GrabOut o = g.Step(in, l);
    EXPECT(o.pick == 1 && o.claim, "the trigger pressed at the second orb: taken");
    // the hand leaves the orb with the trigger down: it is still the orb's, nothing more is taken
    in.hand = away;
    o = g.Step(in, l);
    EXPECT(o.hover < 0 && o.claim && o.pick < 0, "left the orb, the trigger still down: still claimed, no pick");
    in.hand = l.orb[2].c;
    o = g.Step(in, l);
    EXPECT(o.hover == 2 && o.claim && o.pick < 0, "over another orb with it down: no second pick");
    in.hand = away;
    in.press = 0.3f;
    EXPECT(g.Step(in, l).claim, "between 0.2 and 0.55 it is still down");
    // let go: the claim goes at once, and a new press away from the orbs is nobody's
    in.press = 0.1f;
    o = g.Step(in, l);
    EXPECT(!o.claim && o.pick < 0, "let go: not claimed");
    in.press = 0.9f;
    o = g.Step(in, l);
    EXPECT(!o.claim && o.pick < 0, "pressed again away from the orbs: not claimed");
    // without the latch (the grip, as it was) the claim is the hand's being at an orb, no more
    Grab h;
    GrabIn nl = in;
    nl.latch = false;
    nl.hand = l.orb[1].c;
    nl.press = 0.0f;
    h.Step(nl, l);
    nl.press = 0.9f;
    EXPECT(h.Step(nl, l).pick == 1, "the grip pressed at an orb: taken");
    nl.hand = away;
    o = h.Step(nl, l);
    EXPECT(!o.claim && o.pick < 0, "no latch: out of the zone it is not claimed");
    // a press that began outside does not latch: claimed over an orb (nothing taken), free again outside
    Grab k;
    GrabIn out = in;
    out.hand = away;
    out.press = 0.9f;
    k.Step(out, l);
    out.hand = l.orb[1].c;
    o = k.Step(out, l);
    EXPECT(o.hover == 1 && o.claim && o.pick < 0, "held on the way in: the orb's, nothing taken");
    out.hand = away;
    EXPECT(!k.Step(out, l).claim, "... and not latched once it is out again");
    // tracking lost or not allowed while held ends the latch
    Grab m;
    GrabIn lost = in;
    lost.hand = l.orb[1].c;
    lost.press = 0.0f;
    m.Step(lost, l);
    lost.press = 0.9f;
    m.Step(lost, l);
    lost.hand = away;
    EXPECT(m.Step(lost, l).claim, "held, out of the zone: claimed");
    lost.tracked = false;
    m.Step(lost, l);
    lost.tracked = true;
    EXPECT(!m.Step(lost, l).claim, "tracking was lost: the latch is gone");
    // a trigger that is not known claims nothing
    Grab u;
    GrabIn unk = in;
    unk.hand = l.orb[1].c;
    unk.press = -1.0f;
    o = u.Step(unk, l);
    EXPECT(!o.claim && o.pick < 0 && o.hover < 0, "an unknown control: nothing");
}

// The orbs never go; what waits is the taking: a hand may take one only when the string is not drawn and the draw ended more than 0.6 s ago, and a frame that cannot read the stave keeps the last
// good row for half a second.
void TestWhen() {
    std::puts("when an orb is taken, and a row that is held");
    // at rest long after a draw: takeable
    EXPECT(Settled(false, 5.0), "at rest: takeable");
    // the string drawn: not takeable, however old the last end
    EXPECT(!Settled(true, 0.0) && !Settled(true, 5.0) && !Settled(true, 1e9), "drawn: not takeable");
    // just let go: not takeable for 0.6 s (the hand swings past the bow), strictly more than that
    EXPECT(!Settled(false, 0.0) && !Settled(false, 0.3) && !Settled(false, 0.6) && Settled(false, 0.61), "after the draw: takeable once more than 0.6 s are behind it");
    for (double t : {0.0, 0.3, 0.6, 0.7, 5.0}) EXPECT(Settled(false, t) == (t > 0.6), "takeable at %.1f s after: %s", t, Settled(false, t) ? "yes" : "no");
    // a frame that made no row: the last good one is kept for half a second, never one that never was
    EXPECT(Holds(true, 0.0) && Holds(true, 0.1) && Holds(true, 0.49), "a row that was good a moment ago is held");
    EXPECT(!Holds(true, 0.5) && !Holds(true, 2.0) && !Holds(true, 1e9), "... for half a second, then it goes (the stave is not read at all)");
    EXPECT(!Holds(false, 0.0) && !Holds(false, 0.1), "no row was ever good: nothing to hold");
    EXPECT(!Holds(true, -1.0) && !Holds(true, -1e-9) && !Holds(true, NAN), "a time that went back, or is not a time: not held");
    EXPECT(kHoldS >= 0.2 && kHoldS <= 1.0, "a moment, not a hold-over that outlives the stave (%.2f s)", kHoldS);
}

void TestApproach() {
    std::puts("the orb ahead of the grab");
    const Stave st = UprightStave();
    const Entry list[4] = {A(0), A(6), A(7), A(11)};
    Layout l;
    MakeLayout(st, list, 4, nullptr, Tuning{}, &l);
    // the hand at the third orb: that one is 1, the neighbours a ramp of at most half, the far ones 0
    Vec hand = l.orb[2].c + Vec{0.0f, 0.0f, 0.02f};
    const int at = Hover(l, hand);
    Approach(&l, hand, at);
    EXPECT(at == 2 && l.orb[2].hover == 1.0f, "the one the hand is at is fully reached");
    EXPECT(l.orb[1].hover > 0.0f && l.orb[1].hover <= 0.5f && l.orb[3].hover > 0.0f && l.orb[3].hover <= 0.5f, "the neighbours are being neared: %.2f %.2f", l.orb[1].hover, l.orb[3].hover);
    EXPECT(l.orb[0].hover < l.orb[1].hover, "the farther one less (%.2f < %.2f)", l.orb[0].hover, l.orb[1].hover);
    // the hand coming up to an orb: the ramp starts at twice the grab radius and rises
    hand = l.orb[3].c + Vec{0.0f, 0.0f, 0.20f};
    Approach(&l, hand, -1);
    EXPECT(l.orb[3].hover == 0.0f, "20 cm off: nothing");
    float last = 0.0f;
    bool rising = true;
    for (float away = 0.15f; away >= 0.074f; away -= 0.015f) {
        Approach(&l, l.orb[3].c + Vec{0.0f, 0.0f, away}, -1);
        rising = rising && l.orb[3].hover >= last;
        last = l.orb[3].hover;
    }
    EXPECT(rising && last > 0.35f && last <= 0.5f, "it rises as the hand comes (to %.2f at the edge of the grab radius, not yet taken)", last);
    Approach(&l, {NAN, 0, 0}, -1);
    EXPECT(l.orb[0].hover == 0.0f, "NaN hand: no ramp");
    Approach(nullptr, hand, -1);
}

}  // namespace

int main() {
    TestLayout();
    TestTuning();
    TestTurn();
    TestFallback();
    TestGrab();
    TestLatch();
    TestWhen();
    TestApproach();
    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}
