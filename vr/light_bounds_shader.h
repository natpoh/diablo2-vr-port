#pragma once
// The light grid's depth bounds, as a replacement for the game's own compute shader of that job
// (vr/light_bounds.cpp). Written here from what the game's shader does, not copied from it.
//
// What the game does: its lighting cuts the picture into a grid of tiles (32 x 20) and, for each tile,
// finds the smallest and the largest depth in it - the lights are then binned against that range. One
// 32-lane group per tile; the lanes walk the tile's 2x2 depth blocks (one gather each) and meet in a
// group-shared min / max. Zero depth is the background and does not count towards the smallest.
//
// What it gets wrong: the lane's running index is split into a block column and a block row, and the
// native shader takes the row from the tile's block HEIGHT. That is right only while a tile is as tall as
// it is wide, which a 16:10 picture on a 32 x 20 grid gives. At a square window the tiles are 1.6 times
// as tall as wide: the lower rows of each tile are never read (and at a wide window the walk runs past the
// tile into the next one), so a tile's range is wrong, the lights binned against it vanish in hard
// rectangles, and the shading flickers as the head moves. Here the row comes from the block WIDTH.
//
// Everything else - the tile edges, the block walk, the gather's coordinates, what is written - is kept
// as the game has it, so at a 16:10 size the result is the same bit for bit. The binding layout is the
// game's (register, space and type of each), because the replacement runs in the game's own root signature:
//   b3   float4 [4]: [1].xy the lit area in pixels; [2].zw and [3].xy the offset and scale from the lit
//        area's uv to the depth texture's (a smaller area drawn into a bigger target)
//   b12  uint4 [2]:  [0].xy the grid (tiles across, down); [1].y the length of a row of bounds
//   t10  the depth, s1 its sampler, u0 the bounds: (smallest non-zero depth, largest), (0, 0) when none
// tests: cleanroom/tests/light_bounds_test.cpp

namespace lightbounds {

inline constexpr char kShader[] = R"hlsl(
cbuffer PassRect : register(b3)
{
    float4 rect[4];
};
cbuffer LightGrid : register(b12)
{
    uint4 grid[2];
};
Texture2D<float> sceneDepth : register(t10);
SamplerState depthSampler : register(s1);
RWStructuredBuffer<float2> tileBounds : register(u0);

// Depths are never negative, so the bits of a float order like the unsigned integer they are.
groupshared uint g_smallestBits;
groupshared uint g_largestBits;

[numthreads(32, 1, 1)]
void main(uint3 tile : SV_GroupID, uint3 thread : SV_GroupThreadID)
{
    const uint lane = thread.x;
    if (lane == 0)
    {
        g_smallestBits = asuint(1.0f);
        g_largestBits = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    const float2 area = rect[1].xy;
    const float2 tiles = float2(grid[0].xy);
    const uint2 first = (uint2)(float2(tile.xy) * area / tiles);         // the tile's first pixel
    const uint2 end = (uint2)(float2(tile.xy + 1) * area / tiles);       // and the first one past it
    const uint2 blocks = (end - first + 1) >> 1;                         // 2x2 blocks across and down
    const float2 origin = float2(first) / area;
    const uint count = blocks.x * blocks.y;

    float smallest = 1.0f;
    float largest = 0.0f;
    for (uint i = lane; i < count; i += 32)
    {
        const uint2 block = uint2(i % blocks.x, i / blocks.x);           // the row from the width
        const float2 uv = (origin + (float2(block << 1) + 0.5f) / area) * rect[3].xy + rect[2].zw;
        const float4 z = sceneDepth.GatherRed(depthSampler, uv);
        largest = max(largest, max(max(z.x, z.y), max(z.z, z.w)));
        const float4 lit = float4(z.x != 0.0f ? z.x : 3.402823466e+38f, z.y != 0.0f ? z.y : 3.402823466e+38f,
                                  z.z != 0.0f ? z.z : 3.402823466e+38f, z.w != 0.0f ? z.w : 3.402823466e+38f);
        smallest = min(smallest, min(min(lit.x, lit.y), min(lit.z, lit.w)));
    }
    InterlockedMin(g_smallestBits, asuint(smallest));
    InterlockedMax(g_largestBits, asuint(largest));
    GroupMemoryBarrierWithGroupSync();

    if (lane == 0)
    {
        const float2 range = float2(asfloat(g_smallestBits), asfloat(g_largestBits));
        const bool none = range.x == 1.0f && range.y == 0.0f;
        tileBounds[tile.y * grid[1].y + tile.x] = none ? float2(0.0f, 0.0f) : range;
    }
}
)hlsl";

}  // namespace lightbounds
