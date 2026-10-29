// Offline checks of vr/menu_zoom.h ([openxr] menu_zoom): the right grip + the right stick bring a game panel nearer / farther in VR F4 - where
// it stays readable, a step of the distance, the grip's hold frame by frame (what is taken from the game and when the distance is saved).
// No OpenXR, no headset. Run: menu_zoom_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "../../vr/menu_zoom.h"
#include "../../vr/direct_layout.h"
#include "../../vr/hold_tap.h"
#include "../../vr/hset_defs.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using menuzoom::Eye;
using menuzoom::Grab;
using menuzoom::In;
using menuzoom::Limits;

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

// PS VR2 on SteamVR (the author's headset): the runtime's recommended eye picture 2404 x 2452 (d2r_vr_headset.ini); a field of
// about 52 deg out, 45 in, 50 up and 55 down per eye.
static void PsVr2(Eye e[2]) {
    const float out = std::tan(52.0f * 3.14159265f / 180.0f), in = std::tan(45.0f * 3.14159265f / 180.0f);
    const float up = std::tan(50.0f * 3.14159265f / 180.0f), down = std::tan(55.0f * 3.14159265f / 180.0f);
    e[0] = {-out, in, up, -down, 2404, 2452};
    e[1] = {-in, out, up, -down, 2404, 2452};
}

static void TestConstants() {
    // the thresholds are the direct layout's: the grip is the zoom's from the moment it would have been the menu's right bumper
    CHECK(menuzoom::kGripOn == directinput::layout::kGripOn && menuzoom::kGripOff == directinput::layout::kGripOff);
    CHECK(menuzoom::kTriggerOn == directinput::layout::kTriggerOn && menuzoom::kTriggerOff == directinput::layout::kTriggerOff);
    CHECK(menuzoom::kGripTapMs == directinput::holdtap::kPulseMs);
    // the key's range is the headset settings panel's "Panel distance" row (and D2R VR Settings' slider, 0 - 8)
    bool found = false;
    for (const hset::Def& d : hset::kDefs)
        if (std::wcscmp(d.section, L"screen") == 0 && std::wcscmp(d.key, L"menu_distance_m") == 0) {
            found = true;
            CHECK(d.min == menuzoom::kKeyMinM && d.max == menuzoom::kKeyMaxM);
        }
    CHECK(found);
}

static void TestLimits() {
    Eye e[2];
    PsVr2(e);
    // the author's panel: 2.3 m wide, the square 1536 picture
    const Limits l = menuzoom::LimitsFor(2.3f, 2.3f, 1536, 1536, e);
    CHECK(l.derived);
    // near: the half height over the lower of up (50 deg) and down (55 deg) - the vertical is the tighter here (square panel)
    const float nearV = 1.15f / std::tan(50.0f * 3.14159265f / 180.0f), nearH = 1.15f / std::tan(52.0f * 3.14159265f / 180.0f);
    CHECK(Near(l.nearM, std::max(nearV, nearH), 1e-3f));
    // far: two texels a display pixel, the coarser axis
    const float densX = 2404.0f / (e[0].tanR - e[0].tanL), densY = 2452.0f / (e[0].tanU - e[0].tanD);
    CHECK(Near(l.farM, std::min(2.0f * 2.3f * densX / 1536.0f, 2.0f * 2.3f * densY / 1536.0f), 1e-3f));
    // the author's distance (1.9 m) is inside: nothing about it changes by itself
    CHECK(l.nearM < 1.9f && l.farM > 1.9f);
    // a sharper picture of the same panel (more texels) is readable only nearer; a wider panel farther and not as near
    const Limits sharp = menuzoom::LimitsFor(2.3f, 2.3f, 3072, 3072, e);
    CHECK(sharp.farM < l.farM);
    const Limits wide = menuzoom::LimitsFor(3.0f, 3.0f, 1536, 1536, e);
    CHECK(wide.farM > l.farM && wide.nearM > l.nearM);
    // the narrower eye's side decides across: a wide flat panel's near comes from the eyes' outer edges
    const Limits flat = menuzoom::LimitsFor(4.0f, 0.5f, 2048, 256, e);
    CHECK(Near(flat.nearM, 2.0f / std::tan(52.0f * 3.14159265f / 180.0f), 1e-3f));
    // nothing known: the key's range, not derived
    const Limits none = menuzoom::LimitsFor(2.3f, 2.3f, 0, 0, e);
    CHECK(!none.derived && none.nearM == menuzoom::kKeyMinM && none.farM == menuzoom::kKeyMaxM);
    Eye bad[2] = {e[0], e[1]};
    bad[1].pixelsW = 0;
    CHECK(!menuzoom::LimitsFor(2.3f, 2.3f, 1536, 1536, bad).derived);
    bad[1] = e[1];
    bad[0].tanR = bad[0].tanL;   // a field of no width
    CHECK(!menuzoom::LimitsFor(2.3f, 2.3f, 1536, 1536, bad).derived);
    CHECK(!menuzoom::LimitsFor(2.3f, 2.3f, 1536, 1536, nullptr).derived);
    CHECK(!menuzoom::LimitsFor(NAN, 2.3f, 1536, 1536, e).derived);
    // a coarse headset (far nearer than near): one readable distance, the near
    Eye coarse[2] = {e[0], e[1]};
    coarse[0].pixelsW = coarse[1].pixelsW = 300;
    coarse[0].pixelsH = coarse[1].pixelsH = 300;
    const Limits c = menuzoom::LimitsFor(2.3f, 2.3f, 1536, 1536, coarse);
    CHECK(c.derived && c.farM == c.nearM);
    // always in the key's range: a tiny panel's near would be under 0.5 m, a huge one's far over 8 m
    const Limits tiny = menuzoom::LimitsFor(0.2f, 0.2f, 16, 16, e);
    CHECK(tiny.nearM == menuzoom::kKeyMinM && tiny.farM == menuzoom::kKeyMaxM);
}

