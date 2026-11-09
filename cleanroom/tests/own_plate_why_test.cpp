// vr/own_plate_why.h: which monster plate the log says is in use (ours or the game's) and why, per view and preset.
//
//   own_plate_why_test   exit code 0 = every case passed

#include "../../vr/own_plate_why.h"

#include <cstdio>
#include <cstring>

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

using namespace ownplatewhy;

// everything in place, VR F4
In Ready() {
    In s;
    s.own = s.allViews = s.firstPerson = s.vrOurs = s.stereo = true;
    s.dropGame = s.drawHook = s.textHook = s.effectReady = s.layerOut = s.namesOn = true;
    return s;
}
}  // namespace

int main() {
    In s = Ready();
    CHECK(Of(s) == kOurs);
    CHECK(!GamePlateDrawn(Of(s)));

    // F2 / F3: wanted only with the all-views key and a stereo pair
    s.firstPerson = false;
    CHECK(Of(s) == kOurs);
    s.allViews = false;
    CHECK(Of(s) == kF4Only);
    s.allViews = true;
    s.stereo = false;
    CHECK(Of(s) == kNoStereo);
    // ... F4 does not need the all-views key
    s = Ready();
    s.allViews = false;
    CHECK(Of(s) == kOurs);

    // the reasons in order: the key, our view
    s = Ready(); s.own = false; CHECK(Of(s) == kOff); CHECK(GamePlateDrawn(Of(s)));
    s = Ready(); s.vrOurs = false; CHECK(Of(s) == kNotOurView);
    // wanted, but a piece missing
    s = Ready(); s.dropGame = false; CHECK(Of(s) == kNoDrop); CHECK(GamePlateDrawn(Of(s)));
    s = Ready(); s.drawHook = false; CHECK(Of(s) == kNoDrawHook); CHECK(GamePlateDrawn(Of(s)));
    // ... the game's plate hidden all the same once the Draw hook is in, whether or not ours reached the picture (Serg: "просто прятать этот слой")
    s = Ready(); s.effectReady = false; s.layerOut = false; CHECK(Of(s) == kNoEffect); CHECK(!GamePlateDrawn(Of(s)));
    s = Ready(); s.layerOut = false; CHECK(Of(s) == kNoLayer); CHECK(!GamePlateDrawn(Of(s)));
    s = Ready(); s.namesOn = false; CHECK(Of(s) == kNotYet); CHECK(!GamePlateDrawn(Of(s)));
    // F2 / F3 all the same, any preset (the preset is not an input: its stages act on the game's passes, not on the plate)
    s = Ready(); s.firstPerson = false; s.drawHook = true; CHECK(Of(s) == kOurs); CHECK(!GamePlateDrawn(Of(s)));
    s = Ready(); s.textHook = false; CHECK(Of(s) == kNoTextHook); CHECK(!GamePlateDrawn(Of(s)));

    // every reason has its own text, the preset its name
    for (int w = kOurs; w <= kNotYet; ++w) CHECK(std::strcmp(Text((Why)w), "?") != 0);
    CHECK(std::strcmp(StagesName(2), "Potato") == 0);
    CHECK(std::strcmp(StagesName(1), "Low") == 0);
    CHECK(std::strcmp(StagesName(0), "Potato") != 0);

    std::printf("own_plate_why_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
