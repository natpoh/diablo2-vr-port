// props_scene.h - [weight] knock_props: the level's own small props (cups, plates, vases, books, stools...) that the held weapon and the fists knock about
// (docs/notes/knock_props.md). Everything here is plain data and arithmetic, shared by the plugin (vr/prop_scan.cpp, vr/props_world.cpp), D2R VR Settings (which
// makes the catalogue from the player's own game files, tools/settings/props_assets.cpp) and the tests (cleanroom/tests/props_scene_test.cpp).
//
// After bearstonem/d2r-pcvr (MIT), feat/knockable-props of 2026-10-22 (steamvr/props.h, steamvr/prepare_props.py): the catalogue of preset entities by id and
// position, the kinds of model a prop is, the renderer's matrices taken apart, and what a prop stands on - the same rules, in our words. Nothing of the game's is
// changed by any of it: the props are only drawn where they lie.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace propscene {

struct Vec {
    float x = 0, y = 0, z = 0;
};
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};
inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec Cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(Vec a) { return std::sqrt(Dot(a, a)); }
inline Vec Norm(Vec a) { const float l = Len(a); return l > 1e-12f ? a * (1.0f / l) : Vec{}; }
inline bool Finite(Vec v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
inline Vec Rotate(Quat q, Vec v) {
    const Vec u{q.x, q.y, q.z};
    const Vec t = Cross(u, v) * 2.0f;
    return v + t * q.w + Cross(u, t);
}
// The rotation whose columns are the three axes (right-handed, orthonormal).
inline Quat FromAxes(Vec x, Vec y, Vec z) {
    const float tr = x.x + y.y + z.z;
    Quat q;
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s};
    } else if (x.x > y.y && x.x > z.z) {
        const float s = std::sqrt(1.0f + x.x - y.y - z.z) * 2.0f;
        q = {0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
    } else if (y.y > z.z) {
        const float s = std::sqrt(1.0f + y.y - x.x - z.z) * 2.0f;
        q = {(y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s};
    } else {
        const float s = std::sqrt(1.0f + z.z - x.x - y.y) * 2.0f;
        q = {(z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s};
    }
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 1e-6f ? Quat{q.x / n, q.y / n, q.z / n, q.w / n} : Quat{};
}

// ---- What a prop is: its model's name -> a shape and a material ----
enum class Shape : uint8_t { Round, Box, Ball };       // = D2RP_SHAPE_*
enum class Material : uint8_t { Ceramic, Glass, Metal, Wood, Paper, Bone, Wicker, Food, Cloth, Furniture, Stone, Sack };   // = D2RP_MAT_*
constexpr unsigned kShapes = 3, kMaterials = 12;
struct KindDef {
    const char* name;
    Shape shape;
    Material material;
};
// The model names (without their number, lower case) that are knockable props. Candles, lanterns and lamps are not: their flames are separate entities.
// (bearstonem's KINDS, prepare_props.py: 101 names; and the 96 of his catalogue version 4, 508c5d0 on feat/more-knockable-props, MIT: what lies about in
// houses, barns, camps and dungeons - groups modelled as one, wall-mounted pieces, candles and lanterns stay out; anything over kMaxExtent stays put.)
inline const std::vector<KindDef>& Kinds() {
    static const std::vector<KindDef> k = [] {
        std::vector<KindDef> v;
        auto add = [&](std::initializer_list<const char*> names, Shape s, Material m) { for (const char* n : names) v.push_back({n, s, m}); };
        add({"cup", "mug", "plate", "plate_small", "act1_plate", "act2_plate", "offering_plate", "serving_platter", "plate_stack", "bowl_set", "bowl_stack", "bowl_stack_small",
             "stack_o_bowls", "fruit_bowl", "bowl", "bowl_angled", "dish", "jug", "stoneware_jug", "pitcher", "jar", "canopic_jar", "urn", "act2_urn", "expansion_urn",
             "offering_urn", "pot", "town_pot", "chamber_pot", "vase", "vase_small", "vase_white", "vase_purple", "vase_red", "vase_brown", "vase_blue"},
            Shape::Round, Material::Ceramic);
        add({"bottle", "bottle_small", "bottle_green", "bottle_brown", "bottle_glass", "bottle_flower"}, Shape::Round, Material::Glass);
        add({"pail", "bucket", "pan", "kettle"}, Shape::Round, Material::Metal);
        add({"knife", "tool_knife", "tool_spoon", "hammer", "shovel"}, Shape::Box, Material::Metal);
        add({"barrel"}, Shape::Round, Material::Wood);
        add({"book", "book_green", "book_red", "book_orange", "book_open", "arcane_book", "scrolls", "book_stack", "package_brown", "package_yellow", "package_white"},
            Shape::Box, Material::Paper);
        add({"skull", "skull_single", "animal_skull", "skull_animal", "skull_ornate", "bone", "shinbone"}, Shape::Box, Material::Bone);
        add({"basket", "basket_woven", "basket_dirty", "act3_basket", "plate_woven", "basket_stack"}, Shape::Round, Material::Wicker);
        add({"apple", "potato"}, Shape::Ball, Material::Food);
        add({"fish"}, Shape::Box, Material::Food);
        add({"pillow", "pillow_blue", "pillow_red", "pillow_green", "pillow_purple", "cloth_folded", "crumpled_cloth", "rag"}, Shape::Box, Material::Cloth);
        add({"crate", "container", "figurine_box", "firewood", "firewood_log"}, Shape::Box, Material::Wood);
        add({"chair", "stool", "act1_stool", "barstool"}, Shape::Box, Material::Furniture);
        add({"tablet", "tablet_small"}, Shape::Box, Material::Stone);
        add({"sack", "sack_small", "hay_bale"}, Shape::Box, Material::Sack);
        // bearstonem/d2r-pcvr 508c5d0 (catalogue version 4)
        add({"l_barrel", "r_barrel", "global_barrel", "global_barrel01_open", "global_barrel02_open", "burned_barrel", "barrel_broken", "cell_barrel", "drum", "wheel",
             "wheel_broken", "wagonwheel"},
            Shape::Round, Material::Wood);
        add({"chair_broken", "stoolframe", "stool_frame", "bench", "act1_bench"}, Shape::Box, Material::Furniture);
        add({"wood_board_broken", "burned_board", "firewood_a", "wood_log", "log", "burnt_log", "cutting_board", "sticks_bundled", "crate01_open", "crate02_open", "quiver",
             "bow", "crossbow", "arrows", "arrow", "club_devilkin", "axe_handle", "shield_wood"},
            Shape::Box, Material::Wood);
        add({"sword", "sword_rusty", "sword_corrupted", "sword_bare", "short_sword", "bastard_sword", "claymore_sword", "dagger", "axe", "axe_crude", "battle_axe",
             "great_axe", "large_axe", "spear", "spear_corrupted", "fishing_spear", "mace", "shield", "shield_rusty", "shield_corrupted", "shield_fallen", "kite_shield",
             "buckler_shield", "body_shield", "armor_shoulderpad_fallen", "tongs", "tools", "tool", "tool_cleaver", "tool_sharpener", "mining_tools", "rake", "pitchfork"},
            Shape::Box, Material::Metal);
        add({"armor_helm", "armor_helm_cap", "overseer_helmet"}, Shape::Round, Material::Metal);
        add({"bowl_broken", "plate_broken", "vase_brown_broken", "vase_blue_broken", "vase_red_broken", "canopic_jar_broken"}, Shape::Round, Material::Ceramic);
        add({"skull_fragment", "long_skull", "short_skull", "baboon_skull", "skull_mandible", "skull_animal_mandible", "rib_cage", "horn"}, Shape::Box, Material::Bone);
        add({"onion"}, Shape::Ball, Material::Food);
        add({"meat", "fish_bunch"}, Shape::Box, Material::Food);
        add({"bag", "sack_striped"}, Shape::Box, Material::Sack);
        add({"act3_basket01_open", "act3_basket02_open", "act3_basket03_open", "lobster_pot", "fishing_trap"}, Shape::Round, Material::Wicker);
        add({"figurine"}, Shape::Box, Material::Stone);
        return v;
    }();
    return k;
}
// The kind of a model file ("data/hd/env/model/act1/props/cup_02.model"): its name without the folder, ".model" and a trailing number; -1 when it is no prop.
inline int KindOfModel(const std::string& path) {
    std::string s = path;
    for (char& c : s) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const size_t slash = s.find_last_of('/');
    if (slash != std::string::npos) s = s.substr(slash + 1);
    const std::string ext = ".model";
    if (s.size() <= ext.size() || s.compare(s.size() - ext.size(), ext.size(), ext) != 0) return -1;
    s.resize(s.size() - ext.size());
    size_t end = s.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(s[end - 1]))) --end;
    if (end < s.size()) {   // "_02" or "02"
        if (end > 0 && s[end - 1] == '_') --end;
        s.resize(end);
    }
    const auto& k = Kinds();
    for (size_t i = 0; i < k.size(); ++i)
        if (s == k[i].name) return static_cast<int>(i);
    return -1;
}

