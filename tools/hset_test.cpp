// Tests for the settings panel in the headset ([openxr] headset_settings): its keys and model, the hit test
// on the painted atlas, the chord and the lasers with made-up poses, the release gate, the ini transaction
// (backup, conflict) on files in a temp folder, the ini thread and the picture. No headset, no game.
//
//   build\Release\hset_test.exe        (exit code 0 = all passed)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "hset_panel.h"
#include "view_gate.h"
#include "hset_paint.h"
#include "hset_store.h"
#include "ini_txn.h"

using namespace hset;

static int g_fail = 0, g_checks = 0;
#define CHECK(c, msg)                                                       \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, msg); } \
    } while (0)

// -- helpers ----------------------------------------------------------------------------
static Q Mul(const Q& a, const Q& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
// An aim pose at `from` pointing at the world point `to`.
static Pose Aim(V3 from, V3 to) {
    const V3 d = Unit(Sub(to, from));
    const float yaw = std::atan2(-d.x, -d.z);
    const float pitch = std::asin(d.y);
    const Q pq{std::sin(0.5f * pitch), 0.0f, 0.0f, std::cos(0.5f * pitch)};   // about +x: + looks up
    Pose p;
    p.p = from;
    p.q = Mul(YawQ(yaw), pq);
    p.valid = true;
    return p;
}
// The world point at (u, v) of a panel.
static V3 PanelPoint(const Pose& panel, float u, float v) {
    return Add(panel.p, Rot(panel.q, {(u - 0.5f) * kWidthM, (0.5f - v) * kHeightM, 0.0f}));
}
static void Centre(const Rect& r, float* u, float* v) {
    *u = (r.x + 0.5f * r.w) / kW;
    *v = (r.y + 0.5f * r.h) / kH;
}
static Snapshot Snap(bool ready = true) {
    Snapshot s;
    s.ready = ready;
    return s;
}
static Pose Head(float y = 1.6f) {
    Pose p;
    p.p = {0.0f, y, 0.0f};
    p.valid = true;
    return p;
}

// A scripted session: frames of 11 ms.
struct Rig {
    Controller c;
    double ms = 1000.0;
    Snapshot snap = Snap();
    Pose head = Head();
    Pose aim[2];
    bool present[2] = {true, true};
    Pad pad[2];
    bool enabled = true, focused = true;
    bool calibrating = false;
    video::Env env;
    int view = 4;
    FrameOut last;
    FrameOut Step(double dt = 11.0) {
        FrameIn f;
        ms += dt;
        f.ms = ms;
        f.enabled = enabled;
        f.focused = focused;
        f.calibrating = calibrating;
        f.env = env;
        f.view = view;
        f.head = head;
        for (int h = 0; h < 2; ++h) { f.aim[h] = aim[h]; f.present[h] = present[h]; f.pad[h] = pad[h]; }
        f.snapshot = snap;
        last = c.Step(f);
        if (last.action.what == Do::Calibrate) calibrating = true;   // the glue asks the plugin, whose measurement is running from the next frame
        return last;
    }
    void Run(double totalMs) {
        for (double t = 0; t < totalMs; t += 11.0) Step();
    }
    void Grips(bool on) {
        for (int h = 0; h < 2; ++h) {
            pad[h].squeeze = on ? 0.9f : 0.0f;
            if (on) pad[h].buttons |= kSqueeze; else pad[h].buttons &= ~kSqueeze;
        }
    }
    void StickDown(int hand, bool on) { if (on) pad[hand].buttons |= kStick; else pad[hand].buttons &= ~kStick; }
    // a click of a thumbstick: down for two frames
    FrameOut ClickStick(int hand) {
        StickDown(hand, true);
        FrameOut o = Step();
        Step();
        StickDown(hand, false);
        Step();
        return o;
    }
    // the chord: both grips squeezed, a beat, a click (the grips let go after)
    void Open() {
        Grips(true);
        Run(400);
        ClickStick(1);
        Grips(false);
        Step();
    }
    void Point(int hand, float u, float v) { aim[hand] = Aim({hand ? 0.25f : -0.25f, 1.3f, 0.0f}, PanelPoint(c.PanelPose(), u, v)); }
    void PullTrigger(int hand, bool on) { pad[hand].trigger = on ? 1.0f : 0.0f; if (on) pad[hand].buttons |= kTrigger; else pad[hand].buttons &= ~kTrigger; }
    // one click on a button, the trigger of `hand`
    FrameOut Click(int hand, const Rect& r) {
        float u, v;
        Centre(r, &u, &v);
        Point(hand, u, v);
        Step();
        PullTrigger(hand, true);
        FrameOut o = Step();
        PullTrigger(hand, false);
        Step();
        return o;
    }
};

// -- 1. the table of keys ----------------------------------------------------------------------
static void TestDefs() {
    std::set<std::wstring> seen;
    int perPage[kMaxPages] = {};
    for (int i = 0; i < kCount; ++i) {
        const Def& d = kDefs[i];
        CHECK(seen.insert(std::wstring(d.section) + L"/" + d.key).second, "a key twice in the table");
        CHECK(d.page == kNoPage || (d.page >= 0 && d.page < kMaxPages), "page in range, or no row (a hand-drawn page's key)");
        if (d.page >= 0 && d.page < kMaxPages) ++perPage[d.page];
        CHECK(d.min <= d.def && d.def <= d.max, "default inside its range");
        CHECK(d.max > d.min && d.step > 0.0f, "range and step make sense");
        CHECK(Normalize(i, d.def) == d.def || std::fabs(Normalize(i, d.def) - d.def) < 1e-4f, "the default is on the grid");
        CHECK(Normalize(i, Normalize(i, 1.2345f)) == Normalize(i, 1.2345f), "Normalize is idempotent");
        CHECK(Normalize(i, NAN) == d.def && Normalize(i, INFINITY) == d.def, "not a number is the default");
        CHECK(Normalize(i, d.min - 100.0f) == d.min && Normalize(i, d.max + 100.0f) == d.max, "clamped to the range");
        if (IsToggle(d)) CHECK(d.off[0] && d.on[0], "a toggle has two faces");
        else CHECK(d.fmt[0] == '%', "a stepper has a format");
        CHECK(d.label[0] && d.detail[0], "label and detail");
    }
    for (int p = 0; p < kMaxPages; ++p) CHECK(perPage[p] <= kRowsPerPage, "no more rows than the page has room for");
    CHECK(perPage[kPageView] <= kRowsPerPage - 1, "the first page keeps its last row for the game view buttons");
    CHECK(RowsOn(kPageView) > 0 && RowsOn(kPageOptions) > 0, "the two table pages have rows");
    CHECK(RowsOn(kPageVideo) == 0 && RowsOn(kPageBody) == 0, "the two hand-drawn pages have no table rows (they draw their own)");
    CHECK(PageCount() == 4 && PageCount() <= kMaxPages, "four pages, they fit the tabs");
    // the keys the hand-drawn pages work on are in the table, with no row, named as the plugin reads them
    const int hidden[6] = {kKeyMode, kKeyPx, kKeyUse, kKeyVersion, kKeyEye, kKeySpan};
    for (int k : hidden) CHECK(k >= 0 && k < kCount && kDefs[k].page == kNoPage, "a hand-drawn page's key has no row");
    CHECK(!wcscmp(kDefs[kKeyMode].section, L"render") && !wcscmp(kDefs[kKeyMode].key, L"vr_resolution") &&
              !wcscmp(kDefs[kKeyPx].section, L"render") && !wcscmp(kDefs[kKeyPx].key, L"vr_resolution_px"), "the render size's two keys");
    CHECK(!wcscmp(kDefs[kKeyUse].section, L"calibration") && !wcscmp(kDefs[kKeyUse].key, L"use") && !wcscmp(kDefs[kKeyVersion].key, L"version") &&
              !wcscmp(kDefs[kKeyEye].key, L"eye_height_m") && !wcscmp(kDefs[kKeySpan].key, L"arm_span_m"), "the measurement's four keys");
    CHECK(kDefs[kKeyPx].max == (float)rendersize::kSideMax, "the side's range ends where the plugin's does");
    CHECK(!std::strcmp(kPageNames[kPageVideo], "Video") && !std::strcmp(kPageNames[kPageBody], "Body"), "the tabs are called Video and Body");
    // keys exist in the shipped ini names the game reads
    bool has[5] = {};
    for (const Def& d : kDefs) {
        if (!wcscmp(d.key, L"menu_distance_m")) has[0] = true;
        if (!wcscmp(d.key, L"flat_width_m")) has[1] = true;
        if (!wcscmp(d.key, L"brightness")) has[2] = true;
        if (!wcscmp(d.key, L"user_height_m")) has[3] = true;
        if (!wcscmp(d.key, L"laser")) has[4] = true;
    }
    CHECK(has[0] && has[1] && has[2] && has[3] && has[4], "the starting keys are offered");
    // a step lands on the grid: 1.6 + 0.1 and back
    const int md = KeyAt(0, 0);
    CHECK(std::fabs(Normalize(md, 1.6f + 0.1f) - 1.7f) < 1e-4f, "a step of 0.1");
    CHECK(Changed(Defaults(), Defaults()) == 0, "no change between equals");
}

// -- 2. the buttons' places ---------------------------------------------------------------------
static bool Inside(const Rect& r) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= kW && r.y + r.h <= kH; }
static bool Overlap(const Rect& a, const Rect& b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static void TestHitTest() {
    CHECK(Inside(kApply) && Inside(kClose) && Inside(kRecentre) && Inside(kStatus), "bottom buttons inside the atlas");
    CHECK(!Overlap(kApply, kClose) && !Overlap(kClose, kRecentre) && !Overlap(kApply, kRecentre), "bottom buttons apart");
    CHECK(!Overlap(kStatus, kApply) && !Overlap(kStatus, kClose) && !Overlap(kStatus, kRecentre), "the status line is clear of the buttons");
    for (int i = 0; i < kMaxPages; ++i) CHECK(Inside(kTab(i)), "tabs inside the atlas");
    for (int i = 0; i + 1 < kMaxPages; ++i) CHECK(!Overlap(kTab(i), kTab(i + 1)), "tabs apart");
    for (int r = 0; r < kRowsPerPage; ++r) {
        CHECK(Inside(kDec(r)) && Inside(kInc(r)) && Inside(kVal(r)) && Inside(kToggle(r)), "row inside the atlas");
        CHECK(!Overlap(kDec(r), kVal(r)) && !Overlap(kVal(r), kInc(r)) && !Overlap(kDec(r), kInc(r)), "a row's buttons apart");
        CHECK(!Overlap(kDec(r), kStatus) && !Overlap(kToggle(r), kStatus), "rows end above the status line");
        if (r + 1 < kRowsPerPage) CHECK(!Overlap(kToggle(r), kToggle(r + 1)), "rows apart");
    }
    for (int p = 0; p < PageCount(); ++p) {
        for (int r = 0; r < RowsOn(p); ++r) {
            const int key = KeyAt(p, r);
            float u, v;
            if (IsToggle(kDefs[key])) {
                Centre(kToggle(r), &u, &v);
                CHECK((HitTest(p, u, v) == Hit{Kind::Change, key, 1}), "a toggle's centre toggles");
            } else {
                Centre(kDec(r), &u, &v);
                CHECK((HitTest(p, u, v) == Hit{Kind::Change, key, -1}), "minus");
                Centre(kInc(r), &u, &v);
                CHECK((HitTest(p, u, v) == Hit{Kind::Change, key, 1}), "plus");
                Centre(kVal(r), &u, &v);
                CHECK(HitTest(p, u, v).kind == Kind::None, "the value box is not a button");
            }
        }
        float u, v;
        Centre(kTab(p), &u, &v);
        CHECK((HitTest(p, u, v) == Hit{Kind::Tab, p, 0}), "a tab");
        Centre(kApply, &u, &v);
        CHECK(HitTest(p, u, v).kind == Kind::Apply, "Apply");
        Centre(kClose, &u, &v);
        CHECK(HitTest(p, u, v).kind == Kind::Close, "Close");
        Centre(kRecentre, &u, &v);
        CHECK(HitTest(p, u, v).kind == Kind::Recentre, "Recentre");
        // a row the page does not have is nothing
        const int rows = RowsOn(p);
        if (rows < kRowsPerPage && !(p == kPageView && rows == kViewRow)) {   // (the first page's last row is the view buttons)
            Centre(kToggle(rows), &u, &v);
            CHECK(HitTest(p, u, v).kind == Kind::None, "past the last row is nothing");
        }
    }
    CHECK(HitTest(0, -0.01f, 0.5f).kind == Kind::None && HitTest(0, 1.0f, 0.5f).kind == Kind::None &&
          HitTest(0, 0.5f, 1.0f).kind == Kind::None && HitTest(0, NAN, 0.5f).kind == Kind::None, "outside the atlas is nothing");
    // a key of the other page is not hit on this one
    if (PageCount() > 1) {
        float u, v;
        Centre(kToggle(0), &u, &v);
        const Hit a = HitTest(0, u, v), b = HitTest(1, u, v);
        CHECK(a != b, "the same place is a different key on another page");
    }
}

// -- 3. the model ---------------------------------------------------------------------------------
static void TestModel() {
    Snapshot s = Snap();
    s.values[0] = s.file[0] = 2.5f;   // menu distance as shipped
    Model m;
    m.Open(s);
    CHECK(m.open && m.ready && !m.Dirty() && m.page == 0, "opens clean on page 0");
    const int md = 0;
    Action a = m.Click({Kind::Change, md, 1});
    CHECK(a.what == Do::Nothing && m.Dirty() && std::fabs(m.Get()[md] - 2.6f) < 1e-4f, "plus is one step");
    CHECK(m.Click({Kind::Apply, -1, 0}).what == Do::Save, "Apply on a dirty draft saves");
    CHECK(m.Busy() && m.result == Result::Saving, "busy while saving");
    CHECK(m.Click({Kind::Change, md, 1}).what == Do::Nothing && std::fabs(m.Get()[md] - 2.6f) < 1e-4f, "no edits while saving");
    CHECK(m.Click({Kind::Apply, -1, 0}).what == Do::Nothing, "no second Apply while saving");

    Model n;
    n.Open(s);
    n.Click({Kind::Change, md, -1});
    n.Click({Kind::Change, md, -1});
    Action sv = n.Click({Kind::Apply, -1, 0});
    CHECK(sv.what == Do::Save && sv.request.changed == (1u << md), "only the changed key goes");
    CHECK(std::fabs(sv.request.base[md] - 2.5f) < 1e-4f && std::fabs(sv.request.values[md] - 2.3f) < 1e-4f, "base is the file's, values the draft");
    CHECK(sv.request.sequence != 0, "a sequence number");
    // the answer
    Snapshot ans = s;
    ans.values[md] = ans.file[md] = 2.3f;
    ans.sequence = sv.request.sequence;
    ans.result = Result::Saved;
    n.Observe(ans);
    CHECK(!n.Busy() && n.result == Result::Saved && !n.Dirty(), "Saved: clean again");
    // a conflict reloads
    Model c;
    c.Open(s);
    c.Click({Kind::Change, md, 1});
    Action cs = c.Click({Kind::Apply, -1, 0});
    Snapshot theirs = s;
    theirs.values[md] = theirs.file[md] = 4.0f;
    theirs.sequence = cs.request.sequence;
    theirs.result = Result::Conflict;
    c.Observe(theirs);
    CHECK(c.result == Result::Conflict && std::fabs(c.Get()[md] - 4.0f) < 1e-4f && !c.Dirty(), "conflict: the outside value is shown, nothing of ours kept");
    // a wrong sequence is not our answer
    Model w;
    w.Open(s);
    w.Click({Kind::Change, md, 1});
    Action ws = w.Click({Kind::Apply, -1, 0});
    Snapshot other = s;
    other.sequence = ws.request.sequence + 5;
    other.result = Result::Saved;
    w.Observe(other);
    CHECK(w.Busy(), "another sequence's answer is not ours");
    w.SubmitFailed(ws.request.sequence);
    CHECK(!w.Busy() && w.result == Result::Failed && w.Dirty(), "a refused hand-over: failed, edits kept");
    // an outside edit while clean is taken over, while dirty it is not
    Model o;
    o.Open(s);
    Snapshot out = s;
    out.values[md] = out.file[md] = 3.1f;
    o.Observe(out);
    CHECK(std::fabs(o.Get()[md] - 3.1f) < 1e-4f && !o.Dirty(), "a clean panel follows an outside edit");
    o.Click({Kind::Change, md, 1});
    Snapshot out2 = out;
    out2.values[md] = out2.file[md] = 5.0f;
    o.Observe(out2);
    CHECK(std::fabs(o.Get()[md] - 3.2f) < 1e-4f, "a dirty draft is not overwritten");
    // not ready: nothing to edit
    Model nr;
    nr.Open(Snap(false));
    CHECK(nr.Click({Kind::Change, md, 1}).what == Do::Nothing && !nr.Dirty(), "no ini, no edits");
    CHECK(nr.Click({Kind::Apply, -1, 0}).what == Do::Nothing, "no ini, no Apply");
    // limits
    Model lim;
    lim.Open(s);
    for (int i = 0; i < 200; ++i) lim.Click({Kind::Change, md, 1});
    CHECK(lim.Get()[md] == kDefs[md].max, "stops at the top");
    for (int i = 0; i < 400; ++i) lim.Click({Kind::Change, md, -1});
    CHECK(lim.Get()[md] == kDefs[md].min, "stops at the bottom");
    // toggles
    const int tg = KeyAt(1, 0);
    Model t;
    Snapshot ts = Snap();
    t.Open(ts);
    t.Click({Kind::Change, tg, 1});
    CHECK(t.Get()[tg] != ts.values[tg], "a toggle flips");
    t.Click({Kind::Change, tg, 1});
    CHECK(!t.Dirty(), "and flips back: clean");
    // tabs and the buttons that are not edits
    CHECK(t.Click({Kind::Tab, 1, 0}).what == Do::Nothing && t.page == 1, "a tab changes the page");
    CHECK(t.Click({Kind::Tab, 99, 0}).what == Do::Nothing && t.page == 1, "a tab that is not there does nothing");
    CHECK(t.Click({Kind::Close, -1, 0}).what == Do::Close, "Close is asked for");
    CHECK(t.Click({Kind::Recentre, -1, 0}).what == Do::Recentre, "Recentre is asked for");
    Model closed;
    CHECK(closed.Click({Kind::Close, -1, 0}).what == Do::Nothing, "a closed panel does nothing");
}

// -- 4. rays ---------------------------------------------------------------------------------------
static void TestRay() {
    Pose panel;
    panel.p = {0.0f, 1.5f, -1.0f};
    panel.valid = true;
    PanelHit h = IntersectPanel({0, 1.5f, 0}, {0, 0, -1}, panel, kWidthM, kHeightM);
    CHECK(h.hit && std::fabs(h.u - 0.5f) < 1e-4f && std::fabs(h.v - 0.5f) < 1e-4f && std::fabs(h.dist - 1.0f) < 1e-4f, "straight at the middle");
    h = IntersectPanel({0.3f, 1.5f, 0}, {0, 0, -1}, panel, kWidthM, kHeightM);
    CHECK(h.hit && std::fabs(h.u - (0.5f + 0.3f / kWidthM)) < 1e-4f, "right of the middle is right in u");
    h = IntersectPanel({0, 1.7f, 0}, {0, 0, -1}, panel, kWidthM, kHeightM);
    CHECK(h.hit && h.v < 0.5f, "above the middle is up in v");
    h = IntersectPanel({0, 1.5f, 0}, {0, 0, 1}, panel, kWidthM, kHeightM);
    CHECK(!h.hit, "pointing away");
    h = IntersectPanel({0, 1.5f, -2.0f}, {0, 0, 1}, panel, kWidthM, kHeightM);
    CHECK(!h.hit, "from behind the panel it is not hit");
    h = IntersectPanel({0, 1.5f, 0}, {1, 0, 0}, panel, kWidthM, kHeightM);
    CHECK(!h.hit, "parallel");
    h = IntersectPanel({5.0f, 1.5f, 0}, {0, 0, -1}, panel, kWidthM, kHeightM);
    CHECK(!h.hit && (h.u > 1.0f || h.u < 0.0f), "beside it");
    Pose none;
    CHECK(!IntersectPanel({0, 0, 0}, {0, 0, -1}, none, kWidthM, kHeightM).hit, "an unplaced panel is never hit");
    // a turned panel
    panel.q = YawQ(0.7f);
    panel.p = {-1.0f, 1.5f, -1.0f};
    const V3 target = PanelPoint(panel, 0.25f, 0.75f);
    const Pose aim = Aim({0.0f, 1.4f, 0.0f}, target);
    h = IntersectPanel(aim.p, Unit(Rot(aim.q, {0, 0, -1})), panel, kWidthM, kHeightM);
    CHECK(h.hit && std::fabs(h.u - 0.25f) < 2e-3f && std::fabs(h.v - 0.75f) < 2e-3f, "a turned panel is hit where it was aimed");
}

// -- 5. the chord, the clicks, the gate --------------------------------------------------------------
static void TestChord() {
    {   // both grips, a beat, a click
        Rig r;
        r.Grips(true);
        r.Run(200);
        CHECK(!r.last.open, "grips alone open nothing");
        r.Run(250);
        r.StickDown(1, true);
        FrameOut o = r.Step();
        CHECK(o.open && r.c.Open() && o.justOpened, "a click after 0.35 s of both grips opens it");
        CHECK(!(o.pad[1].buttons & kStick), "and is not the game's");
        for (int h = 0; h < 2; ++h) CHECK(o.pad[h].buttons == 0 && o.pad[h].trigger == 0 && o.pad[h].squeeze == 0, "all zero while open");
        CHECK(r.c.PanelPose().valid, "the panel stands somewhere");
        // 1 m ahead of the eyes (the head looks down -z), 5 cm below
        CHECK(std::fabs(r.c.PanelPose().p.z + kAheadM) < 1e-3f && std::fabs(r.c.PanelPose().p.y - (1.6f - kBelowM)) < 1e-3f, "placed ahead of the head");
        CHECK(r.c.Open(), "still open on the next frame");
        r.Step();
        CHECK(r.c.Open(), "the click that opened it does not close it");
    }
    {   // a click inside the 0.35 s is the game's, as always (swap weapons, Alt, recentre live there)
        Rig r;
        r.Grips(true);
        r.Run(150);
        r.StickDown(1, true);
        FrameOut o = r.Step();
        CHECK(!o.open && (o.pad[1].buttons & kStick), "a click right after the grips passes through");
    }
    {   // one grip + a click: the game's
        Rig r;
        r.pad[0].squeeze = 0.9f;
        r.Run(1000);
        FrameOut o = r.ClickStick(1);
        CHECK(!o.open && (o.pad[1].buttons & kStick), "one grip does not arm it");
        CHECK(!r.c.Open(), "closed");
    }
    {   // no grips: both sticks, however long, are the game's
        Rig r;
        r.StickDown(0, true);
        r.StickDown(1, true);
        bool passed = true;
        for (double t = 0; t < 2000; t += 11) {
            FrameOut o = r.Step();
            if (!(o.pad[0].buttons & kStick) || !(o.pad[1].buttons & kStick) || o.open) passed = false;
        }
        CHECK(passed, "both stick clicks alone are the game's (its layout has them)");
    }
    {   // armed: no stick click reaches the game, with the panel open or not
        Rig r;
        r.head.valid = false;   // it cannot open: the clicks are still taken
        r.Grips(true);
        r.Run(500);
        bool leaked = false;
        for (int h = 0; h < 2; ++h) {
            r.StickDown(h, true);
            FrameOut o = r.Step();
            if (o.pad[h].buttons & kStick) leaked = true;
            r.Run(60);
            if (r.last.pad[h].buttons & kStick) leaked = true;
            r.StickDown(h, false);
            r.Step();
        }
        CHECK(!leaked, "while both grips are squeezed (past the beat) the clicks are not the game's");
        CHECK(!r.c.Open(), "no head, no panel");
        r.head = Head();
        r.ClickStick(0);
        CHECK(r.c.Open(), "once the head is there the next click opens it (either stick)");
    }
    {   // a click already down when the grips arm is not a new click
        Rig r;
        r.StickDown(1, true);
        r.Step();
        r.Grips(true);
        r.Run(800);
        CHECK(!r.c.Open(), "a stick held from before is no chord click");
        CHECK(!(r.last.pad[1].buttons & kStick), "but it is held back once the chord is armed");
        r.StickDown(1, false);
        r.Step();
        r.ClickStick(0);
        CHECK(r.c.Open(), "a fresh click does");
    }
    {   // the panel placed where the head looks
        Rig r;
        r.head.q = YawQ(1.0f);
        r.head.p = {1.0f, 1.7f, 2.0f};
        r.Open();
        const Pose p = r.c.PanelPose();
        const V3 want = Add(r.head.p, Rot(YawQ(1.0f), {0.0f, -kBelowM, -kAheadM}));
        CHECK(std::fabs(p.p.x - want.x) < 1e-3f && std::fabs(p.p.z - want.z) < 1e-3f, "in front of a turned head");
        CHECK(std::fabs(YawOf(p.q) - 1.0f) < 1e-3f, "facing the player");
    }
    {   // disabled: nothing, and nothing held back
        Rig r;
        r.enabled = false;
        r.Grips(true);
        r.Run(500);
        bool kept = true;
        for (int h = 0; h < 2; ++h) { r.StickDown(h, true); FrameOut o = r.Step(); if (!(o.pad[h].buttons & kStick) || o.open) kept = false; r.StickDown(h, false); }
        CHECK(kept, "off: clicks pass, nothing opens");
        r.Grips(false);
        r.Step();
        CHECK(!r.c.Busy(), "off: nothing going on");
    }
    {   // the chord closes it, and what was held stays the panel's until let go
        Rig r;
        r.Open();
        CHECK(r.c.Open(), "open");
        r.Grips(true);
        r.Run(400);
        CHECK(r.c.Open(), "grips alone do not close it");
        r.StickDown(0, true);
        FrameOut o = r.Step();
        CHECK(!r.c.Open() && o.justClosed, "the chord closes it");
        CHECK(!(o.pad[0].buttons & kStick) && o.pad[0].squeeze == 0.0f && o.pad[1].squeeze == 0.0f, "the click and the grips that closed it are not the game's");
        r.Run(200);
        CHECK(r.c.Busy() && r.last.pad[0].squeeze == 0.0f, "held back while held");
        r.StickDown(0, false);
        r.Grips(false);
        r.Step();
        CHECK(!r.c.Busy(), "all let go: free");
        r.StickDown(0, true);
        FrameOut again = r.Step();
        CHECK(again.pad[0].buttons & kStick, "a stick click after that is the game's again");
        r.StickDown(0, false);
        r.Step();
    }
    {   // the same chord can open it again (a click is a toggle)
        Rig r;
        r.Open();
        r.Grips(true);
        r.Run(400);
        r.ClickStick(0);
        CHECK(!r.c.Open(), "closed by the click");
        r.ClickStick(1);
        CHECK(r.c.Open(), "opened again by the next one, the grips still squeezed");
    }
}

static void TestClicks() {
    {   // a click on a stepper's plus; Apply asks to save; Close closes and the held trigger stays released
        Rig r;
        r.Open();
        const int md = KeyAt(0, 0);
        const float before = r.c.model.Get()[md];
        r.Click(1, kInc(0));
        CHECK(std::fabs(r.c.model.Get()[md] - (before + kDefs[md].step)) < 1e-4f, "plus moved the draft by a step");
        CHECK(r.c.model.Dirty(), "dirty");
        FrameOut o = r.Click(1, kApply);
        CHECK(o.action.what == Do::Save && o.action.request.changed == (1u << md), "Apply hands over only the changed key");
        CHECK(r.c.model.Busy(), "waiting for the answer");
        // the trigger's down state never reaches the game
        r.PullTrigger(1, true);
        r.Step();
        CHECK(r.last.pad[1].trigger == 0.0f && !(r.last.pad[1].buttons & kTrigger), "no trigger goes on while open");
        r.PullTrigger(1, false);
        r.Step();
    }
    {   // tabs
        Rig r;
        r.Open();
        r.Click(1, kTab(1));
        CHECK(r.c.model.page == 1, "tab 1");
        const int tg = KeyAt(1, 0);
        r.Click(1, kToggle(0));
        CHECK(r.c.model.Get()[tg] != r.snap.values[tg], "toggle on page 1");
    }
    {   // Close with the trigger still held: the release gate
        Rig r;
        r.Open();
        float u, v;
        Centre(kClose, &u, &v);
        r.Point(1, u, v);
        r.Step();
        r.PullTrigger(1, true);
        r.pad[1].squeeze = 0.9f;
        r.pad[1].buttons |= kSqueeze;
        FrameOut o = r.Step();
        CHECK(!o.open && !r.c.Open(), "Close closed it");
        CHECK(o.pad[1].trigger == 0.0f && !(o.pad[1].buttons & kTrigger), "the trigger that closed it is not a click in the game");
        CHECK(o.pad[1].squeeze == 0.0f && !(o.pad[1].buttons & kSqueeze), "nor the grip held with it");
        r.Run(200);
        CHECK(r.last.pad[1].trigger == 0.0f, "still held back while held");
        r.PullTrigger(1, false);
        r.pad[1].squeeze = 0.0f;
        r.pad[1].buttons &= ~kSqueeze;
        r.Step();
        r.PullTrigger(1, true);
        r.Step();
        CHECK(r.last.pad[1].trigger == 1.0f, "after it was let go the trigger is the game's again");
        r.PullTrigger(1, false);
        r.Step();
        CHECK(!r.c.Busy(), "nothing held back at the end");
    }
    {   // B / Y / Menu close it; a press already down when it opened does not
        for (int hand = 0; hand < 2; ++hand)
            for (uint32_t button : {(uint32_t)kSecondary, (uint32_t)kMenu}) {
                Rig r;
                r.pad[hand].buttons |= button;   // held while the chord is made
                r.Open();
                CHECK(r.c.Open(), "a button held at the opening does not close it");
                r.pad[hand].buttons &= ~button;
                r.Step();
                CHECK(r.c.Open(), "nor letting it go");
                r.pad[hand].buttons |= button;
                FrameOut o = r.Step();
                CHECK(!r.c.Open() && !(o.pad[hand].buttons & button), "a fresh press closes it and is not the game's");
                r.pad[hand].buttons &= ~button;
                r.Step();
                CHECK(!r.c.Busy(), "released: free");
            }
    }
    {   // a trigger held at the opening is not a click
        Rig r;
        float u, v;
        Centre(kApply, &u, &v);
        r.PullTrigger(1, true);   // held from before / during the chord
        r.Grips(true);
        r.Run(400);
        r.ClickStick(0);
        CHECK(r.c.Open(), "open");
        r.Grips(false);
        r.Point(1, u, v);
        r.Run(100);
        CHECK(r.last.action.what == Do::Nothing, "a trigger held from before clicks nothing");
        r.PullTrigger(1, false);
        r.Step();
        r.PullTrigger(1, true);
        r.Step();
        CHECK(r.c.hover.kind == Kind::Apply, "hover is the button under the beam");
    }
    {   // not focused: no clicks
        Rig r;
        r.Open();
        r.focused = false;
        float u, v;
        Centre(kInc(0), &u, &v);
        r.Point(1, u, v);
        r.Step();
        r.PullTrigger(1, true);
        r.Step();
        r.PullTrigger(1, false);
        r.Step();
        CHECK(!r.c.model.Dirty(), "no focus, no click");
        CHECK(!r.last.beam, "no beam without the focus");
    }
    {   // a click beside the buttons does nothing
        Rig r;
        r.Open();
        r.Point(1, 0.5f, 0.02f);
        r.Step();
        r.PullTrigger(1, true);
        r.Step();
        CHECK(r.c.hover.kind == Kind::None && !r.c.model.Dirty(), "nothing under the beam");
        r.PullTrigger(1, false);
        r.Step();
        // pointing past the panel's edge
        r.aim[1] = Aim({0, 1.3f, 0}, {3.0f, 1.3f, -1.0f});
        r.Step();
        CHECK(r.last.beam && !r.last.hit && r.last.len == kBeamM, "a beam that meets nothing is its own length");
    }
    {   // the other hand takes the beam when its trigger is pulled on the panel
        Rig r;
        r.Open();
        float u, v;
        Centre(kInc(0), &u, &v);
        r.Point(1, 0.5f, 0.3f);
        r.Point(0, u, v);
        r.Step();
        CHECK(r.last.hand == 1, "the right hand by default");
        r.PullTrigger(0, true);
        r.Step();
        CHECK(r.last.hand == 0, "the hand that pulls its trigger takes the beam");
        CHECK(r.c.model.Dirty(), "and its click counts");
        r.PullTrigger(0, false);
        r.Step();
    }
    {   // holding the trigger on plus repeats
        Rig r;
        r.Open();
        const int md = KeyAt(0, 0);
        const float before = r.c.model.Get()[md];
        float u, v;
        Centre(kInc(0), &u, &v);
        r.Point(1, u, v);
        r.Step();
        r.PullTrigger(1, true);
        r.Step();
        CHECK(std::fabs(r.c.model.Get()[md] - (before + kDefs[md].step)) < 1e-4f, "one step at the press");
        r.Run(300);
        CHECK(std::fabs(r.c.model.Get()[md] - (before + kDefs[md].step)) < 1e-4f, "none before the first repeat time");
        r.Run(1000);
        const float steps = (r.c.model.Get()[md] - before) / kDefs[md].step;
        CHECK(steps >= 5.0f && steps <= 10.0f, "then a step every ~110 ms while held");
        r.PullTrigger(1, false);
        const float held = r.c.model.Get()[md];
        r.Run(500);
        CHECK(std::fabs(r.c.model.Get()[md] - held) < 1e-4f, "none after the release");
        // a toggle does not repeat
        r.Click(1, kTab(1));
        float tu, tv;
        Centre(kToggle(0), &tu, &tv);
        r.Point(1, tu, tv);
        r.Step();
        const float tgBefore = r.c.model.Get()[KeyAt(1, 0)];
        r.PullTrigger(1, true);
        r.Run(2000);
        CHECK(r.c.model.Get()[KeyAt(1, 0)] != tgBefore, "a toggle flipped once");
        CHECK(std::fabs(r.c.model.Get()[KeyAt(1, 0)] - (1.0f - tgBefore)) < 1e-4f, "and stayed (no repeat)");
        r.PullTrigger(1, false);
        r.Step();
    }
    {   // switched off while open: closed, held controls released first
        Rig r;
        r.Open();
        r.PullTrigger(0, true);
        r.Step();
        r.enabled = false;
        FrameOut o = r.Step();
        CHECK(!r.c.Open() && o.justClosed, "switching it off closes it");
        CHECK(o.pad[0].trigger == 0.0f, "the trigger held then stays held back");
        r.PullTrigger(0, false);
        r.Step();
        CHECK(!r.c.Busy(), "free again");
    }
    {   // F4 only (vr/view_gate.h): the glue (xr_headset_settings.h) passes enabled = key && F4() - the view changes while it is open
        static bool f4 = true;
        viewgate::Set([] { return f4; });
        const bool key = true;
        Rig r;
        r.enabled = key && viewgate::F4();
        r.Open();
        CHECK(r.c.Open(), "F4: the chord opens it");
        r.PullTrigger(0, true);
        r.Step();
        f4 = false;
        r.enabled = key && viewgate::F4();
        FrameOut o = r.Step();
        CHECK(!r.c.Open() && o.justClosed, "F4 -> another view: the panel closes at once");
        CHECK(o.pad[0].trigger == 0.0f && !o.open, "the trigger held then stays held back, nothing is the panel's");
        r.PullTrigger(0, false);
        r.Step();
        CHECK(!r.c.Busy(), "free again");
        // in another view the chord arms nothing and the clicks are the game's
        r.Grips(true);
        r.Run(500);
        r.StickDown(1, true);
        o = r.Step();
        CHECK(!o.open && (o.pad[1].buttons & kStick), "another view: both grips and a click - the click is the game's, no panel");
        r.StickDown(1, false);
        r.Grips(false);
        r.Step();
        CHECK(!r.c.Busy(), "another view: nothing going on");
        // back in F4: it starts fresh
        f4 = true;
        r.enabled = key && viewgate::F4();
        r.Open();
        CHECK(r.c.Open(), "back in F4: the chord opens it again");
        viewgate::Set(nullptr);
    }
    {   // the poses are not in a Pad: the panel does not touch them (nothing to check but the shape of the output)
        FrameOut o;
        CHECK(sizeof(o.pad) == sizeof(Pad) * 2, "the output carries buttons, triggers and sticks only");
    }
}

// -- 6. the release gate on its own -------------------------------------------------------------------
static void TestGate() {
    ReleaseGate g;
    Pad held[2];
    held[0].buttons = kSecondary | kTrigger;
    held[0].trigger = 0.9f;
    held[1].stickX = 0.9f;
    held[1].squeeze = 0.8f;
    g.Arm(held);
    CHECK(g.Active(), "armed");
    Pad now[2] = {held[0], held[1]};
    g.Apply(now);
    CHECK(now[0].buttons == 0 && now[0].trigger == 0.0f && now[1].stickX == 0.0f && now[1].squeeze == 0.0f, "all held controls zero");
    Pad part[2] = {};
    part[0].buttons = kTrigger;
    part[0].trigger = 0.9f;   // B let go, trigger still down
    g.Apply(part);
    CHECK(part[0].buttons == 0 && part[0].trigger == 0.0f, "what is still down stays zero");
    Pad fresh[2] = {};
    fresh[0].buttons = kSecondary;   // B pressed again: a new press passes
    g.Apply(fresh);
    CHECK(fresh[0].buttons == kSecondary, "a control let go and pressed again is free");
    Pad up[2] = {};
    g.Apply(up);
    CHECK(!g.Active(), "all let go: the gate is spent");
}

// -- 7. the ini transaction -----------------------------------------------------------------------------
static std::wstring g_dir;
static std::string Slurp(const std::wstring& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
static void Spit(const std::wstring& p, const std::string& t) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << t;
}
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static int TmpFiles() {
    int n = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_dir + L"\\*.tmp").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) { do ++n; while (FindNextFileW(h, &fd)); FindClose(h); }
    return n;
}
static const char kIni[] =
    "; a comment the editor must keep\r\n"
    "[openxr]\r\n"
    "on=1\r\n"
    "; about the laser\r\n"
    "laser=0\r\n"
    "flat_width_m=2.4\r\n"
    "[screen]\r\n"
    "menu_distance_m=2.5\r\n"
    "[stereo]\r\n"
    "user_height_m=1.75\r\n";

