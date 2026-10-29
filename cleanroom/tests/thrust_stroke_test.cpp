// Hit by touch: the THRUST (vr/thrust_stroke.h) - a poke along the blade's own axis with its own least speed and distance, apart from the swing's. Pure maths with the real
// swing engine (stroke_contact.h) twice, as vr/melee_touch.cpp runs it: the swing's engine at its numbers and the thrust's at its own, on the same synthetic poses.
// Run: build/Release/thrust_stroke_test.exe [-v]   (exit code 0 = all passed; -v prints every contact)

#include "../../vr/thrust_stroke.h"
#include "../../vr/thrust_debug.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace stroke;
using namespace strikedir;

static int g_failures = 0;
static bool g_verbose = false;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static const float kRad = 0.0174532925f;

static void RuleTests() {
    const thrust::Rule r;
    Expect(r.on && r.minSpeed == 0.8f && r.cm == 5.0f && r.alongPct == 70.0f && r.minAccel == 0.0f, "defaults: on, 0.8 m/s, 5 cm, 70 %, the acceleration not asked");
    for (int k = 0; k < kKinds; ++k) Expect(r.kindAccel[static_cast<size_t>(k)] == 0.0f, "defaults: no kind's own acceleration", k);
    {   // the acceleration: clamped 0 .. 300, a kind's own over the general one, 0 = the general one
        thrust::Rule a;
        a.minAccel = 900.0f;
        a.kindAccel[kSpear] = -4.0f;
        a.kindAccel[kDagger] = 1000.0f;
        a = thrust::Clamp(a);
        Expect(a.minAccel == 300.0f && a.kindAccel[kSpear] == 0.0f && a.kindAccel[kDagger] == 300.0f, "acceleration clamped", a.minAccel);
        a.minAccel = NAN;
        Expect(thrust::Clamp(a).minAccel == 0.0f, "acceleration not a number: not asked");
        thrust::Rule b;
        b.kindAccel[kSpear] = 15.0f;
        Expect(thrust::NumbersFor(b, kSpear).minAccel == 15.0f && thrust::NumbersFor(b, kDagger).minAccel == 0.0f, "a kind's own acceleration while the general one is off");
        b.minAccel = 8.0f;
        Expect(thrust::NumbersFor(b, kSpear).minAccel == 15.0f && thrust::NumbersFor(b, kDagger).minAccel == 8.0f, "a kind's own over the general one; the others take the general");
    }
    for (int k = 0; k < kKinds; ++k) Expect(r.mode[static_cast<size_t>(k)] == thrust::kByGame && r.kindSpeed[static_cast<size_t>(k)] == 0.0f && r.kindCm[static_cast<size_t>(k)] == 0.0f, "defaults: every kind by the game, the general numbers", k);
    thrust::Rule hi;
    hi.minSpeed = 99.0f;
    hi.cm = 99.0f;
    hi.alongPct = 120.0f;
    hi.mode[kSword] = 7;
    hi.kindSpeed[kSpear] = 50.0f;
    hi.kindCm[kSpear] = 50.0f;
    hi = thrust::Clamp(hi);
    Expect(hi.minSpeed == 20.0f && hi.cm == 30.0f && hi.alongPct == 95.0f && hi.mode[kSword] == thrust::kAlways && hi.kindSpeed[kSpear] == 20.0f && hi.kindCm[kSpear] == 30.0f, "clamped high");
    thrust::Rule lo;
    lo.minSpeed = NAN;
    lo.cm = -3.0f;
    lo.alongPct = 10.0f;
    lo.mode[kAxe] = -9;
    lo.kindSpeed[kSpear] = 0.1f;
    lo.kindCm[kSpear] = NAN;
    lo = thrust::Clamp(lo);
    Expect(lo.minSpeed == 0.8f && lo.cm == 1.0f && lo.alongPct == 50.0f && lo.mode[kAxe] == thrust::kNever && lo.kindSpeed[kSpear] == 0.0f && lo.kindCm[kSpear] == 0.0f, "clamped low and not a number");
    Expect(thrust::ModeOf(-1.0f) == thrust::kNever && thrust::ModeOf(0.0f) == thrust::kByGame && thrust::ModeOf(1.0f) == thrust::kAlways && thrust::ModeOf(NAN) == thrust::kByGame &&
               thrust::ModeOf(2.0f) == thrust::kAlways && thrust::ModeOf(0.3f) == thrust::kByGame,
           "the ini's number as a mode");
    // the tick per kind (melee_thrust_on_<kind>) wins; without it the older -1 / 0 / 1 number; without both, by the game's class
    Expect(thrust::ModeFromIni(1.0f, -1.0f) == thrust::kAlways && thrust::ModeFromIni(0.0f, 1.0f) == thrust::kNever, "the tick wins over the older number");
    Expect(thrust::ModeFromIni(NAN, -1.0f) == thrust::kNever && thrust::ModeFromIni(NAN, 1.0f) == thrust::kAlways && thrust::ModeFromIni(NAN, 0.0f) == thrust::kByGame,
           "no tick: the older number (-1 never, 1 always, 0 by the game)");
    Expect(thrust::ModeFromIni(NAN, NAN) == thrust::kByGame, "neither: by the game's class");
    for (int k = 0; k < kKinds; ++k)
        Expect(thrust::DefaultTick(k) == (k == kSpear || k == kDagger || k == kJavelin || k == kThrowing || k == kSword), "ticked to begin with: spears, daggers, javelins, throwing weapons, swords", k);
    {   // ticked / unticked through Applies, whatever the item's class says
        thrust::Rule t;
        t.mode[kSword] = thrust::ModeFromIni(1.0f, NAN);
        t.mode[kThrowing] = thrust::ModeFromIni(1.0f, NAN);
        t.mode[kSpear] = thrust::ModeFromIni(0.0f, NAN);
        Expect(thrust::Applies(t, kSword, 0) && thrust::Applies(t, kThrowing, 0) && !thrust::Applies(t, kSpear, 1), "a ticked sword and throwing axe thrust; an unticked spear does not");
    }
    // the numbers of a kind: its own where set, else the general
    thrust::Rule n;
    n.minSpeed = 1.2f;
    n.cm = 6.0f;
    n.kindSpeed[kSpear] = 0.6f;
    n.kindCm[kDagger] = 3.0f;
    Expect(thrust::NumbersFor(n, kSpear).minSpeed == 0.6f && thrust::NumbersFor(n, kSpear).cm == 6.0f, "a spear's own speed, the general distance");
    Expect(thrust::NumbersFor(n, kDagger).minSpeed == 1.2f && thrust::NumbersFor(n, kDagger).cm == 3.0f, "a dagger's own distance, the general speed");
    Expect(thrust::NumbersFor(n, -1).minSpeed == 1.2f && thrust::NumbersFor(n, 99).cm == 6.0f, "no kind: the general numbers");
    // the engine's gates: the thrust begins a stroke at its own least speed (the swing's engine asks 1.2 at most), the onset half the distance, 2 .. 4 cm
    Params p = thrust::ParamsFor({0.8f, 5.0f});
    Expect(p.strokeSpeed == 0.8f && p.beginSpeed == 0.8f && std::fabs(p.quietSpeed - 0.45f) < 1e-6f && std::fabs(p.onsetExcursion - 0.025f) < 1e-6f, "params at 0.8 / 5", p.onsetExcursion);
    p = thrust::ParamsFor({0.4f, 1.0f});
    Expect(p.beginSpeed == 0.4f && std::fabs(p.quietSpeed - 0.225f) < 1e-6f && std::fabs(p.onsetExcursion - 0.02f) < 1e-6f, "params low: the begin speed follows, the onset at least 2 cm", p.quietSpeed);
    p = thrust::ParamsFor({3.0f, 25.0f});
    Expect(p.beginSpeed == 3.0f && p.quietSpeed == 0.45f && std::fabs(p.onsetExcursion - 0.04f) < 1e-6f, "params high: the onset at most 4 cm (the distance is the path's)", p.onsetExcursion);
}

