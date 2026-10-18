// own_effects.h - [render] dim_own_effects: the effects that sit ON the hero - a state's overlays (Necromancer's Bone Armor, Frozen / Shiver / Chilling Armor, Energy Shield, Cyclone
// Armor, Blade Shield, Fade ...) and the aura rings at his feet - drawn fainter, in VR first person (F4). A player's request (2026-10-18): Bone Armor wraps round the eyes of the
// first-person view and blinds. VISUAL ONLY: the buff, its state, its damage and its timers are the game's and are not touched; what changes is how faint the game's own draw of the
// effect is, per effect instance, or (at 0 %) that it is not drawn. Monsters', mercenaries' and other players' effects, and every overlay that is not a state's, are left alone.
//
// What the game does (static reading of D2R 3.3.93787, the loader layout; docs/own_effects_recon.md). A unit's overlays are made by UnitTranslationLayer::BeginCreateOverlayInstance
// (0x762900: this, the return slot, the unit's key u64, UNIT*, TOVERLAYNODE*) and FinalizeCreateOverlayInstance (0x768780: this, the return slot, the OverlayCreationState: the
// OverlayDefinition at +0, the unit's key at +0x18), which builds the OverlayInstance from the definition (OverlayDefinition::PlaceInstance 0x96FD30) into the return slot: a
// shared_ptr whose object keeps its entity ids at +0x10 (u32 array) / +0x18 (count), its definition at +0x38 and its unit's key at +0x48. The definition's name - a char* at +0x70 -
// is the HD file the overlay comes from (data/hd/overlays/necro/bonearmor_front.json), which is Overlay.txt's name for it. The overlay's entity is a particle effect (a
// VfxDefinitionComponent: Bone Armor's is data/hd/vfx/particles/overlays/necro/bonearmor_front/vfx_BoneArmor_Front.particles) whose runtime effect component the renderer keeps in
// the VFX storage (vr/potion_hide.h: StorageElement) with the effect instance at +0x10.
// THE LEVER (second version; the first took 0xA4ABB0 for an opacity - it is the effect's PLAYBACK SPEED: the player saw the bones turn slower, not fainter): the game fades an overlay
// with the unit's alpha, and it does so by a NAMED FLOAT PARAMETER of the effect, "Fade". Its per-frame unit update (0x76D13F) queues, for every entity of every overlay of the
// unit, "set the float named by the string object at 0x341D348 to the unit's alpha" (0x70E370; the string object is built at start-up by 0x42000 and holds the four characters
// "Fade" at +0x18), and the queue's consumer (0x790920) calls, for each entity's VFX component, VfxSetFloat 0xF6A380 (component, the name's address, float): it looks the name up in
// the effect's parameters (0xA48BD0) and sets the value (0xA4D7A0); true when it did. That is the lever: the same call, the same name, the player's percent. For 0 % the game's own
// VfxSetVisible (0xF6A830) as the potion hide uses it.
//
// Which overlays are "own effects" is the game's own data, not a list of offsets: states.txt names, for every state, the overlays it shows (overlay1 - 4, pgsvoverlay), the flash
// when it is cast (castoverlay) and whether it is an aura (aura = 1); tools/gen_own_effects.py turns the two tables into vr/own_effects_table.h: the overlay names of the states
// as two groups, Body and Aura (the rings at the feet). Two opacities, one for each.
//
// This header is the pure part: the names, the groups, the numbers, what to do with one effect each frame, the set of effects tracked. The game's side is vr/own_effects.cpp;
// the test is cleanroom/tests/own_effects_test.cpp.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "own_effects_table.h"

