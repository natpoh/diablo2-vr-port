// frame_gen.h - [openxr] frame_generation (experimental): OFXR Bridge by tig3rmast3r (LGPL-3.0-or-later, https://github.com/tig3rmast3r/OFXR-Bridge), an OpenXR
// API layer that makes every other headset frame by optical flow from two the game drew. Native OpenXR, both eyes from one game frame only (by turns it steps
// aside: by turns and frame generation together leave each eye a quarter of its frames real). The idea and the way it is loaded are bearstonem/d2r-pcvr's
// (v0.3.25, MIT: steamvr/frame_generation.h); this is ours, in our words.
//
// Where it lives: D2R VR Settings downloads the bridge's pinned release (its SHA-256 checked) and puts its layer DLL, its licences, its ofxr_bridge.ini and an
// explicit layer manifest into <the plugin's folder>\..\d2r_vr_libs\ofxr (in the D2RLoader mod: mods\D2R-VR\d2rloader\d2r_vr_libs\ofxr - not among the
// plugins, where D2RLoader takes any DLL for a plugin). No tray program, no registry entry, nothing for other games: with the key on, the plugin names that
// folder to the OpenXR loader for its own process (XR_API_LAYER_PATH) and asks for the layer in xrCreateInstance - on or off takes a game restart. The
// backend and NVIDIA's options reach the loaded layer live through its control API (its provider_api.hpp, V1, mirrored below).
//
// Plain data and arithmetic, shared by the plugin (vr/frame_gen.cpp), D2R VR Settings and the tests (cleanroom/tests/frame_gen_test.cpp).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace framegen {

// ---- the pinned release ----
constexpr wchar_t kHost[] = L"github.com";
constexpr wchar_t kPath[] = L"/tig3rmast3r/OFXR-Bridge/releases/download/0.2.1/OFXR-Bridge-v0.2.1-V116.zip";
constexpr char kSha256[] = "db224491cbc9553a51fe55d08623d25864c4ec0a3d49bd76e9d6c4d9b2410c67";   // (the hash bearstonem's Setup pins for the same file)
constexpr wchar_t kVersion[] = L"0.2.1 (V116)";
constexpr int kBuild = 116;
constexpr char kLayerName[] = "XR_APILAYER_XRFrameBridge_diagnostic";
constexpr wchar_t kFolder[] = L"ofxr";                                          // in d2r_vr_libs
constexpr wchar_t kLibraryFile[] = L"XR_APILAYER_XRFrameBridge_diagnostic.dll";
constexpr wchar_t kManifestFile[] = L"XR_APILAYER_XRFrameBridge_diagnostic.json";
constexpr wchar_t kIniFile[] = L"ofxr_bridge.ini";

// The explicit layer manifest written beside the layer (the library by its relative path: the folder can move with the game).
inline std::string Manifest() {
    char b[600];
    std::snprintf(b, sizeof b,
                  "{\n  \"file_format_version\": \"1.0.0\",\n  \"api_layer\": {\n    \"name\": \"%s\",\n    \"library_path\": \"./%s.dll\",\n"
                  "    \"api_version\": \"1.0\",\n    \"implementation_version\": \"%d\",\n"
                  "    \"description\": \"OFXR Bridge V%d (tig3rmast3r, LGPL-3.0-or-later), loaded by D2R VR for its own process only\"\n  }\n}\n",
                  kLayerName, kLayerName, kBuild, kBuild);
    return b;
}
// Its settings file as the plugin starts it (the bridge reads ofxr_bridge.ini beside its DLL; the backend is set live afterwards): its own defaults, the log off.
inline std::string DefaultIni() {
    return "[ofxr]\r\nenabled=1\r\nframe_generation=ofxr\r\nmotion_vectors=off\r\nbackend=fidelityfx\r\nnvidia_preset=medium\r\nnvidia_input_scale=50\r\n"
           "nvidia_bidirectional=0\r\n\r\n[diagnostics]\r\nlogging_enabled=0\r\nmax_file_mb=32\r\nflush_each_event=0\r\n\r\n[overlay]\r\nposition=off\r\n";
}

