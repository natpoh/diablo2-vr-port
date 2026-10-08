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
uniform float CeilFineDetail <
    ui_type = "slider"; ui_min = 0.0; ui_max = 2.0; ui_step = 0.05;
    ui_label = "Ceiling fine detail";
    ui_tooltip = "A finer layer of the ceiling's own stone picture over it, near the eye - 0 = none.";
> = 0.7;
uniform bool ShowCeilingCut <
    ui_label = "Show ceiling cut (tuning)";
    ui_tooltip = "Under a cave ceiling: what the game drew, by its height over the hero's floor -\n"
                 "blue low to green at the ceiling, red above it (cut), a white line every 5 units.";
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

// The cave ceiling (docs/plan_cave_ceiling.md), set by vrcam ([ceiling]) in the
// biomes on its list, from inside only: in the void, where the sky is outdoors,
// each eye's ray meets a plane CeilHeight over the hero's floor - so it stands
// at its true depth in stereo. Uses the sky's per-eye axes and projection.
// First step, the test: a grey plane with a grid (stereo and height in the headset).
uniform bool CeilOn < hidden = true; > = false;
uniform float CeilHeight < hidden = true; > = 30.0;     // over the hero's floor, world units
uniform float CeilScale < hidden = true; > = 10.0;      // world units per tile (the grid's cell)
uniform float CeilBrightness < hidden = true; > = 1.0;
uniform float3 CeilEye0 < hidden = true; > = float3(0.0, 7.0, 0.0);   // each eye less the hero (his feet)
uniform float3 CeilEye1 < hidden = true; > = float3(0.0, 7.0, 0.0);
uniform float2 CeilHero0 < hidden = true; > = float2(0.0, 0.0);       // the hero's x, z wrapped on 64 tiles
uniform float2 CeilHero1 < hidden = true; > = float2(0.0, 0.0);

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
// The part of the depth buffer the game draws the scene into (vrcam, from the game's
// viewport): 1 without an upscaler; DLSS draws at 0.5 - 0.67 of the screen into the top
// left of a screen-size buffer, so every read of the depth goes through DepthAt (2026-10-08).
uniform float2 DepthScale < hidden = true; > = float2(1.0, 1.0);
float4 DepthAt(float2 duv) { return tex2Dlod(FogDepthSmp, float4(duv * DepthScale, 0, 0)); }

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
uniform bool UiMaskOff <
    ui_label = "Ignore the interface mask (test)";
    ui_tooltip = "Fog and sky over everything, the game's interface layer not looked at - to see whether a straight line "
                 "across the picture comes from it (act 2 town, 2026-10-07).";
> = false;

// How much fog a depth value gets, 0..1. Empty depth (the void) is far: full fog.
// How much the full fog covers, 0..1 (vrcam [fog] caves_strength in caves, else 1): below 1 the far end shows through.
uniform float FogStrength < hidden = true; > = 1.0;
float FogAt(float dist)
{
    return pow(saturate((dist - FogStart) / max(FogEnd - FogStart, 1.0)), FogCurve) * FogStrength;
}
float FogAmount(float depth)
{
    return FogAt(NearPlane / max(depth, 1e-7));    // reverse-Z infinite: d = near / z
}

// Value noise that repeats every `per` lattice cells: the ceiling's world x, z
// come wrapped on 64 tiles (vrcam), so everything on it must repeat with them.
float HashP(float2 i, float per) { return Hash(i - per * floor(i / per)); }
float NoiseP(float2 p, float per)
{
    const float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(HashP(i, per), HashP(i + float2(1.0, 0.0), per), f.x),
                lerp(HashP(i + float2(0.0, 1.0), per), HashP(i + float2(1.0, 1.0), per), f.x), f.y);
}

// How far the rock hangs down at q (tiles: world x, z / CeilScale), 0 = up at the
// plane .. 1 = CeilRelief below it. Lumpy stone: four octaves, 2 tiles and finer.
// (Stalactites as blunt cones in half the 2x2-tile cells were taken out: they read
// as little pyramids hanging in rows - the user, 2026-10-06.)
float CeilRockNoise(float2 q)
{
    float v = 0.0, a = 0.5, f = 0.5;
    [unroll] for (int i = 0; i < 4; ++i) { v += a * NoiseP(q * f, 64.0 * f); f *= 2.0; a *= 0.5; }
    return saturate(0.55 * v);
}

uniform float CeilRelief < hidden = true; > = 6.0;   // world units the rock hangs down, 0 = flat

// The cave ceiling along this eye's ray (dir: unit, in the world); false where the
// ray never meets it - looking level or down, or the eye above it - and the void
// stays as it was. Rock hanging from a plane CeilHeight over the hero's floor,
// CeilRelief deep: the ray is marched through that band (CeilSteps steps, then
// a secant between the last two). Lit from the hero, fading with the distance
// from him, and dimly from one fixed side; the hollows darker; the fog by its
// distance from the eye, as the walls get.
uniform int CeilSteps < hidden = true; > = 12;
uniform float CeilLightRadius < hidden = true; > = 25.0;   // world units from the hero where his light is down to half
// The stone's pictures, one per kind of dungeon (vrcam [ceiling] texture_<biome>; drawn by
// tools/casc_extract/gen_ceiling_from_ref.py): 4 slots, D2R_CEILING_1..4, the area's in
// CeilTexSlot - the act 1 caves' rock, the crypts' brick (2026-10-07). One picture spans
// 4 tiles: 64, the wrap, is a whole number of them. Without a file (CeilTexOn off) the
// stone is drawn here. A slot vrcam has no file for keeps one that is there, never sampled.
#ifndef D2R_CEILING_1
 #define D2R_CEILING_1 "D2R_Sky_ours/D2R_Ceiling_act1_caves_walls.png"
#endif
#ifndef D2R_CEILING_2
 #define D2R_CEILING_2 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_3
 #define D2R_CEILING_3 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_4
 #define D2R_CEILING_4 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_5
 #define D2R_CEILING_5 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_6
 #define D2R_CEILING_6 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_7
 #define D2R_CEILING_7 D2R_CEILING_1
#endif
#ifndef D2R_CEILING_8
 #define D2R_CEILING_8 D2R_CEILING_1
