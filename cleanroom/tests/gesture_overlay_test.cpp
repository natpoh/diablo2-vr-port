// Offline checks of the skill panel's Gestures tab ([openxr] gesture_overlay): the command ring writer (vr/gesture_cmds.h), the pictures'
// geometry (vr/gesture_picture.h: points -> a thumbnail, a motion's arrow, pose joints -> a stick figure, the thought cloud), the screens'
// model against a made-up BodyWalk (vr/gesture_overlay.h), the tab in the window's controller (vr/wheel_controller.h) - there with the key,
// not there without it or outside F4 - and the window's pictures (vr/tiles_paint.cpp; --dump <folder> writes them as BMP files to look at).
// No game, no OpenXR, no headset, no BodyWalk.
// Run: gesture_overlay_test.exe [--dump <folder>]   (exit code 0 = all passed)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "../../vr/bodywalk_start.h"
#include "../../vr/game_ui.h"
#include "../../vr/gesture_engine.h"
#include "../../vr/gesture_engine_lib.h"
#include "../../vr/gesture_cmds.h"
#include "../../vr/gesture_overlay.h"
#include "../../vr/gesture_picture.h"
#include "../../vr/tiles_geom.h"
#include "../../vr/tiles_paint.h"
#include "../../vr/wheel_controller.h"
#include "../../vr/wheel_model.h"
#include "../../vr/walk_path.h"
#include "../../vr/rune_flash.h"
#include "../../vr/near_overlay.h"
#include "../../vr/gesture_play.h"
#include "../../vr/trigger_roles.h"
#include "../../vr/wheel_icons.h"

namespace di = directinput;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

static_assert(gov::kW == tiles::kW && gov::kH == tiles::kH, "the Gestures tab is laid out on the tiles window");

// -- the layout -------------------------------------------------------------------------------------------------------------------------------------

static bool Inside(gov::R a, gov::R o) { return a.l >= o.l && a.t >= o.t && a.r <= o.r && a.b <= o.b; }
static bool Overlap(gov::R a, gov::R b) { return a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b; }
static gov::R FromP(tiles::PRect r) { return {r.l, r.t, r.r, r.b}; }

static void TestLayout() {
    const gov::R panel = FromP(tiles::kPanel);
    // the rows: inside the panel, under the title plate, above the status lines, apart from each other; their parts inside them and apart
    for (int i = 0; i < gov::kRowsPerPage; ++i) {
        const gov::R r = gov::RowRect(i);
        CHECK(Inside(r, panel));
        CHECK(r.t >= tiles::kTitlePlate.b);
        CHECK(r.b <= gov::kNoteLine.t);
        if (i) CHECK(!Overlap(r, gov::RowRect(i - 1)));
        for (int p = 0; p < gov::kParts; ++p) {
            CHECK(Inside(gov::PartRect(i, p), r));
            for (int q = 0; q < p; ++q) CHECK(!Overlap(gov::PartRect(i, p), gov::PartRect(i, q)));
        }
        CHECK(!Overlap(gov::NameRect(i), gov::PartRect(i, gov::kThrLess)));
        CHECK(Inside(gov::ThrTextRect(i), r) && !Overlap(gov::ThrTextRect(i), gov::PartRect(i, gov::kThrLess)) && !Overlap(gov::ThrTextRect(i), gov::PartRect(i, gov::kThrMore)));
    }
    // the lines and the footer are the tiles window's
    CHECK(Near(gov::kNoteLine.t, tiles::kRedLine.t) && Near(gov::kHintLine.t, tiles::kHintLine.t));
    CHECK(Near(gov::kFootT, tiles::kFooterTop) && Near(gov::kFootB, tiles::kFooterBottom));
    for (int n = 1; n <= 4; ++n)
        for (int i = 0; i < n; ++i) {
            CHECK(Inside(gov::FooterRect(i, n), panel));
            if (i) CHECK(!Overlap(gov::FooterRect(i, n), gov::FooterRect(i - 1, n)));
        }
    // the choices and the grid: inside the panel, under the plate, above the lines
    for (int n = 2; n <= 3; ++n)
        for (int i = 0; i < n; ++i) {
            CHECK(Inside(gov::ChoiceRect(i, n), panel) && gov::ChoiceRect(i, n).t > tiles::kTitlePlate.b && gov::ChoiceRect(i, n).b < gov::kNoteLine.t);
            if (i) CHECK(!Overlap(gov::ChoiceRect(i, n), gov::ChoiceRect(i - 1, n)));
        }
    for (int i = 0; i < gov::kGridCount; ++i) CHECK(Inside(gov::GridRect(i), panel) && gov::GridRect(i).b < gov::kNoteLine.t && gov::GridRect(i).t > tiles::kTitlePlate.b);
    CHECK(Inside(gov::kReviewPic, panel) && gov::kReviewLine.b < gov::kNoteLine.t && gov::kRecState.b < gov::kNoteLine.t);
    // three tabs fit the title plate, apart; two are where they always were
    for (int n = 2; n <= 3; ++n)
        for (int i = 0; i < n; ++i) {
            const tiles::PRect r = tiles::TabRect(i, n);
            CHECK(r.l >= tiles::kTitlePlate.l && r.r <= tiles::kTitlePlate.r);
            if (i) CHECK(tiles::TabRect(i - 1, n).r < r.l);
        }
    CHECK(Near(tiles::TabRect(0).l, tiles::TabRect(0, 2).l) && Near(tiles::TabRect(1).r, tiles::TabRect(1, 2).r));
    CHECK(tiles::TabCell(2) == wheel::kTabGestures);
    // the hit test of the tiles window: the third tab only with three; on the Gestures tab nothing but the strip
    const tiles::PRect t3 = tiles::TabRect(2, 3);
    const float u3 = 0.5f * (t3.l + t3.r) / tiles::kW, v3 = 0.5f * (t3.t + t3.b) / tiles::kH;
    CHECK(tiles::HitTest(wheel::Page::Wheel, u3, v3, wheel::kNone, 0, true) != wheel::kTabGestures);
    CHECK(tiles::HitTest(wheel::Page::Wheel, u3, v3, wheel::kNone, 0, true, false, false, false, false, 3) == wheel::kTabGestures);
    const tiles::PRect s0 = tiles::SlotRect(0);
    const float us = 0.5f * (s0.l + s0.r) / tiles::kW, vs = 0.5f * (s0.t + s0.b) / tiles::kH;
    CHECK(tiles::HitTest(wheel::Page::Wheel, us, vs, wheel::kNone, 0, true, false, false, false, false, 3, false) == 0);
    CHECK(tiles::HitTest(wheel::Page::Wheel, us, vs, wheel::kNone, 0, true, false, false, false, false, 3, true) == wheel::kNone);
    CHECK(tiles::HitTest(wheel::Page::Wheel, 70.0f / tiles::kW, 580.0f / tiles::kH, wheel::kNone, 0, true, false, false, false, false, 3, true) == wheel::kNone);   // (no flip arrow there)
}

// -- the command ring ---------------------------------------------------------------------------------------------------------------------------------

// BodyWalk's reader as its contract has it (services/gesture_link_core.h CmdReader): every sequence after the last seen, at most the ring, each once.
struct Reader {
    uint32_t last = 0;
    int Step(const BW_LinkGestureCmds& c, BW_LinkGestureCmd* out, int max) {
        int n = 0;
        uint32_t from = last + 1;
        if (c.lastSequence >= BW_LINK_GESTURECMDS_RING && from < c.lastSequence - BW_LINK_GESTURECMDS_RING + 1) from = c.lastSequence - BW_LINK_GESTURECMDS_RING + 1;
        for (uint32_t seq = from; seq <= c.lastSequence && seq != 0; ++seq) {
            const BW_LinkGestureCmd& e = c.ring[seq % BW_LINK_GESTURECMDS_RING];
            if (e.sequence == seq && n < max) out[n++] = e;
        }
        last = c.lastSequence;
        return n;
    }
};

static void TestCmds() {
    BW_LinkGestureCmds b{};
    gesturecmd::Cmd c;
    c.op = BW_LINK_GC_RECORD;
    c.type = BW_LINK_GT_GLYPH;
    c.hand = BW_LINK_GH_RIGHT;
    c.skill = 47;
    const uint32_t s1 = gesturecmd::Push(b, c);
    CHECK(s1 == 1 && b.lastSequence == 1);
    CHECK(b.ring[1].sequence == 1 && b.ring[1].op == BW_LINK_GC_RECORD && b.ring[1].skill == 47 && b.ring[1].type == BW_LINK_GT_GLYPH && b.ring[1].hand == BW_LINK_GH_RIGHT);
    Reader r;
    BW_LinkGestureCmd got[16];
    CHECK(r.Step(b, got, 16) == 1 && got[0].op == BW_LINK_GC_RECORD);
    CHECK(r.Step(b, got, 16) == 0);   // once
    // ten more: the reader that missed them all gets the newest eight, in order
    for (int i = 0; i < 10; ++i) {
        gesturecmd::Cmd k;
        k.op = BW_LINK_GC_THRESHOLD;
        k.gesture = 5;
        k.value = 50.0f + i;
        gesturecmd::Push(b, k);
    }
    CHECK(b.lastSequence == 11);
    const int n = r.Step(b, got, 16);
    CHECK(n == (int)BW_LINK_GESTURECMDS_RING);
    CHECK(n > 0 && got[0].sequence == 4 && got[n - 1].sequence == 11 && Near(got[n - 1].value, 59.0f));
    // the sequence never takes 0 and wraps
    CHECK(gesturecmd::Next(0xFFFFFFFFu) == 1u && gesturecmd::Next(4) == 5);
    BW_LinkGestureCmds w{};
    w.lastSequence = 0xFFFFFFFFu;
    CHECK(gesturecmd::Push(w, c) == 1u && w.ring[1].sequence == 1u);
    // Seed: a new game continues after the block's and BodyWalk's numbers
    BW_LinkGestureCmds f{};
    gesturecmd::Seed(f, 30, 12);
    CHECK(f.lastSequence == 30);
    gesturecmd::Seed(f, 3, 44);
    CHECK(f.lastSequence == 44);
    CHECK(gesturecmd::Push(f, c) == 45);
    // Done: BodyWalk's cmdDone at or after the sequence, wrap-safe; 0 never
    BW_LinkGestures g{};
    CHECK(!gesturecmd::Done(g, 1));
    g.cmdDone = 45;
    CHECK(gesturecmd::Done(g, 45) && gesturecmd::Done(g, 40) && !gesturecmd::Done(g, 46) && !gesturecmd::Done(g, 0));
    g.cmdDone = 2;
    CHECK(gesturecmd::Done(g, 0xFFFFFFF0u) && !gesturecmd::Done(g, 3));
    CHECK(std::strcmp(gesturecmd::OpName(BW_LINK_GC_DISCARD), "DISCARD") == 0);
}

// -- the pictures ---------------------------------------------------------------------------------------------------------------------------------------

