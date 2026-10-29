// turn_warp.cpp - see turn_warp.h. The shaders follow bearstonem/d2r-pcvr's steamvr/turn_warp.cpp (MIT, v0.3.24): a full-screen
// triangle for the turn, a grid of two triangles a cell for each picture moved with its depth (the drawn eye's cells across an edge
// as two layers), small passes over 16-pixel blocks for the near things and the game's effects; compiled once a process.

#include "turn_warp.h"
#include "gpu_spans.h"   // each pass's time on the graphics card, for the 10 s line (xr.cpp TurnTell)

#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace turnwarp {
namespace {

// The pictures turned or moved land between texels nearly always. Read bilinear, a half-texel step averages two, and fine detail and
// text lost contrast every other frame in each eye: softer than both eyes drawn (pcvr's owner, 2026-10-23). Catmull-Rom ([stereo]
// turns_sharp, Gpu::Sharpen) keeps more of it: five bilinear taps (the four corner ones left out), kept within the four nearest
// texels, so no ring or halo past them. On a texel's centre it is that texel. bearstonem/d2r-pcvr bd88507 (MIT).
#define TURN_SHARP R"(
float4 Sharp(Texture2D<float4> t, SamplerState s, float2 uv) {
    float w, h;
    t.GetDimensions(w, h);
    const float2 size = float2(w, h), at = uv * size, first = floor(at - 0.5) + 0.5, f = at - first;
    const float2 w0 = f * (-0.5 + f * (1 - 0.5 * f)), w1 = 1 + f * f * (-2.5 + 1.5 * f), w2 = f * (0.5 + f * (2 - 1.5 * f)), w3 = f * f * (-0.5 + 0.5 * f);
    const float2 w12 = w1 + w2, mid = (first + w2 / w12) / size, lo = (first - 1) / size, hi = (first + 2) / size;
    const float4 c = (t.SampleLevel(s, float2(mid.x, lo.y), 0) * (w12.x * w0.y) + t.SampleLevel(s, float2(lo.x, mid.y), 0) * (w0.x * w12.y)
                    + t.SampleLevel(s, mid, 0) * (w12.x * w12.y) + t.SampleLevel(s, float2(hi.x, mid.y), 0) * (w3.x * w12.y)
                    + t.SampleLevel(s, float2(mid.x, hi.y), 0) * (w12.x * w3.y))
                   / (w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y);
    const int2 last = int2(size) - 1, a = clamp(int2(first - 0.5), 0, last), b = clamp(int2(first + 0.5), 0, last);
    const float4 p = t.Load(int3(a, 0)), q = t.Load(int3(b.x, a.y, 0)), r = t.Load(int3(a.x, b.y, 0)), u = t.Load(int3(b, 0));
    return clamp(c, min(min(p, q), min(r, u)), max(max(p, q), max(r, u)));
}
)"

// Pass 1: this frame's image coordinates to the drawn image's (Homography's rows), the picture sampled there.
const char kTurnShader[] = R"(
Texture2D<float4> drawn : register(t0);
SamplerState edge : register(s0);
cbuffer Turn : register(b0) { float4 row0; float4 row1; float4 row2; float4 filter; };   // filter.x: 1 Catmull-Rom (Sharp), 0 bilinear
)" TURN_SHARP R"(
float4 Read(float2 uv) { return filter.x > 0 ? Sharp(drawn, edge, uv) : drawn.SampleLevel(edge, uv, 0); }
struct Pixel { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Pixel VS(uint id : SV_VertexID) {
    Pixel p;
    p.uv = float2(id == 1 ? 2 : 0, id == 2 ? 2 : 0);
    p.position = float4(p.uv.x * 2 - 1, 1 - p.uv.y * 2, 0, 1);
    return p;
}
#if MADE_DEPTH
struct Out { float4 colour : SV_Target0; float depth : SV_Target1; };
#else
struct Out { float4 colour : SV_Target0; };
#endif
Out PS(Pixel p) {
    const float3 at = float3(p.uv, 1);
    const float3 h = float3(dot(row0.xyz, at), dot(row1.xyz, at), dot(row2.xyz, at));
    Out o;
#if MADE_DEPTH
    o.depth = 0;                                                                   // the turn alone knows no depth: far
#endif
    o.colour = h.z <= 1e-6 ? float4(0, 0, 0, 1)                                    // behind the drawn eye: nothing it saw
                           : Read(h.xy / h.z);                                     // past its edge: the edge drawn out
    return o;
}
)";
constexpr UINT kTurnConstants = 16;

// Passes 2 and 3: a grid over the drawn picture, each point put back at its distance (reverse-Z, infinite: near / depth),
// moved (what the hero carries by `carry`) and projected into this frame's eye, nearer over farther. Pass 3 (reach.z > 0): a cell
// across an edge as two flat layers, each pixel in the one its own depth is nearer (turn_math.h SplitCell); pass 2's picture kept
// where it shows the same surface (keep, turn_math.h KeepOwn).
const char kDepthShader[] = R"(
Texture2D<float4> drawn : register(t0);
Texture2D<float> drawnDepth : register(t1);
Texture2D<float4> own : register(t2);           // pass 3: this eye's own picture as pass 2 left it (keep)
Texture2D<float> ownDepth : register(t3);       // and pass 2's depth test there, 0 where it drew nothing
Texture2D<float> nearSource : register(t4);     // the nearest depth within reach of each block (kNearShader): the drawn eye's
Texture2D<float> nearTarget : register(t5);     // this eye's, from pass 2's depth test
Texture2D<float> effectsSource : register(t6);  // the strongest of the game's effects layer round each block (Effects): the drawn eye's
Texture2D<float> effectsTarget : register(t7);  // this eye's, as it was last drawn
SamplerState edge : register(s0);
cbuffer Move : register(b0) {
    float4 move0; float4 move1; float4 move2;      // Move's rows
    float4 drawnTan;                               // the drawn eye's tangents: left, right, down, up
    float4 nowTan;                                 // this frame's
    float4 depthInfo;                              // near; the depth's drawn part x, y
    uint4 grid;                                    // cells across, down; z: 1 Catmull-Rom (Sharp), 0 bilinear
    float4 carry0; float4 carry1; float4 carry2;   // Carry's rows
    float4 reach;                                  // carried nearer than x, the world's farther than y; z: the edge scale (0: no layers)
    float4 keep;                                   // x: parallax pixels a unit of the depth test (KeepScale), 0 off; y: the same surface within;
                                                   // z: only round something nearer by this many pixels, 0 everywhere (or where the effects say)
    float4 nearInfo;                               // x, y: the picture's coordinates to the near blocks; z, w: this frame's pixels to them
    float4 effects;                                // x, y: the effects' blocks across, down; z: from what level they count, 0 off
};
)" TURN_SHARP R"(
// split: 0 the whole cell; across an edge, > 0 the nearer layer (its pixels nearer than split), < 0 the farther (the rest)
struct Pixel { float4 position : SV_Position; float2 uv : TEXCOORD0; noperspective float depth : TEXCOORD1; nointerpolation float split : TEXCOORD2; };
static const float2 Corner[6] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(0, 1), float2(1, 0), float2(1, 1) };
static const float Ahead = 1e-3;        // nearer than this to this frame's eye: cut off (turnwarp::kAhead)
static const float EdgePixels = 2;      // turnwarp::kEdgePixels
// The nearest of the texels round a point (reverse-Z: the largest), so an edge stays with what is in front; the sky (0) far.
float Nearest(float2 uv) {
    const float4 d4 = drawnDepth.GatherRed(edge, uv * depthInfo.yz);
    return max(max(max(d4.x, d4.y), max(d4.z, d4.w)), 1e-6);
}
// The farthest and the nearest of them.
float2 Range(float2 uv) {
    const float4 d4 = drawnDepth.GatherRed(edge, uv * depthInfo.yz);
    return max(float2(min(min(d4.x, d4.y), min(d4.z, d4.w)), max(max(d4.x, d4.y), max(d4.z, d4.w))), 1e-6);
}
// layer (the instance): 0 every cell; 1 only the farther part of a cell across an edge.
Pixel VS(uint id : SV_VertexID, uint layer : SV_InstanceID) {
    const uint cell = id / 6;
    const float2 at = float2(cell % grid.x, cell / grid.x), size = float2(grid.xy), corner = Corner[id % 6];
    const float2 uv = (at + corner) / size;
    float d, split = 0;
    if (reach.z > 0) {   // the drawn eye's picture: its texels at the cell's corners and middle
        const float2 a = Range(at / size), b = Range((at + float2(1, 0)) / size), c = Range((at + float2(0, 1)) / size),
                     f = Range((at + 1) / size), m = Range((at + 0.5) / size);
        const float back = min(min(min(a.x, b.x), min(c.x, f.x)), m.x), front = max(max(max(a.y, b.y), max(c.y, f.y)), m.y);
        d = corner.y > 0 ? (corner.x > 0 ? f.y : c.y) : (corner.x > 0 ? b.y : a.y);   // Nearest(uv)
        if ((front - back) * reach.z > EdgePixels) { split = (front + back) * 0.5; d = layer ? back : front; if (layer) split = -split; }
    } else d = Nearest(uv);
    Pixel o;
    o.uv = uv;
    o.split = split;
    o.depth = 0;
    if (layer && split == 0) { o.position = float4(0, 0, 0, -1); return o; }   // no farther part: out of the clip volume
    const float away = depthInfo.x / d;
    const float3 p = float3(lerp(drawnTan.x, drawnTan.y, uv.x), lerp(drawnTan.w, drawnTan.z, uv.y), -1) * away;
    const float3 world = float3(dot(move0.xyz, p) + move0.w, dot(move1.xyz, p) + move1.w, dot(move2.xyz, p) + move2.w);
    const float3 carried = float3(dot(carry0.xyz, p) + carry0.w, dot(carry1.xyz, p) + carry1.w, dot(carry2.xyz, p) + carry2.w);
    const float3 q = reach.y > reach.x ? lerp(carried, world, saturate((away - reach.x) / (reach.y - reach.x))) : world;
    const float w = -q.z;   // this frame's projection, its depth reversed: nearer is larger
    o.depth = depthInfo.x / max(w, 1e-6);   // this frame's eye's own reverse-Z depth (near / distance): linear on the screen
    o.position = float4(2 * (q.x - nowTan.x * w) / (nowTan.y - nowTan.x) - w, w - 2 * (nowTan.w * w - q.y) / (nowTan.w - nowTan.z), Ahead, w);
    return o;
}
// Something nearer than this surface by more than keep.z pixels of parallax within reach: as the drawn eye saw it round here (where
// its glow was left), or as this eye did (where its glow belongs).
bool NearThing(Pixel p, float there) {
    const float source = nearSource.Load(int3(p.uv * nearInfo.xy, 0)), target = nearTarget.Load(int3(p.position.xy * nearInfo.zw, 0));
    return (source - Nearest(p.uv)) * reach.z > keep.z || (target - there) * keep.x > keep.z;
}
// Something of the game's effects layer round here: as the drawn eye drew it, round where this pixel comes from (where it was left),
// or as this eye did, round here (where it belongs).
bool Effect(Pixel p) {
    const float2 here = p.position.xy * nearInfo.zw / nearInfo.xy;   // this frame's picture coordinates
    const float source = effectsSource.Load(int3(p.uv * depthInfo.yz * effects.xy, 0)), target = effectsTarget.Load(int3(here * depthInfo.yz * effects.xy, 0));
    return max(source, target) >= effects.z;
}
float4 Colour(Pixel p) {
    if (keep.x > 0) {   // where this eye's own picture shows the same surface, it stays (round near things: keep.z; effects: effects.z)
        const int3 at = int3(p.position.xy, 0);
        const float there = ownDepth.Load(at);
        const bool where = (keep.z <= 0 && effects.z <= 0) || (keep.z > 0 && NearThing(p, there)) || (effects.z > 0 && Effect(p));
        if (there > 0 && abs(p.position.z - there) * keep.x <= keep.y && where) return own.Load(at);
    }
    return grid.z ? Sharp(drawn, edge, p.uv) : drawn.SampleLevel(edge, p.uv, 0);
}
// The texel a point of the picture lies in. A cell across an edge sorts each pixel into its two layers by this one texel: by the four
// round it (Nearest) each layer, a fraction of a texel apart, could sort a texel out of both, and it was left a hole - the eye's own
// older picture through it, a stripe off a roof's ridge (pcvr's turn captures, 2026-10-25; bearstonem/d2r-pcvr 5fa6874, MIT).
float Texel(float2 uv) {
    float w, h;
    drawnDepth.GetDimensions(w, h);
    return drawnDepth.Load(int3(min(int2(uv * depthInfo.yz * float2(w, h)), int2(w, h) - 1), 0));
}
bool OtherLayer(Pixel p) { return p.split != 0 && (Texel(p.uv) > abs(p.split)) != (p.split > 0); }
#if MADE_DEPTH
struct Out { float4 colour : SV_Target0; float depth : SV_Target1; };
Out PS(Pixel p) { if (OtherLayer(p)) discard; Out o; o.colour = Colour(p); o.depth = p.depth; return o; }
#else
float4 PS(Pixel p) : SV_Target { if (OtherLayer(p)) discard; return Colour(p); }
#endif
)";
constexpr UINT kDepthConstants = 56;

