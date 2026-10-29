// The bow draw's wiring (vr/bow_draw_wire.cpp) fed direct_input.h's frames, with stand-ins for what it calls: direct_input's
// Listen / Take / Action / PadPulse / Haptic, gamestate's weapon, and skeletons' SetBowDraw / BowString. No game, no headset -
// and no BodyWalk: nothing here links vr/link.cpp, so a draw that works in this test works with direct_input=1 and BodyWalk absent.
//
//   bow_draw_wire_test
//
// Checks: in any view but F4 nothing exists (no Take, no press, no aim, no vectors, no feed, no model string asked for); in F4 the
// trigger or the grip takes the string and the control it took is the one kept from the layout (Take), the release is one
// press of the game's attack or of a skill slot's pad button; leaving F4 mid-draw ends it with no shot and lets go of the
// control; the grip taken by the draw is also kept from the belt.

#include "../../vr/bow_draw_wire.h"
#include "../../vr/direct_input.h"
#include "../../vr/direct_layout.h"   // (pure: the table a binding on the right trigger lives in, driven here by the takes the wire makes)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>

#include "d2r_vr_shared.h"
#include "d2r_vr_state.h"
#include "../../vr/skeletons.h"

// ---- the stand-ins ----
namespace gamestate {
uint32_t g_class = D2RVR_WEAPON_BOW;
uint32_t WeaponClass() { return g_class; }
uint32_t WeaponType() { return D2RVR_TYPE_BOW; }
uint32_t WeaponSet() { return 1; }
uint32_t HandsKey() { return 7; }
bool MenuOpen() { return false; }
}   // namespace gamestate

namespace directinput {
FrameFn g_fn = nullptr;
int g_takes = 0, g_takeHand = -1;
uint32_t g_takeMask = 0;
int g_actionsDown = 0, g_actionsUp = 0, g_pulses = 0, g_haptics = 0;
PadPress g_pulse{};
bool Listen(FrameFn fn, void*) { g_fn = fn; return true; }
void Take(int hand, uint32_t buttons, Source) { ++g_takes; g_takeHand = hand; g_takeMask = buttons; }
void Action(uint32_t bits, bool down, Source) { if (bits == D2RVR_ACT_ATTACK) (down ? g_actionsDown : g_actionsUp)++; }
void PadPulse(const PadPress& p, uint32_t) { ++g_pulses; g_pulse = p; }
void Haptic(int, float, float, float) { ++g_haptics; }
}   // namespace directinput

namespace skel {
int g_feeds = 0;
bool g_lastDrawing = false;
void SetBowDraw(const BowDraw& d) { ++g_feeds; g_lastDrawing = d.drawing; }
int g_ctlFeeds = 0, g_ctlSide = -1;       // [bow] hand_pose: the string hand's own controls, as the wire hands them over
float g_ctlTrigger = 0.0f, g_ctlSqueeze = 0.0f;
void SetBowHandControls(int side, float trigger, float squeeze) { ++g_ctlFeeds; g_ctlSide = side; g_ctlTrigger = trigger; g_ctlSqueeze = squeeze; }
bool g_haveString = false;
bool BowString(float out[3]) { out[0] = 0.0f; out[1] = 0.0f; out[2] = 0.0f; return g_haveString; }
bool BowAxis(float out[3]) { out[0] = 0.0f; out[1] = 0.0f; out[2] = -1.0f; return false; }   // ([bow] aim_along_hands: the bow's real axis, not read here)
bool BowAimState(float*, float*, float*, const char** why) { if (why) *why = "off"; return false; }
std::string BowAimWho() { return std::string(); }
bool g_haveRest = false;                  // [bow] arrow_rest_*: the arrow's rest point, in the hands' frame, as the hero's pose made it
float g_rest[3] = {};
bool BowRestPoint(float out[3]) { for (int i = 0; i < 3; ++i) out[i] = g_rest[i]; return g_haveRest; }
bool g_haveAnchor = false;                // [bow] string_anchor: where the string is held (the fingers' first joints), in the hands' frame
float g_anchor[3] = {};
bool StringAnchorHands(float out[3]) { for (int i = 0; i < 3; ++i) out[i] = g_anchor[i]; return g_haveAnchor; }
}   // namespace skel

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
        }                                                      \
    } while (0)   /* (a message after the condition says what it is for; it is not printed) */

constexpr double kDt = 1.0 / 90.0;
bool g_f4 = true;
int g_bowHandVr = 1;   // vrcam's: 1 = the bow in the left hand
bool InF4() { return g_f4; }
int BowHandNow() { return g_bowHandVr; }

// A headset: the head at the origin of the room, the left hand holds the bow, the right takes the string.
struct Rig {
    directinput::Frame f{};
    float str[3];
    float bow[3] = {-0.2f, 1.3f, -0.4f};
    Rig() {
        f.version = directinput::kVersion;
        f.session = 1;
        f.epoch = 1;
        f.flags = directinput::kFocused | directinput::kDirect;
        f.time = 100.0;
        f.head.rot[3] = 1.0f;
        f.head.pos[1] = 1.7f;
        f.head.valid = 1;
        for (int h = 0; h < 2; ++h) {
            directinput::Hand& d = f.hand[h];
            d.grip.rot[3] = 1.0f;
            d.grip.valid = 1;
            d.aim = d.grip;
            d.active = 1;
        }
        str[0] = bow[0]; str[1] = bow[1]; str[2] = bow[2] + 0.16f;   // the nock point: 16 cm behind the bow hand (the controller's +Z)
        Place();
    }
    void Place() {
        for (int i = 0; i < 3; ++i) { f.hand[0].grip.pos[i] = bow[i]; f.hand[1].grip.pos[i] = str[i]; }
    }
    void Step() {
        f.time += kDt;
        f.counter++;
        Place();
        if (directinput::g_fn) directinput::g_fn(f, nullptr);
    }
    void Run(double seconds) { for (double t = 0; t < seconds - 1e-9; t += kDt) Step(); }
    void Pull(float behind, double seconds) {   // the string hand to `behind` m behind the bow hand, in steps
        const float from = str[2], to = bow[2] + behind;
        const int n = (int)std::lround(seconds / kDt);
        for (int i = 1; i <= n; ++i) { str[2] = from + (to - from) * ((float)i / (float)n); Step(); }
    }
    void Trigger(float v) { f.hand[1].trigger = v; Step(); }
    void Grip(float v) { f.hand[1].squeeze = v; Step(); }
};

void Reset() {
    directinput::g_takes = directinput::g_takeHand = 0;
    directinput::g_takeHand = -1;
    directinput::g_takeMask = 0;
    directinput::g_actionsDown = directinput::g_actionsUp = directinput::g_pulses = directinput::g_haptics = 0;
    skel::g_feeds = 0;
    skel::g_lastDrawing = false;
    skel::g_haveString = false;
    skel::g_haveRest = false;
    skel::g_haveAnchor = false;
    skel::g_ctlFeeds = 0;
    skel::g_ctlSide = -1;
    skel::g_ctlTrigger = skel::g_ctlSqueeze = 0.0f;
}