static void TestPictures() {
    const gpic::Box wide{100, 50, 400, 150};   // 300 x 100
    // the fitted square's corners: one scale both ways (the box's short side less the margins), centred, y turned down
    const gpic::P2 tl = gpic::Map(-0.5f, 0.5f, wide, 10), br = gpic::Map(0.5f, -0.5f, wide, 10);
    CHECK(Near(tl.x, 210.0f) && Near(tl.y, 60.0f) && Near(br.x, 290.0f) && Near(br.y, 140.0f));
    const gpic::P2 c = gpic::Map(0, 0, wide, 10);
    CHECK(Near(c.x, 250.0f) && Near(c.y, 100.0f));
    // a polyline: non-finite and far-out points skipped
    float pts[5][2] = {{-0.5f, -0.5f}, {NAN, 0.0f}, {0.0f, 0.5f}, {5.0f, 5.0f}, {0.5f, -0.5f}};
    gpic::P2 out[8];
    CHECK(gpic::Polyline(pts, 5, wide, 10, out, 8) == 3);
    CHECK(Near(out[1].x, 250.0f) && Near(out[1].y, 60.0f));
    CHECK(gpic::Polyline(pts, 5, wide, 10, out, 2) == 2);
    // the arrow head: both strokes behind the end, either side of the path
    const gpic::P2 path[3] = {{0, 0}, {50, 0}, {100, 0}};
    gpic::P2 l, r;
    CHECK(gpic::ArrowHead(path, 3, 20.0f, &l, &r));
    CHECK(l.x < 100.0f && r.x < 100.0f && ((l.y < 0) != (r.y < 0)) && Near(std::hypot(l.x - 100, l.y), 20.0f, 0.01f));
    const gpic::P2 still[2] = {{5, 5}, {5.5f, 5}};
    CHECK(!gpic::ArrowHead(still, 2, 20.0f, &l, &r) && !gpic::ArrowHead(path, 1, 20.0f, &l, &r));

    // the elbow: the two bones as long as the arm's when the hand is in reach; bent outward and down
    {
        const gpic::V3 s{0.17f, 0.47f, 0}, h{0.30f, 0.25f, -0.35f};
        const gpic::V3 e = gpic::Elbow(s, h, 1.0f);
        CHECK(Near(gpic::Len(gpic::Sub(e, s)), gpic::kUpperArm, 1e-3f) && Near(gpic::Len(gpic::Sub(h, e)), gpic::kForearm, 1e-3f));
        const gpic::V3 mid = gpic::Mul(gpic::Add(s, h), 0.5f);
        CHECK(e.y < mid.y + 1e-4f && e.x > mid.x - 1e-4f);
        const gpic::V3 away{1.5f, 0.47f, 0};   // out of reach: straight toward it
        const gpic::V3 e2 = gpic::Elbow(s, away, 1.0f);
        CHECK(Near(e2.y, 0.47f, 0.02f) && e2.x > s.x);
        CHECK(std::isfinite(gpic::Elbow(s, s, 1.0f).x));   // the hand at the shoulder: no NaN
    }
    // the stick figure: right hand up, left hand forward and closed
    {
        float j[5][3] = {{0, 0.72f, 0}, {0, 0.42f, 0}, {0, 0, 0}, {-0.25f, 0.30f, -0.40f}, {0.25f, 1.05f, 0}};
        const float grip[2] = {0.9f, 0.1f};
        const uint32_t all = 0x1F;
        const gpic::Box b{0, 0, 200, 300};
        const gpic::Figure f = gpic::StickFigure(j, all, grip, b, 8);
        CHECK(f.head.y < f.neck.y && f.neck.y < f.pelvis.y && f.pelvis.y < f.foot[0].y && f.pelvis.y < f.foot[1].y);
        CHECK(f.hand[1].y < f.head.y);                   // the raised right hand above the head
        CHECK(f.hand[1].x < f.pelvis.x && f.shoulder[1].x < f.shoulder[0].x);   // the figure faces the viewer: its right on the picture's left
        CHECK(f.fist[0] && !f.fist[1] && f.known[0] && f.known[1]);
        for (const gpic::P2& p : {f.head, f.hand[0], f.hand[1], f.foot[0], f.foot[1], f.elbow[0], f.elbow[1]})
            CHECK(p.x >= b.l && p.x <= b.r && p.y >= b.t && p.y <= b.b);
        CHECK(f.head.y - f.headR >= b.t + 7.0f && std::max(f.foot[0].y, f.foot[1].y) <= b.b - 7.0f);
        // a hand BodyWalk did not have: hanging at the side, below its shoulder
        const gpic::Figure g = gpic::StickFigure(j, all & ~(1u << BW_LINK_J_RHAND), grip, b, 8);
        CHECK(!g.known[1] && g.hand[1].y > g.shoulder[1].y && g.hand[1].y > g.elbow[1].y);
        // nothing valid, NaN joints: a standing figure, finite
        float bad[5][3] = {{NAN, NAN, NAN}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        const gpic::Figure z = gpic::StickFigure(bad, 0x01, nullptr, b, 8);
        CHECK(std::isfinite(z.head.x) && std::isfinite(z.hand[0].y) && z.head.y < z.pelvis.y && !z.fist[0]);
        // leg stubs (the cloud's figure): the same box, the figure bigger, the feet just under the pelvis
        const gpic::Figure stub = gpic::StickFigure(j, all, grip, b, 8, 0.28f);
        CHECK(stub.scale > 1.2f * f.scale && stub.foot[0].y > stub.pelvis.y && stub.foot[0].y - stub.pelvis.y < 0.35f * stub.scale);
        // a box twice as big: the figure twice as big
        const gpic::Figure big = gpic::StickFigure(j, all, grip, gpic::Box{0, 0, 400, 600}, 16);
        CHECK(Near(big.scale, 2.0f * f.scale, 0.02f * f.scale));
    }
    // the thought cloud and the pose picture's layout
    {
        const gpic::Box b{0, 0, 300, 200};
        const gpic::PoseLayout p = gpic::LayoutPose(b);
        CHECK(p.thinker.l >= b.l && p.thinker.b <= b.b && p.cloud.body.r <= b.r && p.cloud.body.t >= b.t);
        CHECK(p.cloud.body.Cx() > p.thinker.Cx() && p.cloud.body.Cy() < p.thinker.Cy());   // up and to its right: in front of it
        const gpic::Cloud& c2 = p.cloud;
        CHECK(c2.inner.l > c2.body.l && c2.inner.r < c2.body.r && c2.inner.t > c2.body.t && c2.inner.b < c2.body.b);
        for (int i = 1; i < gpic::kBubbles; ++i) CHECK(c2.bubbleR[i] > c2.bubbleR[i - 1] && c2.bubble[i].x > c2.bubble[i - 1].x);
        for (int i = 0; i < gpic::kLobes; ++i)   // every bump inside the cloud's box
            CHECK(c2.lobe[i].x - c2.lobeR[i] >= c2.body.l - 0.01f && c2.lobe[i].x + c2.lobeR[i] <= c2.body.r + 0.01f && c2.lobe[i].y - c2.lobeR[i] >= c2.body.t - 0.01f &&
                  c2.lobe[i].y + c2.lobeR[i] <= c2.body.b + 0.01f);
        CHECK(c2.bubble[0].y > c2.bubble[gpic::kBubbles - 1].y);   // rising from the head to the cloud
    }
}

// -- a made-up BodyWalk ---------------------------------------------------------------------------------------------------------------------------------

static BW_LinkGesture G(uint32_t id, uint8_t type, int32_t skill, const char* name) {
    BW_LinkGesture g{};
    g.id = id;
    g.type = type;
    g.hand = BW_LINK_GH_RIGHT;
    g.flags = BW_LINK_GF_RECORDED | BW_LINK_GF_LIVE;
    g.skill = skill;
    g.rgba = gov::kPalette[0];
    g.lastPercent = -1.0f;
    g.threshold = 80.0f;
    std::snprintf(g.name, sizeof g.name, "%s", name);
    g.pointCount = BW_LINK_GESTURE_POINTS;
    for (uint32_t i = 0; i < BW_LINK_GESTURE_POINTS; ++i) {   // a house-ish zig: a triangle on a square
        const float t = float(i) / (BW_LINK_GESTURE_POINTS - 1);
        const float a = t * 6.2831853f;
        g.pts[i][0] = 0.45f * std::sin(a) * (type == BW_LINK_GT_MOTION ? t : 1.0f);
        g.pts[i][1] = 0.45f * std::cos(a * (type == BW_LINK_GT_MOTION ? 0.5f : 1.0f));
    }
    const float j[5][3] = {{0, 0.72f, 0}, {0, 0.42f, 0}, {0, 0, 0}, {-0.22f, 0.25f, -0.38f}, {0.30f, 0.95f, -0.10f}};
    std::memcpy(g.joints, j, sizeof j);
    g.jointValid = 0x1F;
    g.grip[0] = 0.95f;
    g.grip[1] = 0.05f;
    if (type == BW_LINK_GT_BUTTON) std::snprintf(g.button, sizeof g.button, "%s", "Left Y");
    return g;
}

static wheel::Skill Sk(int id, const char* name, int tree) {
    wheel::Skill s;
    s.id = id;
    s.known = true;
    s.available = true;
    s.tree = (int8_t)tree;
    std::snprintf(s.name, sizeof s.name, "%s", name);
    return s;
}

struct Bw {   // the Gestures block as BodyWalk would keep it, and the commands it saw
    BW_LinkGestures g{};
    Bw() {
        std::snprintf(g.setKey, sizeof g.setKey, "%s", "sorceress");
        g.g[0] = G(11, BW_LINK_GT_GLYPH, 36, "Fire Bolt glyph");
        g.g[1] = G(12, BW_LINK_GT_POSE, 47, "Fire Ball pose");
        g.g[2] = G(13, BW_LINK_GT_MOTION, 54, "Teleport swipe");
        g.g[3] = G(14, BW_LINK_GT_BUTTON, 40, "Frozen Armor");
        g.count = 4;
        g.revision = 1;
    }
    void Act(uint32_t seq, uint32_t result = BW_LINK_GR_OK) { g.cmdDone = seq; g.cmdResult = result; }
};

struct Rig {
    gov::Model m;
    Bw bw;
    wheel::Skill catalog[8];
    int catalogCount = 0;
    double t = 1.0;
    bool link = true, have = true, shown = true, canStart = true, triggers = false;
    gov::Missing missing = gov::Avail::NotRunning;
    uint32_t seq = 0;   // the last sequence the "ring" gave
    gov::Out last;
    std::vector<gov::In::ActionOf> actions;   // the game actions the host says the gestures do
    bool actionsOk = true;
    bool handsOk = true;
    bool modesOk = true;
    uint8_t trigRole[2] = {0, 0};
    const char* trigAction[2] = {"", ""};
    std::vector<gov::In::ModeOf> modes;   // the gestures in select mode, as the host says
    std::vector<gov::In::ZoneSize> zones;   // the host's zone sizes
    bool zonesShown = false;
    Rig() {
        catalog[catalogCount++] = Sk(0, "Attack", 0);    // a plain action: never offered
        catalog[catalogCount++] = Sk(36, "Fire Bolt", 1);
        catalog[catalogCount++] = Sk(47, "Fire Ball", 1);
        catalog[catalogCount++] = Sk(54, "Teleport", 2);
        catalog[catalogCount++] = Sk(40, "Frozen Armor", 3);
        catalog[catalogCount++] = Sk(359, "Town Portal", 0);
        catalog[catalogCount++] = Sk(36, "Fire Bolt", 1);   // (an item's copy: one entry)
    }
    gov::Out Step(float x = -1, float y = -1, bool click = false, bool back = false) {
        gov::In in;
        in.time = t += 0.011;
        in.shown = shown;
        in.pointer = x >= 0;
        in.u = x / gov::kW;
        in.v = y / gov::kH;
        in.click = click;
        in.back = back;
        in.link = link;
        in.g = have ? &bw.g : nullptr;
        in.missing = missing;
        in.canStart = canStart;
        in.triggers = triggers;
        in.catalog = catalog;
        in.catalogCount = catalogCount;
        in.zones = zones.empty() ? nullptr : zones.data();
        in.zoneCount = (int)zones.size();
        in.zonesShown = zonesShown;
        in.actionOf = actions.empty() ? nullptr : actions.data();
        in.actionOfCount = (int)actions.size();
        in.actionsOk = actionsOk;
        in.handsOk = handsOk;
        in.modesOk = modesOk;
        in.trigRole[0] = trigRole[0];
        in.trigRole[1] = trigRole[1];
        in.trigAction[0] = trigAction[0];
        in.trigAction[1] = trigAction[1];
        in.modeOf = modes.empty() ? nullptr : modes.data();
        in.modeOfCount = (int)modes.size();
        last = m.Step(in);
        if (last.send) m.Sent(++seq, t);
        return last;
    }
    // aim, pull (the click), let go: a RECORD leaves on the let-go frame - its command is returned with the click's frame
    gov::Out Click(gov::R r) {
        const float x = 0.5f * (r.l + r.r), y = 0.5f * (r.t + r.b);
        Step(x, y);
        triggers = true;
        gov::Out o = Step(x, y, true);
        triggers = false;
        if (!o.send) {
            const gov::Out up = Step(x, y);
            if (up.send) { o.send = true; o.cmd = up.cmd; }
        }
        last = o;
        return o;
    }
    gov::Out ClickCell(int cell) {
        gov::R r;
        if (!gov::CellRect(m.Current(), cell, &r)) { std::printf("  (no rect for cell %d on screen %d)\n", cell, (int)m.Current().screen); return {}; }
        return Click(r);
    }
    const gov::View& V() const { return m.Current(); }
};

static void TestModelList() {
    {   // nothing to manage: the skill link off, no BodyWalk, no hero - the list says why, NEW does nothing
        Rig r;
        r.link = false;
        r.Step();
        CHECK(r.V().avail == gov::Avail::LinkOff && r.V().screen == gov::Screen::List && r.V().rows == 0 && !gov::FooterEnabled(r.V(), gov::kNew));
        r.link = true;
        r.have = false;
        r.Step();
        CHECK(r.V().avail == gov::Avail::NotRunning);
        r.have = true;
        r.bw.g.setKey[0] = 0;
        r.Step();
        CHECK(r.V().avail == gov::Avail::NoHero);
        r.ClickCell(gov::kNew);
        CHECK(r.V().screen == gov::Screen::List && !r.last.send);
    }
    {   // the rows: BodyWalk's gestures with the skills' names from the game; paging past six
        Rig r;
        r.Step();
        CHECK(r.V().avail == gov::Avail::Ok && r.V().rows == 4 && r.V().count == 4 && r.V().pages == 1);
        CHECK(r.V().row[0].g.id == 11 && std::strcmp(r.V().row[0].skill, "Fire Bolt") == 0 && std::strcmp(r.V().row[2].skill, "Teleport") == 0);
        CHECK(std::strcmp(r.V().setKey, "sorceress") == 0);
        CHECK(!r.m.WantsBack());
        for (uint32_t i = 4; i < 9; ++i) r.bw.g.g[i] = G(20 + i, BW_LINK_GT_GLYPH, 99, "more");   // (99: no such skill now - its number)
        r.bw.g.count = 9;
        r.Step();
        CHECK(r.V().pages == 2 && r.V().rows == 6);
        r.ClickCell(gov::kNext);
        CHECK(r.V().page == 1 && r.V().rows == 3 && std::strcmp(r.V().row[0].skill, "#99") == 0);
        r.ClickCell(gov::kNext);   // (no page after: refused)
        CHECK(r.V().page == 1 && r.last.feedback == gov::Feedback::Refused);
        r.ClickCell(gov::kPrev);
        CHECK(r.V().page == 0);
        // the revision moves with what is shown, not every frame
        r.Step();
        const uint32_t rev = r.V().revision;
        r.Step();
        CHECK(r.V().revision == rev);
        r.Step(gov::PicRect(0).l + 5, gov::PicRect(0).t + 5);
        CHECK(r.V().revision != rev && r.V().hover == gov::RowCell(0, gov::kPic) && r.last.feedback == gov::Feedback::Hover);
    }
    {   // a row's parts: colour, threshold, clear (asks again), bind; a button gesture has no re-record and no threshold
        Rig r;
        r.Step();
        // the chip by the name: the Colour screen, a swatch sets the ray's colour (SetColor) and the list is back
        gov::Out o = r.ClickCell(gov::RowCell(0, gov::kColour));
        CHECK(!o.send && r.V().screen == gov::Screen::Colour && r.V().haveColour && r.V().colour.g.id == 11 && r.m.WantsBack());
        o = r.ClickCell(gov::kSwatch0 + 1);
        CHECK(o.send && o.cmd.op == BW_LINK_GC_COLOR && o.cmd.gesture == 11 && o.cmd.rgba == gov::kPalette[1] && r.V().screen == gov::Screen::List);
        r.bw.Act(r.seq);
        r.Step();
        CHECK(r.V().note == gov::Note::Coloured && !r.V().waiting);
        r.ClickCell(gov::RowCell(2, gov::kColour));   // (a motion has a ray too; back steps out, nothing asked)
        CHECK(r.V().screen == gov::Screen::Colour);
        o = r.Step(-1, -1, false, true);
        CHECK(r.V().screen == gov::Screen::List && !o.send);
        o = r.ClickCell(gov::RowCell(0, gov::kThrLess));
        CHECK(o.send && o.cmd.op == BW_LINK_GC_THRESHOLD && Near(o.cmd.value, 75.0f));
        // while BodyWalk has not acted, nothing else is asked
        o = r.ClickCell(gov::RowCell(0, gov::kThrMore));
        CHECK(!o.send && o.feedback == gov::Feedback::Refused && r.V().waiting);
        r.bw.Act(r.seq);
        r.Step();
        o = r.ClickCell(gov::RowCell(0, gov::kThrMore));
        CHECK(o.send && Near(o.cmd.value, 85.0f));
        r.bw.Act(r.seq);
        r.Step();
        // Clear: the first click arms it, the second clears
        o = r.ClickCell(gov::RowCell(1, gov::kRowClear));
        CHECK(!o.send && r.V().clearArmed == 12 && r.V().note == gov::Note::ClearAgain);
        o = r.ClickCell(gov::RowCell(1, gov::kRowClear));
        CHECK(o.send && o.cmd.op == BW_LINK_GC_CLEAR && o.cmd.gesture == 12 && r.V().clearArmed == 0);
        r.bw.Act(r.seq);
        r.Step();
        // armed, then three seconds: it asks anew
        r.ClickCell(gov::RowCell(2, gov::kRowClear));
        CHECK(r.V().clearArmed == 13);
        for (int i = 0; i < 300; ++i) r.Step();
        CHECK(r.V().clearArmed == 0);
        // the button gesture: no re-record, no threshold arrows, no colour
        CHECK(!gov::PartUsable(r.V().row[3].g, gov::kRowRerecord) && !gov::PartUsable(r.V().row[3].g, gov::kThrLess) && !gov::PartUsable(r.V().row[3].g, gov::kPic));
        CHECK(gov::HitTest(r.V(), (gov::PartRect(3, gov::kRowRerecord).l + 5) / gov::kW, (gov::PartRect(3, gov::kRowRerecord).t + 5) / gov::kH, gov::kNone) == gov::kNone);
        CHECK(!gov::PartUsable(r.V().row[1].g, gov::kColour) && !gov::PartUsable(r.V().row[3].g, gov::kColour));   // (a pose, a button: no ray colour)
        CHECK(gov::PartUsable(r.V().row[1].g, gov::kPic) && gov::PartUsable(r.V().row[0].g, gov::kPic));   // (their pictures play them)
        // Bind: the grid of the class's skills (no plain action, one entry per id, the trees first), the pick binds; No skill unbinds
        r.ClickCell(gov::RowCell(0, gov::kRowBind));
        CHECK(r.V().screen == gov::Screen::BindSkill && r.V().options == 5 && r.V().bindSkill == 36 && r.m.WantsBack());
        CHECK(r.V().option[0].id == 36 && r.V().option[3].id == 40 && r.V().option[4].id == 359);
        o = r.ClickCell(gov::kGrid0 + 2);
        CHECK(o.send && o.cmd.op == BW_LINK_GC_BIND && o.cmd.gesture == 11 && o.cmd.skill == 54 && r.V().screen == gov::Screen::List);
        r.bw.Act(r.seq);
        r.Step();
        CHECK(r.V().note == gov::Note::Bound);
        r.ClickCell(gov::RowCell(0, gov::kRowBind));
        o = r.ClickCell(gov::kNoSkill);
        CHECK(o.send && o.cmd.op == BW_LINK_GC_BIND && o.cmd.skill == -1);
        r.bw.Act(r.seq);
        r.Step();
        // back from the grid: the list
        r.ClickCell(gov::RowCell(0, gov::kRowBind));
        r.Step(-1, -1, false, true);
        CHECK(r.V().screen == gov::Screen::List && !r.last.send);
    }
    {   // BodyWalk refuses, or does not answer
        Rig r;
        r.Step();
        r.ClickCell(gov::RowCell(0, gov::kThrLess));
        r.bw.Act(r.seq, BW_LINK_GR_BUSY);
        r.Step();
        CHECK(r.V().note == gov::Note::Busy && r.last.feedback == gov::Feedback::Refused && !r.V().waiting);
        r.ClickCell(gov::RowCell(0, gov::kThrLess));
        for (int i = 0; i < 600; ++i) r.Step();   // 6.6 s (BodyWalk is given kAnswerS)
        CHECK(r.V().note == gov::Note::NoAnswer && !r.V().waiting);
    }
    {   // another class: back to the list, from its start
        Rig r;
        r.Step();
        r.ClickCell(gov::kNew);
        CHECK(r.V().screen == gov::Screen::NewType);
        std::snprintf(r.bw.g.setKey, sizeof r.bw.g.setKey, "%s", "barbarian");
        r.Step();
        CHECK(r.V().screen == gov::Screen::List && std::strcmp(r.V().setKey, "barbarian") == 0);
    }
}

static void TestMissing() {
    {   // the skill link off: ENABLE, right where the list would be
        Rig r;
        r.link = false;
        r.Step();
        CHECK(r.V().avail == gov::Avail::LinkOff && gov::FixFor(r.V().avail, r.V().canStart) == gov::Fix::EnableLink);
        CHECK(gov::HitTest(r.V(), 0.5f * (gov::kFixRect.l + gov::kFixRect.r) / gov::kW, 0.5f * (gov::kFixRect.t + gov::kFixRect.b) / gov::kH, gov::kNone) == gov::kFix);
        const gov::Out o = r.ClickCell(gov::kFix);
        CHECK(o.fix == gov::Fix::EnableLink && !o.send && o.feedback == gov::Feedback::Chosen);
        r.link = true;   // (the host wrote it; the plugin took the ini; BodyWalk answers)
        r.Step();
        CHECK(r.V().avail == gov::Avail::Ok && r.V().rows == 4);
    }
    {   // BodyWalk not running: START BODYWALK when a copy was found; none found: no button, where to get one
        Rig r;
        r.have = false;
        r.missing = gov::Avail::NotRunning;
        r.Step();
        gov::Out o = r.ClickCell(gov::kFix);
        CHECK(o.fix == gov::Fix::StartBodyWalk && r.V().avail == gov::Avail::NotRunning && r.V().canStart);
        r.missing = gov::Avail::Starting;   // started: no button while it comes up
        r.Step();
        CHECK(r.V().avail == gov::Avail::Starting && gov::FixFor(r.V().avail, true) == gov::Fix::None);
        CHECK(gov::HitTest(r.V(), 0.5f * (gov::kFixRect.l + gov::kFixRect.r) / gov::kW, 0.5f * (gov::kFixRect.t + gov::kFixRect.b) / gov::kH, gov::kNone) == gov::kNone);
        r.missing = gov::Avail::NotRunning;
        r.canStart = false;
        r.Step();
        CHECK(!r.V().canStart && gov::FixFor(r.V().avail, r.V().canStart) == gov::Fix::None);
        o = r.ClickCell(gov::kFix);
        CHECK(o.fix == gov::Fix::None);
    }
    {   // BodyWalk runs but does not answer / takes no skills / writes no gestures: said which, no button (nothing to switch in BodyWalk)
        for (gov::Missing m : {gov::Avail::NoGameLink, gov::Avail::NoSkills, gov::Avail::NoGestures}) {
            Rig r;
            r.have = false;
            r.missing = m;
            r.Step();
            CHECK(r.V().avail == m && gov::FixFor(r.V().avail, true) == gov::Fix::None && !gov::FooterEnabled(r.V(), gov::kNew));
        }
        Rig r;   // a nonsense reason from the host: taken as not running
        r.have = false;
        r.missing = gov::Avail::Ok;
        r.Step();
        CHECK(r.V().avail == gov::Avail::NotRunning);
        r.missing = gov::Avail::NoHero;
        r.Step();
        CHECK(r.V().avail == gov::Avail::NotRunning);
    }
    {   // the Steam ids out of BodyWalk's environment, the rest kept, the block well formed
        const wchar_t env[] = L"PATH=C:/d\0SteamAppId=2536520\0Foo=1\0steamgameid=2536520\0SteamOverlayGameId=9\0Bar=2\0";
        const std::wstring b = bwstart::WithoutSteamIds(env);
        CHECK(b == std::wstring(L"PATH=C:/d\0Foo=1\0Bar=2\0\0", 23));
        CHECK(bwstart::WithoutSteamIds(nullptr) == std::wstring(L"\0\0", 2));
        const wchar_t only[] = L"SteamAppId=1\0";
        CHECK(bwstart::WithoutSteamIds(only) == std::wstring(L"\0\0", 2));
    }
}

static void TestModelRecord() {
    {   // a new glyph: type -> hand -> skill -> RECORD; READY, DRAWING (blind: no hover, no click), SAVED -> review -> Keep
        Rig r;
        r.Step();
        r.ClickCell(gov::kNew);
        CHECK(r.V().screen == gov::Screen::NewType && r.V().choices == 3 && r.m.WantsBack());
        r.ClickCell(gov::kChoice0 + 0);
        CHECK(r.V().screen == gov::Screen::NewHand && r.V().choices == 3 && r.V().newType == BW_LINK_GT_GLYPH);   // (Right, Left, Either)
        r.Step(-1, -1, false, true);   // back: the type again
        CHECK(r.V().screen == gov::Screen::NewType);
        r.ClickCell(gov::kChoice0 + 1);   // a hand pose: three hands
        CHECK(r.V().screen == gov::Screen::NewHand && r.V().choices == 3 && r.V().newType == BW_LINK_GT_POSE);
        r.Step(-1, -1, false, true);
        r.ClickCell(gov::kChoice0 + 0);
        r.ClickCell(gov::kChoice0 + 0);   // right (the top one)
        CHECK(r.V().screen == gov::Screen::NewSkill && r.V().newHand == BW_LINK_GH_RIGHT && r.V().options == 5);
        gov::Out o = r.ClickCell(gov::kGrid0 + 1);   // Fire Ball
        CHECK(o.send && o.cmd.op == BW_LINK_GC_RECORD && o.cmd.gesture == 0 && o.cmd.type == BW_LINK_GT_GLYPH && o.cmd.hand == BW_LINK_GH_RIGHT && o.cmd.skill == 47);
        CHECK(r.V().screen == gov::Screen::Recording && r.m.Recording() && r.V().live && r.V().waiting);
        CHECK(std::strcmp(r.V().recSkill, "Fire Ball") == 0 && r.V().recType == BW_LINK_GT_GLYPH);
        // an old SAVED in the block before BodyWalk took our RECORD is not ours
        r.bw.g.recordState = BW_LINK_REC_SAVED;
        r.bw.g.recordGesture = 11;
        r.bw.g.g[0].flags |= BW_LINK_GF_REVIEW;
        r.Step();
        CHECK(r.V().screen == gov::Screen::Recording);
        r.bw.g.g[0].flags &= ~BW_LINK_GF_REVIEW;
        // BodyWalk takes it: READY
        r.bw.Act(r.seq);
        r.bw.g.recordState = BW_LINK_REC_READY;
        r.bw.g.recordGesture = 0;
        r.Step();
        CHECK(r.V().screen == gov::Screen::Recording && r.m.Recording() && !r.V().waiting && r.V().recordState == BW_LINK_REC_READY);
        // blind: the beam on the footer's Stop, a pull - nothing (the pull is BodyWalk's pen)
        o = r.ClickCell(gov::kStop);
        CHECK(!o.send && r.V().hover == gov::kNone && r.V().screen == gov::Screen::Recording);
        r.bw.g.recordState = BW_LINK_REC_DRAWING;
        r.Step();
        CHECK(r.V().recordState == BW_LINK_REC_DRAWING && r.m.Recording());
        // the take: a new gesture in review
        r.bw.g.g[4] = G(31, BW_LINK_GT_GLYPH, 47, "Fire Ball");
        r.bw.g.g[4].flags = BW_LINK_GF_REVIEW;
        r.bw.g.count = 5;
        r.bw.g.recordGesture = 31;
        r.bw.g.recordState = BW_LINK_REC_SAVED;
        r.Step();
        CHECK(r.V().screen == gov::Screen::Review && r.V().haveReview && r.V().review.g.id == 31 && !r.m.Recording() && r.last.feedback == gov::Feedback::Applied);
        CHECK(std::strcmp(r.V().review.skill, "Fire Ball") == 0);
        r.Step(-1, -1, false, true);   // back: it asks for a choice, stays
        CHECK(r.V().screen == gov::Screen::Review && r.V().note == gov::Note::ChooseReview && r.m.WantsBack());
        o = r.ClickCell(gov::kKeep);
        CHECK(o.send && o.cmd.op == BW_LINK_GC_KEEP && o.cmd.gesture == 31 && r.V().screen == gov::Screen::List);
        r.bw.g.g[4].flags = BW_LINK_GF_RECORDED | BW_LINK_GF_LIVE;
        r.bw.Act(r.seq);
        r.Step();
        CHECK(r.V().note == gov::Note::Kept && r.V().rows == 5);
    }
    {   // re-record from the list; Re-record and Discard from the review
        Rig r;
        r.Step();
        gov::Out o = r.ClickCell(gov::RowCell(2, gov::kRowRerecord));
        CHECK(o.send && o.cmd.op == BW_LINK_GC_RECORD && o.cmd.gesture == 13 && o.cmd.type == BW_LINK_GT_MOTION);
        CHECK(r.V().screen == gov::Screen::Recording && std::strcmp(r.V().recName, "Teleport swipe") == 0);
        r.bw.Act(r.seq);
        r.bw.g.recordState = BW_LINK_REC_READY;
        r.bw.g.recordGesture = 13;
        r.Step();
        r.bw.g.recordState = BW_LINK_REC_SAVED;
        r.bw.g.g[2].flags |= BW_LINK_GF_REVIEW;
        r.Step();
        CHECK(r.V().screen == gov::Screen::Review);
        o = r.ClickCell(gov::kRerecord);
        CHECK(o.send && o.cmd.op == BW_LINK_GC_RECORD && o.cmd.gesture == 13 && r.V().screen == gov::Screen::Recording);
        r.bw.Act(r.seq);
        r.bw.g.recordState = BW_LINK_REC_READY;
        r.Step();
        r.bw.g.recordState = BW_LINK_REC_SAVED;
        r.Step();
        o = r.ClickCell(gov::kDiscard);
        CHECK(o.send && o.cmd.op == BW_LINK_GC_DISCARD && o.cmd.gesture == 13 && r.V().screen == gov::Screen::List);
    }
    {   // the back key while recording: STOP; the window going away while recording: STOP
        Rig r;
        r.Step();
        r.ClickCell(gov::RowCell(0, gov::kRowRerecord));
        r.bw.Act(r.seq);
        r.bw.g.recordState = BW_LINK_REC_DRAWING;
        r.bw.g.recordGesture = 11;
        r.Step();
        gov::Out o = r.Step(-1, -1, false, true);
        CHECK(o.send && o.cmd.op == BW_LINK_GC_STOP && o.cmd.gesture == 11 && r.V().screen == gov::Screen::List && !r.m.Recording());
        r.bw.Act(r.seq);
        r.bw.g.recordState = BW_LINK_REC_CANCELLED;
        r.Step();
        CHECK(r.V().note == gov::Note::Stopped);
        r.ClickCell(gov::RowCell(0, gov::kRowRerecord));
        r.bw.Act(r.seq);
        r.bw.g.recordState = BW_LINK_REC_READY;
        r.Step();
        CHECK(r.m.Recording());
        r.shown = false;
        o = r.Step();
        CHECK(o.send && o.cmd.op == BW_LINK_GC_STOP && r.V().screen == gov::Screen::List && !r.m.Recording());
    }
    {   // a failed take: Try again (RECORD of the same) or Back; a RECORD BodyWalk refuses or does not answer: Try again / Back too, not the list
        Rig r;
        r.Step();
        r.ClickCell(gov::kNew);
        r.ClickCell(gov::kChoice0 + 2);   // a body motion
        r.ClickCell(gov::kChoice0 + 1);   // left (the second one)
        r.ClickCell(gov::kGrid0 + 0);
        CHECK(r.last.cmd.type == BW_LINK_GT_MOTION && r.last.cmd.hand == BW_LINK_GH_LEFT && r.last.cmd.skill == 36);
        r.bw.Act(r.seq);
        r.bw.g.recordState = BW_LINK_REC_READY;
        r.Step();
        r.bw.g.recordState = BW_LINK_REC_FAILED;
        r.bw.g.recordGesture = 0;
        r.Step();
        CHECK(r.V().failed && !r.m.Recording() && r.V().note == gov::Note::Failed);
        gov::Out o = r.ClickCell(gov::kRetry);
        CHECK(o.send && o.cmd.op == BW_LINK_GC_RECORD && o.cmd.gesture == 0 && o.cmd.type == BW_LINK_GT_MOTION && o.cmd.skill == 36 && r.m.Recording() && !r.V().failed);
        r.bw.Act(r.seq, BW_LINK_GR_BUSY);
        r.Step();
        CHECK(r.V().screen == gov::Screen::Recording && r.V().failed && r.V().note == gov::Note::Busy);
        r.ClickCell(gov::kBack);
        CHECK(r.V().screen == gov::Screen::List);
        // no answer to a RECORD: Try again / Back after kAnswerS
        r.ClickCell(gov::RowCell(0, gov::kRowRerecord));
        CHECK(r.m.Recording());
        for (int i = 0; i < 300; ++i) r.Step();   // 3.3 s: still waiting
        CHECK(r.V().screen == gov::Screen::Recording && r.m.Recording());
        for (int i = 0; i < 300; ++i) r.Step();   // 6.6 s
        CHECK(r.V().screen == gov::Screen::Recording && r.V().failed && !r.m.Recording() && r.V().note == gov::Note::NoAnswer);
    }
    {   // the last stroke read: its gesture and percent for the hint line
        Rig r;
        r.bw.g.lastStamp = 77;
        r.bw.g.lastGesture = 12;
        r.bw.g.lastPercent = 91.5f;
        r.Step();
        CHECK(r.V().lastShown && std::strcmp(r.V().lastName, "Fire Ball pose") == 0 && Near(r.V().lastPercent, 91.5f));
    }
}

// -- the whole flow against a BodyWalk that behaves as the real one did (Serg's run, 0.206.8 + BodyWalk 1.86, 2026-11-07) ----------------------------
//
// FakeBw follows skill-gestures-184's gesture_service.cpp / game_skills.cpp: its command reader primes on the first look at a written block and takes
// what it finds then as old; RECORD arms a take (READY) in the same write as cmdDone; the glyph pen is the trigger HELD (level): held -> DRAWING, let
// go -> SAVED (enough points: the gesture in REVIEW) or FAILED (too short - a new gesture's row is erased, and recordGesture then names whatever row
// moved into its place); the state of the last take stays in the block until the next RECORD; it may tick slowly (a minimised BodyWalk).

struct FakeBw {
    struct G2 { uint32_t id; uint8_t type, hand; int32_t skill; bool recorded, review; };
    std::vector<G2> rows;
    uint32_t nextId = 500;
    // the reader (CmdReader)
    bool primed = false;
    uint32_t last = 0;
    // the take
    bool active = false, started = false, madeNew = false;
    int idx = -1, points = 0;
    uint32_t state = BW_LINK_REC_NONE;
    int32_t skill = -1;
    uint32_t cmdDone = 0, cmdResult = 0;
    int every = 1, frame = 0;   // acts once every `every` frames
    bool stall = false;         // writes nothing at all (Gestures still)
    FakeBw() {
        rows.push_back({11, BW_LINK_GT_GLYPH, 36, true, false});
        rows.push_back({12, BW_LINK_GT_GLYPH, 47, true, false});
    }
    int Find(uint32_t id) const {
        for (int i = 0; i < (int)rows.size(); ++i)
            if (rows[i].id == id) return i;
        return -1;
    }
    void EndTake() {
        const bool ok = idx >= 0 && idx < (int)rows.size() && points > 10;
        state = ok ? BW_LINK_REC_SAVED : started ? BW_LINK_REC_FAILED : BW_LINK_REC_CANCELLED;
        if (ok) rows[idx].review = true;
        else if (madeNew && idx >= 0 && idx < (int)rows.size()) rows.erase(rows.begin() + idx);   // (idx kept: the real one's)
        active = false;
    }
    uint32_t Do(const BW_LinkGestureCmd& c) {
        switch (c.op) {
        case BW_LINK_GC_RECORD: {
            if (active) EndTake();
            int i = -1;
            if (c.gesture) {
                i = Find(c.gesture);
                if (i < 0) return BW_LINK_GR_NO_GESTURE;
                madeNew = false;
            } else {
                rows.push_back({nextId++, c.type, c.hand, c.skill, false, false});
                i = (int)rows.size() - 1;
                madeNew = true;
            }
            idx = i;
            active = true;
            started = false;
            points = 0;
            skill = rows[i].skill;
            state = BW_LINK_REC_READY;
            return BW_LINK_GR_OK;
        }
        case BW_LINK_GC_STOP: if (active) EndTake(); return BW_LINK_GR_OK;
        case BW_LINK_GC_KEEP:
            for (G2& r : rows) if (r.review) { r.review = false; r.recorded = true; }
            return BW_LINK_GR_OK;
        case BW_LINK_GC_DISCARD:
            for (int i = 0; i < (int)rows.size(); ++i)
                if (rows[i].review) {
                    if (madeNew) rows.erase(rows.begin() + i);
                    else rows[i].review = false;
                    break;
                }
            return BW_LINK_GR_OK;
        default: return BW_LINK_GR_OK;
        }
    }
    // One BodyWalk tick: the commands (if the block was ever written), the pen, the block out. `trigger`: the take's hand's trigger held.
    void Tick(const BW_LinkGestureCmds& ring, bool written, bool trigger, BW_LinkGestures* out) {
        if (stall || ++frame % every) return;
        if (written) {
            if (!primed) {   // the first look: what is there is old
                primed = true;
                last = ring.lastSequence;
            } else {
                for (uint32_t seq = last + 1; seq <= ring.lastSequence; ++seq) {
                    const BW_LinkGestureCmd& e = ring.ring[seq % BW_LINK_GESTURECMDS_RING];
                    if (e.sequence != seq) continue;
                    cmdResult = Do(e);
                    cmdDone = seq;
                }
                last = ring.lastSequence;
            }
        }
        if (active && rows[idx].type == BW_LINK_GT_GLYPH) {   // the pen: held = drawing
            if (trigger) {
                state = BW_LINK_REC_DRAWING;
                started = true;
                ++points;
            } else if (state == BW_LINK_REC_DRAWING) {
                EndTake();
            }
        }
        std::memset(out, 0, sizeof *out);
        out->header.magic = BW_LINK_MAGIC;
        std::snprintf(out->setKey, sizeof out->setKey, "%s", "sorceress");
        out->count = (uint32_t)rows.size();
        for (size_t i = 0; i < rows.size(); ++i) {
            BW_LinkGesture& g = out->g[i];
            g = G(rows[i].id, rows[i].type, rows[i].skill, "g");
            g.hand = rows[i].hand;
            g.flags = uint8_t((rows[i].recorded ? BW_LINK_GF_RECORDED | BW_LINK_GF_LIVE : 0) | (rows[i].review ? BW_LINK_GF_REVIEW : 0));
        }
        out->cmdDone = cmdDone;
        out->cmdResult = cmdResult;
        out->recordState = state;
        out->recordGesture = idx >= 0 && idx < (int)rows.size() ? rows[idx].id : 0;   // (the real one's: idx after an erase names another row)
        out->recordSkill = skill;
    }
};

// The game's side: the model, the ring it writes (as vr/link.cpp does: primed or not), the triggers; one frame = model step + BodyWalk tick.
struct Flow {
    gov::Model m;
    FakeBw bw;
    BW_LinkGestures block{};
    BW_LinkGestureCmds ring{};
    bool written = false, prime = true, seeded = false;
    bool trigger = false;      // the pointing hand's (the click's)
    bool pen = false;          // the take's hand's, as the player draws
    bool shown = true, have = true;
    double t = 1.0;
    float u = -1, v = -1;
    wheel::Skill catalog[3];
    gov::Out last;
    int sent = 0;
    std::vector<uint32_t> ops;   // every command the game sent
    std::vector<std::string> traces;   // [debug] gesture_trace: the model's lines
    bool Traced(const char* part) const {
        for (const std::string& line : traces)
            if (line.find(part) != std::string::npos) return true;
        return false;
    }
    Flow() {
        catalog[0] = Sk(36, "Fire Bolt", 1);
        catalog[1] = Sk(47, "Fire Ball", 1);
        catalog[2] = Sk(54, "Teleport", 2);
        bw.Tick(ring, written, false, &block);
    }
    void Frame(bool click = false) {
        if (prime && !written && have) written = true;   // (link::PrimeGestureCmds: the empty ring out as soon as BodyWalk's Gestures are there)
        gov::In in;
        in.time = t += 0.011;
        in.shown = shown;
        in.pointer = u >= 0;
        in.u = u / gov::kW;
        in.v = v / gov::kH;
        in.click = click;
        in.link = true;
        in.g = have ? &block : nullptr;
        in.missing = gov::Avail::NoGestures;
        in.triggers = trigger || pen;
        in.catalog = catalog;
        in.catalogCount = 3;
        last = m.Step(in);
        for (int i = 0; i < last.traces; ++i) traces.push_back(last.trace[i]);
        if (last.send) {
            if (!seeded) { gesturecmd::Seed(ring, written ? ring.lastSequence : 0, block.cmdDone); seeded = true; }
            m.Sent(gesturecmd::Push(ring, last.cmd), t);
            written = true;
            ops.push_back(last.cmd.op);
            ++sent;
        }
        bw.Tick(ring, written, pen, &block);
    }
    void Run(int frames) { for (int i = 0; i < frames; ++i) Frame(); }
    void At(gov::R r) { u = 0.5f * (r.l + r.r); v = 0.5f * (r.t + r.b); }
    // aim, pull, hold `holdFrames`, let go
    void Click(int cell, int holdFrames = 2) {
        gov::R r;
        if (!gov::CellRect(m.Current(), cell, &r)) { std::printf("  (flow: no rect for cell %d on screen %d)\n", cell, (int)m.Current().screen); return; }
        At(r);
        Frame();
        trigger = true;
        Frame(true);
        for (int i = 0; i < holdFrames; ++i) Frame();
        trigger = false;
        Frame();
        u = v = -1;
    }
    const gov::View& V() const { return m.Current(); }
    // NEW GESTURE -> Glyph -> Right -> the skill
    void NewGlyph(int skillCell = gov::kGrid0 + 0, int holdFrames = 2) {
        Click(gov::kNew);
        Click(gov::kChoice0 + 0);
        Click(gov::kChoice0 + 0);
        Click(skillCell, holdFrames);
    }
    void Draw(int frames) {
        pen = true;
        Run(frames);
        pen = false;
        Run(3);
    }
};

static void TestFlowFakeBodyWalk() {
    {   // the whole way, BodyWalk prompt: list -> new -> type -> hand -> skill -> READY -> DRAWING -> SAVED -> review -> Keep -> the list with it
        Flow f;
        f.Run(3);
        f.NewGlyph();
        CHECK(f.V().screen == gov::Screen::Recording && f.sent == 1 && f.ops[0] == BW_LINK_GC_RECORD);
        f.Run(2);
        CHECK(f.V().recordState == BW_LINK_REC_READY && f.m.Recording() && !f.V().failed);
        f.pen = true;
        f.Run(20);
        CHECK(f.V().recordState == BW_LINK_REC_DRAWING && f.m.Recording());
        f.pen = false;
        f.Run(3);
        CHECK(f.V().screen == gov::Screen::Review && f.V().haveReview && f.V().review.g.id == 500);
        f.Click(gov::kKeep);
        f.Run(3);
        CHECK(f.V().screen == gov::Screen::List && f.V().count == 3 && f.V().note == gov::Note::Kept && f.bw.rows[2].recorded);
    }
    {   // 1. the first command of a game that never wrote the ring was lost (BodyWalk's first look took it as old): now the ring is written empty first
        Flow old;
        old.prime = false;   // as 0.206.8 did
        old.Run(3);
        old.NewGlyph();
        old.Run(100);
        CHECK(old.bw.cmdDone == 0 && old.V().recordState == BW_LINK_REC_NONE);   // swallowed: no answer
        Flow f;   // primed
        f.Run(3);
        f.NewGlyph();
        f.Run(2);
        CHECK(f.bw.cmdDone == 1 && f.V().recordState == BW_LINK_REC_READY);
        // and with no answer at all the flow is not thrown to the list: Try again / Back, the same RECORD again
        old.Run(560);   // past kAnswerS
        CHECK(old.V().screen == gov::Screen::Recording && old.V().failed && old.V().note == gov::Note::NoAnswer);
        // gesture_trace: the timeout and the screen changes are told
        CHECK(old.Traced("TIMEOUT: no answer to #1 (RECORD)") && old.Traced("screen 'new: skill' -> 'recording'") && old.Traced("click on cell"));
        CHECK(f.Traced("the triggers are up: the RECORD goes out") && f.Traced("answer to #1 (RECORD): result 0") && f.Traced("BodyWalk's recording: ready"));
        old.Click(gov::kRetry);
        old.Run(2);
        CHECK(old.sent == 2 && old.bw.cmdDone == 2 && old.V().recordState == BW_LINK_REC_READY && !old.V().failed);
    }
    {   // 2. the pull that clicked the skill tile is not the pen: the RECORD waits until it is let go, so no take starts by itself
        Flow f;
        f.Run(3);
        f.Click(gov::kNew);
        f.Click(gov::kChoice0 + 0);
        f.Click(gov::kChoice0 + 0);
        gov::R r;
        gov::CellRect(f.V(), gov::kGrid0 + 0, &r);
        f.At(r);
        f.Frame();
        f.trigger = true;
        f.pen = true;   // (the click's hand is the pen's hand: the same trigger)
        f.Frame(true);
        f.Run(40);      // held 0.4 s on the tile
        CHECK(f.sent == 0 && f.V().screen == gov::Screen::Recording && f.V().queued && f.m.Recording() && f.bw.state == BW_LINK_REC_NONE);
        f.trigger = false;
        f.pen = false;
        f.Run(3);
        CHECK(f.sent == 1 && f.V().recordState == BW_LINK_REC_READY && !f.V().queued);
        f.Run(30);      // nothing drawn: still READY - no take by itself ("already recorded although I did not start")
        CHECK(f.V().screen == gov::Screen::Recording && f.V().recordState == BW_LINK_REC_READY && !f.bw.started);
        // the old behaviour, for the record: a RECORD sent on the press with the trigger still held starts and ends a take at once
        FakeBw b;
        BW_LinkGestureCmds ring{};
        BW_LinkGestures blk{};
        b.Tick(ring, true, false, &blk);   // (primed)
        gesturecmd::Cmd c;
        c.op = BW_LINK_GC_RECORD;
        c.type = BW_LINK_GT_GLYPH;
        gesturecmd::Push(ring, c);
        for (int i = 0; i < 20; ++i) b.Tick(ring, true, true, &blk);   // held on from the click
        b.Tick(ring, true, false, &blk);
        CHECK(blk.recordState == BW_LINK_REC_SAVED);
    }
    {   // 3. the take before's SAVED (and its gesture in review) still in the block when the next RECORD is answered late: never read as this one
        Flow f;
        f.Run(3);
        f.NewGlyph();
        f.Run(2);
        f.Draw(20);
        CHECK(f.V().screen == gov::Screen::Review);
        // BodyWalk slow: the old SAVED + REVIEW stays in the block for a while after the RECORD is out
        f.bw.every = 60;
        f.bw.frame = 0;
        f.Click(gov::kRerecord);   // the same again
        CHECK(f.V().screen == gov::Screen::Recording);
        f.Run(40);
        CHECK(f.V().screen == gov::Screen::Recording && f.m.Recording() && f.V().recordState == BW_LINK_REC_NONE);
        f.Run(20);   // BodyWalk ticks: READY
        CHECK(f.V().recordState == BW_LINK_REC_READY && f.V().screen == gov::Screen::Recording);
        // a cmdDone that BodyWalk sends with a stale state (READY never shown): not read as the end - after kStartS it says it did not start
        Flow g;
        g.Run(3);
        g.NewGlyph();
        g.Run(2);
        g.Draw(20);
        g.Click(gov::kRerecord);
        g.bw.stall = true;   // BodyWalk writes nothing more ...
        g.block.cmdDone = g.ring.lastSequence;   // ... except that one block with its answer and the old state
        g.block.recordState = BW_LINK_REC_SAVED;
        g.Run(5);
        CHECK(g.V().screen == gov::Screen::Recording && !g.V().failed);
        g.Run(200);
        CHECK(g.V().screen == gov::Screen::Recording && g.V().failed && g.V().note == gov::Note::Failed);
    }
    {   // 4. a new glyph too short: FAILED, its row erased, recordGesture then names another gesture - Try again asks for a NEW one again
        Flow f;
        f.bw.rows.push_back({13, BW_LINK_GT_GLYPH, 54, true, false});
        f.Run(3);
        f.NewGlyph();
        f.Run(2);
        f.Draw(3);   // too short
        CHECK(f.V().failed && f.V().screen == gov::Screen::Recording && f.bw.rows.size() == 3);
        CHECK(f.bw.Find(500) < 0 && f.block.recordGesture == 0);   // (its row is gone; 0.206.8 re-sent 500 here and BodyWalk said NO_GESTURE)
        f.Click(gov::kRetry);
        CHECK(f.sent == 2 && f.ops[1] == BW_LINK_GC_RECORD);
        const BW_LinkGestureCmd& e = f.ring.ring[f.ring.lastSequence % BW_LINK_GESTURECMDS_RING];
        CHECK(e.gesture == 0 && e.type == BW_LINK_GT_GLYPH && e.hand == BW_LINK_GH_RIGHT && e.skill == 36);
        f.Run(2);
        CHECK(f.V().recordState == BW_LINK_REC_READY && !f.V().failed);
        f.Draw(20);
        CHECK(f.V().screen == gov::Screen::Review && f.V().review.g.skill == 36);
    }
    {   // 5. BodyWalk stops writing a moment (busy, minimised): the screen stays; kLostS of it, the list
        Flow f;
        f.Run(3);
        f.Click(gov::kNew);
        f.Click(gov::kChoice0 + 0);
        f.have = false;
        f.Run(180);   // 2 s
        CHECK(f.V().screen == gov::Screen::NewHand);
        f.have = true;
        f.Run(3);
        f.Click(gov::kChoice0 + 0);
        CHECK(f.V().screen == gov::Screen::NewSkill);
        f.have = false;
        f.Run(500);   // 5.5 s
        CHECK(f.V().screen == gov::Screen::List);
    }
    {   // 6. a slow BodyWalk (a tick every 0.45 s): the whole way still works, nothing times out
        Flow f;
        f.bw.every = 40;
        f.Run(50);
        f.NewGlyph();
        f.Run(90);
        CHECK(f.V().recordState == BW_LINK_REC_READY);
        f.pen = true;
        f.Run(500);   // (11 ticks drawn)
        f.pen = false;
        f.Run(90);
        CHECK(f.V().screen == gov::Screen::Review);
        f.Click(gov::kDiscard);
        f.Run(90);
        CHECK(f.V().screen == gov::Screen::List && f.V().note == gov::Note::Discarded && f.V().count == 2);
    }
    {   // 7. an older command's late answer is not the RECORD's: a threshold asked, BodyWalk stalls past kAnswerS, a RECORD, then it acts on both
        Flow f;
        f.Run(3);
        f.bw.stall = true;
        f.Click(gov::RowCell(0, gov::kThrLess));
        f.Run(600);
        CHECK(f.V().note == gov::Note::NoAnswer && !f.V().waiting);
        f.NewGlyph();
        CHECK(f.sent == 2);
        f.bw.stall = false;
        f.block.cmdDone = 1;   // (an answer for the threshold alone first)
        f.Frame();
        f.Run(2);
        CHECK(f.bw.cmdDone == 2 && f.V().recordState == BW_LINK_REC_READY);
    }
    {   // the hand choice: Right on top; the take's hand goes out
        Flow f;
        f.Run(3);
        f.Click(gov::kNew);
        f.Click(gov::kChoice0 + 0);
        CHECK(gov::HandOfChoice(0) == BW_LINK_GH_RIGHT && gov::HandOfChoice(1) == BW_LINK_GH_LEFT && gov::HandOfChoice(2) == BW_LINK_GH_BOTH);
        f.Click(gov::kChoice0 + 1);
        CHECK(f.V().newHand == BW_LINK_GH_LEFT);
    }
}

// -- [gestures] engine_in_game: the engine in the game (vr/gesture_engine.h, vr/gesture_engine_lib.cpp) against the stub DLL -------------------------

// What BodyWalk is given of the hands (link::PublishXr, vr/walk_path.h ControlsForBodyWalk): during a take the controls as held, although the skill panel
// took the triggers from the game (Serg's 0.206.10: BodyWalk read R 0.00 while the right trigger was at 1.00).
struct XrHandT { float trigger, squeeze, stickX, stickY; uint32_t buttons; float x; };
static void TestControlsForBodyWalk() {
    XrHandT raw[2] = {{0.0f, 0.2f, 0.1f, 0.0f, 0x1u, 1.0f}, {1.0f, 0.8f, 0.0f, 0.5f, 0x10u, 2.0f}};
    XrHandT taken[2] = {{0.0f, 0.0f, 0.0f, 0.0f, 0u, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f, 0u, 2.0f}};   // (HideTaken: the panel holds triggers and grips)
    XrHandT out[2];
    walkpath::ControlsForBodyWalk(out, taken, raw, true, true, true);   // direct mode, the skill link, recording
    CHECK(Near(out[1].trigger, 1.0f) && Near(out[1].squeeze, 0.8f) && out[1].buttons == 0x10u && Near(out[1].stickY, 0.5f) && Near(out[1].x, 2.0f));
    CHECK(Near(out[0].squeeze, 0.2f) && out[0].buttons == 0x1u);
    walkpath::ControlsForBodyWalk(out, taken, raw, true, true, false);   // not recording: the triggers as the game has them, nothing else
    CHECK(Near(out[1].trigger, 0.0f) && Near(out[1].squeeze, 0.0f) && out[1].buttons == 0u && Near(out[1].x, 2.0f));
    XrHandT play[2] = {{0.4f, 0.9f, 0.3f, 0.3f, 0x7u, 1.0f}, {0.7f, 0.9f, 0.3f, 0.3f, 0x7u, 2.0f}};
    walkpath::ControlsForBodyWalk(out, play, play, true, true, false);
    CHECK(Near(out[0].trigger, 0.4f) && Near(out[1].trigger, 0.7f) && Near(out[1].squeeze, 0.0f) && out[1].buttons == 0u && Near(out[1].stickX, 0.0f));
    walkpath::ControlsForBodyWalk(out, play, play, true, false, false);   // no skill link: no control at all
    CHECK(Near(out[1].trigger, 0.0f));
    walkpath::ControlsForBodyWalk(out, taken, raw, false, true, true);    // not direct mode: the frame as it is (BodyWalk is the input)
    CHECK(Near(out[1].trigger, 0.0f) && Near(out[1].x, 2.0f));
}

// The glyph's "done right" flash (vr/rune_flash.h): 100 ms bright in its own colour, thicker, opaque, its glow full; then as it was.
static void TestRuneFlash() {
    runeflash::Flash f;
    BW_LinkVrRune r{};
    r.trailRgba = 0x803C3CE6u;   // (a red glyph's trail, half opaque)
    r.litRgba = 0xFF2896F0u;
    r.runeRgba = 0x40FFFFFFu;
    r.trailWidth = 0.010f;
    r.lineWidth = 0.012f;
    r.glow = 0.2f;
    r.fade = 0.5f;
    BW_LinkVrRune a = r;
    f.Apply(a, 5000);   // never triggered (a stroke not recognised): untouched
    CHECK(std::memcmp(&a, &r, sizeof r) == 0 && !f.On(5000));
    f.Trigger(10000);
    CHECK(f.On(10000) && f.On(10099) && !f.On(10100) && !f.On(9999) && !f.On(20000));
    a = r;
    f.Apply(a, 10050);
    const int r0 = int(a.trailRgba & 0xFF), g0 = int((a.trailRgba >> 8) & 0xFF), b0 = int((a.trailRgba >> 16) & 0xFF);
    CHECK((a.trailRgba >> 24) == 0xFF && r0 > 0xE6 - 1 && g0 > 0x3C && b0 > 0x3C && r0 >= g0 && r0 >= b0);   // brighter, opaque, still red
    CHECK(Near(a.trailWidth, 0.014f, 1e-6f) && Near(a.lineWidth, 0.0168f, 1e-6f) && a.glow == 1.0f && a.fade == 1.0f);
    CHECK((a.litRgba >> 24) == 0xFF && (a.litRgba & 0xFF) >= 0xF0 && ((a.litRgba >> 16) & 0xFF) > 0x28);   // (R in the low byte)
    a = r;
    f.Apply(a, 10100);   // 100 ms on: as it was (it fades as it would)
    CHECK(std::memcmp(&a, &r, sizeof r) == 0);
    f.Trigger(20000);   // another recognised one: again
    a = r;
    f.Apply(a, 20001);
    CHECK(a.glow == 1.0f);
    f.Reset();
    CHECK(!f.On(20002));
    // the colour brightened in its own hue: a dark blue stays blue, white stays white, black lifts only to a grey
    const uint32_t blue = runeflash::Brighter(0xFFA02010u);
    CHECK(((blue >> 16) & 0xFF) > ((blue >> 8) & 0xFF) && ((blue >> 8) & 0xFF) > (blue & 0xFF));
    CHECK(runeflash::Brighter(0xFFFFFFFFu) == 0xFFFFFFFFu && (runeflash::Brighter(0xFF000000u) & 0xFF) == 64);
    CHECK(Near(runeflash::Wider(0.01f), 0.014f, 1e-7f) && runeflash::Wider(0.0f) == 0.0f);
}

static void TestEnginePure() {
    // the pens: the take's hand(s) while recording; none with the panel up; the key's in play; none out of play
    uint8_t pen[2];
    gengine::PenIn pi;
    pi.play = true;
    gengine::Pens(pi, pen);
    CHECK(pen[0] == 1 && pen[1] == 1);
    pi.playPens = gengine::kPenRight;
    gengine::Pens(pi, pen);
    CHECK(pen[0] == 0 && pen[1] == 1);
    pi.panelOpen = true;
    gengine::Pens(pi, pen);
    CHECK(pen[0] == 0 && pen[1] == 0);
    pi.recording = true;
    pi.recHand = BW_LINK_GH_LEFT;
    gengine::Pens(pi, pen);
    CHECK(pen[0] == 1 && pen[1] == 0);
    pi.recHand = BW_LINK_GH_BOTH;
    gengine::Pens(pi, pen);
    CHECK(pen[0] == 1 && pen[1] == 1);
    pi = gengine::PenIn{};
    pi.play = false;
    gengine::Pens(pi, pen);
    CHECK(pen[0] == 0 && pen[1] == 0);
    // the frame: poses, controls, the pens, PLAY; a hand with no controller is not valid
    di::Frame f{};
    f.time = 12.5;
    f.head.valid = 1;
    f.head.pos[1] = 1.6f;
    f.hand[1].active = 1;
    f.hand[1].grip.valid = 1;
    f.hand[1].grip.pos[0] = 0.3f;
    f.hand[1].trigger = 0.9f;
    f.hand[0].grip.valid = 1;   // (tracked but no controller)
    f.hand[0].squeeze = 0.4f;
    const uint8_t p2[2] = {0, 1};
    const BWGE_Frame e = gengine::FrameOf(f, true, p2);
    CHECK(e.size == sizeof(BWGE_Frame) && (e.flags & BWGE_FRAME_PLAY) && Near((float)e.timeS, 12.5f) && e.head.valid && Near(e.head.pos[1], 1.6f));
    CHECK(e.hand[1].valid && Near(e.hand[1].pos[0], 0.3f) && Near(e.trigger[1], 0.9f) && e.pen[1] == 1 && e.pen[0] == 0);
    CHECK(!e.hand[0].valid && Near(e.grip[0], 0.4f) && !e.chest.valid);
    CHECK(!(gengine::FrameOf(f, false, p2).flags & BWGE_FRAME_PLAY));
}

// gelib as gengine::Do's engine (the plugin's gesture_engine_wire.cpp Calls)
struct LibCalls {
    int Record(uint32_t g, uint8_t t, uint8_t h, int32_t s) { return gelib::Record(g, t, h, s); }
    int Stop() { return gelib::Stop(); }
    int Keep() { return gelib::Keep(); }
    int Discard() { return gelib::Discard(); }
    int Clear(uint32_t g) { return gelib::Clear(g); }
    int Bind(uint32_t g, int32_t s) { return gelib::Bind(g, s); }
    int SetColor(uint32_t g, uint32_t c) { return gelib::SetColor(g, c); }
    int SetThreshold(uint32_t g, float v) { return gelib::SetThreshold(g, v); }
};

static std::vector<std::string> g_engineLog;
static void EngineLog(const char* line) { g_engineLog.push_back(line); }

// The tab's model on the engine, as skill_wheel.cpp + gesture_engine_wire.cpp run them each frame: List (answers stamped) -> model -> Push; Feed
// with this frame's pens.
struct EngineFlow {
    gov::Model m;
    gengine::Answers answers;
    BW_LinkGestures list{};
    bool shown = true, play = true, have = true;
    bool clickTrigger = false;   // the pointing (right) hand's trigger, the click
    float penL = 0, penR = 0;    // the player's triggers as pens
    double t = 1.0;
    float u = -1, v = -1;
    wheel::Skill catalog[3];
    gov::Out last;
    std::vector<uint32_t> ops, results;
    std::vector<BWGE_Cast> casts;
    static constexpr int kCircleFrames = 60;
    int phase[2] = {0, 0};
    EngineFlow() {
        catalog[0] = Sk(36, "Fire Bolt", 1);
        catalog[1] = Sk(47, "Fire Ball", 1);
        catalog[2] = Sk(54, "Teleport", 2);
        BWGE_Skill sk[3] = {{36, "Fire Bolt"}, {47, "Fire Ball"}, {54, "Teleport"}};
        gelib::SetSet("sorceress", "Sorceress", sk, 3);
    }
    void Frame(bool click = false) {
        have = gelib::Loaded();
        if (have) {
            gelib::List(&list);
            answers.Stamp(list);
        }
        gov::In in;
        in.time = t += 0.011;
        in.shown = shown;
        in.pointer = u >= 0;
        in.u = u / gov::kW;
        in.v = v / gov::kH;
        in.click = click;
        in.link = true;
        in.g = have ? &list : nullptr;
        in.missing = gov::Avail::NoEngine;
        const float trig[2] = {penL, std::max(penR, clickTrigger ? 0.9f : 0.0f)};
        in.triggers = trig[0] > 0.3f || trig[1] > 0.3f;
        in.catalog = catalog;
        in.catalogCount = 3;
        last = m.Step(in);
        if (last.send) {
            LibCalls calls;
            const uint32_t seq = answers.Push(calls, last.cmd);
            m.Sent(seq, t);
            ops.push_back(last.cmd.op);
            results.push_back(answers.Result());
        }
        // the engine's frame with this frame's pens (the tab's take: its hand; the panel up otherwise: none; else the play pens)
        gengine::PenIn pi;
        pi.play = play;
        pi.panelOpen = shown;
        pi.recording = m.Recording();
        pi.recHand = m.Current().recHand;
        uint8_t pen[2];
        gengine::Pens(pi, pen);
        di::Frame f{};
        f.time = t;
        f.head.valid = 1;
        // the body: the head 1.6 m up looking ahead (-z); a hand at rest by its side, or - while its trigger is held - tracing a circle 0.18 m round
        // in front of the chest (the real engine reads the shape: one turn in kCircleFrames)
        f.head.pos[1] = 1.6f;
        f.head.rot[3] = 1.0f;
        for (int h = 0; h < 2; ++h) {
            f.hand[h].active = 1;
            f.hand[h].grip.valid = 1;
            f.hand[h].grip.rot[3] = 1.0f;
            f.hand[h].trigger = trig[h];
            const float side = h ? 1.0f : -1.0f;
            if (trig[h] > 0.3f && !(h == 1 && clickTrigger)) {
                const float a = 6.2831853f * float(phase[h]++) / float(kCircleFrames);
                f.hand[h].grip.pos[0] = side * 0.10f + 0.18f * std::cos(a);
                f.hand[h].grip.pos[1] = 1.30f + 0.18f * std::sin(a);
                f.hand[h].grip.pos[2] = -0.45f;
            } else {
                phase[h] = 0;
                f.hand[h].grip.pos[0] = side * 0.25f;
                f.hand[h].grip.pos[1] = 0.95f;
                f.hand[h].grip.pos[2] = -0.10f;
            }
        }
        gelib::Feed(gengine::FrameOf(f, play && !shown, pen));
        BWGE_Cast c[4];
        const int n = gelib::PollCasts(c, 4);
        for (int i = 0; i < n; ++i) casts.push_back(c[i]);
    }
    void Run(int frames) { for (int i = 0; i < frames; ++i) Frame(); }
    void Click(int cell) {
        gov::R r;
        if (!gov::CellRect(m.Current(), cell, &r)) { std::printf("  (engine flow: no rect for cell %d on screen %d)\n", cell, (int)m.Current().screen); return; }
        u = 0.5f * (r.l + r.r);
        v = 0.5f * (r.t + r.b);
        Frame();
        clickTrigger = true;
        Frame(true);
        Frame();
        clickTrigger = false;
        Frame();
        u = v = -1;
    }
    const gov::View& V() const { return m.Current(); }
    void NewGlyph(int handChoice = 0, int skillCell = gov::kGrid0 + 0) {
        Click(gov::kNew);
        Click(gov::kChoice0 + 0);
        Click(gov::kChoice0 + handChoice);
        Click(skillCell);
    }
    void Draw(int frames, bool right = true) {
        (right ? penR : penL) = 0.9f;
        Run(frames);
        (right ? penR : penL) = 0.0f;
        Run(3);
    }
};

static void TestEngineStub() {
    // the loader: no file -> Missing (the tab: NoEngine); the stub -> loaded, its log through ours
    gelib::SetLog(&EngineLog);
    gelib::SetPath(L"C:\\no such folder\\d2r_vr_gestures.dll");
    CHECK(!gelib::Ensure("C:\\temp") && gelib::GetStatus() == gelib::Status::Missing);
    CHECK(gelib::Record(0, 1, 1, 36) == BW_LINK_GR_BAD && gelib::List(nullptr) == 0);   // (no engine: nothing done)
    {
        Rig r;   // the tab says so
        r.have = false;
        r.missing = gov::Avail::NoEngine;
        r.Step();
        CHECK(r.V().avail == gov::Avail::NoEngine && gov::FixFor(r.V().avail, true) == gov::Fix::None);
    }
    wchar_t stub[MAX_PATH] = {};
    MultiByteToWideChar(CP_UTF8, 0, GESTURE_ENGINE_STUB, -1, stub, MAX_PATH);
    gelib::SetPath(stub);
    CHECK(gelib::Ensure("C:\\temp") && gelib::Loaded());
    bool told = false;
    for (const std::string& l : g_engineLog) told = told || l.find("gesture engine - stub engine open") != std::string::npos;
    CHECK(told);

    {   // the whole way on the engine: new glyph -> RECORD after the click's release -> a fresh press draws -> saved -> review -> keep
        EngineFlow f;
        f.Run(3);
        CHECK(f.V().avail == gov::Avail::Ok && f.V().count == 0);
        f.NewGlyph();
        f.Run(2);
        CHECK(f.ops.size() == 1 && f.ops[0] == BW_LINK_GC_RECORD && f.results[0] == BW_LINK_GR_OK);
        CHECK(f.V().screen == gov::Screen::Recording && f.V().recordState == BW_LINK_REC_READY && f.m.Recording());
        f.Draw(20);
        CHECK(f.V().screen == gov::Screen::Review && f.V().haveReview);
        const uint32_t first = f.V().review.g.id;
        f.Click(gov::kKeep);
        f.Run(2);
        CHECK(f.V().screen == gov::Screen::List && f.V().count == 1 && f.V().note == gov::Note::Kept && (f.V().row[0].g.flags & BW_LINK_GF_RECORDED));
        // re-record it, then discard: the old take stays
        f.Click(gov::RowCell(0, gov::kRowRerecord));
        f.Run(2);
        CHECK(f.V().recordState == BW_LINK_REC_READY && f.V().recName[0]);
        f.Draw(15);
        CHECK(f.V().screen == gov::Screen::Review && f.V().review.g.id == first);
        f.Click(gov::kDiscard);
        f.Run(2);
        CHECK(f.V().screen == gov::Screen::List && f.V().count == 1 && f.V().note == gov::Note::Discarded);
        // a second gesture right after the first, with the left hand: the right trigger does not draw it, the left does
        f.NewGlyph(1, gov::kGrid0 + 2);
        f.Run(2);
        CHECK(f.V().recordState == BW_LINK_REC_READY && f.V().recHand == BW_LINK_GH_LEFT);
        f.Draw(20, true);
        CHECK(f.V().screen == gov::Screen::Recording && f.V().recordState == BW_LINK_REC_READY);   // (the right trigger is no pen for it)
        f.Draw(20, false);
        CHECK(f.V().screen == gov::Screen::Review);
        f.Click(gov::kKeep);
        f.Run(2);
        CHECK(f.V().count == 2);
        // a too-short take of a new one: FAILED, Try again asks for a new one again, and it is taken
        f.NewGlyph();
        f.Run(2);
        f.Draw(3);
        CHECK(f.V().failed && f.V().count == 2);
        f.Click(gov::kRetry);
        f.Run(2);
        CHECK(f.ops.back() == BW_LINK_GC_RECORD && f.V().recordState == BW_LINK_REC_READY);
        f.Draw(20);
        CHECK(f.V().screen == gov::Screen::Review);
        f.Click(gov::kKeep);
        f.Run(2);
        CHECK(f.V().count == 3);
        // clear (asks twice), bind, colour, threshold - each answered at once
        f.Click(gov::RowCell(2, gov::kRowClear));
        f.Click(gov::RowCell(2, gov::kRowClear));
        f.Run(2);
        CHECK(f.V().count == 2 && f.V().note == gov::Note::Cleared);
        f.Click(gov::RowCell(0, gov::kRowBind));
        f.Click(gov::kGrid0 + 1);
        f.Run(2);
        CHECK(f.V().row[0].g.skill == 47 && f.V().note == gov::Note::Bound);
        f.Click(gov::RowCell(0, gov::kThrMore));
        f.Run(2);
        CHECK(Near(f.V().row[0].g.threshold, 85.0f));
        // in play (the panel down): a stroke with the right trigger casts the right hand's recorded glyph's skill
        f.shown = false;
        f.Run(3);
        f.Draw(15);
        CHECK(!f.casts.empty() && f.casts.back().skill == 47);
        // the panel up: a stroke casts nothing (the click is not a pen)
        const size_t before = f.casts.size();
        f.shown = true;
        f.Draw(15);
        CHECK(f.casts.size() == before);
    }
    {   // the click's pull is no pen even on the engine: held on the skill tile, nothing starts; let go, READY; only a fresh press draws
        EngineFlow f;
        f.Run(2);
        f.Click(gov::kNew);
        f.Click(gov::kChoice0 + 0);
        f.Click(gov::kChoice0 + 0);
        gov::R r;
        gov::CellRect(f.V(), gov::kGrid0 + 0, &r);
        f.u = 0.5f * (r.l + r.r);
        f.v = 0.5f * (r.t + r.b);
        f.Frame();
        f.clickTrigger = true;
        f.Frame(true);
        f.Run(30);
        CHECK(f.ops.empty() && f.V().queued);
        f.clickTrigger = false;
        f.Run(3);
        CHECK(f.ops.size() == 1 && f.V().recordState == BW_LINK_REC_READY);
    }
    gelib::Shutdown();
    CHECK(!gelib::Loaded() && gelib::List(nullptr) == 0);
    gelib::SetPath(nullptr);
}

// -- the real engine: BodyWalk's d2r_vr_gestures.dll (GESTURE_ENGINE_DLL=<its path>), the same flow, a scratch data folder ---------------------------
//
// The joint harness with the BodyWalk session: the tab's model, the same calls the plugin makes, the real library through the real loader. Its data
// folder is a fresh one under %TEMP% (never BodyWalk's own), so nothing of the player's gestures is read or written.

static void Say(EngineFlow& f, const char* what) {
    std::printf("  real engine: %-34s screen %-11s state %-9s note %2d count %d failed %d\n", what, gov::ScreenName(f.V().screen), gov::StateName(f.V().recordState),
                (int)f.V().note, f.V().count, f.V().failed ? 1 : 0);
}

// A folder and all under it gone (the scratch data folder).
static void DeleteTree(const std::string& dir) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::string name = fd.cFileName;
            if (name == "." || name == "..") continue;
            const std::string path = dir + "\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(path);
            else DeleteFileA(path.c_str());
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}

