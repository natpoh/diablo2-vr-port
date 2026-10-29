// The controller a landed blow buzzes, for every source of a blow: vr/hit_claim.h (who asked for it) through haptics::HitHand (the hand) to the controller the pulse is played in.
// No game, no headset: the sources are what the plugin does at the moment it asks the game to attack (the press) and the hit observer is what the game's server thread does when the
// blow lands. The whole line is one function here, Buzzed(), the way vrcam's hit-hand source is: hitclaim::Take -> HitHand(claim, weight's carrying hand, weight placed) -> Expand(Hit).
//
//   hit_claim_test

#include "../../vr/hit_claim.h"
#include "../../vr/haptics_pattern.h"
#include "../../vr/shield_smite.h"

#include <cstdio>
#include <thread>
#include <vector>

namespace {

int g_fails = 0;

#define EXPECT(c, ...)                                         \
	do {                                                       \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

using haptics::kLeft;
using haptics::kRight;

// The weight model's answers at the moment of the blow (weight::CarryingHand / Placed): skeleton side of the hand that carries the weapon (0 right, 1 left, -1 none), and whether it is placed.
struct Weight {
	int carrying = 0;
	bool placed = true;
};

// What the hit observer does when a player's blow lands at `nowMs`: the controller the pulse is played in.
int Buzzed(int64_t nowMs, Weight w) {
	const int hand = haptics::HitHand(hitclaim::Take(nowMs), w.carrying, w.placed);
	haptics::Resolved r[2];
	return haptics::Expand(haptics::Event::Hit, hand, r) == 1 ? r[0].hand : -1;
}

// The two sources that press: hit by touch (the controller whose blade touched the monster) and the shield's bash (the arm that wears the shield, [hands] shoulder_draw's follower or none).
void Touch(int hand, int64_t nowMs) { hitclaim::Press(hand, nowMs); }
void Bash(int followerArm, int64_t nowMs) { hitclaim::Press(shieldsmite::BashSide(followerArm), nowMs); }

}   // namespace

int main() {
	const Weight swordRight{0, true}, swordLeft{1, true}, none{-1, false};

	// the ledger itself
	{
		hitclaim::Claims c;
		EXPECT(c.Take(1000) == -1, "nothing asked: nobody's blow");
		c.Press(1, 1000);
		EXPECT(c.Take(1040) == 1, "the left hand asked, the blow 40 ms later is the left hand's");
		EXPECT(c.Take(1100) == 1, "...and another roll of the same swing 60 ms after that");
		EXPECT(c.Take(1161) == -1, "...but not one 121 ms after the first: the swing is over");
		c.Press(0, 2000);
		EXPECT(c.Take(2000 + hitclaim::kWindowMs) == 0, "a claim is good for its whole window");
		c.Press(0, 3000);
		EXPECT(c.Take(3000 + hitclaim::kWindowMs + 1) == -1, "...and no longer: the game refused the attack, the next blow is nobody's");
		c.Press(1, 4000);
		c.Clear();
		EXPECT(c.Take(4010) == -1, "cleared: nothing");
		c.Press(1, 0);
		EXPECT(c.Take(1) == -1, "a clock that reads 0 claims nothing");
		c.Press(1, 5000);
		c.Press(0, 5000 + 100);
		EXPECT(c.Take(5040) == 1 && c.Take(5140) == 0, "two hands that swing within a few frames: each blow goes to the one that asked first that has not had its blow yet");
		EXPECT(c.Take(5150) == 0, "...and the second roll of the right hand's swing is the right hand's, not the left's whose swing is over");
		// more claims than slots: the oldest is dropped, nothing breaks
		hitclaim::Claims many;
		for (int i = 0; i < 9; ++i) many.Press(i & 1, 10000 + i);
		int taken = 0;
		for (int i = 0; i < 9; ++i) taken += many.Take(10010 + i) >= 0 ? 1 : 0;
		EXPECT(taken == 9, "a flood of presses and blows is answered (the later ones fall into the grace of the last)");
	}

	// ---- every source of a blow ----------------------------------------------------------------------------------------------------------------------------------------
	hitclaim::Clear();
	// 1. hit by touch with the weapon in the right hand: the right controller (as ever)
	Touch(0, 1000);
	EXPECT(Buzzed(1040, swordRight) == kRight, "touch, the right hand's blade: the RIGHT controller");
	// 2. hit by touch with a Barbarian's second blade in the LEFT hand while the weight model carries the primary in the right: the LEFT controller (the reported bug)
	hitclaim::Clear();
	Touch(1, 2000);
	EXPECT(Buzzed(2040, swordRight) == kLeft, "touch, the left hand's blade (dual wield; the weight model carries the right one): the LEFT controller");
	// 3. the same Barbarian strikes with both blades in turn
	hitclaim::Clear();
	Touch(1, 3000);
	EXPECT(Buzzed(3030, swordRight) == kLeft, "dual wield, the left blade first: left");
	Touch(0, 3100);
	EXPECT(Buzzed(3135, swordRight) == kRight, "...then the right blade: right");
	Touch(1, 3300);
	Touch(0, 3320);
	EXPECT(Buzzed(3340, swordRight) == kLeft && Buzzed(3360, swordRight) == kRight, "both blades within 20 ms: the left asked first, the right second - each its own buzz");
	// 4. a weapon drawn into the left hand ([hands] shoulder_draw): touch with that hand, and the game's own attack button with nobody's claim
	hitclaim::Clear();
	Touch(1, 4000);
	EXPECT(Buzzed(4040, swordLeft) == kLeft, "a weapon drawn into the left hand, hit by touch: left");
	EXPECT(Buzzed(4500, swordLeft) == kLeft, "...and with the game's own attack button (nobody's claim): the hand that holds the weapon - left");
	EXPECT(Buzzed(4600, swordRight) == kRight, "...and the weapon back in the right hand: right");
	// 5. the shield bash, [hands] shoulder_draw OFF (the follower is not handled, -1): the shield is on the left arm, the weapon in the right hand
	hitclaim::Clear();
	Bash(-1, 5000);
	EXPECT(Buzzed(5040, swordRight) == kLeft, "a shield bash with shoulder_draw off: the LEFT controller (the reported bug)");
	// ... with the follower on, the shield on the left arm / the right arm (after the swap, the sword in the left hand)
	hitclaim::Clear();
	Bash(1, 5200);
	EXPECT(Buzzed(5240, swordRight) == kLeft, "a bash, the shield on the left arm: left");
	hitclaim::Clear();
	Bash(0, 5400);
	EXPECT(Buzzed(5440, swordLeft) == kRight, "a bash, the shield on the right arm, the sword in the left hand: the RIGHT controller");
	// ... the bash with the weight model empty (a weapon let go) still buzzes the shield's arm
	hitclaim::Clear();
	Bash(-1, 5600);
	EXPECT(Buzzed(5640, none) == kLeft, "a bash with no weapon in hand: left");
	// 6. a sword swing right after a bash, outside the bash's window: the sword's hand
	hitclaim::Clear();
	Bash(-1, 6000);
	EXPECT(Buzzed(6040, swordRight) == kLeft, "the bash lands: left");
	EXPECT(Buzzed(6400, swordRight) == kRight, "a blow of the sword 360 ms later, nobody's claim: the right hand that holds it");
	// 7. a thrown weapon (javelin, throwing knife) thrown by gesture from the right hand: no press of ours claims it, the weapon is in the air (not placed): the event's own, the right
	hitclaim::Clear();
	EXPECT(Buzzed(7000, none) == kRight, "a thrown weapon lands, thrown from the right hand: the RIGHT controller");
	// ... thrown by touch at a point-blank monster from the right hand: its claim
	Touch(0, 7500);
	EXPECT(Buzzed(7540, none) == kRight, "a point-blank throw by touch: the hand that swung it");
	// 8. the game's own attack with no weapon of ours: the event's own
	hitclaim::Clear();
	EXPECT(Buzzed(8000, none) == kRight, "no claim, no weapon placed: the right controller (the event's own)");
	// 9. a refused press does not steal the next, later blow
	hitclaim::Clear();
	Touch(1, 9000);   // (the game refused it: no blow comes)
	EXPECT(Buzzed(9000 + hitclaim::kWindowMs + 50, swordRight) == kRight, "an unanswered claim is gone after its window: the next blow is the weapon's");

	// ---- the observer's thread and the presses' threads at once: nothing but a side or -1 comes out, nothing hangs ----------------------------------------------------------------
	{
		hitclaim::Claims c;
		std::vector<int> seen(4, 0);
		std::thread presser([&] { for (int i = 0; i < 20000; ++i) c.Press(i & 1, 100000 + i / 4); });
		std::thread taker([&] {
			for (int i = 0; i < 20000; ++i) {
				const int s = c.Take(100000 + i / 4);
				++seen[(s + 1) & 3];
			}
		});
		presser.join();
		taker.join();
		EXPECT(seen[0] + seen[1] + seen[2] == 20000 && seen[3] == 0, "the observer's answers are a side (0 or 1) or -1");
	}

	if (g_fails) {
		std::printf("hit_claim_test: %d FAILED\n", g_fails);
		return 1;
	}
	std::printf("hit_claim_test: every source of a blow (touch with either hand, a Barbarian's two blades, a weapon in the left hand, the shield bash with the follower off / left / right, a thrown weapon, the game's own attack) buzzes the right controller: all passed\n");
	return 0;
}