void TestNotF4() {
    std::printf("any view but F4: nothing exists\n");
    Reset();
    g_f4 = false;
    bowdrawwire::Configure(true, 0, bowdraw::kStartEither, true, true);
    Rig r;
    r.Step();
    r.Trigger(1.0f);
    r.Pull(0.6f, 0.4);
    r.Run(0.2);
    EXPECT(!bowdrawwire::Owned() && !bowdrawwire::HoldsGrip(0) && !bowdrawwire::HoldsGrip(1));
    r.Trigger(0.0f);
    r.Grip(1.0f);
    r.Pull(0.0f, 0.2);
    r.Pull(0.6f, 0.4);
    r.Run(0.2);
    r.Grip(0.0f);
    r.Run(0.2);
    EXPECT(directinput::g_takes == 0);
    EXPECT(directinput::g_actionsDown == 0 && directinput::g_actionsUp == 0 && directinput::g_pulses == 0);
    EXPECT(directinput::g_haptics == 0);
    EXPECT(skel::g_feeds == 0 && skel::g_ctlFeeds == 0, "F4 only: the string hand's controls are not handed to the pose elsewhere");
    EXPECT(!bowdrawwire::AimLive());
    float h[3];
    EXPECT(!bowdrawwire::AimDir(h));
    bowtouch::VectorSet v;
    EXPECT(!bowdrawwire::DebugVectors(&v) && !bowdrawwire::DebugWanted());
    EXPECT(!bowdrawwire::WantsModelString());
    EXPECT(bowdrawwire::On());   // the box is the player's; the view is the gate
}

void TestTrigger() {
    std::printf("F4, the trigger\n");
    Reset();
    g_f4 = true;
    bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, true, false);
    Rig r;
    r.Step();
    r.Trigger(1.0f);
    EXPECT(bowdrawwire::Owned());
    EXPECT(directinput::g_takes >= 1 && directinput::g_takeHand == 1 && directinput::g_takeMask == (directinput::kTrigger | directinput::kTakeTrigger));
    EXPECT(skel::g_feeds > 0 && skel::g_lastDrawing);
    // the hand pose reads the string hand's trigger as the controller has it, with the draw holding the control: the raw value, not 0
    EXPECT(skel::g_ctlFeeds > 0 && skel::g_ctlSide == 0 && skel::g_ctlTrigger == 1.0f && skel::g_ctlSqueeze == 0.0f,
           "the string hand's raw trigger reaches the pose (side %d, trigger %.2f, squeeze %.2f)", skel::g_ctlSide, skel::g_ctlTrigger, skel::g_ctlSqueeze);
    r.Pull(0.6f, 0.4);
    r.Run(0.2);
    EXPECT(directinput::g_actionsDown == 0);
    r.Trigger(0.0f);
    EXPECT(skel::g_ctlTrigger == 0.0f, "and the let go too");
    EXPECT(directinput::g_actionsDown == 1 && directinput::g_pulses == 0);
    EXPECT(bowdrawwire::AimLive());
    float h[3];
    EXPECT(bowdrawwire::AimDir(h));
    EXPECT(std::fabs(h[2] + 1.0f) < 1e-3f);   // along the line to the bow: forward (-Z), no head turn
    r.Run(0.2);
    EXPECT(directinput::g_actionsUp == 1);
    EXPECT(!bowdrawwire::Owned());
    // not allowed while a menu is open: no draw
    Reset();
    r.f.flags |= directinput::kInMenu;
    r.Step();
    r.Trigger(0.0f);
    r.Step();
    r.Trigger(1.0f);
    r.Run(0.2);
    EXPECT(directinput::g_takes == 0 && !bowdrawwire::Owned());
}

// [bow] arrow_rest_aims: the arrow lies on the line from the string hand through its rest point on the bow hand (a point fixed in the hand's own
// frame), and the shot goes along that line when the option is on - else, or with no rest point made, along the line through the hand's own point.
void TestRestAims() {
    std::printf("F4, [bow] arrow_rest_aims\n");
    const float len = std::sqrt(0.05f * 0.05f + 0.6f * 0.6f);
    // 0 on with a rest point; 1 off with one; 2 on and none made; 3 on with one, the string held at the fingers' first joints (skel::StringAnchorHands:
    // the arrow lies on the line from THERE through the rest point, and the shot with it); 4 off, the string held there (the shot as before)
    for (int pass = 0; pass < 5; ++pass) {
        Reset();
        g_f4 = true;
        bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, true, false);
        bowdrawwire::SetRestAims(pass != 1 && pass != 4);
        skel::g_haveRest = pass != 2;
        skel::g_haveAnchor = pass >= 3;
        // the bow hand is at (-0.2 1.3 -0.4) in the room and the head at (0 1.7 0): in the hands' frame the hand is (-0.2 -0.4 -0.4); the rest point 5 cm above it
        skel::g_rest[0] = -0.2f; skel::g_rest[1] = -0.35f; skel::g_rest[2] = -0.4f;
        // the string hand is 60 cm behind it, level, at (-0.2 -0.4 0.2); the fingers' first joints 3 cm to the right, 4 up, 10 ahead of it
        skel::g_anchor[0] = -0.17f; skel::g_anchor[1] = -0.36f; skel::g_anchor[2] = 0.10f;
        Rig r;
        r.Step();
        r.Trigger(1.0f);
        r.Pull(0.6f, 0.4);   // the string hand 60 cm behind the bow hand
        r.Run(0.2);
        r.Trigger(0.0f);
        EXPECT(directinput::g_actionsDown == 1, "the release shoots");
        float h[3];
        EXPECT(bowdrawwire::AimDir(h), "the shot's aim is live");
        const float anchorLen = std::sqrt(0.03f * 0.03f + 0.01f * 0.01f + 0.5f * 0.5f);   // rest - anchor = (-0.03 0.01 -0.5)
        const float wantX = pass == 3 ? -0.03f / anchorLen : 0.0f, wantY = pass == 0 ? 0.05f / len : pass == 3 ? 0.01f / anchorLen : 0.0f,
                    wantZ = pass == 0 ? -0.6f / len : pass == 3 ? -0.5f / anchorLen : -1.0f;
        EXPECT(std::fabs(h[0] - wantX) < 2e-3f && std::fabs(h[1] - wantY) < 2e-3f && std::fabs(h[2] - wantZ) < 2e-3f,
               "pass %d: the shot goes %.4f %.4f %.4f, wanted %.4f %.4f %.4f", pass, h[0], h[1], h[2], wantX, wantY, wantZ);
        r.Run(0.5);
    }
    bowdrawwire::SetRestAims(true);   // (the default)
    Reset();
}