static void TestEngineReal() {
    char dll[MAX_PATH] = {};
    if (!GetEnvironmentVariableA("GESTURE_ENGINE_DLL", dll, sizeof dll)) {
        std::printf("  real engine: GESTURE_ENGINE_DLL not set - the stub only\n");
        return;
    }
    char tmp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tmp);
    char dataDir[MAX_PATH * 2] = {};
    std::snprintf(dataDir, sizeof dataDir, "%sgesture_engine_test_%llu", tmp, (unsigned long long)GetTickCount64());
    CreateDirectoryA(dataDir, nullptr);
    wchar_t wdll[MAX_PATH] = {};
    MultiByteToWideChar(CP_UTF8, 0, dll, -1, wdll, MAX_PATH);
    g_engineLog.clear();
    gelib::SetLog(&EngineLog);
    gelib::SetGameId("d2rtest");   // (not "d2r": a BodyWalk running beside the test would hold back its casts while the engine's mutex is held)
    gelib::SetPath(wdll);
    const bool open = gelib::Ensure(dataDir);
    CHECK(open);
    std::printf("  real engine: %s, data in %s\n", open ? "loaded" : gelib::StatusText(gelib::GetStatus()), dataDir);
    if (!open) {
        for (const std::string& l : g_engineLog) std::printf("    %s\n", l.c_str());
        gelib::SetPath(nullptr);
        return;
    }
    const int kDraw = EngineFlow::kCircleFrames + 4;
    uint32_t firstId = 0;
    {
        EngineFlow f;
        f.Run(5);
        CHECK(f.V().avail == gov::Avail::Ok && f.V().count == 0);
        // 1. a new glyph (a circle, right hand): READY after the click's release, a fresh press draws it, saved, review, keep
        f.NewGlyph();
        f.Run(3);
        Say(f, "new glyph asked");
        CHECK(f.ops.size() == 1 && f.results[0] == BW_LINK_GR_OK && f.V().recordState == BW_LINK_REC_READY && f.m.Recording());
        f.penR = 0.9f;
        f.Run(10);
        Say(f, "drawing");
        CHECK(f.V().recordState == BW_LINK_REC_DRAWING);
        f.Run(kDraw - 10);
        f.penR = 0.0f;
        f.Run(4);
        Say(f, "let go");
        CHECK(f.V().screen == gov::Screen::Review && f.V().haveReview && f.V().review.g.type == BW_LINK_GT_GLYPH && f.V().review.g.pointCount > 2);
        firstId = f.V().review.g.id;
        f.Click(gov::kKeep);
        f.Run(3);
        Say(f, "kept");
        CHECK(f.V().screen == gov::Screen::List && f.V().count == 1 && f.V().note == gov::Note::Kept && (f.V().row[0].g.flags & BW_LINK_GF_RECORDED));
        // 2. re-record it, discard the new take: the old one stays
        f.Click(gov::RowCell(0, gov::kRowRerecord));
        f.Run(3);
        CHECK(f.V().recordState == BW_LINK_REC_READY);
        f.Draw(kDraw);
        Say(f, "re-recorded");
        CHECK(f.V().screen == gov::Screen::Review && f.V().review.g.id == firstId);
        f.Click(gov::kDiscard);
        f.Run(3);
        Say(f, "discarded");
        CHECK(f.V().screen == gov::Screen::List && f.V().count == 1 && f.V().note == gov::Note::Discarded);
        // 3. a second gesture right after, with the left hand: the right trigger does not draw it, the left does
        f.NewGlyph(1, gov::kGrid0 + 2);
        f.Run(3);
        CHECK(f.V().recordState == BW_LINK_REC_READY && f.V().recHand == BW_LINK_GH_LEFT);
        f.Draw(kDraw, true);
        Say(f, "left take, right trigger");
        CHECK(f.V().screen == gov::Screen::Recording && f.V().recordState == BW_LINK_REC_READY);
        f.Draw(kDraw, false);
        Say(f, "left take, left trigger");
        CHECK(f.V().screen == gov::Screen::Review);
        f.Click(gov::kKeep);
        f.Run(3);
        CHECK(f.V().count == 2);
        // 4. a too-short take of a new one: FAILED, its row gone, Try again asks for a new one again, and that one is taken
        f.NewGlyph();
        f.Run(3);
        f.Draw(2);
        f.Run(5);
        Say(f, "too short");
        CHECK(f.V().failed && f.V().count == 2);
        f.Click(gov::kRetry);
        f.Run(3);
        CHECK(f.ops.back() == BW_LINK_GC_RECORD && f.results.back() == BW_LINK_GR_OK && f.V().recordState == BW_LINK_REC_READY);
        f.Draw(kDraw);
        Say(f, "taken again");
        CHECK(f.V().screen == gov::Screen::Review);
        f.Click(gov::kKeep);
        f.Run(3);
        CHECK(f.V().count == 3);
        // 5. clear (asks twice), bind, threshold
        f.Click(gov::RowCell(2, gov::kRowClear));
        f.Click(gov::RowCell(2, gov::kRowClear));
        f.Run(3);
        Say(f, "cleared");
        CHECK(f.V().count == 2 && f.V().note == gov::Note::Cleared);
        f.Click(gov::RowCell(0, gov::kRowBind));
        f.Click(gov::kGrid0 + 1);
        f.Run(3);
        CHECK(f.V().row[0].g.skill == 47 && f.V().note == gov::Note::Bound);
        f.Click(gov::RowCell(0, gov::kThrLess));
        f.Run(3);
        CHECK(Near(f.V().row[0].g.threshold, 75.0f, 0.5f));
        // 5a. a game action (the engine's optional calls): bound, read back, the skill gone; a skill again takes it away
        if (gelib::HasActions()) {
            const uint32_t gid = f.V().row[0].g.id;
            char a[32] = {};
            CHECK(gelib::BindAction(gid, "d2r.town_portal") == BW_LINK_GR_OK && gelib::GetAction(gid, a, sizeof a) == BW_LINK_GR_OK && std::strcmp(a, "d2r.town_portal") == 0);
            f.Run(3);
            CHECK(f.V().row[0].g.skill == -1);
            std::printf("  real engine: game action bound and read back (%s), the skill taken away\n", a);
            CHECK(gelib::Bind(gid, 47) == BW_LINK_GR_OK && gelib::GetAction(gid, a, sizeof a) == BW_LINK_GR_OK && a[0] == 0);
            f.Run(3);
            CHECK(f.V().row[0].g.skill == 47);
        } else {
            std::printf("  real engine: an older library without game actions\n");
        }
        // 5a'. a glyph's hand (the engine's optional call): left, either, right again - kept with the gesture, no take
        if (gelib::HasSetHand()) {
            const uint32_t gid = f.V().row[0].g.id;
            CHECK(gelib::SetHand(gid, BW_LINK_GH_BOTH) == BW_LINK_GR_OK);
            f.Run(3);
            CHECK(f.V().row[0].g.hand == BW_LINK_GH_BOTH);
            CHECK(gelib::SetHand(gid, BW_LINK_GH_RIGHT) == BW_LINK_GR_OK);
            f.Run(3);
            CHECK(f.V().row[0].g.hand == BW_LINK_GH_RIGHT);
            std::printf("  real engine: a glyph's hand set to either and back to the right\n");
        } else {
            std::printf("  real engine: an older library without BWGE_SetHand\n");
        }
        // 5a''. a gesture's mode (the engine's optional calls): select kept and read back, cast once again
        if (gelib::HasModes()) {
            const uint32_t gid = f.V().row[0].g.id;
            uint8_t m = 9;
            CHECK(gelib::SetMode(gid, 1) == BW_LINK_GR_OK && gelib::GetMode(gid, &m) == BW_LINK_GR_OK && m == 1);
            CHECK(gelib::SetMode(gid, 7) != BW_LINK_GR_OK);
            CHECK(gelib::SetMode(gid, 0) == BW_LINK_GR_OK && gelib::GetMode(gid, &m) == BW_LINK_GR_OK && m == 0);
            std::printf("  real engine: a gesture's mode set to select and back to cast once\n");
        } else {
            std::printf("  real engine: an older library without BWGE_SetMode\n");
        }
        // 5b. a pose's zone (the engine's optional calls): a glyph has none; a two-hand pose taken, its zone read, sized, kept; the take dropped
        if (gelib::HasPoseZones()) {
            BWGE_PoseZone z{};
            CHECK(gelib::GetPoseZone(f.V().row[0].g.id, &z) == BW_LINK_GR_BAD);
            f.Click(gov::kNew);
            f.Click(gov::kChoice0 + 1);   // Hand pose
            CHECK(f.V().screen == gov::Screen::NewHand && f.V().choices == 3);
            f.Click(gov::kChoice0 + 2);   // Both hands
            f.Click(gov::kGrid0 + 0);
            f.Run(3);
            f.Draw(kDraw);
            f.Run(5);
            Say(f, "two-hand pose");
            if (f.V().screen == gov::Screen::Review && f.V().haveReview) {
                const uint32_t pid = f.V().review.g.id;
                CHECK(f.V().review.g.type == BW_LINK_GT_POSE && f.V().review.g.hand == BW_LINK_GH_BOTH);
                CHECK(gelib::GetPoseZone(pid, &z) == BW_LINK_GR_OK && z.handValid == 3u && z.radiusM >= 0.03f && z.radiusM <= 0.5f && z.armM > 0.3f);
                std::printf("  real engine: pose zone %.0f cm, rotation %.2f, body %.2f m / arm %.2f m, right hand at %.2f %.2f %.2f (joints %.2f %.2f %.2f)\n",
                            100.0f * z.radiusM, z.rotationMatch, z.heightM, z.armM, z.centre[1][0], z.centre[1][1], z.centre[1][2],
                            f.V().review.g.joints[BW_LINK_J_RHAND][0], f.V().review.g.joints[BW_LINK_J_RHAND][1], f.V().review.g.joints[BW_LINK_J_RHAND][2]);
                CHECK(gelib::SetPoseZone(pid, 0.10f) == BW_LINK_GR_OK && gelib::GetPoseZone(pid, &z) == BW_LINK_GR_OK && Near(z.radiusM, 0.10f));
                gelib::SetPoseZone(pid, 0.9f);
                CHECK(gelib::GetPoseZone(pid, &z) == BW_LINK_GR_OK && Near(z.radiusM, 0.50f));   // (clamped)
                std::vector<posezones::Zone> pz(4);
                const float rr[2] = {z.radiusM, z.radiusM};
                CHECK(posezones::TargetsOf(f.V().review.g, rr, 1, pz.data(), 4, z.heightM) == 2);   // both hands drawn
            } else {
                CHECK(!"the two-hand pose take reached its review");
            }
            f.Click(gov::kDiscard);
            f.Run(3);
            CHECK(f.V().count == 2);
        } else {
            std::printf("  real engine: an older library without pose zones\n");
        }
        // 6. in play (the panel down): the same circle with the right hand casts the first gesture's skill (now Fire Ball); the panel up: nothing
        f.shown = false;
        f.Run(60);
        f.Draw(kDraw);
        f.Run(10);
        std::printf("  real engine: casts in play %zu%s\n", f.casts.size(), f.casts.empty() ? "" : (f.casts.back().skill == 47 ? " (Fire Ball)" : " (another skill)"));
        CHECK(!f.casts.empty() && f.casts.back().skill == 47 && f.casts.back().gesture == firstId);
        const size_t before = f.casts.size();
        f.shown = true;
        f.Run(60);
        f.Draw(kDraw);
        f.Run(10);
        CHECK(f.casts.size() == before);
    }
    // 7. kept on disk: closed and opened again, the two gestures are there
    gelib::Shutdown();
    CHECK(!gelib::Loaded());
    CHECK(gelib::Ensure(dataDir) || (Sleep(2100), gelib::Ensure(dataDir)));
    {
        EngineFlow f;
        f.Run(5);
        Say(f, "reopened");
        CHECK(f.V().count == 2 && f.V().row[0].g.id == firstId);
    }
    char index[MAX_PATH * 2];
    std::snprintf(index, sizeof index, "%s\\games\\d2rtest\\gestures.json", dataDir);
    CHECK(GetFileAttributesA(index) != INVALID_FILE_ATTRIBUTES);
    gelib::Shutdown();
    gelib::SetPath(nullptr);
    gelib::SetGameId(nullptr);
    DeleteTree(dataDir);
    CHECK(GetFileAttributesA(dataDir) == INVALID_FILE_ATTRIBUTES);
    std::printf("  real engine: %zu log lines from the library; the scratch data folder removed\n", g_engineLog.size());
}

