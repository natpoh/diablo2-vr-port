// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed PR #518 (Kudlatytrue 3599cf4 / 0132117, reshade/D2R_Targeting.fxh SparkHash / SparkAt / Sparks): the campfire's
// sparks at a click on the fire ([campfire] sparks). The burst itself as it is; laid onto D2R_DepthFog.fx's Phantom pass (its ray per
// pixel, the game's depth there) instead of Unleashed's Outline pass: SparkEye0/1 each eye, SparkPos the fire, both relative to where the
// hands hang (vrcam's g_eyeWorld). The rest of D2R_DepthFog.fx is MIT; this file stays MPL 2.0.

// The campfire's sparks (vrcam: a click on the fire): a burst of embers out of the fire, rising, drifting and dying out over two seconds,
// each a small glowing streak along its flight. Drawn into the picture as light (added), hidden by whatever stands nearer than the spark at
// that pixel (the depth), and over the sky as well. SparkTime: seconds since the click, SparkSeed: a different burst each click.
uniform bool SparkOn < hidden = true; > = false;
uniform float3 SparkPos < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 SparkEye0 < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 SparkEye1 < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float SparkTime < hidden = true; > = 0.0;
uniform float SparkSeed < hidden = true; > = 0.0;
uniform float SparkNear < hidden = true; > = 0.5;   // the scene camera's near plane (the depth's distance: SparkNear / depth, reversed Z)
#define SPARK_COUNT 44
float SparkHash(float n) { return frac(sin(n * 12.9898 + SparkSeed * 78.233) * 43758.5453); }
// Where spark i is t seconds after the click, and how bright (0 once dead).
float3 SparkAt(int i, float t, out float life)
{
    const float fi = float(i);
    const float a = SparkHash(fi) * 6.2831853, r = SparkHash(fi + 11.0);
    const float delay = SparkHash(fi + 23.0) * 0.35;           // not all at once: a flurry
    const float span = 0.7 + SparkHash(fi + 37.0) * 1.3;       // how long it glows
    const float tt = max(t - delay, 0.0);
    life = (t > delay) ? saturate(1.0 - tt / span) : 0.0;
    const float3 v = float3(cos(a) * (1.5 + 4.0 * r), 9.0 + 13.0 * SparkHash(fi + 51.0), sin(a) * (1.5 + 4.0 * r));
    const float3 start = SparkPos + float3(cos(a) * 1.5 * r, 1.0 + SparkHash(fi + 63.0) * 2.0, sin(a) * 1.5 * r);
    // rising fast, slowed by the air, a wander from side to side, at the end sinking a little
    const float drag = (1.0 - exp(-tt * 1.6)) / 1.6;
    const float3 wander = float3(sin(tt * 5.0 + fi) , 0.0, cos(tt * 4.0 + fi * 1.7)) * 0.8 * tt;
    return start + v * drag + wander + float3(0.0, -2.5 * tt * tt, 0.0);
}
// c: the picture here; dir: this pixel's ray (unit); back: the eye's back axis; d: the game's depth here (reversed Z, 0 = nothing)
float3 Sparks(float3 c, int eye, float3 dir, float3 back, float d)
{
    const float3 e = eye == 0 ? SparkEye0 : SparkEye1;
    const float ahead = max(dot(dir, -back), 1e-3);   // the ray's share along the view: the depth's distance is along the view
    const float scene = d > 1e-6 ? SparkNear / d / ahead : 1e9;
    // the whole burst inside a ball over the fire: rays missing it are left alone
    const float3 ball = SparkPos + float3(0.0, 14.0, 0.0) - e;
    const float along = dot(ball, dir);
    if (length(ball - dir * along) > 22.0) return c;
    float3 glow = 0.0;
    const float t = SparkTime;
    [loop] for (int i = 0; i < SPARK_COUNT; ++i) {
        float life;
        const float3 q0 = SparkAt(i, t, life);
        if (life <= 0.0) continue;
        float lifeB;
        const float3 q1 = SparkAt(i, max(t - 0.045, 0.0), lifeB);   // its streak: where it was a moment ago
        // nearest approach of the pixel's ray to the streak, sampled along it
        float best = 1e9, depth = 0.0;
        [unroll] for (int k = 0; k < 3; ++k) {
            const float3 q = lerp(q0, q1, k * 0.5) - e;
            const float s = dot(q, dir);
            const float dd = length(q - dir * s) / max(s, 1.0);   // as an angle: the same size near and far
            if (s > 0.0 && dd < best) { best = dd; depth = s; }
        }
        if (depth <= 0.0 || depth > scene + 0.5) continue;   // behind something
        const float size = 0.0012 * (0.6 + SparkHash(float(i) + 71.0));
        const float flick = 0.75 + 0.25 * sin(t * 40.0 + float(i) * 3.1);
        const float g = exp(-(best * best) / (size * size)) * life * flick;
        // white-hot when young, orange, then a deep red ember
        const float3 hot = lerp(float3(1.0, 0.25, 0.05), lerp(float3(1.0, 0.6, 0.15), float3(1.0, 0.95, 0.75), saturate(life * 2.0 - 1.0)), saturate(life * 1.6));
        glow += hot * g * 3.0;
    }
    return c + glow;
}
