// native_replay.h - [stereo] native_replay: one CPU pass, two GPU frames without ReShade (docs/notes/native_replay.md).
//
// The right-eye replay (vr/render/stereo/replay.cpp: the left pass's command lists submitted again with the right eye's camera slots patched by
// camfix) was built on ReShade's add-on events, and when 0.203 dropped ReShade for everyone it was never registered again: "Both eyes" ran
// the game's whole draw pass twice on the CPU (Serg's PC, 0.207: 61-65 pairs/s, ~13 ms CPU a pair; 0.166-0.170 with the replay: ~106).
// This is the replay on the plugin's own D3D12 hooks: what ReShade's events gave it comes from
//   - the game's ExecuteCommandLists on each queue type (vr/ecl_hook.h; ReShade's execute_command_list) - the left pass's lists, and the game's
//     own calls for [debug] replay_marks 3 / 4;
//   - the device's resource creation (vr/native_d3d12.h Sinks::uploadMade / uploadGone; ReShade's init_resource / destroy_resource) - the
//     game's upload buffers, where camfix reads the camera's constants;
//   - camfix's own root signature / root CBV hooks on the direct and compute lists (as before; ReShade's reset_command_list only told it when);
//   - the game's present function (as before) and the plugin's own present hook for the right eye's effects (vrcam noreshade::Present).
// Everything else - the fences, the replayed allocators' wait, the per-list submission, the area / long-frame / resize pause, the DLSS twin,
// reflections_per_eye, the labels' half-disparity - is the replay's own code and comes along unchanged.
//
// The key decides, not [stereo] replay_right (the ReShade path's). 1 (the default 2026-11-08 - 0.213) = the replay; 0 (the default since
// 0.214, Serg 2026-11-12: one pass hung graphics cards; an ini without the key reads so, as shared/render_type.h's "Native two passes") = two passes. The hooks above go
// in only when the key is on at the game's start (the upload buffers are made then); switched on later, it waits for the next start. F4 only,
// native OpenXR only (FlatVR takes its pictures at every present). A paused pair (area change, long frame, resize) is two passes, never black.
// Pure: vrcam drives it, tools/native_replay_test.cpp proves it on WARP.
#pragma once

namespace nativereplay {

// The hooks go in at the plugin's load: the key on then, and no ReShade (always since 0.203).
inline bool InstallAtStart(int key, bool noReshade) { return key == 1 && noReshade; }

enum class Path { kTwoPasses, kNative, kReShade };
enum class Why { kOn, kKeyOff, kNotAtStart, kRootHooks, kNotXr, kNotF4, kReShade };

struct In {
    bool reshade = false;      // the old path: ReShade joined (replay::Register ran) - its own key, replay_right
    int key = 0;               // [stereo] native_replay now (live)
    bool hooksIn = false;      // the hooks went in at the start (InstallAtStart)
    bool rootHooksOk = false;  // camfix's root signature / root CBV hooks are in (another hook on the same functions makes them fail)
    bool xr = false;           // native OpenXR runs (xr::On)
    bool f4 = false;           // VR first person with the body (viewgate::F4)
};

// Which path this pair takes. *why: the reason, for the log (told once at the start and whenever it changes).
inline Path Decide(const In& in, Why* why) {
    Why w = Why::kOn;
    Path p = Path::kNative;
    if (in.reshade) { w = Why::kReShade; p = Path::kReShade; }
    else if (in.key != 1) { w = Why::kKeyOff; p = Path::kTwoPasses; }
    else if (!in.hooksIn) { w = Why::kNotAtStart; p = Path::kTwoPasses; }
    else if (!in.rootHooksOk) { w = Why::kRootHooks; p = Path::kTwoPasses; }
    else if (!in.xr) { w = Why::kNotXr; p = Path::kTwoPasses; }
    else if (!in.f4) { w = Why::kNotF4; p = Path::kTwoPasses; }
    if (why) *why = w;
    return p;
}
inline const char* WhyText(Why w) {
    switch (w) {
        case Why::kOn: return "one pass, two pictures without ReShade ([stereo] native_replay=1): the right eye is the left pass's lists again with its own camera";
        case Why::kKeyOff: return "two passes a pair ([stereo] native_replay=0, as since 0.203)";
        case Why::kNotAtStart: return "two passes a pair - [stereo] native_replay was switched on after the game's start: its hooks go in at the next start";
        case Why::kRootHooks: return "two passes a pair - the replay is SKIPPED: the camera's root hooks did not go in (another hook holds them)";
        case Why::kNotXr: return "two passes a pair - native OpenXR is not running (the replay is made for it; FlatVR takes each present)";
        case Why::kNotF4: return "two passes a pair - not VR first person (F4)";
        case Why::kReShade: return "the replay through ReShade's events ([stereo] replay_right)";
    }
    return "?";
}

// The game's ExecuteCommandLists, seen by the hook (vr/ecl_hook.h): is a list of it the left pass's, to be submitted again? Queue types as
// D3D12's (0 direct, 2 compute, 3 copy). As the ReShade path's OnExecute: only while the left pass is captured, never our own submissions;
// the copy queue's only from the draw thread with [stereo] replay_copy (the streaming's uploads come from other threads and stay out).
inline bool NoteList(bool capturing, bool ours, int queueType, bool drawThread, bool replayCopy) {
    if (!capturing || ours) return false;
    if (queueType == 0 || queueType == 2) return true;
    return replayCopy && drawThread;
}

}  // namespace nativereplay