// ---- the game's weapon class ----

static std::vector<uint8_t> Row(size_t size, uint32_t at, const char* cls, uint32_t at2 = 0, const char* cls2 = nullptr, uint8_t pad = 0) {
    std::vector<uint8_t> r(size, 0);
    for (size_t i = 0; i < size; ++i) r[i] = static_cast<uint8_t>((i * 37 + 11) & 255);   // noise
    const auto put = [&](uint32_t o, const char* c) { r[o] = static_cast<uint8_t>(c[0]); r[o + 1] = static_cast<uint8_t>(c[1]); r[o + 2] = static_cast<uint8_t>(c[2]); r[o + 3] = pad; };
    put(at, cls);
    if (cls2) put(at2, cls2);
    return r;
}

static void ClassTests() {
    const uint8_t a[4] = {'1', 'h', 't', 0}, b[4] = {'2', 'H', 'T', ' '}, c[4] = {'1', 'h', 's', 0}, d[4] = {'1', 'h', 't', 'x'}, e[4] = {'s', 't', 'f', 0};
    Expect(thrust::Spells(a, "1ht") && thrust::Spells(b, "2ht") && thrust::Spells(c, "1hs") && !thrust::Spells(d, "1ht") && !thrust::Spells(a, "1hs") && !thrust::Spells(nullptr, "1ht"),
           "Spells: three characters, any case, a zero or a space after");
    Expect(thrust::ThrustClass(a) && thrust::ThrustClass(b) && !thrust::ThrustClass(c) && !thrust::ThrustClass(e) && !thrust::ThrustClass(d), "the thrust classes: 1ht and 2ht");
    // rows of a dagger, a spear, a short sword, a short staff, a bow, the class at 0xC0 and the two-handed class at 0xC4 (the same for these)
    const char* const cls[5] = {"1ht", "2ht", "1hs", "stf", "bow"};
    std::vector<std::vector<uint8_t>> rows;
    for (const char* k : cls) rows.push_back(Row(0x1A8, 0xC0, k, 0xC4, k));
    std::vector<thrust::ClassProbe> probes;
    for (size_t i = 0; i < rows.size(); ++i) probes.push_back({rows[i].data(), static_cast<uint32_t>(rows[i].size()), cls[i]});
    uint32_t at[4] = {};
    size_t n = thrust::FindClassField(probes.data(), probes.size(), at, 4);
    Expect(n == 2 && at[0] == 0xC0 && at[1] == 0xC4, "the field found: the class first, the two-handed class after it", static_cast<double>(n));
    // spaces as the padding, and one probe's row shorter: still found where all rows reach
    rows.clear();
    for (const char* k : cls) rows.push_back(Row(0x1A8, 0x40, k, 0, nullptr, ' '));
    rows[2].resize(0x50);
    probes.clear();
    for (size_t i = 0; i < rows.size(); ++i) probes.push_back({rows[i].data(), static_cast<uint32_t>(rows[i].size()), cls[i]});
    n = thrust::FindClassField(probes.data(), probes.size(), at, 4);
    Expect(n == 1 && at[0] == 0x40, "space padded, a short row: found", static_cast<double>(n));
    // a build whose rows do not hold the classes as text: nothing found (the kind decides)
    for (auto& r : rows) for (size_t i = 0x40; i < 0x44; ++i) r[i] = 7;
    Expect(thrust::FindClassField(probes.data(), probes.size(), at, 4) == 0, "no field: nothing found");
    Expect(thrust::FindClassField(nullptr, 0, at, 4) == 0, "no probes: nothing");
    // one probe whose class is wrong for the place: not that place
    rows.clear();
    for (const char* k : cls) rows.push_back(Row(0x100, 0x20, k));
    rows[1][0x20] = '1';   // the spear reads 1ht there
    probes.clear();
    for (size_t i = 0; i < rows.size(); ++i) probes.push_back({rows[i].data(), static_cast<uint32_t>(rows[i].size()), cls[i]});
    Expect(thrust::FindClassField(probes.data(), probes.size(), at, 4) == 0, "a probe that does not fit: the place is not taken");

    // which weapons thrust
    const thrust::Rule r;
    Expect(thrust::Applies(r, kSpear, 1) && thrust::Applies(r, kDagger, 1) && thrust::Applies(r, kThrowing, 1) && thrust::Applies(r, kJavelin, 1), "by the game: a thrust class thrusts");
    Expect(!thrust::Applies(r, kSword, 0) && !thrust::Applies(r, kThrowing, 0) && !thrust::Applies(r, kPolearm, 0) && !thrust::Applies(r, kMace, 0), "by the game: a swing class (a sword, a throwing axe, a polearm) does not");
    Expect(thrust::Applies(r, kSpear, -1) && thrust::Applies(r, kDagger, -1) && thrust::Applies(r, kJavelin, -1) && !thrust::Applies(r, kThrowing, -1) && !thrust::Applies(r, kSword, -1) &&
               !thrust::Applies(r, kClaw, -1),
           "the class not read: spears, daggers and javelins");
    thrust::Rule m = r;
    m.mode[kSword] = thrust::kAlways;
    m.mode[kSpear] = thrust::kNever;
    Expect(thrust::Applies(m, kSword, 0) && !thrust::Applies(m, kSpear, 1) && thrust::Applies(m, kDagger, 1), "a kind set always (a sword's tip) or never (a spear)");
    m.on = false;
    Expect(!thrust::Applies(m, kSword, 0) && !thrust::Applies(m, kDagger, 1), "thrusts off: none");
    Expect(!thrust::Applies(r, -1, 1) && !thrust::Applies(r, kKinds, 1), "no kind: none");
}