// -- the tab in the window's controller --------------------------------------------------------------------------------------------------------------

static wheel::Snapshot MakeSnap(uint64_t session, uint64_t at) {
    wheel::Snapshot s;
    s.session = session;
    s.at = at;
    s.ready = true;
    s.editable = true;
    s.bank = 0;
    for (int k = 0; k < wheel::kSlotCount; ++k) s.slot[k].known = true;
    s.count = 4;
    s.catalog[0] = Sk(36, "Fire Bolt", 1);
    s.catalog[1] = Sk(47, "Fire Ball", 1);
    s.catalog[2] = Sk(54, "Teleport", 2);
    s.catalog[3] = Sk(40, "Frozen Armor", 3);
    return s;
}

struct World {
    wheel::Controller c;
    wheel::Snapshot snap;
    uint64_t now = 20000;
    uint32_t counter = 0;
    double time = 100.0;
    di::Frame f{};
    wheel::Reply reply;
    wheel::SettingsIn host;
    wheel::GesturesIn gin;
    Bw bw;
    bool f4 = true, request = false, useGestures = true;
    uint32_t seq = 0;
    explicit World(bool gestureTab) {
        wheel::Settings cfg;
        cfg.style = wheel::Style::Tiles;
        cfg.openMode = 1;
        cfg.settingsTab = true;
        cfg.gestureTab = gestureTab;
        c.Configure(cfg);
        snap = MakeSnap(3, now);
        host.ini.ready = true;
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
        gin.link = true;
        gin.g = &bw.g;
    }
    wheel::Step Next(double dt = 0.011) {
        f.counter = ++counter;
        f.time = time += dt;
        f.displayTime = (int64_t)(time * 1e9);
        now += (uint64_t)(dt * 1000.0);
        snap.at = now;
        const bool req = request;
        request = false;
        const wheel::Step s = c.Frame(f, snap, now, reply, f4, req, &host, useGestures ? &gin : nullptr);
        if (s.gestureSend) c.GestureSent(++seq, f.time);
        return s;
    }
    void Hand(int h, float trigger, uint32_t buttons) {
        f.hand[h].trigger = trigger;
        f.hand[h].buttons = buttons;
    }
    void AimPx(float x, float y) {
        const wheel::Pose p = c.PanelPose();
        const float w = wheel::PanelWidthM(c.Config()), h = wheel::PanelHeightM(c.Config());
        const float u = x / tiles::kW, v = y / tiles::kH;
        const wheel::V3 target = wheel::Add(p.p, wheel::Rot(p.q, wheel::V3{(u - 0.5f) * w, (0.5f - v) * h, 0.0f}));
        const wheel::V3 from{f.hand[1].aim.pos[0], f.hand[1].aim.pos[1], f.hand[1].aim.pos[2]};
        const wheel::V3 d = wheel::Unit(wheel::Sub(target, from));
        const wheel::V3 z{0, 0, -1};
        wheel::V3 axis{z.y * d.z - z.z * d.y, z.z * d.x - z.x * d.z, z.x * d.y - z.y * d.x};
        const float s = wheel::Len(axis), cs = wheel::Dot(z, d);
        const float ang = std::atan2(s, cs);
        axis = s > 1e-6f ? wheel::Mul(axis, 1.0f / s) : wheel::V3{0, 1, 0};
        f.hand[1].aim.rot[0] = axis.x * std::sin(ang / 2);
        f.hand[1].aim.rot[1] = axis.y * std::sin(ang / 2);
        f.hand[1].aim.rot[2] = axis.z * std::sin(ang / 2);
        f.hand[1].aim.rot[3] = std::cos(ang / 2);
    }
    wheel::Step ClickPx(float x, float y) {   // (what the let-go frame sends - a RECORD - is told with the click's)
        AimPx(x, y);
        Hand(1, 0, 0);
        Next();
        Hand(1, 0.9f, 0);
        wheel::Step s = Next();
        Hand(1, 0, 0);
        const wheel::Step up = Next();
        if (up.gestureSend && !s.gestureSend) { s.gestureSend = true; s.gestureCmd = up.gestureCmd; }
        return s;
    }
    wheel::Step ClickRect(gov::R r) { return ClickPx(0.5f * (r.l + r.r), 0.5f * (r.t + r.b)); }
    wheel::Step ClickTab(int i, int n) { const tiles::PRect r = tiles::TabRect(i, n); return ClickPx(0.5f * (r.l + r.r), 0.5f * (r.t + r.b)); }
    wheel::Step Open() { request = true; return Next(); }
};