// Grid::keep's near things: the nearest depth in each kNearBlock block (Reduce), then the nearest within reach of each block (Spread,
// across then down). And the effects: the strongest channel of one of the game's effects targets over each block, blended as the
// larger into the block (EffectsReduce), then spread the same way.
const char kNearShader[] = R"(
Texture2D<float> input : register(t0);
SamplerState edge : register(s0);
cbuffer Near : register(b0) {
    float4 area;     // the target's width and height in pixels; the depth's drawn part x, y
    float4 spread;   // its direction x, y
    uint4 limits;    // blocks across, down; the reach in blocks
};
static const int Block = 16;   // turnwarp::kNearBlock
float4 VS(uint id : SV_VertexID) : SV_Position {
    const float2 uv = float2(id == 1 ? 2 : 0, id == 2 ? 2 : 0);
    return float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1);
}
// The block's nearest depth (reverse-Z: the largest).
float Reduce(float4 position : SV_Position) : SV_Target {
    const float2 first = floor(position.xy) * Block;
    float nearest = 0;
    [unroll] for (int y = 1; y < Block; y += 2) [unroll] for (int x = 1; x < Block; x += 2) {
        const float4 d4 = input.GatherRed(edge, (first + float2(x, y)) / area.xy * area.zw);
        nearest = max(nearest, max(max(d4.x, d4.y), max(d4.z, d4.w)));
    }
    return nearest;
}
float Spread(float4 position : SV_Position) : SV_Target {
    const int2 at = int2(position.xy), step = int2(spread.xy), size = int2(limits.xy);
    float nearest = input.Load(int3(at, 0));
    for (int i = 1; i <= int(limits.z); ++i) {
        const int2 a = at + step * i, b = at - step * i;
        if (all(a < size)) nearest = max(nearest, input.Load(int3(a, 0)));
        if (all(b >= 0)) nearest = max(nearest, input.Load(int3(b, 0)));
    }
    return nearest;
}
)";
const char kEffectsShader[] = R"(
Texture2D<float4> input : register(t0);
SamplerState edge : register(s0);
cbuffer Near : register(b0) { float4 area; float4 spread; uint4 limits; };   // kNearShader's
static const int Block = 16;   // turnwarp::kNearBlock
// Four texels a sample, between them.
float Reduce(float4 position : SV_Position) : SV_Target {
    const float2 first = floor(position.xy) * Block;
    float strongest = 0;
    [unroll] for (int y = 1; y < Block; y += 2) [unroll] for (int x = 1; x < Block; x += 2) {
        const float4 c = input.SampleLevel(edge, (first + float2(x, y)) / area.xy * area.zw, 0);
        strongest = max(strongest, max(max(c.r, c.g), max(c.b, c.a)));
    }
    return strongest;
}
)";
constexpr UINT kNearConstants = 12;