// ---- the path ----

static void PathTests() {
    thrust::Path p;
    double t = 1.0;
    // a blade along +z, its tip pushed 10 cm forward in 10 poses: a pure thrust
    for (int i = 0; i <= 10; ++i) {
        const Vec base{0.0f, 1.2f, 0.25f + 0.01f * i};
        p.Feed(t, base, base + Vec{0.0f, 0.0f, 0.8f}, i >= 3 ? 1u : 0u);
        t += 1.0 / 90.0;
    }
    Expect(std::fabs(p.AlongM() - p.PathM()) < 1e-4f && p.Share() > 0.99f && p.StepCos() > 0.99f, "a pure thrust: all of the path along the blade", p.Share());
    Expect(p.AlongM() > 0.085f && p.AlongM() < 0.105f, "the onset window counts (the stroke began at the 3rd pose)", p.AlongM());
    // drawn back: the path grows, the along does not
    for (int i = 0; i < 5; ++i) {
        const Vec base{0.0f, 1.2f, 0.35f - 0.01f * i};
        p.Feed(t, base, base + Vec{0.0f, 0.0f, 0.8f}, 1u);
        t += 1.0 / 90.0;
    }
    Expect(p.StepCos() < -0.99f && p.Share() < 0.75f, "drawn back: the step goes back, the share falls", p.Share());
    // no stroke: nothing
    p.Feed(t, {0.0f, 1.2f, 0.3f}, {0.0f, 1.2f, 1.1f}, 0u);
    Expect(p.AlongM() == 0.0f && p.PathM() == 0.0f && p.Share() == 0.0f && p.Stroke() == 0u, "no stroke: no path");
    // a swing: the blade turns about the hand, its tip goes ACROSS the blade
    thrust::Path s;
    t = 2.0;
    for (int i = 0; i <= 20; ++i) {
        const float a = (-40.0f + 4.0f * i) * kRad;
        const Vec hand{0.0f, 1.2f, 0.25f};
        s.Feed(t, hand, hand + Vec{std::sin(a), 0.0f, std::cos(a)} * 0.8f, 5u);
        t += 1.0 / 90.0;
    }
    Expect(s.Share() < 0.05f && std::fabs(s.StepCos()) < 0.1f, "a swing: nothing along the blade", s.Share());
    // time going back starts afresh
    s.Feed(1.0, {0.0f, 1.2f, 0.25f}, {0.0f, 1.2f, 1.05f}, 5u);
    Expect(s.PathM() == 0.0f, "time went back: afresh");
    // the verdict on a path
    thrust::Path q;
    t = 3.0;
    for (int i = 0; i <= 4; ++i) {   // 4 cm forward
        const Vec base{0.0f, 1.2f, 0.25f + 0.01f * i};
        q.Feed(t, base, base + Vec{0.0f, 0.0f, 0.8f}, 2u);
        t += 1.0 / 90.0;
    }
    const thrust::Rule r;
    thrust::Verdict v = thrust::Judge(r, kSpear, q);
    Expect(v.why == thrust::Why::Short && std::fabs(v.alongCm - 4.0f) < 0.05f && v.needCm == 5.0f, "4 cm: too short for 5", v.alongCm);
    thrust::Rule shortRule = r;
    shortRule.kindCm[kSpear] = 3.0f;
    Expect(thrust::Judge(shortRule, kSpear, q).why == thrust::Why::Counts && thrust::Judge(shortRule, kDagger, q).why == thrust::Why::Short, "the spear's own 3 cm: counts; the dagger keeps 5");
    Expect(thrust::Judge(r, kSpear, s).why == thrust::Why::NotAlong, "the swing's path: not a thrust");
    Expect(std::strcmp(thrust::WhyText(thrust::Why::Counts), "a thrust") == 0 && thrust::WhyText(thrust::Why::Short)[0] && thrust::WhyText(thrust::Why::NotAlong)[0], "the texts");
    // StrikeRule: the swing's amplitude off, the rest as it was
    const strikedir::Rule sr;
    const strikedir::Rule tr = thrust::StrikeRule(sr);
    Expect(!tr.amplitude && tr.toward == sr.toward && tr.head == sr.head && tr.towardDeg == sr.towardDeg && tr.headKinds == sr.headKinds, "the strike rules of a thrust: no swing amplitude");
}

// ---- the touch loop with both engines ----

struct Run { unsigned presses = 0, bySwing = 0, byThrust = 0, refusedShort = 0, refusedAway = 0, refusedSmall = 0, notAlong = 0, refusedWeak = 0; float peakAccel = 0.0f; };
struct Setup {
    StrokeTuning swing{6.0f, 15.0f};   // the author's swing numbers: real swings only
    strikedir::Rule strike;
    thrust::Rule thrust;
    int itemClass = 1;                 // the game's class of the item: 1 a thrust class
};

