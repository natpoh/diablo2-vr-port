// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed's vr/vrcam.cpp AlignAreaEntry by Piotr Kożuch (ptkk73), PR #602, as it stands at
// https://github.com/bearstonem/d2r-pcvr/commit/b57ed57c5458d87fb12f04e832faadc715a022b2 (the commits before it: vr/unleashed_entry_facing.h).
// Thank you, Piotr. Taken into D2R VR Authentic for [camera] entry_facing, rewritten onto our own game reads:
//   - the hero and his level by the sewer stairs' accessors (vr/input/spell_aim.cpp SewerHero / SewerLevel: D2RLoader's own GetLevelIdFromUnit,
//     each behind SEH), the room / unit layout gated by SewerLayoutReady (the game's own instructions that read +0x38, +0x20, +0x00 / +0x40,
//     +0xA8, +0x160 and the static +0x10 / +0x14, checked byte for byte) - not Unleashed's room-level call (0x2EFC10): the town walk takes every
//     loaded room (the NPC ids are the town's own, an NPC of a town is never in another level);
//   - the walkable cells for an entrance under the hero from our copy of the game's wall cells (vr/unleashed_wall_cells.cpp: read on the game
//     thread, SEH-guarded), never the game's room / collision calls from here; none (a joined game): an entrance beside the hero is turned
//     away from as it is, one under him left as it is;
//   - the turn not added here but handed to TurnTick (vr/view/views.cpp), which adds it to the body turn before the next pair holds it, so both
//     eyes take the same yaw (Unleashed's "double rotation" fix, 1b223a0, by other means);
//   - the yaw the turn is measured against is the view's own as built (head included), so no separate native formula (NativeTurnDelta);
//   - an arrival waits for what it needs at most ef::kWindowMs (Window), and is dropped outside VR F4 or with the switch off.
// The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
//
// Read-only: game memory is read through SafeRead and D2RLoader's accessor; nothing of the game is written or called beyond those.
#include "vrcam_headers.h"
#include "unleashed_entry_facing.h"
#include "unleashed_wall_cells_game.h"

#include <mutex>

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

bool SewerLayoutReady();   // vr/input/spell_aim.cpp

namespace {
namespace ef = unleashed::entryfacing;
namespace uw = unleashedwalls;

std::atomic<float> g_turn{0.0f};   // the turn waiting for TurnTick, degrees (+ right, as the body turn)
std::mutex g_mutex;                // the state below: each view built may call (both eyes, any pass)
ef::State g_state;
ef::Window g_window;
uint64_t g_triedAt = 0;

bool Read(uintptr_t at, void* out, size_t n) {
    return at >= 0x10000 && at <= UINTPTR_MAX - n && SafeRead(out, reinterpret_cast<const void*>(at), n);
}
const auto kRead = [](uintptr_t at, auto& value) { return Read(at, &value, sizeof value); };

void Drop(const char* why) {
    if (!g_window.open) return;
    LogF("vrcam: entry facing - from level %d to %d: %s; the view keeps its turn", g_window.from, g_window.to, why);
    g_window.Close();
}

// The heading the arrival wants (degrees, as the camera's). False: not yet (`wait`) or never.
bool Resolve(uintptr_t hero, uint64_t session, int level, uint64_t now, float* heading, bool* wait, char* said, size_t saidSize) {
    *wait = false;
    uintptr_t path = 0, room = 0;
    uint32_t xy[2]{};
    if (!kRead(hero + 0x38, path) || !kRead(path, xy) || !kRead(path + 0x20, room) || room < 0x10000) { *wait = true; return false; }
    const float px = xy[0] / 65536.0f, py = xy[1] / 65536.0f;
    if (ef::TownArrival(g_window.from, level)) {
        ef::TownTargets targets;
        ef::WalkTown(room, level, kRead, [level](uintptr_t) { return level; }, [&](uintptr_t unit, const uint32_t* h) {
            uintptr_t npcPath = 0;
            uint32_t at[2]{};
            if (h[0] == 1 && kRead(unit + 0x38, npcPath) && kRead(npcPath, at)) targets.Add(level, h[1], at[0] / 65536.0f, at[1] / 65536.0f);
        });
        if (!targets.Heading(level, px, py, *heading)) { *wait = true; return false; }   // the NPC's room not loaded yet
        snprintf(said, saidSize, "town arrival, the hero at %.2f,%.2f, the act's NPC%s", px, py, level == 103 ? "s (midpoint)" : "");
        return true;
    }
    // Another area: the nearest entrance tile (type 5) within 20 cells; its warp class names the jail's and the sewers' own exit ways.
    float hx = 0, hy = 0, best = 20.0f, tx = 0, ty = 0;
    uintptr_t tile = 0;
    uint32_t warp = UINT32_MAX;
    ef::NearUnits(hero, kRead, [&](uintptr_t unit, const uint32_t* h) {
        uint32_t cell[2]{};
        if (h[0] != 5 || !ef::StaticCell(unit, kRead, cell)) return true;
        const float d = std::hypot(float(cell[0]) - hx, float(cell[1]) - hy);
        if (d < best) { best = d; tile = unit; warp = h[1]; tx = float(cell[0]); ty = float(cell[1]); }   // several tiles of one entrance: the nearest
        return true;
    }, hx, hy);
    if (!tile) { *wait = true; return false; }
    if (ef::NativeExitHeading(warp, *heading)) {
        snprintf(said, saidSize, "entrance warp %u (its own exit way), the hero at %.2f,%.2f", warp, hx, hy);
        return true;
    }
    const uw::Cell cell{(int)std::floor(hx), (int)std::floor(hy)};
    uw::Want(session, cell, now);   // the game thread samples the cells around the hero (several frames)
    uw::Patch p;
    const bool walls = uw::Latest(p) && p.session == session && p.at >= g_window.at && p.Covers(cell.x - 6, cell.z - 6) &&
                       p.Covers(cell.x + 6, cell.z + 6);
    const float apart = std::hypot(hx - tx, hy - ty);
    if (walls) {
        if (!ef::Direction(hx, hy, tx, ty, [&p](int x, int y) { return p.Covers(x, y) && !p.Blocked(x, y); }, *heading)) return false;
        snprintf(said, saidSize, "away from entrance warp %u at %.0f,%.0f (%.2f cells), the hero at %.2f,%.2f, by the wall cells", warp, tx, ty, apart, hx, hy);
        return true;
    }
    if (!g_window.WallsLate(now)) { *wait = true; return false; }
    if (apart < 0.5f) { *wait = true; return false; }   // on the entrance and no wall cells: which way is in is not known (waits out the window)
    if (!ef::Direction(hx, hy, tx, ty, [](int, int) { return true; }, *heading)) return false;
    snprintf(said, saidSize, "away from entrance warp %u at %.0f,%.0f (%.2f cells), the hero at %.2f,%.2f, no wall cells", warp, tx, ty, apart, hx, hy);
    return true;
}
}  // namespace

