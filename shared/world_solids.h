// world_solids.h - [weight] world_contact: the level pieces' solid parts, made from their own meshes (the making: shared/world_solids_make.h). Pure: D2R VR Settings makes the catalogue from the
// player's game (tools/settings/props_assets.cpp PrepareSolids -> d2r_vr_props/solids.bin), the plugin reads it (vr/world_contact.cpp), the tests drive both.
//
// Serg, 2026-10-31: «контакт с обьектами надо найти их мещи и братть их контуры», «кусты различные надо игнорировать». The boxes round the models (and the
// game's own physics boxes in the presets, which are those boxes again: a doorway one solid box, a tree's with its crown) are solid where nothing is drawn.
// So each model's triangles (shared/granny_geometry.h) are laid in a grid of cells (kMinCellUnits, about 19 cm, or a 64th of the model's longest side), a
// cell a triangle passes through is solid, so is an empty cell walled in on all six sides (the inside of a closed wall), and the solid cells are joined
// into boxes (runs along x, then rows along y, then slabs along z) - a doorway stays open, an L-shaped corner is two boxes, a tree is its trunk.
//
// Left out (Classify / UsedMesh):
//   foliage   every model under data/hd/env/foliage/ that is not a tree: bushes, shrubs, grass, weeds, ferns, flowers, ivy, vines, saplings, roots ...
//   leaves    a tree's meshes whose name or material says leaves (its crown): only the trunk and branches are solid
//
// The catalogue: each model's parts in its own units and the bounds of the meshes they come from; and each level piece of a preset (its entity id, its place
// in the preset - as the props catalogue matches them, shared/props_scene.h) with the model(s) it may be drawn with (a ModelVariation picks one of up to
// kMaxVariants - the plugin takes the one whose bounds are the renderer's).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>


