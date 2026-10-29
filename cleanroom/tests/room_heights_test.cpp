// [render] keep_room_heights (vr/room_heights.h): the game's room heights, as its CalculateHeightOffset works them out, on a level whose tile
// edges do not agree all the way round. A hero walks the level, leaves it and comes back another way: the game's own heights come out
// different for the same rooms (the floor "slides"); kept, every room is drawn at the height it was first drawn at. The rooms, the level and
// the layer's sorted records are laid out in memory at the game's offsets, so the reads are the ones the plugin makes. Two rooms are one
// preset: the game asks for the first of them drawn and copies its height to the other (0x6A0E20); kept by room, as in 0.183, the preset
// still came back at another height - kept by preset it does not.
//   room_heights_test [-v]
#include "room_heights.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

static int g_fail = 0;
static bool g_verbose = false;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}

using namespace roomheights;

static bool Read(void* dst, uintptr_t src, size_t n) {
    if (!src) return false;
    std::memcpy(dst, reinterpret_cast<const void*>(src), n);
    return true;
}

enum { W, N, E, S };

// A level of kSize x kSize rooms in the game's layout: DRLG_ROOMs, the DRLG_LEVEL, the layer with its records sorted by unique id.
struct Level {
    static constexpr int kSize = 6;
    static constexpr int32_t kId = 105;   // Act 4's Outer Steppes
    std::vector<uint8_t> level = std::vector<uint8_t>(0x200);
    std::vector<std::unique_ptr<uint8_t[]>> rooms;
    std::vector<std::vector<float>> edges;   // per room: its tiles' height at the west, north, east and south edge
    std::vector<int> preset;                 // per room: the preset it is a part of, -1 none
    std::vector<std::unique_ptr<uint8_t[]>> presets, infos;   // a preset (its grid place at +0x18), the room's +0x40 that points to it
    std::vector<uint8_t> layer = std::vector<uint8_t>(0x400);
    std::map<uint64_t, std::pair<float, std::vector<float>>> drawn;   // the records: unique id -> height, edges
    std::vector<uint8_t> records;
    int hero = -1;

    Level() {
        std::memcpy(&level[kLevelId], &kId, 4);
        for (int i = 0; i < kSize * kSize; ++i) {
            rooms.emplace_back(new uint8_t[0x200]());
            const int32_t xy[2] = {(i % kSize) * 8, (i / kSize) * 8};   // the grid place of a room of 8 x 8 tiles
            const uintptr_t lv = reinterpret_cast<uintptr_t>(level.data());
            const uint64_t uid = 0x1000 + uint64_t(i) * 7;               // sorted as the rooms are, gaps between
            std::memcpy(rooms[i].get() + kRoomX, xy, sizeof xy);
            std::memcpy(rooms[i].get() + kRoomLevel, &lv, sizeof lv);
            std::memcpy(rooms[i].get() + kRoomUid, &uid, sizeof uid);
            // The tiles' edges: the east and south ones a little off what the next room's west and north have - they never close a loop.
            const int x = i % kSize, y = i / kSize;
            edges.push_back({0.0f, 0.0f, 0.9f * float((x * 3 + y) % 4) - 0.6f, 0.7f * float((x + y * 2) % 3) - 0.4f});
            const int32_t type = 1;
            std::memcpy(rooms[i].get() + kRoomType, &type, sizeof type);
            preset.push_back(-1);
        }
        Publish();
    }
    // Rooms a and b one preset (a DS1 map over both, at a's place): type 2, the preset's edges on both.
    void MakePreset(int a, int b) {
        presets.emplace_back(new uint8_t[0x40]());
        std::memcpy(presets.back().get() + kPresetX, rooms[a].get() + kRoomX, 8);
        const uintptr_t pr = reinterpret_cast<uintptr_t>(presets.back().get());
        for (const int r : {a, b}) {
            infos.emplace_back(new uint8_t[0x20]());
            std::memcpy(infos.back().get() + kPresetInfoPreset, &pr, sizeof pr);
            const uintptr_t info = reinterpret_cast<uintptr_t>(infos.back().get());
            std::memcpy(rooms[r].get() + kRoomPresetInfo, &info, sizeof info);
            std::memcpy(rooms[r].get() + kRoomType, &kTypePreset, sizeof kTypePreset);
            preset[r] = int(presets.size()) - 1;
        }
        edges[b] = edges[a];
    }
    // The preset path's first look (0x6A0E20): a drawn room of the same preset, -1 none.
    int DrawnSibling(int r) const {
        if (preset[r] < 0) return -1;
        for (int i = 0; i < kSize * kSize; ++i)
            if (i != r && preset[i] == preset[r] && drawn.count(Uid(i))) return i;
        return -1;
    }
    uintptr_t Room(int i) const { return reinterpret_cast<uintptr_t>(rooms[i].get()); }
    uint64_t Uid(int i) const { return 0x1000 + uint64_t(i) * 7; }
    uintptr_t Layer() { return reinterpret_cast<uintptr_t>(layer.data()); }
    int At(int x, int y) const { return x < 0 || y < 0 || x >= kSize || y >= kSize ? -1 : y * kSize + x; }

