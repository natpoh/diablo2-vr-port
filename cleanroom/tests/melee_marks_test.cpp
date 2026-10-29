// Offline checks of the speed marks of [debug] melee_boxes (vr/melee_marks.h, and how melee_boxes.h puts them in the scene): where the
// stroke engine's "a contact would count" flag turned on and off, the tip's trail coloured by it, the budget of capsule places they share
// with the monsters, and the module's [debug] melee_mark_ms. No game, no headset.
//
//   melee_marks_test.exe      the checks (exit code 0 = all passed)

#include "../../vr/melee_boxes.h"
#include "../../vr/melee_marks.h"
#include "../../vr/stroke_tuning.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace stroke;
using namespace meleeboxes;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

static Vec P(float x) { return {x, 1.0f, 0.0f}; }

// ---- the crossing detector ----------------------------------------------------------------------------------------------------------
static void CrossingTests() {
    // A tip going along x at 3 m/s, a pose every 10 ms; a contact counts for poses 20..49 only.
    {
        Track t;
        for (int i = 0; i < 71; ++i) t.Feed(static_cast<uint64_t>(i) * 10, P(0.03f * static_cast<float>(i)), i >= 20 && i <= 49);
        Mark m[kMarkSlots];
        size_t n = t.Marks(700, kDefaultMarkMs, m, kMarkSlots);
        Expect(n == 2, "marks: one where the speed was reached and one where it fell below again", static_cast<double>(n));
        if (n == 2) {
            Expect(!m[0].reached && m[1].reached, "marks: newest first - slowed, then reached");
            Expect(m[1].ms == 200 && Near(m[1].at.x, 0.6f, 1e-5f), "marks: reached sits where the flag TURNED (pose 20), not where it was confirmed", m[1].at.x);
            Expect(m[0].ms == 500 && Near(m[0].at.x, 1.5f, 1e-5f), "marks: slowed sits where the flag fell (pose 50)", m[0].at.x);
            Expect(Near(m[1].at.y, 1.0f, 1e-6f), "marks: at the tip's height");
        }
        // they stay for the time asked: young while now - ms < keep
        Expect(t.Marks(999, 800, m, kMarkSlots) == 2, "marks: both still there 799 ms after the first");
        n = t.Marks(1000, 800, m, kMarkSlots);
        Expect(n == 1 && !m[0].reached, "marks: the first is gone after 800 ms, the second stays", static_cast<double>(n));
        Expect(t.Marks(1300, 800, m, kMarkSlots) == 0, "marks: both gone after 800 ms");
        Expect(t.Marks(700, 0, m, kMarkSlots) == 0, "marks: a time of 0 shows none");
        Expect(t.Marks(700, 5000, m, 1) == 1 && !m[0].reached, "marks: the room asked for caps them, the newest kept");
        Expect(t.Marks(100, 800, m, kMarkSlots) == 0, "marks: nothing yet before the first mark's time");
    }
    // A flicker around the threshold leaves nothing: the new state must hold 30 ms.
    {
        Track t;
        t.Feed(0, P(0.0f), false);
        t.Feed(100, P(0.1f), true);
        t.Feed(110, P(0.2f), true);
        t.Feed(120, P(0.3f), true);       // 20 ms in: not yet
        t.Feed(130, P(0.4f), false);      // back: the pending turn is dropped
        t.Feed(140, P(0.5f), false);
        Mark m[kMarkSlots];
        Expect(t.Marks(200, 800, m, kMarkSlots) == 0, "flicker: 20 ms of 'counting' leaves no mark");
        Track u;
        u.Feed(0, P(0.0f), false);
        for (int i = 0; i <= 3; ++i) u.Feed(100 + static_cast<uint64_t>(i) * 10, P(0.1f * static_cast<float>(i + 1)), true);   // 100..130: held 30 ms
        const size_t n = u.Marks(200, 800, m, kMarkSlots);
        Expect(n == 1 && m[0].reached && m[0].ms == 100 && Near(m[0].at.x, 0.1f, 1e-5f), "flicker: 30 ms held leaves a mark where it began", static_cast<double>(n));
        // false, true 20 ms, false 20 ms, true 40 ms: only the last stretch counts
        Track v;
        v.Feed(0, P(0.0f), false);
        v.Feed(10, P(0.1f), true); v.Feed(20, P(0.2f), true); v.Feed(30, P(0.3f), false);
        v.Feed(40, P(0.4f), false); v.Feed(50, P(0.5f), false);
        v.Feed(60, P(0.6f), true); v.Feed(70, P(0.7f), true); v.Feed(80, P(0.8f), true); v.Feed(90, P(0.9f), true);
        const size_t nv = v.Marks(200, 800, m, kMarkSlots);
        Expect(nv == 1 && m[0].reached && m[0].ms == 60, "flicker: a stutter then a held turn is one mark, at the held turn's beginning", static_cast<double>(nv));
    }
    // The first pose is the baseline: a blade that appears already counting has no 'reached' to show, only its later slowing.
    {
        Track t;
        for (int i = 0; i < 10; ++i) t.Feed(static_cast<uint64_t>(i) * 10, P(0.03f * static_cast<float>(i)), true);
        for (int i = 10; i < 20; ++i) t.Feed(static_cast<uint64_t>(i) * 10, P(0.03f * static_cast<float>(i)), false);
        Mark m[kMarkSlots];
        const size_t n = t.Marks(250, 800, m, kMarkSlots);
        Expect(n == 1 && !m[0].reached && m[0].ms == 100, "baseline: only the slowing is marked", static_cast<double>(n));
    }
    // Many swings: the newest kMarkSlots stay, newest first.
    {
        Track t;
        uint64_t ms = 0;
        int i = 0;
        for (int swing = 0; swing < 8; ++swing) {
            for (int k = 0; k < 5; ++k, ms += 10, ++i) t.Feed(ms, P(0.03f * static_cast<float>(i)), false);
            for (int k = 0; k < 5; ++k, ms += 10, ++i) t.Feed(ms, P(0.03f * static_cast<float>(i)), true);
        }
        Mark m[kMarkSlots];
        const size_t n = t.Marks(ms, 5000, m, kMarkSlots);
        Expect(n == kMarkSlots && t.MarkCount() == kMarkSlots, "ring: the newest few marks are kept", static_cast<double>(n));
        bool ordered = true;
        for (size_t k = 1; k < n; ++k) ordered = ordered && m[k - 1].ms > m[k].ms;
        Expect(ordered, "ring: newest first");
    }
    // Rubbish in, nothing out: a tip that is not finite is ignored; time going back starts afresh.
    {
        Track t;
        t.Feed(0, P(0.0f), false);
        t.Feed(10, {std::nanf(""), 0.0f, 0.0f}, true);
        Expect(t.PointCount() == 1, "rubbish: a tip that is not a number is ignored", static_cast<double>(t.PointCount()));
        for (int i = 1; i < 10; ++i) t.Feed(static_cast<uint64_t>(i) * 10, P(0.03f * static_cast<float>(i)), i >= 3);
        Mark m[kMarkSlots];
        Expect(t.Marks(200, 800, m, kMarkSlots) == 1, "rubbish: the real turn still marked");
        t.Feed(5, P(0.0f), false);   // time went back
        Expect(t.PointCount() == 1 && t.MarkCount() == 0, "rubbish: time going back clears the track", static_cast<double>(t.PointCount()));
    }
}

