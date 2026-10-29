// vr/throw_gesture_wire.cpp on scripted headset frames, with stand-ins for the game's weapon state and
// for vr/direct_input.h's calls (no game, no headset):
//   throw_gesture_wire_test       exit code 0 = every check passed
//
// What is checked: a throw released with a javelin in the hand presses the game's attack once (Action
// D2RVR_ACT_ATTACK, held ~70 ms, then let go) and puts the heading in the head's own frame; the trigger is
// taken from the layout only with such a weapon in hand; a sword, a menu, a lost focus, a changed epoch,
// a camera that cannot aim, the box unticked: no press. The grip mode (throw_by_grip): the grip is taken
// instead of the trigger, held as long as you like, the release at the end of a swing is the throw, a release
// at rest is not; GripIsThrows() and LastThrowTickMs() say what the weapon-weight logic needs.

#include "direct_input.h"
#include "throw_gesture_wire.h"
#include "wheel_game.h"
#include "view_gate.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "d2r_vr_shared.h"
#include "d2r_vr_state.h"
#include "throw_born.h"

// -- stand-ins ----------------------------------------------------------------------------------------------------------
namespace {
uint32_t g_weapon = 0;
uint32_t g_weaponHand = 0;   // the game's hand the weapon is in (0 right, 1 left)
bool g_placed = false;       // the weight model's word (throwwire::SetCarrySource): it holds the weapon,
int g_carrying = -1;         // ...the hand that carries it (0 right, 1 left, -1 none)
bool g_holds[2] = {};        // ...the hands on it (the first weapon's: the right slot's in these tests)
int g_secondHand = -1;       // the second weapon (a weapon in each hand): the hand that carries it,
bool g_secondPlaced = false; // ...it is a body of its own,
unsigned g_holders2 = 0;     // ...the hands on it (1 right, 2 left)
int g_firstSlot = 0;         // the first weapon's slot
int g_secondThrown = 0;      // weight::SecondThrown calls
uint32_t g_otherKind = 0;    // the other slot's weapon kind (gamestate::HandKind)
unsigned HoldersOf(int slot) {
	if (slot == g_firstSlot) return (g_holds[0] ? 1u : 0u) | (g_holds[1] ? 2u : 0u);
	return g_secondPlaced ? g_holders2 : 0u;
}
bool g_menu = false;
bool g_canAim = true;
directinput::FrameFn g_listener = nullptr;
struct ActionCall { uint32_t bits; bool down; directinput::Source src; double at; };
std::vector<ActionCall> g_actions;
struct PulseCall { uint16_t buttons; uint8_t lt; double at; };
std::vector<PulseCall> g_pulses;      // the pad pulses (the Throw skill's slot button)
bool g_potion = false;                // the held item is a throwing potion
wheel::Snapshot g_snap;               // the controller's skill slots the wheel's reads would give
bool g_snapReady = true;
bool g_auto = false;                  // the swap hook is in and on ([hands] throw_auto_skill)
uint32_t g_heroMode = 1;
int g_takes = 0;        // Take calls on the right trigger
int g_gripTakes = 0;    // Take calls on the right grip
int g_leftTakes = 0, g_leftGripTakes = 0;   // ... on the left trigger and the left grip ([hands] throw_left_hand)
double g_now = 0.0;   // the frame being fed, for the log of calls
bool g_byGrip = false;  // the mode under test
// the weight model's account of the throw (throwwire::SetWeightSource)
bool g_wValid = false;
long long g_wTickOffsetMs = 0;       // the outcome's step against GetTickCount64 now
float g_wLandX = 0.0f, g_wLandZ = -12.0f, g_wVelX = 0.0f, g_wVelZ = -10.0f;   // head frame, x right, z BACK
float g_wFlight = 0.8f;
int g_wCalls = 0;
bool WeightStub(throwsrc::Outcome* o) {
	++g_wCalls;
	*o = throwsrc::Outcome();
	o->valid = g_wValid;
	o->tickMs = (uint64_t)((long long)GetTickCount64() + g_wTickOffsetMs);
	o->velocity[0] = g_wVelX; o->velocity[1] = 1.0f; o->velocity[2] = g_wVelZ;
	o->speed = std::sqrt(g_wVelX * g_wVelX + g_wVelZ * g_wVelZ + 1.0f);
	o->landing[0] = g_wLandX; o->landing[1] = -1.5f; o->landing[2] = g_wLandZ;
	o->flightS = g_wFlight;
	o->elevationDeg = 5.0f;
	return g_wValid;
}
bool g_hideReady = false;             // [hands] potion_hide_game: the renderer hooks are in and the option on
bool g_hideWanted = false;            // ...the option alone
int g_hideArms = 0;                   // potionhide::Arm calls
uint64_t g_hideNow = 0;               // the last Arm's time (GetTickCount64)
double g_hideFlight = 0.0;
bool g_bornReady = false;             // [hands] throw_direct: the create hook is in and the option on
int g_bornArms = 0;                   // throwborn::Arm calls
uint64_t g_bornNow = 0, g_bornLandAt = 0;   // the last Arm's times (GetTickCount64)
double g_bornFlight = 0.0;
}  // namespace

namespace throwborn {
void Init(const D2RL::PluginContext*) {}
void Install() {}
bool Ready() { return g_bornReady; }
bool Arm(uint64_t now, uint64_t landAt, double flightS) { ++g_bornArms; g_bornNow = now; g_bornLandAt = landAt; g_bornFlight = flightS; return g_bornReady; }
bool ActionFramePassed() { return false; }
}  // namespace throwborn

namespace potionhide {
void Init(const D2RL::PluginContext*) {}
void Install() {}
bool Wanted() { return g_hideWanted; }
bool Ready() { return g_hideReady; }
bool Arm(uint64_t now, double flightS) { ++g_hideArms; g_hideNow = now; g_hideFlight = flightS; return g_hideReady; }
}  // namespace potionhide

namespace gamestate {
uint32_t WeaponType() { return g_weapon; }
bool MenuOpen() { return g_menu; }
uint32_t WeaponHand() { return g_weaponHand; }
uint32_t HandKind(int side) { return (uint32_t)(side & 1) == g_weaponHand ? g_weapon : g_otherKind; }
uint32_t HandsKey() { return g_weapon * 131u + g_otherKind * 7u + g_weaponHand; }
uint32_t HandCode(int) { return 'g' | ('p' << 8) | ('s' << 16) | (' ' << 24); }
bool HandIsThrowPotion(int) { return g_potion; }
}  // namespace gamestate

namespace wheelgame {
bool Latest(wheel::Snapshot* out, uint32_t* revision) {
    if (!g_snapReady) return false;
    *out = g_snap;
    out->at = GetTickCount64();
    out->ready = true;
    *revision = out->revision;
    return true;
}
}  // namespace wheelgame

namespace directinput {
bool Listen(FrameFn fn, void*) { g_listener = fn; return true; }
void Action(uint32_t actBits, bool down, Source src) { g_actions.push_back({actBits, down, src, g_now}); }
void PadPulse(const PadPress& p, uint32_t) { g_pulses.push_back({p.buttons, p.lt, g_now}); }
void Take(int hand, uint32_t buttons, Source src) {
    if (hand == 1 && (buttons & kTakeTrigger) && src == kSrcJavelin) ++g_takes;
    if (hand == 1 && (buttons & kTakeSqueeze) && (buttons & kSqueeze) && src == kSrcJavelin) ++g_gripTakes;
    if (hand == 0 && (buttons & kTakeTrigger) && src == kSrcJavelin) ++g_leftTakes;
    if (hand == 0 && (buttons & kTakeSqueeze) && (buttons & kSqueeze) && src == kSrcJavelin) ++g_leftGripTakes;
}
}  // namespace directinput

namespace {

int g_fails = 0, g_checks = 0;
#define EXPECT(c, ...)                                           \
	do {                                                         \
		++g_checks;                                              \
		if (!(c)) {                                              \
			++g_fails;                                           \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c);  \
			std::printf(__VA_ARGS__);                            \
			std::printf("\n");                                   \
		}                                                        \
	} while (0)

constexpr double kPi = 3.14159265358979323846;

struct Rig {
	double yawLeftDeg = 0.0;   // the head's turn about the vertical, + to the left
	uint32_t flags = directinput::kFocused;
	uint32_t session = 1, epoch = 1;
	bool headValid = true;
	bool grip = false;         // the swing's control is the grip (squeeze), not the trigger
	int xr = 1;                // the controller that swings (0 left, 1 right); the other one is not tracked...
	bool both = false;         // ...unless this: the other does exactly the same
};

// A throw along the room's direction (dx, dz): trigger down at 0.2 s, a wind-up back along -dir, a swing of
// 0.16 s at 5 m/s, the trigger up right after it. Returns the frames fed, one per 1/90 s, until `end`.
struct Swing {
	float dx = 0.0f, dz = -1.0f, dy = 0.0f;   // the way it goes (unit with dy)
	double t0 = 0.2;
	bool release = true;
	float speed = 5.0f;
	double idle = 0.0;          // the control down at t0, the hand at rest this long before the wind-up (a held grip)
	void Hand(double t, float* p, float* trig) const {
		const double windup = 0.30, swing = 0.16, a = t0 + idle;
		const float back = 0.25f;
		float along = 0.0f;
		if (t >= a && t < a + windup) along = -back * (float)std::sin((t - a) / windup * kPi / 2);
		else if (t >= a + windup && t < a + windup + swing) along = -back + speed * (float)(t - a - windup);
		else if (t >= a + windup + swing) along = -back + speed * (float)swing;
		p[0] = 0.3f + dx * along;
		p[1] = 1.3f + dy * along;
		p[2] = -0.2f + dz * along;
		const double rel = a + windup + swing + 0.005;
		*trig = t >= t0 && (!release || t < rel) ? 1.0f : 0.0f;
	}
	double End() const { return t0 + idle + 0.3 + 0.16 + 0.6; }
	double Released() const { return t0 + idle + 0.3 + 0.16 + 0.005; }   // the first frame with the control up is just after
};

