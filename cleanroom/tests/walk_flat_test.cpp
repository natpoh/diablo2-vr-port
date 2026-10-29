// Offline checks of vr/walk_flat.h and the view gate behind it: walking with the controller's left stick in VR F1, F2, F3 without BodyWalk
// ([direct] walk_flat_views). No game, no OpenXR, no headset.
// Run: build/Release/walk_flat_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <thread>

#include "../../vr/view_gate.h"
#include "../../vr/walk_flat.h"
#include "../../vr/walk_path.h"

using namespace walkflat;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

// What a flat view with everything in order looks like; a test turns one thing off.
static Inputs Good() {
    Inputs in;
    in.keyOn = true; in.directOn = true; in.flatView = true; in.framesFresh = true;
    in.bodyWalkRunning = false; in.inWorld = true; in.menuOpen = false; in.chatOpen = false;
    return in;
}

static void TestStick() {
    int16_t lx = 1, ly = 1;
    StickToPad(0.0f, 1.0f, &lx, &ly);
    CHECK(lx == 0 && ly == 32767);
    StickToPad(0.0f, -1.0f, &lx, &ly);
    CHECK(lx == 0 && ly == -32767);
    StickToPad(-1.0f, 0.0f, &lx, &ly);
    CHECK(lx == -32767 && ly == 0);
    StickToPad(1.0f, 1.0f, &lx, &ly);   // a corner comes into the unit disc, the direction kept
    CHECK(Near(lx / 32767.0f, 0.7071f) && Near(ly / 32767.0f, 0.7071f));
    StickToPad(3.0f, 4.0f, &lx, &ly);
    CHECK(Near(lx / 32767.0f, 0.6f) && Near(ly / 32767.0f, 0.8f));
    StickToPad(0.3f, -0.4f, &lx, &ly);  // inside the disc: as it is
    CHECK(Near(lx / 32767.0f, 0.3f) && Near(ly / 32767.0f, -0.4f));
    StickToPad(NAN, 1.0f, &lx, &ly);
    CHECK(lx == 0 && ly == 0);
    StickToPad(0.5f, INFINITY, &lx, &ly);
    CHECK(lx == 0 && ly == 0);
    StickToPad(0.0f, 0.0f, &lx, &ly);
    CHECK(lx == 0 && ly == 0);
    // the game's own dead zone is the game's: a light push past it is a walk in the pad
    StickToPad(0.0f, 0.4f, &lx, &ly);
    CHECK(walkpath::PushedRaw(lx, ly));
    StickToPad(0.0f, 0.2f, &lx, &ly);
    CHECK(!walkpath::PushedRaw(lx, ly));
}

static void TestDecide() {
    CHECK(Decide(Good()) == Verdict::Walk);
    Inputs in = Good();
    in.keyOn = false;
    CHECK(Decide(in) == Verdict::Off);                       // the key off: today's behaviour
    in = Good(); in.directOn = false;
    CHECK(Decide(in) == Verdict::Off);                       // [openxr] direct_input=0: not ours
    in = Good(); in.flatView = false;
    CHECK(Decide(in) == Verdict::NotFlatView);               // F4 or the flat game
    in = Good(); in.bodyWalkRunning = true;
    CHECK(Decide(in) == Verdict::BodyWalk);                  // BodyWalk running: nothing changes
    in = Good(); in.framesFresh = false;
    CHECK(Decide(in) == Verdict::NoFrames);
    in = Good(); in.inWorld = false;
    CHECK(Decide(in) == Verdict::NoGame);
    in = Good(); in.menuOpen = true;
    CHECK(Decide(in) == Verdict::Menu);
    in = Good(); in.chatOpen = true;
    CHECK(Decide(in) == Verdict::Chat);
    // the order of reasons: the key and the view come first, then BodyWalk, then the facts of the moment
    in = Good(); in.keyOn = false; in.flatView = false; in.bodyWalkRunning = true;
    CHECK(Decide(in) == Verdict::Off);
    in = Good(); in.flatView = false; in.bodyWalkRunning = true; in.menuOpen = true;
    CHECK(Decide(in) == Verdict::NotFlatView);
    in = Good(); in.bodyWalkRunning = true; in.framesFresh = false; in.menuOpen = true; in.chatOpen = true;
    CHECK(Decide(in) == Verdict::BodyWalk);
    in = Good(); in.framesFresh = false; in.inWorld = false; in.menuOpen = true;
    CHECK(Decide(in) == Verdict::NoFrames);
    in = Good(); in.inWorld = false; in.menuOpen = true;
    CHECK(Decide(in) == Verdict::NoGame);
    in = Good(); in.menuOpen = true; in.chatOpen = true;
    CHECK(Decide(in) == Verdict::Menu);
    // nothing but Walk walks
    int walks = 0;
    for (int m = 0; m < 256; ++m) {
        Inputs x;
        x.keyOn = m & 1; x.directOn = m & 2; x.flatView = m & 4; x.framesFresh = m & 8;
        x.bodyWalkRunning = m & 16; x.inWorld = m & 32; x.menuOpen = m & 64; x.chatOpen = m & 128;
        const bool should = (m & 1) && (m & 2) && (m & 4) && (m & 8) && !(m & 16) && (m & 32) && !(m & 64) && !(m & 128);
        if (Decide(x) == Verdict::Walk) ++walks;
        CHECK((Decide(x) == Verdict::Walk) == should);
    }
    CHECK(walks == 1);
    // every verdict has words for the log
    for (int i = 0; i <= (int)Verdict::Chat; ++i) CHECK(Name((Verdict)i)[0] != 0);
}