// -- a hand pose's zone (vr/pose_zones.h): the sphere where the hand must be, sized on the tab, shown in play or hidden ---------------------------------
static void TestZones() {
    {   // the size: bounds, steps, nonsense
        CHECK(Near(posezones::Clamp(0.01f), posezones::kMinM) && Near(posezones::Clamp(5.0f), posezones::kMaxM));
        CHECK(Near(posezones::Clamp(std::nanf("")), posezones::kDefaultM));
        CHECK(Near(posezones::Step(0.12f, 1), 0.13f) && Near(posezones::Step(0.12f, -1), 0.11f));
        CHECK(Near(posezones::kMinM, 0.03f) && Near(posezones::kMaxM, 0.50f) && Near(posezones::kDefaultM, 0.06f));   // (BWGE_SetPoseZone's bounds, the engine's default)
        CHECK(Near(posezones::Step(posezones::kMaxM, 1), posezones::kMaxM) && Near(posezones::Step(posezones::kMinM, -1), posezones::kMinM));
    }
    {   // a recorded pose's zones: the hand joints from the pelvis (the engine's body frame), per hand of the pose; not a pose: none
        BW_LinkGesture g = G(12, BW_LINK_GT_POSE, 47, "pose");
        const float r[2] = {0.08f, 0.20f};
        posezones::Zone z[4];
        int n = posezones::TargetsOf(g, r, 0x60FFFFFFu, z, 4);
        CHECK(n == 1 && z[0].attach == posezones::Attach::Body && Near(z[0].radius, 0.20f) && Near(z[0].heightM, posezones::kDefaultHeightM));
        CHECK(Near(z[0].off[0], 0.30f) && Near(z[0].off[1], 0.95f) && Near(z[0].off[2], -0.10f));   // the right hand from the pelvis
        CHECK(posezones::TargetsOf(g, r, 0x60FFFFFFu, z, 4, 1.85f) == 1 && Near(z[0].heightM, 1.85f));
        g.hand = BW_LINK_GH_BOTH;
        n = posezones::TargetsOf(g, r, 0x60FFFFFFu, z, 4);
        CHECK(n == 2 && Near(z[0].off[0], -0.22f) && Near(z[0].radius, 0.08f) && Near(z[1].radius, 0.20f));
        CHECK(posezones::TargetsOf(g, r, 0x60FFFFFFu, z, 1) == 1);   // (no more than asked)
        g.jointValid &= ~(1u << BW_LINK_J_LHAND);
        CHECK(posezones::TargetsOf(g, r, 0x60FFFFFFu, z, 4) == 1);   // (a hand not recorded: no zone for it)
        BW_LinkGesture glyph = G(11, BW_LINK_GT_GLYPH, 36, "glyph");
        CHECK(posezones::TargetsOf(glyph, r, 0x60FFFFFFu, z, 4) == 0);
        BW_LinkGesture wild = G(12, BW_LINK_GT_POSE, 47, "pose");
        wild.joints[BW_LINK_J_RHAND][0] = 40.0f;   // (a broken joint: no zone off in another room)
        CHECK(posezones::TargetsOf(wild, r, 0x60FFFFFFu, z, 4) == 0);
    }
    {   // a take's zones follow the hands; in the room: a body zone from the pelvis the head gives (the engine's frame), turning with the head; a hand
        // zone sits on the hand; none when what it hangs on is not tracked
        posezones::Zone z[4];
        CHECK(posezones::FollowHands(BW_LINK_GH_LEFT, 0.5f, 1, z, 4) == 1 && z[0].attach == posezones::Attach::LeftHand && Near(z[0].radius, posezones::kMaxM));
        CHECK(posezones::FollowHands(BW_LINK_GH_BOTH, 0.1f, 1, z, 4) == 2 && z[1].attach == posezones::Attach::RightHand);
        posezones::Head head;
        head.pos[0] = 1.0f; head.pos[1] = 1.6f; head.pos[2] = 2.0f;
        head.ok = true;   // (looking level down -z)
        const float hands[2][3] = {{0.5f, 1.0f, 1.5f}, {1.4f, 1.1f, 1.6f}};
        bool ok[2] = {true, false};
        float c[3], pel[3], yaw = 9;
        // the pelvis: head - 0.07 H forward - 0.04 H up - 0.11 H up - 0.42 m (H 1.8): 12.6 cm behind the head... (forward -z: +z), 0.69 m under it
        CHECK(posezones::PelvisOf(head, 1.8f, pel, &yaw) && Near(pel[0], 1.0f) && Near(pel[1], 1.6f - 0.27f - 0.42f) && Near(pel[2], 2.0f + 0.126f) && Near(yaw, 0.0f));
        posezones::Zone h;
        h.attach = posezones::Attach::Body;
        h.heightM = 1.8f;
        h.off[0] = 0.25f; h.off[1] = 0.19f; h.off[2] = -0.426f;   // (the engine session's check: a 1.8 m player's right hand)
        CHECK(posezones::CentreOf(h, head, hands, ok, c) && Near(c[0], 1.25f) && Near(c[1], pel[1] + 0.19f) && Near(c[2], pel[2] - 0.426f));
        posezones::Head left = head;   // turned left a quarter: ahead is -x, the body's right is -z
        left.fwd[0] = -1.0f; left.fwd[2] = 0.0f;
        CHECK(posezones::PelvisOf(left, 1.8f, pel, &yaw) && Near(yaw, 1.5707963f) && Near(pel[0], 1.126f) && Near(pel[2], 2.0f));
        CHECK(posezones::CentreOf(h, left, hands, ok, c) && Near(c[0], pel[0] - 0.426f) && Near(c[2], pel[2] - 0.25f));
        posezones::Head down = head;   // the head pitched down: the chest stays under it (the head's own forward and up count)
        down.fwd[0] = 0.0f; down.fwd[1] = -0.7071f; down.fwd[2] = -0.7071f;
        down.up[0] = 0.0f; down.up[1] = 0.7071f; down.up[2] = -0.7071f;
        CHECK(posezones::PelvisOf(down, 1.8f, pel, &yaw) && Near(yaw, 0.0f));
        head.ok = false;
        CHECK(!posezones::CentreOf(h, head, hands, ok, c));
        head.ok = true;
        CHECK(posezones::CentreOf(z[0], head, hands, ok, c) && Near(c[0], 0.5f) && Near(c[2], 1.5f));
        CHECK(!posezones::CentreOf(z[1], head, hands, ok, c));
    }
    {   // the list: a pose's row shows its zone (the host's, else the default); its arrows size the zone (no BodyWalk command), a glyph's the threshold
        Rig r;
        r.Step();
        CHECK(r.V().row[1].g.type == BW_LINK_GT_POSE && Near(r.V().row[1].zone[1], posezones::kDefaultM));
        r.zones.push_back({12, {0.06f, 0.18f}});
        r.Step();
        CHECK(Near(r.V().row[1].zone[0], 0.06f) && Near(r.V().row[1].zone[1], 0.18f) && Near(r.V().row[0].zone[1], posezones::kDefaultM));
        gov::Out o = r.ClickCell(gov::RowCell(1, gov::kThrMore));
        CHECK(!o.send && o.zoneSet && o.zoneGesture == 12 && o.zoneHand == BW_LINK_GH_RIGHT && Near(o.zoneM, 0.19f));
        o = r.ClickCell(gov::RowCell(1, gov::kThrLess));
        CHECK(!o.send && o.zoneSet && Near(o.zoneM, 0.17f));
        o = r.ClickCell(gov::RowCell(0, gov::kThrLess));
        CHECK(o.send && !o.zoneSet && o.cmd.op == BW_LINK_GC_THRESHOLD);
        r.bw.Act(r.seq);
        r.Step();
        // the footer's Zones: always there, flips (the host does it), even with nothing to manage
        CHECK(gov::FooterEnabled(r.V(), gov::kZonesToggle) && !r.V().zonesShown);
        o = r.ClickCell(gov::kZonesToggle);
        CHECK(o.toggleZones && !o.send);
        r.zonesShown = true;
        r.Step();
        CHECK(r.V().zonesShown);
        r.link = false;
        r.Step();
        o = r.ClickCell(gov::kZonesToggle);
        CHECK(o.toggleZones);
    }
    {   // a pose's review: its zone under the line, its arrows size it; a glyph's review has none
        Rig r;
        r.Step();
        gov::Out o = r.ClickCell(gov::RowCell(1, gov::kRowRerecord));
        CHECK(o.send && o.cmd.op == BW_LINK_GC_RECORD && o.cmd.gesture == 12 && r.V().recTarget == 12);
        r.bw.Act(r.seq);
        r.bw.g.recordState = BW_LINK_REC_READY;
        r.bw.g.recordGesture = 12;
        r.Step();
        r.bw.g.recordState = BW_LINK_REC_SAVED;
        r.bw.g.g[1].flags |= BW_LINK_GF_REVIEW;
        r.Step();
        CHECK(r.V().screen == gov::Screen::Review && r.V().review.g.id == 12 && Near(r.V().review.zone[1], posezones::kDefaultM));
        gov::R zr;
        CHECK(gov::CellRect(r.V(), gov::kZoneMore, &zr) && gov::CellRect(r.V(), gov::kZoneLess, &zr));
        o = r.ClickCell(gov::kZoneMore);
        CHECK(o.zoneSet && o.zoneGesture == 12 && Near(o.zoneM, 0.07f) && r.V().screen == gov::Screen::Review);
        o = r.ClickCell(gov::kZoneLess);
        CHECK(o.zoneSet && Near(o.zoneM, 0.05f));
        CHECK(!Overlap(gov::kZoneLessRect, gov::kZoneMoreRect) && !Overlap(gov::kZoneText, gov::kReviewLine));
    }
    {   // the picture: one figure (no second figure in a bubble), the zone a disc round the pose's hand
        gpic::Box b{0, 0, 200, 200};
        const BW_LinkGesture g = G(12, BW_LINK_GT_POSE, 47, "pose");
        const gpic::Figure f = gpic::StickFigure(g.joints, g.jointValid, g.grip, b, 8);
        CHECK(f.hand[1].x > b.l && f.hand[1].x < b.r && f.hand[1].y > b.t && f.hand[1].y < b.b);
    }
}