static Request Req(std::initializer_list<std::pair<int, float>> edits, const Snapshot& s) {
    Request r;
    r.base = s.file;
    r.values = s.values;
    for (auto& e : edits) { r.values[e.first] = e.second; r.changed |= 1u << e.first; }
    r.sequence = 1;
    return r;
}
static int KeyOf(const wchar_t* key) {
    for (int i = 0; i < kCount; ++i) if (!wcscmp(kDefs[i].key, key)) return i;
    return -1;
}

static void TestIni() {
    CHECK(ini::SameValue(L"2.5", L"2.50") && ini::SameValue(L" 1 ", L"1.0") && !ini::SameValue(L"1", L"1.01") && ini::SameValue(L"Right", L"right") &&
              !ini::SameValue(L"a", L"b"), "values compare as numbers, else as text");

    const std::wstring path = g_dir + L"\\d2r_vr.ini";
    const std::wstring backup = path + L".before-headset-menu";
    DeleteFileW(backup.c_str());
    Spit(path, kIni);
    Snapshot s = store::LoadSnapshot(path.c_str());
    CHECK(s.ready, "the ini is read");
    const int md = KeyOf(L"menu_distance_m"), lw = KeyOf(L"flat_width_m"), br = KeyOf(L"brightness"), la = KeyOf(L"laser"), uh = KeyOf(L"user_height_m");
    CHECK(std::fabs(s.values[md] - 2.5f) < 1e-4f && std::fabs(s.values[lw] - 2.4f) < 1e-4f && std::fabs(s.values[uh] - 1.75f) < 1e-4f && s.values[la] == 0.0f, "values as the file has them");
    CHECK(s.values[br] == kDefs[br].def, "a key that is not in the file is its default");
    CHECK(!store::LoadSnapshot((g_dir + L"\\nothing.ini").c_str()).ready, "no file: not ready");
    // a value outside the range is shown clamped, the file value kept apart
    Spit(path, "[screen]\r\nmenu_distance_m=9\r\n");
    Snapshot big = store::LoadSnapshot(path.c_str());
    CHECK(big.values[md] == kDefs[md].max && std::fabs(big.file[md] - 9.0f) < 1e-4f, "shown clamped, parsed value kept for the conflict check");
    Spit(path, kIni);
    s = store::LoadSnapshot(path.c_str());

    // 1. a save: only the changed key, comments kept, the original backed up
    std::string why;
    Result r = store::SaveRequest(path.c_str(), Req({{md, 3.0f}}, s), &why);
    CHECK(r == Result::Saved, "saved");
    const std::string after = Slurp(path);
    CHECK(after.find("; a comment the editor must keep") != std::string::npos && after.find("; about the laser") != std::string::npos, "comments kept");
    Snapshot s2 = store::LoadSnapshot(path.c_str());
    CHECK(std::fabs(s2.values[md] - 3.0f) < 1e-4f, "the new value is in the file");
    CHECK(std::fabs(s2.values[lw] - 2.4f) < 1e-4f && s2.values[la] == 0.0f && std::fabs(s2.values[uh] - 1.75f) < 1e-4f, "the other keys untouched");
    CHECK(Exists(backup) && Slurp(backup) == std::string(kIni), "the original is kept, byte for byte");
    CHECK(TmpFiles() == 0, "no temp file left behind");

    // 2. a second save does not replace the backup
    r = store::SaveRequest(path.c_str(), Req({{md, 3.5f}}, s2));
    CHECK(r == Result::Saved && Slurp(backup) == std::string(kIni), "the backup stays the first original");

    // 3. a key that was not in the file is added
    Snapshot s3 = store::LoadSnapshot(path.c_str());
    r = store::SaveRequest(path.c_str(), Req({{br, 0.8f}}, s3));
    CHECK(r == Result::Saved && std::fabs(store::LoadSnapshot(path.c_str()).values[br] - 0.8f) < 1e-4f, "a missing key is written");

    // 4. a conflict: the key was changed outside after the panel read it - nothing written
    Spit(path, kIni);
    DeleteFileW(backup.c_str());
    Snapshot s4 = store::LoadSnapshot(path.c_str());
    std::string edited = kIni;
    edited.replace(edited.find("laser=0"), 7, "laser=1");
    Spit(path, edited);
    r = store::SaveRequest(path.c_str(), Req({{la, 1.0f}}, s4), &why);
    CHECK(r == Result::Conflict, "an outside edit of the same key is a conflict");
    CHECK(Slurp(path) == edited, "and nothing was written");
    CHECK(!Exists(backup), "and no backup made for it");
    CHECK(TmpFiles() == 0, "no temp file after a conflict");

    // 5. an outside edit of ANOTHER key is no conflict, and is kept
    Spit(path, kIni);
    Snapshot s5 = store::LoadSnapshot(path.c_str());
    std::string other = kIni;
    other.replace(other.find("user_height_m=1.75"), 18, "user_height_m=1.90");
    Spit(path, other);
    r = store::SaveRequest(path.c_str(), Req({{md, 2.0f}}, s5));
    CHECK(r == Result::Saved, "an outside edit of another key is no conflict");
    Snapshot s6 = store::LoadSnapshot(path.c_str());
    CHECK(std::fabs(s6.values[uh] - 1.9f) < 1e-4f && std::fabs(s6.values[md] - 2.0f) < 1e-4f, "both edits are in the file");

    // 6. the same key written the same way outside (2.5 -> 2.50) is not a conflict
    Spit(path, kIni);
    Snapshot s7 = store::LoadSnapshot(path.c_str());
    std::string same = kIni;
    same.replace(same.find("menu_distance_m=2.5"), 19, "menu_distance_m=2.50");
    Spit(path, same);
    CHECK(store::SaveRequest(path.c_str(), Req({{md, 2.2f}}, s7)) == Result::Saved, "2.5 and 2.50 are the same value");

    // 7. no file
    CHECK(store::SaveRequest((g_dir + L"\\gone.ini").c_str(), Req({{md, 2.0f}}, s7)) == Result::Failed, "no file: failed");
    CHECK(!Exists(g_dir + L"\\gone.ini"), "and none made");

    // 8. a request with nothing changed is saved at once and touches nothing
    Spit(path, kIni);
    Request none;
    none.base = s7.file;
    none.values = s7.values;
    none.sequence = 9;
    CHECK(store::SaveRequest(path.c_str(), none) == Result::Saved && Slurp(path) == std::string(kIni), "nothing changed, nothing written");

    // 9. the raw ini library: a key whose text is empty is present, a missing key is not
    Spit(path, "[a]\r\nk=\r\nx=1\r\n");
    CHECK(ini::Read(path.c_str(), L"a", L"k").present && !ini::Read(path.c_str(), L"a", L"nope").present && ini::Read(path.c_str(), L"a", L"x").text == L"1", "present and missing");
}