#endif
uniform bool CeilTexOn < hidden = true; > = true;
uniform float CeilTexSlot < hidden = true; > = 1.0;
texture2D CeilTex1 < source = D2R_CEILING_1; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex2 < source = D2R_CEILING_2; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex3 < source = D2R_CEILING_3; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex4 < source = D2R_CEILING_4; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex5 < source = D2R_CEILING_5; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex6 < source = D2R_CEILING_6; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex7 < source = D2R_CEILING_7; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
texture2D CeilTex8 < source = D2R_CEILING_8; > { Width = 1024; Height = 1024; Format = RGBA8; MipLevels = 11; };
sampler2D CeilSmp1 { Texture = CeilTex1; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp2 { Texture = CeilTex2; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp3 { Texture = CeilTex3; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp4 { Texture = CeilTex4; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp5 { Texture = CeilTex5; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp6 { Texture = CeilTex6; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp7 { Texture = CeilTex7; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
sampler2D CeilSmp8 { Texture = CeilTex8; AddressU = WRAP; AddressV = WRAP; MipFilter = LINEAR; };
static const float kCeilTexTiles = 4.0;
// World units one picture spans - its own per dungeon (vrcam [ceiling] tex_size_<biome>; 0 = 4 tiles):
// the sewers' brick wanted its own size, the tile grid (the height map) stays (the user, 2026-10-07)
uniform float CeilTexSize < hidden = true; > = 0.0;
float TexSpan() { return CeilTexSize > 0.5 ? CeilTexSize : kCeilTexTiles * CeilScale; }
float3 CeilPic(float2 uv, float lod)
{
    const int slot = (int)(CeilTexSlot + 0.5);
    if (slot == 2) return tex2Dlod(CeilSmp2, float4(uv, 0, lod)).rgb;
    if (slot == 3) return tex2Dlod(CeilSmp3, float4(uv, 0, lod)).rgb;
    if (slot == 4) return tex2Dlod(CeilSmp4, float4(uv, 0, lod)).rgb;
    if (slot == 5) return tex2Dlod(CeilSmp5, float4(uv, 0, lod)).rgb;
    if (slot == 6) return tex2Dlod(CeilSmp6, float4(uv, 0, lod)).rgb;
    if (slot == 7) return tex2Dlod(CeilSmp7, float4(uv, 0, lod)).rgb;
    if (slot == 8) return tex2Dlod(CeilSmp8, float4(uv, 0, lod)).rgb;
    return tex2Dlod(CeilSmp1, float4(uv, 0, lod)).rgb;
}

// The relief from the picture itself, not noise (the user, 2026-10-07: the sewers' bricks were flat
// paint): what stands out of the vault by CeilRelief - the light parts (1), the dark (-1), or the
// coloured (2: bricks in grey mortar). Against the picture's own blur, so the whole stays level.
// vrcam [ceiling] relief_pic_<biome>; 0 = the old lumpy noise.
uniform float CeilReliefPic < hidden = true; > = 0.0;
float PicRock(float2 q)
{
    const float2 uv = q * CeilScale / TexSpan();
    const float3 c = CeilPic(uv, 1.0), m = CeilPic(uv, 6.0);
    const int mode = (int)round(CeilReliefPic);
    float v, a;
    if (mode == 2) {
        v = (max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b))) / max(max(c.r, max(c.g, c.b)), 1e-3);
        a = (max(m.r, max(m.g, m.b)) - min(m.r, min(m.g, m.b))) / max(max(m.r, max(m.g, m.b)), 1e-3);
    } else {
        v = dot(c, float3(0.2126, 0.7152, 0.0722)); a = dot(m, float3(0.2126, 0.7152, 0.0722));
        if (mode < 0) { v = 1.0 - v; a = 1.0 - a; }
    }
    // the raised parts at 1 (hanging down the most), the joints at 0; bevelled edges, not cliffs
    // (at x5 the bricks' sides stood as lit icicles - 2026-10-07)
    return smoothstep(0.0, 1.0, saturate(0.5 + (v - a) * 2.5));
}
bool PicRelief() { return CeilTexOn && abs(CeilReliefPic) > 0.5; }
float CeilRock(float2 q) { return PicRelief() ? PicRock(q) : CeilRockNoise(q); }

// Torches light the ceiling (the user, 2026-10-06), found in the picture: fire is
// bright and warm. Each frame the brightest fire of each of 8 x 4 screen cells is
// placed in the world by the depth at it (TorchCandTex), and kept in a list of 16
// lights in the world (TorchTex) - matched to what is there already, new ones into
// free places - for CeilTorchMemory seconds after it was last seen, so the light
// stays when the torch leaves the view (looking straight up at the ceiling over it).
// Positions: the world's x, z wrapped on 64 tiles as the ceiling's; y over the hero's floor.
uniform bool CeilTorches < hidden = true; > = true;
uniform float CeilTorchRadius < hidden = true; > = 20.0;    // [ceiling] torch_radius: world units where a torch's light is down to half
uniform float CeilTorchBright < hidden = true; > = 0.6;     // [ceiling] torch_brightness
uniform float CeilTorchMemory < hidden = true; > = 8.0;     // seconds a torch out of sight still lights
uniform float CeilTorchWarm < hidden = true; > = 0.6;      // [ceiling] torch_warmth: the game's pale torch colour toward fire's orange
uniform float CeilHalo < hidden = true; > = 1.0;            // [ceiling] torch_halo: how much of the game's glow round a flame shows in a cave (1 = all)
uniform float FrameTime < source = "frametime"; >;
static const int kFireW = BUFFER_WIDTH / 16;
static const int kFireH = BUFFER_HEIGHT / 16;
texture2D FireTex { Width = BUFFER_WIDTH / 16; Height = BUFFER_HEIGHT / 16; Format = RGBA32F; };
sampler2D FireSmp { Texture = FireTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };
texture2D TorchCandTex { Width = 32; Height = 2; Format = RGBA32F; };   // row 0: place, fire; row 1: colour
sampler2D TorchCandSmp { Texture = TorchCandTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };
texture2D TorchTex { Width = 16; Height = 2; Format = RGBA32F; };       // row 0: place, strength; row 1: colour, life
sampler2D TorchSmp { Texture = TorchTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };
texture2D TorchOldTex { Width = 16; Height = 2; Format = RGBA32F; };
sampler2D TorchOldSmp { Texture = TorchOldTex; AddressU = CLAMP; AddressV = CLAMP; MinFilter = POINT; MagFilter = POINT; };

float4 Texel(sampler2D s, int i, int row, float w, float h) { return tex2Dlod(s, float4((i + 0.5) / w, (row + 0.5) / h, 0, 0)); }
// a to b, the shortest way round the wrap
float3 WrapDelta(float3 a, float3 b)
{
    const float P = 64.0 * CeilScale;
    float3 d = b - a;
    d.xz -= P * round(d.xz / P);
    return d;
}

// The walls are wet rock: a glint where a light's half-way vector meets the normal
// (Blinn), on top of the diffuse. The ceiling had none and read as dry mud beside them.
uniform float CeilWet < hidden = true; > = 0.35;       // [ceiling] wet: how much the rock shines
uniform float CeilBump < hidden = true; > = 0.8;       // [ceiling] detail: world units the picture's light parts stand out
uniform float CeilContrast < hidden = true; > = 1.6;   // [ceiling] contrast: the picture's fine detail times this
// A light's falloff with k = distance / its radius: a bright spot (1.78 at the light), down
// to 1 at the radius, gone at twice it - a quick fall-off, "a bright spot" as the user asked.
// The old 1 / (1 + k^2) was so flat that a torch 9 units under the ceiling lit it almost as
// much 13 units aside as straight above: an even wash, not a spot (the user, 2026-10-07).
float Falloff(float k) { const float x = saturate(1.0 - 0.25 * k * k); return x * x * 1.78; }
float Glint(float3 nrm, float3 toLight, float3 toEye) { return pow(saturate(dot(nrm, normalize(toLight + toEye))), 40.0); }

// The torches' light on a point of the ceiling (wh: world, wrapped; nrm: into the air;
// v: toward the eye). x: diffuse, the colour; the glint in w.
float4 TorchLight(float3 wh, float3 nrm, float3 v)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    float glint = 0.0;
    [loop] for (int i = 0; i < 16; ++i) {
        const float4 L = Texel(TorchSmp, i, 0, 16.0, 2.0);
        if (L.w <= 0.0) continue;
        const float4 C = Texel(TorchSmp, i, 1, 16.0, 2.0);
        const float3 dv = WrapDelta(wh, L.xyz);
        const float dist = length(dv);
        const float3 l = dv / max(dist, 1e-3);
        const float k = dist / max(CeilTorchRadius, 1.0);
        const float fall = Falloff(k) * L.w * saturate(C.w * 3.0);
        sum += C.rgb * fall * (0.35 + 0.65 * saturate(dot(nrm, l)));
        glint += fall * Glint(nrm, l, v);
    }
    return float4(sum, glint);
}

// The lit objects the game itself has near the hero (vrcam reads them from the game's
// rooms; their light from its Objects table): xyz less the hero (his feet), w the
// game's light radius (Lit: a torch 19). With them the fire in the picture is not
// looked for: the picture's depth at a flame was the wall behind it.
uniform bool GameLightsOn < hidden = true; > = false;
uniform float4 GameLightPos[16] < hidden = true; >;
uniform float4 GameLightCol[16] < hidden = true; >;

// Their light on a point of the ceiling (p: less the hero), as TorchLight; a torch's
// reach is CeilTorchRadius, the others' by their radius against a torch's 19. A slow
// flicker of a few percent, each its own, as a flame's light breathes.
float4 GameLight(float3 p, float3 nrm, float3 v)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    float glint = 0.0;
    [loop] for (int i = 0; i < 16; ++i) {
        const float4 L = GameLightPos[i];
        if (L.w <= 0.0) continue;
        const float3 dv = L.xyz - p;
        const float dist = length(dv);
        const float3 l = dv / max(dist, 1e-3);
        const float k = dist / max(CeilTorchRadius * L.w / 19.0, 1.0);
        const float flicker = 0.93 + 0.07 * sin(Timer * 0.011 + i * 2.3) * sin(Timer * 0.0037 + i * 1.1);
        // gone at 2 radii (Falloff), and with the light's distance from the hero (GameLightCol.w, vrcam [ceiling] torch_distance)
        const float fall = Falloff(k) * flicker * GameLightCol[i].w;
        // the game's light colours are pale (a torch's 255/236/176): on grey stone they only
        // brightened it - "not yellow enough" (the user, 2026-10-07); fire's orange mixed in
        const float3 col = lerp(GameLightCol[i].rgb, float3(1.0, 0.58, 0.22), saturate(CeilTorchWarm));
        sum += col * fall * (0.35 + 0.65 * saturate(dot(nrm, l)));
        glint += fall * Glint(nrm, l, v);
    }
    return float4(sum, glint);
}

// The far wall (2026-10-07, the user: "paint the void in the distance - the cave's
// texture round the hero, 200 away"): past the floor's edge the void stayed black under
// the ceiling. A cylinder of the same stone CeilWallDist round the hero closes the cave:
// what the ceiling's ray does not meet first (looking level or down, or the ceiling past
// the wall) meets the wall, sunk in the fog by its distance like the rest. It goes with
// the hero - far off in the fog that does not show. 0 = none.
uniform float CeilWallDist < hidden = true; > = 200.0;   // [ceiling] wall_distance
// And a floor under the real one (the user, 2026-10-07): a ray going down past the drawn
// floor's edge meets stone CeilFloorDepth under the hero's floor before the wall. 0 = none.
uniform float CeilFloorDepth < hidden = true; > = 5.0;  // [ceiling] floor_depth

// A dome over what stands higher than the ceiling (the user, 2026-10-07: the cathedral's
// altar canopy rose through it - "bend the ceiling there, evenly, as a sphere"). Every
// frame a compute pass puts each drawn pixel back in the world and keeps the highest
// point per tile in a 64 x 64 map over the wrap (HBits, atomic max, in 1/16 units); the
// map keeps the most ever seen until the area changes (CeilMapGen, vrcam). Over a tile
// whose top is above the ceiling, the ceiling rises as a sphere CeilDomeRadius round,
// CeilDomeClear over that top, by CeilDomeMax at most; elsewhere it stays CeilHeight.
uniform bool CeilDome < hidden = true; > = false;           // [ceiling] dome_<biome>
uniform float CeilDomeRadius < hidden = true; > = 30.0;     // [ceiling] dome_radius_<biome>
uniform float CeilDomeMax < hidden = true; > = 30.0;        // [ceiling] dome_max_<biome>
uniform float CeilDomeClear < hidden = true; > = 3.0;       // world units over the top it covers
// Only what reaches over this gets a dome (the user, 2026-10-07: "a slider to find where to round
// it" - over the altar's canopy, not the columns); under the ceiling's height it is that height.
uniform float CeilDomeFind < hidden = true; > = 0.0;        // [ceiling] dome_find_<biome>
float DomeLine() { return max(CeilHeight, CeilDomeFind); }
uniform float CeilMapGen < hidden = true; > = 0.0;          // vrcam: another area - the map starts again
texture2D HBitsTex { Width = 64; Height = 64; Format = R32U; };
storage2D<uint> HBitsSt { Texture = HBitsTex; };
sampler2D<uint> HBitsSmp { Texture = HBitsTex; };
texture2D SupHiTex { Width = 64; Height = 64; Format = R32U; };   // this frame's points over the dome's line
storage2D<uint> SupHiSt { Texture = SupHiTex; };
sampler2D<uint> SupHiSmp { Texture = SupHiTex; };
texture2D SupTex { Width = 64; Height = 64; Format = RGBA32F; };      // a: those points, kept (not one bat - many)
sampler2D SupSmp { Texture = SupTex; MinFilter = POINT; MagFilter = POINT; };
texture2D SupOldTex { Width = 64; Height = 64; Format = RGBA32F; };
sampler2D SupOldSmp { Texture = SupOldTex; MinFilter = POINT; MagFilter = POINT; };
texture2D HMapTex { Width = 64; Height = 64; Format = R32F; };      // the highest point per tile, kept
sampler2D HMapSmp { Texture = HMapTex; AddressU = WRAP; AddressV = WRAP; MinFilter = POINT; MagFilter = POINT; };
texture2D HMapOldTex { Width = 64; Height = 64; Format = R32F; };
sampler2D HMapOldSmp { Texture = HMapOldTex; MinFilter = POINT; MagFilter = POINT; };
texture2D HMapGenTex { Width = 1; Height = 1; Format = R32F; };     // the CeilMapGen HMapOld belongs to
sampler2D HMapGenSmp { Texture = HMapGenTex; MinFilter = POINT; MagFilter = POINT; };

void RecordHeights(uint3 id, int eye)
{
    if (!CeilOn || !CeilDome || id.x >= BUFFER_WIDTH || id.y >= BUFFER_HEIGHT) return;
    const float2 uv = (id.xy + 0.5) / float2(BUFFER_WIDTH, BUFFER_HEIGHT);
    const float d = DepthAt(UpsideDown ? float2(uv.x, 1.0 - uv.y) : uv).x;
    if (d <= 1e-6) return;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0) * (NearPlane / d);
    const float3 off = eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1;
    const float P = 64.0 * CeilScale;
    if (abs(off.x) > 0.45 * P || abs(off.z) > 0.45 * P) return;   // a tile once round the wrap is another place
    const float3 e = eye == 0 ? CeilEye0 : CeilEye1;
    const float h = e.y + off.y;
    if (h < 1.0) return;   // the floor
    const float2 w = (e.xz + off.xz + (eye == 0 ? CeilHero0 : CeilHero1)) / CeilScale;
    const int2 cell = int2(w - 64.0 * floor(w / 64.0)) & 63;
    atomicMax(HBitsSt, cell, (uint)(min(h, 2000.0) * 16.0));
    if (h > DomeLine()) atomicAdd(SupHiSt, cell, 1u);
}
void CS_RecordHeights(uint3 id : SV_DispatchThreadID) { RecordHeights(id, 0); }
void CS_RecordHeightsR(uint3 id : SV_DispatchThreadID) { RecordHeights(id, 1); }
void CS_ClearHeights(uint3 id : SV_DispatchThreadID)
{
    tex2Dstore(HBitsSt, int2(id.xy), 0u);
    tex2Dstore(SupHiSt, int2(id.xy), 0u);
}
// the points over the dome's line, kept (none kept from another area)
float4 PS_SupMerge(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const int2 c = int2(pos.xy);
    const bool same = abs(tex2Dfetch(HMapGenSmp, int2(0, 0)).x - CeilMapGen) < 0.5;
    const float old = same ? tex2Dfetch(SupOldSmp, c).a : 0.0;
    return float4(0.0, 0.0, 0.0, min(old + (float)tex2Dfetch(SupHiSmp, c).x, 1e6));
}
float4 PS_SupStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2Dfetch(SupSmp, int2(pos.xy)); }
// this frame's highest points on top of what was kept (none kept from another area)
float PS_HMapMerge(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    const int2 c = int2(pos.xy);
    const float now = tex2Dfetch(HBitsSmp, c).x / 16.0;
    const bool same = abs(tex2Dfetch(HMapGenSmp, int2(0, 0)).x - CeilMapGen) < 0.5;
    return max(now, same ? tex2Dfetch(HMapOldSmp, c).x : 0.0);
}
float PS_HMapStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2Dfetch(HMapSmp, int2(pos.xy)).x; }
float PS_HMapGenStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return CeilMapGen; }
// One smooth dome over each thing that stands over the ceiling (the user, 2026-10-07: "a flat
// ceiling, and over the church one even round dome" - a sphere per tile made it lumpy). A peak
// is a tile drawn over the ceiling often (not one bat) and the highest such within
// CeilDomeRadius; its dome stands over the middle of the tiles over the ceiling round it
// (weighted by how far they reach over), CeilDomeClear over the peak's top, an even half
// ellipsoid CeilDomeRadius round down to the ceiling. Each tile keeps its nearest peak, and a
// point takes the domes of the 3 x 3 tiles round it - all found in parallel, every frame.
texture2D PeakTex { Width = 64; Height = 64; Format = RGBA32F; };       // rg: the dome's middle (tiles, wrapped), b: its top, a: 1 = one
sampler2D PeakSmp { Texture = PeakTex; MinFilter = POINT; MagFilter = POINT; };
texture2D DomeSeedTex { Width = 64; Height = 64; Format = RGBA32F; };   // the nearest peak, as PeakTex
sampler2D DomeSeedSmp { Texture = DomeSeedTex; MinFilter = POINT; MagFilter = POINT; };
bool DomeTall(int2 c) { return tex2Dfetch(SupSmp, c & 63).a >= 100.0 && tex2Dfetch(HMapSmp, c & 63).x > DomeLine() + 0.5; }
float2 WrapTiles(float2 d) { return d - 64.0 * round(d / 64.0); }
float4 PS_Peaks(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilDome) return 0.0;
    const int2 c = int2(pos.xy);
    if (!DomeTall(c)) return 0.0;
    const float me = tex2Dfetch(HMapSmp, c).x;
    const float Rt = max(CeilDomeRadius, CeilScale) / CeilScale;   // tiles
    const int n = min((int)ceil(Rt), 8);
    float w = 0.0;
    float2 sum = 0.0;
    [loop] for (int y = -8; y <= 8; ++y) {
        if (abs(y) > n) continue;
        [loop] for (int x = -8; x <= 8; ++x) {
            if (abs(x) > n || length(float2(x, y)) > Rt) continue;
            const int2 cn = c + int2(x, y);
            if (!DomeTall(cn)) continue;
            const float h = tex2Dfetch(HMapSmp, cn & 63).x;
            // a higher one near (or as high, before this one): that one is the peak
            if (h > me || (h == me && (y < 0 || (y == 0 && x < 0)))) return 0.0;
            const float wi = h - DomeLine();
            w += wi; sum += wi * float2(x, y);
        }
    }
    const float2 mid = c + 0.5 + sum / max(w, 1e-3);
    return float4(mid - 64.0 * floor(mid / 64.0), me, 1.0);
}
float4 PS_DomeSeeds(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilDome) return 0.0;
    const int2 c = int2(pos.xy);
    const float Rt = max(CeilDomeRadius, CeilScale) / CeilScale;
    const int n = min((int)ceil(Rt) + 1, 9);
    float best = 1e6;
    float4 at = 0.0;
    [loop] for (int y = -9; y <= 9; ++y) {
        if (abs(y) > n) continue;
        [loop] for (int x = -9; x <= 9; ++x) {
            if (abs(x) > n) continue;
            const float4 pk = tex2Dfetch(PeakSmp, (c + int2(x, y)) & 63);
            if (pk.a < 0.5) continue;
            const float d = length(WrapTiles(pk.rg - (c + 0.5)));
            if (d < best) { best = d; at = pk; }
        }
    }
    return at;
}
// The ceiling's plane at q (tiles, wrapped): its own height, or a dome's
float CeilTopAt(float2 q)
{
    if (!CeilDome) return CeilHeight;
    float top = CeilHeight;
    const float R = max(CeilDomeRadius, CeilScale);
    const int2 c = int2(floor(q));
    [loop] for (int y = -1; y <= 1; ++y)
        [loop] for (int x = -1; x <= 1; ++x) {
            const float4 d = tex2Dfetch(DomeSeedSmp, (c + int2(x, y)) & 63);
            if (d.a < 0.5) continue;
            const float r = length(WrapTiles(q - d.rg)) * CeilScale / R;
            if (r >= 1.0) continue;
            top = max(top, CeilHeight + (d.b + CeilDomeClear - CeilHeight) * sqrt(1.0 - r * r));
        }
    return min(top, CeilHeight + max(CeilDomeMax, 0.0));
}

