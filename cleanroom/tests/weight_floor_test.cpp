// Offline checks of the floor a let-go weapon falls to (vr/weight_floor.h): the head's height over the game's ground from the camera's eye and the
// hero's ground in world units, and that floor in the room. No game, no headset.  Run: build/Release/weight_floor_test.exe

#include "../../vr/weight_floor.h"

#include <cmath>
#include <cstdio>

using namespace weightfloor;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

int main() {
    const float nan = std::nanf("");
    // the units: ViewHeight world units over a user eye height of 1.6 m: 3.3 units per metre; the eye 5.28 units over the ground = 1.6 m
    {
        const float eyeM = 1.6f, upm = 3.3f, view = eyeM * upm;
        Expect(Near(HeadAboveGroundM(view, 0.0f, upm, eyeM), 1.6, 1e-4), "eye = ViewHeight over the ground: the user's eye height");
        Expect(Near(HeadAboveGroundM(12.0f + view, 12.0f, upm, eyeM), 1.6, 1e-3), "the ground need not be at 0 (the hero's own height in the world)");
        Expect(Near(HeadAboveGroundM(view + 0.7f, 0.0f, upm, eyeM), 1.6 + 0.7 / 3.3, 1e-4), "a jump's lift: the head is higher over the ground");
        Expect(Near(HeadAboveGroundM(view - 0.5f, 0.0f, upm, eyeM), 1.6 - 0.5 / 3.3, 1e-4), "the head ducked in native OpenXR (travel from the recentre): lower");
        // a hero whose eyes are 5.3 units over the ground for a 1.7 m player (2026-10: a sorceress, 5.3 units)
        Expect(Near(HeadAboveGroundM(5.3f, 0.0f, 5.3f / 1.7f, 1.7f), 1.7, 1e-4), "5.3 units for a 1.7 m player");
    }
    // not usable: the fallback (the user's standing eye height)
    {
        Expect(HeadAboveGroundM(nan, 0.0f, 3.3f, 1.6f) == 1.6f, "no eye yet: the fallback");
        Expect(HeadAboveGroundM(5.0f, nan, 3.3f, 1.6f) == 1.6f, "no ground yet: the fallback");
        Expect(HeadAboveGroundM(5.0f, 0.0f, 0.0f, 1.6f) == 1.6f, "no scale: the fallback");
        Expect(HeadAboveGroundM(5.0f, 0.0f, -3.3f, 1.6f) == 1.6f, "a negative scale: the fallback");
        Expect(HeadAboveGroundM(5.0f, 0.0f, 300.0f, 1.6f) == 1.6f, "a scale of 300: the fallback");
        Expect(HeadAboveGroundM(0.2f, 0.0f, 3.3f, 1.6f) == 1.6f, "6 cm over the ground: not a head");
        Expect(HeadAboveGroundM(900.0f, 0.0f, 3.3f, 1.6f) == 1.6f, "270 m over the ground: not a head");
        Expect(HeadAboveGroundM(-5.0f, 0.0f, 3.3f, 1.6f) == 1.6f, "an eye under the ground: the fallback");
    }
    // the floor in the room
    {
        // standing, the room's floor is the ground: head 1.6 m over the room's floor and 1.6 m over the ground: floor 0
        Expect(Near(RoomFloorY(1.6, 1.6), 0.0, 1e-9), "standing as the setting says: the room's floor");
        // a player 30 cm shorter than the setting: head 1.3 m over the room's floor, the ground 1.6 m below the head: 30 cm UNDER the room's floor
        Expect(Near(RoomFloorY(1.3, 1.6), -0.3, 1e-9), "a shorter player: the ground is under the room's floor (a weapon dropped to y = 0 lay 30 cm over it)");
        // seated at 1.1 m: the visible ground is 50 cm under the room's floor
        Expect(Near(RoomFloorY(1.1, 1.6), -0.5, 1e-9), "seated: the ground is far under the room's floor");
        // a taller player: the ground is over the room's floor
        Expect(Near(RoomFloorY(1.9, 1.6), 0.3, 1e-9), "a taller player");
        // unknown: the old floor
        Expect(RoomFloorY(1.6, 0.0) == 0.0 && RoomFloorY(1.6, -1.0) == 0.0 && RoomFloorY(1.6, std::nan("")) == 0.0 && RoomFloorY(std::nan(""), 1.6) == 0.0,
               "no reading: the room's floor, as before");
    }
    if (g_failures) {
        std::printf("%d FAILED\n", g_failures);
        return 1;
    }
    std::printf("weight_floor_test: all passed\n");
    return 0;
}
