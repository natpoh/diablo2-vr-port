// The wrist status (vr/wrist_status.h, vr/wrist_status_paint.cpp): the tables, the reads of the game's pet lists against a made-up heap, the state capture, the cells and
// their order, the settings, the glance gate and where the panel hangs - and the picture, drawn. No game, no headset.
//
//   wrist_status_test [<folder for the pictures>]
//
// Layout of the made-up heap = the layout the real reads rely on (vr/wrist_status.h ReadRoster's comment): a player unit {type 0, class, id, mode}; +0x10 its data; the
// data's +0x98 the pet root; the root's first pointer an array of 32-byte entries, each a list {head, count, room}; the nodes {flags, id, 16 bytes, next at +24}.

#include "../../vr/wrist_status.h"
#include "../../vr/wrist_status_paint.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int g_fails = 0, g_checks = 0;
#define EXPECT(c)                                                       \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(c)) {                                                     \
            ++g_fails;                                                  \
            printf("  FAIL line %d: %s\n", __LINE__, #c);               \
        }                                                               \
    } while (0)

using namespace wriststatus;
using hset::Q;
using hset::V3;

// ---- a made-up heap ----------------------------------------------------------------------------------------------------------------------------

struct Heap {
    static constexpr uintptr_t kBase = 0x200000;
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x20000, 0);
    template <class T>
    void Put(uintptr_t at, const T& v) {
        std::memcpy(&mem[at - kBase], &v, sizeof v);
    }
    template <class T>
    bool Get(uintptr_t at, T& v) const {
        if (at < kBase || at - kBase + sizeof v > mem.size()) return false;
        std::memcpy(&v, &mem[at - kBase], sizeof v);
        return true;
    }
    // The reader the template wants: `read(address, value)`.
    auto Reader() const {
        return [this](uintptr_t at, auto& v) { return Get(at, v); };
    }
};

constexpr uintptr_t kPlayer = Heap::kBase + 0x0000, kData = Heap::kBase + 0x1000, kRoot = Heap::kBase + 0x2000, kLists = Heap::kBase + 0x3000, kNodes = Heap::kBase + 0x8000;

struct NodeWrite {
    uint32_t flags, id;
};

// One hero: `members[type]` = its nodes; `room[type]` = the list's room. Types not mentioned have empty lists.
struct Hero {
    std::map<unsigned, std::vector<NodeWrite>> members;
    std::map<unsigned, int> room;
    uint32_t mode = 1, cls = 3, id = 77;
    bool petRoot = true;
};

unsigned Build(Heap& h, const Hero& hero, unsigned typeCount = 22) {
    const uint32_t head[4] = {0, hero.cls, hero.id, hero.mode};
    h.Put(kPlayer, head);
    h.Put<uintptr_t>(kPlayer + 0x10, kData);
    h.Put<uintptr_t>(kData + 0x98, hero.petRoot ? kRoot : 0);
    h.Put<uintptr_t>(kRoot, kLists);
    uintptr_t next = kNodes;
    for (unsigned t = 1; t < typeCount; ++t) {
        const uintptr_t entry = kLists + t * 32;
        const auto m = hero.members.find(t);
        const std::vector<NodeWrite> none;
        const std::vector<NodeWrite>& nodes = m == hero.members.end() ? none : m->second;
        uintptr_t first = 0;
        for (size_t i = 0; i < nodes.size(); ++i) {
            const uintptr_t at = next;
            next += 32;
            h.Put<uint32_t>(at, nodes[i].flags);
            h.Put<uint32_t>(at + 4, nodes[i].id);
            h.Put<uintptr_t>(at + 24, i + 1 < nodes.size() ? next : 0);   // the next node is the one after it
            if (!first) first = at;
        }
        h.Put<uintptr_t>(entry, first);
        h.Put<int32_t>(entry + 8, (int32_t)nodes.size());
        const auto r = hero.room.find(t);
        h.Put<int32_t>(entry + 12, r == hero.room.end() ? (int32_t)nodes.size() : r->second);
    }
    return typeCount;
}

void TestTables() {
    printf("the tables\n");
    std::set<int> types;
    for (const Companion& c : kCompanions) {
        EXPECT(c.type > 0 && c.type < kPetTypes);
        EXPECT(types.insert(c.type).second);   // each pet type once
        EXPECT(c.name && *c.name && c.tag && std::strlen(c.tag) >= 2 && std::strlen(c.tag) <= 3);
    }
    EXPECT(kCompanions[0].type == 7);   // the mercenary first
    std::set<std::pair<int, bool>> states;
    for (const Effect& e : kEffects) {
        EXPECT(e.state > 0 && e.state < (int)kMaxStates && e.name && *e.name);
        EXPECT(states.insert({e.state, e.kind == Kind::Harm}).second);   // a state once as helpful and once as harmful at most
    }
    EXPECT(std::strlen(kEffects[0].name) > 0 && Count(Status{}) == 0);
    EXPECT(Bit(0) == 0 && Bit(kPetTypes) == 0 && Bit(4) == 16u && Bit(21) == (1u << 21));
}

