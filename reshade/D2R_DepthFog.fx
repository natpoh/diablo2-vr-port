// D2R_DepthFog - distance fog and a sky for Diablo II: Resurrected under the
// vrcam plugin.
//
// Fog hides the edge where the loaded rooms end, so the render distance can
// stay modest. vrcam draws with an infinite reverse-Z projection, so the raw
// depth is d = near / z: distance = NearPlane / d, in the game's world units
// (the hero is about 7.5 tall).
//
// The sky fills the black void beyond the world: pixels with no depth at all
// that are (nearly) black. Its colour depends only on the direction the pixel
// looks in, so in stereo it sits exactly at infinity. vrcam hands over the
// camera's axes and projection per eye, and the palette of the area's biome;
// it switches the sky off underground, in caves and while a panel is open.
// With the sky on, the fog fades into the sky's horizon instead of FogColor.
//
// vrcam joins ReShade as an add-on and sets every uniform each frame from
// d2r_vr.ini [fog] and [sky] - the settings window's sliders. It also switches
// the technique on and off. Values typed into ReShade's own UI are overwritten
// while vrcam is running.
//
// Self-contained on purpose: FlatVR's ReShade install skips the effect
// packages, so ReShade.fxh is usually not there.
//
// The HUD is drawn over the same depth, so HUD pieces sitting over far scenery
// get fogged too. Bottom-corner orbs sit over the near ground and stay clear.
// Over the sky, only dark HUD pixels are at risk: bright ones are left alone.

uniform float NearPlane <
    hidden = true;
> = 1.5;

uniform bool FogOn <
    hidden = true;
> = true;

uniform float FogStart <
    ui_type = "slider"; ui_min = 0.0; ui_max = 2000.0; ui_step = 1.0;
    ui_label = "Fog start"; ui_tooltip = "World distance where the fog begins (set by vrcam).";
> = 150.0;

uniform float FogEnd <
    ui_type = "slider"; ui_min = 1.0; ui_max = 4000.0; ui_step = 1.0;
    ui_label = "Fog end"; ui_tooltip = "World distance where the fog is complete (set by vrcam).";
> = 600.0;

uniform float FogCurve <
    ui_type = "slider"; ui_min = 0.3; ui_max = 4.0; ui_step = 0.05;
    ui_label = "Fog curve"; ui_tooltip = "1 = linear, above 1 keeps the middle distance clearer.";
> = 1.6;

uniform float FogBlur <
    ui_type = "slider"; ui_min = 0.0; ui_max = 12.0;
    ui_label = "Fog depth blur (px)";
    ui_tooltip = "The fog is averaged over a 3x3 grid this many pixels apart: single holes and specks in the depth melt away. 0 = per pixel.";
> = 3.0;

uniform float3 FogColor <
    ui_type = "color"; ui_label = "Fog colour"; ui_tooltip = "Set by vrcam per act ([fog] color_*); without one the sky's horizon is used.";
> = float3(0.02, 0.02, 0.025);

uniform bool FogFixed < hidden = true; > = false;   // vrcam: the act has its own fog colour, the sky does not tint it
// vrcam: how much of each act's night picture shows, 0 by day .. 1 at full
// night - the game's own light (D2R_SkyNight_<act>.png, tools/gen_night_sky.py).
uniform float SkyNightMix < hidden = true; > = 0.0;
// vrcam, [sky] drift: the painted sky turns slowly round the viewer (1) or stands still (0).
// Off by default: the stars painted into a night picture went round with its clouds.
uniform float SkyDrift < hidden = true; > = 0.0;

uniform bool UpsideDown <
    ui_label = "Depth upside down"; ui_tooltip = "Tick if the fog sits on the wrong half of the picture.";
> = false;

uniform bool ShowDistance <
    ui_label = "Show distance (tuning)"; ui_tooltip = "Grey ramp of the fog amount instead of the picture.";
> = false;

// Sky, all set by vrcam.
//
// Both eyes' values, each present: which eye a frame is for comes from the
// technique (D2R_DepthFog = left, D2R_DepthFog_R = right), never from a
// uniform. Under D3D12 ReShade keeps one constant buffer per effect and
// rewrites it in place at every present; with a pair drawn per game frame the
// right eye's present wrote it before the GPU had run the left eye's effect,
// and the left eye drew the right eye's sky.
uniform bool SkyOn < hidden = true; > = false;
// The camera's axes in the world (y up); it looks along -CamBack. 0 = left eye, 1 = right.
uniform float3 CamRight0 < hidden = true; > = float3(1.0, 0.0, 0.0);
uniform float3 CamUp0 < hidden = true; > = float3(0.0, 1.0, 0.0);
uniform float3 CamBack0 < hidden = true; > = float3(0.0, 0.0, 1.0);
uniform float3 CamRight1 < hidden = true; > = float3(1.0, 0.0, 0.0);
uniform float3 CamUp1 < hidden = true; > = float3(0.0, 1.0, 0.0);
uniform float3 CamBack1 < hidden = true; > = float3(0.0, 0.0, 1.0);
// Each eye's projection: M[0], M[5], M[8], M[9].
uniform float4 SkyProj0 < hidden = true; > = float4(0.5625, 1.0, 0.0, 0.0);
uniform float4 SkyProj1 < hidden = true; > = float4(0.5625, 1.0, 0.0, 0.0);
uniform float3 SkyZenith < hidden = true; > = float3(0.10, 0.13, 0.18);
uniform float3 SkyHorizon < hidden = true; > = float3(0.35, 0.30, 0.28);
uniform float3 SunDir < hidden = true; > = float3(0.0, 0.2, 1.0);
uniform float SkySun < hidden = true; > = 0.5;          // 0 = no sun
uniform float SkyClouds < hidden = true; > = 0.7;       // cover 0..1
uniform float SkyStars < hidden = true; > = 0.0;
uniform float SkyBrightness < hidden = true; > = 1.0;
uniform float SkyTex < hidden = true; > = 0.0;        // 0 = drawn here, 1..6 = painted: act 1-5, act 5 snow
uniform bool SkyCapOn < hidden = true; > = true;       // the painted sky has its zenith picture
uniform float Timer < source = "timer"; >;

// The HUD nearer in AFR stereo (vrcam [stereo] ui_near). The game draws its
// HUD without a camera of its own, so it is found in the picture: in the left
// and the right eye's frames it sits on the same pixels, while the world moves
// by the eyes' parallax. Pixels equal to the previous frame (the other eye)
// in the lower part of the screen are taken for the HUD and moved apart per
// eye. Flat areas of the ground that match too move along - unseen, being flat.
uniform float HudShift < hidden = true; > = 0.0;   // uv: the left eye's HUD moves this much right, the right eye's left
uniform float HudTop < hidden = true; > = 0.70;    // the HUD is looked for below this height (uv)
uniform float HudTol < hidden = true; > = 0.02;    // colour difference still counted as "the same"
texture2D HudCurTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
texture2D HudPrevTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
sampler2D HudCurSmp { Texture = HudCurTex; MinFilter = POINT; MagFilter = POINT; };
sampler2D HudPrevSmp { Texture = HudPrevTex; MinFilter = POINT; MagFilter = POINT; };
// Where the HUD was found, held for a few frames: an animated orb or a pixel
// the eyes happen to differ on left the HUD for one frame and it flickered.
texture2D HudMaskTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8; };
texture2D HudMaskOldTex { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8; };
sampler2D HudMaskSmp { Texture = HudMaskTex; MinFilter = POINT; MagFilter = POINT; };
sampler2D HudMaskOldSmp { Texture = HudMaskOldTex; MinFilter = POINT; MagFilter = POINT; };