// ---- the trail ------------------------------------------------------------------------------------------------------------------------
static void TrailTests() {
    // 3 m/s along x for 0.7 s, a pose every 10 ms (3 cm); a contact counts from x = 1.02 m (pose 34) on.
    Track t;
    for (int i = 0; i < 70; ++i) t.Feed(static_cast<uint64_t>(i) * 10, P(0.03f * static_cast<float>(i)), i >= 34);
    TrailSegment seg[64];
    const size_t n = t.Trail(700, 800, seg, 64);
    Expect(n >= 8 && n <= static_cast<size_t>(kTrailSegments), "trail: thinned to chords, within the per-blade segments", static_cast<double>(n));
    if (n >= 2) {
        bool chain = true;
        for (size_t k = 1; k < n; ++k) chain = chain && Near(seg[k - 1].a.x, seg[k].b.x, 1e-5f) && Near(seg[k - 1].a.y, seg[k].b.y, 1e-5f);
        Expect(chain, "trail: the segments join up, newest first");
        Expect(Near(seg[0].b.x, 0.03f * 69.0f, 1e-5f), "trail: it ends at the tip's latest place", seg[0].b.x);
        Expect(Near(seg[n - 1].a.x, 0.0f, 1e-5f), "trail: it begins at the oldest pose still young enough", seg[n - 1].a.x);
        int hotWrong = 0, coldWrong = 0, boundary = 0;
        for (size_t k = 0; k < n; ++k) {
            if (seg[k].a.x >= 1.02f - 1e-4f) hotWrong += !seg[k].hot;     // starts where the flag is on: green
            if (seg[k].a.x < 0.99f - 1e-4f) coldWrong += seg[k].hot;      // starts before: cyan
            if (Near(seg[k].a.x, 0.99f, 1e-4f) && Near(seg[k].b.x, 1.02f, 1e-4f) && !seg[k].hot) ++boundary;
        }
        Expect(hotWrong == 0 && coldWrong == 0, "trail: cyan before the flag turned on, green from there", hotWrong + coldWrong);
        Expect(boundary == 1, "trail: the colour boundary is the pose pair around the turn (never a chord across it)", boundary);
        bool longest = true;
        for (size_t k = 0; k < n; ++k) longest = longest && Length(seg[k].b - seg[k].a) <= kTrailChordM + 0.03f + 1e-4f;
        Expect(longest, "trail: no chord longer than the chord length plus one pose");
    }
    // only the young part
    const size_t young = t.Trail(700, 300, seg, 64);
    Expect(young >= 2, "trail: the last 300 ms still has a trail", static_cast<double>(young));
    if (young >= 1) Expect(Near(seg[young - 1].a.x, 0.03f * 41.0f, 1e-4f), "trail: it starts at the oldest pose younger than 300 ms", seg[young - 1].a.x);
    Expect(t.Trail(1600, 800, seg, 64) == 0, "trail: nothing is left a second after the swing");
    Expect(t.Trail(700, 0, seg, 64) == 0, "trail: a time of 0 shows none");
    // the room: the newest are kept
    const size_t few = t.Trail(700, 800, seg, 4);
    Expect(few == 4 && Near(seg[0].b.x, 0.03f * 69.0f, 1e-5f), "trail: with room for four, the newest four", static_cast<double>(few));
    // a hand at rest adds nothing
    Track still;
    for (int i = 0; i < 100; ++i) still.Feed(static_cast<uint64_t>(i) * 10, P(0.5f), false);
    Expect(still.PointCount() == 1 && still.Trail(1000, 800, seg, 64) == 0, "trail: a tip at rest leaves a point, no trail", static_cast<double>(still.PointCount()));
    // a flip of the flag without moving makes a point, but no segment to draw
    still.Feed(1000, P(0.5f), true);
    Expect(still.PointCount() == 2 && still.Trail(1000, 800, seg, 64) == 0, "trail: a flip at rest is a point without a segment", static_cast<double>(still.PointCount()));
    // the ring forgets the oldest
    Track longRun;
    for (int i = 0; i < 400; ++i) longRun.Feed(static_cast<uint64_t>(i) * 2, P(0.02f * static_cast<float>(i)), false);
    Expect(longRun.PointCount() == kTrailPoints, "trail: the ring holds a fixed number of points", static_cast<double>(longRun.PointCount()));
}

