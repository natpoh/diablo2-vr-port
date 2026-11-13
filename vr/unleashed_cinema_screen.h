// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/cinema_screen.h: 7e470e3 ("Cinematics on a screen in the world"), a28bee8 (the film at the window's own
// size, top left) and df3f71d (6 m wide at 5 m). Taken into D2R VR Authentic as [cinema] world_screen (default 0 = as before): the
// tracker and its message table as they are; the movie's part of the frame and the screen's place on our native OpenXR path
// (vr/xr.cpp CloseFrame, the flat picture) instead of Unleashed's transport. FilmIn (the film's 16:9 inside that part) and Place
// (plain floats) are ours. The rest of D2R VR Authentic is MIT; this file and cleanroom/tests/unleashed_cinema_screen_test.cpp stay
// MPL 2.0.
//
// How D2R plays its cinematics: each is a WebM (data/hd/local/video/<name>.webm, with its .flac) shown by a UI widget,
// MoviePlayerWidget, in a movie panel - InGameMoviePlayerPanel in a game (the act videos and the ending, asked for by
// 'ProgressionMessage' 'PlayVideo'), MoviePlayerPanel from the main menu's Cinematics, LogoMenuPanel for the logos and the first
// launch's intro. TitleScreenPanel loops its logo ('PlayLooped'): a menu, not a cinematic. The game draws no world meanwhile.
//
// Told by the game's own UI messages (data, not code bytes: no game build is assumed), with two failsafes: in a game, the world
// drawn again for a while (an end message missed), and a cap.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace cinema {

enum class Signal : uint8_t { None, Start, End };

// The movie panels: one closed or unloaded ends what played in it.
inline bool MoviePanel(std::string_view name) {
    return name == "InGameMoviePlayerPanel" || name == "MoviePlayerPanel" || name == "LogoMenuPanel";
}

// One UI message (target, command, text) as the D2RLoader UI message event gives it.
inline Signal Classify(std::string_view target, std::string_view command, std::string_view text) {
    if (target == "MoviePlayerMessage") {
        if (command == "Play") return Signal::Start;
        // PlayLooped: the title screen's loop, a menu. Cancel: sent as a movie panel closes. Stop: not seen yet.
        if (command == "PlayLooped" || command == "Cancel" || command == "Stop") return Signal::End;
        return Signal::None;   // DisableHoldToSkip and the like: still playing
    }
    if (target == "Cinematics")   // MovieComplete: an act video done; Close: the main menu's Cinematics left
        return command == "MovieComplete" || command == "Close" ? Signal::End : Signal::None;
    if (target == "FrontEndNavigation") return Signal::End;   // to the main menu, a game, the exit: menus
    if (target == "PanelManager") {
        if (command == "UnloadAll" || command == "CloseAll") return Signal::End;
        if ((command == "ClosePanel" || command == "UnloadPanel") && MoviePanel(text)) return Signal::End;
    }
    return Signal::None;
}

enum class Why : uint8_t { None, Message, WorldDrawn, TooLong };
inline const char* Reason(Why why) {
    switch (why) {
    case Why::Message: return "the game's message";
    case Why::WorldDrawn: return "the game drew its world again (no end message seen)";
    case Why::TooLong: return "longer than any cinematic (a stuck start?)";
    default: return "";
    }
}
struct Change { bool changed = false, playing = false; Why why = Why::None; };

// Whether a cinematic plays. Not thread-safe: the caller holds one lock round all of it.
class Tracker {
public:
    // The game's world drawn (DrawGameScreen) this long without a gap ends a cinematic whose end message was missed: none draws it
    // while a movie plays. Draws in the first GraceMs of a movie are the frames already on their way.
    static constexpr uint64_t GraceMs = 2000, WorldMs = 750, GapMs = 250;
    // The longest of D2R's cinematics is a few minutes; past this a start was stuck.
    static constexpr uint64_t MaxMs = 12 * 60 * 1000;