// melee_touch.cpp's order: each pose both engines step; a thrust contact is judged first (its own speed, the path, the strike rules without the amplitude), else the swing's;
// a monster pressed by one is marked in the other's hit set for the stroke that is on.
template <class PoseOf>
static Run Loop(const Setup& su, int kind, int steps, PoseOf poseOf, const Body& monster) {
    Engine swingEng(ParamsFor(su.swing));
    const bool thrusts = thrust::Applies(su.thrust, kind, su.itemClass);
    Engine thrustEng(thrust::ParamsFor(thrust::NumbersFor(su.thrust, kind)));
    HitSet swingHits, thrustHits;
    StrokeTravel travel;
    thrust::Path path;
    Run run;
    double t = 1.0;
    for (int i = 0; i < steps; ++i) {
        Sample s;
        s.time = t;
        s.active = true;
        s.epoch = 1;
        s.context = 7;
        Blade b;
        Quat q;
        poseOf(i, &b, &q);
        s.hand = {b.base, q};
        s.blade = b;
        s.anchor = {0.0f, 1.2f, 0.0f};
        s.scale = 4.0f;
        s.lever = Length(b.tip - b.base);
        s.room = s.blade;
        s.haveRoom = true;
        Sweep sw, tsw;
        const bool swept = swingEng.Step(s, sw);
        travel.Feed(t, s.room.tip, swingEng.StrokeId());
        bool tswept = false;
        if (thrusts) {
            Sample ts = s;   // the whole weapon, as melee_touch.cpp: the engine on the grip, the path from it
            ts.room = thrust::GripBlade(s.room, s.hand.p);
            ts.lever = Length(ts.room.tip - ts.room.base);
            tswept = thrustEng.Step(ts, tsw);
            path.Feed(t, ts.room.base, s.room.tip, thrustEng.StrokeId());
            run.peakAccel = std::max(run.peakAccel, path.PeakAccel());
        }
        bool pressed = false;
        if (tswept) {
            const Contact c = TouchOf(tsw, monster);
            if (c.t >= 0.0f && !thrustHits.Contains(1, tsw.stroke, 1)) {
                const thrust::Verdict v = thrust::Judge(su.thrust, kind, path);
                if (v.why == thrust::Why::NotAlong) ++run.notAlong;
                else if (c.speed >= thrustEng.GetParams().strokeSpeed) {
                    const Judgement j = Judge(thrust::StrikeRule(su.strike), kind, tsw, c, monster, 0.0f);
                    if (g_verbose) std::printf("      step %3d thrust %llu along %5.1f cm share %.2f step %.2f way %5.1f deg -> %s / %s\n", i, static_cast<unsigned long long>(tsw.stroke), v.alongCm, v.share, v.stepCos, j.angleDeg, thrust::WhyText(v.why), WhyText(j.why));
                    if (j.why == Why::Away) ++run.refusedAway;
                    else if (j.why != Why::Counts) {}
                    else if (v.why == thrust::Why::Short) ++run.refusedShort;
                    else if (v.why == thrust::Why::Weak) ++run.refusedWeak;
                    else if (thrustHits.Insert(1, tsw.stroke, 1)) {
                        ++run.presses;
                        ++run.byThrust;
                        pressed = true;
                        if (swingEng.StrokeId()) swingHits.Insert(1, swingEng.StrokeId(), 1);
                    }
                }
            }
        }
        if (!pressed && swept) {
            const Contact c = TouchOf(sw, monster);
            if (c.t >= 0.0f && !swingHits.Contains(1, sw.stroke, 1) && c.speed >= swingEng.GetParams().strokeSpeed) {
                const Judgement j = Judge(su.strike, kind, sw, c, monster, travel.Meters());
                if (g_verbose) std::printf("      step %3d swing %llu travel %5.1f cm -> %s\n", i, static_cast<unsigned long long>(sw.stroke), travel.Meters() * 100.0f, WhyText(j.why));
                if (j.why == Why::TooSmall) ++run.refusedSmall;
                else if (j.why == Why::Away) ++run.refusedAway;
                else if (j.why == Why::Counts && swingHits.Insert(1, sw.stroke, 1)) {
                    ++run.presses;
                    ++run.bySwing;
                    if (thrusts && thrustEng.StrokeId()) thrustHits.Insert(1, thrustEng.StrokeId(), 1);
                }
            }
        }
        t += 1.0 / 90.0;
    }
    return run;
}

// A rod 0.8 m long pointing straight ahead (+z), pushed forward `distance` in `samples` poses after a rest (90 Hz: speed = distance * 90 / samples), toward a post whose
// surface the tip reaches just before the end of the push. back: pulled back the same way afterwards.
static Run Poke(const Setup& su, int kind, float distance, int samples, bool back = false) {
    const float tipStart = 0.25f + 0.8f;
    const float postZ = tipStart + distance + 0.065f + 0.03f - 0.004f;
    const Body post{{0.0f, 0.0f, postZ}, {0.0f, 1.8f, postZ}, 0.03f};
    const auto pose = [&](int i, Blade* b, Quat* q) {
        float k = i < 12 ? 0.0f : std::min(1.0f, static_cast<float>(i - 12) / static_cast<float>(samples));
        if (back && i >= 12 + samples + 4) k = std::max(0.0f, 1.0f - static_cast<float>(i - 12 - samples - 4) / static_cast<float>(samples));
        const Vec hand{0.0f, 1.2f, 0.25f + distance * k};
        *b = {hand, hand + Vec{0.0f, 0.0f, 0.8f}, 0.065f};
        *q = Quat{};
    };
    return Loop(su, kind, 12 + samples * 2 + 30, pose, post);
}

// A spear pulled back from the post's side of it: the tip starts inside reach and goes away (the pull-back of a poke that began in the monster).
static Run PullBack(const Setup& su, int kind) {
    const Body post{{0.0f, 0.0f, 1.08f}, {0.0f, 1.8f, 1.08f}, 0.1f};
    const auto pose = [&](int i, Blade* b, Quat* q) {
        const float k = i < 12 ? 0.0f : std::min(1.0f, static_cast<float>(i - 12) / 9.0f);
        const Vec hand{0.0f, 1.2f, 0.25f - 0.10f * k};
        *b = {hand, hand + Vec{0.0f, 0.0f, 0.8f}, 0.065f};
        *q = Quat{};
    };
    return Loop(su, kind, 50, pose, post);
}

// A sideways swing of the rod about the hand through a post at its side: the tip across the blade, `deg` of arc at `degPerS`.
static Run Sideways(const Setup& su, int kind, float degPerS) {
    const Vec hand{0.0f, 1.2f, 0.25f};
    const Body post{{0.35f, 0.0f, 0.25f + 0.7f}, {0.35f, 1.8f, 0.25f + 0.7f}, 0.1f};
    float deg = -40.0f;
    const auto pose = [&](int i, Blade* b, Quat* q) {
        if (i >= 12 && deg < 60.0f) deg += degPerS / 90.0f;
        const float a = deg * kRad;
        *b = {hand, hand + Vec{std::sin(a), 0.0f, std::cos(a)} * 0.8f, 0.065f};
        *q = Quat{0.0f, std::sin(a * 0.5f), 0.0f, std::cos(a * 0.5f)};
    };
    return Loop(su, kind, 80, pose, post);
}

// A smooth poke: the rod pushed `distance` forward over `seconds`, its speed rising and falling as a hand's does (x = d (1 - cos(pi t / T)) / 2: the top speed pi d / 2T at the
// middle, the top acceleration pi^2 d / 2T^2 at the start), toward a post the tip meets at 60 % of the way - near the top speed.
static Run Smooth(const Setup& su, int kind, float distance, float seconds) {
    const float tipStart = 0.25f + 0.8f;
    const float postZ = tipStart + 0.6f * distance + 0.065f + 0.03f - 0.004f;
    const Body post{{0.0f, 0.0f, postZ}, {0.0f, 1.8f, postZ}, 0.03f};
    const int moving = static_cast<int>(seconds * 90.0f);
    const auto pose = [&](int i, Blade* b, Quat* q) {
        const float k = i < 12 ? 0.0f : std::min(1.0f, static_cast<float>(i - 12) / static_cast<float>(moving));
        const Vec hand{0.0f, 1.2f, 0.25f + distance * 0.5f * (1.0f - std::cos(3.14159265f * k))};
        *b = {hand, hand + Vec{0.0f, 0.0f, 0.8f}, 0.065f};
        *q = Quat{};
    };
    return Loop(su, kind, 12 + moving + 30, pose, post);
}