// ---- the scene: the marks among the capsules --------------------------------------------------------------------------------------------
static Blade MakeBlade(float x) { return {{x, 1.0f, 0.0f}, {x, 1.0f, 0.65f}, 0.016f}; }

static Frame MakeFrame(uint64_t now, float x, bool counting, const Monster* mons = nullptr, size_t n = 0) {
    Frame f;
    f.nowMs = now;
    f.upm = 4.0f;
    f.blades[0].have = true;
    f.blades[0].side = 0;
    f.blades[0].blade = MakeBlade(x);
    f.blades[0].inStroke = true;
    f.blades[0].counting = counting;
    f.monsters = mons;
    f.count = n;
    return f;
}

static int Count(const Capsule* c, int n, Kind k) {
    int r = 0;
    for (int i = 0; i < n; ++i) r += c[i].kind == k;
    return r;
}

static Monster MakeMonster(uint32_t id, float x) {
    Monster m;
    m.id = id;
    m.cls = 5;
    m.d2 = x * x;
    m.body = {{x, 0.4f, 3.0f}, {x, 1.6f, 3.0f}, 0.4f};
    m.verdict = Verdict::Hostile;
    return m;
}

static void SceneTests() {
    // a swing through the scene: poses 10 ms apart, the contact counting for poses 20..49
    {
        Scene s;
        uint64_t last = 0;
        for (int i = 0; i < 71; ++i) {
            last = 1000 + static_cast<uint64_t>(i) * 10;
            s.Set(MakeFrame(last, 0.03f * static_cast<float>(i), i >= 20 && i <= 49));
        }
        Capsule c[kMaxCapsules];
        int n = s.Capsules(last, c, kMaxCapsules);
        Expect(Count(c, n, kMarkReached) == 1 && Count(c, n, kMarkSlowed) == 1, "scene: one ball where the speed was reached, one where it fell", n);
        Expect(Count(c, n, kTrailCold) > 0 && Count(c, n, kTrailHot) > 0, "scene: the trail has both colours");
        Expect(c[0].kind == kBlade, "scene: the blade first (cold now), the marks after it");
        // the balls: a short vertical stub of radius 3.5 cm, centred on the tip
        for (int i = 0; i < n; ++i) {
            if (c[i].kind != kMarkReached && c[i].kind != kMarkSlowed) continue;
            const Vec mid = (c[i].a + c[i].b) * 0.5f;
            Expect(Near(c[i].radius, kMarkRadiusM, 1e-6f) && Near(Length(c[i].b - c[i].a), 2.0f * kMarkHalfM, 1e-6f) && Near(c[i].a.x, c[i].b.x, 1e-6f), "scene: a ball is a short vertical stub");
            Expect(Near(mid.z, 0.65f, 1e-5f), "scene: the ball is at the blade's TIP (the tip is 0.65 m ahead)", mid.z);
            if (c[i].kind == kMarkReached) Expect(Near(mid.x, 0.6f, 1e-5f), "scene: reached at the tip where the flag turned", mid.x);
            else Expect(Near(mid.x, 1.5f, 1e-5f), "scene: slowed at the tip where the flag fell", mid.x);
        }
        for (int i = 0; i < n; ++i)
            if (c[i].kind == kTrailCold || c[i].kind == kTrailHot) Expect(Near(c[i].radius, kTrailRadiusM, 1e-6f), "scene: the trail is a thin tube");
        // order: the marks are the last, on top of the trail
        Expect(c[n - 1].kind == kMarkReached || c[n - 1].kind == kMarkSlowed, "scene: the balls are drawn last, on top");
        // the pack: the kind reaches the shader
        float a[kMaxCapsules][4], b[kMaxCapsules][4];
        const float eye[3] = {0.0f, 0.0f, 0.0f};
        const int k = Pack(c, n, 4.0f, eye, a, b);
        Expect(k == n, "scene: every mark packs", k);
        bool kinds = false;
        for (int i = 0; i < k; ++i) kinds = kinds || b[i][3] == static_cast<float>(kMarkReached);
        Expect(kinds, "scene: the kind is the shader's colour number");
        // a scene that is no longer fed is not drawn at all; one that goes on being fed shows the swing's marks only as long as asked for
        n = s.Capsules(last + kStaleMs + 1, c, kMaxCapsules);
        Expect(n == 0, "scene: a scene older than 300 ms draws nothing at all");
        s.Set(MakeFrame(last + 900, 2.1f, false));   // the feed goes on: the hand rests (the balls were made 1.4 s and 1.1 s ago)
        n = s.Capsules(last + 900, c, kMaxCapsules);
        Expect(Count(c, n, kMarkReached) == 0 && Count(c, n, kMarkSlowed) == 0 && Count(c, n, kTrailHot) == 0, "scene: 900 ms after the swing the marks and the trail are gone", n);
    }
    // ... and a time of 0 draws no marks at all, only the colour of the blade
    {
        Scene s;
        uint64_t last = 0;
        for (int i = 0; i < 71; ++i) {
            last = 1000 + static_cast<uint64_t>(i) * 10;
            Frame f = MakeFrame(last, 0.03f * static_cast<float>(i), i >= 20 && i <= 49);
            f.markMs = 0;
            s.Set(f);
        }
        Capsule c[kMaxCapsules];
        const int n = s.Capsules(last, c, kMaxCapsules);
        Expect(n == 1 && c[0].kind == kBlade, "off: a time of 0 leaves the blade alone", n);
    }
    // the blade goes away: its marks go with it; another hand's blade in that slot starts afresh
    {
        Scene s;
        for (int i = 0; i < 40; ++i) s.Set(MakeFrame(1000 + static_cast<uint64_t>(i) * 10, 0.03f * static_cast<float>(i), i >= 10));
        Capsule c[kMaxCapsules];
        int n = s.Capsules(1390, c, kMaxCapsules);
        Expect(Count(c, n, kMarkReached) == 1, "slot: the swing has its ball", n);
        Frame other = MakeFrame(1400, 0.0f, false);
        other.blades[0].side = 1;   // the other hand's weapon now fills the slot
        s.Set(other);
        n = s.Capsules(1400, c, kMaxCapsules);
        Expect(Count(c, n, kMarkReached) == 0 && Count(c, n, kTrailHot) == 0 && Count(c, n, kTrailCold) == 0, "slot: another hand's blade starts without them", n);
        Frame none = MakeFrame(1410, 0.0f, false);
        none.blades[0].have = false;
        s.Set(none);
        n = s.Capsules(1410, c, kMaxCapsules);
        Expect(n == 0, "slot: no blade, nothing", n);
    }
    // the places: the marks and the trail take what the nearest monsters leave
    {
        std::vector<Monster> crowd;
        for (uint32_t i = 0; i < 40; ++i) crowd.push_back(MakeMonster(100 + i, 1.0f + 0.5f * static_cast<float>(i)));
        Scene s;
        uint64_t last = 0;
        for (int i = 0; i < 71; ++i) {
            last = 1000 + static_cast<uint64_t>(i) * 10;
            s.Set(MakeFrame(last, 0.03f * static_cast<float>(i), i >= 20 && i <= 49, crowd.data(), crowd.size()));
        }
        s.Struck(139, kStruckKey, last);
        Capsule c[kMaxCapsules];
        const int n = s.Capsules(last, c, kMaxCapsules);
        Expect(n == kMaxCapsules, "places: full", n);
        Expect(Count(c, n, kMarkReached) == 1 && Count(c, n, kMarkSlowed) == 1, "places: both balls kept in a crowd", n);
        Expect(Count(c, n, kTrailCold) + Count(c, n, kTrailHot) >= 8, "places: the trail kept in a crowd");
        int near = 0;
        for (int i = 0; i < n; ++i)
            for (int k = 0; k < 3; ++k)
                if (c[i].kind == kMonster && Near(c[i].a.x, crowd[static_cast<size_t>(k)].body.bottom.x, 1e-6f)) ++near;
        Expect(near == 3, "places: the nearest three monsters are still drawn", near);
        Expect(c[n - 1].kind == kMarkReached || c[n - 1].kind == kMarkSlowed, "places: the balls on top");
        // two blades swinging and four struck monsters: the nearest three still fit
        Frame f = MakeFrame(last + 10, 2.1f, true, crowd.data(), crowd.size());
        f.blades[1] = f.blades[0];
        f.blades[1].side = 1;
        s.Set(f);
        for (uint32_t i = 0; i < 4; ++i) s.Struck(136 + i, kStruckKey, last + 10);
        const int n2 = s.Capsules(last + 10, c, kMaxCapsules);
        int near2 = 0;
        for (int i = 0; i < n2; ++i)
            for (int k = 0; k < 3; ++k)
                if (c[i].kind == kMonster && Near(c[i].a.x, crowd[static_cast<size_t>(k)].body.bottom.x, 1e-6f)) ++near2;
        Expect(n2 <= kMaxCapsules && near2 == 3, "places: two blades, four struck, marks and trail - the nearest three still drawn", near2);
    }
}