// [debug] bow_aim_ray: the shot's line while the string is drawn - from the arrow's release point, along the detector's aim - in the hands' frame.
void TestAimRay() {
    std::puts("F4, [debug] bow_aim_ray");
    auto near3 = [](bowtouch::Vec a, float x, float y, float z, float tol = 2e-3f) { return std::fabs(a.x - x) < tol && std::fabs(a.y - y) < tol && std::fabs(a.z - z) < tol; };
    Reset();
    g_f4 = true;
    bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, true, false);
    bowdrawwire::SetRestAims(true);
    skel::g_haveRest = false;
    skel::g_haveAnchor = false;
    bowtouch::Vec st, dir;
    bool model = true;
    {   // switched off: no ray, however the string is drawn
        bowdrawwire::SetAimRay(false);
        Rig r;
        r.Step();
        r.Trigger(1.0f);
        r.Pull(0.6f, 0.3);
        EXPECT(!bowdrawwire::AimRayWanted() && !bowdrawwire::AimRayLine(&st, &dir, &model), "off: nothing");
        r.Trigger(0.0f);
        r.Run(0.6);
    }
    Reset();
    bowdrawwire::SetAimRay(true);
    // the bow hand is at (-0.2 1.3 -0.4) in the room, the head at (0 1.7 0): in the hands' frame the hand is (-0.2 -0.4 -0.4); the string hand pulls 60 cm straight back
    Rig r;
    r.Step();
    r.Run(0.1);
    EXPECT(bowdrawwire::AimRayWanted() && !bowdrawwire::AimRayLine(&st, &dir, &model), "on, nothing drawn yet: no line");
    r.Trigger(1.0f);
    r.Pull(0.6f, 0.4);
    EXPECT(bowdrawwire::AimRayLine(&st, &dir, &model), "while the string is drawn: a line");
    EXPECT(near3(st, -0.2f, -0.4f, -0.4f) && near3(dir, 0.0f, 0.0f, -1.0f) && !model, "no rest point read: from the bow hand, straight ahead, the hand-set line (orange): start %.3f %.3f %.3f dir %.3f %.3f %.3f model %d",
           st.x, st.y, st.z, dir.x, dir.y, dir.z, model);
    // the model's arrow rest point read (5 cm above the hand): from there, the line through it, green
    skel::g_haveRest = true;
    skel::g_rest[0] = -0.2f; skel::g_rest[1] = -0.35f; skel::g_rest[2] = -0.4f;
    r.Run(0.1);
    const float len = std::sqrt(0.05f * 0.05f + 0.6f * 0.6f);
    EXPECT(bowdrawwire::AimRayLine(&st, &dir, &model) && model && near3(st, -0.2f, -0.35f, -0.4f) && near3(dir, 0.0f, 0.05f / len, -0.6f / len),
           "the rest point read: from it, the shot through it (green): start %.3f %.3f %.3f dir %.3f %.3f %.3f model %d", st.x, st.y, st.z, dir.x, dir.y, dir.z, model);
    // arrow_rest_aims off: the same start, but the line is the plain hand-set one again
    bowdrawwire::SetRestAims(false);
    r.Run(0.1);
    EXPECT(bowdrawwire::AimRayLine(&st, &dir, &model) && !model && near3(dir, 0.0f, 0.0f, -1.0f), "arrow_rest_aims off: the hand-set line (orange)");
    bowdrawwire::SetRestAims(true);
    r.Run(0.1);
    // the shot: the line stays for the 0.4 s the aim is live, then it goes
    EXPECT(bowdrawwire::Drawing(), "the string is held");
    {
        float s[3], d[3];
        EXPECT(!bowdrawwire::AimLine(s, d), "[bow] pitch_aim: no shot's line while the string is drawn");
    }
    r.Trigger(0.0f);
    EXPECT(directinput::g_actionsDown == 1, "the release shoots");
    EXPECT(!bowdrawwire::Drawing(), "let go: the string is not held");
    {   // [bow] pitch_aim: the shot's line whole - from the arrow's release point at the release, kept as the rest point moves on after it
        float s[3], d[3];
        EXPECT(bowdrawwire::AimLine(s, d) && near3({s[0], s[1], s[2]}, -0.2f, -0.35f, -0.4f) && near3({d[0], d[1], d[2]}, 0.0f, 0.05f / len, -0.6f / len),
               "the shot's line: start %.3f %.3f %.3f dir %.3f %.3f %.3f", s[0], s[1], s[2], d[0], d[1], d[2]);
        skel::g_rest[1] = -0.1f;
        r.Step();
        EXPECT(bowdrawwire::AimLine(s, d) && near3({s[0], s[1], s[2]}, -0.2f, -0.35f, -0.4f), "the release point is kept after the shot: %.3f %.3f %.3f", s[0], s[1], s[2]);
        skel::g_rest[1] = -0.35f;
    }
    r.Run(0.2);
    EXPECT(bowdrawwire::AimRayLine(&st, &dir, &model), "0.2 s after the shot: the line is still there");
    r.Run(0.5);
    EXPECT(!bowdrawwire::AimRayLine(&st, &dir, &model), "0.7 s after it: gone");
    // a view that is not F4: nothing, and what was drawn is gone with it
    r.Pull(0.16f, 0.3);   // (the string hand back at the nock first)
    r.Run(0.1);
    r.Trigger(1.0f);
    r.Pull(0.6f, 0.3);
    EXPECT(bowdrawwire::AimRayLine(&st, &dir, &model), "drawn again");
    g_f4 = false;
    EXPECT(!bowdrawwire::AimRayWanted() && !bowdrawwire::AimRayLine(&st, &dir, &model), "any view but F4: nothing");
    g_f4 = true;
    r.Trigger(0.0f);
    r.Run(0.8);
    // a draw that is cancelled (the weapon changes): the line goes with it
    skel::g_haveRest = false;
    bowdrawwire::SetAimRay(false);
    Reset();
}