static void TestLog() {
    VerdictLog l;
    Verdict say = Verdict::Walk;
    uint32_t changes = 0;
    // a view that is not ours is no news
    CHECK(!l.Step(Verdict::NotFlatView, 1000, &say, &changes));
    CHECK(!l.Step(Verdict::Off, 1001, &say, &changes));
    // the first verdict is said at once
    CHECK(l.Step(Verdict::Walk, 1002, &say, &changes) && say == Verdict::Walk && changes == 1);
    CHECK(!l.Step(Verdict::Walk, 1003, &say, &changes));     // the same again: nothing
    CHECK(l.Step(Verdict::Menu, 1004, &say, &changes) && say == Verdict::Menu && changes == 1);
    CHECK(l.Step(Verdict::Walk, 1005, &say, &changes) && say == Verdict::Walk);
    // away (F4) and back with the same verdict: said again, it is news in a new visit
    CHECK(!l.Step(Verdict::NotFlatView, 2000, &say, &changes));
    CHECK(l.Step(Verdict::Walk, 2001, &say, &changes) && say == Verdict::Walk);
    // ... also when the pad poll simply did not call for a while (it calls only inside a flat view)
    CHECK(!l.Step(Verdict::Walk, 2002, &say, &changes));
    CHECK(l.Step(Verdict::Walk, 2002 + VerdictLog::kGapMs + 1, &say, &changes) && say == Verdict::Walk);
    CHECK(!l.Step(Verdict::Walk, 2002 + VerdictLog::kGapMs + 2, &say, &changes));
    // a flapping verdict: the head at once, then one line in 5 s standing for the changes since
    VerdictLog f;
    uint32_t said = 0;
    for (int i = 0; i < 200; ++i)
        if (f.Step(i & 1 ? Verdict::Menu : Verdict::Walk, 1000, &say, &changes)) ++said;
    CHECK(said == VerdictLog::kHead && f.Changes() == 200 && f.Lines() == VerdictLog::kHead);
    CHECK(!f.Step(Verdict::Walk, 2500, &say, &changes));
    CHECK(!f.Step(Verdict::Menu, 4000, &say, &changes));
    CHECK(!f.Step(Verdict::Walk, 5500, &say, &changes));
    CHECK(f.Step(Verdict::Menu, 6000, &say, &changes) && say == Verdict::Menu && changes == 174);   // 170 swallowed in the head's burst, 4 since
    CHECK(f.Lines() == VerdictLog::kHead + 1 && f.Changes() == 204);
}

static void TestPack() {
    int16_t lx, ly;
    uint32_t c;
    for (int16_t a : {(int16_t)0, (int16_t)1, (int16_t)-1, (int16_t)32767, (int16_t)-32767, (int16_t)-32768}) {
        for (int16_t b : {(int16_t)0, (int16_t)12345, (int16_t)-12345, (int16_t)32767, (int16_t)-32768}) {
            Unpack(Pack(a, b, 0xDEADBEEFu), &lx, &ly, &c);
            CHECK(lx == a && ly == b && c == 0xDEADBEEFu);
        }
    }
    Unpack(0, &lx, &ly, &c);   // nothing published
    CHECK(lx == 0 && ly == 0 && c == 0);
}

