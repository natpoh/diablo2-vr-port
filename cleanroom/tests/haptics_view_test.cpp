// F4 only for the controller pulses (vr/haptics.cpp, vr/view_gate.h): no pulse is posted, kept or played in another view,
// whichever way it comes (a named event, a pulse of a gesture's, the bow's ratchet, directinput::Haptic), and what waits when
// the view changes is dropped, not played late. No game, no headset: haptics.cpp with the output played by this test.
//
//   haptics_view_test       exit code 0 = all passed

#include "haptics.h"
#include "direct_input.h"
#include "view_gate.h"

#include <windows.h>

#include <cstdio>

namespace {

int g_fails = 0, g_checks = 0;
#define EXPECT(c, ...)                                         \
	do {                                                       \
		++g_checks;                                            \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
			std::printf(__VA_ARGS__);                          \
			std::printf("\n");                                 \
		}                                                      \
	} while (0)

bool g_f4 = true;
bool TheView() { return g_f4; }

int g_played = 0, g_lastHand = -1;
bool Apply(int hand, float, int) { ++g_played; g_lastHand = hand; return true; }

// A frame's flush on the real clock (a pulse older than 150 ms is a ghost): what waits is played (or dropped) and counted.
int Flush(bool canPlay = true) {
	g_played = 0;
	haptics::Flush(GetTickCount64(), canPlay, &Apply);
	Sleep(40);   // (the limiter wants 30 ms between starts on a hand)
	return g_played;
}

}  // namespace

int main() {
	viewgate::Set(&TheView);
	haptics::Configure(true, 1.0f);

	// F4: every way of asking is felt
	g_f4 = true;
	haptics::Play(haptics::Event::Interact);
	EXPECT(Flush() >= 1, "F4: a named event is played");
	haptics::Pulse(haptics::kLeft, 0.5f, 40);
	EXPECT(Flush() == 1 && g_lastHand == haptics::kLeft, "F4: a pulse of a gesture's is played in its hand");
	directinput::Haptic(1, 0.5f, 40.0f);
	EXPECT(Flush() == 1, "F4: directinput::Haptic reaches the motor");
	haptics::BowDraw(0.0f);
	haptics::BowDraw(0.9f);
	EXPECT(Flush() >= 1, "F4: the bow's ratchet ticks");

	// another view: nothing is posted, so nothing is played
	g_f4 = false;
	haptics::Play(haptics::Event::Hit);
	haptics::Play(haptics::Event::MenuClick, haptics::kLeft);
	haptics::Pulse(haptics::kRight, 0.7f, 40);
	directinput::Haptic(0, 0.7f, 40.0f);
	haptics::Sink(0, 0.7f, 40.0f, 0.0f);
	haptics::BowDraw(0.0f);
	haptics::BowDraw(0.9f);
	EXPECT(Flush() == 0, "another view: no pulse of any kind");

	// the view changes while a pulse waits: dropped at the flush, not played late
	g_f4 = true;
	haptics::Pulse(haptics::kRight, 0.7f, 40);
	g_f4 = false;
	EXPECT(Flush() == 0, "F4 -> another view with a pulse waiting: it is dropped");
	g_f4 = true;
	EXPECT(Flush() == 0, "... and does not come back with the view");

	// back in F4: as before
	haptics::Play(haptics::Event::Interact);
	EXPECT(Flush() >= 1, "back in F4: events are played again");

	haptics::Configure(false, 1.0f);
	viewgate::Set(nullptr);
	std::printf("haptics_view_test: %d checks, %d failed\n", g_checks, g_fails);
	return g_fails ? 1 : 0;
}