// ---- The catalogue (d2r_vr_props/props.bin beside the plugin): every catalogued preset entity by its id and its place in the preset ----
// Layout (little-endian): u32 magic 'PRP1', u32 format 1, u32 kinds, u32 entries; per kind 28 bytes of name, u8 shape, u8 material, u16 0; per entry u32 id, 3 x f32 the
// preset position, u16 kind, u16 flags (bit 0: the entity is a prefab's; 0 in bearstonem's) - entries sorted by id. (bearstonem's layout, so either installer's file
// reads in both.)
constexpr uint32_t kCatalogMagic = 0x31505250u, kCatalogFormat = 1;
// A preset's position comes through the renderer's local matrix exactly; this only takes in the float noise.
constexpr float kMatchUnits = 1e-3f;
struct CatalogKind {
    char name[28] = {};
    Shape shape = Shape::Round;
    Material material = Material::Ceramic;
};
struct Entry {
    uint32_t id = 0;
    float x = 0, y = 0, z = 0;
    uint16_t kind = 0;
    uint16_t flags = 0;
};
constexpr uint16_t kEntryPrefab = 1;
class Catalog {
    std::vector<Entry> entries_;
    std::vector<CatalogKind> kinds_;

public:
    // False for a file that is not one; nothing is kept then.
    bool Parse(const uint8_t* data, size_t size) {
        entries_.clear();
        kinds_.clear();
        auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, data + at, 4); return v; };
        if (!data || size < 16 || u32(0) != kCatalogMagic || u32(4) != kCatalogFormat) return false;
        const size_t k = u32(8), n = u32(12);
        if (!k || k > 255 || n > (1u << 20) || size != 16 + k * 32 + n * 20) return false;
        std::vector<CatalogKind> kinds(k);
        std::vector<Entry> entries(n);
        for (size_t i = 0; i < k; ++i) {
            const uint8_t* at = data + 16 + i * 32;
            std::memcpy(kinds[i].name, at, 27);
            if (at[28] >= kShapes || at[29] >= kMaterials) return false;
            kinds[i].shape = static_cast<Shape>(at[28]);
            kinds[i].material = static_cast<Material>(at[29]);
        }
        for (size_t i = 0; i < n; ++i) {
            const uint8_t* at = data + 16 + k * 32 + i * 20;
            Entry& e = entries[i];
            std::memcpy(&e.id, at, 4);
            std::memcpy(&e.x, at + 4, 4);
            std::memcpy(&e.y, at + 8, 4);
            std::memcpy(&e.z, at + 12, 4);
            std::memcpy(&e.kind, at + 16, 2);
            std::memcpy(&e.flags, at + 18, 2);
            if (e.kind >= k || !std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.z) || (i && e.id < entries[i - 1].id)) return false;
        }
        kinds_ = std::move(kinds);
        entries_ = std::move(entries);
        return true;
    }
    // The kind of the preset entity with this id at this local position, or -1: an id alone may be another preset's entity. A prefab's entity is found by its id alone
    // when no entry is at that place (the game may place a prefab's entities under the preset rather than under the prefab: then their local positions are not the
    // prefab's). how: 0 not found, 1 at its place, 2 a prefab's by its id alone, 3 the id is known but at other places (not a prefab's: not it).
    int Find(uint32_t id, const float local[3], int* how = nullptr) const {
        auto it = std::lower_bound(entries_.begin(), entries_.end(), id, [](const Entry& e, uint32_t v) { return e.id < v; });
        int prefabKind = -1;
        bool known = false;
        for (; it != entries_.end() && it->id == id; ++it) {
            if (std::fabs(it->x - local[0]) <= kMatchUnits && std::fabs(it->y - local[1]) <= kMatchUnits && std::fabs(it->z - local[2]) <= kMatchUnits) {
                if (how) *how = 1;
                return it->kind;
            }
            known = true;
            if ((it->flags & kEntryPrefab) && prefabKind < 0) prefabKind = it->kind;
        }
        if (how) *how = prefabKind >= 0 ? 2 : known ? 3 : 0;
        return prefabKind;
    }
    const CatalogKind& KindAt(unsigned i) const {
        static const CatalogKind none{};
        return i < kinds_.size() ? kinds_[i] : none;
    }
    size_t Size() const { return entries_.size(); }
    unsigned KindCount() const { return static_cast<unsigned>(kinds_.size()); }
};
// What the catalogue is made of (D2R VR Settings): (id, kind index into Kinds(), preset position) rows -> the file; duplicates (same id and position) once.
struct Row {
    uint32_t id = 0;
    int kind = -1;
    float x = 0, y = 0, z = 0;
    bool prefab = false;   // an entity of a prefab (data/hd/env/model/**.json), not of a level preset
};
inline std::vector<uint8_t> Encode(std::vector<Row> rows) {
    std::vector<uint8_t> out;
    std::vector<int> used;   // Kinds() indices that occur, sorted by name
    for (const Row& r : rows)
        if (r.kind >= 0 && std::find(used.begin(), used.end(), r.kind) == used.end()) used.push_back(r.kind);
    std::sort(used.begin(), used.end(), [](int a, int b) { return std::strcmp(Kinds()[a].name, Kinds()[b].name) < 0; });
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.id != b.id) return a.id < b.id;
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    std::vector<Row> unique;
    for (const Row& r : rows) {
        if (r.kind < 0 || !std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.z)) continue;
        if (!unique.empty() && unique.back().id == r.id && std::fabs(unique.back().x - r.x) < 1e-4f && std::fabs(unique.back().y - r.y) < 1e-4f &&
            std::fabs(unique.back().z - r.z) < 1e-4f)
            continue;
        unique.push_back(r);
    }
    if (used.empty() || used.size() > 255 || unique.size() > (1u << 20)) return out;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto putf = [&](float f) { uint32_t v; std::memcpy(&v, &f, 4); put32(v); };
    put32(kCatalogMagic);
    put32(kCatalogFormat);
    put32(static_cast<uint32_t>(used.size()));
    put32(static_cast<uint32_t>(unique.size()));
    for (int k : used) {
        char name[28] = {};
        std::memcpy(name, Kinds()[k].name, (std::min<size_t>)(27, std::strlen(Kinds()[k].name)));
        out.insert(out.end(), name, name + 28);
        out.push_back(static_cast<uint8_t>(Kinds()[k].shape));
        out.push_back(static_cast<uint8_t>(Kinds()[k].material));
        out.push_back(0);
        out.push_back(0);
    }
    for (const Row& r : unique) {
        put32(r.id);
        putf(r.x);
        putf(r.y);
        putf(r.z);
        const uint16_t idx = static_cast<uint16_t>(std::find(used.begin(), used.end(), r.kind) - used.begin());
        out.push_back(static_cast<uint8_t>(idx & 255));
        out.push_back(static_cast<uint8_t>(idx >> 8));
        out.push_back(r.prefab ? static_cast<uint8_t>(kEntryPrefab) : 0);
        out.push_back(0);
    }
    return out;
}