namespace owneffects {

enum class Group : uint8_t { None = 0, Body = 1, Aura = 2 };

inline const char* GroupName(Group g) { return g == Group::Body ? "body" : g == Group::Aura ? "aura" : "none"; }

// -- names. An overlay is known by its definition's name: the HD file's path or just its name, any case; Overlay.txt spells some numbered names without the underscore the HD
// files have (cyclonearmor1front / cyclonearmor_1front), so a name is compared as lowercase letters and digits only, from the last path component without ".json".
inline size_t Key(const char* raw, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (!raw) return 0;
    const char* base = raw;
    for (const char* p = raw; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    size_t len = std::strlen(base);
    if (len >= 5) {
        const char* e = base + len - 5;
        if (e[0] == '.' && (e[1] | 0x20) == 'j' && (e[2] | 0x20) == 's' && (e[3] | 0x20) == 'o' && (e[4] | 0x20) == 'n') len -= 5;
    }
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < cap; ++i) {
        char c = base[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[n++] = c;
    }
    out[n] = 0;
    return n;
}

inline Group GroupOfKey(const char* key) {
    int lo = 0, hi = (int)table::kCount - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const int c = std::strcmp(key, table::kEntries[mid].name);
        if (c == 0) return table::kEntries[mid].group == 2 ? Group::Aura : Group::Body;
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return Group::None;
}
inline Group GroupOf(const char* rawName) {
    char key[96];
    return Key(rawName, key, sizeof key) ? GroupOfKey(key) : Group::None;
}

// -- the numbers ([render] dim_own_effects_pct, dim_own_auras_pct: the opacity of the effect in percent, 100 = as the game draws it, 0 = not drawn)
struct Settings {
    bool on = false;
    float body = 0.30f;    // 0..1
    float aura = 0.50f;
};
inline float ClampPct(float pct) { return !std::isfinite(pct) ? 100.0f : pct < 0.0f ? 0.0f : pct > 100.0f ? 100.0f : pct; }
inline float OpacityFor(Group g, const Settings& s) {
    if (!s.on) return 1.0f;
    return g == Group::Body ? s.body : g == Group::Aura ? s.aura : 1.0f;
}

// -- what is done to one effect each frame. What WE did to it is remembered (State) so that an effect the player never asked about is never touched and a restored one is
// restored once. The effect's "Fade" cannot be read back, so while it is to be faint it is set EVERY frame: the game sets it itself when the unit's alpha changes, and its queue
// may be worked off after our tick. A new effect instance (the game made it again) starts from the game's own value and is not "restored". Visible: 0 hide by the game's
// switch, 1 show, -1 leave.
struct State {
    bool hidden = false;       // we switched it off (0 %)
    bool faded = false;        // we set its Fade to something but 1
    bool fadeBad = false;      // the effect refused the parameter (it has no "Fade"): not asked again of this effect instance
    float applied = 1.0f;      // the value we set last
    uintptr_t effect = 0;      // the effect instance we have seen on this entity
};
struct Plan {
    int visible = -1;          // VfxSetVisible(component, 0 / 1), or leave
    bool setFade = false;
    float fade = 1.0f;         // VfxSetFloat(component, "Fade", fade)
    bool cannotFade = false;   // wanted a fade, the effect has none (or the parameter's name could not be found): told once, left as the game draws it
};
inline constexpr float kSame = 0.004f;
// effect: the effect instance (0 = not made yet); paramReady: the game's "Fade" name object was found and reads "Fade".
inline Plan Decide(Group g, const Settings& s, bool f4, uintptr_t effect, bool paramReady, const State& st) {
    Plan p;
    if (!effect) return p;   // not made yet: the next frame
    const bool fresh = effect != st.effect;   // a new effect instance: its Fade is the game's own
    const float target = (f4 && g != Group::None) ? OpacityFor(g, s) : 1.0f;
    if (target >= 1.0f - kSame) {   // as the game draws it
        if (st.hidden) p.visible = 1;
        if (st.faded && !fresh && !st.fadeBad && paramReady) { p.setFade = true; p.fade = 1.0f; }
        return p;
    }
    if (target <= kSame) {   // not drawn: the game's own visibility switch
        if (!st.hidden) p.visible = 0;
        return p;
    }
    if (st.hidden) p.visible = 1;   // (it was off at 0 %; a fainter value shows it again)
    if (!paramReady || (st.fadeBad && !fresh)) { p.cannotFade = true; return p; }
    p.setFade = true;
    p.fade = target;
    return p;
}
// ok: the game's setter said it set the parameter.
inline void Apply(const Plan& p, uintptr_t effect, bool ok, State* st) {
    if (p.visible == 0) st->hidden = true;
    else if (p.visible == 1) st->hidden = false;
    if (effect != st->effect) { st->effect = effect; st->fadeBad = false; }
    if (p.setFade) {
        if (ok) { st->applied = p.fade; st->faded = p.fade < 1.0f - kSame; st->fadeBad = false; }
        else st->fadeBad = true;
    }
}

// -- reading the game's objects through rd(address, out, size) -> bool (guarded in the game, a fake memory in the test)
constexpr size_t kInstOffEntities = 0x10, kInstOffCount = 0x18, kInstOffDef = 0x38, kInstOffOwner = 0x48;   // the OverlayInstance
constexpr size_t kDefOffName = 0x70;                                                                          // the OverlayDefinition: char* to the file / name
constexpr size_t kStateOffDef = 0x00, kStateOffKey = 0x18;                                                    // the OverlayCreationState
constexpr size_t kEffectOffInstance = 0x10;                                                                   // the effect component: the effect instance
constexpr size_t kNameOffChars = 0x18;                                                                        // a string object (refcount, capacity, length, then the characters)
constexpr uint32_t kMaxEntities = 16;

// The entity ids of an OverlayInstance: begin pointer at +0x10, count at +0x18. False when it does not look like one (no pointer, a count over the cap, an id that is not one).
template <class Rd>
inline bool ReadEntities(Rd&& rd, uintptr_t inst, uint32_t* out, uint32_t cap, uint32_t* n) {
    uintptr_t begin = 0;
    uint64_t count = 0;
    *n = 0;
    if (inst < 0x10000 || !rd(inst + kInstOffEntities, &begin, sizeof begin) || !rd(inst + kInstOffCount, &count, sizeof count)) return false;
    if (count == 0) return true;
    if (begin < 0x10000 || count > kMaxEntities) return false;
    const uint32_t take = (uint32_t)count < cap ? (uint32_t)count : cap;
    for (uint32_t i = 0; i < take; ++i) {
        uint32_t e = 0;
        if (!rd(begin + (uintptr_t)i * 4, &e, sizeof e)) return false;
        if (e == 0xFFFFFFFFu || (e & 0xFFFFF) == 0xFFFFF) return false;
        out[i] = e;
    }
    *n = take;
    return true;
}

// The definition's name: a char* at +0x70 to printable text, cap-1 characters at most. False when it is not one.
template <class Rd>
inline bool ReadName(Rd&& rd, uintptr_t def, char* out, size_t cap) {
    if (!out || cap < 2) return false;
    out[0] = 0;
    uintptr_t p = 0;
    if (def < 0x10000 || !rd(def + kDefOffName, &p, sizeof p) || p < 0x10000) return false;
    size_t n = 0;
    for (; n + 1 < cap; ++n) {
        char c = 0;
        if (!rd(p + n, &c, 1)) return false;
        if (c == 0) break;
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
        out[n] = c;
    }
    out[n] = 0;
    return n > 0 && n + 1 < cap;
}

// The game's "Fade" parameter name: the global at `handle` holds a pointer to a string object whose characters (at +0x18) are "Fade". The setter takes the global's ADDRESS (as the game's
// own queue passes it), so what is checked here is that the object it leads to really reads "Fade" - a proof from the game's own memory, not a constant.
template <class Rd>
inline bool ReadFadeName(Rd&& rd, uintptr_t handle, char out[8]) {
    uintptr_t so = 0;
    char c[8] = {};
    out[0] = 0;
    if (handle < 0x10000 || !rd(handle, &so, sizeof so) || so < 0x10000 || !rd(so + kNameOffChars, c, 5)) return false;
    c[5] = 0;
    for (int i = 0; i < 5; ++i) out[i] = c[i];
    out[5] = 0;
    return std::strcmp(c, "Fade") == 0;
}

// -- the effects tracked: the entities of the hero's overlays that are in the table. Made by the hook of the overlay's creation, read every frame by the tick (the registry's
// thread). An entity that has no effect component (the overlay's empty root) is let go after kNoEffectMs; one that had and has not any more (the overlay ended: the game destroys
// its entities and a recycled index has a new generation) is let go at once - and given back what we did to it if it is still there.
class Tracker {
public:
    static constexpr size_t kCapacity = 48;
    static constexpr uint32_t kNone = 0xFFFFFFFFu;
    static constexpr uint64_t kNoEffectMs = 4000;
    struct Slot {
        uint32_t entity = kNone;
        Group group = Group::None;
        State st;
        uint64_t addedAt = 0;
        bool seen = false;       // its effect component has been found once
        bool told = false;       // the log has said what it is
        bool toldNoFade = false;
        float loggedFade = -1.0f;   // the last Fade the log has said
        char name[40] = {};
    };

    Slot* Add(uint32_t entity, Group g, const char* name, uint64_t now) {
        if (entity == kNone || (entity & 0xFFFFF) == 0xFFFFF) return nullptr;
        Slot* freeSlot = nullptr;
        for (Slot& s : slots_) {
            if (s.entity == entity) { s.group = g; s.addedAt = now; return &s; }
            if (!freeSlot && s.entity == kNone) freeSlot = &s;
        }
        if (!freeSlot) return nullptr;
        *freeSlot = Slot{};
        freeSlot->entity = entity;
        freeSlot->group = g;
        freeSlot->addedAt = now;
        if (name) {
            size_t n = std::strlen(name);
            if (n > sizeof freeSlot->name - 1) n = sizeof freeSlot->name - 1;
            std::memcpy(freeSlot->name, name, n);
            freeSlot->name[n] = 0;
        }
        return freeSlot;
    }
    void Drop(Slot* s) { *s = Slot{}; }
    void Clear() { for (Slot& s : slots_) s = Slot{}; }
    size_t Count() const { size_t n = 0; for (const Slot& s : slots_) if (s.entity != kNone) ++n; return n; }
    std::array<Slot, kCapacity>& Slots() { return slots_; }
    // A slot whose entity has no effect component this frame: true when it should be let go (it never had one for long enough, or it had and has not any more).
    static bool GoneNow(const Slot& s, bool found, uint64_t now) {
        if (found) return false;
        if (s.seen) return true;
        return now > s.addedAt + kNoEffectMs;
    }

private:
    std::array<Slot, kCapacity> slots_{};
};

// -- who is the hero. A unit passed to the overlay-create function is the local hero's when it is a player (type 0, the first dword) and it is the unit the game's own
// ClientPlayer(LocalIndex()) gives, or the one the aim function last worked on (vrcam's), or - in case the translation layer holds another copy of the same player - it has the
// id of either. Offline there is one player; the id keeps another player's unit out when there are more.
constexpr size_t kUnitOffType = 0x0, kUnitOffClass = 0x4, kUnitOffId = 0x8;
struct UnitIds {
    uint32_t type = 0xFFFFFFFFu, cls = 0, id = 0;
};
inline bool IsHero(uintptr_t unit, const UnitIds& u, uintptr_t localUnit, uint32_t localId, uintptr_t seenUnit, uint32_t seenId) {
    if (unit < 0x10000 || u.type != 0) return false;
    if (localUnit && unit == localUnit) return true;
    if (seenUnit && unit == seenUnit) return true;
    if (localId && u.id == localId) return true;
    if (seenId && u.id == seenId) return true;
    return false;
}

// -- the second way to tell an overlay is the hero's, for when the first (his unit and key in the create function) does not come out: a state's overlay of the table that stands at the
// hero's place - its entity's world place (its transform's translation) within kNearXZ of his on the ground and within kNearY of his height. The hero's own overlays are made at his feet / on
// his body; another unit's are at that unit's place (a mercenary stands several units away). Used only while no key of the hero's is known, and told in the log when it is.
constexpr float kNearXZ = 2.5f, kNearY = 8.0f;   // world units (the hero is ~7.5 tall)
inline bool NearHero(const float hero[3], const float other[3]) {
    for (int i = 0; i < 3; ++i) if (!std::isfinite(hero[i]) || !std::isfinite(other[i])) return false;
    const float dx = hero[0] - other[0], dz = hero[2] - other[2], dy = hero[1] - other[1];
    return dx * dx + dz * dz <= kNearXZ * kNearXZ && std::fabs(dy) <= kNearY;
}

// -- the hero's keys: the u64 the game gives a unit's overlays (BeginCreateOverlayInstance's third argument), learned when the unit is the hero's. Offline there is one hero; a
// few slots keep it from growing if the key changes (an area change, a new game) - and keep a key a finalize waits for (the definition still loading) from being pushed out.
class HeroKeys {
public:
    // true when the key is new
    bool Add(uint64_t key) {
        for (uint64_t k : k_) if (k == key) return false;
        k_[next_++ % k_.size()] = key;
        return true;
    }
    bool Has(uint64_t key) const { for (uint64_t k : k_) if (k == key && key != 0) return true; return false; }
    bool Any() const { for (uint64_t k : k_) if (k != 0) return true; return false; }
    void Clear() { k_.fill(0); next_ = 0; }
private:
    std::array<uint64_t, 16> k_{};
    unsigned next_ = 0;
};

}  // namespace owneffects

// The game's side (vr/own_effects.cpp).
namespace D2RL {
struct PluginContext;
}
namespace owneffects {

void Init(const D2RL::PluginContext* ctx);
// The hero as vrcam knows him besides the game's own getter: the unit the aim function last worked on (0 = not known) and his id (false = not known).
void SetHeroSource(uintptr_t (*unit)(), bool (*id)(uint32_t*));
// [render] dim_own_effects, dim_own_effects_pct, dim_own_auras_pct (percent 0..100): live.
void Configure(bool on, float bodyPct, float auraPct);
bool Wanted();
// Puts the two hooks that WATCH the creation of overlays in as soon as they are found, whether the option is on or not (they only look: the original always runs, nothing is
// changed; so an effect that is already on when the option is ticked live is known and is dimmed at once) - tried again now and then; and tells a status line while the option is on.
// Cheap to call every frame.
void Install();
// On the thread that may change the renderer's registry, every frame (vr/vrcam.cpp, the engine unit's setter hook): the opacity of each tracked effect.
void Tick();

}  // namespace owneffects