// The stone's underside at q (tiles, wrapped), over the hero's floor
float CeilUnder(float2 q)
{
    const float R = max(CeilRelief, 0.0);
    return CeilTopAt(q) - (R > 1e-3 ? R * CeilRock(q) : 0.0);
}

bool Ceiling(float3 dir, int eye, out float3 c, float tMax)
{
    c = float3(0.0, 0.0, 0.0);
    const float3 e = eye == 0 ? CeilEye0 : CeilEye1;
    const float2 hero = eye == 0 ? CeilHero0 : CeilHero1;   // the hero in the world (wrapped)
    const float R = max(CeilRelief, 0.0);
    const float3 back = eye == 0 ? CamBack0 : CamBack1;
    const float ahead = max(dot(dir, -back), 0.0);          // view depth per unit along the ray
    // the wall: where the ray leaves the cylinder (the eye is inside it)
    const float W = CeilWallDist;
    float tw = 1e9;
    if (W > 1.0) {
        const float qa = dot(dir.xz, dir.xz), qb = dot(e.xz, dir.xz), qc = dot(e.xz, e.xz) - W * W;
        if (qa > 1e-6) tw = (-qb + sqrt(max(qb * qb - qa * qc, 0.0))) / qa;
    }
    const bool ceilRay = CeilHeight - e.y > 0.0 && dir.y > 1e-3;
    // the floor under the floor: a plane, nearer than the wall
    float tf = 1e9;
    if (CeilFloorDepth > 0.0 && dir.y < -1e-3) tf = (-CeilFloorDepth - e.y) / dir.y;
    const bool onFloor = tf < tw;
    if (onFloor) tw = tf;
    if (!ceilRay && tw > 1e8) return false;
    float t = tw, h = 0.5;
    bool onWall = true;
    if (ceilRay) {
        // the band the rock hangs in: from its lowest reach to the plane (a dome's highest)
        const float topHi = CeilHeight + (CeilDome ? max(CeilDomeMax, 0.0) : 0.0);
        const float t0 = max(CeilHeight - R - e.y, 0.0) / dir.y;
        float t1 = min((topHi - e.y) / dir.y, tMax);
        if (t0 < tw) {
            // past the full fog nothing shows: the fog's colour, no march
            if (FogOn && FogStrength >= 0.999 && t0 * ahead > FogEnd) { c = FogColor; return true; }
            t = t1;
            if (R > 1e-3 || CeilDome) {
                if (FogOn) t1 = min(t1, FogEnd / max(ahead, 0.05));
                // the vault's thin ribs want finer steps than lumpy rock; a dome's band is tall
                const int n = clamp(CeilDome || PicRelief() ? max(CeilSteps, 32) : CeilSteps, 1, 64);
                const float dt = (t1 - t0) / n;
                float tPrev = t0, gPrev = -1.0;   // g: the ray's height above the rock's underside (> 0 = in the rock)
                [loop] for (int i = 0; i <= n; ++i) {
                    const float ti = t0 + dt * i;
                    const float2 q = (e.xz + dir.xz * ti + hero) / CeilScale;
                    const float g = e.y + dir.y * ti - CeilUnder(q);
                    if (g >= 0.0) {
                        t = ti;
                        if (i > 0) {
                            // halved 5 times between the last step outside and the first inside: the
                            // vault's edges against the far stone stood in steps (2026-10-07)
                            float ta = tPrev, tb = ti;
                            [loop] for (int b = 0; b < 5; ++b) {
                                const float tm = 0.5 * (ta + tb);
                                const float2 qm = (e.xz + dir.xz * tm + hero) / CeilScale;
                                if (e.y + dir.y * tm - CeilUnder(qm) >= 0.0) tb = tm; else ta = tm;
                            }
                            t = 0.5 * (ta + tb);
                        }
                        break;
                    }
                    tPrev = ti; gPrev = g;
                }
            }
            onWall = t > tw;   // the ceiling past the wall: the wall
            if (onWall) t = tw;
        }
    }
    const float3 hit = float3(e.x, e.y, e.z) + dir * t;     // less the hero (his feet)
    // Where on the stone, and its axes: on the ceiling the world's x, z (tiles); on the wall
    // round it and down (the angle round the hero times the radius, the height)
    float2 q;
    float3 nrm, tanU, tanV;
    if (!onWall) {
        q = (hit.xz + hero) / CeilScale;
        h = R > 1e-3 ? CeilRock(q) : 0.0;
        // the normal into the air (the rock is above): -(R dh/dx, 1, R dh/dz), and a dome's slope
        const float k = PicRelief() ? 0.02 : 0.08;   // tiles (a brick's joint is fine)
        const float ux = (CeilUnder(q + float2(k, 0.0)) - CeilUnder(q - float2(k, 0.0))) / (2.0 * k * CeilScale);
        const float uz = (CeilUnder(q + float2(0.0, k)) - CeilUnder(q - float2(0.0, k))) / (2.0 * k * CeilScale);
        nrm = normalize(float3(ux, -1.0, uz));
        tanU = float3(1.0, 0.0, 0.0); tanV = float3(0.0, 0.0, 1.0);
        if (abs(nrm.y) < 0.5) {
            // a raised wall: the picture along it and up it, not smeared down it from above
            const float2 side = normalize(nrm.xz);
            const float2 along = float2(-side.y, side.x);
            q = float2(dot(hit.xz + hero, along), -hit.y) / CeilScale;
            nrm = float3(side.x, 0.0, side.y);
            tanU = float3(along.x, 0.0, along.y); tanV = float3(0.0, -1.0, 0.0);
            h = 0.5;
        }
    } else if (onFloor) {
        q = (hit.xz + hero) / CeilScale;
        nrm = float3(0.0, 1.0, 0.0);
        tanU = float3(1.0, 0.0, 0.0); tanV = float3(0.0, 0.0, 1.0);
    } else {
        const float2 radial = hit.xz / max(length(hit.xz), 1e-3);
        q = float2(atan2(radial.y, radial.x) * W, -hit.y) / CeilScale;
        nrm = float3(-radial.x, 0.0, -radial.y);   // toward the hero
        tanU = float3(-radial.y, 0.0, radial.x); tanV = float3(0.0, -1.0, 0.0);
    }
    // The stone: the picture (its mip by one pixel's footprint, stretched where the ray
    // grazes), or dark brown-grey mottled. Its fine detail is pushed up (the picture less
    // its blur three mips up, times CeilContrast), and its light parts stand out of the
    // rock by CeilBump: the normal tilts with the picture's slope, so every grain and crack
    // catches the light the way the walls' normal maps do.
    float3 stone;
    if (CeilTexOn) {
        const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
        const float foot = t * 2.0 / (abs(sp.y) * BUFFER_HEIGHT) / max(-dot(dir, nrm), 0.05);   // world units
        const float lod = log2(max(foot / TexSpan() * 1024.0, 1.0));
        const float2 uvT = q * CeilScale / TexSpan();
        const float3 s0 = CeilPic(uvT, lod);
        const float3 blur = CeilPic(uvT, lod + 3.0);
        stone = max(blur + (s0 - blur) * CeilContrast, 0.0) * 0.55;
        // A finer layer: the same picture 5.3 times smaller, only its fine detail - one 1024 picture
        // over 40 units stood soft beside the game's walls (2026-10-07). Its own mips put it out far off.
        if (CeilFineDetail > 0.0) {
            const float k = 5.3, lodF = lod + log2(k);
            const float3 f0 = CeilPic(uvT * k + 0.37, lodF), f1 = CeilPic(uvT * k + 0.37, lodF + 2.0);
            stone = max(stone + (f0 - f1) * 0.55 * CeilFineDetail, 0.0);
        }
        const float du = exp2(lod) * 1.5 / 1024.0;   // a texel and a half at that mip, in uv
        const float3 lw = float3(0.2126, 0.7152, 0.0722);
        const float lx = dot(CeilPic(uvT + float2(du, 0.0), lod) - CeilPic(uvT - float2(du, 0.0), lod), lw);
        const float lz = dot(CeilPic(uvT + float2(0.0, du), lod) - CeilPic(uvT - float2(0.0, du), lod), lw);
        const float perWorld = 1.0 / (2.0 * du * TexSpan());   // the slope per world unit
        nrm = normalize(nrm - CeilBump * perWorld * (lx * tanU + lz * tanV));
    } else stone = lerp(float3(0.16, 0.13, 0.10), float3(0.30, 0.25, 0.20), NoiseP(q * 6.0, 384.0));
    // the hero's light (as from a torch at his shoulder), dying with the distance from
    // him - lit over his head, dark toward the walls, which hides where their tops meet
    // the ceiling - and a dim fixed fill from one side, so the far rock keeps a shape
    const float3 toEye = -dir;
    const float3 fromHero = float3(0.0, 0.7 * e.y, 0.0) - hit;
    const float3 lHero = fromHero / max(length(fromHero), 1e-3);
    const float dHero = length(fromHero) / max(CeilLightRadius, 1.0);
    const float fall = Falloff(dHero);
    // (the fill and the ambient kept low: with more, the rock went flat and grey - 2026-10-07)
    const float lit = 1.0 * fall * saturate(dot(nrm, lHero))
                    + 0.18 * saturate(dot(nrm, normalize(float3(0.5, -1.0, 0.3)))) + 0.04;
    float3 light = lit.xxx;
    float glint = 0.8 * fall * Glint(nrm, lHero, toEye);
    if (CeilTorches) {
        const float4 tl = CeilTorchBright * (GameLightsOn ? GameLight(hit, nrm, toEye)
                                                          : TorchLight(float3(hit.x + hero.x, hit.y, hit.z + hero.y), nrm, toEye));
        light += tl.rgb;
        glint += tl.w;
    }
    // the hollows (up at the plane) darker, the tips lighter; the glint is the light's own colour, not the stone's
    c = (stone * light + CeilWet * glint * float3(1.0, 0.9, 0.75)) * lerp(0.55, 1.0, h) * CeilBrightness;
    if (FogOn) c = lerp(c, FogColor, FogAt(t * ahead));
    return true;
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
        const bool drawn = DepthAt(duv + float2(0.0, down * off)).x > 1e-6;
        hit = hit > 0.0 ? hit : (drawn ? off : 0.0);
        miss = hit > 0.0 ? miss : off;
    }
    [unroll] for (int b = 0; b < 9; ++b) {
        const float mid = 0.5 * (miss + hit);
        const bool drawn = hit > 0.0 && DepthAt(duv + float2(0.0, down * mid)).x > 1e-6;
        hit = drawn ? mid : hit;
        miss = drawn ? miss : mid;
    }
    return hit > 0.0 ? float2(DepthAt(duv + float2(0.0, down * (hit + 1.0))).x, hit) : float2(0.0, 0.0);
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

