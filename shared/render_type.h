// render_type.h - the render type, one choice over the ini keys that make it (Serg 2026-11-08: five entries, this order; one pass the default):
//   0 Native two passes      [openxr] on=1, [stereo] afr=1 pair_per_tick=1 native_replay=0 - the game's whole draw twice a pair
//   1 Native one pass        ... native_replay=1 - the right eye drawn again from the left pass's lists (vr/native_replay.h); paused
//                            frames (an area change, a load, a resize) are two passes, never a black eye
//   2 Native eyes by turns   [openxr] on=1, [stereo] afr=1 pair_per_tick=0 - one eye drawn a frame, the other made from the depth
//                            ([stereo] turn_warp; the "depth fill" - stereo source 3 in Serg's list)
//   3 FlatVR stereo          [openxr] on=0, [stereo] afr=1 source=2 (pair_per_tick / replay_right as FlatVR's one-pass stereo had them)
//   4 FlatVR depth           [openxr] on=0, [stereo] afr=0 source=1 - one picture, FlatVR makes the second eye from the depth
// Old inis keep their meaning: they are read from the keys (FromKeys) - an ini without native_replay reads as one pass (the default; it was
// one pass with replay_right=1 until 0.203 lost it), FlatVR's and by turns' keys read as they always did. Pure: D2R VR Settings' "3D in the
// headset" list and the headset's Video page row both go through it; tools/hset_test.cpp proves it.
#pragma once

namespace rendertype {

enum Type : int { kNativeTwo = 0, kNativeOne = 1, kNativeTurns = 2, kFlatStereo = 3, kFlatDepth = 4, kCount = 5 };

// The keys, as the ini has them (-1 = not in the file: its default)
struct Keys {
    int openxrOn = 0;      // [openxr] on
    int afr = 0;           // [stereo] afr
    int pairPerTick = 0;   // [stereo] pair_per_tick
    int nativeReplay = 1;  // [stereo] native_replay (absent: 1, the default)
    int source = -1;       // [stereo] source (absent: afr ? 2 : 1)
    int replayRight = 0;   // [stereo] replay_right (FlatVR stereo's one pass; not read on the native path)
    int replayAhead = 1;   // [stereo] replay_ahead
};

inline int FromKeys(const Keys& k) {
    if (k.openxrOn) {
        if (k.afr && !k.pairPerTick) return kNativeTurns;
        return k.nativeReplay == 0 ? kNativeTwo : kNativeOne;
    }
    if (k.afr) return kFlatStereo;
    const int source = k.source >= 0 ? k.source : 1;
    return source == 0 ? kFlatStereo : kFlatDepth;   // (mono, which no list offers any more, shows as FlatVR stereo, as before)
}

// The keys a type writes; the others stay as they were (k is read and written).
inline void ToKeys(int type, Keys* k) {
    switch (type) {
        case kNativeTwo:
        case kNativeOne:
            k->openxrOn = 1; k->afr = 1; k->pairPerTick = 1; k->source = 2;
            k->nativeReplay = type == kNativeOne ? 1 : 0;
            k->replayRight = 1; k->replayAhead = 2;   // (what Settings' "Native OpenXR" wrote: replay_right is the ReShade path's, harmless)
            break;
        case kNativeTurns:
            k->openxrOn = 1; k->afr = 1; k->pairPerTick = 0; k->source = 2; k->replayRight = 0;
            break;
        case kFlatStereo:
            k->openxrOn = 0; k->afr = 1; k->pairPerTick = 1; k->source = 2; k->replayRight = 1; k->replayAhead = 2;
            break;
        default:   // kFlatDepth
            k->openxrOn = 0; k->afr = 0; k->source = 1;
            break;
    }
}

inline bool Native(int type) { return type == kNativeTwo || type == kNativeOne || type == kNativeTurns; }

// A press of < (-1) or > (+1) on a stepper: the next type, stopping at the ends.
inline int Step(int type, int delta) {
    const int to = type + (delta > 0 ? 1 : -1);
    return to < 0 ? 0 : to >= kCount ? kCount - 1 : to;
}

// English names (callers translate by key: "vr.rendertype.<n>" in the headset, the list's own in Settings)
inline const char* Name(int type) {
    switch (type) {
        case kNativeTwo: return "Native two passes";
        case kNativeOne: return "Native one pass";
        case kNativeTurns: return "Native eyes by turns";
        case kFlatStereo: return "FlatVR stereo";
        default: return "FlatVR depth";
    }
}

}  // namespace rendertype
