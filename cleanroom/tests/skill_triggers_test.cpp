// Offline checks of [debug] skill_triggers (vr/skill_triggers.h, skill_triggers.cpp): the shield bash's capsule, colours, speed marks, trail, flash, scan,
// budget, the log line and its reasons, and the module's off state and 5 Hz line. No game, no headset.
//
//   skill_triggers_test.exe        the checks (exit code 0 = all passed)

#include "../../vr/melee_bodies.h"
#include "../../vr/skill_triggers.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace skilltriggers;
using stroke::Vec;
// the effect's colours this module shares with melee_boxes (its own names stay in meleeboxes::)
using meleeboxes::kBladeHot;
using meleeboxes::kMarkReached;
using meleeboxes::kMarkSlowed;
using meleeboxes::kMonster;
using meleeboxes::kRefused;
using meleeboxes::kStruckKey;
using meleeboxes::kTrailCold;
using meleeboxes::kTrailHot;
using meleeboxes::kMaxCapsules;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static bool NearV(Vec a, Vec b, float tol) { return stroke::Length(a - b) <= tol; }

constexpr float kUpm = 4.0f;

// A monster `ahead` metres in front of the origin and `side` to the right: its capsule from the game's own shape numbers (a zombie's), as the scan makes it.
static Monster MakeMonster(uint32_t id, uint32_t cls, float ahead, float side, Verdict v = Verdict::Hostile) {
    Monster m;
    m.id = id;
    m.cls = cls;
    m.d2 = (ahead * ahead + side * side) * kUpm * kUpm;
    m.verdict = v;
    Expect(meleetouch::BodyFor({2, 2, 72}, side * kUpm, -ahead * kUpm, 0.0f, kUpm, m.body), "synthetic monster body");
    return m;
}

// The shield at `centre`, as shield_smite.cpp feeds it: its capsule (0.4 m along its axis, r 0.15) and a few numbers.
static BashFrame MakeFrame(uint64_t now, Vec centre, bool counting, const Monster* mons = nullptr, size_t n = 0) {
    BashFrame f;
    f.nowMs = now;
    f.upm = kUpm;
    f.bash.have = true;
    f.bash.blade = {centre - Vec{0.0f, 0.2f, 0.0f}, centre + Vec{0.0f, 0.2f, 0.0f}, 0.15f};
    f.bash.counting = counting;
    f.bash.inStroke = counting;
    f.bash.speed = counting ? 2.0f : 0.1f;
    f.bash.minSpeed = 0.8f;
    f.bash.beginSpeed = 1.2f;
    f.bash.startCm = 8.0f;
    f.bash.slot = 2;
    f.bash.padButtons = 0x8000;
    f.bash.manaNeed = 2.0f;
    f.monsters = mons;
    f.count = n;
    return f;
}
static int Count(const Capsule* c, int n, meleeboxes::Kind kind) {
    int k = 0;
    for (int i = 0; i < n; ++i) k += c[i].kind == kind;
    return k;
}
static int CountRaw(const Capsule* c, int n, int kind) {
    int k = 0;
    for (int i = 0; i < n; ++i) k += static_cast<int>(c[i].kind) == kind;
    return k;
}

static int g_sinkCalls = 0;
static std::string g_sinkLast;
static void Sink(const char* line) { ++g_sinkCalls; g_sinkLast = line; }

