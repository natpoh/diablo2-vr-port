// vr/view_gate.h: the one predicate behind "F4 only" - nothing registered is F4 (the tests of every module), a registered
// function is asked every time (no stale copy), and an Edge tells each change of the view once.
//
//   view_gate_test       exit code 0 = all passed

#include "view_gate.h"

#include <cstdio>
#include <thread>

static int g_failed = 0, g_checks = 0;
#define CHECK(c)                                                                  \
	do {                                                                          \
		++g_checks;                                                               \
		if (!(c)) { ++g_failed; std::printf("FAIL line %d: %s\n", __LINE__, #c); } \
	} while (0)

static bool g_f4 = true;
static bool TheView() { return g_f4; }

int main() {
	CHECK(viewgate::F4());   // nothing registered: F4 (a module is tested as it acts in F4)
	viewgate::Set(&TheView);
	CHECK(viewgate::F4());
	g_f4 = false;
	CHECK(!viewgate::F4());   // asked every time, not remembered
	g_f4 = true;
	CHECK(viewgate::F4());

	// another thread sees the change at once (the game's threads ask it too)
	g_f4 = false;
	bool seen = true;
	std::thread([&] { seen = viewgate::F4(); }).join();
	CHECK(!seen);
	g_f4 = true;

	viewgate::Set(nullptr);
	g_f4 = false;
	CHECK(viewgate::F4());   // unregistered again: F4

	// the Edge: the first call is never a change; then each change once
	viewgate::Edge e;
	CHECK(!e.Step(true));
	CHECK(!e.Step(true));
	CHECK(e.Step(false) && !e.Last());
	CHECK(!e.Step(false));
	CHECK(e.Step(true) && e.Last());
	viewgate::Edge started;
	CHECK(!started.Step(false));   // a module that starts in another view has nothing under way
	CHECK(started.Step(true));

	std::printf("view_gate_test: %d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
