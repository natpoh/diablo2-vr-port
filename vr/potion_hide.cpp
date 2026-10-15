// potion_hide.cpp - [hands] potion_hide_game, the game's side: the renderer's visual-init callback (0x771A10, once per entity with the unit it belongs to) is hooked to LOOK,
// and the world-matrix function (0xE3A8C0) to put a hidden matrix on the entities of the hero's thrown-potion missile while a throw of ours is armed (the pure part, the
// reasons and the numbers: vr/potion_hide.h). Visual only; offline or not, nothing of the server is touched. bearstonem/d2r-pcvr (MIT) found both functions and their shape
// (vr/bow.cpp, vr/projectiles.cpp: the arrow's mesh put on its own flight); they are used here the other way round.
//
// Safety, as with "Direct hit" and throw_direct: every read of the game's memory and every write is behind an SEH guard; the visual-init hook always calls the original
// unchanged; a fault turns the option off for the session (the game's potion is drawn again, as before); the hooks do nothing at all unless a throw of ours is armed or an
// entity is in the set, which only the hero's potion missile in the armed window can put there, in VR first person (F4).

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
#include "melee_touch.h"
#include "potion_hide.h"
#include "sigscan.h"
#include "view_gate.h"

using namespace D2RL;

namespace potionhide {
namespace {

// Addresses in 3.3.93787 (the keys of game_sigs.h).
constexpr uint64_t RVA_VISUAL_INIT = 0x771A10, RVA_WORLD_XFORM = 0xE3A8C0;
// push rbp; push rbx; push rsi; push rdi; push r13; push r14; push r15; lea rbp,[rsp-270h]
const uint8_t kSigVisualInit[19] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x90, 0xFD, 0xFF, 0xFF};
// mov rax,rsp; mov [rax+8],rbx; push rbp; push rsi; push rdi; push r12; push r13; push r14
const uint8_t kSigXform[16] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56};
// The effect (VFX) component's own visibility switch (see potion_hide.h): the registry global, the storage getter (registry), VfxSetVisible (component, bool).
constexpr uint64_t RVA_ECS_REGISTRY = 0x2677170, RVA_VFX_STORAGE = 0x66DBB0, RVA_VFX_SETVISIBLE = 0xF6A830;
// mov [rsp+20h],rbx; push rsi; push rdi; push r14; sub rsp,40h; mov ...
const uint8_t kSigVfxStorage[14] = {0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48};
// mov [rcx+1Ch],dl; mov rcx,[rcx+10h]; test rcx,rcx; jne (the jump's target moves with the build)
const uint8_t kSigVfxVisible[14] = {0x88, 0x51, 0x1C, 0x48, 0x8B, 0x49, 0x10, 0x48, 0x85, 0xC9, 0x0F, 0x85, 0x80, 0x04};
using VfxStorageFn = uintptr_t (*)(uintptr_t registry);
using VfxVisibleFn = void (*)(uintptr_t component, uint8_t visible);
// the TransformComponent (0xD0 bytes): the local matrix at +0, the world matrix the draw reads at +0x40, the previous frame's at +0x80 (docs/doll_binding.md)
constexpr size_t kCompWorld = 0x40, kCompPrev = 0x80;

using VisualInitFn = void (*)(uintptr_t assets, uintptr_t definition, uint32_t entity, uintptr_t unit);
using XformFn = uintptr_t (*)(uintptr_t out, uint32_t entity, uintptr_t component);

const PluginContext* g_ctx = nullptr;
std::atomic<bool> g_wanted{false}, g_faulted{false}, g_effectOk{false};   // g_effectOk: the effect component's switch is usable (found, checked)
std::atomic<int> g_state{0};   // 0 not tried, 1 both hooks are in, 2 not possible (another build?)
bool g_visualIn = false, g_xformIn = false;
VisualInitFn g_origVisual = nullptr;
XformFn g_origXform = nullptr;
Window g_window;
EntitySet g_set;
bool (*g_heroId)(uint32_t*) = nullptr;

