// Offline checks of the skill wheel (vr/wheel_*.h): the wedges' hit test, the slot mapping, the bank logic against a fake
// game (reads and the game's own setter, with a read-back), the model's pages and requests, the trigger's cast, and the
// whole controller fed with synthetic headset frames - no game, no OpenXR, no headset.
// Run: build/Release/skill_wheel_test.exe  (exit code 0 = all passed)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../vr/wheel_bank.h"
#include "../../vr/wheel_cast.h"
#include "../../vr/wheel_controller.h"
#include "../../vr/wheel_geom.h"
#include "../../vr/wheel_model.h"
#include "../../vr/wheel_icons.h"
#include "../../vr/wheel_paint.h"
#include "../../vr/wheel_ray.h"
#include "../../vr/wheel_types.h"

using namespace wheel;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

// -- geometry --------------------------------------------------------------------------------------------------------

static void TestWedges() {
    // the middle of each wedge hits itself, the ring's hole and outside hit nothing
    for (int k = 0; k < kSlots; ++k) {
        float u, v;
        WedgePoint(k, 0.27f, &u, &v);
        CHECK(WedgeAt(u, v, -1) == k);
        CHECK(HitTest(Page::Wheel, u, v, -1, 0) == k);
        CHECK(HitTest(Page::PickSlot, u, v, -1, 0) == k);
    }
    CHECK(WedgeAt(kCx, kCy, -1) == -1);            // the hole
    CHECK(WedgeAt(kCx, kCy - 0.5f * (kCentreR + kRingIn), -1) == -1);    // between the centre button and the ring
    CHECK(WedgeAt(kCx, kCy - 0.5f, -1) == -1);     // outside
    CHECK(HitTest(Page::Wheel, kCx, kCy, -1, 0) == kCentre);
    CHECK(HitTest(Page::PickSlot, kCx, kCy, -1, 0) == kNone);   // no centre button while choosing a slot
    // wedge 0 is the upper right, clockwise: 1 right-upper... (angles from straight up)
    CHECK(WedgeAt(kCx + 0.05f, kCy - 0.27f, -1) == 0);
    CHECK(WedgeAt(kCx + 0.27f, kCy - 0.05f, -1) == 1);
    CHECK(WedgeAt(kCx + 0.27f, kCy + 0.05f, -1) == 2);
    CHECK(WedgeAt(kCx + 0.05f, kCy + 0.27f, -1) == 3);
    CHECK(WedgeAt(kCx - 0.05f, kCy + 0.27f, -1) == 4);
    CHECK(WedgeAt(kCx - 0.27f, kCy + 0.05f, -1) == 5);
    CHECK(WedgeAt(kCx - 0.27f, kCy - 0.05f, -1) == 6);
    CHECK(WedgeAt(kCx - 0.05f, kCy - 0.27f, -1) == 7);
    // the hold: just past the edge of wedge 0 into wedge 1 stays on 0 while it was the hover, goes to 1 well past it
    const float edge = kWedge;   // the boundary between 0 and 1, clockwise from up
    auto at = [&](float a) { return std::pair<float, float>{kCx + std::sin(a) * 0.27f, kCy - std::cos(a) * 0.27f}; };
    auto p = at(edge + 0.01f);
    CHECK(WedgeAt(p.first, p.second, -1) == 1);
    CHECK(WedgeAt(p.first, p.second, 0) == 0);
    p = at(edge + kWedgeHold + 0.02f);
    CHECK(WedgeAt(p.first, p.second, 0) == 1);
    // the seam at straight up: wedge 7 ends and wedge 0 starts there
    p = at(-0.01f);
    CHECK(WedgeAt(p.first, p.second, -1) == 7);
    p = at(0.01f);
    CHECK(WedgeAt(p.first, p.second, -1) == 0);
}

static void TestCells() {
    // every footer button is where FooterRect says, and not a wedge
    for (Page page : {Page::Wheel, Page::PickSlot, Page::PickSkill})
        for (int i = 0; i < FooterCount(page); ++i) {
            const Rect r = FooterRect(page, i);
            CHECK(HitTest(page, 0.5f * (r.l + r.r), 0.5f * (r.t + r.b), -1, 8) == FooterCell(page, i));
        }
    CHECK(FooterCell(Page::Wheel, 0) == kEdit && FooterCell(Page::Wheel, 1) == kClose);
    CHECK(FooterCell(Page::PickSkill, 0) == kPrev && FooterCell(Page::PickSkill, 1) == kNext && FooterCell(Page::PickSkill, 2) == kBack);
    // the editor's list: entry i, and only as many as there are
    for (int i = 0; i < kOptionsPerPage; ++i) {
        const Rect r = OptionRect(i);
        CHECK(HitTest(Page::PickSkill, 0.5f * (r.l + r.r), 0.5f * (r.t + r.b), -1, 8) == i);
        CHECK(HitTest(Page::PickSkill, 0.5f * (r.l + r.r), 0.5f * (r.t + r.b), -1, i) == kNone);
    }
    CHECK(HitTest(Page::PickSkill, 0.8f, 0.15f, -1, 8) == kCentre);   // CLEAR SLOT
    // the cells do not overlap: no point is in two rectangles
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x) {
            const float u = (x + 0.5f) / 100, v = (y + 0.5f) / 100;
            int n = 0;
            for (int i = 0; i < kOptionsPerPage; ++i) n += OptionRect(i).Has(u, v);
            n += kClearRect.Has(u, v);
            for (int i = 0; i < 3; ++i) n += FooterRect(Page::PickSkill, i).Has(u, v);
            CHECK(n <= 1);
        }
    CHECK(HitTest(Page::Wheel, -0.1f, 0.5f, -1, 0) == kNone);
    CHECK(HitTest(Page::Wheel, 0.5f, 1.0f, -1, 0) == kNone);
    CHECK(HitTest(Page::Wheel, NAN, 0.5f, -1, 0) == kNone);
}

static void TestCompose() {
    // the room's place in the layers' space: a turn and a shift
    Pose room;
    room.valid = true;
    room.q = YawQ(0.7f);
    room.p = V3{0.3f, -1.1f, 0.4f};
    Pose child;
    child.valid = true;
    child.q = QMul(YawQ(-0.4f), Q{0.2f, 0.0f, 0.0f, std::sqrt(1.0f - 0.04f)});
    child.p = V3{1.0f, 2.0f, -3.0f};
    const V3 v{0.5f, -0.25f, 0.75f};
    const Pose both = Compose(room, child);
    const V3 a = Apply(both, v), b = Apply(room, Apply(child, v));
    CHECK(both.valid && Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z));
    const V3 c = Rot(QMul(room.q, child.q), v), d = Rot(room.q, Rot(child.q, v));
    CHECK(Near(c.x, d.x) && Near(c.y, d.y) && Near(c.z, d.z));
    // an identity room changes nothing; an invalid one makes the result invalid
    Pose none;
    none.valid = true;
    const Pose same = Compose(none, child);
    CHECK(Near(same.p.x, child.p.x) && Near(same.q.w, child.q.w, 1e-4f));
    room.valid = false;
    CHECK(!Compose(room, child).valid);
    // a ray along the panel's normal still meets the panel's middle after both are carried into the other space
    Pose panel;
    panel.valid = true;
    panel.p = V3{0.0f, 1.5f, -1.0f};
    Pose r2;
    r2.valid = true;
    r2.q = YawQ(1.2f);
    r2.p = V3{2.0f, 0.5f, -1.0f};
    const Pose moved = Compose(r2, panel);
    const V3 from = Apply(r2, V3{0.0f, 1.5f, 0.0f}), dir = Rot(r2.q, V3{0.0f, 0.0f, -1.0f});
    const PanelHit h = IntersectPanel(from, dir, moved, 0.8f, 0.8f);
    CHECK(h.hit && Near(h.u, 0.5f) && Near(h.v, 0.5f) && Near(h.dist, 1.0f));
}

static void TestRay() {
    Pose panel;
    panel.valid = true;
    panel.p = V3{0.0f, 1.5f, -1.0f};   // 1 m ahead of the origin, facing it (+z toward the viewer)
    // the middle
    PanelHit h = IntersectPanel(V3{0, 1.5f, 0}, V3{0, 0, -1}, panel, 0.8f, 0.8f);
    CHECK(h.hit && Near(h.u, 0.5f) && Near(h.v, 0.5f) && Near(h.dist, 1.0f));
    // up and to the right: u grows, v shrinks
    h = IntersectPanel(V3{0, 1.5f, 0}, Unit(V3{0.2f, 0.2f, -1.0f}), panel, 0.8f, 0.8f);
    CHECK(h.hit && h.u > 0.5f && h.v < 0.5f);
    CHECK(Near(h.u, 0.5f + 0.2f / 0.8f) && Near(h.v, 0.5f - 0.2f / 0.8f));
    CHECK(!IntersectPanel(V3{0, 1.5f, 0}, Unit(V3{0.6f, 0, -1.0f}), panel, 0.8f, 0.8f).hit);   // off its edge
    CHECK(!IntersectPanel(V3{0, 1.5f, 0}, V3{0, 0, 1}, panel, 0.8f, 0.8f).hit);                 // away from it
    CHECK(!IntersectPanel(V3{0, 1.5f, -2.0f}, V3{0, 0, 1}, panel, 0.8f, 0.8f).hit);             // at its back
    CHECK(!IntersectPanel(V3{0, 1.5f, 0}, V3{1, 0, 0}, panel, 0.8f, 0.8f).hit);                 // parallel
    // turned a quarter to the left: the ray along -z no longer hits its face
    Pose turned = panel;
    turned.q = YawQ(kPi / 2.0f);
    CHECK(!IntersectPanel(V3{0, 1.5f, 0}, V3{0, 0, -1}, turned, 0.8f, 0.8f).hit);
}

// -- the game, faked ---------------------------------------------------------------------------------------------------

struct FakeGame : Game {
    bool controller = true;
    int bank = 0;
    bool aliased = false;           // two wheel slots on one game slot
    bool ignoreWrites = false;      // the setter does nothing
    bool failRead = false;
    Binding table[32];
    struct SkillRow { int32_t level; bool left; };
    std::map<std::pair<int32_t, int32_t>, SkillRow> skills;
    std::vector<Learned> learned;
    bool haveList = true;
    std::map<int32_t, std::string> names;
    int assigns = 0, clears = 0;
    int lastIndex = -1;
    bool lastLeft = false;

    bool ControllerLayout() override { return controller; }
    int Bank() override { return bank; }
    int ButtonSlot(uint16_t button, bool alt) override {
        int base = button == kPadB ? 2 : button == kPadX ? 3 : button == kPadY ? 4 : button == kPadRb ? 5 : -1;
        if (base < 0) return -1;
        if (aliased && button == kPadX && !alt) return 2;
        return alt ? base + 6 : base;
    }
    bool ReadBinding(int index, Binding* out) override {
        if (failRead || index < 0 || index >= 32) return false;
        *out = table[index];
        return true;
    }
    bool HasSkill(int32_t id, int32_t owner, int32_t* level) override {
        auto it = skills.find({id, owner});
        if (it == skills.end()) return false;
        *level = it->second.level;
        return true;
    }
    bool LeftFlag(int32_t id, int32_t owner, bool* left) override {
        auto it = skills.find({id, owner});
        if (it == skills.end()) return false;
        *left = it->second.left;
        return true;
    }
    bool LearnedSkills(std::vector<Learned>* out) override {
        if (!haveList) return false;
        *out = learned;
        return true;
    }
    bool Name(int32_t id, char* out, int n) override {
        auto it = names.find(id);
        if (it == names.end()) return false;
        std::snprintf(out, n, "%s", it->second.c_str());
        return true;
    }
    void DoAssign(int slotIndex, bool left, int32_t skill, int32_t owner) override {
        ++assigns;
        lastIndex = slotIndex;
        lastLeft = left;
        if (ignoreWrites) return;
        // like the game's setter: the same skill leaves any other slot of the bank
        for (int i = 0; i < 16; ++i) if (table[bank + i].skill == skill && i != slotIndex) table[bank + i] = Binding{};
        table[bank + slotIndex] = Binding{skill, owner};
    }
    void DoClear(int slotIndex) override {
        ++clears;
        lastIndex = slotIndex;
        if (ignoreWrites) return;
        table[bank + slotIndex] = Binding{};
    }
    FakeGame() {
        names = {{36, "Fire Bolt"}, {47, "Fire Ball"}, {54, "Teleport"}, {61, "Fire Mastery"}, {kSkillInteract, "Interact"},
                 {kSkillTownPortal, "Town Portal"}, {219, "Scroll"}, {220, "Tome"}, {500, "Warlock Thing"}};
        skills[{36, -1}] = {5, true};
        skills[{47, -1}] = {3, false};
        skills[{54, -1}] = {1, false};
        skills[{219, -1}] = {0, false};
        skills[{220, -1}] = {1, false};
        learned = {{54, -1, 1}, {36, -1, 5}, {47, -1, 3}, {219, -1, 0}, {220, -1, 1}, {217, -1, 1}, {47, -1, 3}};
        table[2] = {36, -1};   // B
        table[3] = {47, -1};   // X
    }
};