void TestRoster() {
    printf("the pet lists, read from a made-up heap\n");
    Heap h;
    Hero hero;
    hero.members[4] = {{0, 1001}, {0, 1002}, {0, 1003}};   // three skeletons
    hero.members[5] = {{0, 1004}};                          // a mage
    hero.members[7] = {{1, 1005}};                          // the mercenary, dead
    hero.room[3] = 1;                                       // golems: learned, none alive
    hero.room[10] = 0;                                      // ravens: not learned
    hero.room[7] = 1;
    const unsigned n = Build(h, hero);
    Roster r = ReadRoster(kPlayer, n, h.Reader());
    EXPECT(r.known);
    EXPECT(r.alive[4] == 3 && r.alive[5] == 1 && r.alive[7] == 0 && r.alive[3] == 0 && r.alive[10] == 0);
    EXPECT((r.relevant & Bit(4)) && (r.relevant & Bit(5)) && (r.relevant & Bit(7)) && (r.relevant & Bit(3)));   // has members, or has room (a golem learned)
    EXPECT(!(r.relevant & Bit(10)) && !(r.relevant & Bit(11)));
    EXPECT(r == ReadRoster(kPlayer, n, h.Reader()));
    // a mercenary's list with room but no record is not shown (every hero's list has room for one)
    Hero none;
    none.room[7] = 1;
    Heap h2;
    Build(h2, none);
    const Roster r2 = ReadRoster(kPlayer, 22, h2.Reader());
    EXPECT(r2.known && !(r2.relevant & Bit(7)) && r2.alive[7] == 0);
    // a living mercenary
    Hero alive = none;
    alive.members[7] = {{0, 2001}};
    Heap h3;
    Build(h3, alive);
    const Roster r3 = ReadRoster(kPlayer, 22, h3.Reader());
    EXPECT(r3.known && r3.alive[7] == 1 && (r3.relevant & Bit(7)));
    // no pet root: the hero has none
    Hero bare;
    bare.petRoot = false;
    Heap h4;
    Build(h4, bare);
    const Roster r4 = ReadRoster(kPlayer, 22, h4.Reader());
    EXPECT(r4.known && r4.relevant == 0);
    // what makes the whole read unknown
    auto unknown = [&](const char* what, const Roster& rr) {
        EXPECT(!rr.known && rr.relevant == 0);
        (void)what;
    };
    {
        Heap g;
        Build(g, hero);
        const uint32_t notPlayer[4] = {1, 3, 77, 1};
        g.Put(kPlayer, notPlayer);
        unknown("a monster, not a player", ReadRoster(kPlayer, 22, g.Reader()));
    }
    {
        Heap g;
        Hero dead = hero;
        dead.mode = 0;
        Build(g, dead);
        unknown("a dead hero", ReadRoster(kPlayer, 22, g.Reader()));
        Hero portal = hero;
        portal.mode = 17;
        Build(g, portal);
        unknown("in the town portal", ReadRoster(kPlayer, 22, g.Reader()));
        Hero wild = hero;
        wild.cls = 9;
        Build(g, wild);
        unknown("a class that is not one", ReadRoster(kPlayer, 22, g.Reader()));
    }
    unknown("too few types", ReadRoster(kPlayer, 19, h.Reader()));
    unknown("too many types", ReadRoster(kPlayer, 65, h.Reader()));
    unknown("a read that fails (no such address)", ReadRoster(0x1234, 22, h.Reader()));
    {
        Heap g;
        Build(g, hero);
        g.Put<int32_t>(kLists + 4 * 32 + 8, 2);   // the list says 2, it holds 3
        unknown("a count that does not match", ReadRoster(kPlayer, 22, g.Reader()));
        Build(g, hero);
        g.Put<int32_t>(kLists + 4 * 32 + 8, 4);   // the list says 4, it holds 3
        unknown("a count that is too big", ReadRoster(kPlayer, 22, g.Reader()));
        Build(g, hero);
        g.Put<uintptr_t>(kNodes + 24, kNodes);   // the first skeleton points to itself
        unknown("a loop", ReadRoster(kPlayer, 22, g.Reader()));
        Build(g, hero);
        g.Put<uint32_t>(kNodes + 4, 0xFFFFFFFFu);   // an id of all ones
        unknown("an id of all ones", ReadRoster(kPlayer, 22, g.Reader()));
        Build(g, hero);
        g.Put<int32_t>(kLists + 4 * 32 + 8, -1);
        unknown("a negative count", ReadRoster(kPlayer, 22, g.Reader()));
        Build(g, hero);
        g.Put<uintptr_t>(kNodes + 2 * 32 + 24, 0x10);   // the last skeleton's next points nowhere readable
        unknown("a next pointer into nothing", ReadRoster(kPlayer, 22, g.Reader()));
        Build(g, hero);
        g.Put<uintptr_t>(kRoot, 0);
        unknown("no list array", ReadRoster(kPlayer, 22, g.Reader()));
    }
    // a game with more types than we know of: the unknown ones are walked (and must add up) but not shown
    {
        Hero big = hero;
        big.members[30] = {{0, 3001}, {0, 3002}};
        Heap g;
        const unsigned count = Build(g, big, 40);
        const Roster rb = ReadRoster(kPlayer, count, g.Reader());
        EXPECT(rb.known && rb.alive[4] == 3 && rb.relevant == r.relevant);
    }
}