// ---- The torches' light: finding fire, placing it, keeping it --------------------------

// How much a colour looks like fire: bright, red well over blue.
float Fireness(float3 c) { return saturate((c.r - 0.65) * 4.0) * saturate((c.r - c.b - 0.3) * 3.0); }

// Each texel: the most fire-like of 4 x 4 pixels of its 16 x 16 block (x), where (yz),
// and how much fire the block holds (w: the sum over the 16). The interface is left
// out: the bottom of the screen, and the game's own layer's letters.
float4 PS_Fire(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilOn || !CeilTorches || GameLightsOn) return 0.0;
    const float2 corner = floor(pos.xy) * 16.0;
    float best = 0.0, sum = 0.0;
    float2 at = float2(0.0, 0.0);
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x) {
            const float2 p = (corner + float2(x, y) * 4.0 + 2.0) * float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
            float s = p.y < 0.88 ? Fireness(tex2Dlod(FogColorSmp, float4(p, 0, 0)).rgb) : 0.0;
            if (UiMaskOn) s *= saturate(tex2Dlod(GameLayerSmp, float4(p, 0, 0)).a);
            sum += s;
            if (s > best) { best = s; at = p; }
        }
    return float4(best, at, sum);
}

// Each of the 32 candidates: the brightest fire of its screen cell (8 x 4), put in the
// world by the depth there. A flame is often drawn without depth: what it stands on
// right under it (the brazier) is nearer than what is behind it, and is taken instead.
float4 TorchCands(float4 pos, int eye)
{
    if (!CeilOn || !CeilTorches || GameLightsOn) return 0.0;
    const int idx = (int)pos.x, row = (int)pos.y;
    const int cw = (kFireW + 7) / 8, ch = (kFireH + 3) / 4;
    const int gx = idx % 8, gy = idx / 8;
    float best = 0.0;
    float2 at = float2(0.0, 0.0);
    int2 bt = int2(0, 0);
    [loop] for (int y = 0; y < ch; ++y)
        [loop] for (int x = 0; x < cw; ++x) {
            const int2 t = int2(gx * cw + x, gy * ch + y);
            if (t.x >= kFireW || t.y >= kFireH) continue;
            const float4 f = Texel(FireSmp, t.x, t.y, (float)kFireW, (float)kFireH);
            if (f.x > best) { best = f.x; at = f.yz; bt = t; }
        }
    if (best < 0.05) return 0.0;
    // the fire round the brightest point, 5 x 5 blocks (80 px): how big the flame is on the screen
    float area = 0.0;
    [unroll] for (int ay = -2; ay <= 2; ++ay)
        [unroll] for (int ax = -2; ax <= 2; ++ax) {
            const int2 t = bt + int2(ax, ay);
            if (t.x >= 0 && t.y >= 0 && t.x < kFireW && t.y < kFireH) area += Texel(FireSmp, t.x, t.y, (float)kFireW, (float)kFireH).w;
        }
    const float2 duv = UpsideDown ? float2(at.x, 1.0 - at.y) : at;
    const float own = DepthAt(duv).x;
    const float2 below = FirstDepthBelowAt(duv, UpsideDown ? -BUFFER_RCP_HEIGHT : BUFFER_RCP_HEIGHT);
    float d = own;
    if (below.y > 0.0 && below.y <= 64.0 && below.x > own * 1.1) d = below.x;
    if (d <= 1e-6) return 0.0;
    const float z = NearPlane / d;                      // along the view
    if (FogOn && z > FogEnd) return 0.0;
    const float2 ndc = float2(at.x * 2.0 - 1.0, 1.0 - at.y * 2.0);
    const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
    const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0) * z;
    const float3 off = eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1;
    const float3 e = eye == 0 ? CeilEye0 : CeilEye1;
    const float2 hero = eye == 0 ? CeilHero0 : CeilHero1;
    const float P = 64.0 * CeilScale;
    float3 w = float3(e.x + off.x + hero.x, e.y + off.y, e.z + off.z + hero.y);
    w.xz -= P * floor(w.xz / P);
    // The light by the flame's size in the world: its area on the screen grows as the
    // square of nearness, so it is taken back by the distance (30 units = as seen).
    // A torch's flame ~ 1; a candle's or an ember's a small part - a small fire, a dim light.
    const float res = BUFFER_HEIGHT / 1350.0;   // more pixels to the same flame on a taller screen
    const float size = area * (z / 30.0) * (z / 30.0) / (res * res);
    const float strength = saturate(size / 40.0);
    if (strength < 0.02) return 0.0;
    if (row == 0) return float4(w, strength);
    const float3 col = tex2Dlod(FogColorSmp, float4(at, 0, 0)).rgb;
    return float4(col / max(col.r, 1e-3), 1.0);
}
float4 PS_TorchCands(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return TorchCands(pos, 0); }
float4 PS_TorchCandsR(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return TorchCands(pos, 1); }

