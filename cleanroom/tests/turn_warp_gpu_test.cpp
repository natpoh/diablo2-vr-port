// Eyes by turns, the eye not drawn made on the GPU (vr/turn_warp.cpp): the passes themselves, run on D3D12's software device (WARP) -
// no headset, no game:
//
//   turn_warp_gpu_test        every case; exit code 0 = all passed
//
// warm      Warm builds the passes on a worker beside an Init on this thread; a later Init builds from the compiled code
// move      a wall, the sky, a post over a wall moved with the depth; what the hero carries stays
// edges     the drawn eye's picture to the pixel along a post's edges, what it hid keeps this eye's own; a blade two texels wide
//           inside one cell moves with its own depth (pcvr v0.3.24 c72b90d: the old shader cut the cell whole)
// keep      where this eye's own picture shows the same surface it stays, the drawn eye's where they part; off: the drawn eye's
// near      [stereo] turns_keep_own=1: kept only within reach of a near thing, as either eye saw it
// effects   [stereo] turns_keep_own=3: kept only where either eye's effects layer has something (Effects), none below the level or unread
// made      the made depth (turn_overlay_hide) beside the picture, to the pixel: the depth of what each pixel shows (it hides the eye made's medallions)
// fill      [stereo] turns_fill: a frame-old post in the strip the drawn eye cannot see gives way to what is behind beside it, its made depth too;
//           what this eye rightly sees there stays (at its outer edge, a barrel behind the post); a moved monster: fewer ghost pixels ticked
// rims      [stereo] turns_rims: the dark rim round a post against the void goes with it; a beam's edge a hair apart leaves no hole (the texel sort)
// sharp     [stereo] turns_sharp: Catmull-Rom keeps more contrast at half a pixel, whole pixels exact, no ring at a step
//
//   TW_BENCH=1344 turn_warp_gpu_test   no checks: the eye made on the graphics card at 1344 x 1344 (a post before a wall, both grids, its own
//                                      picture kept round near things), each switch off in turn, every pass timed (vr/gpu_spans.h) - what each
//                                      costs a frame, for the weak PC's choice ([stereo] turns_sharp / turns_fill / turns_rims / turns_keep_own ...)
//
// The cases follow bearstonem/d2r-pcvr's steamvr/turn_warp_test.cpp (MIT, v0.3.24; fill, rims, sharp v0.3.25-v0.3.28), for our Gpu.

#include "turn_warp.h"
#include "gpu_spans.h"

#include <dxgi1_4.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace turnwarp;

namespace {

int g_failed = 0, g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::printf("FAIL: %s\n", what); }
}
void Must(HRESULT hr, const char* what) {
    if (FAILED(hr)) { std::printf("%s: %08lx\n", what, (unsigned long)hr); throw std::runtime_error(what); }
}
template <class T> void Free(T*& p) { if (p) { p->Release(); p = nullptr; } }

