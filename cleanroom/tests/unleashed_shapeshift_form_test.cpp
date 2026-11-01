// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 3d83a31ef88f58f77c7d27806ac1d9c29ac8baa7, its steamvr/shapeshift_form_test.cpp, for D2R VR Authentic's port
// (vr/unleashed_shapeshift_form.h): the states as two flags, the arm fit against our bodycal::ArmScale (its bound is 0.65, Unleashed's
// form passes 0.5), the world-at-the-druid's-size block left out (the world follows the eye height here), PlaceShoulders added.
// Stays MPL 2.0.
#include "unleashed_shapeshift_form.h"
#include "body_calibration.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace unleashed::shapeshift;
static int g_fail = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++g_fail; } } while (0)
static bool Near(float a, float b, float e = 0.01f) { return std::fabs(a - b) <= e; }
int main() {
    {   // The forms' models (F10 capture 2026-10-21), and nothing that merely starts the same.
        CHECK(FromModel("data/hd/character/enemy/wolf/skeleton/wolf.skeleton") == Wolf);
        CHECK(FromModel("data/hd/character/enemy/bear/skeleton/bear.skeleton") == Bear);
        CHECK(FromModel("data/hd/character/player/druid/skeleton/druid.skeleton") == None);
        CHECK(FromModel("data/hd/character/enemy/wolfrider/skeleton/wolfrider.skeleton") == None);
        CHECK(FromModel("data/hd/character/enemy/bearshaman/skeleton/bearshaman.skeleton") == None);
        CHECK(FromModel("") == None);
        CHECK(std::string_view(Name(Wolf)) == "werewolf" && std::string_view(Name(Bear)) == "werebear" && std::string_view(Name(None)) == "human");
    }
    {   // The player's states: 139 Werewolf, 140 Werebear; an unread status is no form.
        CHECK(FromStates(false, true, false) == None);
        CHECK(FromStates(true, true, false) == Wolf);
        CHECK(FromStates(true, false, true) == Bear);
        CHECK(FromStates(true, false, false) == None);
    }
    {   // Heights: the form's own eyes, the setting as a bounded fraction of them.
        CHECK(Near(TargetEye(Wolf, 1), 6.93f) && Near(TargetEye(Bear, 1), 7.92f) && TargetEye(None, 1) == 0);
        CHECK(Near(TargetEye(Wolf, 1.02f), 7.07f, 0.02f));   // the werewolf at the druid's height
        CHECK(Near(TargetEye(Wolf, 9), 10.40f) && Near(TargetEye(Bear, 0), 3.96f));
        CHECK(Near(StandingEye(Wolf) / 7.07f, 0.98f) && Near(StandingEye(Bear) / 7.07f, 1.12f));
        // the druid drawn at 0.95 of world, a form at 1.0: her units are 0.95 of the form's
        CHECK(Near(PersonUnits(0.95f, 1.0f), 0.95f) && Near(PersonUnits(0, 1), 1) && Near(PersonUnits(1, 0), 1) && Near(PersonUnits(1, NAN), 1));
        CHECK(Near(TargetEye(Wolf, 1) * PersonUnits(0.95f, 1.0f), 0.98f * 7.07f * 0.95f));
    }
    {   // Graft, with the capture's idle frames: torso upright about the spine root,
        // shoulders where a person's are, scale so the eyes stand at the form's height.
        // Werewolf: cog 3.853,-0.787; shoulder joints' midpoint 5.363,0.757.
        const float wolfUp = UprightShoulders(3.853f, -0.787f, 5.363f, 0.757f);
        CHECK(Near(wolfUp, 6.01f));
        const float wolfRig = RigEye(wolfUp);
        CHECK(Near(wolfRig, 7.12f));
        CHECK(Near(Scale(TargetEye(Wolf, 1), wolfRig), 0.973f));
        // Werebear: cog 4.037,-0.067; shoulder joints' midpoint 6.789,0.366.
        const float bearRig = RigEye(UprightShoulders(4.037f, -0.067f, 6.789f, 0.366f));
        CHECK(Near(bearRig, 8.07f));
        CHECK(Near(Scale(7.69f, bearRig), 0.95f));
        // The rest poses (F10, 2026-10-21): werewolf rig eye 8.32, werebear 8.49.
        CHECK(Near(Scale(TargetEye(Wolf, 1), 8.32f), 0.833f) && Near(Scale(TargetEye(Bear, 1), 8.49f), 0.933f));
        // The druid's own proportions give back the druid.
        CHECK(Near(RigEye(5.972f), 7.07f) && Near(Scale(7.07f, 7.07f), 1));
        CHECK(Near(ShoulderRatio + NeckAboveShoulders + EyeAboveNeck, 1, 0.001f));
        // An already upright torso stays as it is.
        CHECK(Near(UprightShoulders(4, 0, 6, 0), 6) && UprightTilt(4, 0, 6, 0) == 0.0f);
        // Bad input never scales a body away.
        CHECK(Scale(0, 7) == 1 && Scale(5, 0) == 1 && Scale(NAN, 7) == 1 && Scale(100, 1) == 2 && Scale(1, 100) == 0.3f);
        // every [shapeshift] height from 50 to 150 % is drawn as asked, inside the clamp
        for (float f = 0.5f; f <= 1.5f; f += 0.05f) {
            const float w = Scale(TargetEye(Wolf, f), wolfRig), b = Scale(TargetEye(Bear, f), bearRig);
            CHECK(Near(w * wolfRig, TargetEye(Wolf, f), 1e-3f) && Near(b * bearRig, TargetEye(Bear, f), 1e-3f));
        }
    }
    {   // The tilt stands the shoulders straight over the spine root at the upright height: (0, dy, dz) * R = (0, |dy dz|, 0).
        const float py = 3.853f, pz = -0.787f, sy = 5.363f, sz = 0.757f;
        const float t = UprightTilt(py, pz, sy, sz), dy = sy - py, dz = sz - pz;
        const float y = dy * std::cos(t) - dz * std::sin(t), z = dy * std::sin(t) + dz * std::cos(t);
        CHECK(Near(z, 0.0f, 1e-4f) && Near(py + y, UprightShoulders(py, pz, sy, sz), 1e-4f));
        CHECK(Near(std::fabs(t) * 57.2958f, 45.7f, 0.5f));   // the werewolf's idle hunch, some 46 degrees forward
        // and put under the camera: across the floor at the model's origin, a person's neck behind the eyes
        float x = 1, zz = 1;
        PlaceShoulders(7.12f, &x, &zz);
        CHECK(x == 0.0f && Near(zz, -0.2136f, 1e-3f));
        PlaceShoulders(0.0f, &x, &zz);
        CHECK(x == 0.0f && zz == 0.0f);
    }
    {   // Arm fit (rest poses, F10 2026-10-21; Unleashed's calibration eye 1.635 m, span 1.525 m).
        // Werebear: shoulders 3.11 apart, arm 3.707, half palm 0.413, rig eye 8.49. Fitted from its own shoulders
        // the arms want half their length (our bound holds them at 0.65); from a person's, about 0.68.
        const float bearOwn = bodycal::ArmScale(8.49f, 1.635f, 1.525f, 3.11f, 3.707f, 0.413f);
        const float bearFit = bodycal::ArmScale(8.49f, 1.635f, 1.525f, ArmFitShoulders(3.11f, 8.49f), 3.707f, 0.413f);
        CHECK(Near(bearOwn, 0.65f, 1e-4f) && Near(bearFit, 0.68f, 0.02f) && bearFit > bearOwn);
        // Werewolf: shoulders 1.80 apart (narrower than a person's 2.02 at rig eye 8.32): unchanged.
        CHECK(Near(ArmFitShoulders(1.80f, 8.32f), 1.80f) && Near(ArmFitShoulders(3.11f, 8.49f), 2.06f));
        CHECK(ArmFitShoulders(1.5f, 0) == 1.5f);
        // The druid (shoulders 1.715 at eyes 7.07) is a person already.
        CHECK(Near(ArmFitShoulders(1.715f, 7.07f), 1.715f, 1e-3f));
    }
    {   // Eye ease: the first height is taken at once; a transform eases over 500 ms.
        EyeEase e;
        CHECK(Near(e.Step(7.07f, 1000), 7.07f));
        CHECK(Near(e.Step(5.20f, 1010), 7.07f));                  // the werewolf appears: starts at the druid's
        CHECK(Near(e.Step(5.20f, 1260), (7.07f + 5.20f) / 2));    // halfway at 250 ms (smoothstep)
        CHECK(Near(e.Step(5.20f, 1510), 5.20f) && Near(e.Settled(), 5.20f));
        const float before = e.Step(7.69f, 1600);                // a new target mid-way continues from where it is
        CHECK(Near(before, 5.20f));
        const float mid = e.Step(7.07f, 1800);
        CHECK(mid > 5.20f && mid < 7.69f && Near(e.Settled(), 7.07f));
        CHECK(Near(e.Step(7.07f, 2400), 7.07f));
        CHECK(Near(e.Step(5.65f, 5000), 5.65f));                  // unseen for more than a second: at once
        CHECK(Near(e.Step(5.20f, 5010), 5.65f));                  // a form eases in
        CHECK(Near(e.Step(7.07f, 5100, false), 7.07f) && Near(e.Settled(), 7.07f));   // a person's rig: at once
        CHECK(Near(e.Step(7.07f, 5110), 7.07f));
        CHECK(Near(e.Step(0, 5120), 7.07f) && Near(e.Step(NAN, 5130), 7.07f));
    }
    {   // Leg hold: standing holds, walking lets the stride play, both blend in 150 ms.
        LegHold h;
        uint64_t t = 1000;
        float x = 100, z = 200;
        CHECK(h.Step(t, x, z) == 1);
        for (int i = 0; i < 30; ++i) { t += 10; CHECK(h.Step(t, x + 0.001f * i, z) == 1); }   // sway-size drift: still standing
        float w = 1;
        for (int i = 0; i < 30; ++i) { t += 10; x += 0.04f; w = h.Step(t, x, z); }            // 4 units/s
        CHECK(w == 0);
        for (int i = 0; i < 30; ++i) { t += 10; w = h.Step(t, x, z); }
        CHECK(w == 1);
        {   // halfway through the blend after the window that sees the stop
            LegHold m;
            uint64_t u = 0;
            float p = 0;
            m.Step(u, p, 0);
            for (int i = 0; i < 40; ++i) { u += 10; p += 0.05f; m.Step(u, p, 0); }
            float v = 0;
            for (int i = 0; i < 13; ++i) { u += 10; v = m.Step(u, p, 0); }
            CHECK(v > 0 && v < 1);
        }
        t += 5000;
        CHECK(h.Step(t, x + 50, z) == 1);                                                     // after a pause: standing again
        CHECK(h.Step(t + 10, NAN, z) == 1);
    }
    {   // The swap: a rig the sample still names is gone once the game has not drawn it for ~3 frames.
        CHECK(!RigDrawn(1000, 0) && !RigGone(1000, 0));                                       // never drawn: neither
        CHECK(RigDrawn(1000, 1000) && RigDrawn(1000, 1012) && RigDrawn(1040, 1000));          // this frame, a later stamp, 40 ms
        CHECK(!RigGone(1012, 1000) && !RigGone(1040, 1000));                                  // a frame or three apart: still drawn
        CHECK(RigGone(1041, 1000) && !RigDrawn(1041, 1000));
        CHECK(RigJustGone(1041, 1000) && !RigJustGone(2600, 1000));                           // a swap, not a body gone long ago
        const uint64_t druid[3] = {1000, 0, 0}, wolf[3] = {1000, 1030, 0}, none[3] = {0, 0, 0};
        CHECK(PosedRig(1050, druid) == 0 && PosedRig(1050, wolf) == 1 && PosedRig(1050, none) == -1);
        CHECK(PosedRig(1130, wolf) == 1 && PosedRig(1131, wolf) == -1);                       // nothing drawn for 100 ms: the sample answers
        const uint64_t back[3] = {1200, 1030, 0};
        CHECK(PosedRig(1210, back) == 0);                                                     // back in human form
    }
    if (g_fail) { std::printf("unleashed_shapeshift_form_test: %d FAILED\n", g_fail); return 1; }
    std::puts("unleashed_shapeshift_form_test: models, states, heights, graft, tilt, arm fit, eye ease, leg hold and rig swap passed");
    return 0;
}