static void AccelTests() {
    // the slow long push: 30 cm in 0.5 s - top speed 0.94 m/s (over the thrust's 0.8), top acceleration about 6 m/s^2; the sharp jab: 10 cm in 0.12 s - 1.3 m/s, about 34 m/s^2
    Setup su;
    Run push = Smooth(su, kSpear, 0.30f, 0.5f), jab = Smooth(su, kSpear, 0.10f, 0.12f);
    std::printf("  the acceleration not asked: a slow 30 cm push %u hit (peak %.1f m/s^2), a sharp 10 cm jab %u hit (peak %.1f m/s^2)\n", push.presses, push.peakAccel, jab.presses, jab.peakAccel);
    Expect(push.byThrust == 1, "the acceleration off (0): the slow long push hits, as before", push.presses);
    Expect(jab.byThrust == 1, "the acceleration off: the jab hits", jab.presses);
    Expect(push.peakAccel > 3.0f && push.peakAccel < 9.0f, "the push's peak acceleration is a few m/s^2", push.peakAccel);
    Expect(jab.peakAccel > 20.0f, "the jab's peak acceleration is tens of m/s^2", jab.peakAccel);
    su.thrust.minAccel = 12.0f;
    push = Smooth(su, kSpear, 0.30f, 0.5f);
    jab = Smooth(su, kSpear, 0.10f, 0.12f);
    Expect(push.presses == 0 && push.refusedWeak >= 1, "12 m/s^2 asked: the slow long push is refused - a push, not a jab", push.refusedWeak);
    Expect(jab.byThrust == 1, "12 m/s^2 asked: the sharp jab hits", jab.presses);
    // the kind's own: the spear asks nothing more while the general asks 12; the dagger keeps the general
    su.thrust.kindAccel[kSpear] = 4.0f;
    Expect(Smooth(su, kSpear, 0.30f, 0.5f).byThrust == 1 && Smooth(su, kDagger, 0.30f, 0.5f).presses == 0, "a spear's own 4 m/s^2: its push hits; a dagger's push (the general 12) does not");
    // the acceleration is the burst that began it, not the speed at the touch: the jab meets the post coasting at the top speed, and counts
    Setup coast;
    coast.thrust.minAccel = 12.0f;
    Expect(Smooth(coast, kSpear, 0.10f, 0.12f).byThrust == 1, "the jab touches near its top speed (no acceleration left at the touch) and still counts: the peak decides");
    // the path's own reading on a poke at a constant speed from rest (no ramp): a jump in speed reads as a big acceleration, a still rod as none
    thrust::Path still;
    for (int i = 0; i < 30; ++i) still.Feed(1.0 + i / 90.0, {0.0f, 1.2f, 0.25f}, {0.0f, 1.2f, 1.05f}, i > 20 ? 3u : 0u);
    Expect(still.PeakAccel() == 0.0f, "a still rod: no acceleration", still.PeakAccel());
}

static void LoopTests() {
    const Setup su;   // the swing at 6 m/s / 15 cm (the author's), the thrust at its defaults
    // an 8 cm poke of a spear at 1 m/s: the swing's engine sees nothing, the thrust hits once
    Run r = Poke(su, kSpear, 0.08f, 7);
    std::printf("  a spear poked 8 cm at 1 m/s (the swing at 6 m/s / 15 cm): %u presses (%u by a thrust, %u by a swing)\n", r.presses, r.byThrust, r.bySwing);
    Expect(r.presses == 1 && r.byThrust == 1, "a short quick poke of a spear: one hit, by the thrust", r.presses);
    // the same poke with thrusts off: nothing (as before)
    Setup off = su;
    off.thrust.on = false;
    Expect(Poke(off, kSpear, 0.08f, 7).presses == 0, "thrusts off: the poke does nothing (as before)");
    // a dagger (1ht) the same; a sword (1hs) not - unless its kind is set to thrust
    Expect(Poke(su, kDagger, 0.08f, 7).byThrust == 1, "a dagger's poke: one hit");
    Setup sword = su;
    sword.itemClass = 0;
    Expect(Poke(sword, kSword, 0.08f, 7).presses == 0, "a sword (a swing class): its poke does nothing");
    sword.thrust.mode[kSword] = thrust::kAlways;
    Expect(Poke(sword, kSword, 0.08f, 7).byThrust == 1, "a sword set to thrust: its tip pokes");
    // the class not read: the kind decides (a spear yes)
    Setup unread = su;
    unread.itemClass = -1;
    Expect(Poke(unread, kSpear, 0.08f, 7).byThrust == 1 && Poke(unread, kThrowing, 0.08f, 7).presses == 0, "the class not read: a spear thrusts, a throwing weapon does not");
    // too short: 3 cm against 5 - seen and refused
    r = Poke(su, kSpear, 0.03f, 3);
    Expect(r.presses == 0 && r.refusedShort >= 1, "a 3 cm poke: too short (5 cm), refused - not lost", r.refusedShort);
    Setup shortOk = su;
    shortOk.thrust.kindCm[kSpear] = 2.0f;
    Expect(Poke(shortOk, kSpear, 0.03f, 3).byThrust == 1, "the spear's own 2 cm: the 3 cm poke hits");
    // too slow: 8 cm in 18 poses (0.4 m/s) against 0.8
    Expect(Poke(su, kSpear, 0.08f, 18).presses == 0, "a slow push (0.4 m/s): nothing");
    Setup slow = su;
    slow.thrust.kindSpeed[kSpear] = 0.35f;
    Expect(Poke(slow, kSpear, 0.08f, 18).byThrust == 1, "the spear's own 0.35 m/s: the slow push hits");
    // drawn back out of a monster: never a hit
    Expect(PullBack(su, kSpear).presses == 0, "the pull-back out of the monster: nothing");
    // a sideways swing at 1.5 m/s: not along the blade - the thrust does not take it (and the swing's 6 m/s does not either)
    r = Sideways(su, kSpear, 110.0f);
    std::printf("  a spear swung sideways at ~1.5 m/s: %u presses, %u contacts not along the blade\n", r.presses, r.notAlong);
    Expect(r.presses == 0, "a slow sideways swing of a spear: no hit (not a thrust, under the swing's speed)", r.presses);
    // ... while the swing keeps its own numbers: a hard swing (~6.5 m/s at the tip) hits by the swing, once
    r = Sideways(su, kSpear, 480.0f);
    std::printf("  a spear swung sideways hard: %u presses (%u by a swing, %u by a thrust)\n", r.presses, r.bySwing, r.byThrust);
    Expect(r.presses == 1 && r.bySwing == 1, "a hard swing: one hit, by the swing", r.presses);
    // a long hard thrust both engines count: one hit
    Setup soft = su;
    soft.swing = {0.8f, 8.0f};
    r = Poke(soft, kSpear, 0.30f, 6);
    std::printf("  a 30 cm thrust at 4.5 m/s, the swing at its defaults: %u presses (%u by a thrust, %u by a swing)\n", r.presses, r.byThrust, r.bySwing);
    Expect(r.presses == 1, "a thrust both engines count: one hit", r.presses);
    // the swing's amplitude does not judge a thrust: a 6 cm dagger poke the swing calls too small (8 cm) is a thrust
    Setup small = su;
    small.swing = {0.8f, 2.0f};
    r = Poke(small, kDagger, 0.06f, 3);
    Expect(r.byThrust == 1 && r.presses == 1, "a 6 cm dagger poke: a thrust (the swing's 8 cm amplitude is the swing's)", r.presses);
    off = small;
    off.thrust.on = false;
    r = Poke(off, kDagger, 0.06f, 3);
    Expect(r.presses == 0 && r.refusedSmall >= 1, "... thrusts off: the swing refuses it as too small, as before", r.refusedSmall);
    // the strike rules still judge a thrust: the head only for a kind ticked - a poke's contact at the shaft is refused
    Setup head = su;
    head.strike.headKinds |= Bit(kSpear);
    head.strike.headPct = 10.0f;
    const Body side{{0.06f, 0.0f, 0.6f}, {0.06f, 1.8f, 0.6f}, 0.03f};   // beside the shaft, not in front of the tip
    const auto pose = [&](int i, Blade* b, Quat* q) {
        const float k = i < 12 ? 0.0f : std::min(1.0f, static_cast<float>(i - 12) / 7.0f);
        const Vec hand{0.0f, 1.2f, 0.25f + 0.08f * k};
        *b = {hand, hand + Vec{0.0f, 0.0f, 0.8f}, 0.065f};
        *q = Quat{};
    };
    Expect(Loop(head, kSpear, 50, pose, side).presses == 0, "a poke grazing a monster beside the shaft: no hit (not toward it, not the head)");
}

