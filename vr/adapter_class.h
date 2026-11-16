// adapter_class.h - what kind of graphics card the game's own D3D12 device runs on, from what the device and DXGI say of it (no name lists).
// Pure: no Windows, no D3D; vr/gpu_fault.cpp reads the facts once a device is watched, tools/adapter_class_test.cpp tests the rules.
//
// Crash batch 2026-11-16 (reports 362-366, 372, one player, 0.217.1, the flat game): "Intel(R) UHD Graphics", 128 MB of its own memory - every
// game hung the GPU (DXGI_ERROR_DEVICE_HUNG) about 100 s in, a page fault in the game's "Early Post Processing", and every log showed our Medium
// preset raising the game's own Ambient Occlusion and Shadow Quality ([render] preset_game_options). A weak adapter is told by:
//   - Software: DXGI_ADAPTER_FLAG_SOFTWARE (WARP, the Microsoft Basic Render Driver) - no graphics card at all;
//   - Integrated: D3D12_FEATURE_DATA_ARCHITECTURE.UMA - the GPU shares the PC's memory (Intel UHD / Iris, AMD APUs: an AMD APU may report a
//     carve-out of 512 MB - 4 GB as "dedicated", UMA still says what it is);
//   - Small: a card of its own (not UMA) with less than 1 GiB of its own memory (DXGI_ADAPTER_DESC1.DedicatedVideoMemory) - the oldest discrete
//     cards (GT 710 1 GB and below) and a driver that reports a UMA part wrong.
// Anything else is Discrete. Not known (no device yet, the ask faulted) is Unknown - the caller decides what that means.
#pragma once

#include <cstdint>

namespace adapterclass {

enum class Class { Unknown, Discrete, Small, Integrated, Software };

struct Facts {
    bool known = false;              // read from a device (false: nothing below means anything)
    bool software = false;           // DXGI_ADAPTER_FLAG_SOFTWARE
    bool uma = false;                // D3D12_FEATURE_DATA_ARCHITECTURE.UMA (node 0)
    bool archKnown = false;          // the device answered the architecture ask
    uint64_t dedicatedBytes = 0;     // DXGI_ADAPTER_DESC1.DedicatedVideoMemory
    uint64_t sharedBytes = 0;        // DXGI_ADAPTER_DESC1.SharedSystemMemory (told in the log only)
};

constexpr uint64_t kSmallDedicated = 1ull << 30;   // less than 1 GiB of its own memory: Small

constexpr Class Classify(const Facts& f) {
    if (!f.known) return Class::Unknown;
    if (f.software) return Class::Software;
    if (f.archKnown && f.uma) return Class::Integrated;
    if (f.dedicatedBytes < kSmallDedicated) return Class::Small;
    return Class::Discrete;
}

// A weak adapter: the game's heavier Video options hang it - our preset must never raise them there.
constexpr bool Weak(Class c) { return c == Class::Integrated || c == Class::Small || c == Class::Software; }

inline const char* Name(Class c) {
    switch (c) {
    case Class::Discrete: return "a graphics card of its own";
    case Class::Small: return "a graphics card with under 1 GB of its own memory";
    case Class::Integrated: return "integrated graphics (shares the PC's memory)";
    case Class::Software: return "a software adapter (no graphics card)";
    default: return "not known yet";
    }
}

}  // namespace adapterclass
