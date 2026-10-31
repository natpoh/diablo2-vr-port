// world_walls.h - [weight] world_contact: the walls for the weapon and the bare hands - the solid parts of the level pieces near the hero, each a box turned with
// its piece. Pure: no game calls, no globals (vr/world_contact.cpp feeds it the renderer's entities, knockprops::LevelBoxes, and the catalogue Settings made,
// shared/world_solids.h).
//
// A piece the catalogue knows (its preset entity id at its place in the preset) is drawn with one of its models: the one whose mesh bounds are the renderer's box
// of the piece. Its parts (boxes in the model's units, made from its own triangles: a doorway stays open, a tree is its trunk, foliage has none) are laid by the
// piece's world matrix as the renderer draws it; when the renderer's box is not the mesh's (a model drawn with another level of detail, a scaled bone) the parts are
// stretched from the one onto the other.
//
// A piece the catalogue does not know is no preset's piece: a game object with a model of its own (a camp's torch on its tripod, a chest, a well - Serg, 0.192.15:
// "факел перестал считаться объектом почему-то"). It is its renderer's box (one part) when it is object-sized - kMinSideM .. kObjectMaxM - and not a movable prop.
//
// Touching pieces are one body (a dry-stone wall of many stones, a palisade of posts, a fence: Serg, "надо объекты которые рядом объединять в один большой меш",
// "чтобы он не считал их разными"): each part's Box::body is the least entity id of the parts touching it, one after the other (within kTouchM) - the weight model's
// contact holds one way out of the whole body, one strike, one pulse (cleanroom/heft/walls.h Wall::body).
//
// Left out as well:
//   movable   a prop the props catalogue knows (knock props / grab props move it: Serg, "объекты из списка двигаемых не должны считаться стенами") - by its preset id and
//             place (the caller's `movable`), and every model the solids catalogue marks so (shared/world_solids.h kMovable)
//   tiny      a piece whose longest side is under kMinSideM (cups, plates, books: knock props moves those, a hand must not stop on a cup)
//   low       a part whose top is under kLowTopM over the hero's floor (the floor is not a wall)
//   high      a part whose bottom is over kHighBottomM over the floor (beams, crowns: out of reach)
//   round     a part the hero's middle (kMiddleM over his feet) is inside, widened kAroundM - he stands in it
// and of the rest the kMaxKept nearest the hero's middle are kept.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "world_solids.h"