// ---- [debug] thrust_debug: the readout's decision and its text ----
static bool Has(const thrustdbg::Line* l, size_t n, const char* text, thrustdbg::Colour c) {
    for (size_t i = 0; i < n; ++i)
        if (std::strstr(l[i].text, text) && l[i].colour == c) return true;
    return false;
}
static void DebugTests() {
    using namespace thrustdbg;
    thrust::Rule r;
    r.minAccel = 12.0f;
    r.kindCm[kSpear] = 3.0f;
    thrust::Verdict v;
    v.share = 0.9f;
    v.alongCm = 6.0f;
    v.needCm = 3.0f;
    v.accel = 30.0f;
    v.needAccel = 12.0f;
    // a counted contact: green head line, every number green, the spear's own cm marked
    Reading c = ContactReading(r, kSpear, v, 1.4f, strikedir::Why::Counts, 77, 5, 1000);
    Line l[8];
    size_t n = Lines(c, "TOUCH", l, 8);
    Expect(c.counted && c.fail == Fail::None && n == 6, "a counted contact: six lines (head, straight, turn, speed, along, accel)", static_cast<double>(n));
    Expect(Has(l, n, "TOUCH SPEAR - COUNTED", kPass), "head line: TOUCH SPEAR - COUNTED in the pass colour");
    Expect(Has(l, n, "speed 1.40 / 0.80 m/s", kPass) && Has(l, n, "along 6.0 / 3 cm (own)", kPass) && Has(l, n, "straight 90 / 70 %", kPass) && Has(l, n, "accel 30 / 12 m/s2", kPass),
           "each number with its threshold, green; the spear's own cm says (own)");
    // the order of the failures, as the touch checks them
    thrust::Verdict s = v;
    s.why = thrust::Why::NotAlong;
    s.share = 0.4f;
    Expect(ContactReading(r, kSpear, s, 0.5f, strikedir::Why::Away, 1, 1, 1).fail == Fail::NotStraight, "not along the blade first");
    Expect(ContactReading(r, kSpear, v, 0.5f, strikedir::Why::Away, 1, 1, 1).fail == Fail::Slow, "then too slow where it touched");
    Expect(ContactReading(r, kSpear, v, 1.4f, strikedir::Why::Away, 1, 1, 1).fail == Fail::Away, "then not toward the monster");
    Expect(ContactReading(r, kSpear, v, 1.4f, strikedir::Why::NotHead, 1, 1, 1).fail == Fail::NotHead, "then not the head");
    thrust::Verdict sh = v;
    sh.why = thrust::Why::Short;
    sh.alongCm = 2.0f;
    c = ContactReading(r, kSpear, sh, 1.4f, strikedir::Why::Counts, 1, 1, 1);
    n = Lines(c, "TOUCH", l, 8);
    Expect(c.fail == Fail::Short && Has(l, n, "TOUCH SPEAR - REJECTED: too short", kFail) && Has(l, n, "along 2.0 / 3 cm (own)", kFail) && Has(l, n, "speed 1.40", kPass),
           "too short: red head line naming it, the cm red, the speed still green");
    thrust::Verdict wk = v;
    wk.why = thrust::Why::Weak;
    wk.accel = 5.0f;
    c = ContactReading(r, kDagger, wk, 1.4f, strikedir::Why::Counts, 1, 1, 1);
    n = Lines(c, "TOUCH", l, 8);
    Expect(c.fail == Fail::NoAccel && Has(l, n, "DAGGER - REJECTED: no accel", kFail) && Has(l, n, "accel 5 / 12 m/s2", kFail) && Has(l, n, "along 6.0 / 5 cm", kPass),
           "no accel: named, the accel red; the dagger takes the general 5 cm (no (own))");
    Expect(!Has(l, n, "(own)", kPass) && !Has(l, n, "(own)", kFail), "the dagger: no own threshold");
    c = ContactReading(r, kSpear, v, 1.4f, strikedir::Why::Away, 1, 1, 1);
    n = Lines(c, "TOUCH", l, 8);
    Expect(Has(l, n, "REJECTED: not toward the monster", kFail), "not toward the monster: named");
    // a jab with no monster: would count / would fail, by the thrust's own thresholds
    Reading j = JabReading(r, kSpear, 1.3f, 8.0f, 92.0f, 31.0f, 3, 1);
    n = Lines(j, "JAB", l, 8);
    Expect(j.counted && !j.contact && Has(l, n, "JAB SPEAR - would count", kPass), "a good jab: would count, green");
    j = JabReading(r, kSpear, 0.9f, 20.0f, 95.0f, 5.9f, 3, 1);
    n = Lines(j, "JAB", l, 8);
    Expect(j.fail == Fail::NoAccel && Has(l, n, "would fail: no accel", kFail), "a slow long push: would fail - no accel");
    j = JabReading(r, kSpear, 0.5f, 20.0f, 95.0f, 40.0f, 3, 1);
    Expect(j.fail == Fail::Slow, "a jab under the speed: too slow");
    j = JabReading(r, kSpear, 2.0f, 20.0f, 30.0f, 40.0f, 3, 1);
    Expect(j.fail == Fail::NotStraight, "a swing's path: not straight");
    thrust::Rule off;
    j = JabReading(off, kSpear, 1.0f, 6.0f, 90.0f, 3.0f, 3, 1);
    n = Lines(j, "JAB", l, 8);
    Expect(j.counted && Has(l, n, "accel 3 m/s2 (not asked)", kWhite), "the acceleration not asked: shown, white, not judged");
    // [debug] thrust_debug_only: a movement not straight enough to be a thrust (a swing, an overhead blow) is not shown; every thrust attempt is
    {
        const Reading swingLike = JabReading(r, kSpear, 3.0f, 20.0f, 30.0f, 40.0f, 9, 1);
        const Reading slowJab = JabReading(r, kSpear, 0.5f, 20.0f, 95.0f, 40.0f, 9, 1);
        Expect(swingLike.fail == Fail::NotStraight && Shown(swingLike, false) && !Shown(swingLike, true), "only thrusts: a swing's movement is hidden (shown without the tick)");
        Expect(Shown(slowJab, true) && Shown(c, true), "only thrusts: a thrust attempt that fails another threshold is shown, so is a refused touch");
        Expect(!Shown(Reading{}, false), "nothing read: not shown");
        // The straightness asked for high (94 %): a real poke at 80 % fails it, and is still a thrust attempt to be shown (it was hidden: no tint, no log line).
        thrust::Rule strict = r;
        strict.alongPct = 94.0f;
        const Reading crookedJab = JabReading(strict, kSpear, 1.5f, 10.0f, 80.0f, 40.0f, 9, 1);
        Expect(crookedJab.fail == Fail::NotStraight && Shown(crookedJab, true), "only thrusts, straightness 94 %: a poke at 80 % is shown (not straight 80 / 94)");
        const Reading halfWay = JabReading(strict, kSpear, 1.5f, 10.0f, 49.0f, 40.0f, 9, 1);
        Expect(!Shown(halfWay, true), "only thrusts: under half along the blade is a swing's movement, hidden");
    }
    Reading none;
    n = Lines(none, "TOUCH", l, 8);
    Expect(n == 1 && Has(l, n, "TOUCH: none yet", kGrey), "nothing yet: one grey line");
    Expect(Lines(c, "T", l, 2) == 2 && Lines(c, "T", nullptr, 8) == 0, "the lines stop at max");
    // the jab tracker: peaks while the stroke is on, the finished jab once when it ends
    JabTracker t;
    JabTracker::Peaks done;
    Expect(!t.Feed(0, 0.2f, 0.0f, 0.0f, 0.0f, &done), "no stroke: nothing");
    t.Feed(4, 0.9f, 1.0f, 80.0f, 20.0f, &done);
    t.Feed(4, 1.3f, 4.0f, 90.0f, 12.0f, &done);
    Expect(t.Current().stroke == 4 && t.Current().speed == 1.3f && t.Current().cm == 4.0f && t.Current().accel == 20.0f && t.Current().alongPct == 90.0f, "peaks while it is on");
    Expect(t.Feed(0, 0.1f, 0.0f, 0.0f, 0.0f, &done) && done.stroke == 4 && done.speed == 1.3f && done.cm == 4.0f, "the jab ends: its peaks given once");
    Expect(!t.Feed(0, 0.1f, 0.0f, 0.0f, 0.0f, &done) && t.Current().stroke == 0, "and not again");
}

