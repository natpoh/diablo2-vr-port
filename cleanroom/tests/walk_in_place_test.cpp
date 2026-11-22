// Offline checks of [walk_in_place] (vr/walk_in_place.h, vr/walk_engine_lib.cpp; docs/notes/walk_in_place.md):
//   - the pure part: the trims, the library's speed made into the game's walking stick against its run threshold (walk up to the run point, run past
//     it, every length past the pad's dead zone), the Run key, where the stick joins the pad, the gate;
//   - end to end against a stand-in library (cleanroom/tests/walk_engine_stub.cpp: same interface, a plain made-up detector, never shipped) on
//     synthetic headset tracks: standing, stepping, stepping harder, stopping, the trims, a frame that may not walk, a missing library.
// Run: walk_in_place_test.exe  (exit code 0 = all passed)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "walk_engine_lib.h"
#include "walk_in_place.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
static bool GameRuns(float len, float t) { return len > t; }   // the game (0x14AA10): longer than its threshold = a run (its run permitting)
static bool PastDeadZone(float len) { return len * 32767.0f > 10349.0f; }

static void TestTrims() {
    wip::Config c;
    const wip::Trims d = wip::TrimsOf(c);
    CHECK(Near(d.sensitivity, 0.5f) && Near(d.speed, 0.5f) && Near(d.smoothing, 0.5f));   // the defaults = BodyWalk's profile as it is
    c.sensitivity = 150.0f; c.speed = -20.0f; c.smoothing = NAN; c.runAt = 5.0f;
    const wip::Config k = wip::Clamped(c);
    CHECK(k.sensitivity == 100.0f && k.speed == 0.0f && k.smoothing == 50.0f && k.runAt == wip::kRunAtMin);
    const wip::Trims t = wip::TrimsOf(c);
    CHECK(Near(t.sensitivity, 1.0f) && Near(t.speed, 0.0f) && Near(t.smoothing, 0.5f));
    CHECK(wip::TrimsOf(wip::Config{}) == d && wip::TrimsOf(c) != d);
    CHECK(wip::Runs(80.0f) && wip::Runs(95.0f) && !wip::Runs(100.0f) && !wip::Runs(250.0f));
}

static void TestStickLength() {
    for (float t : {0.40f, 0.55f, 0.70f, 0.85f, 0.95f}) {
        for (float runAt : {20.0f, 50.0f, 80.0f, 95.0f}) {
            float prev = 0.0f;
            for (int i = 1; i <= 100; ++i) {
                const float s = i / 100.0f;
                const float len = wip::StickLength(s, runAt, t);
                if (s < wip::kStanding) { CHECK(len == 0.0f); continue; }
                CHECK(len > 0.0f && len <= 1.0f);
                CHECK(PastDeadZone(len));                                  // the slowest step still moves the hero
                CHECK(GameRuns(len, t) == (s > runAt / 100.0f + 1e-6f));   // walk up to the run point, run past it
                CHECK(len >= prev - 1e-6f);                                // harder steps never a shorter stick
                prev = len;
            }
            CHECK(Near(wip::StickLength(runAt / 100.0f, runAt, t), t));   // a full-strength walk right at the run point
            CHECK(Near(wip::StickLength(1.0f, runAt, t), 1.0f));          // the rim at full speed
        }
        // never a run point: the walk's range over the whole speed, never past the game's threshold
        for (int i = 2; i <= 100; ++i) CHECK(!GameRuns(wip::StickLength(i / 100.0f, 100.0f, t), t) && PastDeadZone(wip::StickLength(i / 100.0f, 100.0f, t)));
        CHECK(Near(wip::StickLength(1.0f, 100.0f, t), t));
    }
    // the threshold not readable: up to the run point under the rim, the rim past it (the Run key does the rest)
    CHECK(wip::StickLength(0.5f, 80.0f, 0.0f) < 1.0f && PastDeadZone(wip::StickLength(0.05f, 80.0f, 0.0f)));
    CHECK(wip::StickLength(0.9f, 80.0f, 0.0f) == 1.0f && wip::StickLength(0.9f, 80.0f, NAN) == 1.0f);
    // a threshold inside the dead zone (nonsense): treated as unknown
    CHECK(PastDeadZone(wip::StickLength(0.1f, 80.0f, 0.2f)));
    // standing, nonsense
    CHECK(wip::StickLength(0.0f, 80.0f, 0.7f) == 0.0f && wip::StickLength(0.01f, 80.0f, 0.7f) == 0.0f);
    CHECK(wip::StickLength(NAN, 80.0f, 0.7f) == 0.0f && wip::StickLength(-1.0f, 80.0f, 0.7f) == 0.0f);
    CHECK(Near(wip::StickLength(7.0f, 80.0f, 0.7f), 1.0f));
}

