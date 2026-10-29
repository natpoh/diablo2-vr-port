// room_heights.h - [render] floors_stay: a room of the level keeps the height the game first drew it at, for the rest of the game.
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
//
// 0.188.3, the author in Act 4 (the Outer Steppes by the Pandemonium Fortress): a raised slab of ground with a sharp edge, a terrace that is
// not there; Unleashed (no room heights of its own) has none. The log: "level 104 preset 1032,992 asked again - the game worked out -9.00,
// kept at 0.00". Two faults of keeping a height as it was first drawn:
//   - a room drawn before its neighbours (put off while their models load - after a waypoint, at a level's edge) gets 0 out of nothing:
//     the game's own step for one visit, and the next visit stitches it right. Kept, that 0 stayed for the game: a step every visit.
//   - the game's heights have no fixed zero: each time it draws a room with nothing round it (a level entered, a waypoint) it starts from 0
//     there. Kept heights are from the frame of the visit that drew them; given back as they were, beside ground drawn in another frame,
//     the seam is a step.
// Now an orphan (a room the game answered out of nothing while other rooms are drawn) is not kept, and kept heights are only kept relative
// to one another: an island of rooms kept together is placed by its first room asked (the game's answer for it), so it meets the ground
// drawn now without a step and keeps its own shape (Keeper).
//
// 0.188.4, the same place: "ступенька не исправлена" - "level 104 preset 1040,992 asked again - the game worked out -9.00, given 0.00 to
// stand as it did beside the rooms kept with it". An orphan was not kept, but it was drawn - at 0 - and the rooms the game stitched to it
// next looked stitched and were kept: a whole slab grown from one orphan's 0, 9 over the ground the town's side stitches at -9. Now only
// ground that comes from real ground is kept (Keeper: grounded), and a room stitched to an orphan's ground takes a grounded neighbour's
// height. (A kept room yielding to the game's answer where it fits more of the ground round it was tried: at every seam between kept and
// new ground it moved the kept rooms - the slide back.)
//
// 0.188.5: with it on, the Fortress step stayed even next to it; off, it is as in Unleashed - seen from afar (the game's own -9 / 0
// rooms), gone as the hero walks up, when the game draws those rooms again with their neighbours in. The author: "да, эту галочку лучше
// снять". Off by default since, under a new key ([render] floors_stay, was keep_room_heights=1: the installer keeps an upgrading
// player's old values, a new key comes in with its shipped 0); kept as an option - it trades the game's own fixes as you come near for
// floors that do not slide on a way back.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace roomheights {

// DRLG_ROOM (D2Common): its place on the grid, its level, its unique id. DRLG_LEVEL: the level's id.
constexpr size_t kRoomX = 0x60, kRoomY = 0x64, kRoomLevel = 0x90, kRoomUid = 0x1B8, kLevelId = 0x1F8;
// The rooms near it (its neighbours, the corners too): DRLG_ROOM* list at +0x10, their count at +0x18 (CalculateHeightOffset's own walk).
constexpr size_t kRoomNear = 0x10, kRoomNearCount = 0x18;
// A room of type 2 is a part of a preset: [room+0x40] -> +8 the preset, its grid place at +0x18 / +0x1C (its size at +0x20 / +0x24).
constexpr size_t kRoomType = 0x74, kRoomPresetInfo = 0x40, kPresetInfoPreset = 0x8, kPresetX = 0x18, kPresetY = 0x1C;
constexpr int32_t kTypePreset = 2;
static_assert(kPresetY == kPresetX + 4 && kRoomY == kRoomX + 4, "each place is read as a pair");
// LevelTranslationLayer: the drawn rooms' records, {record* data; size_t count}, sorted by the unique id at the record's +0.
constexpr size_t kLayerRecords = 0x2B0, kRecordSize = 0x2B0, kRecordHeight = 0x38;
// The record's DRLG_ROOM* (the preset path reads its type through it, 0x6A1010); its edges W N E S (CalculateHeightOffset reads them).
constexpr size_t kRecordRoom = 0x8, kRecordEdges = 0x28;

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

// The drawn room's record by unique id, as the game's own lookup (0x6B7450: lower_bound over the sorted vector): its address.
inline bool RecordAt(ReadFn read, uintptr_t layer, uint64_t uid, uintptr_t* record) {
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
    if (lo == count || !read(&k, data + lo * kRecordSize, sizeof k) || k != uid) return false;
    *record = data + lo * kRecordSize;
    return true;
}
// ... and its height.
inline bool RecordHeight(ReadFn read, uintptr_t layer, uint64_t uid, float* height) {
    uintptr_t rec = 0;
    float h = 0.0f;
    if (!RecordAt(read, layer, uid, &rec) || !read(&h, rec + kRecordHeight, sizeof h) || !std::isfinite(h)) return false;
    *height = h;
    return true;
}

