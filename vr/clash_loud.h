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

// [weight] clash_sound_min_mps / clash_sound_gap_s: which knocks of the hero's own steel - the weapon on his own shield ([weight] shield_collide), the two
// weapons of a pair ([weight] dual_collide) - are HEARD. The pulse is not asked: every knock is felt as before. An idea from Berk (bearstonem)'s D2R VR
// Unleashed, https://github.com/bearstonem/d2r-pcvr/commit/1a11dd7 (his owner heard 128 clanks on 203 touches of a scepter on the shield; his fix: only a
// clank of 3 m/s and up, at most one every 0.5 s) - our own code, the two numbers made settings. 0 and 0 (the defaults) are as before: each site's own
// threshold (0.5 m/s the pair, 1 m/s the shield) and no gap; Bear's values are 3 and 0.5. Pure: tested in clash_loud_test.
struct GateSettings {
    float minMps = 0.0f;   // a knock under this is silent (on top of the site's own threshold); 0 = only the site's own
    float gapS = 0.0f;     // at least this long after the last knock heard before the next one is; 0 = no gap
};
constexpr float kGateMaxMps = 10.0f, kGateMaxGapS = 5.0f;
inline GateSettings Clamp(GateSettings s) {
    s.minMps = std::isfinite(s.minMps) ? std::clamp(s.minMps, 0.0f, kGateMaxMps) : 0.0f;
    s.gapS = std::isfinite(s.gapS) ? std::clamp(s.gapS, 0.0f, kGateMaxGapS) : 0.0f;
    return s;
}
// One gate for both sites (a scepter on the shield and on the other weapon is the same clatter). Ask it only for a knock the site would sound; true =
// sound it now (and the gap starts from now). One thread (the weight thread).
class Gate {
public:
    bool Pass(GateSettings s, float speedMps, uint64_t nowMs) {
        s = Clamp(s);
        if (!(speedMps >= s.minMps)) return false;   // (NaN: silent)
        const uint64_t gapMs = static_cast<uint64_t>(std::lround(s.gapS * 1000.0f));
        if (played_ && nowMs - lastMs_ < gapMs) return false;
        played_ = true;
        lastMs_ = nowMs;
        return true;
    }
    void Reset() { played_ = false; lastMs_ = 0; }

private:
    bool played_ = false;
    uint64_t lastMs_ = 0;
};

}  // namespace clashloud