void TestGrip() {
    std::printf("F4, the grip\n");
    Reset();
    g_f4 = true;
    bowdrawwire::Configure(true, 1, bowdraw::kStartGrip, true, false);   // the release presses the pad's B
    Rig r;
    r.Step();
    r.Trigger(1.0f);   // the trigger is the plain trigger
    EXPECT(!bowdrawwire::Owned() && directinput::g_takes == 0);
    r.Trigger(0.0f);
    r.Grip(1.0f);
    EXPECT(directinput::g_takes >= 1 && directinput::g_takeMask == (directinput::kSqueeze | directinput::kTakeSqueeze));
    EXPECT(bowdrawwire::HoldsGrip(0) && !bowdrawwire::HoldsGrip(1));   // the right hand's (vrcam's 0): the belt must not take it
    EXPECT(!bowdrawwire::Owned());   // the BodyWalk-pad mask is the trigger's
    r.Pull(0.6f, 0.4);
    r.Run(0.2);
    const int takes = directinput::g_takes;
    r.Trigger(1.0f);
    r.Trigger(0.0f);
    EXPECT(directinput::g_takeMask == (directinput::kSqueeze | directinput::kTakeSqueeze));   // the trigger was never taken
    EXPECT(directinput::g_takes > takes);
    r.Grip(0.0f);
    EXPECT(directinput::g_pulses == 1 && directinput::g_pulse.buttons == directinput::pad::kB && directinput::g_pulse.lt == 0);
    EXPECT(directinput::g_actionsDown == 0);
    r.Run(0.1);
    EXPECT(!bowdrawwire::HoldsGrip(0));
}

void TestLeavingF4() {
    std::printf("leaving F4 mid-draw\n");
    Reset();
    g_f4 = true;
    bowdrawwire::Configure(true, 0, bowdraw::kStartEither, true, true);
    Rig r;
    r.Step();
    r.Grip(1.0f);
    r.Pull(0.6f, 0.4);
    r.Run(0.2);
    EXPECT(bowdrawwire::HoldsGrip(0));
    g_f4 = false;   // F2, F3, F1 or a flat view
    const int takes = directinput::g_takes;
    r.Step();
    EXPECT(!bowdrawwire::HoldsGrip(0) && !bowdrawwire::Owned() && !bowdrawwire::AimLive() && !bowdrawwire::DebugWanted());
    r.Run(0.1);
    EXPECT(directinput::g_takes == takes);   // nothing taken any more: the control is the layout's
    r.Grip(0.0f);   // let go of the grip in the other view
    r.Run(0.3);
    EXPECT(directinput::g_actionsDown == 0 && directinput::g_pulses == 0);   // no shot
    EXPECT(!bowdrawwire::AimLive());
    // back in F4 with the grip already down at the string: not a fresh press, nothing starts by itself
    r.str[2] = r.bow[2] + 0.16f;
    r.Grip(1.0f);
    r.Run(0.1);
    const int takes2 = directinput::g_takes;
    g_f4 = true;
    r.Run(0.2);
    EXPECT(directinput::g_takes == takes2 && !bowdrawwire::HoldsGrip(0));
    r.Pull(0.6f, 0.4);
    r.Run(0.2);
    r.Grip(0.0f);
    r.Run(0.2);
    EXPECT(directinput::g_pulses == 0 && directinput::g_actionsDown == 0);
}

void TestDropout() {
    std::printf("a controller out of tracking for a moment\n");
    Reset();
    g_f4 = true;
    bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, true, false);
    Rig r;
    r.Step();
    r.Trigger(1.0f);
    r.Pull(0.6f, 0.4);
    r.Run(0.2);
    EXPECT(bowdrawwire::Owned());
    r.f.hand[1].active = 0;   // the string hand's controller drops out; it reads as let go meanwhile
    r.f.hand[1].trigger = 0.0f;
    const int takes = directinput::g_takes;
    for (int i = 0; i < 12; ++i) {   // 130 ms
        r.Step();
        EXPECT(bowdrawwire::Owned() && skel::g_lastDrawing);
    }
    EXPECT(directinput::g_takes > takes);   // the control stays the draw's the whole time
    EXPECT(directinput::g_actionsDown == 0);   // nothing is shot over a dropout
    r.f.hand[1].active = 1;
    r.f.hand[1].trigger = 1.0f;
    r.Run(0.1);
    EXPECT(bowdrawwire::Owned() && skel::g_lastDrawing);   // and the draw went on
    r.Trigger(0.0f);
    EXPECT(directinput::g_actionsDown == 1);
    r.Run(0.2);
    // a dropout of 0.4 s ends it
    Reset();
    r.f.hand[1].trigger = 0.0f;
    r.Step();
    r.Trigger(1.0f);
    r.Pull(0.6f, 0.4);
    r.Run(0.2);
    r.f.hand[1].active = 0;
    r.f.hand[1].trigger = 0.0f;
    r.Run(0.4);
    r.f.hand[1].active = 1;
    r.Step();
    EXPECT(directinput::g_actionsDown == 0 && directinput::g_pulses == 0);
}

void TestReach() {
    std::printf("[hands] bow_draw_reach_cm\n");
    Reset();
    g_f4 = true;
    bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 10.0f);   // 10 cm
    Rig r;
    r.str[2] = r.bow[2] + 0.16f + 0.15f;   // 15 cm from the touch point: outside 10 cm, inside the 28 of before
    r.Step();
    r.Trigger(1.0f);
    EXPECT(!bowdrawwire::Owned() && directinput::g_takes == 0);
    r.Trigger(0.0f);
    r.str[2] = r.bow[2] + 0.16f + 0.05f;   // 5 cm
    r.Step();
    r.Trigger(1.0f);
    EXPECT(bowdrawwire::Owned());
    r.Trigger(0.0f);
    r.Run(0.2);
    Reset();
    bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false);   // the default 28
    r.str[2] = r.bow[2] + 0.16f + 0.15f;
    r.Step();
    r.Trigger(1.0f);
    EXPECT(bowdrawwire::Owned());
    r.Trigger(0.0f);
    r.Run(0.2);
}