namespace worldwalls {

constexpr float kMinSideM = 0.35f;
constexpr float kLowTopM = 0.15f;
constexpr float kHighBottomM = 2.6f;
constexpr float kMiddleM = 1.0f;
constexpr float kAroundM = 0.2f;
constexpr int kMaxKept = 256;
constexpr float kSameBoundsShare = 0.05f;   // the renderer's box and the mesh's agree within this share of the size (and kSameBoundsUnits)
constexpr float kSameBoundsUnits = 0.05f;
constexpr float kObjectMaxM = 3.0f;    // a piece not in the catalogue: its renderer's box when no side is longer
constexpr float kTouchM = 0.03f;       // parts this near are one body

// A level piece as the renderer has it: its entity, its world matrix (row vectors: rows 0-2 its axes, scaled; row 3 its place), its model's own box (render units)
// and its preset entity id (0: not known) and place under its parent.
struct Entity {
    uint32_t id = 0;
    float world[16] = {};
    float lo[3] = {}, hi[3] = {};
    uint32_t preset = 0;
    float local[3] = {};
};

// A part's box, render metres: its centre, its axes (unit), half its size along each; its lowest and highest point; the piece's entity (id: entity and part).
struct Box {
    uint32_t id = 0, entity = 0, body = 0;
    float c[3] = {};
    float axis[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float half[3] = {};
    float bottom = 0.0f, top = 0.0f;
};

enum class Why { kKept, kBad, kTiny, kLow, kHigh, kRound, kUnknown, kFoliage, kNoParts, kMovable, kBig };
constexpr int kWhys = 11;
inline const char* WhyName(Why w) {
    switch (w) {
    case Why::kKept: return "kept";
    case Why::kBad: return "not a box";
    case Why::kTiny: return "tiny";
    case Why::kLow: return "low";
    case Why::kHigh: return "high";
    case Why::kRound: return "round the hero";
    case Why::kUnknown: return "not in the catalogue";
    case Why::kFoliage: return "foliage";
    case Why::kNoParts: return "no mesh";
    case Why::kMovable: return "movable";
    case Why::kBig: return "not in the catalogue and big";
    }
    return "?";
}

// A box of the entity's own frame (lo .. hi, render units) under its world matrix, in render metres. False: not finite, or an axis squeezed to nothing.
inline bool Place(const Entity& e, const float lo[3], const float hi[3], float upm, Box* out) {
    if (!(upm > 1e-3f)) return false;
    Box b;
    b.entity = e.id;
    const float mid[3] = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
    for (int k = 0; k < 3; ++k) b.c[k] = (mid[0] * e.world[k] + mid[1] * e.world[4 + k] + mid[2] * e.world[8 + k] + e.world[12 + k]) / upm;
    for (int i = 0; i < 3; ++i) {
        const float* row = e.world + 4 * i;
        const float len = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
        if (!(len > 1e-6f) || !std::isfinite(len)) return false;
        for (int k = 0; k < 3; ++k) b.axis[i][k] = row[k] / len;
        b.half[i] = std::fabs(hi[i] - lo[i]) * 0.5f * len / upm;
    }
    float ext = 0.0f;   // half its height: the axes' reach up
    for (int i = 0; i < 3; ++i) ext += std::fabs(b.axis[i][1]) * b.half[i];
    b.bottom = b.c[1] - ext;
    b.top = b.c[1] + ext;
    for (int k = 0; k < 3; ++k)
        if (!std::isfinite(b.c[k]) || !std::isfinite(b.half[k])) return false;
    *out = b;
    return true;
}

// A point's distance outside the box widened by `grow` (<= 0: inside it).
inline float Outside(const Box& b, const float p[3], float grow) {
    float out2 = 0.0f, worst = -1e9f;
    for (int i = 0; i < 3; ++i) {
        const float l = (p[0] - b.c[0]) * b.axis[i][0] + (p[1] - b.c[1]) * b.axis[i][1] + (p[2] - b.c[2]) * b.axis[i][2];
        const float d = std::fabs(l) - b.half[i] - grow;
        worst = std::max(worst, d);
        if (d > 0) out2 += d * d;
    }
    return out2 > 0 ? std::sqrt(out2) : worst;
}

// Is a part a wall for the hero standing with his feet at `feet` (render metres)?
inline Why Judge(const Box& b, const float feet[3]) {
    if (b.top < feet[1] + kLowTopM) return Why::kLow;
    if (b.bottom > feet[1] + kHighBottomM) return Why::kHigh;
    const float middle[3] = {feet[0], feet[1] + kMiddleM, feet[2]};
    if (Outside(b, middle, kAroundM) <= 0.0f) return Why::kRound;
    return Why::kKept;
}

struct Counts {
    unsigned seen = 0, matched = 0, byId = 0, stretched = 0, objects = 0, parts = 0, kept = 0, beyond = 0, bodies = 0, joined = 0;
    unsigned why[kWhys] = {};   // pieces (unknown: laid by their box, foliage, no mesh: laid by their box, movable, big, tiny, not a box) and parts (low, high, round)
};

// The entity's longest side in metres (its renderer box under its matrix).
inline float Longest(const Entity& e, float upm) {
    float l = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float* row = e.world + 4 * i;
        l = std::max(l, std::fabs(e.hi[i] - e.lo[i]) * std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]) / upm);
    }
    return l;
}

// Two boxes touch (their bounds in the world within `gap`).
inline bool Touch(const Box& a, const Box& b, float gap) {
    for (int k = 0; k < 3; ++k) {
        float ea = 0.0f, eb = 0.0f;   // each one's reach along the world axis k
        for (int i = 0; i < 3; ++i) { ea += std::fabs(a.axis[i][k]) * a.half[i]; eb += std::fabs(b.axis[i][k]) * b.half[i]; }
        if (std::fabs(a.c[k] - b.c[k]) > ea + eb + gap) return false;
    }
    return true;
}

// Whether a preset entity at its place is a movable prop (the props catalogue's); may be null.
using MovableFn = bool (*)(uint32_t preset, const float local[3]);