// Painted skies (tools/gen_sky.py, or any picture), one pair per act. vrcam sets
// the file names as preprocessor definitions from d2r_vr.ini [sky] texture_* /
// cap_* (bare names are looked for in reshade-shaders\Textures); a slot with no
// picture keeps the act 1 files here and is never sampled. Pictures of another
// size are scaled to the declared one.
// The band goes round the viewer, wrapped twice round the circle: its bottom
// edge 8 degrees under the horizon, its top 65 degrees up. Above 40 degrees it
// hands over to the cap, a square picture of the clouds seen straight up, laid
// flat over the viewer (a band's top rows would only pinch into a point there).
#ifndef D2R_SKY_ACT1
 #define D2R_SKY_ACT1 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_ACT1
 #define D2R_SKYCAP_ACT1 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKY_ACT2
 #define D2R_SKY_ACT2 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_ACT2
 #define D2R_SKYCAP_ACT2 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKY_ACT3
 #define D2R_SKY_ACT3 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_ACT3
 #define D2R_SKYCAP_ACT3 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKY_ACT4
 #define D2R_SKY_ACT4 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_ACT4
 #define D2R_SKYCAP_ACT4 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKY_ACT5
 #define D2R_SKY_ACT5 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_ACT5
 #define D2R_SKYCAP_ACT5 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKY_SNOW
 #define D2R_SKY_SNOW "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAP_SNOW
 #define D2R_SKYCAP_SNOW "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif

// Each act's night picture, blended in by SkyNightMix; an act without one keeps
// the act 1 day file here and is never sampled.
#ifndef D2R_SKYN_ACT1
 #define D2R_SKYN_ACT1 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_ACT1
 #define D2R_SKYCAPN_ACT1 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKYN_ACT2
 #define D2R_SKYN_ACT2 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_ACT2
 #define D2R_SKYCAPN_ACT2 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKYN_ACT3
 #define D2R_SKYN_ACT3 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_ACT3
 #define D2R_SKYCAPN_ACT3 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKYN_ACT4
 #define D2R_SKYN_ACT4 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_ACT4
 #define D2R_SKYCAPN_ACT4 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKYN_ACT5
 #define D2R_SKYN_ACT5 "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_ACT5
 #define D2R_SKYCAPN_ACT5 "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
#ifndef D2R_SKYN_SNOW
 #define D2R_SKYN_SNOW "D2R_Sky_ours/D2R_Sky_act1.png"
#endif
#ifndef D2R_SKYCAPN_SNOW
 #define D2R_SKYCAPN_SNOW "D2R_Sky_ours/D2R_SkyCap_act1.png"