// -- 8. the ini's thread -------------------------------------------------------------------------------
static void TestStore() {
    const std::wstring path = g_dir + L"\\thread.ini";
    Spit(path, kIni);
    DeleteFileW((path + L".before-headset-menu").c_str());
    store::Start(path.c_str());
    Snapshot s = store::Read();
    CHECK(s.ready, "the first reading is there at once");
    const int md = KeyOf(L"menu_distance_m");
    CHECK(std::fabs(s.values[md] - 2.5f) < 1e-4f, "with the file's values");
    Request r = Req({{md, 3.3f}}, s);
    r.sequence = 7;
    CHECK(store::Submit(r), "an Apply is taken");
    Snapshot a;
    for (int i = 0; i < 80; ++i) {
        a = store::Read();
        if (a.sequence == 7 && a.result != Result::Saving) break;
        Sleep(50);
    }
    CHECK(a.sequence == 7 && a.result == Result::Saved && std::fabs(a.values[md] - 3.3f) < 1e-4f, "the answer arrives with the new values");
    CHECK(!store::Submit(Request{}), "an empty Apply is refused");
    // an outside edit shows up within a couple of seconds
    std::string text = Slurp(path);
    text.replace(text.find("flat_width_m=2.4"), 16, "flat_width_m=4.0");
    Spit(path, text);
    bool seen = false;
    const int lw = KeyOf(L"flat_width_m");
    for (int i = 0; i < 60 && !seen; ++i) {
        seen = std::fabs(store::Read().values[lw] - 4.0f) < 1e-4f;
        if (!seen) Sleep(50);
    }
    CHECK(seen, "an outside edit is picked up by the thread");
    // a conflict arriving through the thread
    Snapshot stale = store::Read();
    text = Slurp(path);
    text.replace(text.find("menu_distance_m=3.3"), 19, "menu_distance_m=1.1");
    Spit(path, text);
    Request c = Req({{md, 5.0f}}, stale);
    c.sequence = 8;
    CHECK(store::Submit(c), "taken");
    for (int i = 0; i < 80; ++i) {
        a = store::Read();
        if (a.sequence == 8 && a.result != Result::Saving) break;
        Sleep(50);
    }
    CHECK(a.sequence == 8 && a.result == Result::Conflict && std::fabs(a.values[md] - 1.1f) < 1e-4f, "a conflict through the thread reloads the outside value");
    store::Stop();
    store::Join(3000);
    CHECK(!store::Submit(Req({{md, 2.0f}}, store::Read())), "a stopped thread takes nothing");
    // started again after a stop
    store::Start(path.c_str());
    Request again = Req({{md, 2.0f}}, store::Read());
    again.sequence = 9;
    CHECK(store::Submit(again), "started again, it takes an Apply");
    for (int i = 0; i < 80; ++i) {
        a = store::Read();
        if (a.sequence == 9 && a.result != Result::Saving) break;
        Sleep(50);
    }
    CHECK(a.result == Result::Saved, "and saves it");
    store::Stop();
    store::Join(3000);
}

