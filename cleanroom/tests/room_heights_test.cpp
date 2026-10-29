// [render] floors_stay (vr/room_heights.h): the game's room heights, as its CalculateHeightOffset works them out, on a level whose tile
// edges do not agree all the way round. A hero walks the level, leaves it and comes back another way: the game's own heights come out
// different for the same rooms (the floor "slides"); kept, the rooms seen before come back as they stood beside one another. The rooms, the
// level and the layer's sorted records are laid out in memory at the game's offsets, so the reads are the ones the plugin makes. Two rooms
// are one preset: the game asks for the first of them drawn and copies its height to the other (0x6A0E20); kept by room, as in 0.183, the
// preset still came back at another height - kept by preset it does not. 0.188.3's step: a room drawn before its neighbours (the game's 0
// out of nothing) is not kept, and kept rooms met from ground drawn in another frame are placed where they meet it.
//   room_heights_test [-v]
#include "room_heights.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
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

// A level of kSize x kSize rooms in the game's layout: DRLG_ROOMs with their near lists, the DRLG_LEVEL, the layer with its records sorted
// by unique id.
struct Level {
    static constexpr int kSize = 6;
    int32_t id;   // the level's id (105 by default; the Outer Steppes are 104)
    std::vector<uint8_t> level = std::vector<uint8_t>(0x200);
    std::vector<std::unique_ptr<uint8_t[]>> rooms;
    std::vector<std::vector<uintptr_t>> near;   // per room: the rooms round it, corners too (+0x10 / +0x18)
    std::vector<std::vector<float>> edges;      // per room: its tiles' height at the west, north, east and south edge
    std::vector<int> preset;                    // per room: the preset it is a part of, -1 none
    std::vector<std::unique_ptr<uint8_t[]>> presets, infos;   // a preset (its grid place at +0x18), the room's +0x40 that points to it
    std::vector<uint8_t> layer = std::vector<uint8_t>(0x400);
    std::map<uint64_t, std::pair<float, std::vector<float>>> drawn;   // the records: unique id -> height, edges
    std::vector<uint8_t> records;
    int hero = -1;

    explicit Level(int32_t levelId = 105, int32_t baseX = 0, int32_t baseY = 0) : id(levelId) {
        std::memcpy(&level[kLevelId], &id, 4);
        for (int i = 0; i < kSize * kSize; ++i) {
            rooms.emplace_back(new uint8_t[0x200]());
            const int32_t xy[2] = {baseX + (i % kSize) * 8, baseY + (i / kSize) * 8};   // the grid place of a room of 8 x 8 tiles
            const uintptr_t lv = reinterpret_cast<uintptr_t>(level.data());
            const uint64_t uid = Uid(i);                                  // sorted as the rooms are, gaps between
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
        near.resize(rooms.size());
        for (int i = 0; i < kSize * kSize; ++i) {   // the near list: the corner first, as nothing in the game's order is promised
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int n = At(i % kSize + dx, i / kSize + dy);
                    if (n >= 0 && n != i) near[i].push_back(Room(n));
                }
            const uintptr_t list = reinterpret_cast<uintptr_t>(near[i].data());
            const uint64_t count = near[i].size();
            std::memcpy(rooms[i].get() + kRoomNear, &list, sizeof list);
            std::memcpy(rooms[i].get() + kRoomNearCount, &count, sizeof count);
        }
        Publish();
    }
    uintptr_t Room(int i) const { return reinterpret_cast<uintptr_t>(rooms[i].get()); }
    static uint64_t Uid(int i) { return 0x1000 + uint64_t(i) * 7; }
    static int Index(uint64_t uid) { return int((uid - 0x1000) / 7); }
    uintptr_t Layer() { return reinterpret_cast<uintptr_t>(layer.data()); }
    int At(int x, int y) const { return x < 0 || y < 0 || x >= kSize || y >= kSize ? -1 : y * kSize + x; }

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

