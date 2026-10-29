// room_heights.h - [render] keep_room_heights: a room of the level keeps the height the game first drew it at, for the rest of the game.
// Pure; the test is cleanroom/tests/room_heights_test.cpp, the wiring is vr/vrcam.cpp (HookRoomHeight, RoomHeightsTick).
//
// A player on flat, 0.18x (Discord #d2r-vr-support, 2026-10-26, Act 4 Outer Steppes): "Why is it slipping? ... It was still flat in front of
// me! I went up to another area and when I came back, I was sliding. In the other acts too." His pictures: blocks of floor a step higher or
// lower than the ones beside them, pushed sideways, the void showing in the gaps. The same ground had been flat on his first visit.
//
// The cause, read in the game's code (3.3.93787, names from its profiler zones; static reading, not run against the game):
//   - LevelTranslationLayer::ActivateTranslatedRoom (0x6A1958) gives every room it starts to draw a height of its own:
//     CalculateHeightOffset (0x6A2A40, (layer, DRLG_ROOM*, const float edges[4]) -> float). The room's four edge heights (west, north, east,
//     south) are matched to the neighbours ALREADY drawn: for each side's neighbour with a record in the layer's sorted vector (+0x2B0, records
//     of 0x2B0 bytes, the room's unique id at +0 - the DRLG_ROOM's +0x1B8; the record's edges at +0x28 +0x2C +0x30 +0x34, its height at +0x38)
//     the height that makes the two edges meet is neighbour edge + neighbour height - own edge. The hero's own room wins at once; otherwise the
//     largest of them; no neighbour drawn: 0.
//   - DeactivateTranslatedRoom (0x6A3EE9) erases the record when the room leaves the drawn set, and leaving the level drops them all
//     (DeactivateTranslatedLevel). The next time the room is drawn its height is worked out again, from whatever is drawn round it then.
//   - The edges of the level's tiles do not agree all the way round, so the answer depends on the way in: enter from the waypoint or from the
//     next area, walk round a block, and a room comes back a step up or down. Height is drawn sheared along the game's own view (flat_ground.h:
//     render x, z = grid + y * sqrt(3/2)), so from above a room at any height sits exactly where it should and nobody ever sees it. From our
//     camera a room a step off is a ledge pushed sideways - "sliding" - and the hero, the eye in VR F4 with him, goes up and down with it.
//
// What is done (visual only - the game's world is a 2D grid, nothing of the server, the paths or the hits uses these heights): the height a
// room was first drawn at is kept, by level and the room's place on the grid; when the game asks for that room's height again it gets the
// kept one. Rooms never drawn before are worked out by the game as always - against neighbours that now keep theirs - so one layout grows
// over the game and never changes. Cleared at the main menu (another game has other rooms at the same places).
//
// The same player on 0.183 (keep_room_heights=1, rings=1): "Floors stay didn't help, even worse ... it jumps to the right height right in
// front of me." The rooms of one preset (a DS1 map larger than a room, split over several DRLG_ROOMs of type 2 - Act 4's mesas are made of
// them) share ONE height in the game: ActivateTranslatedRoom's preset path (0x6A0E20) first looks for a drawn room of the same preset
// ([[room+0x40]+8] the same) and copies that record's edges and height - without asking CalculateHeightOffset; only the first room of the
// preset drawn is asked. Kept by room (0.183), a preset came back at the kept height of whichever of its rooms was drawn first that time,
// or at the game's new answer for a room of it never asked before - from one way in to the next the whole preset went up and down, and with
// it every room the game then stitched to it. Kept by preset (the preset's grid place, +0x18 / +0x1C), a preset has one height for the game
// whichever of its rooms comes first; the copy path then copies the kept height.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace roomheights {

// DRLG_ROOM (D2Common): its place on the grid, its level, its unique id. DRLG_LEVEL: the level's id.
constexpr size_t kRoomX = 0x60, kRoomY = 0x64, kRoomLevel = 0x90, kRoomUid = 0x1B8, kLevelId = 0x1F8;
// A room of type 2 is a part of a preset: [room+0x40] -> +8 the preset, its grid place at +0x18 / +0x1C (its size at +0x20 / +0x24).
constexpr size_t kRoomType = 0x74, kRoomPresetInfo = 0x40, kPresetInfoPreset = 0x8, kPresetX = 0x18, kPresetY = 0x1C;
constexpr int32_t kTypePreset = 2;
static_assert(kPresetY == kPresetX + 4 && kRoomY == kRoomX + 4, "each place is read as a pair");
// LevelTranslationLayer: the drawn rooms' records, {record* data; size_t count}, sorted by the unique id at the record's +0.
constexpr size_t kLayerRecords = 0x2B0, kRecordSize = 0x2B0, kRecordHeight = 0x38;

// Reads n bytes at src; false when they cannot be read. The game: a guarded memcpy; the test: plain memory.
using ReadFn = bool (*)(void* dst, uintptr_t src, size_t n);

