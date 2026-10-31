// unit_models.h - [weight] world_contact: a unit's models are never walls (Serg, 0.192.39: "НПС тоже принимается за твёрдый объект и обводится квадратной
// рамкой" - an NPC standing still was laid by its renderer's box). Pure: no game calls, no globals (vr/skeletons.cpp skel::UnitEntities reads the skeletons'
// paths, vr/knock_props.cpp LevelBoxes walks each level piece's parents).
//
//   a unit's model: a skeleton under /character/ (a player, a monster, an NPC, a merc - the game's own folders: character/player, character/monster,
//   character/npc) or an item's or a missile's; the level's objects (a chest, the camp's torch, a door) are under /objects/ and stay what they were
//   an entity is a unit's when it holds such a skeleton itself or its parents lead up to one that does (its body parts, its weapon, its shield)
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace unitmodels {

// Is a skeleton of this path (as the game names it: its SkeletonInstance's name) a unit's?
inline bool UnitPath(const char* path) {
    if (!path || !*path) return false;
    char low[192];
    size_t i = 0;
    for (; path[i] && i < sizeof low - 1; ++i) low[i] = (char)((path[i] >= 'A' && path[i] <= 'Z') ? path[i] + 32 : path[i] == '\\' ? '/' : path[i]);
    low[i] = 0;
    return std::strstr(low, "/character/") || std::strstr(low, "/item") || std::strstr(low, "/missile") || std::strstr(low, "dropped_items") ||
           std::strstr(low, "gold_");
}

// Is an entity (`self`, its parent `parent`) a unit's, or does it hang on one? isUnit(index): does the entity of this index (the low 20 bits) hold a unit's
// skeleton; parentOf(index, &parent) gives a known entity's parent by its index (false: not known - the walk ends). kIndexMask ends a chain.
constexpr uint32_t kIndexMask = 0xFFFFFu;
template <class IsUnit, class ParentOf>
bool OfUnit(uint32_t self, uint32_t parent, IsUnit&& isUnit, ParentOf&& parentOf, int maxUp = 12) {
    if (isUnit(self & kIndexMask)) return true;
    uint32_t p = parent & kIndexMask;
    for (int up = 0; up < maxUp && p != kIndexMask; ++up) {
        if (isUnit(p)) return true;
        uint32_t next = kIndexMask;
        if (!parentOf(p, &next)) return false;
        p = next & kIndexMask;
    }
    return false;
}

}  // namespace unitmodels