double g_t = 0.0;   // the next frame's time
// Frames of 1/90 s from the running clock up to `upTo` (absolute seconds), the swing scripted in the same clock.
void Feed(const Rig& rig, const Swing& sw, double upTo = -1.0) {
	const double end = upTo < 0.0 ? sw.End() : upTo;
	const double dt = 1.0 / 90.0;
	for (; g_t <= end + 1e-9; g_t += dt) {
		const double t = g_t;
		g_now = t;
		directinput::Frame f{};
		f.version = directinput::kVersion;
		f.counter = (uint32_t)(t * 90.0);
		f.session = rig.session;
		f.epoch = rig.epoch;
		f.flags = rig.flags;
		f.time = t;
		f.head.valid = rig.headValid ? 1 : 0;
		const double h = rig.yawLeftDeg * kPi / 360.0;   // a turn about +y: q = (0, sin, 0, cos)
		f.head.rot[1] = (float)std::sin(h);
		f.head.rot[3] = (float)std::cos(h);
		f.head.pos[1] = 1.7f;
		directinput::Hand& r = f.hand[rig.xr];
		float trig = 0.0f;
		sw.Hand(t, r.grip.pos, &trig);
		r.grip.rot[3] = 1.0f;
		r.grip.valid = 1;
		if (rig.grip) r.squeeze = trig; else r.trigger = trig;
		r.active = 1;
		if (rig.both) f.hand[rig.xr ^ 1] = r;
		g_listener(f, nullptr);
	}
}

int Presses() { int n = 0; for (const ActionCall& a : g_actions) if (a.down) ++n; return n; }
int Releases() { int n = 0; for (const ActionCall& a : g_actions) if (!a.down) ++n; return n; }
// Between scenarios: an idle frame lets go of a press still held, then the calls are forgotten, the aim cleared, a second goes by.
void Reset() {
	{ Rig idle; Swing none; none.t0 = 1e9; g_t += 0.5; Feed(idle, none, g_t); }
	throwwire::SetModes(false, false);
	throwwire::SetModes(!g_byGrip, g_byGrip);
	throwwire::SetFollowHand(false);
	throwwire::SetCarrySource(throwwire::CarrySource{});
	g_placed = false; g_carrying = -1; g_holds[0] = g_holds[1] = false;
	g_secondHand = -1; g_secondPlaced = false; g_holders2 = 0; g_firstSlot = 0; g_secondThrown = 0; g_otherKind = 0;
	g_actions.clear(); g_pulses.clear(); g_takes = 0; g_gripTakes = 0; g_leftTakes = 0; g_leftGripTakes = 0; g_weapon = 0; g_weaponHand = 0; g_menu = false; g_canAim = true;
	g_potion = false; g_snap = wheel::Snapshot(); g_snapReady = true; g_auto = false;
	throwwire::SetWeightSource(nullptr);
	throwwire::SetPotionMode(0);
	g_wFlight = 0.8f;
	g_bornReady = false; g_bornArms = 0; g_bornNow = 0; g_bornLandAt = 0; g_bornFlight = 0.0;
	g_hideReady = false; g_hideWanted = false; g_hideArms = 0; g_hideNow = 0; g_hideFlight = 0.0;
	g_wValid = false; g_wTickOffsetMs = 0; g_wLandX = 0.0f; g_wLandZ = -12.0f; g_wVelX = 0.0f; g_wVelZ = -10.0f; g_wCalls = 0;
	g_t += 1.0;
}

// A swing that begins 0.2 s after the running clock.
Swing Fresh() { Swing sw; sw.t0 = g_t + 0.2; return sw; }
// ...and the time just after its trigger went up.
double AfterRelease(const Swing& sw) { return sw.Released() + 0.02; }

void TestJavelinThrow() {
	std::printf("a javelin thrown straight ahead\n");
	Reset();
	g_weapon = D2RVR_TYPE_JAVELIN;
	float r = 9, f = 9;
	Rig rig;
	Swing sw = Fresh();
	Feed(rig, sw, AfterRelease(sw));
	EXPECT(Presses() == 1, "presses %d", Presses());
	EXPECT(!g_actions.empty() && g_actions[0].bits == D2RVR_ACT_ATTACK && g_actions[0].down && g_actions[0].src == directinput::kSrcJavelin,
	       "the first call is not the javelin's attack press");
	EXPECT(throwwire::Aim(&r, &f), "no aim just after the release");
	EXPECT(std::fabs(r) < 0.02f && std::fabs(f - 1.0f) < 0.02f, "heading right %.3f forward %.3f", r, f);
	Feed(rig, sw, AfterRelease(sw) + 0.2);   // the rest of the press
	EXPECT(Presses() == 1 && Releases() == 1, "presses %d releases %d", Presses(), Releases());
	if (Presses() && Releases()) {
		double down = 0.0, up = 0.0;
		for (const ActionCall& a : g_actions) (a.down ? down : up) = a.at;
		EXPECT(up - down >= 0.07 - 1e-6 && up - down < 0.10, "held %.3f s", up - down);
	}
	EXPECT(g_takes > 0, "the trigger was not taken from the layout");
}

void TestInfo() {
	std::printf("what the release was (for the aim point)\n");
	Reset();
	g_weapon = D2RVR_TYPE_THROWING;
	Rig rig;
	Swing sw = Fresh();
	throwwire::ThrowInfo a, b;
	Feed(rig, sw, AfterRelease(sw));
	EXPECT(throwwire::Info(&a), "no info after a throw");
	EXPECT(a.speed > 3.5f && a.speed < 5.3f && std::fabs(a.horizSpeed - a.speed) < 0.1f && std::fabs(a.vertSpeed) < 0.2f,
	       "speed %.2f horizontal %.2f vertical %.2f", a.speed, a.horizSpeed, a.vertSpeed);
	EXPECT(std::fabs(a.handBelowHeadM - 0.4f) < 0.01f, "the hand is 0.4 m below the head: %.3f", a.handBelowHeadM);
	EXPECT(std::fabs(a.right) < 0.02f && std::fabs(a.forward - 1.0f) < 0.02f, "heading %.3f %.3f", a.right, a.forward);
	// a throw going up at 30 degrees: the vertical part is positive and the heading is the ground part
	Reset();
	g_weapon = D2RVR_TYPE_THROWING;
	Swing up = Fresh();
	up.dy = 0.5f; up.dz = -0.8660254f;
	Feed(rig, up, AfterRelease(up));
	EXPECT(throwwire::Info(&b), "no info after the second throw");
	EXPECT(b.seq != a.seq, "the throw's number did not move");
	EXPECT(b.vertSpeed > 1.5f && std::fabs(b.vertSpeed / b.horizSpeed - 0.5f / 0.866f) < 0.1f, "vertical %.2f over %.2f", b.vertSpeed, b.horizSpeed);
	EXPECT(std::fabs(b.right) < 0.02f && std::fabs(b.forward - 1.0f) < 0.02f, "heading %.3f %.3f", b.right, b.forward);
	Sleep(700);
	EXPECT(!throwwire::Info(&b), "the info outlives the aim");
}

// The weight model's account of a throw (vr/throw_source.h): its heading and landing win when it is valid and belongs to the throw; else the hand's.
void TestWeightSource() {
	std::printf("the weight model's account of the throw\n");
	Rig rig;
	float r = 9, f = 9;
	throwwire::ThrowInfo ti;
	// no source registered: the hand (thrown straight ahead: right 0, forward 1)
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(throwwire::Info(&ti) && !ti.fromWeight && ti.landingM == 0.0f && std::fabs(ti.forward - 1.0f) < 0.02f, "no source: from weight %d", ti.fromWeight);
		EXPECT(g_wCalls == 0, "nothing asked");
	}
	// a valid outcome for the throw, to the head's right: the heading is the weight model's, the landing is its own metres
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		throwwire::SetWeightSource(&WeightStub);
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f;
		Swing sw = Fresh();   // the hand goes straight ahead
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r - 1.0f) < 1e-4f && std::fabs(f) < 1e-4f, "weight heading right %.3f forward %.3f", r, f);
		EXPECT(throwwire::Info(&ti) && ti.fromWeight && std::fabs(ti.landingM - 10.0f) < 1e-4f && std::fabs(ti.flightS - 0.8f) < 1e-4f && std::fabs(ti.elevationDeg - 5.0f) < 1e-4f,
		       "info: from weight %d landing %.2f", ti.fromWeight, ti.landingM);
		EXPECT(std::fabs(ti.right - 1.0f) < 1e-4f && !ti.headingFromVelocity && !ti.weightCapped, "info heading %.3f", ti.right);
		EXPECT(ti.speed > 3.5f && std::fabs(ti.horizSpeed - ti.speed) < 0.2f, "the hand's own numbers stay (%.2f m/s)", ti.speed);
		EXPECT(Presses() == 1, "the throw is pressed once, as always: %d", Presses());
		// once taken it stays for that throw, whatever the model says next
		g_wLandX = -10.0f;
		const int calls = g_wCalls;
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r - 1.0f) < 1e-4f, "kept: %.3f", r);
		EXPECT(g_wCalls == calls, "no second ask for this throw (%d)", g_wCalls - calls);
		// the next throw is asked again
		Feed(rig, sw, sw.End() + 0.8);
		Sleep(600);
		Swing sw2 = Fresh();
		g_wLandX = -10.0f;
		Feed(rig, sw2, AfterRelease(sw2));
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r + 1.0f) < 1e-4f, "the second throw is the model's new account: %.3f", r);
	}
	// the head's own yaw does not matter: the model's frame is the head's, and so is Aim's
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		throwwire::SetWeightSource(&WeightStub);
		g_wValid = true; g_wLandX = -6.0f; g_wLandZ = -8.0f;   // left and ahead: right -0.6, forward 0.8
		Rig turned;
		turned.yawLeftDeg = 90.0;
		Swing sw = Fresh();
		sw.dx = -1.0f; sw.dz = 0.0f;   // the hand goes where the turned head looks
		Feed(turned, sw, AfterRelease(sw));
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r + 0.6f) < 1e-4f && std::fabs(f - 0.8f) < 1e-4f, "right %.3f forward %.3f", r, f);
	}
	// not valid, or an outcome of another moment: the hand's heading and no landing
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		throwwire::SetWeightSource(&WeightStub);
		g_wValid = false; g_wLandX = 10.0f; g_wLandZ = 0.0f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r) < 0.02f && std::fabs(f - 1.0f) < 0.02f, "invalid: the hand's heading %.3f %.3f", r, f);
		EXPECT(throwwire::Info(&ti) && !ti.fromWeight && ti.landingM == 0.0f && ti.flightS == 0.0f, "invalid: info from weight %d", ti.fromWeight);
		EXPECT(g_wCalls >= 1, "it was asked");
		EXPECT(Presses() == 1, "and the throw still goes: %d", Presses());

		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		throwwire::SetWeightSource(&WeightStub);
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wTickOffsetMs = -5000;   // five seconds old
		Swing s2 = Fresh();
		Feed(rig, s2, AfterRelease(s2));
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r) < 0.02f && std::fabs(f - 1.0f) < 0.02f, "stale: the hand's heading %.3f %.3f", r, f);
		EXPECT(throwwire::Info(&ti) && !ti.fromWeight, "stale: not from weight");

		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		throwwire::SetWeightSource(&WeightStub);
		g_wValid = true; g_wLandX = 0.0f; g_wLandZ = 0.0f; g_wVelX = 0.0f; g_wVelZ = 0.0f;   // no landing, no velocity: no heading
		Swing s3 = Fresh();
		Feed(rig, s3, AfterRelease(s3));
		EXPECT(throwwire::Info(&ti) && !ti.fromWeight && std::fabs(ti.forward - 1.0f) < 0.02f, "degenerate: not from weight");
	}
	// the outcome comes in a moment after the release (the model had not seen the grip open): taken by the first read that finds it
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		throwwire::SetWeightSource(&WeightStub);
		g_wValid = false; g_wLandX = 10.0f; g_wLandZ = 0.0f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r) < 0.02f, "not yet: the hand's heading %.3f", r);
		g_wValid = true;
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r - 1.0f) < 1e-4f, "then the model's %.3f", r);
		EXPECT(throwwire::Info(&ti) && ti.fromWeight && std::fabs(ti.landingM - 10.0f) < 1e-4f, "and its landing");
	}
	// no throw, no account: a released throw is what asks, a hand at rest never does
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		throwwire::SetWeightSource(&WeightStub);
		g_wValid = true;
		Swing sw = Fresh();
		sw.release = false;
		Feed(rig, sw);
		EXPECT(!throwwire::Aim(&r, &f) && !throwwire::Info(&ti), "no throw: no aim");
		EXPECT(g_wCalls == 0, "and the model is not asked (%d)", g_wCalls);
	}
	// the grip mode asks the same
	{
		g_byGrip = true;
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		throwwire::SetWeightSource(&WeightStub);
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f;
		Rig grip; grip.grip = true; grip.flags = directinput::kFocused | directinput::kDirect;
		Swing sw = Fresh(); sw.idle = 2.0;
		Feed(grip, sw, AfterRelease(sw));
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r - 1.0f) < 1e-4f && throwwire::Info(&ti) && ti.fromWeight, "grip mode: right %.3f", r);
		g_byGrip = false;
		Reset();
	}
}