void TestShift() {
    std::printf("[hands] bow_draw_nock_shift_cm and the radius's limits\n");
    g_f4 = true;
    for (float shiftCm : {-20.0f, -6.0f, 0.0f, 12.0f, 25.0f}) {
        Reset();
        bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 8.0f, shiftCm);   // an 8 cm zone, centre moved
        Rig r;
        r.str[2] = r.bow[2] + 0.16f + shiftCm * 0.01f;   // on the shifted centre
        r.Step();
        r.Trigger(1.0f);
        EXPECT(bowdrawwire::Owned(), "the string is taken on the shifted centre, %+.0f cm", shiftCm);
        r.Trigger(0.0f);
        r.Run(0.2);
        Reset();
        Rig q;
        q.str[2] = q.bow[2] + 0.16f;   // the unshifted place
        q.Step();
        q.Trigger(1.0f);
        EXPECT(bowdrawwire::Owned() == (std::fabs(shiftCm) < 8.0f), "the old place is %s the zone shifted %+.0f cm", bowdrawwire::Owned() ? "in" : "out of", shiftCm);
        q.Trigger(0.0f);
        q.Run(0.2);
    }
    // the radius is held to 5..50 cm, the shift to -25..+25
    Reset();
    bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 1.0f, 99.0f);   // 1 cm -> 5 cm; 99 cm -> +25 cm
    Rig a;
    a.str[2] = a.bow[2] + 0.16f + 0.25f + 0.07f;   // 7 cm from the clamped centre: outside 5 cm
    a.Step();
    a.Trigger(1.0f);
    EXPECT(!bowdrawwire::Owned(), "outside the 5 cm floor");
    a.Trigger(0.0f);
    a.str[2] = a.bow[2] + 0.16f + 0.25f + 0.04f;   // 4 cm: inside
    a.Step();
    a.Trigger(1.0f);
    EXPECT(bowdrawwire::Owned(), "inside it, and the shift stopped at +25 cm");
    a.Trigger(0.0f);
    a.Run(0.2);
    Reset();
    bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 200.0f, -99.0f);   // 200 cm -> 50 cm; -99 -> -25
    Rig b;
    b.str[2] = b.bow[2] + 0.16f - 0.25f + 0.47f;   // 47 cm from the clamped centre: inside 50 cm
    b.Step();
    b.Trigger(1.0f);
    EXPECT(bowdrawwire::Owned(), "inside the 50 cm ceiling");
    b.Trigger(0.0f);
    b.Run(0.2);
    Reset();
    bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, NAN, NAN);   // not finite: as it was (28 cm, no shift)
    Rig c;
    c.str[2] = c.bow[2] + 0.16f + 0.25f;
    c.Step();
    c.Trigger(1.0f);
    EXPECT(bowdrawwire::Owned(), "NaN is the default 28 cm, no shift");
    c.Trigger(0.0f);
    c.Run(0.2);
}

void TestMinPull() {
    std::printf("[hands] bow_draw_min_pull_cm\n");
    g_f4 = true;
    // pulled to `behind` m behind the bow hand from the 16 cm nock and released: did the release press the attack?
    auto shot = [&](float minPullCm, float behind) {
        Reset();
        bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, minPullCm);
        Rig r;
        r.Step();
        r.Trigger(1.0f);
        r.Pull(behind, 0.3);
        r.Run(0.3);
        r.Trigger(0.0f);
        const bool shoots = directinput::g_actionsDown == 1;
        r.Run(0.2);
        return shoots;
    };
    EXPECT(!shot(12.0f, 0.275f) && shot(12.0f, 0.285f), "12 cm (the default): 28 cm apart");
    EXPECT(!shot(5.0f, 0.205f) && shot(5.0f, 0.215f), "5 cm: 21 cm apart");
    EXPECT(!shot(30.0f, 0.45f) && shot(30.0f, 0.47f), "30 cm: 46 cm apart");
    EXPECT(shot(1.0f, 0.215f) && !shot(1.0f, 0.205f), "1 cm is held to the 5 cm floor");
    EXPECT(!shot(99.0f, 0.55f) && shot(99.0f, 0.57f), "99 cm is held to the 40 cm ceiling: 56 cm apart");
    EXPECT(!shot(NAN, 0.275f) && shot(NAN, 0.285f), "NaN is the default 12 cm");
}

void TestDrawOnly() {
    std::printf("[hands] bow_draw_only: a press away from the string shoots nothing\n");
    g_f4 = true;
    const uint32_t trig = directinput::kTrigger | directinput::kTakeTrigger, grip = directinput::kSqueeze | directinput::kTakeSqueeze;
    const uint32_t shots = directinput::kNoShotTrigger | directinput::kNoShotSqueeze;   // the shots of the string hand's trigger and grip dropped, nothing taken whole
    {   // on: a trigger press away from the string keeps its other bindings but shoots nothing; no draw, no shot
        Reset();
        bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, 12.0f, true);
        Rig r;
        r.str[0] = r.bow[0] + 0.8f;   // 80 cm to the side: nowhere near the zone
        r.Step();
        r.Step();
        EXPECT(directinput::g_takes >= 1 && directinput::g_takeHand == 1 && directinput::g_takeMask == shots, "away from the zone only the shots go, from the start (mask %x)", directinput::g_takeMask);
        r.Trigger(1.0f);
        r.Run(0.2);
        EXPECT(!bowdrawwire::Owned() && skel::g_lastDrawing == false, "no draw away from the string");
        EXPECT(directinput::g_takeMask == shots, "the trigger itself stays the layout's away from the zone (mask %x)", directinput::g_takeMask);
        EXPECT(bowdrawwire::KeepsTrigger(1) && !bowdrawwire::KeepsTrigger(0), "the right trigger is the draw's to keep (the wheel asks)");
        const int takes = directinput::g_takes;
        r.Trigger(0.0f);
        r.Run(0.2);
        EXPECT(directinput::g_actionsDown == 0 && directinput::g_pulses == 0, "and nothing shot");
        EXPECT(directinput::g_takes > takes, "kept the whole time, not only while pressed");
    }
    {   // on: the press at the string still starts the draw, and the draw still shoots
        Reset();
        bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, 12.0f, true);
        Rig r;
        r.Step();
        r.Trigger(1.0f);
        EXPECT(bowdrawwire::Owned() && skel::g_lastDrawing, "the string is taken at the zone");
        r.Pull(0.6f, 0.4);
        r.Run(0.2);
        r.Trigger(0.0f);
        EXPECT(directinput::g_actionsDown == 1, "and the release shoots: %d", directinput::g_actionsDown);
        r.Run(0.2);
        // the release can press a skill slot instead: kept, as before
        Reset();
        bowdrawwire::Configure(true, 2, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, 12.0f, true);
        Rig q;
        q.Step();
        q.Trigger(1.0f);
        q.Pull(0.6f, 0.4);
        q.Run(0.2);
        q.Trigger(0.0f);
        EXPECT(directinput::g_pulses == 1 && directinput::g_pulse.buttons == directinput::pad::kX, "the skill slot's press goes through");
        q.Run(0.2);
    }
    {   // on, the grip as the start: the trigger is dropped at the string too; the grip at the string is the draw's and the trigger stays dropped
        Reset();
        bowdrawwire::Configure(true, 0, bowdraw::kStartGrip, false, false, 28.0f, 0.0f, 12.0f, true);
        Rig r;
        r.Step();
        r.Trigger(1.0f);
        EXPECT(!bowdrawwire::Owned() && directinput::g_takeMask == shots, "the trigger at the string starts nothing, and its shots are dropped (it is not the string's)");
        r.Trigger(0.0f);
        r.Grip(1.0f);
        EXPECT(skel::g_lastDrawing && directinput::g_takeMask == (trig | grip), "the grip takes the string; the trigger stays kept (mask %x)", directinput::g_takeMask);
        EXPECT(skel::g_ctlSqueeze == 1.0f && skel::g_ctlSide == 0, "the grip the draw took is still read raw by the pose (squeeze %.2f)", skel::g_ctlSqueeze);
        EXPECT(bowdrawwire::HoldsGrip(0), "and the belt cannot have the grip");
        r.Pull(0.6f, 0.4);
        r.Run(0.2);
        r.Grip(0.0f);
        EXPECT(directinput::g_actionsDown == 1, "the grip's release shoots");
        r.Run(0.2);
        // a grip pressed away from the string is not touched
        Reset();
        Rig q;
        q.str[0] = q.bow[0] + 0.8f;
        q.Step();
        q.Grip(1.0f);
        q.Run(0.2);
        EXPECT(directinput::g_takeMask == shots, "away from the string nothing is taken whole: the grip keeps its other duties, only its shots go (mask %x)", directinput::g_takeMask);
        q.Grip(0.0f);
        q.Run(0.1);
    }
    {   // off: the plain trigger as before
        Reset();
        bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, 12.0f, false);
        Rig r;
        r.str[0] = r.bow[0] + 0.8f;
        r.Step();
        r.Trigger(1.0f);
        r.Run(0.2);
        r.Trigger(0.0f);
        EXPECT(directinput::g_takes == 0 && !bowdrawwire::KeepsTrigger(1), "unticked: the old plain trigger, nothing kept");
    }
    {   // where the draw does not act, nothing is kept: another weapon, a menu, a panel of the plugin's up, another view, the box off
        bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, 12.0f, true);
        auto kept = [&](uint32_t classNow, uint32_t flagsAdd, bool f4) {
            Reset();
            g_f4 = f4;
            gamestate::g_class = classNow;
            Rig r;
            r.f.flags |= flagsAdd;
            r.str[0] = r.bow[0] + 0.8f;
            r.Step();
            r.Trigger(1.0f);
            r.Run(0.1);
            const bool k = directinput::g_takes > 0 || bowdrawwire::KeepsTrigger(1);
            r.Trigger(0.0f);
            r.Run(0.1);
            gamestate::g_class = D2RVR_WEAPON_BOW;
            g_f4 = true;
            return k;
        };
        EXPECT(kept(D2RVR_WEAPON_BOW, 0, true), "control: a bow in F4 keeps it");
        EXPECT(!kept(D2RVR_WEAPON_MELEE, 0, true), "a sword in hand: the trigger is the layout's");
        EXPECT(!kept(D2RVR_WEAPON_CROSSBOW, 0, true), "a crossbow: as before");
        EXPECT(!kept(D2RVR_WEAPON_NONE, 0, true), "empty hands");
        EXPECT(!kept(D2RVR_WEAPON_BOW, directinput::kInMenu, true), "a menu open");
        EXPECT(!kept(D2RVR_WEAPON_BOW, directinput::kUiUp, true), "the skill wheel or the settings panel up");
        EXPECT(!kept(D2RVR_WEAPON_BOW, 0, false), "any view but F4");
        bowdrawwire::Configure(false, 0, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, 12.0f, true);
        EXPECT(!kept(D2RVR_WEAPON_BOW, 0, true), "the draw unticked");
    }
}

