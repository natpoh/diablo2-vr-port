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
// WHY BONE ARMOR STILL DID NOT FADE (the third test, 2026-10-18: "Fade" set to 30 % on bonearmorfront, accepted, the bones unchanged; read 2026-10-24 from the HD files): the effect is
// PopcornFX's (vfx_BoneArmor_Front.particles) and has two renderers - the bones, a mesh drawn with vfx/materials/Diablo_Default_Mesh.pkma (opaque, lit), and a ribbon trail drawn with
// Diablo_Uber_Ribbon. Only the ribbon binds the material property MonsterFade.Fade to the effect's "Fade" attribute; the opaque mesh binds none, so the bones cannot be drawn fainter
// by any value of it. The game itself never fades them either: at its unit's alpha 0 it switches the effect off (VfxSetVisible). So an effect the table marks SOLID (its HD particle
// file has a Diablo_Default_Mesh renderer: Bone Armor, Blade Shield, Energy Shield, the shrines ...; tools/gen_own_effects.py --game) is NOT DRAWN in F4 whenever it is to be any
// fainter (TargetFor); the others are faded with "Fade" as before (Frozen / Shiver / Chilling Armor are ribbons and billboards that bind it).
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

inline const table::Entry* EntryOfKey(const char* key) {
    int lo = 0, hi = (int)table::kCount - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const int c = std::strcmp(key, table::kEntries[mid].name);
        if (c == 0) return &table::kEntries[mid];
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return nullptr;
}
inline Group GroupOfKey(const char* key) {
    const table::Entry* e = EntryOfKey(key);
    return !e ? Group::None : e->group == 2 ? Group::Aura : Group::Body;
}
inline Group GroupOf(const char* rawName) {
    char key[96];
    return Key(rawName, key, sizeof key) ? GroupOfKey(key) : Group::None;
}
// A SOLID effect (the table's mark, from the game's HD particle files): a part of it is drawn with the opaque lit-mesh material (Bone Armor's bones, Blade Shield's blades), which
// binds no "Fade" - the third test (2026-10-18) set Fade to 30 % on Bone Armor and the bones did not change. It can be drawn or not drawn, not drawn fainter.
inline bool SolidOf(const char* rawName) {
    char key[96];
    const table::Entry* e = Key(rawName, key, sizeof key) ? EntryOfKey(key) : nullptr;
    return e && e->solid != 0;
}

// -- the numbers ([render] dim_own_effects_pct, dim_own_auras_pct: the opacity of the effect in percent, 100 = as the game draws it, 0 = not drawn)
struct Settings {
    bool on = false;
    float body = 0.30f;    // 0..1
    float aura = 0.50f;
    bool slow = false;     // [render] slow_own_effects
    float speed = 0.20f;   // [render] own_effects_speed_pct / 100: 0.01..1
    bool lower = false;    // [render] lower_own_effects
    float heightCm = 0.0f; // [render] own_effects_height_cm: -150..+50
};

// -- the height ([render] lower_own_effects, own_effects_height_cm; the player's wish of 2026-10-25: "a slider for the height from the ground - lower, it may not be in the way
// so much in VR"). The hero's SOLID effects (Bone Armor first) are drawn that many cm lower (or higher) in F4: the world matrix the renderer's world-matrix function (0xE3A8C0)
// gives the overlay's entity - the entity that carries the effect, which places its bones - is moved along the world's up (y), after the game computed it; the game's own
// place, the buff and its numbers are untouched (visual only). cm become world units with the world's units a metre (vrcam: the view's height over the player's eye height).
constexpr float kHeightMinCm = -150.0f, kHeightMaxCm = 50.0f;
inline float ClampHeightCm(float cm) { return !std::isfinite(cm) ? 0.0f : cm < kHeightMinCm ? kHeightMinCm : cm > kHeightMaxCm ? kHeightMaxCm : cm; }
inline float LiftUnits(float cm, float unitsPerMetre) { return std::isfinite(unitsPerMetre) && unitsPerMetre > 1e-4f ? ClampHeightCm(cm) / 100.0f * unitsPerMetre : 0.0f; }
// The lift of one effect now (world units, 0 = the game's place).
inline float LiftFor(bool solid, bool f4, const Settings& s, float unitsPerMetre) { return s.lower && solid && f4 ? LiftUnits(s.heightCm, unitsPerMetre) : 0.0f; }
// A world matrix (the game's layout: the translation at m[12..14], y up) moved up by dy. False (and untouched) when it is not a plain one: not finite, m[15] not 1.
inline bool LiftMatrix(float m[16], float dy) {
    for (int i = 0; i < 16; ++i) if (!std::isfinite(m[i])) return false;
    if (std::fabs(m[15] - 1.0f) > 1e-4f || !std::isfinite(dy)) return false;
    m[13] += dy;
    return true;
}
// Giving a lifted matrix back: the height read now is still the one we wrote (the game has not placed it again since) - then it is put back by the lift.
inline bool StillLifted(float yNow, float yWrote) {
    return std::isfinite(yNow) && std::isfinite(yWrote) && std::fabs(yNow - yWrote) <= 1e-4f * (1.0f + std::fabs(yWrote));
}
inline float ClampPct(float pct) { return !std::isfinite(pct) ? 100.0f : pct < 0.0f ? 0.0f : pct > 100.0f ? 100.0f : pct; }