// Gpu::Rims (kRimPixels, [stereo] turns_rims): a depth, the void within kRimPixels of something taken as the nearest depth there. Through
// pass 1's root signature: the depth at t0. bearstonem/d2r-pcvr 2de7cbd (MIT).
const char kRimShader[] = R"(
Texture2D<float> input : register(t0);   // reverse-Z: nearer is larger, 0 the void
static const int Rim = 2;                // turnwarp::kRimPixels
float4 VS(uint id : SV_VertexID) : SV_Position {
    const float2 uv = float2(id == 1 ? 2 : 0, id == 2 ? 2 : 0);
    return float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1);
}
float PS(float4 position : SV_Position) : SV_Target {
    const int2 at = int2(position.xy);
    const float d = input.Load(int3(at, 0));
    if (d > 0) return d;   // anything with depth keeps its own
    uint width, height;
    input.GetDimensions(width, height);
    const int2 last = int2(width, height) - 1;
    float nearest = 0;
    [unroll] for (int y = -Rim; y <= Rim; ++y) [unroll] for (int x = -Rim; x <= Rim; ++x)
        if (x * x + y * y <= Rim * Rim + 1) nearest = max(nearest, input.Load(int3(clamp(at + int2(x, y), 0, last), 0)));
    return nearest;
}
)";

// Gpu::Fill ([stereo] turns_fill): beside anything near, the eye not drawn sees a strip the drawn eye could not (pass 3 reached nothing
// there), and keeps its own picture from a frame back there. A monster that stood there a frame ago and has moved since was left in
// it: a ghost beside it in one eye (pcvr's owner, 2026-10-23). Such a strip is always of what is behind - the drawn eye's view of it was
// blocked by something nearer - so where this eye's own picture there is nearer than the farther end of the strip along the row (the
// drawn eye's), it is something since gone: the picture beside that end instead (filled from what is behind, as depth-image-based
// rendering does). Where it shows what is behind, its own texture stays. Through the grids' root signature: the picture at t0, pass 3's
// depth test at t1, pass 2's at t2. bearstonem/d2r-pcvr 8ae54f2 (MIT); the made depth (MADE_DEPTH) of that end too, ours.
// Stricter than his (the author's headset test of 98f8b8d6, 2026-10-26: ticked, more ghosting, not less): "nearer than the farther end"
// alone also took for a ghost what this eye rightly sees and the drawn eye does not, and painted the column beside it over it -
//  - at the eye's outer edge, past the drawn eye's picture (one end found, nothing that hid it): now both ends are needed;
//  - a thing between the two, behind the near thing from the drawn eye (a barrel behind a monster): now only what lies in the near
//    end's half of the depth between the ends - a frame-old copy of the thing that hid the strip - and only with the near end on the
//    side the eyes' parallax puts it (made right of the drawn eye: on the left);
// and the strip is filled from what is behind mirrored past its end, where that is still what is behind, not one column drawn out.
const char kHoleShader[] = R"(
Texture2D<float4> picture : register(t0);   // the eye made as the grids left it (a copy)
Texture2D<float> covered : register(t1);    // pass 3's depth test: 0 where it reached nothing
Texture2D<float> ownDepth : register(t2);   // pass 2's: 0 where it drew nothing
// holes: x: how far along the row to look either side, pixels; y: parallax pixels a unit of the depth test; z: within how many it shows
// what is behind; w: the depth test to the made depth (near / Ahead). sides.x: +1 what is behind is on the strip's right (the eye made
// right of the drawn one), -1 on its left, 0 either
cbuffer Holes : register(b0) { float4 holes; float4 sides; };
float4 VS(uint id : SV_VertexID) : SV_Position {
    const float2 uv = float2(id == 1 ? 2 : 0, id == 2 ? 2 : 0);
    return float4(uv.x * 2 - 1, 1 - uv.y * 2, 0, 1);
}
#if MADE_DEPTH
struct Out { float4 colour : SV_Target0; float depth : SV_Target1; };
#else
struct Out { float4 colour : SV_Target0; };
#endif
Out PS(float4 position : SV_Position) {
    const int2 at = int2(position.xy);
    if (covered.Load(int3(at, 0)) > 0) discard;   // the drawn eye's picture: as it is
    const float own = ownDepth.Load(int3(at, 0));
    if (own <= 0) discard;   // nothing of its own with a depth here: as the turn left it
    uint width, height;
    covered.GetDimensions(width, height);
    float left = 0, right = 0;   // the strip's ends: the drawn eye's nearest pixels either side
    int2 l = at, r = at;
    for (int k = 1; k <= int(holes.x) && (left <= 0 || right <= 0); ++k) {
        if (left <= 0 && at.x - k >= 0) { const float d = covered.Load(int3(at.x - k, at.y, 0)); if (d > 0) { left = d; l = int2(at.x - k, at.y); } }
        if (right <= 0 && at.x + k < int(width)) { const float d = covered.Load(int3(at.x + k, at.y, 0)); if (d > 0) { right = d; r = int2(at.x + k, at.y); } }
    }
    if (left <= 0 || right <= 0) discard;   // both ends: the thing that hid the strip and what is behind it (not the eye's outer edge)
    const bool fromLeft = left <= right;    // the farther end (reverse-Z: the smaller)
    if (sides.x != 0 && (fromLeft ? -1.0 : 1.0) != sides.x) discard;   // what is behind not on the side the parallax uncovers
    const float end = fromLeft ? left : right, hid = fromLeft ? right : left;
    if ((hid - end) * holes.y <= holes.z) discard;   // no edge between the ends: nothing hid it
    if ((own - end) * holes.y <= holes.z || own * 2 < hid + end) discard;   // its own shows what is behind, or something between: it stays
    const int2 e = fromLeft ? l : r, mirror = int2(clamp(2 * e.x - at.x, 0, int(width) - 1), at.y);
    const float m = covered.Load(int3(mirror, 0));
    Out o;
    o.colour = picture.Load(int3(m > 0 && abs(m - end) * holes.y <= holes.z ? mirror : e, 0));
#if MADE_DEPTH
    o.depth = end * holes.w;
#endif
    return o;
}
)";
constexpr float kHoleReachPx = 256;   // how far along the row a strip's ends are looked for: a 4096-wide eye's pixels (pcvr's 128: a
                                      // hand's strip is wider, and both ends are needed now)
constexpr UINT kHoleConstants = 8;

// A slot's descriptors: [0] pass 1's picture; pass 2's table and pass 3's, kGridTable each (a picture, its depth; pass 2's picture and
// its depth test as it left them; the near blocks, the drawn eye's and this eye's; the effects blocks, the same); one between for the
// near passes; the two depths the rims are taken from (Rims: this eye's own, the drawn eye's); a grid's table for the strips (Fill).
// Effects uses [0..3] of a slot of its own: the layer's two targets, the two blocks between.
constexpr UINT kGridTable = 8;
constexpr UINT kOwnTable = 1, kDrawnTable = kOwnTable + kGridTable, kBetween = kDrawnTable + kGridTable, kRimTable = kBetween + 1,
               kHoleTable = kRimTable + 2, kPerSlot = kHoleTable + kGridTable;
// The RTV heap: the picture, the made depth, the near blocks (3), the effects blocks (4), the rims (2).
constexpr UINT kRtvPicture = 0, kRtvMade = 1, kRtvNears = 2, kRtvEffects = 5, kRtvRims = 9, kRtvs = 11;
const D3D12_RESOURCE_STATES kReadable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

template <class T> void Free(T*& p) { if (p) { p->Release(); p = nullptr; } }

