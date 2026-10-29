// The wrist status' wiring (vr/wrist_status_wire.cpp) fed direct_input.h's frames, with stand-ins for what it calls: direct_input's Listen / Unlisten and what the hero has
// (vr/wrist_status_game.h). The painter is the real one (on its worker thread). No game, no headset.
//
//   wrist_status_wire_test
//
// Checks: nothing happens with the box unticked (no layer, no picture, no read); the back of a wrist turned to the face brings THAT wrist's layer up - its pose over the wrist,
// the size from the settings, the colour scale rising - and the painter delivers a picture of at least the moment it was wanted; the two wrists are independent and show what the
// setting says (the effects on the left and the companions on the right, swapped, both); a wrist with nothing to show does not come up; a menu, a panel of the plugin's, another
// view (the view gate), the lost focus, a stale or unknown read, no frames for a moment, a stall and the setting unticked take it away; what the hero has is read only while the box
// is ticked, in F4, in a game; a settings change is live; Teardown forgets the pictures and new ones are painted afterwards.

#include "../../vr/direct_input.h"
#include "../../vr/view_gate.h"
#include "../../vr/wrist_status.h"
#include "../../vr/wrist_status_game.h"
#include "../../vr/wrist_status_paint.h"
#include "../../vr/wrist_status_wire.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// ---- the stand-ins ----
namespace directinput {
FrameFn g_fn = nullptr;
bool Listen(FrameFn fn, void*) { g_fn = fn; return true; }
void Unlisten(FrameFn fn, void*) { if (g_fn == fn) g_fn = nullptr; }
}   // namespace directinput

namespace wriststatus {
namespace game {
Roster g_roster;
Status g_status;
bool g_have = true;
double g_age = 100.0;
int g_ticks = 0;
bool g_lastWant = false;
void Init(const D2RL::PluginContext*) {}
void Tick(bool want) { ++g_ticks; g_lastWant = want; }
bool Latest(Roster* r, Status* s, double* ageMs) {
    if (!g_have) return false;
    *r = g_roster;
    *s = g_status;
    *ageMs = g_age;
    return true;
}
}   // namespace game
}   // namespace wriststatus

namespace {

int g_fails = 0, g_checks = 0;

#define EXPECT(c)                                                       \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(c)) {                                                     \
            ++g_fails;                                                  \
            printf("  FAIL line %d: %s\n", __LINE__, #c);               \
        }                                                               \
    } while (0)

using namespace wriststatus;
using hset::Q;
using hset::V3;

bool g_f4 = true;
bool IsF4() { return g_f4; }