static bool g_f4 = false, g_flat = true;
static bool TheF4() { return g_f4; }
static bool TheFlat() { return g_flat; }

static void TestViewGate() {
    // nothing registered: F4 true (every module is tested as it acts in F4), the flat views false (the walk is not there)
    viewgate::Set(nullptr);
    viewgate::SetFlat(nullptr);
    CHECK(viewgate::F4() && !viewgate::Flat());
    viewgate::Set(&TheF4);
    viewgate::SetFlat(&TheFlat);
    g_f4 = false; g_flat = true;     // F1, F2, F3
    CHECK(!viewgate::F4() && viewgate::Flat());
    g_f4 = true; g_flat = false;     // F4
    CHECK(viewgate::F4() && !viewgate::Flat());
    g_f4 = false; g_flat = false;    // the flat game, the title: neither
    CHECK(!viewgate::F4() && !viewgate::Flat());
    // asked every time, from any thread
    g_flat = true;
    bool seen = false;
    std::thread([&] { seen = viewgate::Flat(); }).join();
    CHECK(seen);
    g_flat = false;
    std::thread([&] { seen = viewgate::Flat(); }).join();
    CHECK(!seen);
    // the two registrations are independent
    viewgate::Set(nullptr);
    g_flat = true;
    CHECK(viewgate::F4() && viewgate::Flat());
    viewgate::SetFlat(nullptr);
    CHECK(!viewgate::Flat());
}

// The walk's journey through the plugin, on the numbers: the stick the headset gives -> the pad's left stick -> the game's dead zone.
static void TestToTheGame() {
    int16_t lx, ly;
    // a light push is below the game's dead zone: the pad carries it, the game ignores it, the hero stands
    StickToPad(0.0f, 0.25f, &lx, &ly);
    CHECK(!walkpath::PushedRaw(lx, ly));
    // a push past the dead zone walks, in whatever direction
    for (float a = 0.0f; a < 6.28f; a += 0.3f) {
        StickToPad(std::cos(a) * 0.9f, std::sin(a) * 0.9f, &lx, &ly);
        CHECK(walkpath::PushedRaw(lx, ly));
    }
    // the stick turned into the game's frame the way BodyWalk's stick is: the same helper, the length kept
    float x = 0.0f, y = 1.0f;
    CHECK(walkpath::TurnByYaw(&x, &y, 90.0f, 1.0f, 1.0f, 1.0f));
    CHECK(Near(std::sqrt(x * x + y * y), 1.0f));
}

// A pad as a game hands it: every field set to something recognisable.
static PadBits PadWith(uint16_t buttons, uint8_t lt, uint8_t rt, int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    PadBits p;
    p.buttons = buttons; p.lt = lt; p.rt = rt; p.lx = lx; p.ly = ly; p.rx = rx; p.ry = ry;
    return p;
}
static bool SamePad(const PadBits& a, const PadBits& b) {
    return a.buttons == b.buttons && a.lt == b.lt && a.rt == b.rt && a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry;
}