// A game that knows the skill trees (the cache made, the class read): Fire Ball and Fire Bolt on tree 1, Teleport on tree 2, an item's skill
// that the game's table puts on tree 3 (it must still be General / Items).
struct TreeGame : FakeGame {
    int cls = 1;
    bool known = true;
    int run = kWalking;
    int PlayerClass() override { return cls; }
    bool TreesKnown() override { return known; }
    int Tree(int32_t id, int32_t) override {
        if (!known) return kTreeUnknown;
        return id == 36 || id == 47 ? 1 : id == 54 ? 2 : id == 70 ? 3 : 0;
    }
    bool TreeName(int t, char* out, int n) override {
        static const char* const kTreeNames[3] = {"Fire Spells", "Lightning Spells", "Cold Spells"};
        if (!known || t < 1 || t > 3) return false;
        std::snprintf(out, n, "%s", kTreeNames[t - 1]);
        return true;
    }
    int RunState() override { return run; }
    TreeGame() {
        names[70] = "Grisly Thing";
        skills[{70, 5}] = {1, false};
        learned.push_back({70, 5, 2});
    }
};

static Snapshot Captured(FakeGame& g, uint64_t session, uint64_t at) {
    Snapshot s;
    Capture(g, &s);
    s.session = session;
    s.at = at;
    return s;
}

static void TestCapture() {
    FakeGame g;
    Snapshot s;
    CHECK(Capture(g, &s));
    CHECK(s.ready && s.editable && s.bank == 0);
    CHECK(s.slot[0].id == 36 && s.slot[0].available && std::strcmp(s.slot[0].name, "Fire Bolt") == 0);   // B
    CHECK(s.slot[1].id == 47 && s.slot[1].level == 3);                                                    // X
    CHECK(s.slot[2].id == -1 && s.slot[2].known && std::strcmp(s.slot[2].name, "Unassigned") == 0);       // Y
    // the catalog: Interact first, then by name, town portal once, no identify, no duplicate
    CHECK(s.count == 5);
    CHECK(s.catalog[0].id == kSkillInteract);
    CHECK(std::strcmp(s.catalog[1].name, "Fire Ball") == 0 && std::strcmp(s.catalog[2].name, "Fire Bolt") == 0);
    CHECK(std::strcmp(s.catalog[3].name, "Teleport") == 0 && s.catalog[4].id == kSkillTownPortal);
    CHECK(!Offered(s, 217, -1) && Offered(s, 47, -1) && Offered(s, kSkillTownPortal, -1) && !Offered(s, 999, -1));
    // the second bank is the other half of the table
    g.bank = 16;
    g.table[16 + 2] = {54, -1};
    CHECK(Capture(g, &s) && s.bank == 16 && s.slot[0].id == 54);
    g.bank = 5;   // a bank this does not know
    CHECK(!Capture(g, &s) && !s.ready);
    g.bank = 0;
    // a layout where two wheel slots share a game slot: nothing is read, nothing can be edited or cast
    g.aliased = true;
    CHECK(!Capture(g, &s) && !s.ready);
    g.aliased = false;
    g.failRead = true;
    CHECK(!Capture(g, &s));
    g.failRead = false;
    // the mouse and keyboard layout: readable, not editable
    g.controller = false;
    CHECK(Capture(g, &s) && s.ready && !s.editable);
    g.controller = true;
    // no list of skills: readable, not editable
    g.haveList = false;
    CHECK(Capture(g, &s) && s.ready && !s.editable && s.count == 0);
    g.haveList = true;
    // a skill with no name in the game's text falls back to "Skill <id>"
    g.table[2] = {77, -1};
    CHECK(Capture(g, &s) && std::strcmp(s.slot[0].name, "Skill 77") == 0 && !s.slot[0].available);
    // the town portal in a slot is the scroll while there is one, else the tome
    g.table[2] = {kSkillTownPortal, -1};
    CHECK(Capture(g, &s) && s.slot[0].available);    // the scroll is at level 0, the tome is there: available by the tome
    g.skills.erase({220, -1});
    CHECK(Capture(g, &s) && !s.slot[0].available);   // neither
    g.skills[{219, -1}] = {2, false};
    CHECK(Capture(g, &s) && s.slot[0].available);    // a scroll
}

static Request Assign(const Snapshot& s, int slot, int32_t skill, int32_t owner) {
    Request r;
    r.op = Op::Assign;
    r.sequence = 1;
    r.session = s.session;
    r.slot = slot;
    r.skill = skill;
    r.owner = owner;
    r.bank = s.bank;
    r.prevSkill = s.slot[slot].id;
    r.prevOwner = s.slot[slot].owner;
    return r;
}

static void TestApply() {
    FakeGame g;
    const Snapshot s = Captured(g, 7, 1000);
    const char* why = nullptr;
    // an assignment goes through the game's setter, once, with the left flag from the skill's row, and is read back
    Request r = Assign(s, 2, 54, -1);   // Teleport onto Y
    CHECK(Apply(g, r, s, 1100, &why) == Result::Applied);
    CHECK(g.assigns == 1 && g.lastIndex == 4 && !g.lastLeft);
    CHECK(g.table[4].skill == 54);
    // Fire Bolt has the left flag
    g = FakeGame();
    Snapshot s2 = Captured(g, 7, 1000);
    CHECK(Apply(g, Assign(s2, 3, 36, -1), s2, 1100, &why) == Result::Applied && g.lastLeft && g.lastIndex == 5);
    CHECK(g.table[2].skill == -1);   // (the setter took Fire Bolt off B, as the game's does)
    // the interact command is always a left-hand skill
    g = FakeGame();
    s2 = Captured(g, 7, 1000);
    CHECK(Apply(g, Assign(s2, 2, kSkillInteract, -1), s2, 1100, &why) == Result::Applied && g.lastLeft);
    // the town portal takes the flag of the tome when the scroll is empty
    g = FakeGame();
    s2 = Captured(g, 7, 1000);
    CHECK(Apply(g, Assign(s2, 2, kSkillTownPortal, -1), s2, 1100, &why) == Result::Applied && g.table[4].skill == kSkillTownPortal);
    // a clear
    g = FakeGame();
    s2 = Captured(g, 7, 1000);
    Request c;
    c.op = Op::Clear; c.sequence = 2; c.session = 7; c.slot = 0; c.bank = 0; c.prevSkill = 36; c.prevOwner = -1;
    CHECK(Apply(g, c, s2, 1100, &why) == Result::Applied && g.clears == 1 && g.table[2].skill == -1);
    // a clear counts when the skill is gone, whatever the game leaves in the owner field
    g = FakeGame();
    g.table[2] = {36, 7};
    s2 = Captured(g, 7, 1000);
    Request c2 = c;
    c2.prevOwner = 7;
    struct KeepOwner : FakeGame { void DoClear(int i) override { ++clears; table[bank + i].skill = -1; } };
    KeepOwner k;
    k.table[2] = {36, 7};
    Snapshot sk = Captured(k, 7, 1000);
    CHECK(Apply(k, c2, sk, 1100, &why) == Result::Applied && k.table[2].skill == -1 && k.table[2].owner == 7);
    g = FakeGame();
    s2 = Captured(g, 7, 1000);

    // refusals: nothing is written
    auto refused = [&](FakeGame& game, const Request& req, const Snapshot& snap, uint64_t now) {
        const int a = game.assigns, cl = game.clears;
        const Result res = Apply(game, req, snap, now, &why);
        return res == Result::Refused && game.assigns == a && game.clears == cl;
    };
    g = FakeGame();
    s2 = Captured(g, 7, 1000);
    CHECK(refused(g, Assign(s2, 2, 54, -1), s2, 1000 + 901));                     // the snapshot is too old
    Request wrongSession = Assign(s2, 2, 54, -1);
    wrongSession.session = 8;
    CHECK(refused(g, wrongSession, s2, 1100));                                   // another session
    g.bank = 16;
    CHECK(refused(g, Assign(s2, 2, 54, -1), s2, 1100));                           // the weapon set changed since
    g.bank = 0;
    g.table[4] = {47, -1};
    CHECK(refused(g, Assign(s2, 2, 54, -1), s2, 1100));                           // the slot changed since the player chose
    g = FakeGame();
    CHECK(refused(g, Assign(s2, 2, 999, -1), s2, 1100));                          // not offered (the player has none)
    g.skills[{61, -1}] = {1, false};
    CHECK(refused(g, Assign(s2, 2, 61, -1), s2, 1100));                           // the player has it but the list does not offer it (a passive)
    CHECK(refused(g, Assign(s2, 8, 54, -1), s2, 1100));                           // no such slot
    CHECK(refused(g, Assign(s2, -1, 54, -1), s2, 1100));
    Request none = Assign(s2, 2, 54, -1);
    none.op = Op::None;
    CHECK(refused(g, none, s2, 1100));
    Request badClear = c;
    badClear.skill = 36;
    CHECK(refused(g, badClear, s2, 1100));                                       // a clear names no skill
    g.controller = false;
    CHECK(refused(g, Assign(s2, 2, 54, -1), s2, 1100));                           // not the controller layout: the setter would write the other table
    g.controller = true;
    Snapshot notEditable = s2;
    notEditable.editable = false;
    CHECK(refused(g, Assign(s2, 2, 54, -1), notEditable, 1100));
    g.aliased = true;
    CHECK(refused(g, Assign(s2, 2, 54, -1), s2, 1100));                           // slots this does not know
    g.aliased = false;
    // the game takes the call and does nothing: not "applied"
    g.ignoreWrites = true;
    const int before = g.assigns;
    CHECK(Apply(g, Assign(s2, 2, 54, -1), s2, 1100, &why) == Result::Refused && g.assigns == before + 1);
}

// -- the model ---------------------------------------------------------------------------------------------------------

struct Rig {
    Model m;
    Snapshot snap;
    double t = 10.0;
    uint64_t now = 5000;
    uint64_t session = 7;
    uint32_t epoch = 1;
    Reply reply;
    bool categories = false, runWalk = false;   // [openxr] skill_categories / skill_run_walk
    bool editFirst = false, held = false;       // [openxr] skill_panel_edit_first, and the trigger still down
    Rig() {
        FakeGame g;
        snap = Captured(g, session, now);
    }
    Out Step(bool toggle = false, bool click = false, float u = -1, float v = -1, bool back = false, bool allowed = true) {
        In in;
        in.time = t += 0.01;
        in.nowMs = now += 10;
        snap.at = now;   // a live snapshot, read just now
        in.allowed = allowed;
        in.session = session;
        in.epoch = epoch;
        in.toggle = toggle;
        in.back = back;
        in.pointer = u >= 0;
        in.u = u;
        in.v = v;
        in.click = click;
        in.snap = &snap;
        in.reply = reply;
        in.categories = categories;
        in.runWalk = runWalk;
        in.editFirst = editFirst;
        in.held = held || click;
        return m.Step(in);
    }
    void Aim(int k) { WedgePoint(k, 0.27f, &lu, &lv); }
    void AimCell(Page page, int cell) {
        if (cell == kEdit || cell == kClose || cell == kBack || cell == kPrev || cell == kNext || cell == kRunWalk) {
            for (int i = 0; i < FooterCount(page, runWalk); ++i)
                if (FooterCell(page, i, runWalk) == cell) { const Rect r = FooterRect(page, i, runWalk); lu = 0.5f * (r.l + r.r); lv = 0.5f * (r.t + r.b); }
        } else if (cell >= kCategory0 && cell < kCategory0 + kCategories) { const Rect r = CategoryRect(cell - kCategory0); lu = 0.5f * (r.l + r.r); lv = 0.5f * (r.t + r.b); }
        else if (page == Page::PickSkill && cell == kCentre) { lu = 0.8f; lv = 0.15f; }
        else if (page == Page::PickSkill) { const Rect r = OptionRect(cell); lu = 0.5f * (r.l + r.r); lv = 0.5f * (r.t + r.b); }
        else if (cell == kCentre) { lu = 0.5f; lv = 0.45f; }
        else Aim(cell);
    }
    Out Point(Page page, int cell, bool click) {
        AimCell(page, cell);
        return Step(false, click, lu, lv);
    }
    float lu = 0, lv = 0;
};