void TestStatus() {
    printf("the states\n");
    std::set<int> on = {33, 9, 100, 217};   // Might, Amplify Damage, a healing potion, Hex Purge (harm)
    int asked = 0;
    const Status s = Capture(200, [&](int state) { ++asked; return on.count(state) ? 1 : 0; });
    EXPECT(s.known && s.Has(33) && s.Has(9) && s.Has(100));
    EXPECT(!s.Has(217));   // past the table's row count (200): not asked, not on
    EXPECT(!s.Has(34) && !s.Has(0) && !s.Has(-1) && !s.Has(600));
    EXPECT(asked > 80);
    EXPECT(Count(s) == 3);
    const Status all = Capture(512, [&](int state) { return on.count(state) ? 1 : 0; });
    EXPECT(all.Has(217) && Count(all) == 4);
    EXPECT(!Capture(179, [&](int) { return 0; }).known && !Capture(513, [&](int) { return 0; }).known);   // not the game this was made for
    EXPECT(!Capture(200, [&](int state) { return state == 50 ? 2 : 0; }).known);                            // an answer that is neither 1 nor 0 discards the whole read
    EXPECT(!Capture(200, [&](int state) { return state == 50 ? -1 : 0; }).known);
    const Status none = Capture(200, [&](int) { return 0; });
    EXPECT(none.known && Count(none) == 0 && !none.Has(33));
    Status unread;
    unread.Set(33);
    EXPECT(!unread.Has(33));   // not known: nothing is on
    EXPECT(s == Capture(200, [&](int state) { return on.count(state) ? 1 : 0; }));
}

Roster MakeRoster(std::initializer_list<std::pair<int, int>> counts, std::initializer_list<int> zero = {}) {
    Roster r;
    r.known = true;
    for (auto& c : counts) {
        r.alive[(size_t)c.first] = (uint16_t)c.second;
        r.relevant |= Bit(c.first);
    }
    for (int z : zero) r.relevant |= Bit(z);
    return r;
}
Status MakeStatus(std::initializer_list<int> states) {
    Status s;
    s.known = true;
    for (int x : states) s.Set(x);
    return s;
}

