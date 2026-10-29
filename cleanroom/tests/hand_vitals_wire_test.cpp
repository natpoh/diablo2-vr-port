// The hand vitals panel's wiring (vr/hand_vitals_wire.cpp) fed direct_input.h's frames, with stand-ins for what it calls: direct_input's
// Listen / Unlisten and the hero's numbers (vr/hand_vitals_game.h). The painter is the real one (on its worker thread). No game, no headset.
//
//   hand_vitals_wire_test
//
// Checks: nothing happens with the box unticked (no layer, no picture, no read); a palm turned to the face brings the layer up - the pose in
// front of the hand, the size from the settings, the colour scale rising - and the painter delivers a picture of at least the moment it
// was wanted; a menu, a panel of the plugin's, another view (the view gate), the lost focus, a stale or unknown or dead read, no frames
// for a moment, and the setting unticked take it away; the hero's numbers are read only while the box is ticked, in F4, in a game; a
// settings change is live; Teardown forgets the picture and a new one is painted afterwards; the soft pulse breathes.

#include "../../vr/direct_input.h"
#include "../../vr/hand_vitals.h"
#include "../../vr/hand_vitals_game.h"
#include "../../vr/hand_vitals_paint.h"
#include "../../vr/hand_vitals_wire.h"
#include "../../vr/view_gate.h"

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

namespace handvitals {
namespace game {
Vitals g_vitals;
bool g_have = true;
double g_age = 100.0;
int g_ticks = 0;
bool g_lastWant = false;
void Init(const D2RL::PluginContext*) {}
void Tick(bool want) { ++g_ticks; g_lastWant = want; }
bool Latest(Vitals* out, double* ageMs) {
    if (!g_have) return false;
    *out = g_vitals;
    *ageMs = g_age;
    return true;
}
}   // namespace game
}   // namespace handvitals

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

using namespace handvitals;
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

// A frame: the head at (0, 1.6, 0) looking down -z; the left hand at `pos` with its palm turned to the head `angleDeg` off (0 = at the face).
struct Feed {
    double time = 10.0;
    uint32_t counter = 0, session = 1, epoch = 1;
    uint32_t flags = directinput::kFocused;
    V3 leftPos{-0.05f, 1.45f, -0.38f};
    float angle = 0.0f;
    float squeeze = 0.0f;
    bool leftValid = true;

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
        const V3 dir = hset::Unit(hset::Sub(head, leftPos));
        const float a = angle * 3.14159265f / 180.0f;
        V3 perp = Cross(dir, {0, 1, 0});
        perp = hset::Unit(perp);
        const V3 n = hset::Unit(hset::Add(hset::Mul(dir, std::cos(a)), hset::Mul(perp, std::sin(a))));
        const Q q = FromTo({1.0f, 0.0f, 0.0f}, n);   // the left palm's normal is +X
        directinput::Hand& h = f.hand[0];
        h.grip.valid = leftValid ? 1 : 0;
        h.grip.pos[0] = leftPos.x;
        h.grip.pos[1] = leftPos.y;
        h.grip.pos[2] = leftPos.z;
        h.grip.rot[0] = q.x;
        h.grip.rot[1] = q.y;
        h.grip.rot[2] = q.z;
        h.grip.rot[3] = q.w;
        h.squeeze = squeeze;
        f.hand[1].grip.valid = 1;
        f.hand[1].grip.pos[0] = 0.3f;
        f.hand[1].grip.pos[1] = 0.9f;
        f.hand[1].grip.rot[3] = 1.0f;
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
    c.style = kStylePanel;   // (the default look is the orbs, which the effect draws: vr/hand_orbs.h)
    return c;
}

// Waits (up to 3 s) for the painter's picture; the revision and the size come back.
bool WaitPicture(std::vector<uint32_t>* px, uint32_t* rev, uint32_t* w = nullptr, uint32_t* h = nullptr) {
    uint32_t ww = 0, hh = 0;
    for (int i = 0; i < 300; ++i) {
        if (Picture(px, rev, &ww, &hh)) {
            if (w) *w = ww;
            if (h) *h = hh;
            return true;
        }
        Sleep(10);
    }
    return false;
}

void SetVitals(float life, float maxLife, float mana, float maxMana) {
    game::g_vitals = FromStats((int32_t)(life * 256), (int32_t)(maxLife * 256), (int32_t)(mana * 256), (int32_t)(maxMana * 256));
    game::g_have = true;
    game::g_age = 100.0;
}

}  // namespace

