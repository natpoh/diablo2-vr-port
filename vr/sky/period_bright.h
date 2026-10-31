// period_bright.h - [sky] bright_<period>: the sky's brightness for each period of the game's day, Serg's own tuning ("ты можешь мне
// поставить ползунки яркости неба для каждого времени суток я сам настрою", 2026-10-31). The game's Env Cycle names six periods (vr/sky/
// day_night.cpp env::Now: 0 end of night / dawn, 1 end of dawn / start of day, 2 day, 3 end of day / dusk, 4 end of dusk / start of night,
// 5 night); each slider is the sky's brightness (a multiplier, 1 = as the day and night curve has it) as its period begins, and across the
// period it eases to the next one's by the game's own light: from the light in this period farthest from where the next one begins
// (the period's first light while the light runs toward it; midday's peak and midnight's trough on the plateaus, where the light first
// rises or falls away) to that next light. Where each period begins is the light the game shows when it does - learned as the periods
// turn; until then the ones the game showed on PC1 (0.192.5 - 0.192.7 logs). Pure; tested in cleanroom/tests/period_bright_test.cpp.
#pragma once

#include <cmath>

namespace periodbright {

constexpr int kPeriods = 6;
// the game's light as each period began on PC1 (debug_mode's time steps, act 1): dawn 96, start of day 117, day 132, dusk 150, start of
// night 117, night 65
constexpr float kStartLight[kPeriods] = {96.0f, 117.0f, 132.0f, 150.0f, 117.0f, 65.0f};

struct State {
    float start[kPeriods] = {kStartLight[0], kStartLight[1], kStartLight[2], kStartLight[3], kStartLight[4], kStartLight[5]};
    int period = -1;   // the period last seen (-1 none yet)
    float farthest = 0.0f;  // the light in it farthest from where the next one begins
};

// The multiplier now: `period` 0..5 as the game has it, `light` its light, `bright` the six sliders. A period out of range: 1.
inline float Mul(State& s, int period, float light, const float bright[kPeriods]) {
    if (period < 0 || period >= kPeriods) return 1.0f;
    if (period != s.period) {
        if (s.period >= 0) s.start[period] = light;   // (a turn seen: where this period begins; the first one seen may be mid-period)
        s.period = period;
        s.farthest = light;
    }
    const int next = (period + 1) % kPeriods;
    const float to = s.start[next];
    // the day and the night: the light first rises to midday (falls to midnight) past where the next one begins - only its way back from
    // there eases (the day's toward dusk from above, the night's toward dawn from below); on the other side the period's own
    if (period == 2 || period == 5) {
        const bool beyond = period == 2 ? light > to : light < to;
        const bool cameFromBeyond = period == 2 ? s.farthest > to : s.farthest < to;
        if (!beyond) {
            if (cameFromBeyond) return bright[next];   // (back at where the next one begins: its own until it does)
            s.farthest = light;
            return bright[period];
        }
        if (!cameFromBeyond) s.farthest = light;   // (just come round to that side)
    }
    if (std::fabs(light - to) > std::fabs(s.farthest - to)) s.farthest = light;
    const float span = s.farthest - to;
    float t = std::fabs(span) > 0.5f ? (s.farthest - light) / span : 1.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return bright[period] + (bright[next] - bright[period]) * t;
}

}  // namespace periodbright