// A device, a queue and one list to record on, run and wait for.
struct Device {
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 value = 0;
    std::vector<ID3D12Resource*> made;
    explicit Device(bool card = false) {   // card: the graphics card (the bench), else WARP
        IDXGIFactory4* factory = nullptr;
        IDXGIAdapter* warp = nullptr;
        if (!card && SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory))) factory->EnumWarpAdapter(__uuidof(IDXGIAdapter), (void**)&warp);
        Must(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev), "device");
        Free(warp);
        Free(factory);
        D3D12_COMMAND_QUEUE_DESC q{};
        Must(dev->CreateCommandQueue(&q, __uuidof(ID3D12CommandQueue), (void**)&queue), "queue");
        Must(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&alloc), "allocator");
        Must(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&list), "list");
        Must(list->Close(), "close");
        Must(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence), "fence");
    }
    ~Device() {
        for (ID3D12Resource* r : made) r->Release();
        Free(fence); Free(list); Free(alloc); Free(queue); Free(dev);
    }
    void Record(const std::function<void()>& work) {
        Must(alloc->Reset(), "reset allocator");
        Must(list->Reset(alloc, nullptr), "reset list");
        work();
        Must(list->Close(), "close");
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        Must(queue->Signal(fence, ++value), "signal");
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Must(fence->SetEventOnCompletion(value, e), "completion");
        const DWORD waited = WaitForSingleObject(e, 30000);
        CloseHandle(e);
        if (waited != WAIT_OBJECT_0) throw std::runtime_error("GPU timeout");
    }
    ID3D12Resource* Texture(UINT w, UINT h, DXGI_FORMAT f, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
        D3D12_RESOURCE_DESC d{};
        d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        d.Width = w; d.Height = h; d.DepthOrArraySize = d.MipLevels = 1; d.Format = f; d.SampleDesc.Count = 1; d.Flags = flags;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        ID3D12Resource* r = nullptr;
        Must(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, __uuidof(ID3D12Resource), (void**)&r), "texture");
        made.push_back(r);
        return r;
    }
    ID3D12Resource* Buffer(UINT64 bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
        D3D12_RESOURCE_DESC b{};
        b.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        b.Width = bytes; b.Height = b.DepthOrArraySize = b.MipLevels = 1; b.SampleDesc.Count = 1; b.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = type;
        ID3D12Resource* r = nullptr;
        Must(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &b, state, nullptr, __uuidof(ID3D12Resource), (void**)&r), "buffer");
        made.push_back(r);
        return r;
    }
    // texels, row by row, into a texture (made in COPY_DEST) left in `after`
    template <class Texel> void Upload(ID3D12Resource* t, const std::vector<Texel>& texels, D3D12_RESOURCE_STATES after) {
        const D3D12_RESOURCE_DESC d = t->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT f{};
        UINT64 bytes = 0;
        dev->GetCopyableFootprints(&d, 0, 1, 0, &f, nullptr, nullptr, &bytes);
        ID3D12Resource* up = Buffer(bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* data = nullptr;
        const D3D12_RANGE none{0, 0};
        Must(up->Map(0, &none, &data), "map upload");
        for (UINT y = 0; y < d.Height; ++y) memcpy((char*)data + y * f.Footprint.RowPitch, &texels[y * d.Width], d.Width * sizeof(Texel));
        up->Unmap(0, nullptr);
        Record([&] {
            D3D12_TEXTURE_COPY_LOCATION from{}, to{};
            from.pResource = up; from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint = f;
            to.pResource = t; to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition = {t, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, after};
            list->ResourceBarrier(1, &b);
        });
    }
    // What `draw` recorded into `target` (in `state` before and after, 32-bit texels), read back.
    std::vector<uint32_t> Drawn(ID3D12Resource* target, const std::function<void()>& draw,
                                D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET) {
        const D3D12_RESOURCE_DESC d = target->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT f{};
        UINT64 bytes = 0;
        dev->GetCopyableFootprints(&d, 0, 1, 0, &f, nullptr, nullptr, &bytes);
        ID3D12Resource* back = Buffer(bytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        Record([&] {
            draw();
            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition = {target, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, state, D3D12_RESOURCE_STATE_COPY_SOURCE};
            list->ResourceBarrier(1, &b);
            D3D12_TEXTURE_COPY_LOCATION from{}, to{};
            from.pResource = target; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource = back; to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = f;
            list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
            std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
            list->ResourceBarrier(1, &b);
        });
        std::vector<uint32_t> out(d.Width * d.Height);
        void* data = nullptr;
        const D3D12_RANGE range{0, SIZE_T(bytes)}, none{0, 0};
        Must(back->Map(0, &range, &data), "map readback");
        for (UINT y = 0; y < d.Height; ++y) memcpy(&out[y * d.Width], (char*)data + y * f.Footprint.RowPitch, d.Width * 4);
        back->Unmap(0, &none);
        return out;
    }
};

// sRGB decoded, filtered at the texel's centre and encoded again: a step at most
bool Same(uint32_t got, uint32_t want) {
    for (int lane = 0; lane < 4; ++lane)
        if (std::abs(int((got >> lane * 8) & 255) - int((want >> lane * 8) & 255)) > 1) return false;
    return true;
}
constexpr std::array<float, 9> kIdentity{1, 0, 0, 0, 1, 0, 0, 0, 1};
const Tan kSquare{-1, 1, -1, 1};   // 90 degrees each way
const std::array<float, 12> kStill = Move({0, 0, 0}, {0, 0, 0, 1}, {0, 0, 0}, {0, 0, 0, 1});
constexpr DXGI_FORMAT kEye = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
constexpr D3D12_RESOURCE_STATES kReadable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

// A grid of a picture with a depth by column (distance; 0: the sky), seen from the origin looking down -z with a square fov.
struct Scene {
    Device& d;
    UINT side;
    float nearPlane = 1;
    Scene(Device& dev, UINT s) : d(dev), side(s) {}
    ID3D12Resource* Picture(const std::function<uint32_t(UINT, UINT)>& texel) {
        std::vector<uint32_t> t(side * side);
        for (UINT y = 0; y < side; ++y) for (UINT x = 0; x < side; ++x) t[y * side + x] = texel(x, y);
        ID3D12Resource* r = d.Texture(side, side, kEye, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        d.Upload(r, t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        return r;
    }
    ID3D12Resource* Depth(const std::function<float(UINT)>& distance) {   // depth = near / distance, by column
        return DepthXY([&](UINT x, UINT) { return distance(x); });
    }
    ID3D12Resource* DepthXY(const std::function<float(UINT, UINT)>& distance) {   // ... by pixel
        std::vector<uint32_t> t(side * side);
        for (UINT y = 0; y < side; ++y)
            for (UINT x = 0; x < side; ++x) {
                const float a = distance(x, y), v = a > 0 ? nearPlane / a : 0.0f;
                memcpy(&t[y * side + x], &v, 4);
            }
        ID3D12Resource* r = d.Texture(side, side, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        d.Upload(r, t, kReadable);
        return r;
    }
    Grid Of(ID3D12Resource* image, ID3D12Resource* depth, Vec drawnAt) {
        Grid g;
        g.image = image; g.depth = depth; g.depthView = DXGI_FORMAT_R32_FLOAT; g.nearPlane = nearPlane;
        g.drawnTan = g.nowTan = kSquare;
        g.move = Move(drawnAt, {0, 0, 0, 1}, {0, 0, 0}, {0, 0, 0, 1});
        g.carry = g.move;
        return g;
    }
    // the drawn eye: `apart` away, its cells across an edge in two layers
    Grid Drawn(ID3D12Resource* image, ID3D12Resource* depth, float apart) {
        Grid g = Of(image, depth, {-apart, 0, 0});
        g.edgeScale = EdgeScale(side, kSquare, apart, nearPlane);
        return g;
    }
};

uint32_t Mine(UINT x, UINT y) { return (x & 255) | (y & 255) << 8 | 0x80u << 16 | 0xffu << 24; }    // this eye's own picture
uint32_t Theirs(UINT x, UINT y) { return (x & 255) | (y & 255) << 8 | 0x30u << 16 | 0xffu << 24; }  // the drawn eye's

// out at columns [a, b): this eye's own (unmoved) or the drawn eye's from `shift` columns right
void Expect(const std::vector<uint32_t>& out, UINT side, UINT a, UINT b, int shift, bool drawnEye, const char* what, UINT rowStep = 1) {
    bool same = true;
    for (UINT y = 0; y < side; y += rowStep)
        for (UINT x = a; x < b; ++x) {
            const uint32_t want = drawnEye ? Theirs(x + shift, y) : Mine(x, y);
            if (!Same(out[y * side + x], want)) {
                if (same) std::printf("  %s: pixel %u,%u = %08x, expected %08x\n", what, x, y, out[y * side + x], want);
                same = false;
            }
        }
    Check(same, what);
}

void WarmCases(Device& d) {
    bool warmed = false;
    std::thread worker([&] { Gpu w; warmed = w.Init(d.dev, kEye, nullptr); w.Release(); });
    Gpu first;
    const bool built = first.Init(d.dev, kEye, nullptr);
    worker.join();
    Check(warmed && built && first.DepthReady() && first.NearReady() && first.EffectsReady() && first.RimReady() && first.HoleReady(),
          "warm: every pass built beside a running compile");
    first.Release();
    // Warm: false while the worker builds, true once it is done (bounded wait)
    bool done = Warm(d.dev, kEye);
    for (int i = 0; i < 500 && !done; ++i) { Sleep(10); done = Warm(d.dev, kEye); }
    Check(done, "warm: Warm says when its worker is done");
    Gpu next;
    Check(next.Init(d.dev, kEye, nullptr) && next.DepthReady() && next.NearReady() && next.EffectsReady() && next.RimReady() && next.HoleReady(),
          "warm: a later Init builds every pass from the compiled code");
    next.Release();
}

// The depth passes: a 64-pixel picture, a 90-degree view, the eye moved half a unit right. A point d away goes 0.5 / d of the
// half-width (32 px) to the left: 4 px at 4 units, 2 at 8, 8 at 2.
void MoveCases(Device& d) {
    constexpr UINT side = 64;
    Scene s(d, side);
    Gpu gpu;
    Check(gpu.Init(d.dev, kEye, nullptr) && gpu.DepthReady(), "move: pipelines");
    ID3D12Resource* own = s.Picture(Mine);
    ID3D12Resource* target = d.Texture(side, side, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    int slot = 0;
    auto warp = [&](const Grid* o, const Grid* dr, bool* kept = nullptr) {
        return d.Drawn(target, [&] { Check(gpu.Render(d.list, target, side, side, own, kIdentity, o, dr, slot = (slot + 1) % kSlots, false, 0, kept), "render"); });
    };
    auto mineFrom = [&](const std::vector<uint32_t>& out, UINT a, UINT b, int shift, const char* what) {   // this eye's own, moved
        bool same = true;
        for (UINT y = 0; y < side; ++y)
            for (UINT x = a; x < b; ++x)
                if (!Same(out[y * side + x], Mine(x + shift, y))) {
                    if (same) std::printf("  %s: pixel %u,%u = %08x, expected %08x\n", what, x, y, out[y * side + x], Mine(x + shift, y));
                    same = false;
                }
        Check(same, what);
    };
    {   // this eye's own picture moved: a wall 4 units away 4 px to the left; the edge it leaves keeps the turn alone
        Grid g = s.Of(own, s.Depth([](UINT) { return 4.0f; }), {-0.5f, 0, 0});
        const auto out = warp(&g, nullptr);
        mineFrom(out, 0, 56, 4, "move: a wall 4 units away moves 4 px");
        mineFrom(out, 61, 64, 0, "move: the edge the wall leaves keeps the turn alone");
    }
    {
        Grid g = s.Of(own, s.Depth([](UINT) { return 0.0f; }), {-0.5f, 0, 0});
        mineFrom(warp(&g, nullptr), 0, 64, 0, "move: the sky stays where it is");
    }
    {   // a post 2 units away (columns 24-39) before a wall at 8: the post goes 8 px, over the wall's 2
        Grid g = s.Of(own, s.Depth([](UINT x) { return x >= 24 && x < 40 ? 2.0f : 8.0f; }), {-0.5f, 0, 0});
        const auto out = warp(&g, nullptr);
        mineFrom(out, 18, 30, 8, "move: the post moves 8 px and covers the wall behind it");
        mineFrom(out, 2, 14, 2, "move: the wall left of it moves 2 px");
        // carried: a hand 2 units away goes along and stays; the wall at 8 still moves 2 px
        g.carry = kStill;
        g.nearW = 3.0f; g.farW = 3.5f;
        const auto c = warp(&g, nullptr);
        mineFrom(c, 26, 38, 0, "move: what the hero carries stays with the eye");
        mineFrom(c, 2, 18, 2, "move: the wall left of it still moves 2 px");
    }
    // The drawn eye's picture over this eye's own (turned only): a post 2 units away (columns 24-39) before a wall at 4, seen from half a
    // unit to the left. The post goes 8 px, the wall 4; the 4 px the post uncovers on its right, hidden from the drawn eye, keep this eye's own.
    ID3D12Resource* theirs = s.Picture(Theirs);
    {
        Grid dr = s.Drawn(theirs, s.Depth([](UINT x) { return x >= 24 && x < 40 ? 2.0f : 4.0f; }), 0.5f);
        auto out = warp(nullptr, &dr);
        Expect(out, side, 2, 15, 4, true, "edges: the wall from the drawn eye, moved 4 px");
        Expect(out, side, 16, 32, 8, true, "edges: the post from the drawn eye, moved 8 px to both its edges");
        Expect(out, side, 32, 36, 0, false, "edges: what the post hid from the drawn eye keeps this eye's own");
        Expect(out, side, 36, 56, 4, true, "edges: the wall past it from the drawn eye again, from its edge on");
        dr.edgeScale = 0;   // no edge test: the post's edge drawn out over the gap instead
        out = warp(nullptr, &dr);
        Check(!Same(out[10 * side + 35], Mine(35, 10)), "edges: (without the layers the gap is the drawn eye's, drawn out)");
    }
    {   // a blade two texels wide (columns 33-34) inside one cell, 2 units away before the wall at 4: it moves 8 px, not with the wall
        Grid dr = s.Drawn(theirs, s.Depth([](UINT x) { return x == 33 || x == 34 ? 2.0f : 4.0f; }), 0.5f);
        const auto out = warp(nullptr, &dr);
        Expect(out, side, 2, 24, 4, true, "edges: the wall left of the blade moved 4 px");
        Expect(out, side, 25, 27, 8, true, "edges: the blade inside one cell moved 8 px");
        Expect(out, side, 29, 31, 0, false, "edges: what the blade hid from the drawn eye keeps this eye's own");
        Expect(out, side, 31, 56, 4, true, "edges: the wall right of the blade moved 4 px");
    }
    {   // This eye's own picture, moved with its own depth (the head still: in place), showed the wall alone; the drawn eye's shows the post
        // moved in front of it since. Where both show the wall this eye's own stays; the post is the drawn eye's; what it hid is this eye's own.
        Grid dr = s.Drawn(theirs, s.Depth([](UINT x) { return x >= 24 && x < 40 ? 2.0f : 4.0f; }), 0.5f);
        dr.keep = KeepOf(kKeepAll, 4096, kKeepGapPx, kKeepReachPx, kEffectsLevel);
        Grid still = s.Of(own, s.Depth([](UINT) { return 4.0f; }), {0, 0, 0});
        bool kept = false;
        auto out = warp(&still, &dr, &kept);
        Check(kept, "keep: Render says it kept");
        Expect(out, side, 2, 15, 0, false, "keep: where both show the wall this eye's own stays");
        Expect(out, side, 16, 32, 8, true, "keep: the post moved in, from the drawn eye");
        Expect(out, side, 32, 56, 0, false, "keep: what it hid and the wall past it: this eye's own");
        dr.keep = Keep{};
        out = warp(&still, &dr, &kept);
        Check(!kept, "keep: off - Render says it did not");
        Expect(out, side, 2, 15, 4, true, "keep: off - the drawn eye's wall everywhere it reaches");
    }
    {   // the made depth (turn_overlay_hide) written beside the picture through the layers: Render still makes the picture
        Grid dr = s.Drawn(theirs, s.Depth([](UINT x) { return x >= 24 && x < 40 ? 2.0f : 4.0f; }), 0.5f);
        Grid still = s.Of(own, s.Depth([](UINT) { return 4.0f; }), {0, 0, 0});   // this eye's own last picture: the wall, the head still
        const auto out = d.Drawn(target, [&] { Check(gpu.Render(d.list, target, side, side, own, kIdentity, &still, &dr, 3, true, 0), "render, made depth"); });
        Check(gpu.MadeDepth() != nullptr, "made: the made depth is there");
        Expect(out, side, 16, 32, 8, true, "made: the post from the drawn eye with the made depth too");
        Expect(out, side, 32, 36, 0, false, "made: what it hid keeps this eye's own with the made depth too");
        // The made depth hides the eye made's overlays (D2RVR_DEPTH_MADE, [stereo] turn_overlay_hide): it must be the depth of what the made
        // picture shows in each pixel - a medallion on the wrist is hidden exactly where the picture shows the hand in front of it. Read
        // back: near / distance, the post 2 units away (0.5) where the picture shows the post, the wall 4 away (0.25) beside it and in the
        // gap it uncovered (this eye's own wall). Cut whole at 4-pixel cells, the post's edge cells had the wall's depth over the post's
        // colour (or the reverse): the medallion over the hand's edge drawn on one side, hidden on the other - doubled (2026-10-20).
        const auto depthBits = d.Drawn(gpu.MadeDepth(), [] {}, kReadable);
        auto depthAt = [&](UINT x, UINT y) { float v; memcpy(&v, &depthBits[y * side + x], 4); return v; };
        auto depthOver = [&](UINT a, UINT b, float want, const char* what) {
            bool same = true;
            for (UINT y = 0; y < side; ++y)
                for (UINT x = a; x < b; ++x)
                    if (std::fabs(depthAt(x, y) - want) > 1e-3f) {
                        if (same) std::printf("  %s: depth at %u,%u = %.4f, expected %.4f\n", what, x, y, depthAt(x, y), want);
                        same = false;
                    }
            Check(same, what);
        };
        depthOver(2, 15, 0.25f, "made depth: the wall's where the picture shows the wall");
        depthOver(16, 32, 0.5f, "made depth: the post's to both its edges, as its colour");
        depthOver(32, 36, 0.25f, "made depth: the wall's in the gap the post uncovered (this eye's own picture there)");
        depthOver(36, 56, 0.25f, "made depth: the wall's past it");
    }
    gpu.Release();
}

// Near things only: a 256-pixel wall 4 units away, a post 2 units away in columns 96-127, the drawn eye 0.125 units to the left (the post
// goes 8 px, the wall 4). This eye's own picture is kept where it shows the same wall within reach (32 px, two 16-px blocks) of the post
// - as the drawn eye saw it, then as this eye did - and the drawn eye's elsewhere.
void NearCases(Device& d) {
    constexpr UINT side = 256;
    Scene s(d, side);
    Gpu gpu;
    Check(gpu.Init(d.dev, kEye, nullptr) && gpu.NearReady(), "near: pipelines");
    ID3D12Resource* own = s.Picture(Mine);
    ID3D12Resource* theirs = s.Picture(Theirs);
    ID3D12Resource* target = d.Texture(side, side, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource* withPost = s.Depth([](UINT x) { return x >= 96 && x < 128 ? 2.0f : 4.0f; });
    ID3D12Resource* wallOnly = s.Depth([](UINT) { return 4.0f; });
    int slot = 0;
    auto warp = [&](const Grid* o, const Grid* dr) {
        return d.Drawn(target, [&] { Check(gpu.Render(d.list, target, side, side, own, kIdentity, o, dr, slot = (slot + 1) % kSlots, false, 0), "render"); });
    };
    Grid still = s.Of(own, wallOnly, {0, 0, 0});
    Grid dr = s.Drawn(theirs, withPost, 0.125f);
    dr.keep = Keep{kEdgePixels, 2.0f, 32.0f, 0.0f};   // the post parts from the wall by 4 px
    // the post moved in: the drawn eye shows it, this eye's own the wall alone; round where the drawn eye saw it the wall stays this eye's
    auto out = warp(&still, &dr);
    Expect(out, side, 2, 56, 4, true, "near: far from the post the drawn eye's wall", 7);
    Expect(out, side, 62, 86, 0, false, "near: within reach of the post, as the drawn eye saw it, this eye's own wall", 7);
    Expect(out, side, 90, 118, 8, true, "near: the post from the drawn eye", 7);
    Expect(out, side, 126, 154, 0, false, "near: within reach right of it, this eye's own wall", 7);
    Expect(out, side, 160, 250, 4, true, "near: past the reach the drawn eye's wall again", 7);
    // the post moved away: this eye's own showed it, the drawn eye the wall alone; round where this eye saw it the wall stays its own
    dr.depth = wallOnly;
    still.depth = withPost;
    out = warp(&still, &dr);
    Expect(out, side, 2, 44, 4, true, "near: far from it the drawn eye's wall", 7);
    Expect(out, side, 50, 92, 0, false, "near: within reach of the post, as this eye saw it, its own wall", 7);
    Expect(out, side, 98, 126, 4, true, "near: where the post was, the drawn eye's wall", 7);
    Expect(out, side, 134, 174, 0, false, "near: within reach right of it, its own wall", 7);
    Expect(out, side, 180, 250, 4, true, "near: past the reach the drawn eye's wall again", 7);
    gpu.Release();
}

uint16_t Half(float f) {   // IEEE half: 0 and normal values only
    if (f == 0) return 0;
    uint32_t b;
    memcpy(&b, &f, 4);
    return uint16_t(((b >> 16) & 0x8000u) | ((((b >> 23) & 0xffu) - 112u) << 10) | ((b >> 13) & 0x3ffu));
}

// The game's effects: a 256-pixel wall 4 units away in both eyes, the drawn eye 0.125 units to the left (the wall goes 4 px). The drawn
// eye's effects layer has something in columns 96-127 (its first target), this eye's in 160-191 (its second, left by the game to be
// copied from). This eye's own picture is kept where the pixel comes from the drawn eye's effect, and where this eye's own is.
void EffectsCases(Device& d) {
    constexpr UINT side = 256;
    Scene s(d, side);
    Gpu gpu;
    Check(gpu.Init(d.dev, kEye, nullptr) && gpu.EffectsReady(), "effects: pipelines");
    ID3D12Resource* own = s.Picture(Mine);
    ID3D12Resource* theirs = s.Picture(Theirs);
    ID3D12Resource* target = d.Texture(side, side, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12Resource* wall = s.Depth([](UINT) { return 4.0f; });
    using Texel = std::array<uint16_t, 4>;
    auto layer = [&](UINT from, UINT to, D3D12_RESOURCE_STATES after) {   // an effect over these columns, nothing elsewhere
        std::vector<Texel> t(side * side, Texel{0, 0, 0, 0});
        for (UINT y = 0; y < side; ++y) for (UINT x = from; x < to; ++x) t[y * side + x] = Texel{Half(1), Half(0.5f), Half(0.25f), Half(1)};
        ID3D12Resource* r = d.Texture(side, side, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
        d.Upload(r, t, after);
        return r;
    };
    const D3D12_RESOURCE_STATES ps = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, copied = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12Resource* drawnFx = layer(96, 128, ps);
    ID3D12Resource* drawnNone = layer(0, 0, ps);
    ID3D12Resource* ownNone = layer(0, 0, copied);
    ID3D12Resource* ownFx = layer(160, 192, copied);
    auto effects = [&](ID3D12Resource* a, ID3D12Resource* b, D3D12_RESOURCE_STATES state, int eye, int slot) {
        ID3D12Resource* const pair[2] = {a, b};
        const D3D12_RESOURCE_STATES states[2] = {state, state};
        bool read = false;
        d.Record([&] { read = gpu.Effects(d.list, pair, states, eye, 0.0f, slot); });
        Check(read, "effects: the layer read into its eye's blocks");
    };
    effects(drawnFx, drawnNone, ps, 1, 0);     // the drawn eye: 1
    effects(ownNone, ownFx, copied, 0, 1);     // this eye: 0, its layer left as the game left it
    Grid still = s.Of(own, wall, {0, 0, 0});
    Grid dr = s.Drawn(theirs, wall, 0.125f);
    dr.keep = Keep{kEdgePixels, 0.0f, 0.0f, 0.5f};
    auto warp = [&](int slot) {
        return d.Drawn(target, [&] { Check(gpu.Render(d.list, target, side, side, own, kIdentity, &still, &dr, slot, false, 0), "render"); });
    };
    auto out = warp(2);
    Expect(out, side, 2, 88, 4, true, "effects: away from them the drawn eye's picture", 7);
    Expect(out, side, 94, 122, 0, false, "effects: where the drawn eye's effect would land, this eye's own", 7);
    Expect(out, side, 128, 156, 4, true, "effects: between them the drawn eye's again", 7);
    Expect(out, side, 162, 190, 0, false, "effects: where this eye's own effect is, its own", 7);
    Expect(out, side, 196, 250, 4, true, "effects: past them the drawn eye's again", 7);
    dr.keep.keepEffects = 1.5f;   // above the effects' level: none count
    out = warp(3);
    Expect(out, side, 2, 250, 4, true, "effects: below the level they keep nothing", 7);
    dr.keep.keepEffects = 0.5f;
    gpu.ForgetEffects(0);
    gpu.ForgetEffects(1);
    out = warp(4);
    Expect(out, side, 2, 250, 4, true, "effects: none read - effects only keeps nothing, not everything", 7);
    gpu.Release();
}

// The made depth read back, near / distance, over columns [a, b).
void DepthOver(Device& d, Gpu& gpu, UINT side, UINT a, UINT b, float want, const char* what) {
    const auto bits = d.Drawn(gpu.MadeDepth(), [] {}, kReadable);
    bool same = true;
    for (UINT y = 0; y < side; ++y)
        for (UINT x = a; x < b; ++x) {
            float v;
            memcpy(&v, &bits[y * side + x], 4);
            if (std::fabs(v - want) > 1e-3f) {
                if (same) std::printf("  %s: depth at %u,%u = %.4f, expected %.4f\n", what, x, y, v, want);
                same = false;
            }
        }
    Check(same, what);
}

// Fill ([stereo] turns_fill): the drawn eye's post 2 units away (columns 24-39) before a wall at 4, from half a unit to the left (the
// post goes 8 px, the wall 4); the strip it cannot see is columns 32-35. This eye's own picture, the head still, showed the wall there:
// it stays. Showing the post where it stood a frame ago (columns 28-43), nearer than the wall behind the strip, it was a ghost: what is
// behind beside the strip goes there instead - the drawn eye's wall past 36 (its 40), mirrored. pcvr 8ae54f2's case; the edge, ours.
void FillCases(Device& d) {
    constexpr UINT side = 64;
    Scene s(d, side);
    Gpu gpu;
    Check(gpu.Init(d.dev, kEye, nullptr) && gpu.HoleReady(), "fill: pipelines");
    ID3D12Resource* own = s.Picture(Mine);
    ID3D12Resource* theirs = s.Picture(Theirs);
    ID3D12Resource* target = d.Texture(side, side, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    int slot = 0;
    auto warp = [&](const Grid* o, const Grid* dr, bool made) {
        return d.Drawn(target, [&] { Check(gpu.Render(d.list, target, side, side, own, kIdentity, o, dr, slot = (slot + 1) % kSlots, made, 0), "render"); });
    };
    Grid dr = s.Drawn(theirs, s.Depth([](UINT x) { return x >= 24 && x < 40 ? 2.0f : 4.0f; }), 0.5f);
    Grid still = s.Of(own, s.Depth([](UINT) { return 4.0f; }), {0, 0, 0});
    auto out = warp(&still, &dr, false);
    Expect(out, side, 32, 36, 0, false, "fill: its own wall in the strip the drawn eye cannot see stays");
    Grid stale = s.Of(own, s.Depth([](UINT x) { return x >= 28 && x < 44 ? 2.0f : 4.0f; }), {0, 0, 0});
    out = warp(&stale, &dr, false);
    bool filled = true;   // (the wall mirrored past the strip's end at 36: this eye's 72 - x, the drawn eye's 76 - x - not Expect's shift)
    for (UINT y = 0; y < side; ++y)
        for (UINT x = 32; x < 36; ++x)
            if (!Same(out[y * side + x], Theirs(76 - x, y))) {
                if (filled) std::printf("  fill: a frame-old post in the strip - pixel %u,%u = %08x, expected %08x\n", x, y, out[y * side + x], Theirs(76 - x, y));
                filled = false;
            }
    Check(filled, "fill: a frame-old post in the strip - what is behind beside it instead");
    Expect(out, side, 16, 32, 8, true, "fill: the post itself from the drawn eye still");
    Expect(out, side, 36, 56, 4, true, "fill: the wall past it from the drawn eye");
    // the made depth (turn_overlay_hide) of what fills it: the wall's, not the frame-old post's (ours)
    warp(&stale, &dr, true);
    DepthOver(d, gpu, side, 32, 36, 0.25f, "fill: the made depth in the strip is the wall's beside it");
    gpu.Fill(false);
    out = warp(&stale, &dr, false);
    Expect(out, side, 32, 36, 0, false, "fill off: the frame-old post stays, as before");
    warp(&stale, &dr, true);
    DepthOver(d, gpu, side, 32, 36, 0.5f, "fill off: the made depth there the post's, as before");
    gpu.Fill(true);
    {   // The eye's outer edge: the drawn eye's picture ends 4 px short of it (the wall at 4 goes 4 px), and this eye's own shows something
        // nearer there (a pillar coming into view at 2). That is no strip beside a near thing - nothing hid it - and stays this eye's own.
        // (0.182 + 98f8b8d6 smeared the column beside it over it: one end of a strip was enough.)
        Grid wall = s.Drawn(theirs, s.Depth([](UINT) { return 4.0f; }), 0.5f);
        Grid edge = s.Of(own, s.Depth([](UINT x) { return x >= 60 ? 2.0f : 4.0f; }), {0, 0, 0});
        out = warp(&edge, &wall, false);
        Expect(out, side, 2, 59, 4, true, "fill: the drawn eye's wall up to its edge");
        Expect(out, side, 60, 64, 0, false, "fill: at the eye's outer edge, past the drawn eye's picture, this eye's own nearer thing stays");
    }
    gpu.Release();
}

// Fill on a wider picture (128 px: a point d away goes 32 / d px from half a unit to the left). The drawn eye: a post at 2 (columns 48-79,
// it goes 16 px) before a wall at 8 (4 px): the strip it cannot see is columns 64-75.
//  - Something between them that only this eye sees, behind the post from the drawn eye (a barrel at 4, columns 64-75 of this eye's own
//    picture, the head still): it is no frame-old copy of the post - far nearer the wall than the post - and stays. (98f8b8d6 took
//    anything nearer than the wall beside the strip for a ghost and painted the wall's column over it.)
//  - A monster that has moved (the post, a frame ago at columns 60-91 of this eye's own picture): its frame-old copy in the strip is a
//    ghost; ticked there are fewer ghost pixels than unticked - none.
void FillWideCases(Device& d) {
    constexpr UINT side = 128;
    Scene s(d, side);
    Gpu gpu;
    Check(gpu.Init(d.dev, kEye, nullptr) && gpu.HoleReady(), "fill wide: pipelines");
    constexpr uint32_t kPost = 0xff0000ffu;   // the post's colour (pure red), in both pictures
    auto mine = [&](UINT lo, UINT hi) {
        return s.Picture([=](UINT x, UINT y) { return x >= lo && x < hi ? kPost : Mine(x, y); });
    };
    ID3D12Resource* theirs = s.Picture([](UINT x, UINT y) { return x >= 48 && x < 80 ? kPost : Theirs(x, y); });
    ID3D12Resource* target = d.Texture(side, side, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    int slot = 0;
    auto warp = [&](ID3D12Resource* base, const Grid* o, const Grid* dr) {
        return d.Drawn(target, [&] { Check(gpu.Render(d.list, target, side, side, base, kIdentity, o, dr, slot = (slot + 1) % kSlots, false, 0), "render"); });
    };
    Grid dr = s.Drawn(theirs, s.Depth([](UINT x) { return x >= 48 && x < 80 ? 2.0f : 8.0f; }), 0.5f);
    {
        ID3D12Resource* own = mine(0, 0);
        Grid barrel = s.Of(own, s.Depth([](UINT x) { return x >= 64 && x < 76 ? 4.0f : 8.0f; }), {0, 0, 0});
        const auto out = warp(own, &barrel, &dr);
        Expect(out, side, 65, 75, 0, false, "fill wide: what only this eye sees behind the post, nearer the wall than the post, stays its own");
    }
    {
        ID3D12Resource* own = mine(60, 92);
        Grid moved = s.Of(own, s.Depth([](UINT x) { return x >= 60 && x < 92 ? 2.0f : 8.0f; }), {0, 0, 0});
        auto ghosts = [&](const std::vector<uint32_t>& out) {   // the post's colour outside where the post is now (columns 32-63)
            int n = 0;
            for (UINT y = 0; y < side; ++y)
                for (UINT x = 0; x < side; ++x)
                    if ((x < 31 || x > 64) && Same(out[y * side + x], kPost)) ++n;
            return n;
        };
        gpu.Fill(false);
        const int off = ghosts(warp(own, &moved, &dr));
        gpu.Fill(true);
        const int on = ghosts(warp(own, &moved, &dr));
        std::printf("  fill wide: a monster that has moved - %d ghost pixels unticked, %d ticked\n", off, on);
        Check(off > 0 && on < off, "fill wide: ticked, fewer ghost pixels of a monster that has moved than unticked");
        Check(on == 0, "fill wide: ticked, none");
    }
    gpu.Release();
}

// Rims (kRimPixels, [stereo] turns_rims): a post 2 units away in columns 24-39 against the void (depth 0), the drawn eye half a unit
// to the left (the post goes 8 px, the void none). The two columns either side of the post, 22-23 and 40-41, are its rim in the picture
// but have the void's depth. They go with the post; left with the void, the right one stood 8 px from it. pcvr 2de7cbd's case; and
// 5fa6874's: a beam along the bottom, the drawn eye a hair lower - sorted by the four texels round each point, the row above the beam's
// rim fell out of both layers, the eye's own older picture through a hole along the ridge.
void RimCases(Device& d) {
    constexpr UINT side = 64;
    Scene s(d, side);
    Gpu gpu;
    Check(gpu.Init(d.dev, kEye, nullptr) && gpu.DepthReady() && gpu.RimReady(), "rims: pipelines");
    ID3D12Resource* own = s.Picture(Mine);
    ID3D12Resource* theirs = s.Picture(Theirs);
    ID3D12Resource* target = d.Texture(side, side, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    int slot = 0;
    auto warp = [&](const Grid* dr) {
        return d.Drawn(target, [&] { Check(gpu.Render(d.list, target, side, side, own, kIdentity, nullptr, dr, slot = (slot + 1) % kSlots, false, 0), "render"); });
    };
    Grid dr = s.Drawn(theirs, s.Depth([](UINT x) { return x >= 24 && x < 40 ? 2.0f : 0.0f; }), 0.5f);
    auto out = warp(&dr);
    Expect(out, side, 2, 13, 0, true, "rims: the void left of the post stays");
    Expect(out, side, 16, 32, 8, true, "rims: the post moves 8 px");
    Expect(out, side, 14, 16, 8, true, "rims: its left rim with it");
    Expect(out, side, 32, 34, 8, true, "rims: its right rim with it, beside it still");
    Expect(out, side, 34, 42, 0, false, "rims: what the post and its rim hid from the drawn eye - this eye's own");
    Expect(out, side, 42, 60, 0, true, "rims: the void right of it stays");
    gpu.Rims(false);
    out = warp(&dr);
    Expect(out, side, 16, 32, 8, true, "rims off: the post moves 8 px");
    Expect(out, side, 32, 40, 0, false, "rims off: this eye's own beside it");
    Expect(out, side, 40, 42, 0, true, "rims off: the right rim left 8 px from the post, as before");
    gpu.Rims(true);
    {
        Grid low = s.Drawn(theirs, s.DepthXY([](UINT, UINT y) { return y >= 40 ? 2.0f : 0.0f; }), 0.5f);
        low.move = Move({-0.5f, -0.002f, 0}, {0, 0, 0, 1}, {0, 0, 0}, {0, 0, 0, 1});
        low.carry = low.move;
        out = warp(&low);
        bool covered = true;
        for (UINT y = 30; y < 50; ++y)
            for (UINT x = 10; x < 50; ++x)
                if (Same(out[y * side + x], Mine(x, y))) {
                    if (covered) std::printf("  rims: a hole along the beam - pixel %u,%u is this eye's own (%08x)\n", x, y, out[y * side + x]);
                    covered = false;
                }
        Check(covered, "rims: no hole along a level edge against the void a hair apart (each pixel sorted by its own texel)");
    }
    gpu.Release();
}

// Sharpen ([stereo] turns_sharp): a 64-pixel picture of vertical stripes, 4 px a period, turned half a pixel. Bilinear averages each two
// neighbours; Catmull-Rom keeps more of the stripes' contrast. A whole-pixel turn reads every texel as it is either way, and a hard step
// from 64 to 192 neither undershoots nor overshoots (kept within the nearest texels). pcvr bd88507's case.
void SharpCases(Device& d) {
    constexpr UINT side = 64;
    Scene s(d, side);
    Gpu gpu;
    Check(gpu.Init(d.dev, kEye, nullptr), "sharp: pipelines");
    auto grey = [](uint32_t v) { return v | v << 8 | v << 16 | 0xffu << 24; };
    static constexpr uint32_t kStripe[4] = {40, 140, 240, 140};
    ID3D12Resource* stripes = s.Picture([&](UINT x, UINT) { return grey(kStripe[x % 4]); });
    ID3D12Resource* step = s.Picture([&](UINT x, UINT) { return grey(x < 32 ? 64 : 192); });
    ID3D12Resource* target = d.Texture(side, side, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    int slot = 0;
    auto turn = [&](ID3D12Resource* base, const std::array<float, 9>& h) {
        return d.Drawn(target, [&] { Check(gpu.Render(d.list, target, side, side, base, h, nullptr, nullptr, slot = (slot + 1) % kSlots, false, 0), "render"); });
    };
    const std::array<float, 9> half{1, 0, 0.5f / side, 0, 1, 0, 0, 0, 1}, two{1, 0, 2.0f / side, 0, 1, 0, 0, 0, 1};
    auto contrast = [&](const std::vector<uint32_t>& out) {   // the red lane's range over the middle of row 32
        int lo = 255, hi = 0;
        for (UINT x = 16; x < 48; ++x) { const int r = out[32 * side + x] & 255; lo = (std::min)(lo, r); hi = (std::max)(hi, r); }
        return hi - lo;
    };
    gpu.Sharpen(false);
    const int soft = contrast(turn(stripes, half));
    gpu.Sharpen(true);
    const int sharp = contrast(turn(stripes, half));
    std::printf("  sharp: half a pixel - the stripes' contrast %d bilinear, %d Catmull-Rom\n", soft, sharp);
    Check(sharp >= soft + soft / 8, "sharp: Catmull-Rom keeps more of the stripes than bilinear");
    auto out = turn(stripes, two);
    bool same = true;
    for (UINT y = 0; y < side; ++y)
        for (UINT x = 0; x < side - 2; ++x)
            if (!Same(out[y * side + x], grey(kStripe[(x + 2) % 4]))) {
                if (same) std::printf("  sharp: whole pixels - pixel %u,%u = %08x, expected %08x\n", x, y, out[y * side + x], grey(kStripe[(x + 2) % 4]));
                same = false;
            }
    Check(same, "sharp: a whole-pixel turn reads every texel as it is");
    out = turn(step, half);
    bool within = true;
    for (UINT y = 0; y < side; ++y)
        for (UINT x = 0; x < side; ++x) {
            const int r = out[y * side + x] & 255;
            if (r < 63 || r > 193) {
                if (within) std::printf("  sharp: step - pixel %u,%u red %d\n", x, y, r);
                within = false;
            }
        }
    Check(within, "sharp: a hard step neither undershoots nor overshoots");
    {   // pass 3, where nearly all of the eye made comes from: the drawn eye's stripes on a wall 32 units away go half a pixel (16 / 32)
        Grid dr = s.Drawn(stripes, s.Depth([](UINT) { return 32.0f; }), 0.5f);
        auto moved = [&] {
            return d.Drawn(target, [&] { Check(gpu.Render(d.list, target, side, side, stripes, kIdentity, nullptr, &dr, slot = (slot + 1) % kSlots, false, 0), "render"); });
        };
        gpu.Sharpen(false);
        const int gridSoft = contrast(moved());
        gpu.Sharpen(true);
        const int gridSharp = contrast(moved());
        std::printf("  sharp: the drawn eye moved half a pixel - the stripes' contrast %d bilinear, %d Catmull-Rom\n", gridSoft, gridSharp);
        Check(gridSharp >= gridSoft + gridSoft / 8, "sharp: the drawn eye's grid (pass 3) reads Catmull-Rom too");
    }
    gpu.Release();
}

// The bench (TW_BENCH=side): the eye made as the game makes it - this eye's own picture moved with its depth, the drawn eye's over it, its
// own kept round near things - 120 frames a set of switches, each pass timed.
void Bench(UINT side) {
    Device d(true);
    Scene s(d, side);
    Gpu gpu;
    if (!gpu.Init(d.dev, kEye, nullptr)) { std::printf("bench: no pipelines\n"); return; }
    ID3D12Resource* own = s.Picture(Mine);
    ID3D12Resource* theirs = s.Picture(Theirs);
    ID3D12Resource* target = d.Texture(side, side, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    const UINT post0 = side * 3 / 8, post1 = side * 5 / 8;
    ID3D12Resource* depthOwn = s.Depth([&](UINT x) { return x >= post0 && x < post1 ? 2.0f : 8.0f; });
    ID3D12Resource* depthDrawn = s.Depth([&](UINT x) { return x >= post0 + side / 32 && x < post1 + side / 32 ? 2.0f : 8.0f; });
    gpuspans::Setup(d.dev, d.queue);
    struct Set { const char* name; bool sharp, fill, rims, keep, own, drawn; };
    const Set sets[] = {
        {"all on (the default)", true, true, true, true, true, true},
        {"turns_sharp=0", false, true, true, true, true, true},
        {"turns_fill=0", true, false, true, true, true, true},
        {"turns_rims=0", true, true, false, true, true, true},
        {"turns_keep_own=0", true, true, true, false, true, true},
        {"sharp, fill, rims, keep own off", false, false, false, false, true, true},
        {"turn_from_drawn=0 (its own picture alone)", false, false, false, false, true, false},
        {"turn_warp=0 (turned only)", false, false, false, false, false, false},
        {"all on again (the order's own share)", true, true, true, true, true, true},
    };
    int slot = 0;
    for (int round = 0; round < 3; ++round)   // (rounds 0 and 1 only wake the card up to its clocks; round 2 is told)
    for (const Set& set : sets) {
        gpu.Sharpen(set.sharp);
        gpu.Fill(set.fill);
        gpu.Rims(set.rims);
        Grid o = s.Of(own, depthOwn, {-0.03f, 0, 0});
        Grid dr = s.Drawn(theirs, depthDrawn, 0.06f);
        dr.keep = set.keep ? KeepOf(kKeepDefault, side, kKeepGapPx, kKeepReachPx, kEffectsLevel) : Keep{};
        gpuspans::Take(nullptr);
        for (int f = 0; f < 120 + (int)gpuspans::kSlots; ++f) {
            d.Record([&] {
                gpuspans::FrameStart();
                const int span = gpuspans::Begin(d.list, "total");
                gpu.Render(d.list, target, side, side, own, kIdentity, set.own ? &o : nullptr, set.drawn ? &dr : nullptr, slot = (slot + 1) % kSlots, true, 0);
                gpuspans::End(d.list, span);
                gpuspans::Resolve(d.list);
            });
        }
        unsigned frames = 0;
        const std::string spans = gpuspans::Take(&frames);
        if (round == 2) std::printf("  %-44s %s\n", set.name, spans.c_str());
    }
    gpu.Release();
}

}  // namespace

int main() {
    if (const char* b = std::getenv("TW_BENCH")) {
        try {
            std::printf("the eye made at %dx%d on the graphics card (ms a frame, %u frames each):\n", std::atoi(b), std::atoi(b), 120u);
            Bench((UINT)std::atoi(b));
        } catch (const std::exception& e) {
            std::printf("bench: %s\n", e.what());
            return 2;
        }
        return 0;
    }
    try {
        Device d;
        WarmCases(d);
        MoveCases(d);
        NearCases(d);
        EffectsCases(d);
        FillCases(d);
        FillWideCases(d);
        RimCases(d);
        SharpCases(d);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 2;
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