// potion_throw_mode 1 (vr/throw_land.h): a potion's game throw waits for the weight model's flight, then is pressed at its landing.
void TestPotionLanding() {
	std::printf("potion_throw_mode 1: the game's throw at the model's landing\n");
	Rig rig;
	float r = 9, f = 9;
	throwwire::ThrowInfo ti;
	// mode 0 (the default): a potion is pressed at the release
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && throwwire::Aim(&r, &f), "mode 0: pressed at once (%d)", Presses());
		EXPECT(throwwire::PressIsThrow(), "the A press held now is a throw's (the pad poll leaves Stand Still out of it when asked)");
		Feed(rig, sw, sw.Released() + 0.4);
		EXPECT(!throwwire::PressIsThrow() && Releases() == 1, "and it is not after the press is let go");
	}
	// mode 1: nothing at the release, nothing live; the press comes when the flight is over, aimed by the weight model
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		throwwire::SetPotionMode(1);
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 0 && g_pulses.empty(), "mode 1: nothing pressed at the release (%d)", Presses());
		EXPECT(!throwwire::Aim(&r, &f) && !throwwire::Info(&ti), "and no aim is live yet");
		Feed(rig, sw, sw.Released() + 0.60);
		EXPECT(Presses() == 0, "0.6 s into a 0.8 s flight: still nothing (%d)", Presses());
		Feed(rig, sw, sw.Released() + 0.74);
		EXPECT(Presses() == 0, "0.74 s: still nothing (%d; press at %.3f, release at %.3f, flight %.2f)", Presses(), g_actions.empty() ? -1.0 : g_actions[0].at, sw.Released(), g_wFlight);
		Feed(rig, sw, sw.Released() + 0.95);
		EXPECT(Presses() == 1, "the flight is over: pressed once (%d)", Presses());
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r - 1.0f) < 1e-4f, "the aim is live, the weight model's: right %.3f", r);
		EXPECT(throwwire::Info(&ti) && ti.fromWeight && std::fabs(ti.landingM - 10.0f) < 1e-4f, "with its landing");
		// the press is the same press as always: held 70 ms, then let go
		Feed(rig, sw, sw.Released() + 1.3);
		EXPECT(Presses() == 1 && Releases() == 1, "presses %d releases %d", Presses(), Releases());
		Sleep(600);
		EXPECT(!throwwire::Aim(&r, &f), "and the aim lets go after its window");
	}
	// a second throw while the first potion is in the air is not taken
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		throwwire::SetPotionMode(1);
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 3.0f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		Swing sw2 = Fresh();
		Feed(rig, sw2, AfterRelease(sw2));
		EXPECT(Presses() == 0, "the second swing, 0.7 s later, presses nothing (%d)", Presses());
		Feed(rig, sw2, sw.Released() + 3.2);
		EXPECT(Presses() == 1, "one potion, one press at its landing (%d)", Presses());
	}
	// a javelin is not a potion: pressed at the release; no weight account: at the release
	{
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		throwwire::SetPotionMode(1);
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1, "a javelin in mode 1: at once (%d)", Presses());
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetPotionMode(1);   // (no weight source)
		Swing s2 = Fresh();
		Feed(rig, s2, AfterRelease(s2));
		EXPECT(Presses() == 1, "a potion with no weight account in mode 1: at once (%d)", Presses());
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		throwwire::SetPotionMode(1);
		g_wValid = false;   // the model has no valid outcome
		Swing s3 = Fresh();
		Feed(rig, s3, AfterRelease(s3));
		EXPECT(Presses() == 1, "a potion whose weight outcome is not valid: at once (%d)", Presses());
	}
	// the play stops while the potion flies: the press is dropped and never comes
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		throwwire::SetPotionMode(1);
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 1.0f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		Rig other = rig;
		other.flags = directinput::kFocused | directinput::kOtherView;   // the view left F4
		Feed(other, sw, sw.Released() + 0.4);
		Feed(rig, sw, sw.Released() + 1.6);
		EXPECT(Presses() == 0, "the view changed: no press (%d)", Presses());
		// the weapon changed
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		throwwire::SetPotionMode(1);
		g_wValid = true; g_wFlight = 1.0f;
		Swing s2 = Fresh();
		Feed(rig, s2, AfterRelease(s2));
		g_weapon = D2RVR_TYPE_SWORD;
		Feed(rig, s2, s2.Released() + 1.6);
		EXPECT(Presses() == 0, "the weapon changed: no press (%d)", Presses());
	}
	// a hand that is only at rest never arms anything
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		throwwire::SetPotionMode(1);
		g_wValid = true; g_wFlight = 0.5f;
		Swing sw = Fresh();
		sw.release = false;
		Feed(rig, sw);
		EXPECT(Presses() == 0, "no release: no press");
	}
	Reset();
}

void TestHeadFrame() {
	std::printf("the head's own frame\n");
	struct Case { double yaw; float dx, dz; float right, fwd; const char* what; };
	const Case cases[] = {
		{0.0, 0.0f, -1.0f, 0.0f, 1.0f, "facing -z, thrown along -z"},
		{0.0, 1.0f, 0.0f, 1.0f, 0.0f, "facing -z, thrown along +x (to the right)"},
		{0.0, -1.0f, 0.0f, -1.0f, 0.0f, "facing -z, thrown along -x (to the left)"},
		{90.0, -1.0f, 0.0f, 0.0f, 1.0f, "head turned left to face -x, thrown along -x"},
		{90.0, 0.0f, -1.0f, 1.0f, 0.0f, "head turned left to face -x, thrown along -z (its right)"},
		{-90.0, 1.0f, 0.0f, 0.0f, 1.0f, "head turned right to face +x, thrown along +x"},
		{180.0, 0.0f, 1.0f, 0.0f, 1.0f, "head turned about to face +z, thrown along +z"},
	};
	for (const Case& c : cases) {
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig;
		rig.yawLeftDeg = c.yaw;
		Swing sw = Fresh();
		sw.dx = c.dx; sw.dz = c.dz;
		float r = 9, f = 9;
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(throwwire::Aim(&r, &f), "%s: no aim", c.what);
		EXPECT(std::fabs(r - c.right) < 0.03f && std::fabs(f - c.fwd) < 0.03f, "%s: right %.3f forward %.3f", c.what, r, f);
	}
}

void TestPotionBorn() {
	std::printf("throw_direct: the game's throw at the release, its potion timed to the model's landing\n");
	Rig rig;
	float r = 9, f = 9;
	throwwire::ThrowInfo ti;
	// ready, a 0.8 s flight: the press is at the release (the animation plays while the model flies), the hook is armed with the landing time
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		throwwire::SetPotionMode(1);   // even with the mode "at the landing": throw_direct presses at the release
		g_bornReady = true;
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && g_bornArms == 1, "pressed at the release and the hook armed (%d, %d)", Presses(), g_bornArms);
		EXPECT(g_bornLandAt > g_bornNow && std::fabs((double)(g_bornLandAt - g_bornNow) - 800.0) < 60.0, "the model lands %lld ms on", (long long)(g_bornLandAt - g_bornNow));
		EXPECT(std::fabs(g_bornFlight - 0.8) < 1e-3, "with its flight: %.3f s", g_bornFlight);
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r - 1.0f) < 1e-4f, "aimed at the model's landing: right %.3f", r);
		EXPECT(throwwire::Info(&ti) && ti.fromWeight && std::fabs(ti.landingM - 10.0f) < 1e-4f, "with the weight model's landing");
		Feed(rig, sw, sw.Released() + 1.3);
		EXPECT(Presses() == 1 && g_bornArms == 1, "and no more (%d, %d)", Presses(), g_bornArms);
	}
	// a short flight: the same, at once
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		g_bornReady = true;
		g_wValid = true; g_wLandX = 3.0f; g_wLandZ = 0.0f; g_wFlight = 0.25f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && g_bornArms == 1, "a 0.25 s flight: pressed at the release, armed (%d, %d)", Presses(), g_bornArms);
	}
	// not ready (off, no hook, faulted): the throw is the mode 0 one - pressed at the release, the hook not armed
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		g_bornReady = false;
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && g_bornArms == 0, "not ready: pressed at the release, nothing armed (%d, %d)", Presses(), g_bornArms);
	}
	// not ready, with the mode "at the landing": that mode as before (nothing at the release)
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		throwwire::SetPotionMode(1);
		g_bornReady = false;
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 0 && g_bornArms == 0, "not ready, mode 1: nothing at the release, as before (%d, %d)", Presses(), g_bornArms);
	}
	// a thrown weapon (not a potion) is never armed
	{
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN; g_potion = false; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		g_bornReady = true;
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(g_bornArms == 0, "a javelin: the hook is not armed (%d)", g_bornArms);
	}
	// no weight model's account (the hand's own estimate): no landing time, nothing armed
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		g_bornReady = true;
		g_wValid = false;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(g_bornArms == 0, "no weight account: not armed (%d)", g_bornArms);
	}
}

