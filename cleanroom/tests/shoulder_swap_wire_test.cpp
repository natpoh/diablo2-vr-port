// The shoulder swap's wiring (vr/shoulder_swap_wire.cpp) fed direct_input.h's frames, with stand-ins for what it
// calls: direct_input's Listen / Take / Tap and gamestate's WeaponSet. No game, no headset.
//
//   shoulder_swap_wire_test
//
// Checks: nothing happens with the box unticked; one press behind the shoulder is one Tap of the game's command
// 0x2C and a Take of the grip for as long as it is held; the swap waits for the weapon set to show and is never
// asked again by itself; a menu, the lost focus, a new context, an unknown weapon set and the other hand do not
// swap; the head's yaw, pitch and roll do not matter; a claim goes stale without frames; with a weapon stowed on
// the back ([weight] to_back) the press is a take, not a swap.

#include "../../vr/direct_input.h"
#include "../../vr/shoulder_swap_wire.h"
#include "../../vr/throw_gesture_wire.h"
#include "../../vr/view_gate.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>

// ---- the stand-ins ----
namespace gamestate {
uint32_t g_shown = 1;
uint32_t WeaponSet() { return g_shown; }
}   // namespace gamestate

namespace throwwire {   // [hands] throw_by_grip: the right grip is a throw's while a throwing weapon is wielded
bool g_gripThrows = false;
bool GripIsThrows() { return g_gripThrows; }
int g_throwXr = 1;   // [hands] throw_left_hand: the controller that throws (0 left, 1 right)
bool GripIsThrowsAt(int xr) { return g_gripThrows && xr == g_throwXr; }
}   // namespace throwwire

namespace directinput {
FrameFn g_fn = nullptr;
int g_takes = 0, g_taps = 0;
int g_takeHand = -1;
uint32_t g_takeMask = 0, g_tapCmd = 0;
Source g_takeSrc = kSrcLayout;
bool Listen(FrameFn fn, void*) { g_fn = fn; return true; }
void Take(int hand, uint32_t buttons, Source src) { ++g_takes; g_takeHand = hand; g_takeMask = buttons; g_takeSrc = src; }
void Tap(uint32_t cmd) { ++g_taps; g_tapCmd = cmd; }
}   // namespace directinput

namespace {

int g_fails = 0;

#define EXPECT(c)                                              \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

constexpr double kDt = 1.0 / 90.0;

struct Q { float v[4]; };
Q AxisAngle(float ax, float ay, float az, float a) {
	const float s = std::sin(a * 0.5f);
	return Q{{ax * s, ay * s, az * s, std::cos(a * 0.5f)}};
}
Q Mul(const Q& a, const Q& b) {
	const float *p = a.v, *q = b.v;
	return Q{{p[3] * q[0] + p[0] * q[3] + p[1] * q[2] - p[2] * q[1], p[3] * q[1] - p[0] * q[2] + p[1] * q[3] + p[2] * q[0],
	          p[3] * q[2] + p[0] * q[1] - p[1] * q[0] + p[2] * q[3], p[3] * q[3] - p[0] * q[0] - p[1] * q[1] - p[2] * q[2]}};
}
void Turn(const Q& q, const float v[3], float o[3]) {
	const float x = q.v[0], y = q.v[1], z = q.v[2], w = q.v[3];
	o[0] = (1 - 2 * (y * y + z * z)) * v[0] + 2 * (x * y - w * z) * v[1] + 2 * (x * z + w * y) * v[2];
	o[1] = 2 * (x * y + w * z) * v[0] + (1 - 2 * (x * x + z * z)) * v[1] + 2 * (y * z - w * x) * v[2];
	o[2] = 2 * (x * z - w * y) * v[0] + 2 * (y * z + w * x) * v[1] + (1 - 2 * (x * x + y * y)) * v[2];
}

// A headset: the head in the room, a hand held at `rel` in the head's turn frame, both hands tracked.
struct Rig {
	directinput::Frame f{};
	float rel[2][3] = {{0.0f, -0.4f, -0.3f}, {0.23f, -0.2f, 0.14f}};   // [0 left, 1 right]
	float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
	Rig() {
		f.version = directinput::kVersion;
		f.session = 1;
		f.epoch = 1;
		f.flags = directinput::kFocused;
		f.time = 100.0;
		f.head.valid = 1;
		f.head.pos[0] = 2.0f; f.head.pos[1] = 1.7f; f.head.pos[2] = -4.0f;
		for (auto& h : f.hand) { h.active = 1; h.grip.valid = 1; h.aim.valid = 1; }
	}
	void Frame(int n = 1) {
		while (n-- > 0) {
			++f.counter;
			f.time += kDt;
			const Q yawQ = AxisAngle(0, 1, 0, yaw);
			const Q head = Mul(yawQ, Mul(AxisAngle(1, 0, 0, pitch), AxisAngle(0, 0, 1, roll)));
			for (int i = 0; i < 4; ++i) f.head.rot[i] = head.v[i];
			for (int i = 0; i < 2; ++i) {
				float w[3];
				Turn(yawQ, rel[i], w);
				for (int k = 0; k < 3; ++k) f.hand[i].grip.pos[k] = f.head.pos[k] + w[k];
				f.hand[i].grip.rot[3] = 1.0f;
			}
			directinput::g_fn(f, nullptr);
		}
	}
	void Grip(int xrHand, float v) { f.hand[xrHand].squeeze = v; }
};

void ResetCounts() { directinput::g_takes = directinput::g_taps = 0; directinput::g_takeHand = -1; directinput::g_takeMask = 0; }

}   // namespace