// -- 9. the picture -------------------------------------------------------------------------------------
static void TestPaint() {
    Model m;
    m.Open(Snap());
    std::vector<uint32_t> a, b;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(Paint(m, {}, a), "painted");
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  one paint: %.2f ms (the first, fonts included)\n", ms);
    CHECK(a.size() == (size_t)kW * kH, "the atlas' size");
    bool opaque = true;
    std::set<uint32_t> colours;
    for (size_t i = 0; i < a.size(); i += 97) { if ((a[i] >> 24) != 0xFF) opaque = false; colours.insert(a[i]); }
    CHECK(opaque, "every pixel opaque");
    CHECK(colours.size() > 6, "more than a flat colour");
    float u, v;
    Centre(kInc(0), &u, &v);
    CHECK(Paint(m, HitTest(0, u, v), b) && a != b, "the hovered button looks different");
    m.Click({Kind::Change, 0, 1});
    std::vector<uint32_t> c;
    Paint(m, {}, c);
    CHECK(a != c, "a changed value is painted");
    m.Click({Kind::Tab, 1, 0});
    std::vector<uint32_t> d;
    Paint(m, {}, d);
    CHECK(d != c, "the other page looks different");
    Model nr;
    nr.Open(Snap(false));
    CHECK(Paint(nr, {}, b) && std::strlen(StatusText(nr)) > 10, "no ini: painted with its reason");
    const auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) Paint(m, {}, b);
    std::printf("  ten more paints: %.2f ms each\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / 10.0);
}

// -- 10. the Video page ------------------------------------------------------------------------------------
static constexpr uint32_t Bit(int key) { return 1u << key; }
static Snapshot SnapWith(std::initializer_list<std::pair<int, double>> v, bool ready = true) {
    Snapshot s = Snap(ready);
    for (auto& e : v) s.values[e.first] = s.file[e.first] = (float)e.second;
    return s;
}
static Model OnPage(const Snapshot& s, int page) {
    Model m;
    m.Open(s);
    m.Click({Kind::Tab, page, 0});
    return m;
}

static void TestVideoPure() {
    using namespace hset::video;
    CHECK(kPresetCount == 8 && kPresets[0].kind == rendersize::Kind::Desktop && kPresets[kPresetCount - 1].kind == rendersize::Kind::Headset,
          "the window's own size first, the headset's own last");
    int last = 0;
    for (int i = 1; i < kPresetCount - 1; ++i) {
        const Preset& p = kPresets[i];
        CHECK(p.kind == rendersize::Kind::Square && p.side > last, "squares, small to large");
        CHECK(p.side >= rendersize::kSideMin && p.side <= rendersize::kSideMax && (p.side - rendersize::kSideMin) % rendersize::kSideStep == 0 &&
                  p.side % 2 == 0, "on the slider's grid");
        CHECK(std::to_string(p.side) == p.label, "named by its side");
        last = p.side;
    }
    CHECK(kPresets[1].side == 1024 && kPresets[2].side == 1536 && kPresets[3].side == 2048 && kPresets[4].side == 2560 && kPresets[5].side == 3072,
          "1024 / 1536 / 2048 / 2560 / 3072, as asked");
    CHECK(PickOfPreset(-1) == Pick{} && PickOfPreset(kPresetCount) == Pick{}, "a preset that is not there is the window's");

    // the two keys together, as the plugin reads them (shared/render_size.h)
    CHECK(PickOf(0, 0) == Pick{} && PickOf(NAN, NAN) == Pick{}, "nothing written: the window's own size");
    CHECK(PickOf(1, 0).kind == rendersize::Kind::Headset && PickOf(1, 2048).kind == rendersize::Kind::Headset, "mode 1 is the headset's, whatever the side");
    CHECK(PickOf(0, 2560) == Square(2560) && PickOf(4, 0) == Square(2560) && PickOf(2, 0) == Square(1536), "a side, or an older square mode");
    CHECK(PickOf(0, 99999) == Square(4096) && PickOf(0, 500) == Square(1024), "clamped as the plugin clamps");
    CHECK(PickOf(0, 3001) == Square(3000), "made even");

    // an Apply writes what reads back as the choice, and what the PC window's slider and box write
    for (float px : {0.0f, 1024.0f, 2048.0f, 3071.0f, 4096.0f})
        for (int i = 0; i < kPresetCount; ++i) {
            const Pick want = PickOfPreset(i);
            const Keys k = KeysFor(want, px);
            CHECK(PickOf(k.mode, k.px) == want, "what is written reads back as the choice");
        }
    for (int stop = 0; stop < kStops; ++stop) {
        const Keys k = KeysFor(Square(SideOfStop(stop)), 0.0f);
        CHECK(PickOf(k.mode, k.px) == Square(SideOfStop(stop)) && k.mode == 0.0f, "a slider stop reads back, with mode 0");
    }
    CHECK(KeysFor(Pick{}, 2048).mode == 0.0f && KeysFor(Pick{}, 2048).px == 0.0f, "the window's own size clears both keys");
    CHECK(KeysFor(PickOfPreset(7), 2048).mode == 1.0f && KeysFor(PickOfPreset(7), 2048).px == 2048.0f, "the headset's own is mode 1 and keeps the side (the PC box does the same)");
    CHECK(KeysFor(PickOfPreset(7), NAN).px == 0.0f && KeysFor(PickOfPreset(7), -5.0f).px == 0.0f, "no side to keep");
    CHECK(rendersize::ModeAfterSlider(4) == (int)KeysFor(Square(2048), 0).mode && rendersize::ModeAfterSlider(0) == (int)KeysFor(Square(2048), 0).mode,
          "a square clears an older square mode, as moving the PC slider does");

    // the slider
    CHECK(kStops == 49 && SideOfStop(0) == 1024 && SideOfStop(kStops - 1) == 4096 && SideOfStop(-5) == 1024 && SideOfStop(99) == 4096, "49 stops, 1024 to 4096");
    bool grid = true;
    for (int i = 0; i < kStops; ++i) {
        const int side = SideOfStop(i);
        if (StopOfSide(side) != i || side % 2 != 0 || SideAtFraction(FractionOfSide(side)) != side) grid = false;
    }
    CHECK(grid, "every stop: its side, back to the stop, the fraction of it, back to the side");
    CHECK(SideAtFraction(0.0f) == 1024 && SideAtFraction(1.0f) == 4096 && SideAtFraction(0.5f) == 2560 && SideAtFraction(-3.0f) == 1024 &&
              SideAtFraction(7.0f) == 4096 && SideAtFraction(NAN) == 1024, "the track's ends, its middle, off the ends, not a number");
    int prev = 0;
    bool monotone = true;
    for (int i = 0; i <= 1000; ++i) {
        const int side = SideAtFraction((float)i / 1000.0f);
        if (side < prev) monotone = false;
        prev = side;
    }
    CHECK(monotone, "further right is never a smaller side");
    CHECK(StepSide(2048, 1) == 2112 && StepSide(2048, -1) == 1984 && StepSide(4096, 1) == 4096 && StepSide(1024, -1) == 1024, "a step is 64, held at the ends");

    // the lines
    Env none;
    CHECK(SizeText({}) == "not known yet" && SizeText({2496, 2688}) == "2496 x 2688", "a size as text");
    CHECK(PickText(Pick{}, none) == "Window's own size" && PickText(Square(2560), none) == "2560 x 2560" && PickText(PickOfPreset(7), none) == "Headset's own size",
          "a choice as text, the headset's size not known");
    Env e;
    e.now = {2048, 2048};
    e.recommended = {2496, 2688};
    e.maximum = {4096, 4096};
    CHECK(PickText(PickOfPreset(7), e) == "Headset's own: 2496 x 2688", "the headset's own with its size");
    CHECK(CostText(Square(2048), e) == "Draws 1.00 x the pixels of 2048 x 2048." && CostText(Square(4096), e).find("4.00") != std::string::npos, "the cost against 2048 x 2048");
    CHECK(CostText(PickOfPreset(7), e).find("1.60") != std::string::npos, "the headset's own costs 2496 x 2688 / 2048 x 2048");
    CHECK(CostText(Pick{}, e).find("Nothing changes") != std::string::npos, "the window's own size costs nothing new");
    CHECK(CostText(PickOfPreset(7), none).find("first starts") != std::string::npos, "the headset's size not known yet is said");
    CHECK(LimitText(Square(4096), e).empty() && LimitText(Square(4096), none).empty(), "within the limits, or no limits known: nothing said");
    e.maximum = {2400, 2688};
    CHECK(LimitText(Square(4096), e) == "Your headset takes 2400 at most: shrunk to that." && LimitText(Square(2304), e).empty() &&
              LimitText(PickOfPreset(7), e).empty() && LimitText(Pick{}, e).empty(), "a square over what the headset takes is told");
    CHECK(NowText(e) == "The game draws 2048 x 2048 now. Your headset recommends 2496 x 2688." && NowText(none).find("not known") != std::string::npos, "the sizes now");
    Env ea = e, eb = e;
    eb.now = {2560, 2560};
    CHECK(ea == e && !(ea == eb), "an Env compares");
}