void TestPotionHide() {
	std::printf("potion_hide_game: the game's own potion is not drawn while our potion flies\n");
	Rig rig;
	// ready and wanted, the weight model has the throw (our model flies 0.8 s): armed at the press, with the flight
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		g_hideWanted = true; g_hideReady = true;
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && g_hideArms == 1, "pressed at the release and the hide armed (%d, %d)", Presses(), g_hideArms);
		EXPECT(std::fabs(g_hideFlight - 0.8) < 1e-3, "with the model's flight: %.3f s", g_hideFlight);
		Feed(rig, sw, sw.Released() + 1.3);
		EXPECT(Presses() == 1 && g_hideArms == 1, "and no more (%d, %d)", Presses(), g_hideArms);
	}
	// with throw_direct as well: both armed by the one press
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		g_hideWanted = true; g_hideReady = true; g_bornReady = true;
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && g_hideArms == 1 && g_bornArms == 1, "the one press arms both (%d, %d, %d)", Presses(), g_hideArms, g_bornArms);
	}
	// the mode "when the potion lands": the press is late, and the hide is armed with it - not before (the game's missile is made after the press)
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		throwwire::SetPotionMode(1);
		g_hideWanted = true; g_hideReady = true;
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 0 && g_hideArms == 0, "mode 1: nothing yet at the release (%d, %d)", Presses(), g_hideArms);
		Feed(rig, sw, sw.Released() + 1.3);
		EXPECT(Presses() == 1 && g_hideArms == 1, "armed with the late press (%d, %d)", Presses(), g_hideArms);
	}
	// wanted but not ready (no hook, faulted): the throw is as before, nothing armed
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		g_hideWanted = true; g_hideReady = false;
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && g_hideArms == 0, "not ready: pressed, nothing armed (%d, %d)", Presses(), g_hideArms);
	}
	// not wanted: nothing armed
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		g_hideWanted = false; g_hideReady = true;
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && g_hideArms == 0, "off: pressed, nothing armed (%d, %d)", Presses(), g_hideArms);
	}
	// no weight account: no model of ours flies, so the game's potion must stay drawn
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING; g_potion = true; g_auto = true;
		g_hideWanted = true; g_hideReady = true;
		g_wValid = false;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && g_hideArms == 0, "no weight account: the game's potion stays drawn (%d, %d)", Presses(), g_hideArms);
	}
	// a javelin: the hide is for potions only
	{
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN; g_potion = false; g_auto = true;
		throwwire::SetWeightSource(&WeightStub);
		g_hideWanted = true; g_hideReady = true;
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f; g_wFlight = 0.8f;
		Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(g_hideArms == 0, "a javelin: not armed (%d)", g_hideArms);
	}
}

void TestNoThrow() {
	std::printf("what must not throw\n");
	float r, f;
	{   // a sword
		Reset();
		g_weapon = D2RVR_TYPE_SWORD;
		Rig rig; Swing sw = Fresh(); Feed(rig, sw);
		EXPECT(Presses() == 0 && g_takes == 0 && !throwwire::Aim(&r, &f), "a sword: presses %d, takes %d", Presses(), g_takes);
	}
	{   // the game's menu open
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_menu = true;
		Rig rig; Swing sw = Fresh(); Feed(rig, sw);
		EXPECT(Presses() == 0 && g_takes == 0, "the game's menu: presses %d, takes %d", Presses(), g_takes);
	}
	for (uint32_t flags : {(uint32_t)(directinput::kFocused | directinput::kInMenu), (uint32_t)0}) {   // the headset's menu flag; no focus
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig; rig.flags = flags;
		Swing sw = Fresh();
		Feed(rig, sw);
		EXPECT(Presses() == 0 && g_takes == 0, "flags %u: presses %d, takes %d", flags, Presses(), g_takes);
	}
	{   // the camera cannot aim
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_canAim = false;
		Rig rig; Swing sw = Fresh(); Feed(rig, sw);
		EXPECT(Presses() == 0 && !throwwire::Aim(&r, &f), "no camera frame: presses %d", Presses());
	}
	{   // no head pose
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig; rig.headValid = false; Swing sw = Fresh(); Feed(rig, sw);
		EXPECT(Presses() == 0, "no head pose: presses %d", Presses());
	}
	{   // the trigger never let go
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig; Swing sw = Fresh(); sw.release = false; Feed(rig, sw);
		EXPECT(Presses() == 0, "trigger held: presses %d", Presses());
	}
	{   // a hand at rest
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig; Swing sw = Fresh(); sw.speed = 0.0f; Feed(rig, sw);
		EXPECT(Presses() == 0, "hand at rest: presses %d", Presses());
	}
}

void TestCancels() {
	std::printf("a stroke dropped half way\n");
	struct Drop { const char* what; void (*change)(Rig&); };
	const Drop drops[] = {
		{"the epoch changes (focus lost and back, a menu opened and closed)", [](Rig& r) { ++r.epoch; }},
		{"a new session", [](Rig& r) { ++r.session; }},
		{"the weapon swapped for a sword", [](Rig&) { g_weapon = D2RVR_TYPE_SWORD; }},
		{"the weapon swapped for another throwing weapon", [](Rig&) { g_weapon = D2RVR_TYPE_THROWING; }},
		{"the game opens a menu", [](Rig&) { g_menu = true; }},
	};
	for (const Drop& d : drops) {
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig;
		Swing sw = Fresh();
		Feed(rig, sw, sw.t0 + 0.3 + 0.08);   // half way through the swing
		d.change(rig);
		Feed(rig, sw);
		EXPECT(Presses() == 0, "%s: presses %d", d.what, Presses());
	}
}

void TestBoxUnticked() {
	std::printf("the box unticked\n");
	Reset();
	g_weapon = D2RVR_TYPE_JAVELIN;
	throwwire::SetModes(false, false);
	float r, f;
	{ Rig rig; Swing sw = Fresh(); Feed(rig, sw); }
	EXPECT(Presses() == 0 && g_takes == 0 && !throwwire::Aim(&r, &f), "unticked: presses %d, takes %d", Presses(), g_takes);
	throwwire::SetModes(true, false);
	Reset();
	g_weapon = D2RVR_TYPE_JAVELIN;
	{ Rig rig; Swing sw = Fresh(); Feed(rig, sw, AfterRelease(sw)); }
	EXPECT(Presses() == 1, "ticked again: presses %d", Presses());
}

void TestLiveWindow() {
	std::printf("the heading is only good for the ticks the game takes to read the press\n");
	Reset();
	g_weapon = D2RVR_TYPE_JAVELIN;
	Rig rig;
	Swing sw = Fresh();
	float r, f;
	Feed(rig, sw, AfterRelease(sw));
	EXPECT(throwwire::Aim(&r, &f), "no aim right after the release");
	Sleep(700);
	EXPECT(!throwwire::Aim(&r, &f), "the aim is still there 0.7 s later");
}

void TestTwoThrows() {
	std::printf("two throws, two presses\n");
	Reset();
	g_weapon = D2RVR_TYPE_JAVELIN;
	Rig rig;
	Swing a = Fresh();
	Feed(rig, a);
	Swing b = Fresh();
	b.dx = 1.0f; b.dz = 0.0f;
	Feed(rig, b);
	EXPECT(Presses() == 2 && Releases() == 2, "presses %d releases %d", Presses(), Releases());
}

bool g_f4 = true;
bool TheView() { return g_f4; }

void TestOtherView() {
	std::printf("F4 only: another view throws nothing, takes nothing, and a view change lets go\n");
	viewgate::Set(&TheView);
	float r, f;
	{   // another view all through, the frames carry the flag (what direct input gives)
		Reset();
		g_f4 = false;
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig; rig.flags = directinput::kFocused | directinput::kInMenu | directinput::kOtherView;
		Swing sw = Fresh(); Feed(rig, sw);
		EXPECT(Presses() == 0 && g_takes == 0 && !throwwire::Aim(&r, &f), "another view (flag): presses %d, takes %d", Presses(), g_takes);
	}
	{   // ... the view test alone, frames without the flag
		Reset();
		g_f4 = false;
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig; Swing sw = Fresh(); Feed(rig, sw);
		EXPECT(Presses() == 0 && g_takes == 0 && !throwwire::Aim(&r, &f), "another view (test): presses %d, takes %d", Presses(), g_takes);
	}
	{   // the view changes half way through the swing: nothing is thrown, the trigger is the layout's again
		Reset();
		g_f4 = true;
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig; Swing sw = Fresh();
		Feed(rig, sw, sw.t0 + 0.3 + 0.08);
		EXPECT(g_takes > 0, "F4: the trigger is taken");
		g_f4 = false;
		rig.flags = directinput::kFocused | directinput::kInMenu | directinput::kOtherView;
		++rig.epoch;
		g_takes = 0;
		Feed(rig, sw);
		EXPECT(Presses() == 0 && g_takes == 0, "F4 -> another view mid-swing: presses %d, takes %d", Presses(), g_takes);
	}
	{   // the view changes just after the release: the press is let go on time and the aim is gone at once
		Reset();
		g_f4 = true;
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig; Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && throwwire::Aim(&r, &f), "F4: thrown, presses %d", Presses());
		g_f4 = false;
		EXPECT(!throwwire::Aim(&r, &f), "the view changed: the throw's aim is gone");
		rig.flags = directinput::kFocused | directinput::kInMenu | directinput::kOtherView;
		++rig.epoch;
		Feed(rig, sw, AfterRelease(sw) + 0.2);
		EXPECT(Presses() == 1 && Releases() == 1, "the attack press is let go: presses %d releases %d", Presses(), Releases());
		g_f4 = true;   // back: the aim does not come back with the view
		EXPECT(!throwwire::Aim(&r, &f), "back in F4: the old throw's aim does not return");
	}
	{   // back in F4: a fresh throw works as before
		Reset();
		g_f4 = true;
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig rig; Swing sw = Fresh();
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && throwwire::Aim(&r, &f), "back in F4: presses %d", Presses());
	}
	g_f4 = true;
	viewgate::Set(nullptr);
}