// -- Play (vr/gesture_play.h; the model's picture): once, from the controllers, back to them ----------------------------------------------------------
static void TestPlay() {
    {   // the model: a picture plays its gesture once (asks BodyWalk nothing, even while it is busy); the review's Play; the swatches on a glyph's review
        Rig r;
        r.Step();
        gov::Out o = r.ClickCell(gov::RowCell(1, gov::kPic));   // (the pose)
        CHECK(o.play == 12 && !o.send && r.V().playId == 12 && r.V().playT >= 0.0f && r.V().playT < 0.2f && r.V().playHand == BW_LINK_GH_RIGHT);
        for (int i = 0; i < 40; ++i) r.Step();   // 0.44 s of 1.0
        CHECK(r.V().playId == 12 && r.V().playT > 0.3f && r.V().playT < 0.7f);
        for (int i = 0; i < 60; ++i) r.Step();
        CHECK(r.V().playId == 0);   // (once: no loop)
        r.ClickCell(gov::RowCell(0, gov::kThrLess));   // BodyWalk busy with a threshold: Play still plays
        CHECK(r.V().waiting);
        o = r.ClickCell(gov::RowCell(0, gov::kPic));
        CHECK(o.play == 11 && r.V().playId == 11);
        CHECK(gov::FooterEnabled(r.V(), gov::kNew) == false);   // (the rest waits for BodyWalk)
        // the host's duration is taken while it plays
        Rig q;
        q.Step();
        q.ClickCell(gov::RowCell(2, gov::kPic));
        for (int i = 0; i < 100; ++i) q.Step();   // 1.1 s: past a motion's own 1.4 s? not yet
        CHECK(q.V().playId == 13);
        // a glyph's review: swatches, Play in the footer
        Rig v;
        v.Step();
        v.ClickCell(gov::RowCell(0, gov::kRowRerecord));
        v.bw.Act(v.seq);
        v.bw.g.recordState = BW_LINK_REC_READY;
        v.bw.g.recordGesture = 11;
        v.Step();
        v.bw.g.recordState = BW_LINK_REC_SAVED;
        v.bw.g.g[0].flags |= BW_LINK_GF_REVIEW;
        v.Step();
        CHECK(v.V().screen == gov::Screen::Review && v.V().review.g.id == 11);
        gov::R sr;
        CHECK(gov::CellRect(v.V(), gov::kSwatch0 + 4, &sr) && gov::CellRect(v.V(), gov::kPlay, &sr) && !gov::CellRect(v.V(), gov::kZoneMore, &sr));
        o = v.ClickCell(gov::kSwatch0 + 4);
        CHECK(o.send && o.cmd.op == BW_LINK_GC_COLOR && o.cmd.gesture == 11 && o.cmd.rgba == gov::kPalette[4] && v.V().screen == gov::Screen::Review);
        v.bw.Act(v.seq);
        v.Step();
        o = v.ClickCell(gov::kPlay);
        CHECK(o.play == 11 && v.V().screen == gov::Screen::Review && v.V().playId == 11);
        for (int i = 0; i < gov::kSwatches; ++i)   // the swatches clear of the review's lines and of each other
            CHECK(!Overlap(gov::SwatchRect(i, false), gov::kReviewLine) && (i == 0 || !Overlap(gov::SwatchRect(i, false), gov::SwatchRect(i - 1, false))) &&
                  Inside(gov::SwatchRect(i, true), gov::R{0, 0, gov::kW, gov::kH}) && !Overlap(gov::SwatchRect(i, true), gov::kColourPic));
        CHECK(!Overlap(gov::PartRect(0, gov::kColour), gov::NameRect(0)) && !Overlap(gov::PartRect(0, gov::kColour), gov::TypeRect(0)));
    }
    {   // a pose made up from the list: from the arms hanging to its hands, the grips with it
        gplay::Take t;
        BW_LinkGesture g = G(12, BW_LINK_GT_POSE, 47, "pose");
        CHECK(gplay::FromGesture(g, &t) && t.s.size() == 2 && Near(t.DurationS(), 0.7f));
        CHECK(t.s[1].handValid == 2u && Near(t.s[1].hand[1][0], 0.30f) && Near(t.s[1].hand[1][1], 0.95f) && Near(t.s[1].grip[1], 0.05f));
        g.hand = BW_LINK_GH_BOTH;
        CHECK(gplay::FromGesture(g, &t) && t.s[1].handValid == 3u && Near(t.s[1].hand[0][0], -0.22f) && Near(t.s[1].grip[0], 0.95f));   // both arms
        const gplay::Sample mid = gplay::At(t, 0.35f);
        CHECK(Near(mid.hand[1][1], 0.5f * (t.s[0].hand[1][1] + 0.95f)));
        // a glyph: its path drawn by the right hand on a sheet before the chest, in 1.6 s
        BW_LinkGesture gl = G(11, BW_LINK_GT_GLYPH, 36, "glyph");
        CHECK(gplay::FromGesture(gl, &t) && (int)t.s.size() == BW_LINK_GESTURE_POINTS && Near(t.DurationS(), 1.6f) && t.s[5].handValid == 2u);
        CHECK(Near(t.s[0].hand[1][2], -0.45f) && Near(t.s[0].hand[1][0], 0.12f + 0.5f * gl.pts[0][0]));
        BW_LinkGesture btn = G(14, BW_LINK_GT_BUTTON, 40, "button");
        CHECK(!gplay::FromGesture(btn, &t));
    }
    {   // the weight: in from the controllers, the take, held, out - and nothing after (once)
        gplay::Take t;
        gplay::FromGesture(G(12, BW_LINK_GT_POSE, 47, "pose"), &t);
        float tt = 0;
        CHECK(Near(gplay::Weight(t, 0.0f, &tt), 0.0f) && Near(gplay::Weight(t, gplay::kLeadS, &tt), 1.0f) && Near(tt, 0.0f));
        CHECK(Near(gplay::Weight(t, gplay::kLeadS + 0.5f, &tt), 1.0f) && Near(tt, 0.5f));
        CHECK(Near(gplay::Weight(t, gplay::kLeadS + 0.7f + gplay::kHoldS - 0.01f, &tt), 1.0f) && Near(tt, 0.7f));
        CHECK(Near(gplay::Weight(t, t.TotalS(), &tt), 0.0f) && Near(gplay::Weight(t, t.TotalS() + 1.0f, &tt), 0.0f));
        // the body frame into the drawing's: the pelvis 0.69 m under a 1.8 m player's eyes, 12.6 cm behind them, looking level
        float o[3];
        const float p0[3] = {0, 0, 0};
        gplay::ToHeadFrame(p0, 1.8f, 0.0f, o);
        CHECK(Near(o[0], 0.0f) && Near(o[1], -0.072f - 0.198f - 0.42f) && Near(o[2], 0.126f));
        // a frame: the right arm (the drawing's side 0) is the take's right hand; the left keeps its controller
        const float ctl[2][3] = {{0.3f, -0.4f, -0.3f}, {-0.3f, -0.4f, -0.3f}};
        const float ctlRot[2][4] = {{0, 0, 0, 1}, {0, 0, 0, 1}};
        const float grips[2] = {0.0f, 0.0f};
        gplay::Arms a;
        CHECK(!gplay::Frame(t, -0.1f, 1.8f, 0.0f, ctl, ctlRot, grips, &a) && !gplay::Frame(t, t.TotalS() + 0.01f, 1.8f, 0.0f, ctl, ctlRot, grips, &a));
        CHECK(gplay::Frame(t, gplay::kLeadS + 0.7f, 1.8f, 0.0f, ctl, ctlRot, grips, &a) && a.valid == 1u && Near(a.weight, 1.0f));
        CHECK(Near(a.hand[0][0], 0.30f) && Near(a.hand[0][1], 0.95f - 0.69f) && Near(a.hand[0][2], -0.10f + 0.126f));   // the take's right hand
        CHECK(Near(a.hand[1][0], -0.3f) && Near(a.hand[1][2], -0.3f));   // the left: its controller, as it is
        CHECK(gplay::Frame(t, 0.5f * gplay::kLeadS, 1.8f, 0.0f, ctl, ctlRot, grips, &a) && a.weight > 0.3f && a.weight < 0.7f);   // (on its way from the controller)
    }
}