// ---- the engine's own flag --------------------------------------------------------------------------------------------------------------
// A real engine driven by a swing (rest, a push to 4 m/s, a hold, a stop): the ball of 'reached' is where the engine's Counting turned on, and
// the ball of 'slowed' where it turned off - the marks follow the engine's gate, not an estimate of their own.
static void EngineTests() {
    for (const float minSpeed : {0.8f, 2.5f}) {
        stroke::Engine eng(stroke::ParamsFor({minSpeed, 8.0f}));
        Scene scene;
        double t = 1.0;
        Vec hand{0.0f, 1.0f, 0.0f};
        bool was = false;
        Vec firstOn{}, lastOff{};
        bool haveOn = false, haveOff = false;
        uint64_t last = 0;
        for (int i = 0; i < 150; ++i) {
            // 0..29 rest, 30..59 push up to 4 m/s, 60..89 hold, 90..109 slow to nothing, then rest
            float v = 0.0f;
            if (i >= 30 && i < 60) v = 4.0f * static_cast<float>(i - 29) / 30.0f;
            else if (i >= 60 && i < 90) v = 4.0f;
            else if (i >= 90 && i < 110) v = 4.0f * static_cast<float>(110 - i) / 20.0f;
            t += 1.0 / 90.0;
            hand.x += v / 90.0f;
            Sample s;
            s.time = t; s.active = true; s.epoch = 1; s.context = 1;
            s.hand = {hand, {}};
            s.blade = {hand, hand + Vec{0.0f, 0.0f, 0.65f}, 0.04f};
            s.anchor = {hand.x, 1.0f, 0.0f};
            s.scale = 4.0f;
            Sweep sw;
            eng.Step(s, sw);
            const bool on = eng.Counting();
            if (on && !was && !haveOn) { firstOn = s.blade.tip; haveOn = true; }
            if (!on && was) { lastOff = s.blade.tip; haveOff = true; }
            was = on;
            last = 1000 + static_cast<uint64_t>(std::llround((t - 1.0) * 1000.0));
            Frame f = MakeFrame(last, 0.0f, on);
            f.markMs = 5000;   // the whole 1.6 s run stays on show
            f.blades[0].blade = s.blade;
            f.blades[0].inStroke = eng.InStroke();
            f.blades[0].speed = eng.LastSpeed();
            scene.Set(f);
        }
        Capsule c[kMaxCapsules];
        const int n = scene.Capsules(last, c, kMaxCapsules);
        Expect(haveOn && haveOff, "engine: the swing began and ended counting", minSpeed);
        // the first 'reached' ball is the oldest one: its centre is the tip at the first pose the engine counted
        Vec reached{}, slowed{};
        bool gotReached = false, gotSlowed = false;
        for (int i = 0; i < n; ++i) {
            const Vec mid = (c[i].a + c[i].b) * 0.5f;
            if (c[i].kind == kMarkReached) { reached = mid; gotReached = true; }
            if (c[i].kind == kMarkSlowed) { slowed = mid; gotSlowed = true; }
        }
        Expect(gotReached && gotSlowed, "engine: a swing leaves both balls", minSpeed);
        if (gotReached && haveOn) Expect(Length(reached - firstOn) < 1e-4f, "engine: the 'reached' ball is at the tip where Counting turned on", Length(reached - firstOn));
        if (gotSlowed && haveOff) Expect(Length(slowed - lastOff) < 1e-4f, "engine: the 'slowed' ball is at the tip where Counting turned off", Length(slowed - lastOff));
        // a higher least speed is reached later in the push, so the ball sits farther along the swing
        static float firstX = 0.0f;
        if (minSpeed < 1.0f) firstX = reached.x;
        else Expect(reached.x > firstX + 0.02f, "engine: a higher least speed is reached farther along the same push", reached.x - firstX);
    }
}