static void TestStepped() {
    Limits l;
    l.nearM = 1.0f; l.farM = 3.0f; l.derived = true;
    // forward: nearer, back: farther, speed x travel x time
    CHECK(Near(menuzoom::Stepped(2.0f, 1.0f, 1.0f, 0.05f, l), 1.95f));
    CHECK(Near(menuzoom::Stepped(2.0f, -0.5f, 1.0f, 0.05f, l), 2.025f));
    CHECK(Near(menuzoom::Stepped(2.0f, 1.0f, 2.0f, 0.05f, l), 1.9f));
    // a stall is not a jump
    CHECK(Near(menuzoom::Stepped(2.0f, 1.0f, 1.0f, 5.0f, l), 2.0f - menuzoom::kMaxStepS));
    // the limits stop it
    CHECK(Near(menuzoom::Stepped(1.02f, 1.0f, 1.0f, 0.05f, l), 1.0f));
    CHECK(Near(menuzoom::Stepped(1.0f, 1.0f, 1.0f, 0.05f, l), 1.0f));
    CHECK(Near(menuzoom::Stepped(2.98f, -1.0f, 1.0f, 0.05f, l), 3.0f));
    // ... but never move a distance already past one: it stays, and goes back towards the other freely
    CHECK(Near(menuzoom::Stepped(4.0f, -1.0f, 1.0f, 0.05f, l), 4.0f));
    CHECK(Near(menuzoom::Stepped(4.0f, 1.0f, 1.0f, 0.05f, l), 3.95f));
    CHECK(Near(menuzoom::Stepped(0.8f, 1.0f, 1.0f, 0.05f, l), 0.8f));
    CHECK(Near(menuzoom::Stepped(0.8f, -1.0f, 1.0f, 0.05f, l), 0.85f));
    // nothing to do: no travel, no time, a broken value, an over-range stick
    CHECK(menuzoom::Stepped(2.0f, 0.0f, 1.0f, 0.05f, l) == 2.0f);
    CHECK(menuzoom::Stepped(2.0f, 1.0f, 1.0f, 0.0f, l) == 2.0f);
    CHECK(menuzoom::Stepped(2.0f, NAN, 1.0f, 0.05f, l) == 2.0f);
    CHECK(Near(menuzoom::Stepped(2.0f, 3.0f, 1.0f, 0.05f, l), 1.95f));
    // whole centimetres for the ini
    CHECK(Near(menuzoom::Saved(1.456f), 1.46f) && Near(menuzoom::Saved(1.454f), 1.45f));
}

// A frame of the right hand over a panel at `d`.
static In Frame(float d, float grip, float stickY, bool applies = true, float trigger = 0.0f) {
    In in;
    in.applies = applies;
    in.grip = grip;
    in.trigger = trigger;
    in.stickY = stickY;
    in.dtS = 0.02f;
    in.distance = d;
    in.speed = 1.0f;
    in.limits.nearM = 1.0f; in.limits.farM = 3.0f; in.limits.derived = true;
    return in;
}

