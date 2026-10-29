// sky_dome_test - vr/sky_dome.h: [sky] game_sky's dome - when it is shown, where it is laid, whether the fog and our passes leave it alone.
//   1. shown only ticked, in VR F4, in a world, with a painted sky wanted, in act 1, the eye known - each missing one named;
//   2. its matrix: its middle at the eye, scaled from the model's 800 units to the radius; put away 5000 under the eye, a thousandth;
//   3. the radius the 24-bit depth holds (32 steps above the far plane: ~26000 units at near 0.05 - 10^7 rounded to the far plane and
//      was never drawn), the one asked for when nearer; the depth band our passes leave to it (from 98 % of its radius);
//   sky_dome_test [<unit .json>]: that file as the game would get it (make_sky_dome.py's, staged).
//   4. the unit's file before the game loads it: exactly one UnitRootComponent and a model - the first prototype's file without a root
//      component (it crashed the game in the camp, PC1 2026-10-27) refused, as an empty or missing file, two roots, no model.
#include "sky_dome.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

using namespace skydome;

int main(int argc, char** argv) {
    In ok;
    ok.ticked = ok.f4 = ok.inWorld = ok.eyeOk = true;
    ok.palette = 1;
    ok.act = 1;
    CHECK(std::strcmp(WhyNot(ok), "") == 0);
    {
        In in = ok; in.ticked = false; CHECK(std::strstr(WhyNot(in), "game_sky=0") != nullptr);
        in = ok; in.f4 = false; CHECK(std::strstr(WhyNot(in), "view") != nullptr);
        in = ok; in.f4 = false; in.viewWhy = "the table view"; CHECK(std::strcmp(WhyNot(in), "the table view") == 0);
        // the views: every view of our camera round the world (Serg: VR F2 had our old sky) - not from above, the table, F12, flat without game_drawn_flat
        ViewIn v;
        v.vr = v.enabled = true;
        for (int k = 2; k <= 4; k += 2) { v.view = k; CHECK(!*ViewWhyNot(v)); }                 // VR F2, F4
        v.view = 1; CHECK(std::strstr(ViewWhyNot(v), "above") != nullptr);                      // VR F1
        v.view = 3; CHECK(std::strstr(ViewWhyNot(v), "table") != nullptr);                      // VR F3
        v.view = 2; v.enabled = false; CHECK(std::strstr(ViewWhyNot(v), "F12") != nullptr);
        v.vr = false; v.enabled = true; v.gameDrawnFlat = true;
        for (int k = 2; k <= 3; ++k) { v.view = k; CHECK(!*ViewWhyNot(v)); }                    // flat F2, F3
        v.view = 1; CHECK(std::strstr(ViewWhyNot(v), "above") != nullptr);                      // flat F1
        v.view = 2; v.gameDrawnFlat = false; CHECK(std::strstr(ViewWhyNot(v), "game_drawn_flat") != nullptr);
        in = ok; in.inWorld = false; CHECK(std::strstr(WhyNot(in), "world") != nullptr);
        in = ok; in.palette = -1; CHECK(std::strstr(WhyNot(in), "no sky wanted") != nullptr);
        // [sky] game_skybox: the Arreat Summit has its own (the game's skybox); no other area of play does
        CHECK(IsGameSkyboxArea("UberArreatSummit") && IsGameSkyboxArea("uberarreatsummit"));   // (level 137, the uber Summit: its own too)
        in = ok; in.gameSkybox = true; CHECK(!*WhyNot(in));   // there too the game's dome (Serg: "а почему там старое небо") - laid behind the cloud sea
        CHECK(SkyboxRadius(200.0f, 0.1f) == DrawableRadius(1.0e9f, 0.1f) && SkyboxRadius(200.0f, 0.1f) > 360.0f * 10.0f);   // far past the presets' 210 x 291
        CHECK(SkyboxRadius(1.0e9f, 0.1f) == 1.0e9f && SkyboxRadius(200.0f, 0.0f) == 25000.0f);   // never nearer than asked; no near plane yet: 25000
        CHECK(IsGameSkyboxArea("expansion_mountaintop") && !IsGameSkyboxArea("expansion_ruins_snow") && !IsGameSkyboxArea("act1_outdoors") &&
              !IsGameSkyboxArea(""));
        in = ok; in.act = 0; CHECK(std::strstr(WhyNot(in), "no act's sky") != nullptr);
        for (int a = 1; a <= 5; ++a) { in = ok; in.act = a; CHECK(std::strcmp(WhyNot(in), "") == 0); }   // (each act its own dome)
        in = ok; in.eyeOk = false; CHECK(std::strstr(WhyNot(in), "eye") != nullptr);
    }
    // [sky] game_dome_caves: underground only, the models' radius times its size
    {
        CaveIn c;
        c.ticked = c.f4 = c.inWorld = c.underground = c.eyeOk = true;
        CHECK(std::strcmp(CaveWhyNot(c), "") == 0);
        CaveIn in = c; in.ticked = false; CHECK(std::strstr(CaveWhyNot(in), "game_dome_caves=0") != nullptr);
        in = c; in.f4 = false; CHECK(std::strstr(CaveWhyNot(in), "F4") != nullptr);
        in = c; in.inWorld = false; CHECK(std::strstr(CaveWhyNot(in), "world") != nullptr);
        in = c; in.underground = false; CHECK(std::strstr(CaveWhyNot(in), "underground") != nullptr);
        in = c; in.eyeOk = false; CHECK(std::strstr(CaveWhyNot(in), "eye") != nullptr);
        CHECK(CaveRadius(210.0f, 1.0f) == 210.0f && CaveRadius(210.0f, 0.5f) == 105.0f && CaveRadius(10.0f, 1.0f) == 20.0f);
    }
    {
        const float eye[3] = {100.0f, 20.0f, -50.0f};
        float m[16];
        Matrix(eye, 1.0e7f, true, m);
        CHECK(std::fabs(m[0] - 12500.0f) < 0.01f && m[5] == m[0] && m[10] == m[0] && m[15] == 1.0f);
        CHECK(m[12] == 100.0f && m[13] == 20.0f && m[14] == -50.0f);
        CHECK(m[1] == 0.0f && m[4] == 0.0f && m[3] == 0.0f);
        Matrix(eye, 1.0e7f, false, m);
        CHECK(m[0] == 0.001f && m[13] == 20.0f - 5000.0f && m[12] == 100.0f);
        Matrix(eye, 800.0f, true, m);
        CHECK(m[0] == 1.0f);
    }
    {
        const float r = DrawableRadius(1.0e7f, 0.05f);
        CHECK(std::fabs(r - 26214.4f) < 1.0f);
        CHECK(0.05f / r * 16777216.0f >= 31.9f);                 // 32 of the depth's steps above the far plane: drawn
        CHECK(0.05f / 1.0e7f * 16777216.0f < 1.0f);              // the first build's 10^7: under one step - the far plane, never drawn
        CHECK(DrawableRadius(800.0f, 0.05f) == 800.0f);          // nearer than that: as asked
        CHECK(DrawableRadius(1.0e7f, 0.0f) == 25000.0f);         // no near plane yet
        const float dd = DomeDepth(r, 0.05f);
        CHECK(0.05f / r <= dd && 0.05f / (r * 0.97f) > dd);     // its own depth in the band, 97 % of its radius not
        CHECK(DomeDepth(0.0f, 0.05f) == 1e-6f);
    }
    {
        const std::string good = R"({"type":"UnitDefinition","entities":[{"components":[{"type":"UnitRootComponent"},{"type":"TransformDefinitionComponent"}]},)"
                                 R"({"components":[{"type":"ModelDefinitionComponent"}]}]})";
        CHECK(UnitFileProblem(good) == nullptr);
        // the first prototype's: a root transform and the model, no root component
        const std::string noRoot = R"({"type":"UnitDefinition","entities":[{"components":[{"type":"TransformDefinitionComponent"}]},)"
                                   R"({"components":[{"type":"ModelDefinitionComponent"}]}]})";
        CHECK(UnitFileProblem(noRoot) != nullptr && std::strstr(UnitFileProblem(noRoot), "UnitRootComponent") != nullptr);
        CHECK(UnitFileProblem("") != nullptr);
        CHECK(UnitFileProblem(R"({"type":"UnitDefinition","a":{"type":"UnitRootComponent"},"b":{"type":"UnitRootComponent"},"c":"ModelDefinitionComponent"})") != nullptr);
        CHECK(UnitFileProblem(R"({"type":"UnitDefinition","a":{"type":"UnitRootComponent"}})") != nullptr);
        CHECK(UnitFileProblem(R"({"type":"Preset","a":{"type":"UnitRootComponent"},"c":"ModelDefinitionComponent"})") != nullptr);
    }
    if (argc > 1) {   // a unit file as make_sky_dome.py wrote it (the staged one): the game may load it
        std::string text;
        if (FILE* f = std::fopen(argv[1], "rb")) {
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            std::fclose(f);
        }
        const char* bad = UnitFileProblem(text);
        std::printf("  %s: %s\n", argv[1], bad ? bad : "the game may load it");
        CHECK(bad == nullptr);
    }
    if (g_fail) { std::printf("sky_dome_test: %d FAILED\n", g_fail); return 1; }
    std::printf("sky_dome_test: shown only where wanted (each reason named), laid at the eye at its radius, put away small under the ground; "
                "the radius the 24-bit depth holds and the depth band our passes leave to it: all passed\n");
    return 0;
}
