// bazaar_mask.h - [holes] bazaar_mask_hidden: the stray terrain-mask plane deep under Kurast Bazaar taken out of the scenery.
//
// Serg 2026-11-25 (a headset screenshot, a flat grey rectangle in the grass): «в 3 акте на базаре есть текстура которая находится ниже земли и
// её видно насквозь ... эта текстура глубоко под землей». Found by its height (the game's own presets, read 2026-11-25):
//   - data/hd/env/preset/act3/kurast/burbs08x16_1.json (a Kurast Bazaar tile) places entity 1197355067 "terrainmask_plane_01": the model
//     data/hd/env/model/global/prop/act3/kurast/terrainmask_plane.model - one flat quad, 6.92 x 6.92 units, its material "pasted__TerrainMask"
//     of the technique TerrainMaskMaterial - at (-24.67, -27.40, -8.85), scaled 2.15 x 3.99: a 14.9 x 27.6 unit plane lying 23 units under the
//     tile's ground (its pieces' median height -4.5) and outside the tile's own pieces (the nearest, a water plane, 18.7 units away). Nothing
//     sits on it or near it: a leftover of the level's build. The mask's material is not hidden by the ground in front of it, so in first
//     person the plane shows through the grass as a flat grey sheet (the view from above never looks out there).
//   - The only other use of the model in the game, BorderMetroS1.json (Upper Kurast), lies at its tile's ground beside a wall (-4.9, the
//     wall 2.3 units away): the designers' own, left alone.
// The change: that one entity cut out of the file's text; every other byte stays (its other 173 entities, the terrain, the dependency list).
// Scenery only - the HD preset is what the renderer draws; the level's collision and paths are the legacy map's. Made by D2R_VR_Settings from
// the player's own game (tools/settings/bazaar_assets.cpp) into the mod's own data folder, never in the repository or a build.
// cleanroom/tests/bazaar_mask_test.cpp tests this.
#pragma once

#include <cstdlib>
#include <string>

namespace bazaarmask {

constexpr const char* kPreset = "data/hd/env/preset/act3/kurast/burbs08x16_1.json";
constexpr const char* kModel = "data/hd/env/model/global/prop/act3/kurast/terrainmask_plane.model";
constexpr const char* kId = "\"id\":1197355067,";
constexpr double kDeepBelow = -15.0;   // the plane's height must be under this (the tile's ground is at about -4.5): never a mask at the ground

enum State {
    kRemoved,   // out = the preset without the plane
    kNoNeed,    // the plane is not there (taken out already, or a newer game without it): nothing to change
    kRefused,   // the preset is not as known: nothing changed, why says what
};

inline size_t Count(const std::string& s, const std::string& what) {
    size_t n = 0;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + what.size())) ++n;
    return n;
}

// The end (one past its '}') of the JSON object that starts at `begin` ('{'), strings and their escapes skipped; npos when it does not close.
inline size_t ObjectEnd(const std::string& s, size_t begin) {
    int depth = 0;
    bool str = false;
    for (size_t i = begin; i < s.size(); ++i) {
        const char c = s[i];
        if (str) {
            if (c == '\\') ++i;
            else if (c == '"') str = false;
            continue;
        }
        if (c == '"') str = true;
        else if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') {
            if (--depth == 0) return c == '}' ? i + 1 : std::string::npos;
            if (depth < 0) return std::string::npos;
        }
    }
    return std::string::npos;
}

// The preset without the stray plane. Only the file as it is known: a Preset, the entity once, named terrainmask_plane_01, its model the
// terrain-mask plane, one position deep under the ground; gone is kNoNeed; anything else is refused with the reason.
inline State Remove(const std::string& json, std::string* out, std::string* why) {
    out->clear();
    if (json.empty() || json[0] != '{' || json.find("\"type\":\"Preset\"") == std::string::npos) {
        *why = "not an HD level preset";
        return kRefused;
    }
    const size_t ids = Count(json, kId);
    if (ids == 0) return kNoNeed;
    if (ids != 1) {
        *why = "the plane's entity is there " + std::to_string(ids) + " times";
        return kRefused;
    }
    const size_t id = json.find(kId);
    const size_t begin = json.rfind("{\"type\":\"Entity\"", id);
    if (begin == std::string::npos || id - begin > 200) {
        *why = "the plane's entity does not begin as an entity";
        return kRefused;
    }
    const size_t end = ObjectEnd(json, begin);
    if (end == std::string::npos) {
        *why = "the plane's entity does not end";
        return kRefused;
    }
    const std::string entity = json.substr(begin, end - begin);
    if (Count(entity, "\"name\":\"terrainmask_plane_01\"") != 1 || Count(entity, std::string("\"filename\":\"") + kModel + "\"") != 1 ||
        Count(entity, "\"filename\":") != 1) {
        *why = "the entity is not the terrain-mask plane";
        return kRefused;
    }
    const size_t pos = entity.find("\"position\":{");
    if (pos == std::string::npos || Count(entity, "\"position\":{") != 1) {
        *why = "the plane has no one position";
        return kRefused;
    }
    const size_t y = entity.find("\"y\":", pos);
    const double at = y == std::string::npos ? 0.0 : std::strtod(entity.c_str() + y + 4, nullptr);
    if (y == std::string::npos || !(at < kDeepBelow)) {
        *why = "the plane is not deep under the ground (y " + std::to_string(at) + ")";
        return kRefused;
    }
    // cut with one comma: the one after it, or (the list's last) the one before it
    size_t from = begin, to = end;
    if (to < json.size() && json[to] == ',') ++to;
    else if (from > 0 && json[from - 1] == ',') --from;
    else if (!(from > 0 && json[from - 1] == '[' && to < json.size() && json[to] == ']')) {
        *why = "the plane's entity is not in a list";
        return kRefused;
    }
    *out = json.substr(0, from) + json.substr(to);
    return kRemoved;
}

}  // namespace bazaarmask