static void TestVideoModel() {
    using namespace hset::video;
    const uint32_t both = Bit(kKeyMode) | Bit(kKeyPx);
    {   // the window's own size -> 2048: only the side changes (the mode is 0 already)
        Model m = OnPage(Snap(), kPageVideo);
        CHECK(m.page == kPageVideo && m.DraftPick() == Pick{} && !m.Dirty() && !m.VideoEdited(), "opens on the file's choice");
        m.Click({Kind::Preset, 3, 0});
        CHECK(m.DraftPick() == Square(2048) && m.Dirty() && m.VideoEdited() && m.Get()[kKeyPx] == 2048.0f && m.Get()[kKeyMode] == 0.0f, "2048 chosen");
        Action a = m.Click({Kind::Apply, -1, 0});
        CHECK(a.what == Do::Save && a.request.changed == both, "the two keys are one value: both go, so an outside change of either is a conflict");
        CHECK(a.request.values[kKeyPx] == 2048.0f && a.request.base[kKeyPx] == 0.0f && a.request.values[kKeyMode] == 0.0f, "from the file's value to the new one");
    }
    {   // from the headset's own size to a square: both keys change
        Model m = OnPage(SnapWith({{kKeyMode, 1}, {kKeyPx, 3072}}), kPageVideo);
        CHECK(m.DraftPick().kind == rendersize::Kind::Headset && !m.Dirty(), "the box's state reads as the headset's own");
        m.Click({Kind::Preset, 3, 0});
        Action a = m.Click({Kind::Apply, -1, 0});
        CHECK(a.request.changed == both && a.request.values[kKeyMode] == 0.0f && a.request.values[kKeyPx] == 2048.0f, "mode back to 0 and the side");
    }
    {   // an older square mode (4 = 2560) chosen again is no change; another size clears the mode
        Model m = OnPage(SnapWith({{kKeyMode, 4}}), kPageVideo);
        CHECK(m.DraftPick() == Square(2560), "mode 4 is 2560");
        const uint64_t rev = m.revision;
        m.Click({Kind::Preset, 4, 0});
        CHECK(!m.Dirty() && m.revision == rev, "the size it already is: nothing");
        m.Click({Kind::Preset, 3, 0});
        Action a = m.Click({Kind::Apply, -1, 0});
        CHECK(a.request.changed == both && a.request.values[kKeyMode] == 0.0f && a.request.values[kKeyPx] == 2048.0f, "the old mode is cleared with the new side");
    }
    {   // the headset's own size keeps the side; the window's own size clears it
        Model h = OnPage(SnapWith({{kKeyPx, 2048}}), kPageVideo);
        h.Click({Kind::Preset, 7, 0});
        Action a = h.Click({Kind::Apply, -1, 0});
        CHECK(a.request.changed == both && a.request.values[kKeyMode] == 1.0f && a.request.values[kKeyPx] == 2048.0f, "the headset's own: mode 1, the side kept");
        Model w = OnPage(SnapWith({{kKeyPx, 2048}}), kPageVideo);
        w.Click({Kind::Preset, 0, 0});
        a = w.Click({Kind::Apply, -1, 0});
        CHECK(a.request.changed == both && a.request.values[kKeyPx] == 0.0f && a.request.values[kKeyMode] == 0.0f, "the window's own size: the side cleared");
    }
    {   // choosing back what the file has is clean again
        Model m = OnPage(SnapWith({{kKeyPx, 2048}}), kPageVideo);
        m.Click({Kind::Preset, 5, 0});
        CHECK(m.Dirty(), "changed");
        m.Click({Kind::Preset, 3, 0});
        CHECK(!m.Dirty() && !m.VideoEdited(), "and back: clean");
    }
    {   // the slider: a place on the track, the same place again, the steppers
        Model m = OnPage(Snap(), kPageVideo);
        m.Click(SliderAt((float)kSlX0));
        CHECK(m.DraftPick() == Square(1024), "the left end of the track");
        m.Click(SliderAt((float)kSlX1));
        CHECK(m.DraftPick() == Square(4096), "the right end");
        m.Click({Kind::Slider, 3008, 0});
        const uint64_t rev = m.revision;
        m.Click({Kind::Slider, 3008, 0});
        CHECK(m.revision == rev && m.Get()[kKeyPx] == 3008.0f, "the same place again changes nothing (a held trigger asks every frame)");
        m.Click({Kind::Slider, -1, -1});
        CHECK(m.Get()[kKeyPx] == 2944.0f, "minus is one stop");
        m.Click({Kind::Slider, -1, 1});
        CHECK(m.Get()[kKeyPx] == 3008.0f, "plus is one stop");
        for (int i = 0; i < 100; ++i) m.Click({Kind::Slider, -1, 1});
        CHECK(m.Get()[kKeyPx] == 4096.0f, "held at the top");
        for (int i = 0; i < 100; ++i) m.Click({Kind::Slider, -1, -1});
        CHECK(m.Get()[kKeyPx] == 1024.0f, "held at the bottom");
        m.Click({Kind::Slider, 100, 0});
        CHECK(m.Get()[kKeyPx] == 1024.0f, "a side that is not on the track is nothing (index under the range is not a place)");
    }
    {   // the steppers from a choice that is not a square: the first press takes the side the slider rests at
        Model m = OnPage(SnapWith({{kKeyMode, 1}, {kKeyPx, 3072}}), kPageVideo);
        m.Click({Kind::Slider, -1, 1});
        CHECK(m.DraftPick() == Square(3072) && m.Get()[kKeyMode] == 0.0f, "the first press: the remembered side, a square now");
        m.Click({Kind::Slider, -1, 1});
        CHECK(m.Get()[kKeyPx] == 3136.0f, "the next steps from it");
        Model w = OnPage(Snap(), kPageVideo);
        w.Click({Kind::Slider, -1, -1});
        CHECK(w.DraftPick() == Square(2048), "nothing remembered: 2048");
    }
    {   // not ready, saving, closed: no edits
        Model nr = OnPage(Snap(false), kPageVideo);
        nr.Click({Kind::Preset, 3, 0});
        nr.Click({Kind::Slider, 3008, 0});
        CHECK(!nr.Dirty() && nr.Click({Kind::Apply, -1, 0}).what == Do::Nothing, "no ini, no edits");
        Model b = OnPage(Snap(), kPageVideo);
        b.Click({Kind::Preset, 4, 0});
        b.Click({Kind::Apply, -1, 0});
        b.Click({Kind::Preset, 3, 0});
        b.Click({Kind::Slider, 3008, 0});
        CHECK(b.DraftPick() == Square(2560), "no edits while saving");
        Model c;
        c.Click({Kind::Preset, 3, 0});
        CHECK(!c.Dirty(), "a closed panel does nothing");
        Model bad = OnPage(Snap(), kPageVideo);
        bad.Click({Kind::Preset, -1, 0});
        bad.Click({Kind::Preset, 99, 0});
        CHECK(!bad.Dirty(), "a preset that is not there does nothing");
    }
    {   // the answer, a conflict, the PC window changing the size while the panel is open
        Model m = OnPage(Snap(), kPageVideo);
        m.Click({Kind::Preset, 4, 0});
        Action a = m.Click({Kind::Apply, -1, 0});
        Snapshot ans = SnapWith({{kKeyPx, 2560}});
        ans.sequence = a.request.sequence;
        ans.result = Result::Saved;
        m.Observe(ans);
        CHECK(!m.Dirty() && m.result == Result::Saved && m.DraftPick() == Square(2560) && m.FilePick() == Square(2560), "Saved: the size is the file's now");

        Model c = OnPage(Snap(), kPageVideo);
        c.Click({Kind::Preset, 4, 0});
        Action ca = c.Click({Kind::Apply, -1, 0});
        Snapshot theirs = SnapWith({{kKeyPx, 3072}});
        theirs.sequence = ca.request.sequence;
        theirs.result = Result::Conflict;
        c.Observe(theirs);
        CHECK(c.result == Result::Conflict && c.DraftPick() == Square(3072) && !c.Dirty(), "a conflict shows the size the PC window set");

        Model o = OnPage(Snap(), kPageVideo);
        o.Observe(SnapWith({{kKeyPx, 3072}}));
        CHECK(o.DraftPick() == Square(3072) && !o.Dirty(), "a clean panel follows a change made in the PC window");
        o.Click({Kind::Preset, 1, 0});
        o.Observe(SnapWith({{kKeyPx, 4096}}));
        CHECK(o.DraftPick() == Square(1024), "an unapplied choice is not overwritten");
    }
    {   // the sizes the page shows follow the plugin
        Model m = OnPage(Snap(), kPageVideo);
        const uint64_t r0 = m.revision;
        Env e;
        e.now = {2048, 2048};
        m.SetEnv(e);
        CHECK(m.revision != r0 && m.Env().now.w == 2048, "a new size now repaints");
        const uint64_t r1 = m.revision;
        m.SetEnv(e);
        CHECK(m.revision == r1, "the same sizes do not");
    }
}

// the places of the new pages
static void TestPagesHit() {
    using namespace hset::video;
    for (int i = 0; i < kPresetCount; ++i) {
        CHECK(Inside(kPreset(i)), "a preset inside the atlas");
        CHECK(!Overlap(kPreset(i), kStatus) && !Overlap(kPreset(i), kSlTrack) && !Overlap(kPreset(i), kSlDec) && !Overlap(kPreset(i), kSlInc), "presets clear of the slider and the status line");
        for (int j = i + 1; j < kPresetCount; ++j) CHECK(!Overlap(kPreset(i), kPreset(j)), "presets apart");
        float u, v;
        Centre(kPreset(i), &u, &v);
        CHECK((HitTest(kPageVideo, u, v) == Hit{Kind::Preset, i, 0}), "a preset's centre");
        CHECK(HitTest(kPageView, u, v).kind != Kind::Preset && HitTest(kPageBody, u, v).kind != Kind::Preset, "and not on the other pages");
    }
    CHECK(Inside(kSlDec) && Inside(kSlInc) && Inside(kSlTrack) && !Overlap(kSlDec, kSlTrack) && !Overlap(kSlInc, kSlTrack) && !Overlap(kSlDec, kSlInc), "the slider's parts");
    CHECK(!Overlap(kSlTrack, kStatus) && !Overlap(kVidNote, kStatus) && !Overlap(kVidNote, kApply) && Inside(kVidNote) && Inside(kVidCost) && Inside(kVidLimit) && Inside(kVidNow) && Inside(kVidTitle),
          "the page's lines clear of the status line and the buttons");
    CHECK(kSlX0 > kSlTrack.x && kSlX1 < kSlTrack.x + kSlTrack.w, "the thumb's travel is inside the track");
    float u, v;
    Centre(kSlDec, &u, &v);
    CHECK((HitTest(kPageVideo, u, v) == Hit{Kind::Slider, -1, -1}), "slider minus");
    Centre(kSlInc, &u, &v);
    CHECK((HitTest(kPageVideo, u, v) == Hit{Kind::Slider, -1, 1}), "slider plus");
    const float mid = (kSlTrack.y + 0.5f * kSlTrack.h) / kH;
    CHECK((HitTest(kPageVideo, (float)kSlX0 / kW, mid) == Hit{Kind::Slider, 1024, 0}) && (HitTest(kPageVideo, (float)kSlX1 / kW, mid) == Hit{Kind::Slider, 4096, 0}),
          "the track's ends");
    CHECK((HitTest(kPageVideo, 0.5f, mid) == Hit{Kind::Slider, SideAtFraction(((0.5f * kW) - kSlX0) / (kSlX1 - kSlX0)), 0}), "and a place on it");
    CHECK(HitTest(kPageVideo, (kSlTrack.x + 1.0f) / kW, mid).index == 1024 && HitTest(kPageVideo, (kSlTrack.x + kSlTrack.w - 1.0f) / kW, mid).index == 4096, "the track's very edges are the ends");
    CHECK(HitTest(kPageVideo, 0.5f, (kVidNow.y + 5.0f) / kH).kind == Kind::None, "a line of text is not a button");
    CHECK(Repeats({Kind::Slider, -1, 1}) && Repeats({Kind::Change, KeyAt(0, 0), 1}) && !Repeats({Kind::Slider, 2048, 0}) && !Repeats({Kind::Preset, 2, 0}) &&
              !Repeats({Kind::Change, KeyAt(1, 0), 1}) && !Repeats({Kind::Calibrate, -1, 0}), "what goes on pressing while the trigger is held");

    CHECK(Inside(kCalibrate) && Inside(kForget) && !Overlap(kCalibrate, kForget) && Inside(kBodyStored) && Inside(kBodyUse) && Inside(kBodyStep(3)), "the Body page's parts");
    CHECK(!Overlap(kBodyStep(3), kStatus) && !Overlap(kBodyStep(3), kApply) && !Overlap(kBodyUse, kBodyStep(0)) && !Overlap(kBodyStored, kCalibrate) && !Overlap(kBodyStored, kBodyUse), "and clear of each other");
    Centre(kCalibrate, &u, &v);
    CHECK(HitTest(kPageBody, u, v).kind == Kind::Calibrate && HitTest(kPageVideo, u, v).kind != Kind::Calibrate, "Calibrate body, on its page");
    Centre(kForget, &u, &v);
    CHECK(HitTest(kPageBody, u, v).kind == Kind::Forget, "Forget measurement");
    CHECK(HitTest(kPageBody, 0.5f, (kBodyStep(1).y + 5.0f) / kH).kind == Kind::None, "the steps are text");
    for (int p = 0; p < kPageCount; ++p) {
        Centre(kTab(p), &u, &v);
        CHECK((HitTest(kPageBody, u, v) == Hit{Kind::Tab, p, 0}) && (HitTest(kPageVideo, u, v) == Hit{Kind::Tab, p, 0}), "all four tabs from the new pages too");
    }
}