// ---- the module: [debug] melee_mark_ms -----------------------------------------------------------------------------------------------------
static void ModuleTests() {
    SetWanted(true);
    float a[kMaxCapsules][4], b[kMaxCapsules][4];
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    auto swing = [&](uint64_t from) {
        for (int i = 0; i < 71; ++i) Update(MakeFrame(from + static_cast<uint64_t>(i) * 10, 0.03f * static_cast<float>(i), i >= 20 && i <= 49));
    };
    // how many of what the effect gets are balls (colours 7, 8) / are anything of the marks (colours 7..10)
    auto balls = [&](uint64_t now) {
        const int n = Take(now, eye, a, b);
        int k = 0;
        for (int i = 0; i < n; ++i) k += b[i][3] > 6.5f && b[i][3] < 8.5f;
        return k;
    };
    auto anyMark = [&](uint64_t now) {
        const int n = Take(now, eye, a, b);
        int k = 0;
        for (int i = 0; i < n; ++i) k += b[i][3] > 6.5f;
        return k;
    };
    SetMarkMs(800);
    swing(1000);
    Expect(balls(1700) == 2 && anyMark(1700) > 2, "module: with the default time the swing's balls and trail reach the effect", balls(1700));
    SetWanted(false);
    SetWanted(true);
    SetMarkMs(0);
    swing(5000);
    Expect(anyMark(5700) == 0, "module: a time of 0 sends no marks and no trail", anyMark(5700));
    SetWanted(false);
    SetWanted(true);
    SetMarkMs(4000000000u);   // clamped to kMaxMarkMs (5 s): the marks, made at 1200 and 1500 ms, are young at 1700 + 3000 ...
    for (int i = 0; i < 71; ++i) Update(MakeFrame(1000 + static_cast<uint64_t>(i) * 10, 0.03f * static_cast<float>(i), i >= 20 && i <= 49));
    Update(MakeFrame(1700 + 3000, 2.1f, false));   // the feed keeps on, the hand rests
    Expect(balls(1700 + 3000) == 2, "module: a long time keeps the balls for seconds", balls(1700 + 3000));
    Update(MakeFrame(1700 + 4900, 2.1f, false));   // ... and gone by 1700 + 4900 (older than 5 s): the time is clamped, not 4e9 ms
    Expect(balls(1700 + 4900) == 0, "module: the biggest time is 5 s", balls(1700 + 4900));
    SetWanted(false);
    SetMarkMs(kDefaultMarkMs);
}

int main() {
    CrossingTests();
    TrailTests();
    SceneTests();
    EngineTests();
    ModuleTests();
    if (g_failures) {
        std::printf("melee_marks_test: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("melee_marks_test: all passed\n");
    return 0;
}