    // The layer's vector of records as the game keeps it: sorted, 0x2B0 bytes each, the unique id at +0, the height at +0x38.
    void Publish() {
        records.assign(std::max<size_t>(1, drawn.size()) * kRecordSize, 0);
        size_t i = 0;
        for (const auto& [uid, rec] : drawn) {
            std::memcpy(&records[i * kRecordSize], &uid, sizeof uid);
            std::memcpy(&records[i * kRecordSize + kRecordHeight], &rec.first, sizeof(float));
            ++i;
        }
        const uintptr_t data = reinterpret_cast<uintptr_t>(records.data());
        const size_t count = drawn.size();
        std::memcpy(&layer[kLayerRecords], &data, sizeof data);
        std::memcpy(&layer[kLayerRecords + 8], &count, sizeof count);
    }

    // LevelTranslationLayer::CalculateHeightOffset (0x6A2A40) as the game has it: each side's neighbour already drawn gives the height that
    // makes the two edges meet; the hero's room wins at once, otherwise the largest; none drawn: 0.
    float GameHeight(int r) const {
        const int x = r % kSize, y = r / kSize;
        const int nb[4] = {At(x - 1, y), At(x, y - 1), At(x + 1, y), At(x, y + 1)};
        const int opposite[4] = {E, S, W, N};
        float best = 0.0f;
        for (int d = 0; d < 4; ++d) {
            if (nb[d] < 0) continue;
            const auto it = drawn.find(Uid(nb[d]));
            if (it == drawn.end()) continue;
            const float c = it->second.second[opposite[d]] + it->second.first - edges[r][d];
            if (nb[d] == hero) return c;
            if (std::fabs(c) > std::fabs(best)) best = c;
        }
        return best;
    }
};

// One visit of the level: the hero walks the path; each step the game draws his room and the ring round it (his room first) and drops the
// rooms that left it. With a keeper, the plugin's hook answers each height and the tick's harvest follows (byRoom: every room by its own
// place, as 0.183 kept them). The heights the rooms were drawn at.
static std::map<int, float> Visit(Level& lv, const std::vector<int>& path, Keeper* keeper, uint32_t* tick, int* moved = nullptr,
                                  bool byRoom = false) {
    std::map<int, float> seen;
    for (const int c : path) {
        lv.hero = c;
        std::vector<int> want = {c};
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int n = lv.At(c % Level::kSize + dx, c / Level::kSize + dy);
                if (n >= 0 && n != c) want.push_back(n);
            }
        for (auto it = lv.drawn.begin(); it != lv.drawn.end();) {   // DeactivateTranslatedRoom: the record is erased
            bool keep = false;
            for (const int w : want) keep = keep || lv.Uid(w) == it->first;
            it = keep ? std::next(it) : lv.drawn.erase(it);
        }
        lv.Publish();
        for (const int r : want) {   // ActivateTranslatedRoom for the rooms not drawn yet
            if (lv.drawn.count(lv.Uid(r))) continue;
            const int sib = lv.DrawnSibling(r);
            if (sib >= 0) {   // a part of a preset already drawn: its height copied, nobody asked
                lv.drawn[lv.Uid(r)] = lv.drawn[lv.Uid(sib)];
                lv.Publish();
                seen[r] = lv.drawn[lv.Uid(r)].first;
                continue;
            }
            float h = lv.GameHeight(r);
            if (keeper) {
                Key k;
                uint64_t uid = 0;
                Check(ReadRoom(&Read, lv.Room(r), &k, &uid), "a room reads at the game's offsets");
                if (byRoom) {
                    int32_t xy[2];
                    std::memcpy(xy, reinterpret_cast<const void*>(lv.Room(r) + kRoomX), sizeof xy);
                    k = {k.level, xy[0], xy[1], 0};
                }
                bool m = false;
                h = Answer(*keeper, k, h, true, &m);
                if (m && moved) ++*moved;
                keeper->Pend(k, uid, *tick);
            }
            lv.drawn[lv.Uid(r)] = {h, lv.edges[r]};
            lv.Publish();
            seen[r] = h;
        }
        if (keeper) keeper->Harvest(&Read, lv.Layer(), *tick);
        ++*tick;
    }
    lv.drawn.clear();   // DeactivateTranslatedLevel: the hero went to another area
    lv.Publish();
    return seen;
}