// -- which command a throw presses: the game's Throw skill by its pad button, else the A attack ------------------------------------------

void PutThrowOn(int slot) {
	g_snap.ready = true;
	g_snap.slot[slot].id = 2;
	g_snap.slot[slot].available = true;
	g_snap.slot[slot].known = true;
	++g_snap.revision;
}
int Pulses(uint16_t button) { int n = 0; for (const PulseCall& p : g_pulses) if (p.buttons == button) ++n; return n; }

void TestRoute() {
	std::printf("the command a throw presses\n");
	Rig rig;
	// Throw on the X button: a knife is thrown by pressing X, no A attack
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		PutThrowOn(1);
		Swing sw = Fresh();
		Feed(rig, sw);
		EXPECT(Presses() == 0, "an A attack was pressed with Throw on X: %d", Presses());
		EXPECT(Pulses(wheel::kPadX) >= 5 && Pulses(wheel::kPadX) <= 14, "X was pulsed %d times", Pulses(wheel::kPadX));
		EXPECT(Pulses(wheel::kPadB) == 0 && Pulses(wheel::kPadY) == 0, "other buttons pulsed");
		bool ltEver = false;
		for (const PulseCall& p : g_pulses) ltEver = ltEver || p.lt != 0;
		EXPECT(!ltEver, "the left trigger was used for a first-row slot");
		double first = 1e9, last = 0;
		for (const PulseCall& p : g_pulses) { first = std::fmin(first, p.at); last = std::fmax(last, p.at); }
		EXPECT(last - first >= 0.09 && last - first <= 0.14, "the press ran %.3f s", last - first);
	}
	// a potion with Throw on Y: the same
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		g_potion = true;
		PutThrowOn(2);
		Swing sw = Fresh();
		Feed(rig, sw);
		EXPECT(Presses() == 0 && Pulses(wheel::kPadY) >= 5, "potion: presses %d, Y pulses %d", Presses(), Pulses(wheel::kPadY));
	}
	// the second row: the left trigger alone first, then the button
	{
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		PutThrowOn(5);   // LT + X
		Swing sw = Fresh();
		Feed(rig, sw);
		int ltOnly = 0, both = 0, buttonWithoutLt = 0;
		for (const PulseCall& p : g_pulses) {
			if (p.lt && !p.buttons) ++ltOnly;
			else if (p.lt && p.buttons == wheel::kPadX) ++both;
			else if (!p.lt && p.buttons) ++buttonWithoutLt;
		}
		EXPECT(ltOnly >= 6 && both >= 3 && buttonWithoutLt == 0, "LT alone %d, LT + X %d, X alone %d", ltOnly, both, buttonWithoutLt);
		EXPECT(!g_pulses.empty() && g_pulses.front().lt && !g_pulses.front().buttons, "the first pulse is the left trigger alone");
	}
	// no Throw on any slot: a knife, axe or javelin goes by the A attack as before
	{
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Swing sw = Fresh();
		Feed(rig, sw);
		EXPECT(Presses() == 1 && g_pulses.empty(), "no slot, a javelin: presses %d, pulses %zu", Presses(), g_pulses.size());
	}
	// ... but a potion is not pressed at all (the game's Attack refuses it): nothing, and no aim is wasted
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		g_potion = true;
		Swing sw = Fresh();
		Feed(rig, sw);
		EXPECT(Presses() == 0 && g_pulses.empty(), "no slot, a potion: presses %d, pulses %zu", Presses(), g_pulses.size());
	}
	// a skill table that was never read, or Throw on a slot the player does not have now: as no slot
	{
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_snapReady = false;
		Swing sw = Fresh();
		Feed(rig, sw);
		EXPECT(Presses() == 1 && g_pulses.empty(), "no table: presses %d", Presses());
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		PutThrowOn(0);
		g_snap.slot[0].available = false;
		Swing s2 = Fresh();
		Feed(rig, s2);
		EXPECT(Presses() == 1 && g_pulses.empty(), "an unavailable Throw: presses %d", Presses());
	}
	// the first of B X Y RB wins
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		PutThrowOn(3);
		PutThrowOn(1);
		Swing sw = Fresh();
		Feed(rig, sw);
		EXPECT(Pulses(wheel::kPadX) >= 5 && Pulses(wheel::kPadRb) == 0, "X %d RB %d", Pulses(wheel::kPadX), Pulses(wheel::kPadRb));
	}
	// the swap hook on: the A attack is pressed whatever the slots hold, a potion included (the game reads it as Throw); no pad button
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		g_potion = true;
		g_auto = true;
		Swing sw = Fresh();
		Feed(rig, sw);
		EXPECT(Presses() == 1 && g_pulses.empty(), "auto, a potion, no slot: presses %d, pulses %zu", Presses(), g_pulses.size());
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_auto = true;
		PutThrowOn(1);
		Swing s2 = Fresh();
		Feed(rig, s2);
		EXPECT(Presses() == 1 && g_pulses.empty(), "auto, a javelin, Throw on X: presses %d, pulses %zu", Presses(), g_pulses.size());
		// switched off again: the slot route
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		PutThrowOn(1);
		Swing s3 = Fresh();
		Feed(rig, s3);
		EXPECT(Presses() == 0 && Pulses(wheel::kPadX) >= 5, "not auto, Throw on X: presses %d, X pulses %d", Presses(), Pulses(wheel::kPadX));
		// the grip mode too
		g_byGrip = true;
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		g_potion = true;
		g_auto = true;
		Rig grip; grip.grip = true; grip.flags = directinput::kFocused | directinput::kDirect;
		Swing s4 = Fresh(); s4.idle = 2.0;
		Feed(grip, s4);
		EXPECT(Presses() == 1 && g_pulses.empty(), "auto, grip mode, a potion: presses %d", Presses());
		g_byGrip = false;
		Reset();
	}
	// the grip mode presses the same
	{
		g_byGrip = true;
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		PutThrowOn(0);
		Rig grip; grip.grip = true; grip.flags = directinput::kFocused | directinput::kDirect;
		Swing sw = Fresh(); sw.idle = 2.0;
		Feed(grip, sw);
		EXPECT(Presses() == 0 && Pulses(wheel::kPadB) >= 5, "grip mode: presses %d, B pulses %d", Presses(), Pulses(wheel::kPadB));
		g_byGrip = false;
		Reset();
	}
	// the controller's skill slots are wanted exactly while a throw could come
	{
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		Rig r2;
		Swing sw = Fresh();
		sw.release = false;
		Feed(r2, sw, sw.t0 + 0.3);
		EXPECT(throwwire::WantsSlots(), "wanted with a thrown weapon in play");
		g_weapon = D2RVR_TYPE_SWORD;
		Feed(r2, sw, sw.t0 + 0.4);
		EXPECT(!throwwire::WantsSlots(), "not wanted with a sword");
		g_weapon = D2RVR_TYPE_THROWING;
		g_menu = true;
		Feed(r2, sw, sw.t0 + 0.5);
		EXPECT(!throwwire::WantsSlots(), "not wanted in a menu");
		g_menu = false;
	}
}

// -- the grip mode ([hands] throw_by_grip) --------------------------------------------------------------------------------

void GripMode() { g_byGrip = true; Reset(); }

void TestGripThrow() {
	std::printf("grip mode: held for seconds, a swing, let go at its end\n");
	GripMode();
	g_weapon = D2RVR_TYPE_THROWING;
	Rig rig; rig.grip = true; rig.flags = directinput::kFocused | directinput::kDirect;
	float r = 9, f = 9;
	const uint64_t before = throwwire::LastThrowTickMs();
	Swing sw = Fresh();
	sw.idle = 8.0;   // the grip held 8 s before the swing
	Feed(rig, sw, AfterRelease(sw));
	EXPECT(Presses() == 1, "presses %d", Presses());
	EXPECT(!g_actions.empty() && g_actions[0].bits == D2RVR_ACT_ATTACK && g_actions[0].down && g_actions[0].src == directinput::kSrcJavelin,
	       "the first call is not the javelin's attack press");
	EXPECT(throwwire::Aim(&r, &f) && std::fabs(r) < 0.02f && std::fabs(f - 1.0f) < 0.02f, "heading right %.3f forward %.3f", r, f);
	EXPECT(g_gripTakes > 0 && g_takes == 0, "the grip is taken (%d), the trigger is not (%d)", g_gripTakes, g_takes);
	EXPECT(throwwire::LastThrowTickMs() != 0 && throwwire::LastThrowTickMs() >= before, "the throw's tick was not noted");
	Feed(rig, sw, AfterRelease(sw) + 0.2);
	EXPECT(Presses() == 1 && Releases() == 1, "presses %d releases %d", Presses(), Releases());
	// the trigger is the layout's own in this mode: a trigger swing does nothing
	Reset();
	g_weapon = D2RVR_TYPE_THROWING;
	{ Rig tr; tr.flags = rig.flags; Swing s2 = Fresh(); Feed(tr, s2); }
	EXPECT(Presses() == 0 && g_takes == 0, "a trigger swing in grip mode: presses %d, takes %d", Presses(), g_takes);
}

void TestGripOwns() {
	std::printf("grip mode: who owns the grip\n");
	Rig rig; rig.grip = true; rig.flags = directinput::kFocused | directinput::kDirect;
	auto owns = [&](uint32_t weapon, const Rig& rg, bool menu) {
		GripMode();
		g_weapon = weapon;
		g_menu = menu;
		Swing sw = Fresh();
		sw.release = false;
		Feed(rg, sw, sw.t0 + 0.4);   // the grip squeezed, the weapon in hand
		return throwwire::GripIsThrows();
	};
	EXPECT(owns(D2RVR_TYPE_JAVELIN, rig, false), "a javelin: the grip is the throw's");
	EXPECT(owns(D2RVR_TYPE_THROWING, rig, false), "a throwing weapon: the grip is the throw's");
	EXPECT(!owns(D2RVR_TYPE_SWORD, rig, false), "a sword: the grip is free");
	EXPECT(!owns(D2RVR_TYPE_JAVELIN, rig, true), "a menu open: the grip is free");
	Rig noFocus = rig; noFocus.flags = directinput::kDirect;
	EXPECT(!owns(D2RVR_TYPE_JAVELIN, noFocus, false), "no input focus: the grip is free");
	Rig inMenu = rig; inMenu.flags |= directinput::kInMenu;
	EXPECT(!owns(D2RVR_TYPE_JAVELIN, inMenu, false), "the headset's menu flag: the grip is free");
	Rig otherView = rig; otherView.flags |= directinput::kOtherView;
	EXPECT(!owns(D2RVR_TYPE_JAVELIN, otherView, false), "another view than F4: the grip is free");
	Rig notDirect = rig; notDirect.flags = directinput::kFocused;
	EXPECT(!owns(D2RVR_TYPE_JAVELIN, notDirect, false), "direct input not feeding the game: the grip is free, nothing taken (%d)", g_gripTakes);
	EXPECT(g_gripTakes == 0, "taken without direct input");
	// the trigger mode never owns the grip
	g_byGrip = false; Reset();
	g_weapon = D2RVR_TYPE_JAVELIN;
	{ Rig tr; Swing sw = Fresh(); sw.release = false; Feed(tr, sw, sw.t0 + 0.4); }
	EXPECT(!throwwire::GripIsThrows(), "trigger mode: the grip is free");
}