// The whole weapon, not its tip (thrust::GripBlade, Path from the grip, TurnDeg): the author's spear (tip 2.06 m ahead of the hand, butt 0.70 m behind) poked 30 cm
// forward with the hand turning 10 deg on the way - what his eleven real pokes did (2026-10-22: 51 - 76 % "straight" by the tip, refused at 70 %).
static Vec Yawed(Vec forward, float deg) {
    const float a = deg * 0.0174532925f;
    return {forward.x * std::cos(a) + forward.z * std::sin(a), forward.y, -forward.x * std::sin(a) + forward.z * std::cos(a)};
}
static void WholeWeaponTests() {
    // GripBlade: the hand on the weapon's axis, kGripAheadM toward the tip
    const Blade spear{{0.0f, 1.2f, -0.70f}, {0.0f, 1.2f, 2.06f}, 0.016f};
    Blade g = thrust::GripBlade(spear, {0.03f, 1.2f, 0.0f});   // the hand 3 cm off the axis, at the bone
    Expect(Length(g.base - Vec{0.0f, 1.2f, 0.0f}) < 1e-4f && Length(g.tip - Vec{0.0f, 1.2f, 0.5f}) < 1e-4f, "the grip: the hand on the axis, half a metre ahead");
    const Blade dagger{{0.0f, 1.2f, 0.0f}, {0.0f, 1.2f, 0.28f}, 0.02f};
    g = thrust::GripBlade(dagger, {0.0f, 1.2f, 0.0f});
    Expect(Length(g.tip - dagger.tip) < 1e-4f, "a dagger is shorter than the lever: its own tip");
    Expect(Length(thrust::GripBlade(dagger, {NAN, 0.0f, 0.0f}).tip - dagger.tip) < 1e-6f, "a hand that does not read: the blade as drawn");

    // The poke: 30 cm forward in 0.2 s (eased), the hand turning 10 deg.
    const Vec ahead{0.0f, 0.0f, 1.0f};
    thrust::Path path;
    double t = 5.0;
    float tipAlong = 0.0f, tipPath = 0.0f;   // the old measure, by the tip: what refused his pokes
    Vec lastTip{};
    for (int i = 0; i <= 18; ++i) {
        const float k = static_cast<float>(i) / 18.0f, ease = 0.5f - 0.5f * std::cos(k * 3.14159265f);
        const Vec hand = Vec{0.0f, 1.2f, 0.0f} + ahead * (0.30f * ease);
        const Vec axis = Yawed(ahead, 10.0f * k);
        const Blade room{hand - axis * 0.70f, hand + axis * 2.06f, 0.016f};
        const Blade grip = thrust::GripBlade(room, hand);
        path.Feed(t, grip.base, room.tip, 1u);
        if (i) {
            const Vec step = room.tip - lastTip;
            tipAlong += std::max(0.0f, Dot(step, axis));
            tipPath += Length(step);
        }
        lastTip = room.tip;
        t += 1.0 / 90.0;
    }
    const thrust::Rule r;   // the shipped numbers: 70 % straight, 5 cm, 0.8 m/s
    const thrust::Verdict v = thrust::Judge(r, kSpear, path);
    Expect(tipAlong / tipPath < 0.70f, "by the tip the poke reads under 70 % - why his pokes were refused", tipAlong / tipPath);
    Expect(v.share > 0.95f && std::fabs(v.turnDeg - 10.0f) < 1.0f && v.alongCm > 29.0f, "by the grip: nearly all along the weapon, 10 deg of turn, 30 cm", v.share);
    Expect(v.why == thrust::Why::Counts, "the poke counts at the shipped 70 %", static_cast<double>(v.why));

    // A swing: the hand barely moves, the weapon turns 70 deg - not a thrust.
    thrust::Path swing;
    t = 9.0;
    for (int i = 0; i <= 18; ++i) {
        const float k = static_cast<float>(i) / 18.0f;
        const Vec hand = Vec{0.0f, 1.2f, 0.0f} + Vec{0.10f * k, 0.0f, 0.05f * k};
        const Vec axis = Yawed(ahead, 70.0f * k);
        swing.Feed(t, hand, hand + axis * 2.06f, 1u);
        t += 1.0 / 90.0;
    }
    const thrust::Verdict sv = thrust::Judge(r, kSpear, swing);
    Expect(sv.why != thrust::Why::Counts && sv.turnDeg > 60.0f, "a swing: the weapon turned 70 deg - no thrust", sv.turnDeg);
    // The same push straight on, but the weapon turned 40 deg on the way: refused for the turn alone.
    thrust::Path turned;
    t = 12.0;
    for (int i = 0; i <= 18; ++i) {
        const float k = static_cast<float>(i) / 18.0f;
        const Vec hand = Vec{0.0f, 1.2f, 0.0f} + ahead * (0.30f * k);
        turned.Feed(t, hand, hand + Yawed(ahead, 40.0f * k) * 2.06f, 1u);
        t += 1.0 / 90.0;
    }
    const thrust::Verdict tv = thrust::Judge(r, kSpear, turned);
    Expect(tv.why == thrust::Why::Turned || tv.why == thrust::Why::NotAlong, "a push with the weapon turning 40 deg: refused", tv.turnDeg);
    Expect(std::strcmp(thrust::WhyText(thrust::Why::Turned), "a thrust") != 0, "the turn's own text");

    // [gameplay] melee_thrust_with_run: the hands still in the room, the hero running 3 m/s with the spear held point first - a charge.
    Expect(std::fabs(thrust::GripAt(spear, {0.0f, 1.2f, 0.0f}) - 0.70f / 2.76f) < 1e-4f && thrust::GripAt(spear, {NAN, 0.0f, 0.0f}) < 0.0f, "GripAt: the grip's part of the blade");
    thrust::Path inRoom, inWorld;
    t = 20.0;
    const Blade still{{0.0f, 1.2f, -0.70f}, {0.0f, 1.2f, 2.06f}, 0.016f};
    const float at = thrust::GripAt(still, {0.0f, 1.2f, 0.0f});
    for (int i = 0; i <= 18; ++i) {
        const Vec run = ahead * (3.0f * static_cast<float>(i) / 90.0f);   // the hero's run in the world
        const Blade world{still.base + run, still.tip + run, still.radius};
        const Blade gRoom = thrust::GripBladeAt(still, at), gWorld = thrust::GripBladeAt(world, at);
        inRoom.Feed(t, gRoom.base, still.tip, 1u);
        inWorld.Feed(t, gWorld.base, world.tip, 1u);
        t += 1.0 / 90.0;
    }
    const thrust::Verdict rv = thrust::Judge(r, kSpear, inRoom), wv = thrust::Judge(r, kSpear, inWorld);
    Expect(rv.why != thrust::Why::Counts && rv.alongCm < 0.1f, "measured in the room: the run is no thrust (the option off)", rv.alongCm);
    Expect(wv.why == thrust::Why::Counts && wv.alongCm > 59.0f && wv.share > 0.99f, "measured in the world: a 60 cm charge along the spear counts", wv.alongCm);
    // Backing AWAY from a monster with the spear still pointed at it (the author: "the movement away from the monster must not poke it"): no thrust.
    thrust::Path away;
    t = 25.0;
    for (int i = 0; i <= 18; ++i) {
        const Vec run = ahead * (-3.0f * static_cast<float>(i) / 90.0f);
        const Blade world{still.base + run, still.tip + run, still.radius};
        away.Feed(t, thrust::GripBladeAt(world, at).base, world.tip, 1u);
        t += 1.0 / 90.0;
    }
    const thrust::Verdict av = thrust::Judge(r, kSpear, away);
    Expect(av.why == thrust::Why::NotAlong && av.alongCm < 0.1f && av.stepCos < -0.9f, "backing away with the spear forward: not a thrust (nothing forward along it)", av.alongCm);

    // A long charge: 3 s of running at 3 m/s, the hero steering 60 deg on the way (20 deg/s) - one stroke. The turn is the last quarter second's, so the steering
    // does not add up to a "turned" refusal at the touch (it did when the turn was counted from the stroke's start).
    thrust::Path charge;
    t = 30.0;
    Vec where{0.0f, 1.2f, 0.0f};
    for (int i = 0; i <= 270; ++i) {
        const Vec axis = Yawed(ahead, 20.0f * static_cast<float>(i) / 90.0f);
        if (i) where = where + axis * (3.0f / 90.0f);
        const Blade world{where - axis * 0.70f, where + axis * 2.06f, 0.016f};
        const Blade gw = thrust::GripBladeAt(world, at);
        charge.Feed(t, gw.base, world.tip, 1u);
        t += 1.0 / 90.0;
    }
    const thrust::Verdict cv = thrust::Judge(r, kSpear, charge);
    Expect(cv.turnDeg < 7.0f && cv.why == thrust::Why::Counts, "a 3 s charge steering 60 deg: the last quarter second turned ~5 deg - counts", cv.turnDeg);
}

int main(int argc, char** argv) {
    g_verbose = argc > 1 && argv[1][0] == '-' && argv[1][1] == 'v';
    WholeWeaponTests();
    RuleTests();
    ClassTests();
    PathTests();
    LoopTests();
    AccelTests();
    DebugTests();
    if (g_failures == 0) std::printf("thrust_stroke_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