// The shaders' bytecode, compiled once a process (pcvr's 84f560d, 2026-10-20: the compiles were about 106 of the 112 ms its passes
// took on the draw thread at every new session), copied out of the compiler's blobs, which must not outlive d3dcompiler_47. A shader
// that fails to compile stays failed for the process (its pass unavailable, as before); an exception inside leaves what did not
// finish unavailable instead of escaping call_once (pcvr's 4e2cc65).
struct Compiled {
    bool compiler = false;
    std::vector<uint8_t> vs, ps[2], depthVs, depthPs[2], nearVs, nearReduce, nearSpread, effectsReduce, rimVs, rimPs, holeVs, holePs[2];
    std::string errors, depthErrors, nearErrors, effectsErrors, rimErrors, holeErrors;   // the first failure's messages of each group
};
const Compiled& Shaders() {
    static std::once_flag once;
    static Compiled c;
    std::call_once(once, [] {
        const HMODULE dc = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        const pD3DCompile compile = dc ? (pD3DCompile)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (compile) try {
            c.compiler = true;
            // made: the variant that also writes the made eye's depth (MadeDepth, [stereo] turn_overlay_hide) as a second target
            auto build = [&](const char* src, const char* entry, const char* target, bool made, std::vector<uint8_t>& out, std::string* errors) {
                ID3DBlob* code = nullptr;
                ID3DBlob* err = nullptr;
                const D3D_SHADER_MACRO defs[] = {{"MADE_DEPTH", made ? "1" : "0"}, {nullptr, nullptr}};
                if (SUCCEEDED(compile(src, strlen(src), "d2rvr_turn", defs, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err))) {
                    const uint8_t* p = (const uint8_t*)code->GetBufferPointer();
                    out.assign(p, p + code->GetBufferSize());
                } else if (errors && errors->empty()) {
                    *errors = err ? (const char*)err->GetBufferPointer() : "?";
                }
                Free(code);
                Free(err);
            };
            build(kTurnShader, "VS", "vs_5_0", false, c.vs, &c.errors);
            build(kTurnShader, "PS", "ps_5_0", false, c.ps[0], &c.errors);
            build(kTurnShader, "PS", "ps_5_0", true, c.ps[1], nullptr);
            build(kDepthShader, "VS", "vs_5_0", false, c.depthVs, &c.depthErrors);
            build(kDepthShader, "PS", "ps_5_0", false, c.depthPs[0], &c.depthErrors);
            build(kDepthShader, "PS", "ps_5_0", true, c.depthPs[1], nullptr);
            build(kNearShader, "VS", "vs_5_0", false, c.nearVs, &c.nearErrors);
            build(kNearShader, "Reduce", "ps_5_0", false, c.nearReduce, &c.nearErrors);
            build(kNearShader, "Spread", "ps_5_0", false, c.nearSpread, &c.nearErrors);
            build(kEffectsShader, "Reduce", "ps_5_0", false, c.effectsReduce, &c.effectsErrors);
            build(kRimShader, "VS", "vs_5_0", false, c.rimVs, &c.rimErrors);
            build(kRimShader, "PS", "ps_5_0", false, c.rimPs, &c.rimErrors);
            build(kHoleShader, "VS", "vs_5_0", false, c.holeVs, &c.holeErrors);
            build(kHoleShader, "PS", "ps_5_0", false, c.holePs[0], &c.holeErrors);
            build(kHoleShader, "PS", "ps_5_0", true, c.holePs[1], nullptr);
        } catch (...) {}   // out of memory: what did not finish stays unavailable
        if (dc) FreeLibrary(dc);
    });
    return c;
}
D3D12_SHADER_BYTECODE Code(const std::vector<uint8_t>& v) { return {v.data(), v.size()}; }

ID3D12Resource* Texture(ID3D12Device* dev, UINT64 w, UINT h, DXGI_FORMAT f, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                        const D3D12_CLEAR_VALUE* clear, const wchar_t* name) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = d.MipLevels = 1;
    d.Format = f;
    d.SampleDesc.Count = 1;
    d.Flags = flags;
    ID3D12Resource* r = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, clear, __uuidof(ID3D12Resource), (void**)&r))) return nullptr;
    r->SetName(name);
    return r;
}

D3D12_RESOURCE_BARRIER Tr(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
    return b;
}

// Warm: the build under way or done for a device and format.
struct Warming {
    ID3D12Device* dev = nullptr;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::atomic<bool> done{false};
};
std::mutex g_warmLock;
std::shared_ptr<Warming> g_warm;

}  // namespace

bool Warm(ID3D12Device* dev, DXGI_FORMAT target) {
    if (!dev) return false;
    std::shared_ptr<Warming> w;
    {
        std::lock_guard<std::mutex> hold(g_warmLock);
        if (g_warm && g_warm->dev == dev && g_warm->format == target) return g_warm->done.load();
        w = std::make_shared<Warming>();
        w->dev = dev;
        w->format = target;
        g_warm = w;
    }
    dev->AddRef();   // the worker's, while it builds
    try {
        std::thread([w] {
            Gpu g;
            g.Init(w->dev, w->format, nullptr);   // the compile (once a process), and the driver's first pipelines for this one
            g.Release();
            w->dev->Release();
            w->done.store(true);
        }).detach();
    } catch (...) {   // no thread: Init builds it all on the caller's, as before
        dev->Release();
        w->done.store(true);
    }
    return false;
}

void Gpu::Drop(ID3D12Resource*& r) {
    if (!r) return;
    if (retire_) retire_(r);
    else r->Release();
    r = nullptr;
}

void Gpu::Release() {
    Free(depthTarget_);
    Free(drawnTarget_);
    Free(ownTurned_);
    for (auto*& r : rims_) Free(r);
    for (auto*& n : nears_) Free(n);
    for (auto*& e : effects_) Free(e);
    effectsKept_[0] = effectsKept_[1] = false;
    Free(madeDepth_);
    Free(dsv_);
    Free(rtv_);
    Free(srv_);
    for (int k = 0; k < 2; ++k) { Free(depthPso_[k]); Free(pso_[k]); }
    Free(reducePso_);
    Free(spreadPso_);
    Free(effectsPso_);
    Free(rimPso_);
    for (auto*& h : holePso_) Free(h);
    Free(nearRoot_);
    Free(depthRoot_);
    Free(root_);
    depthW_ = depthH_ = 0;
    format_ = DXGI_FORMAT_UNKNOWN;
    dev_ = nullptr;
    tried_ = false;
}