// What a binding on the string hand's trigger (or grip) gives with a bow wielded, end to end: the wire's takes of the frame of the press, applied to
// the layout the way direct_input.cpp does (a whole take hides the control, the shots mask strips the shots), against a table where
// right_trigger = attack + a pad button (B) + a potion command and right_grip = attack + RB + another command.
struct Gave { bool attack = false, pad = false, cmd = false; };
Gave TableGives(uint32_t takeMask, directinput::layout::Ctl ctl) {
    using namespace directinput::layout;
    Table t = DefaultTable();
    t.holdEnabled = false;
    t.plain[kRTrigger] = Bind(OutAction(act::kAttack), OutPadBtn(directinput::pad::kB), OutCmd(0x1A));
    t.plain[kRGrip] = Bind(OutAction(act::kAttack), OutPadBtn(directinput::pad::kRightBumper), OutCmd(0x1B));
    const uint32_t take[2] = {0, takeMask};   // (the string hand is the right one: direct_input's hand 1)
    const bool hidden = (takeMask & (ctl == kRTrigger ? directinput::kTakeTrigger : directinput::kTakeSqueeze)) != 0;
    Controls up, down;
    down.v[ctl] = hidden ? 0.0f : 1.0f;
    Layout L;
    L.SetWorld(t);
    L.Step(Mode::World, up, 0, false, NoShotMask(take));
    const Outputs o = L.Step(Mode::World, down, 10, false, NoShotMask(take));
    Gave g;
    g.attack = (o.actions & act::kAttack) != 0;
    g.pad = o.pad.buttons != 0;
    g.cmd = (o.commands[0] & ((1ull << 0x1A) | (1ull << 0x1B))) != 0;
    return g;
}

