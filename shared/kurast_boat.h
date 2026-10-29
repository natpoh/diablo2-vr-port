// kurast_boat.h - the canoe that hangs in the sky over the Kurast Docks, put down on the water ([world] kurast_boat).
//
// Found by bearstonem/d2r-pcvr (MIT), commit bed674b (steamvr/prepare_kurast_dock.py, docs/KURAST_FLYING_CANOE.md): the HD
// preset of the Act 3 town, data/hd/env/preset/act3/docktown/docktown3.json, places entity 3418903870 ("prefab014", the prefab
// pf_canoe01_07_bob.json) at (0, 50, 0). From above the camera never sees it; in first person it hangs 50 units up, rocking on
// water that is not there. Bear's installer takes the entity out; we keep the boat and only change its height.
//
// Where it belongs - the game's own files, read 2026-10-24 (game build of that day):
//   - pf_canoe01_07_bob.json places pf_canoe01_07.json at its origin, and pf_canoe01_07.json was built in WORLD coordinates: its
//     canoe (canoe01.model) at (65.33, -2.77, 210.11), the oar, sacks and bedroll round it. The placement's x = 0, z = 0 are right;
//     only its y of 50 lifts the lot. With y = 0 the canoe lies where it was built, beside the western docks, 7 units from the
//     canoe of prefab016 (58.28, 212.16) and near the gondolas of prefab009 / prefab019 - open water: the level's terrain mesh
//     starts at x 139, west of it there is only the water and the swamp floor (swamp_terrain.model at y -8).
//   - The water: act3_jungle_water_flow/docktown3.model, placed at y 0, is a flat plane at y -3.508 over the whole town.
//     canoe01.model's keel is 0.727 below its origin, so at y = 0 the keel is at -3.497: on the water. The other floating
//     canoe01 hulls of the preset are at -2.51 .. -2.61 (keels 0.2 - 0.3 above the plane).
// So the one change: "y":50.0 -> "y":0.0 in that entity's position. Every other byte of the file stays as it was (x, z, the
// orientation, the scale, the other 4988 entities): scenery only - the HD preset is what the renderer draws; the game's own
// level, its collision and paths are the legacy map's and are not in this file.
//
// What comes out is Blizzard's file with one number changed: made on the player's machine from their own game (D2R_VR_Settings,
// tools/settings/boat_assets.cpp) into the mod's own data folder, never in the repository or a build.
// cleanroom/tests/kurast_boat_test.cpp tests this.
#pragma once

#include <cstdlib>
#include <string>

namespace kurastboat {

constexpr const char* kPreset = "data/hd/env/preset/act3/docktown/docktown3.json";
constexpr const char* kPrefab = "data/hd/env/model/act3/docktown/act3_docktown_boats/prefab/pf_canoe01_07_bob.json";
constexpr const char* kId = "\"id\":3418903870,";
constexpr const char* kInSky = "\"position\":{\"x\":0.0,\"y\":50.0,\"z\":0.0}";
constexpr const char* kOnWater = "\"position\":{\"x\":0.0,\"y\":0.0,\"z\":0.0}";
constexpr double kWaterY = 0.0;    // the placement's y that puts the canoe on the water (above)
constexpr double kSkyFrom = 25.0;  // a placement this high is the boat in the sky (Bear's test too)

enum State {
    kLowered,   // out = the preset with the boat on the water
    kNoNeed,    // the boat is not in the sky in this preset (gone, or already low): nothing to change
    kRefused,   // the preset is not as known: nothing changed, why says what
};

inline size_t Count(const std::string& s, const char* what) {
    size_t n = 0;
    const size_t len = std::char_traits<char>::length(what);
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + len)) ++n;
    return n;
}

// The preset with the boat lowered. Only the file as it is known (2026-10): a Preset, the entity once, its prefab the canoe's, its
// position once and (0, 50, 0) exactly; a boat already low or not there is kNoNeed; anything else is refused with the reason.
inline State Lower(const std::string& json, std::string* out, std::string* why) {
    out->clear();
    if (json.compare(0, 1, "{") != 0 || json.find("\"type\":\"Preset\"") == std::string::npos) {
        *why = "not an HD level preset";
        return kRefused;
    }
    const size_t ids = Count(json, kId);
    if (ids == 0) return kNoNeed;   // (Bear's mod, or the game, took it out)
    if (ids != 1) {
        *why = "the boat's entity is there " + std::to_string(ids) + " times";
        return kRefused;
    }
    const size_t id = json.find(kId);
    const size_t begin = json.rfind("{\"type\":\"Entity\"", id);
    if (begin == std::string::npos || id - begin > 200) {
        *why = "the boat's entity does not begin as an entity";
        return kRefused;
    }
    size_t end = json.find("{\"type\":\"Entity\"", id);
    if (end == std::string::npos) end = json.find("],\"terrain\"", id);
    if (end == std::string::npos) {
        *why = "the boat's entity does not end";
        return kRefused;
    }
    const std::string entity = json.substr(begin, end - begin);
    if (Count(entity, kPrefab) != 1 || Count(entity, "\"prefab\":") != 1) {
        *why = "the entity is not the canoe pf_canoe01_07_bob";
        return kRefused;
    }
    if (Count(entity, "\"position\":{") != 1) {
        *why = "the boat's entity has no one position";
        return kRefused;
    }
    if (Count(entity, kInSky) != 1) {
        const size_t y = entity.find("\"y\":", entity.find("\"position\":{"));
        const double at = y == std::string::npos ? 0.0 : std::strtod(entity.c_str() + y + 4, nullptr);
        if (y != std::string::npos && at < kSkyFrom) return kNoNeed;
        *why = "the boat is placed elsewhere than (0, 50, 0)";
        return kRefused;
    }
    const size_t pos = begin + entity.find(kInSky);
    *out = json.substr(0, pos) + kOnWater + json.substr(pos + std::char_traits<char>::length(kInSky));
    return kLowered;
}

}  // namespace kurastboat
