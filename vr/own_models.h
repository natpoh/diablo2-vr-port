// own_models.h - the entities of the models we draw as the game's own (vr/engine/engitem.cpp: the belt's bottles, the emptied bottles, an item or a bottle in a hand,
// the other players' copies), for the readers that must tell them from the level: [weight] world_contact's scan leaves them out (ownbody::Ours - Serg, 0.197.1:
// the weapons stopped on the belt's bottles). engitem publishes them from the render thread; any thread copies them.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ownmodels {

inline SRWLOCK g_lock = SRWLOCK_INIT;
inline std::vector<uint32_t> g_entities;   // by their index (the low 20 bits), sorted

// The entities of all our placed instances now (any order; the index is taken and they are sorted here).
inline void Publish(std::vector<uint32_t> entities) {
    for (uint32_t& e : entities) e &= 0xFFFFFu;
    std::sort(entities.begin(), entities.end());
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    AcquireSRWLockExclusive(&g_lock);
    g_entities.swap(entities);
    ReleaseSRWLockExclusive(&g_lock);
}

// A copy of them, sorted by index.
inline void Copy(std::vector<uint32_t>* out) {
    AcquireSRWLockShared(&g_lock);
    *out = g_entities;
    ReleaseSRWLockShared(&g_lock);
}

}  // namespace ownmodels