static void TestRunKey() {
    wip::RunKey r;
    CHECK(!r.Step(0.5f, 80.0f, false));
    CHECK(r.Step(0.85f, 80.0f, false));    // past the run point, the game's switch on walk: held
    CHECK(r.Step(0.79f, 80.0f, false));    // a little under: still held (no flutter)
    CHECK(!r.Step(0.70f, 80.0f, false));   // well under: let go
    CHECK(!r.Step(0.95f, 80.0f, true));    // the switch on run: nothing needed
    wip::RunKey never;
    for (float s : {0.2f, 0.9f, 1.0f}) CHECK(!never.Step(s, 100.0f, false));   // no run point: never held
    wip::RunKey stop;
    CHECK(stop.Step(0.9f, 80.0f, false) && !stop.Step(0.0f, 80.0f, false) && !stop.Step(NAN, 80.0f, false));
}

static void TestMergeAndGate() {
    const wip::Stick ours = wip::StickOf(0.6f);
    CHECK(ours.lx == 0 && ours.ly == (int16_t)std::lround(0.6f * 32767.0f));   // straight ahead in the view
    CHECK(!wip::StickOf(0.0f).Any() && !wip::StickOf(NAN).Any() && wip::StickOf(2.0f).ly == 32767);
    int16_t x = 0, y = 0;
    CHECK(wip::Merge(&x, &y, ours) && x == ours.lx && y == ours.ly);   // the controller's stick at rest: ours
    x = 3000; y = -2000;                                              // resting inside the dead zone (a drifting stick)
    CHECK(wip::Merge(&x, &y, ours) && y == ours.ly);
    x = 20000; y = 0;                                                 // pushed: the controller's wins
    CHECK(!wip::Merge(&x, &y, ours) && x == 20000 && y == 0);
    x = 0; y = 0;
    CHECK(!wip::Merge(&x, &y, wip::Stick{}) && x == 0 && y == 0);     // standing: nothing changes
    wip::Gate g{true, true, true, false, false, false};
    CHECK(wip::Play(g));
    wip::Gate h = g; h.f4 = false; CHECK(!wip::Play(h));
    h = g; h.inWorld = false; CHECK(!wip::Play(h));
    h = g; h.focused = false; CHECK(!wip::Play(h));
    h = g; h.menu = true; CHECK(!wip::Play(h));
    h = g; h.panelUp = true; CHECK(!wip::Play(h));
    h = g; h.chat = true; CHECK(!wip::Play(h));
}

// -- end to end on synthetic headset tracks ------------------------------------------------------------------------------------------------------------

struct Track {
    double t = 0.0;
    uint32_t seed = 12345;
    float Noise(float amp) {   // a small steady jitter (tracking noise)
        seed = seed * 1664525u + 1013904223u;
        return amp * (((seed >> 8) & 0xFFFF) / 32767.5f - 1.0f);
    }
};

// Feeds `seconds` of a headset at 90 Hz bobbing `bobM` (half the swing, metres) at `hz` steps a second (0 = standing); returns the stick lengths
// (with the game's threshold `t`, run point `runAt`) of the frames of the last `tailS` seconds.
static std::vector<float> Feed(Track& tr, double seconds, float bobM, float hz, bool play, float t, float runAt, double tailS, float* lastSpeed = nullptr) {
    std::vector<float> out;
    const double end = tr.t + seconds;
    for (; tr.t < end; tr.t += 1.0 / 90.0) {
        BWWE_Frame f{};
        f.size = sizeof f;
        f.flags = play ? BWWE_FRAME_PLAY : 0u;
        f.timeS = tr.t;
        f.head.valid = 1;
        f.head.rot[3] = 1.0f;
        const float bob = hz > 0.0f ? bobM * (float)std::sin(2.0 * 3.14159265358979 * hz * tr.t) : 0.0f;
        f.head.pos[0] = 0.01f * (float)std::sin(3.14159265358979 * hz * tr.t) + tr.Noise(0.0005f);   // the sway from foot to foot
        f.head.pos[1] = 1.65f + bob + tr.Noise(0.0005f);
        f.head.pos[2] = tr.Noise(0.0005f);
        BWWE_State st{};
        st.size = sizeof st;
        welib::Feed(f, &st);
        if (lastSpeed) *lastSpeed = st.speed;
        if (end - tr.t <= tailS) out.push_back(wip::StickLength(st.speed, runAt, t));
    }
    return out;
}
static float MaxOf(const std::vector<float>& v) { float m = 0.0f; for (float x : v) m = std::max(m, x); return m; }
static float MinOf(const std::vector<float>& v) { float m = 1.0f; for (float x : v) m = std::min(m, x); return v.empty() ? 0.0f : m; }

