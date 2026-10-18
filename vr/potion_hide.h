// potion_hide.h - [hands] potion_hide_game: the game's OWN thrown-potion missile is not DRAWN while OUR potion - the weapon-weight model's model - flies the thrown
// trajectory ([weight] potion_model_flies). Visual only: the server's missile, its damage, its stack spending and its explosion are the game's and are not touched;
// the client's copy of the missile keeps living, stepping and exploding as ever (the explosion is a missile of its own - another unit, another class - and is
// drawn). Only the potion missile of the hero, in the window of a throw we pressed, is hidden; a monster's missile, a trap's, the hero's arrows, any other missile
// are untouched.
//
// What the game does (static reading of D2R 3.3.93787; docs and notes: side-session-notes/potion-hide.md). The client draws a unit through the renderer's entity
// registry (entt): the unit's visual is an entity (or several: a root and its meshes, effects) made from an asset definition by the asset-instantiation function
// 0x74FBB0, which calls - once per entity, with the unit it belongs to - the "visual init" callback 0x771A10 (assets, definition, entity, unit). The renderer then
// computes every entity's world matrix each frame in 0xE3A8C0 (out, entity, TransformComponent*), which calls itself for the entity's parent first and writes the
// result to `out` and to the component at +0x40 (the matrix the draw reads; +0x80 is the previous frame's, for motion vectors). bearstonem/d2r-pcvr (MIT) hooks both
// to put the arrow's and javelin's mesh on its own flight (vr/bow.cpp, vr/projectiles.cpp); the same two hooks, used the other way round, take a mesh out of the picture:
//   1. the visual-init hook only LOOKS (the original runs unchanged): for the hero's potion missile in the armed window it notes the entity ids;
//   2. the world-matrix hook lets the original compute, then for a noted entity writes a matrix that is a point a long way under the ground (HiddenMatrix): every
//      child of that entity inherits it (the recursion goes through the hook), and the draw, the bounds and the motion vectors all see an invisible thing.
// The explosion's own sprites are other units (the client's hit function makes new ones) and are not in the set.
//
// This header is the pure part: the unit's fields, the decision, the set of hidden entities, the hidden matrix, the armed window. The game's side is
// vr/potion_hide.cpp; the test is cleanroom/tests/potion_hide_test.cpp.
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace potionhide {

// -- the client's unit (read: vr/melee_touch.cpp MissileScan, the game's 0x9A7B0 / 0x34FB00). A missile is type 3; the six throwing potions are classes 44 - 49
// (the server's table, vr/throw_born.h); the owner is recorded when the flag word at +0x128 has 0x400: its unit type at +0xE8 (0 = a player) and its id at +0xEC.
constexpr uint32_t kTypePlayer = 0, kTypeMissile = 3;
constexpr int32_t kClassFirst = 44, kClassLast = 49;
constexpr size_t kOffType = 0x0, kOffClass = 0x4, kOffId = 0x8, kOffFlags = 0x128, kOffOwnerType = 0xE8, kOffOwnerId = 0xEC;
constexpr uint32_t kFlagHasOwner = 0x400;

struct UnitHead {
    uint32_t type = ~0u, cls = 0, id = 0, flags = 0, ownerType = ~0u, ownerId = 0;
};

inline bool IsPotionClass(uint32_t c) { return c >= (uint32_t)kClassFirst && c <= (uint32_t)kClassLast; }

enum class Verdict : uint8_t {
    Hide,        // the hero's potion missile in the armed window: its visual is not drawn
    NotArmed,    // no throw of ours is waiting for its missile
    NotMissile,  // the unit is not a missile
    NotPotion,   // a missile, but not one of the six potions
    NotHero,     // a potion missile of someone else (a monster's, another player's)
};

inline const char* Why(Verdict v) {
    switch (v) {
        case Verdict::Hide: return "the hero's potion missile in the armed throw's window";
        case Verdict::NotArmed: return "no throw armed";
        case Verdict::NotMissile: return "not a missile";
        case Verdict::NotPotion: return "not a throwing potion";
        case Verdict::NotHero: return "not the hero's";
    }
    return "";
}

// heroId: the hero's unit id when it is known (0: not known - any player's unit counts, which is the hero's alone offline).
inline Verdict Decide(const UnitHead& u, bool armed, uint32_t heroId) {
    if (!armed) return Verdict::NotArmed;
    if (u.type != kTypeMissile) return Verdict::NotMissile;
    if (!IsPotionClass(u.cls)) return Verdict::NotPotion;
    if (!(u.flags & kFlagHasOwner) || u.ownerType != kTypePlayer) return Verdict::NotHero;
    if (heroId != 0 && u.ownerId != heroId) return Verdict::NotHero;
    return Verdict::Hide;
}

// -- the matrix a hidden entity gets: the 3 x 3 part scaled to a speck and the translation put kSink world units under the ground (the renderer's y is up; a hero is
// a few units tall). Both, so that a mesh, an emitter that spawns at the entity's place and a light are all out of the picture. A row-vector 4 x 4: m[12..14] is the place.
inline constexpr float kScale = 1.0e-4f;
inline constexpr float kSink = 1.0e5f;
inline bool HiddenMatrix(const float in[16], float out[16]) {
    for (int i = 0; i < 16; ++i) if (!std::isfinite(in[i])) return false;
    std::memcpy(out, in, sizeof(float) * 16);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out[r * 4 + c] *= kScale;
    out[13] -= kSink;
    return true;
}

// -- the effect component of an entity. The thrown potion's visual is NOT a mesh: its HD unit definition (data/hd/missiles/mslops.json ... mslgpl.json) is a root entity and ONE entity with a
// VfxDefinitionComponent, a particle effect (data/hd/vfx/particles/missiles/mslops/vfx_mslops.particles). The renderer keeps the runtime effect component in an entt storage (the one
// 0x66DBB0 (registry) returns): per entity one 0x30-byte element whose first field points to the component object; the object holds the effect instance at +0x10 (0 until it is made) and
// the visibility wish at +0x1C, which VfxSetVisible (0xF6A830) stores and applies, and the creation applies too (0xF6AA09). The storage is a sparse set (read: 0x78A5C0 / 0x1D5840 and the
// loop at 0x750F61 that calls the game's own 0xF6A800 on the same component): pages at +0x08 / +0x10 (a page = 256 u32, page = (entity >> 8) & 0xFFF, slot = entity & 0xFF, the dense index
// in the low 20 bits, 0xFFFFF = none), the packed entities at +0x20 / +0x28, the elements at +0x38.
constexpr size_t kStOffPagesBegin = 0x08, kStOffPagesEnd = 0x10, kStOffPackedBegin = 0x20, kStOffPackedEnd = 0x28, kStOffElements = 0x38;
constexpr size_t kEffectElementSize = 0x30;
constexpr size_t kEffectOffInstance = 0x10, kEffectOffVisible = 0x1C;

// The element of `entity` in a storage, read through rd(address, out, size) -> bool (guarded in the game, a fake memory in the test). False when the entity has none.
template <class Rd>
inline bool StorageElement(Rd&& rd, uintptr_t storage, uint32_t entity, size_t elementSize, uintptr_t* element) {
    uintptr_t pages = 0, pagesEnd = 0, packed = 0, packedEnd = 0, elements = 0;
    if (!rd(storage + kStOffPagesBegin, &pages, sizeof pages) || !rd(storage + kStOffPagesEnd, &pagesEnd, sizeof pagesEnd) ||
        !rd(storage + kStOffPackedBegin, &packed, sizeof packed) || !rd(storage + kStOffPackedEnd, &packedEnd, sizeof packedEnd) ||
        !rd(storage + kStOffElements, &elements, sizeof elements))
        return false;
    if (!pages || !packed || !elements || pagesEnd < pages || packedEnd < packed) return false;
    const uintptr_t page = (entity >> 8) & 0xFFF;
    if (page >= (pagesEnd - pages) / sizeof(uintptr_t)) return false;
    uintptr_t pg = 0;
    uint32_t sparse = 0;
    if (!rd(pages + page * sizeof(uintptr_t), &pg, sizeof pg) || !pg) return false;
    if (!rd(pg + (uintptr_t)(entity & 0xFF) * 4, &sparse, sizeof sparse)) return false;
    const uint32_t index = sparse & 0xFFFFF;
    if (index == 0xFFFFF || index >= (packedEnd - packed) / 4) return false;
    uint32_t packedEntity = 0;
    if (!rd(packed + (uintptr_t)index * 4, &packedEntity, sizeof packedEntity) || packedEntity != entity) return false;   // the entity itself, generation and all
    *element = elements + (uintptr_t)index * elementSize;
    return true;
}

// -- the entities that are hidden: at most kCapacity (one potion's visual is a handful), each for kTtlMs (a potion's flight, its explosion and a margin; the
// game destroys the entities with the unit, and a recycled index has a new generation, so a dead id is never met again). Written by the visual-init callback,
// read by the world-matrix hook, on the renderer's threads: plain atomics, a race costs a frame.
class EntitySet {
public:
    static constexpr size_t kCapacity = 24;
    static constexpr uint32_t kNone = 0xFFFFFFFFu;
    static constexpr uint64_t kTtlMs = 8000;

    EntitySet() { Clear(); }
    void Clear() {
        for (size_t i = 0; i < kCapacity; ++i) { id_[i].store(kNone); until_[i].store(0); }
        live_.store(0);
    }
    // true when it is a new entity of the set (false: already there, or no room, or not an entity id)
    bool Add(uint32_t entity, uint64_t now, uint64_t ttlMs = kTtlMs) {
        if (entity == kNone || (entity & 0xFFFFF) == 0xFFFFF) return false;
        size_t freeAt = kCapacity;
        for (size_t i = 0; i < kCapacity; ++i) {
            if (id_[i].load() == entity) { until_[i].store(now + ttlMs); return false; }
            if (freeAt == kCapacity && (id_[i].load() == kNone || until_[i].load() < now)) freeAt = i;
        }
        if (freeAt == kCapacity) return false;
        until_[freeAt].store(now + ttlMs);
        id_[freeAt].store(entity);
        Recount(now);
        return true;
    }
    bool Has(uint32_t entity, uint64_t now) const {
        for (size_t i = 0; i < kCapacity; ++i)
            if (id_[i].load(std::memory_order_relaxed) == entity) return until_[i].load(std::memory_order_relaxed) >= now;
        return false;
    }
    bool Remove(uint32_t entity, uint64_t now) {
        for (size_t i = 0; i < kCapacity; ++i)
            if (id_[i].load() == entity) { id_[i].store(kNone); until_[i].store(0); Recount(now); return true; }
        return false;
    }
    void Prune(uint64_t now) {
        for (size_t i = 0; i < kCapacity; ++i)
            if (id_[i].load() != kNone && until_[i].load() < now) { id_[i].store(kNone); until_[i].store(0); }
        Recount(now);
    }
    // The cheap gate of the hot hook: nothing is hidden (as of the last Add / Remove / Prune).
    int Live() const { return live_.load(std::memory_order_relaxed); }

private:
    void Recount(uint64_t now) {
        int n = 0;
        for (size_t i = 0; i < kCapacity; ++i)
            if (id_[i].load() != kNone && until_[i].load() >= now) ++n;
        live_.store(n, std::memory_order_relaxed);
    }
    std::array<std::atomic<uint32_t>, kCapacity> id_;
    std::array<std::atomic<uint64_t>, kCapacity> until_;
    std::atomic<int> live_{0};
};

// -- the armed window: from the press of a throw whose potion OUR model flies, until the game's own potion has been created and drawn (its creation is the hero's
// action frame, a third of a second on; with an unusually late press the margin is wide). Many visits in it (a potion's visual has more than one entity).
class Window {
public:
    // The press of a throw at nowMs; flightS: how long the model flies (a late creation, a throw the game took slowly).
    void Arm(uint64_t nowMs, double flightS) {
        const double f = std::isfinite(flightS) ? (flightS < 0.0 ? 0.0 : flightS > 10.0 ? 10.0 : flightS) : 0.0;
        armedAt_.store(nowMs, std::memory_order_release);
        until_.store(nowMs + kBaseMs + (uint64_t)(f * 1000.0), std::memory_order_release);
    }
    void Cancel() { until_.store(0, std::memory_order_release); }
    bool Armed(uint64_t nowMs) const { const uint64_t u = until_.load(std::memory_order_acquire); return u != 0 && nowMs <= u; }
    uint64_t ArmedAt() const { return armedAt_.load(std::memory_order_acquire); }
    uint64_t Until() const { return until_.load(std::memory_order_acquire); }
    static constexpr uint64_t kBaseMs = 3500;   // after the press, besides the model's flight: the animation's action frame, the missile's creation, the renderer's init

private:
    std::atomic<uint64_t> armedAt_{0}, until_{0};
};

}  // namespace potionhide

