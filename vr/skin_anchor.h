// [weight] death_ragdoll, monster_ragdoll_pose, knock_down: the anchor a skinning finds its body by - the root of the entity the game is drawing.
//
// 0.199.0 (Serg: «трупы мерцают постоянно»; «надо на них вешать какие то якоря чтобы не искать их каждый кадр»): the game skins most of a monster's draws
// from a copy of its pose, so the skinning has to be told whose copy it is. Six fallen1 corpses lying in the same last frame of their death animation have
// the same matrices; dollpose::ByEntity looked for the entity drawn among the pose's root and its direct children only and found none (the log's "0 by the
// entity drawn" every 5 s), so most of those skinnings were refused as alike and drawn as the game's - the corpse there in one pass, at the game's place in
// the next. The game's own hierarchy says it outright: the entity drawn hangs (TransformComponent's parent, as many levels as the model has) under the unit's
// root, and every body pose is made for an entity of that same tree. Each entity's root is looked up once and kept (Cache), checked by its full id (the
// slot's generation: an entity freed and made again is another id) and for a few seconds only; the pose whose entity has the drawn entity's root is the one.

#pragma once

#include "doll_pose.h"

#include <cstdint>

namespace skinanchor {

inline bool NoEntity(uint32_t e) { return !e || (e & 0xFFFFFu) == 0xFFFFFu; }

// The top of e's tree: its parents followed (parentOf(entity, &parent) false: unreadable) until one has none. 0: not read (or deeper than kMaxDepth).
constexpr int kMaxDepth = 12;
template <class ParentOf>
uint32_t RootOf(uint32_t e, ParentOf&& parentOf) {
    if (NoEntity(e)) return 0;
    for (int d = 0; d < kMaxDepth; ++d) {
        uint32_t parent = 0xFFFFFFFFu;
        if (!parentOf(e, &parent)) return 0;
        if (NoEntity(parent)) return e;
        if ((parent & 0xFFFFFu) == (e & 0xFFFFFu)) return 0;   // (a loop: not a tree)
        e = parent;
    }
    return 0;
}

// Each entity's root as last looked up: one look up the tree per entity, kept kKeepMs (a skinning thread's own; no lock).
struct Cache {
    static constexpr int kSize = 128;
    static constexpr uint64_t kKeepMs = 3000;
    struct Entry { uint32_t entity = 0, root = 0; uint64_t atMs = 0; };
    Entry e[kSize];
    template <class ParentOf>
    uint32_t Root(uint32_t entity, uint64_t nowMs, ParentOf&& parentOf, bool* looked = nullptr) {
        if (looked) *looked = false;
        if (NoEntity(entity)) return 0;
        Entry& s = e[(entity * 2654435761u >> 7) % kSize];
        if (s.entity == entity && nowMs >= s.atMs && nowMs - s.atMs < kKeepMs) return s.root;
        if (looked) *looked = true;
        s.entity = entity;
        s.root = RootOf(entity, parentOf);
        s.atMs = nowMs;
        return s.root;
    }
};

// The candidate whose entity's root is `drawnRoot` (one body's poses only); -1: none, or two bodies under one root (not known).
template <class RootFn>
int ByRoot(const dollpose::Pose* cands, int k, uint32_t drawnRoot, RootFn&& rootOf) {
    if (!cands || k <= 0 || NoEntity(drawnRoot)) return -1;
    int found = -1;
    for (int c = 0; c < k; ++c) {
        const uint32_t r = rootOf(cands[c].entity);
        if (NoEntity(r) || r != drawnRoot) continue;
        if (found >= 0 && cands[found].self != cands[c].self) return -1;
        if (found < 0) found = c;   // (one body's two poses: its first)
    }
    return found;
}

}  // namespace skinanchor
