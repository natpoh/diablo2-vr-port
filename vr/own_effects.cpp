// own_effects.cpp - [render] dim_own_effects, the game's side: the hero's own effects (a state's overlays: Bone Armor, the armors, Energy Shield, the aura rings ...) drawn fainter in
// VR first person (F4). The pure part, the reasons and the offsets: vr/own_effects.h; the notes: docs/own_effects_recon.md. VISUAL ONLY: nothing of the server, the buff or the
// state is touched; the game's own draw of the effect is made fainter with the game's own named float parameter "Fade" of the effect (VfxSetFloat 0xF6A380 - what the game sets to
// the unit's alpha for every effect of its overlays, vr/own_effects.h), or at 0 % not drawn (VfxSetVisible). An effect the table marks SOLID (Bone Armor: its bones are an opaque mesh
// that binds no Fade) is not drawn whenever it is to be any fainter; one the game switches back on (its unit's alpha changed) is switched off again the next frame.
//
// Two hooks LOOK (the original always runs, unchanged): UnitTranslationLayer::BeginCreateOverlayInstance (the unit it is for: is it the local hero? then its key is remembered) and
// FinalizeCreateOverlayInstance (an overlay of the hero's unit, by key, whose definition's name is in the game-data table: its entities are tracked). They are put in as soon as the
// game's functions are found, WHETHER THE OPTION IS ON OR NOT - they only look - so that an effect already on when the option is ticked live (the usual way to try it) is known and is
// dimmed at once; the first run (0.170) hooked only once the option was on and so never saw a buff cast before it. A tick on the thread that may change the registry (the engine
// unit's setter hook, vr/vrcam.cpp; put in when the option is on) sets the Fade of each tracked effect once its effect instance is made, and again every frame (the game sets it itself
// when the unit's alpha changes); leaving F4 or unticking the option gives it all back. Every read and write of the game's memory is behind an SEH guard; a fault turns the option off for the session
// (the effects are drawn as before; what we had dimmed is not touched again).
//
// The log says what the hooks see (the first overlays of any unit, then every overlay of the hero's, and a status line while the option is on), so that a run that dims nothing
// shows where the chain breaks: no overlay created at all, the unit not the hero, the key, the definition's name, the table, the effect's Fade parameter.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <D2RLPlugin/api.h>

#include "log_async.h"
#include "own_effects.h"
#include "potion_hide.h"
#include "sigscan.h"
#include "skeletons.h"
#include "view_gate.h"

using namespace D2RL;