static void TestModelFlow() {
    Rig r;
    // closed: nothing happens without the chord
    r.Step();
    CHECK(!r.m.Open());
    // the chord opens it (only in a game with a fresh state)
    Out o = r.Step(true);
    CHECK(r.m.Open() && o.feedback == Feedback::Opened);
    CHECK(r.m.Current().page == Page::Wheel);
    // the wedge under the pointer is the hover; moving to another gives a tick
    o = r.Point(Page::Wheel, 1, false);
    CHECK(r.m.Current().hover == 1 && o.feedback == Feedback::Hover);
    o = r.Point(Page::Wheel, 1, false);
    CHECK(o.feedback == Feedback::None);
    // a click on a slot with a skill picks it and closes the wheel
    o = r.Point(Page::Wheel, 1, true);
    CHECK(!r.m.Open() && r.m.Selected() == 1 && o.feedback == Feedback::Selected);
    // reopen: an empty slot (Y) is refused, the wheel stays up
    r.Step(true);
    o = r.Point(Page::Wheel, 2, true);
    CHECK(r.m.Open() && r.m.Selected() == 1 && o.feedback == Feedback::Refused);
    // the centre is the basic attack
    o = r.Point(Page::Wheel, kCentre, true);
    CHECK(!r.m.Open() && r.m.Selected() == -1);
    // CLOSE and the close keys and the chord again
    r.Step(true);
    o = r.Point(Page::Wheel, kClose, true);
    CHECK(!r.m.Open() && o.feedback == Feedback::Closed);
    r.Step(true);
    r.Step(false, false, -1, -1, true);
    CHECK(!r.m.Open());
    r.Step(true);
    r.Step(true);
    CHECK(!r.m.Open());
    // a menu opening (not allowed), another input context, and another session close it
    r.Step(true);
    r.Step(false, false, -1, -1, false, false);
    CHECK(!r.m.Open());
    r.Step(true);
    ++r.epoch;
    r.Step();
    CHECK(!r.m.Open());
    r.Step(true);
    CHECK(r.m.Open());
    // no chord while not allowed
    Rig q;
    q.Step(true, false, -1, -1, false, false);
    CHECK(!q.m.Open());
    // no fresh game state: it does not open (and says so)
    Rig stale;
    stale.snap.session = 99;
    o = stale.Step(true);
    CHECK(!stale.m.Open() && o.feedback == Feedback::Refused);
    // a selection survives closing and reopening, and ends when the game has the slot empty
    Rig s;
    s.Step(true);
    s.Point(Page::Wheel, 0, true);
    CHECK(s.m.Selected() == 0);
    s.snap.slot[0] = Skill{};
    s.snap.slot[0].known = true;
    s.snap.slot[0].id = -1;
    s.Step();
    CHECK(s.m.Selected() == -1);
}

static void TestModelEditor() {
    Rig r;
    r.Step(true);
    // EDIT SKILLS -> choose a slot -> the list
    r.Point(Page::Wheel, kEdit, true);
    CHECK(r.m.Current().page == Page::PickSlot);
    r.Point(Page::PickSlot, 2, true);   // Y, empty
    CHECK(r.m.Current().page == Page::PickSkill && r.m.Current().editSlot == 2);
    CHECK(r.m.Current().options == 5 && r.m.Current().editPages == 1);
    CHECK(r.m.Current().option[0].id == kSkillInteract);
    // an entry asks for an assignment through the game's UI thread, with what the slot held
    r.AimCell(Page::PickSkill, 3);   // Teleport
    r.Step(false, false, r.lu, r.lv);
    Out o = r.Step(false, true, r.lu, r.lv);
    CHECK(o.request && o.req.op == Op::Assign && o.req.slot == 2 && o.req.skill == 54 && o.req.bank == 0 && o.req.prevSkill == -1);
    CHECK(o.req.session == 7 && o.req.sequence == 1);
    CHECK(r.m.Current().result == Result::Pending);
    // while it is in flight a second click asks for nothing
    o = r.Step(false, true, r.lu, r.lv);
    CHECK(!o.request);
    // the answer: applied -> back to the slot choice with a line saying so
    r.reply = Reply{1, Result::Applied};
    o = r.Step();
    CHECK(o.feedback == Feedback::Applied && r.m.Current().page == Page::PickSlot);
    CHECK(std::strcmp(r.m.Current().status, "Assigned") == 0);
    // a clear
    r.Point(Page::PickSlot, 0, true);
    o = r.Point(Page::PickSkill, kCentre, true);
    CHECK(o.request && o.req.op == Op::Clear && o.req.slot == 0 && o.req.prevSkill == 36 && o.req.sequence == 2);
    r.reply = Reply{2, Result::Refused};
    o = r.Step();
    CHECK(o.feedback == Feedback::Refused && r.m.Current().page == Page::PickSlot);
    // no answer in 1.5 s is a refusal
    r.Point(Page::PickSlot, 1, true);
    o = r.Point(Page::PickSkill, 1, true);
    CHECK(o.request);
    r.t += 2.0;
    o = r.Step();
    CHECK(o.feedback == Feedback::Refused && r.m.Current().page == Page::PickSlot);
    // an answer for another request is not this one's
    r.Point(Page::PickSlot, 1, true);
    o = r.Point(Page::PickSkill, 1, true);
    CHECK(o.request);
    r.reply = Reply{o.req.sequence - 1, Result::Applied};
    r.Step();
    CHECK(r.m.Current().result == Result::Pending);
    // BACK steps back one page at a time
    Rig b;
    b.Step(true);
    b.Point(Page::Wheel, kEdit, true);
    b.Point(Page::PickSlot, 0, true);
    b.Point(Page::PickSkill, kBack, true);
    CHECK(b.m.Current().page == Page::PickSlot);
    b.Point(Page::PickSlot, kBack, true);
    CHECK(b.m.Current().page == Page::Wheel);
    // the weapon set changing under the editor ends it
    Rig w;
    w.Step(true);
    w.Point(Page::Wheel, kEdit, true);
    w.Point(Page::PickSlot, 0, true);
    w.snap.bank = 16;
    w.Step();
    CHECK(w.m.Current().page == Page::Wheel);
    // a layout the setter cannot write: the editor does not open
    Rig n;
    n.snap.editable = false;
    n.Step(true);
    Out on = n.Point(Page::Wheel, kEdit, true);
    CHECK(n.m.Current().page == Page::Wheel && on.feedback == Feedback::Refused);
}

// [openxr] skill_panel_edit_first (the user, 2026-10-25: "the skill menu, when I open it, should be in edit mode at once - I tap a tile and can assign"):
// the panel opens on its first page, and a tap on a slot is that slot's skill list; the old tap (the trigger casts that slot) is the trigger held kPickHoldS.
static void TestModelEditFirst() {
    Rig r;
    r.editFirst = true;
    Out o = r.Step(true);
    CHECK(r.m.Open() && r.m.Current().page == Page::Wheel && r.m.Current().editFirst);   // no EDIT SKILLS to press first
    // a tap on Y (empty): the trigger goes down - nothing yet - and up: the list for Y
    r.held = true;
    o = r.Point(Page::Wheel, 2, true);
    CHECK(r.m.Current().page == Page::Wheel && !o.request);
    r.Point(Page::Wheel, 2, false);
    CHECK(r.m.Current().page == Page::Wheel);   // (held a frame longer: still the press)
    r.held = false;
    o = r.Point(Page::Wheel, 2, false);
    CHECK(r.m.Current().page == Page::PickSkill && r.m.Current().editSlot == 2 && o.feedback == Feedback::Hover);
    CHECK(r.m.Selected() == -1);   // (not picked for the trigger)
    // a skill from the list: the game's setter puts it on Y, and the answer comes back to the first page
    o = r.Point(Page::PickSkill, 3, true);   // Teleport
    CHECK(o.request && o.req.op == Op::Assign && o.req.slot == 2 && o.req.skill == 54 && o.req.bank == 0 && o.req.prevSkill == -1);
    r.reply = Reply{o.req.sequence, Result::Applied};
    o = r.Step();
    CHECK(o.feedback == Feedback::Applied && r.m.Current().page == Page::Wheel && std::strcmp(r.m.Current().status, "Assigned") == 0);
    // BACK from a list a tap opened: the first page again
    r.Point(Page::Wheel, 0, true);
    r.Point(Page::Wheel, 0, false);
    CHECK(r.m.Current().page == Page::PickSkill && r.m.Current().editSlot == 0);
    r.Point(Page::PickSkill, kBack, true);
    CHECK(r.m.Current().page == Page::Wheel);
    // the old tap: the trigger held on B half a second - the trigger casts B from now on, the panel closes
    r.held = true;
    r.Point(Page::Wheel, 0, true);
    for (int i = 0; i < 45; ++i) r.Point(Page::Wheel, 0, false);
    CHECK(r.m.Open() && r.m.Selected() == -1);   // (0.45 s: not yet)
    for (int i = 0; i < 10 && r.m.Open(); ++i) o = r.Point(Page::Wheel, 0, false);
    CHECK(!r.m.Open() && r.m.Selected() == 0 && o.feedback == Feedback::Selected);
    r.held = false;
    r.Step();
    // the press, then the beam off the slot before it is let go: nothing
    r.Step(true);
    r.held = true;
    r.Point(Page::Wheel, 1, true);
    r.Point(Page::Wheel, 2, false);
    r.held = false;
    r.Point(Page::Wheel, 1, false);
    CHECK(r.m.Current().page == Page::Wheel && r.m.Selected() == 0);
    // the footer still works on the edge: CLOSE
    o = r.Point(Page::Wheel, kClose, true);
    CHECK(!r.m.Open() && o.feedback == Feedback::Closed);
    // EDIT SKILLS is still there; its list goes back to the slot choice, as before
    r.Step(true);
    r.Point(Page::Wheel, kEdit, true);
    r.Point(Page::PickSlot, 1, true);
    r.Point(Page::PickSkill, kBack, true);
    CHECK(r.m.Current().page == Page::PickSlot);
    // a layout the setter cannot write: a tap says so; the held trigger still picks the slot
    Rig n;
    n.editFirst = true;
    n.snap.editable = false;
    n.Step(true);
    n.Point(Page::Wheel, 1, true);
    o = n.Point(Page::Wheel, 1, false);
    CHECK(n.m.Current().page == Page::Wheel && o.feedback == Feedback::Refused && std::strcmp(n.m.Current().status, "Skills cannot be changed in this layout") == 0);
    n.held = true;
    n.Point(Page::Wheel, 1, true);
    for (int i = 0; i < 60 && n.m.Open(); ++i) n.Point(Page::Wheel, 1, false);
    CHECK(!n.m.Open() && n.m.Selected() == 1);
    // the key off: a tap picks for the trigger at once, as ever
    Rig off;
    off.Step(true);
    off.Point(Page::Wheel, 1, true);
    CHECK(!off.m.Open() && off.m.Selected() == 1);
}

static void TestCloseWithRequest() {
    Rig r;
    r.Step(true);
    r.Point(Page::Wheel, kEdit, true);
    r.Point(Page::PickSlot, 2, true);
    r.AimCell(Page::PickSkill, 3);
    r.Step(false, false, r.lu, r.lv);
    Out o = r.Step(false, true, r.lu, r.lv);
    CHECK(o.request && r.m.Current().result == Result::Pending);
    // the wheel is closed with the request still with the game: it stays pending, its answer is still told, the page is the wheel's again
    r.Step(true);
    CHECK(!r.m.Open());
    o = r.Step(true);   // (the chord again: no new request can be made while one is in flight - the wheel just opens)
    CHECK(o.request == false);
    r.Step(true);       // closed again
    CHECK(!r.m.Open());
    r.reply = Reply{o.req.sequence ? o.req.sequence : 1, Result::Applied};
    o = r.Step();
    CHECK(o.feedback == Feedback::Applied && !r.m.Open() && r.m.Current().page == Page::Wheel);
    // reopened: the wheel page, nothing pending
    r.Step(true);
    CHECK(r.m.Open() && r.m.Current().page == Page::Wheel && r.m.Current().result == Result::None);
}