// The pad the game gets in VR F1-F3 (2026-10-15): BodyWalk running = untouched, bit for bit; otherwise the plugin's own controller, added to a pad that
// answers (never over it), or alone when none does.
static void TestPassThrough() {
    // a pad with everything pressed: A, B, X, Y, the bumpers, RT, LT, the right stick
    const PadBits busy = PadWith(0x1000 | 0x2000 | 0x4000 | 0x8000 | 0x0100 | 0x0200, 200, 250, 0, 0, 12000, -30000);
    // the plugin's controller: the right trigger's attack (pad A), the left stick walking, nothing else
    const PadBits ours = PadWith(0x1000, 0, 0, 0, 32767, 0, 0);
    // BodyWalk running (and every other verdict that is not the walk): nothing is touched whatever ours offers
    for (Verdict v : {Verdict::BodyWalk, Verdict::Off, Verdict::NotFlatView, Verdict::NoFrames, Verdict::NoGame, Verdict::Menu, Verdict::Chat}) {
        CHECK(ChooseSource(v, false) == PadSource::AsBefore && ChooseSource(v, true) == PadSource::AsBefore);
        CHECK(SamePad(Compose(ChooseSource(v, true), busy, ours), busy));
        CHECK(SamePad(Compose(ChooseSource(v, false), busy, ours), busy));
    }
    // no BodyWalk, no pad answering: the plugin's pad, whole - the right-hand controls in it
    CHECK(ChooseSource(Verdict::Walk, false) == PadSource::OurPad);
    const PadBits mine = PadWith(0x1000 | 0x2000, 255, 0, 0, 32767, 9000, -9000);   // the attack, B, LT, the walk, the right stick
    CHECK(SamePad(Compose(PadSource::OurPad, busy, mine), mine));
    CHECK(SamePad(Compose(PadSource::OurPad, PadBits{}, mine), mine));
    // the walk alone ([direct] flat_controller=0): the left stick and nothing else
    const PadBits walk = WalkPad(-32767, 0);
    CHECK(walk.lx == -32767 && walk.ly == 0 && walk.buttons == 0 && walk.lt == 0 && walk.rt == 0 && walk.rx == 0 && walk.ry == 0);
    // a pad answers as well (a real one, or BodyWalk's virtual one with its Game Link not alive): ours is added, never over it
    CHECK(ChooseSource(Verdict::Walk, true) == PadSource::RealPlusOurs);
    PadBits got = Compose(PadSource::RealPlusOurs, busy, mine);
    CHECK(got.buttons == busy.buttons && got.lt == 255 && got.rt == 250 && got.lx == 0 && got.ly == 32767 && got.rx == 12000 && got.ry == -30000);   // its right stick is pushed: it keeps its own
    const PadBits quiet = PadWith(0x0100, 10, 0, 0, 0, 0, 0);
    got = Compose(PadSource::RealPlusOurs, quiet, mine);
    CHECK(got.buttons == (0x0100 | 0x1000 | 0x2000) && got.lt == 255 && got.rt == 0 && got.lx == 0 && got.ly == 32767 && got.rx == 9000 && got.ry == -9000);
    // its own left stick pushed: the pad's walk, not ours (its right stick rests - ours is taken there)
    const PadBits walking = PadWith(0x1000, 0, 255, -20000, 15000, 5000, 5000);
    got = Compose(PadSource::RealPlusOurs, walking, mine);
    CHECK(got.lx == -20000 && got.ly == 15000 && got.rx == 9000 && got.ry == -9000 && got.buttons == (0x1000 | 0x2000) && got.rt == 255 && got.lt == 255);
    // ours at rest changes nothing
    PadBits unchanged = busy;
    CHECK(!MergePads(&unchanged, PadBits{}) && SamePad(unchanged, busy));
    CHECK(!MergePads(&unchanged, WalkPad(3000, -3000)) && SamePad(unchanged, busy));   // inside the game's dead zone
    // the property, over many pads and controllers: BodyWalk running = the pad bit for bit; a pad answering keeps everything it had (buttons only added to,
    // triggers only raised, a pushed stick of its own kept)
    unsigned seed = 12345u;
    auto next = [&]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    int merged = 0;
    for (int i = 0; i < 20000; ++i) {
        const PadBits pad = PadWith((uint16_t)next(), (uint8_t)next(), (uint8_t)next(), (int16_t)next(), (int16_t)next(), (int16_t)next(), (int16_t)next());
        const PadBits own = PadWith((uint16_t)next(), (uint8_t)next(), (uint8_t)next(), (int16_t)next(), (int16_t)next(), (int16_t)next(), (int16_t)next());
        const Verdict v = (Verdict)(next() % 8);
        const bool answers = next() & 1;
        const PadSource src = ChooseSource(v, answers);
        const PadBits out = Compose(src, pad, own);
        if (src == PadSource::AsBefore) CHECK(SamePad(out, pad));
        if (src == PadSource::OurPad) CHECK(SamePad(out, own));
        if (src == PadSource::RealPlusOurs) {
            CHECK((out.buttons & pad.buttons) == pad.buttons && (out.buttons & ~(pad.buttons | own.buttons)) == 0);
            CHECK(out.lt >= pad.lt && out.rt >= pad.rt);
            if (StickPushed(pad.lx, pad.ly)) CHECK(out.lx == pad.lx && out.ly == pad.ly);
            if (StickPushed(pad.rx, pad.ry)) CHECK(out.rx == pad.rx && out.ry == pad.ry);
            if (!SamePad(out, pad)) ++merged;
        }
    }
    CHECK(merged > 0);
    // the hint's window (the controller off): once in half a minute at most
    CHECK(kHintEveryMs >= 10000);
}

