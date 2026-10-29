// native_depth.h - [render] no_reshade: the scene's depth-stencil copied for the eye made (vrcam scenedepth::BindNative), in plain D3D12 so
// tools/native_d3d12_test runs the same code on WARP and reads the copy back. Our copy has the game's buffer's size and typeless format and stays
// readable by shaders (xr.cpp's kReadable) between copies; the game's buffer goes back to the state its last barrier left it in.
#pragma once

#include <d3d12.h>

namespace nativedepth {

constexpr D3D12_RESOURCE_STATES kReadable = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

// A depth format (or its typeless / readable relatives) -> the typeless one a copy can have; UNKNOWN: not a depth format known here.
inline DXGI_FORMAT Typeless(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: return DXGI_FORMAT_R32G8X24_TYPELESS;
        case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: return DXGI_FORMAT_R24G8_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
        case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_R16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}
// ... and how a shader reads its depth (as vrcam's SrvFormat for ReShade's copy)
inline DXGI_FORMAT ReadAs(DXGI_FORMAT typeless) {
    switch (typeless) {
        case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

inline D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

// Our copy for a game's depth-stencil described by `game` (its size, the typeless of its format), in kReadable. nullptr: not a known format, or
// it could not be made.
inline ID3D12Resource* MakeCopy(ID3D12Device* dev, const D3D12_RESOURCE_DESC& game) {
    const DXGI_FORMAT tl = Typeless(game.Format);
    if (tl == DXGI_FORMAT_UNKNOWN) return nullptr;
    D3D12_RESOURCE_DESC cd{};
    cd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    cd.Width = game.Width;
    cd.Height = game.Height;
    cd.DepthOrArraySize = cd.MipLevels = 1;
    cd.Format = tl;
    cd.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    ID3D12Resource* r = nullptr;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &cd, kReadable, nullptr, __uuidof(ID3D12Resource), (void**)&r))) return nullptr;
    r->SetName(L"d2rvr no-reshade scene depth");
    return r;
}

// The copy recorded: the game's buffer from its state (`from`; 0, COMMON, as the game's depth-write) to a copy source and back, ours from
// readable to a copy destination and back.
inline void RecordCopy(ID3D12GraphicsCommandList* l, ID3D12Resource* game, D3D12_RESOURCE_STATES from, ID3D12Resource* ours) {
    if (from == D3D12_RESOURCE_STATE_COMMON) from = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    const bool move = from != D3D12_RESOURCE_STATE_COPY_SOURCE;
    D3D12_RESOURCE_BARRIER b[2] = {Transition(ours, kReadable, D3D12_RESOURCE_STATE_COPY_DEST), Transition(game, from, D3D12_RESOURCE_STATE_COPY_SOURCE)};
    l->ResourceBarrier(move ? 2 : 1, b);
    l->CopyResource(ours, game);
    D3D12_RESOURCE_BARRIER e[2] = {Transition(ours, D3D12_RESOURCE_STATE_COPY_DEST, kReadable), Transition(game, D3D12_RESOURCE_STATE_COPY_SOURCE, from)};
    l->ResourceBarrier(move ? 2 : 1, e);
}

}  // namespace nativedepth