// What one armed throw's window saw (the summary line after it).
std::atomic<unsigned> g_entities{0}, g_units{0}, g_writes{0}, g_removed{0}, g_effects{0}, g_effectsNone{0};
std::atomic<uint32_t> g_lastUnit{0}, g_lastClass{0}, g_firstEntity{0};
std::atomic<bool> g_summaryDue{false}, g_toldNotHero{false};
std::atomic<unsigned> g_otherTold{0};

void Log(const char* text) { if (g_ctx) logasync::Info(g_ctx, text); }
template <typename... A> void LogF(const char* fmt, A... a) {
    char b[400];
    snprintf(b, sizeof b, fmt, a...);
    Log(b);
}

void Fault(const char* where) {
    if (g_faulted.exchange(true)) return;
    g_window.Cancel();
    g_set.Clear();
    LogF("potion hide: a fault in %s - the option is off for this session, the game's own thrown potion is drawn again", where);
}

bool ReadHead(uintptr_t unit, UnitHead* u) noexcept {
    if (unit < 0x10000) return false;
    __try {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(unit);
        u->type = *reinterpret_cast<const uint32_t*>(p + kOffType);
        u->cls = *reinterpret_cast<const uint32_t*>(p + kOffClass);
        u->id = *reinterpret_cast<const uint32_t*>(p + kOffId);
        u->flags = *reinterpret_cast<const uint32_t*>(p + kOffFlags);
        u->ownerType = *reinterpret_cast<const uint32_t*>(p + kOffOwnerType);
        u->ownerId = *reinterpret_cast<const uint32_t*>(p + kOffOwnerId);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadAt(uintptr_t at, void* out, size_t n) noexcept {
    if (at < 0x10000) return false;
    __try { std::memcpy(out, reinterpret_cast<const void*>(at), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The game's own switch: the entity's effect component told "not visible" (VfxSetVisible stores the wish and applies it to the effect if it is made - or, if it is not made yet,
// when it is). 1 done; 0 the entity has no effect component (the root of the unit); -1 not usable / a fault. made / wasVisible: what the component said before (for the log).
int HideEffect(uint32_t entity, bool* made, uint32_t* wasVisible) noexcept {
    if (!g_effectOk.load(std::memory_order_relaxed)) return -1;
    __try {
        const uintptr_t reg = d2rsig::Addr(RVA_ECS_REGISTRY);
        const VfxStorageFn storageOf = reinterpret_cast<VfxStorageFn>(d2rsig::Addr(RVA_VFX_STORAGE));
        const VfxVisibleFn setVisible = reinterpret_cast<VfxVisibleFn>(d2rsig::Addr(RVA_VFX_SETVISIBLE));
        if (!reg || !storageOf || !setVisible) return -1;
        const uintptr_t storage = storageOf(reg);
        if (!storage) return 0;
        uintptr_t element = 0, component = 0, instance = 0;
        uint8_t visible = 1;
        if (!StorageElement(&ReadAt, storage, entity, kEffectElementSize, &element)) return 0;
        if (!ReadAt(element, &component, sizeof component) || component < 0x10000) return 0;
        ReadAt(component + kEffectOffInstance, &instance, sizeof instance);
        ReadAt(component + kEffectOffVisible, &visible, sizeof visible);
        *made = instance > 0x10000;
        *wasVisible = visible;
        setVisible(component, 0);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// A missile whose table row has a light carries a dynamic light object (the client create's 0xE28E0 makes it, the unit keeps it at +0xA8; read, never changed): it is a glow
// that moves with the missile and is NOT an entity - this look only tells whether the potion has one, for the next test's log. radius8: its radius * 8 as the record has it.
bool ReadLight(uintptr_t unit, bool* has, uint32_t* radius8, uint8_t rgb[3]) noexcept {
    if (unit < 0x10000) return false;
    __try {
        const uintptr_t light = *reinterpret_cast<const uintptr_t*>(unit + 0xA8);
        *has = light > 0x10000;
        if (!*has) return true;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(light);
        *radius8 = *reinterpret_cast<const uint32_t*>(p + 0x18);
        rgb[0] = p[0x25]; rgb[1] = p[0x26]; rgb[2] = p[0x27];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 1 written, 0 left alone (the matrix is not a finite one), -1 a fault.
int PutHidden(uintptr_t out, uintptr_t comp) noexcept {
    if (out < 0x10000 || comp < 0x10000) return 0;
    __try {
        float m[16], h[16];
        std::memcpy(m, reinterpret_cast<const void*>(out), sizeof m);
        if (!HiddenMatrix(m, h)) return 0;
        std::memcpy(reinterpret_cast<void*>(out), h, sizeof h);
        std::memcpy(reinterpret_cast<void*>(comp + kCompWorld), h, sizeof h);
        std::memcpy(reinterpret_cast<void*>(comp + kCompPrev), h, sizeof h);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// The visual-init callback's look, on the renderer's thread: is this entity one of the hero's potion missile's?
void NoteEntity(uint32_t entity, uintptr_t unit) noexcept {
    const uint64_t now = GetTickCount64();
    const bool armed = g_window.Armed(now);
    if (!armed && g_set.Live() == 0) return;   // the usual call: nothing waits for a potion
    UnitHead u;
    if (!ReadHead(unit, &u)) return;
    uint32_t hero = 0;
    if (g_heroId) { uint32_t id = 0; if (g_heroId(&id)) hero = id; }
    const Verdict v = Decide(u, armed && viewgate::F4(), hero);
    if (v == Verdict::Hide) {
        if (g_set.Add(entity, now)) {
            const unsigned n = ++g_entities;
            if (u.id != g_lastUnit.load()) { g_lastUnit.store(u.id); g_lastClass.store(u.cls); ++g_units; }
            if (n == 1) g_firstEntity.store(entity);
            // the effect of the entity, by the game's own switch (the matrix below is the second, weaker layer: an emitter that ignores it would still be drawn)
            bool made = false;
            uint32_t was = 1;
            const int fx = HideEffect(entity, &made, &was);
            if (fx > 0) ++g_effects;
            else if (fx == 0) ++g_effectsNone;
            if (n <= 6)
                LogF("potion hide: the game's potion missile #%u (class %u) has a visual entity %08X - it is not drawn (%s%s)", u.id, u.cls, entity,
                     fx > 0 ? "its effect component told not visible by the game's own switch" : fx == 0 ? "no effect component on it - the unit's root; its matrix is put under the ground" : "the effect switch is not usable - only its matrix is put under the ground",
                     fx > 0 ? (made ? ", the effect was already made" : ", the effect is not made yet - the creation will honour it") : "");
            if (n == 1) {
                unsigned inRow = 0;   // for the throw trace: is this unit in the row of the client's hash that it walks?
                const bool found = meleetouch::MissileInHash(unit, &inRow);
                LogF("potion hide: the unit #%u is %s in the client's missile hash row the throw trace and bow latency read (%u units in it)", u.id, found ? "found" : "NOT found", inRow);
                bool has = false;
                uint32_t r8 = 0;
                uint8_t rgb[3] = {};
                if (ReadLight(unit, &has, &r8, rgb)) {
                    if (has)
                        LogF("potion hide: the missile carries a dynamic light of its own (unit + 0xA8: radius field %u, colour %u %u %u, as the record has them - not verified): "
                             "such a glow is not an entity and is NOT hidden by this", r8, (unsigned)rgb[0], (unsigned)rgb[1], (unsigned)rgb[2]);
                    else
                        Log("potion hide: the missile carries no dynamic light of its own (unit + 0xA8 is empty)");
                }
            }
        }
        return;
    }
    // any other missile of the hero's in the window (told a few times per throw): says what the game made, in case the potion's class is not the one thought
    if (v == Verdict::NotPotion && armed && u.type == kTypeMissile && (u.flags & kFlagHasOwner) && u.ownerType == kTypePlayer && g_otherTold.fetch_add(1) < 6)
        LogF("potion hide: the hero's missile #%u class %u (entity %08X) in the window is not one of the six throwing potions (classes %d - %d) - left drawn", u.id, u.cls, entity,
             kClassFirst, kClassLast);
    // a potion missile in the window that is not the hero's (told once per throw: it says whose it was, in case the hero's id was misread)
    if (v == Verdict::NotHero && armed && !g_toldNotHero.exchange(true))
        LogF("potion hide: a potion missile #%u (class %u) in the window is owned by unit type %u id %u (the hero's id %u, flags %X) - not the hero's, so it is drawn", u.id, u.cls,
             u.ownerType, u.ownerId, hero, u.flags);
    // an entity of the set that now belongs to some other unit (an index the game reused): out of the set, drawn again
    if (g_set.Remove(entity, now)) { ++g_removed; LogF("potion hide: entity %08X is now another unit's (%s) - it is drawn again", entity, Why(v)); }
}

__declspec(noinline) void HookVisualInit(uintptr_t assets, uintptr_t definition, uint32_t entity, uintptr_t unit) {
    if (unit && !g_faulted.load(std::memory_order_relaxed) && (g_wanted.load(std::memory_order_relaxed) || g_set.Live() != 0)) {
        __try { NoteEntity(entity, unit); } __except (EXCEPTION_EXECUTE_HANDLER) { Fault("the visual-init look"); }
    }
    g_origVisual(assets, definition, entity, unit);
}

__declspec(noinline) uintptr_t HookXform(uintptr_t out, uint32_t entity, uintptr_t component) {
    const uintptr_t r = g_origXform(out, entity, component);
    if (g_set.Live() == 0 || g_faulted.load(std::memory_order_relaxed)) return r;
    if (g_set.Has(entity, GetTickCount64()) && viewgate::F4()) {
        const int w = PutHidden(out, component);
        if (w > 0) ++g_writes;
        else if (w < 0) Fault("the hidden matrix write");
    }
    return r;
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    // (the ini is read before the log is there: what was set then is told now)
    if (g_wanted.load()) Log("potion hide: on ([hands] potion_hide_game) - the game's own thrown potion is not drawn while our potion flies (visual only, F4)");
}

void SetHeroIdSource(bool (*fn)(uint32_t* id)) { g_heroId = fn; }

void Configure(bool wanted) {
    const bool was = g_wanted.exchange(wanted);
    if (wanted != was) {
        Log(wanted ? "potion hide: on ([hands] potion_hide_game) - the game's own thrown potion is not drawn while our potion flies (visual only, F4)"
                   : "potion hide: off ([hands] potion_hide_game=0) - the game's own thrown potion is drawn as ever");
        if (!wanted) { g_window.Cancel(); g_set.Clear(); }
    }
}
bool Wanted() { return g_wanted.load(); }
bool Ready() { return g_wanted.load() && g_state.load() == 1 && !g_faulted.load(); }
bool HooksIn() { return g_state.load() == 1; }

void Install() {
    static ULONGLONG retryAt = 0;
    static unsigned tries = 0;
    const ULONGLONG now = GetTickCount64();
    if (g_summaryDue.load() && !g_window.Armed(now) && now > g_window.Until() + 1500) {   // one summary per armed throw, after its window
        g_summaryDue.store(false);
        if (const unsigned n = g_entities.load())
            LogF("potion hide: the throw's window is over - %u visual entit%s of the game's potion missile #%u (class %u) hidden (first %08X): %u effect component(s) switched off by the game's own "
                 "switch, %u entit%s without one, %u hidden matrices written, %u given back",
                 n, n == 1 ? "y" : "ies", g_lastUnit.load(), g_lastClass.load(), g_firstEntity.load(), g_effects.load(), g_effectsNone.load(), g_effectsNone.load() == 1 ? "y" : "ies",
                 g_writes.load(), g_removed.load());
        else
            Log("potion hide: the throw's window is over and NO visual entity was seen for the game's potion missile - nothing was hidden (the game did not make one for "
                "this throw, or draws it some way the visual-init callback does not see); the game's potion was drawn as ever");
    }
    if (g_set.Live() != 0) g_set.Prune(now);
    if (!g_wanted.load() || g_state.load() != 0 || !g_ctx || now < retryAt) return;
    retryAt = now + (tries < 30 ? 2000 : 30000);   // the renderer's code is plain from the start, but the signature search may still be waiting
    if (!d2rsig::Addr(RVA_VISUAL_INIT) || !d2rsig::Addr(RVA_WORLD_XFORM)) return;   // not found (yet)
    if (!d2rsig::Check(RVA_VISUAL_INIT, kSigVisualInit, sizeof kSigVisualInit) || !d2rsig::Check(RVA_WORLD_XFORM, kSigXform, sizeof kSigXform)) {
        if (++tries == 30) Log("potion hide: the renderer's functions are not where they were (another build?) - the game's own thrown potion is drawn as ever");
        return;
    }
    if (!g_visualIn)
        g_visualIn = d2rsig::Hook(RVA_VISUAL_INIT, kSigVisualInit, sizeof kSigVisualInit, reinterpret_cast<void*>(&HookVisualInit), reinterpret_cast<void**>(&g_origVisual));
    if (g_visualIn && !g_xformIn)
        g_xformIn = d2rsig::Hook(RVA_WORLD_XFORM, kSigXform, sizeof kSigXform, reinterpret_cast<void*>(&HookXform), reinterpret_cast<void**>(&g_origXform));
    if (g_visualIn && g_xformIn) {
        g_state.store(1);
        Log("potion hide: ready - the renderer's visual-init callback and world-matrix function are hooked (they do nothing unless a thrown potion of ours is armed)");
        const bool fx = d2rsig::Addr(RVA_ECS_REGISTRY) && d2rsig::Addr(RVA_VFX_STORAGE) && d2rsig::Addr(RVA_VFX_SETVISIBLE) &&
                        d2rsig::Check(RVA_VFX_STORAGE, kSigVfxStorage, sizeof kSigVfxStorage) && d2rsig::Check(RVA_VFX_SETVISIBLE, kSigVfxVisible, sizeof kSigVfxVisible);
        g_effectOk.store(fx);
        Log(fx ? "potion hide: the game's own switch of an effect's visibility is found - the thrown potion's effect (its visual is a particle effect, not a mesh) is turned off with it"
               : "potion hide: the game's own switch of an effect's visibility is NOT found (another build?) - only the entities' matrices are put under the ground");
        // The world-matrix function is also CALLED by the engine unit (the belt potions, vr/vrcam.cpp), which checks its bytes before it uses it: the sigscan module vouches for a
        // function we hooked with the bytes the hook proved (d2rsig::Check). If it did not, that check would fail on our own patch and the belt would be gone: say so, loudly, and
        // stand down (a hook cannot be taken out, so the detour stays and only passes the call on).
        if (!d2rsig::Check(RVA_WORLD_XFORM, kSigXform, sizeof kSigXform) || !d2rsig::Check(RVA_VISUAL_INIT, kSigVisualInit, sizeof kSigVisualInit)) {
            g_faulted.store(true);
            g_window.Cancel();
            g_set.Clear();
            Log("potion hide: !!! after the hooks went in the byte check of the hooked functions FAILS - anything that checks them before it calls them (the engine unit: the belt "
                "potions) would switch itself off. The potion hide stands down for this session (the detours only pass the calls on); see 'engine unit' in this log.");
        } else {
            Log("potion hide: the hooked functions still check out against their original bytes (the engine unit's check is safe)");
        }
    } else if (++tries == 30) {
        g_state.store(2);
        Log("potion hide: the hooks could not be put in - the game's own thrown potion is drawn as ever");
    }
}

bool Arm(uint64_t nowMs, double flightS) {
    if (!Ready() || !viewgate::F4()) return false;
    g_entities.store(0); g_units.store(0); g_writes.store(0); g_removed.store(0); g_effects.store(0); g_effectsNone.store(0); g_lastUnit.store(0); g_lastClass.store(0); g_firstEntity.store(0);
    g_window.Arm(nowMs, flightS);
    g_toldNotHero.store(false);
    g_otherTold.store(0);
    g_summaryDue.store(true);
    return true;
}

void Disarm() { g_window.Cancel(); }

}  // namespace potionhide
