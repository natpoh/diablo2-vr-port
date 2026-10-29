// Offline checks of vr/walk_path.h: BodyWalk's walking through the plugin into the game, on a synthetic pad - a fake pad report of BodyWalk
// and a fake stick of the controller in, the game's walking stick and its command out - in direct mode and not, with and without the
// stray-walk guard, in every view, with a panel open and without the game.
// Run: build/Release/walk_path_test.exe (exit code 0 = passed)

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>

#include "../../vr/pad_presence.h"
#include "../../vr/stray_walk.h"
#include "../../vr/view_gate.h"
#include "../../vr/walk_path.h"

using namespace walkpath;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool Near(float a, float b, float eps = 0.002f) { return std::fabs(a - b) <= eps; }
static bool NearS(Stick a, Stick b, float eps = 0.002f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }

// -- the synthetic world: what the plugin and the game do with the report ---------------------------------------------------------------
static bool g_f4 = true;
static bool TheView() { return g_f4; }

struct World {
    bool f4 = true;               // the view the player is in (F4 = VR first person)
    bool directOn = true;         // [openxr] direct_input
    bool setting = true;          // [openxr] direct_bodywalk_walk
    bool guard = true;            // [gameplay] block_stray_walk
    bool bodywalkPadOn = true;    // [input] bodywalk_pad
    bool menu = false, inWorld = true;
    bool bwLive = true;           // BodyWalk writes its pad report
    int16_t ownLX = 0, ownLY = 0; // the controller's left stick (the plugin's own pad)
    int16_t bwLX = 0, bwLY = 0;   // BodyWalk's left stick (its pad report)
    float camYaw = 0.0f;          // the camera's yaw the stick is turned by
    int ctx = 0;                  // straywalk::Ctx the command comes out of (0 = no click on the stack: the controller update)
};

struct Result {
    Out pad;                // what the plugin gives the game's pad poll
    Stick stick;            // the stick the game's walking code takes (turned by the camera, past the dead zone or not)
    bool walks = false;     // the game starts a walk: its stick is past its dead zone
    int cmd = 0;            // the player's command the game makes for it (0x42, the stick's direction), 0 none
    straywalk::Verdict verdict = straywalk::Verdict::Pass;   // the command funnel's guard
    bool reaches = false;   // the walk reaches the server
};

static Result Run(const World& w) {
    g_f4 = w.f4;
    viewgate::Set(&TheView);
    Result r;
    Inputs in;
    in.directFeeding = w.directOn && viewgate::F4();   // directinput::Active()
    in.settingOn = w.setting;
    in.inWorld = w.inWorld;
    in.menuOpen = w.menu;
    in.bodywalkPadOn = w.bodywalkPadOn;
    in.reportLive = w.bwLive;
    in.ownLX = w.ownLX; in.ownLY = w.ownLY;
    in.bwLX = w.bwLX; in.bwLY = w.bwLY;
    r.pad = Resolve(in);
    // the game's walking code takes the report's left stick - turned into its frame by the camera - when no panel is open
    float x = r.pad.lx / 32767.0f, y = r.pad.ly / 32767.0f;
    if (!w.menu && r.pad.source != Source::None) TurnByYaw(&x, &y, w.camYaw, 1.0f, 1.0f, 1.0f);
    else x = y = 0.0f;
    r.stick = {x, y};
    r.walks = !w.menu && r.pad.source != Source::None && PushedRaw((int)std::lround(x * 32767.0f), (int)std::lround(y * 32767.0f));
    if (r.walks) r.cmd = straywalk::kStickDirection;
    r.verdict = straywalk::WalkVerdict(w.guard, viewgate::F4(), r.cmd, (straywalk::Ctx)w.ctx);
    r.reaches = r.walks && r.verdict != straywalk::Verdict::Drop;
    viewgate::Set(nullptr);
    return r;
}

static const int16_t kFull = 32767, kHalf = 16384, kSmall = 6000;

