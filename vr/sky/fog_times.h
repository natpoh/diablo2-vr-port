// fog_times.h - [fog] game_fog's colour by the game's time of day (Serg 2026-11-09: "наш туман всегда одного цвета а ночью он должен быть
// темным"; "сделай мне отдельную страницу с ползунками нового тумана, чтобы старый не ломать"; "а почему туман только дневной и ночной, а
// нет вечернего").
//   [fog] game_fog_colour=0 (the default): the act's colours of the game's fog by time of day - [fog] game_fog_<act>_dawn / _day / _dusk /
//     _night (Settings > Game fog), display colours as the old fog's; the game's six lighting files take them by their names (dawn 1 and
//     dawn 2 the dawn's, the day the day's, dusk 1 and dusk 2 the dusk's, the night the night's), blended by the game's own time and weights
//     (Weights), as it blends its files. Empty: the day's = the old fog's colour of the act ([fog] color_<act>); the night's = the day's times
//     [sky] night_brightness (the old fog's night); the dawn's and the dusk's = the day's times the game's own change from its day colour
//     to that file's (kGame, the act's lighting files' depthFogColor), per channel - our brightness, the game's cast - each file its own;
//   =1: the game's own colour as its files hold it, untouched (with our near, dense distances a pale wall by day and at night).
// The old effect fog's keys and code are not touched. Pure; tested in cleanroom/tests/sky_time_test.cpp.
#pragma once

#include <cmath>

namespace fogtimes {

constexpr int kPeriods = 6;   // the game's lighting files, in its order (its VisualDataSystem's +0x190)
constexpr const char* kPeriodNames[kPeriods] = {"dawn 1", "dawn 2", "day", "dusk 1", "dusk 2", "night"};
enum Group { kDawn = 0, kDay = 1, kDusk = 2, kNight = 3, kGroups = 4 };
constexpr const char* kGroupKeys[kGroups] = {"dawn", "day", "dusk", "night"};
// Which of the four colours each lighting file takes, by its name
inline int GroupOf(int period) { return period <= 1 ? kDawn : period == 2 ? kDay : period <= 4 ? kDusk : kNight; }

// The game's own depth fog colours, bytes as each act's open-air lighting files hold them (data/hd/env/vis/<files>_<period>.json
// depthFogColor x 255): act1_outdoors, act2_town (the desert too), act3_docktown (Kurast, the jungle), act4_mesa (black: no change to
// take), expansion_siege (Harrogath, the ruins, the snow)
constexpr unsigned char kGame[7][kPeriods][3] = {
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{37, 37, 48}, {98, 107, 125}, {255, 255, 255}, {127, 127, 127}, {197, 142, 156}, {77, 94, 128}},
    {{52, 77, 128}, {208, 208, 167}, {208, 208, 167}, {208, 208, 167}, {238, 167, 167}, {75, 133, 255}},
    {{86, 78, 77}, {170, 158, 147}, {103, 94, 107}, {104, 95, 108}, {52, 46, 84}, {102, 125, 136}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{87, 86, 106}, {198, 171, 122}, {109, 97, 88}, {135, 97, 87}, {81, 79, 104}, {90, 103, 147}},
    {{87, 86, 106}, {198, 171, 122}, {109, 97, 88}, {135, 97, 87}, {81, 79, 104}, {90, 103, 147}},
};
// The game's change from its day colour to `period`'s, per channel (0 .. 4; a channel black by day, or no act: 1)
inline void GameChange(int act, int period, float out[3]) {
    for (int c = 0; c < 3; ++c) {
        out[c] = 1.0f;
        if (act < 1 || act > 6 || period < 0 || period >= kPeriods) continue;
        const float d = kGame[act][2][c] / 255.0f, p = kGame[act][period][c] / 255.0f;
        if (d > 0.02f) out[c] = p / d < 4.0f ? p / d : 4.0f;
    }
}

// The game's weights for its time (0xF5D603): the period's file 1, the one before it (fraction under 0.5) or after it (over 0.5)
// |fraction - 0.5|, wrapping - normalised. `next` -1 when the fraction is at the middle. False when the time is out of range.
inline bool Weights(int period, float frac, int* next, float* own, float* other) {
    if (period < 0 || period >= kPeriods || !(frac >= 0.0f && frac <= 1.0f)) return false;
    float w = 0.0f;
    *next = -1;
    if (frac < 0.5f) { w = 0.5f - frac; *next = period == 0 ? kPeriods - 1 : period - 1; }
    else if (frac > 0.5f) { w = frac - 0.5f; *next = period == kPeriods - 1 ? 0 : period + 1; }
    *own = 1.0f / (1.0f + w);
    *other = w / (1.0f + w);
    return true;
}

// The act's colours (any space, as long as all are in it) as set: `set[g]` false = empty
struct Set { float colour[kGroups][3]; bool set[kGroups]; };
// Each lighting file's colour: its group's if set, else the defaults above (`day` already the day's - set or the old fog's; `nightDefault`
// the day's times night_brightness, in the same space)
inline void FileColours(int act, const Set& s, const float day[3], const float nightDefault[3], float out[kPeriods][3]) {
    for (int p = 0; p < kPeriods; ++p) {
        const int g = GroupOf(p);
        float k[3];
        GameChange(act, p, k);
        for (int c = 0; c < 3; ++c)
            out[p][c] = s.set[g] ? s.colour[g][c] : g == kDay ? day[c] : g == kNight ? nightDefault[c] : day[c] * k[c];
    }
}
// The colour now by the game's time from the six files' colours; false (out untouched) when the time is out of range
inline bool ColourNow(int period, float frac, const float files[kPeriods][3], float out[3]) {
    int next = -1;
    float own = 1.0f, other = 0.0f;
    if (!Weights(period, frac, &next, &own, &other)) return false;
    for (int c = 0; c < 3; ++c) out[c] = files[period][c] * own + (next >= 0 ? files[next][c] * other : 0.0f);
    return true;
}

}  // namespace fogtimes
