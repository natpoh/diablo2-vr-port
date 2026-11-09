// sky_time_test - [sky] game_sky's baked pictures by the game's own lighting's time of day (vr/sky/three_skies.h VisShares), not the
// world clock. Serg on 0.208.2 (act 2 town, the clock at "end of dusk, night", its light 128): "в игре день а небо темное".
//   1. the game's lighting as it is made (day, 0.00 into it: the day file and half of dawn2): the day picture whole;
//   2. each lighting file by its own light (kVisPic): dawn1 / night the night picture, dawn2 / dusk1 / dusk2 the dawn / dusk one, day the
//      day's; at its middle the same blended or whole;
//   3. off the middle the game's own weights: the period's file 1, the neighbour |fraction - 0.5|, wrapping at both ends; they add to 1;
//   4. out of range or NaN (not read, another layout): the day's.
#include <cmath>
#include <cstdio>

#include "sky/fog_times.h"
#include "sky/three_skies.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace threeskies;

static bool Is(const Shares& s, float day, float night, float dd) {
    return std::fabs(s.pic[kDay] - day) < 1e-5f && std::fabs(s.pic[kNight] - night) < 1e-5f && std::fabs(s.pic[kDawnDusk] - dd) < 1e-5f;
}
static float Sum(const Shares& s) { return s.pic[kDay] + s.pic[kNight] + s.pic[kDawnDusk]; }