static float MaxDiff(const std::map<int, float>& a, const std::map<int, float>& b, int* rooms) {
    float m = 0.0f;
    *rooms = 0;
    for (const auto& [r, h] : a) {
        const auto it = b.find(r);
        if (it == b.end()) continue;
        ++*rooms;
        m = std::max(m, std::fabs(it->second - h));
    }
    return m;
}

int main(int argc, char** argv) {
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    const int K = Level::kSize;

    // ---- reading the game's memory: the room's key, its record by unique id ----
    {
        Level lv;
        Key k;
        uint64_t uid = 0;
        Check(ReadRoom(&Read, lv.Room(lv.At(3, 2)), &k, &uid), "ReadRoom reads a room");
        Check(k.level == Level::kId && k.x == 24 && k.y == 16 && uid == lv.Uid(lv.At(3, 2)), "ReadRoom: level id, grid place, unique id");
        Check(!ReadRoom(&Read, 0, &k, &uid), "no room, no key");
        for (int i = 0; i < K * K; i += 3) lv.drawn[lv.Uid(i)] = {0.25f * float(i), lv.edges[i]};
        lv.Publish();
        float h = -1.0f;
        bool all = true;
        for (int i = 0; i < K * K; ++i) {
            const bool got = RecordHeight(&Read, lv.Layer(), lv.Uid(i), &h);
            all = all && got == (i % 3 == 0) && (!got || h == 0.25f * float(i));
        }
        Check(all, "RecordHeight finds every drawn room's height, and only those (the game's lower_bound)");
        Check(!RecordHeight(&Read, lv.Layer(), 0x0FFF, &h) && !RecordHeight(&Read, lv.Layer(), 0x9999, &h), "ids before the first and after the last");
        lv.drawn.clear();
        lv.Publish();
        Check(!RecordHeight(&Read, lv.Layer(), lv.Uid(0), &h), "nothing drawn, nothing found");
    }

    // ---- the cause: the game's own heights, a level left and entered again another way ----
    std::vector<int> fromWest, fromSouth;   // the first visit along the top row west to east; the second up the east column, then back west
    for (int x = 0; x < K; ++x) fromWest.push_back(x);
    for (int y = K - 1; y >= 0; --y) fromSouth.push_back(K - 1 + y * K);
    for (int x = K - 2; x >= 0; --x) fromSouth.push_back(x);
    {
        Level lv;
        uint32_t tick = 0;
        const auto a = Visit(lv, fromWest, nullptr, &tick);
        const auto b = Visit(lv, fromSouth, nullptr, &tick);
        const auto c = Visit(lv, fromWest, nullptr, &tick);
        int n1 = 0, n2 = 0;
        const float d1 = MaxDiff(a, b, &n1), d2 = MaxDiff(a, c, &n2);
        if (g_verbose) std::printf("game's own: the same %d rooms %.2f apart on the way back, %d rooms %.2f the same way in again\n", n1, d1, n2, d2);
        Check(n1 >= 6 && d1 > 0.5f, "the game's own: the rooms come back at other heights when the level is entered another way");
        Check(d2 < 1e-5f, "the game's own: the same way in gives the same heights (it is the way in, not chance)");
    }

    // ---- the fix: kept, every room comes back at its first height, whichever way the hero comes in ----
    {
        Level lv;
        Keeper keeper;
        uint32_t tick = 0;
        int moved = 0;
        const auto a = Visit(lv, fromWest, &keeper, &tick, &moved);
        Check(moved == 0 && keeper.KeptCount() == a.size(), "the first visit: the game's own heights, every drawn room kept");
        const auto b = Visit(lv, fromSouth, &keeper, &tick, &moved);
        int n = 0;
        const float d = MaxDiff(a, b, &n);
        if (g_verbose) std::printf("kept: the same %d rooms %.4f apart; %d answers were not the game's new one\n", n, d, moved);
        Check(n >= 6 && d < 1e-5f, "kept: the rooms seen before come back at the height they were first drawn at");
        Check(moved > 0, "kept: the game's own answer would have moved some of them");
        const auto c = Visit(lv, fromWest, &keeper, &tick, &moved);
        int n2 = 0;
        Check(MaxDiff(b, c, &n2) < 1e-5f && n2 == int(c.size()), "kept: a third visit, every room as before");
        // the rooms first seen on the second visit (worked out by the game against kept neighbours) are kept as well
        std::map<int, float> both = a;
        both.insert(b.begin(), b.end());
        Check(keeper.KeptCount() == both.size() && both.size() > a.size(), "kept: every room drawn on either visit, the second visit's new ones too");
        keeper.Clear();
        Check(keeper.KeptCount() == 0 && keeper.PendingCount() == 0, "Clear: another game starts with nothing kept");
    }

    // ---- a preset of two rooms: the first way in reaches one of them, the second way in the other ----
    // (4,1) and (4,2): the walk along the top row draws (4,1) only; the walk up the east column comes to (4,2) first, (4,1) a step later.
    {
        const int pa = 1 * K + 4, pb = 2 * K + 4;
        {
            Level lv;
            lv.MakePreset(pa, pb);
            Key ka, kb;
            uint64_t ua = 0, ub = 0;
            Check(ReadRoom(&Read, lv.Room(pa), &ka, &ua) && ReadRoom(&Read, lv.Room(pb), &kb, &ub) && ka == kb && ka.preset == 1 &&
                      ka.x == 32 && ka.y == 8 && ua != ub,
                  "ReadRoom: the rooms of one preset have the preset's key (its place), each its own unique id");
            Key kr;
            ReadRoom(&Read, lv.Room(pa - 1), &kr, &ua);
            Check(kr.preset == 0 && kr.x == 24 && kr.y == 8, "ReadRoom: a room of no preset keeps its own key");
        }
        for (const bool byRoom : {true, false}) {
            Level lv;
            lv.MakePreset(pa, pb);
            Keeper keeper;
            uint32_t tick = 0;
            const auto a = Visit(lv, fromWest, &keeper, &tick, nullptr, byRoom);
            Check(a.count(pa) && !a.count(pb), "the preset: the first walk draws only one of its rooms");
            const auto b = Visit(lv, fromSouth, &keeper, &tick, nullptr, byRoom);
            Check(b.count(pa) && b.count(pb) && b.at(pa) == b.at(pb), "the preset: drawn together, its rooms have one height (the copy)");
            const auto c = Visit(lv, fromWest, &keeper, &tick, nullptr, byRoom);
            if (g_verbose)
                std::printf("preset kept by %s: %.2f on the first walk, %.2f on the second, %.2f on the third\n", byRoom ? "room" : "preset",
                            a.at(pa), b.at(pa), c.at(pa));
            if (byRoom) {
                Check(std::fabs(b.at(pa) - a.at(pa)) > 0.1f, "kept by room (0.183): the preset comes back at another height on the way back");
            } else {
                Check(b.at(pa) == a.at(pa) && b.at(pb) == a.at(pa) && c.at(pa) == a.at(pa),
                      "kept by preset: the preset has its first height on every walk, whichever of its rooms comes first");
                int n = 0;
                Check(MaxDiff(a, b, &n) < 1e-5f && MaxDiff(b, c, &n) < 1e-5f, "kept by preset: every room seen before as it was");
            }
        }
    }

    // ---- a room put off: no record yet, nothing kept until it is really drawn ----
    {
        Level lv;
        Keeper keeper;
        Key k;
        uint64_t uid = 0;
        const int r = lv.At(2, 2);
        ReadRoom(&Read, lv.Room(r), &k, &uid);
        keeper.Pend(k, uid, 0);
        Check(keeper.Harvest(&Read, lv.Layer(), 1) == 0 && keeper.PendingCount() == 1, "asked but not drawn: pending, not kept");
        lv.drawn[uid] = {1.75f, lv.edges[r]};
        lv.Publish();
        float h = 0.0f;
        Check(keeper.Harvest(&Read, lv.Layer(), 2) == 1 && keeper.Kept(k, &h) && h == 1.75f && keeper.PendingCount() == 0,
              "drawn on a later tick: the height it was drawn at is kept");
        bool m = true;
        Check(Answer(keeper, k, -3.0f, true, &m) == 1.75f && m, "asked again: the kept height");
        Check(Answer(keeper, k, -3.0f, false, &m) == -3.0f && !m, "[render] keep_room_heights=0: the game's own");
        Key other = k;
        other.x += 8;
        Check(Answer(keeper, other, 0.5f, true, &m) == 0.5f && !m, "a room never drawn: the game's own");
        keeper.Pend(other, uid + 1, 10);
        Check(keeper.Harvest(&Read, lv.Layer(), 10 + Keeper::kPendingTicks + 1) == 0 && keeper.PendingCount() == 0,
              "a room never drawn is forgotten after a while (asked again when it is)");
    }

    // ---- the drawn ground: a room's height is only seen off the game's own view ----
    {
        // render x, z = grid + y * sqrt(3/2) (flat_ground.h): along the game's view (0.612, 0.5, 0.612) a room 1.5 up lands on the screen
        // where it would at 0; from an eye looking along the ground it is 1.5 up and 1.84 aside on each axis - the step and the slide.
        const float k = 1.22474487139f, h = 1.5f;
        const float dir[3] = {0.612f, 0.5f, 0.612f};
        const float t = h / dir[1];
        Check(std::fabs((h * k - t * dir[0])) < 0.01f, "along the game's view a height is invisible");
        Check(h * k > 1.8f, "from ours it is a step pushed aside");
    }

    if (g_fail == 0) std::printf("room_heights_test: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