namespace owneffects {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_BEGIN = 0x762900, RVA_FINALIZE = 0x768780, RVA_VFX_SETFLOAT = 0xF6A380, RVA_FADE_NAME = 0x341D348;
constexpr uint64_t RVA_ECS_REGISTRY = 0x2677170, RVA_VFX_STORAGE = 0x66DBB0, RVA_VFX_SETVISIBLE = 0xF6A830;
constexpr uint64_t RVA_LOCAL_INDEX = 0x8B2D0, RVA_CLIENT_PLAYER = 0x9A480;
// mov [rsp+18h],r8; push rbp; push rbx; push rsi; push rdi; push r12; push r13; push r14
const uint8_t kSigBegin[15] = {0x4C, 0x89, 0x44, 0x24, 0x18, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
// mov [rsp+20h],rbx; push rbp; push rsi; push rdi; push r12; push r13; push r14
const uint8_t kSigFinalize[14] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
// VfxSetFloat: push rbx; push rsi; sub rsp,88h; mov rax,[rip+...]
const uint8_t kSigSetFloat[13] = {0x40, 0x53, 0x56, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05};
// the VFX storage getter and VfxSetVisible: the same bytes the potion hide checks
const uint8_t kSigVfxStorage[14] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48};
const uint8_t kSigVfxVisible[14] = {0x88, 0x51, 0x1C, 0x48, 0x8B, 0x49, 0x10, 0x48, 0x85, 0xC9, 0x0F, 0x85, 0x80, 0x04};

using BeginFn = uintptr_t (*)(uintptr_t self, uintptr_t ret, uint64_t key, uintptr_t unit, uintptr_t node);
using FinalizeFn = uintptr_t (*)(uintptr_t self, uintptr_t ret, uintptr_t state);
using FloatFn = uint8_t (*)(uintptr_t component, uintptr_t nameHandle, float value);   // true (al) when the effect took the parameter
using VfxStorageFn = uintptr_t (*)(uintptr_t registry);
using VfxVisibleFn = void (*)(uintptr_t component, uint8_t visible);

const PluginContext* g_ctx = nullptr;
std::atomic<bool> g_on{false}, g_faulted{false}, g_touched{false};   // g_touched: we have changed an effect (so the tick runs, to give it back, even with the option off)
std::atomic<float> g_body{0.30f}, g_aura{0.50f};
std::atomic<bool> g_slow{false};                 // [render] slow_own_effects
std::atomic<float> g_speed{0.20f};               // [render] own_effects_speed_pct / 100
std::atomic<bool> g_lower{false};                // [render] lower_own_effects
std::atomic<float> g_heightCm{0.0f};             // [render] own_effects_height_cm
std::atomic<int> g_state{0};   // 0 not tried, 1 hooks in, 2 not possible (another build?)
BeginFn g_origBegin = nullptr;
FinalizeFn g_origFinalize = nullptr;
uintptr_t (*g_seenUnit)() = nullptr;
bool (*g_seenId)(uint32_t*) = nullptr;
HeroKeys g_keys;               // the game thread's (the two hooks)
Tracker g_tracker;
Tracker g_cands;               // the table's overlays of units that are not known as the hero's (while no key of his is known): the place decides in the tick
SRWLOCK g_lock = SRWLOCK_INIT;   // the trackers: the finalize hook adds, the tick reads

// What the hooks have seen (the status line and the diagnostics).
std::atomic<unsigned> g_beginCalls{0}, g_beginHero{0}, g_beginNullUnit{0}, g_finCalls{0}, g_finHero{0}, g_finInTable{0}, g_finTracked{0}, g_ticks{0}, g_nameFails{0};
std::atomic<unsigned> g_diagBegin{0}, g_diagFin{0}, g_diagHero{0}, g_diagHeroFin{0}, g_diagCand{0}, g_candsSeen{0}, g_candsTaken{0};

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[640];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

void Fault(const char* where) {
    if (g_faulted.exchange(true)) return;
    LogF("own effects: a fault in %s - the option is off for this session; the effects are drawn as the game draws them (what was dimmed is not touched again)", where);
}

bool ReadAt(uintptr_t at, void* out, size_t n) noexcept {
    if (at < 0x10000) return false;
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ReadUnit(uintptr_t unit, UnitIds* u) noexcept {
    uint32_t t = 0, c = 0, i = 0;
    if (!ReadAt(unit + kUnitOffType, &t, sizeof t) || !ReadAt(unit + kUnitOffClass, &c, sizeof c) || !ReadAt(unit + kUnitOffId, &i, sizeof i)) return false;
    u->type = t; u->cls = c; u->id = i;
    return true;
}

// The local hero's unit: the game's own LocalIndex() and ClientPlayer(index), as the wrist's life and the wheel read them. 0 when it is not there (yet).
uintptr_t LocalHeroUnit() noexcept {
    const uintptr_t indexFn = d2rsig::Addr(RVA_LOCAL_INDEX), playerFn = d2rsig::Addr(RVA_CLIENT_PLAYER);
    if (!indexFn || !playerFn) return 0;
    __try {
        const int index = reinterpret_cast<int (*)()>(indexFn)();
        if (index < 0 || index >= 8) return 0;
        const uintptr_t unit = reinterpret_cast<uintptr_t (*)(int)>(playerFn)(index);
        if (unit < 0x10000 || *reinterpret_cast<const volatile uint32_t*>(unit) != 0) return 0;   // a player's unit has type 0
        return unit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// -- the hooks -------------------------------------------------------------------------------------------------------------------------------------------------------------

struct BeginLook {
    bool hero = false;
    bool isNew = false;
    bool unitRead = false;
    UnitIds u;
    uintptr_t local = 0, seen = 0;
    uint32_t localId = 0, seenId = 0;
    uint64_t node[8] = {};
    bool nodeRead = false;
};

// Begin: whose overlay is this? Only the key is kept; nothing is changed.
void LookAtBegin(uint64_t key, uintptr_t unit, uintptr_t node, BeginLook* b) noexcept {
    b->local = LocalHeroUnit();
    b->seen = g_seenUnit ? g_seenUnit() : 0;
    UnitIds lu, su;
    if (b->local && ReadUnit(b->local, &lu)) b->localId = lu.id;
    if (b->seen && ReadUnit(b->seen, &su)) b->seenId = su.id;
    uint32_t id = 0;
    if (!b->seenId && g_seenId && g_seenId(&id)) b->seenId = id;
    if (unit >= 0x10000) b->unitRead = ReadUnit(unit, &b->u);
    if (b->unitRead && IsHero(unit, b->u, b->local, b->localId, b->seen, b->seenId)) {
        b->hero = true;
        b->isNew = g_keys.Add(key);
    }
    if (b->hero && node >= 0x10000) b->nodeRead = ReadAt(node, b->node, sizeof b->node);
}

__declspec(noinline) uintptr_t HookBegin(uintptr_t self, uintptr_t ret, uint64_t key, uintptr_t unit, uintptr_t node) {
    if (!g_faulted.load(std::memory_order_relaxed)) {
        BeginLook b;
        ++g_beginCalls;
        if (unit < 0x10000) ++g_beginNullUnit;
        __try { LookAtBegin(key, unit, node, &b); } __except (EXCEPTION_EXECUTE_HANDLER) { Fault("the overlay-create look"); b.hero = false; }
        if (b.hero) ++g_beginHero;
        // the evidence: the first creations of any unit (what the hook sees), then the hero's
        const bool first = g_diagBegin.fetch_add(1) < 30;
        if (b.hero ? g_diagHero.fetch_add(1) < 60 : first) {
            LogF("own effects: overlay create - unit %p (%s), the hero is %p (id %u) / %p (id %u) -> %s; key %016llX%s", (void*)unit,
                 b.unitRead ? (b.u.type == 0 ? "a player" : b.u.type == 1 ? "a monster" : b.u.type == 3 ? "a missile" : "another kind") : (unit < 0x10000 ? "none" : "unreadable"),
                 (void*)b.local, b.localId, (void*)b.seen, b.seenId, b.hero ? "THE HERO'S" : "not the hero's", (unsigned long long)key, b.isNew ? " (a new key)" : "");
            if (b.hero && b.nodeRead)
                LogF("own effects:   its node %p: %016llX %016llX %016llX %016llX %016llX %016llX %016llX %016llX", (void*)node, (unsigned long long)b.node[0], (unsigned long long)b.node[1],
                     (unsigned long long)b.node[2], (unsigned long long)b.node[3], (unsigned long long)b.node[4], (unsigned long long)b.node[5], (unsigned long long)b.node[6],
                     (unsigned long long)b.node[7]);
        }
    }
    return g_origBegin(self, ret, key, unit, node);
}

struct Seen {
    bool hero = false;
    Group group = Group::None;
    bool nameOk = false;
    uint64_t key = 0;
    uintptr_t def = 0;
    char name[96] = {};
};

// Before the original: is this the hero's overlay, and what is it called? (The name is read for the hero's and for the first overlays of anyone: the log's evidence.)
void LookAtState(uintptr_t state, bool wantName, Seen* s) noexcept {
    if (!ReadAt(state + kStateOffKey, &s->key, sizeof s->key)) return;
    s->hero = g_keys.Has(s->key);
    if (!s->hero && !wantName) return;
    if (!ReadAt(state + kStateOffDef, &s->def, sizeof s->def)) return;
    s->nameOk = ReadName(&ReadAt, s->def, s->name, sizeof s->name);
    if (s->nameOk) s->group = GroupOf(s->name);
    else {   // the next test's evidence: what is at the definition's +0x60 .. +0x80
        uint64_t q[4] = {};
        ReadAt(s->def + 0x60, q, sizeof q);
        snprintf(s->name, sizeof s->name, "? def %p [+60]=%llX [+68]=%llX [+70]=%llX [+78]=%llX", (void*)s->def, (unsigned long long)q[0], (unsigned long long)q[1], (unsigned long long)q[2],
                 (unsigned long long)q[3]);
    }
}

// After the original: the instance is in the return slot; its entities are tracked.
unsigned TrackInto(Tracker* into, uintptr_t ret, Group g, const char* name, uint64_t now, uintptr_t* instOut) noexcept {
    uintptr_t inst = 0;
    uint32_t ents[kMaxEntities] = {};
    uint32_t n = 0;
    char key[40];
    Key(name, key, sizeof key);   // the slot keeps the short key ("bonearmorfront"), which the log then reads
    if (!ReadAt(ret, &inst, sizeof inst) || inst < 0x10000) return 0;   // not made yet (the definition is still loading): the game finalizes it later, through this same hook
    *instOut = inst;
    if (!ReadEntities(&ReadAt, inst, ents, kMaxEntities, &n)) return 0;
    const bool solid = SolidOf(name);
    AcquireSRWLockExclusive(&g_lock);
    unsigned added = 0;
    for (uint32_t i = 0; i < n; ++i)
        if (into->Add(ents[i], g, key, now, solid)) ++added;
    ReleaseSRWLockExclusive(&g_lock);
    return added;
}

__declspec(noinline) uintptr_t HookFinalize(uintptr_t self, uintptr_t ret, uintptr_t state) {
    Seen s;
    const bool active = !g_faulted.load(std::memory_order_relaxed);
    ++g_finCalls;
    const bool diagAny = g_diagFin.load(std::memory_order_relaxed) < 30;
    const bool noKey = !g_keys.Any();   // no key of the hero's known: the place decides, so every name is read
    if (active && state) {
        __try { LookAtState(state, diagAny || noKey, &s); } __except (EXCEPTION_EXECUTE_HANDLER) { Fault("the overlay-finalize look"); s.hero = false; }
    }
    const uintptr_t r = g_origFinalize(self, ret, state);
    if (s.hero) {
        ++g_finHero;
        unsigned added = 0;
        uintptr_t inst = 0;
        if (s.group != Group::None) {
            ++g_finInTable;
            if (!g_faulted.load(std::memory_order_relaxed)) {
                __try { added = TrackInto(&g_tracker, ret, s.group, s.name, GetTickCount64(), &inst); } __except (EXCEPTION_EXECUTE_HANDLER) { Fault("the overlay-instance look"); }
                g_finTracked += added;
            }
        }
        if (!s.nameOk) ++g_nameFails;
        if (g_diagHeroFin.fetch_add(1) < 60) {
            if (!s.nameOk) LogF("own effects: a hero's overlay was made (key %016llX), its definition's name could not be read - left as the game draws it (%s)", (unsigned long long)s.key, s.name);
            else if (s.group == Group::None) LogF("own effects: the hero's overlay \"%s\" is not one of the states' overlays (hit, cast of a skill ...) - left as the game draws it", s.name);
            else LogF("own effects: the hero's overlay \"%s\" is made - %s group%s, %u effect entit%s tracked (instance %p)", s.name, GroupName(s.group),
                      SolidOf(s.name) ? ", SOLID (a part of it is an opaque mesh the effect's Fade does not reach: under 100 % it is not drawn)" : "", added, added == 1 ? "y" : "ies", (void*)inst);
        } else if (g_diagHeroFin.load() == 61) Log("own effects: (no more lines about the hero's overlays)");
    } else {
        if (noKey && s.group != Group::None && active && !g_faulted.load(std::memory_order_relaxed)) {   // one of the table's, whoever's: kept as a candidate, the tick tells by its place
            unsigned added = 0;
            uintptr_t inst = 0;
            __try { added = TrackInto(&g_cands, ret, s.group, s.name, GetTickCount64(), &inst); } __except (EXCEPTION_EXECUTE_HANDLER) { Fault("the candidate look"); }
            g_candsSeen += added;
            if (added && g_diagCand.fetch_add(1) < 20) LogF("own effects: no key of the hero's is known - the table's overlay \"%s\" (%u effect entit%s) is a candidate: the tick takes it as his if it stands at his place", s.name, added, added == 1 ? "y" : "ies");
        }
        if (diagAny && g_diagFin.fetch_add(1) < 30)
            LogF("own effects: overlay finalize - key %016llX is not the hero's%s%s%s", (unsigned long long)s.key, s.def ? ", definition \"" : "", s.def ? s.name : "", s.def ? "\"" : "");
    }
    return r;
}

// -- the tick --------------------------------------------------------------------------------------------------------------------------------------------------------------

// The entity's effect component, effect instance and the component's visibility wish (1 / 0, -1 not read). 1 found (effect may be 0: not made yet), 0 the entity has none, -1 not usable.
int ResolveEffect(uint32_t entity, uintptr_t* component, uintptr_t* effect, int* shown) noexcept {
    __try {
        const uintptr_t reg = d2rsig::Addr(RVA_ECS_REGISTRY);
        const VfxStorageFn storageOf = reinterpret_cast<VfxStorageFn>(d2rsig::Addr(RVA_VFX_STORAGE));
        if (!reg || !storageOf) return -1;
        const uintptr_t storage = storageOf(reg);
        if (!storage) return 0;
        uintptr_t element = 0, comp = 0, inst = 0;
        if (!potionhide::StorageElement(&ReadAt, storage, entity, potionhide::kEffectElementSize, &element)) return 0;
        if (!ReadAt(element, &comp, sizeof comp) || comp < 0x10000) return 0;
        ReadAt(comp + kEffectOffInstance, &inst, sizeof inst);
        uint8_t vis = 0;
        *shown = ReadAt(comp + kEffectOffVisible, &vis, sizeof vis) ? (vis != 0 ? 1 : 0) : -1;
        *component = comp;
        *effect = inst > 0x10000 ? inst : 0;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// The world place of a TransformComponent: its translation (row 3 of the matrix) in the world matrix at +0x40, or in the local one at +0 when it has no parent - as the hero's is read.
bool PlaceOf(uintptr_t comp, float out[3]) noexcept {
    uint32_t parent = 0;
    if (!ReadAt(comp + 0xC0, &parent, sizeof parent)) return false;
    return ReadAt(comp + ((parent & 0xFFFFF) == 0xFFFFF ? 0x00 : 0x40) + 12 * 4, out, 3 * sizeof(float));
}

bool HeroPlace(float out[3]) noexcept {
    __try {
        skel::HeroXform hx;
        return skel::HeroTransform(&hx) && hx.comp && PlaceOf(hx.comp, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool EntityPlace(uint32_t entity, float out[3]) noexcept {
    __try {
        uintptr_t comp = 0;
        return skel::EntityTransform(entity, &comp) && comp && PlaceOf(comp, out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The game's "Fade" name object, proved from its own memory: the global at RVA_FADE_NAME holds a pointer to a string object whose characters read "Fade" (checked once it reads; asked
// again every two seconds until it does). The setter takes the global's address, as the game's own queue passes it.
std::atomic<bool> g_fadeOk{false};
bool FadeNameReady() {
    if (g_fadeOk.load(std::memory_order_relaxed)) return true;
    static ULONGLONG retryAt = 0;
    static bool toldBad = false;
    const ULONGLONG now = GetTickCount64();
    if (now < retryAt) return false;
    retryAt = now + 2000;
    const uintptr_t handle = d2rsig::Addr(RVA_FADE_NAME);
    char nm[8];
    if (ReadFadeName(&ReadAt, handle, nm)) {
        g_fadeOk.store(true);
        LogF("own effects: the game's overlay fade parameter: the string object at the global %p reads \"%s\" - it is the name the game's own queue (0x70E370 -> 0x790920 -> VfxSetFloat 0xF6A380) "
             "sets to the unit's alpha on every effect of an overlay; the same call is what dims here", (void*)handle, nm);
        return true;
    }
    if (!toldBad) {
        toldBad = true;
        LogF("own effects: the game's overlay fade parameter was NOT found (the global at %p does not lead to a string \"Fade\"; it reads \"%s\") - nothing can be made fainter, 0 %% still works", (void*)handle, nm);
    }
    return false;
}

// CanSwitchOff reads the effect's definition flag at the offsets the game's own effect setter (where VfxSetVisible goes on: the jne at +0xA) reads in THIS build - its bytes are
// checked once: mov rax,[rcx+40h] at +0xA, test byte [rax+188h],8 at +0x14. When they differ the log says the flag was not read.
bool SwitchFlagReadable() noexcept {
    static int ok = -1;   // the tick's thread only
    if (ok >= 0) return ok == 1;
    ok = 0;
    const uintptr_t vis = d2rsig::Addr(RVA_VFX_SETVISIBLE);
    int32_t rel = 0;
    if (!vis || !ReadAt(vis + 0xC, &rel, sizeof rel)) return false;
    const uintptr_t fn = vis + 0x10 + (intptr_t)rel;
    static const uint8_t a[4] = {0x48, 0x8B, 0x41, 0x40}, b[7] = {0xF6, 0x80, 0x88, 0x01, 0x00, 0x00, 0x08};
    uint8_t x[4] = {}, y[7] = {};
    ok = ReadAt(fn + 0xA, x, sizeof x) && ReadAt(fn + 0x14, y, sizeof y) && !std::memcmp(x, a, sizeof a) && !std::memcmp(y, b, sizeof b) ? 1 : 0;
    return ok == 1;
}

const char* SwitchWord(uintptr_t effect) {
    if (!SwitchFlagReadable()) return "its switch flag not read (another build?)";
    const int c = CanSwitchOff(&ReadAt, effect);
    return c == 1 ? "the effect takes the switch" : c == 0 ? "the effect's definition says it CANNOT be switched off - the switch does nothing to it" : "its switch flag not read";
}

// -- the solid part's own models (vr/own_effects.h: the bones are ModelInstances the effect's script makes; found in the renderer's list of models, hidden with the game's
// SetVisibleLayers, bit 31). The list is walked under the game's own lock on it (so no model is destroyed while it is read or changed), every 250 ms and at once when what
// is to be hidden changes; a model that appears in between shows for at most that long.
constexpr uint64_t RVA_SET_VISIBLE_LAYERS = 0xF3F840, RVA_GET_RENDERER = 0xE21140, RVA_LIST_LOCK = 0x122AD60, RVA_LIST_UNLOCK = 0x122AD90;
// mov r11,rsp; push rsi; sub rsp,70h; mov rax,[rip+...]
const uint8_t kSigSetLayers[11] = {0x4C, 0x8B, 0xDC, 0x56, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0x05};
// sub rsp,28h; mov rax,gs:[58h] (cleanroom/camera/game_layout.h)
const uint8_t kSigGetRenderer[13] = {0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00};
// the game's lock and unlock of an SRW lock: rex.w jmp [import]
const uint8_t kSigImportJump[3] = {0x48, 0xFF, 0x25};
using SetLayersFn = void (*)(uintptr_t instance, uint32_t mask);
using RendererFn = uintptr_t (*)();
using LockFn = void (*)(uintptr_t lock);
HiddenModels g_hiddenModels;   // the tick's thread only
ResourceKinds g_kinds;
int g_sceneOk = 0;             // 0 not resolved yet, 1 the game's, -1 not in this build (or a fault): the models are left alone

// All four through the signature table (cleanroom/sigscan/game_sigs.h: ModelSetLayers, GetRenderer, ModelListLock / ModelListUnlock - the last two found at the model factory's own calls,
// whose pattern holds the list's offset +0x2EFB8: found means the layout is the one read here). Asked again while the game may still be decrypting the code; said once, with where.
bool SceneReady() {
    if (g_sceneOk != 0) return g_sceneOk > 0;
    static const struct { uint64_t rva; const uint8_t* body; uint32_t size; const char* name; } kFns[] = {
        {RVA_SET_VISIBLE_LAYERS, kSigSetLayers, sizeof kSigSetLayers, "ModelInstance::SetVisibleLayers"},
        {RVA_GET_RENDERER, kSigGetRenderer, sizeof kSigGetRenderer, "GetRenderer"},
        {RVA_LIST_LOCK, kSigImportJump, sizeof kSigImportJump, "the model list's lock"},
        {RVA_LIST_UNLOCK, kSigImportJump, sizeof kSigImportJump, "the model list's unlock"},
    };
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    const ULONGLONG t = GetTickCount64();
    if (t < retryAt) return false;
    retryAt = t + 2000;
    char where[400];
    size_t w = 0;
    for (const auto& f : kFns) {
        const bool gone = d2rsig::NotInBuild(f.rva);
        if (!d2rsig::Addr(f.rva) && !gone && ++tries < 30) return false;   // (not found yet: asked again)
        const bool ok = d2rsig::Addr(f.rva) && d2rsig::Check(f.rva, f.body, f.size);
        const int n = std::snprintf(where + w, sizeof where - w, "%s%s %s", w ? ", " : "", f.name,
                                    ok ? "" : d2rsig::Addr(f.rva) ? "NOT as expected" : "NOT FOUND");
        if (n > 0) w = std::min(sizeof where - 1, w + (size_t)n);
        if (ok) {
            const int m = std::snprintf(where + w, sizeof where - w, "at RVA 0x%llX", (unsigned long long)d2rsig::Rva(f.rva));
            if (m > 0) w = std::min(sizeof where - 1, w + (size_t)m);
        } else {
            g_sceneOk = -1;
        }
    }
    if (g_sceneOk == 0) g_sceneOk = 1;
    LogF("own effects: the game's model functions (by signature, 3.3.93787's addresses moved where this build has them): %s - %s", where,
         g_sceneOk > 0 ? "the solid effects' models (Bone Armor's bones) can be hidden" : "the solid effects' models (Bone Armor's bones) are left as the game draws them");
    return g_sceneOk > 0;
}

bool SetLayers(uintptr_t inst, uint32_t mask) noexcept {
    __try { reinterpret_cast<SetLayersFn>(d2rsig::Addr(RVA_SET_VISIBLE_LAYERS))(inst, mask); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uintptr_t Renderer() noexcept {
    __try { return reinterpret_cast<RendererFn>(d2rsig::Addr(RVA_GET_RENDERER))(); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool ListLock(uintptr_t lock, bool take) noexcept {
    __try { reinterpret_cast<LockFn>(d2rsig::Addr(take ? RVA_LIST_LOCK : RVA_LIST_UNLOCK))(lock); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

struct SceneCount {
    uint64_t models = 0;    // in the renderer's list
    int solid = 0;          // of a solid effect's model file
    int mine = 0;           // of those, the hero's
    int newMine = 0, newOther = 0;   // seen for the first time now: taken as the hero's / not
    int hidden = 0, shown = 0, again = 0;   // switched off / given back now; shown again by the game
    int entry = -1;         // the first solid one's table entry (the log)
    char path[96] = {};
};
SeenModels g_seen;             // the tick's thread only
uint32_t g_walk = 0;

// One walk of the list, under its lock. heroAt[e]: when the hero's newest overlay of table entry e was tracked (0: none live); firstLook[e]: the list has not been looked at for e
// since then. False: a fault in the game's setter.
bool SceneWalk(uintptr_t data, uint64_t count, const bool* offEntry, const uint64_t* heroAt, const bool* firstLook, const float hero[3], bool heroOk, uint64_t now,
               SceneCount* c) noexcept {
    ++g_walk;
    for (uint64_t i = 0; i < count; ++i) {
        uintptr_t inst = 0, res = 0, pathPtr = 0;
        if (!ReadAt(data + i * 8, &inst, sizeof inst) || inst < 0x10000 || !ReadAt(inst + kModelOffResource, &res, sizeof res) || res < 0x10000 ||
            !ReadAt(res + kResourceOffPath, &pathPtr, sizeof pathPtr))
            continue;
        int entry = -1;
        char path[96] = {};
        if (!g_kinds.Get(res, pathPtr, &entry)) {
            entry = SolidOfResource(&ReadAt, res, path, sizeof path);
            g_kinds.Put(res, pathPtr, entry);
        }
        if (entry < 0) continue;
        ++c->solid;
        if (c->entry < 0) {
            c->entry = entry;
            if (!path[0]) ReadModelPath(&ReadAt, res, path, sizeof path);
            std::memcpy(c->path, path, sizeof c->path);
        }
        int mine = g_seen.Mine(inst);
        if (mine < 0) {   // first sight: whose is it?
            float p[3] = {};
            const bool placed = ReadAt(inst + kModelOffPlace, p, sizeof p) && std::isfinite(p[0] + p[1] + p[2]) && (p[0] != 0.0f || p[1] != 0.0f || p[2] != 0.0f);
            mine = (placed ? heroOk && NearHero(hero, p) : ClaimAtFirstSight(now, heroAt[entry], firstLook[entry])) ? 1 : 0;
            if (g_seen.Add(inst, mine != 0, g_walk)) ++(mine ? c->newMine : c->newOther);
            else mine = 0;   // (no room to remember it: left as the game draws it)
        } else {
            g_seen.Seen(inst, g_walk);
        }
        if (mine) ++c->mine;
        uint8_t state = 0;
        uint32_t layers = 0;
        if (!ReadAt(res + kResourceOffState, &state, 1) || state != kResourceLoaded || !ReadAt(inst + kModelOffLayers, &layers, sizeof layers)) continue;
        const bool ours = g_hiddenModels.Has(inst);
        const int step = ModelStep(offEntry[entry], mine != 0, ours, layers);
        if (step > 0) {
            if (!g_hiddenModels.Add(inst, now)) continue;   // (no room to remember it: not hidden, so it is never left hidden)
            if (!SetLayers(inst, layers | kLayersHidden)) { g_hiddenModels.Remove(inst); return false; }
            ++c->hidden;
            if (ours) ++c->again;
        } else if (step < 0) {
            if (!SetLayers(inst, layers & ~kLayersHidden)) return false;
            g_hiddenModels.Remove(inst);
            ++c->shown;
        } else if (ours) {
            if (layers & kLayersHidden) g_hiddenModels.Seen(inst, now);
            else g_hiddenModels.Remove(inst);   // shown by the game and not to be hidden: nothing of ours left on it
        }
    }
    g_seen.Prune(g_walk);
    return true;
}

// offEntry[i]: the solid table entry i has an effect of the hero's that is not to be drawn now (F4, under 100 %); heroAt[i]: when his newest overlay of it was tracked (0: none
// live). anySolid: a solid effect of his is tracked at all.
void ScenePass(const bool* offEntry, const uint64_t* heroAt, bool anySolid, uint64_t now) {
    static uint64_t nextAt = 0, lastSig = ~0ull;
    static bool toldList = false, toldAgain = false;
    static int toldHidden = -1, toldSolid = -1;
    static uint64_t lookedAt[table::kCount] = {};   // the heroAt the list was last looked at for (a new live overlay: a first look again)
    if (!anySolid && !g_hiddenModels.Count()) return;
    uint64_t sig = 1469598103934665603ull;
    for (unsigned i = 0; i < table::kCount; ++i) sig = (sig ^ (offEntry[i] ? i + 1 : 0) ^ (heroAt[i] << 8)) * 1099511628211ull;
    if (now < nextAt && sig == lastSig) return;
    nextAt = now + 250;
    lastSig = sig;
    if (!SceneReady()) return;
    bool firstLook[table::kCount] = {};
    for (unsigned i = 0; i < table::kCount; ++i) {
        firstLook[i] = heroAt[i] && !lookedAt[i];
        lookedAt[i] = heroAt[i] ? (lookedAt[i] ? lookedAt[i] : heroAt[i]) : 0;   // (no live overlay of it: the next one is a first look again)
    }
    float hero[3] = {};
    const bool heroOk = HeroPlace(hero);
    const uintptr_t renderer = Renderer();
    uintptr_t data = 0;
    uint64_t count = 0;
    if (!renderer || !ReadModelList(&ReadAt, renderer, &data, &count)) {
        if (!toldList) { toldList = true; LogF("own effects: the renderer's model list does not read (renderer %p +0x%X) - the solid effects' models are left as the game draws them", (void*)renderer, (unsigned)kRendererOffModels); }
        return;
    }
    const uintptr_t lock = renderer + kRendererOffModelsLock;
    if (!ListLock(lock, true)) { g_sceneOk = -1; Fault("the model list's lock"); return; }
    SceneCount c;
    ReadModelList(&ReadAt, renderer, &data, &count);   // (again, now that it is locked)
    c.models = count;
    const bool ok = SceneWalk(data, count, offEntry, heroAt, firstLook, hero, heroOk, now, &c);
    ListLock(lock, false);
    if (!ok) { g_sceneOk = -1; Fault("the models' layers"); return; }
    g_hiddenModels.Forget(now);
    if (c.again && !toldAgain) {
        toldAgain = true;
        LogF("own effects: a solid effect's model we hid was shown again by the game - hidden again (said once)");
    }
    const int nowHidden = (int)g_hiddenModels.Count();
    if (c.hidden || c.shown || c.newMine || c.newOther || nowHidden != toldHidden || (c.solid != toldSolid && c.solid == 0)) {
        LogF("own effects: the renderer's models: %llu; %d of the solid effects' model files (\"%s\", entry \"%s\"), %d of them the hero's (new now: %d his - made within %llu ms after his "
             "overlay, or there at the first look -, %d another unit's) - %d hidden now, %d given back, %d of ours hidden in all (ModelInstance::SetVisibleLayers 0xF3F840, bit 31: "
             "the whole model, as the game hides an overlay's model at its unit's alpha 0)", (unsigned long long)c.models, c.solid, c.path, c.entry >= 0 ? table::kEntries[c.entry].name : "-",
             c.mine, c.newMine, (unsigned long long)kClaimMs, c.newOther, c.hidden, c.shown, nowHidden);
        toldHidden = nowHidden;
        toldSolid = c.solid;
    }
}

// -- [sky] summit_backdrop_hidden: the Arreat Summit's ground picture (IsSummitBackdrop) hidden in the first-person views --------------------------------------------------
// The same list and the same setter as the solid effects' models (bit 31: the whole model). Only the instances we hid are given back.
std::atomic<bool> g_backdropWant{false};
HiddenModels g_hiddenBackdrop;   // the tick's thread only
ResourceKinds g_backdropKinds;   // 1 the backdrop's resource, -1 not
bool g_backdropFault = false;

void BackdropPass(uint64_t now) {
    static uint64_t nextAt = 0;
    static int toldHidden = -1;
    const bool want = g_backdropWant.load(std::memory_order_relaxed);
    if (g_backdropFault || (!want && !g_hiddenBackdrop.Count())) return;
    if (now < nextAt) return;
    nextAt = now + 250;
    if (!SceneReady()) return;
    const uintptr_t renderer = Renderer();
    uintptr_t data = 0;
    uint64_t count = 0;
    if (!renderer || !ReadModelList(&ReadAt, renderer, &data, &count)) return;
    const uintptr_t lock = renderer + kRendererOffModelsLock;
    if (!ListLock(lock, true)) { g_backdropFault = true; Log("sky: the model list's lock faulted - the Summit's backdrop is left as the game draws it"); return; }
    ReadModelList(&ReadAt, renderer, &data, &count);   // (again, now that it is locked)
    int found = 0, hidden = 0, shown = 0;
    bool ok = true;
    for (uint64_t i = 0; i < count && ok; ++i) {
        uintptr_t inst = 0, res = 0, pathPtr = 0;
        if (!ReadAt(data + i * 8, &inst, sizeof inst) || inst < 0x10000 || !ReadAt(inst + kModelOffResource, &res, sizeof res) || res < 0x10000 ||
            !ReadAt(res + kResourceOffPath, &pathPtr, sizeof pathPtr))
            continue;
        int kind = -1;
        if (!g_backdropKinds.Get(res, pathPtr, &kind)) {
            char path[160];
            kind = ReadModelPath(&ReadAt, res, path, sizeof path) && IsSummitBackdrop(path) ? 1 : -1;
            g_backdropKinds.Put(res, pathPtr, kind);
        }
        if (kind < 0) continue;
        ++found;
        uint8_t state = 0;
        uint32_t layers = 0;
        if (!ReadAt(res + kResourceOffState, &state, 1) || state != kResourceLoaded || !ReadAt(inst + kModelOffLayers, &layers, sizeof layers)) continue;
        const bool ours = g_hiddenBackdrop.Has(inst);
        const int step = ModelStep(want, true, ours, layers);
        if (step > 0) {
            if (!g_hiddenBackdrop.Add(inst, now)) continue;   // (no room to remember it: not hidden, so it is never left hidden)
            if (!SetLayers(inst, layers | kLayersHidden)) { g_hiddenBackdrop.Remove(inst); ok = false; break; }
            ++hidden;
        } else if (step < 0) {
            if (!SetLayers(inst, layers & ~kLayersHidden)) { ok = false; break; }
            g_hiddenBackdrop.Remove(inst);
            ++shown;
        } else if (ours) {
            if (layers & kLayersHidden) g_hiddenBackdrop.Seen(inst, now);
            else g_hiddenBackdrop.Remove(inst);
        }
    }
    ListLock(lock, false);
    if (!ok) { g_backdropFault = true; Log("sky: the game's model setter faulted - the Summit's backdrop is left as the game draws it"); return; }
    g_hiddenBackdrop.Forget(now);
    const int nowHidden = (int)g_hiddenBackdrop.Count();
    if (hidden || shown || nowHidden != toldHidden) {
        LogF("sky: the Arreat Summit's backdrop (expansion_mountaintop_skybox/skybox01, a picture of the ground far below): %d in the renderer's list, %d hidden now, %d given back, "
             "%d hidden in all - %s ([sky] summit_backdrop_hidden)", found, hidden, shown, nowHidden,
             want ? "first person: our sky and fog in its place" : "shown as the game draws it");
        toldHidden = nowHidden;
    }
}

// -- [render] slow_own_effects: the effect's own playback speed (vr/own_effects.h: SpeedFor, DecideSpeed) ----------------------------------------------------------------------
constexpr uint64_t RVA_SET_SPEED = 0xA4ABB0;
// push rdi; sub rsp,30h; mov rax,[rcx+40h]; mov rdi,rcx; movaps [rsp+20h],xmm6 (game_sigs.h: EffectSetSpeed)
const uint8_t kSigSetSpeed[18] = {0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x41, 0x40, 0x48, 0x8B, 0xF9, 0x0F, 0x29, 0x74, 0x24, 0x20};
using SpeedFn = void (*)(uintptr_t effect, float speed);
int g_speedOk = 0;   // 0 not resolved yet, 1 the game's, -1 not in this build (the effects turn at the game's speed)

bool SpeedReady() {
    if (g_speedOk != 0) return g_speedOk > 0;
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    const ULONGLONG t = GetTickCount64();
    if (t < retryAt) return false;
    retryAt = t + 2000;
    if (!d2rsig::Addr(RVA_SET_SPEED) && !d2rsig::NotInBuild(RVA_SET_SPEED) && ++tries < 30) return false;   // (not found yet: asked again)
    g_speedOk = d2rsig::Addr(RVA_SET_SPEED) && d2rsig::Check(RVA_SET_SPEED, kSigSetSpeed, sizeof kSigSetSpeed) ? 1 : -1;
    if (g_speedOk > 0)
        LogF("own effects: the effect's playback speed (by signature EffectSetSpeed, 3.3.93787's 0xA4ABB0) is at RVA 0x%llX in this build - the solid effects can be slowed",
             (unsigned long long)d2rsig::Rva(RVA_SET_SPEED));
    else
        LogF("own effects: the effect's playback speed (EffectSetSpeed, 3.3.93787's 0xA4ABB0) is %s in this build - the solid effects turn at the game's speed",
             d2rsig::Addr(RVA_SET_SPEED) ? "NOT as expected" : "NOT FOUND");
    return g_speedOk > 0;
}

bool SetSpeed(uintptr_t effect, float speed) noexcept {
    __try { reinterpret_cast<SpeedFn>(d2rsig::Addr(RVA_SET_SPEED))(effect, speed); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// One tracked solid effect's speed this frame. False: a fault in the game's setter.
bool SpeedPass(Tracker::Slot& s, uintptr_t effect, float target, bool f4, const Settings& set) noexcept {
    if (!SpeedReady()) return true;
    if (target < 1.0f - 0.004f) {
        const int can = CanChangeSpeed(&ReadAt, effect);
        if (can == 0) {
            if (!s.toldNoSpeed) {
                s.toldNoSpeed = true;
                LogF("own effects: \"%s\" (entity %08X) - its effect's definition does not let its speed change (effect %p) - it turns at the game's speed", s.name, s.entity, (void*)effect);
            }
            return true;
        }
    }
    float cur = NAN;
    if (!ReadAt(effect + kFxOffSpeed, &cur, sizeof cur)) cur = NAN;
    const float v = DecideSpeed(target, cur, effect, s.sp);
    if (v >= 0.0f && !SetSpeed(effect, v)) return false;
    ApplySpeed(v, effect, &s.sp);
    if (v >= 0.0f && std::fabs(v - s.loggedSpeed) > 0.004f) {
        s.loggedSpeed = v;
        if (v < 1.0f - 0.004f)
            LogF("own effects: \"%s\" (entity %08X) - its playback speed set to %.0f %% of the game's (EffectSetSpeed, effect %p; it read %.2f) - it turns slower in F4, visual only", s.name,
                 s.entity, (double)v * 100.0, (void*)effect, (double)cur);
        else
            LogF("own effects: \"%s\" (entity %08X) - its playback speed given back (100 %%: %s)", s.name, s.entity, !set.slow ? "slow_own_effects is off" : !f4 ? "not F4" : "the slider is at 100");
    }
    return true;
}

// -- [render] lower_own_effects: the hero's own solid overlays drawn lower (vr/own_effects.h: LiftFor, LiftMatrix). The tick (the registry's thread) publishes which entities
// are lifted by how much; the writer at the world-matrix function (the renderer's thread, vr/potion_hide.cpp's hook) moves their matrices; the tick gives a matrix back when
// its entity is no longer to be lifted and the game has not placed it again since.
constexpr size_t kLifts = 16;
constexpr size_t kXformWorld = 0x40, kXformPrev = 0x80;   // a TransformComponent's world matrix and last frame's (as the potion hide writes them)
struct Lift {
    std::atomic<uint32_t> entity{Tracker::kNone};
    std::atomic<float> dy{0.0f};          // world units now (0: not lifted)
    std::atomic<float> applied{0.0f};     // the lift of the last matrix written (what a give-back takes off)
    std::atomic<float> wroteY{NAN};       // the height of the last matrix written
    std::atomic<uint32_t> hits{0};        // matrices written
    uint64_t since = 0;                   // (the tick's) when it was published
    bool told = false, toldPlaced = false, toldNever = false;
    char name[40] = {};
};
Lift g_lifts[kLifts];
float (*g_upm)() = nullptr;

// The renderer's thread, after the game computed the entity's world matrix: one of ours? (a scan of 16 - most calls end at the first compare with kNone)
void OnXform(uint32_t entity, uintptr_t component, uintptr_t out) {
    if (entity == Tracker::kNone) return;
    for (Lift& l : g_lifts) {
        if (l.entity.load(std::memory_order_relaxed) != entity) continue;
        const float dy = l.dy.load(std::memory_order_relaxed);
        if (dy == 0.0f || !viewgate::F4() || out < 0x10000 || component < 0x10000) return;
        __try {
            float m[16];
            std::memcpy(m, reinterpret_cast<const void*>(out), sizeof m);
            if (!LiftMatrix(m, dy)) return;
            std::memcpy(reinterpret_cast<void*>(out), m, sizeof m);
            std::memcpy(reinterpret_cast<void*>(component + kXformWorld), m, sizeof m);
            std::memcpy(reinterpret_cast<void*>(component + kXformPrev), m, sizeof m);
            l.applied.store(dy, std::memory_order_relaxed);
            l.wroteY.store(m[13], std::memory_order_relaxed);
            l.hits.fetch_add(1, std::memory_order_relaxed);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return;
    }
}

// A lifted matrix given back (the tick's thread): when the entity's world height is still the one we wrote, the lift is taken off it.
void GiveBack(Lift& l) noexcept {
    const uint32_t e = l.entity.load();
    const float applied = l.applied.load(), wrote = l.wroteY.load();
    if (e == Tracker::kNone || l.hits.load() == 0 || applied == 0.0f) return;
    __try {
        uintptr_t comp = 0;
        float y = NAN;
        if (!skel::EntityTransform(e, &comp) || !comp || !ReadAt(comp + kXformWorld + 13 * 4, &y, sizeof y) || !StillLifted(y, wrote)) return;   // (placed again by the game: nothing of ours on it)
        const float back = y - applied;
        std::memcpy(reinterpret_cast<void*>(comp + kXformWorld + 13 * 4), &back, sizeof back);
        float yp = NAN;
        if (ReadAt(comp + kXformPrev + 13 * 4, &yp, sizeof yp) && StillLifted(yp, wrote)) std::memcpy(reinterpret_cast<void*>(comp + kXformPrev + 13 * 4), &back, sizeof back);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

struct LiftWant {
    uint32_t entity;
    float dy;
    const char* name;
};

// The tick's thread: publish this frame's lifts; give back the ones that are no longer; say per effect what was done.
void PublishLifts(const LiftWant* want, size_t n, float heightCm, float upm, uint64_t now) {
    for (Lift& l : g_lifts) {   // the ones no longer wanted (or no longer lifted): given back, let go
        const uint32_t e = l.entity.load();
        if (e == Tracker::kNone) continue;
        float dy = 0.0f;
        for (size_t i = 0; i < n; ++i) if (want[i].entity == e) dy = want[i].dy;
        if (dy != 0.0f) continue;
        GiveBack(l);
        if (l.told) LogF("own effects: \"%s\" (entity %08X) - its height given back (the game's place: %s)", l.name, e, !g_lower.load() ? "lower_own_effects is off" : "not F4, or the slider at 0");
        l.dy.store(0.0f);
        l.entity.store(Tracker::kNone);
        l.hits.store(0);
        l.applied.store(0.0f);
        l.wroteY.store(NAN);
        l.told = l.toldPlaced = l.toldNever = false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (want[i].dy == 0.0f) continue;
        Lift* slot = nullptr;
        for (Lift& l : g_lifts) if (l.entity.load() == want[i].entity) { slot = &l; break; }
        if (!slot) {
            for (Lift& l : g_lifts) if (l.entity.load() == Tracker::kNone) { slot = &l; break; }
            if (!slot) break;
            std::snprintf(slot->name, sizeof slot->name, "%s", want[i].name);
            slot->since = now;
            slot->dy.store(want[i].dy);
            slot->entity.store(want[i].entity);
        } else if (slot->dy.load() != want[i].dy) {
            slot->dy.store(want[i].dy);
            slot->told = false;   // (the slider moved: told again)
        }
        Lift& l = *slot;
        if (!l.told) {
            l.told = true;
            LogF("own effects: \"%s\" (entity %08X) - drawn %.0f cm %s in F4 (%.2f world units at %.2f units a metre; the overlay's world matrix as the game computed it, moved along its "
                 "up after the world-matrix function 0xE3A8C0 - visual only)", l.name, l.entity.load(), (double)std::fabs(heightCm), heightCm < 0 ? "lower" : "higher", (double)want[i].dy, (double)upm);
        }
        const uint32_t hits = l.hits.load();
        if (hits && !l.toldPlaced) {
            l.toldPlaced = true;
            LogF("own effects: \"%s\" (entity %08X) - the renderer has placed it lowered (%u matrices so far; now at height %.2f)", l.name, l.entity.load(), hits, (double)l.wroteY.load());
        } else if (!hits && !l.toldNever && now > l.since + 3000) {
            l.toldNever = true;
            LogF("own effects: \"%s\" (entity %08X) - the world-matrix function has not placed this entity in 3 s - it is not lowered (its effect is placed some other way)", l.name, l.entity.load());
        }
    }
}

size_t LiftsLive() { size_t n = 0; for (const Lift& l : g_lifts) if (l.entity.load(std::memory_order_relaxed) != Tracker::kNone) ++n; return n; }

bool Write(uintptr_t component, const Plan& p, bool* fadeOk) noexcept {
    __try {
        if (p.visible >= 0) reinterpret_cast<VfxVisibleFn>(d2rsig::Addr(RVA_VFX_SETVISIBLE))(component, (uint8_t)p.visible);
        if (p.setFade) *fadeOk = reinterpret_cast<FloatFn>(d2rsig::Addr(RVA_VFX_SETFLOAT))(component, d2rsig::Addr(RVA_FADE_NAME), p.fade) != 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The status line: what the hooks have seen and what is tracked. Told when the option is ticked and, while it is on, when something has changed (at most every 20 s).
void Status(const char* why) {
    size_t tracked = 0, made = 0;
    AcquireSRWLockShared(&g_lock);
    for (const Tracker::Slot& s : g_tracker.Slots())
        if (s.entity != Tracker::kNone) { ++tracked; if (s.seen) ++made; }
    ReleaseSRWLockShared(&g_lock);
    LogF("own effects: status (%s) - hooks %s; the Fade parameter %s; overlays created: %u seen by Begin (%u for the hero, %u with no unit), %u finalized (%u the hero's, %u in the table, %u name unreadable); %zu effect "
         "entit%s tracked (%zu with an effect component); the tick has run %u time%s%s; a key of the hero's is %s; %u candidate%s seen by place, %u taken",
         why, g_state.load() == 1 ? "in" : g_state.load() == 2 ? "NOT in" : "not yet in", g_fadeOk.load() ? "found" : "not found (yet)", g_beginCalls.load(), g_beginHero.load(), g_beginNullUnit.load(), g_finCalls.load(), g_finHero.load(),
         g_finInTable.load(), g_nameFails.load(), tracked, tracked == 1 ? "y" : "ies", made, g_ticks.load(), g_ticks.load() == 1 ? "" : "s", g_faulted.load() ? "; FAULTED" : "", g_keys.Any() ? "known" : "NOT known", g_candsSeen.load(), g_candsSeen.load() == 1 ? "" : "s",
         g_candsTaken.load());
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    potionhide::SetXformEffectsWriter(&OnXform);   // [render] lower_own_effects: the hero's own solid overlays lowered (writes only their matrices)
    if (g_on.load()) Log("own effects: on ([render] dim_own_effects) - the hero's own effects are drawn fainter in first person (visual only, F4)");
}

void SetHeroSource(uintptr_t (*unit)(), bool (*id)(uint32_t*)) { g_seenUnit = unit; g_seenId = id; }

void Configure(bool on, float bodyPct, float auraPct) {
    g_body.store(ClampPct(bodyPct) / 100.0f);
    g_aura.store(ClampPct(auraPct) / 100.0f);
    const bool was = g_on.exchange(on);
    if (on != was) {
        if (on)
            LogF("own effects: on ([render] dim_own_effects) - the hero's own effects are drawn at %.0f %% (the aura rings at %.0f %%), visual only, first person (F4) only", (double)ClampPct(bodyPct),
                 (double)ClampPct(auraPct));
        else
            Log("own effects: off ([render] dim_own_effects=0) - the effects are drawn as the game draws them");
        if (on && g_ctx) Status("ticked");
    }
}

void ConfigureSpeed(bool slow, float speedPct) {
    g_speed.store(ClampSpeedPct(speedPct) / 100.0f);
    const bool was = g_slow.exchange(slow);
    if (slow != was)
        LogF(slow ? "own effects: slow on ([render] slow_own_effects) - the hero's solid effects (Bone Armor ...) turn at %.0f %% of the game's speed in first person (F4), visual only"
                  : "own effects: slow off ([render] slow_own_effects=0) - the solid effects turn at the game's speed (%.0f %% kept for when it is ticked)",
             (double)ClampSpeedPct(speedPct));
}

void ConfigureHeight(bool lower, float heightCm) {
    g_heightCm.store(ClampHeightCm(heightCm));
    const bool was = g_lower.exchange(lower);
    potionhide::WantHooksForEffects(lower);
    if (lower != was)
        LogF(lower ? "own effects: height on ([render] lower_own_effects) - the hero's solid effects (Bone Armor ...) are drawn %.0f cm from the game's place in first person (F4), visual only"
                   : "own effects: height off ([render] lower_own_effects=0) - the solid effects are drawn where the game puts them (%.0f cm kept for when it is ticked)",
             (double)ClampHeightCm(heightCm));
}

void SetScaleSource(float (*unitsPerMetre)()) { g_upm = unitsPerMetre; }

bool Wanted() { return g_on.load() || g_slow.load() || g_lower.load(); }

void HideSummitBackdrop(bool hide) { g_backdropWant.store(hide, std::memory_order_relaxed); }
void BackdropTick() { BackdropPass(GetTickCount64()); }

void Install() {
    static ULONGLONG retryAt = 0, statusAt = 0;
    static unsigned tries = 0;
    static unsigned lastBegin = ~0u, lastFin = ~0u;
    if (!g_ctx) return;
    const ULONGLONG now = GetTickCount64();
    if (g_state.load() == 1 && g_on.load() && now >= statusAt) {   // while the option is on: one status line when the numbers have moved
        statusAt = now + 20000;
        const unsigned b = g_beginCalls.load(), f = g_finCalls.load();
        if (b != lastBegin || f != lastFin) { lastBegin = b; lastFin = f; Status("while on"); }
    }
    if (g_state.load() != 0) return;
    if (now < retryAt) return;
    retryAt = now + (tries < 30 ? 2000 : 30000);   // the signature search may still be waiting
    if (!d2rsig::Addr(RVA_BEGIN) || !d2rsig::Addr(RVA_FINALIZE) || !d2rsig::Addr(RVA_VFX_SETFLOAT) || !d2rsig::Addr(RVA_FADE_NAME)) return;
    const bool fx = d2rsig::Addr(RVA_ECS_REGISTRY) && d2rsig::Addr(RVA_VFX_STORAGE) && d2rsig::Addr(RVA_VFX_SETVISIBLE) &&
                    d2rsig::Check(RVA_VFX_STORAGE, kSigVfxStorage, sizeof kSigVfxStorage) && d2rsig::Check(RVA_VFX_SETVISIBLE, kSigVfxVisible, sizeof kSigVfxVisible);
    if (!fx || !d2rsig::Check(RVA_BEGIN, kSigBegin, sizeof kSigBegin) || !d2rsig::Check(RVA_FINALIZE, kSigFinalize, sizeof kSigFinalize) ||
        !d2rsig::Check(RVA_VFX_SETFLOAT, kSigSetFloat, sizeof kSigSetFloat)) {
        if (++tries == 30) {
            g_state.store(2);
            Log("own effects: the game's overlay functions are not where 3.3.93787 has them (another build?) - the effects are drawn as the game draws them");
        }
        return;
    }
    static bool beginIn = false, finalizeIn = false;
    if (!beginIn) beginIn = d2rsig::Hook(RVA_BEGIN, kSigBegin, sizeof kSigBegin, reinterpret_cast<void*>(&HookBegin), reinterpret_cast<void**>(&g_origBegin));
    if (beginIn && !finalizeIn)
        finalizeIn = d2rsig::Hook(RVA_FINALIZE, kSigFinalize, sizeof kSigFinalize, reinterpret_cast<void*>(&HookFinalize), reinterpret_cast<void**>(&g_origFinalize));
    if (beginIn && finalizeIn) {
        g_state.store(1);
        LogF("own effects: ready - the game's overlay creation is watched (looking only, whether the option is on or not: %s); the hero's overlays of the states, by the game's own tables, are "
             "dimmed with the effect's own \"Fade\" parameter once the effect is made", g_on.load() ? "on" : "off");
    } else if (++tries == 30) {
        g_state.store(2);
        Log("own effects: the hooks could not be put in - the effects are drawn as the game draws them");
    }
}

void Tick() {
    if (g_state.load(std::memory_order_relaxed) != 1 || g_faulted.load(std::memory_order_relaxed)) return;
    if (!g_on.load(std::memory_order_relaxed) && !g_slow.load(std::memory_order_relaxed) && !g_lower.load(std::memory_order_relaxed) && !g_touched.load(std::memory_order_relaxed)) return;   // off, and nothing of ours to give back
    ++g_ticks;
    AcquireSRWLockExclusive(&g_lock);
    if (g_cands.Count() != 0) {   // the table's overlays of units not known as the hero's: his, if they stand at his place (only while no key of his is known)
        const uint64_t at = GetTickCount64();
        float hero[3];
        const bool haveHero = !g_keys.Any() && HeroPlace(hero);
        for (Tracker::Slot& c : g_cands.Slots()) {
            if (c.entity == Tracker::kNone) continue;
            if (g_keys.Any() || at > c.addedAt + 120000) { g_cands.Drop(&c); continue; }   // his key is known now (the keyed way decides), or it never came near him
            float p[3];
            if (!haveHero || !EntityPlace(c.entity, p)) continue;
            if (!NearHero(hero, p)) continue;
            if (g_tracker.Add(c.entity, c.group, c.name, at, c.solid)) {
                ++g_candsTaken;
                LogF("own effects: \"%s\" (entity %08X) stands at the hero's place (%.1f %.1f %.1f; he is at %.1f %.1f %.1f) - taken as his (no key of the hero's is known)", c.name, c.entity, (double)p[0], (double)p[1], (double)p[2],
                     (double)hero[0], (double)hero[1], (double)hero[2]);
            }
            g_cands.Drop(&c);
        }
    }
    bool offEntry[table::kCount] = {};   // the solid effects of his not to be drawn now, by table entry (their models are hidden by ScenePass)
    uint64_t heroAt[table::kCount] = {};  // when his newest overlay of each was tracked (whose a new model is)
    bool anySolid = false;
    LiftWant lifts[kLifts];   // [render] lower_own_effects: the hero's solid overlays to draw lower now
    size_t nLifts = 0;
    if (g_tracker.Count() == 0) {
        ReleaseSRWLockExclusive(&g_lock);
        if (LiftsLive()) PublishLifts(lifts, 0, 0.0f, 0.0f, GetTickCount64());   // (ours given back)
        if (g_hiddenModels.Count()) ScenePass(offEntry, heroAt, false, GetTickCount64());   // (ours given back)
        return;
    }
    Settings set;
    set.on = g_on.load(std::memory_order_relaxed);
    set.body = g_body.load(std::memory_order_relaxed);
    set.aura = g_aura.load(std::memory_order_relaxed);
    set.slow = g_slow.load(std::memory_order_relaxed);
    set.speed = g_speed.load(std::memory_order_relaxed);
    set.lower = g_lower.load(std::memory_order_relaxed);
    set.heightCm = g_heightCm.load(std::memory_order_relaxed);
    const float upm = g_upm ? g_upm() : 0.0f;
    const bool f4 = viewgate::F4();
    const uint64_t now = GetTickCount64();
    bool touched = false;
    const bool paramReady = FadeNameReady();
    for (Tracker::Slot& s : g_tracker.Slots()) {
        if (s.entity == Tracker::kNone) continue;
        uintptr_t comp = 0, effect = 0;
        int shown = -1;
        const int found = ResolveEffect(s.entity, &comp, &effect, &shown);
        if (found < 0) { Fault("the effect lookup"); break; }
        if (Tracker::GoneNow(s, found > 0, now)) { g_tracker.Drop(&s); continue; }
        if (s.solid) {   // the solid part's own models (Bone Armor's bones): the effect's switch does not reach them - ScenePass, after the loop (made or not: the models come with it)
            anySolid = true;
            if (const table::Entry* e = EntryOfKey(s.name)) {
                const size_t k = (size_t)(e - table::kEntries);
                if (TargetFor(s.group, set, f4, true) <= kSame) offEntry[k] = true;
                if (s.addedAt > heroAt[k]) heroAt[k] = s.addedAt;
            }
            const float dy = LiftFor(true, f4, set, upm);
            if (dy != 0.0f && nLifts < kLifts) lifts[nLifts++] = LiftWant{s.entity, dy, s.name};
        }
        if (found == 0) continue;
        s.seen = true;
        if (s.solid && effect && (set.slow || s.sp.sped)) {   // [render] slow_own_effects: the effect's own playback speed
            if (!SpeedPass(s, effect, SpeedFor(true, f4, set), f4, set)) { Fault("the effect's speed"); break; }
        }
        if (s.sp.sped) touched = true;
        const bool onAgain = s.st.hidden && shown == 1;   // we switched it off, the game switched it on (its unit's alpha changed)
        const Plan p = Decide(s.group, set, f4, effect, paramReady, s.st, s.solid, shown);
        if (s.st.hidden || s.st.faded) touched = true;
        if (p.cannotFade && !s.toldNoFade) {
            s.toldNoFade = true;
            LogF("own effects: \"%s\" (entity %08X) cannot be made fainter - %s; it is left as the game draws it (0 %% would still switch it off)", s.name, s.entity,
                 paramReady ? "its effect has no parameter \"Fade\" (the game's setter refused it)" : "the game's \"Fade\" name was not found");
        }
        if (p.visible < 0 && !p.setFade) {
            Apply(p, effect, true, &s.st);
            if (effect && !s.told && set.on && f4) {
                s.told = true;
                LogF("own effects: \"%s\" (entity %08X) - %s group, the effect is made (%p)", s.name, s.entity, GroupName(s.group), (void*)effect);
            }
            continue;
        }
        bool ok = false;
        if (!Write(comp, p, &ok)) { Fault("the Fade write"); break; }
        Apply(p, effect, ok, &s.st);
        if (s.st.hidden || s.st.faded) touched = true;
        if (p.visible == 0 && onAgain) {
            if (!s.toldOnAgain) {
                s.toldOnAgain = true;
                LogF("own effects: \"%s\" (entity %08X) was switched back on by the game (its unit's alpha changed) - switched off again (said once)", s.name, s.entity);
            }
        } else if (p.visible == 0) {
            const float want = OpacityFor(s.group, set);
            if (s.solid && want > kSame)
                LogF("own effects: \"%s\" (entity %08X) - %s group, SOLID - not drawn in F4 (the slider asks %.0f %%; its opaque mesh takes no Fade, so the nearest is off: the game's own "
                     "visibility switch VfxSetVisible 0xF6A830; %s; effect %p)", s.name, s.entity, GroupName(s.group), (double)want * 100.0, SwitchWord(effect), (void*)effect);
            else
                LogF("own effects: \"%s\" (entity %08X) - %s group - not drawn in F4 (0 %%: the game's own visibility switch VfxSetVisible 0xF6A830; %s; effect %p)", s.name, s.entity,
                     GroupName(s.group), SwitchWord(effect), (void*)effect);
        } else if (p.visible == 1 && !p.setFade)
            LogF("own effects: \"%s\" (entity %08X) is drawn again (%s)", s.name, s.entity, f4 && set.on ? "the slider is at 100 %" : !set.on ? "the option is off" : "not F4");
        if (p.setFade) {
            if (!ok) LogF("own effects: \"%s\" (entity %08X) - the game's setter REFUSED the parameter \"Fade\" (effect %p): it has none - left as the game draws it", s.name, s.entity, (void*)effect);
            else if (std::fabs(p.fade - s.loggedFade) > kSame) {
                s.loggedFade = p.fade;
                LogF("own effects: \"%s\" (entity %08X) - %s group - \"Fade\" set to %.0f %% on effect %p (VfxSetFloat 0xF6A380, the name the game uses for the unit's alpha)", s.name, s.entity,
                     GroupName(s.group), (double)p.fade * 100.0, (void*)effect);
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!g_faulted.load(std::memory_order_relaxed)) ScenePass(offEntry, heroAt, anySolid, now);   // (outside our lock: it takes the game's)
    PublishLifts(lifts, nLifts, set.heightCm, upm, now);
    if (LiftsLive()) touched = true;
    if (g_hiddenModels.Count()) touched = true;
    g_touched.store(touched, std::memory_order_relaxed);   // anything of ours left to give back keeps the tick going after the option is unticked
}

}  // namespace owneffects
