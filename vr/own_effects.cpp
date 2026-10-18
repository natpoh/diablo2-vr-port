// own_effects.cpp - [render] dim_own_effects, the game's side: the hero's own effects (a state's overlays: Bone Armor, the armors, Energy Shield, the aura rings ...) drawn fainter in
// VR first person (F4). The pure part, the reasons and the offsets: vr/own_effects.h; the notes: docs/own_effects_recon.md. VISUAL ONLY: nothing of the server, the buff or the
// state is touched; the game's own draw of the effect is made fainter with the game's own named float parameter "Fade" of the effect (VfxSetFloat 0xF6A380 - what the game sets to
// the unit's alpha for every effect of its overlays, vr/own_effects.h), or at 0 % not drawn (VfxSetVisible).
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
    AcquireSRWLockExclusive(&g_lock);
    unsigned added = 0;
    for (uint32_t i = 0; i < n; ++i)
        if (into->Add(ents[i], g, key, now)) ++added;
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
            else LogF("own effects: the hero's overlay \"%s\" is made - %s group, %u effect entit%s tracked (instance %p)", s.name, GroupName(s.group), added, added == 1 ? "y" : "ies", (void*)inst);
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

// The entity's effect component and effect instance. 1 found (effect may be 0: not made yet), 0 the entity has none, -1 not usable.
int ResolveEffect(uint32_t entity, uintptr_t* component, uintptr_t* effect) noexcept {
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

bool Wanted() { return g_on.load(); }

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
    if (!g_on.load(std::memory_order_relaxed) && !g_touched.load(std::memory_order_relaxed)) return;   // off, and nothing of ours to give back
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
            if (g_tracker.Add(c.entity, c.group, c.name, at)) {
                ++g_candsTaken;
                LogF("own effects: \"%s\" (entity %08X) stands at the hero's place (%.1f %.1f %.1f; he is at %.1f %.1f %.1f) - taken as his (no key of the hero's is known)", c.name, c.entity, (double)p[0], (double)p[1], (double)p[2],
                     (double)hero[0], (double)hero[1], (double)hero[2]);
            }
            g_cands.Drop(&c);
        }
    }
    if (g_tracker.Count() == 0) { ReleaseSRWLockExclusive(&g_lock); return; }
    Settings set;
    set.on = g_on.load(std::memory_order_relaxed);
    set.body = g_body.load(std::memory_order_relaxed);
    set.aura = g_aura.load(std::memory_order_relaxed);
    const bool f4 = viewgate::F4();
    const uint64_t now = GetTickCount64();
    bool touched = false;
    const bool paramReady = FadeNameReady();
    for (Tracker::Slot& s : g_tracker.Slots()) {
        if (s.entity == Tracker::kNone) continue;
        uintptr_t comp = 0, effect = 0;
        const int found = ResolveEffect(s.entity, &comp, &effect);
        if (found < 0) { Fault("the effect lookup"); break; }
        if (Tracker::GoneNow(s, found > 0, now)) { g_tracker.Drop(&s); continue; }
        if (found == 0) continue;
        s.seen = true;
        const Plan p = Decide(s.group, set, f4, effect, paramReady, s.st);
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
        if (p.visible == 0) LogF("own effects: \"%s\" (entity %08X) is not drawn (0 %%, the game's own visibility switch)", s.name, s.entity);
        else if (p.visible == 1 && !p.setFade) LogF("own effects: \"%s\" (entity %08X) is drawn again", s.name, s.entity);
        if (p.setFade) {
            if (!ok) LogF("own effects: \"%s\" (entity %08X) - the game's setter REFUSED the parameter \"Fade\" (effect %p): it has none - left as the game draws it", s.name, s.entity, (void*)effect);
            else if (std::fabs(p.fade - s.loggedFade) > kSame) {
                s.loggedFade = p.fade;
                LogF("own effects: \"%s\" (entity %08X) - %s group - \"Fade\" set to %.0f %% on effect %p (VfxSetFloat 0xF6A380, the name the game uses for the unit's alpha)", s.name, s.entity,
                     GroupName(s.group), (double)p.fade * 100.0, (void*)effect);
            }
        }
    }
    g_touched.store(touched, std::memory_order_relaxed);   // anything of ours left to give back keeps the tick going after the option is unticked
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace owneffects