void TestLook() {
    printf("the cells\n");
    Config cfg;
    cfg.on = true;
    const Roster ro = MakeRoster({{4, 5}, {7, 1}, {5, 2}}, {3, 10 /* a dead mercenary elsewhere */});
    const Status st = MakeStatus({33, 9, 100, 11, 128, 95});   // Might, Amplify Damage, healing potion, Chilled, armor shrine, Berserk (other)
    Look l = MakeLook(ro, st, kContentCompanions | kContentEffects, cfg, 5);
    // the companions first (the mercenary, then the summons in the table's order), the harmful effects, then the others in the table's order
    EXPECT(l.n == 5 + 2 + 4);   // the mercenary, 5 skeletons' chip, mages, a golem at zero, ravens at zero; two harmful; four others
    EXPECT(l.cell[0].kind == CellKind::Companion && l.cell[0].id == 7 && l.cell[0].count == 1);
    EXPECT(l.cell[1].id == 4 && l.cell[1].count == 5 && l.cell[2].id == 5 && l.cell[2].count == 2 && l.cell[3].id == 3 && l.cell[3].count == 0);
    EXPECT(l.cell[4].kind == CellKind::Companion && l.cell[4].id == 10 && l.cell[4].count == 0);
    EXPECT(l.cell[5].kind == CellKind::Harm && l.cell[6].kind == CellKind::Harm);                     // Chilled and Amplify Damage
    EXPECT(l.cell[5].id == 11 && l.cell[6].id == 9);                                                   // (the table's order: Chilled before Amplify Damage)
    EXPECT(l.cell[7].kind == CellKind::Benefit && l.cell[7].id == 33);                                // Might
    EXPECT(l.icons == 5 && l.opacity == 20);
    // what a wrist shows
    Look c = MakeLook(ro, st, kContentCompanions, cfg, 0);
    EXPECT(c.n == 5);
    Look e = MakeLook(ro, st, kContentEffects, cfg, 0);
    EXPECT(e.n == 6 && e.cell[0].kind == CellKind::Harm);
    EXPECT(MakeLook(ro, st, 0, cfg, 0).n == 0);
    EXPECT(MakeLook(Roster{}, Status{}, 3, cfg, 0).n == 0);                       // nothing known: nothing to show
    EXPECT(MakeLook(ro, Status{}, 3, cfg, 0).n == 5 && MakeLook(Roster{}, st, 3, cfg, 0).n == 6);   // each part on its own
    EXPECT(MakeLook(MakeRoster({}), MakeStatus({}), 3, cfg, 0).n == 0);            // known, and empty
    // icons: a companion's skill, an effect's skill, the ones with none
    EXPECT(l.cell[1].skill == 70 && l.cell[0].skill == -1 && l.cell[7].skill == 98);
    // opacity in steps of 0.05, held in range
    Config dim = cfg;
    dim.opacity = 0.5f;
    EXPECT(MakeLook(ro, st, 3, dim, 0).opacity == 10);
    dim.opacity = 9.0f;
    EXPECT(MakeLook(ro, st, 3, dim, 0).opacity == 20);
    dim.opacity = std::nanf("");
    EXPECT(MakeLook(ro, st, 3, dim, 0).opacity == 20);
    // the same input is the same look (no repaint), a changed count or state is not
    EXPECT(l == MakeLook(ro, st, 3, cfg, 5));
    Roster ro2 = ro;
    ro2.alive[4] = 4;
    EXPECT(!(l == MakeLook(ro2, st, 3, cfg, 5)));
    EXPECT(!(l == MakeLook(ro, MakeStatus({33}), 3, cfg, 5)));
    EXPECT(!(l == MakeLook(ro, st, 3, cfg, 6)));   // a new icon decoded
    // more than the grid holds: the last cell says how many were left out
    Status crowd;
    crowd.known = true;
    for (const Effect& ef : kEffects) crowd.Set(ef.state);
    const Look big = MakeLook(ro, crowd, 3, cfg, 0);
    EXPECT(big.n == kMaxCells && big.cell[kMaxCells - 1].kind == CellKind::More);
    int expectHidden = 0;
    {
        int total = 5;   // the companions of `ro`
        std::set<std::pair<int, bool>> seen;
        for (const Effect& ef : kEffects) total += 1 * (seen.insert({ef.state, ef.kind == Kind::Harm}).second ? 1 : 0);
        expectHidden = total - (kMaxCells - 1);
    }
    EXPECT(big.cell[kMaxCells - 1].id == std::min(999, expectHidden));
    EXPECT(big.cell[0].kind == CellKind::Companion);   // the companions are never the ones left out
    // the harmful ones come before the helpful ones among the effects
    bool seenGood = false, order = true;
    for (int i = 0; i < big.n - 1; ++i) {
        if (big.cell[(size_t)i].kind == CellKind::Companion) continue;
        if (big.cell[(size_t)i].kind != CellKind::Harm) seenGood = true;
        else if (seenGood) order = false;
    }
    EXPECT(order);
}

void TestSlots() {
    printf("the grid\n");
    EXPECT(RowsFor(0) == 0 && RowsFor(1) == 1 && RowsFor(6) == 1 && RowsFor(7) == 2 && RowsFor(12) == 2 && RowsFor(13) == 3 && RowsFor(18) == 3 && RowsFor(40) == 3);
    // every cell inside the picture, none on another, rows centred
    for (int n = 1; n <= kMaxCells; ++n) {
        std::set<std::pair<int, int>> at;
        bool inside = true;
        for (int i = 0; i < n; ++i) {
            const Slot s = SlotOf(i, n);
            inside = inside && s.x >= 0 && s.y >= 0 && s.x + 64 <= kPicW && s.y + 64 <= kPicH;
            EXPECT(at.insert({s.x, s.y}).second);
        }
        EXPECT(inside);
    }
    EXPECT(SlotOf(0, 1).x == (kPicW - 64) / 2 && SlotOf(0, 1).y == (kPicH - 64) / 2);   // one cell: the middle
    EXPECT(SlotOf(0, 6).x == 0 && SlotOf(5, 6).x == kPicW - 64);                        // a full row: edge to edge
    // 7 cells: 4 + 3, the second row's three centred
    EXPECT(SlotOf(0, 7).y == SlotOf(3, 7).y && SlotOf(4, 7).y == SlotOf(0, 7).y + 64 && SlotOf(4, 7).x == (kPicW - 3 * 64) / 2);
    EXPECT(SlotOf(0, 18).x == 0 && SlotOf(17, 18).x == kPicW - 64 && SlotOf(17, 18).y == kPicH - 64 && SlotOf(0, 18).y == 0);
    EXPECT(SlotOf(5, 5).x == 0 && SlotOf(-1, 5).x == 0 && SlotOf(0, 0).y == 0);   // out of range: nothing
}