void EntryFacingView(float cameraHeading, float yaw) {
    if (!g_set.entryFacing.load(std::memory_order_relaxed)) {
        std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
        if (lock.owns_lock()) { g_state = {}; g_window.Close(); }
        return;
    }
    std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;   // the other eye is at it
    const uint64_t now = GetTickCount64();
    const uint64_t session = gamestate::LocalPlayer();
    const uintptr_t hero = session ? SewerHero() : 0;
    if (!hero) return;   // (a new game or hero is a new session: State starts over by itself)
    const int level = SewerLevel(hero);
    const int previous = g_state.level;
    if (g_state.Observe(session, level, now)) {
        if (g_window.open) Drop("another level came first");
        g_window.Start(previous, level, now);
        g_triedAt = 0;
    }
    if (!g_window.open) return;
    if (level != g_window.to) { Drop("left the level"); return; }
    if (g_window.Expired(now)) { Drop("what it needs did not come in time"); return; }
    if (!viewgate::F4() || ThirdPerson()) { Drop("not in VR F4"); return; }
    if (!std::isfinite(cameraHeading) || !std::isfinite(yaw)) return;
    if (g_triedAt && now - g_triedAt < 16) return;   // once a frame at most
    g_triedAt = now;
    if (!SewerLayoutReady()) return;   // not verified (yet: it looks once a second; another build never) - the window runs out, with its line
    float heading = 0.0f;
    bool wait = false;
    char said[200] = "";
    if (!Resolve(hero, session, level, now, &heading, &wait, said, sizeof said)) {
        if (!wait) Drop("no way into the area found");
        return;
    }
    const float delta = ef::TurnDelta(cameraHeading, yaw, heading);
    g_turn.store(delta);
    g_window.Close();
    LogF("vrcam: entry facing ([camera] entry_facing) - from level %d to %d: %s; facing %.1f, the view turned %.1f deg (camera %.1f, yaw %.1f), %llu ms after the arrival",
         g_window.from, level, said, heading, delta, cameraHeading, yaw,
         (unsigned long long)g_window.Age(now));
}

float TakeEntryTurn() {
    return g_turn.exchange(0.0f);
}

}  // namespace vrc