// -- the speed ([render] slow_own_effects, own_effects_speed_pct; the player's wish of 2026-10-24: "let it turn slowly" - Bone Armor's bones whirl round the eyes). The hero's
// SOLID effects (the table's mark: Bone Armor first) turn at the player's percent of the game's speed in F4, with the effect's own playback speed (0xA4ABB0 (effect, float): it
// stores the speed at +0xA4; the game's own VfxSetFade sets it to 0 / 1 when it hides / shows an overlay - the first version of this file took it for an opacity, and the bones
// turned slower). 1 % at the least (they must still turn), 100 % = the game's. Visual only: the buff, its numbers and timers are the game's.
inline float ClampSpeedPct(float pct) { return !std::isfinite(pct) ? 100.0f : pct < 1.0f ? 1.0f : pct > 100.0f ? 100.0f : pct; }
inline float SpeedFor(bool solid, bool f4, const Settings& s) { return s.slow && solid && f4 ? s.speed : 1.0f; }
struct SpeedState {
    bool sped = false;         // we set its speed to something but 1
    uintptr_t effect = 0;      // the effect instance it was for
};
// What to set this frame: <0 nothing, else the speed. current: the effect's speed read at +0xA4 (NaN: not read). A new effect instance starts at the game's own speed and is
// not "given back"; the game may set the speed itself (0 / 1 as it hides / shows the overlay), so a wanted speed is set again whenever it reads otherwise.
inline float DecideSpeed(float target, float current, uintptr_t effect, const SpeedState& st) {
    if (!effect) return -1.0f;
    const bool fresh = effect != st.effect;
    const bool known = std::isfinite(current);
    if (target < 1.0f - 0.004f) return (!known || std::fabs(current - target) > 0.004f) ? target : -1.0f;
    if (st.sped && !fresh && (!known || std::fabs(current - 1.0f) > 0.004f)) return 1.0f;
    return -1.0f;
}
inline void ApplySpeed(float set, uintptr_t effect, SpeedState* st) {
    if (effect != st->effect) { st->effect = effect; st->sped = false; }
    if (set >= 0.0f) st->sped = set < 1.0f - 0.004f;
}
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
// The opacity wanted of one effect: the group's, in F4 only; a solid effect that is to be any fainter is not drawn (the nearest it can come: its solid part has no fainter).
inline float TargetFor(Group g, const Settings& s, bool f4, bool solid) {
    const float t = (f4 && g != Group::None) ? OpacityFor(g, s) : 1.0f;
    return solid && t < 1.0f - kSame ? 0.0f : t;
}
// effect: the effect instance (0 = not made yet); paramReady: the game's "Fade" name object was found and reads "Fade"; solid: the table's mark (SolidOf); shownNow: the effect
// component's visibility wish read this frame (1 shown, 0 not, -1 not read) - the game's own unit update switches an overlay's effects back on when the unit's alpha changes, so
// one we switched off and find on again is switched off again.
inline Plan Decide(Group g, const Settings& s, bool f4, uintptr_t effect, bool paramReady, const State& st, bool solid = false, int shownNow = -1) {
    Plan p;
    if (!effect) return p;   // not made yet: the next frame
    const bool fresh = effect != st.effect;   // a new effect instance: its Fade is the game's own
    const float target = TargetFor(g, s, f4, solid);
    if (target >= 1.0f - kSame) {   // as the game draws it
        if (st.hidden) p.visible = 1;
        if (st.faded && !fresh && !st.fadeBad && paramReady) { p.setFade = true; p.fade = 1.0f; }
        return p;
    }
    if (target <= kSame) {   // not drawn: the game's own visibility switch
        if (!st.hidden || shownNow == 1) p.visible = 0;
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
constexpr size_t kEffectOffVisible = 0x1C;                                                                    // the effect component: the visibility wish (a byte) VfxSetVisible stores
constexpr size_t kFxOffDef = 0x40, kFxDefOffFlags = 0x188;                                                    // the effect instance: its definition; the definition's flags
constexpr uint8_t kFxDefCanHide = 0x08;                                                                       // ... "its drawing can be switched off"
constexpr uint8_t kFxDefCanSpeed = 0x04;                                                                      // ... "its speed can change" (0xA4ABB0 tests it)
constexpr size_t kFxOffSpeed = 0xA4;                                                                          // the effect instance: its playback speed (float)
constexpr size_t kNameOffChars = 0x18;                                                                        // a string object (refcount, capacity, length, then the characters)
constexpr uint32_t kMaxEntities = 16;

// Whether the game's visibility switch can take this effect off the screen: VfxSetVisible goes on to 0xA4ACC0 (effect, bool), which changes nothing unless the effect's
// definition (+0x40) has the flag 0x08 at +0x188 (Bone Armor's effect script reads effect.isRenderingEnabled; the game hides an overlay this way when its unit's alpha is 0).
// 1 it can, 0 it cannot (the switch would do nothing), -1 not read.
template <class Rd>
inline int CanSwitchOff(Rd&& rd, uintptr_t effect) {
    uintptr_t def = 0;
    uint8_t flags = 0;
    if (effect < 0x10000 || !rd(effect + kFxOffDef, &def, sizeof def) || def < 0x10000 || !rd(def + kFxDefOffFlags, &flags, sizeof flags)) return -1;
    return (flags & kFxDefCanHide) ? 1 : 0;
}
// Whether the effect's playback speed can change (the flag 4 the speed setter 0xA4ABB0 tests first): 1 it can, 0 the setter would do nothing, -1 not read.
template <class Rd>
inline int CanChangeSpeed(Rd&& rd, uintptr_t effect) {
    uintptr_t def = 0;
    uint8_t flags = 0;
    if (effect < 0x10000 || !rd(effect + kFxOffDef, &def, sizeof def) || def < 0x10000 || !rd(def + kFxDefOffFlags, &flags, sizeof flags)) return -1;
    return (flags & kFxDefCanSpeed) ? 1 : 0;
}

// -- the solid part's own MODELS (the fourth test, 2026-10-24: "the bone armour did not become see-through" - the switch was taken, the bones stayed). The bones are not
// drawn by the effect at all: its script calls the game's SimInterfaceCallback_CreateAnimatedModelInstance (0x10039D0, D2Render popcorn_sim_interfaces/
// sim_animated_model_instance_node.cpp), which makes plain ModelInstances of the game's renderer for them (0x100A830 -> the renderer's model factory 0xE1B340, no entity).
// The effect's switch does not reach them. The game hides a whole model with ModelInstance::SetVisibleLayers 0xF3F840 (instance, mask): bit 31 of the mask at +0 = the whole
// model hidden (UpdateVisibleMeshes 0xF3FFF0 skips it and rebuilds the render items) - what its own unit update does to an overlay's ModelComponent when the unit's alpha is 0
// (0x76DCE2). So that is done here, to the solid effect's models that stand at the hero's place.
// Where they are (the fifth test, 2026-10-24: the walk from the effect through its +0x40 found nothing - that +0x40 is the PopcornFX MEDIUM's descriptor in the callback, not
// the D2R effect's): the renderer (GetRenderer 0xE21140) keeps EVERY ModelInstance it made in a vector at +0x2EFB8 (data, count at +0x2EFC0) under an SRW lock at +0x2EFB0 -
// the factory pushes into it (0xE1B393 - 0xE1B500), the destroy takes out (0xE1F02B - 0xE1F0E0), the frame's bounds update walks it (0xE38088 - 0xE38509), all under that
// lock. A ModelInstance keeps its model resource at +0x20 (a resource: its file's path a char* at +0x08 - the game's own name getter 0x122A8F0 reads it, 0xF3EA44 -, loaded when
// the byte at +0xC8 is 4) and its world matrix at +0x260 (the translation at +0x290). The table names the model file of each solid effect (its renderer's General.Mesh:
// Bone Armor's bonearmor.model -> "bonearmor"); only a model under data/hd/vfx/ is ever taken (never a body part), and only one that is the hero's (SeenModels, below).
constexpr size_t kRendererOffModelsLock = 0x2EFB0, kRendererOffModels = 0x2EFB8;   // the SRW lock; the vector (data, count at +8)
constexpr size_t kModelOffLayers = 0x00, kModelOffResource = 0x20, kModelOffPlace = 0x290, kResourceOffPath = 0x08, kResourceOffState = 0xC8;
constexpr uint8_t kResourceLoaded = 4;   // (the rebuild blocks on a model still loading: only a loaded one is touched)
constexpr uint32_t kLayersHidden = 0x80000000u;
constexpr uint64_t kMaxSceneModels = 50000;

// The path text of a resource: printable, ends ".model" (any case). False when it is not one.
template <class Rd>
inline bool ReadModelPath(Rd&& rd, uintptr_t res, char* out, size_t cap) {
    uintptr_t p = 0;
    out[0] = 0;
    if (res < 0x10000 || !rd(res + kResourceOffPath, &p, sizeof p) || p < 0x10000) return false;
    size_t n = 0;
    for (; n + 1 < cap; ++n) {
        char c = 0;
        if (!rd(p + n, &c, 1)) return false;
        if (!c) break;
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
        out[n] = c;
    }
    out[n] = 0;
    if (n < 6 || n + 1 >= cap) return false;
    const char* e = out + n - 6;
    return e[0] == '.' && (e[1] | 0x20) == 'm' && (e[2] | 0x20) == 'o' && (e[3] | 0x20) == 'd' && (e[4] | 0x20) == 'e' && (e[5] | 0x20) == 'l';
}

// A model file's key as the table writes it (the last path component without ".model", lowercase letters and digits), and whether the file is one of the effects' own meshes
// (data/hd/vfx/..., either slash, any case) - never a unit's body part.
// The loaded file carries its level of detail (the seventh test, read in the running game: "bonearmor_lod0.model" where the effect names "bonearmor.model"): a trailing
// "_lod<digits>" is not part of the key.
inline size_t ModelKey(const char* path, char* out, size_t cap) {
    char tmp[128];
    size_t n = path ? std::strlen(path) : 0;
    if (n >= 6 && path[n - 6] == '.') n -= 6;
    size_t d = n;
    while (d > 0 && path[d - 1] >= '0' && path[d - 1] <= '9') --d;
    if (d < n && d >= 4 && path[d - 4] == '_' && (path[d - 3] | 0x20) == 'l' && (path[d - 2] | 0x20) == 'o' && (path[d - 1] | 0x20) == 'd') n = d - 4;
    if (n >= sizeof tmp) n = sizeof tmp - 1;
    if (path) std::memcpy(tmp, path, n);
    tmp[n] = 0;
    return Key(tmp, out, cap);
}
inline bool IsVfxModel(const char* path) {
    static const char kPre[] = "data/hd/vfx/";
    if (!path) return false;
    for (size_t i = 0; i + 1 < sizeof kPre; ++i) {
        char c = path[i];
        if (!c) return false;
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != kPre[i]) return false;
    }
    return true;
}
// The solid table entry whose model key this is, or -1 (a model that is no solid effect's).
inline int SolidOfModelKey(const char* key) {
    if (!key || !*key) return -1;
    for (unsigned i = 0; i < table::kCount; ++i)
        if (table::kEntries[i].solid && table::kEntries[i].model[0] && std::strcmp(table::kEntries[i].model, key) == 0) return (int)i;
    return -1;
}

// What each model resource is (looked up once: a resource and its path stay while it is loaded; a resource whose path pointer changed is looked up again). -1 no solid
// effect's model, else the table entry.
class ResourceKinds {
public:
    static constexpr size_t kCapacity = 1024;
    // false: not known yet (the caller reads the path and Puts it)
    bool Get(uintptr_t res, uintptr_t pathPtr, int* entry) const {
        const E& e = e_[Slot(res)];
        if (e.res != res || e.path != pathPtr) return false;
        *entry = e.entry;
        return true;
    }
    void Put(uintptr_t res, uintptr_t pathPtr, int entry) { e_[Slot(res)] = E{res, pathPtr, entry}; }
private:
    struct E { uintptr_t res = 0, path = 0; int entry = -1; };
    static size_t Slot(uintptr_t res) { return (size_t)((res >> 4) * 0x9E3779B97F4A7C15ull >> 54) % kCapacity; }
    std::array<E, kCapacity> e_{};
};

// The entry a model resource is: its path read (ReadModelPath), kept only when it is under data/hd/vfx/ and its key is a solid effect's model. -1 otherwise (or unreadable).
template <class Rd>
inline int SolidOfResource(Rd&& rd, uintptr_t res, char* path, size_t cap) {
    if (!ReadModelPath(rd, res, path, cap) || !IsVfxModel(path)) return -1;
    char key[96];
    return ModelKey(path, key, sizeof key) ? SolidOfModelKey(key) : -1;
}

// [sky] summit_backdrop_hidden: the Arreat Summit's backdrop - the game's skybox01 (expansion_mountaintop_skybox, placed by MtnTop.json and ColossalSummit.json about 90 units
// below the mountain with a parallax component): a picture of the ground far below, made to be seen from above. Either slash, any case, any level of detail.
inline bool IsSummitBackdrop(const char* path) {
    static const char kDir[] = "/expansion_mountaintop_skybox/skybox";
    if (!path) return false;
    for (const char* p = path; *p; ++p) {
        size_t i = 0;
        for (; i + 1 < sizeof kDir; ++i) {
            char c = p[i];
            if (!c) return false;
            if (c == '\\') c = '/';
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            if (c != kDir[i]) break;
        }
        if (i + 1 == sizeof kDir) return true;
    }
    return false;
}

// The renderer's model list: false when it does not read as one (no data with a count, a count out of range).
template <class Rd>
inline bool ReadModelList(Rd&& rd, uintptr_t renderer, uintptr_t* data, uint64_t* count) {
    *data = 0;
    *count = 0;
    if (renderer < 0x10000 || !rd(renderer + kRendererOffModels, data, sizeof *data) || !rd(renderer + kRendererOffModels + 8, count, sizeof *count)) return false;
    return *count <= kMaxSceneModels && (*count == 0 || *data >= 0x10000);
}

// What to do with one model this frame: 1 hide it (set bit 31), -1 show it again (clear it), 0 leave it. off: its effect is not to be drawn; mine: it is the hero's (SeenModels);
// ours: we hid it. A model of another unit is never hidden, and one we hid that is no longer to be hidden is given back.
inline int ModelStep(bool off, bool mine, bool ours, uint32_t layers) {
    const bool hidden = (layers & kLayersHidden) != 0;
    if (off && mine) return hidden ? 0 : 1;
    if (ours) return hidden ? -1 : 0;
    return 0;
}

// The models we hid (so that exactly those are given back), each with when it was last seen in the renderer's list: one not seen for kForgetMs is gone (destroyed).
class HiddenModels {
public:
    static constexpr size_t kCapacity = 48;
    static constexpr uint64_t kForgetMs = 5000;
    bool Has(uintptr_t inst) const { for (const E& e : e_) if (e.inst == inst) return true; return false; }
    bool Add(uintptr_t inst, uint64_t now) {
        for (E& e : e_) if (e.inst == inst) { e.seen = now; return true; }
        for (E& e : e_) if (!e.inst) { e.inst = inst; e.seen = now; return true; }
        return false;
    }
    void Seen(uintptr_t inst, uint64_t now) { for (E& e : e_) if (e.inst == inst) e.seen = now; }
    void Remove(uintptr_t inst) { for (E& e : e_) if (e.inst == inst) e = E{}; }
    void Forget(uint64_t now) { for (E& e : e_) if (e.inst && now > e.seen + kForgetMs) e = E{}; }
    size_t Count() const { size_t n = 0; for (const E& e : e_) if (e.inst) ++n; return n; }
private:
    struct E { uintptr_t inst = 0; uint64_t seen = 0; };
    std::array<E, kCapacity> e_{};
};

// -- whose a model is (the seventh test, read in the running game: the three bonearmor_lod0.model instances keep no place - +0x260 is all zeros: the effect places them per particle -,
// so "at the hero's place" never holds for them). A solid effect's model is the hero's when the game made it while his overlay of that effect was being made: it first appears in
// the list within kClaimMs after the hero's overlay was tracked, or - an effect already on when the option first looks - it is there the first time the list is looked at for
// that effect while his overlay is live. One that appears at any other time (another necromancer's Bone Armor in a multiplayer game) is never touched. A model with a place
// (an entity's) is the hero's when it stands at his place.
constexpr uint64_t kClaimMs = 3000;
// heroOverlayAt: when the newest of the hero's overlays of this effect was tracked (0: none live); firstLook: the list is looked at for this effect for the first time since his
// overlay of it is live.
inline bool ClaimAtFirstSight(uint64_t now, uint64_t heroOverlayAt, bool firstLook) {
    if (!heroOverlayAt) return false;
    return firstLook || now <= heroOverlayAt + kClaimMs;
}

// The solid effects' models seen in the list, each with whether it is the hero's (decided once, at first sight) and the walk it was last seen in: one not seen in a walk is gone.
class SeenModels {
public:
    static constexpr size_t kCapacity = 64;
    // -1 not seen before, else 1 the hero's, 0 not
    int Mine(uintptr_t inst) const { for (const E& e : e_) if (e.inst == inst) return e.mine ? 1 : 0; return -1; }
    bool Add(uintptr_t inst, bool mine, uint32_t walk) {
        for (E& e : e_) if (!e.inst) { e = E{inst, walk, mine}; return true; }
        return false;
    }
    void Seen(uintptr_t inst, uint32_t walk) { for (E& e : e_) if (e.inst == inst) e.walk = walk; }
    void Prune(uint32_t walk) { for (E& e : e_) if (e.inst && e.walk != walk) e = E{}; }
    size_t Count() const { size_t n = 0; for (const E& e : e_) if (e.inst) ++n; return n; }
private:
    struct E { uintptr_t inst = 0; uint32_t walk = 0; bool mine = false; };
    std::array<E, kCapacity> e_{};
};

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
        bool solid = false;      // the table's mark: drawn or not drawn, never fainter
        State st;
        uint64_t addedAt = 0;
        bool seen = false;       // its effect component has been found once
        bool told = false;       // the log has said what it is
        bool toldNoFade = false;
        bool toldOnAgain = false;   // the log has said the game switched it back on
        SpeedState sp;              // [render] slow_own_effects
        float loggedSpeed = -1.0f;  // the last speed the log has said
        bool toldNoSpeed = false;
        float loggedFade = -1.0f;   // the last Fade the log has said
        char name[40] = {};
    };

    Slot* Add(uint32_t entity, Group g, const char* name, uint64_t now, bool solid = false) {
        if (entity == kNone || (entity & 0xFFFFF) == 0xFFFFF) return nullptr;
        Slot* freeSlot = nullptr;
        for (Slot& s : slots_) {
            if (s.entity == entity) { s.group = g; s.solid = solid; s.addedAt = now; return &s; }
            if (!freeSlot && s.entity == kNone) freeSlot = &s;
        }
        if (!freeSlot) return nullptr;
        *freeSlot = Slot{};
        freeSlot->entity = entity;
        freeSlot->group = g;
        freeSlot->solid = solid;
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
// [render] slow_own_effects, own_effects_speed_pct (percent 1..100): live.
void ConfigureSpeed(bool slow, float speedPct);
// [render] lower_own_effects, own_effects_height_cm (-150..+50; 0 = the game's place): live.
void ConfigureHeight(bool lower, float heightCm);
// The world's units a metre, as vrcam knows them (the view's height over the player's eye height); 0 = not known (nothing is moved).
void SetScaleSource(float (*unitsPerMetre)());
bool Wanted();
// Puts the two hooks that WATCH the creation of overlays in as soon as they are found, whether the option is on or not (they only look: the original always runs, nothing is
// changed; so an effect that is already on when the option is ticked live is known and is dimmed at once) - tried again now and then; and tells a status line while the option is on.
// Cheap to call every frame.
void Install();
// On the thread that may change the renderer's registry, every frame (vr/vrcam.cpp, the engine unit's setter hook): the opacity of each tracked effect.
void Tick();
// [sky] summit_backdrop_hidden: true = the Arreat Summit's backdrop model (IsSummitBackdrop) hidden, false = the instances we hid given back. Live, any thread.
void HideSummitBackdrop(bool hide);
// The same thread as Tick: walks the renderer's model list (every 250 ms while wanted or while one of ours is hidden). Independent of the effects' hooks.
void BackdropTick();

}  // namespace owneffects
