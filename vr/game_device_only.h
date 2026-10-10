#pragma once
// ReShade's events from the game's own D3D12 device only.
//
// ReShade wraps every device made in the process, not only the game's: native OpenXR's
// runtime (Virtual Desktop's VDXR) makes a D3D11 device of its own beside the game's
// D3D12 one, and its immediate context's Flush comes as an execute_command_list event.
// Our handlers take what they get for D3D12 objects (the replay called a D3D12 queue's
// GetDesc on that D3D11 context: the game crashed in d3d11.dll, 2026-10-10), so each is
// registered through D3D12Only<&Handler>::Call, which drops the others' events.

#include <type_traits>
#include <reshade.hpp>

namespace d2rvr {

inline bool OnGameDevice(reshade::api::device* d) { return d != nullptr && d->get_api() == reshade::api::device_api::d3d12; }
inline bool OnGameDevice(reshade::api::device_object* o) { return o != nullptr && OnGameDevice(o->get_device()); }

template <auto F> struct D3D12Only;
template <typename R, typename First, typename... A, R (*F)(First, A...)>
struct D3D12Only<F> {
    static R Call(First first, A... rest) {
        if (!OnGameDevice(first)) {
            if constexpr (std::is_void_v<R>) return;
            else return R{};   // (a bool event: false, not skipped)
        }
        return F(first, rest...);
    }
};

}  // namespace d2rvr
