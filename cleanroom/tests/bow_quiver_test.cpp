// [bow] quiver (vr/bow_quiver.h) on synthetic numbers, without the game and without a headset: the bow skills and the cycle through them, the controls' names, the machine that
// turns a button press into one request of the game's own skill setter and the game's answer into feedback, and the glow the arrow gets.
//
//   bow_quiver_test

#include "../../vr/bow_quiver.h"

#include <cmath>
#include <cstdio>

using namespace bowquiver;

namespace {

int g_fails = 0;
#define EXPECT(c, ...)                                         \
    do {                                                       \
        if (!(c)) {                                            \
            ++g_fails;                                         \
            std::printf("  FAIL line %d: %s - ", __LINE__, #c); \
            std::printf(__VA_ARGS__);                          \
            std::printf("\n");                                 \
        }                                                      \
    } while (0)

// The game's controller table as the wheel's snapshot has it: a catalog of what the hero can bind and the slots.
struct Game {
    wheel::Snapshot snap;
    uint64_t now = 100000;
    Game() {
        snap.session = 7;
        snap.at = now;
        snap.ready = snap.editable = true;
        snap.bank = 0;
    }
    void Offer(int32_t id, int32_t owner = -1, bool available = true) {
        wheel::Skill& k = snap.catalog[snap.count++];
        k.id = id; k.owner = owner; k.known = true; k.available = available;
    }
    void Slot(int slot, int32_t id, int32_t owner = -1) {
        wheel::Skill& k = snap.slot[slot];
        k.id = id; k.owner = owner; k.known = true; k.available = true;
    }
};

// Drives the machine: one step per call, the clock moving.

// A Necromancer-less Amazon: Attack, Magic, Fire, Cold, Multiple Shot, Guided, Strafe and some skills that are no arrows.
void AmazonCatalog(Game& g) {
    g.Offer(wheel::kSkillInteract);
    g.Offer(0);           // Attack
    g.Offer(6);           // Magic Arrow
    g.Offer(7);           // Fire Arrow
    g.Offer(11);          // Cold Arrow
    g.Offer(12);          // Multiple Shot
    g.Offer(22);          // Guided Arrow
    g.Offer(26);          // Strafe
    g.Offer(36);          // Jab (not an arrow)
    g.Offer(16, -1, false);   // Exploding Arrow: listed, not available now
}

void TestSkills() {
    std::puts("the bow's skills");
    EXPECT(FindArrow(6) && std::strcmp(FindArrow(6)->name, "Magic Arrow") == 0, "Magic Arrow is 6");
    EXPECT(FindArrow(31) && FindArrow(27) && FindArrow(22) && FindArrow(26) && FindArrow(12), "the rest of the table");
    EXPECT(!FindArrow(0) && !FindArrow(36) && !FindArrow(-1) && !FindArrow(357), "the plain attack, a melee skill, nothing, interact are no arrows");
    int n = 0;
    for (const Arrow& a : kArrows) {
        ++n;
        EXPECT(a.rgb[0] >= 0.0f && a.rgb[0] <= 1.0f && a.rgb[1] >= 0.0f && a.rgb[1] <= 1.0f && a.rgb[2] >= 0.0f && a.rgb[2] <= 1.0f, "a colour in range: %s", a.name);
    }
    EXPECT(n == 10, "ten arrows: %d", n);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) EXPECT(kArrows[i].id != kArrows[j].id, "ids differ");
}

void TestList() {
    std::puts("the cycle's list");
    Game g;
    AmazonCatalog(g);
    Entry list[kMaxList];
    size_t n = BuildList(g.snap.catalog, g.snap.count, list, kMaxList);
    EXPECT(n == 7 && list[0].id == 0 && list[1].id == 6 && list[2].id == 7 && list[3].id == 11 && list[4].id == 12 && list[5].id == 22 && list[6].id == 26, "the plain attack first, then the arrows the hero has in the table's order, none unavailable (%zu)", n);
    // an item grants Fire Arrow too: both, as their own entries
    g.Offer(7, 41);
    n = BuildList(g.snap.catalog, g.snap.count, list, kMaxList);
    EXPECT(n == 8 && list[2].id == 7 && list[2].owner == -1 && list[3].id == 7 && list[3].owner == 41, "an item's Fire Arrow is its own entry (%zu)", n);
    // the same skill and owner twice in the catalog: once
    g.Offer(7, 41);
    n = BuildList(g.snap.catalog, g.snap.count, list, kMaxList);
    EXPECT(n == 8, "once (%zu)", n);
    // a catalog that does not offer the plain attack: the white orb all the same (0.175: it only disarms, nothing is bound), then the arrows
    Game noAttack;
    noAttack.Offer(6);
    noAttack.Offer(7);
    n = BuildList(noAttack.snap.catalog, noAttack.snap.count, list, kMaxList);
    EXPECT(n == 3 && list[0].id == 0 && list[1].id == 6, "no plain attack offered: the white orb first all the same (%zu)", n);
    // no bow skill: empty; the smallest room: no overrun
    wheel::Skill none[2];
    none[0].id = 36; none[0].available = true;
    EXPECT(BuildList(none, 1, list, kMaxList) == 1 && list[0].id == 0 && BuildList(nullptr, 0, list, kMaxList) == 1, "no bow skill: the plain shot alone");
    // the orbs only with a choice (the plain shot and at least one bow skill): no lone white orb
    EXPECT(!OrbsWanted(BuildList(none, 1, list, kMaxList)) && !OrbsWanted(0) && OrbsWanted(BuildList(noAttack.snap.catalog, noAttack.snap.count, list, kMaxList)) && OrbsWanted(2),
           "no bow skill: no orbs; one skill or more: the orbs");
    // [bow] draw_shoots_arrow: a release's read of the A slot. A player's Warlock (0.178) had a spell on A: the string drew, no arrow flew.
    EXPECT(DrawSlotAnswer(0, 0) == 0, "Attack on A: the plain arrow");
    EXPECT(DrawSlotAnswer(400, 0) == 0 && DrawSlotAnswer(9, 0) == 0, "a spell or a melee skill on A: read as the plain arrow");
    EXPECT(DrawSlotAnswer(12, 0) == 12 && DrawSlotAnswer(26, 0) == 26, "a bow skill on A stays (Multiple Shot, Strafe)");
    EXPECT(DrawSlotAnswer(400, 7) == 7 && DrawSlotAnswer(12, 7) == 7 && DrawSlotAnswer(0, 31) == 31, "an armed quiver skill comes first");
    Entry tiny[2];
    EXPECT(BuildList(g.snap.catalog, g.snap.count, tiny, 2) == 2 && tiny[0].id == 0, "a short list is cut, not overrun");
    EXPECT(Offers(g.snap.catalog, g.snap.count, {6, -1}) && !Offers(g.snap.catalog, g.snap.count, {16, -1}) && !Offers(g.snap.catalog, g.snap.count, {6, 3}), "Offers: available, owner exact");
}

void TestNext() {
    std::puts("stepping");
    const Entry l[3] = {{6, -1}, {7, -1}, {11, -1}};
    Entry o;
    EXPECT(Next(l, 3, {6, -1}, +1, &o) && o.id == 7, "forward");
    EXPECT(Next(l, 3, {11, -1}, +1, &o) && o.id == 6, "forward wraps");
    EXPECT(Next(l, 3, {6, -1}, -1, &o) && o.id == 11, "back wraps");
    EXPECT(Next(l, 3, {7, -1}, -1, &o) && o.id == 6, "back");
    EXPECT(Next(l, 3, {0, -1}, +1, &o) && o.id == 6, "a skill that is not in the list: forward goes to the first");
    EXPECT(Next(l, 3, {0, -1}, -1, &o) && o.id == 11, "... back to the last");
    EXPECT(Next(l, 3, {7, 5}, +1, &o) && o.id == 6, "the owner counts");
    EXPECT(Next(l, 1, {6, -1}, +1, &o) && o.id == 6, "one entry, the current: itself");
    EXPECT(Next(l, 1, {0, -1}, +1, &o) && o.id == 6, "one entry, another current: that one");
    EXPECT(!Next(l, 0, {6, -1}, +1, &o) && !Next(l, 3, {6, -1}, +1, nullptr), "empty list, no output");
}

// (0.175) TestControls checked SlotOfButton - which game slot a draw button loads; the quiver writes no slot now.

// (0.175) The quiver's machine no longer asks the game to put a skill on a slot: it only arms one. Its tests - arm, disarm,
// the shot, the plain arrow, the weapon swap, the old arrow on A given back - are cleanroom/tests/bow_quiver_armed_test.cpp.


// The white orb with the slot holding an arrow from the very first look (the game keeps the chosen arrow between runs): the plain attack is in the list and can be picked.


void TestGlow() {
    std::puts("the glow");
    EXPECT(std::fabs(GlowRadius(1e9f) - 0.012f) < 1e-6f && std::fabs(GlowRadius(kPulseS) - 0.012f) < 1e-6f, "the steady size");
    EXPECT(GlowRadius(0.0f) > GlowRadius(0.2f) && GlowRadius(0.2f) > GlowRadius(0.4f) && GlowRadius(0.4f) > 0.012f, "it swells at the load and settles");
    EXPECT(std::fabs(GlowRadius(-1.0f) - 0.012f) < 1e-6f && std::fabs(GlowRadius(NAN) - 0.012f) < 1e-6f, "a bad time is the steady size");

    // the arrow: from the string hand through the rest point, the head a little ahead of it
    Glow g;
    const Arrow& fire = *FindArrow(7);
    const bowtouch::Vec rest{-0.2f, -0.4f, -0.4f}, str{0.2f, -0.4f, 0.0f};
    EXPECT(MakeGlow(rest, str, fire, 0.1f, &g), "made");
    const bowtouch::Vec d = rest - str;
    const float len = bowdraw::Length(d);
    EXPECT(bowdraw::Length(g.head - (rest + d * (0.05f / len))) < 1e-4f, "the head is 5 cm past the rest point along the line");
    EXPECT(std::fabs(bowdraw::Length(g.head - g.tail) - 0.30f) < 1e-4f, "a trail of 30 cm");
    EXPECT(bowdraw::Length((g.head - g.tail) * (1.0f / bowdraw::Length(g.head - g.tail)) - d * (1.0f / len)) < 1e-4f, "along the line");
    EXPECT(g.rgb[0] == fire.rgb[0] && g.rgb[1] == fire.rgb[1] && g.rgb[2] == fire.rgb[2] && g.sinceS == 0.1f, "the skill's colour, the time");
    // a short pull: the trail does not go past the string hand
    EXPECT(MakeGlow({0.0f, 0.0f, -0.1f}, {0.0f, 0.0f, 0.0f}, fire, 0.0f, &g) && bowdraw::Length(g.head - g.tail) <= 0.15f + 1e-4f, "the trail stops at the hand");
    // nonsense: no glow
    EXPECT(!MakeGlow(rest, rest, fire, 0.0f, &g) && !MakeGlow({NAN, 0, 0}, str, fire, 0.0f, &g) && !MakeGlow(rest, str, fire, 0.0f, nullptr), "the same point, NaN, no output");

    // into the effect's picture: a segment and two balls, the ring beside the full ball
    bowtouch::VectorSet v;
    MakeGlow(rest, str, fire, 1e9f, &g);
    EXPECT(AddGlow(v, g) && v.nseg == 1 && v.nball == 2, "added");
    EXPECT(bowdraw::Length(v.seg[0].a - g.tail) < 1e-6f && bowdraw::Length(v.seg[0].b - g.head) < 1e-6f && v.seg[0].rgb[0] == fire.rgb[0], "the line");
    EXPECT(v.ball[0].fill == 1.0f && v.ball[1].fill == 0.0f && v.ball[1].r > v.ball[0].r && bowdraw::Length(v.ball[0].c - g.head) < 1e-6f, "a full ball and a ring round it");
    // the fullest picture bow_vectors draws leaves no room: refused, as it was
    bowtouch::Scene s;
    s.bow = {0, 0, 0}; s.str = {0, 0, 0.4f}; s.forward = {0, 0, -1}; s.nock = {0, 0, 0.16f}; s.nockConst = s.nock; s.haveModel = true; s.nockModel = s.nock;
    s.haveModelAxis = true; s.modelAxis = {0, 0, -1}; s.drawing = true; s.aimOwned = true; s.aim = {0, 0, -1}; s.pullNeed = 0.3f;
    bowtouch::VectorSet full = bowtouch::MakeVectors(s);
    const int ns = full.nseg, nb = full.nball;
    EXPECT(!AddGlow(full, g) && full.nseg == ns && full.nball == nb, "a full picture keeps what it has (%d segments, %d balls)", ns, nb);
}

// The element of the loaded skill and the arrow it burns on ([bow] arrow_fx): which skills have one, which colour of effect, the geometry of the arrow, the swell of a fresh load.
void TestElement() {
    std::puts("the element of the arrow");
    struct Row { int32_t id; Element kind; int code; float heavy; };
    const Row rows[] = {{7, Element::Fire, 1, 1.00f},  {16, Element::Fire, 1, 1.30f},  {27, Element::Fire, 1, 1.15f},
                        {11, Element::Frost, 2, 1.00f}, {21, Element::Frost, 2, 1.10f}, {31, Element::Frost, 2, 1.30f},
                        {6, Element::Arcane, 3, 1.00f}};
    for (const Row& r : rows) {
        const ElementFx e = ElementOf(r.id);
        EXPECT(e.kind == r.kind && EffectCode(e.kind) == r.code && std::fabs(e.heavy - r.heavy) < 1e-6f, "skill %d: element %d, effect %d, weight %.2f", r.id, (int)e.kind, EffectCode(e.kind), e.heavy);
        EXPECT(FindArrow(r.id) != nullptr, "skill %d is one of the quiver's", r.id);
    }
    // no element of their own: the plain shot, Guided Arrow, Multiple Shot, Strafe, and anything that is not a bow skill
    for (int32_t id : {0, 12, 22, 26, -1, 97, 5, 357}) EXPECT(ElementOf(id).kind == Element::None && EffectCode(ElementOf(id).kind) == 0, "skill %d has no element", id);
    // every skill of the table either has an element or is one of the four that keep the coloured glow
    for (const Arrow& a : kArrows) {
        const bool plain = a.id == 12 || a.id == 22 || a.id == 26;
        EXPECT(plain == (ElementOf(a.id).kind == Element::None), "%s: %s", a.name, plain ? "keeps the coloured glow" : "has an element");
    }
    // the swell: a fresh load flares, then it settles
    EXPECT(Swell(0.0f) > 1.8f && Swell(0.0f) < 2.0f, "a fresh load: %.2f", Swell(0.0f));
    EXPECT(Swell(0.1f) < Swell(0.0f) && Swell(0.1f) > Swell(0.3f) && Swell(0.3f) > 1.0f, "settling: %.2f %.2f %.2f", Swell(0.1f), Swell(0.3f), Swell(0.4f));
    EXPECT(Swell(kPulseS) == 1.0f && Swell(1e9f) == 1.0f && Swell(-1.0f) == 1.0f && Swell(NAN) == 1.0f, "settled, an old load, a bad time");
    // the arrow: the bow's own arrow as it is drawn (skel::BowArrow's nock and head) - taken as it is, nothing moved, lengthened or cut; the weight and the swell
    // in the strength
    const bowtouch::Vec nock{0.2f, -0.4f, 0.0f}, head{-0.3f, -0.35f, -0.55f};
    ArrowFx fx;
    EXPECT(MakeArrowFx(nock, head, 7, 1e9f, &fx) && fx.kind == Element::Fire, "made for Fire Arrow");
    EXPECT(bowdraw::Length(fx.tail - nock) == 0.0f && bowdraw::Length(fx.head - head) == 0.0f, "the arrow's nock and head, exactly as drawn");
    EXPECT(std::fabs(fx.strength - 1.0f) < 1e-6f, "a settled Fire Arrow: strength 1 (%.3f)", fx.strength);
    EXPECT(MakeArrowFx(nock, head, 16, 1e9f, &fx) && std::fabs(fx.strength - 1.30f) < 1e-6f, "Exploding Arrow weighs 1.3");
    EXPECT(MakeArrowFx(nock, head, 31, 0.0f, &fx) && std::fabs(fx.strength - 1.30f * Swell(0.0f)) < 1e-5f && fx.kind == Element::Frost, "Freezing Arrow just loaded: its weight times the swell");
    // a long arrow stays as long as it is (no hand-set limit)
    EXPECT(MakeArrowFx({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.2f}, 6, 1e9f, &fx) && std::fabs(bowdraw::Length(fx.head - fx.tail) - 1.2f) < 1e-5f && fx.kind == Element::Arcane, "a 1.2 m arrow stays 1.2 m");
    // nothing to draw: no element, the same point, NaN, no output
    EXPECT(!MakeArrowFx(nock, head, 22, 0.0f, &fx) && !MakeArrowFx(nock, head, 0, 0.0f, &fx), "Guided Arrow and the plain shot: no effect (the coloured glow)");
    EXPECT(!MakeArrowFx(nock, nock, 7, 0.0f, &fx) && !MakeArrowFx({NAN, 0, 0}, head, 7, 0.0f, &fx) && !MakeArrowFx(nock, {0, INFINITY, 0}, 7, 0.0f, &fx) &&
           !MakeArrowFx(nock, head, 7, 0.0f, nullptr), "the same point, NaN, infinity, no output");
    EXPECT(!MakeArrowFx({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -0.01f}, 7, 0.0f, &fx), "an arrow of 1 cm (a hidden one, shrunk): none");
    // [bow] arrow_fx_along_cm: the head moved along the shaft, the nock where it is
    {
        ArrowFx m;
        EXPECT(MakeArrowFx(nock, head, 7, 1e9f, &m), "an arrow to move");
        const float len = bowdraw::Length(head - nock);
        const bowtouch::Vec u = (head - nock) * (1.0f / len);
        ArrowFx f = m;
        MoveHead(&f, 0.30f);
        EXPECT(bowdraw::Length(f.head - (head + u * 0.30f)) < 1e-5f && bowdraw::Length(f.tail - nock) == 0.0f, "+30 cm: the head 30 cm further along the shaft, the nock stays");
        f = m;
        MoveHead(&f, -0.10f);
        EXPECT(bowdraw::Length(f.head - (head - u * 0.10f)) < 1e-5f, "-10 cm: 10 cm back toward the string");
        f = m;
        MoveHead(&f, -5.0f);
        EXPECT(std::fabs(bowdraw::Length(f.head - f.tail) - 0.02f) < 1e-5f && bowdraw::Dot(f.head - f.tail, u) > 0.0f, "far back: never nearer the nock than 2 cm, never behind it");
        f = m;
        MoveHead(&f, 0.0f);
        EXPECT(bowdraw::Length(f.head - head) == 0.0f, "0: as it was");
        f = m;
        MoveHead(&f, NAN);
        EXPECT(bowdraw::Length(f.head - head) == 0.0f, "NaN: as it was");
        ArrowFx none;
        none.tail = none.head = nock;
        MoveHead(&none, 0.3f);
        EXPECT(bowdraw::Length(none.head - nock) == 0.0f, "no shaft: as it was");
        MoveHead(nullptr, 0.3f);
    }
}

}  // namespace

int main() {
    TestSkills();
    TestList();
    TestNext();
    TestGlow();
    TestElement();
    std::printf(g_fails ? "\n%d FAILED\n" : "\nall passed\n", g_fails);
    return g_fails ? 1 : 0;
}