// -- the pure mix -------------------------------------------------------------------------------------------------------------------------
static void TestCombine() {
    const Stick idle{0, 0};
    const Stick fwd{0, 1}, fwdHalf{0, 0.5f}, right{1, 0}, back{0, -1}, backHalf{0, -0.5f};
    const Stick tiny{0.1f, 0.1f};   // inside the dead zone
    // one alone is itself
    CHECK(Combine(fwd, idle).how == How::OwnAlone && NearS(Combine(fwd, idle).out, fwd));
    CHECK(Combine(idle, fwd).how == How::BodyWalkAlone && NearS(Combine(idle, fwd).out, fwd));
    CHECK(Combine(idle, idle).how == How::Neither);
    // a stick inside the dead zone is nothing: BodyWalk alone walks as it is, the drifting thumbstick does not skew it
    CHECK(Combine(tiny, fwd).how == How::BodyWalkAlone && NearS(Combine(tiny, fwd).out, fwd));
    CHECK(Combine(fwd, tiny).how == How::OwnAlone && NearS(Combine(fwd, tiny).out, fwd));
    // neither past the dead zone: the controller's stick untouched (the game's dead zone drops it)
    CHECK(Combine(tiny, tiny).how == How::Neither && NearS(Combine(tiny, tiny).out, tiny));
    // two sticks across: the sum on the unit disc
    {
        const Mixed m = Combine(fwd, right);
        CHECK(m.how == How::Sum && Near(Len(m.out), 1.0f) && Near(m.out.x, m.out.y));
    }
    {   // short ones add up, below the disc
        const Mixed m = Combine({0.4f, 0}, {0, 0.4f});
        CHECK(m.how == How::Sum && Near(m.out.x, 0.4f) && Near(m.out.y, 0.4f));
    }
    // pulling apart: the stronger wins, never cancelled
    {
        const Mixed m = Combine(fwd, backHalf);
        CHECK(m.how == How::OwnStronger && NearS(m.out, fwd));
    }
    {
        const Mixed m = Combine(backHalf, fwd);
        CHECK(m.how == How::BodyWalkStronger && NearS(m.out, fwd));
    }
    {   // equal and opposite: the controller's (the player's hand) stays
        const Mixed m = Combine(fwd, back);
        CHECK(m.how == How::OwnStronger && NearS(m.out, fwd));
    }
    // the same push twice (BodyWalk mirroring the thumbstick): the stronger, not doubled
    {
        const Mixed m = Combine(fwdHalf, fwdHalf);
        CHECK(m.how == How::SameWay && NearS(m.out, fwdHalf) && Len(m.out) < 0.51f);
        const Mixed n = Combine(fwdHalf, {0.05f, 0.55f});
        CHECK(n.how == How::SameWay && Near(Len(n.out), Len({0.05f, 0.55f})));
    }
    // the length is never less than the stronger one, never more than 1 - over a sweep of angles and lengths
    for (int a = 0; a < 360; a += 7) {
        for (float la : {0.35f, 0.6f, 1.0f}) {
            for (float lb : {0.35f, 0.5f, 1.0f}) {
                const float ra = (float)a * 3.14159265f / 180.0f;
                const Stick own{la, 0}, bw{lb * std::cos(ra), lb * std::sin(ra)};
                const Mixed m = Combine(own, bw);
                const float l = Len(m.out);
                CHECK(l <= 1.0001f && l >= std::max(la, lb) - 0.002f);
            }
        }
    }
}

// the turn commutes with the mix: BodyWalk's direction means what the controller's means (one rotation for either, before or after)
static void TestTurnCommutes() {
    for (float yaw : {0.0f, 37.0f, 90.0f, -135.0f, 180.0f}) {
        const Stick own{0.0f, 0.7f}, bw{0.6f, 0.2f};
        Stick a = Combine(own, bw).out;   // mixed, then turned
        TurnByYaw(&a.x, &a.y, yaw, 1.0f, 1.0f, 1.0f);
        Stick o2 = own, b2 = bw;          // turned, then mixed
        TurnByYaw(&o2.x, &o2.y, yaw, 1.0f, 1.0f, 1.0f);
        TurnByYaw(&b2.x, &b2.y, yaw, 1.0f, 1.0f, 1.0f);
        const Stick c = Combine(o2, b2).out;
        CHECK(NearS(a, c, 0.01f));
        // and BodyWalk's stick alone ends up where the controller's own stick of the same value ends up
        Stick fromBw = bw, fromOwn = bw;
        TurnByYaw(&fromBw.x, &fromBw.y, yaw, 1.0f, 1.0f, 1.0f);
        TurnByYaw(&fromOwn.x, &fromOwn.y, yaw, 1.0f, 1.0f, 1.0f);
        CHECK(NearS(fromBw, fromOwn, 1e-6f));
        CHECK(Near(Len(fromBw), Len(bw), 0.001f));   // the length stays: the game's run threshold means the same
    }
    // straight ahead turned by a quarter turn is sideways
    Stick s{0, 1};
    TurnByYaw(&s.x, &s.y, 90.0f, 1.0f, 1.0f, 1.0f);
    CHECK(Near(std::fabs(s.x), 1.0f) && Near(s.y, 0.0f));
}

