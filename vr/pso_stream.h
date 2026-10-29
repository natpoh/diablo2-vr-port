// pso_stream.h - reading a D3D12 pipeline state stream (Device2::CreatePipelineState): each subobject an alignas(void*) { type; value }.
// Used where the plugin's own D3D12 hooks ([render] no_reshade) keep a pipeline the game made from a stream and make a variant of it:
// the game's fog pass (vr/fog_height.cpp) and its light-bin depth bounds (vr/light_bounds.cpp).
#pragma once

#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace psostream {

// A subobject's value's offset and the subobject's whole size
template <class T> constexpr void Of(size_t* at, size_t* size) {
    const size_t a = alignof(T) > 4 ? alignof(T) : 4;
    *at = (4 + a - 1) / a * a;
    *size = (*at + sizeof(T) + sizeof(void*) - 1) / sizeof(void*) * sizeof(void*);
}
// False: a kind this does not know (the stream is then not read past it)
inline bool Subobject(uint32_t type, size_t* at, size_t* size) {
    switch (type) {
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE: Of<ID3D12RootSignature*>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS: case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS: case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS: case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS: case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS: case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: Of<D3D12_SHADER_BYTECODE>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT: Of<D3D12_STREAM_OUTPUT_DESC>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND: Of<D3D12_BLEND_DESC>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK: Of<UINT>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: Of<D3D12_RASTERIZER_DESC>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: Of<D3D12_DEPTH_STENCIL_DESC>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT: Of<D3D12_INPUT_LAYOUT_DESC>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE: Of<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY: Of<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: Of<D3D12_RT_FORMAT_ARRAY>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT: Of<DXGI_FORMAT>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC: Of<DXGI_SAMPLE_DESC>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK: Of<UINT>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO: Of<D3D12_CACHED_PIPELINE_STATE>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS: Of<D3D12_PIPELINE_STATE_FLAGS>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: Of<D3D12_DEPTH_STENCIL_DESC1>(at, size); return true;
    case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING: Of<D3D12_VIEW_INSTANCING_DESC>(at, size); return true;
    default: return false;
    }
}
inline bool IsShader(uint32_t t) {
    return t == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS || t == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS || t == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS ||
           t == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS || t == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS || t == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS ||
           t == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS || t == D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
}

// Where the stream's subobject of `type` keeps its value (its offset in the stream): false when it has none, or holds a kind this does not
// read (`unread`, when given, then has that kind).
inline bool Find(const uint8_t* s, size_t n, uint32_t type, size_t* valueAt, uint32_t* unread = nullptr) {
    for (size_t at = 0; at + 4 <= n;) {
        uint32_t t = 0;
        std::memcpy(&t, s + at, 4);
        size_t vo = 0, size = 0;
        if (!Subobject(t, &vo, &size) || at + size > n) {
            if (unread) *unread = t;
            return false;
        }
        if (t == type) { *valueAt = at + vo; return true; }
        at += size;
    }
    return false;
}

}  // namespace psostream