static void TestPaging() {
    Rig r;
    r.snap.count = 20;
    for (uint32_t i = 0; i < 20; ++i) {
        r.snap.catalog[i] = Skill{};
        r.snap.catalog[i].id = (int)(100 + i);
        r.snap.catalog[i].known = r.snap.catalog[i].available = true;
        std::snprintf(r.snap.catalog[i].name, kNameLen, "Skill %u", i);
    }
    r.Step(true);
    r.Point(Page::Wheel, kEdit, true);
    r.Point(Page::PickSlot, 0, true);
    CHECK(r.m.Current().editPages == 3 && r.m.Current().options == 8 && r.m.Current().editPage == 0);
    r.Point(Page::PickSkill, kNext, true);
    CHECK(r.m.Current().editPage == 1 && r.m.Current().option[0].id == 108);
    r.Point(Page::PickSkill, kNext, true);
    CHECK(r.m.Current().editPage == 2 && r.m.Current().options == 4 && r.m.Current().option[3].id == 119);
    r.Point(Page::PickSkill, kNext, true);
    CHECK(r.m.Current().editPage == 2);   // no page after the last
    r.Point(Page::PickSkill, kPrev, true);
    r.Point(Page::PickSkill, kPrev, true);
    r.Point(Page::PickSkill, kPrev, true);
    CHECK(r.m.Current().editPage == 0);
    // an entry past the last on a short page is not there to click
    r.Point(Page::PickSkill, kNext, true);
    r.Point(Page::PickSkill, kNext, true);
    r.AimCell(Page::PickSkill, 5);
    const Out o = r.Step(false, true, r.lu, r.lv);
    CHECK(!o.request);
}

// -- the trigger's cast ------------------------------------------------------------------------------------------------

static void TestCast() {
    CastGate c;
    double t = 0;
    auto step = [&](int slot, float trigger, bool allowed = true) {
        CastGate::In in;
        in.time = t += 0.011;
        in.slot = slot;
        in.trigger = trigger;
        in.allowed = allowed;
        return c.Step(in);
    };
    // nothing picked: the trigger is the layout's
    for (int i = 0; i < 5; ++i) { const auto o = step(-1, 1.0f); CHECK(!o.casting && !o.takeTrigger && !o.buttons); }
    step(-1, 0.0f);
    // the first row: the trigger is the slot's button, and the layout's attack is taken
    CastGate::Out o = step(1, 0.0f);
    CHECK(!o.casting);
    o = step(1, 0.9f);
    CHECK(o.casting && o.takeTrigger && o.takeLeftTrigger && o.buttons == kPadX && o.lt == 0);
    for (int i = 0; i < 30; ++i) o = step(1, 0.9f);
    CHECK(o.casting && o.buttons == kPadX);     // held while the trigger is
    o = step(1, 0.0f);
    CHECK(!o.casting && !o.takeTrigger && !o.buttons);   // let go: no new pulse (the last one runs out by itself)
    // a tap is held for at least kMinPressS
    step(1, 0.9f);
    o = step(1, 0.0f);
    CHECK(o.casting && o.buttons == kPadX);
    for (int i = 0; i < 20; ++i) o = step(1, 0.0f);
    CHECK(!o.casting && !o.takeTrigger && !o.buttons);
    // the second row: the left trigger alone first, then the button with it
    step(5, 0.0f);
    o = step(5, 0.9f);
    CHECK(o.casting && o.lt == 255 && o.buttons == 0);   // slot 5 = LT + X
    for (int i = 0; i < 12; ++i) o = step(5, 0.9f);
    CHECK(o.lt == 255 && o.buttons == kPadX);
    for (int i = 0; i < 40; ++i) o = step(5, 0.0f);
    CHECK(!o.casting && o.lt == 0);
    // changing the pick mid-hold does not change the cast under way
    step(0, 0.0f);
    step(0, 0.9f);
    o = step(3, 0.9f);
    CHECK(o.buttons == kPadB);
    for (int i = 0; i < 30; ++i) o = step(3, 0.0f);
    // a trigger held when the gate comes back (through the wheel, a menu) starts nothing until it is let go
    step(2, 0.9f, false);
    for (int i = 0; i < 5; ++i) o = step(2, 0.9f, true);
    CHECK(!o.casting && !o.takeTrigger);
    step(2, 0.0f);
    o = step(2, 0.9f);
    CHECK(o.casting && o.buttons == kPadY);
    // the click that closes the wheel is the first allowed frame with the trigger rising: not a cast
    c.Reset();
    step(2, 0.0f, false);
    step(2, 0.0f, false);
    o = step(2, 0.9f, true);
    CHECK(!o.casting && !o.takeTrigger);
    // not allowed (the wheel, a menu, no focus) ends it at once
    o = step(2, 0.9f, false);
    CHECK(!o.casting && !o.buttons && !o.takeTrigger);
    // the hysteresis: a trigger wobbling between the thresholds does not tap twice
    c.Reset();
    step(0, 0.0f);
    step(0, 0.9f);
    int presses = 0;
    bool was = true;   // (the press that began above is held)
    for (int i = 0; i < 40; ++i) {
        o = step(0, i % 2 ? 0.4f : 0.3f);
        presses += o.buttons && !was;
        was = o.buttons != 0;
    }
    CHECK(presses == 0);   // (one press began before the loop; the wobble above kTriggerOff keeps it held, never starts another)
}

// -- the controller, on synthetic frames ----------------------------------------------------------------------------------

namespace di = directinput;

struct World {
    Controller c;
    Snapshot snap;
    uint64_t now = 20000;
    uint32_t counter = 0;
    double time = 100.0;
    di::Frame f{};
    Reply reply;
    World() {
        FakeGame g;
        snap = Captured(g, 3, now);
        f.version = di::kVersion;
        f.session = 3;
        f.epoch = 1;
        f.flags = di::kFocused | di::kDirect;
        for (int h = 0; h < 2; ++h) {
            f.hand[h].active = 1;
            f.hand[h].grip.valid = f.hand[h].aim.valid = 1;
            f.hand[h].grip.rot[3] = f.hand[h].aim.rot[3] = 1.0f;
            f.hand[h].aim.pos[0] = h ? 0.25f : -0.25f;
            f.hand[h].aim.pos[1] = 1.2f;
            f.hand[h].aim.pos[2] = -0.3f;
        }
        f.head.valid = 1;
        f.head.pos[1] = 1.6f;
        f.head.rot[3] = 1.0f;
    }
    Step Next() {
        f.counter = ++counter;
        f.time = time += 0.011;
        f.displayTime = (int64_t)(time * 1e9);
        now += 11;
        snap.at = now;
        return c.Frame(f, snap, now, reply);
    }
    void Hand(int h, float trigger, float squeeze, uint32_t buttons) {
        f.hand[h].trigger = trigger;
        f.hand[h].squeeze = squeeze;
        f.hand[h].buttons = buttons;
    }
    // the pointing hand (right) aims at a point of the panel
    void AimAt(float u, float v) {
        const Pose p = c.PanelPose();
        const float w = c.Config().widthM;
        const V3 target = Add(p.p, Rot(p.q, V3{(u - 0.5f) * w, (0.5f - v) * w, 0.0f}));
        const V3 from{f.hand[1].aim.pos[0], f.hand[1].aim.pos[1], f.hand[1].aim.pos[2]};
        const V3 d = Unit(Sub(target, from));
        // a rotation taking -z to d
        const V3 z{0, 0, -1};
        V3 axis{z.y * d.z - z.z * d.y, z.z * d.x - z.x * d.z, z.x * d.y - z.y * d.x};
        const float s = Len(axis), cs = Dot(z, d);
        const float ang = std::atan2(s, cs);
        axis = s > 1e-6f ? Mul(axis, 1.0f / s) : V3{0, 1, 0};
        f.hand[1].aim.rot[0] = axis.x * std::sin(ang / 2);
        f.hand[1].aim.rot[1] = axis.y * std::sin(ang / 2);
        f.hand[1].aim.rot[2] = axis.z * std::sin(ang / 2);
        f.hand[1].aim.rot[3] = std::cos(ang / 2);
    }
    void AimCell(Page page, int cell) {
        float u = 0.5f, v = 0.5f;
        if (page == Page::Wheel && cell >= 0 && cell < kSlots) WedgePoint(cell, 0.27f, &u, &v);
        else if (cell == kRunWalk) {   // (the wheel's third footer button)
            for (int i = 0; i < FooterCount(page, true); ++i)
                if (FooterCell(page, i, true) == cell) { const Rect r = FooterRect(page, i, true); u = 0.5f * (r.l + r.r); v = 0.5f * (r.t + r.b); }
        } else if (cell >= kCategory0 && cell < kCategory0 + kCategories) { const Rect r = CategoryRect(cell - kCategory0); u = 0.5f * (r.l + r.r); v = 0.5f * (r.t + r.b); }
        else if (cell == kEdit || cell == kClose || cell == kBack || cell == kPrev || cell == kNext) {
            for (int i = 0; i < FooterCount(page); ++i)
                if (FooterCell(page, i) == cell) { const Rect r = FooterRect(page, i); u = 0.5f * (r.l + r.r); v = 0.5f * (r.t + r.b); }
        } else if (page == Page::PickSkill && cell == kCentre) { u = 0.8f; v = 0.15f; }
        else if (page == Page::PickSkill) { const Rect r = OptionRect(cell); u = 0.5f * (r.l + r.r); v = 0.5f * (r.t + r.b); }
        else if (page == Page::PickSlot && cell >= 0) WedgePoint(cell, 0.27f, &u, &v);
        else if (cell == kCentre) { u = 0.5f; v = 0.45f; }
        AimAt(u, v);
    }
};

constexpr uint32_t kStickBit = di::kStick;

static void OpenWheel(World& w) {
    w.Hand(0, 0, 0.9f, 0);   // the left grip
    w.Next();
    w.Hand(0, 0, 0.9f, 0);
    w.Hand(1, 0, 0, kStickBit);   // and the right stick click
    w.Next();
}

