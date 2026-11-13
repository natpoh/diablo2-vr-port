// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit b0f8eb1dfab71ec1cbc7e9535d24d72f11be5d6b ("Weapons hit walls"), its vr/wall_cells.cpp: the game's wall cells
// around the hero read on the game thread with the same room lookup and missile-barrier mask physical arrows test against walls - the server's
// room of the hero (0x34B440), the room that holds a cell (0x38DFB0), the cell's collision bits (0x364D50), every bit in the one call. Taken
// into D2R VR Authentic for [weight] wall_contact_mode 2 (Bear's): one stream (the held weapon's, around the hero); the addresses through our signature table
// (cleanroom/sigscan/game_sigs.h: ServerRoomOf, ServerRoomPoint, ServerCellMask) and each call behind an SEH guard (a fault turns it off for
// the session, the weapon passes walls as before); the hero's server unit through hit by touch's server lookup (single player or the host).
// The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
//
// Read-only: the game's own query functions are called on its own thread, nothing of the game is written.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <D2RLPlugin/api.h>

#include "melee_touch.h"
#include "sigscan.h"
#include "unleashed_wall_cells_game.h"

using namespace D2RL;

namespace gamestate {
uint64_t LocalPlayer();
}

namespace unleashedwalls {
namespace {

constexpr uint64_t RVA_ROOM_OF = 0x34B440, RVA_ROOM_POINT = 0x38DFB0, RVA_CELL_MASK = 0x364D50;   // ServerRoomOf, ServerRoomPoint, ServerCellMask
using RoomOfFn = uintptr_t (*)(uintptr_t unit);
using RoomPointFn = uintptr_t (*)(uintptr_t room, int x, int y);
using MaskFn = uint16_t (*)(uintptr_t room, int x, int y, int size, int mask);
using LookupFn = uintptr_t (*)(uintptr_t game, int type, uint32_t id);

const PluginContext* g_ctx = nullptr;
const ThreadService* g_threads = nullptr;
std::mutex g_mutex;
struct Wanted {
    uint64_t session = 0;
    Cell centre;
};
Wanted g_wanted;
Patch g_latest;
bool g_have = false, g_scheduled = false;
uint64_t g_sampledAt = 0, g_askedAt = 0, g_serial = 0;
std::atomic<bool> g_faulted{false};
std::atomic<int> g_state{0};   // 0 not asked yet, 1 sampling, 2 the game's functions not found, 3 no server here (a joined game)
std::atomic<unsigned> g_samples{0}, g_refused{0}, g_blocked{0};
std::atomic<uint64_t> g_ticks{0};

bool Read(void* d, uintptr_t s, size_t n) noexcept {
    if (s < 0x10000) return false;
    __try { memcpy(d, reinterpret_cast<const void*>(s), n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallLookup(uintptr_t fn, uintptr_t game, int type, uint32_t id, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<LookupFn>(fn)(game, type, id); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallRoomOf(uintptr_t fn, uintptr_t unit, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<RoomOfFn>(fn)(unit); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallRoomPoint(uintptr_t fn, uintptr_t room, int x, int y, uintptr_t* out) noexcept {
    __try { *out = reinterpret_cast<RoomPointFn>(fn)(room, x, y); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CallMask(uintptr_t fn, uintptr_t room, int x, int y, uint16_t* out) noexcept {
    __try { *out = reinterpret_cast<MaskFn>(fn)(room, x, y, 1, 0xFFFF); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Log(const char* text) {
    if (g_ctx) g_ctx->LogInfo(text);
}

void Fault(const char* where) {
    if (g_faulted.exchange(true)) return;
    char b[200];
    snprintf(b, sizeof b, "wall collide: a fault in the game's %s - the wall cells are off for this session, the weapon passes walls as before", where);
    Log(b);
}

// The sample, on the game's thread: the hero's server room, then every cell of the patch in a serpentine (each lookup starts from a neighbour's room).
// False: nothing sampled (no server here, the hero not known yet, a function not found, a fault).
bool SampleNow(const Wanted& w, Patch* out) {
    if (g_faulted.load()) return false;
    const uintptr_t roomOf = d2rsig::Addr(RVA_ROOM_OF), roomPoint = d2rsig::Addr(RVA_ROOM_POINT), mask = d2rsig::Addr(RVA_CELL_MASK);
    if (!roomOf || !roomPoint || !mask) { g_state.store(2); return false; }
    uintptr_t game = 0, lookup = 0;
    if (!meleetouch::ServerLookup(&game, &lookup)) { g_state.store(3); return false; }
    const uint32_t heroId = meleetouch::HeroId();
    if (!heroId || !w.session || w.session != gamestate::LocalPlayer()) return false;
    uintptr_t hero = 0, room = 0;
    uint32_t head[3] = {};
    if (!CallLookup(lookup, game, 0, heroId, &hero)) { Fault("unit lookup"); return false; }
    if (!hero || !Read(head, hero, sizeof head) || head[0] != 0 || head[2] != heroId) return false;
    if (!CallRoomOf(roomOf, hero, &room)) { Fault("room of the hero"); return false; }
    if (!room) return false;
    g_state.store(1);
    Patch p;
    p.session = w.session;
    p.x0 = w.centre.x - kPatchCells / 2;
    p.z0 = w.centre.z - kPatchCells / 2;
    unsigned blocked = 0;
    for (int z = 0; z < kPatchCells; ++z)
        for (int i = 0; i < kPatchCells; ++i) {
            const int x = (z & 1) ? kPatchCells - 1 - i : i, cx = p.x0 + x, cz = p.z0 + z;
            const bool offMap = cx < 1 || cz < 1 || cx > 65534 || cz > 65534;
            uintptr_t next = 0;
            uint16_t bits = 0;
            if (!offMap) {
                if (!CallRoomPoint(roomPoint, room, cx, cz, &next)) { Fault("room of a cell"); return false; }
                // closed or unloaded rooms are conservative walls, as for arrows; every bit read in the one call
                if (next && !CallMask(mask, next, cx, cz, &bits)) { Fault("cell's collision bits"); return false; }
                if (next) room = next;
            }
            p.Mark(x, z, offMap, next != 0, bits);
            if (p.Blocked(p.x0 + x, p.z0 + z)) ++blocked;
        }
    g_blocked.store(blocked);
    *out = p;
    return true;
}

void __cdecl Sample(const PluginContext*, void*) noexcept {
    LARGE_INTEGER began;
    QueryPerformanceCounter(&began);
    Wanted w;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        w = g_wanted;
    }
    Patch p;
    const bool ok = SampleNow(w, &p);
    LARGE_INTEGER ended;
    QueryPerformanceCounter(&ended);
    g_ticks += (uint64_t)(ended.QuadPart - began.QuadPart);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_scheduled = false;
    g_sampledAt = GetTickCount64();
    if (ok) {
        p.serial = ++g_serial;
        p.at = g_sampledAt;
        g_latest = p;
        g_have = true;
        ++g_samples;
    } else {
        ++g_refused;
    }
}

}  // namespace

void Init(const PluginContext* ctx) {
    g_ctx = ctx;
    const ThreadService* t = nullptr;
    g_threads = ctx && ctx->QueryService(&t) == ServiceQueryResult::Success ? t : nullptr;
}

void Want(uint64_t session, Cell centre, uint64_t now) {
    if (!g_ctx || !g_threads || !g_threads->runOnGameThread || !session || g_faulted.load()) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // queued and not run yet - the loader drops queued game work at a session change: one asked long ago is lost
        if (g_scheduled && now >= g_askedAt && now - g_askedAt < kQueuedMs) return;
        if (!SampleDue(g_latest, g_have, g_sampledAt, session, centre, now)) return;
        g_wanted = {session, centre};
        g_scheduled = true;
        g_askedAt = now;
    }
    if (g_threads->runOnGameThread(g_ctx, &Sample, nullptr) != Threads::Result::Success) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_scheduled = false;
        g_sampledAt = now;
    }
}

bool Latest(Patch& out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_have) return false;
    out = g_latest;
    return true;
}

void Forget() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_have = false;
    g_latest = Patch{};
}

SampleStats TakeStats() {
    SampleStats s;
    s.samples = g_samples.exchange(0);
    s.refused = g_refused.exchange(0);
    s.blocked = g_blocked.load();
    s.state = g_state.load();
    s.faulted = g_faulted.load();
    const uint64_t ticks = g_ticks.exchange(0);
    static LARGE_INTEGER frequency{};
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    s.us = frequency.QuadPart ? (double)ticks * 1e6 / (double)frequency.QuadPart : 0.0;
    return s;
}

}  // namespace unleashedwalls
