// rune_shader.h - [openxr] runes_draw: the pixel shader that draws a gesture rune's picture or trail into a cell of the runes' atlas,
// the constant buffer it reads, and the pure functions that fill that buffer from the shared block (vr/rune_geom.h).
//
// One draw per cell: a full-cell triangle, the pixel shader loops over the cell's segment list (up to 96) and finds, for the matched
// part of the picture and for the rest, how much of a line, a halo, a hot middle and a dark rim each segment adds at the pixel (the
// largest wins, so crossing or closing lines do not jump): an anti-aliased edge, a middle that goes toward white with the glow, a soft
// halo as strong as the glow, and a dark rim outside the edge so a pale line is still seen against a bright scene. The output is premultiplied, in LINEAR light, to an sRGB
// target - the quad layer is blended with the premultiplied rule. The trail tapers toward its old end and ends in a bright blob.
//
// The CPU mirror (vr/rune_mirror.h) does the same sums in C++; cleanroom/tests/rune_test.cpp compiles THIS text on a WARP device
// and compares the two pixel by pixel, so a change here without the mirror (or the reverse) fails the test.
#pragma once

#include "rune_geom.h"

namespace runes {

// The atlas: one cell per picture on the top row, one per trail on the bottom row.
constexpr uint32_t kCell = 320;
constexpr uint32_t kCols = kMax, kRows = 2;
constexpr uint32_t kAtlasW = kCell * kCols, kAtlasH = kCell * kRows;

// The look's constants. HLSL below carries the same numbers; the test holds them together.
constexpr float kTailWidth = 0.5f;    // a trail's oldest end is this much of the width ...
constexpr float kTailAlpha = 0.45f;   // ... and of the opacity
constexpr float kRimAlpha = 0.40f;    // the dark rim outside the edge
constexpr float kHaloAdd = 0.4f;      // the halo's alpha as a fraction of its brightness (< 1: it adds light, not only covers)
constexpr float kWhiteCore = 0.35f;   // how far the middle of a line goes toward white with full glow
constexpr float kRimReach = 1.6f;     // the dark rim reaches this many line widths past the edge

// The shader's constant buffer, one per cell. 16-byte rows, 1632 bytes.
struct alignas(16) CellCb {
    float cell[4];     // x, y: the cell's top left in the atlas, pixels; z: its side, pixels
    float ext[4];      // x: the cell's side, metres; y: the line's width, metres; z: glow 0..1; w: segments used
    float lit[4];      // the matched part: linear rgb, alpha (opacity x fade)
    float unlit[4];    // the rest of a picture; all of a trail
    float misc[4];     // x: segments lit (index < x); y: 1 = a trail (tapering, with a blob), 0 = a picture
    float blob[4];     // x, y: the blob, metres from the cell's centre; z: its radius in line widths, 0 = none
    float seg[96][4];  // x0 y0 x1 y1 per segment, metres from the cell's centre, y up
};
static_assert(sizeof(CellCb) == 6 * 16 + 96 * 16, "CellCb is the shader's constant buffer");
constexpr uint32_t kCbSlot = 2048;   // each cell's buffer in the upload ring, rounded up to the 256 bytes a CBV needs
static_assert(kCbSlot >= sizeof(CellCb) && kCbSlot % 256 == 0, "a slot holds a CellCb and is 256-aligned");

inline void SetRgba(float* o, const Rgba& c) { o[0] = c.r; o[1] = c.g; o[2] = c.b; o[3] = c.a; }

// The picture's cell: its segments straight from the block's points (kept one to one with the points so the lit split is the
// block's), the matched ones in the lit colour.
inline void FillPicture(CellCb* cb, const BW_LinkVrRune& r, const Look& look, const Panel& panel, uint32_t col) {
    std::memset(cb, 0, sizeof *cb);
    const uint32_t n = std::min<uint32_t>(r.pointCount, kPoints);
    const uint32_t segs = n >= 2 ? n - 1 : 0;
    cb->cell[0] = (float)(col * kCell); cb->cell[1] = 0.0f; cb->cell[2] = (float)kCell;
    cb->ext[0] = panel.side; cb->ext[1] = look.lineW; cb->ext[2] = look.glow; cb->ext[3] = (float)segs;
    SetRgba(cb->lit, look.lit);
    SetRgba(cb->unlit, look.rune);
    cb->misc[0] = (float)LitSegments(r.progress, n);
    cb->misc[1] = 0.0f;
    for (uint32_t i = 0; i < segs; ++i) {
        cb->seg[i][0] = r.pts[i][0]; cb->seg[i][1] = r.pts[i][1];
        cb->seg[i][2] = r.pts[i + 1][0]; cb->seg[i][3] = r.pts[i + 1][1];
        for (float& v : cb->seg[i]) if (!std::isfinite(v)) v = 0.0f;
    }
}

// The trail's cell: all one colour (the "unlit" slot; nothing is lit), the blob at the hand.
inline void FillTrail(CellCb* cb, const Look& look, const TrailBoard& tb, uint32_t col) {
    std::memset(cb, 0, sizeof *cb);
    cb->cell[0] = (float)(col * kCell); cb->cell[1] = (float)kCell; cb->cell[2] = (float)kCell;
    cb->ext[0] = tb.side; cb->ext[1] = look.trailW; cb->ext[2] = look.glow; cb->ext[3] = (float)std::min<uint32_t>(tb.segs, 96);
    SetRgba(cb->lit, look.trail);
    SetRgba(cb->unlit, look.trail);
    cb->misc[0] = 0.0f;
    cb->misc[1] = 1.0f;
    if (tb.blob) { cb->blob[0] = tb.blobAt[0]; cb->blob[1] = tb.blobAt[1]; cb->blob[2] = kBlobSize; }
    for (uint32_t i = 0; i < cb->ext[3]; ++i) std::memcpy(cb->seg[i], tb.seg[i], sizeof cb->seg[i]);
}

// vs_5_0 VSMain / ps_5_0 PSRune. Root signature: one root CBV (b0).
inline const char* Hlsl() {
    return R"(
cbuffer Rune : register(b0) {
  float4 g_cell;   // x, y: the cell's top left in the atlas, pixels; z: its side, pixels
  float4 g_ext;    // x: the cell's side, metres; y: the line's width, metres; z: glow 0..1; w: segments used
  float4 g_lit;    // the matched part: linear rgb, alpha
  float4 g_unlit;  // the rest of a picture; all of a trail
  float4 g_misc;   // x: segments lit (index < x); y: 1 = a trail (tapering, with a blob), 0 = a picture
  float4 g_blob;   // x, y: the blob, metres from the cell's centre; z: its radius in line widths, 0 = none
  float4 g_seg[96];   // x0 y0 x1 y1, metres from the cell's centre, y up
};

float4 VSMain(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

static const float kTailW = 0.5;    // a trail's oldest end: this much of the width ...
static const float kTailA = 0.45;   // ... and of the opacity
static const float kRim = 0.40;     // the dark rim outside the edge
static const float kAdd = 0.4;      // the halo's alpha as a fraction of its brightness
static const float kWhite = 0.35;   // how far the middle of a line goes toward white with full glow

// premultiplied "over"
float4 Over(float4 top, float4 bottom) { return top + bottom * (1.0 - top.a); }

// A line from what the loop found: acc = (cover, halo, deep, rim), each the largest over the segments of this colour (the trail's
// opacity taper is folded in; deep = 1 at a line's middle, 0 at its edge). col: linear rgb, alpha. Premultiplied.
float4 Stroke(float4 acc, float4 col, float glow) {
  float3 core = lerp(col.rgb, float3(1.0, 1.0, 1.0), kWhite * glow * acc.z * acc.z);
  float4 c = float4(core * acc.x, acc.x) * col.a;
  float h = acc.y * (0.15 + 0.5 * glow) * col.a;
  return Over(c, float4(col.rgb * h, h * kAdd));
}

// What one segment of the loop adds at a pixel: sd metres past its edge, ta its opacity. A halo that dies out before 4 scales.
float4 Reach(float sd, float ta, float hw, float hs, float px, float w) {
  float beyond = max(sd, 0.0);
  float reach = 4.0 * hs;
  float rimFall = 1.0 - smoothstep(0.0, 1.6 * w, sd);
  return float4(saturate(0.5 - sd / px), exp(-beyond / hs) * (1.0 - smoothstep(0.5 * reach, reach, beyond)), saturate(-sd / hw),
                smoothstep(-0.5 * px, 0.5 * px, sd) * rimFall * rimFall) * float4(ta, ta, 1.0, ta);
}

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

float4 PSRune(float4 pos : SV_Position) : SV_Target {
  float2 q = ((pos.xy - g_cell.xy) / g_cell.z - 0.5) * g_ext.x;
  q.y = -q.y;
  const float px = g_ext.x / g_cell.z;       // one pixel, metres
  const float w = g_ext.y;
  const float hw = 0.5 * w;
  const float hs = w * (0.6 + 1.2 * g_ext.z);   // the halo's scale
  const float reach = 4.0 * hs;
  const uint n = (uint)g_ext.w;
  const uint nl = (uint)g_misc.x;
  const bool trail = g_misc.y > 0.5;
  float4 accL = float4(0.0, 0.0, 0.0, 0.0);   // the lit part's, the rest's (a trail's all)
  float4 accU = float4(0.0, 0.0, 0.0, 0.0);
  [loop] for (uint i = 0; i < n; ++i) {
    float4 s = g_seg[i];
    float2 a = s.xy;
    float2 ab = s.zw - s.xy;
    float t = saturate(dot(q - a, ab) / max(dot(ab, ab), 1e-10));
    float d = length(q - (a + ab * t));
    float along = trail ? (float(i) + t) / float(n) : 1.0;
    float wf = trail ? lerp(kTailW, 1.0, along) : 1.0;
    float sd = d - hw * wf;
    if (sd > reach) continue;                  // too far from this one to see anything of it
    float4 v = Reach(sd, trail ? lerp(kTailA, 1.0, along) : 1.0, hw * wf, hs, px, w);
    if (i < nl) accL = max(accL, v); else accU = max(accU, v);
  }
  float4 cl = g_lit;
  float4 cu = g_unlit;
  const float rim = kRim * max(accL.w * cl.a * (0.5 + 1.2 * Luma(cl.rgb)), accU.w * cu.a * (0.5 + 1.2 * Luma(cu.rgb)));
  float4 res = float4(0.0, 0.0, 0.0, rim);
  res = Over(Stroke(accU, cu, g_ext.z), res);
  res = Over(Stroke(accL, cl, g_ext.z), res);
  if (g_blob.z > 0.0) {
    float rc = g_blob.z * w;
    float db = length(q - g_blob.xy) - rc;
    res = Over(Stroke(Reach(db, 1.0, rc, 0.45 * w, px, w), g_unlit, max(g_ext.z, 0.8)), res);
  }
  return saturate(res);
}
)";
}

}  // namespace runes