static void TestController() {
    {
        World w;
        // the chord: the other (left) grip down, then the right stick click
        Step s = w.Next();
        CHECK(!w.c.Open() && s.take[0] == 0 && s.take[1] == 0);
        w.Hand(0, 0, 0.9f, 0);
        s = w.Next();
        CHECK(!w.c.Open());
        CHECK((s.take[1] & di::kStick) != 0);   // from the grip on, the click is the wheel's: it cannot leak into a swap
        w.Hand(1, 0, 0, kStickBit);
        s = w.Next();
        CHECK(w.c.Open() && s.feedback == Feedback::Opened);
        CHECK((s.take[0] & (di::kTakeTrigger | di::kTakeSqueeze)) == (di::kTakeTrigger | di::kTakeSqueeze));
        CHECK((s.take[1] & (di::kTakeTrigger | di::kTakeSqueeze | di::kSecondary | di::kPrimary | di::kMenuButton | di::kStick)) ==
              (di::kTakeTrigger | di::kTakeSqueeze | di::kSecondary | di::kPrimary | di::kMenuButton | di::kStick));
        CHECK(s.layer.show && Near(s.layer.widthM, 0.8f));
        // the panel is ahead of the head, a little below, facing it
        const Pose p = w.c.PanelPose();
        CHECK(Near(p.p.x, 0.0f) && Near(p.p.y, 1.6f - 0.08f) && Near(p.p.z, -0.8f));
        // the beam starts at the hand and ends on the panel
        w.AimCell(Page::Wheel, 1);
        s = w.Next();
        CHECK(s.layer.beam && s.layer.hit && s.layer.len > 0.3f && s.layer.len < 1.2f);
        CHECK(w.c.ViewNow().hover == 1);
        // the sticks are not taken: walking and turning go on
        CHECK((s.take[0] & di::kStick) != 0);   // (the click is; the analog stick is not part of a take at all)
        // the trigger picks the wedge
        w.Hand(1, 0.9f, 0, kStickBit);
        s = w.Next();
        CHECK(!w.c.Open() && w.c.Selected() == 1 && s.feedback == Feedback::Selected);
        // what is held when it closes stays taken until it is let go: the trigger, the stick click
        CHECK((s.take[1] & di::kTakeTrigger) && (s.take[1] & di::kStick));
        s = w.Next();
        CHECK((s.take[1] & di::kTakeTrigger) && (s.take[1] & di::kStick) && !s.casting);
        w.Hand(1, 0.0f, 0, 0);
        w.Hand(0, 0, 0, 0);
        s = w.Next();
        CHECK(s.take[0] == 0 && s.take[1] == 0);
        // now the trigger casts the slot: X (picked wedge 1), the layout's attack taken
        w.Hand(1, 0.9f, 0, 0);
        s = w.Next();
        CHECK(s.casting && s.buttons == kPadX && s.lt == 0);
        CHECK((s.take[1] & di::kTakeTrigger) && (s.take[0] & di::kTakeTrigger));
        w.Hand(1, 0.0f, 0, 0);
        for (int i = 0; i < 30; ++i) s = w.Next();
        CHECK(!s.casting && s.take[1] == 0);
        // the bow draw keeps the pointing hand's trigger (a bow wielded with the draw on shoots by a draw only): the picked slot is not
        // cast with it, nor does the layout's attack get it from here
        w.c.SetCastBlocked(true);
        w.Hand(1, 0.9f, 0, 0);
        for (int i = 0; i < 5; ++i) { s = w.Next(); CHECK(!s.casting && s.buttons == 0 && !(s.take[1] & di::kTakeTrigger)); }
        // given back with the trigger still down: nothing starts until it is let go and pressed again
        w.c.SetCastBlocked(false);
        for (int i = 0; i < 3; ++i) { s = w.Next(); CHECK(!s.casting); }
        w.Hand(1, 0.0f, 0, 0);
        s = w.Next();
        w.Hand(1, 0.9f, 0, 0);
        s = w.Next();
        CHECK(s.casting && s.buttons == kPadX);
        w.Hand(1, 0.0f, 0, 0);
        for (int i = 0; i < 30; ++i) s = w.Next();
        CHECK(!s.casting);
    }
    {   // both grips: the settings panel's chord, not this one
        World w;
        w.Hand(0, 0, 0.9f, 0);
        w.Hand(1, 0, 0.9f, kStickBit);
        Step s = w.Next();
        s = w.Next();
        CHECK(!w.c.Open());
        CHECK((s.take[1] & di::kStick) == 0);
    }
    {   // the click alone, a grip alone, the grip after the click: nothing
        World w;
        w.Hand(1, 0, 0, kStickBit);
        w.Next();
        CHECK(!w.c.Open());
        w.Hand(1, 0, 0, 0);
        w.Hand(0, 0, 0.9f, 0);
        w.Next();
        CHECK(!w.c.Open());
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, kStickBit);
        w.Next();
        w.Hand(0, 0, 0.9f, 0);
        w.Next();
        CHECK(!w.c.Open());   // (the grip came after the click: the click was an edge long gone)
    }
    {   // not in a game with a menu open, not without the focus
        World w;
        w.f.flags |= di::kInMenu;
        OpenWheel(w);
        CHECK(!w.c.Open());
        World n;
        n.f.flags = di::kDirect;   // no focus
        OpenWheel(n);
        CHECK(!n.c.Open());
        World nh;
        nh.f.head.valid = 0;
        OpenWheel(nh);
        CHECK(!nh.c.Open());
        World held;
        held.f.head.valid = 2;   // orientation only: a held place, not a place to hang a panel
        OpenWheel(held);
        CHECK(!held.c.Open());
        World stale;
        stale.snap.session = 77;
        OpenWheel(stale);
        CHECK(!stale.c.Open());
    }
    {   // the wheel closes when a menu opens under it, and its controls stay with it
        World w;
        OpenWheel(w);
        CHECK(w.c.Open());
        w.f.flags |= di::kInMenu;
        ++w.f.epoch;
        const Step s = w.Next();
        CHECK(!w.c.Open());
        (void)s;
    }
    {   // the editor through the controller: a request goes out for the UI thread
        World w;
        OpenWheel(w);
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        w.Next();
        auto click = [&](Page page, int cell) {
            w.AimCell(page, cell);
            w.Hand(1, 0, 0, 0);
            w.Next();
            w.Hand(1, 0.9f, 0, 0);
            const Step s = w.Next();
            w.Hand(1, 0, 0, 0);
            w.Next();
            return s;
        };
        click(Page::Wheel, kEdit);
        click(Page::PickSlot, 2);
        const Step s = click(Page::PickSkill, 3);
        CHECK(s.request && s.req.op == Op::Assign && s.req.skill == 54 && s.req.slot == 2);
    }
    {   // a left-handed player: the pointing hand is the left, the modifier grip the right
        World w;
        Settings cfg;
        cfg.pointerHand = 0;
        w.c.Configure(cfg);
        w.Hand(1, 0, 0.9f, 0);
        w.Next();
        w.Hand(0, 0, 0, kStickBit);
        w.Next();
        CHECK(w.c.Open());
    }
}

// -- the picture and the icon manifest ---------------------------------------------------------------------------------------

static Skill Sk(int id, const char* name, bool available = true, int level = 5) {
    Skill s;
    s.id = id; s.known = true; s.available = available; s.level = level;
    std::snprintf(s.name, kNameLen, "%s", name);
    return s;
}

static View SampleView(Page page, int hover, int selected) {
    View v;
    v.open = true; v.page = page; v.hover = hover; v.selected = selected;
    v.ready = v.editable = true; v.bank = 0;
    v.slot[0] = Sk(36, "Fire Bolt");
    v.slot[1] = Sk(47, "Fire Ball");
    v.slot[2] = Skill{}; v.slot[2].known = true;
    v.slot[3] = Sk(54, "Teleport", false);
    v.slot[4] = Sk(40, "Frozen Armor");
    v.slot[5] = Sk(45, "Ice Blast");
    v.slot[6] = Sk(kSkillTownPortal, "Town Portal", true, 0);
    v.slot[7] = Sk(56, "Meteor");
    v.editSlot = page == Page::PickSkill ? 2 : -1;
    v.editPage = 0; v.editPages = 2; v.options = 8;
    for (int i = 0; i < 8; ++i) v.option[i] = Sk(100 + i, i == 0 ? "Interact / pick up" : "A Skill With A Rather Long Name", true, i);
    v.option[0].id = kSkillInteract;
    v.revision = 1;
    return v;
}

static uint8_t Chan(uint32_t p, int c) { return (uint8_t)(p >> (8 * c)); }

static void DumpRgba(const char* dir, const char* name, const std::vector<uint32_t>& px) {
    char path[400];
    std::snprintf(path, sizeof path, "%s/%s.rgba", dir, name);
    FILE* f = nullptr;
    if (!fopen_s(&f, path, "wb") && f) { std::fwrite(px.data(), 4, px.size(), f); std::fclose(f); }
}

static void TestPaint(const char* dumpDir) {
    const int N = wheelpaint::kSize;
    std::vector<uint32_t> px;
    wheelpaint::Render(SampleView(Page::Wheel, 2, 1), px);
    CHECK(px.size() == (size_t)N * N);
    CHECK(Chan(px[0], 3) == 0);                                      // the corner is clear
    CHECK(Chan(px[(size_t)(N - 1) * N + N - 1], 3) == 0);
    // premultiplied: no channel above the alpha
    bool ok = true;
    for (size_t i = 0; i < px.size() && ok; i += 7) ok = Chan(px[i], 0) <= Chan(px[i], 3) + 1 && Chan(px[i], 1) <= Chan(px[i], 3) + 1 && Chan(px[i], 2) <= Chan(px[i], 3) + 1;
    CHECK(ok);
    // a point in the middle of each wedge, near the ring's inner edge, is opaque; the one under the beam is brighter than its neighbour
    auto at = [&](int k, float radius) {
        float u, v;
        WedgePoint(k, radius, &u, &v);
        return px[(size_t)(v * N) * N + (size_t)(u * N)];
    };
    for (int k = 0; k < kSlots; ++k) CHECK(Chan(at(k, 0.16f), 3) > 200);
    const int hot = Chan(at(2, 0.16f), 0), cold = Chan(at(3, 0.16f), 0);
    CHECK(hot > cold + 10);
    // the hole between the centre button and the ring is clear of the backdrop's edge but the backdrop is there; far outside is clear
    CHECK(Chan(px[(size_t)5 * N + N / 2], 3) == 0);
    // the editor's page and the other page paint, and differ from the wheel's
    std::vector<uint32_t> list, slots;
    wheelpaint::Render(SampleView(Page::PickSkill, 3, -1), list);
    wheelpaint::Render(SampleView(Page::PickSlot, 4, -1), slots);
    CHECK(list.size() == (size_t)N * N && slots.size() == (size_t)N * N && list != px && slots != px && list != slots);
    // the category strip over the editor's list: five tabs (the shown one lit, a hovered one lit, an empty one dim), only in the strip's band
    View strip = SampleView(Page::PickSkill, 3, -1);
    strip.catStrip = true;
    strip.category = kCatTree1;
    strip.catCount[kCatAll] = 26; strip.catCount[kCatTree1] = 10; strip.catCount[kCatTree2] = 3; strip.catCount[kCatTree3] = 0; strip.catCount[kCatGeneral] = 13;
    std::snprintf(strip.treeName[0], kNameLen, "%s", "Fire Spells");
    std::snprintf(strip.treeName[1], kNameLen, "%s", "Lightning Spells");
    std::snprintf(strip.treeName[2], kNameLen, "%s", "Cold Spells");
    std::vector<uint32_t> stripPx, stripHot, stripOther, stripNoHover;
    wheelpaint::Render(strip, stripPx);
    View noHover = strip;
    noHover.hover = kNone;
    wheelpaint::Render(noHover, stripNoHover);
    View hotTab = strip;
    hotTab.hover = CategoryCell(kCatGeneral);
    wheelpaint::Render(hotTab, stripHot);
    View otherTab = strip;
    otherTab.category = kCatTree2;
    wheelpaint::Render(otherTab, stripOther);
    CHECK(stripPx.size() == (size_t)N * N && stripPx != list && stripNoHover != stripHot && stripPx != stripOther);
    {   // the band of the strip is all that differs between the list with it and without it, and the tab under the beam is the only change a hover makes
        const Rect band = CategoryRect(kCatAll), bandEnd = CategoryRect(kCatGeneral);
        int outside = 0, hotOutside = 0;
        const Rect hotRect = CategoryRect(kCatGeneral);
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
                const bool inBand = u >= band.l - 0.03f && u <= bandEnd.r + 0.03f && v >= band.t - 0.03f && v <= band.b + 0.03f;
                if (!inBand) outside += stripPx[(size_t)y * N + x] != list[(size_t)y * N + x];
                if (!(u >= hotRect.l - 0.03f && u <= hotRect.r + 0.03f && v >= hotRect.t - 0.03f && v <= hotRect.b + 0.03f))
                    hotOutside += stripNoHover[(size_t)y * N + x] != stripHot[(size_t)y * N + x];
            }
        if (outside || hotOutside) std::printf("  strip: %d pixels differ outside the band, %d outside the hovered tab\n", outside, hotOutside);
        CHECK(outside == 0 && hotOutside == 0);
    }
    // RUN / WALK in the footer of the wheel's first page: there with the flag only, three buttons then; the state differs; not on the other pages
    View runView = SampleView(Page::Wheel, -1, -1);
    runView.runWalk = true;
    std::vector<uint32_t> runOff, runUnknown, runWalking, runRunning, runHot, runPick, runPickOff;
    wheelpaint::Render(SampleView(Page::Wheel, -1, -1), runOff);
    wheelpaint::Render(runView, runUnknown);
    runView.run = kWalking;
    wheelpaint::Render(runView, runWalking);
    runView.run = kRunning;
    wheelpaint::Render(runView, runRunning);
    runView.hover = kRunWalk;
    wheelpaint::Render(runView, runHot);
    CHECK(runOff != runUnknown && runUnknown != runWalking && runWalking != runRunning && runRunning != runHot);
    {
        View v = SampleView(Page::PickSlot, -1, -1);
        wheelpaint::Render(v, runPickOff);
        v.runWalk = true;
        wheelpaint::Render(v, runPick);
        CHECK(runPick == runPickOff);
    }
    {   // only the footer band changes with the state (the ring is the same whichever it is)
        const Rect first = FooterRect(Page::Wheel, 0, true), last = FooterRect(Page::Wheel, 2, true);
        int outside = 0;
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                const float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
                if (u >= first.l - 0.03f && u <= last.r + 0.03f && v >= first.t - 0.03f && v <= last.b + 0.03f) continue;
                outside += runWalking[(size_t)y * N + x] != runRunning[(size_t)y * N + x];
            }
        CHECK(outside == 0);
    }
    // a game that is not ready: still a picture (the labels, "Skills unavailable")
    View off = SampleView(Page::Wheel, -1, -1);
    off.ready = false;
    std::vector<uint32_t> idle;
    wheelpaint::Render(off, idle);
    CHECK(idle.size() == (size_t)N * N && idle != px);
    // the worker: a view submitted comes back painted, once
    wheelpaint::Async a;
    a.Submit(SampleView(Page::Wheel, 0, -1));
    std::vector<uint32_t> got;
    uint32_t rev = 0;
    bool have = false;
    for (int i = 0; i < 200 && !have; ++i) { have = a.Fetch(got, &rev); if (!have) Sleep(10); }
    CHECK(have && rev == 1 && got.size() == (size_t)N * N);
    CHECK(!a.Fetch(got, &rev));
    a.Stop();
    // stopped with the session and started again with the next: the same object paints again
    a.Warm();
    View again = SampleView(Page::PickSlot, 1, 2);
    again.revision = 7;
    a.Submit(again);
    have = false;
    for (int i = 0; i < 200 && !have; ++i) { have = a.Fetch(got, &rev); if (!have) Sleep(10); }
    CHECK(have && rev == 7);
    a.Stop();
    a.Stop();   // (twice is nothing)
    // with a real cache (SKILL_WHEEL_ICONS = the folder; made by tools/skill_icons from the tester's own game): the icons are drawn
    char icons[400] = {};
    if (dumpDir && GetEnvironmentVariableA("SKILL_WHEEL_ICONS", icons, sizeof icons)) {
        wchar_t wide[400] = {};
        MultiByteToWideChar(CP_UTF8, 0, icons, -1, wide, 399);
        wheelicons::Shutdown();   // (the manifest of the default folder was read already)
        wheelicons::SetDirectory(wide);
        CHECK(wheelicons::Available() && wheelicons::NameKey(36) == "skillname36");
        wheelpaint::Render(SampleView(Page::Wheel, 2, 1), px);   // (asks for the icons)
        wheelpaint::Render(SampleView(Page::PickSkill, 3, -1), list);
        for (int i = 0; i < 100; ++i) { Sleep(20); }
        std::vector<uint32_t> withIcons;
        wheelpaint::Render(SampleView(Page::Wheel, 2, 1), withIcons);
        CHECK(withIcons != px);   // the icons arrived
        px.swap(withIcons);
        wheelpaint::Render(SampleView(Page::PickSkill, 3, -1), list);
        wheelicons::Shutdown();
    }
    if (dumpDir) {   // how long a picture takes (on the painter's thread, not the frame thread)
        LARGE_INTEGER freq, t0, t1;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < 10; ++i) wheelpaint::Render(SampleView(Page::Wheel, i % 8, 1), idle);
        QueryPerformanceCounter(&t1);
        std::printf("  a wheel picture: %.1f ms\n", 1000.0 * double(t1.QuadPart - t0.QuadPart) / double(freq.QuadPart) / 10.0);
    }
    if (dumpDir) {
        DumpRgba(dumpDir, "wheel", px);
        DumpRgba(dumpDir, "pickskill", list);
        DumpRgba(dumpDir, "pickslot", slots);
        DumpRgba(dumpDir, "notready", idle);
        DumpRgba(dumpDir, "pickskill_trees", stripPx);
        DumpRgba(dumpDir, "wheel_runwalk", runHot);
    }
}