    Change Message(Signal signal, uint64_t now) {
        if (signal == Signal::Start) {
            const bool was = playing;
            playing = true; since = now; worldFrom = worldLast = 0;
            return {!was, true, Why::Message};
        }
        if (signal == Signal::End && playing) return Stop(Why::Message);
        return {};
    }
    Change WorldDrawn(uint64_t now) {
        if (!playing || now < since + GraceMs) return {};
        if (!worldFrom || now > worldLast + GapMs) worldFrom = now;
        worldLast = now;
        return now - worldFrom >= WorldMs ? Stop(Why::WorldDrawn) : Change{};
    }
    Change Tick(uint64_t now) { return playing && now >= since + MaxMs ? Stop(Why::TooLong) : Change{}; }
    bool Playing() const { return playing; }
    uint64_t Since() const { return since; }

private:
    Change Stop(Why why) { playing = false; worldFrom = worldLast = 0; return {true, false, why}; }
    bool playing = false;
    uint64_t since = 0, worldFrom = 0, worldLast = 0;
};

// -- The screen ---------------------------------------------------------------------------------------------------------------------
struct Crop { uint32_t x = 0, y = 0, w = 0, h = 0; };

// The part of the frame the movie player draws in. It asks for the window's size and is given the physical client's (Unleashed
// VR_RESOLUTION.md: movies keep their original dimensions), so it draws at the window's own size in the frame's top left corner;
// clamped to the frame. windowWidth/windowHeight 0 = not known: the whole frame (our interface lies over the whole picture in a game,
// vr_resolution.h - Unleashed's fallback was its 16:9 interface band).
inline Crop MovieBand(uint32_t width, uint32_t height, uint32_t windowWidth = 0, uint32_t windowHeight = 0) {
    if (width && height && windowWidth && windowHeight) return {0, 0, std::min(windowWidth, width), std::min(windowHeight, height)};
    return {0, 0, width, height};
}
// Ours: the film inside that part - the game keeps a cinematic's 16:9 (black bars on a window of another shape), centred. Our window
// is near square in VR (it follows the render size), so the screen shows the 16:9 middle of the band, not 6 m of black bars too.
inline Crop FilmIn(Crop band) {
    if (!band.w || !band.h) return band;
    if ((uint64_t)band.h * 16 > (uint64_t)band.w * 9) {   // taller than 16:9: bars above and below
        const uint32_t h = std::max<uint32_t>(1, (uint32_t)((uint64_t)band.w * 9 / 16));
        return {band.x, band.y + (band.h - h) / 2, band.w, h};
    }
    const uint32_t w = std::max<uint32_t>(1, (uint32_t)((uint64_t)band.h * 16 / 9));   // wider: bars left and right
    return {band.x + (band.w - w) / 2, band.y, w, band.h};
}

// [cinema] distance_m and width_m: a screen ahead at eye height, 6 m wide at 5 m by default - about 62 degrees across, the whole film
// in view. Unleashed's owner, 2026-11-12: 3 m at 3 m, then 12 m at 3 m ("four times as large"), then "half as large ... it's too big
// ... very up close to our face". Width up to 48 m, distance up to 20 m.
struct Screen { float distance = 5.f, width = 6.f; };
inline Screen Clamped(float distance, float width) {
    const auto keep = [](float v, float fallback, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; };
    return {keep(distance, 5.f, 1.f, 20.f), keep(width, 6.f, .5f, 48.f)};
}
struct Size { float width = 0, height = 0; };
// The screen's size in metres: the shown part's own shape at the width asked for.
inline Size ScreenSize(Crop shown, float width) {
    if (!shown.w || !shown.h) return {width, width * 9.f / 16.f};
    return {width, width * float(shown.h) / float(shown.w)};
}
// The shown part as fractions of the frame (left, top, right, bottom): what the laser's point on the screen stands for in the
// window (xr.cpp FlatShown crop: the game's pointer conversions take the whole frame).
inline void Fractions(Crop shown, uint32_t width, uint32_t height, float out[4]) {
    if (!width || !height || !shown.w || !shown.h) { out[0] = out[1] = 0.f; out[2] = out[3] = 1.f; return; }
    out[0] = float(shown.x) / float(width);
    out[1] = float(shown.y) / float(height);
    out[2] = float(shown.x + shown.w) / float(width);
    out[3] = float(shown.y + shown.h) / float(height);
}

// Ours: the screen's centre, placed once from the eyes (x, y, z; y up, -z ahead at yaw 0) and the head's yaw (radians, turned left
// positive, as xr.cpp's Yaw): `distance` straight ahead at eye height, level. World-locked: the head moving later moves nothing.
struct Point { float x = 0, y = 0, z = 0; };
inline Point Place(Point eyes, float yaw, float distance) {
    return {eyes.x - std::sin(yaw) * distance, eyes.y, eyes.z - std::cos(yaw) * distance};
}

}  // namespace cinema