void TestConfig() {
    printf("the settings\n");
    Config d;
    EXPECT(!d.on && d.companions && d.effects && d.sides == 0 && d.sizeCm == 10.0f && d.heightCm == 7.0f && d.alongCm == 0.0f && d.coneDeg == 40.0f && d.opacity == 1.0f && !d.flip);
    Config w;
    w.sides = 9;
    w.sizeCm = 99.0f;
    w.heightCm = -50.0f;
    w.alongCm = std::nanf("");
    w.coneDeg = 5.0f;
    w.opacity = 0.0f;
    const Config k = Sanitized(w);
    EXPECT(k.sides == 0 && k.sizeCm == kSizeMaxCm && k.heightCm == handvitals::kHeightMin && k.alongCm == 0.0f && k.coneDeg == handvitals::kConeMin && k.opacity == kOpacityMin);
    w.sizeCm = 1.0f;
    w.heightCm = 99.0f;
    w.alongCm = 99.0f;
    w.coneDeg = 99.0f;
    const Config k2 = Sanitized(w);
    EXPECT(k2.sizeCm == kSizeMinCm && k2.heightCm == handvitals::kHeightMax && k2.alongCm == kAlongMaxCm && k2.coneDeg == handvitals::kConeMax);
    // which wrist shows what
    Config c;
    EXPECT(ContentFor(c, 0) == kContentEffects && ContentFor(c, 1) == kContentCompanions);
    c.sides = 1;
    EXPECT(ContentFor(c, 0) == kContentCompanions && ContentFor(c, 1) == kContentEffects);
    c.sides = 2;
    EXPECT(ContentFor(c, 0) == (kContentCompanions | kContentEffects) && ContentFor(c, 1) == (kContentCompanions | kContentEffects));
    c.sides = 0;
    c.companions = false;
    EXPECT(ContentFor(c, 0) == kContentEffects && ContentFor(c, 1) == 0);
    c.companions = true;
    c.effects = false;
    EXPECT(ContentFor(c, 0) == 0 && ContentFor(c, 1) == kContentCompanions);
    c.sides = 2;
    EXPECT(ContentFor(c, 0) == kContentCompanions);
    EXPECT(Fresh(0.0) && Fresh(1999.0) && !Fresh(2001.0) && !Fresh(-1.0) && !Fresh(std::nan("")));
}

// ---- the glance gate and the hang ---------------------------------------------------------------------------------------------------------------