int main() {
    viewgate::Set(&IsF4);
    Init(nullptr);
    EXPECT(directinput::g_fn != nullptr);   // the listener is in
    SetVitals(700, 1000, 100, 400);

    // ---- unticked: nothing ----
    {
        printf("unticked\n");
        Feed f;
        f.Run(1.0f);
        Layer L;
        uint32_t need = 0;
        EXPECT(!Enabled());
        EXPECT(!Draw(&L, &need));
        std::vector<uint32_t> px;
        uint32_t rev = 0, w = 0, h = 0;
        Sleep(50);
        EXPECT(!Picture(&px, &rev, &w, &h));
        game::g_ticks = 0;
        Tick(true);
        EXPECT(game::g_ticks == 0);   // no read of the hero's numbers while it is off
    }

    // ---- ticked: a palm to the face ----
    Configure(Cfg());
    EXPECT(Enabled());
    {
        printf("palm to the face\n");
        Feed f;
        f.Run(0.05f);
        Layer L;
        uint32_t need = 0;
        EXPECT(Draw(&L, &need) && !L.visible);   // the palm is turned but not held yet: wanted, not shown (the picture is painted meanwhile)
        f.Run(0.6f);
        EXPECT(Draw(&L, &need));
        EXPECT(L.visible && L.pose.valid);
        EXPECT(std::fabs(L.colorScale - 1.0f) < 1e-4f);
        EXPECT(std::fabs(L.widthM - 0.08f) < 1e-5f && std::fabs(L.heightM - 0.08f * 2.0f / 3.0f) < 1e-5f);
        // over the left hand: at its place, a little toward the head and 6 cm up
        EXPECT(std::fabs(L.pose.p.x - (-0.05f)) < 0.05f && std::fabs(L.pose.p.y - (1.45f + 0.06f)) < 0.05f && std::fabs(L.pose.p.z - (-0.38f)) < 0.06f);
        const V3 z = hset::Rot(L.pose.q, {0, 0, 1});
        EXPECT(hset::Dot(z, hset::Unit(hset::Sub({0, 1.6f, 0}, L.pose.p))) > 0.999f);   // its front toward the head
        std::vector<uint32_t> px;
        uint32_t rev = 0, w = 0, h = 0;
        EXPECT(WaitPicture(&px, &rev, &w, &h));
        EXPECT(w == (uint32_t)handvitalspaint::kW && h == (uint32_t)handvitalspaint::kH && px.size() == (size_t)w * h);
        EXPECT(rev >= need && need > 0);
        // the picture is the hero's: red up to 70% in the life globe, the text
        const uint32_t pxl = px[(size_t)(100 + 60) * w + 96];
        EXPECT((pxl & 255) > 90 && (pxl & 255) > 2 * ((pxl >> 8) & 255));

        // it follows the hand
        const V3 before = L.pose.p;
        f.leftPos = {-0.25f, 1.45f, -0.38f};
        f.Run(0.1f);
        EXPECT(Draw(&L, &need));
        EXPECT(L.pose.p.x < before.x - 0.15f);

        // a settings change is live: bigger, higher
        Config c = Cfg();
        c.sizeCm = 12.0f;
        c.heightCm = 12.0f;
        Configure(c);
        f.Step();
        EXPECT(Draw(&L, &need));
        EXPECT(std::fabs(L.widthM - 0.12f) < 1e-5f);
        EXPECT(L.pose.p.y > 1.45f + 0.10f);
        Configure(Cfg());
        f.Step();
        EXPECT(Draw(&L, &need) && std::fabs(L.widthM - 0.08f) < 1e-5f);

        // the numbers changed: a new picture comes without the panel going out
        SetVitals(300, 1000, 100, 400);
        f.Step();
        uint32_t rev2 = 0;
        EXPECT(WaitPicture(&px, &rev2) && rev2 > rev);
        EXPECT(Draw(&L, &need) && L.visible);   // (the panel was not waiting for it: need is the first moment's)
    }

    // ---- what takes it away ----
    auto up = [&](Feed& f) {
        Configure(Cfg());
        SetVitals(700, 1000, 100, 400);
        f.flags = directinput::kFocused;
        f.angle = 0.0f;
        f.Run(0.7f);
        Layer L;
        uint32_t need = 0;
        return Draw(&L, &need) && L.visible;
    };
    auto gone = [&](Feed& f, float within) {
        f.Run(within);
        Layer L;
        uint32_t need = 0;
        return !Draw(&L, &need) || !L.visible;
    };
    {
        printf("what takes it away\n");
        Feed f;
        EXPECT(up(f));
        f.flags |= directinput::kInMenu;
        EXPECT(gone(f, 0.25f));   // a menu: out in 0.12 s
        f.flags = directinput::kFocused;
        f.Run(0.7f);
        { Layer L; uint32_t n; EXPECT(Draw(&L, &n) && L.visible); }   // menu closed: it comes back by itself
        f.flags |= directinput::kUiUp;
        EXPECT(gone(f, 0.25f));   // the skill panel / settings panel
        f.flags = directinput::kFocused;
        EXPECT(up(f));
        f.flags |= directinput::kOtherView;
        f.Step();
        { Layer L; uint32_t n; EXPECT(!Draw(&L, &n)); }   // another view: at once (the view gate in the frame)
        f.flags = directinput::kFocused;
        EXPECT(up(f));
        g_f4 = false;
        { Layer L; uint32_t n; EXPECT(!Draw(&L, &n)); }   // the view gate itself says no: at once, even before a frame
        f.Run(0.5f);
        { Layer L; uint32_t n; EXPECT(!Draw(&L, &n)); }
        g_f4 = true;
        f.Step();
        { Layer L; uint32_t n; EXPECT(!(Draw(&L, &n) && L.visible)); }   // back in F4: it starts again, not shown at once
        f.Run(0.7f);
        { Layer L; uint32_t n; EXPECT(Draw(&L, &n) && L.visible); }
        f.flags = 0;   // no input focus
        EXPECT(gone(f, 0.25f));
        f.flags = directinput::kFocused;
        EXPECT(up(f));
        game::g_age = 2000.0;   // a stale read
        EXPECT(gone(f, 0.25f));
        EXPECT(up(f));
        game::g_vitals = Vitals{};   // numbers that made no sense
        EXPECT(gone(f, 0.25f));
        EXPECT(up(f));
        SetVitals(0, 1000, 0, 400);   // dead
        EXPECT(gone(f, 0.25f));
        EXPECT(up(f));
        game::g_have = false;   // no read at all
        EXPECT(gone(f, 0.25f));
        EXPECT(up(f));
        f.angle = 120.0f;   // the palm turned away
        EXPECT(gone(f, 0.5f));
        f.angle = 0.0f;
        EXPECT(up(f));
        f.squeeze = 0.95f;   // a fist
        EXPECT(gone(f, 0.5f));
        f.squeeze = 0.0f;
        EXPECT(up(f));
        f.leftValid = false;   // tracking lost
        EXPECT(gone(f, 0.5f));
        f.leftValid = true;
        EXPECT(up(f));
        // no frames: a panel frozen in the air is not hung
        Sleep(300);
        { Layer L; uint32_t n; EXPECT(!Draw(&L, &n)); }
        f.Step();
        { Layer L; uint32_t n; EXPECT(Draw(&L, &n) && L.visible); }
        // a stall of the frames (more than a quarter second of headset time): the palm has to be shown again
        f.Step(0.5f);
        { Layer L; uint32_t n; EXPECT(!(Draw(&L, &n) && L.visible)); }
        f.Run(0.7f);
        { Layer L; uint32_t n; EXPECT(Draw(&L, &n) && L.visible); }
        // a new session forgets
        f.session = 2;
        f.Step();
        { Layer L; uint32_t n; EXPECT(!(Draw(&L, &n) && L.visible)); }
        f.Run(0.7f);
        { Layer L; uint32_t n; EXPECT(Draw(&L, &n) && L.visible); }
        // unticked while it is up: nothing at once
        Configure(Cfg(false));
        { Layer L; uint32_t n; EXPECT(!Draw(&L, &n)); }
        f.Run(0.2f);
        { Layer L; uint32_t n; EXPECT(!Draw(&L, &n)); }
        Configure(Cfg());
    }

    // ---- the reads ----
    {
        printf("the reads\n");
        game::g_ticks = 0;
        Tick(true);
        EXPECT(game::g_ticks == 1 && game::g_lastWant);
        Tick(false);
        EXPECT(game::g_ticks == 2 && !game::g_lastWant);   // (the game side queues nothing)
        g_f4 = false;
        Tick(true);
        EXPECT(game::g_ticks == 3 && !game::g_lastWant);   // not in F4: no read asked for
        g_f4 = true;
        Configure(Cfg(false));
        Tick(true);
        EXPECT(game::g_ticks == 3);   // off: not even called
        Configure(Cfg());
    }

    // ---- Teardown forgets; the picture is painted afresh ----
    {
        printf("teardown\n");
        Feed f;
        EXPECT(up(f));
        std::vector<uint32_t> px;
        uint32_t rev = 0;
        WaitPicture(&px, &rev);
        Teardown();
        { Layer L; uint32_t n; EXPECT(!Draw(&L, &n)); }
        f.Run(0.7f);
        uint32_t rev2 = 0;
        EXPECT(WaitPicture(&px, &rev2));   // a picture again after the session (the swapchain was destroyed with it)
        Layer L;
        uint32_t need = 0;
        EXPECT(Draw(&L, &need) && L.visible && rev2 >= need);
    }

    // ---- the other look: the 3D orbs are the effect's, the panel stands down ----
    {
        printf("the orbs look\n");
        Feed f;
        EXPECT(up(f));
        { Layer L; uint32_t n; EXPECT(Draw(&L, &n) && L.visible); }
        Config c = Cfg();
        c.style = kStyleOrbs;
        Configure(c);
        EXPECT(Enabled() && OrbsOn() && !PanelOn());
        { Layer L; uint32_t n; EXPECT(!Draw(&L, &n)); }   // no quad layer for the orbs: at once
        f.Run(0.5f);
        { Layer L; uint32_t n; EXPECT(!Draw(&L, &n)); }   // and a perfect palm shows nothing
        game::g_ticks = 0;
        Tick(true);
        EXPECT(game::g_ticks == 1 && game::g_lastWant);   // the hero's numbers are still read (the orbs use them)
        SetVitals(500, 1000, 100, 400);
        Vitals v;
        EXPECT(Snapshot(&v) && v.known && std::fabs(v.life - 500.0f) < 1e-3f && std::fabs(v.maxMana - 400.0f) < 1e-3f);
        game::g_age = 2000.0;
        EXPECT(!Snapshot(&v));   // a stale read is not the hero's numbers
        game::g_age = 100.0;
        game::g_vitals = Vitals{};
        EXPECT(!Snapshot(&v));   // unknown numbers are not either
        SetVitals(700, 1000, 100, 400);
        const Config now = Settings();
        EXPECT(now.on && now.style == kStyleOrbs && now.orbHands == 0 && std::fabs(now.orbSizeCm - 5.0f) < 1e-6f);
        c.orbHands = 2;
        c.orbSizeCm = 7.0f;
        c.orbLeft.upCm = 5.0f;
        c.orbLeft.alongCm = 1.0f;
        c.orbLeft.sideCm = -2.5f;
        c.orbLeft.rotXDeg = 30.0f;
        c.orbLeft.rotYDeg = -45.0f;
        c.orbLeft.rotZDeg = 170.0f;
        c.orbRight.rotXDeg = -10.0f;
        c.orbRight.rotYDeg = 20.0f;
        c.orbRight.rotZDeg = 999.0f;   // out of range: held at 180
        c.orbRight.upCm = 6.5f;
        c.orbRight.alongCm = -3.0f;
        c.orbRight.sideCm = 4.0f;
        c.orbMirror = true;
        c.orbOpacity = 0.5f;
        Configure(c);
        const Config live = Settings();
        EXPECT(live.orbHands == 2 && std::fabs(live.orbSizeCm - 7.0f) < 1e-6f && std::fabs(live.orbOpacity - 0.5f) < 1e-6f);   // the orbs' settings are live
        EXPECT(std::fabs(live.orbLeft.upCm - 5.0f) < 1e-6f && std::fabs(live.orbLeft.alongCm - 1.0f) < 1e-6f && std::fabs(live.orbLeft.sideCm + 2.5f) < 1e-6f);   // each wrist's three numbers
        EXPECT(std::fabs(live.orbLeft.rotXDeg - 30.0f) < 1e-6f && std::fabs(live.orbLeft.rotYDeg + 45.0f) < 1e-6f && std::fabs(live.orbLeft.rotZDeg - 170.0f) < 1e-6f &&
               std::fabs(live.orbRight.rotXDeg + 10.0f) < 1e-6f && std::fabs(live.orbRight.rotYDeg - 20.0f) < 1e-6f && std::fabs(live.orbRight.rotZDeg - 180.0f) < 1e-6f);   // the medallion's turn, each wrist, live
        EXPECT(std::fabs(live.orbRight.upCm - 6.5f) < 1e-6f && std::fabs(live.orbRight.alongCm + 3.0f) < 1e-6f && std::fabs(live.orbRight.sideCm - 4.0f) < 1e-6f && live.orbMirror);
        c.orbClassZCm[3] = 1.5f;
        c.orbClassZCm[6] = -99.0f;   // out of range: held at -5
        Configure(c);
        EXPECT(std::fabs(Settings().orbClassZCm[3] - 1.5f) < 1e-6f && std::fabs(Settings().orbClassZCm[6] + 5.0f) < 1e-6f && Settings().orbClassZCm[0] == 0.0f);   // the per-class depth, live
        c.orbClassZCm[6] = 0.0f;
        c.orbClassZCm[3] = 0.0f;
        c.orbShieldClassUpCm[3] = 1.2f;
        c.orbShieldClassUpCm[6] = -99.0f;       // out of range: held at -5
        c.orbShieldClassAlongCm[5] = -2.5f;
        c.orbShieldClassAlongCm[2] = 99.0f;     // out of range: held at 5
        Configure(c);
        EXPECT(std::fabs(Settings().orbShieldClassUpCm[3] - 1.2f) < 1e-6f && std::fabs(Settings().orbShieldClassUpCm[6] + 5.0f) < 1e-6f && Settings().orbShieldClassUpCm[0] == 0.0f &&
               std::fabs(Settings().orbShieldClassAlongCm[5] + 2.5f) < 1e-6f && std::fabs(Settings().orbShieldClassAlongCm[2] - 5.0f) < 1e-6f && Settings().orbShieldClassAlongCm[3] == 0.0f);   // the per-class shield depth and along, live
        c.orbShieldClassRotXDeg[3] = 35.0f;
        c.orbShieldClassRotYDeg[1] = -99.5f;
        c.orbShieldClassRotZDeg[6] = 999.0f;     // out of range: held at 180
        c.orbShieldClassRotXDeg[7] = -999.0f;    // held at -180
        Configure(c);
        EXPECT(std::fabs(Settings().orbShieldClassRotXDeg[3] - 35.0f) < 1e-6f && std::fabs(Settings().orbShieldClassRotYDeg[1] + 99.5f) < 1e-6f && std::fabs(Settings().orbShieldClassRotZDeg[6] - 180.0f) < 1e-6f &&
               std::fabs(Settings().orbShieldClassRotXDeg[7] + 180.0f) < 1e-6f && Settings().orbShieldClassRotXDeg[0] == 0.0f && Settings().orbShieldClassRotYDeg[3] == 0.0f);   // the per-class shield turn, live
        c.orbShieldClassRotXDeg[3] = 0.0f;
        c.orbShieldClassRotYDeg[1] = 0.0f;
        c.orbShieldClassRotZDeg[6] = 0.0f;
        c.orbShieldClassRotXDeg[7] = 0.0f;
        c.orbShieldClassUpCm[3] = 0.0f;
        c.orbShieldClassUpCm[6] = 0.0f;
        c.orbShieldClassAlongCm[5] = 0.0f;
        c.orbShieldClassAlongCm[2] = 0.0f;
        c.orbFill = kFillThumb;
        c.orbShape = kShapeBall;
        c.orbMetal = 2;
        c.orbPatina = 0.3f;
        c.orbDiscFaces = 0;
        Configure(c);
        {
            const Config m = Settings();   // the medallion's settings are live too
            EXPECT(m.orbFill == kFillThumb && m.orbShape == kShapeBall && m.orbMetal == 2 && std::fabs(m.orbPatina - 0.3f) < 1e-6f && m.orbDiscFaces == 0);
        }
        {   // the wrist under a shield: the tick and its seven coefficients are live too (defaults: ticked, a half turn, nothing added)
            EXPECT(Settings().orbUnderShield && std::fabs(Settings().orbShield.turnDeg - 180.0f) < 1e-6f && Settings().orbShield.upCm == 0.0f);
            c.orbUnderShield = false;
            c.orbShield.turnDeg = 120.0f;
            c.orbShield.alongCm = 1.5f;
            c.orbShield.sideCm = -2.0f;
            c.orbShield.upCm = 99.0f;   // out of range: held at 10
            c.orbShield.rotXDeg = 15.0f;
            c.orbShield.rotYDeg = -25.0f;
            c.orbShield.rotZDeg = 35.0f;
            Configure(c);
            const Config u = Settings();
            EXPECT(!u.orbUnderShield && std::fabs(u.orbShield.turnDeg - 120.0f) < 1e-6f && std::fabs(u.orbShield.alongCm - 1.5f) < 1e-6f && std::fabs(u.orbShield.sideCm + 2.0f) < 1e-6f &&
                   std::fabs(u.orbShield.upCm - 10.0f) < 1e-6f && std::fabs(u.orbShield.rotXDeg - 15.0f) < 1e-6f && std::fabs(u.orbShield.rotYDeg + 25.0f) < 1e-6f && std::fabs(u.orbShield.rotZDeg - 35.0f) < 1e-6f);
            c.orbUnderShield = true;
            c.orbShield = OrbUnderShield{};
            Configure(c);
            EXPECT(Settings().orbUnderShield && std::fabs(Settings().orbShield.turnDeg - 180.0f) < 1e-6f && Settings().orbShield.rotZDeg == 0.0f);
        }
        c.orbFill = 99;   // not a direction: the default, out of the hand
        Configure(c);
        EXPECT(Settings().orbFill == kFillOutOfHand);
        c.orbMirror = false;
        c.orbLeft.sideCm = 99.0f;   // out of range: put in range on the way in
        Configure(c);
        EXPECT(!Settings().orbMirror && std::fabs(Settings().orbLeft.sideCm - 10.0f) < 1e-6f);
        c.style = kStyleGlobes;
        Configure(c);
        EXPECT(Enabled() && OrbsOn() && !PanelOn());   // the game's own globes are the effect's too
        { Layer L; uint32_t n; EXPECT(!Draw(&L, &n)); }
        Configure(Cfg());
        EXPECT(PanelOn() && !OrbsOn());
        f.Step();
        EXPECT(up(f));   // back to the panel: it comes up again by the palm
        Configure(Cfg(false));
        EXPECT(!Enabled() && !PanelOn() && !OrbsOn());
        Configure(Cfg());
    }

    // ---- the soft pulse ----
    {
        printf("pulse\n");
        Feed f;
        Config c = Cfg();
        c.pulse = true;
        c.lowFrac = 0.30f;
        Configure(c);
        SetVitals(100, 1000, 100, 400);   // 10%: under the threshold
        f.angle = 0.0f;
        f.Run(0.7f);
        float lo = 2.0f, hi = -1.0f;
        for (int i = 0; i < 180; ++i) {
            f.Step();
            Layer L;
            uint32_t n;
            if (Draw(&L, &n) && L.visible) {
                lo = std::min(lo, L.colorScale);
                hi = std::max(hi, L.colorScale);
            }
        }
        EXPECT(lo > 0.79f && lo < 0.85f && hi > 0.97f && hi <= 1.0001f);   // breathing between 80% and 100%
        SetVitals(800, 1000, 100, 400);   // healed
        f.Run(0.3f);
        lo = 2.0f;
        hi = -1.0f;
        for (int i = 0; i < 90; ++i) {
            f.Step();
            Layer L;
            uint32_t n;
            if (Draw(&L, &n) && L.visible) {
                lo = std::min(lo, L.colorScale);
                hi = std::max(hi, L.colorScale);
            }
        }
        EXPECT(lo > 0.999f);   // no breath
        c.pulse = false;
        Configure(c);
        SetVitals(100, 1000, 100, 400);
        f.Run(0.5f);
        lo = 2.0f;
        for (int i = 0; i < 90; ++i) {
            f.Step();
            Layer L;
            uint32_t n;
            if (Draw(&L, &n) && L.visible) lo = std::min(lo, L.colorScale);
        }
        EXPECT(lo > 0.999f);   // the pulse is its own box: off, it never breathes
    }

    Shutdown();
    EXPECT(directinput::g_fn == nullptr);
    printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