void TestTable() {
    std::printf("what is bound to the string hand's trigger and grip, with a bow wielded\n");
    using directinput::layout::kRTrigger;
    using directinput::layout::kRGrip;
    g_f4 = true;
    gamestate::g_class = D2RVR_WEAPON_BOW;
    // a press of the trigger: the mask of the frame it is made in
    auto press = [&](int start, bool only, bool inZone, bool useGrip, uint32_t cls = D2RVR_WEAPON_BOW) {
        Reset();
        gamestate::g_class = cls;
        bowdrawwire::Configure(true, 0, start, false, false, 28.0f, 0.0f, 12.0f, only);
        Rig r;
        if (!inZone) r.str[0] = r.bow[0] + 0.8f;
        r.Step();
        r.Step();
        directinput::g_takeMask = 0;
        if (useGrip) r.Grip(1.0f); else r.Trigger(1.0f);
        const uint32_t mask = directinput::g_takeMask;
        const Gave g = TableGives(mask, useGrip ? kRGrip : kRTrigger);
        if (useGrip) r.Grip(0.0f); else r.Trigger(0.0f);
        r.Run(0.1);
        gamestate::g_class = D2RVR_WEAPON_BOW;
        return g;
    };
    using bowdraw::kStartTrigger; using bowdraw::kStartGrip; using bowdraw::kStartEither;
    // the trigger, away from the zone: the attack is dropped, the pad button and the command work as bound
    {
        const Gave g = press(kStartTrigger, true, false, false);
        EXPECT(!g.attack && g.pad && g.cmd, "away from the zone: no attack, the rest as bound (%d %d %d)", g.attack, g.pad, g.cmd);
    }
    // in the zone: nothing of the binding fires - the press is the draw's
    {
        const Gave g = press(kStartTrigger, true, true, false);
        EXPECT(!g.attack && !g.pad && !g.cmd, "in the zone: nothing (%d %d %d)", g.attack, g.pad, g.cmd);
        const Gave h = press(kStartEither, true, true, false);
        EXPECT(!h.attack && !h.pad && !h.cmd, "in the zone, trigger or grip: nothing");
        // the same without draw-only: the draw's own press still keeps the binding from firing
        const Gave k = press(kStartTrigger, false, true, false);
        EXPECT(!k.attack && !k.pad && !k.cmd, "in the zone without draw-only: nothing, the draw owns the press");
    }
    // draw-only off, away from the zone: as bound, the plain trigger of old
    {
        const Gave g = press(kStartTrigger, false, false, false);
        EXPECT(g.attack && g.pad && g.cmd, "draw-only off: as before");
    }
    // the grip starts the draw: the trigger is not the string's - in the zone or away its attack is dropped and the rest works
    {
        const Gave a = press(kStartGrip, true, true, false);
        EXPECT(!a.attack && a.pad && a.cmd, "grip starts: the trigger in the zone gives no attack, the rest as bound (%d %d %d)", a.attack, a.pad, a.cmd);
        const Gave b = press(kStartGrip, true, false, false);
        EXPECT(!b.attack && b.pad && b.cmd, "grip starts: the trigger away gives no attack, the rest as bound");
    }
    // the grip: in the zone it is the draw's (nothing fires); away, its attack is dropped and its other bindings stay
    {
        const Gave a = press(kStartGrip, true, true, true);
        EXPECT(!a.attack && !a.pad && !a.cmd, "the grip at the string: nothing fires (%d %d %d)", a.attack, a.pad, a.cmd);
        const Gave b = press(kStartGrip, true, false, true);
        EXPECT(!b.attack && b.pad && b.cmd, "the grip away from the string: its other bindings work, no attack (%d %d %d)", b.attack, b.pad, b.cmd);
        const Gave c = press(kStartEither, true, false, true);
        EXPECT(!c.attack && c.pad && c.cmd, "same with trigger-or-grip");
        const Gave d = press(kStartGrip, false, false, true);
        EXPECT(d.attack && d.pad && d.cmd, "draw-only off: the grip as bound");
    }
    // not a bow, or not allowed: everything as bound
    {
        const Gave a = press(kStartTrigger, true, false, false, D2RVR_WEAPON_MELEE);
        EXPECT(a.attack && a.pad && a.cmd, "a sword: as bound");
        const Gave b = press(kStartTrigger, true, true, false, D2RVR_WEAPON_CROSSBOW);
        EXPECT(b.attack && b.pad && b.cmd, "a crossbow, even in a bow's zone: as bound");
        const Gave c = press(kStartTrigger, true, false, false, D2RVR_WEAPON_NONE);
        EXPECT(c.attack && c.pad && c.cmd, "empty hands: as bound");
    }
    {   // another view, a menu, a panel of the plugin's up, the box off: as bound
        bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, 12.0f, true);
        auto pressWith = [&](uint32_t flagsAdd, bool f4, bool boxOn) {
            Reset();
            g_f4 = f4;
            bowdrawwire::Configure(boxOn, 0, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, 12.0f, true);
            Rig r;
            r.f.flags |= flagsAdd;
            r.str[0] = r.bow[0] + 0.8f;
            r.Step();
            r.Step();
            directinput::g_takeMask = 0;
            r.Trigger(1.0f);
            const Gave g = TableGives(directinput::g_takeMask, kRTrigger);
            r.Trigger(0.0f);
            r.Run(0.1);
            g_f4 = true;
            return g;
        };
        EXPECT(!pressWith(0, true, true).attack, "control: a bow in F4 away from the zone drops the attack");
        EXPECT(pressWith(directinput::kInMenu, true, true).attack, "a menu open: as bound");
        EXPECT(pressWith(directinput::kUiUp, true, true).attack, "the wheel or settings panel up: as bound");
        EXPECT(pressWith(0, false, true).attack, "any view but F4: as bound");
        EXPECT(pressWith(0, true, false).attack, "the draw unticked: as bound");
    }
    // a trigger pressed away from the zone and carried into it (not a draw: it was down before): from the zone on it is the draw's, whole -
    // what it was bound to stops there, and does not come back on the way out until the trigger is let go
    {
        Reset();
        bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, 12.0f, true);
        Rig r;
        r.str[0] = r.bow[0] + 0.8f;
        r.Step();
        r.Step();
        directinput::g_takeMask = 0;
        r.Trigger(1.0f);
        const uint32_t shotsOnly = directinput::kNoShotTrigger | directinput::kNoShotSqueeze;
        EXPECT(directinput::g_takeMask == shotsOnly, "pressed away: the shots only (mask %x)", directinput::g_takeMask);
        const float x0 = r.str[0], x1 = r.bow[0];
        for (int i = 1; i <= 20; ++i) { r.str[0] = x0 + (x1 - x0) * (float)i / 20.0f; r.Step(); }
        EXPECT(!bowdrawwire::Owned(), "a trigger that was down before the zone starts no draw");
        EXPECT((directinput::g_takeMask & directinput::kTakeTrigger) != 0, "but in the zone the trigger is the draw's, whole (mask %x)", directinput::g_takeMask);
        EXPECT(!TableGives(directinput::g_takeMask, kRTrigger).pad, "and what it was bound to does not fire there");
        r.Trigger(0.0f);
        r.Run(0.2);
    }
    // the wheel (which casts its picked slot on the trigger) stands down while the shots are stripped, in the zone and away
    {
        Reset();
        bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, false, false, 28.0f, 0.0f, 12.0f, true);
        Rig r;
        r.str[0] = r.bow[0] + 0.8f;
        r.Step();
        r.Step();
        EXPECT(bowdrawwire::KeepsTrigger(1), "away from the zone the wheel's cast is still stood down");
    }
}

// What the draw says about itself each frame, for what rides on it (the quiver, vr/bow_quiver_wire.cpp).
bowdrawwire::DrawInfo g_ds;
int g_hookCalls = 0;
void Hook(const bowdrawwire::DrawInfo& s, const directinput::Frame&) { g_ds = s; ++g_hookCalls; }