int main() {
    const Vec centre{0.3f, 1.1f, -0.4f};
    Capsule c[kMaxCapsules];

    {   // the capsule: violet (kind 11), green while a bash would count; exactly the capsule the bash tests
        Scene s;
        s.SetBash(MakeFrame(1000, centre, false));
        int n = s.Capsules(1010, c, kMaxCapsules);
        Expect(n == 1 && CountRaw(c, n, kKindTrigger) == 1, "capsule: its own colour (violet, kind 11) when no bash would count", n);
        Expect(NearV(c[0].a, centre - Vec{0.0f, 0.2f, 0.0f}, 1e-5f) && NearV(c[0].b, centre + Vec{0.0f, 0.2f, 0.0f}, 1e-5f) && Near(c[0].radius, 0.15f, 1e-6f),
               "capsule: the one drawn is the one tested (+-0.20 m along the axis, r 0.15)");
        s.SetBash(MakeFrame(1010, centre, true));
        n = s.Capsules(1020, c, kMaxCapsules);
        Expect(n >= 1 && c[0].kind == kBladeHot, "capsule: green while a bash would count", c[0].kind);
        Expect(s.Have(), "capsule: known");
        Expect(s.Capsules(1010 + kStaleMs + 1, c, kMaxCapsules) == 0, "capsule: stale after 300 ms without a feed");
        BashFrame none = MakeFrame(1100, centre, false);
        none.bash.have = false;
        s.SetBash(none);
        Expect(s.Capsules(1110, c, kMaxCapsules) == 0, "capsule: not drawn when the arm has no shield");
        Expect(s.Capsules(1110, c, 0) == 0, "capsule: a budget of none draws none");
    }
    {   // the speed marks and the trail: white where the speed was reached, orange where it fell below, the path green / cyan - at the shield's centre
        Scene s;
        uint64_t t = 5000;
        Vec at = centre;
        for (int phase = 0; phase < 3; ++phase)
            for (int i = 0; i < 30; ++i, t += 10) {
                at.x += 0.02f;
                s.SetBash(MakeFrame(t, at, phase == 1));   // counting only in the middle phase
            }
        const int n = s.Capsules(t, c, kMaxCapsules);
        Expect(Count(c, n, kMarkReached) >= 1, "marks: a white ball where the speed was reached", Count(c, n, kMarkReached));
        Expect(Count(c, n, kMarkSlowed) >= 1, "marks: an orange ball where it fell below", Count(c, n, kMarkSlowed));
        Expect(Count(c, n, kTrailHot) >= 1 && Count(c, n, kTrailCold) >= 1, "trail: green where a bash would count, cyan where not");
        bool atCentre = false;
        for (int i = 0; i < n; ++i)
            if (c[i].kind == kMarkReached) atCentre = atCentre || std::fabs(0.5f * (c[i].a.y + c[i].b.y) - centre.y) < 0.01f;
        Expect(atCentre, "marks: at the shield's centre (not its tip)");
        Expect(Count(c, n, kBladeHot) + CountRaw(c, n, kKindTrigger) == 1, "marks: one shield capsule", n);
        Expect(n <= kShare, "marks: within the share", n);
        Scene off;   // [debug] skill_mark_ms 0: no marks, no trail
        for (int i = 0; i < 20; ++i) {
            BashFrame f = MakeFrame(9000 + 10u * i, centre + Vec{0.02f * i, 0.0f, 0.0f}, i > 5);
            f.markMs = 0;
            off.SetBash(f);
        }
        Expect(off.Capsules(9200, c, kMaxCapsules) == 1, "marks: skill_mark_ms 0 leaves the capsule only");
        Scene shorter;   // marks older than the time given are gone
        for (int i = 0; i < 40; ++i) {
            BashFrame f = MakeFrame(100 + 10u * i, centre + Vec{0.02f * i, 0.0f, 0.0f}, i > 10 && i < 25);
            f.markMs = 200;
            shorter.SetBash(f);
        }
        const int k = shorter.Capsules(100 + 10u * 39, c, kMaxCapsules);
        Expect(Count(c, k, kMarkReached) == 0, "marks: the white ball is gone after skill_mark_ms", Count(c, k, kMarkReached));
    }
    {   // the flash: the monster a bash pressed Smite on shows red for half a second, from the capsule given
        Scene s;
        s.SetBash(MakeFrame(7000, centre, true));
        const Monster m = MakeMonster(5, 21, 1.0f, 0.4f);
        s.StruckBody(5, kStruckKey, 7001, m.body);
        int n = s.Capsules(7010, c, kMaxCapsules);
        Expect(Count(c, n, kStruckKey) == 1, "flash: the monster a bash hit is red", Count(c, n, kStruckKey));
        s.SetBash(MakeFrame(7400, centre, false));
        n = s.Capsules(7410, c, kMaxCapsules);
        Expect(Count(c, n, kStruckKey) == 1, "flash: still red 400 ms later");
        s.SetBash(MakeFrame(7600, centre, false));
        n = s.Capsules(7610, c, kMaxCapsules);
        Expect(Count(c, n, kStruckKey) == 0, "flash: gone after half a second", Count(c, n, kStruckKey));
    }
    {   // the scan: the nearest monsters are drawn (pale when the server refuses them), the one flashed is not drawn twice
        Scene s;
        const Monster mons[3] = {MakeMonster(1, 10, 3.0f, 0.0f), MakeMonster(2, 11, 1.0f, 0.5f, Verdict::Refused), MakeMonster(3, 12, 2.0f, -0.5f, Verdict::Unknown)};
        s.SetBash(MakeFrame(100, centre, false, mons, 3));
        int n = s.Capsules(110, c, kMaxCapsules);
        Expect(n == 4 && Count(c, n, kMonster) == 2 && Count(c, n, kRefused) == 1, "scan: the shield and three monsters, one of them pale", n);
        Expect(NearV(c[1].a, mons[1].body.bottom, 1e-5f), "scan: the nearest first");
        s.StruckBody(2, kStruckKey, 105, mons[1].body);
        n = s.Capsules(110, c, kMaxCapsules);
        Expect(Count(c, n, kStruckKey) == 1 && Count(c, n, kRefused) == 0 && n == 4, "scan: a flashed monster is red, not drawn twice", n);
    }
    {   // the budget: a crowd, a bash in progress with its marks and trail - never over the share, the nearest monsters still there
        Scene s;
        std::vector<Monster> crowd;
        for (uint32_t i = 0; i < 40; ++i) crowd.push_back(MakeMonster(100 + i, 10 + i, 1.0f + 0.1f * i, 0.3f * (i % 5)));
        uint64_t t = 100;
        Vec at = centre;
        for (int i = 0; i < 80; ++i, t += 10) {
            at.x += 0.02f;
            s.SetBash(MakeFrame(t, at, (i / 7) % 2, crowd.data(), crowd.size()));
        }
        s.StruckBody(100, kStruckKey, t, crowd[0].body);
        s.StruckBody(101, kStruckKey, t, crowd[1].body);
        const int n = s.Capsules(t, c, kMaxCapsules);
        Expect(n <= kShare, "budget: never more than the share", n);
        Expect(n == kShare || n > 8, "budget: and the share is used", n);
        Expect(Count(c, n, kStruckKey) == 2, "budget: both flashes there", Count(c, n, kStruckKey));
        Expect(Count(c, n, kMonster) + Count(c, n, kStruckKey) >= kNearestAlways, "budget: the nearest monsters are not squeezed out", Count(c, n, kMonster));
        const int fewer = s.Capsules(t, c, 6);
        Expect(fewer <= 6 && fewer >= 1, "budget: a smaller share is kept to", fewer);
    }
    {   // the log line: what it says, and why a bash would not count
        Scene s;
        BashFrame f = MakeFrame(3000, centre, false);
        f.bash.speed = 0.4f;
        f.bash.rightTrigger = 0.9f;
        f.bash.mana = 1.0f;
        f.bash.padLt = true;
        f.bash.haveNear = true;
        f.bash.nearCls = 12;
        f.bash.nearHeroM = 1.4f;
        f.bash.nearGapM = 0.25f;
        f.bash.nearVerdict = Verdict::Refused;
        f.bash.why = kWhySlow | kWhyTrigger | kWhyNoMana | kWhyNotHostile;
        s.SetBash(f);
        char line[900];
        Expect(s.DescribeBash(line, sizeof line, 3010), "log: a line when the shield is fed");
        const std::string text = line;
        Expect(text.find("skill trigger shield bash:") == 0, "log: starts with the name", 0);
        Expect(text.find("0.40 m/s") != std::string::npos && text.find("bash speed 0.80") != std::string::npos && text.find("begins at 1.20") != std::string::npos &&
                   text.find("start 8 cm") != std::string::npos,
               "log: the shield's speed, the bash speed, where a stroke begins and its start distance");
        Expect(text.find("no stroke") != std::string::npos, "log: the stroke state");
        Expect(text.find("right trigger 0.90") != std::string::npos && text.find("Smite slot 2 = pad 0x8000 + LT") != std::string::npos, "log: the trigger, the slot and its pad button");
        Expect(text.find("mana 1.0 (Smite needs 2.0)") != std::string::npos, "log: the mana against Smite's cost");
        Expect(text.find("cls 12 refused") != std::string::npos && text.find("gap +0.25") != std::string::npos && text.find("1.4 m from the hero") != std::string::npos,
               "log: the nearest monster");
        Expect(text.find("would NOT count: too slow, right trigger down, no mana, nearest not hostile") != std::string::npos, "log: the reasons, in order");
        BashFrame ok = MakeFrame(3300, centre, true);
        ok.bash.why = 0;
        s.SetBash(ok);
        Expect(s.DescribeBash(line, sizeof line, 3310) && std::string(line).find("COUNTS") != std::string::npos && std::string(line).find("a bash would count") != std::string::npos,
               "log: COUNTS and 'a bash would count' when nothing is in the way");
        Expect(std::string(line).find("no monster in the scan") != std::string::npos && std::string(line).find("not read (Smite needs 2.0)") != std::string::npos,
               "log: no monster, no mana reading");
        Expect(!s.DescribeBash(line, sizeof line, 3300 + kStaleMs + 1), "log: nothing when the feed is stale");
        char why[kWhyTextMax];
        WhyText(0, why, sizeof why);
        Expect(std::string(why) == "none", "why: 0 is none");
        WhyText(kWhyOff, why, sizeof why);
        Expect(std::string(why).find("shield_smite is off") != std::string::npos, "why: off");
        WhyText(0xFFFFFFFFu, why, sizeof why);
        Expect(std::string(why).find("nearest not hostile") != std::string::npos, "why: every reason fits in the buffer");
    }
    {   // the module: off by default, the share, a 5 Hz line, the colours it hands the effect
        g_sinkCalls = 0;
        SetLogSink(&Sink);
        SetWanted(false);
        UpdateBash(MakeFrame(1000, centre, false));
        float a[kMaxCapsules][4], b[kMaxCapsules][4];
        const float eye[3] = {0.0f, 0.0f, 0.0f};
        Expect(Take(1010, eye, a, b) == 0 && g_sinkCalls == 0, "module: off - nothing kept, nothing logged");
        SetWanted(true);
        UpdateBash(MakeFrame(2000, centre, true));
        Expect(g_sinkCalls == 1 && g_sinkLast.find("skill trigger shield bash:") == 0, "module: the first line at once", g_sinkCalls);
        UpdateBash(MakeFrame(2100, centre, true));
        UpdateBash(MakeFrame(2199, centre, true));
        Expect(g_sinkCalls == 1, "module: not faster than 5 Hz", g_sinkCalls);
        UpdateBash(MakeFrame(2200, centre, true));
        Expect(g_sinkCalls == 2, "module: the next 200 ms later", g_sinkCalls);
        int n = Take(2210, eye, a, b);
        Expect(n >= 1 && Near(b[0][3], static_cast<float>(kBladeHot), 1e-6f), "module: green while counting", n);
        UpdateBash(MakeFrame(2300, centre, false));
        n = Take(2310, eye, a, b);
        Expect(n >= 1 && static_cast<int>(b[0][3]) == 11, "module: the shield's own colour is kind 11", n);
        Expect(Near(a[0][3], 0.15f * kUpm, 1e-5f), "module: the radius in world units");
        Expect(Take(2310, eye, a, b, 1) == 1, "module: a share of one is kept to");
        Expect(Take(2310, eye, a, b, 0) == 0, "module: a share of none draws none");
        StruckBody(7, kStruckKey, 2310, MakeMonster(7, 21, 1.0f, 0.3f).body);
        const int k = Take(2320, eye, a, b);
        bool red = false;
        for (int i = 0; i < k; ++i) red = red || static_cast<int>(b[i][3]) == kStruckKey;
        Expect(red, "module: a bash's monster is red in Take");
        SetWanted(false);
        Expect(Take(2330, eye, a, b) == 0, "module: off again - nothing");
        SetWanted(true);
        Expect(Take(2330, eye, a, b) == 0, "module: on again - nothing left over from before");
        SetWanted(false);
        SetLogSink(nullptr);
    }
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("skill_triggers_test: all passed\n");
    return 0;
}