bool Gpu::Init(ID3D12Device* dev, DXGI_FORMAT target, void (*log)(const char*), void (*retire)(ID3D12Resource*)) {
    if (pso_[0] && dev == dev_ && target == format_) return true;
    if (tried_ && dev == dev_ && target == format_) return false;   // failed for this one already
    Release();
    dev_ = dev;
    format_ = target;
    retire_ = retire;
    tried_ = true;
    auto say = [&](const char* fmt, const char* a) {
        if (!log) return;
        char b[384];
        snprintf(b, sizeof b, fmt, a);
        log(b);
    };
    if (!dev) return false;
    const Compiled& c = Shaders();
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    HMODULE d12 = GetModuleHandleW(L"d3d12.dll");
    const SerializeFn serialize = d12 ? (SerializeFn)GetProcAddress(d12, "D3D12SerializeRootSignature") : nullptr;
    if (!c.compiler || !serialize) {
        say("vrcam: eyes by turns - no shader compiler (%s): the eye not drawn goes as it was last drawn", "d3dcompiler_47.dll");
        return false;
    }
    if (c.vs.empty() || c.ps[0].empty()) {
        say("vrcam: eyes by turns - a shader did not compile: %s", c.errors.c_str());
        return false;
    }
    auto rootOf = [&](UINT srvs, UINT constants, D3D12_SHADER_VISIBILITY vis, ID3D12RootSignature** out) {
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvs, 0, 0, 0};
        D3D12_ROOT_PARAMETER rp[2] = {};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[0].DescriptorTable = {1, &range};
        rp[0].ShaderVisibility = vis;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[1].Constants = {0, 0, constants};
        rp[1].ShaderVisibility = vis;
        D3D12_STATIC_SAMPLER_DESC smp{};
        smp.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        smp.MaxLOD = D3D12_FLOAT32_MAX;
        smp.ShaderVisibility = vis;
        const D3D12_ROOT_SIGNATURE_DESC rsd{2, rp, 1, &smp, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        ID3DBlob* blob = nullptr, *err = nullptr;
        const bool ok = SUCCEEDED(serialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) &&
                        SUCCEEDED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)out));
        Free(blob);
        Free(err);
        return ok;
    };
    // rt: the target's format (DXGI_FORMAT_UNKNOWN: format_); made: the made depth a second target; depth: the grids' test; larger: blended as the larger
    auto psoOf = [&](ID3D12RootSignature* rs, const std::vector<uint8_t>& vs, const std::vector<uint8_t>& ps, DXGI_FORMAT rt, bool depth, bool made,
                     bool larger, ID3D12PipelineState** out) {
        if (vs.empty() || ps.empty()) return false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = rs;
        d.VS = Code(vs);
        d.PS = Code(ps);
        D3D12_RENDER_TARGET_BLEND_DESC& b = d.BlendState.RenderTarget[0];
        b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        if (larger) {
            b.BlendEnable = TRUE;
            b.SrcBlend = b.DestBlend = b.SrcBlendAlpha = b.DestBlendAlpha = D3D12_BLEND_ONE;
            b.BlendOp = b.BlendOpAlpha = D3D12_BLEND_OP_MAX;
        }
        d.SampleMask = UINT_MAX;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = made ? 2 : 1;   // the picture, and its depth (MadeDepth)
        d.RTVFormats[0] = rt == DXGI_FORMAT_UNKNOWN ? format_ : rt;
        if (made) d.RTVFormats[1] = DXGI_FORMAT_R32_FLOAT;
        d.SampleDesc.Count = 1;
        if (depth) {
            d.DepthStencilState.DepthEnable = TRUE;
            d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;   // reverse-Z: nearer is larger
            d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        }
        return SUCCEEDED(dev->CreateGraphicsPipelineState(&d, __uuidof(ID3D12PipelineState), (void**)out));
    };
    bool ok = rootOf(1, kTurnConstants, D3D12_SHADER_VISIBILITY_PIXEL, &root_) && psoOf(root_, c.vs, c.ps[0], DXGI_FORMAT_UNKNOWN, false, false, false, &pso_[0]);
    // the made depth's variant: without it the overlays of the eye made are hidden by nothing (as with turn_overlay_hide=0)
    if (ok && !psoOf(root_, c.vs, c.ps[1], DXGI_FORMAT_UNKNOWN, false, true, false, &pso_[1])) {
        Free(pso_[1]);
        say("vrcam: eyes by turns - the made eye's depth unavailable (%s): its overlays are hidden by nothing", "its pass could not be made");
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSlots * kPerSlot, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ok = ok && SUCCEEDED(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&srv_));
    D3D12_DESCRIPTOR_HEAP_DESC rd{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvs, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    ok = ok && SUCCEEDED(dev->CreateDescriptorHeap(&rd, __uuidof(ID3D12DescriptorHeap), (void**)&rtv_));
    if (!ok) {
        for (int k = 0; k < 2; ++k) Free(pso_[k]);
        Free(root_); Free(srv_); Free(rtv_);
        say("vrcam: eyes by turns - the GPU pass could not be made (%s): the eye not drawn goes as it was last drawn", "pipeline or heaps");
        return false;
    }
    srvInc_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    rtvInc_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    srv_->SetName(L"d2rvr eyes by turns srv");
    // the rims' pass ([stereo] turns_rims): without it they stay with the void
    if (!psoOf(root_, c.rimVs, c.rimPs, DXGI_FORMAT_R32_FLOAT, false, false, false, &rimPso_)) {
        Free(rimPso_);
        if (!c.rimErrors.empty()) say("vrcam: eyes by turns - a shader did not compile: %s", c.rimErrors.c_str());
        say("vrcam: eyes by turns - the rims round things against the sky moved with them unavailable (%s): they stay with the sky",
            "its pass could not be made");
    }
    // the depth passes: without them the eye not drawn is turned only
    const bool depthOk = rootOf(kGridTable, kDepthConstants, D3D12_SHADER_VISIBILITY_ALL, &depthRoot_) &&
                         psoOf(depthRoot_, c.depthVs, c.depthPs[0], DXGI_FORMAT_UNKNOWN, true, false, false, &depthPso_[0]);
    D3D12_DESCRIPTOR_HEAP_DESC dd{D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};   // pass 2's test, pass 3's
    if (!depthOk || FAILED(dev->CreateDescriptorHeap(&dd, __uuidof(ID3D12DescriptorHeap), (void**)&dsv_))) {
        for (int k = 0; k < 2; ++k) Free(depthPso_[k]);
        Free(depthRoot_); Free(dsv_);
        if (!c.depthErrors.empty()) say("vrcam: eyes by turns - a shader did not compile: %s", c.depthErrors.c_str());
        say("vrcam: eyes by turns - moving with the depth unavailable (%s): the eye not drawn is turned only", "its pass could not be made");
        return true;
    }
    if (pso_[1] && !psoOf(depthRoot_, c.depthVs, c.depthPs[1], DXGI_FORMAT_UNKNOWN, true, true, false, &depthPso_[1])) {
        Free(depthPso_[1]);
        Free(pso_[1]);   // the made depth whole or not at all: from the turn alone it would be far everywhere
        say("vrcam: eyes by turns - the made eye's depth unavailable (%s): its overlays are hidden by nothing", "its grid pass could not be made");
    }
    // the strips' pass ([stereo] turns_fill), each variant as the grids': without it they keep this eye's own picture as before
    for (int v = 0; v < 2; ++v)
        if ((v == 0 || pso_[1]) && !psoOf(depthRoot_, c.holeVs, c.holePs[v], DXGI_FORMAT_UNKNOWN, false, v == 1, false, &holePso_[v])) Free(holePso_[v]);
    if (!holePso_[0]) {
        Free(holePso_[1]);
        if (!c.holeErrors.empty()) say("vrcam: eyes by turns - a shader did not compile: %s", c.holeErrors.c_str());
        say("vrcam: eyes by turns - filling the strips beside near things unavailable (%s): they keep the eye's own picture", "its pass could not be made");
    }
    // the near things' and the effects' passes ([stereo] turns_keep_own): without them those modes keep the drawn eye's picture
    if (!rootOf(1, kNearConstants, D3D12_SHADER_VISIBILITY_PIXEL, &nearRoot_) ||
        !psoOf(nearRoot_, c.nearVs, c.nearReduce, DXGI_FORMAT_R32_FLOAT, false, false, false, &reducePso_) ||
        !psoOf(nearRoot_, c.nearVs, c.nearSpread, DXGI_FORMAT_R32_FLOAT, false, false, false, &spreadPso_)) {
        Free(reducePso_); Free(spreadPso_); Free(nearRoot_);
        if (!c.nearErrors.empty()) say("vrcam: eyes by turns - a shader did not compile: %s", c.nearErrors.c_str());
        say("vrcam: eyes by turns - finding near things and the game's effects unavailable (%s): the eye not drawn keeps its own picture "
            "only with [stereo] turns_keep_own=2", "its passes could not be made");
        return true;
    }
    if (!psoOf(nearRoot_, c.nearVs, c.effectsReduce, DXGI_FORMAT_R32_FLOAT, false, false, true, &effectsPso_)) {
        Free(effectsPso_);
        if (!c.effectsErrors.empty()) say("vrcam: eyes by turns - a shader did not compile: %s", c.effectsErrors.c_str());
        say("vrcam: eyes by turns - finding the game's effects unavailable (%s): [stereo] turns_keep_own=3 keeps nothing", "its pass could not be made");
    }
    return true;
}

