// three_skies.h - the game's dome as the act's own pictures, baked, nothing recoloured (Serg: "надо полностью отключить покраску неба,
// оставить только 3 неба: дневное, ночное и утренне-вечернее с 3 яркостями", "которые должны быть запечены"; "делай небо без подкраски"). The
// default sky since 0.192.33: three pictures, each with its own brightness ([sky] three_bright_day / _night / _dawn_dusk) - the day's (the
// game's own, the atlas), the night's (the effect's SkyNBand / SkyNCap) and one for dawn and dusk alike; act 4 (Hell: "там ночи нет") one,
// the day's, at [sky] three_bright_act4, the time of day doing nothing there. No act has a dawn / dusk picture yet: until one is made, the
// day's stands in for it at the dawn / dusk brightness.
// The game's own lighting's time of day picks the picture (VisShares below - not the world clock, which the HD world is not lit by; until
// 0.208.2 it was the clock, vr/sky/day_night.cpp env::Now: 0 end of night / dawn, 1 end of dawn / start of day, 2 day, 3 end of day /
// dusk, 4 end of dusk / start of night, 5 night - and the night picture stood over a world the game lit as day): each period's picture
// by kVisPic (by the files' own light), [sky] sky_crossfade blending them by the game's own weights, otherwise the period's whole. (SharesNow / kStartPic, the
// clock's crossfade by its light, are left for the tests.) The bake (vr/sky/dome_atlas.h: lerp(day, night,
// night share) x brightness) takes either as its two numbers, exactly; it runs only when they change.
// The fog's colour with it ([fog] fog_colour_measured unticked, the default): the dome's own at the horizon - the pictures' 0 degree rows'
// means, as the dome lays them under the horizon (tools/game_sky/make_sky_dome.py), mixed by the same two numbers (HorizonScene).
// The tinting (DomeSeen's gain, DomeLook, dome_gain_once, bright_<period>) is parked behind [sky] dome_tint: "мы вернемся позже к покраске
// когда научимся красить без контуров вокруг деревьев" - open until the dome can be tinted with no outline round the trees. Pure; tested in
// tools/dome_atlas_test.cpp.
#pragma once

#include <cmath>
#include <cstdint>

#include "sky/period_bright.h"

