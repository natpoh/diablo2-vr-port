// potion_hide_test - [hands] potion_hide_game, the pure part (vr/potion_hide.h): which unit is the hero's thrown potion, the set of hidden entities, the hidden
// matrix, the armed window. No game: the units are built by hand the way the client's missile units are laid out (vr/melee_touch.cpp MissileScan).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../vr/potion_hide.h"

namespace {

int g_fails = 0, g_checks = 0;
#define EXPECT(c, ...)                                           \
    do {                                                         \
        ++g_checks;                                              \
        if (!(c)) {                                              \
            ++g_fails;                                           \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c);  \
            std::printf(__VA_ARGS__);                            \
            std::printf("\n");                                   \
        }                                                        \
    } while (0)

using namespace potionhide;

// A client missile owned by a player unit (the flag word says an owner is recorded).
UnitHead PotionMissile(uint32_t cls = 46, uint32_t ownerType = 0, uint32_t ownerId = 7, uint32_t flags = kFlagHasOwner) {
    UnitHead u;
    u.type = kTypeMissile;
    u.cls = cls;
    u.id = 1234;
    u.flags = flags;
    u.ownerType = ownerType;
    u.ownerId = ownerId;
    return u;
}

void TestDecide() {
    std::printf("which unit is the hero's thrown potion\n");
    EXPECT(Decide(PotionMissile(), true, 7) == Verdict::Hide, "the hero's potion in the window");
    for (uint32_t c = 44; c <= 49; ++c) EXPECT(Decide(PotionMissile(c), true, 7) == Verdict::Hide, "class %u is one of the six", c);
    EXPECT(Decide(PotionMissile(43), true, 7) == Verdict::NotPotion, "class 43 is not a potion");
    EXPECT(Decide(PotionMissile(50), true, 7) == Verdict::NotPotion, "class 50 is not a potion");
    EXPECT(Decide(PotionMissile(0), true, 7) == Verdict::NotPotion, "an arrow (class 0)");
    EXPECT(Decide(PotionMissile(51), true, 7) == Verdict::NotPotion, "the potion's burst (class 51) is the explosion: drawn");
    EXPECT(Decide(PotionMissile(29), true, 7) == Verdict::NotPotion, "fireexplode: drawn");
    EXPECT(Decide(PotionMissile(222), true, 7) == Verdict::NotPotion, "a gas cloud: drawn");
    EXPECT(Decide(PotionMissile(), false, 7) == Verdict::NotArmed, "no throw armed: nothing is hidden");
    UnitHead mon = PotionMissile();
    mon.ownerType = 1;
    EXPECT(Decide(mon, true, 7) == Verdict::NotHero, "a monster's potion (a thrower) is not the hero's");
    EXPECT(Decide(PotionMissile(46, 0, 8), true, 7) == Verdict::NotHero, "another player's");
    EXPECT(Decide(PotionMissile(46, 0, 8), true, 0) == Verdict::Hide, "the hero's id not known: any player's counts (offline: the hero's alone)");
    EXPECT(Decide(PotionMissile(46, 0, 7, 0), true, 7) == Verdict::NotHero, "no owner recorded: not the hero's");
    UnitHead monster;
    monster.type = 1;
    monster.cls = 46;
    EXPECT(Decide(monster, true, 7) == Verdict::NotMissile, "a monster unit with a class 46 is not a missile");
    UnitHead player;
    player.type = 0;
    EXPECT(Decide(player, true, 7) == Verdict::NotMissile, "the hero himself");
    EXPECT(std::strlen(Why(Verdict::Hide)) > 0 && std::strlen(Why(Verdict::NotHero)) > 0 && std::strlen(Why(Verdict::NotArmed)) > 0, "every verdict says why");
}