// Hit by touch with a thrown weapon asks "is the weapon in the hand?" (throwwire::GripHeld): the right grip squeezed, as the gesture reads the controller. The
// shared block's grip cannot say it - the gesture Takes the grip, and a taken control reads as let go there - so with a javelin in hand and throw_by_grip on, the
// touch that asked the shared block (0 while taken) never started (2026-10-14, the log had not one melee touch line for a javelin).
void TestGripHeld() {
	std::printf("grip mode: the grip is held on the weapon (for hit by touch)\n");
	Rig rig; rig.grip = true; rig.flags = directinput::kFocused | directinput::kDirect;
	{   // a javelin, the grip squeezed (the swing not released): owned AND held, though the layout is told the grip is let go (g_gripTakes)
		GripMode();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Swing sw = Fresh(); sw.release = false;
		Feed(rig, sw, sw.t0 + 0.4);
		EXPECT(throwwire::GripIsThrows() && throwwire::GripHeld(), "a squeezed grip on a javelin: owns %d held %d", throwwire::GripIsThrows() ? 1 : 0, throwwire::GripHeld() ? 1 : 0);
		EXPECT(g_gripTakes > 0, "the grip is taken from the layout all the while");
		// let go: still the gesture's, no longer held
		Rig open = rig;
		Swing none; none.t0 = 1e9;
		Feed(open, none, g_t + 0.3);
		EXPECT(throwwire::GripIsThrows() && !throwwire::GripHeld(), "an open grip: owns %d held %d", throwwire::GripIsThrows() ? 1 : 0, throwwire::GripHeld() ? 1 : 0);
	}
	{   // a throwing weapon too; a sword never
		GripMode();
		g_weapon = D2RVR_TYPE_THROWING;
		Swing sw = Fresh(); sw.release = false;
		Feed(rig, sw, sw.t0 + 0.4);
		EXPECT(throwwire::GripHeld(), "a squeezed grip on a throwing weapon is held");
		GripMode();
		g_weapon = D2RVR_TYPE_SWORD;
		Swing s2 = Fresh(); s2.release = false;
		Feed(rig, s2, s2.t0 + 0.4);
		EXPECT(!throwwire::GripIsThrows() && !throwwire::GripHeld(), "a sword: the grip is not the gesture's, nothing is held");
	}
	{   // the trigger's mode and a menu: not held
		g_byGrip = false; Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig tr; Swing sw = Fresh(); sw.release = false;
		Feed(tr, sw, sw.t0 + 0.4);
		EXPECT(!throwwire::GripHeld(), "trigger mode: the grip is not the gesture's");
		GripMode();
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_menu = true;
		Swing s2 = Fresh(); s2.release = false;
		Feed(rig, s2, s2.t0 + 0.4);
		EXPECT(!throwwire::GripHeld(), "a menu open: nothing is held");
		g_menu = false;
		throwwire::SetModes(false, false);
		EXPECT(!throwwire::GripHeld(), "off: nothing is held");
	}
}

void TestGripNoThrow() {
	std::printf("grip mode: what must not throw\n");
	Rig rig; rig.grip = true; rig.flags = directinput::kFocused | directinput::kDirect;
	{   // the grip let go at rest after a long hold: the weapon is put down, nothing is thrown
		GripMode();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Swing sw = Fresh(); sw.idle = 12.0; sw.speed = 0.0f;
		Feed(rig, sw);
		EXPECT(Presses() == 0, "let go at rest: presses %d", Presses());
	}
	{   // the grip still squeezed at the end of the swing: no throw yet
		GripMode();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Swing sw = Fresh(); sw.idle = 3.0; sw.release = false;
		Feed(rig, sw);
		EXPECT(Presses() == 0, "grip held: presses %d", Presses());
	}
	{   // a sword
		GripMode();
		g_weapon = D2RVR_TYPE_SWORD;
		Swing sw = Fresh(); sw.idle = 2.0;
		Feed(rig, sw);
		EXPECT(Presses() == 0 && g_gripTakes == 0, "a sword: presses %d, takes %d", Presses(), g_gripTakes);
	}
	{   // direct input not feeding the game
		GripMode();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig nd = rig; nd.flags = directinput::kFocused;
		Swing sw = Fresh(); sw.idle = 2.0;
		Feed(nd, sw);
		EXPECT(Presses() == 0, "no direct input: presses %d", Presses());
	}
	{   // the menu opens while the weapon is held and closes; letting go is no throw
		GripMode();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Swing sw = Fresh(); sw.idle = 2.0;
		Feed(rig, sw, sw.t0 + 2.0 + 0.3 + 0.08);
		g_menu = true;
		Feed(rig, sw, sw.t0 + 2.0 + 0.3 + 0.12);
		g_menu = false;
		Feed(rig, sw);
		EXPECT(Presses() == 0, "a menu mid-swing: presses %d", Presses());
	}
	{   // not F4
		GripMode();
		g_weapon = D2RVR_TYPE_JAVELIN;
		Rig other = rig; other.flags |= directinput::kOtherView;
		Swing sw = Fresh(); sw.idle = 2.0;
		Feed(other, sw);
		EXPECT(Presses() == 0 && g_gripTakes == 0, "another view: presses %d, takes %d", Presses(), g_gripTakes);
	}
}

void TestGripModes() {
	std::printf("both boxes: the grip wins; one box off\n");
	Rig grip; grip.grip = true; grip.flags = directinput::kFocused | directinput::kDirect;
	g_byGrip = true;
	Reset();
	throwwire::SetModes(true, true);   // both boxes ticked
	g_weapon = D2RVR_TYPE_JAVELIN;
	{ Swing sw = Fresh(); sw.idle = 2.0; Feed(grip, sw); }
	EXPECT(Presses() == 1 && g_gripTakes > 0 && g_takes == 0, "both ticked, a grip swing: presses %d, grip takes %d, trigger takes %d", Presses(), g_gripTakes, g_takes);
	g_actions.clear();
	{ Rig tr; tr.flags = grip.flags; Swing sw = Fresh(); Feed(tr, sw); }
	EXPECT(Presses() == 0, "both ticked, a trigger swing: presses %d", Presses());
	// the trigger box only, back
	g_byGrip = false;
	Reset();
	g_weapon = D2RVR_TYPE_JAVELIN;
	{ Rig tr; tr.flags = grip.flags; Swing sw = Fresh(); Feed(tr, sw); }
	EXPECT(Presses() == 1 && g_takes > 0 && g_gripTakes == 0, "trigger only: presses %d, trigger takes %d, grip takes %d", Presses(), g_takes, g_gripTakes);
	// everything off lets go of the grip
	throwwire::SetModes(false, false);
	EXPECT(!throwwire::GripIsThrows(), "off: the grip is free");
}

// Hit by touch with a thrown weapon (melee_touch_thrown): a touch that pressed the attack drops the release of the same swing - one throw per swing -
// and the gesture is exactly as it was when none did.
void TestHoldOff() {
	std::printf("a touch's hold-off: one throw per swing\n");
	Rig rig; rig.grip = true; rig.flags = directinput::kFocused | directinput::kDirect;
	{   // held off: the release of the swing presses nothing and aims nothing, but is still a throw's release to the weight logic
		GripMode();
		g_weapon = D2RVR_TYPE_JAVELIN;
		float r = 9, f = 9;
		const uint64_t before = throwwire::LastThrowTickMs();
		throwwire::HoldOffRelease(GetTickCount64() + 60000);
		Swing sw = Fresh(); sw.idle = 1.0;
		Feed(rig, sw, AfterRelease(sw) + 0.2);
		EXPECT(Presses() == 0, "held off: presses %d", Presses());
		EXPECT(!throwwire::Aim(&r, &f), "held off: the aim is live for a throw that was dropped");
		EXPECT(throwwire::LastThrowTickMs() != 0 && throwwire::LastThrowTickMs() >= before && GetTickCount64() - throwwire::LastThrowTickMs() < 5000,
		       "held off: the release is still noted as a throw (the weapon leaves the hand, the weight logic does not put it down)");
		EXPECT(g_gripTakes > 0, "held off: the grip is still the gesture's");
	}
	{   // the hold-off over: the gesture throws as ever
		GripMode();
		g_weapon = D2RVR_TYPE_JAVELIN;
		float r = 9, f = 9;
		throwwire::HoldOffRelease(GetTickCount64() > 1000 ? GetTickCount64() - 1000 : 0);   // a hold-off in the past
		Swing sw = Fresh(); sw.idle = 1.0;
		Feed(rig, sw, AfterRelease(sw));
		EXPECT(Presses() == 1, "hold-off over: presses %d", Presses());
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(f - 1.0f) < 0.02f, "hold-off over: the throw is aimed along the hand");
	}
	{   // the trigger's mode too
		g_byGrip = false;
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		Rig tr;
		throwwire::HoldOffRelease(GetTickCount64() + 60000);
		Swing sw = Fresh();
		Feed(tr, sw, AfterRelease(sw) + 0.2);
		EXPECT(Presses() == 0, "held off, the trigger's mode: presses %d", Presses());
		throwwire::HoldOffRelease(0);
		Reset();
		g_weapon = D2RVR_TYPE_THROWING;
		Swing s2 = Fresh();
		Feed(tr, s2, AfterRelease(s2));
		EXPECT(Presses() == 1, "hold-off cleared, the trigger's mode: presses %d", Presses());
	}
	throwwire::HoldOffRelease(0);
}

}  // namespace

// -- [hands] throw_left_hand: the controller of the hand that holds the weapon throws (vr/throw_hand.h) -----------------

