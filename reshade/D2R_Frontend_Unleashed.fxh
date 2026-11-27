// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem, Kudlatytrue and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed PR #583 by Kudlatytrue (a645967, 863697f, 8f25454, 7bd5a0a), merged by bearstonem via PR #584 (9b7abd6) with
// Berk's 9a276e5 (the menu's sky looked up once): reshade/D2R_FogWorld.fxh SkyCutAt and the main menu's places in DepthFogWorld, and
// reshade/D2R_Sky.fxh's [frontend] uniforms - the main menu's act scenes under our sky ([frontend] sky=1 in D2R VR Authentic, default 0):
// the walls between the acts (cut_right / cut_left / cut_far_*: the other acts' vignettes stand beside each act's in the same preset), the
// edges against the void (sky_edges) and the painted sky card's dark edge (sky_blend). Not taken: the per-act sky pictures (pano_N) and the
// wisps (D2R_Lights.fxh). Laid onto D2R VR Authentic's D2R_DepthFog.fx as functions its DepthFogWorld calls (the calls are marked
// [frontend] there). Thanks to Kudlatytrue and bearstonem for the 3D main menu. The rest of D2R_DepthFog.fx is MIT; this file stays MPL 2.0.

// [frontend] sky_blend: the main menu's painted sky is a card that ends in a dark band, and our sky over the void met it in a hard line.
// What is drawn farther than SkyBlendFar and darker than SkyBlendLo..SkyBlendHi (luma), above the horizon, fades into our sky by up to
// SkyBlendDark.
uniform float SkyBlendDark < hidden = true; > = 0.0;
uniform float SkyBlendFar < hidden = true; > = 300.0;
uniform float SkyBlendLo < hidden = true; > = 0.03;
uniform float SkyBlendHi < hidden = true; > = 0.22;
uniform bool SkyEdgeFix < hidden = true; > = false;   // [frontend] sky_edges: edges against the void laid over the sky by how much they cover
// [frontend] cut_right / cut_left: the other acts' scenes stand beside each act's; what lies farther than SkyCutRight to the camera's right
// (SkyCutDir: right on the ground, x z) or SkyCutLeft to its left is our sky instead, faded in over 25 units.
uniform bool SkyCutOn < hidden = true; > = false;
uniform float2 SkyCutDir < hidden = true; > = float2(0.0, 1.0);
uniform float SkyCutRight < hidden = true; > = 1e9;
uniform float SkyCutLeft < hidden = true; > = 1e9;
uniform float SkyCutVoidRight < hidden = true; > = 2.0;   // drawn with no depth this far to the right (dot with the right, 0..1): our sky
uniform float SkyCutVoidLeft < hidden = true; > = 2.0;
uniform float SkyCutFarRight < hidden = true; > = 1e9;   // the right cut's far part: this far to the right ...
uniform float SkyCutFarAhead < hidden = true; > = 1e9;   // ... past this far ahead

// Any of the main menu's sky places on (in a game none is): the sky is then looked up once for all of them (FrontSkyWanted).
bool FrontSkyWanted() { return SkyOn && (SkyCutOn || SkyEdgeFix || SkyBlendDark > 0.0); }

// How much of a point (side: to the camera's right, ahead: in front of it, on the ground) is past the wall between this act and the
// next - 0 here, 1 our sky.
float SkyCutAt(float side, float ahead)
{
    const float k = max(smoothstep(SkyCutRight, SkyCutRight + 25.0, side), smoothstep(SkyCutLeft, SkyCutLeft + 25.0, -side));
    // and the wall's far part, nearer: past SkyCutFarAhead ahead, from SkyCutFarRight (the next act's sky card stands there, closer to
    // the side than this act's own forest by the camera - 2026-11-14)
    return max(k, smoothstep(SkyCutFarRight, SkyCutFarRight + 25.0, side) * smoothstep(SkyCutFarAhead, SkyCutFarAhead + 15.0, ahead));
}