// -- the path, case by case ---------------------------------------------------------------------------------------------------------------
static void TestNotDirect() {
    // direct_input off, BodyWalk's input: its pad is the pad, whole - the walk reaches the game in every view, the guard or not
    for (bool f4 : {true, false}) {
        for (bool guard : {true, false}) {
            World w;
            w.directOn = false; w.f4 = f4; w.guard = guard;
            w.bwLY = kFull;
            Result r = Run(w);
            CHECK(r.pad.source == Source::BodyWalkPad && r.pad.ly == kFull && r.pad.state == State::NotDirect);
            CHECK(r.walks && r.cmd == 0x42 && r.verdict == straywalk::Verdict::Pass && r.reaches);
        }
    }
    // direct_input ON in F1, F2, F3 (not F4): BodyWalk's controls are the controls there, as if the key were 0
    {
        World w;
        w.f4 = false; w.bwLY = kFull; w.ownLX = kFull;   // the controller's stick is not read in those views
        Result r = Run(w);
        CHECK(r.pad.source == Source::BodyWalkPad && r.pad.lx == 0 && r.pad.ly == kFull && r.reaches);
    }
    // BodyWalk not running: no pad of its, the real XInput's answer stays (None) - not a fake walk
    {
        World w;
        w.directOn = false; w.bwLive = false; w.bwLY = kFull;
        CHECK(Run(w).pad.source == Source::None && !Run(w).walks);
    }
    // [input] bodywalk_pad=0: the report is not read (the virtual pad is the route) - the old behaviour
    {
        World w;
        w.directOn = false; w.bodywalkPadOn = false; w.bwLY = kFull;
        CHECK(Run(w).pad.source == Source::None);
    }
}

static void TestDirect() {
    // direct input, F4, BodyWalk walks, the controller idle: the walk reaches the game (the fix)
    {
        World w;
        w.bwLY = kFull;
        const Result r = Run(w);
        CHECK(r.pad.source == Source::Mixed && r.pad.state == State::BodyWalkAlone && r.pad.ly == kFull && r.pad.lx == 0);
        CHECK(r.walks && r.cmd == 0x42 && r.reaches);
    }
    // ... and not with the setting off: ignored as before
    {
        World w;
        w.setting = false; w.bwLY = kFull;
        const Result r = Run(w);
        CHECK(r.pad.source == Source::OwnPad && r.pad.state == State::Off && r.pad.ly == 0 && !r.walks);
    }
    // the controller's own stick still walks, BodyWalk's idle
    {
        World w;
        w.ownLX = kFull;
        const Result r = Run(w);
        CHECK(r.pad.source == Source::OwnPad && r.pad.state == State::OwnAlone && r.pad.lx == kFull && r.reaches);
    }
    // both at once: combined; the controller's stick forward and BodyWalk's to the right = diagonal, on the unit disc
    {
        World w;
        w.ownLY = kFull; w.bwLX = kFull;
        const Result r = Run(w);
        CHECK(r.pad.state == State::Sum && r.pad.source == Source::Mixed);
        CHECK(Near(Len(FromPad(r.pad.lx, r.pad.ly)), 1.0f) && r.pad.lx > 20000 && r.pad.ly > 20000 && r.reaches);
    }
    // pulling against each other: no cancel, the stronger walks
    {
        World w;
        w.ownLY = kFull; w.bwLY = (int16_t)-kHalf;
        const Result r = Run(w);
        CHECK(r.pad.state == State::OwnStronger && r.pad.ly == kFull && r.reaches);
        w.ownLY = kHalf; w.bwLY = (int16_t)-kFull;
        const Result q = Run(w);
        CHECK(q.pad.state == State::BodyWalkStronger && q.pad.ly == -kFull && q.reaches);
    }
    // BodyWalk's stick inside the game's dead zone is rest: the controller's stick is untouched (to the bit)
    {
        World w;
        w.ownLX = 12345; w.ownLY = -2222; w.bwLX = kSmall; w.bwLY = kSmall;
        const Result r = Run(w);
        CHECK(r.pad.source == Source::OwnPad && r.pad.lx == 12345 && r.pad.ly == -2222 && r.pad.state == State::OwnAlone);
    }
    // BodyWalk live but nobody pushes: nothing walks
    {
        World w;
        const Result r = Run(w);
        CHECK(r.pad.state == State::Live && !r.walks && r.cmd == 0);
    }
    // BodyWalk's report not moving (not running): ignored, the controller walks alone
    {
        World w;
        w.bwLive = false; w.bwLY = kFull; w.ownLX = kHalf + 4000;
        const Result r = Run(w);
        CHECK(r.pad.state == State::NoBodyWalk && r.pad.source == Source::OwnPad && r.pad.lx == kHalf + 4000 && r.pad.ly == 0);
    }
    // a panel is open: BodyWalk's walk is dropped, the stick is the menu's; no game: dropped too
    {
        World w;
        w.menu = true; w.bwLY = kFull; w.ownLX = kFull;
        const Result r = Run(w);
        CHECK(r.pad.state == State::Menu && r.pad.source == Source::OwnPad && r.pad.lx == kFull && r.pad.ly == 0 && !r.walks);
        World n;
        n.inWorld = false; n.bwLY = kFull;
        const Result q = Run(n);
        CHECK(q.pad.state == State::NoGame && q.pad.ly == 0);
    }
}