static void TestManifest() {
    const std::string file(64, 'a');
    wheelicons::Entry e;
    CHECK(wheelicons::ParseLine("s36\t" + file + ".png\tskillname36", &e) && e.id == 36 && e.file == file + ".png" && e.key == "skillname36");
    CHECK(wheelicons::ParseLine("s359\t" + file + ".png\r", &e) && e.id == 359 && e.key.empty());
    CHECK(wheelicons::ParseLine("s1\t" + file + ".png\tbad key!", &e) && e.key.empty());     // an odd key is dropped, the icon kept
    CHECK(!wheelicons::ParseLine("i00000000\t" + file + ".png", &e));                          // not a skill
    CHECK(!wheelicons::ParseLine("s36\t..\\..\\x.png", &e));                                  // no path: only a hash and .png
    CHECK(!wheelicons::ParseLine("s36\t" + file + ".exe", &e));
    CHECK(!wheelicons::ParseLine("s99999\t" + file + ".png", &e));
    CHECK(!wheelicons::ParseLine("sx\t" + file + ".png", &e));
    CHECK(!wheelicons::ParseLine("", &e));
    CHECK(!wheelicons::ParseLine("s36", &e));
}

// -- skill trees and RUN / WALK -------------------------------------------------------------------------------------------------

static void TestTrees() {
    TreeGame g;
    Snapshot s;
    CHECK(Capture(g, &s) && s.treesKnown && s.playerClass == 1 && s.run == kWalking);
    CHECK(std::strcmp(s.treeName[0], "Fire Spells") == 0 && std::strcmp(s.treeName[1], "Lightning Spells") == 0 && std::strcmp(s.treeName[2], "Cold Spells") == 0);
    auto treeOf = [&](int32_t id, int32_t owner = -1) {
        for (uint32_t i = 0; i < s.count; ++i)
            if (s.catalog[i].id == id && s.catalog[i].owner == owner) return (int)s.catalog[i].tree;
        return -99;
    };
    CHECK(treeOf(36) == 1 && treeOf(47) == 1 && treeOf(54) == 2);
    CHECK(treeOf(kSkillInteract) == 0 && treeOf(kSkillTownPortal) == 0);
    CHECK(treeOf(70, 5) == 0);                      // an item's skill is General / Items whatever the game's table says of the skill
    CHECK(s.slot[0].tree == kTreeUnknown);          // (the slots' own skills are not placed: only the editor's list is)
    g.run = kRunning;
    CHECK(Capture(g, &s) && s.run == kRunning);
    g.run = kRunUnknown;
    CHECK(Capture(g, &s) && s.run == kRunUnknown);
    // the cache has no tree data: nothing is placed, no names, no strip
    g.known = false;
    CHECK(Capture(g, &s) && !s.treesKnown && treeOf(36) == kTreeUnknown && s.treeName[0][0] == 0);
    // the player's class is not known
    g.known = true;
    g.cls = -1;
    CHECK(Capture(g, &s) && !s.treesKnown);
    // a tree name the game's text does not give: a plain "Tree n"
    struct NoNames : TreeGame {
        bool TreeName(int, char*, int) override { return false; }
    } nn;
    CHECK(Capture(nn, &s) && s.treesKnown && std::strcmp(s.treeName[0], "Tree 1") == 0 && std::strcmp(s.treeName[2], "Tree 3") == 0);
    // a game that says nothing of trees (the interface's defaults): the one list, the run state unknown
    FakeGame plain;
    CHECK(Capture(plain, &s) && !s.treesKnown && s.playerClass == -1 && s.run == kRunUnknown && s.count == 5);
}

static void TestCategoryGeometry() {
    // the strip's five tabs are side by side, in the band between the title and the list, none over the clear button, the list or the footer
    for (int c = 0; c < kCategories; ++c) {
        const Rect r = CategoryRect(c);
        CHECK(r.l >= 0.0f && r.r <= 1.0f && r.t >= kClearRect.b && r.b <= OptionRect(0).t);
        for (int d = c + 1; d < kCategories; ++d) {
            const Rect q = CategoryRect(d);
            CHECK(r.r <= q.l);
        }
        CHECK(HitTest(Page::PickSkill, 0.5f * (r.l + r.r), 0.5f * (r.t + r.b), kNone, 8, true) == CategoryCell(c));
        CHECK(HitTest(Page::PickSkill, 0.5f * (r.l + r.r), 0.5f * (r.t + r.b), kNone, 8, false) == kNone);   // (no strip: nothing there)
    }
    CHECK(CategoryCell(kCatAll) == kCategory0 && CategoryCell(kCatGeneral) == kCategory0 + 4 && kRunWalk == kCategory0 + kCategories);
    // the list is under the strip and still inside the panel, and nothing overlaps: sample the page
    for (int i = 0; i < kOptionsPerPage; ++i) CHECK(OptionRect(i).t >= CategoryRect(0).b && OptionRect(i).b <= 0.855f);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x) {
            const float u = (x + 0.37f) / 200.0f, v = (y + 0.37f) / 200.0f;
            int n = 0;
            for (int i = 0; i < kOptionsPerPage; ++i) n += OptionRect(i).Has(u, v);
            for (int c = 0; c < kCategories; ++c) n += CategoryRect(c).Has(u, v);
            n += kClearRect.Has(u, v);
            for (int i = 0; i < FooterCount(Page::PickSkill); ++i) n += FooterRect(Page::PickSkill, i).Has(u, v);
            CHECK(n <= 1);
        }
    // RUN / WALK: the wheel page's footer has three buttons then, EDIT SKILLS, RUN / WALK, CLOSE, side by side; without it two, as ever
    CHECK(FooterCount(Page::Wheel) == 2 && FooterCount(Page::Wheel, true) == 3 && FooterCount(Page::PickSlot, true) == 1 && FooterCount(Page::PickSkill, true) == 3);
    CHECK(FooterCell(Page::Wheel, 0, true) == kEdit && FooterCell(Page::Wheel, 1, true) == kRunWalk && FooterCell(Page::Wheel, 2, true) == kClose);
    CHECK(FooterCell(Page::Wheel, 0) == kEdit && FooterCell(Page::Wheel, 1) == kClose);
    for (int i = 0; i < 3; ++i) {
        const Rect r = FooterRect(Page::Wheel, i, true);
        CHECK(HitTest(Page::Wheel, 0.5f * (r.l + r.r), 0.5f * (r.t + r.b), kNone, 0, false, true) == FooterCell(Page::Wheel, i, true));
        if (i) CHECK(FooterRect(Page::Wheel, i - 1, true).r <= r.l);
    }
    const Rect mid = FooterRect(Page::Wheel, 1, true);
    CHECK(HitTest(Page::Wheel, 0.5f * (mid.l + mid.r), 0.5f * (mid.t + mid.b), kNone, 0, false, false) == kNone);   // (off: the gap between the two)
}

static void TestManifestTrees() {
    const std::string file(64, 'b');
    const std::string t = "s36\t" + file + ".png\t";
    wheelicons::Entry e;
    CHECK(wheelicons::ParseLine(t + "skillname36\t1\t3", &e) && e.key == "skillname36" && e.cls == 1 && e.page == 3);      // (the key is not swallowed by the columns after it)
    CHECK(wheelicons::ParseLine(t + "skillname36\t7\t0", &e) && e.cls == 7 && e.page == 0);
    CHECK(wheelicons::ParseLine(t + "skillname36", &e) && e.cls == -1 && e.page == 0);                                        // a cache made before the trees
    CHECK(wheelicons::ParseLine(t + "skillname36\t\t", &e) && e.cls == -1 && e.page == 0);                                  // a skill of no class
    CHECK(wheelicons::ParseLine(t + "skillname36\t99\t1", &e) && e.cls == -1 && e.page == 0);                               // a class out of range: none, and no page of it
    CHECK(wheelicons::ParseLine(t + "skillname36\t1\t9", &e) && e.cls == 1 && e.page == 0);                                 // a page out of range
    CHECK(wheelicons::ParseLine(t + "skillname36\tx\ty", &e) && e.cls == -1 && e.page == 0 && e.key == "skillname36");
    CHECK(wheelicons::ParseLine(t + "bad key!\t1\t1", &e) && e.key.empty() && e.cls == 1 && e.page == 1);                  // an odd key is dropped, the rest kept
    CHECK(wheelicons::ParseLine(t + "skillname36\t1\t1\textra\tmore", &e) && e.cls == 1 && e.page == 1);                  // columns after these are not ours
    wheelicons::TreeLine tl;
    CHECK(wheelicons::ParseTreeLine("1\tStrSklTabItem15\tStrSklTabItem14\tStrSklTabItem13", &tl) && tl.cls == 1 && tl.key[0] == "StrSklTabItem15" && tl.key[2] == "StrSklTabItem13");
    CHECK(wheelicons::ParseTreeLine("7\tA\tB\tC\r", &tl) && tl.cls == 7 && tl.key[2] == "C");
    CHECK(wheelicons::ParseTreeLine("2\tA\tbad key\tC", &tl) && tl.key[0] == "A" && tl.key[1].empty() && tl.key[2] == "C");
    CHECK(wheelicons::ParseTreeLine("3\tA", &tl) && tl.key[0] == "A" && tl.key[1].empty());
    CHECK(!wheelicons::ParseTreeLine("", &tl) && !wheelicons::ParseTreeLine("x\tA\tB\tC", &tl) && !wheelicons::ParseTreeLine("99\tA\tB\tC", &tl) && !wheelicons::ParseTreeLine("4", &tl));
}