V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Q FromTo(V3 a, V3 b) {
    const float d = hset::Dot(a, b);
    if (d > 0.999999f) return {};
    const V3 c = Cross(a, b);
    Q q{c.x, c.y, c.z, 1.0f + d};
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

// A frame: the head at (0, 1.6, 0) looking down -z; both wrists held up at the face with the BACK turned to the head `angle` degrees off (0 = at the face).
struct Feed {
    double time = 10.0;
    uint32_t counter = 0, session = 1, epoch = 1;
    uint32_t flags = directinput::kFocused;
    V3 pos[2] = {{-0.05f, 1.45f, -0.38f}, {0.05f, 1.45f, -0.38f}};
    float angle[2] = {0.0f, 0.0f};
    bool valid[2] = {true, true};

    directinput::Frame Make() const {
        directinput::Frame f{};
        f.version = directinput::kVersion;
        f.counter = counter;
        f.session = session;
        f.epoch = epoch;
        f.flags = flags;
        f.time = time;
        f.head.valid = 1;
        f.head.pos[1] = 1.6f;
        f.head.rot[3] = 1.0f;
        const V3 head{0.0f, 1.6f, 0.0f};
        for (int s = 0; s < 2; ++s) {
            const V3 dir = hset::Unit(hset::Sub(head, pos[s]));
            const float a = angle[s] * 3.14159265f / 180.0f;
            const V3 perp = hset::Unit(Cross(dir, {0, 1, 0}));
            const V3 n = hset::Unit(hset::Add(hset::Mul(dir, std::cos(a)), hset::Mul(perp, std::sin(a))));
            const Q q = FromTo({s == 0 ? -1.0f : 1.0f, 0.0f, 0.0f}, n);   // the left grip's back is -X, the right's +X
            directinput::Hand& h = f.hand[s];
            h.grip.valid = valid[s] ? 1 : 0;
            h.grip.pos[0] = pos[s].x;
            h.grip.pos[1] = pos[s].y;
            h.grip.pos[2] = pos[s].z;
            h.grip.rot[0] = q.x;
            h.grip.rot[1] = q.y;
            h.grip.rot[2] = q.z;
            h.grip.rot[3] = q.w;
        }
        return f;
    }
    void Step(float seconds = 1.0f / 90.0f) {
        time += seconds;
        ++counter;
        const directinput::Frame f = Make();
        if (directinput::g_fn) directinput::g_fn(f, nullptr);
    }
    void Run(float seconds) {
        for (float t = 0.0f; t < seconds - 1e-6f; t += 1.0f / 90.0f) Step();
    }
};

Config Cfg(bool on = true) {
    Config c;
    c.on = on;
    return c;
}

// Waits (up to 3 s) for the painter's picture of that wrist; the revision and the size come back.
bool WaitPicture(int side, std::vector<uint32_t>* px, uint32_t* rev, uint32_t* w = nullptr, uint32_t* h = nullptr) {
    uint32_t ww = 0, hh = 0;
    for (int i = 0; i < 300; ++i) {
        if (Picture(side, px, rev, &ww, &hh)) {
            if (w) *w = ww;
            if (h) *h = hh;
            return true;
        }
        Sleep(10);
    }
    return false;
}

// Waits (up to 3 s) until the painter's picture for that wrist is `want` (an older look may still be on the worker's table when a setting changes).
bool WaitFor(int side, const std::vector<uint32_t>& want) {
    std::vector<uint32_t> px;
    uint32_t rev = 0, w = 0, h = 0;
    for (int i = 0; i < 300; ++i) {
        if (Picture(side, &px, &rev, &w, &h) && px == want) return true;
        Sleep(10);
    }
    return false;
}

int Opaque(const std::vector<uint32_t>& px) {
    int n = 0;
    for (uint32_t v : px) if (v >> 24) ++n;
    return n;
}

// What the hero has: the mercenary and five skeletons; Might, a healing potion and Amplify Damage on it.
void SetHero() {
    game::g_roster = Roster{};
    game::g_roster.known = true;
    game::g_roster.alive[7] = 1;
    game::g_roster.alive[4] = 5;
    game::g_roster.relevant = Bit(7) | Bit(4);
    game::g_status = Status{};
    game::g_status.known = true;
    game::g_status.Set(33);
    game::g_status.Set(100);
    game::g_status.Set(9);
    game::g_have = true;
    game::g_age = 100.0;
}

bool Up(int side) {
    Layer L;
    uint32_t need = 0;
    return Draw(side, &L, &need) && L.visible;
}

}  // namespace