static void Tune(float sens, float speed, float smooth) {
    BWWE_Tuning t{};
    t.size = sizeof t;
    t.sensitivity = sens; t.speed = speed; t.smoothing = smooth;
    welib::SetTuning(t);
}

static void TestEndToEnd() {
    welib::SetGameId("d2rtest");
    welib::SetPath(L"C:\\nowhere\\d2r_vr_walk.dll");
    CHECK(!welib::Ensure("") && welib::GetStatus() == welib::Status::Missing);   // not there: nothing walks, said so
    wchar_t stub[MAX_PATH] = {};
    MultiByteToWideChar(CP_UTF8, 0, WALK_ENGINE_STUB, -1, stub, MAX_PATH);
    for (wchar_t* p = stub; *p; ++p) if (*p == L'/') *p = L'\\';   // (LOAD_WITH_ALTERED_SEARCH_PATH wants backslashes)
    welib::SetPath(stub);
    CHECK(welib::Ensure("") && welib::Loaded());
    {   // the library holds its mutex while open (the BodyWalk app leaves the game's walk to it)
        HANDLE m = OpenMutexA(SYNCHRONIZE, FALSE, "Local\\BodyWalk.WalkEngine.d2rtest");
        CHECK(m != nullptr);
        if (m) CloseHandle(m);
    }
    const float t = 0.7f;   // a game threshold (controllersettings.json's stickRunToggleThreshold)
    Tune(0.5f, 0.5f, 0.5f);
    Track tr;
    // standing still: no stick at all
    CHECK(MaxOf(Feed(tr, 3.0, 0.0f, 0.0f, true, t, 80.0f, 2.0)) == 0.0f);
    // stepping in place, a 2 cm bob at 1.8 steps a second: the hero walks (ahead, past the dead zone, under the run threshold)
    std::vector<float> walk = Feed(tr, 4.0, 0.02f, 1.8f, true, t, 80.0f, 1.5);
    CHECK(MinOf(walk) > 0.0f && PastDeadZone(MinOf(walk)));
    CHECK(MaxOf(walk) <= t + 1e-4f);
    // stepping harder: a longer stick, and past the run point the game's threshold is passed (a run)
    std::vector<float> run = Feed(tr, 4.0, 0.045f, 2.6f, true, t, 80.0f, 1.0);
    CHECK(MinOf(run) > MaxOf(walk) - 0.05f && MaxOf(run) > t);
    // ... with no run point the same steps stay a walk
    CHECK(MaxOf(Feed(tr, 2.0, 0.045f, 2.6f, true, t, 100.0f, 1.0)) <= t + 1e-4f);
    // stopping: the stick gone within two seconds (the stand-in's own pace; BodyWalk's is its profile's)
    CHECK(MaxOf(Feed(tr, 3.0, 0.0f, 0.0f, true, t, 80.0f, 1.0)) == 0.0f);
    // a frame that may not walk (a menu, not F4): no speed, even stepping
    CHECK(MaxOf(Feed(tr, 3.0, 0.03f, 2.0f, false, t, 80.0f, 2.0)) == 0.0f);
    welib::Reset();
    // a small bob (5 mm): too light at the middle sensitivity, enough at a high one
    Feed(tr, 2.0, 0.0f, 0.0f, true, t, 80.0f, 0.0);
    CHECK(MaxOf(Feed(tr, 4.0, 0.005f, 2.0f, true, t, 80.0f, 2.0)) == 0.0f);
    Tune(0.9f, 0.5f, 0.5f);
    CHECK(MinOf(Feed(tr, 4.0, 0.005f, 2.0f, true, t, 80.0f, 1.5)) > 0.0f);
    // the speed trim: the same steps faster with it higher
    Tune(0.5f, 0.2f, 0.5f);
    float slow = 0.0f, fast = 0.0f;
    Feed(tr, 4.0, 0.02f, 1.8f, true, t, 80.0f, 0.0, &slow);
    Tune(0.5f, 0.8f, 0.5f);
    Feed(tr, 4.0, 0.02f, 1.8f, true, t, 80.0f, 0.0, &fast);
    CHECK(fast > slow + 0.1f);
    welib::Shutdown();
    CHECK(!welib::Loaded() && welib::GetStatus() == welib::Status::NotLooked);
    BWWE_State st{};
    BWWE_Frame f{};
    welib::Feed(f, &st);
    CHECK(st.speed == 0.0f);   // closed: nothing
}