// -- Bind: the game's actions beside the skills (vr/gesture_actions.h) ---------------------------------------------------------------------------------
static void TestBindActions() {
    {   // the list: the town portal first, the potions, the panels, the pad, every key command of the game - each once, each named
        const gact::List& l = gact::All();
        CHECK(l.Count() > 60 && l.Count() <= gact::kMax && std::strcmp(l.At(0).id, "d2r.town_portal") == 0 && l.At(0).kind == gact::Kind::Item);
        const gact::Def* belt = l.Find("d2r.cmd.17");
        CHECK(belt && belt->kind == gact::Kind::Command && belt->code == 0x17 && std::strcmp(belt->name, "Potion - belt 1") == 0 && l.IndexOf("d2r.cmd.17") < 8);
        CHECK(l.Find("d2r.cmd.2C") && l.Find("d2r.cmd.01") && l.Find("d2r.cmd.25") && l.Find("d2r.cmd.23"));   // swap, inventory, Alt, run / walk
        const gact::Def* lt = l.Find("pad.LT");
        CHECK(lt && lt->kind == gact::Kind::Pad && lt->code == gact::kPadLT && l.Find("pad.A")->code == 0x1000);
        for (int i = 0; i < l.Count(); ++i)
            for (int j = i + 1; j < l.Count(); ++j) CHECK(std::strcmp(l.At(i).id, l.At(j).id) != 0);
        int commands = 0;
        for (int i = 0; i < l.Count(); ++i) commands += l.At(i).kind == gact::Kind::Command;
        CHECK(commands == (int)kD2RVRCommandCount);   // (every key command of the game, once)
        CHECK(std::strcmp(gact::NameOf("d2r.identify"), "Identify (tome or scroll)") == 0 && std::strcmp(gact::NameOf("x.unknown"), "x.unknown") == 0);
        // the Keyboard key tile (vr/key_capture.h: its key's game command is what gets bound) on the first page; the panels said as show / hide
        const gact::Def* key = l.Find(gact::kKeyId);
        CHECK(key && key->kind == gact::Kind::Key && l.IndexOf(gact::kKeyId) >= 0 && l.IndexOf(gact::kKeyId) < 24);
        CHECK(std::strcmp(gact::NameOf("d2r.cmd.01"), "Show / hide inventory") == 0 && std::strcmp(gact::NameOf("d2r.cmd.00"), "Show / hide character") == 0);
    }
    {   // the bind screen: Skills | Game actions; an action binds through the engine's own call (no command), the row says what it does
        Rig r;
        r.Step();
        r.ClickCell(gov::RowCell(0, gov::kRowBind));
        CHECK(r.V().screen == gov::Screen::BindSkill && r.V().bindTab == 0 && r.V().options > 0);
        r.ClickCell(gov::kBindTabActions);
        CHECK(r.V().bindTab == 1 && r.V().actCount == gov::kGridCount && r.V().pickPages >= 3 && r.V().options == 0);
        r.ClickCell(gov::kNext);
        CHECK(r.V().pickPage == 1 && r.V().act[0] == gov::kGridCount);
        r.ClickCell(gov::kPrev);
        gov::Out o = r.ClickCell(gov::kGrid0 + 0);   // Town Portal
        CHECK(o.bindAction && !o.send && o.bindGesture == 11 && std::strcmp(o.action, "d2r.town_portal") == 0 && r.V().screen == gov::Screen::List && r.V().note == gov::Note::Bound);
        r.actions.push_back({11, "d2r.town_portal"});
        r.Step();
        CHECK(std::strcmp(r.V().row[0].action, "d2r.town_portal") == 0 && r.V().row[1].action[0] == 0);
        // opened again it shows the actions, its action framed; Skills back; a skill (BIND) or No skill takes the action away (the engine's rule)
        r.ClickCell(gov::RowCell(0, gov::kRowBind));
        CHECK(r.V().bindTab == 1 && std::strcmp(r.V().bindAction, "d2r.town_portal") == 0);
        r.ClickCell(gov::kBindTabSkills);
        CHECK(r.V().bindTab == 0);
        o = r.ClickCell(gov::kGrid0 + 1);
        CHECK(o.send && o.cmd.op == BW_LINK_GC_BIND && !o.bindAction);
        r.bw.Act(r.seq);
        r.Step();
        // without the engine's call: refused, said why
        Rig q;
        q.actionsOk = false;
        q.Step();
        q.ClickCell(gov::RowCell(0, gov::kRowBind));
        q.ClickCell(gov::kBindTabActions);
        o = q.ClickCell(gov::kGrid0 + 3);
        CHECK(!o.bindAction && q.V().screen == gov::Screen::BindSkill && q.V().note == gov::Note::NoActions && o.feedback == gov::Feedback::Refused);
        gov::R tr;
        CHECK(gov::CellRect(q.V(), gov::kBindTabSkills, &tr) && !Overlap(gov::BindTabRect(0), gov::BindTabRect(1)) && !Overlap(gov::BindTabRect(1), gov::GridRect(0)));
    }
}

// -- a glyph's hand: Right / Left / Either (BW_LINK_GH_BOTH), chosen when new, stepped on its row and its review --------------------------------------
static void TestGlyphHand() {
    {   // new: the third choice is Either - the RECORD asks for both (either trigger the pen)
        Rig r;
        r.Step();
        r.ClickCell(gov::kNew);
        r.ClickCell(gov::kChoice0 + 0);
        r.ClickCell(gov::kChoice0 + 2);
        CHECK(r.V().screen == gov::Screen::NewSkill && r.V().newHand == BW_LINK_GH_BOTH);
        const gov::Out o = r.ClickCell(gov::kGrid0 + 0);
        CHECK(o.send && o.cmd.type == BW_LINK_GT_GLYPH && o.cmd.hand == BW_LINK_GH_BOTH);
        CHECK(gov::NextGlyphHand(BW_LINK_GH_RIGHT) == BW_LINK_GH_LEFT && gov::NextGlyphHand(BW_LINK_GH_LEFT) == BW_LINK_GH_BOTH && gov::NextGlyphHand(BW_LINK_GH_BOTH) == BW_LINK_GH_RIGHT);
    }
    {   // the row: a glyph's type line steps its hand (the engine's own call, no command); not a pose's, not a motion's
        Rig r;
        r.Step();
        CHECK(gov::PartUsable(r.V().row[0].g, gov::kHand) && !gov::PartUsable(r.V().row[1].g, gov::kHand) && !gov::PartUsable(r.V().row[2].g, gov::kHand));
        gov::Out o = r.ClickCell(gov::RowCell(0, gov::kHand));
        CHECK(o.handSet && !o.send && o.handGesture == 11 && o.hand == BW_LINK_GH_LEFT && r.V().note == gov::Note::HandSet);
        r.bw.g.g[0].hand = BW_LINK_GH_BOTH;
        r.Step();
        o = r.ClickCell(gov::RowCell(0, gov::kHand));
        CHECK(o.handSet && o.hand == BW_LINK_GH_RIGHT);
        // without the engine's call: said to record it again
        Rig q;
        q.handsOk = false;
        q.Step();
        o = q.ClickCell(gov::RowCell(0, gov::kHand));
        CHECK(!o.handSet && q.V().note == gov::Note::NoHandSet && o.feedback == gov::Feedback::Refused);
    }
    {   // the review: its hand line steps it
        Rig v;
        v.Step();
        v.ClickCell(gov::RowCell(0, gov::kRowRerecord));
        v.bw.Act(v.seq);
        v.bw.g.recordState = BW_LINK_REC_READY;
        v.bw.g.recordGesture = 11;
        v.Step();
        v.bw.g.recordState = BW_LINK_REC_SAVED;
        v.bw.g.g[0].flags |= BW_LINK_GF_REVIEW;
        v.Step();
        gov::R hr;
        CHECK(v.V().screen == gov::Screen::Review && gov::CellRect(v.V(), gov::kRevHand, &hr));
        const gov::Out o = v.ClickCell(gov::kRevHand);
        CHECK(o.handSet && o.handGesture == 11 && o.hand == BW_LINK_GH_LEFT && v.V().screen == gov::Screen::Review);
    }
}

// -- a gesture's mode: cast once / select (its skill onto the right trigger), on its row's skill line and its review --------------------------------
static void TestMode() {
    {
        Rig r;
        r.Step();
        CHECK(r.V().row[0].mode == 0 && gov::PartUsable(r.V().row[0].g, gov::kMode) && !gov::PartUsable(r.V().row[3].g, gov::kMode));   // (a button: none)
        gov::Out o = r.ClickCell(gov::RowCell(0, gov::kMode));
        CHECK(o.modeSet && !o.send && o.modeGesture == 11 && o.mode == 1 && r.V().note == gov::Note::ModeSet);
        r.modes.push_back({11, 1});
        r.Step();
        CHECK(r.V().row[0].mode == 1 && r.V().row[1].mode == 0);
        o = r.ClickCell(gov::RowCell(0, gov::kMode));
        CHECK(o.modeSet && o.mode == 0);
        // an action is done once: no mode
        r.actions.push_back({12, "d2r.cmd.17"});
        r.bw.g.g[1].skill = -1;
        r.Step();
        CHECK(!gov::PartUsable(r.V().row[1].g, gov::kMode));
        // without the engine's call: refused, said why
        Rig q;
        q.modesOk = false;
        q.Step();
        o = q.ClickCell(gov::RowCell(2, gov::kMode));
        CHECK(!o.modeSet && q.V().note == gov::Note::NoModeSet && o.feedback == gov::Feedback::Refused);
        // the rows' cells: ten a row, under the grids' (the parts fit)
        CHECK(gov::IsRowCell(gov::RowCell(5, gov::kMode)) && gov::RowOf(gov::RowCell(5, gov::kMode)) == 5 && gov::PartOf(gov::RowCell(5, gov::kMode)) == gov::kMode && !gov::IsRowCell(gov::kGrid0));
    }
    {   // the review: the line's right half flips it, its left half a glyph's hand
        Rig v;
        v.Step();
        v.ClickCell(gov::RowCell(0, gov::kRowRerecord));
        v.bw.Act(v.seq);
        v.bw.g.recordState = BW_LINK_REC_READY;
        v.bw.g.recordGesture = 11;
        v.Step();
        v.bw.g.recordState = BW_LINK_REC_SAVED;
        v.bw.g.g[0].flags |= BW_LINK_GF_REVIEW;
        v.Step();
        gov::R a, b;
        CHECK(gov::CellRect(v.V(), gov::kRevMode, &a) && gov::CellRect(v.V(), gov::kRevHand, &b) && !Overlap(a, b));
        gov::Out o = v.ClickCell(gov::kRevMode);
        CHECK(o.modeSet && o.modeGesture == 11 && o.mode == 1 && v.V().screen == gov::Screen::Review);
        o = v.ClickCell(gov::kRevHand);
        CHECK(o.handSet && !o.modeSet);
    }
}

// -- each trigger's role (vr/trigger_roles.h): a glyph trigger tells a cast from a stroke by the hand's motion at the press ---------------------------
static void TestTriggerRoles() {
    using namespace trigroles;
    // (the Hold rule, [gestures] glyph_cast_hold=1: these first checks; the Release rule - the default - after them)
    auto Run = [](Hand& h, Role role, int hand, double t, float trig, float x, bool armed, bool allowed = true) {
        In in;
        in.time = t;
        in.allowed = allowed;
        in.trigger = trig;
        in.pos[0] = x;
        in.posOk = true;
        in.armed = armed;
        in.rule = CastRule::Hold;
        return h.Step(role, hand, in);
    };
    auto Rel = [](Hand& h, int hand, double t, float trig, float x, bool armed, bool point = false, double recognised = -1.0) {
        In in;
        in.time = t;
        in.allowed = true;
        in.trigger = trig;
        in.pos[0] = x;
        in.posOk = true;
        in.armed = armed;
        in.armedPoint = point;
        in.recognisedAt = recognised;
        return h.Step(Role::Glyphs, hand, in);
    };
    {   // Release: drawing (6 cm within 0.12 s) is a stroke - a pen to the let-go, no cast whether or not it matched
        Hand h;
        Out o = Rel(h, 1, 0.0, 0.9f, 0.0f, true);
        CHECK(o.pen && !o.aiming && h.State() == Press::Deciding);
        o = Rel(h, 1, 0.06, 0.9f, 0.09f, true);
        CHECK(o.pen && h.State() == Press::Stroke);
        o = Rel(h, 1, 0.8, 0.9f, 0.09f, true);   // (still now: still drawing - it never turns to an aim)
        CHECK(o.pen && !o.aiming && !o.castHold);
        o = Rel(h, 1, 0.9, 0.0f, 0.09f, true);
        CHECK(!o.castTap && !o.castHold && !o.pen);
    }
    {   // Release: held still - an aim (the stroke cut), its line for a flying skill (the host's), the cast once at the let-go along this hand
        Hand h;
        Rel(h, 1, 0.0, 0.9f, 0.0f, true);
        Out o = Rel(h, 1, 0.13, 0.9f, 0.01f, true);
        CHECK(!o.pen && o.aiming && !o.castHold && !o.castTap);
        o = Rel(h, 1, 1.5, 0.9f, 0.20f, true);   // (the hand moves now: it aims, it does not start a glyph)
        CHECK(o.aiming && !o.pen && h.State() == Press::Cast);
        o = Rel(h, 1, 1.6, 0.0f, 0.20f, true);
        CHECK(o.castTap && o.rt && !o.lt && o.aimEnd && !o.aiming);
    }
    {   // Release: a point skill armed - its slot held while aiming (point_cast's arc and circle), the let-go is point_cast's cast (no tap of ours)
        Hand h;
        Rel(h, 0, 0.0, 0.9f, 0.0f, true, true);
        Out o = Rel(h, 0, 0.13, 0.9f, 0.0f, true, true);
        CHECK(o.aiming && o.castHold && o.lt && !o.rt);   // (the left: the second layer, LT first)
        o = Rel(h, 0, 0.17, 0.9f, 0.0f, true, true);
        CHECK(o.castHold && o.lt && o.rt);
        o = Rel(h, 0, 0.5, 0.0f, 0.0f, true, true);
        CHECK(!o.castTap && !o.castHold && o.aimEnd);
    }
    {   // Release: a quick still tap - one cast at the let-go; nothing armed - no cast at all (an aim with nothing to aim)
        Hand h;
        Rel(h, 1, 0.0, 0.9f, 0.0f, true);
        Out o = Rel(h, 1, 0.05, 0.0f, 0.0f, true);
        CHECK(o.castTap && o.rt);
        Hand n;
        Rel(n, 1, 0.0, 0.9f, 0.0f, false);
        o = Rel(n, 1, 0.3, 0.9f, 0.0f, false);
        CHECK(!o.aiming && !o.pen);
        o = Rel(n, 1, 0.35, 0.0f, 0.0f, false);
        CHECK(!o.castTap);
    }
    {   // the icon cache's aims field (the game's skills.txt: a server missile, not passive, not an aura); an older cache's lines: not known
        wheelicons::Entry e;
        CHECK(wheelicons::ParseLine("s36\t" + std::string(64, 'a') + ".png\tskillname36\t1\t1\t1", &e) && e.aims == 1 && e.page == 1);
        CHECK(wheelicons::ParseLine("s40\t" + std::string(64, 'b') + ".png\tskillname40\t1\t3\t0", &e) && e.aims == 0);
        CHECK(wheelicons::ParseLine("s47\t" + std::string(64, 'c') + ".png\tskillname47\t1\t3", &e) && e.aims == -1);
        CHECK(wheelicons::ParseLine("s359\t" + std::string(64, 'd') + ".png\tkey\t\t\t1", &e) && e.cls == -1 && e.aims == 1);
    }
    {   // as before: nothing taken, nothing pressed
        Hand h;
        const Out o = Run(h, Role::AsBefore, 1, 0.0, 1.0f, 0.0f, true);
        CHECK(!o.take && !o.pen && !o.castHold && !o.lt && !o.rt);
    }
    {   // a still press, armed: a pen until 0.12 s, then the armed slot held (the right: RT) for as long as the trigger is; let go: nothing more
        Hand h;
        Out o = Run(h, Role::Glyphs, 1, 0.0, 0.9f, 0.0f, true);
        CHECK(o.take && o.pen && !o.castHold && h.State() == Press::Deciding);
        o = Run(h, Role::Glyphs, 1, 0.08, 0.9f, 0.01f, true);
        CHECK(o.pen && !o.castHold);
        o = Run(h, Role::Glyphs, 1, 0.13, 0.9f, 0.01f, true);
        CHECK(!o.pen && o.castHold && o.rt && !o.lt && h.State() == Press::Cast);
        o = Run(h, Role::Glyphs, 1, 1.0, 0.9f, 0.02f, true);
        CHECK(o.castHold && o.rt);   // (held: the game repeats it)
        o = Run(h, Role::Glyphs, 1, 1.02, 0.0f, 0.02f, true);
        CHECK(!o.castHold && !o.castTap && o.take && h.State() == Press::None);
    }
    {   // the hand moving 6 cm first: a stroke - a pen to the end, never a cast
        Hand h;
        Run(h, Role::Glyphs, 1, 0.0, 0.9f, 0.0f, true);
        Out o = Run(h, Role::Glyphs, 1, 0.05, 0.9f, 0.08f, true);
        CHECK(o.pen && h.State() == Press::Stroke);
        o = Run(h, Role::Glyphs, 1, 0.5, 0.9f, 0.08f, true);   // (still now: still a stroke)
        CHECK(o.pen && !o.castHold);
        o = Run(h, Role::Glyphs, 1, 0.6, 0.0f, 0.08f, true);
        CHECK(!o.pen && !o.castTap);
    }
    {   // a quick still tap, armed: one cast at its release; not armed: nothing
        Hand h;
        Run(h, Role::Glyphs, 1, 0.0, 0.9f, 0.0f, true);
        Out o = Run(h, Role::Glyphs, 1, 0.06, 0.0f, 0.0f, true);
        CHECK(o.castTap && o.rt && !o.lt);
        Hand n;
        Run(n, Role::Glyphs, 1, 0.0, 0.9f, 0.0f, false);
        o = Run(n, Role::Glyphs, 1, 0.3, 0.9f, 0.0f, false);
        CHECK(o.pen && !o.castHold);   // (not armed: a still press stays a pen - nothing to cast)
        o = Run(n, Role::Glyphs, 1, 0.35, 0.0f, 0.0f, false);
        CHECK(!o.castTap);
    }
    {   // the left: the second layer - LT first, RT a tick later
        Hand h;
        Run(h, Role::Glyphs, 0, 0.0, 0.9f, 0.0f, true);
        Out o = Run(h, Role::Glyphs, 0, 0.13, 0.9f, 0.0f, true);
        CHECK(o.castHold && o.lt && !o.rt);
        o = Run(h, Role::Glyphs, 0, 0.17, 0.9f, 0.0f, true);
        CHECK(o.castHold && o.lt && o.rt);
        CHECK(ArmSlot(0) == 11 && ArmSlot(1) == 9);
    }
    {   // Shift: LT while held; Action: its edges; not allowed (a panel, a menu, not F4): nothing taken, a press under way forgotten
        Hand s;
        Out o = Run(s, Role::Shift, 1, 0.0, 0.9f, 0.0f, false);
        CHECK(o.take && o.lt && !o.rt);
        Hand a;
        o = Run(a, Role::Action, 0, 0.0, 0.9f, 0.0f, false);
        CHECK(o.take && o.actionDown && o.actionHeld);
        o = Run(a, Role::Action, 0, 0.1, 0.0f, 0.0f, false);
        CHECK(o.actionUp && !o.actionHeld);
        Hand g;
        Run(g, Role::Glyphs, 1, 0.0, 0.9f, 0.0f, true);
        o = Run(g, Role::Glyphs, 1, 0.05, 0.9f, 0.0f, true, false);
        CHECK(!o.take && !o.pen && g.State() == Press::None);
        CHECK(RoleOf(1) == Role::Glyphs && RoleOf(3) == Role::Action && RoleOf(9) == Role::AsBefore);
    }
    {   // the engine's pens: with the roles in use, only the glyph triggers' strokes
        gengine::PenIn pi;
        pi.play = true;
        pi.roles = true;
        pi.rolePen[0] = true;
        uint8_t pen[2];
        gengine::Pens(pi, pen);
        CHECK(pen[0] == 1 && pen[1] == 0);
        pi.roles = false;
        gengine::Pens(pi, pen);
        CHECK(pen[0] == 1 && pen[1] == 1);   // (engine_pen as before: both)
    }
    {   // the Triggers screen: the footer's TRIGGERS, a role per trigger, an Action asks for its action (the actions' grid), back to the screen
        Rig r;
        r.Step();
        int cells[5];
        CHECK(gov::FooterCells(r.V(), cells) == 5 && cells[3] == gov::kTriggers);
        for (int i = 0; i < 5; ++i) CHECK(Inside(gov::FooterRect(i, 5), gov::R{0, 0, gov::kW, gov::kH}) && (i == 0 || !Overlap(gov::FooterRect(i, 5), gov::FooterRect(i - 1, 5))));
        r.ClickCell(gov::kTriggers);
        CHECK(r.V().screen == gov::Screen::Triggers && r.m.WantsBack());
        gov::Out o = r.ClickCell(gov::kTrigRole0 + 0 * gov::kTrigRoles + 1);   // the right trigger: glyphs
        CHECK(o.trigSet && o.trigRow == 0 && o.trigRole == 1 && !o.send);
        o = r.ClickCell(gov::kTrigRole0 + 1 * gov::kTrigRoles + 3);   // the left: an action - none yet: the grid
        CHECK(!o.trigSet && r.V().screen == gov::Screen::BindSkill && r.V().bindTrigger == 2 && r.V().bindTab == 1);
        r.ClickCell(gov::kBindTabSkills);
        CHECK(r.V().bindTab == 1);   // (a trigger's: actions only)
        o = r.ClickCell(gov::kGrid0 + 2);   // Potion - belt 1
        CHECK(o.trigSet && o.trigRow == 1 && o.trigRole == 3 && std::strcmp(o.trigAction, "d2r.cmd.17") == 0 && r.V().screen == gov::Screen::Triggers);
        r.trigRole[1] = 3;
        r.trigAction[1] = "d2r.cmd.17";
        r.Step();
        gov::R ar;
        CHECK(gov::CellRect(r.V(), gov::kTrigAction0 + 1, &ar) && !gov::CellRect(r.V(), gov::kTrigAction0, &ar));
        o = r.ClickCell(gov::kTrigRole0 + 1 * gov::kTrigRoles + 3);   // an action again: the one it has, kept
        CHECK(o.trigSet && std::strcmp(o.trigAction, "d2r.cmd.17") == 0);
        r.ClickCell(gov::kTrigAction0 + 1);   // another action: the grid on its page, back key: the Triggers screen
        CHECK(r.V().screen == gov::Screen::BindSkill && std::strcmp(r.V().bindAction, "d2r.cmd.17") == 0);
        r.Step(-1, -1, false, true);
        CHECK(r.V().screen == gov::Screen::Triggers);
        r.Step(-1, -1, false, true);
        CHECK(r.V().screen == gov::Screen::List);
        // the screen works with BodyWalk away (it asks it nothing)
        Rig q;
        q.have = false;
        q.Step();
        q.ClickCell(gov::kTriggers);
        o = q.ClickCell(gov::kTrigRole0 + 2);
        CHECK(q.V().screen == gov::Screen::Triggers && o.trigSet && o.trigRole == 2);
        for (int row = 0; row < 2; ++row)
            for (int role = 0; role < gov::kTrigRoles; ++role) CHECK(!Overlap(gov::TrigRoleRect(row, role), gov::TrigActionRect(row)) && gov::TrigRoleRect(row, role).b < gov::kNoteLine.t);
    }
}