void TestDrawHook() {
    std::printf("the draw's hook (the quiver)\n");
    auto near3 = [](bowtouch::Vec a, float x, float y, float z, float tol = 2e-3f) { return std::fabs(a.x - x) < tol && std::fabs(a.y - y) < tol && std::fabs(a.z - z) < tol; };
    Reset();
    g_f4 = true;
    g_bowHandVr = 1;
    g_hookCalls = 0;
    g_ds = bowdrawwire::DrawInfo{};
    bowdrawwire::SetDrawHook(&Hook);
    bowdrawwire::Configure(true, 3, bowdraw::kStartTrigger, true, false);   // the release presses Y
    Rig r;
    r.Step();
    EXPECT(g_hookCalls == 1 && g_ds.active && g_ds.allowed && !g_ds.drawing && g_ds.bowHand == 0 && g_ds.button == 3 && !g_ds.haveArrow, "idle: the draw says it is on, nothing drawn, the bow in the left hand (direct_input's 0), Y");
    // drawn: the arrow lies from the string hand through the bow hand (no rest point read), in the hands' frame
    r.Trigger(1.0f);
    r.Pull(0.6f, 0.4);
    EXPECT(g_ds.drawing && g_ds.haveArrow, "drawn");
    EXPECT(near3(g_ds.rest, -0.2f, -0.4f, -0.4f) && near3(g_ds.stringHand, -0.2f, -0.4f, 0.2f), "the bow hand and the string hand, metres from the head: %.3f %.3f %.3f | %.3f %.3f %.3f",
           g_ds.rest.x, g_ds.rest.y, g_ds.rest.z, g_ds.stringHand.x, g_ds.stringHand.y, g_ds.stringHand.z);
    // the model's rest point read: the arrow rests there
    skel::g_haveRest = true;
    skel::g_rest[0] = -0.2f; skel::g_rest[1] = -0.35f; skel::g_rest[2] = -0.4f;
    r.Run(0.1);
    EXPECT(g_ds.haveArrow && near3(g_ds.rest, -0.2f, -0.35f, -0.4f), "the rest point read: the arrow rests on it");
    // the other hand holds the bow: the hand follows
    skel::g_haveRest = false;
    r.Trigger(0.0f);
    r.Run(0.8);
    g_bowHandVr = 0;
    r.Step();
    EXPECT(g_ds.bowHand == 1, "the bow in the right hand (direct_input's 1)");
    g_bowHandVr = 1;
    // the release: drawn no more, no arrow
    r.Step();
    EXPECT(!g_ds.drawing && !g_ds.haveArrow, "let go: not drawn");
    // a menu: not allowed
    r.f.flags |= directinput::kInMenu;
    r.Step();
    EXPECT(g_ds.active && !g_ds.allowed, "a menu is open: the draw is on but not allowed");
    r.f.flags &= ~directinput::kInMenu;
    // any view but F4, and unticked: the hook still hears, with nothing active
    const int calls = g_hookCalls;
    g_f4 = false;
    r.Step();
    EXPECT(g_hookCalls == calls + 1 && !g_ds.active && !g_ds.drawing && !g_ds.haveArrow, "any view but F4: told once a frame that the draw is not active");
    g_f4 = true;
    bowdrawwire::Configure(false, 0, bowdraw::kStartEither, true, false);
    r.Step();
    EXPECT(!g_ds.active && !g_ds.drawing, "unticked: not active");
    // no hook: no call
    bowdrawwire::SetDrawHook(nullptr);
    const int before = g_hookCalls;
    bowdrawwire::Configure(true, 0, bowdraw::kStartTrigger, true, false);
    r.Step();
    EXPECT(g_hookCalls == before, "no hook set: nothing is called");
}

// [bow] quiver_orbs: the free hand at an orb on the bow does not take the string there (bowdrawwire::SetStartBlock).
bool g_blockNow = false;
bool BlockNow() { return g_blockNow; }

void TestStartBlock() {
    std::printf("the free hand at an orb: the string is not taken there\n");
    Reset();
    g_f4 = true;
    g_blockNow = false;
    bowdrawwire::Configure(true, 0, bowdraw::kStartEither, true, false);
    bowdrawwire::SetStartBlock(&BlockNow);
    Rig r;
    r.Step();
    r.Run(0.1);
    // held back: the trigger pressed at the string, pulled and let go - no draw, no shot
    g_blockNow = true;
    r.Trigger(1.0f);
    r.Pull(0.6f, 0.4);
    EXPECT(!bowdrawwire::Owned(), "the start is held back: the string is not taken");
    r.Trigger(0.0f);
    r.Run(0.3);
    EXPECT(directinput::g_actionsDown == 0 && directinput::g_pulses == 0, "... and nothing is shot");
    // the grip too (the start is either)
    r.Pull(0.16f, 0.3);
    r.Grip(1.0f);
    r.Pull(0.6f, 0.4);
    r.Grip(0.0f);
    r.Run(0.3);
    EXPECT(directinput::g_actionsDown == 0, "the grip is held back as well");
    // let go of the block: the same pull draws and shoots
    g_blockNow = false;
    r.Pull(0.16f, 0.3);
    r.Run(0.2);
    r.Trigger(1.0f);
    r.Pull(0.6f, 0.4);
    EXPECT(bowdrawwire::Owned(), "no block: the string is taken");
    r.Trigger(0.0f);
    EXPECT(directinput::g_actionsDown == 1, "... and the release shoots");
    r.Run(0.8);
    // a draw already taken is not let go of when the hand comes by an orb
    r.Pull(0.16f, 0.3);
    r.Run(0.2);
    r.Trigger(1.0f);
    r.Pull(0.4f, 0.3);
    g_blockNow = true;
    r.Pull(0.6f, 0.3);
    EXPECT(directinput::g_actionsDown == 1 && bowdrawwire::Owned(), "the block turned on mid-draw: the draw goes on, no early shot");
    r.Trigger(0.0f);
    EXPECT(directinput::g_actionsDown == 2, "... and shoots at the release");
    r.Run(0.8);
    // no hook: nothing is asked
    bowdrawwire::SetStartBlock(nullptr);
    g_blockNow = true;
    r.Pull(0.16f, 0.3);
    r.Run(0.2);
    r.Trigger(1.0f);
    r.Pull(0.6f, 0.4);
    r.Trigger(0.0f);
    EXPECT(directinput::g_actionsDown == 3, "no hook: as ever");
    g_blockNow = false;
    r.Run(0.8);
}

void TestOff() {
    std::printf("unticked: nothing\n");
    Reset();
    g_f4 = true;
    bowdrawwire::Configure(false, 0, bowdraw::kStartEither, true, true);
    Rig r;
    r.Step();
    r.Trigger(1.0f);
    r.Pull(0.6f, 0.4);
    r.Trigger(0.0f);
    r.Run(0.2);
    EXPECT(directinput::g_takes == 0 && directinput::g_actionsDown == 0 && directinput::g_pulses == 0 && skel::g_feeds == 0);
    EXPECT(!bowdrawwire::Owned() && !bowdrawwire::AimLive() && !bowdrawwire::WantsModelString() && !bowdrawwire::DebugWanted());
}

}  // namespace

int main() {
    bowdrawwire::Init(nullptr, &BowHandNow, &InF4);
    EXPECT(directinput::g_fn != nullptr);
    TestNotF4();
    TestTrigger();
    TestRestAims();
    TestAimRay();
    TestGrip();
    TestLeavingF4();
    TestDropout();
    TestReach();
    TestShift();
    TestMinPull();
    TestDrawOnly();
    TestTable();
    TestDrawHook();
    TestStartBlock();
    TestOff();
    std::printf(g_fails ? "\n%d FAILED\n" : "\nbow_draw_wire_test: all passed\n", g_fails);
    return g_fails ? 1 : 0;
}