// What keeps a height: a room by its grid place, or a whole preset by the preset's (preset = 1).
struct Key {
    int32_t level = 0, x = 0, y = 0, preset = 0;
    bool operator==(const Key& o) const { return level == o.level && x == o.x && y == o.y && preset == o.preset; }
};
struct KeyHash {
    size_t operator()(const Key& k) const {
        return std::hash<uint64_t>()((uint64_t(uint32_t(k.level)) << 48) ^ (uint64_t(uint32_t(k.x)) << 24) ^ uint64_t(uint32_t(k.y)) ^
                                     (uint64_t(k.preset) << 63));
    }
};

// A DRLG_ROOM's key - its preset's for a room of a preset - and its unique id. False when the room cannot be read or makes no sense
// (a level id out of the game's range).
inline bool ReadRoom(ReadFn read, uintptr_t room, Key* key, uint64_t* uid) {
    uintptr_t level = 0;
    int32_t xy[2] = {}, id = 0, type = 0;
    if (!room || !read(xy, room + kRoomX, sizeof xy) || !read(&level, room + kRoomLevel, sizeof level) || !level ||
        !read(&id, level + kLevelId, sizeof id) || !read(uid, room + kRoomUid, sizeof *uid) || !read(&type, room + kRoomType, sizeof type))
        return false;
    if (id <= 0 || id > 1024) return false;
    if (type == kTypePreset) {
        uintptr_t info = 0, preset = 0;
        if (!read(&info, room + kRoomPresetInfo, sizeof info) || !info || !read(&preset, info + kPresetInfoPreset, sizeof preset) || !preset ||
            !read(xy, preset + kPresetX, sizeof xy))
            return false;
        *key = {id, xy[0], xy[1], 1};
        return true;
    }
    *key = {id, xy[0], xy[1], 0};
    return true;
}

// The drawn room's record by unique id, as the game's own lookup (0x6B7450: lower_bound over the sorted vector) - its height.
inline bool RecordHeight(ReadFn read, uintptr_t layer, uint64_t uid, float* height) {
    uintptr_t data = 0;
    size_t count = 0;
    if (!layer || !read(&data, layer + kLayerRecords, sizeof data) || !read(&count, layer + kLayerRecords + sizeof data, sizeof count) ||
        !data || count == 0 || count > 100000)
        return false;
    size_t lo = 0, hi = count;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        uint64_t k = 0;
        if (!read(&k, data + mid * kRecordSize, sizeof k)) return false;
        if (k < uid) lo = mid + 1; else hi = mid;
    }
    uint64_t k = 0;
    float h = 0.0f;
    if (lo == count || !read(&k, data + lo * kRecordSize, sizeof k) || k != uid || !read(&h, data + lo * kRecordSize + kRecordHeight, sizeof h) ||
        !std::isfinite(h))
        return false;
    *height = h;
    return true;
}

// Rooms the game worked a height out for are only "pending" until their record is there (a room whose models are not loaded yet is
// put off and asked again next tick, with other neighbours drawn by then): the height kept is the one it was really drawn at.
class Keeper {
public:
    static constexpr uint32_t kPendingTicks = 250;   // ~10 s of game ticks: a room still without a record then is forgotten (asked again later)
    static constexpr size_t kMaxPending = 512;

    bool Kept(const Key& k, float* h) const {
        const auto it = kept_.find(k);
        if (it == kept_.end()) return false;
        *h = it->second;
        return true;
    }
    void Pend(const Key& k, uint64_t uid, uint32_t tick) {
        if (kept_.count(k)) return;
        for (Pending& p : pending_)
            if (p.key == k) { p.uid = uid; p.tick = tick; return; }
        if (pending_.size() < kMaxPending) pending_.push_back({k, uid, tick});
    }
    // After the game's tick: the pending rooms that are drawn now keep the height they were drawn at. How many were kept.
    int Harvest(ReadFn read, uintptr_t layer, uint32_t tick) {
        int n = 0;
        for (size_t i = 0; i < pending_.size();) {
            float h = 0.0f;
            const bool got = RecordHeight(read, layer, pending_[i].uid, &h);
            if (got) { kept_[pending_[i].key] = h; ++n; }
            if (got || tick - pending_[i].tick > kPendingTicks) {
                pending_[i] = pending_.back();
                pending_.pop_back();
            } else {
                ++i;
            }
        }
        return n;
    }
    void Clear() { kept_.clear(); pending_.clear(); }
    size_t KeptCount() const { return kept_.size(); }
    size_t PendingCount() const { return pending_.size(); }

private:
    struct Pending { Key key; uint64_t uid; uint32_t tick; };
    std::unordered_map<Key, float, KeyHash> kept_;
    std::vector<Pending> pending_;
};

// The game asked for a room's height and worked out `game`; what it is given. *moved: the kept height is not the game's new one.
inline float Answer(const Keeper& keeper, const Key& k, float game, bool on, bool* moved) {
    float h = 0.0f;
    *moved = false;
    if (!on || !keeper.Kept(k, &h)) return game;
    *moved = std::fabs(h - game) > 0.01f;
    return h;
}

}   // namespace roomheights