// One side of a room as CalculateHeightOffset sees it: the side's neighbour (the first of the room's near list, +0x10 / count +0x18, in
// the same row west / column north / row east / column south), whether it is drawn, and the height the two edges ask of the room beside
// it - the neighbour's facing edge + its height - the room's own edge (the record's edges W N E S at +0x28, its height at +0x38).
struct Side {
    bool drawn = false;
    float ask = 0.0f;
    Key key;   // the neighbour's (its preset's for a part of a preset)
};
inline void Sides(ReadFn read, uintptr_t layer, uintptr_t room, const float* edges, Side out[4]) {
    for (int d = 0; d < 4; ++d) out[d] = Side{};
    int32_t xy[2] = {};
    uintptr_t list = 0;
    uint64_t count = 0;
    if (!read(xy, room + kRoomX, sizeof xy) || !read(&list, room + kRoomNear, sizeof list) || !read(&count, room + kRoomNearCount, sizeof count) ||
        !list || count > 64)
        return;
    float own[4] = {};
    if (edges) read(own, uintptr_t(edges), sizeof own);
    bool found[4] = {};
    for (uint64_t i = 0; i < count; ++i) {
        uintptr_t n = 0;
        int32_t nxy[2] = {};
        if (!read(&n, list + i * sizeof n, sizeof n) || !n || !read(nxy, n + kRoomX, sizeof nxy)) continue;
        const int d = nxy[0] < xy[0] && nxy[1] == xy[1] ? 0 : nxy[0] == xy[0] && nxy[1] < xy[1] ? 1 : nxy[0] > xy[0] && nxy[1] == xy[1] ? 2
                    : nxy[0] == xy[0] && nxy[1] > xy[1] ? 3 : -1;
        if (d < 0 || found[d]) continue;
        found[d] = true;   // the game takes the first of each side only
        uint64_t uid = 0;
        uintptr_t rec = 0;
        float ne[4] = {}, h = 0.0f;
        Key k;
        if (!ReadRoom(read, n, &k, &uid) || !RecordAt(read, layer, uid, &rec) || !read(ne, rec + kRecordEdges, sizeof ne) ||
            !read(&h, rec + kRecordHeight, sizeof h) || !std::isfinite(h))
            continue;
        static constexpr int kFacing[4] = {2, 3, 0, 1};   // west side: the neighbour's east edge, and so on
        out[d] = {true, ne[kFacing[d]] + h - own[d], k};
    }
}

// How many rooms the layer draws now (its records).
inline size_t DrawnCount(ReadFn read, uintptr_t layer) {
    size_t count = 0;
    if (!layer || !read(&count, layer + kLayerRecords + sizeof(uintptr_t), sizeof count) || count > 100000) return 0;
    return count;
}

// The keeper. Each kept height belongs to an island: rooms kept while they were drawn together, so their heights fit one another. While a
// room of an island is drawn the island has an offset to the ground the game draws now (its frame - the game's heights start at 0 at
// whichever room it draws with nothing round it); its rooms are given kept + offset. The offset is set by the island's first room asked:
// the game's answer for it minus its kept height - so where the island meets the ground drawn now there is no step, and inside it every
// room stands as it did. An island met while another is drawn joins that one (its heights moved into the other's frame).
//
// Only ground that comes from real ground is kept. A height is grounded when the game answered it at the start of a frame (nothing drawn),
// or stitched it to a drawn neighbour whose own height is grounded (kept, or waiting to be). A room the game answered out of nothing while
// other rooms are drawn (an orphan: drawn before its neighbours) is not, and neither is a room it stitched only to such rooms - a whole slab
// grown from one orphan's 0 (0.188.4, the Outer Steppes by the Fortress gate: kept at 0 beside ground at -9). Such a room with a grounded
// neighbour drawn is given that neighbour's height instead (the largest, as the game picks); with none, the game's.
class Keeper {
public:
    static constexpr uint32_t kPendingTicks = 250;   // ~10 s of game ticks: a room still without a record then is forgotten (asked again later)
    static constexpr size_t kMaxPending = 512;
    static constexpr float kFit = 0.01f;             // heights this close meet without a step
    // What an answer was: the game's (a new room, grounded), kept + offset, an island placed by it, an orphan (the game's, not kept), a
    // new room given a grounded neighbour's height.
    enum class How { Game, Kept, Joined, Orphan, Grounded };

