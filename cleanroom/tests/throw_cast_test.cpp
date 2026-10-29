// vr/throw_cast.h on its own: the pad slot that holds Throw, the pad press of a slot frame by frame, and the watch on the hero's mode after
// a press. No game, no headset.
//   throw_cast_test             exit code 0 = every check passed

#include "../../vr/throw_cast.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using namespace throwcast;

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

wheel::Snapshot Snap(uint64_t at = 1000) {
	wheel::Snapshot s;
	s.ready = true;
	s.at = at;
	return s;
}
void Put(wheel::Snapshot& s, int slot, int32_t id, bool available = true) {
	s.slot[slot].id = id;
	s.slot[slot].available = available;
	s.slot[slot].known = true;
}

void TestFindSlot() {
	std::printf("the pad slot that holds Throw\n");
	{
		wheel::Snapshot s = Snap();
		const Pick p = FindSlot(s, 1500);
		EXPECT(p.slot < 0, "an empty table");
	}
	{
		wheel::Snapshot s = Snap();
		Put(s, 1, kSkillThrow);   // X
		const Pick p = FindSlot(s, 1500);
		EXPECT(p.slot == 1 && p.button == wheel::kPadX && !p.alt && std::strcmp(p.label, "X") == 0, "slot %d button %04X", p.slot, p.button);
	}
	{
		// B and X both: the first of the order
		wheel::Snapshot s = Snap();
		Put(s, 2, kSkillThrow);   // Y
		Put(s, 0, kSkillThrow);   // B
		EXPECT(FindSlot(s, 1500).slot == 0, "B before Y");
		// the second row after the first
		wheel::Snapshot t = Snap();
		Put(t, 5, kSkillThrow);   // LT + X
		Put(t, 3, kSkillThrow);   // RB
		EXPECT(FindSlot(t, 1500).slot == 3, "RB before LT + X");
		wheel::Snapshot u = Snap();
		Put(u, 5, kSkillThrow);
		const Pick p = FindSlot(u, 1500);
		EXPECT(p.slot == 5 && p.alt && p.button == wheel::kPadX, "LT + X: slot %d alt %d", p.slot, p.alt);
	}
	{
		// LT + A is a pad cast of its own; A and RT are not
		wheel::Snapshot s = Snap();
		Put(s, 10, kSkillThrow);
		const Pick p = FindSlot(s, 1500);
		EXPECT(p.slot == 10 && p.alt && p.button == wheel::kPadA, "LT + A: slot %d", p.slot);
		wheel::Snapshot a = Snap();
		Put(a, 8, kSkillThrow);   // A: the layout's attack, not a button to press for another skill
		Put(a, 9, kSkillThrow);   // RT: not castable from here
		Put(a, 11, kSkillThrow);
		EXPECT(FindSlot(a, 1500).slot < 0, "A and RT are no slots");
	}
	{
		// other skills, a Throw the player does not have now, a stale or unread snapshot
		wheel::Snapshot s = Snap();
		Put(s, 0, 0);
		Put(s, 1, 4);    // Left Hand Throw is not Throw
		Put(s, 2, kSkillThrow, false);
		EXPECT(FindSlot(s, 1500).slot < 0, "nothing usable");
		Put(s, 3, kSkillThrow);
		EXPECT(FindSlot(s, 1500).slot == 3, "the available one");
		EXPECT(FindSlot(s, 1000 + 3001).slot < 0, "a snapshot 3 s old is stale");
		EXPECT(FindSlot(s, 1000 + 2999).slot == 3, "2.999 s old is not");
		EXPECT(FindSlot(s, 500).slot < 0, "a snapshot from the future");
		s.ready = false;
		EXPECT(FindSlot(s, 1500).slot < 0, "not ready");
	}
}

void TestSwap() {
	std::printf("the swap of the slot's skill\n");
	// Attack, at a cast site, on, a thrown weapon, a live throw: Throw
	EXPECT(SwapSkill(0, true, true, true, true) == kSkillThrow, "the swap");
	// each condition alone is enough to leave it
	EXPECT(SwapSkill(0, false, true, true, true) == 0, "not a cast site (the interface drawing the slots)");
	EXPECT(SwapSkill(0, true, false, true, true) == 0, "switched off");
	EXPECT(SwapSkill(0, true, true, false, true) == 0, "no thrown weapon in hand");
	EXPECT(SwapSkill(0, true, true, true, false) == 0, "no throw live");
	// any other binding stays: Throw itself, the player's own skills, the special ones
	for (int bound : {-1, 1, 2, 4, 36, 54, 97, 357, 359, 0x167}) EXPECT(SwapSkill(bound, true, true, true, true) == bound, "binding %d", bound);
}