#endif
texture2D SkyNBand1Tex < source = D2R_SKYN_ACT1; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand1Smp { Texture = SkyNBand1Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap1Tex < source = D2R_SKYCAPN_ACT1; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap1Smp { Texture = SkyNCap1Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyNBand2Tex < source = D2R_SKYN_ACT2; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand2Smp { Texture = SkyNBand2Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap2Tex < source = D2R_SKYCAPN_ACT2; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap2Smp { Texture = SkyNCap2Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyNBand3Tex < source = D2R_SKYN_ACT3; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand3Smp { Texture = SkyNBand3Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap3Tex < source = D2R_SKYCAPN_ACT3; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap3Smp { Texture = SkyNCap3Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyNBand4Tex < source = D2R_SKYN_ACT4; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand4Smp { Texture = SkyNBand4Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap4Tex < source = D2R_SKYCAPN_ACT4; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap4Smp { Texture = SkyNCap4Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyNBand5Tex < source = D2R_SKYN_ACT5; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand5Smp { Texture = SkyNBand5Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap5Tex < source = D2R_SKYCAPN_ACT5; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap5Smp { Texture = SkyNCap5Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyNBand6Tex < source = D2R_SKYN_SNOW; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyNBand6Smp { Texture = SkyNBand6Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyNCap6Tex < source = D2R_SKYCAPN_SNOW; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyNCap6Smp { Texture = SkyNCap6Tex; AddressU = WRAP; AddressV = WRAP; };

texture2D SkyBand1Tex < source = D2R_SKY_ACT1; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand1Smp { Texture = SkyBand1Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap1Tex < source = D2R_SKYCAP_ACT1; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap1Smp { Texture = SkyCap1Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyBand2Tex < source = D2R_SKY_ACT2; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand2Smp { Texture = SkyBand2Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap2Tex < source = D2R_SKYCAP_ACT2; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap2Smp { Texture = SkyCap2Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyBand3Tex < source = D2R_SKY_ACT3; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand3Smp { Texture = SkyBand3Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap3Tex < source = D2R_SKYCAP_ACT3; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap3Smp { Texture = SkyCap3Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyBand4Tex < source = D2R_SKY_ACT4; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand4Smp { Texture = SkyBand4Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap4Tex < source = D2R_SKYCAP_ACT4; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap4Smp { Texture = SkyCap4Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyBand5Tex < source = D2R_SKY_ACT5; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand5Smp { Texture = SkyBand5Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap5Tex < source = D2R_SKYCAP_ACT5; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap5Smp { Texture = SkyCap5Tex; AddressU = WRAP; AddressV = WRAP; };
texture2D SkyBand6Tex < source = D2R_SKY_SNOW; > { Width = 1536; Height = 1024; Format = RGBA8; };
sampler2D SkyBand6Smp { Texture = SkyBand6Tex; AddressU = WRAP; AddressV = CLAMP; };
texture2D SkyCap6Tex < source = D2R_SKYCAP_SNOW; > { Width = 1024; Height = 1024; Format = RGBA8; };
sampler2D SkyCap6Smp { Texture = SkyCap6Tex; AddressU = WRAP; AddressV = WRAP; };

texture2D FogColorTex : COLOR;
texture2D FogDepthTex : DEPTH;
sampler2D FogColorSmp { Texture = FogColorTex; };
sampler2D FogDepthSmp { Texture = FogDepthTex; MinFilter = POINT; MagFilter = POINT; };

void VS_Fullscreen(in uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD)
{
    uv.x = (id == 2) ? 2.0 : 0.0;
    uv.y = (id == 1) ? 2.0 : 0.0;
    pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float Hash(float2 p)
{
    // Dave Hoskins' hash12: no diagonal banding on integer cells
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float Noise(float2 p)
{
    const float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash(i), Hash(i + float2(1.0, 0.0)), f.x),
                lerp(Hash(i + float2(0.0, 1.0)), Hash(i + float2(1.0, 1.0)), f.x), f.y);
}

float Fbm(float2 p)
{
    float v = 0.0, a = 0.5;
    [unroll] for (int i = 0; i < 5; ++i) { v += a * Noise(p); p = p * 2.03 + 17.1; a *= 0.5; }
    return v;
}

// Zenith-to-horizon gradient; below the horizon the haze darkens toward the ground.
float3 SkyGradient(float h)
{
    if (h < 0.0) return lerp(SkyHorizon, SkyHorizon * 0.55, saturate(-h * 3.0));
    return lerp(SkyHorizon, SkyZenith, pow(saturate(h), 0.45));
}

float3 BandAt(int slot, float4 t, bool night)
{
    if (night) {
        if (slot == 1) return tex2Dlod(SkyNBand1Smp, t).rgb;
        if (slot == 2) return tex2Dlod(SkyNBand2Smp, t).rgb;
        if (slot == 3) return tex2Dlod(SkyNBand3Smp, t).rgb;
        if (slot == 4) return tex2Dlod(SkyNBand4Smp, t).rgb;
        if (slot == 5) return tex2Dlod(SkyNBand5Smp, t).rgb;
        return tex2Dlod(SkyNBand6Smp, t).rgb;
    }
    if (slot == 1) return tex2Dlod(SkyBand1Smp, t).rgb;
    if (slot == 2) return tex2Dlod(SkyBand2Smp, t).rgb;
    if (slot == 3) return tex2Dlod(SkyBand3Smp, t).rgb;
    if (slot == 4) return tex2Dlod(SkyBand4Smp, t).rgb;
    if (slot == 5) return tex2Dlod(SkyBand5Smp, t).rgb;
    return tex2Dlod(SkyBand6Smp, t).rgb;
}
float3 CapAt(int slot, float4 t, bool night)
{
    if (night) {
        if (slot == 1) return tex2Dlod(SkyNCap1Smp, t).rgb;
        if (slot == 2) return tex2Dlod(SkyNCap2Smp, t).rgb;
        if (slot == 3) return tex2Dlod(SkyNCap3Smp, t).rgb;
        if (slot == 4) return tex2Dlod(SkyNCap4Smp, t).rgb;
        if (slot == 5) return tex2Dlod(SkyNCap5Smp, t).rgb;
        return tex2Dlod(SkyNCap6Smp, t).rgb;
    }
    if (slot == 1) return tex2Dlod(SkyCap1Smp, t).rgb;
    if (slot == 2) return tex2Dlod(SkyCap2Smp, t).rgb;
    if (slot == 3) return tex2Dlod(SkyCap3Smp, t).rgb;
    if (slot == 4) return tex2Dlod(SkyCap4Smp, t).rgb;
    if (slot == 5) return tex2Dlod(SkyCap5Smp, t).rgb;
    return tex2Dlod(SkyCap6Smp, t).rgb;
}

float3 PaintedSky(float3 dir)
{
    const float el = degrees(asin(clamp(dir.y, -1.0, 1.0)));
    // two turns round the circle, drifting slowly so the clouds move
    const float u = atan2(dir.x, dir.z) / 3.14159265 + Timer * 0.000001 * SkyDrift;
    const float v = (65.0 - el) / 73.0;
    const float w = smoothstep(40.0, 62.0, el);
    const int slot = (int)(SkyTex + 0.5);
    const float n = saturate(SkyNightMix);   // the night picture's share: the same clouds, at night
    float3 c = float3(0.0, 0.0, 0.0);
    if (w < 1.0) {
        const float4 t = float4(u, v, 0, 0);
        c = BandAt(slot, t, false);
        if (n > 0.0) c = lerp(c, BandAt(slot, t, true), n);
    }
    if (w > 0.0) {
        float3 z = SkyZenith;   // no zenith picture: the palette's colour
        if (SkyCapOn) {
            // the cloud layer straight overhead; one tile spans the whole cap
            const float2 p = dir.xz / max(dir.y, 0.2) * 0.4 + Timer * 0.0000004 * SkyDrift * float2(1.0, 0.3);
            const float4 t = float4(p, 0, 0);
            z = CapAt(slot, t, false);
            if (n > 0.0) z = lerp(z, CapAt(slot, t, true), n);
        }
        c = lerp(c, z, w);
    }
    return c;
}

float3 Sky(float3 dir)
{
    if (SkyTex > 0.5) return PaintedSky(dir) * SkyBrightness;
    const float h = dir.y;
    float3 c = SkyGradient(h);
    const float cs = saturate(dot(dir, normalize(SunDir)));
    const float3 sunTint = float3(1.0, 0.85, 0.6);
    c += SkySun * (pow(cs, 600.0) * 6.0 + pow(cs, 12.0) * 0.35) * sunTint;
    if (h > 0.0) {
        if (SkyStars > 0.0) {
            const float2 g = dir.xz / (h + 0.25) * 220.0;
            const float star = step(0.995, Hash(floor(g))) * smoothstep(0.35, 0.0, length(frac(g) - 0.5));
            c += SkyStars * star * saturate(h * 4.0);
        }
        if (SkyClouds > 0.0) {
            // A flat layer overhead: the direction meets it at dir.xz / dir.y.
            const float2 p = dir.xz / (h + 0.12) * 1.6 + Timer * 0.00001 * float2(1.0, 0.4);
            const float n = Fbm(p);
            const float edge = lerp(0.75, 0.3, saturate(SkyClouds));
            const float dens = smoothstep(edge, edge + 0.25, n) * smoothstep(0.0, 0.15, h);
            const float3 cloud = SkyHorizon * (0.75 + 0.6 * n) + SkySun * 0.15 * sunTint * pow(cs, 4.0);
            c = lerp(c, cloud, dens * 0.9);
        }
    }
    return c * SkyBrightness;
}

// The game's interface left over this frame (labels over monsters, names, chat), as vrcam
// copies it out of the game's interface layer: premultiplied colour, alpha = how much of the
// world shows through. From above it is taken out and drawn back on the labels' plane
// (LabelsOn); otherwise it stays in the picture and only tells the fog where not to lie
// (UiMaskOn): labels over the void were fogged with the particles (2026-10-04).
texture2D GameLayerTex : D2R_GAME_LAYER;
sampler2D GameLayerSmp { Texture = GameLayerTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };
uniform bool UiMaskOn < hidden = true; > = false;

// How much fog a depth value gets, 0..1. Empty depth (the void) is far: full fog.
float FogAmount(float depth)
{
    const float dist = NearPlane / max(depth, 1e-7);    // reverse-Z infinite: d = near / z
    return pow(saturate((dist - FogStart) / max(FogEnd - FogStart, 1.0)), FogCurve);
}

// The depth of the first drawn surface straight below duv on the screen (down =
// one screen pixel down, in the depth's uv), 0 when nothing is drawn within
// 1024 pixels: doublings to the first hit (2, 4 ... 1024 pixels), then halvings
// between the last miss and that hit, to the pixel. The first hit alone moved
// in steps from row to row, and a big glow (a fire) over the void came out in
// bands of fog (2026-10-04).
// x: that depth, y: how many pixels down it was found (0 with nothing found).
float2 FirstDepthBelowAt(float2 duv, float down)
{
    float miss = 0.0, hit = 0.0;
    [unroll] for (int s = 1; s <= 10; ++s) {
        const float off = exp2((float)s);
        const bool drawn = tex2Dlod(FogDepthSmp, float4(duv + float2(0.0, down * off), 0, 0)).x > 1e-6;
        hit = hit > 0.0 ? hit : (drawn ? off : 0.0);
        miss = hit > 0.0 ? miss : off;
    }
    [unroll] for (int b = 0; b < 9; ++b) {
        const float mid = 0.5 * (miss + hit);
        const bool drawn = hit > 0.0 && tex2Dlod(FogDepthSmp, float4(duv + float2(0.0, down * mid), 0, 0)).x > 1e-6;
        hit = drawn ? mid : hit;
        miss = drawn ? miss : mid;
    }
    return hit > 0.0 ? float2(tex2Dlod(FogDepthSmp, float4(duv + float2(0.0, down * (hit + 1.0)), 0, 0)).x, hit) : float2(0.0, 0.0);
}

// The fog of what is drawn without depth over the void, worked out at an eighth
// of the screen and blurred, so it changes smoothly across the picture. Worked out
// per pixel, from the ground straight below each one, neighbouring columns met
// different things below - tree edges, people, gaps - and rain, glows and the far
// trees of Kurast's docks came out cut into vertical bands (2026-10-05).
// The amount: that ground's fog - what stands over far ground goes with it, what
// stands over near ground (a torch's flame, the rain round the hero) stays clear.
texture2D VoidFogTex { Width = BUFFER_WIDTH / 8; Height = BUFFER_HEIGHT / 8; Format = R16F; };
texture2D VoidFogTmpTex { Width = BUFFER_WIDTH / 8; Height = BUFFER_HEIGHT / 8; Format = R16F; };
sampler2D VoidFogSmp { Texture = VoidFogTex; AddressU = CLAMP; AddressV = CLAMP; };
sampler2D VoidFogTmpSmp { Texture = VoidFogTmpTex; AddressU = CLAMP; AddressV = CLAMP; };

float PS_VoidFog(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!FogOn || tex2Dsize(FogDepthSmp, 0).x < 16) return 0.0;
    const float2 duv = UpsideDown ? float2(uv.x, 1.0 - uv.y) : uv;
    const float down = UpsideDown ? -BUFFER_RCP_HEIGHT : BUFFER_RCP_HEIGHT;
    const float2 hit = FirstDepthBelowAt(duv, down);
    // As much fog as the thing stands on that ground: what hangs higher over it
    // on the screen keeps more of its light (rain over the hero, sparks), fading
    // out from 64 to 512 pixels up - at 32..256 far smoke over the far ground
    // shone through the fog (2026-10-05). Nothing drawn below: no fog from here.
    // (One column only: the nearest of nine cut a torch's glow into bars.)
    return hit.y > 0.0 ? FogAmount(hit.x) * (1.0 - smoothstep(64.0, 512.0, hit.y)) : 0.0;
}
// 9 taps two small texels apart (+-64 screen pixels), binomial weights; across, then down.
// Across only left a torch's glow in bars; the far water's edge the down blur
// thinned is taken in the main pass (the ground right below wins, see there).
float PS_VoidFogBlurH(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float2 st = float2(16.0 * BUFFER_RCP_WIDTH, 0.0);
    float t = 70.0 * tex2Dlod(VoidFogSmp, float4(uv, 0, 0)).x;
    t += 56.0 * (tex2Dlod(VoidFogSmp, float4(uv + st, 0, 0)).x + tex2Dlod(VoidFogSmp, float4(uv - st, 0, 0)).x);
    t += 28.0 * (tex2Dlod(VoidFogSmp, float4(uv + 2.0 * st, 0, 0)).x + tex2Dlod(VoidFogSmp, float4(uv - 2.0 * st, 0, 0)).x);
    t += 8.0 * (tex2Dlod(VoidFogSmp, float4(uv + 3.0 * st, 0, 0)).x + tex2Dlod(VoidFogSmp, float4(uv - 3.0 * st, 0, 0)).x);
    t += tex2Dlod(VoidFogSmp, float4(uv + 4.0 * st, 0, 0)).x + tex2Dlod(VoidFogSmp, float4(uv - 4.0 * st, 0, 0)).x;
    return t / 256.0;
}
float PS_VoidFogBlurV(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const float2 st = float2(0.0, 16.0 * BUFFER_RCP_HEIGHT);
    float t = 70.0 * tex2Dlod(VoidFogTmpSmp, float4(uv, 0, 0)).x;
    t += 56.0 * (tex2Dlod(VoidFogTmpSmp, float4(uv + st, 0, 0)).x + tex2Dlod(VoidFogTmpSmp, float4(uv - st, 0, 0)).x);
    t += 28.0 * (tex2Dlod(VoidFogTmpSmp, float4(uv + 2.0 * st, 0, 0)).x + tex2Dlod(VoidFogTmpSmp, float4(uv - 2.0 * st, 0, 0)).x);
    t += 8.0 * (tex2Dlod(VoidFogTmpSmp, float4(uv + 3.0 * st, 0, 0)).x + tex2Dlod(VoidFogTmpSmp, float4(uv - 3.0 * st, 0, 0)).x);
    t += tex2Dlod(VoidFogTmpSmp, float4(uv + 4.0 * st, 0, 0)).x + tex2Dlod(VoidFogTmpSmp, float4(uv - 4.0 * st, 0, 0)).x;
    return t / 256.0;
}

float3 DepthFogWorld(float4 pos, float2 uv, int eye)
{
    const float3 colour = tex2D(FogColorSmp, uv).rgb;
    const float2 duv = UpsideDown ? float2(uv.x, 1.0 - uv.y) : uv;
    const float d = tex2Dlod(FogDepthSmp, float4(duv, 0, 0)).x;

    // No depth at all (ReShade's Generic Depth off, or nothing picked): every
    // pixel read as the void, the fog covered the HUD and the sky filled every
    // dark speck of the ground. ReShade then binds a tiny empty texture, not a
    // screen-sized buffer. (Probing for empty depth low on the screen was wrong:
    // looking up, the sky is down there too, and the sky went out.)
    if (tex2Dsize(FogDepthSmp, 0).x < 16) return colour;

    // The pixel's four neighbours. Where the drawn distance ends, the far
    // ground has single empty pixels (depth 0): each was taken for the void,
    // got the sky, and the sky showed through the fog as dots along the
    // horizon. The void is now only where the pixel AND its neighbours are
    // empty; the fog takes the farthest of the five (reverse-Z: the smallest),
    // which also drowns single near pixels the game's dithered transparencies
    // leave in far ground.
    const float2 px = float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
    const float d1 = tex2Dlod(FogDepthSmp, float4(duv + float2(px.x, 0), 0, 0)).x;
    const float d2 = tex2Dlod(FogDepthSmp, float4(duv - float2(px.x, 0), 0, 0)).x;
    const float d3 = tex2Dlod(FogDepthSmp, float4(duv + float2(0, px.y), 0, 0)).x;
    const float d4 = tex2Dlod(FogDepthSmp, float4(duv - float2(0, px.y), 0, 0)).x;
    const float dNear = max(max(d, d1), max(max(d2, d3), d4));
    const float dFar = min(min(d, d1), min(min(d2, d3), d4));

    float3 fogColour = FogColor;
    if (SkyOn) {
        const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
        const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0);
        const float3 dir = eye == 0 ? normalize(v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0)
                                    : normalize(v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1);
        const float dither = (Hash(pos.xy) - 0.5) / 255.0;
        if (dNear <= 1e-6) {
            // Nothing drawn here: the void, unless a HUD piece lies over it.
            const float luma = dot(colour, float3(0.2126, 0.7152, 0.0722));
            const float k = 1.0 - smoothstep(0.03, 0.08, luma);
            float3 sky = Sky(dir);
            // with the act's own fog colour the sky's low part sinks into it too:
            // the fogged land meets the sky without an edge
            if (FogOn && FogFixed) sky = lerp(sky, FogColor, 1.0 - smoothstep(-0.03, 0.2, dir.y));
            // The interface over the void keeps its own colour; its dark parts are the void.
            const bool hud = uv.y >= HudTop && tex2Dlod(HudMaskSmp, float4(uv, 0, 0)).r > 0.0;
            if (hud) return lerp(colour, sky + dither, k);
            // Anything else here was drawn without depth over the game's black void -
            // particles, grass, glows, the far trees of Kurast's docks. It is laid OVER
            // the sky as light, not in place of it: the dark leaves of those trees took
            // the sky's place and stood in the fog as holes in the shape of trees, and a
            // glow's dim rim, darker than the sky, ringed it in black (2026-10-05).
            // It is fogged as the first drawn surface straight BELOW it on the screen -
            // what it stands on or above: a flame gets its brazier's (clear), grass at
            // the end of the drawn ground that far ground's (gone). With nothing drawn
            // below, it is in the void: gone.
            // How much: the fog of the ground below, blurred (VoidFogTex). But ground
            // right below - 2 to 16 pixels down - wins with its own fog: the blur ran
            // the sky's none into the far ground at the horizon, and the foam along the
            // far water's edge stood out of the fog there (2026-10-05).
            float tf = 0.0;
            if (FogOn && k < 0.999) {
                tf = saturate(tex2Dlod(VoidFogSmp, float4(uv, 0, 0)).x);
                const float down = UpsideDown ? -px.y : px.y;
                float dn = 0.0;
                [unroll] for (int s2 = 1; s2 <= 4; ++s2) {
                    const float dd = tex2Dlod(FogDepthSmp, float4(duv + float2(0.0, down * exp2((float)s2)), 0, 0)).x;
                    dn = dn > 0.0 ? dn : dd;
                }
                if (dn > 1e-6) tf = max(tf, FogAmount(dn));
            }
            // Only what is clearly off black: the near-black spill the game leaves round
            // people and trees over the void, added in full, stood as a pale haze round
            // them (2026-10-05). k fades it in over the same 0.03..0.08 as the void test.
            return sky + dither + colour * ((1.0 - k) * (1.0 - tf));
        }
        const float3 fogDir = normalize(float3(dir.x, clamp(dir.y, -0.1, 0.15), dir.z));
        if (!FogFixed) fogColour = (SkyTex > 0.5 ? PaintedSky(fogDir) : SkyGradient(fogDir.y)) * SkyBrightness + dither;
    }
    if (!FogOn) return colour;

    // The depth smeared: the fog of a 3x3 grid FogBlur pixels apart, averaged
    // (the fog, not the depth - an empty pixel is "far", not a zero to average).
    // 3x3, not 5x5: 25 taps per pixel in both eyes at 180 frames a second cost
    // the pair rate - 58..87 pairs/s instead of 90, and every missing pair
    // was a repeated frame in the headset.
    // Only neighbours whose fog is near this pixel's own are taken (within 0.25):
    // averaged whole, the far water beside a man got a share of his near-nothing
    // and stood round him as a dark rim FogBlur pixels wide (2026-10-05). The far
    // ground's holes and specks melt as before - their fog is the same.
    float t = FogAmount(dFar);
    if (FogBlur >= 0.5) {
        const float t0 = t;
        float sum = 0.0, wsum = 0.0;
        [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x) {
                const float ts = FogAmount(tex2Dlod(FogDepthSmp, float4(duv + float2(x, y) * px * FogBlur, 0, 0)).x);
                const float w = 1.0 - saturate(abs(ts - t0) * 4.0);
                sum += ts * w; wsum += w;
            }
        t = (sum + t0) / (wsum + 1.0);
    }
    if (ShowDistance) return t.xxx;
    return lerp(colour, fogColour, t);
}
// The interface keeps its own colour: the fog and the sky only where the layer lets the world through.
float3 DepthFog(float4 pos, float2 uv, int eye)
{
    const float3 fogged = DepthFogWorld(pos, uv, eye);
    [branch] if (!UiMaskOn) return fogged;
    const float ui = 1.0 - saturate(tex2Dlod(GameLayerSmp, float4(uv, 0, 0)).a);
    return lerp(fogged, tex2D(FogColorSmp, uv).rgb, ui);
}
float3 PS_DepthFog(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return DepthFog(pos, uv, 0); }
float3 PS_DepthFogR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return DepthFog(pos, uv, 1); }

bool SameInBothEyes(float2 uv)
{
    if (uv.y < HudTop || uv.x < 0.0 || uv.x > 1.0) return false;
    // the pixel and the ones above and below: one equal pixel is noise
    // (written out: as a loop the compiler would not unroll it, and the whole effect failed)
    const float2 px = float2(0.0, 1.0 / BUFFER_HEIGHT);
    const float3 d0 = abs(tex2Dlod(HudCurSmp, float4(uv - px, 0, 0)).rgb - tex2Dlod(HudPrevSmp, float4(uv - px, 0, 0)).rgb);
    const float3 d1 = abs(tex2Dlod(HudCurSmp, float4(uv, 0, 0)).rgb - tex2Dlod(HudPrevSmp, float4(uv, 0, 0)).rgb);
    const float3 d2 = abs(tex2Dlod(HudCurSmp, float4(uv + px, 0, 0)).rgb - tex2Dlod(HudPrevSmp, float4(uv + px, 0, 0)).rgb);
    const float3 d = max(d0, max(d1, d2));
    return max(d.r, max(d.g, d.b)) <= HudTol;
}

// 1 where the frames agree now, else what it was, fading over three frames.
float PS_HudMask(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (SameInBothEyes(uv)) return 1.0;
    return max(tex2D(HudMaskOldSmp, uv).r - 0.34, 0.0);
}
float PS_HudMaskStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2D(HudMaskSmp, uv).r; }

bool IsHud(float2 uv)
{
    return uv.y >= HudTop && uv.x >= 0.0 && uv.x <= 1.0 && tex2Dlod(HudMaskSmp, float4(uv, 0, 0)).r > 0.0;
}

// The frame as the game drew it, before the fog: what the next frame compares with.
float4 PS_HudCopy(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2D(FogColorSmp, uv); }
float4 PS_HudStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2D(HudCurSmp, uv); }

// HUD pixels come from where the HUD was; where it left, the world beside it fills in.
float3 Hud(float2 uv, float shift)
{
    const float3 c = tex2D(FogColorSmp, uv).rgb;
    if (shift == 0.0 || uv.y < HudTop) return c;
    const float2 src = uv - float2(shift, 0.0);
    if (IsHud(src) || IsHud(uv)) return tex2D(FogColorSmp, src).rgb;
    return c;
}
float3 PS_Hud(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Hud(uv, HudShift); }
float3 PS_HudR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Hud(uv, -HudShift); }

// The game's toolbar and corner map drawn back at a size of their own, from
// above and from behind (D2R VR Settings > Interface: toolbar / map size in the
// picture). vrcam takes the piece out of the game's interface layer and binds
// its copy here: premultiplied colour, alpha = how much of the world shows
// through, the way the game's own last pass lays the layer over the world.
// After the fog, so neither gets fogged; moved apart per eye like the HUD.
uniform bool GameBarOn < hidden = true; > = false;
uniform float4 GameBarBox < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);   // uv: left, top, right, bottom
uniform float GameBarShift < hidden = true; > = 0.0;   // uv, the toolbar alone: + nearer, - farther (added to HudShift)
uniform bool GameMapOn < hidden = true; > = false;
uniform float4 GameMapBox < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
texture2D GameBarTex : D2R_GAME_BAR;
texture2D GameMapTex : D2R_GAME_MAP;
sampler2D GameBarSmp { Texture = GameBarTex; AddressU = CLAMP; AddressV = CLAMP; };
sampler2D GameMapSmp { Texture = GameMapTex; AddressU = CLAMP; AddressV = CLAMP; };

// From above in stereo: the rest of the interface (labels over monsters, the
// target's name, chat) taken out whole and laid on a plane tilted like the
// ground, so a label near the bottom of the screen stands as near as its
// monster. Per eye the row y moves by LabelBase + LabelTilt y (uv; the top row
// stays where the game puts it, the bottom moves the most;
// left eye right = nearer). The toolbar's strip and the map's box (LabelKeep0/1,
// where they are in the game's picture) stay flat and move like the HUD.
// On the floor (VR F3, vrcam [hud_floor] labels_alpha) the same layer comes out
// where the game puts it (no base, no tilt) only to be faded: LabelAlpha - only
// while vrcam cannot fade the label boxes through the game's own code.
uniform bool LabelsOn < hidden = true; > = false;
uniform float LabelBase < hidden = true; > = 0.0;
uniform float LabelTilt < hidden = true; > = 0.0;
uniform float LabelAlpha < hidden = true; > = 1.0;
uniform float4 LabelKeep0 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 LabelKeep1 < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);

bool InBox(float2 uv, float4 box) { return uv.x >= box.x && uv.y >= box.y && uv.x < box.z && uv.y < box.w; }
bool Kept(float2 uv) { return InBox(uv, LabelKeep0) || InBox(uv, LabelKeep1); }

// A label's pixel faded by LabelAlpha: its cover (1 - a) is cut, the more the darker
// the colour under it - the boxes ({0,0,0,0.6} in the game) fade, the names keep
// their cover and colour. The colour, premultiplied, stays: the box adds none.
float4 FadedLabel(float4 l)
{
    const float cover = 1.0 - saturate(l.a);
    const float lit = max(l.r, max(l.g, l.b)) / max(cover, 1e-3);   // the colour under the cover
    const float k = lerp(LabelAlpha, 1.0, smoothstep(0.1, 0.3, lit));
    return float4(l.rgb, 1.0 - cover * k);
}

float4 GameLabels(float2 uv, float eyeSign)
{
    const float2 flat = uv - float2(eyeSign * HudShift, 0.0);
    if (Kept(flat)) return tex2Dlod(GameLayerSmp, float4(flat, 0, 0));
    const float2 src = uv - float2(eyeSign * (LabelBase + LabelTilt * uv.y), 0.0);
    if (src.x < 0.0 || src.x > 1.0 || Kept(src)) return float4(0.0, 0.0, 0.0, 1.0);
    const float4 l = tex2Dlod(GameLayerSmp, float4(src, 0, 0));
    if (LabelAlpha < 0.999) return FadedLabel(l);
    return l;
}

// The interface's own bright pixels (names, numbers, icons) in this frame's layer -
// out (LabelsOn) or left in the picture (UiMaskOn) - for the floor's key (TableKey),
// which must never take them for the void. A box alone is dark: not one of them.
bool InterfaceLit(float2 uv, float eyeSign)
{
    if (!LabelsOn && !UiMaskOn) return false;
    float4 l = tex2Dlod(GameLayerSmp, float4(uv, 0, 0));
    if (LabelsOn) l = GameLabels(uv, eyeSign);
    return l.a < 0.5 && max(l.r, max(l.g, l.b)) > 0.04;
}

float4 GamePiece(sampler2D s, float4 box, float2 uv)
{
    const float2 t = (uv - box.xy) / (box.zw - box.xy);
    if (t.x < 0.0 || t.y < 0.0 || t.x > 1.0 || t.y > 1.0) return float4(0.0, 0.0, 0.0, 1.0);
    return tex2Dlod(s, float4(t, 0, 0));
}

// The toolbar and the map drawn back, as one premultiplied layer (alpha = the world through).
// GameBarBox may be anywhere on the screen: [hud_*] bar_x / bar_y move it.
float4 GamePieces(float2 uv, float eyeSign)
{
    const float2 src = uv - float2(eyeSign * HudShift, 0.0);
    float4 p = float4(0.0, 0.0, 0.0, 1.0);
    [branch] if (GameBarOn) p = GamePiece(GameBarSmp, GameBarBox, src - float2(eyeSign * GameBarShift, 0.0));
    [branch] if (GameMapOn)
    {
        const float4 m = GamePiece(GameMapSmp, GameMapBox, src);   // the map over the toolbar where they meet
        p = float4(m.rgb + p.rgb * m.a, p.a * m.a);
    }
    return p;
}

// eyeSign: +1 the left eye (moves right = nearer), -1 the right one
float3 GameHud(float2 uv, float eyeSign)
{
    float3 c = tex2D(FogColorSmp, uv).rgb;
    [branch] if (LabelsOn)
    {
        const float4 l = GameLabels(uv, eyeSign);
        c = c * saturate(l.a) + l.rgb;
    }
    const float4 p = GamePieces(uv, eyeSign);
    return c * saturate(p.a) + p.rgb;
}
// vrcam [debug] phantom_ray: the crossbow's shot line (where the bolt goes and the left
// hand takes it), from the hand 1.5 m on, as a red line - the model must lie on it.
// The ends are relative to the eyes' middle, in the world; each eye projects them.
uniform bool PhantomOn < hidden = true; > = false;
uniform float3 PhantomA < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 PhantomB < hidden = true; > = float3(0.0, 0.0, 1.0);
// and the crossbow bone's own axes from its origin: X green, Y blue, Z yellow
uniform bool BoneOn < hidden = true; > = false;
uniform float3 BoneO < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 BoneX < hidden = true; > = float3(1.0, 0.0, 0.0);
uniform float3 BoneY < hidden = true; > = float3(0.0, 1.0, 0.0);
uniform float3 BoneZ < hidden = true; > = float3(0.0, 0.0, 1.0);
// Those points are relative to where the hands hang (between the eyes); this is each eye's
// camera from there, taken off before projecting - without it both eyes saw the lines from
// the middle, with no parallax, and they floated far behind the hand (2026-10-06).
uniform float3 PhantomEye0 < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 PhantomEye1 < hidden = true; > = float3(0.0, 0.0, 0.0);

// A point relative to the eye -> its pixel; z < 0 behind the eye.
float3 PhantomPixel(float3 v, int eye)
{
    const float3 R = eye == 0 ? CamRight0 : CamRight1, U = eye == 0 ? CamUp0 : CamUp1, B = eye == 0 ? CamBack0 : CamBack1;
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    v -= eye == 0 ? PhantomEye0 : PhantomEye1;
    const float3 c = float3(dot(v, R), dot(v, U), dot(v, B));
    const float w = max(-c.z, 1e-4);
    const float2 ndc = float2(c.x / w * sp.x - sp.z, c.y / w * sp.y - sp.w);
    return float3((ndc.x * 0.5 + 0.5) * BUFFER_WIDTH, (0.5 - ndc.y * 0.5) * BUFFER_HEIGHT, -c.z);
}
// How far ahead of this eye a point is (world units; < 0 behind it).
float PhantomAhead(float3 v, int eye)
{
    v -= eye == 0 ? PhantomEye0 : PhantomEye1;
    return -dot(v, eye == 0 ? CamBack0 : CamBack1);
}
float3 Segment(float3 c, float2 p, float3 a3, float3 b3, int eye, float3 col)
{
    // A segment running past the eye is cut where it passes it, not dropped whole: a long
    // grab line along an axe went behind the camera and was never drawn (2026-10-06).
    const float near = 0.05, za = PhantomAhead(a3, eye), zb = PhantomAhead(b3, eye);
    if (za <= near && zb <= near) return c;
    if (za < near) a3 = lerp(a3, b3, (near - za) / (zb - za));
    if (zb < near) b3 = lerp(b3, a3, (near - zb) / (za - zb));
    const float3 a = PhantomPixel(a3, eye), b = PhantomPixel(b3, eye);
    if (a.z <= 0.01 || b.z <= 0.01) return c;
    const float2 ab = b.xy - a.xy;
    const float t = saturate(dot(p - a.xy, ab) / max(dot(ab, ab), 1e-4));
    const float d = length(p - (a.xy + ab * t));
    return lerp(c, col, 1.0 - smoothstep(1.0, 2.5, d));
}
float3 Phantom(float4 pos, float2 uv, int eye)
{
    float3 c = tex2D(FogColorSmp, uv).rgb;
    if (PhantomOn) c = Segment(c, pos.xy, PhantomA, PhantomB, eye, float3(1.0, 0.05, 0.05));
    if (BoneOn) {
        c = Segment(c, pos.xy, BoneO, BoneX, eye, float3(0.1, 1.0, 0.1));
        c = Segment(c, pos.xy, BoneO, BoneY, eye, float3(0.2, 0.4, 1.0));
        c = Segment(c, pos.xy, BoneO, BoneZ, eye, float3(1.0, 0.9, 0.1));
    }
    return c;
}
float3 PS_Phantom(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Phantom(pos, uv, 0); }
float3 PS_PhantomR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Phantom(pos, uv, 1); }

float3 PS_GameHud(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return GameHud(uv, 1.0); }
float3 PS_GameHudR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return GameHud(uv, -1.0); }

// The frame's stamp for FlatVR: when vrcam built this frame's view, and so
// which head pose it is drawn for. 48 black/white pixels at the right end of
// the bottom row - 8 magic bits (0xB2), the 32-bit stamp, 8 check bits (the
// stamp's bytes xor'ed) - which FlatVR reads from the very texture it shows
// and paints over. A stamp in shared memory ran ahead of the GPU copy of the
// picture by a frame or two, so the screen sat at the wrong head pose.
uniform bool StampOn < hidden = true; > = false;
uniform uint FrameStamp0 < hidden = true; > = 0;   // the left eye's, and the right eye's
uniform uint FrameStamp1 < hidden = true; > = 0;

float3 Stamp(float4 pos, float2 uv, uint stamp)
{
    const float3 c = tex2D(FogColorSmp, uv).rgb;
    const int x = int(pos.x), y = int(pos.y);
    const int i = x - (BUFFER_WIDTH - 48);
    if (!StampOn || y != BUFFER_HEIGHT - 1 || i < 0) return c;
    const uint sum = (stamp ^ (stamp >> 8) ^ (stamp >> 16) ^ (stamp >> 24)) & 0xFFu;
    uint bit;
    if (i < 8) bit = (0xB2u >> uint(i)) & 1u;
    else if (i < 40) bit = (stamp >> uint(i - 8)) & 1u;
    else bit = (sum >> uint(i - 40)) & 1u;
    return bit != 0u ? float3(1.0, 1.0, 1.0) : float3(0.0, 0.0, 0.0);
}
float3 PS_Stamp(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Stamp(pos, uv, FrameStamp0); }
float3 PS_StampR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return Stamp(pos, uv, FrameStamp1); }

// The table (vrcam's VR view F5): the game's view from above in a Meta Quest
// over passthrough. The headset keys by brightness (the bridge's Blend
// passthrough: what is darker than a threshold, ~0.012, shows the room) -
// brightness, not a colour key: the video keeps it at full resolution, a
// colour at half (4:2:0) and with a fringe. So the void round the world is
// made exactly black, and everything of the game is lifted to at least
// TableFloor, so its own shadows and night are never cut. The void is the
// pixel and its neighbours two pixels out with no depth AND dark - what the
// interface draws over the void is bright and stays.
uniform bool TableKeyOn < hidden = true; > = false;
uniform float TableFloor < hidden = true; > = 0.05;   // vrcam, [table] floor: how far off black the game is lifted
// vrcam, [table] background: what the void is filled with - black for the bridge's
// brightness key, or a chroma key colour (pure green...) for Virtual Desktop's own.
uniform float3 TableBackground < hidden = true; > = float3(0.0, 0.0, 0.0);

// The diorama's edge: a pixel whose ground point - straight down from what it
// shows, so a wall at the edge stays whole - is outside the game's own view
// (times TableBounds) is the void too. vrcam: this eye's ray (SkyProj, Cam*),
// its eye less the hero, the game's view x projection by columns, the hero.
uniform bool TableBoundsOn < hidden = true; > = false;
uniform float TableBounds < hidden = true; > = 1.0;
uniform float3 TableEye0 < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float3 TableEye1 < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float4 TableGameX < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 TableGameY < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float4 TableGameW < hidden = true; > = float4(0.0, 0.0, 0.0, 1.0);
uniform float3 TableHero < hidden = true; > = float3(0.0, 0.0, 0.0);
uniform float2 TableTurn < hidden = true; > = float2(1.0, 0.0);   // [table] turn as cos, sin: the world turned under the board

bool OutsideTable(float2 uv, float d, int eye)
{
    if (!TableBoundsOn || d <= 0.0) return false;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0);
    const float3 off = eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1;
    // reverse-Z with an infinite far plane: depth = near / distance along the view
    float3 p = (eye == 0 ? TableEye0 : TableEye1) + off * (NearPlane / d);
    p.y = 0.0;   // the ground under it, at the hero's feet
    // the board stays where it was put while the world turns under it: undo the turn
    p = float3(p.x * TableTurn.x - p.z * TableTurn.y, 0.0, p.x * TableTurn.y + p.z * TableTurn.x);
    const float4 w = float4(p + TableHero, 1.0);
    const float cw = dot(w, TableGameW);
    if (cw <= 1e-5) return true;
    const float2 g = float2(dot(w, TableGameX), dot(w, TableGameY)) / cw;
    return max(abs(g.x), abs(g.y)) > TableBounds;
}

float3 TableKey(float2 uv, int eye)
{
    const float3 c = tex2D(FogColorSmp, uv).rgb;
    if (!TableKeyOn || tex2Dsize(FogDepthSmp, 0).x < 16) return c;
    // The toolbar and the map drawn back (PS_GameHud, [hud_floor]) are never the void,
    // wherever bar_x / bar_y put them - over the room or past the board's edge: kept,
    // their dark lifted like the game's so the headset's key does not cut them.
    [branch] if (GameBarOn || GameMapOn)
    {
        if (GamePieces(uv, eye == 0 ? 1.0 : -1.0).a < 0.5)
            return max(c, float3(TableFloor, TableFloor, TableFloor));
    }
    // The names over items and monsters likewise ([hud_floor] labels_*), taken out to be
    // faded or left in the picture: their letters are never the void, wherever the board's
    // edge or no depth below would key them. A box alone keys like any dark (2026-10-06).
    if (InterfaceLit(uv, eye == 0 ? 1.0 : -1.0))
        return max(c, float3(TableFloor, TableFloor, TableFloor));
    const float2 duv = UpsideDown ? float2(uv.x, 1.0 - uv.y) : uv;
    const float2 px = 2.0 * float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
    float d = tex2Dlod(FogDepthSmp, float4(duv, 0, 0)).x;
    d = max(d, tex2Dlod(FogDepthSmp, float4(duv + float2(px.x, 0), 0, 0)).x);
    d = max(d, tex2Dlod(FogDepthSmp, float4(duv - float2(px.x, 0), 0, 0)).x);
    d = max(d, tex2Dlod(FogDepthSmp, float4(duv + float2(0, px.y), 0, 0)).x);
    d = max(d, tex2Dlod(FogDepthSmp, float4(duv - float2(0, px.y), 0, 0)).x);
    const float top = max(c.r, max(c.g, c.b));
    // Nothing drawn here. Dark: the void. Bright: the interface, or something drawn
    // without depth - rain (kept: "it does not get in the way") and grass at the end of
    // the drawn ground, which stood in the room as a strip past the board's edge
    // (2026-10-05). The grass stands on ground a few pixels below it, and that ground
    // is off the board; rain over the void has nothing near below it.
    if (d <= 0.0) {
        // (the interface's letters were kept above; its dark - a label's box - keys as before)
        if (IsHud(uv)) return c;
        if (top < 0.08) return TableBackground;
        const float down = UpsideDown ? -BUFFER_RCP_HEIGHT : BUFFER_RCP_HEIGHT;
        // the nearest drawn pixel 2, 4 ... 64 pixels down (no return inside: the loop must unroll)
        float hitOff = 0.0, hitD = 0.0;
        [unroll] for (int k = 1; k <= 6; ++k) {
            const float off = exp2((float)k);
            const float dd = tex2Dlod(FogDepthSmp, float4(duv + float2(0.0, down * off), 0, 0)).x;
            const bool take = hitD <= 0.0 && dd > 0.0;
            hitOff = take ? off : hitOff;
            hitD = take ? dd : hitD;
        }
        if (hitD > 0.0 && OutsideTable(uv + float2(0.0, BUFFER_RCP_HEIGHT * hitOff), hitD, eye)) return TableBackground;
        return c;
    }
    // A pixel with no depth of its own beside one that has it (the foam and glints
    // along the far water's edge) is tested at its neighbour's depth: at its own,
    // zero, it was never outside and stood on the black at the horizon (2026-10-05).
    const float dc = tex2Dlod(FogDepthSmp, float4(duv, 0, 0)).x;
    if (OutsideTable(uv, dc > 0.0 ? dc : d, eye)) return TableBackground;
    return top < TableFloor ? max(c, float3(TableFloor, TableFloor, TableFloor)) : c;
}
float3 PS_TableKey(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return TableKey(uv, 0); }
float3 PS_TableKeyR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return TableKey(uv, 1); }

// The left eye's frames (and mono). vrcam keeps one of the two on: before
// each present, the one of the eye that present is for.
technique D2R_DepthFog <
    ui_tooltip = "Distance fog and sky for D2R with the vrcam plugin (reads raw reverse-Z depth). Left eye / mono.";
>
{
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudCopy; RenderTarget = HudCurTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMask; RenderTarget = HudMaskTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFog; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurH; RenderTarget = VoidFogTmpTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurV; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DepthFog; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Hud; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_GameHud; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Phantom; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudStore; RenderTarget = HudPrevTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMaskStore; RenderTarget = HudMaskOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TableKey; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Stamp; }
}

technique D2R_DepthFog_R <
    ui_tooltip = "The same for the right eye's frames in AFR stereo; vrcam switches between the two.";
>
{
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudCopy; RenderTarget = HudCurTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMask; RenderTarget = HudMaskTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFog; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurH; RenderTarget = VoidFogTmpTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidFogBlurV; RenderTarget = VoidFogTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DepthFogR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_GameHudR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_PhantomR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudStore; RenderTarget = HudPrevTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMaskStore; RenderTarget = HudMaskOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TableKeyR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_StampR; }
}