void TestMatrix() {
    std::printf("the hidden matrix\n");
    // a row-vector world matrix: a model turned 30 degrees about y, size 1, standing at (100, 2.5, -40)
    const float c = std::cos(0.5236f), s = std::sin(0.5236f);
    const float m[16] = {c, 0, -s, 0,  0, 1, 0, 0,  s, 0, c, 0,  100.0f, 2.5f, -40.0f, 1};
    float h[16];
    EXPECT(HiddenMatrix(m, h), "a finite matrix is hidden");
    for (int r = 0; r < 3; ++r) {
        float len = 0.0f;
        for (int k = 0; k < 3; ++k) len += h[r * 4 + k] * h[r * 4 + k];
        EXPECT(std::sqrt(len) < 2.0f * kScale && std::sqrt(len) > 0.5f * kScale, "row %d is a speck (%g)", r, std::sqrt(len));
    }
    EXPECT(h[12] == 100.0f && h[14] == -40.0f, "it stays where it was on the ground (%g, %g)", h[12], h[14]);
    EXPECT(h[13] < -kSink * 0.99f, "and a long way under it (%g)", h[13]);
    EXPECT(h[3] == 0 && h[7] == 0 && h[11] == 0 && h[15] == 1, "the last column is untouched");
    EXPECT(std::isfinite(h[13]) && std::isfinite(h[0]), "all finite");
    float bad[16];
    std::memcpy(bad, m, sizeof bad);
    bad[5] = std::nanf("");
    EXPECT(!HiddenMatrix(bad, h), "a matrix with a NaN is left alone");
    std::memcpy(bad, m, sizeof bad);
    bad[12] = INFINITY;
    EXPECT(!HiddenMatrix(bad, h), "a matrix with an infinity is left alone");
    // hiding twice (a child that is itself in the set under a parent that is) stays finite
    float h2[16];
    EXPECT(HiddenMatrix(m, h) && HiddenMatrix(h, h2) && std::isfinite(h2[13]) && std::isfinite(h2[0]), "twice: still finite");
}

void TestSet() {
    std::printf("the entities in the set\n");
    EntitySet set;
    const uint64_t t0 = 100000;
    EXPECT(set.Live() == 0 && !set.Has(0x00300123, t0), "empty at first");
    EXPECT(set.Add(0x00300123, t0), "a new entity is added");
    EXPECT(set.Live() == 1 && set.Has(0x00300123, t0), "it is there");
    EXPECT(!set.Add(0x00300123, t0 + 10), "the same entity again is not new");
    EXPECT(set.Live() == 1, "and not counted twice");
    EXPECT(!set.Has(0x00400123, t0), "the entity with the same index and another generation is not it");
    EXPECT(set.Add(0x00300124, t0), "a second entity (the mesh under the root)");
    EXPECT(set.Live() == 2, "two live");
    EXPECT(!set.Add(0xFFFFFFFFu, t0) && !set.Add(0x003FFFFFu, t0), "no-entity ids are refused");
    EXPECT(set.Has(0x00300123, t0 + EntitySet::kTtlMs - 1), "still there before the time is up");
    EXPECT(!set.Has(0x00300123, t0 + 10 + EntitySet::kTtlMs + 1), "gone after it (the game has destroyed it long ago)");
    EXPECT(set.Remove(0x00300124, t0), "an entity the game reused for another unit is taken out");
    EXPECT(!set.Has(0x00300124, t0) && set.Live() == 1, "it is no longer hidden");
    EXPECT(!set.Remove(0x00300124, t0), "...and cannot be taken out twice");
    set.Prune(t0 + 20 * 1000);
    EXPECT(set.Live() == 0, "pruned: the gate is closed again (%d)", set.Live());
    // full: the capacity, and a place freed by an old one
    for (uint32_t i = 0; i < EntitySet::kCapacity; ++i) EXPECT(set.Add(0x00100000 + i, t0), "fill %u", i);
    EXPECT(!set.Add(0x00200000, t0), "no room for one more");
    EXPECT(set.Add(0x00200000, t0 + EntitySet::kTtlMs + 5), "but an expired one's place is taken");
    set.Clear();
    EXPECT(set.Live() == 0 && !set.Has(0x00100000, t0), "cleared");
}

void TestWindow() {
    std::printf("the armed window\n");
    Window w;
    EXPECT(!w.Armed(1000), "not armed at first");
    w.Arm(1000, 1.2);
    EXPECT(w.Armed(1000) && w.Armed(1000 + 3500 + 1200) && !w.Armed(1000 + 3500 + 1201), "it lasts the model's flight and the base margin (%llu)", (unsigned long long)w.Until());
    EXPECT(w.ArmedAt() == 1000, "it remembers when");
    w.Cancel();
    EXPECT(!w.Armed(1001), "cancelled");
    w.Arm(2000, std::nan(""));
    EXPECT(w.Armed(2000) && w.Until() == 2000 + Window::kBaseMs, "a flight that is not a number is the base margin (%llu)", (unsigned long long)w.Until());
    w.Arm(3000, 1e9);
    EXPECT(w.Until() == 3000 + Window::kBaseMs + 10000, "a flight is held to 10 s");
    w.Arm(4000, -5.0);
    EXPECT(w.Until() == 4000 + Window::kBaseMs, "a negative one to none");
}