// ---- the settings ----
enum class Mode { Off = 0, FidelityFx = 1, Nvidia = 2 };   // [openxr] frame_generation 0 / 1 / 2
constexpr const char* kModeNames[] = {"Off", "FidelityFX", "NVIDIA"};
constexpr const char* kPresetNames[] = {"Slow", "Medium", "Fast"};               // [openxr] frame_generation_preset 0..2 (NVIDIA)
constexpr const char* kScaleNames[] = {"100% scale", "75% scale", "50% scale"};  // [openxr] frame_generation_scale 0..2 (NVIDIA's optical flow)
struct Options {
    Mode mode = Mode::Off;
    int preset = 1, scale = 2;   // NVIDIA Medium at 50%: the bridge's own suggested start
    bool On() const { return mode != Mode::Off; }
    bool operator==(const Options& o) const { return mode == o.mode && preset == o.preset && scale == o.scale; }
};
inline int Pick(int v, int count, int fallback) { return v >= 0 && v < count ? v : fallback; }
inline Options Read(int mode, int preset, int scale) { return {static_cast<Mode>(Pick(mode, 3, 0)), Pick(preset, 3, 1), Pick(scale, 3, 2)}; }

// ---- the layer's control API, V1 (its include/xrfg/provider_api.hpp of 0.2.1; a larger struct_size is taken, a smaller refused) ----
constexpr uint32_t kApiVersion = 1;
struct ControlSettingsV1 {
    uint32_t structSize = sizeof(ControlSettingsV1);
    int32_t enabled = 1, backend = 0, preset = 1, scale = 2, backward = 0, motionVectors = 0;   // backend 0 FidelityFX, 1 NVIDIA
};
struct ControlSnapshotV1 {
    uint32_t structSize = sizeof(ControlSnapshotV1), apiVersion = kApiVersion;
    ControlSettingsV1 desired{};
    uint64_t revision = 0;
    uint32_t sessions = 0, pending = 0, bypass = 0, ready = 0, errors = 0;   // the sessions by what they made of the last request
    int32_t lastError = 0;
};
static_assert(sizeof(ControlSettingsV1) == 28 && sizeof(ControlSnapshotV1) == 72, "OFXR control ABI V1");
using VersionFn = uint32_t (*)();
using SnapshotFn = int (*)(ControlSnapshotV1*);
using RequestFn = int (*)(const ControlSettingsV1*);
constexpr char kVersionExport[] = "OFXR_GetProviderApiVersionV1", kSnapshotExport[] = "OFXR_GetControlSnapshotV1", kRequestExport[] = "OFXR_RequestControlV1";

// What the layer is asked for: running only with the key on AND both eyes from one game frame; else it steps aside (enabled 0) until a restart leaves it out.
inline ControlSettingsV1 Control(const Options& o, bool bothEyes) {
    ControlSettingsV1 c;
    c.enabled = o.On() && bothEyes ? 1 : 0;
    c.backend = o.mode == Mode::Nvidia ? 1 : 0;
    c.preset = o.preset;
    c.scale = o.scale;
    return c;
}
inline std::string Describe(int backend, int preset, int scale) {
    if (backend != 1) return kModeNames[1];
    return std::string(kModeNames[2]) + " " + kPresetNames[Pick(preset, 3, 1)] + ", " + kScaleNames[Pick(scale, 3, 2)];
}