bool Gpu::Effects(ID3D12GraphicsCommandList* list, ID3D12Resource* const layer[2], const D3D12_RESOURCE_STATES states[2], int eye, float reach, int slot) {
    if (eye < 0 || eye > 1 || !list || !EffectsReady() || !layer[0] || !layer[1] || slot < 0 || slot >= kSlots) return false;
    effectsKept_[eye] = false;
    const D3D12_RESOURCE_DESC l = layer[0]->GetDesc();
    const UINT across = UINT((l.Width + kNearBlock - 1) / kNearBlock), down = (l.Height + kNearBlock - 1) / kNearBlock;
    if (effects_[0]) {
        const D3D12_RESOURCE_DESC d = effects_[0]->GetDesc();
        if (d.Width != across || d.Height != down) {   // the scene another size (the render scale): both eyes' blocks again
            for (auto*& e : effects_) Drop(e);
            effectsKept_[0] = effectsKept_[1] = false;
        }
    }
    if (!effects_[0]) {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += SIZE_T(rtvInc_) * kRtvEffects;
        for (auto*& e : effects_) {
            e = Texture(dev_, across, down, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        nullptr, L"d2rvr eyes by turns effects blocks");
            if (!e) {
                for (auto*& x : effects_) Free(x);
                return false;
            }
            dev_->CreateRenderTargetView(e, nullptr, rtv);
            rtv.ptr += rtvInc_;
        }
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srv_->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = srv_->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += SIZE_T(srvInc_) * kPerSlot * slot;
    gpu.ptr += UINT64(srvInc_) * kPerSlot * slot;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_SHADER_RESOURCE_VIEW_DESC single = sv;
    single.Format = DXGI_FORMAT_R32_FLOAT;
    for (int k = 0; k < 4; ++k) {   // the layer's two targets, the two blocks between
        D3D12_CPU_DESCRIPTOR_HANDLE at = cpu;
        at.ptr += SIZE_T(srvInc_) * k;
        dev_->CreateShaderResourceView(k < 2 ? layer[k] : effects_[k], k < 2 ? &sv : &single, at);
    }
    D3D12_RESOURCE_BARRIER in[2] = {};
    UINT ins = 0;
    for (int k = 0; k < 2; ++k)
        if (!(states[k] & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) in[ins++] = Tr(layer[k], states[k], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (ins) list->ResourceBarrier(ins, in);
    auto into = [&](int k, bool writing) {
        const D3D12_RESOURCE_BARRIER b = writing ? Tr(effects_[k], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
                                                 : Tr(effects_[k], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        list->ResourceBarrier(1, &b);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += SIZE_T(rtvInc_) * (kRtvEffects + k);
        if (writing) list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        return rtv;
    };
    auto pass = [&](ID3D12PipelineState* pso, int from, float x, float y) {
        list->SetPipelineState(pso);
        D3D12_GPU_DESCRIPTOR_HANDLE table = gpu;
        table.ptr += UINT64(srvInc_) * from;
        list->SetGraphicsRootDescriptorTable(0, table);
        uint32_t c[kNearConstants] = {};
        const float f[8] = {float(l.Width), float(l.Height), 1, 1, x, y, 0, 0};
        memcpy(c, f, sizeof f);
        c[8] = across;
        c[9] = down;
        c[10] = uint32_t(std::ceil(reach / kNearBlock));
        list->SetGraphicsRoot32BitConstants(1, kNearConstants, c, 0);
        list->DrawInstanced(3, 1, 0, 0);
    };
    ID3D12DescriptorHeap* heaps[] = {srv_};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(nearRoot_);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT vp{0.0f, 0.0f, float(across), float(down), 0.0f, 1.0f};
    const D3D12_RECT rect{0, 0, LONG(across), LONG(down)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &rect);
    const float none[4] = {};
    list->ClearRenderTargetView(into(2, true), none, 0, nullptr);   // both targets' strongest into the first between, the larger kept
    pass(effectsPso_, 0, 0, 0);
    pass(effectsPso_, 1, 0, 0);
    into(2, false);
    into(3, true);   // spread across into the second, then down into this eye's
    pass(spreadPso_, 2, 1, 0);
    into(3, false);
    into(eye, true);
    pass(spreadPso_, 3, 0, 1);
    into(eye, false);
    for (UINT k = 0; k < ins; ++k) std::swap(in[k].Transition.StateBefore, in[k].Transition.StateAfter);
    if (ins) list->ResourceBarrier(ins, in);
    effectsKept_[eye] = true;
    return true;
}

bool Gpu::Render(ID3D12GraphicsCommandList* list, ID3D12Resource* target, uint32_t w, uint32_t h, ID3D12Resource* base,
                 const std::array<float, 9>& homography, const Grid* own, const Grid* drawn, int slot, bool madeDepth, int eye, bool* kept) {
    if (kept) *kept = false;
    filled_ = rimmed_ = false;
    const int k = madeDepth && pso_[1] ? 1 : 0;   // 1: the made depth written too (MadeDepth)
    if (!pso_[0] || !list || !target || !base || !w || !h || slot < 0 || slot >= kSlots) return false;
    eye = eye ? 1 : 0;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = format_;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    D3D12_SHADER_RESOURCE_VIEW_DESC single = sv;
    single.Format = DXGI_FORMAT_R32_FLOAT;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu0 = srv_->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu0 = srv_->GetGPUDescriptorHandleForHeapStart();
    cpu0.ptr += SIZE_T(srvInc_) * kPerSlot * slot;
    gpu0.ptr += UINT64(srvInc_) * kPerSlot * slot;
    auto at = [&](UINT i) { D3D12_CPU_DESCRIPTOR_HANDLE c = cpu0; c.ptr += SIZE_T(srvInc_) * i; return c; };
    auto gpuAt = [&](UINT i) { D3D12_GPU_DESCRIPTOR_HANDLE g = gpu0; g.ptr += UINT64(srvInc_) * i; return g; };
    auto rtvAt = [&](UINT i) { D3D12_CPU_DESCRIPTOR_HANDLE c = rtv_->GetCPUDescriptorHandleForHeapStart(); c.ptr += SIZE_T(rtvInc_) * i; return c; };
    dev_->CreateShaderResourceView(base, &sv, at(0));
    D3D12_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = format_;
    rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvAt(kRtvPicture);   // [0] the picture, [1] its depth: one range
    dev_->CreateRenderTargetView(target, &rv, rtv);
    if (k && madeDepth_) {
        const D3D12_RESOURCE_DESC m = madeDepth_->GetDesc();
        if (m.Width != w || m.Height != h) Drop(madeDepth_);
    }
    if (k && !madeDepth_) {   // the made eye's depth, as a picture the effect reads (MadeDepth)
        madeDepth_ = Texture(dev_, w, h, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, kReadable, nullptr, L"d2rvr eyes by turns made depth");
        if (!madeDepth_) return false;
        ++madeGen_;   // another texture, perhaps at the old one's address: the effect's binding made again (MadeDepthGen)
    }
    D3D12_RESOURCE_BARRIER toTarget{};
    if (k) {
        D3D12_RENDER_TARGET_VIEW_DESC dr{};
        dr.Format = DXGI_FORMAT_R32_FLOAT;
        dr.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        dev_->CreateRenderTargetView(madeDepth_, &dr, rtvAt(kRtvMade));
        toTarget = Tr(madeDepth_, kReadable, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &toTarget);
    }
    const UINT targets = k ? 2 : 1;
    auto done = [&] {   // the made depth readable again for the effect
        if (!k) return true;
        const D3D12_RESOURCE_BARRIER back = Tr(madeDepth_, D3D12_RESOURCE_STATE_RENDER_TARGET, kReadable);
        list->ResourceBarrier(1, &back);
        return true;
    };
    const D3D12_VIEWPORT vp{0.0f, 0.0f, float(w), float(h), 0.0f, 1.0f};
    const D3D12_RECT rect{0, 0, LONG(w), LONG(h)};

    // 1. the turn
    ID3D12DescriptorHeap* heaps[] = {srv_};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(root_);
    list->SetGraphicsRootDescriptorTable(0, gpuAt(0));
    float rows[kTurnConstants] = {};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) rows[r * 4 + c] = homography[r * 3 + c];
    rows[12] = sharp_ ? 1.0f : 0.0f;
    list->SetGraphicsRoot32BitConstants(1, kTurnConstants, rows, 0);
    list->OMSetRenderTargets(targets, &rtv, TRUE, nullptr);
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &rect);
    list->SetPipelineState(pso_[k]);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    {
        const int span = gpuspans::Begin(list, "made: turn");
        list->DrawInstanced(3, 1, 0, 0);
        gpuspans::End(list, span);
    }

    auto usable = [](const Grid* g) { return g && g->image && g->depth && g->depthView != DXGI_FORMAT_UNKNOWN && g->nearPlane > 0.0f; };
    const bool ownOk = usable(own), drawnOk = usable(drawn);
    if (!depthPso_[k] || (!ownOk && !drawnOk)) return done();
    if (!depthTarget_ || depthW_ != w || depthH_ != h) {   // the grids' depth tests, pass 2's picture as it left it, the near blocks: the eyes' size
        Drop(depthTarget_);
        Drop(drawnTarget_);
        Drop(ownTurned_);
        for (auto*& n : nears_) Drop(n);
        depthW_ = depthH_ = 0;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DXGI_FORMAT_D32_FLOAT;
        clear.DepthStencil.Depth = 0.0f;
        depthTarget_ = Texture(dev_, w, h, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                               L"d2rvr eyes by turns depth test (own)");
        drawnTarget_ = Texture(dev_, w, h, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
                               L"d2rvr eyes by turns depth test (drawn)");   // (R32 typeless: Fill reads it)
        ownTurned_ = Texture(dev_, w, h, format_, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, L"d2rvr eyes by turns own moved");
        if (!depthTarget_ || !drawnTarget_ || !ownTurned_) {
            Free(depthTarget_); Free(drawnTarget_); Free(ownTurned_);
            return done();   // turned only this frame
        }
        depthW_ = w;
        depthH_ = h;
        D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
        dv.Format = DXGI_FORMAT_D32_FLOAT;
        dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE d = dsv_->GetCPUDescriptorHandleForHeapStart();
        dev_->CreateDepthStencilView(depthTarget_, &dv, d);
        d.ptr += dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        dev_->CreateDepthStencilView(drawnTarget_, &dv, d);
        if (NearReady()) {
            for (UINT n = 0; n < 3; ++n) {
                nears_[n] = Texture(dev_, (w + kNearBlock - 1) / kNearBlock, (h + kNearBlock - 1) / kNearBlock, DXGI_FORMAT_R32_FLOAT,
                                    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, L"d2rvr eyes by turns near blocks");
                if (!nears_[n]) { for (auto*& x : nears_) Free(x); break; }
                dev_->CreateRenderTargetView(nears_[n], nullptr, rtvAt(kRtvNears + n));
            }
        }
    }
    auto dsvAt = [&](int i) {
        D3D12_CPU_DESCRIPTOR_HANDLE d = dsv_->GetCPUDescriptorHandleForHeapStart();
        d.ptr += SIZE_T(dev_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV)) * i;
        return d;
    };
    const uint32_t across = (w + kGridStep - 1) / kGridStep, down = (h + kGridStep - 1) / kGridStep;
    const uint32_t blocksAcross = (w + kNearBlock - 1) / kNearBlock, blocksDown = (h + kNearBlock - 1) / kNearBlock;
    // One grid: its picture and depth into the table at `table`, over its own depth test `test` (0 pass 2's, 1 pass 3's). keepOn: pass 2's
    // picture and depth test as it left them; nearOn: only within reach of near things; fxOn: also where the game's effects are.
    auto grid = [&](const Grid& g, UINT table, int test, bool keepOn, bool nearOn, bool fxOn) {
        dev_->CreateShaderResourceView(g.image, &sv, at(table));
        D3D12_SHADER_RESOURCE_VIEW_DESC dvw = sv;
        dvw.Format = g.depthView;
        dev_->CreateShaderResourceView(g.depth, &dvw, at(table + 1));
        dev_->CreateShaderResourceView(keepOn ? ownTurned_ : nullptr, &sv, at(table + 2));   // none: never read
        dev_->CreateShaderResourceView(keepOn ? depthTarget_ : nullptr, &single, at(table + 3));
        for (UINT n = 0; n < 2; ++n) dev_->CreateShaderResourceView(nearOn ? nears_[n] : nullptr, &single, at(table + 4 + n));
        for (UINT n = 0; n < 2; ++n) {   // the drawn eye's, this eye's
            const int e = n ? eye : eye ^ 1;
            dev_->CreateShaderResourceView(fxOn && effectsKept_[e] ? effects_[e] : nullptr, &single, at(table + 6 + n));
        }
        list->SetGraphicsRootSignature(depthRoot_);
        list->SetPipelineState(depthPso_[k]);
        list->SetGraphicsRootDescriptorTable(0, gpuAt(table));
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvAt(test);
        list->OMSetRenderTargets(targets, &rtv, TRUE, &dsv);
        list->RSSetViewports(1, &vp);
        list->RSSetScissorRects(1, &rect);
        list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);   // each grid over the last whole
        uint32_t c[kDepthConstants] = {};
        float f[kDepthConstants] = {};
        memcpy(f, g.move.data(), sizeof(float) * 12);
        f[12] = g.drawnTan.left; f[13] = g.drawnTan.right; f[14] = g.drawnTan.down; f[15] = g.drawnTan.up;
        f[16] = g.nowTan.left; f[17] = g.nowTan.right; f[18] = g.nowTan.down; f[19] = g.nowTan.up;
        f[20] = g.nearPlane; f[21] = g.scale[0]; f[22] = g.scale[1];
        memcpy(f + 28, g.carry.data(), sizeof(float) * 12);
        f[40] = g.nearW; f[41] = g.farW; f[42] = g.edgeScale;
        f[44] = keepOn ? KeepScale(g.edgeScale, g.nearPlane) : 0.0f;
        f[45] = g.keep.keep;
        f[46] = nearOn ? g.keep.keepNear : 0.0f;
        f[48] = float(w) / kNearBlock; f[49] = float(h) / kNearBlock; f[50] = 1.0f / kNearBlock; f[51] = 1.0f / kNearBlock;
        if (fxOn) {
            const D3D12_RESOURCE_DESC b = effects_[0]->GetDesc();
            f[52] = float(b.Width); f[53] = float(b.Height); f[54] = g.keep.keepEffects;
        }
        memcpy(c, f, sizeof f);
        c[24] = across;
        c[25] = down;
        c[26] = sharp_ ? 1 : 0;
        list->SetGraphicsRoot32BitConstants(1, kDepthConstants, c, 0);
        list->DrawInstanced(6 * across * down, g.edgeScale > 0.0f ? 2 : 1, 0, 0);   // the drawn eye's: the farther layers too
    };
    // The nearest depth within `reach` target pixels of each block, from `input` (in the slot) into nears_[out]: a block each, spread
    // across then down through the one between. (sx, sy): the input's drawn part.
    auto nearPass = [&](ID3D12PipelineState* pso, UINT from, int to, float sx, float sy, float dx, float dy, float reach) {
        D3D12_RESOURCE_BARRIER b = Tr(nears_[to], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &b);
        const D3D12_CPU_DESCRIPTOR_HANDLE r = rtvAt(kRtvNears + to);
        list->OMSetRenderTargets(1, &r, FALSE, nullptr);
        list->SetPipelineState(pso);
        list->SetGraphicsRootDescriptorTable(0, gpuAt(from));
        uint32_t c[kNearConstants] = {};
        const float f[8] = {float(w), float(h), sx, sy, dx, dy, 0, 0};
        memcpy(c, f, sizeof f);
        c[8] = blocksAcross;
        c[9] = blocksDown;
        c[10] = uint32_t(std::ceil(reach / kNearBlock));
        list->SetGraphicsRoot32BitConstants(1, kNearConstants, c, 0);
        list->DrawInstanced(3, 1, 0, 0);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list->ResourceBarrier(1, &b);
    };
    auto nearBlocks = [&](UINT input, int out, float sx, float sy, float reach) {
        nearPass(reducePso_, input, out, sx, sy, 0, 0, reach);
        nearPass(spreadPso_, kDrawnTable + 4 + out, 2, 1, 1, 1, 0, reach);   // (the drawn table holds nears_[0], [1])
        nearPass(spreadPso_, kBetween, out, 1, 1, 0, 1, reach);
    };
    // The rims (kRimPixels, [stereo] turns_rims): each depth with the void round things taken as theirs, this eye's own into rims_[0], the
    // drawn eye's into rims_[1]; everything after reads those.
    Grid ownRimmed, drawnRimmed;
    auto rimmed = [&](const Grid* g, int n, Grid& out) -> const Grid* {
        const D3D12_RESOURCE_DESC size = g->depth->GetDesc();
        if (rims_[n]) {
            const D3D12_RESOURCE_DESC r = rims_[n]->GetDesc();
            if (r.Width != size.Width || r.Height != size.Height) Drop(rims_[n]);
        }
        if (!rims_[n]) {
            rims_[n] = Texture(dev_, size.Width, size.Height, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, kReadable, nullptr,
                               n ? L"d2rvr eyes by turns rims (drawn)" : L"d2rvr eyes by turns rims (own)");
            if (!rims_[n]) return g;   // its own depth this frame
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE r = rtvAt(kRtvRims + n);
        dev_->CreateRenderTargetView(rims_[n], nullptr, r);
        D3D12_SHADER_RESOURCE_VIEW_DESC dvw = sv;
        dvw.Format = g->depthView;
        dev_->CreateShaderResourceView(g->depth, &dvw, at(kRimTable + n));
        D3D12_RESOURCE_BARRIER b = Tr(rims_[n], kReadable, D3D12_RESOURCE_STATE_RENDER_TARGET);
        list->ResourceBarrier(1, &b);
        list->OMSetRenderTargets(1, &r, FALSE, nullptr);
        const D3D12_VIEWPORT whole{0.0f, 0.0f, float(size.Width), float(size.Height), 0.0f, 1.0f};
        const D3D12_RECT wholeRect{0, 0, LONG(size.Width), LONG(size.Height)};
        list->RSSetViewports(1, &whole);
        list->RSSetScissorRects(1, &wholeRect);
        list->SetGraphicsRootSignature(root_);
        list->SetPipelineState(rimPso_);
        list->SetGraphicsRootDescriptorTable(0, gpuAt(kRimTable + n));
        list->DrawInstanced(3, 1, 0, 0);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list->ResourceBarrier(1, &b);
        out = *g;
        out.depth = rims_[n];
        out.depthView = DXGI_FORMAT_R32_FLOAT;
        rimmed_ = true;
        return &out;
    };
    if (rimsOn_ && rimPso_) {
        const int span = gpuspans::Begin(list, "made: rims");
        if (ownOk) own = rimmed(own, 0, ownRimmed);
        if (drawnOk) drawn = rimmed(drawn, 1, drawnRimmed);
        gpuspans::End(list, span);
    }
    if (ownOk) {
        const int span = gpuspans::Begin(list, "made: own grid");
        grid(*own, kOwnTable, 0, false, false, false);
        gpuspans::End(list, span);
    }
    if (drawnOk) {
        // Where this eye's own picture (pass 2) shows the same surface as the drawn eye's, it stays: seen from this eye, a frame old.
        // With keepNear only round near things (without their passes, nowhere); with keepEffects (also) where either eye's effects were.
        const Keep& kp = drawn->keep;
        const bool nearOn = kp.keepNear > 0.0f && NearReady() && nears_[0];
        const bool fxOn = kp.keepEffects > 0.0f && EffectsReady() && effects_[0] && (effectsKept_[0] || effectsKept_[1]);
        const bool somewhere = (kp.keepNear <= 0.0f && kp.keepEffects <= 0.0f) || nearOn || fxOn;   // everywhere, or where these say
        const bool keepOn = ownOk && kp.keep > 0.0f && drawn->edgeScale > 0.0f && somewhere;
        D3D12_RESOURCE_BARRIER b[3] = {Tr(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                       Tr(ownTurned_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
                                       Tr(depthTarget_, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
        const int keepSpan = keepOn ? gpuspans::Begin(list, "made: keep own (copy)") : -1;
        if (keepOn) {
            list->ResourceBarrier(2, b);
            D3D12_TEXTURE_COPY_LOCATION to{}, from{};
            to.pResource = ownTurned_; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            from.pResource = target; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            const D3D12_BOX box{0, 0, 0, w, h, 1};
            list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
            for (int i = 0; i < 2; ++i) std::swap(b[i].Transition.StateBefore, b[i].Transition.StateAfter);
            list->ResourceBarrier(3, b);   // the target back, the copy readable, pass 2's depth test readable
        }
        gpuspans::End(list, keepSpan);
        if (keepOn && nearOn) {
            // the drawn eye's depth and pass 2's depth test as SRVs, the blocks' too
            D3D12_SHADER_RESOURCE_VIEW_DESC dvw = sv;
            dvw.Format = drawn->depthView;
            dev_->CreateShaderResourceView(drawn->depth, &dvw, at(kDrawnTable + 1));
            dev_->CreateShaderResourceView(depthTarget_, &single, at(kDrawnTable + 3));
            for (UINT n = 0; n < 2; ++n) dev_->CreateShaderResourceView(nears_[n], &single, at(kDrawnTable + 4 + n));
            dev_->CreateShaderResourceView(nears_[2], &single, at(kBetween));
            list->SetGraphicsRootSignature(nearRoot_);
            const D3D12_VIEWPORT bvp{0.0f, 0.0f, float(blocksAcross), float(blocksDown), 0.0f, 1.0f};
            const D3D12_RECT brect{0, 0, LONG(blocksAcross), LONG(blocksDown)};
            list->RSSetViewports(1, &bvp);
            list->RSSetScissorRects(1, &brect);
            const int span = gpuspans::Begin(list, "made: near blocks");
            nearBlocks(kDrawnTable + 1, 0, drawn->scale[0], drawn->scale[1], kp.keepReach);   // as the drawn eye saw them
            nearBlocks(kDrawnTable + 3, 1, 1, 1, kp.keepReach);                               // as this eye did
            gpuspans::End(list, span);
        }
        {
            const int span = gpuspans::Begin(list, "made: drawn grid");
            grid(*drawn, kDrawnTable, 1, keepOn, keepOn && nearOn, keepOn && fxOn);
            gpuspans::End(list, span);
        }
        if (keepOn) {
            std::swap(b[2].Transition.StateBefore, b[2].Transition.StateAfter);
            list->ResourceBarrier(1, b + 2);
        }
        // The strips pass 3 did not reach ([stereo] turns_fill): what this eye's own picture left there, unless it is something nearer
        // since gone - then what is behind beside the strip.
        if (fill_ && ownOk && holePso_[k] && drawn->edgeScale > 0.0f) {
            const int span = gpuspans::Begin(list, "made: fill");
            D3D12_RESOURCE_BARRIER r[4] = {Tr(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
                                           Tr(ownTurned_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
                                           Tr(depthTarget_, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                                           Tr(drawnTarget_, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
            list->ResourceBarrier(2, r);
            D3D12_TEXTURE_COPY_LOCATION to{}, from{};   // the picture so far, to read beside the strips
            to.pResource = ownTurned_; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            from.pResource = target; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            const D3D12_BOX box{0, 0, 0, w, h, 1};
            list->CopyTextureRegion(&to, 0, 0, 0, &from, &box);
            for (int i = 0; i < 2; ++i) std::swap(r[i].Transition.StateBefore, r[i].Transition.StateAfter);
            list->ResourceBarrier(4, r);   // the target back, the copy readable, both grids' depth tests readable
            dev_->CreateShaderResourceView(ownTurned_, &sv, at(kHoleTable));
            dev_->CreateShaderResourceView(drawnTarget_, &single, at(kHoleTable + 1));
            dev_->CreateShaderResourceView(depthTarget_, &single, at(kHoleTable + 2));
            for (UINT n = 3; n < kGridTable; ++n) dev_->CreateShaderResourceView(nullptr, &single, at(kHoleTable + n));
            list->SetGraphicsRootSignature(depthRoot_);
            list->SetPipelineState(holePso_[k]);
            list->SetGraphicsRootDescriptorTable(0, gpuAt(kHoleTable));
            // the drawn eye's place in this eye's view (the move's translation): left of it, what is behind is uncovered right of near things
            const float drawnX = drawn->move[3];
            const float holes[kHoleConstants] = {(std::max)(16.0f, kHoleReachPx * float(w) / kKeepWidthPx), KeepScale(drawn->edgeScale, drawn->nearPlane),
                                                 kEdgePixels, drawn->nearPlane / kAhead, drawnX < 0.0f ? 1.0f : drawnX > 0.0f ? -1.0f : 0.0f, 0, 0, 0};
            list->SetGraphicsRoot32BitConstants(1, kHoleConstants, holes, 0);
            filled_ = true;
            list->OMSetRenderTargets(targets, &rtv, TRUE, nullptr);
            list->RSSetViewports(1, &vp);
            list->RSSetScissorRects(1, &rect);
            list->DrawInstanced(3, 1, 0, 0);
            for (auto& x : r) std::swap(x.Transition.StateBefore, x.Transition.StateAfter);
            list->ResourceBarrier(2, r + 2);   // both grids' depth tests back to be written
            gpuspans::End(list, span);
        }
        if (kept) *kept = keepOn;
    }
    return done();
}

void Gpu::ClearMadeDepth(ID3D12GraphicsCommandList* list) {
    if (!madeDepth_ || !list || !rtv_) return;
    D3D12_RENDER_TARGET_VIEW_DESC dr{};
    dr.Format = DXGI_FORMAT_R32_FLOAT;
    dr.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvDepth = rtv_->GetCPUDescriptorHandleForHeapStart();
    rtvDepth.ptr += SIZE_T(rtvInc_) * kRtvMade;
    dev_->CreateRenderTargetView(madeDepth_, &dr, rtvDepth);
    D3D12_RESOURCE_BARRIER b = Tr(madeDepth_, kReadable, D3D12_RESOURCE_STATE_RENDER_TARGET);
    list->ResourceBarrier(1, &b);
    const float farZ[4] = {0, 0, 0, 0};   // (not "far": a Windows macro)
    list->ClearRenderTargetView(rtvDepth, farZ, 0, nullptr);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    list->ResourceBarrier(1, &b);
}

}  // namespace turnwarp