// ---- The renderer's matrices: row vectors - rows 0-2 the model's scaled axes, row 3 its origin (render units) ----
struct Placement {
    Vec p;
    Quat q;
    Vec scale{1, 1, 1};
};
inline bool Decompose(const float m[16], Placement& out) {
    for (int i = 0; i < 16; ++i) if (!std::isfinite(m[i])) return false;
    if (std::fabs(m[3]) > 1e-4f || std::fabs(m[7]) > 1e-4f || std::fabs(m[11]) > 1e-4f || std::fabs(m[15] - 1.0f) > 1e-4f) return false;
    const Vec r[3] = {{m[0], m[1], m[2]}, {m[4], m[5], m[6]}, {m[8], m[9], m[10]}};
    const Vec s{Len(r[0]), Len(r[1]), Len(r[2])};
    if (!(s.x > 1e-4f && s.y > 1e-4f && s.z > 1e-4f && s.x < 1e4f && s.y < 1e4f && s.z < 1e4f)) return false;
    const Vec x = r[0] * (1.0f / s.x), y = r[1] * (1.0f / s.y), z = r[2] * (1.0f / s.z);
    // a mirrored or sheared model cannot be a rigid body
    if (std::fabs(Dot(x, y)) > 0.02f || std::fabs(Dot(y, z)) > 0.02f || std::fabs(Dot(x, z)) > 0.02f || Dot(Cross(x, y), z) < 0.95f) return false;
    const Vec ox = Norm(x), oz = Norm(Cross(ox, y)), oy = Cross(oz, ox);
    out = {{m[12], m[13], m[14]}, FromAxes(ox, oy, oz), s};
    return true;
}
inline void Compose(const Placement& p, float m[16]) {
    const Vec x = Rotate(p.q, {1, 0, 0}) * p.scale.x, y = Rotate(p.q, {0, 1, 0}) * p.scale.y, z = Rotate(p.q, {0, 0, 1}) * p.scale.z;
    const float v[16] = {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, p.p.x, p.p.y, p.p.z, 1};
    std::memcpy(m, v, sizeof v);
}
struct Box {
    Vec lo, hi;
};
// The world bounds of local bounds under a matrix, as the renderer keeps them.
inline Box WorldBounds(const Box& local, const float m[16]) {
    const Vec c = (local.lo + local.hi) * 0.5f, e = (local.hi - local.lo) * 0.5f;
    Vec wc{m[12], m[13], m[14]}, we{};
    const float* cc = &c.x;
    const float* ee = &e.x;
    float* oc = &wc.x;
    float* oe = &we.x;
    for (int k = 0; k < 3; ++k)
        for (int r = 0; r < 3; ++r) {
            oc[k] += cc[r] * m[r * 4 + k];
            oe[k] += ee[r] * std::fabs(m[r * 4 + k]);
        }
    return {wc - we, wc + we};
}
inline bool Near(const Box& a, const Box& b, float tolerance) {
    const float* x = &a.lo.x;
    const float* y = &b.lo.x;
    for (int i = 0; i < 6; ++i) if (!(std::fabs(x[i] - y[i]) <= tolerance)) return false;
    return true;
}