bool g_f4 = true;
bool TheView() { return g_f4; }
bool g_onBack = false;   // [weight] to_back: a weapon is stowed on the back
bool OnBack() { return g_onBack; }
bool g_placed = true;    // [hands] shoulder_draw: the weight model has a weapon
bool Placed() { return g_placed; }

int main() {
	shoulderwire::Init(nullptr);
	EXPECT(directinput::g_fn != nullptr);

	// Unticked: the grip behind the shoulder is nothing.
	{
		shoulderwire::Configure(false, false);
		ResetCounts();
		Rig r;
		r.Frame(20);
		r.Grip(1, 1.0f);
		r.Frame(20);
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 0);
		EXPECT(!shoulderwire::Claims(0) && !shoulderwire::Claims(1));
	}

	// Ticked, the right hand: one press, one Tap of 0x2C; the grip is taken while it is held.
	{
		shoulderwire::Configure(true, false);
		gamestate::g_shown = 1;
		ResetCounts();
		Rig r;
		r.Frame(20);   // the hand rests behind the right shoulder
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 0 && !shoulderwire::Claims(0));   // no grip: nothing claimed
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 1 && directinput::g_tapCmd == 0x2C);
		EXPECT(directinput::g_takes == 1 && directinput::g_takeHand == 1 && directinput::g_takeSrc == directinput::kSrcShoulderSwap);
		EXPECT(directinput::g_takeMask == (directinput::kSqueeze | directinput::kTakeSqueeze));
		EXPECT(shoulderwire::Claims(0) && !shoulderwire::Claims(1));
		r.Frame(60);   // held, and the game has not shown it: no second Tap, the grip stays taken
		EXPECT(directinput::g_taps == 1 && directinput::g_takes == 61);
		gamestate::g_shown = 2;   // the game shows it
		r.Frame(10);
		EXPECT(directinput::g_taps == 1);
		r.Grip(1, 0.0f);
		r.Frame(14);
		EXPECT(!shoulderwire::Claims(0));
		r.Grip(1, 1.0f);   // the next press swaps back: set II is shown
		r.Frame();
		EXPECT(directinput::g_taps == 2);
		// ... and this one the game does not take: set II stays. Not asked again, however long the grip is held.
		r.Frame(400);
		EXPECT(directinput::g_taps == 2);
		r.Grip(1, 0.0f);
		r.Frame(14);
		r.Grip(1, 1.0f);   // only a new press asks again
		r.Frame();
		EXPECT(directinput::g_taps == 3);
		gamestate::g_shown = 1;
		r.Frame(5);
		r.Grip(1, 0.0f);
		r.Frame(2);
	}

	// A swap out and waiting: a press meanwhile claims the grip and asks for nothing more.
	{
		gamestate::g_shown = 1;
		ResetCounts();
		Rig r;
		r.Frame(20);
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 1);
		r.Grip(1, 0.0f);
		r.Frame(14);
		r.Grip(1, 1.0f);   // a second press while the first is still waiting
		r.Frame(3);
		EXPECT(directinput::g_taps == 1 && shoulderwire::Claims(0));
		gamestate::g_shown = 2;   // the first shows; this press is used up
		r.Frame(5);
		EXPECT(directinput::g_taps == 1);
		r.Grip(1, 0.0f);
		r.Frame(2);
	}

	// The right grip is a throw's (a throwing weapon wielded, [hands] throw_by_grip): no swap, no claim with the right hand;
	// the left hand chosen for the swap is not affected.
	{
		gamestate::g_shown = 1;
		ResetCounts();
		throwwire::g_gripThrows = true;
		Rig r;
		r.Frame(20);
		r.Grip(1, 1.0f);
		r.Frame(10);
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 0 && !shoulderwire::Claims(0));
		r.Grip(1, 0.0f);
		r.Frame(14);
		throwwire::g_gripThrows = false;   // the weapon is a sword again: the same press swaps
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 1);
		gamestate::g_shown = 2;
		r.Frame(5);
		r.Grip(1, 0.0f);
		r.Frame(2);
		gamestate::g_shown = 1;
		ResetCounts();
		throwwire::g_gripThrows = true;
		shoulderwire::Configure(true, true);   // the left hand is the swap's: the right grip's ownership does not matter
		Rig l;
		l.rel[0][0] = -0.23f; l.rel[0][1] = -0.2f; l.rel[0][2] = 0.14f;
		l.rel[1][0] = 0.0f; l.rel[1][1] = -0.4f; l.rel[1][2] = -0.3f;
		l.Frame(20);
		l.Grip(0, 1.0f);
		l.Frame();
		EXPECT(directinput::g_taps == 1 && directinput::g_takeHand == 0);
		gamestate::g_shown = 2;
		l.Frame(5);
		l.Grip(0, 0.0f);
		l.Frame(2);
		// [hands] throw_left_hand: the thrown weapon is in the LEFT hand, its grip is the throw's - the left hand does not swap now
		ResetCounts();
		throwwire::g_throwXr = 0;
		gamestate::g_shown = 1;
		Rig l2;
		l2.rel[0][0] = -0.23f; l2.rel[0][1] = -0.2f; l2.rel[0][2] = 0.14f;
		l2.rel[1][0] = 0.0f; l2.rel[1][1] = -0.4f; l2.rel[1][2] = -0.3f;
		l2.Frame(20);
		l2.Grip(0, 1.0f);
		l2.Frame(10);
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 0 && !shoulderwire::Claims(1));
		l2.Grip(0, 0.0f);
		l2.Frame(14);
		throwwire::g_throwXr = 1;
		throwwire::g_gripThrows = false;
		shoulderwire::Configure(true, false);
		gamestate::g_shown = 1;
	}

	// The head turned, tilted and rolled: the same hand behind the same shoulder.
	{
		gamestate::g_shown = 1;
		for (float yaw : {-2.5f, 1.5708f, 2.9f})
			for (float pitch : {-0.9f, 0.0f, 0.9f}) {
				ResetCounts();
				Rig r;
				r.yaw = yaw; r.pitch = pitch; r.roll = 0.4f;
				r.Frame(20);
				r.Grip(1, 1.0f);
				r.Frame();
				EXPECT(directinput::g_taps == 1);
				gamestate::g_shown = gamestate::g_shown == 1 ? 2 : 1;   // it shows
				r.Frame(5);
				r.Grip(1, 0.0f);
				r.Frame(2);
			}
	}

	// The other hand reaching back does nothing when the right is chosen; the left chosen, it is the one.
	{
		gamestate::g_shown = 1;
		ResetCounts();
		Rig r;
		r.rel[0][0] = -0.23f; r.rel[0][1] = -0.2f; r.rel[0][2] = 0.14f;   // the left hand behind the left shoulder
		r.rel[1][0] = 0.0f; r.rel[1][1] = -0.4f; r.rel[1][2] = -0.3f;     // the right hand down in front
		r.Frame(20);
		r.Grip(0, 1.0f);
		r.Frame(3);
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 0);
		r.Grip(0, 0.0f);
		r.Frame(2);
		shoulderwire::Configure(true, true);
		r.Frame(20);
		r.Grip(0, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 1 && directinput::g_takeHand == 0);
		EXPECT(shoulderwire::Claims(1) && !shoulderwire::Claims(0));
		r.Grip(0, 0.0f);
		r.Frame(2);
		shoulderwire::Configure(true, false);
		gamestate::g_shown = 1;
	}

	// A menu, the lost focus, an unknown weapon set, a new context with the grip held.
	{
		ResetCounts();
		Rig r;
		r.f.flags |= directinput::kInMenu;
		r.Frame(20);
		r.Grip(1, 1.0f);
		r.Frame(5);
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 0);   // a menu: not ours
		r.Grip(1, 0.0f);
		r.f.flags = directinput::kFocused;
		r.Frame(20);
		r.f.flags = 0;   // no focus
		r.Grip(1, 1.0f);
		r.Frame(5);
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 0);
		r.Grip(1, 0.0f);
		r.f.flags = directinput::kFocused;
		r.Frame(20);
		// the weapon set not known: the press is claimed, nothing is asked
		gamestate::g_shown = 0;
		r.Grip(1, 1.0f);
		r.Frame(3);
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 3);
		r.Grip(1, 0.0f);
		r.Frame(14);
		gamestate::g_shown = 1;
		// a new context (a menu closed, the focus back) while the grip is held: used up
		r.Frame(14);
		r.Grip(1, 1.0f);
		++r.f.epoch;
		r.Frame(10);
		EXPECT(directinput::g_taps == 0);
		r.Grip(1, 0.0f);
		r.Frame(14);
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 1);
		gamestate::g_shown = 2;
		r.Frame(5);
		r.Grip(1, 0.0f);
		r.Frame(2);
		gamestate::g_shown = 1;
	}

	// A new session forgets a request that was out: the next press asks again (the old one is not a block).
	{
		ResetCounts();
		Rig r;
		r.Frame(20);
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 1);
		r.Grip(1, 0.0f);
		r.Frame(14);
		++r.f.session;
		r.Frame(20);
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 2);
		gamestate::g_shown = 2;
		r.Frame(5);
		r.Grip(1, 0.0f);
		r.Frame(2);
		gamestate::g_shown = 1;
	}

	// F4 only (vr/view_gate.h): in another view the gesture claims nothing, takes nothing and swaps nothing; a view change in the
	// middle of a press lets go at once; back in F4 it starts fresh.
	{
		viewgate::Set(&TheView);
		shoulderwire::Configure(true, false);
		gamestate::g_shown = 1;
		// the press starts in another view (the frames carry the flag, the view test says no too)
		g_f4 = false;
		ResetCounts();
		Rig r;
		r.f.flags = directinput::kFocused | directinput::kInMenu | directinput::kOtherView;
		r.Frame(20);
		r.Grip(1, 1.0f);
		r.Frame(10);
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 0 && !shoulderwire::Claims(0) && !shoulderwire::Claims(1));
		// ... the frames' flag alone is enough
		g_f4 = true;
		r.Grip(1, 0.0f);
		r.Frame(14);
		r.Grip(1, 1.0f);
		r.Frame(5);
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 0);
		// ... and the view test alone (a frame made before the change)
		r.f.flags = directinput::kFocused;
		r.Grip(1, 0.0f);
		r.Frame(14);
		g_f4 = false;
		r.Grip(1, 1.0f);
		r.Frame(5);
		EXPECT(directinput::g_taps == 0 && directinput::g_takes == 0 && !shoulderwire::Claims(0));
		r.Grip(1, 0.0f);
		r.Frame(14);

		// F4: a press, the claim held - then F2 in the middle of it
		g_f4 = true;
		ResetCounts();
		r.Frame(20);
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 1 && shoulderwire::Claims(0));
		g_f4 = false;   // no frame yet: the belt already gets its bottles back
		EXPECT(!shoulderwire::Claims(0) && !shoulderwire::Claims(1));
		r.f.flags = directinput::kFocused | directinput::kInMenu | directinput::kOtherView;
		++r.f.epoch;
		const int takes = directinput::g_takes;
		r.Frame(10);
		EXPECT(directinput::g_taps == 1 && directinput::g_takes == takes && !shoulderwire::Claims(0));   // nothing more taken or tapped
		gamestate::g_shown = 2;   // the swap shows meanwhile: nothing to do about it
		r.Frame(5);
		EXPECT(directinput::g_taps == 1);

		// back in F4 with the grip still down: that press is used up; a new press swaps again
		g_f4 = true;
		r.f.flags = directinput::kFocused;
		++r.f.epoch;
		r.Frame(10);
		EXPECT(directinput::g_taps == 1);
		r.Grip(1, 0.0f);
		r.Frame(14);
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 2 && shoulderwire::Claims(0));
		gamestate::g_shown = 1;
		r.Frame(5);
		r.Grip(1, 0.0f);
		r.Frame(2);
		viewgate::Set(nullptr);
	}

	// [weight] to_back: with a weapon stowed on the back, the grip behind the shoulder is a TAKE (the weight model's own, from the same
	// press), not the swap: the grip is still claimed (kept from the layout), nothing is tapped. The weapon in hand again: the swap as ever.
	// No source registered: the swap as ever, whatever the weight model says.
	{
		shoulderwire::Configure(true, false);
		gamestate::g_shown = 1;
		ResetCounts();
		Rig r;
		r.Frame(20);
		shoulderwire::SetStowedSource(&OnBack);
		g_onBack = true;
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 0);
		EXPECT(directinput::g_takes >= 1 && shoulderwire::Claims(0));
		r.Frame(60);
		EXPECT(directinput::g_taps == 0);
		r.Grip(1, 0.0f);
		r.Frame(14);
		g_onBack = false;   // a weapon in hand: the swap
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 1 && directinput::g_tapCmd == 0x2C);
		gamestate::g_shown = 2;
		r.Frame(5);
		r.Grip(1, 0.0f);
		r.Frame(14);
		shoulderwire::SetStowedSource(nullptr);   // not registered: nothing is ever stowed
		g_onBack = true;
		r.Grip(1, 1.0f);
		r.Frame();
		EXPECT(directinput::g_taps == 2);
		gamestate::g_shown = 1;
		r.Frame(5);
		r.Grip(1, 0.0f);
		r.Frame(2);
		g_onBack = false;
	}

	// [hands] shoulder_draw: the zone is a holster - both hands claim a grip pressed behind a shoulder, the swap is never pressed, with or without shoulder_swap.
	{
		shoulderwire::SetWeaponSource(&Placed);
		for (int swapTick = 0; swapTick < 2; ++swapTick) {
			shoulderwire::Configure(swapTick == 1, false);
			shoulderwire::SetDraw(true);
			gamestate::g_shown = 1;
			ResetCounts();
			Rig r;
			r.rel[0][0] = -0.23f; r.rel[0][1] = -0.2f; r.rel[0][2] = 0.14f;   // the left hand behind the left shoulder
			r.Frame(20);
			EXPECT(!shoulderwire::Claims(0) && !shoulderwire::Claims(1));       // grips open: nothing claimed
			r.Grip(1, 1.0f);                                                    // the RIGHT hand's grip (direct_input's hand 1)
			r.Frame();
			EXPECT(directinput::g_taps == 0 && directinput::g_takes == 1 && directinput::g_takeHand == 1);
			EXPECT(directinput::g_takeMask == (directinput::kSqueeze | directinput::kTakeSqueeze));
			EXPECT(shoulderwire::Claims(0) && !shoulderwire::Claims(1));
			r.Frame(40);
			EXPECT(directinput::g_taps == 0);                                   // never a swap, however long it is held
			r.Grip(1, 0.0f);
			r.Frame(2);
			EXPECT(!shoulderwire::Claims(0));
			ResetCounts();
			r.Grip(0, 1.0f);                                                    // the LEFT hand's grip behind the left shoulder: claimed too
			r.Frame();
			EXPECT(directinput::g_taps == 0 && directinput::g_takes == 1 && directinput::g_takeHand == 0);
			EXPECT(shoulderwire::Claims(1) && !shoulderwire::Claims(0));
			r.Grip(0, 1.0f);
			r.Frame(10);
			r.Grip(0, 0.0f);
			r.Frame(2);
			// a hand in front of the chest: nothing; a menu: nothing
			ResetCounts();
			r.rel[1][0] = 0.23f; r.rel[1][1] = -0.2f; r.rel[1][2] = -0.2f;
			r.Frame(14);
			r.Grip(1, 1.0f);
			r.Frame(5);
			EXPECT(directinput::g_takes == 0 && !shoulderwire::Claims(0));
			r.Grip(1, 0.0f);
			r.rel[1][2] = 0.14f;
			r.Frame(14);
			r.f.flags |= directinput::kInMenu;
			r.Grip(1, 1.0f);
			r.Frame(5);
			EXPECT(directinput::g_takes == 0 && !shoulderwire::Claims(0));
			r.Grip(1, 0.0f);
			r.f.flags = directinput::kFocused;
			r.Frame(14);
			// the weight model has no weapon (a bow in hand, the box off): the zone is nothing
			g_placed = false;
			r.Frame(14);
			r.Grip(1, 1.0f);
			r.Frame(5);
			EXPECT(directinput::g_takes == 0 && !shoulderwire::Claims(0));
			r.Grip(1, 0.0f);
			r.Frame(2);
			g_placed = true;
			// the tick unticked, draw off: a hand there is nothing again; swap ticked: the swap is back
			shoulderwire::SetDraw(false);
			r.Frame(14);
			ResetCounts();
			r.Grip(1, 1.0f);
			r.Frame();
			EXPECT(swapTick == 1 ? directinput::g_taps == 1 : directinput::g_taps == 0);
			r.Grip(1, 0.0f);
			r.Frame(2);
			gamestate::g_shown = 2;
			r.Frame(10);
			gamestate::g_shown = 1;
		}
		shoulderwire::SetWeaponSource(nullptr);
		shoulderwire::SetDraw(false);
		shoulderwire::Configure(true, false);   // (as the blocks before and after have it)
	}

	// Without frames the claim goes stale; the box unticked lets go at once.
	{
		Rig r;
		r.Frame(20);
		r.Grip(1, 1.0f);
		r.Frame(3);
		EXPECT(shoulderwire::Claims(0));
		Sleep(400);
		EXPECT(!shoulderwire::Claims(0));
		r.Frame(1);
		EXPECT(shoulderwire::Claims(0));
		shoulderwire::Configure(false, false);
		EXPECT(!shoulderwire::Claims(0));
		r.Grip(1, 0.0f);
		r.Frame(2);
	}

	if (g_fails) {
		std::printf("shoulder_swap_wire_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("shoulder_swap_wire_test: off, one press one Tap, the grip taken, no repeat, menu/focus/context/set guards, hands, head pose, the holster (both hands, no swap, no weapon = nothing), stale claim: all passed\n");
	return 0;
}