// The walls of the entities for the hero's feet (render metres): each known piece's parts, an unknown object-sized piece's box, the nearest kMaxKept, each with its
// body; what was left out and why in *counts.
inline std::vector<Box> Choose(const std::vector<Entity>& es, const worldsolids::Catalog& cat, float upm, const float feet[3], Counts* counts, MovableFn movable = nullptr) {
    std::vector<Box> kept;
    Counts c;
    for (const Entity& e : es) {
        ++c.seen;
        if (!(upm > 1e-3f)) { ++c.why[(int)Why::kBad]; continue; }
        if (e.preset && movable && movable(e.preset, e.local)) { ++c.why[(int)Why::kMovable]; continue; }
        bool byId = false;
        const worldsolids::Row* row = e.preset ? cat.Find(e.preset, e.local, &byId) : nullptr;
        const worldsolids::Model* m = nullptr;
        if (row) {
            ++c.matched;
            if (byId) ++c.byId;
            float off = 0.0f;
            m = cat.Pick(*row, e.lo, e.hi, &off);
            if (m && (m->flags & worldsolids::kMovable)) { ++c.why[(int)Why::kMovable]; continue; }
            if (!m || (m->flags & worldsolids::kIgnore)) { ++c.why[(int)Why::kFoliage]; continue; }
            if ((m->flags & worldsolids::kNoMesh) || m->parts.empty()) m = nullptr;   // (no mesh read: laid as an unknown piece)
        }
        const float longest = Longest(e, upm);
        if (longest < kMinSideM) { ++c.why[(int)Why::kTiny]; continue; }
        if (!m) {   // not in the catalogue (a game object), or its mesh did not read: its renderer's box, when it is object-sized
            if (longest > kObjectMaxM) { ++c.why[(int)Why::kBig]; continue; }
            ++c.why[(int)(row ? Why::kNoParts : Why::kUnknown)];
            ++c.objects;
            ++c.parts;
            Box b;
            if (!Place(e, e.lo, e.hi, upm, &b)) { ++c.why[(int)Why::kBad]; continue; }
            b.id = e.id * 131u + 130u;
            const Why w = Judge(b, feet);
            if (w != Why::kKept) { ++c.why[(int)w]; continue; }
            kept.push_back(b);
            continue;
        }
        // the mesh's bounds onto the renderer's box of the piece: as they are when they agree, else stretched axis by axis
        float scale[3] = {1, 1, 1}, shift[3] = {0, 0, 0};
        bool stretched = false;
        for (int k = 0; k < 3; ++k) {
            const float ms = m->hi[k] - m->lo[k], rs = e.hi[k] - e.lo[k];
            const float tol = kSameBoundsUnits + kSameBoundsShare * std::max(ms, rs);
            if (std::fabs(m->lo[k] - e.lo[k]) <= tol && std::fabs(m->hi[k] - e.hi[k]) <= tol) continue;
            if (!(ms > 1e-4f) || !(rs > 1e-4f)) continue;
            scale[k] = rs / ms;
            shift[k] = e.lo[k] - m->lo[k] * scale[k];
            stretched = true;
        }
        if (stretched) ++c.stretched;
        uint32_t partNo = 0;
        for (const worldsolids::Part& p : m->parts) {
            ++c.parts;
            float lo[3], hi[3];
            for (int k = 0; k < 3; ++k) { lo[k] = p.lo[k] * scale[k] + shift[k]; hi[k] = p.hi[k] * scale[k] + shift[k]; }
            Box b;
            if (!Place(e, lo, hi, upm, &b)) { ++c.why[(int)Why::kBad]; continue; }
            b.id = e.id * 131u + partNo++;
            const Why w = Judge(b, feet);
            if (w != Why::kKept) { ++c.why[(int)w]; continue; }
            kept.push_back(b);
        }
    }
    const float middle[3] = {feet[0], feet[1] + kMiddleM, feet[2]};
    std::sort(kept.begin(), kept.end(), [&](const Box& a, const Box& b) { return Outside(a, middle, 0.0f) < Outside(b, middle, 0.0f); });
    if (kept.size() > (size_t)kMaxKept) {
        c.beyond = unsigned(kept.size() - kMaxKept);
        kept.resize(kMaxKept);
    }
    c.kept = unsigned(kept.size());
    // the bodies: a piece's parts are one, and so are parts of pieces that touch (one after the other: a whole wall in a row) - each the least entity id of its body
    std::vector<int> up(kept.size());
    for (size_t i = 0; i < up.size(); ++i) up[i] = (int)i;
    auto root = [&](int i) {
        while (up[(size_t)i] != i) i = up[(size_t)i] = up[(size_t)up[(size_t)i]];
        return i;
    };
    for (size_t i = 0; i < kept.size(); ++i)
        for (size_t j = i + 1; j < kept.size(); ++j) {
            if (kept[i].entity != kept[j].entity && !Touch(kept[i], kept[j], kTouchM)) continue;
            const int a = root((int)i), b = root((int)j);
            if (a != b) up[(size_t)std::max(a, b)] = std::min(a, b);
        }
    std::vector<uint32_t> least(kept.size(), ~0u), firstEntity(kept.size(), 0u);
    std::vector<uint8_t> many(kept.size(), 0);
    for (size_t i = 0; i < kept.size(); ++i) {
        const size_t r = (size_t)root((int)i);
        least[r] = std::min(least[r], kept[i].entity);
        if (!firstEntity[r]) firstEntity[r] = kept[i].entity;
        else if (firstEntity[r] != kept[i].entity) many[r] = 1;
    }
    for (size_t i = 0; i < kept.size(); ++i) {
        const size_t r = (size_t)root((int)i);
        kept[i].body = least[r] ? least[r] : 1u;
        if (r == i) {
            ++c.bodies;
            if (many[r]) ++c.joined;
        }
    }
    if (counts) *counts = c;
    return kept;
}

}  // namespace worldwalls