    // The layer's vector of records as the game keeps it: sorted, 0x2B0 bytes each, the unique id at +0, the room at +8, the height at +0x38.
    void Publish() {
        records.assign(std::max<size_t>(1, drawn.size()) * kRecordSize, 0);
        size_t i = 0;
        for (const auto& [uid, rec] : drawn) {
            const uintptr_t room = Room(Index(uid));
            std::memcpy(&records[i * kRecordSize], &uid, sizeof uid);
            std::memcpy(&records[i * kRecordSize + kRecordRoom], &room, sizeof room);
            std::memcpy(&records[i * kRecordSize + kRecordHeight], &rec.first, sizeof(float));
            std::memcpy(&records[i * kRecordSize + kRecordEdges], rec.second.data(), 4 * sizeof(float));
            ++i;
        }
        const uintptr_t data = reinterpret_cast<uintptr_t>(records.data());
        const size_t count = drawn.size();
        std::memcpy(&layer[kLayerRecords], &data, sizeof data);
        std::memcpy(&layer[kLayerRecords + 8], &count, sizeof count);
    }

    // The height the game's tiles ask of room r beside its drawn neighbour n (side d of r): n's edge + n's height - r's edge.
    float Candidate(int r, int d, int n) const {
        const int opposite[4] = {E, S, W, N};
        const auto& rec = drawn.at(Uid(n));
        return rec.second[opposite[d]] + rec.first - edges[r][d];
    }
    int Side(int r, int d) const {
        const int x = r % kSize, y = r / kSize;
        const int nb[4] = {At(x - 1, y), At(x, y - 1), At(x + 1, y), At(x, y + 1)};
        return nb[d];
    }
    // LevelTranslationLayer::CalculateHeightOffset (0x6A2A40) as the game has it: each side's neighbour already drawn gives the height that
    // makes the two edges meet; the hero's room wins at once, otherwise the largest; none drawn: 0.
    float GameHeight(int r) const {
        float best = 0.0f;
        for (int d = 0; d < 4; ++d) {
            const int n = Side(r, d);
            if (n < 0 || !drawn.count(Uid(n))) continue;
            const float c = Candidate(r, d, n);
            if (n == hero) return c;
            if (std::fabs(c) > std::fabs(best)) best = c;
        }
        return best;
    }
};

struct Event { int room; Keeper::How how; float game, given, kept; int drawnSides; };