int main() {
    viewgate::Set(&IsF4);
    Init(nullptr);
    EXPECT(directinput::g_fn != nullptr);   // the listener is in
    SetHero();

    // ---- unticked: nothing ----
    {
        printf("unticked\n");
        Feed f;
        f.Run(1.0f);
        Layer L;
        uint32_t need = 0;
        EXPECT(!Enabled());
        EXPECT(!Draw(0, &L, &need) && !Draw(1, &L, &need));
        std::vector<uint32_t> px;
        uint32_t rev = 0, w = 0, h = 0;
        Sleep(50);
        EXPECT(!Picture(0, &px, &rev, &w, &h) && !Picture(1, &px, &rev, &w, &h));
        game::g_ticks = 0;
        Tick(true);
        EXPECT(game::g_ticks == 1 && !game::g_lastWant);   // (it is told "not wanted": the server's lookup is let alone, the last read dropped)
    }

    // ---- ticked: the backs of both wrists to the face ----
    Configure(Cfg());
    EXPECT(Enabled());
    {
        printf("the backs of the wrists to the face\n");
        Feed f;
        f.Run(0.05f);
        Layer L;
        uint32_t need = 0;
        EXPECT(Draw(0, &L, &need) && !L.visible && Draw(1, &L, &need) && !L.visible);   // turned but not held yet: wanted, not shown (the pictures are painted meanwhile)
        f.Run(0.6f);
        Layer left, right;
        uint32_t needL = 0, needR = 0;
        EXPECT(Draw(0, &left, &needL) && Draw(1, &right, &needR));
        EXPECT(left.visible && left.pose.valid && right.visible && right.pose.valid);
        EXPECT(std::fabs(left.colorScale - 1.0f) < 1e-4f && std::fabs(right.colorScale - 1.0f) < 1e-4f);
        EXPECT(std::fabs(left.widthM - 0.10f) < 1e-5f && std::fabs(left.heightM - 0.05f) < 1e-5f);   // 10 cm wide, half as high
        // each over its own wrist: at its place, a little toward the head and 7 cm up
        EXPECT(std::fabs(left.pose.p.x - (-0.05f)) < 0.05f && std::fabs(left.pose.p.y - (1.45f + 0.07f)) < 0.05f && std::fabs(left.pose.p.z - (-0.38f)) < 0.06f);
        EXPECT(std::fabs(right.pose.p.x - 0.05f) < 0.05f && right.pose.p.x > left.pose.p.x);
        const V3 z = hset::Rot(left.pose.q, {0, 0, 1});
        EXPECT(hset::Dot(z, hset::Unit(hset::Sub({0, 1.6f, 0}, left.pose.p))) > 0.999f);   // its front toward the head
        std::vector<uint32_t> pl, pr;
        uint32_t revL = 0, revR = 0, w = 0, h = 0;
        EXPECT(WaitPicture(0, &pl, &revL, &w, &h));
        EXPECT(w == (uint32_t)kPicW && h == (uint32_t)kPicH && pl.size() == (size_t)w * h);
        EXPECT(revL >= needL && needL > 0);
        EXPECT(WaitPicture(1, &pr, &revR));
        EXPECT(revR >= needR && needR > 0);
        // the left shows the effects (3 chips), the right the companions (2 chips): the pictures differ, each has what it should
        const Look wantL = MakeLook(game::g_roster, game::g_status, ContentFor(Cfg(), 0), Cfg(), 0), wantR = MakeLook(game::g_roster, game::g_status, ContentFor(Cfg(), 1), Cfg(), 0);
        EXPECT(wantL.n == 3 && wantR.n == 2 && wantL.cell[0].kind == CellKind::Harm && wantR.cell[0].kind == CellKind::Companion);
        std::vector<uint32_t> wl, wr;
        wriststatuspaint::Render(wantL, wl);
        wriststatuspaint::Render(wantR, wr);
        EXPECT(pl == wl && pr == wr);

        // only one wrist at the face: only that layer
        f.angle[1] = 100.0f;
        f.Run(0.6f);
        EXPECT(Up(0) && !Up(1));
        f.angle[1] = 0.0f;
        f.Run(0.7f);
        EXPECT(Up(1));

        // it follows the wrist
        const V3 before = left.pose.p;
        f.pos[0] = {-0.25f, 1.45f, -0.38f};
        f.Run(0.1f);
        EXPECT(Draw(0, &left, &needL));
        EXPECT(left.pose.p.x < before.x - 0.15f);

        // a settings change is live: bigger, higher, along the arm
        Config c = Cfg();
        c.sizeCm = 14.0f;
        c.heightCm = 12.0f;
        Configure(c);
        f.Step();
        EXPECT(Draw(1, &right, &needR));
        EXPECT(std::fabs(right.widthM - 0.14f) < 1e-5f && right.pose.p.y > 1.45f + 0.10f);
        c.alongCm = 8.0f;
        Configure(c);
        f.Step();
        Layer shifted;
        EXPECT(Draw(1, &shifted, &needR) && hset::Dot(hset::Sub(shifted.pose.p, right.pose.p), hset::Sub(shifted.pose.p, right.pose.p)) > 0.06f * 0.06f);   // the anchor moved 8 cm along the grip's +Z
        Configure(Cfg());
        f.pos[0] = {-0.05f, 1.45f, -0.38f};
        f.Run(0.3f);

        // the sides: swapped, the left shows the companions
        c = Cfg();
        c.sides = 1;
        Configure(c);
        f.Step();
        std::vector<uint32_t> companions;
        wriststatuspaint::Render(wantR, companions);
        EXPECT(WaitFor(0, companions));
        // both on both: a wrist has 5 chips
        c.sides = 2;
        Configure(c);
        f.Step();
        std::vector<uint32_t> both;
        wriststatuspaint::Render(MakeLook(game::g_roster, game::g_status, 3, c, 0), both);
        EXPECT(WaitFor(1, both));
        // the effects off: the left (effects only, by default) has nothing and goes
        c = Cfg();
        c.effects = false;
        Configure(c);
        f.Run(0.4f);
        EXPECT(!Up(0) && Up(1));
        c.effects = true;
        c.companions = false;
        Configure(c);
        f.Run(0.7f);
        EXPECT(Up(0) && !Up(1));
        Configure(Cfg());
        f.Run(0.7f);
        EXPECT(Up(0) && Up(1));

        // what the hero has changed: a new picture comes without the panel going out
        game::g_roster.alive[4] = 6;
        f.Step();
        std::vector<uint32_t> px;
        uint32_t rev3 = 0;
        EXPECT(WaitPicture(1, &px, &rev3) && rev3 > revR);
        EXPECT(Up(1));
        game::g_roster.alive[4] = 5;
    }

    // ---- what takes it away ----
    auto up = [&](Feed& f) {
        Configure(Cfg());
        SetHero();
        f.flags = directinput::kFocused;
        f.angle[0] = f.angle[1] = 0.0f;
        f.Run(0.7f);
        return Up(0) && Up(1);
    };
    auto gone = [&](Feed& f, float within) {
        f.Run(within);
        return !Up(0) && !Up(1);
    };
    {
        printf("what takes it away\n");
        Feed f;
        EXPECT(up(f));
        f.flags |= directinput::kInMenu;
        EXPECT(gone(f, 0.25f));   // a menu: out in 0.12 s
        f.flags = directinput::kFocused;
        f.Run(0.7f);
        EXPECT(Up(0) && Up(1));   // menu closed: it comes back by itself
        f.flags |= directinput::kUiUp;
        EXPECT(gone(f, 0.25f));   // the skill panel / settings panel
        f.flags = directinput::kFocused;
        EXPECT(up(f));
        f.flags |= directinput::kOtherView;
        f.Step();
        { Layer L; uint32_t n; EXPECT(!Draw(0, &L, &n) && !Draw(1, &L, &n)); }   // another view: at once (the view gate in the frame)
        f.flags = directinput::kFocused;
        EXPECT(up(f));
        g_f4 = false;
        { Layer L; uint32_t n; EXPECT(!Draw(0, &L, &n) && !Draw(1, &L, &n)); }   // the view gate itself says no: at once, even before a frame
        f.Run(0.5f);
        { Layer L; uint32_t n; EXPECT(!Draw(0, &L, &n)); }
        g_f4 = true;
        f.Step();
        EXPECT(!Up(0) && !Up(1));   // back in F4: it starts again, not shown at once
        f.Run(0.7f);
        EXPECT(Up(0) && Up(1));
        f.flags = 0;   // no input focus
        EXPECT(gone(f, 0.25f));
        f.flags = directinput::kFocused;
        EXPECT(up(f));
        game::g_age = 3000.0;   // a stale read
        EXPECT(gone(f, 0.25f));
        EXPECT(up(f));
        game::g_roster = Roster{};   // nothing known
        game::g_status = Status{};
        EXPECT(gone(f, 0.25f));
        EXPECT(up(f));
        game::g_roster = Roster{};   // known, and nothing to show
        game::g_roster.known = true;
        game::g_status = Status{};
        game::g_status.known = true;
        EXPECT(gone(f, 0.25f));
        EXPECT(up(f));
        game::g_have = false;   // no read at all
        EXPECT(gone(f, 0.25f));
        EXPECT(up(f));
        f.angle[0] = f.angle[1] = 120.0f;   // the backs turned away (the palms to the face)
        EXPECT(gone(f, 0.5f));
        f.angle[0] = f.angle[1] = 0.0f;
        EXPECT(up(f));
        f.valid[0] = false;   // tracking of the left lost
        f.Run(0.5f);
        EXPECT(!Up(0) && Up(1));
        f.valid[0] = true;
        EXPECT(up(f));
        // no frames: a panel frozen in the air is not hung
        Sleep(300);
        { Layer L; uint32_t n; EXPECT(!Draw(0, &L, &n) && !Draw(1, &L, &n)); }
        f.Step();
        EXPECT(Up(0) && Up(1));
        // a stall of the frames (more than a quarter second of headset time): the wrists have to be shown again
        f.Step(0.5f);
        EXPECT(!Up(0) && !Up(1));
        f.Run(0.7f);
        EXPECT(Up(0) && Up(1));
        // a new native session forgets everything and shows again
        f.session = 2;
        f.Step();
        EXPECT(!Up(0) && !Up(1));
        f.Run(0.7f);
        EXPECT(Up(0) && Up(1));
    }

    // ---- the reads: only while ticked, in F4, in a game ----
    {
        printf("the reads\n");
        game::g_ticks = 0;
        Tick(true);
        EXPECT(game::g_ticks == 1 && game::g_lastWant);
        Tick(false);   // not in a game
        EXPECT(!game::g_lastWant);
        g_f4 = false;
        Tick(true);
        EXPECT(!game::g_lastWant);   // not F4
        g_f4 = true;
        Configure(Cfg(false));
        Tick(true);
        EXPECT(!game::g_lastWant);   // not ticked
        Configure(Cfg());
        Tick(true);
        EXPECT(game::g_lastWant);
    }

    // ---- unticking, Teardown, the picture afterwards ----
    {
        printf("off, and Teardown\n");
        Feed f;
        EXPECT(up(f));
        Configure(Cfg(false));
        f.Step();
        { Layer L; uint32_t n; EXPECT(!Draw(0, &L, &n) && !Draw(1, &L, &n)); }
        Configure(Cfg());
        EXPECT(up(f));
        std::vector<uint32_t> px;
        uint32_t rev = 0;
        WaitPicture(0, &px, &rev);
        Teardown();
        { Layer L; uint32_t n; EXPECT(!Draw(0, &L, &n)); }   // nothing wanted until a frame comes
        EXPECT(!Picture(0, &px, &rev, &rev, &rev));
        f.Step();
        f.Run(0.7f);
        EXPECT(Up(0));
        EXPECT(WaitPicture(0, &px, &rev) && Opaque(px) > 1200);   // a fresh picture for the new session
        const Config s = Settings();
        EXPECT(s.on && s.sides == 0 && s.sizeCm == 10.0f && s.coneDeg == 40.0f);
        // settings are put in range
        Config wild = Cfg();
        wild.sizeCm = 99.0f;
        wild.coneDeg = 1.0f;
        wild.sides = 7;
        Configure(wild);
        const Config t = Settings();
        EXPECT(t.sizeCm == kSizeMaxCm && t.coneDeg == handvitals::kConeMin && t.sides == 0);
        Configure(Cfg());
    }

    Shutdown();
    EXPECT(directinput::g_fn == nullptr);
    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