int main() {
    // 1. as the game makes its lighting (VisualDataSystem +0x190 = 2, +0x194 = 0.0) and leaves it: day
    CHECK(Is(VisShares(2, 0.0f, false), 1, 0, 0));   // (the dome: one picture whole - the day's)
    CHECK(Is(VisShares(2, 0.0f, true), 2.0f / 3.0f, 0, 1.0f / 3.0f));   // (blended: day 1, dawn2 0.5)
    // 2. each period's middle: its own picture whole, by the files' own light (kVisPic): dawn1 and night the night's, dawn2 / dusk1 /
    //    dusk2 the dawn / dusk picture, day the day's - dusk2 ("end of dusk, start of night", as bright as dawn2) no longer the night's
    const float want[6][3] = {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, 0, 1}, {0, 0, 1}, {0, 1, 0}};
    for (int p = 0; p < 6; ++p) {
        CHECK(Is(VisShares(p, 0.5f, true), want[p][0], want[p][1], want[p][2]));
        CHECK(Is(VisShares(p, 0.5f, false), want[p][0], want[p][1], want[p][2]));
        CHECK(Is(VisShares(p, 0.1f, false), want[p][0], want[p][1], want[p][2]));   // (fade off: the period's whole wherever in it)
    }
    // 3. the game's weights: dusk1 at its start - dusk1 1, day 0.5 -> dawn / dusk 2/3, day 1/3
    CHECK(Is(VisShares(3, 0.0f, true), 1.0f / 3.0f, 0, 2.0f / 3.0f));
    // dusk1 at its end - dusk1 1, dusk2 0.5: both dawn / dusk
    CHECK(Is(VisShares(3, 1.0f, true), 0, 0, 1));
    // wrapping: dawn1 at its start leans on night (5), night at its end on dawn1 (0) - all night
    CHECK(Is(VisShares(0, 0.0f, true), 0, 1, 0));
    CHECK(Is(VisShares(5, 1.0f, true), 0, 1, 0));
    // night at its start - night 1, dusk2 0.5; dawn2 at its start - dawn2 1, dawn1 0.5
    CHECK(Is(VisShares(5, 0.0f, true), 0, 2.0f / 3.0f, 1.0f / 3.0f));
    CHECK(Is(VisShares(1, 0.0f, true), 0, 1.0f / 3.0f, 2.0f / 3.0f));
    // Serg's 22:04:10 (the clock just into "end of dusk, night"): the lighting dusk2, 0.00 into it - the dawn / dusk picture (the day's
    // as it is), not the night's
    CHECK(Is(VisShares(4, 0.0f, false), 0, 0, 1));
    for (int p = 0; p < 6; ++p)
        for (float f = 0.0f; f <= 1.0f; f += 0.125f) {
            const Shares s = VisShares(p, f, true);
            CHECK(std::fabs(Sum(s) - 1.0f) < 1e-5f);
            CHECK(s.pic[kDay] >= 0.0f && s.pic[kNight] >= 0.0f && s.pic[kDawnDusk] >= 0.0f);
        }
    // 4. not read / another layout: the day's
    CHECK(Is(VisShares(-1, 0.0f, true), 1, 0, 0));
    CHECK(Is(VisShares(6, 0.5f, true), 1, 0, 0));
    CHECK(Is(VisShares(4, 1.5f, true), 1, 0, 0));
    CHECK(Is(VisShares(4, -0.1f, true), 1, 0, 0));
    CHECK(Is(VisShares(4, std::nanf(""), true), 1, 0, 0));
    // 5. the dome as effects.cpp bakes it (nothing coloured: one picture whole, every brightness 1): night share 0 or 1 and x1 at every
    //    time of the lighting - never a darkened or blended picture
    const float asIs[kPics] = {1.0f, 1.0f, 1.0f};
    for (int p = 0; p < 6; ++p)
        for (float f = 0.0f; f <= 1.0f; f += 0.125f)
            for (int nightPicture = 0; nightPicture < 2; ++nightPicture) {
                const Bake k = BakeOf(VisShares(p, f, false), asIs, nightPicture != 0);
                CHECK((k.night == 0.0f || k.night == 1.0f) && std::fabs(k.mul - 1.0f) < 1e-6f);
                if (!nightPicture) CHECK(k.night == 0.0f);   // (an act with no night picture: the day's as it is)
            }

    // 6. [fog] game_fog_colour=0 (vr/sky/fog_times.h): the act's dawn / day / dusk / night colours (Settings > Game fog), the game's six
    //    files taking them by name, blended by the game's own time and weights; empty: day = ours, night = given, dawn and dusk = the day's
    //    times the game's own change from its day fog to that file's (each file its own)
    {
        CHECK(fogtimes::GroupOf(0) == fogtimes::kDawn && fogtimes::GroupOf(1) == fogtimes::kDawn && fogtimes::GroupOf(2) == fogtimes::kDay &&
              fogtimes::GroupOf(3) == fogtimes::kDusk && fogtimes::GroupOf(4) == fogtimes::kDusk && fogtimes::GroupOf(5) == fogtimes::kNight);
        float k[3];
        fogtimes::GameChange(2, 4, k);   // (act 2's dusk 2 against its day: 238 167 167 / 208 208 167 - rose)
        CHECK(std::fabs(k[0] - 238.0f / 208.0f) < 1e-5f && std::fabs(k[1] - 167.0f / 208.0f) < 1e-5f && std::fabs(k[2] - 1.0f) < 1e-5f);
        fogtimes::GameChange(2, 3, k);   // (its dusk 1: as its day)
        CHECK(k[0] == 1.0f && k[1] == 1.0f && k[2] == 1.0f);
        fogtimes::GameChange(4, 3, k);   // (act 4: black by day - no change to take)
        CHECK(k[0] == 1.0f && k[2] == 1.0f);
        fogtimes::GameChange(1, 0, k);   // (act 1's dawn 1: 37 37 48 of white)
        CHECK(std::fabs(k[0] - 37.0f / 255.0f) < 1e-5f && std::fabs(k[2] - 48.0f / 255.0f) < 1e-5f);
        const float day[3] = {0.10f, 0.08f, 0.04f}, nightDef[3] = {0.02f, 0.016f, 0.008f};
        fogtimes::Set none{};
        float files[fogtimes::kPeriods][3], out[3];
        fogtimes::FileColours(2, none, day, nightDef, files);
        CHECK(files[2][0] == day[0] && files[5][0] == nightDef[0]);                                   // (day ours, night the given)
        CHECK(std::fabs(files[4][0] - day[0] * 238.0f / 208.0f) < 1e-6f && files[3][1] == day[1]);   // (dusk 2 rose, dusk 1 as day)
        fogtimes::Set dusk{};
        dusk.set[fogtimes::kDusk] = true;
        dusk.colour[fogtimes::kDusk][0] = 0.3f; dusk.colour[fogtimes::kDusk][1] = 0.2f; dusk.colour[fogtimes::kDusk][2] = 0.1f;
        fogtimes::FileColours(2, dusk, day, nightDef, files);
        CHECK(files[3][0] == 0.3f && files[4][0] == 0.3f && files[1][0] == day[0]);                   // (set: both dusk files take it)
        CHECK(fogtimes::ColourNow(3, 0.5f, files, out) && out[0] == 0.3f);                            // (dusk 1's middle)
        CHECK(fogtimes::ColourNow(3, 0.0f, files, out) && std::fabs(out[0] - (0.3f + 0.5f * day[0]) / 1.5f) < 1e-6f);   // (dusk 1, day 0.5)
        CHECK(fogtimes::ColourNow(5, 1.0f, files, out));                                              // (night into dawn 1 - wraps)
        CHECK(std::fabs(out[0] - (nightDef[0] + 0.5f * files[0][0]) / 1.5f) < 1e-6f);
        out[0] = -1.0f;
        CHECK(!fogtimes::ColourNow(6, 0.5f, files, out) && !fogtimes::ColourNow(2, std::nanf(""), files, out) && out[0] == -1.0f);
        int next = 0;
        float own = 0.0f, other = 0.0f;
        for (int p = 0; p < fogtimes::kPeriods; ++p)
            for (float f = 0.0f; f <= 1.0f; f += 0.125f)
                CHECK(fogtimes::Weights(p, f, &next, &own, &other) && std::fabs(own + other - 1.0f) < 1e-6f && own >= other);
    }

    std::printf(g_fail ? "sky_time_test: %d FAILED\n" : "sky_time_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