static void TestController() {
    {   // the key off: two tabs, the third's place is nothing; the gestures block is not even asked about
        World w(false);
        w.Open();
        CHECK(w.c.Open() && w.c.ViewNow().tabs && w.c.ViewNow().tabCount == 2);
        w.ClickTab(2, 3);
        CHECK(w.c.ViewNow().tab != wheel::Tab::Gestures && !w.c.ViewNow().gestures);
        CHECK(!w.c.GesturesModel().Recording());
    }
    {   // the key on, F4: three tabs; the Gestures tab clicked to; its screens take the beam and the trigger; B / Y step back on them
        World w(true);
        w.Open();
        CHECK(w.c.ViewNow().tabCount == 3);
        w.ClickTab(2, 3);
        CHECK(w.c.ViewNow().tab == wheel::Tab::Gestures && w.c.ViewNow().gestures && w.c.ViewNow().gestures->rows == 4);
        // a flick on the Gestures tab does not flip the window
        w.f.hand[1].stick[0] = 0.9f;
        w.Next();
        w.f.hand[1].stick[0] = 0.0f;
        w.Next();
        CHECK(w.c.ViewNow().tab == wheel::Tab::Gestures);
        // NEW GESTURE, a type: the back key steps back to the list, the window stays up
        w.ClickRect(gov::FooterRect(1, 5));   // (the list's footer: < NEW ZONES TRIGGERS >)
        CHECK(w.c.GesturesModel().Shown() == gov::Screen::NewType);
        w.Hand(1, 0, di::kSecondary);
        w.Next();
        w.Hand(1, 0, 0);
        w.Next();
        CHECK(w.c.Open() && w.c.GesturesModel().Shown() == gov::Screen::List);
        // on the list the back key closes the window as ever
        w.Hand(1, 0, di::kSecondary);
        w.Next();
        w.Hand(1, 0, 0);
        w.Next();
        CHECK(!w.c.Open());
    }
    {   // a recording: the command goes out, the window is blind (no beam, lower and smaller), B stops it; F1 - F3 have no Gestures tab
        World w(true);
        w.Open();
        w.ClickTab(2, 3);
        const wheel::Step onList = w.Next();   // (the Gestures tab's list, the window up)
        const wheel::Step s = w.ClickRect(gov::PartRect(0, gov::kRowRerecord));
        CHECK(s.gestureSend && s.gestureCmd.op == BW_LINK_GC_RECORD && s.gestureCmd.gesture == 11);
        w.bw.Act(w.seq);
        w.bw.g.recordState = BW_LINK_REC_READY;
        w.bw.g.recordGesture = 11;
        const wheel::Step r = w.Next();
        CHECK(r.gestureRecording && r.layer.show && !r.layer.beam);
        // [openxr] hands_over_panels runs on the Gestures tab - its list and its take alike (vr/near_overlay.h Wanted) - and nowhere else
        CHECK(r.gesturesTab && nearoverlay::Wanted(nearoverlay::kGesturesTab, true, true, true, r.gesturesTab));
        CHECK(!onList.gestureRecording && onList.gesturesTab && nearoverlay::Wanted(nearoverlay::kGesturesTab, true, true, true, onList.gesturesTab));
        CHECK(!nearoverlay::Wanted(nearoverlay::kOff, true, true, true, onList.gesturesTab));
        // the window keeps its size and distance but stands aside - to the left of a right-hand take - and the space straight ahead is free
        CHECK(Near(r.layer.widthM, wheel::PanelWidthM(w.c.Config()), 1e-5f) && Near(r.layer.heightM, wheel::PanelHeightM(w.c.Config()), 1e-5f));
        {
            const wheel::Pose home = w.c.PanelPose(), aside = r.layer.panel;
            const wheel::V3 head{w.f.head.pos[0], w.f.head.pos[1], w.f.head.pos[2]};
            CHECK(Near(wheel::Len(wheel::Sub(aside.p, head)), wheel::Len(wheel::Sub(home.p, head)), 1e-3f));   // (as far from the head)
            CHECK(Near(aside.p.y, home.p.y, 1e-4f) && aside.p.x < home.p.x - 0.3f);                         // (level, to the left)
            const wheel::V3 ahead = wheel::Unit(wheel::Sub(home.p, head));
            CHECK(!wheel::IntersectPanel(head, ahead, aside, r.layer.widthM, r.layer.heightM).hit);           // (straight ahead: nothing)
            const wheel::V3 toAside = wheel::Unit(wheel::Sub(aside.p, head));
            CHECK(wheel::IntersectPanel(head, toAside, aside, r.layer.widthM, r.layer.heightM).hit);           // (it faces the head)
            // the near edge some 20 degrees off straight ahead
            const float swing = std::atan2(0.5f * r.layer.widthM, w.c.Config().distanceM) + wheel::kAsideClearRad;
            const float yaw = std::atan2(-(aside.p.x - head.x), -(aside.p.z - head.z));
            CHECK(Near(yaw, swing, 0.02f));
        }
        CHECK(r.take[1] & di::kTakeTrigger);   // (the layout does not attack with the pen's pull)
        // the pull while blind: no click anywhere (not the tab strip either)
        w.ClickTab(0, 3);
        CHECK(w.c.ViewNow().tab == wheel::Tab::Gestures && w.c.GesturesModel().Recording());
        w.Hand(1, 0, di::kSecondary);
        const wheel::Step b = w.Next();
        w.Hand(1, 0, 0);
        CHECK(b.gestureSend && b.gestureCmd.op == BW_LINK_GC_STOP && w.c.Open());
        w.Next();
        CHECK(!w.c.GesturesModel().Recording());
        // VR F1: the window alone - two tabs, the Gestures tab gone (back on Skills)
        w.f4 = false;
        w.c.SetView(2);
        w.Next();
        w.Open();
        CHECK(w.c.ViewNow().tabCount == 2 && w.c.ViewNow().tab != wheel::Tab::Gestures);
    }
    {   // the tab's fix goes out of the controller's step (the host does it)
        World w(true);
        w.gin.link = false;
        w.Open();
        w.ClickTab(2, 3);
        const wheel::Step s = w.ClickRect(gov::kFixRect);
        CHECK(s.gestureFix == gov::Fix::EnableLink);
        w.gin.link = true;
        w.gin.g = nullptr;
        w.gin.missing = gov::Avail::NotRunning;
        w.gin.canStart = true;
        w.Next();
        const wheel::Step t = w.ClickRect(gov::kFixRect);
        CHECK(t.gestureFix == gov::Fix::StartBodyWalk && w.c.Open());
    }
    {   // leaving F4 while recording: stopped
        World w(true);
        w.Open();
        w.ClickTab(2, 3);
        w.ClickRect(gov::PartRect(0, gov::kRowRerecord));
        w.bw.Act(w.seq);
        w.bw.g.recordState = BW_LINK_REC_DRAWING;
        w.bw.g.recordGesture = 11;
        w.Next();
        CHECK(w.c.GesturesModel().Recording());
        w.f4 = false;
        w.c.SetView(0);
        const wheel::Step s = w.Next();
        CHECK(s.gestureSend && s.gestureCmd.op == BW_LINK_GC_STOP && !w.c.GesturesModel().Recording());
    }
}

// -- the pictures of the window -------------------------------------------------------------------------------------------------------------------------

static void WriteBmp(const std::string& path, const std::vector<uint32_t>& px, int w, int h) {
    const uint32_t bytes = (uint32_t)(px.size() * 4);
    uint8_t hd[54] = {'B', 'M'};
    const uint32_t size = 54 + bytes, off = 54, dib = 40, ww = (uint32_t)w, hh = (uint32_t)(-h), planes = 1, bpp = 32;
    std::memcpy(hd + 2, &size, 4); std::memcpy(hd + 10, &off, 4); std::memcpy(hd + 14, &dib, 4); std::memcpy(hd + 18, &ww, 4); std::memcpy(hd + 22, &hh, 4);
    std::memcpy(hd + 26, &planes, 2); std::memcpy(hd + 28, &bpp, 2); std::memcpy(hd + 34, &bytes, 4);
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)hd, 54);
    for (uint32_t c : px) {
        const uint32_t out = ((c >> 16) & 0xFF) | (((c >> 8) & 0xFF) << 8) | ((c & 0xFF) << 16) | 0xFF000000u;
        f.write((const char*)&out, 4);
    }
}

static int Differ(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, gov::R r) {
    int n = 0;
    for (int y = std::max(0, int(r.t)); y < std::min(tiles::kH, int(r.b)); ++y)
        for (int x = std::max(0, int(r.l)); x < std::min(tiles::kW, int(r.r)); ++x)
            if (a[(size_t)y * tiles::kW + x] != b[(size_t)y * tiles::kW + x]) ++n;
    return n;
}

static int Bright(const std::vector<uint32_t>& a, gov::R r) {   // pixels lighter than the window's dark grounds
    int n = 0;
    for (int y = std::max(0, int(r.t)); y < std::min(tiles::kH, int(r.b)); ++y)
        for (int x = std::max(0, int(r.l)); x < std::min(tiles::kW, int(r.r)); ++x) {
            const uint32_t c = a[(size_t)y * tiles::kW + x];
            if ((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF) > 330) ++n;
        }
    return n;
}

static void TestPaint(const char* dumpDir) {
    // the views: the window's top level on the Gestures tab, each screen of the tab
    Rig r;
    r.bw.g.g[0].lastPercent = 92.0f;
    r.bw.g.lastStamp = 5;
    r.bw.g.lastGesture = 11;
    r.bw.g.lastPercent = 92.0f;
    r.Step();
    wheel::View base;
    base.open = true;
    base.style = wheel::Style::Tiles;
    base.tabs = true;
    base.tabCount = 2;
    base.ready = true;
    std::vector<uint32_t> two, twoWithGestures, list;
    tilespaint::Render(base, two);
    // unticked draws nothing of it: a view that has the gestures' copy but two tabs is the window as before
    wheel::View held = base;
    held.gestures = std::make_shared<const gov::View>(r.V());
    tilespaint::Render(held, twoWithGestures);
    CHECK(two == twoWithGestures);
    // three tabs, the Gestures tab shown: the strip has the third tab, the rows are drawn
    wheel::View g = held;
    g.tabCount = 3;
    g.tab = wheel::Tab::Gestures;
    tilespaint::Render(g, list);
    CHECK(Differ(two, list, FromP(tiles::TabRect(2, 3))) > 500);
    for (int i = 0; i < 4; ++i) CHECK(Differ(two, list, gov::PicRect(i)) > 300);   // each picture
    CHECK(Bright(list, gov::PartRect(3, gov::kThrLess)) * 4 < Bright(list, gov::PartRect(0, gov::kThrLess)));   // (a button: no threshold arrows)
    std::vector<std::pair<std::string, std::vector<uint32_t>>> shots;
    std::vector<std::pair<std::string, wheel::View>> views;   // (each shot's view: painted again in the game look below)
    shots.push_back({"gestures_list", list});
    views.push_back({"gestures_list", g});
    auto ShotOf = [&](const char* name, const gov::View& gv) {
        wheel::View v = g;
        v.gestures = std::make_shared<const gov::View>(gv);
        std::vector<uint32_t> px;
        tilespaint::Render(v, px);
        shots.push_back({name, px});
        views.push_back({name, v});
        return px;
    };
    auto Shot = [&](const char* name) { return ShotOf(name, r.V()); };
    r.ClickCell(gov::kNew);
    Shot("gestures_new_type");
    r.ClickCell(gov::kChoice0 + 1);
    Shot("gestures_new_hand");
    r.ClickCell(gov::kChoice0 + 2);
    Shot("gestures_new_skill");
    r.ClickCell(gov::kGrid0 + 1);
    r.bw.Act(r.seq);
    r.bw.g.recordState = BW_LINK_REC_DRAWING;
    r.Step();
    const std::vector<uint32_t> rec = Shot("gestures_recording");
    CHECK(Differ(two, rec, gov::kRecState) > 1000);
    r.bw.g.g[4] = G(31, BW_LINK_GT_POSE, 47, "Fire Ball pose 2");
    r.bw.g.g[4].flags = BW_LINK_GF_REVIEW;
    r.bw.g.count = 5;
    r.bw.g.recordGesture = 31;
    r.bw.g.recordState = BW_LINK_REC_SAVED;
    r.Step();
    const std::vector<uint32_t> review = Shot("gestures_review_pose");
    CHECK(r.V().screen == gov::Screen::Review && Differ(two, review, gov::kReviewPic) > 20000);
    {   // a button's picture, big (a button is chosen in BodyWalk and never reviewed: the review screen here only shows its picture)
        static gov::View bv;
        bv = r.V();
        bv.review.g = r.bw.g.g[3];
        std::snprintf(bv.review.skill, sizeof bv.review.skill, "%s", "Frozen Armor");
        const std::vector<uint32_t> px = ShotOf("gestures_picture_button", bv);
        CHECK(Differ(two, px, gov::kReviewPic) > 20000);
    }
    {   // the Colour screen, a picture half played, a glyph's review with its swatches
        Rig cr;
        cr.Step();
        cr.ClickCell(gov::RowCell(0, gov::kColour));
        cr.Step(0.5f * (gov::SwatchRect(3, true).l + gov::SwatchRect(3, true).r), 0.5f * (gov::SwatchRect(3, true).t + gov::SwatchRect(3, true).b));
        const std::vector<uint32_t> px = ShotOf("gestures_colour", cr.V());
        CHECK(cr.V().screen == gov::Screen::Colour && Differ(two, px, gov::kColourPic) > 5000 && Differ(two, px, gov::SwatchRect(0, true)) > 3000);
        Rig pr;
        pr.Step();
        pr.ClickCell(gov::RowCell(0, gov::kPic));
        for (int i = 0; i < 70; ++i) pr.Step();
        const std::vector<uint32_t> half = ShotOf("gestures_playing", pr.V());
        CHECK(pr.V().playId == 11 && Differ(list, half, gov::PicRect(0)) > 100);
        Rig gr;
        gr.Step();
        gr.ClickCell(gov::RowCell(0, gov::kRowRerecord));
        gr.bw.Act(gr.seq);
        gr.bw.g.recordState = BW_LINK_REC_READY;
        gr.bw.g.recordGesture = 11;
        gr.Step();
        gr.bw.g.recordState = BW_LINK_REC_SAVED;
        gr.bw.g.g[0].flags |= BW_LINK_GF_REVIEW;
        gr.Step();
        const std::vector<uint32_t> rg = ShotOf("gestures_review_glyph_swatches", gr.V());
        Rig br;   // the bind screen's game actions
        br.Step();
        br.ClickCell(gov::RowCell(0, gov::kRowBind));
        br.ClickCell(gov::kBindTabActions);
        const std::vector<uint32_t> ba = ShotOf("gestures_bind_actions", br.V());
        CHECK(br.V().bindTab == 1 && Differ(two, ba, gov::GridRect(0)) > 2000 && Differ(two, ba, gov::BindTabRect(1)) > 500);
        CHECK(gr.V().screen == gov::Screen::Review && Differ(two, rg, gov::SwatchRect(2, false)) > 500);
    }
    // the missing pieces, each with its words (and its button where there is a fix)
    for (gov::Missing m : {gov::Avail::LinkOff, gov::Avail::NotRunning, gov::Avail::Starting, gov::Avail::NoGameLink, gov::Avail::NoSkills, gov::Avail::NoGestures}) {
        Rig mr;
        mr.link = m != gov::Avail::LinkOff;
        mr.have = false;
        mr.missing = m;
        mr.Step();
        const char* names[] = {"", "gestures_missing_link_off", "gestures_missing_not_running", "gestures_missing_starting", "gestures_missing_no_game_link",
                               "gestures_missing_no_skills", "gestures_missing_no_gestures"};
        const std::vector<uint32_t> px = ShotOf(names[int(m)], mr.V());
        CHECK(Differ(two, px, gov::kMissTitle) > 1000);
        // the button's place: lit where there is a fix, the dark panel where there is none
        CHECK((Bright(px, gov::kFixRect) > 300) == (gov::FixFor(m, true) != gov::Fix::None));
    }
    // the big pictures of the other kinds
    for (uint32_t k : {0u, 2u, 3u}) {
        r.bw.g.g[k].flags |= BW_LINK_GF_REVIEW;
        Rig rr;
        rr.bw = r.bw;
        rr.bw.g.recordGesture = r.bw.g.g[k].id;
        rr.Step();
        rr.ClickCell(gov::RowCell(int(k), gov::kRowRerecord));
        if (!rr.last.send) continue;   // (the button has no re-record)
        rr.bw.Act(rr.seq);
        rr.bw.g.recordState = BW_LINK_REC_READY;
        rr.Step();
        rr.bw.g.recordState = BW_LINK_REC_SAVED;
        rr.Step();
        CHECK(rr.V().screen == gov::Screen::Review);
        ShotOf(k == 0 ? "gestures_review_glyph" : "gestures_review_motion", rr.V());
    }
    // the game look (GAME_UI_CACHE: a folder D2R_VR_Settings made from the tester's own game - never in the repository): the same screens in the
    // game's art; without the art the key changes nothing
    {
        gameui::SetDir(L"C:\\no such folder\\d2r_vr_game_ui");
        gameui::SetOn(true);
        std::vector<uint32_t> px;
        tilespaint::Render(g, px);
        CHECK(px == list);
        gameui::SetOn(false);
        char dir[400] = {};
        if (GetEnvironmentVariableA("GAME_UI_CACHE", dir, sizeof dir)) {
            wchar_t wide[400] = {};
            MultiByteToWideChar(CP_UTF8, 0, dir, -1, wide, 399);
            gameui::SetDir(wide);
            gameui::SetOn(true);
            CHECK(gameui::Ready());
            tilespaint::Render(g, px);
            CHECK(px != list);
            for (const auto& sv : views) {   // every screen above, in the game's art
                tilespaint::Render(sv.second, px);
                shots.push_back({"game_" + sv.first, px});
            }
            gameui::SetOn(false);
            std::printf("  game look: the Gestures tab in the game's art\n");
        } else {
            std::printf("  game look: GAME_UI_CACHE not set, only the no-art case\n");
        }
    }
    if (dumpDir)
        for (const auto& s : shots) WriteBmp(std::string(dumpDir) + "\\" + s.first + ".bmp", s.second, tiles::kW, tiles::kH);
}

int main(int argc, char** argv) {
    const char* dump = nullptr;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--dump") == 0) dump = argv[i + 1];
    TestLayout();
    TestCmds();
    TestPictures();
    TestModelList();
    TestModelRecord();
    TestMissing();
    TestFlowFakeBodyWalk();
    TestControlsForBodyWalk();
    TestRuneFlash();
    TestEnginePure();
    TestEngineStub();
    TestEngineReal();
    TestZones();
    TestPlay();
    TestBindActions();
    TestGlyphHand();
    TestMode();
    TestTriggerRoles();
    TestController();
    TestPaint(dump);
    std::printf("gesture_overlay_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
