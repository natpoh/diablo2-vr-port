// world_holes_file.h - [holes] patch_world: the places whose holes get patched, their files' paths and the measures file D2R VR Settings writes
// (tools/settings/holes_assets.cpp, shared/world_holes_list.h) and the plugin reads (vrcam engitem::HolesFrame). Standard C++ only: the plugin
// takes this and nothing of the makers'.
#pragma once

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace holeslist {

struct Place {
    const char* name;     // its units' and the log's name
    const char* preset;   // the level preset
    float x, z;           // a point in its hole, preset units (the survey's)
    const char* model;    // the patch's model, under data/hd/env/model/, without "_lod<n>.model"
};
// The bridge out of the Rogue Encampment (Serg 2026-11-09, act1_bridge_holes.webp: "на мосту дыры") and the same bridge's twin in Tristram. Their
// paths: the cathedral's catacomb iterations, a developer's work folder - named by the LOD table only (2026-11-09 scan of the 7742 JSON files:
// 1542 env models so; these never by a file of ours).
constexpr Place kPlaces[] = {
    {"act1_camp_bridge", "data/hd/env/preset/act1/town/townE1.json", 520.0f, 182.0f, "act1/cathedrl/catacombIterations/catFlatRug01"},
    {"tristram_bridge", "data/hd/env/preset/act1/tristram/tri_town4.json", 400.0f, 133.0f, "act1/cathedrl/catacombIterations/catRug01"},
};
constexpr int kPlaceCount = (int)(sizeof kPlaces / sizeof kPlaces[0]);
constexpr int kLods = 5;

constexpr const char* kDir = "data/hd/env/model/d2r_vr/holes/";
constexpr const char* kFile = "data/hd/env/model/d2r_vr/holes/holes.txt";   // the measures (one line a place made)
constexpr int kVersion = 3;   // 2: the side skirts (sides, side_left); 3: the side strips and the eyes' check (ramps, seen)

inline std::string ModelPath(const Place& p) { return std::string("data/hd/env/model/") + p.model + ".model"; }
inline std::string LodPath(const Place& p, int lod) { return std::string("data/hd/env/model/") + p.model + "_lod" + std::to_string(lod) + ".model"; }
inline std::string UnitPath(const Place& p) { return std::string(kDir) + p.name + "_unit.json"; }

// ---- the measures file --------------------------------------------------------------------------------------------------------------------------
struct Line {
    std::string name, unit;
    float anchor[3] = {}, witness[3] = {};   // two top-level pieces' places in the preset (the renderer's local places): the preset found by both
    int triangles = 0, strips = 0, skirts = 0, sides = 0;   // sides: skirts down the rim floors' other open edges (worldpatch::Sides)
    float sideLeft = 0.0f;                                  // of those edges, how long is left open (nothing under them within the reach)
    int ramps = 0;                                          // side strips: such edges carried on level to the parapet (the ramps)
    float seen = 0.0f;                                      // the eyes' check (worldpatch::SeenOpenings): units of floor edge with an opening seen
    float area = 0.0f, holeArea = 0.0f, left = 0.0f;   // the patch's, the hole's, the hole left with the patch laid in (sq units)
    std::string source, sourceMesh, technique;          // the rim floor whose material it takes
};
inline std::string Write(const std::vector<Line>& lines) {
    std::string s = "holes " + std::to_string(kVersion) + "\n";
    char b[1024];
    for (const Line& l : lines) {
        snprintf(b, sizeof b, "hole %s %s %.4f %.4f %.4f %.4f %.4f %.4f %d %d %d %.2f %.2f %.2f %s %s %s %d %.2f %d %.2f\n", l.name.c_str(), l.unit.c_str(), l.anchor[0], l.anchor[1],
                 l.anchor[2], l.witness[0], l.witness[1], l.witness[2], l.triangles, l.strips, l.skirts, l.area, l.holeArea, l.left, l.source.c_str(),
                 l.sourceMesh.empty() ? "-" : l.sourceMesh.c_str(), l.technique.empty() ? "-" : l.technique.c_str(), l.sides, l.sideLeft, l.ramps, l.seen);
        s += b;
    }
    return s;
}
inline bool Parse(const std::string& text, std::vector<Line>* out) {
    out->clear();
    std::istringstream in(text);
    std::string w;
    int version = 0;
    if (!(in >> w >> version) || w != "holes" || version != kVersion) return false;
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        Line l;
        if (!(ls >> w) || w != "hole") continue;
        if (!(ls >> l.name >> l.unit >> l.anchor[0] >> l.anchor[1] >> l.anchor[2] >> l.witness[0] >> l.witness[1] >> l.witness[2] >> l.triangles >> l.strips >>
              l.skirts >> l.area >> l.holeArea >> l.left >> l.source >> l.sourceMesh >> l.technique >> l.sides >> l.sideLeft >> l.ramps >> l.seen))
            continue;
        out->push_back(l);
    }
    return true;
}

}  // namespace holeslist