static void TestModelCategories() {
    Rig r;
    {
        TreeGame g;
        r.snap = Captured(g, r.session, r.now);
    }
    r.categories = true;
    r.Step(true);
    r.Point(Page::Wheel, kEdit, true);
    r.Point(Page::PickSlot, 2, true);
    const View& v = r.m.Current();
    CHECK(v.page == Page::PickSkill && v.editSlot == 2 && v.catStrip && v.category == kCatAll);
    // the catalog is Interact, Fire Ball, Fire Bolt, Grisly Thing (an item's), Teleport, Town Portal
    CHECK(v.catCount[kCatAll] == 6 && v.catCount[kCatTree1] == 2 && v.catCount[kCatTree2] == 1 && v.catCount[kCatTree3] == 0 && v.catCount[kCatGeneral] == 3);
    CHECK(std::strcmp(v.treeName[0], "Fire Spells") == 0 && std::strcmp(v.treeName[2], "Cold Spells") == 0);
    CHECK(v.options == 6 && v.editPages == 1);
    // a tree: only its skills, in the catalog's order, page 0
    r.Point(Page::PickSkill, CategoryCell(kCatTree1), false);
    CHECK(r.m.Current().hover == CategoryCell(kCatTree1));
    Out o = r.Point(Page::PickSkill, CategoryCell(kCatTree1), true);
    CHECK(o.feedback == Feedback::Hover && !o.request);
    CHECK(v.category == kCatTree1 && v.options == 2 && v.option[0].id == 47 && v.option[1].id == 36 && v.editPage == 0 && v.editPages == 1);
    // a tab with nothing in it: refused, the one shown stays
    o = r.Point(Page::PickSkill, CategoryCell(kCatTree3), true);
    CHECK(o.feedback == Feedback::Refused && v.category == kCatTree1 && v.options == 2);
    // General / Items: the command, the item's skill, the town portal; a pick there asks for that skill and its owner
    r.Point(Page::PickSkill, CategoryCell(kCatGeneral), true);
    CHECK(v.category == kCatGeneral && v.options == 3 && v.option[0].id == kSkillInteract && v.option[1].id == 70 && v.option[1].owner == 5 && v.option[2].id == kSkillTownPortal);
    o = r.Point(Page::PickSkill, 1, true);
    CHECK(o.request && o.req.op == Op::Assign && o.req.skill == 70 && o.req.owner == 5 && o.req.slot == 2);
    r.reply = Reply{o.req.sequence, Result::Applied};
    r.Step();
    CHECK(r.m.Current().page == Page::PickSlot);
    // the next editor opens on All again, and the same strip
    r.Point(Page::PickSlot, 0, true);
    CHECK(v.page == Page::PickSkill && v.category == kCatAll && v.options == 6 && v.catCount[kCatTree1] == 2);
    // All again from a tree
    r.Point(Page::PickSkill, CategoryCell(kCatTree2), true);
    CHECK(v.category == kCatTree2 && v.options == 1 && v.option[0].id == 54);
    r.Point(Page::PickSkill, CategoryCell(kCatAll), true);
    CHECK(v.category == kCatAll && v.options == 6);
    // paging is within the category: 30 skills on tree 1 make pages of 8
    Rig p;
    {
        TreeGame g;
        p.snap = Captured(g, p.session, p.now);
        p.snap.count = 20;
        for (uint32_t i = 0; i < 20; ++i) {
            p.snap.catalog[i] = Skill{};
            p.snap.catalog[i].id = (int)(100 + i);
            p.snap.catalog[i].known = p.snap.catalog[i].available = true;
            p.snap.catalog[i].tree = i < 11 ? 1 : 2;
            std::snprintf(p.snap.catalog[i].name, kNameLen, "Skill %u", i);
        }
    }
    p.categories = true;
    p.Step(true);
    p.Point(Page::Wheel, kEdit, true);
    p.Point(Page::PickSlot, 0, true);
    p.Point(Page::PickSkill, CategoryCell(kCatTree1), true);
    CHECK(p.m.Current().options == 8 && p.m.Current().editPages == 2 && p.m.Current().catCount[kCatTree1] == 11);
    p.Point(Page::PickSkill, kNext, true);
    CHECK(p.m.Current().editPage == 1 && p.m.Current().options == 3 && p.m.Current().option[0].id == 108);
    p.Point(Page::PickSkill, CategoryCell(kCatTree2), true);   // (another category starts on its first page)
    CHECK(p.m.Current().editPage == 0 && p.m.Current().options == 8 && p.m.Current().option[0].id == 111 && p.m.Current().catCount[kCatTree2] == 9);
    // switched off ([openxr] skill_categories=0), or the cache does not know the trees: no strip, every skill in the one list, the strip's place is nothing
    for (int variant = 0; variant < 2; ++variant) {
        Rig q;
        TreeGame g;
        g.known = variant == 1;
        q.snap = Captured(g, q.session, q.now);
        q.categories = variant == 1 ? false : true;   // (0: the cache has none, the key is on; 1: the cache has them, the key is off)
        if (variant == 1) { TreeGame k; q.snap = Captured(k, q.session, q.now); }
        q.Step(true);
        q.Point(Page::Wheel, kEdit, true);
        q.Point(Page::PickSlot, 2, true);
        const View& w = q.m.Current();
        CHECK(w.page == Page::PickSkill && !w.catStrip && w.category == kCatAll && w.options == 6);
        q.AimCell(Page::PickSkill, CategoryCell(kCatTree1));
        q.Step(false, true, q.lu, q.lv);
        CHECK(w.hover == kNone && w.options == 6 && w.category == kCatAll);
    }
    // leaving the editor takes the strip away; the weapon set changing under it ends the editor
    Rig b;
    {
        TreeGame g;
        b.snap = Captured(g, b.session, b.now);
    }
    b.categories = true;
    b.Step(true);
    b.Point(Page::Wheel, kEdit, true);
    b.Point(Page::PickSlot, 1, true);
    CHECK(b.m.Current().catStrip);
    b.Point(Page::PickSkill, kBack, true);
    CHECK(!b.m.Current().catStrip && b.m.Current().page == Page::PickSlot);
    b.Point(Page::PickSlot, 1, true);
    b.snap.bank = 16;
    b.Step();
    CHECK(b.m.Current().page == Page::Wheel && !b.m.Current().catStrip);
}

static void TestModelRunWalk() {
    Rig r;
    r.runWalk = true;
    r.Step(true);
    CHECK(r.m.Open() && r.m.Current().runWalk);
    // the button is under the beam, then clicked: the host is told to press the game's switch, once; the panel stays up
    r.Point(Page::Wheel, kRunWalk, false);
    CHECK(r.m.Current().hover == kRunWalk);
    Out o = r.Point(Page::Wheel, kRunWalk, true);
    CHECK(o.runWalk && o.feedback == Feedback::Selected && !o.request && r.m.Open() && r.m.Current().page == Page::Wheel);
    o = r.Point(Page::Wheel, kRunWalk, false);
    CHECK(!o.runWalk);                                                            // (a click is one press)
    CHECK(r.m.Selected() == -1);                                                  // (and it picks no slot)
    // what the game says the switch is on now is in the view, and changes it
    CHECK(r.m.Current().run == r.snap.run);
    r.snap.run = kRunning;
    r.Step();
    CHECK(r.m.Current().run == kRunning);
    r.snap.run = kWalking;
    r.Step();
    CHECK(r.m.Current().run == kWalking);
    // the footer's other buttons are where they were put: EDIT SKILLS and CLOSE still answer
    o = r.Point(Page::Wheel, kEdit, true);
    CHECK(r.m.Current().page == Page::PickSlot);
    r.Point(Page::PickSlot, kBack, true);
    o = r.Point(Page::Wheel, kClose, true);
    CHECK(!r.m.Open() && o.feedback == Feedback::Closed);
    // not on the other pages
    r.Step(true);
    r.Point(Page::Wheel, kEdit, true);
    r.AimCell(Page::Wheel, kRunWalk);
    o = r.Step(false, true, r.lu, r.lv);
    CHECK(!o.runWalk);
    // switched off: no control, the old two-button footer, the same point is nothing
    Rig off;
    off.Step(true);
    CHECK(!off.m.Current().runWalk);
    const Rect mid = FooterRect(Page::Wheel, 1, true);
    off.Step(false, false, 0.5f * (mid.l + mid.r), 0.5f * (mid.t + mid.b));
    CHECK(off.m.Current().hover == kNone);
    off.Step(false, true, 0.5f * (mid.l + mid.r), 0.5f * (mid.t + mid.b));
    const Out none = off.Step(false, true, 0.5f * (mid.l + mid.r), 0.5f * (mid.t + mid.b));
    CHECK(!none.runWalk && off.m.Open());
    // no game state: the state is unknown (the control itself is still there)
    Rig stale;
    stale.runWalk = true;
    stale.Step(true);
    stale.snap.run = kRunning;
    stale.session = 99;   // (the frame's session is not the snapshot's: stale)
    stale.Step();
    CHECK(stale.m.Current().run == kRunUnknown);
}

static void TestControllerRunWalk() {
    {   // the whole chain on frames: the chord opens the wheel, the beam on RUN / WALK and a trigger click say "press the game's switch" once
        World w;
        Settings cfg;
        cfg.runWalk = true;   // ([openxr] skill_run_walk=1: it is off by default)
        w.c.Configure(cfg);
        OpenWheel(w);
        CHECK(w.c.Open());
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        w.Next();
        w.AimCell(Page::Wheel, kRunWalk);
        w.Next();
        CHECK(w.c.ViewNow().hover == kRunWalk && w.c.ViewNow().runWalk);
        w.Hand(1, 0.9f, 0, 0);
        Step s = w.Next();
        CHECK(s.runWalk && w.c.Open() && s.feedback == Feedback::Selected);
        s = w.Next();
        CHECK(!s.runWalk);   // (held: no second press)
        w.Hand(1, 0.0f, 0, 0);
        s = w.Next();
        CHECK(!s.runWalk);
    }
    {   // the key off (the default): the same click is nothing
        World w;
        Settings cfg;
        CHECK(!cfg.runWalk && cfg.categories);
        w.c.Configure(cfg);
        OpenWheel(w);
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        w.Next();
        const Rect mid = FooterRect(Page::Wheel, 1, true);
        w.AimAt(0.5f * (mid.l + mid.r), 0.5f * (mid.t + mid.b));
        w.Next();
        w.Hand(1, 0.9f, 0, 0);
        const Step s = w.Next();
        CHECK(!s.runWalk && !w.c.ViewNow().runWalk);
    }
}

// [weight] weapon_grip_no_chord (Controller::SetGripsTaken): a player's Barbarian holds a two-hander by both grips the whole fight ([weight] on). Each case runs as before (0: the
// grips not marked) and with the key (1: the grips marked as the weapon's): before, the held grips made the chords and blocked the bindings; with it, the buttons are the bindings'.
static Step NextWith(World& w, const SettingsIn* host) {
    w.f.counter = ++w.counter;
    w.f.time = w.time += 0.011;
    w.f.displayTime = (int64_t)(w.time * 1e9);
    w.now += 11;
    w.snap.at = w.now;
    return w.c.Frame(w.f, w.snap, w.now, w.reply, true, false, host);
}