    bool Kept(const Key& k, float* h) const {
        const auto it = kept_.find(k);
        if (it == kept_.end()) return false;
        *h = it->second.h;
        return true;
    }
    // The game asked for room's height and worked out `game`; what it is given. sides: the room's four sides as the game sees them; drawn:
    // the layer draws other rooms now. *joined: the rooms of an island that met the ground drawn now (0 for none).
    float Ask(const Key& k, uint64_t uid, float game, const Side sides[4], bool drawn, uint32_t tick, How* how, size_t* joined = nullptr) {
        if (joined) *joined = 0;
        if (!drawn) current_ = -1;   // nothing drawn: the game starts a new frame at this room
        bool grounded = !drawn, anyGround = false;
        float best = 0.0f;   // the grounded neighbours' ask, the largest (the game's own pick among them)
        for (int d = 0; d < 4; ++d) {
            if (!sides[d].drawn || !Grounded(sides[d].key)) continue;
            grounded = grounded || std::fabs(sides[d].ask - game) < kFit;
            if (!anyGround || std::fabs(sides[d].ask) > std::fabs(best)) best = sides[d].ask;
            anyGround = true;
        }
        const auto it = kept_.find(k);
        if (it == kept_.end()) {
            if (grounded) {
                Pend(k, uid, tick, true);
                *how = How::Game;
                return game;
            }
            if (anyGround) {   // the game stitched it to an orphan's ground: a grounded neighbour's height instead
                Pend(k, uid, tick, true);
                *how = How::Grounded;
                return best;
            }
            Pend(k, uid, tick, false);
            *how = How::Orphan;
            return game;
        }
        const Entry e = it->second;
        if (e.island == current_) {
            *how = How::Kept;
            return e.h + offset_;
        }
        if (drawn && !grounded) {   // nothing grounded to place its island by: the game's, the island left unplaced
            *how = How::Orphan;
            return game;
        }
        if (current_ < 0) {   // the first island met in this frame: its offset from this room
            current_ = e.island;
            offset_ = game - e.h;
            *how = How::Joined;
            if (joined) *joined = Count(e.island);
            return game;
        }
        const float shift = game - e.h - offset_;   // the island's heights into the current island's frame
        size_t n = 0;
        for (auto& [key, v] : kept_)
            if (v.island == e.island) { v.h += shift; v.island = current_; ++n; }
        if (joined) *joined = n;
        *how = How::Joined;
        return game;
    }
    // After the game's tick: the island stays placed while one of its rooms is drawn; the pending rooms drawn now are kept, in its frame
    // (a new island when none is). How many were kept.
    int Harvest(ReadFn read, uintptr_t layer, uint32_t tick) {
        if (current_ >= 0 && !IslandDrawn(read, layer, current_)) current_ = -1;
        int n = 0;
        for (size_t i = 0; i < pending_.size();) {
            float h = 0.0f;
            const bool got = RecordHeight(read, layer, pending_[i].uid, &h);
            if (got && pending_[i].keep && !kept_.count(pending_[i].key)) {
                if (current_ < 0) { current_ = next_++; offset_ = 0.0f; }
                kept_[pending_[i].key] = {h - offset_, current_};
                ++n;
            }
            if (got || tick - pending_[i].tick > kPendingTicks) {
                pending_[i] = pending_.back();
                pending_.pop_back();
            } else {
                ++i;
            }
        }
        return n;
    }
    void Clear() { kept_.clear(); pending_.clear(); current_ = -1; offset_ = 0.0f; }
    size_t KeptCount() const { return kept_.size(); }
    size_t PendingCount() const { return pending_.size(); }
    int Island(const Key& k) const { const auto it = kept_.find(k); return it == kept_.end() ? -1 : it->second.island; }

private:
    struct Entry { float h; int island; };
    struct Pending { Key key; uint64_t uid; uint32_t tick; bool keep; };
    void Pend(const Key& k, uint64_t uid, uint32_t tick, bool keep) {
        for (Pending& p : pending_)
            if (p.key == k) { p.uid = uid; p.tick = tick; p.keep = keep; return; }
        if (pending_.size() < kMaxPending) pending_.push_back({k, uid, tick, keep});
    }
    // A drawn neighbour's height comes from real ground: it is kept, or waits to be.
    // (A kept room counts while its island is the one placed now: drawn otherwise, it stands at the game's height, not in this frame.)
    bool Grounded(const Key& k) const {
        const auto it = kept_.find(k);
        if (it != kept_.end()) return it->second.island == current_;
        for (const Pending& p : pending_)
            if (p.key == k) return p.keep;
        return false;
    }
    size_t Count(int island) const {
        size_t n = 0;
        for (const auto& [key, v] : kept_) n += v.island == island;
        return n;
    }
    // A record of the layer whose room is of the island (the record's +8 is its DRLG_ROOM).
    bool IslandDrawn(ReadFn read, uintptr_t layer, int island) const {
        uintptr_t data = 0;
        const size_t count = DrawnCount(read, layer);
        if (!count || !read(&data, layer + kLayerRecords, sizeof data) || !data) return false;
        for (size_t i = 0; i < count; ++i) {
            uintptr_t room = 0;
            Key k;
            uint64_t uid = 0;
            if (!read(&room, data + i * kRecordSize + kRecordRoom, sizeof room) || !ReadRoom(read, room, &k, &uid)) continue;
            const auto it = kept_.find(k);
            if (it != kept_.end() && it->second.island == island) return true;
        }
        return false;
    }
    std::unordered_map<Key, Entry, KeyHash> kept_;
    std::vector<Pending> pending_;
    int current_ = -1, next_ = 0;   // the island drawn now (-1 none), the next island's number
    float offset_ = 0.0f;           // the island drawn now: the game's frame minus its kept heights
};

}   // namespace roomheights
