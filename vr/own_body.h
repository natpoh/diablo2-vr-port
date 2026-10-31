// own_body.h - [weight] own_body: what of the hero's own body his held weapon meets, and when it is left out (Serg, 0.192.36: "где галочка, чтобы оружие не
// проходило сквозь собственное тело"). Pure: no game calls, no globals (vr/melee_touch.cpp asks it for the bodies it publishes, vr/weight.cpp for each step).
//
//   his trunk and legs, always; his head (in F4 the player's own: the weapon stops at it, never through his face)
//   never his arms or hands, nor anything they hold or that hangs on him: a "free" arm stood where that hand was about to take the shaft (Serg, 0.192.37, a pike:
//   "само об себя стукается и выворачивается", hard to pass from hand to hand) - and world_contact leaves out every piece hanging on him (knockprops::LevelBoxes)
//   left out for a moment after the weapon is taken (kAfterTakeMs: drawn from the back, the hip, called back into the hand - it comes in through him) and
//   while a hand is behind a shoulder where it draws or stows (the back's zones)
#pragma once

#include <cstdint>

namespace ownbody {

constexpr uint64_t kAfterTakeMs = 400;

// Is the hero's own body left out of the weapon's bodies now? nowMs / takenAtMs: the last take of the weapon (0: none); drawZone: a hand is behind a shoulder
// where it may draw or stow.
inline bool Skip(uint64_t nowMs, uint64_t takenAtMs, bool drawZone) {
    return drawZone || (takenAtMs && nowMs >= takenAtMs && nowMs - takenAtMs < kAfterTakeMs);
}

// Does an entity whose parent is `parent` hang on the hero (`root`)? parentOf(index, &parent) gives a known entity's parent by its index; entities compare by
// their index (the low 20 bits), kNone ends a chain. His parts, the weapon in his hand, what is slung on his back: world_contact never makes them walls.
constexpr uint32_t kIndexMask = 0xFFFFFu;
template <class ParentOf>
bool HangsOn(uint32_t parent, uint32_t root, ParentOf&& parentOf, int maxUp = 12) {
    if (!root) return false;
    uint32_t p = parent & kIndexMask;
    for (int up = 0; up < maxUp && p != kIndexMask; ++up) {
        if (p == (root & kIndexMask)) return true;
        uint32_t next = kIndexMask;
        if (!parentOf(p, &next)) return false;
        p = next & kIndexMask;
    }
    return false;
}

}  // namespace ownbody
