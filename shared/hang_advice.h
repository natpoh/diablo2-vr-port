// shared/hang_advice.h - [support] hang_advice (on): after a game whose graphics card stopped, D2R VR Settings' Home says so at its next start and
// offers the safe road - one click each: the game's DLSS off, frame generation off, the render size one step lower - and says to update the
// graphics driver. Nothing is written until the player presses a button (WritePrivateProfileString for d2r_vr.ini, the game's own Settings.json
// spliced as Settings writes its other game options, only with the game closed).
//
// Why (docs/notes/discord_2026-11-07_black_screen_and_freezes.md, section 2): crash reports 229, 267 and martin039383 - GPU hangs (DEVICE_HUNG)
// seconds after video settings were applied in VR, two with DLSS; 223 / 249 / 250 - RTX 50 hangs mid-game; 283 a driver reset at the first stereo
// pair. Each time the next start went on as before, with nothing said.
//
// What counts as "stopped last time":
//  - the plugin's own record, d2rloader\logs\d2r_vr_gpu_fault.txt (vr/gpu_fault.cpp): written the moment the D3D12 device is removed, moved to
//    d2r_vr_gpu_fault_prev.txt at the next game start - so while it exists it is the last game's (or the running one's). Its last
//    "==== <date> <time> - D2R VR - the D3D12 device was removed" header and the removal reason under it. INVALID_CALL is a bad call (ours or
//    the game's), not the card: no advice for that one.
//  - else the game's own DX12 backend asserting on a failed call in D2RLoader's log of the last game (BC_ASSERT: SUCCEEDED(hr) in
//    dx12_backend.cpp - the device-lost signature of 2026-10-17): the reason unknown.
// The advice is shown once per such game: "Hide this advice" keeps that game's stamp in D2R VR Settings' own ini (d2r_vr_settings.ini).
//
// Pure, no Windows (tools/hang_advice_test.cpp).
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace hangadvice {

enum class Reason {
    None,        // nothing found
    Hung,        // 0x887A0006 DXGI_ERROR_DEVICE_HUNG
    Reset,       // 0x887A0007 DXGI_ERROR_DEVICE_RESET
    Removed,     // 0x887A0005 DXGI_ERROR_DEVICE_REMOVED
    Driver,      // 0x887A0020 DXGI_ERROR_DRIVER_INTERNAL_ERROR
    Memory,      // 0x8007000E E_OUTOFMEMORY
    BadCall,     // 0x887A0001 DXGI_ERROR_INVALID_CALL - a bad call, not the card: no advice
    Unknown,     // removed for a reason not in this list, or only the game's assertion seen
};

struct Found {
    Reason reason = Reason::None;
    std::string stamp;   // "2026-11-06 20:16:38" (the record's own time), or what the caller gives for the loader's log
    bool fromLoaderLog = false;
};

// Whether the advice is for this one (the card stopped; a bad call is not the card's doing).
inline bool Advise(const Found& f) { return f.reason != Reason::None && f.reason != Reason::BadCall; }

inline Reason FromCode(uint32_t hr) {
    switch (hr) {
        case 0x887A0006u: return Reason::Hung;
        case 0x887A0007u: return Reason::Reset;
        case 0x887A0005u: return Reason::Removed;
        case 0x887A0020u: return Reason::Driver;
        case 0x8007000Eu: return Reason::Memory;
        case 0x887A0001u: return Reason::BadCall;
        default: return Reason::Unknown;
    }
}

// d2r_vr_gpu_fault.txt's text: its LAST report (an old file from before the rotation held several games).
inline Found FromGpuFault(const std::string& text) {
    Found f;
    static const char kHead[] = "==== ";
    static const char kWhat[] = " - D2R VR - the D3D12 device was removed";
    size_t at = std::string::npos;
    for (size_t p = text.find(kWhat); p != std::string::npos; p = text.find(kWhat, p + 1)) at = p;
    if (at == std::string::npos) return f;
    const size_t lineStart = text.rfind('\n', at) == std::string::npos ? 0 : text.rfind('\n', at) + 1;
    if (text.compare(lineStart, 5, kHead) != 0) return f;
    f.stamp = text.substr(lineStart + 5, at - lineStart - 5);
    f.reason = Reason::Unknown;
    static const char kReason[] = "removal reason: 0x";
    const size_t r = text.find(kReason, at);
    if (r != std::string::npos) {
        const std::string hex = text.substr(r + sizeof kReason - 1, 8);
        char* end = nullptr;
        const unsigned long v = std::strtoul(hex.c_str(), &end, 16);
        if (end && end != hex.c_str()) f.reason = FromCode((uint32_t)v);
    }
    return f;
}

// D2RLoader's log of the last game: the game's DX12 backend asserting on a failed call (the device-lost signature). `stamp`: the log's own time
// (the caller's: its last write) - the game's stamp for "Hide this advice".
inline Found FromLoaderLog(const std::string& text, const std::string& stamp) {
    Found f;
    for (size_t p = text.find("dx12_backend.cpp"); p != std::string::npos; p = text.find("dx12_backend.cpp", p + 1)) {
        const size_t from = p > 600 ? p - 600 : 0;   // the assertion's box: its Message line a few lines above Location
        if (text.find("SUCCEEDED(hr)", from) < p) {
            f.reason = Reason::Unknown;
            f.stamp = stamp;
            f.fromLoaderLog = true;
            return f;
        }
    }
    return f;
}

// "Render size one step lower": the side in pixels to write into [render] vr_resolution_px, 0 = no step to offer. `side` is the square the game
// draws now (the slider's side; for the headset's recommendation or the window's size, the square of the same pixels). The steps are the sizes
// players know from the slider's tips; the lowest offered is 1536.
inline constexpr int kSteps[] = {4096, 3584, 3072, 2880, 2560, 2240, 2048, 1792, 1536};
inline int StepDown(int side) {
    if (side <= 0) return 0;
    for (const int s : kSteps)
        if (s < side - 32) return s;   // (a side within half a slider step of a step is that step)
    return 0;
}
// The square of the same pixels as w x h (the headset's recommendation is not square).
inline int SquareOf(int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    double s = (double)w * (double)h, r = s;
    for (int i = 0; i < 40; ++i) r = 0.5 * (r + s / r);   // (no <cmath> needed: Newton)
    return (int)(r + 0.5);
}

// The game's "NVIDIA DLSS" in its Settings.json: its value (0 = off), -1 = not in the file.
inline int DlssValue(const std::string& json) {
    const size_t k = json.find("\"NVIDIA DLSS\"");
    if (k == std::string::npos) return -1;
    size_t i = k + 13;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
    if (i >= json.size() || json[i] != ':') return -1;
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
    if (i >= json.size() || !(json[i] == '-' || (json[i] >= '0' && json[i] <= '9'))) return -1;
    return std::atoi(json.c_str() + i);
}
// The same text with "NVIDIA DLSS" set to 0 (only that number changed); `ok` false = the key was not there (the text comes back as it was).
inline std::string WithDlssOff(const std::string& json, bool* ok) {
    if (ok) *ok = false;
    const size_t k = json.find("\"NVIDIA DLSS\"");
    if (k == std::string::npos) return json;
    size_t i = k + 13;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
    if (i >= json.size() || json[i] != ':') return json;
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
    size_t e = i;
    if (e < json.size() && json[e] == '-') ++e;
    while (e < json.size() && json[e] >= '0' && json[e] <= '9') ++e;
    if (e == i) return json;
    if (ok) *ok = true;
    return json.substr(0, i) + "0" + json.substr(e);
}

}  // namespace hangadvice