// ---- how it went, for the log and Settings ----
enum class Start {
    NotAsked,     // the key was off at the start
    Missing,      // on, but d2r_vr_libs\ofxr has no layer manifest (Settings installs it)
    Failed,       // on, but the loader did not list it or the instance failed with it: the game started without it
    Loaded,       // the instance runs through it, its control API found
    NoControl,    // the instance runs through it, without a control API this mod knows
};
enum class State { Off, NotInstalled, RestartToStart, Failed, Starting, Applying, Active, Bypassed, Paused, Error, NoControl, NeedsBothEyes };
struct Readout {
    State state = State::Off;
    int backend = 0, preset = 1, scale = 2;
    uint32_t sessions = 0, ready = 0, bypass = 0, errors = 0;
    int32_t lastError = 0;
    bool runtimeHalves = false;   // Active, but the runtime halves the rate again (Virtual Desktop's SSW: HalvingWatch)
};
// started: as above; installed: the manifest is there now; wanted: the settings now; snapshot: the layer's (Loaded, when it answered); bothEyes: not by turns.
inline Readout Classify(Start started, bool installed, const Options& wanted, const ControlSnapshotV1* snapshot, bool bothEyes) {
    Readout r;
    if (started == Start::NoControl) { r.state = State::NoControl; return r; }
    if (started == Start::Loaded) {
        if (!wanted.On()) { r.state = State::Paused; return r; }
        if (!bothEyes) { r.state = State::NeedsBothEyes; return r; }
        if (!snapshot) { r.state = State::Error; return r; }
        r.backend = snapshot->desired.backend; r.preset = snapshot->desired.preset; r.scale = snapshot->desired.scale;
        r.sessions = snapshot->sessions; r.ready = snapshot->ready; r.bypass = snapshot->bypass; r.errors = snapshot->errors; r.lastError = snapshot->lastError;
        r.state = !snapshot->sessions ? State::Starting : snapshot->pending ? State::Applying : snapshot->errors ? State::Error : snapshot->ready ? State::Active : State::Bypassed;
        return r;
    }
    if (!wanted.On()) r.state = State::Off;
    else if (started == Start::Failed) r.state = State::Failed;
    else if (!installed) r.state = State::NotInstalled;
    else if (!bothEyes) r.state = State::NeedsBothEyes;
    else r.state = State::RestartToStart;   // off at the start, or installed since
    return r;
}
inline std::string Line(const Readout& r) {
    char b[200];
    const std::string running = Describe(r.backend, r.preset, r.scale);
    switch (r.state) {
        case State::Off: return "frame generation: off (experimental, OFXR Bridge)";
        case State::NotInstalled: return "frame generation: on, but OFXR Bridge is not installed (D2R VR Settings > Home installs it)";
        case State::RestartToStart: return "frame generation: loading OFXR Bridge - the VR session starts again (a moment of black)";
        case State::Failed: return "frame generation: OFXR Bridge did not start - the game runs without it (see the lines above)";
        case State::Starting: return "frame generation: OFXR Bridge loaded, waiting for the headset session";
        case State::Applying: std::snprintf(b, sizeof b, "frame generation: OFXR Bridge switching to %s", running.c_str()); return b;
        case State::Active:
            if (r.runtimeHalves) return "frame generation: active, but Virtual Desktop's SSW halves the rate again - set SSW to Disabled in Virtual Desktop";
            std::snprintf(b, sizeof b, "frame generation: active - OFXR Bridge %s, the game draws every other headset frame", running.c_str());
            return b;
        case State::Bypassed: return "frame generation: OFXR Bridge loaded but standing aside - no frames made";
        case State::Paused: return "frame generation: off for now - restart the game to unload OFXR Bridge";
        case State::Error:
            if (!r.errors) return "frame generation: OFXR Bridge does not answer";
            std::snprintf(b, sizeof b, "frame generation: OFXR Bridge failed (0x%08X) - no frames made", static_cast<unsigned>(r.lastError));
            return b;
        case State::NoControl: return "frame generation: OFXR Bridge loaded; this version tells nothing of itself";
        case State::NeedsBothEyes: return "frame generation: paused - it needs both eyes from one game frame (not Native OpenXR - eyes by turns)";
    }
    return {};
}

// ---- after bearstonem/d2r-pcvr v0.3.27 (MIT: a30c3eb, e595b8c) ----
// It would run (on, installed, both eyes) but this instance has no bridge (it was off at the start, or installed since): the session is made again, and the new
// instance names the layer - no game restart. Not by turns (it would only wait), not when it failed or cannot load.
inline bool LoadsWithNewSession(const Readout& r) { return r.state == State::RestartToStart; }
// The loaded bridge failed a request made while it ran: under Virtual Desktop, resuming after by turns once it had made frames failed with 0x80070057 every
// time and stayed failed, while a new session set it up afresh (his tester's RTX 4090 and his own PC, 2026-10-24) - so after a live request, once.
inline bool RetriesWithNewSession(const Readout& r) { return r.state == State::Error && r.errors > 0; }
// Under Virtual Desktop the bridge keeps the headset's period while it makes frames; a predicted rate well under the session's highest means Virtual Desktop's
// own Synchronous Spacewarp halves it too, and the game draws a quarter (his tester: 22 pairs a second, 28 % of the time, none with SSW off). Note takes a
// second's predicted rate; HoldSeconds in a row set or clear it.
struct HalvingWatch {
    static constexpr int kHoldSeconds = 3;
    float top = 0;
    int below = 0, above = 0;
    bool halved = false;
    bool Note(float hz) {
        if (!(hz > 0.0f) || !std::isfinite(hz)) return halved;
        top = hz > top ? hz : top;
        if (hz < top * 0.6f) { above = 0; if (++below >= kHoldSeconds) halved = true; }
        else if (hz > top * 0.8f) { below = 0; if (++above >= kHoldSeconds) halved = false; }
        else below = above = 0;
        return halved;
    }
};

}  // namespace framegen