namespace threeskies {

enum Pic { kDay = 0, kNight = 1, kDawnDusk = 2, kPics = 3 };
// [sky] sky_crossfade: the picture each period begins on (and eases from to the next one's)
constexpr int kStartPic[periodbright::kPeriods] = {kNight, kDawnDusk, kDay, kDay, kDawnDusk, kNight};
// as the period turns (the default): its picture whole - the dawn and the dusk the dawn / dusk picture, by their names
constexpr int kSwapPic[periodbright::kPeriods] = {kDawnDusk, kDay, kDay, kDawnDusk, kNight, kNight};
constexpr int kHellAct = 4;   // (one sky: no night there)
// The game's lighting files' pictures (VisShares), by their own light, not their names: dawn1 and night are the dark pair (ambient 0.10
// in act 1's open air and act 2's, key light 3 - 4.5 blue), dawn2 / dusk1 / dusk2 lit like each other (ambient 0.2 - 0.65, key light 5 - 7)
// and the day bright (key light 13.5 in act 1, 50 in act 2). dusk2 - "end of dusk, start of night" - is as bright as dawn2: by its
// name it took the night picture, and the night stood over a world still lit as dusk (Serg on 0.208.2 at 22:04:10, "в игре день а небо
// темное"). dawn2, dusk1 and dusk2 the dawn / dusk picture - the day's as it is until an act has one
constexpr int kVisPic[periodbright::kPeriods] = {kNight, kDawnDusk, kDay, kDawnDusk, kDawnDusk, kNight};

// Each picture's share now (they add up to 1); the time not known (period out of range): the day's
struct Shares { float pic[kPics] = {1.0f, 0.0f, 0.0f}; };
inline Shares Whole(int pic) {
    Shares out;
    out.pic[kDay] = 0.0f;
    out.pic[pic] = 1.0f;
    return out;
}
// [sky] sky_crossfade: `period` 0..5, `light` the game's
inline Shares SharesNow(periodbright::State& s, int period, float light) {
    Shares out;
    if (period < 0 || period >= periodbright::kPeriods) return out;
    const float t = periodbright::Ease(s, period, light);
    out.pic[kDay] = 0.0f;
    out.pic[kStartPic[period]] += 1.0f - t;
    out.pic[kStartPic[(period + 1) % periodbright::kPeriods]] += t;
    return out;
}
// as the period turns
inline Shares SwapShares(int period) { return period < 0 || period >= periodbright::kPeriods ? Shares{} : Whole(kSwapPic[period]); }

// The game's own lighting's time of day - what the HD world is lit by, and so what picks the sky since 0.208.3 (Serg on 0.208.2: "в игре
// день а небо темное", "наше ночное небо не совпадает с ночью в игре"). The HD renderer's VisualDataSystem keeps the time of day the
// world is lit by (+0x190 the period 0..5, +0x194 how far into it 0..1, vr/sky/lights.cpp cavelight::VisTime): made as "day, 0"
// (0xDFA440), then set every tick from the 2D day cycle (its update 0x352270 -> 0x3529E0 sends period and fraction at 0x352B83 to the
// HD side's WeatherTranslation, whose handler 0x7CD8F0 sets them) unless the game's OverrideTimeOfDay switch (+0x1A9, a developer
// remote-debug command, off) holds it; its debug panel (0xF2FF00) and console (0xE24EC0) set it too. Read here rather than the clock
// (env::Now) so the sky is whatever the lighting really is. Every frame it blends the area's six vis files - dawn1, dawn2, day, dusk1,
// dusk2, night - by it (0xF5D603): the period's own file at weight 1, and the one before it (fraction under 0.5) or after it (over 0.5)
// at |fraction - 0.5|, wrapping round. So each vis file is the look at its period's middle. The pictures by the same weights, each vis
// file's by kVisPic; `fade` off: the period's own picture whole. Out of range or not read: the day's (the lighting is made at day).
inline Shares VisShares(int period, float frac, bool fade) {
    Shares out;
    if (period < 0 || period >= periodbright::kPeriods || !(frac >= 0.0f && frac <= 1.0f)) return out;
    if (!fade) return Whole(kVisPic[period]);
    int next = period;
    float w = 0.0f;
    if (frac < 0.5f) { w = 0.5f - frac; next = period == 0 ? periodbright::kPeriods - 1 : period - 1; }
    else if (frac > 0.5f) { w = frac - 0.5f; next = period == periodbright::kPeriods - 1 ? 0 : period + 1; }
    out.pic[kDay] = 0.0f;
    out.pic[kVisPic[period]] += 1.0f / (1.0f + w);
    out.pic[kVisPic[next]] += w / (1.0f + w);
    return out;
}

// The sky's brightness now (our own sky and the open-air fog of the tinted path follow it): the pictures' brightnesses by their shares
inline float Brightness(const Shares& s, const float bright[kPics]) {
    return s.pic[kDay] * bright[kDay] + s.pic[kNight] * bright[kNight] + s.pic[kDawnDusk] * bright[kDawnDusk];
}

// The bake's two numbers (BakeIn::night, ::mul) for the shares: the day's picture weighed by its share times its brightness, the night's
// likewise - lerp(day, night, night) x mul = day x wDay + night x wNight. The dawn / dusk picture is the day's until one is made; an act
// without a night picture (`nightPicture` false) shows the day's at the night's brightness.
struct Bake { float night = 0.0f, mul = 1.0f; };
inline Bake BakeOf(const Shares& s, const float bright[kPics], bool nightPicture) {
    float wDay = s.pic[kDay] * bright[kDay] + s.pic[kDawnDusk] * bright[kDawnDusk];
    float wNight = s.pic[kNight] * bright[kNight];
    if (!nightPicture) { wDay += wNight; wNight = 0.0f; }
    Bake b;
    b.mul = wDay + wNight;
    b.night = b.mul > 1e-6f ? wNight / b.mul : 0.0f;
    return b;
}

// The dome's colour at the horizon for the bake's numbers, in the game's scene units: the day picture's horizon and the night's (scene
// units, HorizonOfRow) as the bake mixes them - the fog's colour, so the far land melts into the sky with no seam
inline void HorizonScene(const Bake& b, const float day[3], const float night[3], float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = (day[c] + (night[c] - day[c]) * b.night) * b.mul;
}

// The band picture's row at the horizon (0 degrees: v = 65 / 73 of its height, as make_sky_dome.py and the bake lay the band)
inline uint32_t HorizonRow(uint32_t height) {
    const uint32_t r = (uint32_t)(65.0f / 73.0f * (float)height);
    return height ? (r < height ? r : height - 1) : 0;
}
// The mean of one RGBA8 row of `width` texels in the game's scene units (sRGB -> linear -> Rec.2020, as make_sky_dome.py scene_values at
// brightness 1 and the bake's Scene)
inline void RowMeanScene(const uint8_t* rgba, uint32_t width, float out[3]) {
    double lin[3] = {0.0, 0.0, 0.0};
    for (uint32_t x = 0; x < width; ++x)
        for (int c = 0; c < 3; ++c) {
            const float v = rgba[x * 4 + c] / 255.0f;
            lin[c] += v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
        }
    const float n = width ? 1.0f / (float)width : 0.0f;
    const float r = (float)lin[0] * n, g = (float)lin[1] * n, b = (float)lin[2] * n;
    out[0] = 0.6274040f * r + 0.3292820f * g + 0.0433136f * b;
    out[1] = 0.0690970f * r + 0.9195400f * g + 0.0113612f * b;
    out[2] = 0.0163916f * r + 0.0880132f * g + 0.8955950f * b;
}

}  // namespace threeskies
