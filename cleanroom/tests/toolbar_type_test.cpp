// The toolbar's type (shared/toolbar_type.h): one choice over [hud] hide and [hud] bar_place.
//
//   toolbar_type_test        exit code 0 = every case passed

#include "../../shared/toolbar_type.h"

#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
int g_fails = 0;
int g_checks = 0;
#define CHECK(c)                                               \
	do {                                                       \
		++g_checks;                                            \
		if (!(c)) {                                            \
			++g_fails;                                         \
			std::printf("  FAIL line %d: %s\n", __LINE__, #c); \
		}                                                      \
	} while (0)
}  // namespace

int main() {
	using namespace toolbartype;

	// An ini reads as the type it is, for every hide and every place it can hold.
	for (int place = 0; place <= 5; ++place) CHECK(TypeOf(0, place) == kPicture);   // hide Nothing: the strip stays in the picture
	for (int hide = 1; hide <= 2; ++hide) {
		CHECK(TypeOf(hide, 5) == kWrists && TypeOf(hide, 4) == kChest && TypeOf(hide, 3) == kLowFront);
		CHECK(TypeOf(hide, 1) == kLeftForearm && TypeOf(hide, 2) == kRightForearm && TypeOf(hide, 0) == kNotShown);
	}
	CHECK(TypeOf(9, 99) == kWrists && TypeOf(-3, 4) == kPicture && TypeOf(1, -1) == kNotShown);   // a key out of range is clamped first

	// Choosing a type and reading it back is the same type, from any start.
	for (int hide = 0; hide <= 2; ++hide)
		for (int place = 0; place <= 5; ++place)
			for (int type = 0; type < kCount; ++type) {
				int h = hide, p = place;
				Choose(type, &h, &p);
				CHECK(TypeOf(h, p) == type);
			}

	// A place always works: hide is lifted from Nothing to the toolbar, and a hide of "all of it" stays.
	{
		int h = 0, p = 3;
		Choose(kWrists, &h, &p);
		CHECK(h == 1 && p == 5);
		h = 2; p = 0;
		Choose(kChest, &h, &p);
		CHECK(h == 2 && p == 4);
		h = 1; p = 5;
		Choose(kNotShown, &h, &p);
		CHECK(h == 1 && p == 0);
	}
	// "In the picture" takes hide to 0 and keeps the place, so going back to a place finds the one chosen before.
	{
		int h = 1, p = 4;
		Choose(kPicture, &h, &p);
		CHECK(h == 0 && p == 4);
		Choose(kWrists, &h, &p);
		CHECK(h == 1 && p == 5);
		h = 2; p = 3;
		Choose(kPicture, &h, &p);
		CHECK(h == 0 && p == 3);
	}
	// The choice is clamped, and so is a key that is out of range.
	{
		int h = 7, p = -4;
		Choose(99, &h, &p);   // the last type
		CHECK(h == 0 && p == 0);
		h = 1; p = 2;
		Choose(-5, &h, &p);   // the first
		CHECK(h == 1 && p == 5);
	}

	// The headset panel's stepper: through the types in order, stopping at both ends, 0 steps nothing.
	{
		int h = 1, p = 5;
		for (int want = 1; want < kCount; ++want) {
			Step(1, &h, &p);
			CHECK(TypeOf(h, p) == want);
		}
		Step(1, &h, &p);   // past the last
		CHECK(TypeOf(h, p) == kPicture && h == 0);
		for (int want = kCount - 2; want >= 0; --want) {
			Step(-1, &h, &p);
			CHECK(TypeOf(h, p) == want);
		}
		Step(-1, &h, &p);  // before the first
		CHECK(TypeOf(h, p) == kWrists);
		const int h0 = h, p0 = p;
		Step(0, &h, &p);   // nothing
		CHECK(h == h0 && p == p0);
		// from an ini that Nothing was set in: the next one hangs the toolbar
		h = 0; p = 5;
		Step(-1, &h, &p);
		CHECK(TypeOf(h, p) == kNotShown && h == 1);
	}

	// The names: one for each type, the short ones fit a value box (about 15 characters), none empty, none the same.
	for (int a = 0; a < kCount; ++a) {
		CHECK(kShort[a] && kLongW[a] && std::strlen(kShort[a]) > 0 && std::strlen(kShort[a]) <= 15 && std::wcslen(kLongW[a]) > 0);
		for (int b = a + 1; b < kCount; ++b) CHECK(std::strcmp(kShort[a], kShort[b]) != 0 && std::wcscmp(kLongW[a], kLongW[b]) != 0);
	}

	std::printf("toolbar_type_test: %d checks, %d failed\n", g_checks, g_fails);
	return g_fails == 0 ? 0 : 1;
}