void TestLeftHand() {
	std::printf("throw_left_hand: a thrown weapon in the left hand\n");
	Rig right;
	Rig left; left.xr = 0;
	{   // unticked: the right controller throws, whatever slot the weapon is in (as ever); the left one's swing is nothing
		g_byGrip = false; Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_weaponHand = 1;
		Swing sw = Fresh();
		Feed(right, sw, AfterRelease(sw) + 0.2);
		EXPECT(Presses() == 1 && g_takes > 0 && g_leftTakes == 0, "off, weapon left, right swing: presses %d takes %d / %d", Presses(), g_takes, g_leftTakes);
		EXPECT(throwwire::Controller() == 1, "off: the right controller (%d)", throwwire::Controller());
		Reset();
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_weaponHand = 1;
		Swing s2 = Fresh();
		Feed(left, s2);
		EXPECT(Presses() == 0 && g_leftTakes == 0, "off, weapon left, left swing: presses %d left takes %d", Presses(), g_leftTakes);
	}
	{   // ticked, the weapon in the left hand: the left trigger and swing throw - the game's same attack press, the heading the same
		g_byGrip = false; Reset();
		throwwire::SetFollowHand(true);
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_weaponHand = 1;
		float r = 9, f = 9;
		Swing sw = Fresh();
		sw.release = false;
		Feed(left, sw, sw.t0 + 0.1);
		EXPECT(throwwire::TriggerIsThrowsAt(0) && !throwwire::TriggerIsThrowsAt(1) && throwwire::TriggerIsThrows(), "the left trigger is the throw's");
		EXPECT(throwwire::Controller() == 0, "on, weapon left: the left controller (%d)", throwwire::Controller());
		sw.release = true;
		Feed(left, sw, AfterRelease(sw));
		EXPECT(Presses() == 1, "on, weapon left, left swing: presses %d", Presses());
		EXPECT(!g_actions.empty() && g_actions[0].bits == D2RVR_ACT_ATTACK && g_actions[0].src == directinput::kSrcJavelin, "the game's own attack press");
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r) < 0.02f && std::fabs(f - 1.0f) < 0.02f, "heading right %.3f forward %.3f", r, f);
		EXPECT(g_leftTakes > 0 && g_takes == 0, "the left trigger is taken (%d), the right is not (%d)", g_leftTakes, g_takes);
	}
	{   // ticked, the weapon in the left hand: the right controller's swing throws nothing
		g_byGrip = false; Reset();
		throwwire::SetFollowHand(true);
		g_weapon = D2RVR_TYPE_THROWING;
		g_weaponHand = 1;
		Swing sw = Fresh();
		Feed(right, sw);
		EXPECT(Presses() == 0 && g_takes == 0, "on, weapon left, right swing: presses %d right takes %d", Presses(), g_takes);
	}
	{   // ticked, the weapon in the right hand: the right controller, as ever
		g_byGrip = false; Reset();
		throwwire::SetFollowHand(true);
		g_weapon = D2RVR_TYPE_THROWING;
		g_weaponHand = 0;
		Swing sw = Fresh();
		Feed(right, sw, AfterRelease(sw));
		EXPECT(Presses() == 1 && g_takes > 0 && g_leftTakes == 0, "on, weapon right, right swing: presses %d takes %d / %d", Presses(), g_takes, g_leftTakes);
		EXPECT(throwwire::Controller() == 1, "on, weapon right: the right controller (%d)", throwwire::Controller());
	}
	{   // the weapon goes to the other hand in the middle of a swing: dropped, nothing thrown
		g_byGrip = false; Reset();
		throwwire::SetFollowHand(true);
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_weaponHand = 1;
		Rig twin = left; twin.both = true;   // (both controllers swing alike: only the change of hand can drop it)
		Swing sw = Fresh();
		Feed(twin, sw, sw.t0 + 0.35);
		g_weaponHand = 0;
		Feed(twin, sw);
		EXPECT(Presses() == 0, "the hand changed mid-swing: presses %d", Presses());
	}
	{   // the grip mode: the left grip held as long as you like, the swing and its release throw; only the left grip is the throw's
		GripMode();
		throwwire::SetFollowHand(true);
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_weaponHand = 1;
		Rig lg = left; lg.grip = true; lg.flags = directinput::kFocused | directinput::kDirect;
		Swing sw = Fresh();
		sw.idle = 3.0;
		sw.release = false;
		Feed(lg, sw, sw.t0 + 1.0);
		EXPECT(throwwire::GripIsThrowsAt(0) && !throwwire::GripIsThrowsAt(1) && throwwire::GripHeld(), "the left grip is the throw's and held");
		EXPECT(g_leftGripTakes > 0 && g_gripTakes == 0, "the left grip is taken (%d), the right is not (%d)", g_leftGripTakes, g_gripTakes);
		sw.release = true;
		Feed(lg, sw, AfterRelease(sw));
		EXPECT(Presses() == 1, "grip mode, weapon left, left swing: presses %d", Presses());
		// unticked again: the right grip is the throw's (as ever), the left one free
		throwwire::SetFollowHand(false);
		Rig rg = right; rg.grip = true; rg.flags = lg.flags;
		Swing s2 = Fresh(); s2.release = false;
		Feed(rg, s2, s2.t0 + 0.4);
		EXPECT(throwwire::GripIsThrowsAt(1) && !throwwire::GripIsThrowsAt(0), "off: the right grip is the throw's");
		g_byGrip = false; Reset();
	}
}

// throw_left_hand with the weight model: the hand that CARRIES the weapon in VR throws - a javelin in the game's right slot handed over to the left
// (the headset test of 2026-10-22: the left grip took it, the right let go; the throw still listened to the right grip, and hit by touch asked it)
void TestCarriedLeft() {
	std::printf("throw_left_hand: the weapon handed over to the left hand (the weight model)\n");
	auto source = [] {
		throwwire::CarrySource cs;
		cs.placed = [] { return g_placed; };
		cs.carrying = [] { return g_carrying; };
		cs.firstSlot = [] { return g_firstSlot; };
		cs.holdersOf = [](int slot) { return HoldersOf(slot); };
		throwwire::SetCarrySource(cs);
		g_placed = true;
	};
	Rig rg; rg.grip = true; rg.flags = directinput::kFocused | directinput::kDirect;
	Rig lg = rg; lg.xr = 0;
	{   // the hand-over: the right grip holds it, the left grip takes it, the right lets go - the left grip is the weapon in the hand at once,
		// its swing and release throw (no fresh squeeze), and the press is the game's own
		GripMode(); source();
		throwwire::SetFollowHand(true);
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_weaponHand = 0;   // the game's right slot, all along
		g_carrying = 0; g_holds[0] = true;
		Swing hold = Fresh(); hold.release = false;
		Feed(rg, hold, hold.t0 + 0.5);
		EXPECT(throwwire::Controller() == 1 && throwwire::GripIsThrowsAt(1), "carried by the right: the right grip");
		g_holds[1] = true;   // the left takes it ...
		g_holds[0] = false; g_carrying = 1;   // ... the right lets go: now in the left hand
		Swing sw; sw.t0 = g_t - 0.5; sw.idle = 1.0;   // (the left grip squeezed since before)
		Feed(lg, sw, g_t + 0.3);
		EXPECT(throwwire::Controller() == 0 && throwwire::GripIsThrowsAt(0) && !throwwire::GripIsThrowsAt(1), "carried by the left: the left grip is the throw's");
		EXPECT(throwwire::GripHeld(), "the left grip squeezed on it: held (hit by touch asks this)");
		EXPECT(g_leftGripTakes > 0, "the left grip is taken from the layout");
		EXPECT(Presses() == 0, "nothing thrown by the hand-over itself: %d", Presses());
		float r = 9, f = 9;
		Feed(lg, sw, AfterRelease(sw));
		EXPECT(Presses() == 1, "the left swing and release throw: presses %d", Presses());
		EXPECT(!g_actions.empty() && g_actions[0].bits == D2RVR_ACT_ATTACK && g_actions[0].src == directinput::kSrcJavelin, "the game's own attack press");
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r) < 0.02f && std::fabs(f - 1.0f) < 0.02f, "along the left hand: right %.3f forward %.3f", r, f);
		// let go: nobody carries it - the left stays the throw's hand (the weight model's release is read on its grip)
		g_carrying = -1; g_holds[1] = false;
		{ Swing none; none.t0 = 1e9; Feed(lg, none, g_t + 0.1); }
		EXPECT(throwwire::Controller() == 0, "after the release: still the left (%d)", throwwire::Controller());
	}
	{   // the right grip let go in a swing while the LEFT hand holds it: a hand-over, not a throw
		GripMode(); source();
		throwwire::SetFollowHand(true);
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_carrying = 0; g_holds[0] = true; g_holds[1] = true;
		Swing sw = Fresh(); sw.idle = 1.0;
		Feed(rg, sw);
		EXPECT(Presses() == 0, "the other hand holds it: presses %d", Presses());
		// unticked, the same swing: thrown, as ever
		GripMode(); source();
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_carrying = 0; g_holds[0] = true; g_holds[1] = true;
		Swing s2 = Fresh(); s2.idle = 1.0;
		Feed(rg, s2);
		EXPECT(Presses() == 1, "off: thrown as ever (%d)", Presses());
	}
	{   // unticked: carried by the left, the right grip is still the throw's (as ever)
		GripMode(); source();
		g_weapon = D2RVR_TYPE_JAVELIN;
		g_carrying = 1; g_holds[1] = true;
		Swing sw = Fresh(); sw.release = false;
		Feed(lg, sw, sw.t0 + 0.4);
		EXPECT(throwwire::Controller() == 1 && throwwire::GripIsThrowsAt(1) && g_leftGripTakes == 0, "off: the right controller");
	}
	g_byGrip = false; Reset();
}