// the stray-walk guard on the command funnel: the stick's walk (command 0x42) is never touched, in any context, with the guard on or off
static void TestGuard() {
    for (bool guard : {true, false}) {
        for (bool f4 : {true, false}) {
            for (int ctx = 0; ctx <= (int)straywalk::Ctx::Other; ++ctx) {
                World w;
                w.guard = guard; w.f4 = f4; w.ctx = ctx; w.bwLY = kFull;
                const Result r = Run(w);
                CHECK(r.cmd == 0x42 || !r.walks);
                CHECK(r.verdict == straywalk::Verdict::Pass);   // whatever context: the stick walk is never dropped, never even noted
                CHECK(r.reaches || !r.walks);
                // BodyWalk's walk in direct mode in F4 reaches the game under every one of them; in the other views direct mode is off
                if (f4) CHECK(r.reaches && r.pad.source == Source::Mixed);
                else CHECK(r.reaches && r.pad.source == Source::BodyWalkPad);
            }
        }
    }
    // for contrast: a walk TO A POINT out of a click on bare ground is what the guard drops (the stick walk is not one)
    CHECK(straywalk::WalkVerdict(true, true, straywalk::kRunToPoint, straywalk::Ctx::GroundClick) == straywalk::Verdict::Drop);
    CHECK(straywalk::WalkVerdict(true, true, straywalk::kWalkToPoint, straywalk::Ctx::GroundClick) == straywalk::Verdict::Drop);
    CHECK(straywalk::WalkVerdict(true, true, straywalk::kStickDirection, straywalk::Ctx::GroundClick) == straywalk::Verdict::Pass);
    CHECK(straywalk::WalkVerdict(true, true, straywalk::kRunToPoint, straywalk::Ctx::Stop) == straywalk::Verdict::Pass);   // the stick's stop (click type 6)
    CHECK(straywalk::ContextOf(6, false) == straywalk::Ctx::Stop && straywalk::ContextOf(6, true) == straywalk::Ctx::Stop);
}

// pad presence: a neutral pad (no real one, BodyWalk's report missing for a moment) is stick-less and never touches the walking path
static void TestPadPresence() {
    CHECK(padpresence::MustStayPresent(true, true, false) && padpresence::MustStayPresent(true, false, true));
    CHECK(!padpresence::MustStayPresent(false, true, true) && !padpresence::MustStayPresent(true, false, false));
    // the walk does not depend on the pad's presence answer: BodyWalk's walk with the real XInput saying "no pad" (r != success) still
    // reaches the game through the report - Resolve has no input for it
    World w;
    w.bwLY = kFull;
    CHECK(Run(w).reaches);
    padpresence::NeutralPacket n;
    const uint32_t a = n.Next(), b = n.Next();
    CHECK(a != b);
}

// the walking hook's line names whose stick walks
static void TestWhoWalks() {
    CHECK(std::string(WhoWalks(State::NotDirect)) == "BodyWalk's pad");
    for (State s : {State::Off, State::NoBodyWalk, State::Menu, State::NoGame, State::Live, State::OwnAlone, State::FlatOwn})
        CHECK(std::string(WhoWalks(s)) == "the controller's stick");
    CHECK(std::string(WhoWalks(State::BodyWalkAlone)) == "BodyWalk's stick");
    for (State s : {State::Sum, State::OwnStronger, State::BodyWalkStronger, State::SameWay})
        CHECK(std::string(WhoWalks(s)).find("both") == 0);
}