// -- 11. the Body page ---------------------------------------------------------------------------------------
static void TestBodyModel() {
    using namespace hset::body;
    {   // what the keys say
        const Stored none = Of(0, 0, 0, 0);
        CHECK(!none.any && !none.valid && MeasuredText(none) == "Not measured yet", "nothing stored");
        const Stored good = Of(1, 1, 1.62f, 1.71f);
        CHECK(good.any && good.valid && good.use && MeasuredText(good) == "Eye height 1.62 m, arm span 1.71 m", "a stored measurement");
        CHECK(UseText(good).find("In use") == 0, "in use");
        const Stored off = Of(0, 1, 1.62f, 1.71f);
        CHECK(off.valid && !off.use && UseText(off).find("unticked") != std::string::npos, "stored but the box is unticked");
        const Stored bad = Of(1, 1, 0.5f, 1.71f);
        CHECK(bad.any && !bad.valid && MeasuredText(bad).find("not usable") != std::string::npos, "numbers that are not a person's");
        CHECK(!Of(1, 0, 1.62f, 1.71f).valid && Of(1, 0, 1.62f, 1.71f).any, "no version: not valid, but there is something to forget");
        CHECK(!Of(NAN, NAN, NAN, NAN).any && !Of(INFINITY, 1, INFINITY, 1).valid, "not numbers");
        CHECK(!Of(1, 1, 1.62f, 0.5f).valid && !Of(1, 1, 2.5f, 1.71f).valid && !Of(1, 1, 1.0f, 2.0f).valid, "out of the plugin's ranges (span, height, their ratio)");
        CHECK(UseText(none).find("BodyWalk") != std::string::npos, "until measured, the old height applies");
    }
    const Snapshot stored = SnapWith({{kKeyUse, 1}, {kKeyVersion, 1}, {kKeyEye, 1.6234f}, {kKeySpan, 1.71f}});
    {   // Calibrate asks for the measurement; the panel does not decide the rest
        Model m = OnPage(Snap(), kPageBody);
        CHECK(m.Click({Kind::Calibrate, -1, 0}).what == Do::Calibrate, "Calibrate body asks for the measurement");
        CHECK(!m.Dirty() && !m.Busy(), "and changes nothing in the draft");
        Model nr = OnPage(Snap(false), kPageBody);
        CHECK(nr.Click({Kind::Calibrate, -1, 0}).what == Do::Nothing, "no ini, nowhere to put the result: no");
        Model busy = OnPage(Snap(), kPageVideo);
        busy.Click({Kind::Preset, 3, 0});
        busy.Click({Kind::Apply, -1, 0});
        CHECK(busy.Click({Kind::Calibrate, -1, 0}).what == Do::Nothing, "not while an Apply is in flight");
        Model closed;
        CHECK(closed.Click({Kind::Calibrate, -1, 0}).what == Do::Nothing, "a closed panel does nothing");
    }
    {   // Forget clears the three keys the PC window's button clears, and nothing else
        Model m = OnPage(stored, kPageBody);
        CHECK(m.Measured().valid && std::fabs(m.Measured().eye - 1.6234f) < 1e-3f && std::fabs(m.Measured().span - 1.71f) < 1e-3f, "the page reads the measurement");
        Action a = m.Click({Kind::Forget, -1, 0});
        CHECK(a.what == Do::Save && a.request.changed == (Bit(kKeyVersion) | Bit(kKeyEye) | Bit(kKeySpan)), "version, eye height, arm span - not the box");
        CHECK(a.request.values[kKeyVersion] == 0.0f && a.request.values[kKeyEye] == 0.0f && a.request.values[kKeySpan] == 0.0f, "to zero");
        CHECK(std::fabs(a.request.base[kKeyEye] - 1.6234f) < 1e-4f && a.request.base[kKeyVersion] == 1.0f, "from what the panel saw");
        CHECK(m.Busy() && m.result == Result::Saving && !m.Dirty(), "saving; the draft has nothing of it");
        CHECK(m.Click({Kind::Forget, -1, 0}).what == Do::Nothing && m.Click({Kind::Calibrate, -1, 0}).what == Do::Nothing, "once at a time");
        Snapshot ans = stored;
        ans.values[kKeyVersion] = ans.file[kKeyVersion] = ans.values[kKeyEye] = ans.file[kKeyEye] = ans.values[kKeySpan] = ans.file[kKeySpan] = 0.0f;
        ans.sequence = a.request.sequence;
        ans.result = Result::Saved;
        m.Observe(ans);
        CHECK(!m.Busy() && m.result == Result::Saved && !m.Measured().any && !m.Dirty() && m.Measured().use == true, "Saved: not measured, the box left as it is");
    }
    {   // nothing to forget: nothing happens
        Model m = OnPage(Snap(), kPageBody);
        CHECK(m.Click({Kind::Forget, -1, 0}).what == Do::Nothing && !m.Busy(), "nothing stored, nothing to forget");
        Model nr = OnPage(Snap(false), kPageBody);
        CHECK(nr.Click({Kind::Forget, -1, 0}).what == Do::Nothing, "no ini");
    }
    {   // a Forget does not drop what was picked and not applied on another page
        Model m = OnPage(stored, kPageView);
        m.Click({Kind::Change, 0, 1});
        const float md = m.Get()[0];
        m.Click({Kind::Tab, kPageVideo, 0});
        m.Click({Kind::Preset, 4, 0});
        m.Click({Kind::Tab, kPageBody, 0});
        Action a = m.Click({Kind::Forget, -1, 0});
        CHECK(a.what == Do::Save && a.request.changed == (Bit(kKeyVersion) | Bit(kKeyEye) | Bit(kKeySpan)), "Forget carries only its own keys");
        Snapshot ans = stored;
        ans.values[kKeyVersion] = ans.file[kKeyVersion] = ans.values[kKeyEye] = ans.file[kKeyEye] = ans.values[kKeySpan] = ans.file[kKeySpan] = 0.0f;
        ans.sequence = a.request.sequence;
        ans.result = Result::Saved;
        m.Observe(ans);
        CHECK(!m.Measured().any, "forgotten");
        CHECK(m.Dirty() && m.Get()[0] == md && m.DraftPick() == video::Square(2560), "and the unapplied distance and size are still in the draft");
        Action ap = m.Click({Kind::Apply, -1, 0});
        CHECK(ap.what == Do::Save && ap.request.changed == (Bit(0) | Bit(kKeyMode) | Bit(kKeyPx)), "and Apply still writes exactly those (the size is its two keys)");
    }
    {   // a conflict on Forget reloads
        Model m = OnPage(stored, kPageBody);
        Action a = m.Click({Kind::Forget, -1, 0});
        Snapshot theirs = SnapWith({{kKeyUse, 1}, {kKeyVersion, 1}, {kKeyEye, 1.7f}, {kKeySpan, 1.8f}});
        theirs.sequence = a.request.sequence;
        theirs.result = Result::Conflict;
        m.Observe(theirs);
        CHECK(m.result == Result::Conflict && std::fabs(m.Measured().eye - 1.7f) < 1e-3f && m.Measured().valid, "measured again elsewhere meanwhile: the new numbers are shown, nothing erased");
    }
    {   // the measurement is followed live, also with an edit pending (the plugin writes it, it is not the draft's)
        Model m = OnPage(Snap(), kPageVideo);
        m.Click({Kind::Preset, 3, 0});
        const uint64_t rev = m.revision;
        m.Observe(stored);
        CHECK(m.Measured().valid && m.revision != rev && m.DraftPick() == video::Square(2048), "a new measurement shows at once, the draft stays");
        CHECK(m.Dirty() && m.Click({Kind::Apply, -1, 0}).request.changed == (Bit(kKeyMode) | Bit(kKeyPx)), "and the Apply still carries only the size");
    }
    {   // Resume: the Body page; a fresh draft when clean, the old one when not
        Model m;
        m.Open(Snap());
        m.Close();
        m.Resume(stored);
        CHECK(m.open && m.page == kPageBody && !m.Dirty() && m.Measured().valid && m.result == Result::None, "back on the Body page with the new measurement");
        Model d;
        d.Open(Snap());
        d.Click({Kind::Change, 0, 1});
        const float md = d.Get()[0];
        d.Close();
        d.Resume(stored);
        CHECK(d.open && d.page == kPageBody && d.Dirty() && d.Get()[0] == md && d.Measured().valid, "an unapplied edit survives it");
    }
}