// throw_left_hand, a weapon in each hand (2026-10-28, a Barbarian): a javelin beside an axe threw nothing (the axe in the other hand read as "the other hand
// holds the weapon" - a hand-over), and of two javelins the left one could not be thrown at all (only the right controller listened)
void TestEachHand() {
	std::printf("throw_left_hand: a weapon in each hand\n");
	Rig rg; rg.grip = true; rg.flags = directinput::kFocused | directinput::kDirect;
	Rig lg = rg; lg.xr = 0;
	auto setup = [](uint32_t right, uint32_t left) {
		GripMode();
		throwwire::SetFollowHand(true);
		throwwire::CarrySource cs;
		cs.placed = [] { return g_placed; };
		cs.carrying = [] { return g_carrying; };
		cs.secondHand = [] { return g_secondHand; };
		cs.secondPlaced = [] { return g_secondPlaced; };
		cs.firstSlot = [] { return g_firstSlot; };
		cs.holdersOf = [](int slot) { return HoldersOf(slot); };
		cs.secondThrown = [] { ++g_secondThrown; };
		throwwire::SetCarrySource(cs);
		g_placed = true; g_firstSlot = 0; g_secondPlaced = true;
		g_weaponHand = 0;
		g_weapon = right; g_otherKind = left;   // (the right slot's kind is the game's weapon kind)
		g_carrying = 0; g_holds[0] = true;       // the first weapon (the right slot's) in the right hand
		g_secondHand = 1; g_holders2 = 2u;       // the second (the left slot's) in the left hand
	};
	{   // a javelin in the right hand, an axe in the left: the right swing throws - the axe in the left hand is no hand-over
		setup(D2RVR_TYPE_JAVELIN, D2RVR_TYPE_AXE);
		Swing sw = Fresh(); sw.idle = 1.0;
		Feed(rg, sw, AfterRelease(sw));
		EXPECT(Presses() == 1, "javelin right, axe left: presses %d", Presses());
		EXPECT(g_gripTakes > 0 && g_leftGripTakes == 0, "the right grip is the throw's (%d), the axe's left grip is not (%d)", g_gripTakes, g_leftGripTakes);
		EXPECT(throwwire::GripIsThrowsAt(1) && !throwwire::GripIsThrowsAt(0), "only the right grip is owned");
		EXPECT(g_secondThrown == 0, "the first weapon thrown: the second stays as it is");
	}
	{   // ... and the same javelin handed to the left hand while it holds nothing else is still a hand-over, not a throw
		setup(D2RVR_TYPE_JAVELIN, D2RVR_TYPE_AXE);
		g_holds[1] = true;   // the left hand on the javelin too
		Swing sw = Fresh(); sw.idle = 1.0;
		Feed(rg, sw);
		EXPECT(Presses() == 0, "the left hand holds the javelin too: presses %d", Presses());
	}
	{   // two javelins: the LEFT swing throws the left one - the game's own throw, the hand's own heading (the weight model's account is the right one's)
		setup(D2RVR_TYPE_JAVELIN, D2RVR_TYPE_JAVELIN);
		throwwire::SetWeightSource(&WeightStub);
		g_wValid = true; g_wLandX = 10.0f; g_wLandZ = 0.0f;   // the first weapon's account: to the right
		Swing sw = Fresh(); sw.idle = 1.0;   // the left hand goes straight ahead
		Feed(lg, sw, AfterRelease(sw));
		EXPECT(Presses() == 1, "two javelins, the left swing: presses %d", Presses());
		EXPECT(!g_actions.empty() && g_actions[0].bits == D2RVR_ACT_ATTACK && g_actions[0].src == directinput::kSrcJavelin, "the game's own attack press");
		EXPECT(g_gripTakes > 0 && g_leftGripTakes > 0, "both grips are the throw's (right %d, left %d)", g_gripTakes, g_leftGripTakes);
		EXPECT(throwwire::GripIsThrowsAt(0) && throwwire::GripIsThrowsAt(1), "both grips owned");
		float r = 9, f = 9;
		throwwire::ThrowInfo ti;
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r) < 0.02f && std::fabs(f - 1.0f) < 0.02f, "along the left hand, not the first's account: right %.3f forward %.3f", r, f);
		EXPECT(throwwire::Info(&ti) && !ti.fromWeight, "the second's throw is not the weight model's account");
		EXPECT(g_secondThrown == 1, "the weight model is told the second was thrown (%d)", g_secondThrown);
		EXPECT(throwwire::Live(), "the press is live (a thrown weapon's, for vrcam)");
		// the right one still throws too, and is the weight model's
		Feed(lg, sw, sw.End() + 0.8);
		Sleep(600);
		Swing s2 = Fresh(); s2.idle = 1.0;
		Feed(rg, s2, AfterRelease(s2));
		EXPECT(Presses() == 2, "then the right swing: presses %d", Presses());
		EXPECT(throwwire::Aim(&r, &f) && std::fabs(r - 1.0f) < 1e-3f, "the right one's heading is the weight model's: %.3f", r);
		EXPECT(g_secondThrown == 1, "the first weapon thrown: no second throw told (%d)", g_secondThrown);
	}
	{   // an axe in the right slot and a javelin in the left: the GAME's Throw takes the right slot's weapon and refuses ("Impossible", the headset 2026-10-28) -
		// so nothing is pressed and neither grip is the throw's (the left one stays the weight model's)
		GripMode();
		throwwire::SetFollowHand(true);
		g_weapon = D2RVR_TYPE_AXE; g_weaponHand = 0; g_otherKind = D2RVR_TYPE_JAVELIN;
		Swing sw = Fresh(); sw.idle = 1.0;
		Feed(lg, sw, AfterRelease(sw));
		EXPECT(Presses() == 0, "axe right, javelin left: the game would refuse - presses %d", Presses());
		EXPECT(g_gripTakes == 0 && g_leftGripTakes == 0, "neither grip is taken (left %d, right %d)", g_leftGripTakes, g_gripTakes);
		EXPECT(!throwwire::GripIsThrowsAt(0) && !throwwire::GripIsThrowsAt(1), "neither grip owned");
		// the right slot empty (or a shield): the game throws the left slot's - the left controller does
		GripMode();
		throwwire::SetFollowHand(true);
		g_weapon = D2RVR_TYPE_JAVELIN; g_weaponHand = 1; g_otherKind = 0;
		Swing s3 = Fresh(); s3.idle = 1.0;
		Feed(lg, s3, AfterRelease(s3));
		EXPECT(Presses() == 1 && g_leftGripTakes > 0 && g_gripTakes == 0, "the right slot empty, javelin left: the left throws (presses %d)", Presses());
		EXPECT(throwwire::Live(), "the press is live");
		// unticked: the game's kind is an axe - nothing, as ever
		GripMode();
		g_weapon = D2RVR_TYPE_AXE; g_weaponHand = 0; g_otherKind = D2RVR_TYPE_JAVELIN;
		Swing s2 = Fresh(); s2.idle = 1.0;
		Feed(lg, s2);
		EXPECT(Presses() == 0 && g_leftGripTakes == 0 && g_gripTakes == 0, "off: an axe's kind throws nothing (%d)", Presses());
	}
	g_byGrip = false; Reset();
}

// throw_left_hand with the weight model: a hand that does not hold its javelin (put down, lying, between throws) is a free hand - its grip is not the
// throw's, so it takes a belt bottle as a sword's free hand does (Serg, 2026-10-28: with javelins held no bottle came). It is the throw's again once the
// javelin is back in it; the release that threw it stays the throw's.
void TestFreeHand() {
	std::printf("throw_left_hand: an empty hand's grip is free (a belt bottle)\n");
	Rig rg; rg.grip = true; rg.flags = directinput::kFocused | directinput::kDirect;
	auto setup = [] {
		GripMode();
		throwwire::SetFollowHand(true);
		throwwire::CarrySource cs;
		cs.placed = [] { return g_placed; };
		cs.carrying = [] { return g_carrying; };
		cs.firstSlot = [] { return g_firstSlot; };
		cs.holdersOf = [](int slot) { return HoldersOf(slot); };
		throwwire::SetCarrySource(cs);
		g_placed = true; g_firstSlot = 0;
		g_weapon = D2RVR_TYPE_JAVELIN; g_weaponHand = 0;
	};
	{   // the javelin lies on the floor (no hand on it): the right grip is free - squeezed at the belt it is not taken, nothing is thrown
		setup();
		g_carrying = -1; g_holds[0] = false;
		Swing sw = Fresh(); sw.idle = 1.0;
		Feed(rg, sw);
		EXPECT(g_gripTakes == 0 && !throwwire::GripIsThrowsAt(1), "an empty hand: its grip is not the throw's (takes %d)", g_gripTakes);
		EXPECT(Presses() == 0, "an empty hand's swing throws nothing: %d", Presses());
		EXPECT(throwwire::ThrownInPlay() == D2RVR_TYPE_JAVELIN, "the javelin is still in play (the trigger stays withheld)");
		// it comes back into the hand: the throw's again, and its swing throws
		g_carrying = 0; g_holds[0] = true;
		Swing s2 = Fresh(); s2.idle = 1.0;
		Feed(rg, s2, AfterRelease(s2));
		EXPECT(g_gripTakes > 0 && Presses() == 1, "back in the hand: the grip is the throw's and it throws (takes %d, presses %d)", g_gripTakes, Presses());
	}
	{   // the release that throws it: the weight model lets go at once (no hand on it in the same frame) - still the throw's
		setup();
		g_carrying = 0; g_holds[0] = true;
		Swing sw = Fresh(); sw.idle = 1.0;
		Feed(rg, sw, sw.Released() - 0.03);
		g_carrying = -1; g_holds[0] = false;   // (the hand opened: the weight model let go before the gesture's frame)
		Feed(rg, sw, AfterRelease(sw));
		EXPECT(Presses() == 1, "the throw's own release is still the throw's: presses %d", Presses());
		Feed(rg, sw, sw.End() + 0.5);
		EXPECT(!throwwire::GripIsThrowsAt(1), "a moment after, the empty hand is free");
	}
	{   // unticked: the grip is the throw's whether or not a hand holds it, as ever
		setup();
		throwwire::SetFollowHand(false);
		g_carrying = -1; g_holds[0] = false;
		Swing sw = Fresh(); sw.release = false;
		Feed(rg, sw, sw.t0 + 0.4);
		EXPECT(throwwire::GripIsThrowsAt(1) && g_gripTakes > 0, "off: owned as ever");
	}
	g_byGrip = false; Reset();
}

int main() {
	throwwire::Init(nullptr, [] { return g_canAim; });
	throwwire::SetAutoSkillFn([] { return g_auto; });
	throwwire::SetModes(true, false);
	if (!g_listener) {
		std::printf("the wire never listened\n");
		return 2;
	}
	TestJavelinThrow();
	TestInfo();
	TestHeadFrame();
	TestWeightSource();
	TestPotionLanding();
	TestPotionBorn();
	TestPotionHide();
	TestNoThrow();
	TestCancels();
	TestBoxUnticked();
	TestTwoThrows();
	TestLiveWindow();
	TestRoute();
	TestGripThrow();
	TestGripOwns();
	TestGripHeld();
	TestGripNoThrow();
	TestGripModes();
	TestHoldOff();
	TestOtherView();
	TestLeftHand();
	TestCarriedLeft();
	TestEachHand();
	TestFreeHand();
	std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
