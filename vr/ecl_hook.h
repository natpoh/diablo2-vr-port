// ecl_hook.h - a look at every ExecuteCommandLists of a D3D12 queue: how many lists the game put in one call (vr/replay_groups.h). The observer is told before
// the call goes on to the real function, on the calling thread; it must be quick and must not call the queue's ExecuteCommandLists itself.
#pragma once

struct ID3D12CommandQueue;
struct ID3D12CommandList;

namespace eclhook {

using Observer = void (*)(ID3D12CommandQueue* q, unsigned n, ID3D12CommandList* const* lists);

// Hooks the ExecuteCommandLists of q's vtable (once for each of the three queue types, once for each function: the types may share one). The observer is
// global (the last one set). True when the hook is in. MinHook must be initialised.
bool Install(ID3D12CommandQueue* q, Observer obs);
// A second observer beside that one ([fog] game_fog_before_fx's submission order, vr/fog_before_fx.h OnExecute), told after it; installs the
// hook on q's type when it is not in yet (Install's observer left as it is).
bool InstallTap(ID3D12CommandQueue* q, Observer tap);
// [debug] gpu_timeline (vr/gpu_timeline.h): a function that makes the call itself, after the observers - it calls `orig` (the queue's real
// ExecuteCommandLists, never the hooked one) for the game's lists and may put lists of its own before and after them. nullptr: the call
// as it was. InstallAround also hooks q's type when it is not in yet.
using ExecFn = void(__stdcall*)(ID3D12CommandQueue*, unsigned, ID3D12CommandList* const*);
using Around = void (*)(ID3D12CommandQueue* q, unsigned n, ID3D12CommandList* const* lists, ExecFn orig);
bool InstallAround(ID3D12CommandQueue* q, Around around);
void SetAround(Around around);

}  // namespace eclhook