// -- 12. the hands: Calibrate body, coming back, the slider drag --------------------------------------------------
static void TestPagesHands() {
    {   // the button hands the hands to the measurement; the panel comes back when it ends
        Rig r;
        r.Open();
        r.Click(1, kTab(kPageBody));
        CHECK(r.c.model.page == kPageBody, "on the Body page");
        float u, v;
        Centre(kCalibrate, &u, &v);
        r.Point(1, u, v);
        r.Step();
        r.PullTrigger(1, true);
        FrameOut o = r.Step();
        CHECK(o.action.what == Do::Calibrate && !o.open && o.justClosed, "Calibrate body closes the panel and asks for the measurement");
        CHECK(o.pad[1].trigger == 0.0f && !(o.pad[1].buttons & kTrigger), "the trigger that pressed it is not the game's");
        CHECK(!r.c.Open() && r.c.Resuming() && r.c.Busy(), "it waits to come back");
        r.Step();
        CHECK(r.last.pad[1].trigger == 0.0f, "still held back while the trigger is down");
        r.PullTrigger(1, false);
        r.Step();
        r.calibrating = true;   // the plugin's prompt is up
        r.Run(2000);
        CHECK(!r.c.Open() && r.c.Resuming(), "closed while the measurement runs");
        r.pad[0].trigger = r.pad[1].trigger = 1.0f;   // the T-pose's triggers are the plugin's own business: nothing here touches them
        r.Step();
        CHECK(r.last.pad[0].trigger == 1.0f && r.last.pad[1].trigger == 1.0f, "the triggers of the T-pose pass through");
        r.pad[0].trigger = r.pad[1].trigger = 0.0f;
        r.Grips(true);
        r.Run(500);
        r.StickDown(1, true);
        FrameOut c = r.Step();
        CHECK(!c.open && (c.pad[1].buttons & kStick), "the chord does nothing during the measurement: the click is the game's");
        r.StickDown(1, false);
        r.Grips(false);
        r.Step();
        r.calibrating = false;
        FrameOut back = r.Step();
        CHECK(back.open && back.justOpened && r.c.Open() && !r.c.Resuming() && r.c.model.page == kPageBody, "the measurement is over: the panel is back, on the Body page");
        for (int h = 0; h < 2; ++h) CHECK(back.pad[h].buttons == 0 && back.pad[h].trigger == 0.0f, "and has the hands again");
        r.Step();
        CHECK(r.c.Open(), "and stays");
    }
    {   // what was not applied is still in the draft
        Rig r;
        r.Open();
        r.Click(1, kInc(0));
        CHECK(r.c.model.Dirty(), "an edit");
        const float md = r.c.model.Get()[KeyAt(0, 0)];
        r.Click(1, kTab(kPageBody));
        r.Click(1, kCalibrate);
        r.calibrating = true;
        r.Run(300);
        r.calibrating = false;
        r.Step();
        CHECK(r.c.Open() && r.c.model.Dirty() && r.c.model.Get()[KeyAt(0, 0)] == md && r.c.model.page == kPageBody, "back with the unapplied edit");
    }
    {   // the key switched off (or another view than F4) meanwhile: it does not come back
        Rig r;
        r.Open();
        r.Click(1, kTab(kPageBody));
        r.Click(1, kCalibrate);
        r.calibrating = true;
        r.Run(200);
        CHECK(r.c.Resuming(), "waiting");
        r.enabled = false;
        r.Step();
        CHECK(!r.c.Resuming(), "switched off: no longer waiting");
        r.enabled = true;
        r.calibrating = false;
        r.Run(300);
        CHECK(!r.c.Open() && !r.c.Resuming(), "and it does not open on its own later");
    }
    {   // the head not tracked when the measurement ends: it waits for it
        Rig r;
        r.Open();
        r.Click(1, kTab(kPageBody));
        r.Click(1, kCalibrate);
        r.calibrating = true;
        r.Run(200);
        r.calibrating = false;
        r.head.valid = false;
        r.Run(200);
        CHECK(!r.c.Open() && r.c.Resuming(), "no head, no panel yet");
        r.head = Head();
        r.Step();
        CHECK(r.c.Open(), "and there it is");
    }
    {   // a measurement started from D2R VR Settings while the panel is up: the panel closes, and does not come back on its own
        Rig r;
        r.Open();
        r.calibrating = true;
        FrameOut o = r.Step();
        CHECK(!o.open && o.justClosed && !r.c.Open() && !r.c.Resuming(), "closed for it");
        r.calibrating = false;
        r.Run(300);
        CHECK(!r.c.Open(), "and not reopened: the player did not ask from here");
    }
    {   // no ini: the button does nothing
        Rig r;
        r.snap = Snap(false);
        r.Open();
        r.Click(1, kTab(kPageBody));
        FrameOut o = r.Click(1, kCalibrate);
        CHECK(o.action.what == Do::Nothing && r.c.Open() && !r.c.Resuming(), "no ini, no measurement");
    }
    {   // the chord closes the panel as before, whatever page it is on, and does not come back
        Rig r;
        r.Open();
        r.Click(1, kTab(kPageBody));
        r.Grips(true);
        r.Run(400);
        r.ClickStick(0);
        CHECK(!r.c.Open() && !r.c.Resuming(), "closed by the chord from the Body page");
    }
    {   // the slider follows the beam while the trigger is held, also off the track, and stays where it was let go
        Rig r;
        r.Open();
        r.Click(1, kTab(kPageVideo));
        const float mid = (kSlTrack.y + 0.5f * kSlTrack.h) / kH;
        r.Point(1, (float)kSlX0 / kW, mid);
        r.Step();
        r.PullTrigger(1, true);
        r.Step();
        CHECK(r.c.model.Get()[kKeyPx] == 1024.0f, "pressed on the left end: 1024");
        r.Point(1, (float)kSlX1 / kW, mid);
        r.Step();
        CHECK(r.c.model.Get()[kKeyPx] == 4096.0f, "dragged to the right end: 4096");
        r.Point(1, 1.06f, 0.2f);   // past the panel's edge, above the track
        r.Step();
        CHECK(r.c.model.Get()[kKeyPx] == 4096.0f && r.c.model.DraftPick() == video::Square(4096), "past the panel's edge: the end");
        r.Point(1, -0.06f, 0.9f);
        r.Step();
        CHECK(r.c.model.Get()[kKeyPx] == 1024.0f, "past the other edge: the other end");
        r.Point(1, 0.5f, mid);
        r.Step();
        CHECK(r.c.model.Get()[kKeyPx] == 2560.0f, "the middle");
        r.PullTrigger(1, false);
        r.Step();
        r.Point(1, (float)kSlX1 / kW, mid);
        r.Run(100);
        CHECK(r.c.model.Get()[kKeyPx] == 2560.0f, "let go: the beam moving does not move the thumb");
        CHECK(r.c.Open() && r.c.model.Dirty(), "the panel is still up with the draft");
    }
    {   // a press elsewhere on the panel and a drag over the slider does not touch it
        Rig r;
        r.Open();
        r.Click(1, kTab(kPageVideo));
        float u, v;
        Centre(kPreset(3), &u, &v);
        r.Point(1, u, v);
        r.Step();
        r.PullTrigger(1, true);
        r.Step();
        CHECK(r.c.model.DraftPick() == video::Square(2048), "a preset");
        r.Point(1, 0.7f, (kSlTrack.y + 0.5f * kSlTrack.h) / kH);
        r.Run(100);
        CHECK(r.c.model.DraftPick() == video::Square(2048), "held and dragged over the slider: it is not a slider drag");
        r.PullTrigger(1, false);
        r.Step();
    }
    {   // the steppers at the slider's ends repeat while the trigger is held
        Rig r;
        r.Open();
        r.Click(1, kTab(kPageVideo));
        float u, v;
        Centre(kSlInc, &u, &v);
        r.Point(1, u, v);
        r.Step();
        r.PullTrigger(1, true);
        r.Step();
        CHECK(r.c.model.Get()[kKeyPx] == 2048.0f, "the first press takes the resting side");
        r.Run(300);
        CHECK(r.c.model.Get()[kKeyPx] == 2048.0f, "no repeat before the first repeat time");
        r.Run(600);
        CHECK(r.c.model.Get()[kKeyPx] > 2048.0f, "held: it goes on stepping");
        r.PullTrigger(1, false);
        r.Step();
    }
    {   // the sizes reach the model through the frame
        Rig r;
        r.env.now = {2048, 2048};
        r.env.recommended = {2496, 2688};
        r.Open();
        CHECK(r.c.model.Env().now.w == 2048 && r.c.model.Env().recommended.h == 2688, "the picture's sizes are the model's");
        r.env.now = {2560, 2560};
        r.Step();
        CHECK(r.c.model.Env().now.w == 2560, "and follow");
    }
}

// -- 13. the ini for the new pages ---------------------------------------------------------------------------
static const char kIni2[] =
    "; a comment the editor must keep\r\n"
    "[openxr]\r\n"
    "on=1\r\n"
    "laser=0\r\n"
    "[render]\r\n"
    "; about the render size\r\n"
    "vr_resolution=1\r\n"
    "vr_resolution_px=2048\r\n"
    "other=7\r\n"
    "[calibration]\r\n"
    "use=1\r\n"
    "version=1\r\n"
    "eye_height_m=1.6234\r\n"
    "arm_span_m=1.7100\r\n";

static void TestIniPages() {
    const std::wstring path = g_dir + L"\\pages.ini";
    const std::wstring backup = path + L".before-headset-menu";
    DeleteFileW(backup.c_str());
    Spit(path, kIni2);
    Snapshot s = store::LoadSnapshot(path.c_str());
    CHECK(s.ready && s.values[kKeyMode] == 1.0f && s.values[kKeyPx] == 2048.0f, "the render size's keys are read");
    CHECK(s.values[kKeyUse] == 1.0f && s.values[kKeyVersion] == 1.0f && std::fabs(s.values[kKeyEye] - 1.6234f) < 1e-4f && std::fabs(s.values[kKeySpan] - 1.71f) < 1e-4f,
          "and the measurement's");
    Model m;
    m.Open(s);
    CHECK(m.DraftPick().kind == rendersize::Kind::Headset && m.Measured().valid && !m.Dirty(), "the model reads them");

    // 1. a size from the Video page: only the two render keys are written, the rest stays as it was
    m.Click({Kind::Tab, kPageVideo, 0});
    m.Click({Kind::Preset, 4, 0});
    Action a = m.Click({Kind::Apply, -1, 0});
    CHECK(a.what == Do::Save && a.request.changed == (Bit(kKeyMode) | Bit(kKeyPx)), "headset's own -> 2560: both keys");
    std::string why;
    CHECK(store::SaveRequest(path.c_str(), a.request, &why) == Result::Saved, "saved");
    const std::string after = Slurp(path);
    CHECK(after.find("; a comment the editor must keep") != std::string::npos && after.find("; about the render size") != std::string::npos && after.find("other=7") != std::string::npos,
          "comments and other keys kept");
    Snapshot s2 = store::LoadSnapshot(path.c_str());
    CHECK(s2.values[kKeyMode] == 0.0f && s2.values[kKeyPx] == 2560.0f && video::PickOf(s2.values[kKeyMode], s2.values[kKeyPx]) == video::Square(2560), "the file says 2560 now, as the plugin will read it");
    CHECK(s2.values[kKeyVersion] == 1.0f && std::fabs(s2.values[kKeyEye] - 1.6234f) < 1e-4f && s2.values[kKeyUse] == 1.0f, "the measurement untouched");
    CHECK(Exists(backup) && Slurp(backup) == std::string(kIni2), "the original is kept, byte for byte");
    Snapshot ans = s2;
    ans.sequence = a.request.sequence;
    ans.result = Result::Saved;
    m.Observe(ans);
    CHECK(!m.Dirty() && m.FilePick() == video::Square(2560), "the panel took the answer");

    // 2. Forget: the three keys, the box and everything else stay
    m.Click({Kind::Tab, kPageBody, 0});
    Action f = m.Click({Kind::Forget, -1, 0});
    CHECK(f.what == Do::Save, "Forget saves");
    CHECK(store::SaveRequest(path.c_str(), f.request, &why) == Result::Saved, "Forget saved");
    Snapshot s3 = store::LoadSnapshot(path.c_str());
    CHECK(s3.values[kKeyVersion] == 0.0f && s3.values[kKeyEye] == 0.0f && s3.values[kKeySpan] == 0.0f && s3.values[kKeyUse] == 1.0f, "version, eye height and span are 0, the box is as it was");
    CHECK(s3.values[kKeyPx] == 2560.0f && s3.values[kKeyMode] == 0.0f, "the render size untouched");
    CHECK(Slurp(path).find("other=7") != std::string::npos, "and the rest");
    CHECK(!body::Of(s3.values[kKeyUse], s3.values[kKeyVersion], s3.values[kKeyEye], s3.values[kKeySpan]).valid, "the plugin would not use it");

    // 3. the PC window changed the size after the panel read it: a conflict, nothing written
    Spit(path, kIni2);
    Snapshot s4 = store::LoadSnapshot(path.c_str());
    Model c;
    c.Open(s4);
    c.Click({Kind::Tab, kPageVideo, 0});
    c.Click({Kind::Preset, 3, 0});
    Action ca = c.Click({Kind::Apply, -1, 0});
    std::string edited = kIni2;
    edited.replace(edited.find("vr_resolution_px=2048"), 21, "vr_resolution_px=3072");
    Spit(path, edited);
    CHECK(store::SaveRequest(path.c_str(), ca.request, &why) == Result::Conflict, "the size was changed in the PC window meanwhile: a conflict");
    CHECK(Slurp(path) == edited, "and nothing was written");

    // 4. a missing [render] section: the keys are added
    Spit(path, "[openxr]\r\non=1\r\n");
    Snapshot s5 = store::LoadSnapshot(path.c_str());
    CHECK(s5.values[kKeyMode] == 0.0f && s5.values[kKeyPx] == 0.0f && !body::Of(s5.values[kKeyUse], s5.values[kKeyVersion], s5.values[kKeyEye], s5.values[kKeySpan]).any, "missing keys: the window's own size, nothing measured");
    Model n;
    n.Open(s5);
    n.Click({Kind::Tab, kPageVideo, 0});
    n.Click({Kind::Preset, 3, 0});
    Action na = n.Click({Kind::Apply, -1, 0});
    CHECK(store::SaveRequest(path.c_str(), na.request, &why) == Result::Saved, "saved into a file with no [render]");
    Snapshot s6 = store::LoadSnapshot(path.c_str());
    CHECK(s6.values[kKeyPx] == 2048.0f && video::PickOf(s6.values[kKeyMode], s6.values[kKeyPx]) == video::Square(2048), "the side is in the file");

    // 5. through the ini's thread: the requests pass its sanity check, and the answer is the panel's
    const std::wstring tpath = g_dir + L"\\pages_thread.ini";
    Spit(tpath, kIni2);
    store::Start(tpath.c_str());
    Snapshot ts = store::Read();
    Model tm;
    tm.Open(ts);
    tm.Click({Kind::Tab, kPageVideo, 0});
    tm.Click({Kind::Preset, 5, 0});
    Action ta = tm.Click({Kind::Apply, -1, 0});
    CHECK(store::Submit(ta.request), "the thread takes a Video request");
    Snapshot got;
    for (int i = 0; i < 80; ++i) {
        got = store::Read();
        if (got.sequence == ta.request.sequence && got.result != Result::Saving) break;
        Sleep(50);
    }
    CHECK(got.result == Result::Saved && got.values[kKeyPx] == 3072.0f && got.values[kKeyMode] == 0.0f, "and saves it");
    tm.Observe(got);
    CHECK(!tm.Dirty() && tm.FilePick() == video::Square(3072), "the panel took the answer through the thread");
    tm.Click({Kind::Tab, kPageBody, 0});
    Action tf = tm.Click({Kind::Forget, -1, 0});
    CHECK(tf.what == Do::Save && store::Submit(tf.request), "the thread takes a Forget request");
    for (int i = 0; i < 80; ++i) {
        got = store::Read();
        if (got.sequence == tf.request.sequence && got.result != Result::Saving) break;
        Sleep(50);
    }
    CHECK(got.result == Result::Saved && got.values[kKeyVersion] == 0.0f && got.values[kKeyEye] == 0.0f, "and forgets");
    tm.Observe(got);
    CHECK(!tm.Measured().any, "the panel says not measured");
    // an outside measurement shows up in the panel without any Apply
    std::string text = Slurp(tpath);
    text.replace(text.find("version=0"), 9, "version=1");
    text.replace(text.find("eye_height_m=0"), 14, "eye_height_m=1.80");
    text.replace(text.find("arm_span_m=0"), 12, "arm_span_m=1.85");
    Spit(tpath, text);
    bool seen = false;
    for (int i = 0; i < 60 && !seen; ++i) {
        got = store::Read();
        seen = std::fabs(got.values[kKeyEye] - 1.8f) < 1e-4f;
        if (!seen) Sleep(50);
    }
    CHECK(seen, "the thread picks up a measurement made elsewhere");
    tm.Observe(got);
    CHECK(tm.Measured().valid && std::fabs(tm.Measured().eye - 1.8f) < 1e-3f, "and the panel shows it");
    store::Stop();
    store::Join(3000);
}