// the log: one line per change, rate-limited, nothing outside direct mode
static void TestLog() {
    StateLog l;
    State s{};
    uint32_t c = 0;
    CHECK(!l.Step(State::NotDirect, 0, &s, &c));
    CHECK(l.Step(State::BodyWalkAlone, 1, &s, &c) && s == State::BodyWalkAlone && c == 1);
    CHECK(!l.Step(State::BodyWalkAlone, 2, &s, &c));   // no change, no line
    CHECK(l.Step(State::Sum, 3, &s, &c) && s == State::Sum && c == 1);
    CHECK(!l.Step(State::NotDirect, 4, &s, &c));        // leaving direct mode is not a state
    CHECK(l.Step(State::Sum, 5, &s, &c) && c == 1);     // coming back says it again
    // flapping Live <-> BodyWalkAlone: the head at once, then one line in 5 s standing for the changes since
    StateLog f;
    uint32_t said = 0;
    for (int i = 0; i < 100; ++i) if (f.Step(i & 1 ? State::Live : State::BodyWalkAlone, 100 + i, &s, &c)) ++said;
    CHECK(said == StateLog::kHead);
    CHECK(!f.Step(State::Live, 5000, &s, &c));                       // not yet
    CHECK(f.Step(State::Live, 100 + 99 + StateLog::kEveryMs, &s, &c) && c >= 1);   // its time has come: the state now, with the count
    CHECK(f.Changes() == 100);
    // a change swallowed by the limit is said when its time comes even if nothing changes after it
    StateLog g;
    for (int i = 0; i < (int)StateLog::kHead; ++i) g.Step(i & 1 ? State::Live : State::BodyWalkAlone, 10, &s, &c);
    CHECK(!g.Step(State::Menu, 20, &s, &c));
    CHECK(g.Step(State::Menu, 20 + StateLog::kEveryMs + 1, &s, &c) && s == State::Menu);
    // every state has words
    for (int i = 0; i <= (int)State::FlatOwn; ++i) CHECK(StateName((State)i)[0] != 0);
}

// what BodyWalk is given of a hand in direct mode: the poses stay, no control is left
struct FakeHand { float pose[7]; float trigger, squeeze, stickX, stickY; uint32_t buttons, active; };
static void TestZeroControls() {
    FakeHand h{{1, 2, 3, 0, 0, 0, 1}, 0.9f, 0.8f, -0.5f, 1.0f, 0x3Fu, 1u};
    ZeroControls(h);
    CHECK(h.trigger == 0.0f && h.squeeze == 0.0f && h.stickX == 0.0f && h.stickY == 0.0f && h.buttons == 0);
    CHECK(h.pose[0] == 1 && h.pose[1] == 2 && h.pose[2] == 3 && h.pose[6] == 1 && h.active == 1u);   // the place and the controller's presence stay
}

