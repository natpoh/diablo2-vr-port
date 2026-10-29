// unleashed_fog_test - the fog parts taken from D2R VR Unleashed (shared/unleashed_fog.h, reshade/D2R_FogEase_Unleashed.fxh; MPL 2.0,
// bearstonem) as D2R VR Authentic uses them: [fog] ease_in - the fog eased in over its first 40 units, off = the curve exactly as
// before (our effect's FogAt, the game's fog by our curve: shared/dxbc_fog_height.h CurveFog) - and [fog] no_fog_river - no added
// fog in the River of Flame, only there, only while on. The effect file includes the .fxh, and the .fxh's function is word for word
// the string the before-the-effects step builds its shader from.
#include "../../shared/unleashed_fog.h"
#include "../../shared/dxbc_fog_height.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAILED line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static std::string ReadLf(const char* path) {   // the file, CRLF read as LF
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str(), out;
    for (char c : s) if (c != '\r') out += c;
    return out;
}

int main() {
    using namespace unleashed::fog;
    // ---- the ease: smoothstep(start, start + 40, d)
    CHECK(Ease(50.0f, 50.0f) == 0.0f && Ease(10.0f, 50.0f) == 0.0f);   // at and before the start: none
    CHECK(std::fabs(Ease(60.0f, 50.0f) - 0.15625f) < 1e-6f);            // a quarter of the way: 0.25^2 (3 - 0.5), Unleashed's own check
    CHECK(std::fabs(Ease(70.0f, 50.0f) - 0.5f) < 1e-6f);                // half way: a half
    CHECK(Ease(90.0f, 50.0f) == 1.0f && Ease(5000.0f, 50.0f) == 1.0f);  // from 40 past the start on: whole
    bool rises = true;
    for (float d = 50.0f; d < 95.0f; d += 0.5f) rises = rises && Ease(d + 0.5f, 50.0f) >= Ease(d, 50.0f);
    CHECK(rises);

    // ---- our curve, ease off: as before, to the bit (Serg's 50 - 180 curve 1; Unleashed's 30 - 600 curve 0.4; a cave's 100 - 200 ^1.6 x0.8)
    const dxbcfog::Curve curves[] = {{true, 50.0f, 180.0f, 1.0f, 1.0f}, {true, 30.0f, 600.0f, 0.4f, 1.0f}, {true, 100.0f, 200.0f, 1.6f, 0.8f}};
    for (const dxbcfog::Curve& c : curves) {
        bool same = true, eased = true;
        dxbcfog::Curve e = c;
        e.ease = true;
        for (float d = 0.0f; d < 800.0f; d += 0.37f) {
            const float x = std::fmin(1.0f, std::fmax(0.0f, (d - c.start) / std::fmax(c.end - c.start, 1.0f)));
            const float old = (x > 0.0f ? std::pow(x, c.curve) : 0.0f) * c.strength;   // FogAt before [fog] ease_in
            same = same && dxbcfog::CurveFog(c, d) == old;
            // eased: never more than before, the same from 40 past the start on, and none at the start
            const float fe = dxbcfog::CurveFog(e, d);
            eased = eased && fe <= old + 1e-7f && (d < c.start + kEaseUnits || fe == old) && (d > c.start || fe == 0.0f);
        }
        CHECK(same);
        CHECK(eased);
        CHECK(!(c == e) && !(e == c));   // the patched pass is made again when the switch changes
    }
    // the hard ring: with curve 0.4 the fog 2 units past its start was already 10 % of the way (0.0035 ^ 0.4), eased it is near nothing
    {
        dxbcfog::Curve c{true, 30.0f, 600.0f, 0.4f, 1.0f}, e = c;
        e.ease = true;
        CHECK(dxbcfog::CurveFog(c, 32.0f) > 0.09f && dxbcfog::CurveFog(e, 32.0f) < 0.01f);
    }

    // ---- no fog in the River of Flame: there only, and only while on
    CHECK(!NoFogArea("act4_lava", false));
    CHECK(NoFogArea("act4_lava", true));
    for (const char* b : {"act4_mesa", "act4_diab", "act1_outdoors", "", "act4_lava2", "act4", "expansion_town"}) CHECK(!NoFogArea(b, true));

    // ---- the effect: the .fxh included by D2R_DepthFog.fx, its function the before-the-effects step's string word for word
    const std::string fxh = ReadLf(UNLEASHED_FXH_PATH), fx = ReadLf(DEPTHFOG_FX_PATH);
    CHECK(!fxh.empty() && fxh.find(kEaseHlsl) != std::string::npos);
    CHECK(fxh.find("Mozilla Public License, v. 2.0") != std::string::npos && fxh.find("843b3a4") != std::string::npos);
    CHECK(fx.find("#include \"D2R_FogEase_Unleashed.fxh\"") != std::string::npos);
    CHECK(fx.find("uniform float FogEaseIn < hidden = true; > = 0.0;") != std::string::npos);   // off unless vrcam says
    CHECK(fx.find("* FogStrength * UnleashedFogEase(dist, FogStart, FogEaseIn);") != std::string::npos);

    if (g_fail) { std::printf("unleashed_fog_test: %d FAILED\n", g_fail); return 1; }
    std::printf("unleashed_fog_test: the fog eased in over 40 units (off: the curve as before, to the bit), none in the River of Flame "
                "only while on, the effect's include in step - all passed\n");
    return 0;
}