// [frontend] cut: another act's scene beside this one's goes to our sky (true: out is the pixel). off: the drawn point less the eye
// (world units, the camera's frame turned to the world's), dPt its depth.
bool FrontCut(float2 uv, float2 duv, float2 px, float3 off, float dPt, float3 colour, float3 frontSky, out float3 outC)
{
    outC = colour;
    if (!SkyCutOn || !SkyOn || dPt <= 1e-6) return false;
    const float side = dot(off.xz, SkyCutDir);
    const float ahead = dot(off.xz, float2(SkyCutDir.y, -SkyCutDir.x));
    const float kC = SkyCutAt(side, ahead);
    if (kC > 0.0) { outC = lerp(colour, frontSky, kC); return true; }
    // In front of the wall, at an edge of what is behind it: the game blended the leaves' and branches' edges with the bright card they
    // stood against - the treetops glowed where nothing was any more (2026-11-14). Two pixels round: the cut ones give the card's
    // colour, the others the tree's own (the darkest); how much of this pixel is card follows from its brightness between the two, and
    // that much is sky. (A neighbour's point: the same ray, nearly, at its own depth - side and ahead scale by dPt / its depth.)
    const float3 lw = float3(0.2126, 0.7152, 0.0722);
    float3 cardC = float3(0.0, 0.0, 0.0), ownC = colour;
    bool nearCut = false;
    [unroll] for (int cutN = 0; cutN < 8; ++cutN) {
        const float2 o = (cutN < 4 ? 1.0 : 2.0) * float2((cutN % 4) == 0 ? px.x : (cutN % 4) == 1 ? -px.x : 0.0,
                                                     (cutN % 4) == 2 ? px.y : (cutN % 4) == 3 ? -px.y : 0.0);
        const float dn = DepthAt(duv + o).x;
        const float3 cn = tex2D(FogColorSmp, uv + float2(o.x, UpsideDown ? -o.y : o.y)).rgb;
        const float r = dn > 1e-6 ? dPt / dn : 0.0;
        if (dn > 1e-6 && SkyCutAt(side * r, ahead * r) > 0.5) {
            nearCut = true;
            if (dot(cn, lw) > dot(cardC, lw)) cardC = cn;
        } else if (dn > 1e-6 && dot(cn, lw) < dot(ownC, lw)) ownC = cn;
    }
    if (!nearCut) return false;
    const float lc = dot(cardC, lw), lo = dot(ownC, lw);
    const float card = lc > lo + 1e-3 ? saturate((dot(colour, lw) - lo) / (lc - lo)) : 0.0;
    outC = max(colour - cardC * card, 0.0) + frontSky * card;
    return true;
}

// [frontend] sky_edges: at an edge between what is drawn and the void, the game's pixel is the thing's colour times how much of the pixel
// it covers, over black - against our sky that stood as a dark outline, or, where the sky was added under it whole, as a glowing one round
// the tower and the treetops (2026-11-13). How much it covers: its brightness against the brightest drawn neighbour's (the thing's own
// colour); the rest of the pixel is sky.
bool FrontEdge(float2 uv, float2 px, float d, float d1, float d2, float d3, float d4, float dNear, float3 colour, float3 frontSky, out float3 outC)
{
    outC = colour;
    if (!SkyEdgeFix || !SkyOn || dNear <= 1e-6 || !(d <= 1e-6 || d1 <= 1e-6 || d2 <= 1e-6 || d3 <= 1e-6 || d4 <= 1e-6)) return false;
    const float3 lw = float3(0.2126, 0.7152, 0.0722);
    const float cy = UpsideDown ? -px.y : px.y;
    float3 obj = d > 1e-6 ? colour : float3(0.0, 0.0, 0.0);
    float3 c;
    c = tex2D(FogColorSmp, uv + float2(px.x, 0.0)).rgb; if (d1 > 1e-6 && dot(c, lw) > dot(obj, lw)) obj = c;
    c = tex2D(FogColorSmp, uv - float2(px.x, 0.0)).rgb; if (d2 > 1e-6 && dot(c, lw) > dot(obj, lw)) obj = c;
    c = tex2D(FogColorSmp, uv + float2(0.0, cy)).rgb;   if (d3 > 1e-6 && dot(c, lw) > dot(obj, lw)) obj = c;
    c = tex2D(FogColorSmp, uv - float2(0.0, cy)).rgb;   if (d4 > 1e-6 && dot(c, lw) > dot(obj, lw)) obj = c;
    const float cover = saturate(dot(colour, lw) / max(dot(obj, lw), 1e-3));
    outC = colour + frontSky * (1.0 - cover);
    return true;
}

// [frontend] sky_blend: the painted sky's dark edge, far off, fades into our sky.
bool FrontBlend(float d, float3 dir, float3 colour, float3 frontSky, out float3 outC)
{
    outC = colour;
    if (!SkyOn || SkyBlendDark <= 0.0 || d <= 1e-6 || NearPlane / d <= SkyBlendFar) return false;
    const float lumaB = dot(colour, float3(0.2126, 0.7152, 0.0722));
    const float kB = (1.0 - smoothstep(SkyBlendLo, SkyBlendHi, lumaB)) * saturate(SkyBlendDark) * smoothstep(-0.02, 0.06, dir.y);
    outC = lerp(colour, frontSky, kB);
    return true;
}

// [frontend] cut: what another act's scene draws with no depth (its painted sky card) is known only by its direction - toward that
// scene's side it is our sky, not laid over it (2026-11-14).
bool FrontVoidCut(float3 dir)
{
    if (!SkyCutOn) return false;
    const float sideV = dot(normalize(dir.xz + float2(1e-6, 0.0)), SkyCutDir);
    return sideV > SkyCutVoidRight || -sideV > SkyCutVoidLeft;
}
