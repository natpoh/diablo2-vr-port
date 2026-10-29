// clash_loud.h - [weight] clash_sound: how loud a knock of steel on steel is, and how hard its pulse - by how fast the two met. One rule for the two
// weapons of a pair striking each other ([weight] dual_collide, vr/weight.cpp) and for a parry's clash (a monster's weapon meeting the weapon or the
// shield, vr/parry_wire.cpp). The user, 2026-10-24: «со стальным звуком, чем сильней стукнулись, тем сильней звук».
//
// Under kQuietMps nothing - no sound, no pulse (a weapon laid on the other, slid along it, a touch). From there the strength rises evenly to 1 at kFullMps
// (a hard blow: the user's swings are 2-6 m/s, the closing speed of two blades a little under the faster one's). The sound is the game's own block
// sound at that share of its own loudness (the game's volume 0-255, 255 = as loud as the game plays it), never quite silent once heard; the pulse from
// 0.6 (the PS VR2's motor shows little under it) to 1.0, longer as well. Pure: tested in clash_loud_test.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace clashloud {

constexpr float kQuietMps = 0.5f;   // under this no sound and no pulse
constexpr float kFullMps = 4.0f;    // this fast and over: the game's full block sound, the hardest pulse
constexpr float kQuietest = 0.15f;  // the share of the game's loudness at kQuietMps

struct Clash {
    bool heard = false;       // over kQuietMps: sounded and felt
    float strength = 0.0f;    // 0 at kQuietMps .. 1 at kFullMps
    float volume = 0.0f;      // the share of the game's own loudness, kQuietest .. 1 (0 when not heard)
    uint32_t gameVolume = 0;  // the same as the game takes it: 0 .. 255
    float pulse = 0.0f;       // the pulse's amplitude, 0.6 .. 1 (0 when not heard)
    int pulseMs = 0;          // its length, 35 .. 70 ms
};

// speedMps: how fast the two met (the closing speed, metres a second).
inline Clash Of(float speedMps) {
    Clash c;
    if (!std::isfinite(speedMps) || speedMps < kQuietMps) return c;
    c.heard = true;
    c.strength = std::clamp((speedMps - kQuietMps) / (kFullMps - kQuietMps), 0.0f, 1.0f);
    c.volume = kQuietest + (1.0f - kQuietest) * c.strength;
    c.gameVolume = static_cast<uint32_t>(std::lround(255.0f * c.volume));
    c.pulse = 0.6f + 0.4f * c.strength;
    c.pulseMs = static_cast<int>(std::lround(35.0f + 35.0f * c.strength));
    return c;
}

}  // namespace clashloud