void TestPress() {
	std::printf("the pad press of a slot\n");
	{   // a first-row slot: the button at once, held 0.12 s
		Press pr;
		Pick p;
		p.slot = 1; p.button = wheel::kPadX; p.alt = false;
		pr.Start(10.0, p);
		const Press::Out a = pr.Step(10.0);
		EXPECT(a.active && a.buttons == wheel::kPadX && a.lt == 0, "at once");
		const Press::Out b = pr.Step(10.119);
		EXPECT(b.active && b.buttons == wheel::kPadX, "still down at 0.119 s");
		const Press::Out c = pr.Step(10.121);
		EXPECT(!c.active && c.buttons == 0 && !pr.Active(), "over after 0.12 s");
		EXPECT(!pr.Step(10.2).active, "stays over");
	}
	{   // the second row: the left trigger alone for 0.09 s, then the button
		Press pr;
		Pick p;
		p.slot = 5; p.button = wheel::kPadX; p.alt = true;
		pr.Start(20.0, p);
		const Press::Out a = pr.Step(20.0);
		EXPECT(a.active && a.lt == 255 && a.buttons == 0, "LT alone first");
		const Press::Out b = pr.Step(20.089);
		EXPECT(b.lt == 255 && b.buttons == 0, "still LT alone at 0.089 s");
		const Press::Out c = pr.Step(20.091);
		EXPECT(c.lt == 255 && c.buttons == wheel::kPadX, "the button after the lead");
		const Press::Out d = pr.Step(20.0 + Press::kLeadS + Press::kMinPressS - 0.001);
		EXPECT(d.active && d.buttons == wheel::kPadX, "held to the end");
		EXPECT(!pr.Step(20.0 + Press::kLeadS + Press::kMinPressS + 0.001).active, "over");
	}
	{   // nothing to press, restart, stop
		Press pr;
		Pick none;
		pr.Start(1.0, none);
		EXPECT(!pr.Active() && !pr.Step(1.0).active, "no slot, no press");
		Pick p; p.slot = 0; p.button = wheel::kPadB;
		pr.Start(5.0, p);
		pr.Stop();
		EXPECT(!pr.Step(5.01).active, "stopped");
		pr.Start(6.0, p);
		EXPECT(pr.Step(6.01).active, "started again");
	}
}

void TestAnswer() {
	std::printf("what the game answered\n");
	EXPECT(std::strcmp(ModeName(11), "throw") == 0 && std::strcmp(ModeName(7), "attack 1") == 0 && std::strcmp(ModeName(1), "neutral") == 0, "names");
	EXPECT(IsActionMode(11) && IsActionMode(7) && IsActionMode(8) && IsActionMode(10) && IsActionMode(13) && !IsActionMode(1) && !IsActionMode(2) && !IsActionMode(3) && !IsActionMode(4), "action modes");
	{   // standing, the press, the hero throws 90 ms later
		AnswerWatch w;
		w.Arm(100.0, true, 1);
		EXPECT(w.Poll(100.01, true, 1).kind == AnswerWatch::Kind::None, "nothing yet");
		EXPECT(w.Poll(100.05, true, 1).kind == AnswerWatch::Kind::None, "nothing yet");
		const AnswerWatch::Event e = w.Poll(100.09, true, 11);
		EXPECT(e.kind == AnswerWatch::Kind::Accepted && e.mode == 11 && std::fabs(e.afterS - 0.09) < 1e-6 && e.before == 1, "accepted");
		EXPECT(!w.Armed() && w.Poll(101.0, true, 1).kind == AnswerWatch::Kind::None, "one answer");
	}
	{   // walking, the press, the hero stays walking: no answer in 0.8 s
		AnswerWatch w;
		w.Arm(50.0, true, 2);
		AnswerWatch::Event e;
		for (double t = 50.0; t < 50.79; t += 0.011) { e = w.Poll(t, true, 2); EXPECT(e.kind == AnswerWatch::Kind::None, "early at %.3f", t); }
		e = w.Poll(50.81, true, 2);
		EXPECT(e.kind == AnswerWatch::Kind::NoAnswer && e.mode == 2 && e.before == 2, "kind %d mode %u", (int)e.kind, e.mode);
	}
	{   // still in the animation of the last throw: not an answer, until he has been out of it
		AnswerWatch w;
		w.Arm(10.0, true, 11);
		EXPECT(w.Poll(10.1, true, 11).kind == AnswerWatch::Kind::None, "the old animation is no answer");
		EXPECT(w.Poll(10.3, true, 1).kind == AnswerWatch::Kind::None, "out of it");
		EXPECT(w.Poll(10.4, true, 11).kind == AnswerWatch::Kind::Accepted, "and in again: an answer");
	}
	{   // never readable
		AnswerWatch w;
		w.Arm(5.0, false, 0);
		AnswerWatch::Event e;
		for (double t = 5.0; t < 5.79; t += 0.011) w.Poll(t, false, 0);
		e = w.Poll(5.9, false, 0);
		EXPECT(e.kind == AnswerWatch::Kind::Unreadable, "kind %d", (int)e.kind);
	}
	{   // readable only later
		AnswerWatch w;
		w.Arm(5.0, false, 0);
		w.Poll(5.2, true, 1);
		const AnswerWatch::Event e = w.Poll(5.9, true, 1);
		EXPECT(e.kind == AnswerWatch::Kind::NoAnswer, "kind %d", (int)e.kind);
	}
	{   // attack mode counts as an answer too (the Attack skill): cast and kick-like skills as well
		for (uint32_t m : {7u, 8u, 10u, 13u, 16u}) {
			AnswerWatch w;
			w.Arm(0.0, true, 1);
			EXPECT(w.Poll(0.05, true, m).kind == AnswerWatch::Kind::Accepted, "mode %u", m);
		}
		AnswerWatch w;
		w.Arm(0.0, true, 1);
		EXPECT(w.Poll(0.05, true, 4).kind == AnswerWatch::Kind::None, "being hit is no answer");
	}
}

}  // namespace

int main() {
	TestFindSlot();
	TestSwap();
	TestPress();
	TestAnswer();
	std::printf("\n%d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