// One visit of the level: the hero walks the path; each step the game draws his room and the ring round it (his room first, then the rooms
// beside it, then the corners) and drops the rooms that left it. putOff: rooms whose models are not loaded on the visit's first step - drawn
// after the rest of it. With a keeper, the plugin's hook answers each height (asked as vrcam asks: the room's key, stitched, anything drawn)
// and the tick's harvest follows (byRoom: every room by its own place, as 0.183 kept them). The heights the rooms were first drawn at.
static std::map<int, float> Visit(Level& lv, const std::vector<int>& path, Keeper* keeper, uint32_t* tick, std::vector<Event>* events = nullptr,
                                  bool byRoom = false, const std::set<int>& putOff = {}) {
    std::map<int, float> seen;
    bool first = true;
    for (const int c : path) {
        lv.hero = c;
        std::vector<int> want = {c};
        for (const bool corners : {false, true})
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int n = lv.At(c % Level::kSize + dx, c / Level::kSize + dy);
                    if (n >= 0 && n != c && (dx != 0 && dy != 0) == corners) want.push_back(n);
                }
        if (first)
            std::stable_partition(want.begin(), want.end(), [&](int r) { return !putOff.count(r); });
        first = false;
        for (auto it = lv.drawn.begin(); it != lv.drawn.end();) {   // DeactivateTranslatedRoom: the record is erased
            bool keep = false;
            for (const int w : want) keep = keep || Level::Uid(w) == it->first;
            it = keep ? std::next(it) : lv.drawn.erase(it);
        }
        lv.Publish();
        for (const int r : want) {   // ActivateTranslatedRoom for the rooms not drawn yet
            if (lv.drawn.count(Level::Uid(r))) continue;
            const int sib = lv.DrawnSibling(r);
            if (sib >= 0) {   // a part of a preset already drawn: its height copied, nobody asked
                lv.drawn[Level::Uid(r)] = lv.drawn[Level::Uid(sib)];
                lv.Publish();
                seen[r] = lv.drawn[Level::Uid(r)].first;
                continue;
            }
            const float game = lv.GameHeight(r);
            float h = game;
            if (keeper) {
                Key k;
                uint64_t uid = 0;
                Check(ReadRoom(&Read, lv.Room(r), &k, &uid), "a room reads at the game's offsets");
                if (byRoom) {
                    int32_t xy[2];
                    std::memcpy(xy, reinterpret_cast<const void*>(lv.Room(r) + kRoomX), sizeof xy);
                    k = {k.level, xy[0], xy[1], 0};
                }
                Side sides[4];
                Sides(&Read, lv.Layer(), lv.Room(r), lv.edges[r].data(), sides);
                const bool drawn = DrawnCount(&Read, lv.Layer()) > 0;
                float kept = NAN;
                keeper->Kept(k, &kept);
                Keeper::How how;
                h = keeper->Ask(k, uid, game, sides, drawn, *tick, &how);
                int drawnSides = 0;
                for (const Side& sd : sides) drawnSides += sd.drawn;
                if (events) events->push_back({r, how, game, h, kept, drawnSides});
            }
            lv.drawn[Level::Uid(r)] = {h, lv.edges[r]};
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

// The rooms of a also in b: how many, and how far their heights in b are from a's moved all by one offset (0: they stand as they did).
static float ShapeDiff(const std::map<int, float>& a, const std::map<int, float>& b, int* rooms, float* offset = nullptr) {
    float m = 0.0f, off = NAN;
    *rooms = 0;
    for (const auto& [r, h] : a) {
        const auto it = b.find(r);
        if (it == b.end()) continue;
        ++*rooms;
        if (std::isnan(off)) off = it->second - h;
        m = std::max(m, std::fabs(it->second - h - off));
    }
    if (offset) *offset = off;
    return m;
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

    // ---- reading the game's memory: the room's key, its record by unique id, its drawn neighbours ----
    {
        Level lv;
        Key k;
        uint64_t uid = 0;
        Check(ReadRoom(&Read, lv.Room(lv.At(3, 2)), &k, &uid), "ReadRoom reads a room");
        Check(k.level == lv.id && k.x == 24 && k.y == 16 && k.preset == 0 && uid == Level::Uid(lv.At(3, 2)),
              "ReadRoom: level id, grid place, unique id");
        Check(!ReadRoom(&Read, 0, &k, &uid), "no room, no key");
        for (int i = 0; i < K * K; i += 3) lv.drawn[Level::Uid(i)] = {0.25f * float(i), lv.edges[i]};
        lv.Publish();
        float h = -1.0f;
        bool all = true;
        for (int i = 0; i < K * K; ++i) {
            const bool got = RecordHeight(&Read, lv.Layer(), Level::Uid(i), &h);
            all = all && got == (i % 3 == 0) && (!got || h == 0.25f * float(i));
        }
        Check(all, "RecordHeight finds every drawn room's height, and only those (the game's lower_bound)");
        Check(!RecordHeight(&Read, lv.Layer(), 0x0FFF, &h) && !RecordHeight(&Read, lv.Layer(), 0x9999, &h), "ids before the first and after the last");
        Check(DrawnCount(&Read, lv.Layer()) == 12, "DrawnCount: the layer's records");
        lv.drawn.clear();
        lv.drawn[Level::Uid(lv.At(2, 2))] = {0.0f, lv.edges[0]};   // a corner of (3,3) only, and (3,1) beside nothing
        lv.Publish();
        const auto Drawn = [&](int r, int* side = nullptr, float* ask = nullptr) {
            Side sd[4];
            Sides(&Read, lv.Layer(), lv.Room(r), lv.edges[r].data(), sd);
            int n = 0;
            for (int d = 0; d < 4; ++d)
                if (sd[d].drawn) { ++n; if (side) *side = d; if (ask) *ask = sd[d].ask; }
            return n;
        };
        Check(Drawn(lv.At(3, 3)) == 0, "Sides: a corner drawn is no neighbour of the game's");
        int side = -1;
        float ask = 0.0f;
        Check(Drawn(lv.At(3, 2), &side, &ask) == 1 && side == W && std::fabs(ask - (lv.edges[0][E] - lv.edges[lv.At(3, 2)][W])) < 1e-6f,
              "Sides: the room west drawn - its east edge + its height - the room's own west edge (the game's sum)");
        Check(Drawn(lv.At(2, 3), &side) == 1 && side == N && Drawn(lv.At(1, 2), &side) == 1 && side == E && Drawn(lv.At(2, 1), &side) == 1 &&
                  side == S,
              "Sides: a drawn room north, east or south of it");
        Check(Drawn(lv.At(4, 2)) == 0, "Sides: two rooms off in the row is no neighbour");
        lv.drawn.clear();
        lv.Publish();
        Check(!RecordHeight(&Read, lv.Layer(), Level::Uid(0), &h) && DrawnCount(&Read, lv.Layer()) == 0, "nothing drawn, nothing found");
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
        const float d1 = ShapeDiff(a, b, &n1), d2 = MaxDiff(a, c, &n2);
        if (g_verbose) std::printf("game's own: the same %d rooms %.2f out of shape on the way back, %d rooms %.2f the same way in again\n", n1, d1, n2, d2);
        Check(n1 >= 6 && d1 > 0.5f, "the game's own: the rooms come back at other heights to one another when the level is entered another way");
        Check(d2 < 1e-5f, "the game's own: the same way in gives the same heights (it is the way in, not chance)");
    }

    // ---- the fix: kept, the rooms seen before come back standing as they did, whichever way the hero comes in ----
    {
        Level lv;
        Keeper keeper;
        uint32_t tick = 0;
        std::vector<Event> ea, eb;
        const auto a = Visit(lv, fromWest, &keeper, &tick, &ea);
        bool game = true;
        for (const Event& e : ea) game = game && e.how == Keeper::How::Game && e.given == e.game;
        Check(game && keeper.KeptCount() == a.size(), "the first visit: the game's own heights, every drawn room kept");
        const auto b = Visit(lv, fromSouth, &keeper, &tick, &eb);
        int n = 0;
        float off = 0.0f;
        const float d = ShapeDiff(a, b, &n, &off);
        // The second visit starts where nothing was kept (its own frame); the kept rooms join it where the hero first meets them.
        const Event* join = nullptr;
        int kept = 0;
        for (const Event& e : eb) {
            if (!join && e.how == Keeper::How::Joined) join = &e;
            kept += e.how == Keeper::How::Kept && std::fabs(e.given - e.game) > 0.01f;
        }
        if (g_verbose)
            std::printf("kept: the same %d rooms %.4f out of shape (all moved by %.2f); joined at room %d: the game %.2f, kept %.2f; %d answers "
                        "not the game's\n", n, d, off, join ? join->room : -1, join ? join->game : 0.0f, join ? join->kept : 0.0f, kept);
        Check(n >= 6 && d < 1e-5f, "kept: the rooms seen before come back standing as they did beside one another");
        Check(kept > 0, "kept: the game's own answer would have moved some of them");
        Check(join && join->given == join->game, "kept: where the hero meets the kept rooms they stand on the ground drawn now (no step)");
        Check(join && std::fabs(join->kept - join->game) > 0.1f,
              "kept: given back as first drawn (0.188.3), that room would have stood off the ground it met - the step");
        const auto c = Visit(lv, fromWest, &keeper, &tick);
        int n2 = 0, n3 = 0;
        Check(MaxDiff(a, c, &n2) < 1e-5f && n2 == int(a.size()), "kept: a third visit the first way in, every room exactly as on the first");
        Check(ShapeDiff(b, c, &n3) < 1e-5f, "kept: ... and the second visit's rooms as they stood then");
        std::map<int, float> both = a;
        both.insert(b.begin(), b.end());
        Check(keeper.KeptCount() == both.size() && both.size() > a.size(), "kept: every room drawn on either visit, the second visit's new ones too");
        keeper.Clear();
        Check(keeper.KeptCount() == 0 && keeper.PendingCount() == 0, "Clear: another game starts with nothing kept");
    }

    // ---- 0.188.3's step: a room drawn before its neighbours (their models still loading) ----
    // On the first step (0,1) and (1,0) are put off: (1,1) is drawn with only the hero's room at its corner - the game's 0 out of nothing.
    {
        const int orphan = 1 * K + 1;
        Level kl, gl;
        Keeper keeper;
        uint32_t tick = 0, gtick = 0;
        std::vector<Event> ev;
        const std::set<int> late = {1, K};
        const auto ka = Visit(kl, fromWest, &keeper, &tick, &ev, false, late);
        const auto ga = Visit(gl, fromWest, nullptr, &gtick, nullptr, false, late);
        Check(ka.at(orphan) == 0.0f && ga.at(orphan) == 0.0f, "put off: the game draws the room before its neighbours at 0");
        bool told = false;
        for (const Event& e : ev) told = told || (e.room == orphan && e.how == Keeper::How::Orphan);
        float h = 0.0f;
        Key k;
        uint64_t uid = 0;
        ReadRoom(&Read, kl.Room(orphan), &k, &uid);
        Check(told && !keeper.Kept(k, &h), "put off: the keeper knows it for an orphan and does not keep its 0");
        const auto kb = Visit(kl, fromWest, &keeper, &tick);
        const auto gb = Visit(gl, fromWest, nullptr, &gtick);
        if (g_verbose) std::printf("orphan: drawn at %.2f first, %.2f the next visit (the game's own: %.2f)\n", ka.at(orphan), kb.at(orphan), gb.at(orphan));
        Check(std::fabs(gb.at(orphan)) > 0.1f && kb.at(orphan) == gb.at(orphan),
              "put off: the next visit it stands where the game stitches it - the 0 kept would have been a step every visit");
        Check(keeper.Kept(k, &h) && h == kb.at(orphan), "put off: kept once it is drawn with its neighbours");
    }

    // ---- a preset of two rooms: the first way in reaches one of them, the second way in the other ----
    // (4,1) and (4,2): the walk along the top row draws (4,1) only; the walk up the east column comes to (4,2) first, (4,1) a step later.
    {
        const int pa = 1 * K + 4, pb = 2 * K + 4, ref = 1 * K + 3;
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
            Check(a.count(pa) && !a.count(pb) && a.count(ref), "the preset: the first walk draws only one of its rooms");
            const auto b = Visit(lv, fromSouth, &keeper, &tick, nullptr, byRoom);
            Check(b.count(pa) && b.count(pb) && b.at(pa) == b.at(pb) && b.count(ref), "the preset: drawn together, its rooms have one height (the copy)");
            const auto c = Visit(lv, fromWest, &keeper, &tick, nullptr, byRoom);
            const float ra = a.at(pa) - a.at(ref), rb = b.at(pa) - b.at(ref), rc = c.at(pa) - c.at(ref);
            if (g_verbose)
                std::printf("preset kept by %s: %.2f over the room west of it on the first walk, %.2f on the second, %.2f on the third\n",
                            byRoom ? "room" : "preset", ra, rb, rc);
            if (byRoom) {
                Check(std::fabs(rb - ra) > 0.1f, "kept by room (0.183): the preset comes back at another height on the way back");
            } else {
                Check(std::fabs(rb - ra) < 1e-5f && std::fabs(rc - ra) < 1e-5f,
                      "kept by preset: the preset stands as it did beside its neighbour on every walk, whichever of its rooms comes first");
                int n = 0;
                Check(ShapeDiff(a, b, &n) < 1e-5f && ShapeDiff(b, c, &n) < 1e-5f, "kept by preset: every room seen before as it was");
            }
        }
    }

    // ---- 0.188.4's terrace by the Fortress gate: "level 104 preset 1040,992 ... the game worked out -9.00, given 0.00" ----
    // The town (columns 0 - 1) and the Outer Steppes (2 - 5) meet at a cliff: the steppes' west edge stands 9 over the town's, so the
    // steppes stitched from the town lie at -9. The hero arrives at (1,4) while (1,3) and (2,4) still load: (2,3) is drawn first at 0 out of
    // nothing, he walks onto it and east, and the steppes round him are stitched to its 0 - a slab 9 over the ground, the preset at
    // 1040,992 ((5,2), one map with (5,1)) in it. He walks back to the town and out again by the south: the game stitches it all at -9.
    {
        const int32_t baseX = 1000, baseY = 976;   // (5,2) at 1040,992
        const auto Make = [&](Level& lv) {
            for (auto& e : lv.edges) e = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int y = 0; y < K; ++y) lv.edges[lv.At(2, y)][W] = 9.0f;
            lv.MakePreset(lv.At(5, 2), lv.At(5, 1));
        };
        Level kl(104, baseX, baseY), gl(104, baseX, baseY);
        Make(kl);
        Make(gl);
        const int P = kl.At(5, 2);
        Key kp;
        uint64_t up = 0;
        Check(ReadRoom(&Read, kl.Room(P), &kp, &up) && kp.level == 104 && kp.preset == 1 && kp.x == 1040 && kp.y == 992,
              "the terrace: the preset reads as level 104 preset 1040,992");
        std::vector<int> path;
        for (const auto& [x, y] : std::vector<std::pair<int, int>>{{1, 4}, {2, 3}, {3, 3}, {4, 3}, {3, 3}, {2, 3}, {1, 3}, {1, 4}, {2, 4}, {3, 4},
                                                                    {4, 4}, {4, 3}})
            path.push_back(kl.At(x, y));
        const std::set<int> late = {kl.At(1, 3), kl.At(2, 4)};
        Keeper keeper;
        uint32_t tick = 0, gtick = 0;
        std::vector<Event> ev;
        const auto ks = Visit(kl, path, &keeper, &tick, &ev, false, late);
        const auto gs = Visit(gl, path, nullptr, &gtick, nullptr, false, late);
        // the game's own: the preset drawn at 0 in the slab first, at -9 on the way back
        const Event* first = nullptr;
        const Event* last = nullptr;
        for (const Event& e : ev)
            if (e.room == P) { if (!first) first = &e; last = &e; }
        Check(first && last && first != last && first->game == 0.0f && last->game == -9.0f && gs.at(P) == -9.0f,
              "the terrace: the game draws the preset at 0 in the slab first, at -9 on the way back");
        if (g_verbose && first && last)
            std::printf("terrace: the preset first %.2f (the game's %.2f, %s), on the way back given %.2f (the game's %.2f)\n", first->given,
                        first->game, first->how == Keeper::How::Orphan ? "not kept" : "kept", last->given, last->game);
        Check(first && first->how == Keeper::How::Orphan && first->drawnSides > 0,
              "the terrace: its first 0 came beside drawn rooms (0.188.4 kept it) - from the orphan's ground, so not kept");
        Check(last && last->given == -9.0f, "the terrace: on the way back the preset stands on the ground at -9 - no terrace");
        // What is kept - what every later visit gets - is the real ground: the town at 0, the steppes 9 under it, nothing of the slab.
        // (Rooms of the slab still drawn round him stay as the game drew them for this visit, as in Unleashed: nothing asks them again.)
        bool real = true;
        int keptRooms = 0, slabDrawn = 0;
        for (int r = 0; r < K * K; ++r) {
            Key kr;
            uint64_t ur = 0;
            float hr = 0.0f;
            ReadRoom(&Read, kl.Room(r), &kr, &ur);
            if (!keeper.Kept(kr, &hr)) continue;
            ++keptRooms;
            const bool ok = hr == (r % K < 2 ? 0.0f : -9.0f);
            real = real && ok;
            if (!ok && g_verbose) std::printf("  terrace room (%d,%d) kept at %.2f\n", r % K, r / K, hr);
        }
        for (const Event& e : ev) slabDrawn += e.how == Keeper::How::Orphan && e.given == 0.0f;
        Check(real && keptRooms >= 15, "the terrace: every room kept stands on the real ground - the town at 0, the steppes at -9");
        Check(slabDrawn >= 4, "the terrace: the slab's rooms drawn at the game's 0 for that visit, none kept");
        float h = 0.0f;
        Check(keeper.Kept(kp, &h) && h == -9.0f, "the terrace: the preset kept at -9, as the ground round it");
    }

    // ---- the keeper's answers, alone ----
    {
        Level lv;
        Keeper keeper;
        Keeper::How how;
        Key k[4];
        uint64_t u[4];
        for (int i = 0; i < 4; ++i) ReadRoom(&Read, lv.Room(i), &k[i], &u[i]);
        const Side none[4] = {};
        keeper.Ask(k[0], u[0], 0.0f, none, false, 0, &how);   // a frame starts: three rooms kept at 0, one island
        for (int i = 1; i < 3; ++i) {
            const Side s[4] = {{true, 0.0f, k[0]}, {}, {}, {}};
            keeper.Ask(k[i], u[i], 0.0f, s, true, 0, &how);
        }
        for (int i = 0; i < 3; ++i) lv.drawn[u[i]] = {0.0f, lv.edges[i]};
        lv.Publish();
        Check(keeper.Harvest(&Read, lv.Layer(), 1) == 3, "three rooms kept, one island");
        Key orphan = k[3];   // drawn, never kept: an orphan's ground
        const Side beside[4] = {{true, -9.0f, k[1]}, {}, {true, 0.0f, orphan}, {}};
        Check(keeper.Ask(Key{105, 99, 99, 0}, 77, 0.0f, beside, true, 2, &how) == -9.0f && how == Keeper::How::Grounded,
              "a new room the game stitched to an orphan's ground: its grounded neighbour's height");
        const Side even[4] = {{true, 0.0f, k[1]}, {true, -9.0f, k[2]}, {true, -9.0f, orphan}, {}};
        Check(keeper.Ask(k[0], u[0], -9.0f, even, true, 4, &how) == 0.0f && how == Keeper::How::Kept,
              "a kept room asked again: its kept height, whatever the game stitched it to now (the way back does not move it)");
    }

    // ---- a room put off: no record yet, nothing kept until it is really drawn ----
    {
        Level lv;
        Keeper keeper;
        Key k;
        uint64_t uid = 0;
        Keeper::How how;
        const int r = lv.At(2, 2);
        ReadRoom(&Read, lv.Room(r), &k, &uid);
        const Side none[4] = {};
        Check(keeper.Ask(k, uid, 0.5f, none, false, 0, &how) == 0.5f && how == Keeper::How::Game, "the first room of a frame: the game's");
        Check(keeper.Harvest(&Read, lv.Layer(), 1) == 0 && keeper.PendingCount() == 1, "asked but not drawn: pending, not kept");
        lv.drawn[uid] = {1.75f, lv.edges[r]};
        lv.Publish();
        float h = 0.0f;
        Check(keeper.Harvest(&Read, lv.Layer(), 2) == 1 && keeper.Kept(k, &h) && h == 1.75f && keeper.PendingCount() == 0,
              "drawn on a later tick: the height it was drawn at is kept");
        Check(keeper.Ask(k, uid, -3.0f, none, true, 3, &how) == 1.75f && how == Keeper::How::Kept, "asked again while its island is drawn: kept");
        Key other = k;
        other.x += 8;
        const Side besideKept[4] = {{true, 0.5f, k}, {}, {}, {}};   // its west neighbour the kept room, asking 0.5 of it
        Check(keeper.Ask(other, uid + 1, 0.5f, besideKept, true, 10, &how) == 0.5f && how == Keeper::How::Game, "a room never drawn: the game's own");
        Check(keeper.Harvest(&Read, lv.Layer(), 10 + Keeper::kPendingTicks + 1) == 0 && keeper.PendingCount() == 0,
              "a room never drawn is forgotten after a while (asked again when it is)");
        lv.drawn.clear();
        lv.Publish();
        keeper.Harvest(&Read, lv.Layer(), 300);   // nothing of its island drawn: it is placed again by the next room asked
        Check(keeper.Ask(k, uid, 4.0f, none, false, 301, &how) == 4.0f && how == Keeper::How::Joined, "its island not drawn: placed by the game's answer");
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