// A gamepad in the hands next to the VR controllers (the Discord report of 2026-10-18: buttons work, the sticks do not): in direct mode the plugin's whole pad is the pad the game
// polls, and used to be the ONLY one. A real pad joins it: buttons ORed, the larger trigger, its pushed stick wins - and the stick the walking hook is given follows its left stick.
static void TestRealPadWithDirect() {
    // the plugin's pad: the right trigger's attack, the left stick at rest, the right stick at rest (the VR controllers are in the hands, nothing pushed)
    const PadBits vrRest = PadWith(0x1000, 0, 0, 200, -300, 150, 90);
    // the gamepad: B held, a bumper, LT half, both sticks pushed (walking and turning)
    const PadBits padPushed = PadWith(0x2000 | 0x0100, 128, 0, -22000, 9000, 25000, -4000);
    PadBits out = padPushed;
    CHECK(MergePads(&out, vrRest));
    CHECK(out.buttons == (0x2000 | 0x0100 | 0x1000));                 // buttons from both
    CHECK(out.lt == 128 && out.rt == 0);                             // the larger trigger
    CHECK(out.lx == -22000 && out.ly == 9000 && out.rx == 25000 && out.ry == -4000);   // its sticks, the VR sticks at rest do not replace them with ~0
    // the reverse: the gamepad lies on the table (sticks at rest, a little drift), the VR sticks are pushed
    const PadBits vrPushed = PadWith(0x4000, 0, 255, 18000, 20000, -26000, 1000);
    const PadBits padRest = PadWith(0, 0, 0, 1200, -900, 300, 700);
    out = padRest;
    CHECK(MergePads(&out, vrPushed));
    CHECK(out.buttons == 0x4000 && out.rt == 255);
    CHECK(out.lx == 18000 && out.ly == 20000 && out.rx == -26000 && out.ry == 1000);   // the VR sticks count where the gamepad's rest
    // both pushed: the gamepad's wins (a hand on the stick is a hand on the stick), the buttons are still both
    out = padPushed;
    MergePads(&out, vrPushed);
    CHECK(out.lx == -22000 && out.ly == 9000 && out.rx == 25000 && out.ry == -4000 && out.buttons == (0x2000 | 0x0100 | 0x4000) && out.rt == 255);
    // nothing pushed on either side: the pad stays as it was (drift inside the game's dead zone is not a push)
    out = padRest;
    CHECK(!MergePads(&out, vrRest) || (out.lx == 1200 && out.ly == -900));
    CHECK(out.lx == 1200 && out.ly == -900 && out.rx == 300 && out.ry == 700);
    // who joins: a poll that answered, and not BodyWalk's virtual pad (its Mapping and gestures are what direct input took out of the game)
    CHECK(RealPadJoins(true, false));
    CHECK(!RealPadJoins(false, false));      // "not connected": nothing to join
    CHECK(!RealPadJoins(true, true));        // BodyWalk's gamepad is in charge: the pad as it was, nothing of a real pad added or the plugin's changed
    CHECK(!RealPadJoins(false, true));
    // the stick the walking hook is given
    int16_t hx = 0, hy = 0;                                            // the VR stick at rest, the gamepad's pushed
    CHECK(FoldRealStick(&hx, &hy, true, -22000, 9000) && hx == -22000 && hy == 9000);
    hx = 15000; hy = 15000;                                            // the VR stick pushed, the gamepad's pushed too: the gamepad's
    CHECK(FoldRealStick(&hx, &hy, true, 0, -30000) && hx == 0 && hy == -30000);
    hx = 15000; hy = 15000;                                            // the gamepad's at rest (drift): the VR stick walks
    CHECK(!FoldRealStick(&hx, &hy, true, 1500, -1200) && hx == 15000 && hy == 15000);
    hx = 15000; hy = 15000;                                            // a gamepad report older than the window (put down, the game stalled): not used
    CHECK(!FoldRealStick(&hx, &hy, false, -22000, 9000) && hx == 15000 && hy == 15000);
    CHECK(RealPadFresh(1000, 900) && RealPadFresh(1000, 850) && !RealPadFresh(1000, 849) && !RealPadFresh(1000, 0) && !RealPadFresh(900, 1000));
    CHECK(kRealPadFreshMs >= 100 && kRealPadFreshMs <= 300);
}

int main() {
    TestStick();
    TestRealPadWithDirect();
    TestPassThrough();
    TestDecide();
    TestLog();
    TestPack();
    TestViewGate();
    TestToTheGame();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("walk_flat_test: all %d checks passed\n", g_checks);
    return 0;
}