// The list of 16 torches: a torch already there is moved toward the candidate seen
// near it and lives on; one not seen fades over CeilTorchMemory seconds; a place
// that was free takes the next candidate no torch is near (the n-th free place the
// n-th such candidate, so every place picks a different one). Two torches that came
// together keep the first.
float4 PS_TorchMerge(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilOn || !CeilTorches || GameLightsOn) return 0.0;
    const int i = (int)pos.x, row = (int)pos.y;
    const float near = 4.0;   // world units: the same torch
    const float4 p0 = Texel(TorchOldSmp, i, 0, 16.0, 2.0), c0 = Texel(TorchOldSmp, i, 1, 16.0, 2.0);
    if (c0.w > 0.0) {
        [loop] for (int k = 0; k < 16; ++k) {
            if (k >= i) break;
            if (Texel(TorchOldSmp, k, 1, 16.0, 2.0).w > 0.0 && length(WrapDelta(p0.xyz, Texel(TorchOldSmp, k, 0, 16.0, 2.0).xyz)) < near)
                return 0.0;
        }
        float bestD = near;
        int bestJ = -1;
        [loop] for (int j = 0; j < 32; ++j) {
            const float4 q = Texel(TorchCandSmp, j, 0, 32.0, 2.0);
            if (q.w < 0.05) continue;
            const float dj = length(WrapDelta(p0.xyz, q.xyz));
            if (dj < bestD) { bestD = dj; bestJ = j; }
        }
        if (bestJ >= 0) {
            const float4 q = Texel(TorchCandSmp, bestJ, 0, 32.0, 2.0);
            if (row == 0) return float4(p0.xyz + WrapDelta(p0.xyz, q.xyz) * 0.3, lerp(p0.w, q.w, 0.3));
            return float4(lerp(c0.rgb, Texel(TorchCandSmp, bestJ, 1, 32.0, 2.0).rgb, 0.3), 1.0);
        }
        const float life = c0.w - FrameTime * 0.001 / max(CeilTorchMemory, 0.1);
        if (life <= 0.0) return 0.0;
        return row == 0 ? p0 : float4(c0.rgb, life);
    }
    int rank = 0;   // free places before this one
    [loop] for (int k2 = 0; k2 < 16; ++k2) {
        if (k2 >= i) break;
        if (Texel(TorchOldSmp, k2, 1, 16.0, 2.0).w <= 0.0) ++rank;
    }
    int n = 0;
    [loop] for (int j2 = 0; j2 < 32; ++j2) {
        const float4 q = Texel(TorchCandSmp, j2, 0, 32.0, 2.0);
        if (q.w < 0.05) continue;
        bool taken = false;
        [loop] for (int k3 = 0; k3 < 16; ++k3)
            if (Texel(TorchOldSmp, k3, 1, 16.0, 2.0).w > 0.0 && length(WrapDelta(q.xyz, Texel(TorchOldSmp, k3, 0, 16.0, 2.0).xyz)) < near) { taken = true; break; }
        [loop] for (int k4 = 0; k4 < 32; ++k4) {
            if (taken || k4 >= j2) break;
            const float4 o = Texel(TorchCandSmp, k4, 0, 32.0, 2.0);
            if (o.w >= 0.05 && length(WrapDelta(q.xyz, o.xyz)) < near) taken = true;
        }
        if (taken) continue;
        if (n == rank) return row == 0 ? q : float4(Texel(TorchCandSmp, j2, 1, 32.0, 2.0).rgb, 1.0);
        ++n;
    }
    return 0.0;
}
float4 PS_TorchStore(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target { return tex2D(TorchSmp, uv); }

// The glow and the flames the game draws over the void (no depth there), warm only (not the grey
// spill round people), averaged into 1/8 and 1/32 of the screen with how much of each block is
// void: a cut thing's place is filled from the void round it, smoothly, whatever its size.
texture2D VoidGlow8Tex { Width = BUFFER_WIDTH / 8; Height = BUFFER_HEIGHT / 8; Format = RGBA16F; };
sampler2D VoidGlow8Smp { Texture = VoidGlow8Tex; AddressU = CLAMP; AddressV = CLAMP; };
texture2D VoidGlow32Tex { Width = BUFFER_WIDTH / 32; Height = BUFFER_HEIGHT / 32; Format = RGBA16F; };
sampler2D VoidGlow32Smp { Texture = VoidGlow32Tex; AddressU = CLAMP; AddressV = CLAMP; };
float4 PS_VoidGlow8(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilOn || SkyOn) return 0.0;
    const int2 base = int2(pos.xy) * 8;
    float4 sum = 0.0;
    [loop] for (int y = 0; y < 8; y += 2)
        [loop] for (int x = 0; x < 8; x += 2) {
            const float2 u = (base + int2(x, y) + 0.5) / float2(BUFFER_WIDTH, BUFFER_HEIGHT);
            if (DepthAt(UpsideDown ? float2(u.x, 1.0 - u.y) : u).x > 1e-6) continue;
            const float3 c = tex2Dlod(FogColorSmp, float4(u, 0, 0)).rgb;
            sum += float4(c * saturate((c.r - c.b) * 15.0), 1.0);
        }
    return sum / 16.0;   // premultiplied: rgb = glow x the void's share, a = the void's share
}
float4 PS_VoidGlow32(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target
{
    if (!CeilOn || SkyOn) return 0.0;
    const int2 base = int2(pos.xy) * 4;
    float4 sum = 0.0;
    [loop] for (int y = 0; y < 4; ++y)
        [loop] for (int x = 0; x < 4; ++x)
            sum += tex2Dfetch(VoidGlow8Smp, base + int2(x, y));
    return sum / 16.0;
}
float3 VoidGlowAt(float2 uv)
{
    const float4 g8 = tex2Dlod(VoidGlow8Smp, float4(uv, 0, 0));
    const float4 g32 = tex2Dlod(VoidGlow32Smp, float4(uv, 0, 0));
    const float3 wide = g32.a > 1e-3 ? g32.rgb / g32.a : 0.0;
    const float3 close = g8.a > 1e-3 ? g8.rgb / g8.a : wide;
    return lerp(wide, close, saturate(g8.a * 4.0));
}

float3 DepthFogWorld(float4 pos, float2 uv, int eye)
{
    const float3 colour = tex2D(FogColorSmp, uv).rgb;
    const float2 duv = UpsideDown ? float2(uv.x, 1.0 - uv.y) : uv;
    const float d = DepthAt(duv).x;

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
    const float d1 = DepthAt(duv + float2(px.x, 0)).x;
    const float d2 = DepthAt(duv - float2(px.x, 0)).x;
    const float d3 = DepthAt(duv + float2(0, px.y)).x;
    const float d4 = DepthAt(duv - float2(0, px.y)).x;
    const float dNear = max(max(d, d1), max(max(d2, d3), d4));
    const float dFar = min(min(d, d1), min(min(d2, d3), d4));

    float3 fogColour = FogColor;
    if (SkyOn || CeilOn) {
        const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
        const float4 sp = eye == 0 ? SkyProj0 : SkyProj1;
        const float3 v = float3((ndc.x + sp.z) / sp.x, (ndc.y + sp.w) / sp.y, -1.0);
        const float3 dir = eye == 0 ? normalize(v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0)
                                    : normalize(v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1);
        const float dither = (Hash(pos.xy) - 0.5) / 255.0;
        // What the game drew above the ceiling - the barracks' wall corners, crosses on the
        // wall tops - pierced the vault (the user's screenshot, 2026-10-07). The drawn
        // point is put back in the world by its depth (the same frame as the ceiling's:
        // the eye less the hero's feet); above the plane, the ceiling is drawn in its place.
        // A rim pixel with no depth of its own but a drawn neighbour takes the neighbour's:
        // left out, the cut shapes stayed outlined in black on the vault.
        const float dPt = d > 1e-6 ? d : dNear;
        // What the ceiling is wanted for here, and as far along the ray as: 1 a cut point, 2 a point
        // behind a wall at the floor's edge, 3 the void. ONE call of Ceiling for all three - three of
        // them inlined made the shader take 164 s to build (2026-10-07).
        int ceilMode = 0;
        float ceilLim = 1e9;
        if (CeilOn && !SkyOn && dPt > 1e-6) {
            const float3 off = (eye == 0 ? v.x * CamRight0 + v.y * CamUp0 + v.z * CamBack0
                                         : v.x * CamRight1 + v.y * CamUp1 + v.z * CamBack1) * (NearPlane / dPt);
            // A flame drawn (without depth) over a cut cross went with it: the cross stood black
            // in the fire (2026-10-07). What is fire in the pixel - warm, or bright - is laid
            // back over the vault as light; the stone of the cross, dim, is not.
            // The cut follows the stone's underside, not the plane: a cathedral's columns end
            // where the vault comes down to them (2026-10-07).
            const float3 eP = eye == 0 ? CeilEye0 : CeilEye1;
            const float2 heroP = eye == 0 ? CeilHero0 : CeilHero1;
            const float2 qPt = (eP.xz + off.xz + heroP) / CeilScale;
            // the point's height over the stone's underside there (> 0: above it)
            const float hPt = eP.y + off.y - CeilUnder(qPt);
            if (ShowCeilingCut) {
                if (frac((eP.y + off.y) / 5.0) < 0.04) return 1.0;
                // the columns the vault stands on (and the band their tops are looked for in)
                if (hPt > 0.2) return float3(1.0, 0.0, 0.0);   // cut: the vault is drawn there
                return hPt > 0.2 ? float3(1.0, 0.0, 0.0)
                                 : lerp(float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 0.0), saturate(1.0 + hPt / max(CeilHeight, 1.0)));
            }
            if (hPt > 0.2) { ceilMode = 1; ceilLim = length(off); }
        }
        // Nothing drawn here: the void, unless a HUD piece lies over it. Outdoors
        // the sky fills it; in a cave the ceiling, where this ray meets it - where
        // it does not, the void goes on as before (fogged below).
        float3 sky = float3(0.0, 0.0, 0.0);
        bool back = false;
        if (ceilMode == 0 && dNear <= 1e-6) {
            if (SkyOn) {
                sky = Sky(dir);
                // with the act's own fog colour the sky's low part sinks into it too:
                // the fogged land meets the sky without an edge
                if (FogOn && FogFixed) sky = lerp(sky, FogColor, 1.0 - smoothstep(-0.03, 0.2, dir.y));
                back = true;
            } else ceilMode = 3;
        }
        if (ceilMode != 0) {
            float3 cc;
            const bool got = Ceiling(dir, eye, cc, ceilLim);
            if (ceilMode == 1 && got) {
                // and bright: torch-lit stone is warm too, and a cut dome on a wall top stood
                // through the ceiling as a ghost in the catacombs (2026-10-07)
                const float peak = max(colour.r, max(colour.g, colour.b));
                const float warm = smoothstep(0.1, 0.3, colour.r - colour.b);
                const float fire = smoothstep(0.5, 0.85, peak) * max(warm, smoothstep(0.85, 1.0, peak));
                // Under it, the game's glow and flames over the void round it, filled in smoothly
                // (VoidGlow): its own lit stone must not show - the domes on the catacombs' walls
                // stood lit beside a flame, or black where 8 samples found no glow (2026-10-07).
                const float3 halo = VoidGlowAt(uv) * saturate(CeilHalo);
                // (eased in over 1.5 units it let the cut caps' outlines through - kept hard, 2026-10-07)
                return cc + dither + lerp(halo, colour, fire);
            }
            if (ceilMode == 3) { back = got; sky = cc; }
        }
        if (back) {
            const float luma = dot(colour, float3(0.2126, 0.7152, 0.0722));
            const float k = 1.0 - smoothstep(0.03, 0.08, luma);
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
                    const float dd = DepthAt(duv + float2(0.0, down * exp2((float)s2))).x;
                    dn = dn > 0.0 ? dn : dd;
                }
                if (dn > 1e-6) tf = max(tf, FogAmount(dn));
            }
            // Only what is clearly off black: the near-black spill the game leaves round
            // people and trees over the void, added in full, stood as a pale haze round
            // them (2026-10-05). k fades it in over the same 0.03..0.08 as the void test.
            // Over the cave ceiling the torches' own halo - a flat glow the game draws for the
            // black void - is kept at CeilHalo (1 = whole: "do not dim the halo", the user,
            // 2026-10-07); the flame itself, bright, always stays.
            float keep = 1.0, seen = 1.0 - k;
            if (!SkyOn) {
                keep = lerp(saturate(CeilHalo), 1.0, smoothstep(0.45, 0.85, max(colour.r, max(colour.g, colour.b))));
                // a halo's dim red rim is under the void's 0.03..0.08 too: over the ceiling it ended
                // short of where it ran on the wall (2026-10-07). Warm is a flame's light, not the
                // grey spill round people - it stays.
                seen = max(seen, saturate((colour.r - colour.b) * 15.0));
            }
            return sky + dither + colour * (seen * (1.0 - tf) * keep);
        }
        const float3 fogDir = normalize(float3(dir.x, clamp(dir.y, -0.1, 0.15), dir.z));
        if (SkyOn && !FogFixed) fogColour = (SkyTex > 0.5 ? PaintedSky(fogDir) : SkyGradient(fogDir.y)) * SkyBrightness + dither;
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
                const float ts = FogAmount(DepthAt(duv + float2(x, y) * px * FogBlur).x);
                const float w = 1.0 - saturate(abs(ts - t0) * 4.0);
                sum += ts * w; wsum += w;
            }
        t = (sum + t0) / (wsum + 1.0);
    }
    if (ShowDistance) return t.xxx;
    // In a cave a torch's halo is laid over the far floor too, and the fog took it away with
    // the floor: the floor's edge stood as a black cut-out in the middle of the glow
    // (2026-10-07). The halo is warm - red well over blue - and the cave's rock is not: what
    // is warm keeps its light through the fog (CeilHalo of it), as it does over the void.
    if (CeilOn && !SkyOn) {
        const float warm = smoothstep(0.05, 0.25, colour.r - colour.b);
        return lerp(colour, fogColour, t) + colour * (t * warm * saturate(CeilHalo));
    }
    return lerp(colour, fogColour, t);
}
// The interface keeps its own colour: the fog and the sky only where the layer lets the world through.
float3 DepthFog(float4 pos, float2 uv, int eye)
{
    const float3 fogged = DepthFogWorld(pos, uv, eye);
    [branch] if (!UiMaskOn || UiMaskOff) return fogged;
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
uniform float GameMapShift < hidden = true; > = 0.0;   // uv, the map alone: + nearer, - farther (F3 [hud_floor] map_near)
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
// vrcam [hud] monster_alpha: the name plate over the target monster (the game's
// MonsterHealth panel, uv box) faded as a whole - box, frame and name.
uniform float4 PlateBox < hidden = true; > = float4(0.0, 0.0, 0.0, 0.0);
uniform float PlateAlpha < hidden = true; > = 1.0;

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
    float4 l = tex2Dlod(GameLayerSmp, float4(src, 0, 0));
    if (LabelAlpha < 0.999) l = FadedLabel(l);
    if (PlateAlpha < 0.999 && InBox(src, PlateBox)) l = float4(l.rgb * PlateAlpha, 1.0 - (1.0 - saturate(l.a)) * PlateAlpha);
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
        const float4 m = GamePiece(GameMapSmp, GameMapBox, src - float2(eyeSign * GameMapShift, 0.0));   // the map over the toolbar where they meet
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
    float d = DepthAt(duv).x;
    d = max(d, DepthAt(duv + float2(px.x, 0)).x);
    d = max(d, DepthAt(duv - float2(px.x, 0)).x);
    d = max(d, DepthAt(duv + float2(0, px.y)).x);
    d = max(d, DepthAt(duv - float2(0, px.y)).x);
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
            const float dd = DepthAt(duv + float2(0.0, down * off)).x;
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
    const float dc = DepthAt(duv).x;
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
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Fire; RenderTarget = FireTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchCands; RenderTarget = TorchCandTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchMerge; RenderTarget = TorchTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchStore; RenderTarget = TorchOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow8; RenderTarget = VoidGlow8Tex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow32; RenderTarget = VoidGlow32Tex; }
    pass { ComputeShader = CS_RecordHeights<16, 16>; DispatchSizeX = BUFFER_WIDTH / 16 + 1; DispatchSizeY = BUFFER_HEIGHT / 16 + 1; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapMerge; RenderTarget = HMapTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapStore; RenderTarget = HMapOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupMerge; RenderTarget = SupTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupStore; RenderTarget = SupOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapGenStore; RenderTarget = HMapGenTex; }
    pass { ComputeShader = CS_ClearHeights<8, 8>; DispatchSizeX = 8; DispatchSizeY = 8; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Peaks; RenderTarget = PeakTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DomeSeeds; RenderTarget = DomeSeedTex; }
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
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Fire; RenderTarget = FireTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchCandsR; RenderTarget = TorchCandTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchMerge; RenderTarget = TorchTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TorchStore; RenderTarget = TorchOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow8; RenderTarget = VoidGlow8Tex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_VoidGlow32; RenderTarget = VoidGlow32Tex; }
    pass { ComputeShader = CS_RecordHeightsR<16, 16>; DispatchSizeX = BUFFER_WIDTH / 16 + 1; DispatchSizeY = BUFFER_HEIGHT / 16 + 1; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapMerge; RenderTarget = HMapTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapStore; RenderTarget = HMapOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupMerge; RenderTarget = SupTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_SupStore; RenderTarget = SupOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HMapGenStore; RenderTarget = HMapGenTex; }
    pass { ComputeShader = CS_ClearHeights<8, 8>; DispatchSizeX = 8; DispatchSizeY = 8; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_Peaks; RenderTarget = PeakTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DomeSeeds; RenderTarget = DomeSeedTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_DepthFogR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_GameHudR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_PhantomR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudStore; RenderTarget = HudPrevTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_HudMaskStore; RenderTarget = HudMaskOldTex; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_TableKeyR; }
    pass { VertexShader = VS_Fullscreen; PixelShader = PS_StampR; }
}