static void TestGrabZoom() {
    Grab g;
    uint64_t t = 1000;
    float d = 2.0f;
    // the grip squeezed over the panel, the stick still: the Y is the zoom's already, the grip's press waits (the laser did not take it)
    menuzoom::Pre p = g.StepPre(Frame(d, 1.0f, 0.0f));
    CHECK(g.state() == Grab::State::Held && p.takeY && !p.zooming && !p.save && p.distance == d);
    menuzoom::Post q = g.StepPost(true, t += 20);
    CHECK(q.hideGrip && !q.tapGrip);
    // the stick forward: nearer, frame by frame
    for (int i = 0; i < 10; ++i) {
        p = g.StepPre(Frame(d, 1.0f, 1.0f));
        CHECK(p.zooming && p.takeY);
        d = p.distance;
        q = g.StepPost(true, t += 20);
        CHECK(q.hideGrip && !q.tapGrip);
    }
    CHECK(Near(d, 1.8f, 1e-3f));
    // the stick at rest: it stays
    p = g.StepPre(Frame(d, 1.0f, 0.0f));
    CHECK(p.zooming && p.distance == d);
    // back: farther
    p = g.StepPre(Frame(d, 1.0f, -0.5f));
    CHECK(Near(p.distance, d + 0.01f));
    d = p.distance;
    // let go: saved, from where the hold took it, and no tap of the grip
    p = g.StepPre(Frame(d, 0.0f, -0.5f));
    CHECK(p.save && Near(p.from, 2.0f) && p.distance == d && !p.zooming);
    CHECK(p.takeY);   // the stick still pushed: its Y stays the zoom's until it is back at rest
    q = g.StepPost(false, t += 20);
    CHECK(!q.hideGrip && !q.tapGrip);
    CHECK(g.state() == Grab::State::Idle);
    p = g.StepPre(Frame(d, 0.0f, -0.5f));
    CHECK(p.takeY && !p.save);
    p = g.StepPre(Frame(d, 0.0f, 0.0f));
    CHECK(!p.takeY);
    p = g.StepPre(Frame(d, 0.0f, 0.4f));
    CHECK(!p.takeY);   // at rest once: the stick is the game's again
    q = g.StepPost(false, t += 200);
    CHECK(!q.tapGrip);
}

static void TestGrabTap() {
    // the grip squeezed and let go with the stick never moved: its own press (the menu's right bumper) as a tap, then
    Grab g;
    uint64_t t = 1000;
    g.StepPre(Frame(2.0f, 1.0f, 0.0f));
    CHECK(g.StepPost(true, t += 20).hideGrip);
    g.StepPre(Frame(2.0f, 1.0f, 0.0f));
    CHECK(g.StepPost(true, t += 20).hideGrip);
    menuzoom::Pre p = g.StepPre(Frame(2.0f, 0.0f, 0.0f));
    CHECK(!p.save && !p.takeY);
    menuzoom::Post q = g.StepPost(false, t += 20);
    CHECK(q.tapGrip && !q.hideGrip);
    g.StepPre(Frame(2.0f, 0.0f, 0.0f));
    CHECK(g.StepPost(false, t += 50).tapGrip);
    g.StepPre(Frame(2.0f, 0.0f, 0.0f));
    CHECK(!g.StepPost(false, t += 60).tapGrip);   // over after kGripTapMs
    // the laser had the grip (it never reached the game): no tap
    Grab h;
    h.StepPre(Frame(2.0f, 1.0f, 0.0f));
    CHECK(!h.StepPost(false, t += 20).hideGrip);
    h.StepPre(Frame(2.0f, 0.0f, 0.0f));
    CHECK(!h.StepPost(false, t += 20).tapGrip);
    // the grip's hysteresis is the layout's: half squeezed is nothing; past kGripOn held, down to kGripOff still held
    Grab k;
    k.StepPre(Frame(2.0f, 0.5f, 0.0f));
    CHECK(k.state() == Grab::State::Idle);
    k.StepPre(Frame(2.0f, 0.65f, 0.0f));
    CHECK(k.state() == Grab::State::Held);
    k.StepPre(Frame(2.0f, 0.45f, 0.0f));
    CHECK(k.state() == Grab::State::Held);
    k.StepPre(Frame(2.0f, 0.35f, 0.0f));
    CHECK(k.state() == Grab::State::Idle);
}

