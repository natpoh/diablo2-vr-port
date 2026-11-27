// video3d_shader.h - [cinema] video_3d: the shaders, compiled when the feature first runs (d3dcompiler_47, as the interface's are).
//
// VSMain    fullscreen triangle, uv 0..1 over the viewport
// PSDown    the film -> the depth network's input size, gamma-encoded (a picture as cameras store it)
// PSEye     one eye of the film: each output pixel looks along its row for the film pixel whose shifted place lands on it, the
//           nearest one winning where two land on the same place (a plain depth-image-based rendering search). Our own code (D2R VR, MIT).
#pragma once

namespace video3d {

inline constexpr char kShader[] = R"(
cbuffer C : register(b0) {
    float4 g_rect;      // the film in the source, uv: x0, y0, w, h
    float  g_eyeSign;   // +1 left eye, -1 right eye
    float  g_sep;       // the largest separation between the eyes, in film widths
    float  g_conv;      // the depth that stays on the screen (0 far .. 1 near)
    float  g_srcGamma;  // 1: the source view returns gamma-encoded values; 0: linear light
    float  g_steps;     // search samples along the row
    float  g_hasDepth;  // 0: no depth yet - the film flat in both eyes
    float2 g_tap;       // PSDown: one network pixel's footprint in source uv
    float  g_outGamma;  // PSEye: 1 when the target is not an _SRGB view (write gamma-encoded)
    float3 g_pad;
};

Texture2D<float4> g_src   : register(t0);
Texture2D<float>  g_depth : register(t1);
SamplerState      g_lin   : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float3 ToLinear(float3 c) {
    c = saturate(c);
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
float3 ToGamma(float3 c) {
    c = saturate(c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

float3 SrcRaw(float2 uvFilm) {
    return g_src.SampleLevel(g_lin, g_rect.xy + saturate(uvFilm) * g_rect.zw, 0).rgb;
}

float4 PSDown(VSOut i) : SV_Target {
    // 4x4 taps over the footprint: a 1920-wide film is 3.75 source pixels to one network pixel.
    float3 sum = 0;
    [unroll] for (int y = 0; y < 4; ++y)
        [unroll] for (int x = 0; x < 4; ++x) {
            float2 off = (float2(x, y) + 0.5) / 4.0 - 0.5;
            sum += g_src.SampleLevel(g_lin, g_rect.xy + i.uv * g_rect.zw + off * g_tap, 0).rgb;
        }
    float3 c = sum / 16.0;
    if (g_srcGamma < 0.5) c = ToGamma(c);
    return float4(c, 1);
}

float DepthAt(float u, float v) { return g_depth.SampleLevel(g_lin, float2(saturate(u), v), 0); }
float ShiftOf(float d) { return g_eyeSign * 0.5 * g_sep * (d - g_conv); }

float4 PSEye(VSOut i) : SV_Target {
    float2 uv = i.uv;
    float u = uv.x;
    float src = u;
    if (g_hasDepth > 0.5 && g_sep > 0.0) {
        // Where a film pixel c lands: c + ShiftOf(depth(c)). Look along the row, as far as any pixel can move, for the places
        // where that crosses this pixel; the nearest surface found there is what this eye sees. Between two samples the
        // crossing is placed by interpolation, so the result is not stepped to the sample spacing.
        float filmW = max(g_rect.z, 1e-6);
        float px;
        { uint w, h; g_src.GetDimensions(w, h); px = 1.0 / max(filmW * w, 1.0); }
        float reach = 0.5 * g_sep * max(g_conv, 1.0 - g_conv) + 2.0 * px;
        int n = (int)clamp(g_steps, 4, 64);
        float step = 2.0 * reach / n;
        float c0 = u - reach;
        float f0 = c0 + ShiftOf(DepthAt(c0, uv.y)) - u;
        float best = -1.0, bestD = -1.0, farD = 2.0;
        [loop] for (int k = 1; k <= n; ++k) {
            float c1 = u - reach + step * k;
            float d1 = DepthAt(c1, uv.y);
            float f1 = c1 + ShiftOf(d1) - u;
            farD = min(farD, d1);
            if ((f0 <= 0.0 && f1 >= 0.0) || (f0 >= 0.0 && f1 <= 0.0)) {
                float t = abs(f0 - f1) > 1e-7 ? f0 / (f0 - f1) : 0.5;
                float c = lerp(c0, c1, saturate(t));
                float d = DepthAt(c, uv.y);
                if (d > bestD) { bestD = d; best = c; }
            }
            c0 = c1;
            f0 = f1;
        }
        // Nothing lands here (only at the film's sides): the farthest thing in reach, unshifted by its depth.
        src = best >= 0.0 ? best : u - ShiftOf(farD);
    }
    float3 c = SrcRaw(float2(src, uv.y));
    bool linearNow = g_srcGamma < 0.5;
    if (g_outGamma > 0.5) { if (linearNow) c = ToGamma(c); }
    else if (!linearNow) c = ToLinear(c);
    return float4(c, 1);
}
)";

}  // namespace video3d