namespace worldsolids {

constexpr float kMinCellUnits = 0.7f;   // model units (about 3.65 a metre at the hero's size): ~19 cm
constexpr int kGridCells = 64;          // the longest side in at most this many cells
constexpr int kMaxParts = 96;           // boxes a model at most (coarser cells until it fits; the smallest dropped past it)
constexpr int kMaxVariants = 4;
constexpr uint16_t kIgnore = 1;         // foliage: no parts
constexpr uint16_t kNoMesh = 2;         // its file did not read: no parts
constexpr uint16_t kMovable = 4;        // a prop the props catalogue knows (shared/props_scene.h KindOfModel: jugs, pots, bottles...): knocked and grabbed, never a wall
                                        // (Serg, 0.192.15: "объекты из списка двигаемых не должны считаться стенами")

struct Part { float lo[3], hi[3]; };
struct Model {
    uint16_t flags = 0;
    float lo[3] = {}, hi[3] = {};   // the bounds of the meshes the parts come from (model units)
    float cell = 0.0f;
    std::vector<Part> parts;
};

inline std::string Lower(std::string s) {
    for (char& c : s) c = (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

// A model's flags by its path (data/hd/env/...): foliage that is not a tree is left out.
inline uint16_t Classify(const std::string& path) {
    const std::string p = Lower(path);
    if (p.find("/env/foliage/") == std::string::npos) return 0;
    const bool tree = p.find("tree") != std::string::npos || p.find("cactus") != std::string::npos || p.find("stump") != std::string::npos ||
                      p.find("banyan") != std::string::npos || p.find("cypress") != std::string::npos || p.find("palm_tree") != std::string::npos;
    const bool notTree = p.find("treeline_canopy") != std::string::npos || p.find("treewall_shadows") != std::string::npos || p.find("sapling") != std::string::npos ||
                         p.find("seedling") != std::string::npos || p.find("/act2_smallcacti/") != std::string::npos;
    return tree && !notTree ? 0 : kIgnore;
}

// ---- the catalogue file ----
constexpr uint32_t kMagic = 0x44494C53u;   // "SLID"
constexpr uint32_t kFormat = 2;
constexpr float kMatchUnits = 1e-3f;      // as the props catalogue's

struct Row {
    uint32_t id = 0;
    float x = 0, y = 0, z = 0;
    uint32_t models[kMaxVariants] = {~0u, ~0u, ~0u, ~0u};   // ~0u: none
    uint32_t flags = 0;
};
constexpr uint32_t kRowPrefab = 1;   // a prefab's entity (data/hd/env/model/**.json): found by its id alone when none is at its place (the game may place it under the preset)

// models then rows (sorted by id), little-endian:
//   u32 magic, u32 format, u32 models, u32 rows
//   per model: u16 flags, u16 parts, f32 cell, f32 lo[3], f32 hi[3], then parts x (f32 lo[3], f32 hi[3])
//   per row: u32 id, f32 x, y, z, u32 models[kMaxVariants], u32 flags
inline std::vector<uint8_t> Encode(const std::vector<Model>& models, std::vector<Row> rows) {
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.id < b.id; });
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t n) { out.insert(out.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
    const uint32_t head[4] = {kMagic, kFormat, (uint32_t)models.size(), (uint32_t)rows.size()};
    put(head, sizeof head);
    for (const Model& m : models) {
        const uint16_t f[2] = {m.flags, (uint16_t)std::min<size_t>(m.parts.size(), 0xFFFF)};
        put(f, sizeof f);
        put(&m.cell, 4);
        put(m.lo, 12);
        put(m.hi, 12);
        for (size_t i = 0; i < f[1]; ++i) { put(m.parts[i].lo, 12); put(m.parts[i].hi, 12); }
    }
    for (const Row& r : rows) { put(&r.id, 4); put(&r.x, 4); put(&r.y, 4); put(&r.z, 4); put(r.models, sizeof r.models); put(&r.flags, 4); }
    return out;
}

class Catalog {
    std::vector<Model> models_;
    std::vector<Row> rows_;

public:
    bool Parse(const uint8_t* d, size_t size) {
        models_.clear();
        rows_.clear();
        size_t at = 0;
        auto get = [&](void* p, size_t n) { if (at + n > size) return false; std::memcpy(p, d + at, n); at += n; return true; };
        uint32_t head[4];
        if (!d || !get(head, sizeof head) || head[0] != kMagic || head[1] != kFormat || head[2] > (1u << 20) || head[3] > (1u << 22)) return false;
        std::vector<Model> models(head[2]);
        for (Model& m : models) {
            uint16_t f[2];
            if (!get(f, 4) || !get(&m.cell, 4) || !get(m.lo, 12) || !get(m.hi, 12)) return false;
            m.flags = f[0];
            m.parts.resize(f[1]);
            for (Part& p : m.parts)
                if (!get(p.lo, 12) || !get(p.hi, 12)) return false;
        }
        std::vector<Row> rows(head[3]);
        for (size_t i = 0; i < rows.size(); ++i) {
            Row& r = rows[i];
            if (!get(&r.id, 4) || !get(&r.x, 4) || !get(&r.y, 4) || !get(&r.z, 4) || !get(r.models, sizeof r.models) || !get(&r.flags, 4)) return false;
            if ((i && r.id < rows[i - 1].id) || !std::isfinite(r.x + r.y + r.z)) return false;
            for (uint32_t m : r.models)
                if (m != ~0u && m >= models.size()) return false;
        }
        if (at != size) return false;
        models_ = std::move(models);
        rows_ = std::move(rows);
        return true;
    }
    // The row of the preset entity with this id at this place in its preset (the transform's local position), else a prefab's entity of that id, or null
    // (as the props catalogue finds them: an id alone may be another preset's entity). *byId: found by its id alone.
    const Row* Find(uint32_t id, const float local[3], bool* byId = nullptr) const {
        auto it = std::lower_bound(rows_.begin(), rows_.end(), id, [](const Row& r, uint32_t v) { return r.id < v; });
        const Row* prefab = nullptr;
        if (byId) *byId = false;
        for (; it != rows_.end() && it->id == id; ++it) {
            if (std::fabs(it->x - local[0]) <= kMatchUnits && std::fabs(it->y - local[1]) <= kMatchUnits && std::fabs(it->z - local[2]) <= kMatchUnits) return &*it;
            if ((it->flags & kRowPrefab) && !prefab) prefab = &*it;
        }
        if (byId) *byId = prefab != nullptr;
        return prefab;
    }
    // Does an id of the 4-byte storage name a row here at all (the storage's discovery)?
    bool Knows(uint32_t id) const {
        auto it = std::lower_bound(rows_.begin(), rows_.end(), id, [](const Row& r, uint32_t v) { return r.id < v; });
        return it != rows_.end() && it->id == id;
    }
    const Model* ModelAt(uint32_t i) const { return i < models_.size() ? &models_[i] : nullptr; }
    size_t Rows() const { return rows_.size(); }
    size_t Models() const { return models_.size(); }
    // Of a row's variants, the model whose bounds are nearest the renderer's box of the piece (its model's own, model units); null: none with parts.
    const Model* Pick(const Row& r, const float lo[3], const float hi[3], float* off) const {
        const Model* best = nullptr;
        float bestOff = 1e30f;
        for (uint32_t i : r.models) {
            const Model* m = ModelAt(i);
            if (!m) continue;
            float o = 0;
            for (int c = 0; c < 3; ++c) o = std::max({o, std::fabs(m->lo[c] - lo[c]), std::fabs(m->hi[c] - hi[c])});
            if (o < bestOff) { bestOff = o; best = m; }
        }
        if (off) *off = bestOff;
        return best;
    }
};

}  // namespace worldsolids
