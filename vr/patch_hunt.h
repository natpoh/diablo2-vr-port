// patch_hunt.h - [debug] patch_hunt: ONE selector that switches the suspects of "floor patches riding with the hero" (docs/support_log.md on
// claude/player-support, 2026-11-07: one or two ~1.5 m patches of ground detail sliding over the ground with the hero, on the right of the view)
// off one at a time, so a tester in flat F3 sees at which step the patch goes. Pure: the steps, their words, the game settings they touch and
// the values they put there; vr/patch_hunt.cpp does it (the game's own setting objects, the overlay line, Ctrl+F8), vr/config/load_settings.cpp
// leaves out our own parts (depth_fix, light_bounds, our sky / fog / ceiling), vr/pipe_hunt.cpp skips one game pipeline.
//
// The game's graphics options (D2R 3.3.93787 under D2RLoader 1.3.1, read off the decrypted image dump_loader/D2R_image.bin of 2026-11-04): each is
// a Settings::X() accessor - sub rsp, 0x58 (0x78); mov rax, gs:[0x58]; a thread-safe static of the setting object, its name given at its first
// call (lea rdx, [rip + "Ambient Occlusion Quality"] before 0xDBB50), then every call its default (0xD2EF30, at +0x30) and its range (0xD2ED90),
// and lea rax, [rip + the object]; add rsp; ret. Its value is at +0xC (as "Offline Difficulty Scaling", vr/players_cmd.h); the setter all of them
// share is 0xD2F020(object, value) - the one the game's key commands and /players call: it clamps, stores, calls the object's listeners
// (vtable +0x10 - what applies it) and marks Settings.json to be written. The game keeps a value set this way at its exit like one set in its menu.
#pragma once

#include <cstdint>

namespace patchhunt {

// The steps (the [debug] patch_hunt value). One suspect off a step, everything else as the player has it; kAllAtOnce is 1 - 7 together.
enum Step : int {
    kNormal = 0,
    kGameAo = 1,         // the game's Ambient Occlusion Quality -> 0 (off)
    kGameShadows = 2,    // the game's Shadow Quality -> 1 (its lowest; the game has no 0)
    kGameLights = 3,     // the game's Light Quality -> 0 (its lowest)
    kGameUpscale = 4,    // the game's Anti Aliasing -> 0 (no TAA), NVIDIA DLSS -> 0 (off), Resolution Scale -> 100 (native)
    kDepthFix = 5,       // [render] depth_fix (depth_fix_mode) -> 0
    kLightBounds = 6,    // [render] light_bounds_fix / light_bounds_flat: not bound (the game's own light bins)
    kOwnEffects = 7,     // our sky, fog, cave ceiling, distance blur and the game's dome / ceiling we lay -> off
    kPipeline = 8,       // one game pipeline's draws skipped (vr/pipe_hunt.h): [debug] patch_hunt_pipe (the K-th of the last trace) or skip_pipeline
    kAllAtOnce = 9,      // 1 - 7 together: does anything of ours or the game's options carry it at all
    kCount = 10
};

constexpr int Clamp(int s) { return s < 0 || s >= kCount ? 0 : s; }
constexpr int Next(int s) { return (Clamp(s) + 1) % kCount; }
constexpr int Prev(int s) { return (Clamp(s) + kCount - 1) % kCount; }

// What a step leaves out (the overlay's and the log's words).
inline const char* What(int s) {
    switch (Clamp(s)) {
    case kGameAo: return "the game's Ambient Occlusion off";
    case kGameShadows: return "the game's Shadow Quality at its lowest";
    case kGameLights: return "the game's Light Quality at its lowest";
    case kGameUpscale: return "the game's TAA, DLSS off, Resolution Scale 100";
    case kDepthFix: return "[render] depth_fix off";
    case kLightBounds: return "[render] light_bounds off (the game's light bins)";
    case kOwnEffects: return "our sky / fog / ceiling / blur off";
    case kPipeline: return "one game pipeline skipped";
    case kAllAtOnce: return "steps 1 - 7 all at once";
    default: return "all normal";
    }
}

// The game's settings the steps touch.
enum Gfx : int { kAo = 0, kShadow, kLight, kAa, kDlss, kResScale, kGfxCount };

struct GfxDef {
    const char* name;     // the game's own name (its Settings.json key), checked in the accessor's first call
    uint64_t accessor;    // Settings::X() (RVA)
    int lo, hi;           // the range its accessor gives it
    int hunt;             // the value a step puts there
};
inline constexpr GfxDef kGfx[kGfxCount] = {
    {"Ambient Occlusion Quality", 0xDC350, 0, 4, 0},
    {"Shadow Quality", 0xDF5B0, 1, 4, 1},
    {"Light Quality", 0xDDF20, 0, 2, 0},
    {"Anti Aliasing", 0xDC420, 0, 3, 0},
    {"NVIDIA DLSS", 0xDED70, 0, 5, 0},
    {"Resolution Scale", 0xDF000, 50, 200, 100},
};
constexpr uint64_t RVA_SETTING_SET = 0xD2F020;   // the setter: push rbx; sub rsp, 0x20; mov rbx, rcx; call 0xD2EC90 (store) ...
inline constexpr uint8_t kSigSet[10] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8};
// An accessor's first bytes: sub rsp, imm8 (0x58 / 0x78) at 0-3, then mov rax, gs:[0x58].
inline constexpr uint8_t kSigAccessorHead[3] = {0x48, 0x83, 0xEC};
inline constexpr uint8_t kSigAccessorGs[9] = {0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00};
constexpr uint32_t kAccessorScan = 0x180;   // the bytes of an accessor looked through for its object and its name
constexpr uint32_t kValueAt = 0xC;

// Does step s touch game setting g, and which steps leave our own parts out.
constexpr bool Touches(int s, int g) {
    switch (Clamp(s)) {
    case kGameAo: return g == kAo;
    case kGameShadows: return g == kShadow;
    case kGameLights: return g == kLight;
    case kGameUpscale: return g == kAa || g == kDlss || g == kResScale;
    case kAllAtOnce: return true;
    default: return false;
    }
}
constexpr bool DepthFixOff(int s) { return Clamp(s) == kDepthFix || Clamp(s) == kAllAtOnce; }
constexpr bool LightBoundsOff(int s) { return Clamp(s) == kLightBounds || Clamp(s) == kAllAtOnce; }
constexpr bool OwnEffectsOff(int s) { return Clamp(s) == kOwnEffects || Clamp(s) == kAllAtOnce; }
constexpr bool PipelineSkip(int s) { return Clamp(s) == kPipeline; }

// A value read from the game, in its range or not known (-1: garbage, the setting not made yet).
constexpr int Sane(int g, int32_t raw) { return g >= 0 && g < kGfxCount && raw >= kGfx[g].lo && raw <= kGfx[g].hi ? raw : -1; }

// Where an instruction `len` long at `at` with rip displacement `disp` points.
constexpr uint64_t RipTarget(uint64_t at, int32_t disp, uint32_t len) { return at + len + (uint64_t)(int64_t)disp; }

}  // namespace patchhunt