// The game's side (vr/potion_hide.cpp).
namespace D2RL {
struct PluginContext;
}
namespace potionhide {

void Init(const D2RL::PluginContext* ctx);
// The hero's client unit id, when vrcam knows it (0 = not known: any player's missile counts - offline that is the hero's).
void SetHeroIdSource(bool (*fn)(uint32_t* id));
// [hands] potion_hide_game (and [weight] potion_model_flies: the game's potion is hidden only while OUR potion flies): live.
void Configure(bool wanted);
bool Wanted();
// Hooks the two renderer functions once they are found (tried again now and then); logs a short summary of each armed throw after its window. Cheap to call every frame.
void Install();
// True when both hooks are in, nothing faulted, and the option is on: the throw wire may rely on it.
bool Ready();
// Both hooks are in (whatever the option says now): the engine unit's start-up line says so.
bool HooksIn();
// The wire pressed the game's throw of a potion whose model flies flightS seconds (F4 only): the hero's potion missile created from now on is not drawn. False
// when the option is not ready.
bool Arm(uint64_t nowMs, double flightS);
void Disarm();
// Another reader of the world-matrix function's results ([hands] xbow_support_model: the crossbow as the renderer places it, skel::XbowRendered):
// called after the original for every entity, on the renderer's thread - it must be cheap and only read. WantHooks(true) puts the two hooks in
// (as Configure(true) does) even with the potion hide off; the hooks still do nothing to any entity of their own then.
using XformObserver = void (*)(uint32_t entity, uintptr_t component, uintptr_t out);
void SetXformObserver(XformObserver fn);
void WantHooks(bool want);

}  // namespace potionhide