static void TestWeaponGrips() {
    {   // the author (2026-10-23): "no combinations with the two grips, they must be ignored" - the plugin passes chordOn from [debug] settings_two_grip_chord (off): both FREE
        // grips held take no stick click and open nothing; the long press is the way in
        World w;
        Settings cfg;
        cfg.style = Style::Tiles;
        cfg.settingsTab = true;
        cfg.openMode = 1;
        w.c.Configure(cfg);
        SettingsIn host;
        host.chordOn = false;
        w.Hand(0, 0, 0.9f, 0);
        w.Hand(1, 0, 0.9f, 0);
        Step s;
        for (int i = 0; i < 50; ++i) s = NextWith(w, &host);
        CHECK(!(s.take[0] & di::kStick) && !(s.take[1] & di::kStick) && !s.wantStore);
        for (int h = 0; h < 2; ++h) {
            w.Hand(h, 0, 0.9f, kStickBit);
            s = NextWith(w, &host);
            CHECK(!w.c.Open() && !(s.take[h] & di::kStick));   // each click is the game's (its binding)
            w.Hand(h, 0, 0.9f, 0);
            NextWith(w, &host);
        }
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        NextWith(w, &host);
        w.f.counter = ++w.counter;
        w.f.time = w.time += 0.011;
        w.now += 11;
        w.snap.at = w.now;
        w.c.Frame(w.f, w.snap, w.now, w.reply, true, true, &host);   // the long press's request
        CHECK(w.c.Open());
    }
    for (int fix = 0; fix < 2; ++fix) {   // the settings window's both-grips chord took BOTH stick clicks: a shout bound to a stick click was dead with the two-hander held
        World w;
        Settings cfg;
        cfg.style = Style::Tiles;
        cfg.settingsTab = true;
        cfg.openMode = 1;   // (the long press opens the panel: the shipped default)
        w.c.Configure(cfg);
        SettingsIn host;
        w.Hand(0, 0, 0.9f, 0);
        w.Hand(1, 0, 0.9f, 0);
        Step s;
        for (int i = 0; i < 50; ++i) {   // 0.55 s: longer than the chord's third of a second
            w.c.SetGripsTaken(fix != 0, fix != 0);
            s = NextWith(w, &host);
        }
        if (!fix) CHECK((s.take[0] & di::kStick) && (s.take[1] & di::kStick));   // (as before: both clicks are the chord's)
        else CHECK(!(s.take[0] & di::kStick) && !(s.take[1] & di::kStick));
        w.Hand(0, 0, 0.9f, kStickBit);   // the left stick click: the player's binding
        w.c.SetGripsTaken(fix != 0, fix != 0);
        s = NextWith(w, &host);
        if (!fix) CHECK(w.c.Open() && (s.take[0] & di::kStick));   // (as before: the click opened the settings window)
        else CHECK(!w.c.Open() && !(s.take[0] & di::kStick));       // the click is the layout's: its binding
    }
    for (int fix = 0; fix < 2; ++fix) {   // the right A as the game's A slot ([openxr] skill_rt_a, the shipped default) holding a shout, the right grip on the weapon
        World w;
        Settings cfg;
        cfg.slotControls = true;
        w.c.Configure(cfg);
        w.snap.slot[8] = w.snap.slot[0];   // the A slot holds a skill
        w.Hand(1, 0, 0.9f, 0);
        Step s;
        for (int i = 0; i < 3; ++i) {
            w.c.SetGripsTaken(false, fix != 0);
            s = w.Next();
        }
        w.Hand(1, 0, 0.9f, di::kPrimary);
        w.c.SetGripsTaken(false, fix != 0);
        s = w.Next();
        if (!fix) CHECK(!s.casting && !(s.buttons & 0x1000));   // (as before: the grip held, the press was not the slot's)
        else CHECK(s.casting && (s.buttons & 0x1000) && (s.take[1] & di::kPrimary));   // the A slot's own press: the shout
    }
    for (int fix = 0; fix < 2; ++fix) {   // the wheel's chord (the left grip + the right stick click; mode 0, and the way in without direct input): the left hand on the shaft
        World w;
        w.Hand(0, 0, 0.9f, 0);
        w.c.SetGripsTaken(fix != 0, false);
        Step s = w.Next();
        if (!fix) CHECK(s.take[1] & di::kStick);   // (as before: from the grip on, the right click was the wheel's)
        else CHECK(!(s.take[1] & di::kStick));
        w.Hand(1, 0, 0, kStickBit);
        w.c.SetGripsTaken(fix != 0, false);
        s = w.Next();
        CHECK(w.c.Open() == !fix);   // with the key the click is the layout's (the swap, or what it is bound to)
    }
    for (int fix = 0; fix < 2; ++fix) {   // a free grip is a modifier as ever: a sword in the right hand (its grip held), the free left grip + the right click open the wheel
        World w;
        w.Hand(0, 0, 0.9f, 0);
        w.Hand(1, 0, 0.9f, 0);
        w.c.SetGripsTaken(false, fix != 0);
        w.Next();
        w.Hand(1, 0, 0.9f, kStickBit);
        w.c.SetGripsTaken(false, fix != 0);
        w.Next();
        CHECK(w.c.Open() == (fix != 0));   // (as before both grips held were the settings' chord, not the wheel's: the weapon's grip blocked it)
    }
}

// The left trigger bound to its own action (directinput::SecondRowOn false): the second row is off - the view greys it (rowOff), the right A as the A slot
// ([openxr] skill_rt_a) takes no second layer from the left trigger, and the left trigger is not taken from its action.
static void TestSecondRowOff() {
    for (int on = 0; on < 2; ++on) {
        World w;
        Settings cfg;
        cfg.slotControls = true;
        w.c.Configure(cfg);
        w.snap.slot[8] = w.snap.slot[10] = w.snap.slot[0];   // A and LT + A hold a skill
        w.Hand(0, 0.9f, 0, 0);                               // the left trigger held
        Step s;
        for (int i = 0; i < 3; ++i) { w.c.SetSecondRow(on != 0); s = w.Next(); }
        CHECK(w.c.ViewNow().rowOff == !on);
        w.Hand(1, 0, 0, di::kPrimary);
        w.c.SetSecondRow(on != 0);
        s = w.Next();
        CHECK(s.slotStarted[0] == (on ? 10 : 8));             // with the row: LT + A; without: A
        CHECK((s.lt == 255) == (on != 0));
        CHECK(((s.take[0] & di::kTakeTrigger) != 0) == (on != 0));   // off: the left trigger stays its action's
    }
}

static void TestControllerLifecycle() {
    // a session change while the gate holds controls: forgotten, nothing taken that is not held
    World w;
    OpenWheel(w);
    w.Hand(0, 0, 0.9f, 0);
    w.Hand(1, 0.9f, 0, kStickBit);
    w.Next();
    CHECK(w.c.Open() || !w.c.Open());   // (either way the next frame is a new session)
    ++w.f.session;
    w.snap.session = w.f.session;
    w.Hand(0, 0, 0, 0);
    w.Hand(1, 0, 0, 0);
    Step s = w.Next();
    CHECK(!w.c.Open() && s.take[0] == 0 && s.take[1] == 0 && !s.casting);
    // Reset while controls are held: nothing is kept taken (the new session's layout has its own gate)
    World r;
    OpenWheel(r);
    r.Hand(1, 0.9f, 0, 0);
    r.Next();
    r.c.Reset();
    r.Hand(0, 0, 0, 0);
    r.Hand(1, 0, 0, 0);
    s = r.Next();
    CHECK(s.take[0] == 0 && s.take[1] == 0 && !r.c.Open());
    // the cast for a left-handed player: the pointing hand is the left one; its trigger casts, the right (modifier) hand's is not touched
    World l;
    Settings cfg;
    cfg.pointerHand = 0;
    l.c.Configure(cfg);
    l.Hand(1, 0, 0.9f, 0);
    l.Next();
    l.Hand(0, 0, 0, kStickBit);
    l.Next();
    CHECK(l.c.Open());
    // pick wedge 0 (B) with the left hand's ray, then cast with the left trigger
    {
        const Pose p = l.c.PanelPose();
        float u, v;
        WedgePoint(0, 0.27f, &u, &v);
        const V3 target = Add(p.p, Rot(p.q, V3{(u - 0.5f) * cfg.widthM, (0.5f - v) * cfg.widthM, 0.0f}));
        const V3 from{l.f.hand[0].aim.pos[0], l.f.hand[0].aim.pos[1], l.f.hand[0].aim.pos[2]};
        const V3 d = Unit(Sub(target, from));
        const V3 z{0, 0, -1};
        V3 axis{z.y * d.z - z.z * d.y, z.z * d.x - z.x * d.z, z.x * d.y - z.y * d.x};
        const float sn = Len(axis), cs = Dot(z, d), ang = std::atan2(sn, cs);
        axis = sn > 1e-6f ? Mul(axis, 1.0f / sn) : V3{0, 1, 0};
        l.f.hand[0].aim.rot[0] = axis.x * std::sin(ang / 2);
        l.f.hand[0].aim.rot[1] = axis.y * std::sin(ang / 2);
        l.f.hand[0].aim.rot[2] = axis.z * std::sin(ang / 2);
        l.f.hand[0].aim.rot[3] = std::cos(ang / 2);
    }
    l.Hand(0, 0, 0, kStickBit);
    l.Next();
    l.Hand(0, 0.9f, 0, kStickBit);
    s = l.Next();
    CHECK(!l.c.Open() && l.c.Selected() == 0);
    l.Hand(0, 0, 0, 0);
    l.Hand(1, 0, 0, 0);
    l.Next();
    l.Next();
    l.Hand(0, 0.9f, 0, 0);
    s = l.Next();
    CHECK(s.casting && s.buttons == kPadB && (s.take[0] & di::kTakeTrigger) && !(s.take[1] & di::kTakeSqueeze));
}

// Random frames in a row (buttons, grips, triggers, flags, poses, a game state coming and going): nothing breaks, and the
// invariants hold - while the wheel is up both triggers are taken, a cast has its trigger taken, nothing is NaN.
static void TestFuzz() {
    uint32_t seed = 12345;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) & 0xFFFF; };
    auto unit = [&]() { return rnd() / 65535.0f; };
    for (int run = 0; run < 4; ++run) {
        World w;
        Settings cfg;
        cfg.pointerHand = run & 1;
        w.c.Configure(cfg);
        int opened = 0, casts = 0, requests = 0;
        for (int i = 0; i < 40000; ++i) {
            if (rnd() % 200 == 0) { w.f.flags = di::kDirect | ((rnd() & 1) ? di::kFocused : 0u) | ((rnd() & 3) == 0 ? di::kInMenu : 0u); ++w.f.epoch; }
            if (rnd() % 997 == 0) { ++w.f.session; w.snap.session = w.f.session; }
            if (rnd() % 50 == 0) w.snap.editable = rnd() & 1;
            if (rnd() % 400 == 0) { w.snap.ready = rnd() & 3; }
            if (rnd() % 300 == 0) { w.f.head.valid = (rnd() & 3) ? 1u : ((rnd() & 1) ? 0u : 2u); }
            for (int h = 0; h < 2; ++h) {
                if (rnd() % 7 == 0) {
                    w.f.hand[h].trigger = (rnd() & 3) == 0 ? 0.0f : unit();
                    w.f.hand[h].squeeze = (rnd() & 3) == 0 ? 0.0f : unit();
                    w.f.hand[h].buttons = rnd() & 0x3F;
                }
                if (rnd() % 5 == 0) {   // the pointing hand sweeps about
                    w.f.hand[h].aim.pos[0] = unit() - 0.5f;
                    w.f.hand[h].aim.pos[1] = 1.0f + unit() * 0.5f;
                    w.f.hand[h].aim.pos[2] = -unit() * 0.6f;
                    float q[4] = {unit() - 0.5f, unit() - 0.5f, unit() - 0.5f, unit()};
                    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
                    for (int k = 0; k < 4; ++k) w.f.hand[h].aim.rot[k] = n > 1e-3f ? q[k] / n : (k == 3 ? 1.0f : 0.0f);
                    w.f.hand[h].aim.valid = rnd() % 8 ? 1 : (rnd() & 1);
                }
            }
            const Step s = w.Next();
            const int P = cfg.pointerHand;
            if (w.c.Open()) {
                ++opened;
                CHECK((s.take[0] & di::kTakeTrigger) && (s.take[1] & di::kTakeTrigger));
                CHECK(!s.casting);
            }
            if (s.casting) {
                ++casts;
                CHECK((s.take[P] & di::kTakeTrigger) != 0);
                CHECK(s.buttons != 0 || s.lt != 0);
                CHECK(!w.c.Open());
            }
            if (s.request) {
                ++requests;
                CHECK(s.req.slot >= 0 && s.req.slot < kSlots && (s.req.op == Op::Assign || s.req.op == Op::Clear));
                w.reply = Reply{s.req.sequence, (rnd() & 1) ? Result::Applied : Result::Refused};
            }
            CHECK(std::isfinite(s.layer.len) && std::isfinite(s.layer.panel.p.x) && std::isfinite(s.layer.dir.x));
            CHECK(w.c.Selected() >= -1 && w.c.Selected() < kSlots);
            CHECK(w.c.ViewNow().hover >= -1 && w.c.ViewNow().hover <= kRunWalk);
        }
        std::printf("  fuzz run %d: %d frames with the wheel up, %d casting, %d requests\n", run, opened, casts, requests);
        CHECK(opened > 0);   // (a run that never opens tests nothing)
    }
}

int main(int argc, char** argv) {
    TestPaint(argc > 2 && std::strcmp(argv[1], "--dump") == 0 ? argv[2] : nullptr);
    TestManifest();
    TestWedges();
    TestCells();
    TestRay();
    TestCompose();
    TestCapture();
    TestApply();
    TestModelFlow();
    TestModelEditor();
    TestModelEditFirst();
    TestCloseWithRequest();
    TestPaging();
    TestCast();
    TestController();
    TestWeaponGrips();
    TestSecondRowOff();
    TestControllerLifecycle();
    TestTrees();
    TestCategoryGeometry();
    TestManifestTrees();
    TestModelCategories();
    TestModelRunWalk();
    TestControllerRunWalk();
    TestFuzz();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
