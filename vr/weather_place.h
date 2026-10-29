// weather_place.h - [sky] weather_first_person: the game's weather (rain, snow) in the first-person views (VR F4, flat F3).
//
// What the game does (read in the decrypted image, dump_ingame/D2R_image.bin): its weather system loads data/hd/env/weather/weather_effects.json
// (a WeatherEffectMap: rain -> data/hd/vfx/particles/env/common/weather/Rain.particles, snow -> .../Snow.particles, the splashes) and keeps the
// effect in an entity of its own, the entity's id at +0x64 of the system. Every update (0x5F2330 in that image) it asks its camera for the
// camera's place (0x8CE310), makes a matrix of it - no turn, no scale - and writes it into the entity's TransformComponent: the local matrix at
// +0x00 and, the entity having no parent (+0xC0 & 0xFFFFF = 0xFFFFF), the world at +0x40 (last frame's world moved to +0x80 first). So the snow
// falls in a box round the game's own camera, high above and behind the hero for its view from above: in first person a box of snow hanging
// in the air (Serg, 0.188.4, the Arreat Summit: "квадрат со снежинками, который должен висеть перед экраном, но он висит где-то в воздухе").
//
// The setting, after that update, in VR F4 and flat F3 only (every other view, and 0, keep the game's place):
//   1 - hidden: the box laid far under the world (kHiddenDrop below the game's place), where nothing sees it;
//   2 - round the viewer: the box laid where the game would lay it for a camera at our head - our head's place, as the game uses its own.
// Visual only: the weather is an effect and nothing more; no unit, no rule of the game is touched.
// The game's side (the hook) is in vr/vrcam.cpp (namespace engitem, the weather part); the test is cleanroom/tests/weather_place_test.cpp.
#pragma once

#include <cmath>

namespace weatherplace {

enum Mode : int { kGame = 0, kHidden = 1, kAround = 2 };
inline int ClampMode(int m) { return m < kGame || m > kAround ? kGame : m; }

constexpr float kHiddenDrop = 100000.0f;   // world units under the game's place: past any view and any light

// Whether the weather is moved now: a mode other than the game's, in a first-person view (VR F4 or flat F3), in the world
inline bool Wanted(int mode, bool firstPerson, bool inWorld) { return ClampMode(mode) != kGame && firstPerson && inWorld; }

// Where the box goes: the game's place (its camera) for kGame, far under it for kHidden, the head for kAround. False: no finite place.
inline bool Place(int mode, const float game[3], const float head[3], float out[3]) {
    switch (ClampMode(mode)) {
    case kHidden:
        out[0] = game[0]; out[1] = game[1] - kHiddenDrop; out[2] = game[2];
        break;
    case kAround:
        out[0] = head[0]; out[1] = head[1]; out[2] = head[2];
        break;
    default:
        out[0] = game[0]; out[1] = game[1]; out[2] = game[2];
        break;
    }
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

// A matrix as the game writes it (row-major, the place in the last row: m[12..14], m[15] = 1) given a new place. False (untouched) when it is
// not such a matrix: not finite, or the last column not (0, 0, 0, 1) - the game writes no turn into it, so anything else is not its weather's.
inline bool SetPlace(float m[16], const float at[3]) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i])) return false;
    if (m[3] != 0.0f || m[7] != 0.0f || m[11] != 0.0f || m[15] != 1.0f) return false;
    m[12] = at[0];
    m[13] = at[1];
    m[14] = at[2];
    return true;
}

// The entity's parent field (TransformComponent +0xC0, its low 20 bits): none = 0xFFFFF. Only a root's local is a world place.
inline bool IsRoot(unsigned parentField) { return (parentField & 0xFFFFFu) == 0xFFFFFu; }

inline const char* ModeName(int mode) {
    switch (ClampMode(mode)) {
    case kHidden: return "hidden";
    case kAround: return "round the viewer";
    default: return "the game's place";
    }
}

}  // namespace weatherplace