V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Q FromTo(V3 a, V3 b) {
    const float d = hset::Dot(a, b);
    if (d > 0.999999f) return {};
    if (d < -0.999999f) return {0.0f, 1.0f, 0.0f, 0.0f};   // half a turn about +y (the two are along x or z: good enough for a test)
    const V3 c = Cross(a, b);
    Q q{c.x, c.y, c.z, 1.0f + d};
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

// A hand at `pos` whose BACK faces `toward` (off by `deg` degrees sideways): the left grip's back is -X, the right's +X.
handvitals::HandPose BackTo(int side, V3 pos, V3 head, float deg) {
    handvitals::HandPose h;
    h.valid = true;
    h.pos = pos;
    const V3 dir = hset::Unit(hset::Sub(head, pos));
    const V3 perp = hset::Unit(Cross(dir, {0, 1, 0}));
    const float a = deg * 3.14159265f / 180.0f;
    const V3 n = hset::Unit(hset::Add(hset::Mul(dir, std::cos(a)), hset::Mul(perp, std::sin(a))));
    h.rot = FromTo({side == 0 ? -1.0f : 1.0f, 0.0f, 0.0f}, n);
    return h;
}

void TestGate() {
    printf("the glance: the back of a wrist turned to the eyes\n");
    Config cfg;
    cfg.on = true;
    const V3 head{0.0f, 1.6f, 0.0f};
    for (int side = 0; side < 2; ++side) {
        const V3 pos{side == 0 ? -0.05f : 0.05f, 1.45f, -0.38f};
        handvitals::Reveal reveal;
        handvitals::In in;
        in.cfg = RevealConfig(cfg, side);
        in.head.valid = true;
        in.head.pos = head;
        in.head.rot = {};
        in.hand[side] = BackTo(side, pos, head, 0.0f);
        in.hand[1 - side] = BackTo(1 - side, {side == 0 ? 0.3f : -0.3f, 0.9f, -0.2f}, head, 0.0f);   // the other hand hangs low: it never comes up
        in.dt = 1.0f / 90.0f;
        handvitals::Out out;
        for (int i = 0; i < 70; ++i) out = reveal.Step(in);
        EXPECT(out.alpha > 0.99f && out.side == side && out.pose.valid);                     // the back of the wrist at the face: up, over that wrist
        EXPECT(std::fabs(out.pose.p.y - (1.45f + 0.07f)) < 0.02f);                            // 7 cm up (and 3 cm toward the head)
        const V3 z = hset::Rot(out.pose.q, {0, 0, 1});
        EXPECT(hset::Dot(z, hset::Unit(hset::Sub(head, out.pose.p))) > 0.999f);                // its front toward the eyes
        // turned 20 degrees off: still up; 70 degrees off: it goes
        in.hand[side] = BackTo(side, pos, head, 20.0f);
        for (int i = 0; i < 20; ++i) out = reveal.Step(in);
        EXPECT(out.alpha > 0.99f);
        in.hand[side] = BackTo(side, pos, head, 70.0f);
        for (int i = 0; i < 90; ++i) out = reveal.Step(in);
        EXPECT(out.alpha == 0.0f && out.side == -1);
        // the PALM turned to the eyes brings nothing up (that is the palm panel's)
        handvitals::Reveal palm;
        in.hand[side] = BackTo(side, pos, head, 0.0f);
        in.hand[side].rot = FromTo({side == 0 ? 1.0f : -1.0f, 0.0f, 0.0f}, hset::Unit(hset::Sub(head, pos)));   // the palm's normal at the eyes
        for (int i = 0; i < 70; ++i) out = palm.Step(in);
        EXPECT(out.alpha == 0.0f);
        // the debug flip makes the palm the one
        Config flipped = cfg;
        flipped.flip = true;
        in.cfg = RevealConfig(flipped, side);
        handvitals::Reveal other;
        for (int i = 0; i < 70; ++i) out = other.Step(in);
        EXPECT(out.alpha > 0.99f);
    }
    // only that wrist is measured: the other hand's back at the face does not bring this reveal up
    {
        handvitals::Reveal reveal;
        handvitals::In in;
        in.cfg = RevealConfig(cfg, 0);
        in.head.valid = true;
        in.head.pos = head;
        in.hand[0] = BackTo(0, {-0.5f, 0.5f, -0.3f}, head, 0.0f);   // the left is low
        in.hand[1] = BackTo(1, {0.05f, 1.45f, -0.38f}, head, 0.0f);   // the right is at the face
        in.dt = 1.0f / 90.0f;
        handvitals::Out out;
        for (int i = 0; i < 70; ++i) out = reveal.Step(in);
        EXPECT(out.alpha == 0.0f);
    }
    // the hang: along the arm moves the anchor along the grip's +Z (toward the elbow)
    const Q id{};
    const V3 a = Anchor({0.1f, 1.0f, -0.3f}, id, 5.0f);
    EXPECT(std::fabs(a.x - 0.1f) < 1e-6f && std::fabs(a.y - 1.0f) < 1e-6f && std::fabs(a.z - (-0.25f)) < 1e-6f);
    const Q quarter = {0.0f, std::sin(3.14159265f / 4.0f), 0.0f, std::cos(3.14159265f / 4.0f)};   // 90 degrees about y: +Z goes to +X
    const V3 b = Anchor({0.0f, 1.0f, 0.0f}, quarter, 10.0f);
    EXPECT(std::fabs(b.x - 0.1f) < 1e-5f && std::fabs(b.z) < 1e-5f);
    EXPECT(std::fabs(Anchor({1, 2, 3}, id, 0.0f).z - 3.0f) < 1e-6f);
    Config sz;
    sz.sizeCm = 12.0f;
    EXPECT(std::fabs(PanelWidthM(sz) - 0.12f) < 1e-6f && std::fabs(PanelHeightM(sz) - 0.06f) < 1e-6f);
}

// ---- the picture ----------------------------------------------------------------------------------------------------------------------------------

bool WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    auto crc = [](const uint8_t* d, size_t n, uint32_t c) {
        static uint32_t table[256];
        static bool made = false;
        if (!made) {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t v = i;
                for (int k = 0; k < 8; ++k) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
                table[i] = v;
            }
            made = true;
        }
        c = ~c;
        for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 255] ^ (c >> 8);
        return ~c;
    };
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), &rgb[(size_t)y * w * 3], &rgb[(size_t)y * w * 3] + (size_t)w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t at = 0; at < raw.size(); at += 65535) {
        const size_t len = std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + len >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)(len & 255));
        z.push_back((uint8_t)(len >> 8));
        z.push_back((uint8_t)(~len & 255));
        z.push_back((uint8_t)((~len >> 8) & 255));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + len);
    }
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    const uint32_t ad = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back((uint8_t)(ad >> s));
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        for (int s = 24; s >= 0; s -= 8) png.push_back((uint8_t)(data.size() >> s));
        std::vector<uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        png.insert(png.end(), body.begin(), body.end());
        const uint32_t c = crc(body.data(), body.size(), 0);
        for (int s = 24; s >= 0; s -= 8) png.push_back((uint8_t)(c >> s));
    };
    std::vector<uint8_t> ihdr;
    for (int s = 24; s >= 0; s -= 8) ihdr.push_back((uint8_t)(w >> s));
    for (int s = 24; s >= 0; s -= 8) ihdr.push_back((uint8_t)(h >> s));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") || !f) return false;
    const bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
    fclose(f);
    return ok;
}

