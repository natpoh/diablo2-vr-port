#pragma once
// The Video page of the in-headset settings panel: the VR render size - the size of the picture the game draws for the
// headset, whatever size its window is. The ini keys are the ones D2R VR Settings writes ([render] vr_resolution_px, a
// square's side, and [render] vr_resolution, the older mode; shared/render_size.h reads the two together), so the panel and
// the PC window stay in step. The plugin already follows a change of those keys live (vr/render_resolution.cpp: the game is
// asked about 0.7 s after the value stops changing, and only once the hero stands in a world), so an Apply on this page is
// all the "live" there is.
//
// This file is the pure part: the presets, the slider's stops, what a choice writes into the two keys, and the page's texts.
// No Windows, no OpenXR - hset_test.exe runs it.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "render_size.h"
#include "ui_text.h"   // the page's words in the player's language ([hud] headset_language)

namespace hset::video {

using Pick = rendersize::Pick;

struct Size {
    int w = 0, h = 0;   // pixels, 0 = not known
    explicit operator bool() const { return w > 0 && h > 0; }
    bool operator==(const Size&) const = default;
};
// What the plugin knows of the picture, for the page's lines (not in the ini): the size of the game's output now, what the
// headset's runtime recommends per eye, and the most it takes (both eyes' limits and the system's swapchain).
struct Env {
    Size now, recommended, maximum;
    bool operator==(const Env&) const = default;
};

// -- the choices ------------------------------------------------------------------------------------------------
constexpr int kPresetCount = 8;
struct Preset {
    const char* label;
    rendersize::Kind kind;
    int side;   // a square's side
};
// The window's own size (Desktop: the picture is the game window's, as without this option), six squares, and the headset's own
// recommendation (its shape, not always square). The slider reaches every side between 1024 and 4096 in between.
inline constexpr Preset kPresets[kPresetCount] = {
    {"Window's size", rendersize::Kind::Desktop, 0},
    {"1024", rendersize::Kind::Square, 1024},
    {"1536", rendersize::Kind::Square, 1536},
    {"2048", rendersize::Kind::Square, 2048},
    {"2560", rendersize::Kind::Square, 2560},
    {"3072", rendersize::Kind::Square, 3072},
    {"4096", rendersize::Kind::Square, 4096},
    {"Headset's own", rendersize::Kind::Headset, 0},
};
// A preset's button: the two that are words in the player's language, the squares their numbers.
inline const char* PresetLabel(int i) {
    if (i < 0 || i >= kPresetCount) return "";
    if (kPresets[i].kind == rendersize::Kind::Desktop) return uitext::Tr("vr.video.preset_window", "Window's size");
    if (kPresets[i].kind == rendersize::Kind::Headset) return uitext::Tr("vr.video.preset_headset", "Headset's own");
    return kPresets[i].label;
}
inline Pick PickOfPreset(int i) {
    if (i < 0 || i >= kPresetCount) return {};
    return {kPresets[i].kind, kPresets[i].side};
}
inline Pick Square(int side) { return {rendersize::Kind::Square, rendersize::ClampSide(side)}; }

// What the two ini keys mean together (the plugin's reading: shared/render_size.h).
inline Pick PickOf(float mode, float px) {
    const auto whole = [](float v) { return std::isfinite(v) ? (int)std::lround(v) : 0; };
    return rendersize::FromIni(whole(mode), whole(px));
}

// What to write into the two keys to make `want` true, given what they hold now. The same writes D2R VR Settings makes: the
// window's own size clears both; the headset's own size is mode 1 and keeps the side (so the slider still has its place
// after the box is unticked there); a square is its side with mode 0 (an older square mode 2..6 is the slider's now).
struct Keys {
    float mode = 0, px = 0;
};
inline Keys KeysFor(const Pick& want, float curPx) {
    switch (want.kind) {
        case rendersize::Kind::Headset: return {1.0f, std::isfinite(curPx) ? std::max(curPx, 0.0f) : 0.0f};
        case rendersize::Kind::Square: return {0.0f, (float)rendersize::ClampSide(want.side)};
        default: return {0.0f, 0.0f};
    }
}

// -- the slider: every side from 1024 to 4096 in steps of 64 (the PC window's slider) -------------------------------
constexpr int kStops = (rendersize::kSideMax - rendersize::kSideMin) / rendersize::kSideStep + 1;
inline int SideOfStop(int stop) { return rendersize::kSideMin + std::clamp(stop, 0, kStops - 1) * rendersize::kSideStep; }
inline int StopOfSide(int side) {
    return std::clamp((int)std::lround((double)(side - rendersize::kSideMin) / rendersize::kSideStep), 0, kStops - 1);
}
// A place along the track (0 = the left end, 1 = the right) is the nearest stop's side.
inline int SideAtFraction(float f) {
    if (!std::isfinite(f)) return rendersize::kSideMin;
    return SideOfStop((int)std::lround(std::clamp(f, 0.0f, 1.0f) * (kStops - 1)));
}
inline float FractionOfSide(int side) { return (float)StopOfSide(side) / (float)(kStops - 1); }
inline int StepSide(int side, int delta) { return SideOfStop(StopOfSide(side) + delta); }

// -- texts ------------------------------------------------------------------------------------------------------
inline std::string SizeText(Size s) {
    if (!s) return uitext::Tr("vr.video.not_known", "not known yet");
    return std::to_string(s.w) + " x " + std::to_string(s.h);
}
// What a choice makes of the picture: "2560 x 2560", "the window's own size", "the headset's own, 2496 x 2688".
inline std::string PickText(const Pick& p, const Env& e) {
    switch (p.kind) {
        case rendersize::Kind::Headset:
            return e.recommended ? uitext::TrF("vr.video.pick_headset_size", "Headset's own: {size}", {{"size", SizeText(e.recommended)}})
                                 : std::string(uitext::Tr("vr.video.pick_headset", "Headset's own size"));
        case rendersize::Kind::Square: return std::to_string(p.side) + " x " + std::to_string(p.side);
        default: return uitext::Tr("vr.video.pick_window", "Window's own size");
    }
}
// The pixel size a choice asks for, if it is known now ({} = the window's own, or not known).
inline Size SizeOf(const Pick& p, const Env& e) {
    switch (p.kind) {
        case rendersize::Kind::Headset: return e.recommended;
        case rendersize::Kind::Square: return {p.side, p.side};
        default: return {};
    }
}
// "Draws 1.56 x the pixels of 2048 x 2048.", or what to say when there is no size to count.
inline std::string CostText(const Pick& p, const Env& e) {
    const Size s = SizeOf(p, e);
    if (!s) {
        if (p.kind == rendersize::Kind::Headset) return uitext::Tr("vr.video.cost_headset_unknown", "Its size is read when the game first starts a headset session.");
        return uitext::Tr("vr.video.cost_window", "Nothing changes: the picture is the game window's.");
    }
    return uitext::TrF("vr.video.cost", "Draws {times} x the pixels of 2048 x 2048.", {{"times", uitext::Num("%.2f", rendersize::CostVs2048(s.w, s.h))}});
}
// "Your headset takes 2400 at most: shrunk to that." when the choice is over what the runtime holds, else empty.
inline std::string LimitText(const Pick& p, const Env& e) {
    if (p.kind != rendersize::Kind::Square || !e.maximum) return {};
    const int most = std::min(e.maximum.w, e.maximum.h);
    if (most <= 0 || p.side <= most) return {};
    return uitext::TrF("vr.video.limit", "Your headset takes {most} at most: shrunk to that.", {{"most", std::to_string(most)}});
}
// "In the game now: 2048 x 2048. Your headset recommends 2496 x 2688."
inline std::string NowText(const Env& e) {
    std::string t = e.now ? uitext::TrF("vr.video.now", "The game draws {size} now.", {{"size", SizeText(e.now)}})
                          : std::string(uitext::Tr("vr.video.now_unknown", "The game's picture size is not known yet."));
    t += " ";
    t += e.recommended ? uitext::TrF("vr.video.recommended", "Your headset recommends {size}.", {{"size", SizeText(e.recommended)}})
                       : std::string(uitext::Tr("vr.video.recommended_unknown", "Your headset's size is read when the game first starts a session."));
    return t;
}

}  // namespace hset::video
