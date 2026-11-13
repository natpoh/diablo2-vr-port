// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed steamvr/cinema_screen_test.cpp (7e470e3, a28bee8, df3f71d): the logged message sequences, the tracker, the
// failsafes, the screen's size and the laser's mapping, on vr/unleashed_cinema_screen.h. FilmIn, Place and the wiring checks of our
// sources are ours. Run: unleashed_cinema_screen_test.exe [vr/gamestate.cpp vr/frame/frame_order.cpp vr/xr.cpp
// vr/render/effects/effects.cpp vr/config/load_settings.cpp] (the wiring checks only with the paths).
#include "unleashed_cinema_screen.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using cinema::Signal;
static int g_fail = 0, g_pass = 0;
static void Check(bool ok, const std::string& label) {
    if (ok) ++g_pass;
    else { ++g_fail; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}
static bool Near(double a, double b, double within = 1e-4) { return std::fabs(a - b) <= within; }
static std::string Slurp(const char* path) {   // line ends as LF, whatever the checkout wrote
    std::ifstream f(path, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(f), {}};
    std::erase(text, '\r');
    return text;
}
static void Has(const std::string& text, const char* file, const char* line, const char* label) {
    Check(text.find(line) != std::string::npos, std::string(file) + ": " + label + " (missing: " + line + ")");
}

struct Ui { const char* target; const char* command; const char* text; };
// Replays UI messages through Classify and a tracker, one millisecond apart from `at`.
static bool Replay(cinema::Tracker& t, const std::vector<Ui>& messages, uint64_t& at) {
    for (const auto& m : messages) t.Message(cinema::Classify(m.target, m.command, m.text), at++);
    return t.Playing();
}

int main(int argc, char** argv) {
    {   // The messages the game sent in the logs (Unleashed's owner's; ours: LogoMenuPanel, Cancel, PlayLooped, InGameMoviePlayerPanel alike).
        Check(cinema::Classify("MoviePlayerMessage", "Play", "") == Signal::Start, "a movie played starts a cinematic");
        Check(cinema::Classify("MoviePlayerMessage", "PlayLooped", "") == Signal::End, "the title screen's loop is a menu, not a cinematic");
        Check(cinema::Classify("MoviePlayerMessage", "Cancel", "") == Signal::End, "Cancel ends it");
        Check(cinema::Classify("MoviePlayerMessage", "DisableHoldToSkip", "") == Signal::None, "hold-to-skip changes nothing");
        Check(cinema::Classify("Cinematics", "MovieComplete", "") == Signal::End, "an act video complete");
        Check(cinema::Classify("Cinematics", "Close", "") == Signal::End, "the main menu's Cinematics left");
        Check(cinema::Classify("FrontEndNavigation", "ToFrontEnd", "") == Signal::End, "menus");
        Check(cinema::Classify("PanelManager", "UnloadAll", "") == Signal::End, "every panel gone");
        for (const char* panel : {"InGameMoviePlayerPanel", "MoviePlayerPanel", "LogoMenuPanel"}) {
            Check(cinema::Classify("PanelManager", "ClosePanel", panel) == Signal::End, std::string(panel) + " closed ends it");
            Check(cinema::Classify("PanelManager", "UnloadPanel", panel) == Signal::End, std::string(panel) + " unloaded ends it");
            Check(cinema::Classify("PanelManager", "OpenPanel", panel) == Signal::None, std::string(panel) + " opened is not a movie yet");
        }
        // The in-game movie panel is open all game (it is in every "menu OPEN" list of our logs too): only a movie counts.
        Check(cinema::Classify("PanelManager", "ClosePanel", "NpcDialogPanel") == Signal::None, "other panels change nothing");
        Check(cinema::Classify("ProgressionMessage", "PlayVideo", "") == Signal::None, "the request alone waits for the movie's own Play");
        Check(cinema::Classify("LogoMenu", "NextMovie", "") == Signal::None, "the next logo: still playing");
        Check(cinema::Classify("InputMessage", "Play", "") == Signal::None, "another target's Play is not a movie");
    }
    {   // 2026-10-15 16:40:57: Meshif's boat to Kurast - the Act II -> III video in a game.
        cinema::Tracker t; uint64_t at = 1000;
        Check(Replay(t, {{"PanelManager", "ClosePanel", "NpcDialogPanel"}, {"ProgressionMessage", "PlayVideo", ""}, {"MoviePlayerMessage", "Play", ""}}, at),
              "the act video plays");
        Check(!Replay(t, {{"Cinematics", "MovieComplete", ""}}, at), "and ends with MovieComplete");
    }
    {   // A first launch - the logos, the title screen, calibration, the intro, the main menu.
        cinema::Tracker t; uint64_t at = 1000;
        Check(Replay(t, {{"PanelManager", "OpenPanel", "LogoMenuPanel"}, {"MoviePlayerMessage", "Play", ""}, {"MoviePlayerMessage", "DisableHoldToSkip", ""},
                         {"LogoMenu", "NextMovie", ""}}, at), "the logos play");
        Check(!Replay(t, {{"PanelManager", "ClosePanel", "LogoMenuPanel"}, {"MoviePlayerMessage", "Cancel", ""},
                          {"PanelManager", "OpenExclusivePanel", "TitleScreenPanel"}, {"MoviePlayerMessage", "PlayLooped", ""}}, at),
              "the title screen's loop is the menu panel's");
        Check(Replay(t, {{"PanelManager", "UnloadPanel", "GammaCalibrationPanel"}, {"LogoMenu", "PlayIntro", ""}, {"MoviePlayerMessage", "Play", ""}}, at),
              "the intro plays");
        Check(!Replay(t, {{"PanelManager", "UnloadPanel", "LogoMenuPanel"}, {"PanelManager", "OpenPanel", "TitleScreenPanel"}}, at), "and ends as its panel goes");
    }
    {   // A launch that skips the logos (ours, 2026-11-12 23:09:11): the panel opens and goes, no movie.
        cinema::Tracker t; uint64_t at = 1000;
        Check(!Replay(t, {{"PanelManager", "OpenPanel", "LogoMenuPanel"}, {"PanelManager", "UnloadPanel", "LogoMenuPanel"}, {"MoviePlayerMessage", "Cancel", ""},
                          {"PanelManager", "OpenExclusivePanel", "TitleScreenPanel"}, {"MoviePlayerMessage", "PlayLooped", ""}}, at), "nothing plays");
    }
    {   // The main menu's Cinematics: picked, played, left.
        cinema::Tracker t; uint64_t at = 1000;
        Check(!Replay(t, {{"FrontEndNavigation", "ToCinematics", ""}, {"PanelManager", "OpenPanel", "CinematicsModal"}, {"PanelManager", "OpenPanel", "MoviePlayerPanel"}}, at),
              "the list is a menu");
        Check(Replay(t, {{"MoviePlayerMessage", "Play", ""}}, at), "one picked plays");
        Check(!Replay(t, {{"Cinematics", "Close", ""}, {"PanelManager", "UnloadPanel", "MoviePlayerPanel"}}, at), "leaving the list ends it");
    }
    {   // A video skipped: the movie panel's Cancel.
        cinema::Tracker t; uint64_t at = 1000;
        Replay(t, {{"MoviePlayerMessage", "Play", ""}}, at);
        Check(!Replay(t, {{"MoviePlayerMessage", "Cancel", ""}}, at), "a skip ends it");
    }
    {   // A game left during a video: every panel unloaded.
        cinema::Tracker t; uint64_t at = 1000;
        Replay(t, {{"MoviePlayerMessage", "Play", ""}}, at);
        Check(!Replay(t, {{"PanelManager", "UnloadPanel", "InGameMoviePlayerPanel"}}, at), "the in-game movie panel unloaded ends it");
    }
    {   // Changes are told once each way.
        cinema::Tracker t;
        auto c = t.Message(Signal::Start, 10); Check(c.changed && c.playing && c.why == cinema::Why::Message, "a start is a change");
        c = t.Message(Signal::Start, 20); Check(!c.changed && c.playing && t.Since() == 20, "a second movie (the next logo) is no change, timed anew");
        c = t.Message(Signal::End, 30); Check(c.changed && !c.playing, "an end is a change");
        c = t.Message(Signal::End, 40); Check(!c.changed, "a second end is none");
        c = t.Message(Signal::None, 50); Check(!c.changed && !t.Playing(), "no signal, nothing");
    }
    {   // The failsafe in a game: no world is drawn while a movie plays.
        using T = cinema::Tracker;
        cinema::Tracker t; t.Message(Signal::Start, 1000);
        bool none = true;
        for (uint64_t now = 1000; now < 1000 + T::GraceMs; now += 11) none = none && !t.WorldDrawn(now).changed;
        Check(none && t.Playing(), "frames on their way when it starts end nothing");
        uint64_t now = 1000 + T::GraceMs + 100;   // a few stray draws, then a gap: no end
        for (int i = 0; i < 10; ++i, now += 11) t.WorldDrawn(now);
        now += T::GapMs + 50;
        none = true;
        for (int i = 0; i < 40; ++i, now += 11) none = none && !t.WorldDrawn(now).changed;
        Check(none && t.Playing(), "440 ms of world after a gap is not yet the game back");
        cinema::Change last;
        for (int i = 0; i < 40 && t.Playing(); ++i, now += 11) last = t.WorldDrawn(now);
        Check(!t.Playing() && last.changed && last.why == cinema::Why::WorldDrawn, "750 ms of world drawn ends a cinematic whose end was missed");
        Check(!t.WorldDrawn(now + 11).changed, "drawn on, nothing more");
        cinema::Tracker idle; Check(!idle.WorldDrawn(5000).changed && !idle.Playing(), "no cinematic, no change");
    }
    {   // The cap.
        using T = cinema::Tracker;
        cinema::Tracker t; t.Message(Signal::Start, 1000);
        Check(!t.Tick(1000 + T::MaxMs - 1).changed && t.Playing(), "under the cap: playing");
        const auto c = t.Tick(1000 + T::MaxMs);
        Check(c.changed && !c.playing && c.why == cinema::Why::TooLong, "past the cap: over");
        Check(T::MaxMs >= 6 * 60 * 1000, "the cap is longer than any of D2R's cinematics");
        Check(*cinema::Reason(cinema::Why::WorldDrawn) && *cinema::Reason(cinema::Why::TooLong) && *cinema::Reason(cinema::Why::Message), "every end has a reason");
    }
    {   // The movie's part of the frame: the window's own size, top left; unknown: the whole frame (ours).
        auto band = cinema::MovieBand(3072, 3072, 1920, 1080);
        Check(band.x == 0 && band.y == 0 && band.w == 1920 && band.h == 1080, "a 1920x1080 window under a 3072 square frame: its top left 1920x1080");
        band = cinema::MovieBand(3072, 3072, 2560, 1600); Check(band.w == 2560 && band.h == 1600, "a 16:10 window: its own shape");
        band = cinema::MovieBand(2560, 1440, 3840, 2160); Check(band.w == 2560 && band.h == 1440, "a window larger than the frame: the frame");
        band = cinema::MovieBand(2496, 2688, 2496, 2688); Check(band.w == 2496 && band.h == 2688, "the window following the render size: all of it");
        band = cinema::MovieBand(3072, 3072, 0, 1080); Check(band.w == 3072 && band.h == 3072, "no window size: the whole frame");
    }
    {   // Ours: the film's 16:9, centred in that part.
        auto film = cinema::FilmIn({0, 0, 1920, 1080}); Check(film.x == 0 && film.y == 0 && film.w == 1920 && film.h == 1080, "16:9: all of it");
        film = cinema::FilmIn({0, 0, 2496, 2688});
        Check(film.x == 0 && film.w == 2496 && film.h == 1404 && film.y == (2688 - 1404) / 2, "a tall window: the 16:9 rows in its middle");
        film = cinema::FilmIn({0, 0, 3440, 1440});
        Check(film.y == 0 && film.h == 1440 && film.w == 2560 && film.x == (3440 - 2560) / 2, "an ultrawide one: the 16:9 columns in its middle");
        film = cinema::FilmIn({0, 0, 2560, 1600}); Check(film.w == 2560 && film.h == 1440 && film.y == 80, "16:10: letterboxed");
        film = cinema::FilmIn({}); Check(film.w == 0 && film.h == 0, "nothing: nothing");
    }
    {   // The screen: the film's shape at the width asked for, comfortably ahead.
        const cinema::Screen d;
        Check(Near(d.distance, 5) && Near(d.width, 6), "6 m wide, 5 m ahead by default");
        const double across = 2 * std::atan(d.width * .5 / d.distance) * 57.29578;
        Check(across > 55 && across < 70, "about 62 degrees across: the whole film in view, not close to the face");
        auto s = cinema::ScreenSize(cinema::FilmIn(cinema::MovieBand(2496, 2688, 2496, 2688)), 6);
        Check(Near(s.width, 6) && Near(s.height, 6 * 1404 / 2496.), "a 16:9 film, 6 x 3.375 m - not stretched to the tall frame");
        s = cinema::ScreenSize({}, 2); Check(Near(s.width, 2) && Near(s.height, 1.125), "no frame yet: 16:9");
        auto c = cinema::Clamped(0, 100); Check(Near(c.distance, 1) && Near(c.width, 48), "limits");
        c = cinema::Clamped(100, 6); Check(Near(c.distance, 20), "20 m at most");
        c = cinema::Clamped(NAN, INFINITY); Check(Near(c.distance, 5) && Near(c.width, 6), "a broken value: the default");
        c = cinema::Clamped(4, 3.5f); Check(Near(c.distance, 4) && Near(c.width, 3.5), "in range: as written");
    }
    {   // Ours: placed once from the eyes, at eye height, straight ahead, the asked distance.
        const cinema::Point eyes{.2f, 1.65f, .1f};
        auto p = cinema::Place(eyes, 0.f, 5.f);
        Check(Near(p.x, .2) && Near(p.y, 1.65) && Near(p.z, .1 - 5), "yaw 0: 5 m along -z, at eye height");
        p = cinema::Place(eyes, 1.5707963f, 5.f);
        Check(Near(p.x, .2 - 5, 1e-3) && Near(p.z, .1, 1e-3), "turned left 90 degrees: 5 m along -x (xr.cpp's Yaw: + turns left)");
        p = cinema::Place(eyes, .6f, 5.f);
        const double dx = p.x - eyes.x, dy = p.y - eyes.y, dz = p.z - eyes.z;
        Check(Near(std::sqrt(dx * dx + dy * dy + dz * dz), 5, 1e-3) && Near(dy, 0), "any yaw: the distance asked for, level");
    }
    {   // The laser on the screen points at the film's own pixels of the frame.
        float f[4];
        const auto film = cinema::FilmIn(cinema::MovieBand(3072, 3072, 1920, 1080));
        cinema::Fractions(film, 3072, 3072, f);
        Check(Near(f[0], 0) && Near(f[1], 0) && Near(f[2], 1920 / 3072.) && Near(f[3], 1080 / 3072.), "the film's corners in the frame");
        cinema::Fractions(cinema::FilmIn({0, 0, 2496, 2688}), 2496, 2688, f);
        Check(Near(f[0], 0) && Near(f[2], 1) && Near(f[1], 642 / 2688.) && Near(f[3], 2046 / 2688.), "the middle rows of a tall frame");
        cinema::Fractions({}, 0, 0, f); Check(f[0] == 0 && f[1] == 0 && f[2] == 1 && f[3] == 1, "no frame: the whole window");
    }
    if (argc >= 6) {   // Wiring: the cinematic reaches every place that decides what the headset shows (and 0 leaves all of it as it was).
        const std::string gamestate = Slurp(argv[1]), order = Slurp(argv[2]), xr = Slurp(argv[3]), effects = Slurp(argv[4]), load = Slurp(argv[5]);
        Check(!gamestate.empty() && !order.empty() && !xr.empty() && !effects.empty() && !load.empty(), "sources read");
        Has(gamestate, "gamestate.cpp", "CinemaMessage(ctx, target, command, text);", "every UI message is classified");
        Has(gamestate, "gamestate.cpp", "bool CinemaPlaying() { return g_cinemaOn.load() && g_cinema.load(); }", "world_screen=0 leaves things as they were");
        Has(load, "load_settings.cpp", "gamestate::SetCinemaEnabled(IniB(L\"cinema\", L\"world_screen\", false));", "the key, default 0");
        Has(order, "frame_order.cpp", "if (!depth) gamestate::CinemaWorldDrawn(GetTickCount64());", "the world drawn again is seen");
        Has(order, "frame_order.cpp", "&& !gamestate::CinemaPlaying();", "no stereo pairs while one plays");
        Has(order, "frame_order.cpp", "hud::SetMenuOpen(gamestate::MenuOpen() || !g_inWorld.load() || gamestate::CinemaPlaying());", "no interface strip cut out of the movie");
        Has(order, "frame_order.cpp", "gamestate::CinemaTick(", "the cap is ticked");
        Has(effects, "effects.cpp", "xr::SetLaserWanted(gamestate::MenuOpen() || !g_inWorld.load() || gamestate::CinemaPlaying());", "the laser on the screen in a game too");
        Has(xr, "xr.cpp", "const bool cinemaNow = gamestate::CinemaPlaying();", "the flat picture asks whether one plays");
        Has(xr, "xr.cpp", "quad.subImage.imageRect = {{(int32_t)film.x, (int32_t)film.y}, {(int32_t)film.w, (int32_t)film.h}};", "the quad shows the film only");
    }
    std::printf("unleashed_cinema_screen_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