int Opaque(const std::vector<uint32_t>& px, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (px[(size_t)y * kPicW + x] >> 24) ++n;
    return n;
}

void TestPaint(const std::string& dir) {
    printf("the picture\n");
    char ini[5];
    wriststatuspaint::Initials("Amplify Damage", ini);
    EXPECT(std::string(ini) == "AD");
    wriststatuspaint::Initials("Holy Shield", ini);
    EXPECT(std::string(ini) == "HS");
    wriststatuspaint::Initials("Chilled", ini);
    EXPECT(std::string(ini) == "CHI");
    wriststatuspaint::Initials("Heart of Wolverine", ini);
    EXPECT(std::string(ini) == "HOW");
    wriststatuspaint::Initials("Mana regen blocked", ini);
    EXPECT(std::string(ini) == "MRB");
    wriststatuspaint::Initials("", ini);
    EXPECT(ini[0] == 0);
    wriststatuspaint::Initials(nullptr, ini);
    EXPECT(ini[0] == 0);

    Config cfg;
    std::vector<uint32_t> px;
    // nothing: a clear picture
    wriststatuspaint::Render(MakeLook(Roster{}, Status{}, 3, cfg, 0), px);
    EXPECT(px.size() == (size_t)kPicW * kPicH && Opaque(px, 0, 0, kPicW, kPicH) == 0);
    // companions: a chip for each, drawn in its own slot and nowhere else
    const Roster ro = MakeRoster({{7, 1}, {4, 5}, {5, 2}, {3, 1}}, {10});
    const Look comp = MakeLook(ro, Status{}, kContentCompanions, cfg, 0);
    wriststatuspaint::Render(comp, px);
    int inside = 0, outside = 0;
    for (int i = 0; i < kMaxCells; ++i) {
        const Slot s = SlotOf(i, comp.n);
        const int n = Opaque(px, s.x, s.y, s.x + 64, s.y + 64);
        if (i < (int)comp.n) { EXPECT(n > 1200); inside += n; }
        else outside += n;
    }
    EXPECT(inside > 0 && Opaque(px, 0, 0, kPicW, kPicH) == inside);   // everything drawn is in the chips' slots
    // a dead companion looks different from a live one (dim, a red count)
    Roster dead = ro;
    dead.alive[4] = 0;
    std::vector<uint32_t> pxDead;
    wriststatuspaint::Render(MakeLook(dead, Status{}, kContentCompanions, cfg, 0), pxDead);
    EXPECT(pxDead != px);
    // effects: the rim's colour says what kind (red harm, green helps)
    const Look eff = MakeLook(Roster{}, MakeStatus({33, 9}), kContentEffects, cfg, 0);   // Amplify Damage (harm) first, then Might
    std::vector<uint32_t> pxE;
    wriststatuspaint::Render(eff, pxE);
    auto avgRim = [&](const std::vector<uint32_t>& p, int i, int n) {   // the average colour along the chip's top edge (3 px under its top)
        const Slot s = SlotOf(i, n);
        double r = 0, g = 0, b = 0, c = 0;
        for (int x = s.x + 20; x < s.x + 44; ++x)
            for (int y = s.y + 3; y < s.y + 6; ++y) {
                const uint32_t v = p[(size_t)y * kPicW + x];
                if (!(v >> 24)) continue;
                r += v & 255u; g += (v >> 8) & 255u; b += (v >> 16) & 255u; ++c;
            }
        return std::array<double, 3>{c ? r / c : 0, c ? g / c : 0, c ? b / c : 0};
    };
    const auto harm = avgRim(pxE, 0, eff.n), help = avgRim(pxE, 1, eff.n);
    EXPECT(harm[0] > harm[1] * 1.5 && harm[0] > harm[2] * 1.5);   // red
    EXPECT(help[1] > help[0] * 1.15 && help[1] > help[2] * 1.15);   // green
    // opacity scales the whole picture, premultiplied
    Config dim = cfg;
    dim.opacity = 0.5f;
    std::vector<uint32_t> pxD;
    wriststatuspaint::Render(MakeLook(ro, Status{}, kContentCompanions, dim, 0), pxD);
    long long a1 = 0, a2 = 0;
    for (size_t i = 0; i < px.size(); ++i) { a1 += px[i] >> 24; a2 += pxD[i] >> 24; }
    EXPECT(a2 > a1 * 0.40 && a2 < a1 * 0.60);
    // premultiplied: no channel above the alpha
    bool premult = true;
    for (uint32_t v : pxD) premult = premult && (v & 255u) <= (v >> 24) && ((v >> 8) & 255u) <= (v >> 24) && ((v >> 16) & 255u) <= (v >> 24);
    EXPECT(premult);
    // a full grid and the "+N" chip
    Status crowd;
    crowd.known = true;
    for (const Effect& e : kEffects) crowd.Set(e.state);
    const Look many = MakeLook(ro, crowd, 3, cfg, 0);
    std::vector<uint32_t> pxM;
    wriststatuspaint::Render(many, pxM);
    EXPECT(Opaque(pxM, 0, 0, kPicW, kPicH) > 18 * 1200);
    if (dir.empty()) return;
    // the pictures: a sheet of looks on a dark, bright and neutral plate
    const int cols = 2;
    const std::vector<Look> looks = {
        MakeLook(ro, MakeStatus({}), kContentCompanions, cfg, 0),
        MakeLook(Roster{}, MakeStatus({33, 9, 100, 11, 128, 95, 3, 101}), kContentEffects, cfg, 0),
        MakeLook(MakeRoster({{7, 0}, {4, 12}, {19, 3}}), MakeStatus({1, 2}), 3, cfg, 0),
        many,
    };
    const int gap = 8, W = cols * kPicW + (cols + 1) * gap, H = (int)((looks.size() + 1) / 2) * kPicH + ((int)((looks.size() + 1) / 2) + 1) * gap;
    std::vector<uint8_t> rgb((size_t)W * H * 3, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const int v = 40 + ((x / 24 + y / 24) & 1) * 18 + (x * 60 / W);
            rgb[((size_t)y * W + x) * 3 + 0] = (uint8_t)(v + 18);
            rgb[((size_t)y * W + x) * 3 + 1] = (uint8_t)(v + 8);
            rgb[((size_t)y * W + x) * 3 + 2] = (uint8_t)v;
        }
    for (size_t i = 0; i < looks.size(); ++i) {
        std::vector<uint32_t> p;
        wriststatuspaint::Render(looks[i], p);
        const int x0 = gap + (int)(i % cols) * (kPicW + gap), y0 = gap + (int)(i / cols) * (kPicH + gap);
        for (int y = 0; y < kPicH; ++y)
            for (int x = 0; x < kPicW; ++x) {
                const uint32_t v = p[(size_t)y * kPicW + x];
                const float a = (float)(v >> 24) / 255.0f;
                uint8_t* d = &rgb[((size_t)(y0 + y) * W + x0 + x) * 3];
                d[0] = (uint8_t)std::min(255.0f, (float)(v & 255u) + d[0] * (1.0f - a));
                d[1] = (uint8_t)std::min(255.0f, (float)((v >> 8) & 255u) + d[1] * (1.0f - a));
                d[2] = (uint8_t)std::min(255.0f, (float)((v >> 16) & 255u) + d[2] * (1.0f - a));
            }
    }
    EXPECT(WritePng(dir + "\\wrist_status_panels.png", W, H, rgb));
    printf("  wrote wrist_status_panels.png\n");
}