// BodyWalk's Output "Game Link": the walk comes in the Walk block as (x, -y) of its locomotion and is put on the report's left stick
// (WithLinkWalk) - it must be the very stick its Xbox output gives (vigem_set_joystick: (int)(x * 32767), (int)(-y * 32767)), so the
// game's walk, run and dead zone are the same in every view and mode.
static void TestLinkWalk() {
    const float samples[] = {0.0f, 0.05f, 0.2f, 0.3158f, 0.5f, 0.7071f, 0.95f, 1.0f, -0.05f, -0.3f, -0.6f, -1.0f, 1.3f, -1.7f};
    int same = 0, total = 0;
    for (float x : samples) {
        for (float yLoco : samples) {   // the locomotion vector: forward negative
            const float cx = std::clamp(x, -1.0f, 1.0f), cy = std::clamp(yLoco, -1.0f, 1.0f);   // BodyWalk's output clamps first
            const int16_t xboxX = (int16_t)(int)(cx * 32767.0f), xboxY = (int16_t)(int)(-cy * 32767.0f);
            int16_t lx = 0, ly = 0;
            const bool put = WithLinkWalk(&lx, &ly, true, cx, -cy);   // the plugin's Walk block: x, -y
            ++total;
            if (put && lx == xboxX && ly == xboxY) ++same;
            // and the game does the same with it, in every view, direct input or not
            for (bool f4 : {true, false}) {
                for (bool directOn : {true, false}) {
                    World a, b;
                    a.f4 = b.f4 = f4; a.directOn = b.directOn = directOn;
                    a.bwLX = xboxX; a.bwLY = xboxY;   // the Xbox output's report
                    b.bwLX = lx; b.bwLY = ly;         // the Game Link walk put on the report
                    const Result ra = Run(a), rb = Run(b);
                    CHECK(ra.pad.lx == rb.pad.lx && ra.pad.ly == rb.pad.ly && ra.walks == rb.walks && ra.cmd == rb.cmd && ra.reaches == rb.reaches);
                }
            }
        }
    }
    CHECK(same == total);
    // a lean forward walks forward: + on the stick's y
    {
        int16_t lx = 0, ly = 0;
        CHECK(WithLinkWalk(&lx, &ly, true, 0.0f, 0.6f) && lx == 0 && ly == (int16_t)(int)(0.6f * 32767.0f) && ly > 0);
    }
    // a small lean stays inside the game's dead zone (no walk), a bigger one walks, full is a run's stick - as on a thumbstick
    {
        int16_t lx = 0, ly = 0;
        WithLinkWalk(&lx, &ly, true, 0.0f, 0.2f);
        CHECK(!PushedRaw(lx, ly));
        lx = ly = 0; WithLinkWalk(&lx, &ly, true, 0.0f, 0.5f);
        CHECK(PushedRaw(lx, ly));
        lx = ly = 0; WithLinkWalk(&lx, &ly, true, 0.0f, 1.0f);
        CHECK(ly == 32767);
    }
    // a stick the Mapping holds on BodyWalk's pad wins, as on its own pad
    {
        int16_t lx = 1200, ly = -32767;
        CHECK(!WithLinkWalk(&lx, &ly, true, 0.0f, 1.0f) && lx == 1200 && ly == -32767);
    }
    // no walk through Game Link (another Output, or stale): the report as it is
    {
        int16_t lx = 0, ly = 0;
        CHECK(!WithLinkWalk(&lx, &ly, false, 0.5f, 0.5f) && lx == 0 && ly == 0);
        lx = 5000; ly = 7000;
        CHECK(!WithLinkWalk(&lx, &ly, false, 0.0f, 0.0f) && lx == 5000 && ly == 7000);
    }
}

// [input] bodywalk_walk_lift: a BodyWalk stick between `from` and the dead zone is lengthened to just past it, its direction kept.
static void TestLift() {
    int16_t lx = 0, ly = 6000;   // 0.18 straight ahead
    CHECK(!Lift(&lx, &ly, 0.0f) && lx == 0 && ly == 6000);       // off
    CHECK(!Lift(&lx, &ly, 0.35f) && lx == 0 && ly == 6000);      // a "from" past the dead zone: off
    CHECK(!Lift(&lx, &ly, 0.20f) && ly == 6000);                 // shorter than from: BodyWalk at rest, left alone
    CHECK(Lift(&lx, &ly, 0.12f) && lx == 0 && ly == kLiftedAxisLen && PushedRaw(lx, ly));
    lx = -3000; ly = 3000;                                       // 0.13 on the diagonal
    CHECK(Lift(&lx, &ly, 0.10f) && PushedRaw(lx, ly) && lx == -ly && lx < 0);
    CHECK(Len(FromPad(lx, ly)) < kDeadZone + 0.01f);             // just past: the slowest walk the game has
    lx = 0; ly = -12000;                                         // already past the dead zone: as it is
    CHECK(!Lift(&lx, &ly, 0.10f) && ly == -12000);
    lx = 0; ly = 2000;                                           // 0.06: standing noise under a 0.10 lift
    CHECK(!Lift(&lx, &ly, 0.10f) && ly == 2000);
    // the lifted stick walks through the mix as BodyWalk's alone, and a controller pushed with it still wins when stronger
    lx = 2500; ly = 2500;
    CHECK(Lift(&lx, &ly, 0.10f));
    CHECK(Combine({}, FromPad(lx, ly)).how == How::BodyWalkAlone);
    CHECK(Combine(Stick{-0.9f, 0.0f}, FromPad(lx, ly)).how == How::OwnStronger);
}

int main() {
    TestLift();
    TestLinkWalk();
    TestZeroControls();
    TestCombine();
    TestTurnCommutes();
    TestNotDirect();
    TestDirect();
    TestGuard();
    TestPadPresence();
    TestWhoWalks();
    TestLog();
    if (g_failures) { std::printf("%d check(s) FAILED\n", g_failures); return 1; }
    std::printf("walk_path_test: all %d checks passed\n", g_checks);
    return 0;
}