static void TestGrabNotTheZooms() {
    uint64_t t = 1000;
    // squeezed where no panel was (the world: the belt, a two-handed grip...): nothing taken, ever in this hold - a panel opening under it changes nothing
    Grab g;
    menuzoom::Pre p = g.StepPre(Frame(2.0f, 1.0f, 1.0f, false));
    CHECK(g.state() == Grab::State::Spent && !p.takeY && !p.zooming);
    CHECK(!g.StepPost(true, t += 20).hideGrip);
    p = g.StepPre(Frame(2.0f, 1.0f, 1.0f, true));
    CHECK(!p.takeY && !p.zooming && p.distance == 2.0f);
    g.StepPre(Frame(2.0f, 0.0f, 0.0f, true));
    CHECK(!g.StepPost(false, t += 20).tapGrip);
    // the trigger first (the laser's grip + trigger = right click; the pad's RB + RT): the hold is not the zoom's, the grip shows from then on
    Grab h;
    h.StepPre(Frame(2.0f, 1.0f, 0.0f));
    CHECK(h.StepPost(true, t += 20).hideGrip);
    p = h.StepPre(Frame(2.0f, 1.0f, 0.0f, true, 1.0f));
    CHECK(h.state() == Grab::State::Spent && !p.takeTrigger && !p.takeY);
    CHECK(!h.StepPost(true, t += 20).hideGrip);
    p = h.StepPre(Frame(2.0f, 1.0f, 1.0f, true, 0.0f));
    CHECK(!p.zooming && !p.takeY);   // the stick later in the same hold is the game's
    h.StepPre(Frame(2.0f, 0.0f, 0.0f));
    CHECK(!h.StepPost(false, t += 20).tapGrip);   // (its press reached the game while it was held)
    // the trigger already down when the grip comes: the same
    Grab k;
    k.StepPre(Frame(2.0f, 1.0f, 0.0f, true, 1.0f));
    CHECK(k.state() == Grab::State::Spent);
    // a trigger click (the controller's own) counts as the trigger
    Grab m;
    In f = Frame(2.0f, 1.0f, 0.0f);
    f.triggerClick = true;
    m.StepPre(f);
    CHECK(m.state() == Grab::State::Spent);
}

static void TestGrabTrigger() {
    // a trigger squeezed while zooming: nobody's until let go, the grip's release included
    Grab g;
    float d = 2.0f;
    g.StepPre(Frame(d, 1.0f, 0.0f));
    menuzoom::Pre p = g.StepPre(Frame(d, 1.0f, 1.0f));
    d = p.distance;
    CHECK(p.zooming && !p.takeTrigger);
    p = g.StepPre(Frame(d, 1.0f, 1.0f, true, 1.0f));
    CHECK(p.zooming && p.takeTrigger);
    d = p.distance;
    p = g.StepPre(Frame(d, 0.0f, 0.0f, true, 1.0f));
    CHECK(p.save && p.takeTrigger);
    p = g.StepPre(Frame(d, 0.0f, 0.0f, true, 1.0f));
    CHECK(p.takeTrigger);
    p = g.StepPre(Frame(d, 0.0f, 0.0f, true, 0.2f));
    CHECK(!p.takeTrigger);
    p = g.StepPre(Frame(d, 0.0f, 0.0f, true, 1.0f));
    CHECK(!p.takeTrigger);   // a new press is the game's
}

static void TestGrabPanelGoes() {
    // the panel closed under the zooming hand: what it moved is saved, the rest of the hold is not the zoom's
    Grab g;
    uint64_t t = 1000;
    float d = 2.0f;
    g.StepPre(Frame(d, 1.0f, 0.0f));
    menuzoom::Pre p = g.StepPre(Frame(d, 1.0f, -1.0f));
    d = p.distance;
    p = g.StepPre(Frame(d, 1.0f, -1.0f, false));
    CHECK(p.save && Near(p.from, 2.0f) && g.state() == Grab::State::Spent && !p.zooming);
    CHECK(!g.StepPost(true, t += 20).hideGrip);
    p = g.StepPre(Frame(d, 1.0f, 0.0f, true));
    CHECK(!p.zooming && !p.takeY && !p.save);
    // a hold that moved it by less than the ini's step saves nothing
    Grab h;
    h.StepPre(Frame(2.0f, 1.0f, 0.0f));
    In tiny = Frame(2.0f, 1.0f, 0.1f);
    tiny.dtS = 0.01f;
    p = h.StepPre(tiny);
    CHECK(p.zooming && Near(p.distance, 1.999f));
    p = h.StepPre(Frame(p.distance, 0.0f, 0.0f));
    CHECK(!p.save);
    // never a NaN through the grip or the trigger
    Grab k;
    p = k.StepPre(Frame(2.0f, NAN, 1.0f, true, NAN));
    CHECK(k.state() == Grab::State::Idle && !p.takeY && !p.takeTrigger);
}

int main() {
    TestConstants();
    TestLimits();
    TestStepped();
    TestGrabZoom();
    TestGrabTap();
    TestGrabNotTheZooms();
    TestGrabTrigger();
    TestGrabPanelGoes();
    std::printf("menu_zoom_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