// ---- the game side: only reads ----------------------------------------------------------------------------------------------------------------------

void TestGameSource() {
#ifdef WRISTSTATUS_SRC_DIR
    printf("the game side's source\n");
    FILE* f = nullptr;
    std::string text;
    if (!fopen_s(&f, (std::string(WRISTSTATUS_SRC_DIR) + "\\wrist_status_game.cpp").c_str(), "rb") && f) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        fclose(f);
    }
    EXPECT(text.size() > 4000);
    for (const char* bad : {"WriteProcessMemory", "VirtualProtect", "InstallInlineHook", "d2rsig::Hook", "d2rsig::Patch", "MH_CreateHook", "WriteMemory", "runOnGameThread(g_ctx, &Write", "SendInput"})
        EXPECT(text.find(bad) == std::string::npos);
    EXPECT(text.find("bearstonem/d2r-pcvr") != std::string::npos);   // the credit
    EXPECT(text.find("__try") != std::string::npos && text.find("d2rsig::Check") != std::string::npos);   // guarded reads, byte guards
    // the one call into the game other than its unit lookup and the getter of the client's hero is the state check
    size_t calls = 0;
    for (size_t at = 0; (at = text.find("reinterpret_cast<", at)) != std::string::npos; ++at) {
        const size_t end = text.find('>', at);
        if (end == std::string::npos) break;
        const std::string what = text.substr(at, end - at);
        if (what.find("(*)") != std::string::npos || what.find("Fn") != std::string::npos) ++calls;
    }
    EXPECT(calls <= 6);   // LocalIndex, ClientPlayer, the lookup, the state check (and the casts of the reads, which are not calls)
    std::string header;
    {
        FILE* g = nullptr;
        if (!fopen_s(&g, (std::string(WRISTSTATUS_SRC_DIR) + "\\wrist_status.h").c_str(), "rb") && g) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, g)) > 0) header.append(buf, n);
            fclose(g);
        }
    }
    EXPECT(header.find("bearstonem/d2r-pcvr (MIT") != std::string::npos);
#endif
}

}  // namespace

int main(int argc, char** argv) {
    TestTables();
    TestRoster();
    TestStatus();
    TestLook();
    TestSlots();
    TestConfig();
    TestGate();
    TestPaint(argc > 1 ? argv[1] : "");
    TestGameSource();
    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
