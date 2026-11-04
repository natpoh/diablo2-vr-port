// vr/engine/held_units.h - letting go of the game's units engitem placed, the way the game does (pure: tools/held_units_test.cpp).
//
// engitem asks the game for unit definitions (EntityContainerDefinition::GetOrLoadDefinition) and places instances of them in the renderer's
// registry (UnitDefinition::PlaceInstance): the belt's potions, what the hands hold, the other players' items, the sky and cave domes, the
// ceiling's slabs, walls, beams and vaults. Each comes back as a blz::shared_ptr the game hands over - one reference of ours. Until 0.201 none
// was ever let go ("how the game's shared_ptr is released safely is not known yet"): every definition and every instance an area change had
// taken stayed referenced, and when the client left a game the game's own teardown found its assets still held - "Assets have been leaked",
// m_assets.empty(), leakCount == 0, object pools not empty (a player's 0.201.0, 2026-11-04: the process died 18 s later, 0xC0000409; Serg on
// 2026-11-03, the same checks after a TCP/IP game).
//
// What the game does (D2RLoader's D2R 3.3.93787, read from its image 2026-11-04 - tools/dump_d2r.py, tools/recon.py):
//   - a blz::shared_ptr is {object, control block}; the control block {vtable, uses at +8, weaks at +0xC}. Letting go (the character
//     screen's hero units, 0x677D16; PlaceInstance's own temporaries, 0x96631E): uses - 1, and if that was the last, vtable +0x10 (the
//     object's destruction), then weaks - 1, and if that was the last, vtable +0x18 (the block itself) - ReleaseCtrl below, word for word.
//   - a UnitInstance's destruction (0x964410) takes it out of its definition's list (0x955E90), then removes each of its entities from the
//     registry (0x955CD0: its list at +0x10, how many at +0x18) - on the registry's owner thread (registry +0xA0), each entity checked as
//     the registry's own (registry +0x30: the entities by index, the low 20 bits) or a BC_ASSERT. Then it lets go of its definition (+0x40).
//   - the world's teardown (0xD72610, from the leave's sequence 0x656D50) clears the registry (0x912C20, 0x912D50) and then unloads the
//     assets, where the leak is found (0xD8CB70). At its entry our instances' entities are still the registry's.
// So: at that entry, on the owner thread, every instance of ours is let go first (its entities still there: the game removes them), then
// every definition; an instance whose entities an area change already took keeps only the live ones in its list (KeepLive), so the game's
// removal never meets an entity that is gone - or a slot reused for another one.
#pragma once

#include <intrin.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace heldunits {

// One reference to a blz::shared_ptr's control block let go, as the game does. True when there was one. No SEH here (engitem wraps it).
inline bool ReleaseCtrl(void* ctrl) {
    if (!ctrl) return false;
    using Fn = void(__fastcall*)(void*);
    auto* uses = reinterpret_cast<volatile long*>(static_cast<char*>(ctrl) + 8);
    auto* weaks = reinterpret_cast<volatile long*>(static_cast<char*>(ctrl) + 0xC);
    if (_InterlockedExchangeAdd(uses, -1) == 1) {
        void** vtbl = *static_cast<void***>(ctrl);
        reinterpret_cast<Fn>(vtbl[2])(ctrl);   // +0x10: the object
        if (_InterlockedExchangeAdd(weaks, -1) == 1) reinterpret_cast<Fn>(vtbl[3])(ctrl);   // +0x18: the block
    }
    return true;
}

// An entity is the registry's own when the registry's entity at its index (the low 20 bits) is that very entity (the version in the high
// bits too) - the game's check (0x955D66). `entities` / `count`: registry +0x30 .. +0x38.
inline bool Live(const uint32_t* entities, size_t count, uint32_t e) {
    const size_t i = e & 0xFFFFFu;
    return entities && i < count && entities[i] == e;
}

// An instance's entity list (+0x10, `n` of them at +0x18) brought down to the live ones, in their order: how many are left. The ones an
// area change took are left out, so the game's removal touches only entities that are still the instance's.
template <class LiveFn>
inline size_t KeepLive(uint32_t* list, size_t n, LiveFn live) {
    size_t k = 0;
    for (size_t i = 0; i < n; ++i)
        if (live(list[i])) list[k++] = list[i];
    return k;
}

// Placing waits from the world's teardown until a menu (no world) has been seen: the frames the game still draws while it leaves would place
// our units again in a registry it is about to clear. The next game's first world places them afresh.
struct Life {
    std::atomic<bool> tornDown{false};
    void Teardown() { tornDown.store(true); }
    void World(bool inWorld) { if (!inWorld) tornDown.store(false); }
    bool MayPlace(bool inWorld) const { return inWorld && !tornDown.load(); }
};

// What one letting go did, for the log
struct Tally {
    int instances = 0;   // instances let go
    int stale = 0;       // ... of them with entities an area change had taken (those left out of the game's removal)
    int definitions = 0; // definitions let go
    int kept = 0;        // instances not let go: not the registry's owner thread (the game's removal would assert) - held as before
};
inline std::string Line(const char* when, const Tally& t) {
    char b[400];
    std::snprintf(b, sizeof b, "vrcam: %s: %d placed instance%s removed (%d with entities an area change had taken), %d definition%s let go%s", when,
                  t.instances, t.instances == 1 ? "" : "s", t.stale, t.definitions, t.definitions == 1 ? "" : "s",
                  t.kept ? " - some instances KEPT: not on the registry's owner thread (the game would assert)" : "");
    std::string s = b;
    if (t.kept) {
        std::snprintf(b, sizeof b, " (%d kept)", t.kept);
        s += b;
    }
    return s;
}

}  // namespace heldunits