// -- 14. the pictures of the new pages ------------------------------------------------------------------------
static void TestPaintPages() {
    Model m;
    m.Open(Snap());
    std::vector<uint32_t> p0, p2, p3, a, b, c, d;
    CHECK(Paint(m, {}, p0), "page 0");
    m.Click({Kind::Tab, kPageVideo, 0});
    CHECK(Paint(m, {}, p2) && p2.size() == (size_t)kW * kH, "the Video page paints");
    m.Click({Kind::Tab, kPageBody, 0});
    CHECK(Paint(m, {}, p3) && p3.size() == (size_t)kW * kH, "the Body page paints");
    CHECK(p0 != p2 && p2 != p3 && p0 != p3, "three different pictures");

    m.Click({Kind::Tab, kPageVideo, 0});
    Paint(m, {}, a);
    m.Click({Kind::Preset, 4, 0});
    Paint(m, {}, b);
    CHECK(a != b, "a chosen size is painted");
    video::Env e;
    e.now = {2048, 2048};
    e.recommended = {2496, 2688};
    e.maximum = {2400, 2400};
    m.SetEnv(e);
    Paint(m, {}, c);
    CHECK(b != c, "the sizes the plugin reports are painted");
    float u, v;
    Centre(kPreset(2), &u, &v);
    Paint(m, HitTest(kPageVideo, u, v), d);
    CHECK(c != d, "a hovered preset looks different");
    m.Click({Kind::Slider, 3008, 0});
    std::vector<uint32_t> f;
    Paint(m, {}, f);
    CHECK(f != c, "the thumb moves");
    m.Click({Kind::Preset, 7, 0});
    std::vector<uint32_t> g;
    Paint(m, {}, g);
    CHECK(g != f, "the headset's own size looks different (a hollow thumb)");
    CHECK(std::strlen(StatusText(m)) > 20 && std::string(StatusText(m)).find("Not applied") == 0, "the status line says it is not applied");
    m.Click({Kind::Tab, kPageBody, 0});

    Model s;
    s.Open(SnapWith({{kKeyUse, 1}, {kKeyVersion, 1}, {kKeyEye, 1.62f}, {kKeySpan, 1.71f}}));
    s.Click({Kind::Tab, kPageBody, 0});
    std::vector<uint32_t> measured, hot;
    Paint(s, {}, measured);
    CHECK(measured != p3, "a measured body looks different from an unmeasured one");
    Centre(kCalibrate, &u, &v);
    Paint(s, HitTest(kPageBody, u, v), hot);
    CHECK(hot != measured, "the hovered Calibrate body button looks different");
    Model nr;
    nr.Open(Snap(false));
    nr.Click({Kind::Tab, kPageBody, 0});
    CHECK(Paint(nr, {}, a) && Paint(nr, {}, b) && std::strlen(StatusText(nr)) > 10, "no ini: both new pages paint");
    nr.Click({Kind::Tab, kPageVideo, 0});
    CHECK(Paint(nr, {}, b), "the Video page without an ini paints");
}

// -- 15. the game view row (F1 - F4) ----------------------------------------------------------------------------
static void TestViewRow() {
    CHECK(kViewRow == kRowsPerPage - 1 && RowsOn(kPageView) <= kViewRow && kViews == 4, "the first page's last row is the view row; the table leaves it free");
    for (int i = 0; i < kViews; ++i) {
        CHECK(Inside(kViewBtn(i)) && !Overlap(kViewBtn(i), kStatus) && !Overlap(kViewBtn(i), kApply) && !Overlap(kViewBtn(i), kClose), "a view button inside the atlas, clear of the status line and the buttons");
        if (i + 1 < kViews) CHECK(!Overlap(kViewBtn(i), kViewBtn(i + 1)), "view buttons apart");
        for (int r = 0; r < RowsOn(kPageView); ++r)
            CHECK(!Overlap(kViewBtn(i), kDec(r)) && !Overlap(kViewBtn(i), kInc(r)) && !Overlap(kViewBtn(i), kVal(r)) && !Overlap(kViewBtn(i), kToggle(r)), "and clear of the table's rows");
        float u, v;
        Centre(kViewBtn(i), &u, &v);
        CHECK((HitTest(kPageView, u, v) == Hit{Kind::View, i + 1, 0}), "a view button's centre");
        CHECK(HitTest(kPageOptions, u, v).kind != Kind::View && HitTest(kPageVideo, u, v).kind != Kind::View && HitTest(kPageBody, u, v).kind != Kind::View, "the view row is the first page's only");
    }
    {
        const float gap = (kViewBtn(0).x + kViewBtn(0).w + 4.0f) / kW;
        CHECK(HitTest(kPageView, gap, (kViewBtn(0).y + 20.0f) / kH).kind == Kind::None, "between two buttons is nothing");
        CHECK(Repeats({Kind::View, 2, 0}) == false, "a view button is a press, not a repeat");
    }

    {   // the model: asks for a view, changes nothing of its own
        Model m;
        m.Open(Snap());
        CHECK(m.View() == 0, "no view known yet");
        const Action a = m.Click({Kind::View, 2, 0});
        CHECK(a.what == Do::View && a.view == 2 && !m.Dirty() && !m.Busy(), "F2 asks for view 2");
        m.SetView(4);
        CHECK(m.View() == 4 && m.Click({Kind::View, 4, 0}).what == Do::Nothing, "the view it is already is not asked for again");
        CHECK(m.Click({Kind::View, 1, 0}).view == 1 && m.Click({Kind::View, 3, 0}).view == 3, "F1 and F3");
        CHECK(m.Click({Kind::View, 0, 0}).what == Do::Nothing && m.Click({Kind::View, 5, 0}).what == Do::Nothing && m.Click({Kind::View, -1, 0}).what == Do::Nothing,
              "a view that is not there is nothing");
        const uint64_t rev = m.revision;
        m.SetView(4);
        CHECK(m.revision == rev, "the same view repaints nothing");
        m.SetView(2);
        CHECK(m.View() == 2 && m.revision != rev, "a new view repaints");
        m.SetView(9);
        CHECK(m.View() == 0, "an unknown view is none");
        Model nr;
        nr.Open(Snap(false));
        CHECK(nr.Click({Kind::View, 1, 0}).what == Do::View, "the view is the game's, not the ini's: no ini needed");
        Model busy = OnPage(Snap(), kPageVideo);
        busy.Click({Kind::Preset, 3, 0});
        busy.Click({Kind::Apply, -1, 0});
        CHECK(busy.Click({Kind::View, 1, 0}).what == Do::View, "and an Apply in flight does not stop it");
        Model closed;
        CHECK(closed.Click({Kind::View, 1, 0}).what == Do::Nothing, "a closed panel does nothing");
    }

    {   // the hands: a click on F2 closes the panel with the view, and is not the game's click
        Rig r;
        r.view = 4;
        r.Open();
        CHECK(r.c.model.View() == 4, "the model has the view from the frame");
        float u, v;
        Centre(kViewBtn(1), &u, &v);
        r.Point(1, u, v);
        r.Step();
        r.PullTrigger(1, true);
        const FrameOut o = r.Step();
        CHECK(o.action.what == Do::View && o.action.view == 2 && !o.open && o.justClosed, "F2: asked for, and the panel closes");
        CHECK(o.pad[1].trigger == 0.0f && !(o.pad[1].buttons & kTrigger), "the trigger that pressed it is not the game's");
        CHECK(!r.c.Open() && !r.c.Resuming(), "closed for good (the view is not F4 any more)");
        r.Step();
        CHECK(r.last.pad[1].trigger == 0.0f, "still held back while it is down");
        r.PullTrigger(1, false);
        r.Step();
        r.pad[1].trigger = 1.0f;
        CHECK(r.Step().pad[1].trigger == 1.0f, "let go once, the next press is the game's again");
        r.pad[1].trigger = 0.0f;
        r.Step();
    }
    {   // the view it is already: nothing, the panel stays
        Rig r;
        r.view = 4;
        r.Open();
        const FrameOut o = r.Click(1, kViewBtn(3));
        CHECK(o.action.what == Do::Nothing && r.c.Open(), "F4 in F4: nothing, the panel stays");
    }
    {   // each of the other three, with a draft that was not applied: the panel closes and that draft is dropped, as Close drops it
        for (int k = 1; k <= 3; ++k) {
            Rig r;
            r.view = 4;
            r.Open();
            r.Click(1, kInc(0));
            CHECK(r.c.model.Dirty(), "an edit");
            const FrameOut o = r.Click(1, kViewBtn(k - 1));
            CHECK(o.action.what == Do::View && o.action.view == k && !r.c.Open(), "F1 / F2 / F3 close the panel");
            r.Open();
            CHECK(r.c.Open() && !r.c.model.Dirty(), "and the next opening is a fresh one");
        }
    }
    {   // the view not known (0): every button asks
        Rig r;
        r.view = 0;
        r.Open();
        const FrameOut o = r.Click(1, kViewBtn(3));
        CHECK(o.action.what == Do::View && o.action.view == 4, "no lit button: F4 is asked for too");
    }
    {   // the pictures: the lit button differs, a hovered one too
        Model m;
        m.Open(Snap());
        std::vector<uint32_t> a, b, c;
        Paint(m, {}, a);
        m.SetView(4);
        Paint(m, {}, b);
        CHECK(a != b, "the current view's button is lit");
        m.SetView(2);
        Paint(m, {}, c);
        CHECK(b != c, "and moves with the view");
        float u, v;
        Centre(kViewBtn(0), &u, &v);
        std::vector<uint32_t> d;
        Paint(m, HitTest(kPageView, u, v), d);
        CHECK(c != d, "a hovered view button looks different");
        m.Click({Kind::Tab, kPageOptions, 0});
        Paint(m, {}, d);
        CHECK(d != c, "the other page has no view row");
    }
}

// hset_test.exe --dump <folder>: the painted pages as .bmp files, to look at (page0, page1 with a hover and an edit).
static void WriteBmp(const std::string& path, const std::vector<uint32_t>& px) {
    const uint32_t bytes = (uint32_t)(px.size() * 4);
    uint8_t h[54] = {'B', 'M'};
    const uint32_t size = 54 + bytes, off = 54, dib = 40, w = kW, hh = (uint32_t)(-kH), planes = 1, bpp = 32;
    memcpy(h + 2, &size, 4); memcpy(h + 10, &off, 4); memcpy(h + 14, &dib, 4); memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
    memcpy(h + 26, &planes, 2); memcpy(h + 28, &bpp, 2); memcpy(h + 34, &bytes, 4);
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)h, 54);
    for (uint32_t c : px) {   // our pixels are R in the low byte; a BMP wants B first
        const uint32_t b = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, r = c & 0xFF;
        const uint32_t out = b | g << 8 | r << 16 | 0xFF000000u;
        f.write((const char*)&out, 4);
    }
}
static void Dump(const std::string& dir) {
    Snapshot s = Snap();
    s.values[0] = s.file[0] = 2.5f;
    Model m;
    m.Open(s);
    m.SetView(4);
    std::vector<uint32_t> px;
    float u, v;
    Centre(kInc(1), &u, &v);
    Paint(m, HitTest(0, u, v), px);
    WriteBmp(dir + "/hset_page0_clean.bmp", px);
    m.Click({Kind::Change, 0, 1});
    m.Click({Kind::Change, 4, -1});
    Centre(kApply, &u, &v);
    Paint(m, HitTest(0, u, v), px);
    WriteBmp(dir + "/hset_page0_dirty.bmp", px);
    m.Click({Kind::Tab, 1, 0});
    Centre(kToggle(0), &u, &v);
    Paint(m, HitTest(1, u, v), px);
    WriteBmp(dir + "/hset_page1.bmp", px);
    Model nr;
    nr.Open(Snap(false));
    Paint(nr, {}, px);
    WriteBmp(dir + "/hset_noini.bmp", px);

    // the Video page: a size picked and not applied, with the headset's numbers; the Body page measured and not
    Snapshot vs = SnapWith({{kKeyPx, 2048}});
    Model vm;
    vm.Open(vs);
    video::Env env;
    env.now = {2048, 2048};
    env.recommended = {2496, 2688};
    env.maximum = {4096, 4096};
    vm.SetEnv(env);
    vm.Click({Kind::Tab, kPageVideo, 0});
    Paint(vm, {}, px);
    WriteBmp(dir + "/hset_page2_video_clean.bmp", px);
    vm.Click({Kind::Slider, 3008, 0});
    Paint(vm, {Kind::Slider, 3008, 0}, px);
    WriteBmp(dir + "/hset_page2_video_edited.bmp", px);
    vm.Click({Kind::Preset, 7, 0});
    Centre(kPreset(2), &u, &v);
    Paint(vm, HitTest(kPageVideo, u, v), px);
    WriteBmp(dir + "/hset_page2_video_headset.bmp", px);
    Model vw;
    vw.Open(SnapWith({{kKeyPx, 4096}}));
    env.maximum = {2400, 2688};
    vw.SetEnv(env);
    vw.Click({Kind::Tab, kPageVideo, 0});
    Paint(vw, {}, px);
    WriteBmp(dir + "/hset_page2_video_over.bmp", px);
    Model bm;
    bm.Open(SnapWith({{kKeyUse, 1}, {kKeyVersion, 1}, {kKeyEye, 1.62}, {kKeySpan, 1.71}}));
    bm.Click({Kind::Tab, kPageBody, 0});
    Centre(kCalibrate, &u, &v);
    Paint(bm, HitTest(kPageBody, u, v), px);
    WriteBmp(dir + "/hset_page3_body_measured.bmp", px);
    Model bn;
    bn.Open(Snap());
    bn.Click({Kind::Tab, kPageBody, 0});
    Paint(bn, {}, px);
    WriteBmp(dir + "/hset_page3_body_none.bmp", px);
}

int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "--dump")) { Dump(argv[2]); return 0; }
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_dir = std::wstring(tmp) + L"hset_test_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(g_dir.c_str(), nullptr);

    TestDefs();
    TestHitTest();
    TestModel();
    TestRay();
    TestChord();
    TestClicks();
    TestGate();
    TestIni();
    TestStore();
    TestPaint();
    TestVideoPure();
    TestVideoModel();
    TestPagesHit();
    TestBodyModel();
    TestPagesHands();
    TestIniPages();
    TestPaintPages();
    TestViewRow();

    // the temp folder's files go
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((g_dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((g_dir + L"\\" + fd.cFileName).c_str()); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(g_dir.c_str());

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