// A fake memory for the storage lookup: addresses are offsets from kBase into one buffer.
struct FakeMem {
    static constexpr uintptr_t kBase = 0x7000000;
    std::vector<uint8_t> b = std::vector<uint8_t>(0x8000, 0);
    template <class T> void Put(uintptr_t at, T v) { std::memcpy(b.data() + (at - kBase), &v, sizeof v); }
    bool operator()(uintptr_t at, void* out, size_t n) const {
        if (at < kBase || at - kBase + n > b.size()) return false;
        std::memcpy(out, b.data() + (at - kBase), n);
        return true;
    }
};

void TestStorage() {
    std::printf("the effect component's place in the storage\n");
    FakeMem m;
    const uintptr_t st = FakeMem::kBase + 0x100, pages = FakeMem::kBase + 0x1000, page2 = FakeMem::kBase + 0x2000, packed = FakeMem::kBase + 0x3000, elems = FakeMem::kBase + 0x4000;
    const uint32_t entity = 0x01000B32;   // generation 0x01, index 0xB32: the page (e >> 8) & 0xFFF = 0xB, slot 0x32
    const uintptr_t pageNo = (entity >> 8) & 0xFFF;
    m.Put<uintptr_t>(st + kStOffPagesBegin, pages);
    m.Put<uintptr_t>(st + kStOffPagesEnd, pages + 16 * 8);               // 16 pages
    m.Put<uintptr_t>(st + kStOffPackedBegin, packed);
    m.Put<uintptr_t>(st + kStOffPackedEnd, packed + 8 * 4);              // 8 entities
    m.Put<uintptr_t>(st + kStOffElements, elems);
    m.Put<uintptr_t>(pages + pageNo * 8, page2);
    for (int i = 0; i < 256; ++i) m.Put<uint32_t>(page2 + i * 4, 0xFFFFFFFFu);   // none
    m.Put<uint32_t>(page2 + (entity & 0xFF) * 4, 5);                              // dense index 5
    m.Put<uint32_t>(packed + 5 * 4, entity);
    m.Put<uintptr_t>(elems + 5 * kEffectElementSize, 0xABC000);                   // the component object
    uintptr_t el = 0;
    EXPECT(StorageElement(m, st, entity, kEffectElementSize, &el) && el == elems + 5 * kEffectElementSize, "the element of entity %08X is element 5 (%llx)", entity, (unsigned long long)el);
    uintptr_t comp = 0;
    EXPECT(m(el, &comp, sizeof comp) && comp == 0xABC000, "whose first field is the component object");
    EXPECT(!StorageElement(m, st, 0x00A00B32, kEffectElementSize, &el), "the same index with another generation is not it (the packed entity differs)");
    EXPECT(!StorageElement(m, st, 0x01000B33, kEffectElementSize, &el), "a slot with no entry (0xFFFFF) has none");
    EXPECT(!StorageElement(m, st, 0x01000C32, kEffectElementSize, &el), "a page that does not exist has none");
    EXPECT(!StorageElement(m, st, 0x01001032, kEffectElementSize, &el), "a page number past the page table has none");
    m.Put<uint32_t>(page2 + 0x34 * 4, 9);   // a dense index past the packed entities
    EXPECT(!StorageElement(m, st, 0x01000B34, kEffectElementSize, &el), "an index past the packed array has none");
    m.Put<uint32_t>(page2 + 0x35 * 4, 0xFFF00000u | 5);   // high bits (a version) above the 20 index bits are ignored
    m.Put<uint32_t>(packed + 5 * 4, 0x01000B35);
    EXPECT(StorageElement(m, st, 0x01000B35, kEffectElementSize, &el) && el == elems + 5 * kEffectElementSize, "bits above the index are not part of it");
    FakeMem empty;
    EXPECT(!StorageElement(empty, st, entity, kEffectElementSize, &el), "memory that does not read has none");
    FakeMem noStorage;
    EXPECT(!StorageElement(noStorage, FakeMem::kBase + 0x100, entity, kEffectElementSize, &el), "a storage of zeros has none");
}

}  // namespace

int main() {
    TestDecide();
    TestMatrix();
    TestSet();
    TestWindow();
    TestStorage();
    std::printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