// ---- What a prop stands on ----
// The floor, another prop (its own body holds it: nothing is added), or a plane at y over the footprint (x0, z0)-(x1, z1). A guessed plane is one the bounds cannot tell:
// a mantle or a ledge that is part of a building. Units: whatever the boxes are in.
struct Support {
    bool floor = true;
    float y = 0, x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    bool guessed = false, onProp = false;
    bool operator==(const Support& o) const {
        return floor == o.floor && y == o.y && x0 == o.x0 && z0 == o.z0 && x1 == o.x1 && z1 == o.z1 && guessed == o.guessed && onProp == o.onProp;
    }
};
constexpr float kFloorTouch = 0.05f, kAboveTop = 0.03f, kSideTolerance = 0.01f;   // m
constexpr float kSupportReach = 1.5f;   // m either side of the prop at most
constexpr float kLargeSupport = 1.6f;   // m: bounds this wide both ways are a room or a building, not furniture - what stands inside them is on something unknown
constexpr float kGuessMargin = 0.03f;   // m: a guessed plane reaches this far past the prop
constexpr float kLedgeHeight = 0.015f, kLedgeGap = 0.3f;   // m: guessed planes this close join into one
// Among the bounds of the things round a prop (not props), the most particular one under it: the smallest footprint holding its centre whose height range holds its
// bottom. Its surface is that thing's top, or the prop's own bottom where that is lower (a shelf inside a cabinet, a prop sunk a little in): never above the prop. Inside a
// room or a building, or with nothing found, the plane is guessed just past the prop's footprint at its bottom, until JoinLedges widens it.
inline Support FindSupport(const Box& prop, const std::vector<Box>& around, float floor, float unitsPerMetre) {
    const float u = unitsPerMetre, bottom = prop.lo.y;
    const float cx = (prop.lo.x + prop.hi.x) * 0.5f, cz = (prop.lo.z + prop.hi.z) * 0.5f;
    if (bottom <= floor + kFloorTouch * u) return {true, floor};
    const float own = (prop.hi.x - prop.lo.x) * (prop.hi.z - prop.lo.z);
    const Box* best = nullptr;
    float bestArea = 0;
    for (const Box& b : around) {
        const float area = (b.hi.x - b.lo.x) * (b.hi.z - b.lo.z);
        if (!(area >= own * 0.8f) || cx < b.lo.x - kSideTolerance * u || cx > b.hi.x + kSideTolerance * u || cz < b.lo.z - kSideTolerance * u ||
            cz > b.hi.z + kSideTolerance * u || bottom < b.lo.y - kSideTolerance * u || bottom > b.hi.y + kAboveTop * u)
            continue;
        if (!best || area < bestArea) { best = &b; bestArea = area; }
    }
    if (!best || (std::min)(best->hi.x - best->lo.x, best->hi.z - best->lo.z) > kLargeSupport * u) {
        const float m = kGuessMargin * u, y = best ? (std::min)(bottom, best->hi.y) : bottom;
        Support s{false, (std::max)(y, floor), prop.lo.x - m, prop.lo.z - m, prop.hi.x + m, prop.hi.z + m};
        s.guessed = true;
        return s;
    }
    const float reach = kSupportReach * u, y = (std::min)(bottom, best->hi.y);
    return {false, (std::max)(y, floor), (std::max)(best->lo.x, cx - reach), (std::max)(best->lo.z, cz - reach), (std::min)(best->hi.x, cx + reach), (std::min)(best->hi.z, cz + reach)};
}
// Props side by side on the same unseen surface (books along a mantle): their guessed planes join into one over the row, so each falls at the row's edge.
inline void JoinLedges(std::vector<Support>& supports, float unitsPerMetre) {
    const size_t n = supports.size();
    std::vector<size_t> group(n);
    for (size_t i = 0; i < n; ++i) group[i] = i;
    auto root = [&](size_t i) { while (group[i] != i) i = group[i] = group[group[i]]; return i; };
    const float h = kLedgeHeight * unitsPerMetre, gap = kLedgeGap * unitsPerMetre;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j) {
            const Support& a = supports[i];
            const Support& b = supports[j];
            if (!a.guessed || !b.guessed || std::fabs(a.y - b.y) > h) continue;
            const float dx = (std::max)({0.0f, b.x0 - a.x1, a.x0 - b.x1}), dz = (std::max)({0.0f, b.z0 - a.z1, a.z0 - b.z1});
            if (dx <= gap && dz <= gap) group[root(i)] = root(j);
        }
    std::vector<Support> joined(supports);
    for (size_t i = 0; i < n; ++i) {
        if (!supports[i].guessed) continue;
        Support& r = joined[root(i)];
        const Support& s = supports[i];
        r.x0 = (std::min)(r.x0, s.x0); r.z0 = (std::min)(r.z0, s.z0); r.x1 = (std::max)(r.x1, s.x1); r.z1 = (std::max)(r.z1, s.z1); r.y = (std::min)(r.y, s.y);
    }
    for (size_t i = 0; i < n; ++i) if (supports[i].guessed) supports[i] = joined[root(i)];
}
// Does a prop sit on another (a cup on a plate, anything on a barrel)? Its top at the prop's bottom, its footprint round the prop's centre.
inline bool RestsOn(const Box& prop, const Box& other, float unitsPerMetre) {
    const float u = unitsPerMetre;
    const float cx = (prop.lo.x + prop.hi.x) * 0.5f, cz = (prop.lo.z + prop.hi.z) * 0.5f;
    return cx >= other.lo.x - kSideTolerance * u && cx <= other.hi.x + kSideTolerance * u && cz >= other.lo.z - kSideTolerance * u && cz <= other.hi.z + kSideTolerance * u &&
           other.hi.y >= prop.lo.y - kAboveTop * u && other.hi.y <= prop.lo.y + 0.02f * u && other.lo.y < prop.lo.y - 0.005f * u;
}

// ---- Limits ----
constexpr unsigned kMaxSeen = 160;   // the nearest props in the world, frozen until touched
constexpr float kMaxExtent = 1.2f, kMinExtent = 0.003f;   // m: a prop's largest side (a chair) and smallest
constexpr float kPropReach = 3.0f;   // m round the hero: the props in the world
constexpr float kBelow = 0.5f, kAbove = 2.5f;   // m below / above the floor under the hero

}  // namespace propscene
