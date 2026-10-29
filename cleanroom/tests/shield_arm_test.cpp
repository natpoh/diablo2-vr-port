// The shield's arm (vr/shield_arm.h), pure: the follower (worn on the left arm, opposite the sword; put away at the left shoulder and drawn onto the arm that reached - two ticks) and the mirror (the
// shield on the right forearm as the reflection of the left arm's strap, handedness kept).
//
//   shield_arm_test

#include "../../vr/shield_arm.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace shieldarm;
using weaponback::Mat;
using weaponback::V3;

namespace {

int g_fails = 0;

#define EXPECT(c)                                              \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)

Mat Make(V3 r0, V3 r1, V3 r2, V3 t) {
	Mat m{};
	const V3 r[3] = {r0, r1, r2};
	for (int i = 0; i < 3; ++i) { m.m[i * 4] = r[i].x; m.m[i * 4 + 1] = r[i].y; m.m[i * 4 + 2] = r[i].z; }
	m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z; m.m[15] = 1.0f;
	return m;
}
bool Near(V3 a, V3 b, float e = 1e-4f) { return weaponback::Len(weaponback::Add(a, weaponback::Mul(b, -1.0f))) < e; }
bool SameMat(const Mat& a, const Mat& b, float e = 1e-4f) {
	for (int i = 0; i < 16; ++i)
		if (std::fabs(a.m[i] - b.m[i]) > e) return false;
	return true;
}

constexpr unsigned kR = 1, kL = 2;   // the hands that hold the weapon, a bit each

void Follows() {
	// WEAPON AND SHIELD ARE ALWAYS ON OPPOSITE ARMS. It starts on the left arm.
	Follower f;
	EXPECT(f.Now() == Arm::Left);
	Out o = f.Step(true, 0);
	EXPECT(o.arm == Arm::Left && !o.changed);   // nothing held, nothing drawn: where it is
	o = f.Step(true, kR);
	EXPECT(o.arm == Arm::Left && !o.changed);   // the weapon in the right hand: the shield on the left, as the game has it
	// the weapon ends up in the shield's hand ALONE: it jumps - drawn into the left hand (a cross draw), taken up by it, passed to it, put in it by the game: all arrive as the same mask
	o = f.Step(true, kL);
	EXPECT(o.arm == Arm::Right && o.changed && f.Now() == Arm::Right);
	o = f.Step(true, kL);
	EXPECT(o.arm == Arm::Right && !o.changed);
	// the weapon let go (falling, lying): the shield stays where it is
	for (int i = 0; i < 5; ++i) EXPECT(f.Step(true, 0).arm == Arm::Right && f.Now() == Arm::Right);
	// ... and into the right hand, which now wears the shield: it jumps back to the left
	o = f.Step(true, kR);
	EXPECT(o.arm == Arm::Left && o.changed);
	// into the hand that does not wear it: it stays
	EXPECT(f.Step(true, kR).arm == Arm::Left);
	// both hands on the weapon (a pass in progress, a two-hand grip): the shield stays; when the hand without it lets go, the weapon is in the shield's hand alone: it jumps
	EXPECT(f.Step(true, kR | kL).arm == Arm::Left && f.Now() == Arm::Left);
	EXPECT(f.Step(true, kL).arm == Arm::Right);                     // the right hand let go: the weapon is in the left hand alone, the shield's: it jumps to the right
	EXPECT(f.Step(true, kR | kL).arm == Arm::Right);                // the right hand takes the shaft too: stays
	EXPECT(f.Step(true, kR).arm == Arm::Left);                      // the left let go: in the right hand alone, the shield's now: back to the left
	// the invariant itself, over every sequence of masks: the weapon is never alone in the hand that wears the shield after a step
	for (unsigned seq = 0; seq < 4 * 4 * 4 * 4; ++seq) {
		Follower g;
		for (int k = 0; k < 4; ++k) {
			const unsigned hands = (seq >> (2 * k)) & 3u;
			const Out r = g.Step(true, hands);
			EXPECT(hands != (1u << (unsigned)r.arm));
		}
	}
	// no shield (another weapon set, a new character): the next one starts on the left arm
	f.Step(true, kL);
	EXPECT(f.Now() == Arm::Right);
	o = f.Step(false, kL);
	EXPECT(o.arm == Arm::Left && f.Now() == Arm::Left);
	o = f.Step(true, 0);
	EXPECT(o.arm == Arm::Left && !o.changed);
	f.Step(true, kL);
	f.Reset();
	EXPECT(f.Now() == Arm::Left);
}

void Mirror() {
	using namespace shieldmirror;
	// a symmetric rig, standing: forearms 0.35 m to each side of the middle, running down. The left bone has a mirrored frame, the right one a relabelled, proper one.
	const V3 eL{0.35f, 1.30f, 0.0f}, eR{-0.35f, 1.30f, 0.0f}, down{0, -1, 0};
	const Mat bindL = Make({1, 0, 0}, down, {0, 0, 1}, eL);
	const Mat bindR = Make({0, 0, 1}, down, {1, 0, 0}, eR);
	EXPECT(std::fabs(PlaneX(bindL, bindR)) < 1e-6f);

	// a shield strapped on the left forearm, its plate standing 12 cm out (+x) from it, 15 cm down the arm: Y along the arm, row 0 its face, row 2 across the plate
	const Mat shield0 = Make({1, 0, 0}, down, {0, 0, 1}, {0.47f, 1.15f, 0.0f});
	EXPECT(DetSign(shield0) == -1);
	{
		const Axes a = FaceAxes(shield0, eL, {0.35f, 1.0f, 0.0f});
		EXPECT(a.known && a.normalRow == 0 && a.acrossRow == 2);
	}
	// ... the same shield with its face on row 2 and the plate's width on row 0
	const Mat shield2 = Make({0, 0, -1}, down, {1, 0, 0}, {0.47f, 1.15f, 0.0f});
	EXPECT(DetSign(shield2) == -1);
	{
		const Axes a = FaceAxes(shield2, eL, {0.35f, 1.0f, 0.0f});
		EXPECT(a.known && a.normalRow == 2 && a.acrossRow == 0);
	}
	// an origin on the forearm's axis tells nothing; one diagonal does not either
	{
		const Mat onAxis = Make({1, 0, 0}, down, {0, 0, 1}, {0.35f, 1.15f, 0.0f});
		EXPECT(!FaceAxes(onAxis, eL, {0.35f, 1.0f, 0.0f}).known);
		const Mat diag = Make({0.7071f, 0, 0.7071f}, down, {-0.7071f, 0, 0.7071f}, {0.47f, 1.15f, 0.0f});
		EXPECT(!FaceAxes(diag, eL, {0.35f, 1.0f, 0.0f}).known);
	}

	for (const Mat& shield : {shield0, shield2}) {
		const Axes a = FaceAxes(shield, eL, {0.35f, 1.0f, 0.0f});
		// unposed: the right arm in its bind pose carries the shield's reflection - the plate 12 cm out the other way, its face turned outward from the right arm,
		// the long axis still along the arm, the handedness the left bone's
		Mat out;
		EXPECT(RightArmMatrix(shield, bindR, bindR, 0.0f, a.acrossRow, -1, &out));
		EXPECT(Near(weaponback::Pos(out), {-0.47f, 1.15f, 0.0f}));
		EXPECT(Near(weaponback::Row(out, 1), down));
		EXPECT(DetSign(out) == -1);
		// the face points away from the body (-x) on the right arm
		EXPECT(weaponback::Row(out, a.normalRow).x < -0.99f);
		// the row across the plate is the one that was negated; the face is not
		const Mat refl = weaponback::Mul(shield, Reflect(0.0f));
		EXPECT(Near(weaponback::Row(out, a.normalRow), weaponback::Row(refl, a.normalRow)));
		EXPECT(Near(weaponback::Row(out, a.acrossRow), weaponback::Mul(weaponback::Row(refl, a.acrossRow), -1.0f)));

		// posed: the arm swung about the elbow by 40 degrees and moved; the shield goes with it exactly (the reflected one, carried as a rigid body)
		const float c = std::cos(0.7f), s = std::sin(0.7f);
		const Mat X = Make({c, s, 0}, {-s, c, 0}, {0, 0, 1}, {0.10f, -0.05f, 0.20f});   // a world motion (rows: v' = v * X)
		const Mat armNow = weaponback::Mul(bindR, X);
		Mat posed;
		EXPECT(RightArmMatrix(shield, bindR, armNow, 0.0f, a.acrossRow, -1, &posed));
		EXPECT(SameMat(posed, weaponback::Mul(out, X)));
		EXPECT(DetSign(posed) == -1);
	}

	// the handedness: wanted +1 (a proper bone) keeps the reflection as it is; the sign of an unusable matrix is refused
	{
		Mat out;
		EXPECT(RightArmMatrix(shield0, bindR, bindR, 0.0f, 2, +1, &out));
		EXPECT(DetSign(out) == 1);
		Mat squashed{};
		EXPECT(!RightArmMatrix(shield0, bindR, squashed, 0.0f, 2, -1, &out));
		EXPECT(!RightArmMatrix(shield0, bindR, bindR, 0.0f, 1, -1, &out));   // the long axis is never the one negated
	}
	// the plane: a rig whose middle is not at x = 0
	{
		const Mat l = Make({1, 0, 0}, down, {0, 0, 1}, {1.35f, 1.30f, 0.0f}), r = Make({0, 0, 1}, down, {1, 0, 0}, {0.65f, 1.30f, 0.0f});
		EXPECT(std::fabs(PlaneX(l, r) - 1.0f) < 1e-6f);
		const Mat sh = Make({1, 0, 0}, down, {0, 0, 1}, {1.47f, 1.15f, 0.0f});
		Mat out;
		EXPECT(RightArmMatrix(sh, r, r, PlaneX(l, r), 2, -1, &out));
		EXPECT(Near(weaponback::Pos(out), {0.53f, 1.15f, 0.0f}));
	}
}

// The two ticks are independent: FOLLOW (weapon and shield on opposite arms) and STOW (a grip behind the left shoulder puts the shield away and draws it).
In Gesture(bool follow, bool stow, unsigned hands, int pressBy = -1, bool busy = false) {
	In in;
	in.shield = true;
	in.follow = follow;
	in.stow = stow;
	in.hands = hands;
	for (int h = 0; h < 2; ++h) {
		in.hand[h].tracked = true;
		in.hand[h].atShield = true;   // (both hands are behind the left shoulder; only the one that presses acts)
		in.hand[h].busy = busy;
	}
	if (pressBy >= 0) in.hand[pressBy].press = true;
	return in;
}

void Ticks() {
	// FOLLOW off: the weapon in the shield's hand moves nothing
	{
		Follower f;
		EXPECT(f.Step(Gesture(false, false, kL)).arm == Arm::Left && !f.Stowed());
		EXPECT(f.Step(Gesture(false, false, kL)).arm == Arm::Left);
		EXPECT(f.Step(Gesture(true, false, kL)).arm == Arm::Right);   // ticked on: it jumps at once
		EXPECT(f.Step(Gesture(false, false, kR)).arm == Arm::Right);  // off again: the weapon in the hand that wears it, nothing moves
	}
	// STOW off: a grip behind the left shoulder does nothing
	{
		Follower f;
		const Out o = f.Step(Gesture(true, false, 0, 1));
		EXPECT(!o.stowed && o.event == Event::None && o.arm == Arm::Left);
	}
	// STOW on: put away by the hand that pressed, drawn by either hand onto ITS arm, put away again, and back
	{
		Follower f;
		Out o = f.Step(Gesture(false, true, 0, 1));
		EXPECT(o.stowed && o.event == Event::PutAway && o.by == 1 && f.Stowed());
		o = f.Step(Gesture(false, true, 0));   // no press: stays away
		EXPECT(o.stowed && o.event == Event::None);
		o = f.Step(Gesture(false, true, 0, 0));   // the RIGHT hand draws it: onto the right arm, mirrored
		EXPECT(!o.stowed && o.event == Event::Drawn && o.by == 0 && o.arm == Arm::Right && !o.changed);
		o = f.Step(Gesture(false, true, 0, 0));   // and puts it away again
		EXPECT(o.stowed && o.event == Event::PutAway && o.by == 0);
		o = f.Step(Gesture(false, true, 0, 1));   // the left hand draws it onto the left arm
		EXPECT(!o.stowed && o.event == Event::Drawn && o.arm == Arm::Left);
	}
	// a hand that holds the weapon or a potion (busy) neither puts away nor draws; a hand not tracked neither; a hand not at the shoulder neither
	{
		Follower f;
		EXPECT(!f.Step(Gesture(false, true, 0, 1, true)).stowed);
		In in = Gesture(false, true, 0, 1);
		in.hand[1].tracked = false;
		EXPECT(!f.Step(in).stowed);
		in = Gesture(false, true, 0, 1);
		in.hand[1].atShield = false;
		EXPECT(!f.Step(in).stowed);
		EXPECT(f.Step(Gesture(false, true, 0, 1)).stowed);
		in = Gesture(false, true, 0, 1);
		in.hand[1].busy = true;
		EXPECT(f.Step(in).stowed && f.Stowed());   // (a busy hand does not draw it either)
		in = Gesture(false, true, 0, 1);
		in.hand[1].atShield = false;
		EXPECT(f.Step(in).stowed);
	}
	// one change in a step: both hands press at once
	{
		Follower f;
		In in = Gesture(false, true, 0);
		in.hand[0].press = in.hand[1].press = true;
		const Out o = f.Step(in);
		EXPECT(o.stowed && o.by == 0);   // (the right hand first; the left's press is not a second change)
	}
	// STOW and FOLLOW together: a shield drawn while the weapon is in the other hand sits opposite it; a shield put away has no arm to jump from
	{
		Follower f;
		f.Step(Gesture(true, true, kR, 1));   // the weapon in the right hand: the left hand puts the shield away
		EXPECT(f.Stowed());
		Out o = f.Step(Gesture(true, true, kL));   // the weapon moves to the left hand while the shield is away: nothing to move
		EXPECT(o.stowed && !o.changed);
		// the left hand now holds the weapon (busy), the right hand draws the shield: onto the right arm, opposite the sword
		In in = Gesture(true, true, kL, 0);
		in.hand[1].busy = true;
		o = f.Step(in);
		EXPECT(!o.stowed && o.arm == Arm::Right && o.event == Event::Drawn);
		// the sword goes to the right hand (alone): the shield jumps to the left
		o = f.Step(Gesture(true, true, kR));
		EXPECT(o.arm == Arm::Left && o.changed);
	}
	// the invariant, over every sequence of (weapon hands, press by, tick states): with FOLLOW and not put away the weapon is never alone in the shield's hand after a step
	{
		int checked = 0;
		for (int follow = 0; follow < 2; ++follow)
			for (int stow = 0; stow < 2; ++stow) {
				Follower f;
				unsigned seed = 12345u + (unsigned)(follow * 7 + stow * 13);
				for (int i = 0; i < 4000; ++i) {
					seed = seed * 1664525u + 1013904223u;
					const unsigned hands = (seed >> 8) & 3u;
					const int press = (seed >> 12) % 5 == 0 ? (int)((seed >> 16) & 1u) : -1;
					const bool busy = ((seed >> 20) & 7u) == 0;
					const Out o = f.Step(Gesture(follow != 0, stow != 0, hands, press, busy));
					if (follow && !o.stowed) {
						EXPECT(hands != (1u << (unsigned)o.arm));
						++checked;
					}
					if (!stow) EXPECT(!o.stowed);
				}
			}
		EXPECT(checked > 2000);
	}
	// the shield carried behind the shoulder and let go there (as a weapon is put away): the hand that WEARS it, its grip let go behind the left shoulder
	{
		auto release = [](unsigned hands, int by, bool atShield, bool busy = false) {
			In in = Gesture(false, true, hands);
			for (int h = 0; h < 2; ++h) in.hand[h].atShield = false;
			in.hand[by].atShield = atShield;
			in.hand[by].busy = busy;
			in.hand[by].release = true;
			return in;
		};
		{   // the left hand wears it (as it starts): let go behind the left shoulder - away, by the release
			Follower f;
			Out o = f.Step(release(0, 1, true));
			EXPECT(o.stowed && o.event == Event::PutAway && o.by == 1 && o.byRelease && f.Stowed());
			// away: a release is never a draw (a draw is a squeeze) - and the hand's squeeze draws it onto its arm
			o = f.Step(release(0, 1, true));
			EXPECT(o.stowed && o.event == Event::None);
			o = f.Step(Gesture(false, true, 0, 0));
			EXPECT(!o.stowed && o.event == Event::Drawn && o.arm == Arm::Right);
		}
		{   // let go outside the zone, by a hand that is busy, by the hand that does not wear it, with the tick off: nothing
			Follower f;
			EXPECT(!f.Step(release(0, 1, false)).stowed);
			EXPECT(!f.Step(release(0, 1, true, true)).stowed);
			EXPECT(!f.Step(release(0, 0, true)).stowed);   // (the right hand: the shield is on the left arm)
			In off = release(0, 1, true);
			off.stow = false;
			EXPECT(!f.Step(off).stowed);
			In untracked = release(0, 1, true);
			untracked.hand[1].tracked = false;
			EXPECT(!f.Step(untracked).stowed);
		}
		{   // the shield on the RIGHT arm (the weapon was in the left hand): the right hand carries it, the left's release is nothing
			Follower f;
			f.Step(Gesture(true, true, kL));
			EXPECT(f.Now() == Arm::Right);
			EXPECT(!f.Step(release(kL, 1, true)).stowed);   // (and the left hand holds the weapon anyway)
			Out o = f.Step(release(0, 0, true));
			EXPECT(o.stowed && o.byRelease && o.by == 0);
			EXPECT(f.Step(Gesture(false, true, 0, 1)).arm == Arm::Left);   // (drawn onto the arm that squeezed)
		}
		{   // the squeeze keeps working beside it, and says it was a squeeze
			Follower f;
			const Out o = f.Step(Gesture(false, true, 0, 1));
			EXPECT(o.stowed && o.event == Event::PutAway && !o.byRelease);
			In both = release(0, 1, true);
			both.hand[1].press = true;   // (a squeeze and a release in one step: the squeeze it is)
			Follower g;
			EXPECT(!g.Step(both).byRelease);
		}
	}
	// no shield: starts on the left arm, never put away
	{
		Follower f;
		f.Step(Gesture(true, true, 0, 1));
		In in = Gesture(true, true, 0, 1);
		in.shield = false;
		const Out o = f.Step(in);
		EXPECT(!o.stowed && o.arm == Arm::Left && !f.Stowed());
	}
}

}   // namespace

int main() {
	Follows();
	Ticks();
	Mirror();
	if (g_fails) {
		std::printf("shield_arm_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("shield_arm_test: weapon and shield on opposite arms (every way the weapon changes hands, both hands on it, a sword let go, no shield), the face axes from the bind pose, the right forearm's mirrored strap and its handedness: all passed\n");
	return 0;
}