// [walk_in_place] source (Serg 2026-11-22: "встроенная библиотека или бодивалк"): 0 the built-in library as before, 1 the BodyWalk app with its Steps settings.
static void TestSource() {
    wip::Config c;
    CHECK(c.source == wip::kSourceLibrary);   // the default: as before
    CHECK(wip::SourceOf(0.0f) == wip::kSourceLibrary && wip::SourceOf(1.0f) == wip::kSourceBodyWalk && wip::SourceOf(0.8f) == wip::kSourceBodyWalk);
    CHECK(wip::SourceOf(2.0f) == wip::kSourceLibrary && wip::SourceOf(-1.0f) == wip::kSourceLibrary && wip::SourceOf(NAN) == wip::kSourceLibrary);   // a stray value: the library
    c.source = 7;
    CHECK(wip::Clamped(c).source == wip::kSourceLibrary);
    // off: nothing either way
    c = {};
    CHECK(!wip::LibraryOn(c) && !wip::BodyWalkOn(c));
    c.source = wip::kSourceBodyWalk;
    CHECK(!wip::LibraryOn(c) && !wip::BodyWalkOn(c));
    // on: one of the two, never both
    c.on = true;
    CHECK(!wip::LibraryOn(c) && wip::BodyWalkOn(c));
    c.source = wip::kSourceLibrary;
    CHECK(wip::LibraryOn(c) && !wip::BodyWalkOn(c));
    // the trims are the library's: the same numbers whatever the source (they wait for the library, unchanged)
    wip::Config b = c;
    b.source = wip::kSourceBodyWalk;
    CHECK(wip::TrimsOf(b) == wip::TrimsOf(c));
    // direct mode: BodyWalk's walking passes by [openxr] direct_bodywalk_walk, or with BodyWalk the walk source - in F4 only
    for (int bits = 0; bits < 16; ++bits) {
        wip::Config k;
        k.on = (bits & 1) != 0;
        k.source = bits & 2 ? wip::kSourceBodyWalk : wip::kSourceLibrary;
        const bool key = (bits & 4) != 0, f4 = (bits & 8) != 0;
        const bool passes = wip::BodyWalkPasses(key, k, f4);
        CHECK(passes == (key || (f4 && k.on && k.source == wip::kSourceBodyWalk)));
        CHECK(!key || passes);                                   // the old key alone: as before
        CHECK(!(passes && !key) || (f4 && wip::BodyWalkOn(k)));  // more than before only for BodyWalk's walk in F4
        CHECK(!(wip::LibraryOn(k) && passes && !key));           // never while the library walks
    }
    // the Walk tab's line with BodyWalk walking
    wip::BodyWalkFacts f;
    CHECK(wip::BodyWalkLineOf(f) == wip::BodyWalkLine::NotF4);
    f.f4 = true;
    CHECK(wip::BodyWalkLineOf(f) == wip::BodyWalkLine::NotRunning);   // nothing of BodyWalk's comes in: said so
    f.running = true;
    CHECK(wip::BodyWalkLineOf(f) == wip::BodyWalkLine::NoStick);      // it runs, no walking stick
    f.stickIn = true;
    CHECK(wip::BodyWalkLineOf(f) == wip::BodyWalkLine::Standing);
    f.walking = true;
    CHECK(wip::BodyWalkLineOf(f) == wip::BodyWalkLine::Walking);
    f.running = false;   // (a stick without Game Link: its pad report alone - it walks)
    CHECK(wip::BodyWalkLineOf(f) == wip::BodyWalkLine::Walking);
    f.f4 = false;
    CHECK(wip::BodyWalkLineOf(f) == wip::BodyWalkLine::NotF4);
}

int main() {
    TestTrims();
    TestSource();
    TestStickLength();
    TestRunKey();
    TestMergeAndGate();
    TestEndToEnd();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("walk in place: all %d checks passed\n", g_checks);
    return 0;
}
