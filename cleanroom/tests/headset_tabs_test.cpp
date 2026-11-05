// Offline checks of the skill panel's tab strip and its Settings tab ([openxr] headset_settings with the tiles' look): the strip's places and
// hit test, the sheet the settings pages are laid on (vr/tiles_geom.h) against the pages' own hit test (vr/hset_model.h), the model's tabs
// (vr/wheel_model.h: which one is shown, remembered, opened on, closed), and the controller on synthetic headset frames with the Settings tab
// hosted - the chord, the long press, the clicks on the pages, Apply, the view buttons, Calibrate body and the panel's return, the slider's
// drag - plus the window's pictures. No game, no OpenXR, no headset.
// Run: build/Release/headset_tabs_test.exe                    (exit code 0 = all passed)
//      build/Release/headset_tabs_test.exe --dump <folder>    also writes the window's pictures as BMP files to look at

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "../../vr/game_ui.h"
#include "../../vr/hset_click.h"
#include "../../vr/hset_scene.h"
#include "../../vr/tiles_geom.h"
#include "../../vr/tiles_paint.h"
#include "../../vr/wheel_controller.h"
#include "../../vr/wheel_flip.h"
#include "../../vr/wheel_icons.h"
#include "../../vr/wheel_model.h"
#include "../../vr/wheel_paint.h"

using namespace wheel;
namespace di = directinput;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
static bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }

static_assert(tiles::kSheetW == hset::kW && tiles::kSheetH == hset::kH, "the window's sheet is the settings pages' own layout");

static float CentreX(const tiles::PRect& r) { return 0.5f * (r.l + r.r); }
static float CentreY(const tiles::PRect& r) { return 0.5f * (r.t + r.b); }
static bool InsideR(const tiles::PRect& r, const tiles::PRect& o) { return r.l >= o.l && r.t >= o.t && r.r <= o.r && r.b <= o.b; }
static bool Overlap(const tiles::PRect& a, const tiles::PRect& b) { return a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b; }


// the window's pixel of a point of the sheet (hset_model.h's pixels)
static float WinX(float sheetX) { return tiles::kSheetX + sheetX * tiles::kSheetScale; }
static float WinY(float sheetY) { return tiles::kSheetY + sheetY * tiles::kSheetScale; }

// -- the strip and the sheet ------------------------------------------------------------------------------------------------------------

static void TestStrip() {
    for (int i = 0; i < tiles::kTabCount; ++i) {
        CHECK(InsideR(tiles::TabRect(i), tiles::kTitlePlate));
        if (i + 1 < tiles::kTabCount) CHECK(!Overlap(tiles::TabRect(i), tiles::TabRect(i + 1)));
        const float x = CentreX(tiles::TabRect(i)), y = CentreY(tiles::TabRect(i));
        CHECK(tiles::HitTest(Page::Wheel, x / tiles::kW, y / tiles::kH, kNone, 0, true) == tiles::TabCell(i));
        CHECK(tiles::HitTest(Page::Wheel, x / tiles::kW, y / tiles::kH, kNone, 0, true, true) == tiles::TabCell(i));   // also on the Settings tab
        CHECK(tiles::HitTest(Page::Wheel, x / tiles::kW, y / tiles::kH, kNone, 0, false) == kNone);                    // no strip without the key
        CHECK(tiles::HitTest(Page::PickSlot, x / tiles::kW, y / tiles::kH, kNone, 0, true) == kNone);                  // the editor's pages have their titles
        CHECK(tiles::HitTest(Page::PickSkill, x / tiles::kW, y / tiles::kH, kNone, 24, true) == kNone);
    }
    CHECK(tiles::TabCell(0) == kTabSkills && tiles::TabCell(1) == kTabSettings && kTabSkills > kCentre && kTabSettings > kTabSkills);
    // the strip is centred in the plate, Skills left of Settings
    CHECK(tiles::TabRect(0).r < tiles::TabRect(1).l);
    CHECK(Near(0.5f * (tiles::TabRect(0).l + tiles::TabRect(1).r), 0.5f * tiles::kW, 0.5f));
    // the tiles are as they were: with the strip but no Settings tab every tile still hits itself
    for (int k = 0; k < kSlotCount; ++k) {
        const tiles::PRect r = tiles::SlotRect(k);
        CHECK(tiles::HitTest(Page::Wheel, CentreX(r) / tiles::kW, CentreY(r) / tiles::kH, kNone, 0, true) == k);
        CHECK(tiles::HitTest(Page::Wheel, CentreX(r) / tiles::kW, CentreY(r) / tiles::kH, kNone, 0, true, true) == kNone);   // the Settings tab: no tiles
    }
}

static void TestSheet() {
    // the sheet's corners and its page tabs: the page tabs stand under the title plate, the buttons end above the panel's foot
    float su, sv;
    tiles::SheetPoint(WinX(0.0f) / tiles::kW, WinY(0.0f) / tiles::kH, &su, &sv);
    CHECK(Near(su, 0.0f, 1e-4f) && Near(sv, 0.0f, 1e-4f));
    tiles::SheetPoint(WinX((float)hset::kW) / tiles::kW, WinY((float)hset::kH) / tiles::kH, &su, &sv);
    CHECK(Near(su, 1.0f, 1e-4f) && Near(sv, 1.0f, 1e-4f));
    const hset::Rect t0 = hset::kTab(0), ap = hset::kApply, cl = hset::kClose, rc = hset::kRecentre, st = hset::kStatus;
    CHECK(WinY((float)t0.y) > tiles::kTitlePlate.b + 20.0f);
    CHECK(WinY((float)(ap.y + ap.h)) < tiles::kPanel.b - 40.0f && WinY((float)(cl.y + cl.h)) < tiles::kPanel.b - 40.0f);
    CHECK(WinX((float)rc.x) > tiles::kPanel.l + 20.0f && WinX((float)(ap.x + ap.w)) < tiles::kPanel.r - 20.0f);
    CHECK(WinY((float)(st.y + st.h)) < WinY((float)ap.y));
    // the whole of the sheet below its title (y 84 on) is inside the panel
    CHECK(WinX(32.0f) > tiles::kPanel.l && WinX(992.0f) < tiles::kPanel.r && WinY(84.0f) > tiles::kPanel.t && WinY(690.0f) < tiles::kPanel.b);

    // every button of every page, at its window pixel, is the same button for the pages' own hit test through SheetPoint
    int checked = 0;
    for (int page = 0; page < hset::PageCount(); ++page) {
        hset::Model m;
        hset::Snapshot sn;
        sn.ready = true;
        m.Open(sn);
        m.Click({hset::Kind::Tab, page, 0});
        const hset::Scene scene = hset::Compose(m, {}, hset::ComposeOptions{false, true, nullptr});
        for (const hset::Item& it : scene) {
            if (it.type != hset::Item::Type::Button) continue;
            const float cx = WinX(it.r.x + 0.5f * it.r.w), cy = WinY(it.r.y + 0.5f * it.r.h);
            tiles::SheetPoint(cx / tiles::kW, cy / tiles::kH, &su, &sv);
            const hset::Hit h = hset::HitTest(page, su, sv);
            CHECK(h.kind != hset::Kind::None);
            // and it is the button of that rectangle: the same hit from the sheet's own pixel
            const hset::Hit own = hset::HitTest(page, (it.r.x + 0.5f * it.r.w) / hset::kW, (it.r.y + 0.5f * it.r.h) / hset::kH);
            CHECK(h == own);
            ++checked;
        }
    }
    CHECK(checked > 30);
    // the strip's pixels are no button of the sheet
    for (int i = 0; i < tiles::kTabCount; ++i)
        for (float fx : {0.1f, 0.5f, 0.9f})
            for (float fy : {0.1f, 0.5f, 0.9f}) {
                const tiles::PRect r = tiles::TabRect(i);
                tiles::SheetPoint((r.l + fx * (r.r - r.l)) / tiles::kW, (r.t + fy * (r.b - r.t)) / tiles::kH, &su, &sv);
                for (int page = 0; page < hset::PageCount(); ++page) CHECK(hset::HitTest(page, su, sv).kind == hset::Kind::None);
            }
}

// -- the model's tabs ---------------------------------------------------------------------------------------------------------------------

static Skill Sk(int id, const char* name) {
    Skill s;
    s.id = id;
    s.known = true;
    s.available = true;
    std::snprintf(s.name, sizeof s.name, "%s", name);
    return s;
}
static Snapshot MakeSnap(uint64_t session, uint64_t at) {
    Snapshot s;
    s.session = session;
    s.at = at;
    s.ready = true;
    s.editable = true;
    s.bank = 0;
    s.slot[0] = Sk(36, "Fire Bolt");
    s.slot[1] = Sk(47, "Fire Ball");
    for (int k = 2; k < kSlotCount; ++k) s.slot[k].known = true;
    s.count = 6;
    for (int i = 0; i < 6; ++i) {
        char name[kNameLen];
        std::snprintf(name, sizeof name, "Skill %d", i);
        s.catalog[i] = Sk(100 + i, name);
    }
    return s;
}

struct Rig {
    Model m;
    Snapshot snap;
    double t = 10.0;
    uint64_t now = 5000;
    Style style = Style::Tiles;
    bool tabs = true;
    Reply reply;
    explicit Rig(Style st = Style::Tiles) : style(st) { snap = MakeSnap(7, now); }
    Out Step(bool toggle = false, bool click = false, float u = -1, float v = -1, int openTab = -1, int setTab = -1, bool close = false, bool back = false) {
        In in;
        in.time = t += 0.01;
        in.nowMs = now += 10;
        snap.at = now;
        in.allowed = true;
        in.session = 7;
        in.epoch = 1;
        in.toggle = toggle;
        in.back = back;
        in.pointer = u >= 0;
        in.u = u;
        in.v = v;
        in.click = click;
        in.style = style;
        in.snap = &snap;
        in.reply = reply;
        in.tabs = tabs;
        in.openTab = openTab;
        in.setTab = setTab;
        in.close = close;
        return m.Step(in);
    }
    void Px(float x, float y, bool click = false) { Step(false, click, x / tiles::kW, y / tiles::kH); }
};

static void TestModelTabs() {
    {   // no strip: the window is what it was
        Rig r;
        r.tabs = false;
        r.Step(true);
        CHECK(r.m.Open() && !r.m.Current().tabs && r.m.Current().tab == Tab::Skills);
        const tiles::PRect t = tiles::TabRect(1);
        r.Px(CentreX(t), CentreY(t), true);
        CHECK(r.m.Current().tab == Tab::Skills && r.m.Current().hover == kNone);
        r.Step(false, false, -1, -1, int(Tab::Settings), int(Tab::Settings));
        CHECK(r.m.Current().tab == Tab::Skills);   // nothing asks for a tab that is not there
    }
    {   // the wheel's look has no strip even if the key is on
        Rig r(Style::Wheel);
        r.Step(true);
        CHECK(r.m.Open() && !r.m.Current().tabs);
        r.Step(false, false, -1, -1, int(Tab::Settings), int(Tab::Settings));
        CHECK(r.m.Current().tab == Tab::Skills);
    }
    {   // with the strip: it is there, the Skills tab first, a click on a tab switches, the laser lights the tab under it
        Rig r;
        r.Step(true);
        CHECK(r.m.Open() && r.m.Current().tabs && r.m.Current().tab == Tab::Skills && r.m.Current().page == Page::Wheel);
        const tiles::PRect s = tiles::TabRect(1);
        r.Px(CentreX(s), CentreY(s));
        CHECK(r.m.Current().hover == kTabSettings);
        const uint32_t rev = r.m.Current().revision;
        const Out o = r.Step(false, true, CentreX(s) / tiles::kW, CentreY(s) / tiles::kH);
        CHECK(r.m.Current().tab == Tab::Settings && r.m.Current().revision != rev && o.feedback == Feedback::Hover);
        // on the Settings tab the tiles are nothing, the strip still is
        const tiles::PRect tile = tiles::SlotRect(1);
        r.Px(CentreX(tile), CentreY(tile));
        CHECK(r.m.Current().hover == kNone);
        const tiles::PRect k = tiles::TabRect(0);
        r.Px(CentreX(k), CentreY(k));
        CHECK(r.m.Current().hover == kTabSkills);
        r.Step(false, true, CentreX(k) / tiles::kW, CentreY(k) / tiles::kH);
        CHECK(r.m.Current().tab == Tab::Skills);
        // a click on the tab that is shown changes nothing
        const uint32_t rev2 = r.m.Current().revision;
        r.Step(false, true, CentreX(k) / tiles::kW, CentreY(k) / tiles::kH);
        CHECK(r.m.Current().tab == Tab::Skills && r.m.Current().revision == rev2);
    }
    {   // the tab is remembered while the session runs: closed and opened again, it is the one it was; a session of its own starts on Skills
        Rig r;
        r.Step(true);
        r.Step(false, false, -1, -1, -1, int(Tab::Settings));
        CHECK(r.m.Current().tab == Tab::Settings);
        r.Step(true);
        CHECK(!r.m.Open() && r.m.Current().tab == Tab::Settings);
        r.Step(true);
        CHECK(r.m.Open() && r.m.Current().tab == Tab::Settings);
        r.Step(true);
        r.Step(true, false, -1, -1, int(Tab::Skills));
        CHECK(r.m.Open() && r.m.Current().tab == Tab::Skills);   // a way in that names its tab
        r.Step(true);
        r.Step(true, false, -1, -1, int(Tab::Settings));
        CHECK(r.m.Open() && r.m.Current().tab == Tab::Settings);
        r.Step(true);
        r.m.Reset();
        CHECK(r.m.Current().tab == Tab::Skills);
    }
    {   // the Settings tab opens without the game's skills; the Skills tab does not
        Rig r;
        r.snap.ready = false;
        r.Step(true);
        CHECK(!r.m.Open());
        const Out o = r.Step(true, false, -1, -1, int(Tab::Settings));
        CHECK(r.m.Open() && r.m.Current().tab == Tab::Settings && o.feedback == Feedback::Opened);
        // (no strip, no Settings tab: it is refused as ever)
        Rig n;
        n.tabs = false;
        n.snap.ready = false;
        n.Step(true, false, -1, -1, int(Tab::Settings));
        CHECK(!n.m.Open());
    }
    {   // switching while up: on the top level only; the editor's pages keep their own way
        Rig r;
        r.Step(true);
        r.Step(false, false, -1, -1, -1, int(Tab::Settings));
        CHECK(r.m.Current().tab == Tab::Settings);
        r.Step(false, false, -1, -1, -1, int(Tab::Skills));
        CHECK(r.m.Current().tab == Tab::Skills);
        // into the editor: the strip is gone (its place is nothing), a request to switch is not taken there
        const tiles::PRect eb = tiles::ButtonRect(Page::Wheel, 0);
        r.Px(CentreX(eb), CentreY(eb), true);
        CHECK(r.m.Current().page == Page::PickSlot);
        const tiles::PRect s = tiles::TabRect(1);
        r.Px(CentreX(s), CentreY(s));
        CHECK(r.m.Current().hover == kNone);
        r.Step(false, true, CentreX(s) / tiles::kW, CentreY(s) / tiles::kH);
        CHECK(r.m.Current().tab == Tab::Skills && r.m.Current().page == Page::PickSlot);
        r.Step(false, false, -1, -1, -1, int(Tab::Settings));
        CHECK(r.m.Current().tab == Tab::Skills);
    }
    {   // closing: the window's close, B, the toggle - on either tab; a request that the pages made
        Rig r;
        r.Step(true);
        r.Step(false, false, -1, -1, -1, int(Tab::Settings));
        Out o = r.Step(false, false, -1, -1, -1, -1, true);
        CHECK(!r.m.Open() && o.feedback == Feedback::Closed && r.m.Current().tab == Tab::Settings);
        r.Step(true);
        o = r.Step(false, false, -1, -1, -1, -1, false, true);
        CHECK(!r.m.Open() && o.feedback == Feedback::Closed);
        r.Step(true);
        r.Step(true);
        CHECK(!r.m.Open());
    }
    {   // the pages for the painter: a copy when they changed, nothing when not
        Rig r;
        r.Step(true);
        hset::Model s;
        hset::Snapshot sn;
        sn.ready = true;
        s.Open(sn);
        const uint32_t rev = r.m.Current().revision;
        r.m.SetSettings(s, {});
        CHECK(r.m.Current().revision != rev && r.m.Current().settings.open);
        const uint32_t rev2 = r.m.Current().revision;
        r.m.SetSettings(s, {});
        CHECK(r.m.Current().revision == rev2);
        r.m.SetSettings(s, {hset::Kind::Tab, 1, 0});
        CHECK(r.m.Current().revision != rev2 && r.m.Current().settingsHover.kind == hset::Kind::Tab);
        s.Click({hset::Kind::Tab, 2, 0});
        r.m.SetSettings(s, {hset::Kind::Tab, 1, 0});
        CHECK(r.m.Current().settings.page == 2);
    }
}

// -- the controller with the Settings tab ---------------------------------------------------------------------------------------------------

// A game's roster as the plugin reads it (vr/party_cmd.h), for the Party page's pictures: one in our party, one inviting us, one to invite, one invited.
static hset::party::View SampleRoster() {
    hset::party::View r;
    r.known = r.inParty = true;
    const char* names[4] = {"Bear", "Pony", "Ann", "Rivas"};
    const uint32_t states[4] = {hset::party::kInParty, hset::party::kInvitedUs, hset::party::kNone, hset::party::kWeInvited};
    for (int i = 0; i < 4; ++i) {
        hset::party::Member m;
        m.id = 10 + i;
        m.name = names[i];
        m.cls = i * 2;
        m.level = 20 + i;
        m.state = states[i];
        m.sameParty = i == 0;
        r.others.push_back(m);
    }
    return r;
}

struct World {
    Controller c;
    Snapshot snap;
    uint64_t now = 20000;
    uint32_t counter = 0;
    double time = 100.0;
    di::Frame f{};
    Reply reply;
    SettingsIn host;
    bool useHost = true;
    bool f4 = true;
    bool request = false;
    int openMode;
    explicit World(int mode = 1, bool tabs = true) : openMode(mode) {
        Settings cfg;
        cfg.style = Style::Tiles;
        cfg.openMode = mode;
        cfg.settingsTab = tabs;
        c.Configure(cfg);
        snap = MakeSnap(3, now);
        host.ini.ready = true;
        host.ini.values[hset::kKeyPx] = host.ini.file[hset::kKeyPx] = 2048.0f;
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
    Step Next(double dt = 0.011) {
        f.counter = ++counter;
        f.time = time += dt;
        f.displayTime = (int64_t)(time * 1e9);
        now += (uint64_t)(dt * 1000.0);
        snap.at = now;
        const bool req = request;
        request = false;
        return c.Frame(f, snap, now, reply, f4, req, useHost ? &host : nullptr);
    }
    void Hand(int h, float trigger, float squeeze, uint32_t buttons) {
        f.hand[h].trigger = trigger;
        f.hand[h].squeeze = squeeze;
        f.hand[h].buttons = buttons;
    }
    void AimUV(float u, float v) {
        const Pose p = c.PanelPose();
        const float w = PanelWidthM(c.Config()), h = PanelHeightM(c.Config());
        const V3 target = Add(p.p, Rot(p.q, V3{(u - 0.5f) * w, (0.5f - v) * h, 0.0f}));
        const V3 from{f.hand[1].aim.pos[0], f.hand[1].aim.pos[1], f.hand[1].aim.pos[2]};
        const V3 d = Unit(Sub(target, from));
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
    void AimPx(float x, float y) { AimUV(x / tiles::kW, y / tiles::kH); }
    void AimSheet(float x, float y) { AimPx(WinX(x), WinY(y)); }
    void AimRect(const hset::Rect& r) { AimSheet(r.x + 0.5f * r.w, r.y + 0.5f * r.h); }
    // aim, pull the trigger (the frame with the click is returned), let go
    Step ClickPx(float x, float y) {
        AimPx(x, y);
        Hand(1, 0, 0, 0);
        Next();
        Hand(1, 0.9f, 0, 0);
        const Step s = Next();
        Hand(1, 0, 0, 0);
        Next();
        return s;
    }
    Step ClickSheet(const hset::Rect& r) { return ClickPx(WinX(r.x + 0.5f * r.w), WinY(r.y + 0.5f * r.h)); }
    Step ClickTab(int i) { return ClickPx(CentreX(tiles::TabRect(i)), CentreY(tiles::TabRect(i))); }
    // a page of the Settings tab: its tab on the sheet, and a World page's button under it (World is one tab of four pages)
    Step ClickPage(int page) {
        Step s = ClickSheet(hset::TabRect(hset::TabOf(page), c.SettingsModel().View()));   // (the view's strip: VR F1 - F3 have fewer tabs)
        if (hset::HasSubRow(page) && page != hset::SubFirst(page)) s = ClickSheet(hset::kSubBtn(page - hset::SubFirst(page), hset::SubCount(page)));   // (World's, Controls' buttons)
        if (page == hset::kPageParty) s = ClickSheet(hset::kPreviewBtn);   // the Co-op page's Party button (Party is that tab's second page)
        return s;
    }
    void Run(int frames) {
        for (int i = 0; i < frames; ++i) Next();
    }
    // the long press's request: the window opens
    Step LongPress() {
        request = true;
        return Next();
    }
    // the chord: both grips, the beat, the click (then everything let go)
    Step Chord(int clicker = 1) {
        Hand(0, 0, 0.9f, 0);
        Hand(1, 0, 0.9f, 0);
        for (int i = 0; i < 40; ++i) Next();   // (0.44 s)
        Hand(clicker, 0, 0.9f, di::kStick);
        const Step s = Next();
        Hand(clicker, 0, 0.9f, 0);
        Next();
        Hand(0, 0, 0, 0);
        Hand(1, 0, 0, 0);
        Next();
        return s;
    }
};

static void TestLongPressTabs() {
    {   // opens on Skills, with the strip; the Settings tab shows the pages with the ini's values; the tab is remembered
        World w;
        Step s = w.LongPress();
        CHECK(w.c.Open() && s.feedback == Feedback::Opened);
        CHECK(w.c.ViewNow().tabs && w.c.ViewNow().tab == Tab::Skills);
        CHECK(w.c.SettingsModel().open && w.c.SettingsModel().ready && w.c.SettingsModel().page == hset::kPageView);
        CHECK(s.wantStore);
        w.ClickTab(1);
        CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Settings);
        CHECK(w.c.ViewNow().settings.open && w.c.ViewNow().settings.Get()[hset::kKeyPx] == 2048.0f);
        // the long press closes it (the toggle), the next one opens it on the Settings tab it was on
        w.LongPress();
        CHECK(!w.c.Open());
        w.Run(5);
        w.LongPress();
        CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Settings);
        // and the Skills tab back
        w.ClickTab(0);
        CHECK(w.c.ViewNow().tab == Tab::Skills);
        w.LongPress();
        w.Run(5);
        w.LongPress();
        CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Skills);
    }
    {   // no host (or the key off): the window is what it was - no strip, nothing of the settings
        World w(1, false);
        w.LongPress();
        CHECK(w.c.Open() && !w.c.ViewNow().tabs && w.c.ViewNow().tab == Tab::Skills && !w.c.SettingsModel().open);
        Step s = w.Next();
        CHECK(!s.wantStore && s.settings.what == hset::Do::Nothing);
        World n;
        n.useHost = false;
        n.LongPress();
        CHECK(n.c.Open() && !n.c.ViewNow().tabs);
        // a click where the strip would be is nothing
        const tiles::PRect t = tiles::TabRect(1);
        n.ClickPx(CentreX(t), CentreY(t));
        CHECK(n.c.ViewNow().tab == Tab::Skills && n.c.Open());
    }
    {   // the wheel's own look has no strip
        World w;
        Settings cfg;
        cfg.style = Style::Wheel;
        cfg.openMode = 1;
        cfg.settingsTab = true;
        w.c.Configure(cfg);
        w.LongPress();
        CHECK(w.c.Open() && !w.c.ViewNow().tabs);
    }
    {   // B closes it from the Settings tab; the long press of the right stick closes it (the model's own timer: the click is the window's while it is up)
        World w;
        w.LongPress();
        w.ClickTab(1);
        w.Hand(1, 0, 0, di::kSecondary);
        const Step s = w.Next();
        CHECK(!w.c.Open() && s.feedback == Feedback::Closed);
        w.Hand(1, 0, 0, 0);
        w.Run(5);
        w.LongPress();
        w.ClickTab(1);
        w.Hand(1, 0, 0, 0);
        w.Run(3);
        w.Hand(1, 0, 0, di::kStick);   // held 0.5 s
        bool closed = false;
        for (int i = 0; i < 60 && !closed; ++i) closed = !w.c.Open() || (w.Next(), !w.c.Open());
        CHECK(closed);
    }
    {   // F4 only: leaving F4 closes the window and drops the pages; coming back starts fresh
        World w;
        w.LongPress();
        w.ClickTab(1);
        w.f4 = false;
        w.Next();
        CHECK(!w.c.Open() && !w.c.SettingsModel().open);
        w.f4 = true;
        w.Run(3);
        w.LongPress();
        CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Skills);   // (the standing-down reset it: a fresh window)
    }
    {   // the Settings tab opens when the game's skills are not there (a menu is another matter: no window in a menu)
        World w;
        w.snap.ready = false;
        w.LongPress();
        CHECK(!w.c.Open());   // the Skills tab is the last one: refused
        // (nothing to click: set the tab by the chord instead, below)
        World m;
        m.f.flags |= di::kInMenu;
        m.LongPress();
        CHECK(!m.c.Open());
    }
}

static void TestKeyOffLive() {
    World w;
    w.LongPress();
    w.ClickTab(1);
    CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Settings);
    Settings cfg = w.c.Config();
    cfg.settingsTab = false;   // [openxr] headset_settings switched off while it is up
    w.c.Configure(cfg);
    w.Next();
    CHECK(w.c.Open() && !w.c.ViewNow().tabs && w.c.ViewNow().tab == Tab::Skills);   // the window is the skills' again, the tiles are hit
    const tiles::PRect t = tiles::SlotRect(1);
    w.AimPx(CentreX(t), CentreY(t));
    w.Next();
    CHECK(w.c.ViewNow().hover == 1);
    const Step s = w.Next();
    CHECK(!s.wantStore && s.settings.what == hset::Do::Nothing);
    // and switched on again while it is up: the strip is there, on Skills
    cfg.settingsTab = true;
    w.c.Configure(cfg);
    w.Next();
    CHECK(w.c.ViewNow().tabs && w.c.ViewNow().tab == Tab::Skills);
}

static void TestChordTabs() {
    {   // the chord opens the same window on the Settings tab, from nothing, with the last tab Skills
        World w;
        Step arming;
        w.Hand(0, 0, 0.9f, 0);
        w.Hand(1, 0, 0.9f, 0);
        w.Run(10);   // 0.11 s: not yet
        arming = w.Next();
        CHECK(!w.c.Open() && (arming.take[0] & di::kStick) == 0 && (arming.take[1] & di::kStick) == 0);
        CHECK(arming.wantStore);   // the ini's thread starts to arm (the panel has values at once)
        w.Run(30);                 // past a third of a second
        arming = w.Next();
        CHECK(!w.c.Open() && (arming.take[0] & di::kStick) != 0 && (arming.take[1] & di::kStick) != 0);   // the clicks are the chord's now: no swap, no Alt
        w.Hand(1, 0, 0.9f, di::kStick);
        const Step s = w.Next();
        CHECK(w.c.Open() && s.feedback == Feedback::Opened && w.c.ViewNow().tab == Tab::Settings);
        CHECK(w.c.SettingsModel().open && w.c.SettingsModel().page == hset::kPageView);
        CHECK((s.take[1] & (di::kTakeTrigger | di::kTakeSqueeze | di::kStick)) == (di::kTakeTrigger | di::kTakeSqueeze | di::kStick));
        w.Hand(1, 0, 0.9f, 0);
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        w.Run(3);
        CHECK(w.c.Open());
        // the chord again closes it
        w.Chord();
        CHECK(!w.c.Open());
        // and opens it again on the Settings tab
        w.Chord();
        CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Settings);
    }
    {   // with the window up on the Skills tab the chord shows the settings, and the next chord closes it
        World w;
        w.LongPress();
        CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Skills);
        w.Chord();
        CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Settings);
        w.Chord();
        CHECK(!w.c.Open());
    }
    {   // in the editor the chord closes (a switch would drop a half-made choice silently)
        World w;
        w.LongPress();
        const tiles::PRect eb = tiles::ButtonRect(Page::Wheel, 0);
        w.ClickPx(CentreX(eb), CentreY(eb));
        CHECK(w.c.ViewNow().page == Page::PickSlot);
        w.Chord();
        CHECK(!w.c.Open());
    }
    {   // one grip, or a click before the beat: the game's own click
        World w;
        w.Hand(0, 0, 0.9f, 0);
        w.Run(60);
        w.Hand(1, 0, 0, di::kStick);
        const Step s = w.Next();
        CHECK(!w.c.Open() && (s.take[1] & di::kStick) == 0);
        World v;
        v.Hand(0, 0, 0.9f, 0);
        v.Hand(1, 0, 0.9f, 0);
        v.Run(10);
        v.Hand(1, 0, 0.9f, di::kStick);
        const Step t = v.Next();
        CHECK(!v.c.Open() && (t.take[1] & di::kStick) == 0);
    }
    {   // no host (or the chord off): nothing of it
        World w(1, true);
        w.host.chordOn = false;
        w.Chord();
        CHECK(!w.c.Open());
        World n(1, false);
        n.Chord();
        CHECK(!n.c.Open());
        World u;
        u.useHost = false;
        u.Chord();
        CHECK(!u.c.Open());
    }
    {   // a stick already down when the grips arm is not the chord's click, and stays taken until let go
        World w;
        w.Hand(1, 0, 0, di::kStick);
        w.Next();
        w.Hand(0, 0, 0.9f, di::kStick);
        w.Hand(1, 0, 0.9f, di::kStick);
        w.Run(50);
        CHECK(!w.c.Open());
        const Step s = w.Next();
        CHECK((s.take[1] & di::kStick) != 0);
        w.Hand(0, 0, 0.9f, 0);
        w.Hand(1, 0, 0.9f, 0);
        w.Next();
        w.Hand(1, 0, 0.9f, di::kStick);
        w.Next();
        CHECK(w.c.Open());
    }
    {   // not in F4, with no head, or in a menu: no chord
        World w;
        w.f4 = false;
        w.Chord();
        CHECK(!w.c.Open());
        World n;
        n.f.head.valid = 0;
        n.Chord();
        CHECK(!n.c.Open());
        World m;
        m.f.flags |= di::kInMenu;
        m.Chord();
        CHECK(!m.c.Open());
    }
    {   // the skill wheel's own chord (the other grip + a click) is as it was, and does not make this one
        World w(0);
        w.Hand(0, 0, 0.9f, 0);
        w.Next();
        w.Hand(1, 0, 0, di::kStick);
        w.Next();
        CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Skills);
    }
}

static void TestPagesOnWindow() {
    {   // the view buttons: F2 asks for the view and closes the window; the trigger that pressed stays the window's until let go
        World w;
        w.LongPress();
        w.ClickTab(1);
        w.AimRect(hset::kViewBtn(1));
        w.Hand(1, 0, 0, 0);
        w.Next();
        CHECK(w.c.ViewNow().settingsHover.kind == hset::Kind::View && w.c.ViewNow().settingsHover.index == 2);
        w.Hand(1, 0.9f, 0, 0);
        const Step s = w.Next();
        CHECK(s.settings.what == hset::Do::View && s.settings.view == 2 && !w.c.Open());
        CHECK((s.take[1] & di::kTakeTrigger) != 0);   // still taken: the click that closed it is not an attack
        w.Run(3);
        CHECK(!w.c.Open());
        w.Hand(1, 0, 0, 0);
        const Step after = w.Next();
        (void)after;
        w.Run(3);
        // the lit one is F4: asking for F4 is nothing
        w.LongPress();
        CHECK(w.c.Open());
        CHECK(w.c.ViewNow().tab == Tab::Settings);
        CHECK(w.c.SettingsModel().View() == 4);
        const Step f4 = w.ClickSheet(hset::kViewBtn(3));
        CHECK(f4.settings.what == hset::Do::Nothing && w.c.Open());
    }
    {   // the render size: a preset is a draft, Apply hands a request out, the store's answer clears it
        World w;
        w.LongPress();
        w.ClickTab(1);
        w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageVideo)));
        CHECK(w.c.SettingsModel().page == hset::kPageVideo);
        w.ClickSheet(hset::kPreset(4));   // 2560
        CHECK(w.c.SettingsModel().Dirty() && w.c.SettingsModel().DraftPick() == hset::video::Square(2560));
        CHECK(w.c.ViewNow().settings.DraftPick() == hset::video::Square(2560));   // the picture's copy follows
        const Step s = w.ClickSheet(hset::kApply);
        CHECK(s.settings.what == hset::Do::Save && s.settings.request.changed == (hset::Bit(hset::kKeyMode) | hset::Bit(hset::kKeyPx)));
        CHECK(s.settings.request.values[hset::kKeyPx] == 2560.0f && s.settings.request.base[hset::kKeyPx] == 2048.0f);
        CHECK(s.feedback == Feedback::Applied && w.c.Open());
        // the answer comes with the ini's snapshot
        w.host.ini.values[hset::kKeyPx] = w.host.ini.file[hset::kKeyPx] = 2560.0f;
        w.host.ini.sequence = s.settings.request.sequence;
        w.host.ini.result = hset::Result::Saved;
        w.Run(2);
        CHECK(!w.c.SettingsModel().Dirty() && w.c.SettingsModel().result == hset::Result::Saved);
        // the picture's sizes and the PC window's changes are followed
        w.host.env.now = {2560, 2560};
        w.host.env.recommended = {2496, 2688};
        w.Run(2);
        CHECK(w.c.ViewNow().settings.Env().recommended.h == 2688);
        w.host.ini.sequence = 0;
        w.host.ini.values[hset::kKeyPx] = w.host.ini.file[hset::kKeyPx] = 3072.0f;
        w.Run(2);
        CHECK(w.c.SettingsModel().DraftPick() == hset::video::Square(3072));
        // a save that could not be handed over
        w.ClickSheet(hset::kPreset(1));
        const Step bad = w.ClickSheet(hset::kApply);
        CHECK(bad.settings.what == hset::Do::Save);
        w.c.SettingsSubmitFailed(bad.settings.request.sequence);
        CHECK(w.c.SettingsModel().result == hset::Result::Failed && w.c.SettingsModel().Dirty());
    }
    {   // the slider: the thumb follows the beam while the trigger is held, also off the sheet; let go, it stays
        World w;
        w.LongPress();
        w.ClickTab(1);
        w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageVideo)));
        const float mid = hset::kSlTrack.y + 0.5f * hset::kSlTrack.h;
        w.AimSheet((float)hset::kSlX0, mid);
        w.Hand(1, 0, 0, 0);
        w.Next();
        w.Hand(1, 0.9f, 0, 0);
        w.Next();
        CHECK(w.c.SettingsModel().Get()[hset::kKeyPx] == 1024.0f);
        w.AimSheet((float)hset::kSlX1, mid);
        w.Next();
        CHECK(w.c.SettingsModel().Get()[hset::kKeyPx] == 4096.0f);
        w.AimSheet(1200.0f, 40.0f);   // past the sheet's edge, above the track, but on the window
        w.Next();
        CHECK(w.c.SettingsModel().Get()[hset::kKeyPx] == 4096.0f);
        w.AimSheet(0.5f * hset::kW, mid);
        w.Next();
        CHECK(w.c.SettingsModel().Get()[hset::kKeyPx] == 2560.0f);
        w.Hand(1, 0, 0, 0);
        w.Next();
        w.AimSheet((float)hset::kSlX1, mid);
        w.Run(5);
        CHECK(w.c.SettingsModel().Get()[hset::kKeyPx] == 2560.0f && w.c.Open());
        // a stepper at the slider's end repeats while the trigger is held
        w.AimRect(hset::kSlInc);
        w.Hand(1, 0.9f, 0, 0);
        w.Next();
        const float first = w.c.SettingsModel().Get()[hset::kKeyPx];
        w.Run(80);
        CHECK(w.c.SettingsModel().Get()[hset::kKeyPx] > first);
        w.Hand(1, 0, 0, 0);
        w.Next();
    }
    {   // Calibrate body hands the window to the measurement and brings it back on the Body page, the draft kept
        World w;
        w.LongPress();
        w.ClickTab(1);
        w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageVideo)));
        w.ClickSheet(hset::kPreset(1));
        CHECK(w.c.SettingsModel().Dirty());
        w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageBody)));
        w.AimRect(hset::kCalibrate);
        w.Hand(1, 0, 0, 0);
        w.Next();
        w.Hand(1, 0.9f, 0, 0);
        const Step s = w.Next();
        CHECK(s.settings.what == hset::Do::Calibrate && !w.c.Open());
        CHECK((s.take[1] & di::kTakeTrigger) != 0);
        // the plugin's measurement runs: the window cannot be had, the chord and the long press are nothing
        w.host.calibrating = true;
        w.Hand(1, 0, 0, 0);
        w.Run(20);
        CHECK(!w.c.Open());
        w.LongPress();
        CHECK(!w.c.Open());
        w.Chord();
        CHECK(!w.c.Open());
        // over: the window is back, on the Body page, with the unapplied draft
        w.host.calibrating = false;
        w.Run(2);
        CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Settings && w.c.SettingsModel().page == hset::kPageBody);
        CHECK(w.c.SettingsModel().Dirty() && w.c.SettingsModel().DraftPick() == hset::video::Square(1024));
        // it does not come back twice
        w.LongPress();
        CHECK(!w.c.Open());
        w.Run(20);
        CHECK(!w.c.Open());
    }
    {   // Calibrate body, then the view leaves F4: it does not come back (and the next opening is fresh)
        World w;
        w.LongPress();
        w.ClickTab(1);
        w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageBody)));
        w.ClickSheet(hset::kCalibrate);
        w.host.calibrating = true;
        w.Run(5);
        w.f4 = false;
        w.Run(3);
        w.f4 = true;
        w.host.calibrating = false;
        w.Run(10);
        CHECK(!w.c.Open());
    }
    {   // a measurement started from the PC window closes the window, and it does not come back on its own
        World w;
        w.LongPress();
        w.ClickTab(1);
        w.host.calibrating = true;
        const Step s = w.Next();
        CHECK(!w.c.Open() && s.feedback == Feedback::Closed);
        w.host.calibrating = false;
        w.Run(20);
        CHECK(!w.c.Open());
    }
    {   // Recentre view and Close from the pages
        World w;
        w.LongPress();
        w.ClickTab(1);
        const Step r = w.ClickSheet(hset::kRecentre);
        CHECK(r.settings.what == hset::Do::Recentre && w.c.Open());
        const Step c = w.ClickSheet(hset::kClose);
        CHECK(!w.c.Open() && c.feedback == Feedback::Closed);
        // the Close button of the pages is not the Skills tab's: the tiles' own CLOSE stays there
        w.Run(5);
        w.LongPress();
        w.ClickTab(0);
        const tiles::PRect cb = tiles::ButtonRect(Page::Wheel, 1);
        w.ClickPx(CentreX(cb), CentreY(cb));
        CHECK(!w.c.Open());
    }
    {   // while up, on either tab, both hands' controls are the window's
        World w;
        w.LongPress();
        for (int tab = 0; tab < 2; ++tab) {
            if (tab) w.ClickTab(1);
            const Step s = w.Next();
            for (int h = 0; h < 2; ++h)
                CHECK((s.take[h] & (di::kTakeTrigger | di::kTakeSqueeze | di::kPrimary | di::kSecondary | di::kMenuButton | di::kStick)) ==
                      (di::kTakeTrigger | di::kTakeSqueeze | di::kPrimary | di::kSecondary | di::kMenuButton | di::kStick));
            CHECK(s.layer.show && s.layer.beam);
        }
    }
    {   // the Skills tab still does what it did: a tile picks a slot
        World w;
        w.LongPress();
        const tiles::PRect t = tiles::SlotRect(1);
        w.ClickPx(CentreX(t), CentreY(t));
        CHECK(!w.c.Open() && w.c.Selected() == 1);
    }
}

// -- the window's pictures ---------------------------------------------------------------------------------------------------------------------

static void WriteBmp(const std::string& path, const std::vector<uint32_t>& px, int w, int h) {
    const uint32_t bytes = (uint32_t)(px.size() * 4);
    uint8_t hd[54] = {'B', 'M'};
    const uint32_t size = 54 + bytes, off = 54, dib = 40, ww = (uint32_t)w, hh = (uint32_t)(-h), planes = 1, bpp = 32;
    std::memcpy(hd + 2, &size, 4); std::memcpy(hd + 10, &off, 4); std::memcpy(hd + 14, &dib, 4); std::memcpy(hd + 18, &ww, 4); std::memcpy(hd + 22, &hh, 4);
    std::memcpy(hd + 26, &planes, 2); std::memcpy(hd + 28, &bpp, 2); std::memcpy(hd + 34, &bytes, 4);
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)hd, 54);
    for (uint32_t c : px) {   // premultiplied, R in the low byte: over black it is what the headset shows
        const uint32_t out = ((c >> 16) & 0xFF) | (((c >> 8) & 0xFF) << 8) | ((c & 0xFF) << 16) | 0xFF000000u;
        f.write((const char*)&out, 4);
    }
}

static std::vector<uint32_t> Paint(const View& v) {
    std::vector<uint32_t> px;
    tilespaint::Render(v, px);
    return px;
}

// The Belt page on the skill panel's Settings tab: a change is written by itself a moment later, through the same Step the Apply button uses.
static void TestLivePagesOnWindow() {
    {   // the first-start guide asks for the window: it comes up on Settings > Guide (once the ini is read and the head is there), pages with the laser, ends with Done
        World w;
        w.host.openGuide = 1;
        w.host.ini.ready = false;
        Step s = w.Next();
        CHECK(!w.c.Open() && !s.guideOpened && s.wantStore);   // (the ini's thread is asked for; the window waits for its reading)
        w.host.ini.ready = true;
        s = w.Next();
        CHECK(w.c.Open() && s.guideOpened && w.c.ViewNow().tab == Tab::Settings && w.c.SettingsModel().page == hset::kPageGuide && w.c.SettingsModel().guide == 1);
        CHECK(w.c.FlipPlace() == 1 + hset::kPageGuide && w.c.ViewNow().flipPlace == 1 + hset::kPageGuide);
        w.host.openGuide = -1;   // (the plugin took the request)
        w.Run(20);
        CHECK(w.c.Open());
        // Next with the laser, Back, to the last page, Done
        w.ClickSheet(hset::kGuideNext);
        CHECK(w.c.SettingsModel().guide == 2 && w.c.Open());
        w.ClickSheet(hset::kGuideBack);
        CHECK(w.c.SettingsModel().guide == 1);
        for (int p = 2; p <= firststart::kGuidePages; ++p) w.ClickSheet(hset::kGuideNext);
        CHECK(w.c.SettingsModel().guide == firststart::kGuidePages && w.c.Open());
        Step done;
        bool got = false, ended = false;
        w.AimRect(hset::kGuideNext);
        w.Hand(1, 0, 0, 0);
        w.Next();
        w.Hand(1, 0.9f, 0, 0);
        for (int i = 0; i < 6; ++i) {
            const Step t = w.Next();
            if (t.settings.what == hset::Do::GuideDone) { got = true; done = t; }
            if (t.guideEnd == 1) ended = true;
        }
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 4 && !ended; ++i) if (w.Next().guideEnd == 1) ended = true;
        CHECK(got && !w.c.Open() && ended);   // Done closes the window, and says the guide is finished
    }
    {   // the window closed with the guide on show (B): the guide is over, skipped; an ordinary window says nothing
        World w;
        w.host.openGuide = 1;
        w.Next();
        w.host.openGuide = -1;
        w.Run(10);
        CHECK(w.c.Open() && w.c.SettingsModel().GuideShown());
        w.Hand(1, 0, 0, di::kSecondary);   // B
        const Step s = w.Next();
        CHECK(!w.c.Open() && s.guideEnd == 2);
        World q;
        q.LongPress();
        q.ClickTab(1);
        q.LongPress();
        CHECK(!q.c.Open());
        int said = 0;
        for (int i = 0; i < 5; ++i) said += q.Next().guideEnd;
        CHECK(said == 0);
    }
    {   // it does not break into a window that is up, nor wait for a window that cannot come (a measurement, another view)
        World w;
        w.LongPress();
        w.host.openGuide = 1;
        w.Run(20);
        CHECK(w.c.Open() && w.c.SettingsModel().page != hset::kPageGuide);   // already up on something else: the request waits
        World m;
        m.host.openGuide = 1;
        m.host.calibrating = true;
        m.Run(30);
        CHECK(!m.c.Open());
        m.host.calibrating = false;
        m.Run(3);
        CHECK(m.c.Open() && m.c.SettingsModel().page == hset::kPageGuide);
        World v;
        v.host.openGuide = 1;
        v.f4 = false;
        v.Run(30);
        CHECK(!v.c.Open());
    }
    {   // the offer: Measure now hides the window for the measurement and it comes back on the guide's first page, not as an end
        World w;
        w.host.openGuide = firststart::kOfferPage;
        w.Next();
        w.host.openGuide = -1;
        w.Run(10);
        CHECK(w.c.Open() && w.c.SettingsModel().guide == firststart::kOfferPage);
        bool calibrate = false, ended = false;
        w.AimRect(hset::kGuideNext);
        w.Hand(1, 0, 0, 0);
        w.Next();
        w.Hand(1, 0.9f, 0, 0);
        for (int i = 0; i < 6; ++i) {
            const Step t = w.Next();
            if (t.settings.what == hset::Do::Calibrate) {
                calibrate = true;
                w.host.calibrating = true;   // the plugin's measurement runs from the next frame
            }
            if (t.guideEnd) ended = true;
        }
        w.Hand(1, 0, 0, 0);
        for (int i = 0; i < 6; ++i) if (w.Next().guideEnd) ended = true;
        CHECK(calibrate && !ended && !w.c.Open());
        w.Run(60);
        CHECK(!w.c.Open());
        w.host.calibrating = false;
        w.Run(3);
        CHECK(w.c.Open() && w.c.SettingsModel().page == hset::kPageGuide && w.c.SettingsModel().guide == 1);
    }
    {   // the belt: one plus, and after the settle time the write comes out of Next() with only that key
        World w;
        w.LongPress();
        w.Run(2);
        w.ClickTab(1);
        w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageBelt)));
        CHECK(w.c.SettingsModel().page == hset::kPageBelt);
        const int up = hset::KeyAt(hset::kPageBelt, 0);
        const Step s = w.ClickSheet(hset::kInc(0));
        CHECK(w.c.SettingsModel().Dirty() && w.c.SettingsModel().LiveOnly() && s.settings.what == hset::Do::Nothing);
        Step saved;
        bool got = false;
        for (int i = 0; i < 60 && !got; ++i) {
            const Step t = w.Next();
            if (t.settings.what == hset::Do::Save) { saved = t; got = true; }
        }
        CHECK(got && saved.settings.request.changed == hset::Bit(up));
        CHECK(w.c.Open() && w.c.ViewNow().tab == Tab::Settings);
        // the answer: clean, and nothing more to write
        w.host.ini.values[up] = w.host.ini.file[up] = saved.settings.request.values[up];
        w.host.ini.sequence = saved.settings.request.sequence;
        w.host.ini.result = hset::Result::Saved;
        bool more = false;
        for (int i = 0; i < 60; ++i) if (w.Next().settings.what == hset::Do::Save) more = true;
        CHECK(!more && !w.c.SettingsModel().Dirty());
    }
    {   // the Melee page's seventh row, quick blow (Serg, 2026-11-05, 0.204; Direct hit by the rules until then): its tick where the closer rows draw it,
        // on as shipped, unticked and written by itself, live
        World w;
        w.LongPress();
        w.Run(2);
        w.ClickTab(1);
        w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageMelee)));
        const int qb = hset::KeyAt(hset::kPageMelee, 6);
        CHECK(w.c.SettingsModel().page == hset::kPageMelee && qb >= 0 && !wcscmp(hset::kDefs[qb].key, L"quick_blow") && hset::kDefs[hset::kKeyMeleeDirect].page == hset::kNoPage);
        CHECK(w.c.SettingsModel().Get()[qb] > 0.5f);
        w.ClickSheet(hset::kToggle(6, hset::kPageMelee));
        CHECK(w.c.SettingsModel().Get()[qb] < 0.5f && w.c.SettingsModel().LiveOnly());
        Step saved;
        bool got = false;
        for (int i = 0; i < 60 && !got; ++i) {
            const Step t = w.Next();
            if (t.settings.what == hset::Do::Save) { saved = t; got = true; }
        }
        CHECK(got && saved.settings.request.changed == hset::Bit(qb) && saved.settings.request.values[qb] == 0.0f);
    }
    {   // the key bindings: a pick waits for Apply (they are not live), then only that key goes, with the line the panel read as its base
        World w;
        w.host.ini.bind[0] = "action:swap";
        w.LongPress();
        w.Run(2);
        w.ClickTab(1);
        w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageKeysL)));
        CHECK(w.c.SettingsModel().page == hset::kPageKeysL);
        const int lt = hset::KeyAt(hset::kPageKeysL, 0);
        w.ClickSheet(hset::kInc(0, hset::kPageKeysL));
        CHECK(w.c.SettingsModel().Dirty() && !w.c.SettingsModel().LiveOnly());
        bool early = false;
        for (int i = 0; i < 60; ++i) if (w.Next().settings.what == hset::Do::Save) early = true;
        CHECK(!early);   // (a live key would have gone by now)
        const Step s = w.ClickSheet(hset::kApply);
        CHECK(s.settings.what == hset::Do::Save && s.settings.request.changed == hset::Bit(lt) && s.settings.request.bind[0] == "action:swap");
    }
}

// [openxr] skill_panel_game_look: the game's sprite sheets read as the game has them, and the window in the game's art.
static std::string FakeSheet(int frames, int frameWidth, int gutter, int height, bool lastGutter) {
    const int width = frames * frameWidth + (frames - 1) * gutter + (lastGutter ? gutter : 0);
    std::string d(40 + (size_t)width * height * 4, '\0');
    std::memcpy(&d[0], "SpA1", 4);
    const uint16_t version = 31, fw = (uint16_t)frameWidth;
    const uint32_t w = (uint32_t)width, h = (uint32_t)height, n = (uint32_t)frames, size = w * h * 4, comps = 4;
    std::memcpy(&d[4], &version, 2);
    std::memcpy(&d[6], &fw, 2);
    std::memcpy(&d[8], &w, 4);
    std::memcpy(&d[12], &h, 4);
    std::memcpy(&d[20], &n, 4);
    std::memcpy(&d[32], &size, 4);
    std::memcpy(&d[36], &comps, 4);
    for (int f = 0; f < frames; ++f)   // each frame's first pixel says which frame it is
        d[40 + (size_t)(f * (frameWidth + gutter)) * 4] = (char)(10 + f);
    return d;
}

static void TestSpriteSheets() {
    gameui::Sheet s;
    // the game's own shapes: frames padded alike (StashTabs 4 x 269 in 1079), the last gutter left out (LeftArrow 4 x 54 in 219), one frame
    CHECK(gameui::Decode(FakeSheet(4, 54, 1, 6, false), &s) && s.frames == 4 && s.frameWidth == 54 && s.stride == 55 && s.At(3, 0, 0)[0] == 13);
    CHECK(gameui::Decode(FakeSheet(3, 20, 2, 5, true), &s) && s.stride == 22 && s.At(2, 0, 0)[0] == 12);
    CHECK(gameui::Decode(FakeSheet(1, 30, 0, 4, false), &s) && s.frames == 1 && s.Ok());
    // what is not a sheet as the game's are: refused, with a reason
    const char* why = nullptr;
    std::string bad = FakeSheet(2, 10, 0, 3, false);
    bad[0] = 'X';
    CHECK(!gameui::Decode(bad, &s, &why) && why);
    bad = FakeSheet(2, 10, 0, 3, false);
    bad[4] = 61;   // another pixel format
    CHECK(!gameui::Decode(bad, &s));
    bad = FakeSheet(2, 10, 0, 3, false);
    bad.pop_back();   // short
    CHECK(!gameui::Decode(bad, &s));
    CHECK(!gameui::Decode(std::string("SpA1"), &s));
    CHECK(!gameui::Decode(FakeSheet(2, 10, 20, 3, false), &s));   // a gutter wider than any of the game's
}

// The game look's Settings tab has its flip arrows at the two ends of the page tabs' row (Serg, 2026-10-30): beside the row, on neither tab,
// inside the window's gold rim; hit there on the Settings tab only - the Skills tab keeps them half-way down, and so does our own look.
static void TestFlipHigh() {
    const float y0 = tiles::kSheetY + hset::kTab(0).y * tiles::kSheetScale, y1 = y0 + hset::kTab(0).h * tiles::kSheetScale;
    const float firstL = tiles::kSheetX + hset::kTab(0).x * tiles::kSheetScale;
    const hset::Rect last = hset::kTab(hset::kTabCount - 1);
    const float lastR = tiles::kSheetX + (last.x + last.w) * tiles::kSheetScale;
    const tiles::PRect l = tiles::FlipRect(0, true), r = tiles::FlipRect(1, true);
    CHECK(l.r <= firstL && r.l >= lastR);                                   // beside the row, on no tab
    CHECK(l.t <= y0 + 4.0f && l.b >= y1 - 4.0f && r.t == l.t && r.b == l.b);   // as tall as the row
    CHECK(l.l >= 30.0f + 30.0f && r.r <= tiles::kW - 60.0f);               // inside the gold rim (30 in, 48 wide: its inner line)
    for (int i = 0; i < 2; ++i) {
        const tiles::PRect hi = tiles::FlipRect(i, true), lo = tiles::FlipRect(i);
        const float hx = 0.5f * (hi.l + hi.r) / tiles::kW, hy = 0.5f * (hi.t + hi.b) / tiles::kH;
        const float lx = 0.5f * (lo.l + lo.r) / tiles::kW, ly = 0.5f * (lo.t + lo.b) / tiles::kH;
        CHECK(tiles::HitTest(Page::Wheel, hx, hy, kNone, 0, true, true, false, false, true) == tiles::FlipCell(i));    // Settings tab, game look: up there
        CHECK(tiles::HitTest(Page::Wheel, lx, ly, kNone, 0, true, true, false, false, true) == kNone);                 // ... and no longer half-way down
        CHECK(tiles::HitTest(Page::Wheel, lx, ly, kNone, 0, true, false, false, false, true) == tiles::FlipCell(i));   // the Skills tab keeps its place
        CHECK(tiles::HitTest(Page::Wheel, lx, ly, kNone, 0, true, true) == tiles::FlipCell(i));                        // our own look keeps its place
        CHECK(tiles::HitTest(Page::Wheel, hx, hy, kNone, 0, true, true) == kNone);
    }
}

static void TestGameLook(const char* dumpDir) {
    World w;
    w.LongPress();
    const View skills = w.c.ViewNow();
    w.ClickTab(1);
    const View settings = w.c.ViewNow();
    gameui::SetOn(false);
    const std::vector<uint32_t> ourSkills = Paint(skills), ourSettings = Paint(settings);
    // the key on but no art beside the plugin: the window as before, to the pixel
    gameui::SetDir(L"C:\\no such folder\\d2r_vr_game_ui");
    gameui::SetOn(true);
    CHECK(!gameui::Ready());
    CHECK(Paint(skills) == ourSkills && Paint(settings) == ourSettings);
    gameui::SetOn(false);
    // with the art (GAME_UI_CACHE: a folder D2R_VR_Settings made from the tester's own game - not in the repository): every picture is the game's
    char dir[400] = {};
    if (!GetEnvironmentVariableA("GAME_UI_CACHE", dir, sizeof dir)) {
        std::printf("  game look: GAME_UI_CACHE not set, only the no-art case\n");
        return;
    }
    wchar_t wide[400] = {};
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, wide, 399);
    gameui::SetDir(wide);
    gameui::SetOn(true);
    CHECK(gameui::Ready());
    CHECK(gameui::Get(gameui::Sprite::Border).frames == 9 && gameui::Get(gameui::Sprite::Checkbox).frames == 7 && gameui::Get(gameui::Sprite::Button).frames == 4);
    const std::vector<uint32_t> gameSkills = Paint(skills);
    CHECK(gameSkills != ourSkills);
    if (dumpDir) WriteBmp(std::string(dumpDir) + "/game_skills.bmp", gameSkills, tiles::kW, tiles::kH);
    w.host.party = SampleRoster();
    std::vector<uint32_t> pages[hset::kMaxPages];
    for (int p = 0; p < hset::PageCount(); ++p) {
        w.ClickPage(p);
        View high = w.c.ViewNow();
        high.flipHigh = true;   // (the game look's layout: the flip arrows at the page tabs' row)
        pages[p] = Paint(high);
        for (int q = 0; q < p; ++q) CHECK(pages[p] != pages[q]);
        if (dumpDir) WriteBmp(std::string(dumpDir) + "/game_settings_page" + std::to_string(p) + ".bmp", pages[p], tiles::kW, tiles::kH);
    }
    gameui::SetOn(false);
    CHECK(Paint(skills) == ourSkills);   // unticked: ours again
    std::printf("  game look: %d settings pages and the skills in the game's art\n", hset::PageCount());
}

static void TestPaint(const char* dumpDir) {
    World w;
    w.host.env.now = {2048, 2048};
    w.host.env.recommended = {2496, 2688};
    w.host.env.maximum = {4096, 4096};
    w.LongPress();
    const std::vector<uint32_t> skills = Paint(w.c.ViewNow());
    CHECK(skills.size() == (size_t)tiles::kW * tiles::kH);
    // the strip is painted: the same window without the key differs in the title plate only
    World n(1, false);
    n.LongPress();
    const std::vector<uint32_t> plain = Paint(n.c.ViewNow());
    CHECK(skills != plain);
    bool belowSame = true;
    for (int y = (int)tiles::kPanel.t + 14; y < tiles::kH && belowSame; ++y)
        for (int x = 0; x < tiles::kW; ++x) {
            const bool hint = (y > 1020 && x > 480 && x < 1120) ||                                                                    // the dots at the foot
                              (y >= 470 && y <= 690 && (x < 110 || x > 1490));                                                       // the arrows at the edges
            if (!hint && skills[(size_t)y * tiles::kW + x] != plain[(size_t)y * tiles::kW + x]) { belowSame = false; break; }
        }
    CHECK(belowSame);   // (the tiles, the status, the buttons: as they were; the flip hints are the only addition)
    bool plateDiffers = false;
    for (int y = 40; y < 104 && !plateDiffers; ++y)
        for (int x = 400; x < 1200; ++x)
            if (skills[(size_t)y * tiles::kW + x] != plain[(size_t)y * tiles::kW + x]) { plateDiffers = true; break; }
    CHECK(plateDiffers);

    // the Settings tab: a picture per page, each its own, none the Skills tab's
    w.ClickTab(1);
    w.host.party = SampleRoster();   // the Party page shows a game's roster
    std::vector<uint32_t> pages[hset::kMaxPages];
    for (int p = 0; p < hset::PageCount(); ++p) {
        w.ClickPage(p);
        CHECK(w.c.SettingsModel().page == p);
        pages[p] = Paint(w.c.ViewNow());
        CHECK(pages[p] != skills);
        for (int q = 0; q < p; ++q) CHECK(pages[p] != pages[q]);
        if (dumpDir) WriteBmp(std::string(dumpDir) + "/tabs_settings_page" + std::to_string(p) + ".bmp", pages[p], tiles::kW, tiles::kH);
    }
    // the guide's pages: each is its own picture (the offer, the first page, the densest one), and Next shows the hovered button
    {
        World g;
        g.host.openGuide = firststart::kOfferPage;
        g.Next();
        g.host.openGuide = -1;
        g.Run(5);
        std::vector<uint32_t> pics[firststart::kPageCount];
        for (int sub = 0; sub < firststart::kPageCount; ++sub) {
            pics[sub] = Paint(g.c.ViewNow());
            for (int q = 0; q < sub; ++q) CHECK(pics[sub] != pics[q]);
            if (dumpDir) WriteBmp(std::string(dumpDir) + "/tabs_guide_" + std::to_string(sub) + ".bmp", pics[sub], tiles::kW, tiles::kH);
            g.ClickSheet(sub == 0 ? hset::kGuideBack : hset::kGuideNext);   // (the offer's Back is Not now: Measure now would hide the window)
        }
    }
    // states: a hovered button, an edited preset, a measured body
    w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageVideo)));
    const std::vector<uint32_t> before = Paint(w.c.ViewNow());
    w.ClickSheet(hset::kPreset(4));
    const std::vector<uint32_t> edited = Paint(w.c.ViewNow());
    CHECK(edited != before);
    w.AimRect(hset::kPreset(2));
    w.Hand(1, 0, 0, 0);
    w.Next();
    const std::vector<uint32_t> hovered = Paint(w.c.ViewNow());
    CHECK(hovered != edited);
    if (dumpDir) {
        WriteBmp(std::string(dumpDir) + "/tabs_skills.bmp", skills, tiles::kW, tiles::kH);
        WriteBmp(std::string(dumpDir) + "/tabs_video_edited.bmp", hovered, tiles::kW, tiles::kH);
    }
    w.host.ini.values[hset::kKeyUse] = w.host.ini.file[hset::kKeyUse] = 1.0f;
    w.host.ini.values[hset::kKeyVersion] = w.host.ini.file[hset::kKeyVersion] = 1.0f;
    w.host.ini.values[hset::kKeyEye] = w.host.ini.file[hset::kKeyEye] = 1.62f;
    w.host.ini.values[hset::kKeySpan] = w.host.ini.file[hset::kKeySpan] = 1.71f;
    w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageBody)));
    w.Run(2);
    const std::vector<uint32_t> measured = Paint(w.c.ViewNow());
    CHECK(measured != pages[hset::kPageBody]);
    if (dumpDir) WriteBmp(std::string(dumpDir) + "/tabs_body_measured.bmp", measured, tiles::kW, tiles::kH);
    // the view page with the lit F4, and a hovered tab
    w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageView)));
    w.AimPx(CentreX(tiles::TabRect(0)), CentreY(tiles::TabRect(0)));
    w.Hand(1, 0, 0, 0);
    w.Next();
    const std::vector<uint32_t> hoveredTab = Paint(w.c.ViewNow());
    CHECK(hoveredTab != pages[hset::kPageView]);
    if (dumpDir) WriteBmp(std::string(dumpDir) + "/tabs_settings_view_tabhover.bmp", hoveredTab, tiles::kW, tiles::kH);
    // the painter puts a button where the hit test says: a button lit under the beam changes the picture around its own rectangle, and only there
    {
        World a;
        a.LongPress();
        a.ClickTab(1);
        a.ClickSheet(hset::kTab(hset::TabOf(hset::kPageVideo)));
        View unlit = a.c.ViewNow();
        unlit.settingsHover = {};
        for (const hset::Rect& r : {hset::kPreset(3), hset::kPreset(6), hset::kClose, hset::kRecentre}) {
            View lit = unlit;
            const float cx = WinX(r.x + 0.5f * r.w), cy = WinY(r.y + 0.5f * r.h);
            float su, sv;
            tiles::SheetPoint(cx / tiles::kW, cy / tiles::kH, &su, &sv);
            lit.settingsHover = hset::HitTest(hset::kPageVideo, su, sv);
            CHECK(lit.settingsHover.kind != hset::Kind::None);
            const std::vector<uint32_t> p0 = Paint(unlit), p1 = Paint(lit);
            int x0 = tiles::kW, y0 = tiles::kH, x1 = -1, y1 = -1;
            for (int y = 0; y < tiles::kH; ++y)
                for (int x = 0; x < tiles::kW; ++x)
                    if (p0[(size_t)y * tiles::kW + x] != p1[(size_t)y * tiles::kW + x]) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
            const float l = WinX((float)r.x), t = WinY((float)r.y), rr = WinX((float)(r.x + r.w)), b = WinY((float)(r.y + r.h));
            // the lit button's glow reaches 6 px past its rectangle: the changed pixels are that rectangle, grown by about that, on every side
            CHECK(x1 >= 0 && x0 >= l - 8.0f && x1 <= rr + 8.0f && y0 >= t - 8.0f && y1 <= b + 8.0f);
            CHECK(x0 <= l - 2.0f && x1 >= rr + 2.0f && y0 <= t - 2.0f && y1 >= b + 2.0f);
        }
    }
    // the editor's pages keep their titles (no strip): the picture of the editor is the one without the key
    w.ClickTab(0);
    const tiles::PRect eb = tiles::ButtonRect(Page::Wheel, 0);
    w.ClickPx(CentreX(eb), CentreY(eb));
    CHECK(w.c.ViewNow().page == Page::PickSlot);
    World e(1, false);
    e.LongPress();
    e.ClickPx(CentreX(eb), CentreY(eb));
    CHECK(Paint(w.c.ViewNow()) == Paint(e.c.ViewNow()));
}

// -- the scene the pages are drawn from ----------------------------------------------------------------------------------------------------

static void TestScene() {
    for (int page = 0; page < hset::PageCount(); ++page) {
        hset::Model m;
        hset::Snapshot sn;
        sn.ready = true;
        m.Open(sn);
        m.Click({hset::Kind::Tab, page, 0});
        const hset::Scene full = hset::Compose(m, {});
        const hset::Scene bare = hset::Compose(m, {}, hset::ComposeOptions{false, true, nullptr});
        CHECK(!full.empty() && full.size() == bare.size() + 2);   // the title and its caption
        CHECK(full.front().type == hset::Item::Type::Fill && full.front().r.w == hset::kW && full.front().r.h == hset::kH);
        // everything drawn is on the sheet, and every button is a hit
        for (const hset::Item& it : bare) {
            CHECK(it.r.x >= 0 && it.r.y >= 0 && it.r.x + it.r.w <= hset::kW && it.r.y + it.r.h <= hset::kH);
            if (it.type == hset::Item::Type::Button) {
                CHECK(!it.text.empty());
                CHECK(hset::HitTest(page, (it.r.x + 0.5f * it.r.w) / hset::kW, (it.r.y + 0.5f * it.r.h) / hset::kH).kind != hset::Kind::None);
            }
        }
        // the page tabs, Recentre, Close and Apply are on every page
        int tabs = 0, bottom = 0;
        for (const hset::Item& it : bare) {
            if (it.type != hset::Item::Type::Button) continue;
            for (int i = 0; i < hset::kTabCount; ++i) if (it.text == hset::kTabNames[i] && it.r.y == hset::kTab(i).y) ++tabs;   // (in the strip: a toggle's face can be "Left")
            if (it.text == "Recentre view" || it.text == "Close" || it.text == "Apply") ++bottom;
        }
        CHECK(tabs == hset::kTabCount && bottom == 3);
    }
    // the one chosen page tab is the selected one
    hset::Model m;
    hset::Snapshot sn;
    sn.ready = true;
    m.Open(sn);
    m.Click({hset::Kind::Tab, hset::kPageVideo, 0});
    int selected = 0;
    for (const hset::Item& it : hset::Compose(m, {}))
        if (it.type == hset::Item::Type::Button && it.text == "Video" && it.selected) ++selected;
    CHECK(selected == 1);
    // the window's status line says how the window closes; the panel's says how that one does
    CHECK(std::string(hset::StatusText(m, true)).find("long press") != std::string::npos || m.page != hset::kPageView);
    hset::Model v;
    v.Open(sn);
    CHECK(std::string(hset::StatusText(v, true)).find("long press") != std::string::npos);
    CHECK(std::string(hset::StatusText(v, false)).find("grips") == std::string::npos);        // (two grips held mean nothing now: the panel closes by B or the long press)
    CHECK(std::string(hset::StatusText(v, false)).find("long press") != std::string::npos);
    // a hovered button is hot, a disabled one is not
    hset::Model nr;
    nr.Open(hset::Snapshot{});   // not ready: nothing can be changed
    nr.Click({hset::Kind::Tab, hset::kPageVideo, 0});
    bool hotDisabled = false;
    for (const hset::Item& it : hset::Compose(nr, {hset::Kind::Preset, 3, 0}))
        if (it.type == hset::Item::Type::Button && it.text == "2048" && it.hot) hotDisabled = true;
    CHECK(!hotDisabled);
    bool hot = false;
    for (const hset::Item& it : hset::Compose(m, {hset::Kind::Preset, 3, 0}))
        if (it.type == hset::Item::Type::Button && it.text == "2048" && it.hot) hot = true;
    CHECK(hot);
}

// -- flipping: a stick's flick, a grip's tap, the arrows and the dots -------------------------------------------------------------------------

struct Pads {   // a frame's sticks and grips for the pure flipper
    float x[2] = {0, 0}, y[2] = {0, 0}, g[2] = {0, 0};
};

static void TestFlipper() {
    CHECK(kFlipPlaces == 20 && kFlipPlaces == 1 + hset::PageCount());
    CHECK(FlipTo(0, -1) == 0 && FlipTo(0, 1) == 1 && FlipTo(2, 1) == 3 && FlipTo(2, -1) == 1 && FlipTo(19, 1) == 19 && FlipTo(19, -1) == 18 && FlipTo(2, 0) == 2);
    auto step = [](Flipper& f, double t, const Pads& p) { return f.Step(t, p.x, p.y, p.g); };
    {   // a flick right is one step forward, left one back; the stick's coming back to the middle re-arms it
        Flipper f;
        Pads p;
        double t = 10.0;
        CHECK(step(f, t += 0.011, p) == 0);
        p.x[1] = 1.0f;
        CHECK(step(f, t += 0.011, p) == 1);
        for (int i = 0; i < 100; ++i) CHECK(step(f, t += 0.011, p) == 0);   // held over: one flick is one step
        p.x[1] = 0.0f;
        CHECK(step(f, t += 0.011, p) == 0);
        p.x[1] = -1.0f;
        CHECK(step(f, t += 0.011, p) == -1);
        p.x[1] = 0.0f;
        step(f, t += 0.011, p);
        // the left hand's stick works as well
        p.x[0] = 0.9f;
        CHECK(step(f, t += 0.011, p) == 1);
        p.x[0] = 0.0f;
        step(f, t += 0.011, p);
        p.x[0] = -0.9f;
        CHECK(step(f, t += 0.011, p) == -1);
    }
    {   // the dead zone and the re-arm: not far enough, not sideways enough, and not back in the middle yet
        Flipper f;
        Pads p;
        double t = 10.0;
        step(f, t += 0.011, p);
        p.x[1] = 0.5f;
        for (int i = 0; i < 10; ++i) CHECK(step(f, t += 0.011, p) == 0);   // inside the dead zone
        p.x[1] = 0.7f;
        p.y[1] = 0.6f;
        CHECK(step(f, t += 0.011, p) == 0);                                 // a diagonal push is no flick
        p.x[1] = 0.0f;
        p.y[1] = 1.0f;
        CHECK(step(f, t += 0.011, p) == 0);                                 // nor is up or down
        p.y[1] = 0.0f;
        step(f, t += 0.011, p);
        p.x[1] = 0.7f;
        p.y[1] = 0.4f;
        CHECK(step(f, t += 0.011, p) == 1);                                 // mostly sideways, past the dead zone: a flick
        p.x[1] = 0.4f;
        p.y[1] = 0.0f;
        CHECK(step(f, t += 0.011, p) == 0);
        p.x[1] = 1.0f;
        CHECK(step(f, t += 0.011, p) == 0);                                 // back only to 0.4: not re-armed
        p.x[1] = 0.2f;
        step(f, t += 0.011, p);
        p.x[1] = 1.0f;
        CHECK(step(f, t += 0.011, p) == 1);                                 // back in the middle: armed again
    }
    {   // a stick already pushed when the window opens is not a flick; it has to come back first
        Flipper f;
        Pads p;
        p.x[1] = 1.0f;
        double t = 10.0;
        CHECK(step(f, t += 0.011, p) == 0);
        for (int i = 0; i < 20; ++i) CHECK(step(f, t += 0.011, p) == 0);
        p.x[1] = 0.0f;
        step(f, t += 0.011, p);
        p.x[1] = 1.0f;
        CHECK(step(f, t += 0.011, p) == 1);
        // the same for one pushed up
        Flipper g;
        Pads q;
        q.y[0] = 1.0f;
        CHECK(step(g, t += 0.011, q) == 0);
        q.y[0] = 0.0f;
        q.x[0] = 1.0f;
        CHECK(step(g, t += 0.011, q) == 0);   // (it was pushed at the start: latched until it is back in the middle)
        q.x[0] = 0.0f;
        step(g, t += 0.011, q);
        q.x[0] = 1.0f;
        CHECK(step(g, t += 0.011, q) == 1);
    }
    {   // both sticks at once: one step; not-a-number values are nothing
        Flipper f;
        Pads p;
        double t = 10.0;
        step(f, t += 0.011, p);
        p.x[0] = -1.0f;
        p.x[1] = 1.0f;
        const int s = step(f, t += 0.011, p);
        CHECK(s == -1 || s == 1);
        CHECK(step(f, t += 0.011, p) == 0);
        Pads n;
        n.x[0] = NAN;
        n.y[1] = INFINITY;
        Flipper g;
        CHECK(step(g, t += 0.011, n) == 0 && step(g, t += 0.011, n) == 0);
    }
    {   // a tap of a grip: the right one forward, the left one back; a long squeeze is nothing
        Flipper f;
        Pads p;
        double t = 10.0;
        step(f, t += 0.011, p);
        p.g[1] = 0.9f;
        CHECK(step(f, t += 0.011, p) == 0);
        for (int i = 0; i < 8; ++i) step(f, t += 0.011, p);
        p.g[1] = 0.0f;
        CHECK(step(f, t += 0.011, p) == 1);
        p.g[0] = 0.9f;
        step(f, t += 0.011, p);
        for (int i = 0; i < 8; ++i) step(f, t += 0.011, p);
        p.g[0] = 0.0f;
        CHECK(step(f, t += 0.011, p) == -1);
        // held 0.5 s: a squeeze, not a tap
        p.g[1] = 0.9f;
        step(f, t += 0.011, p);
        for (int i = 0; i < 45; ++i) step(f, t += 0.011, p);
        p.g[1] = 0.0f;
        CHECK(step(f, t += 0.011, p) == 0);
        // the grip's hysteresis: 0.4 is still down, 0.1 is up
        p.g[1] = 0.9f;
        step(f, t += 0.011, p);
        p.g[1] = 0.4f;
        CHECK(step(f, t += 0.011, p) == 0);
        p.g[1] = 0.1f;
        CHECK(step(f, t += 0.011, p) == 1);
    }
    {   // both grips - the chord's - are never a page: together, or one after the other with the first still down
        Flipper f;
        Pads p;
        double t = 10.0;
        step(f, t += 0.011, p);
        p.g[0] = p.g[1] = 0.9f;
        for (int i = 0; i < 6; ++i) CHECK(step(f, t += 0.011, p) == 0);
        p.g[0] = p.g[1] = 0.0f;
        CHECK(step(f, t += 0.011, p) == 0);
        p.g[0] = 0.9f;
        step(f, t += 0.011, p);
        p.g[1] = 0.9f;   // the right one joins
        step(f, t += 0.011, p);
        p.g[0] = 0.0f;
        CHECK(step(f, t += 0.011, p) == 0);
        p.g[1] = 0.0f;
        CHECK(step(f, t += 0.011, p) == 0);
        // after that a single tap is a page again
        p.g[1] = 0.9f;
        step(f, t += 0.011, p);
        p.g[1] = 0.0f;
        CHECK(step(f, t += 0.011, p) == 1);
    }
    {   // a grip squeezed as the window opens (the chord's) is not a tap when it is let go
        Flipper f;
        Pads p;
        p.g[0] = p.g[1] = 0.9f;
        double t = 10.0;
        CHECK(step(f, t += 0.011, p) == 0);
        p.g[0] = 0.0f;
        CHECK(step(f, t += 0.011, p) == 0);
        p.g[1] = 0.0f;
        CHECK(step(f, t += 0.011, p) == 0);
    }
    {   // Reset starts again (the first frame is the quiet one)
        Flipper f;
        Pads p;
        double t = 10.0;
        step(f, t += 0.011, p);
        f.Reset();
        p.x[1] = 1.0f;
        CHECK(step(f, t += 0.011, p) == 0);
    }
}

static void TestFlipGeometry() {
    const int places = kFlipPlaces;
    // the arrows' zones: at the window's edges, clear of everything else; the dots: at its foot, apart from the buttons
    for (int i = 0; i < 2; ++i) {
        const tiles::PRect z = tiles::FlipRect(i);
        CHECK(InsideR(z, tiles::kPanel));
        CHECK(!Overlap(z, tiles::kTitlePlate));
        for (int k = 0; k < kSlotCount; ++k) CHECK(!Overlap(z, tiles::SlotRect(k)));
        CHECK(!Overlap(z, tiles::LtChipRect()) && !Overlap(z, tiles::LtArrowsRect()));
        for (int b = 0; b < tiles::ButtonCount(Page::Wheel); ++b) CHECK(!Overlap(z, tiles::ButtonRect(Page::Wheel, b)));
        for (int t = 0; t < tiles::kTabCount; ++t) CHECK(!Overlap(z, tiles::TabRect(t)));
        // the settings sheet: no button, text or fill of any page reaches the zone
        for (int page = 0; page < hset::PageCount(); ++page) {
            hset::Model m;
            hset::Snapshot sn;
            sn.ready = true;
            m.Open(sn);
            m.Click({hset::Kind::Tab, page, 0});
            for (const hset::Item& it : hset::Compose(m, {}, hset::ComposeOptions{false, true, nullptr})) {
                if (it.r.w >= hset::kW && it.r.h >= hset::kH) continue;   // (the sheet's own background)
                const tiles::PRect r{WinX((float)it.r.x), WinY((float)it.r.y), WinX((float)(it.r.x + it.r.w)), WinY((float)(it.r.y + it.r.h))};
                CHECK(!Overlap(z, r));
            }
        }
        const float cx = CentreX(z), cy = CentreY(z);
        CHECK(tiles::HitTest(Page::Wheel, cx / tiles::kW, cy / tiles::kH, kNone, 0, true) == tiles::FlipCell(i));
        CHECK(tiles::HitTest(Page::Wheel, cx / tiles::kW, cy / tiles::kH, kNone, 0, true, true) == tiles::FlipCell(i));   // on the Settings tab too
        CHECK(tiles::HitTest(Page::Wheel, cx / tiles::kW, cy / tiles::kH, kNone, 0, false) == kNone);                      // no strip, no arrows
        CHECK(tiles::HitTest(Page::PickSlot, cx / tiles::kW, cy / tiles::kH, kNone, 0, true) == kNone);                    // the editor has none
    }
    CHECK(tiles::FlipCell(0) == kFlipPrev && tiles::FlipCell(1) == kFlipNext && kFlipPrev > kTabSettings && kFlipNext > kFlipPrev);
    // the dots: in order, centred, apart, inside the panel, under the footer's buttons
    float last = -1.0f;
    for (int i = 0; i < places; ++i) {
        const float x = tiles::DotX(i, places);
        CHECK(x > last + 20.0f && x > tiles::kPanel.l && x < tiles::kPanel.r);
        last = x;
    }
    CHECK(Near(0.5f * (tiles::DotX(0, places) + tiles::DotX(places - 1, places)), 0.5f * tiles::kW, 1.0f));
    CHECK(tiles::DotX(1, places) - tiles::DotX(0, places) > tiles::DotX(2, places) - tiles::DotX(1, places));   // the first, SKILLS, a little apart
    CHECK(tiles::kDotY > tiles::kFooterBottom + 10.0f && tiles::kDotY < tiles::kPanel.b - 8.0f);
    const hset::Rect ap = hset::kApply;
    CHECK(WinY((float)(ap.y + ap.h)) < tiles::kDotY - 10.0f);   // the sheet's buttons end above the dots
}

static void TestFlipModel() {
    {   // the arrows only where there is a place that way; a click on one asks for a step
        Rig r;
        r.Step(true);
        r.m.SetPlace(0);
        const tiles::PRect p = tiles::FlipRect(0), n = tiles::FlipRect(1);
        r.Px(CentreX(p), CentreY(p));
        CHECK(r.m.Current().hover == kNone);   // nothing before Skills
        Out o = r.Step(false, true, CentreX(p) / tiles::kW, CentreY(p) / tiles::kH);
        CHECK(o.flip == 0);
        r.Px(CentreX(n), CentreY(n));
        CHECK(r.m.Current().hover == kFlipNext);
        o = r.Step(false, true, CentreX(n) / tiles::kW, CentreY(n) / tiles::kH);
        CHECK(o.flip == 1);
        r.m.SetPlace(2);
        r.Px(CentreX(p), CentreY(p));
        CHECK(r.m.Current().hover == kFlipPrev);
        o = r.Step(false, true, CentreX(p) / tiles::kW, CentreY(p) / tiles::kH);
        CHECK(o.flip == -1);
        r.m.SetPlace(kFlipPlaces - 1);
        r.Px(CentreX(n), CentreY(n));
        CHECK(r.m.Current().hover == kNone);
        o = r.Step(false, true, CentreX(n) / tiles::kW, CentreY(n) / tiles::kH);
        CHECK(o.flip == 0);
        // the place is kept in range and bumps the picture only when it changed
        const uint32_t rev = r.m.Current().revision;
        r.m.SetPlace(kFlipPlaces - 1);
        CHECK(r.m.Current().revision == rev);
        r.m.SetPlace(99);
        CHECK(r.m.Current().flipPlace == kFlipPlaces - 1);
        r.m.SetPlace(-3);
        CHECK(r.m.Current().flipPlace == 0 && r.m.Current().revision != rev);
    }
    {   // SetTab: with the strip only, a change repaints
        Rig r;
        r.Step(true);
        const uint32_t rev = r.m.Current().revision;
        r.m.SetTab(Tab::Settings);
        CHECK(r.m.Current().tab == Tab::Settings && r.m.Current().revision != rev);
        const uint32_t rev2 = r.m.Current().revision;
        r.m.SetTab(Tab::Settings);
        CHECK(r.m.Current().revision == rev2);
        Rig n;
        n.tabs = false;
        n.Step(true);
        n.m.SetTab(Tab::Settings);
        CHECK(n.m.Current().tab == Tab::Skills);
    }
}

// the sticks of a World
static void Stick(World& w, int hand, float x, float y) {
    w.f.hand[hand].stick[0] = x;
    w.f.hand[hand].stick[1] = y;
}
// a flick of the right stick (to the right: +1) and back to the middle
static void Flick(World& w, float dir, int hand = 1) {
    Stick(w, hand, dir, 0.0f);
    w.Next();
    Stick(w, hand, 0.0f, 0.0f);
    w.Next();
}
// a tap of a grip
static void Tap(World& w, int hand) {
    w.Hand(hand, 0, 0.9f, 0);
    w.Run(6);
    w.Hand(hand, 0, 0, 0);
    w.Next();
}

// The Life and Look pages' "Preview 5 s": a click is an action for the plugin and the window stays up; opening a page asks for nothing.
static void TestPreviewButton() {
    World w;
    w.LongPress();
    w.ClickTab(1);
    for (int page : {hset::kPageLife, hset::kPageLook}) {
        const Step t = w.ClickSheet(hset::kTab(hset::TabOf(page)));
        CHECK(t.settings.what == hset::Do::Nothing && w.c.SettingsModel().page == page && w.c.Open());   // opening a page starts nothing
        const Step s = w.ClickSheet(hset::kPreviewBtn);
        CHECK(s.settings.what == hset::Do::Preview && w.c.Open() && !w.c.SettingsModel().Dirty());
        const Step again = w.ClickSheet(hset::kPreviewBtn);
        CHECK(again.settings.what == hset::Do::Preview && w.c.Open());
    }
    w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageHud)));
    const Step other = w.ClickSheet(hset::kPreviewBtn);   // (the same place on another page is nothing of the kind)
    CHECK(other.settings.what != hset::Do::Preview);
}

// The Co-op page on the window's Settings tab (vr/hset_coop.h): D2R Rooms' three choices, clicked with the laser, written at once (the step
// carries the edit to the plugin: SettingsAct -> store::SubmitCoop), in our look and in the game's.
static void TestCoopOnWindow(const char* dumpDir) {
    auto st = std::make_shared<hset::coop::State>();
    st->installed = st->iniThere = true;
    st->cfg = roomsbridge::ParseIni(roomsbridge::DefaultIni() + "Mine = wss://relay.example.org\n");
    st->statusThere = true;
    st->status.plugin = "0.9.7";
    st->status.relays = {{"D2R Rooms", roomsbridge::kDefaultRelayUrl, "active", 0.12, true}, {"Mine", "wss://relay.example.org", "no_answer", -1, false}};
    st->ageMin = 1;
    World w;
    w.host.ini.coop = st;
    w.LongPress();
    w.ClickTab(1);
    const Step t = w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageCoop)));
    CHECK(t.settings.what == hset::Do::Nothing && w.c.SettingsModel().page == hset::kPageCoop && w.c.FlipPlace() == 1 + hset::kPageCoop && w.c.SettingsModel().CoopReady());
    const std::vector<uint32_t> page = Paint(w.c.ViewNow());
    if (dumpDir) WriteBmp(std::string(dumpDir) + "/tabs_coop.bmp", page, tiles::kW, tiles::kH);
    // the bridge's switch: [relay] enabled = no, at once, the click felt
    Step s = w.ClickSheet(hset::kToggle(hset::coop::kEnabled));
    CHECK(s.settings.what == hset::Do::Coop && s.settings.coop.key == "enabled" && s.settings.coop.value == "no" && s.settings.coop.sequence == 1);
    CHECK(s.feedback == Feedback::Applied && w.c.Open() && !w.c.SettingsModel().Dirty());
    CHECK(w.c.SettingsModel().CoopBusy() && !w.c.SettingsModel().CoopShown().enabled);
    const std::vector<uint32_t> clicked = Paint(w.c.ViewNow());
    CHECK(clicked != page);   // (the switch shows Off at once, amber)
    CHECK(w.ClickSheet(hset::kInc(hset::coop::kMode)).settings.what == hset::Do::Nothing);   // one at a time
    // the thread's answer comes with the store's next reading
    auto off = std::make_shared<hset::coop::State>(*st);
    off->cfg.enabled = false;
    off->written = 1;
    w.host.ini.coop = off;
    w.Run(2);
    CHECK(!w.c.SettingsModel().CoopBusy() && w.c.SettingsModel().CoopResult() == hset::Result::Saved && !w.c.SettingsModel().CoopShown().enabled);
    CHECK(w.ClickSheet(hset::kInc(hset::coop::kMode)).settings.what == hset::Do::Nothing);   // the bridge off: its choices wait
    s = w.ClickSheet(hset::kToggle(hset::coop::kEnabled));
    CHECK(s.settings.what == hset::Do::Coop && s.settings.coop.value == "yes" && s.settings.coop.sequence == 2);
    auto on = std::make_shared<hset::coop::State>(*st);
    on->written = 2;
    w.host.ini.coop = on;
    w.Run(2);
    // the relay to host on: > steps to the first of the list, < from there back to "the first that answers"
    s = w.ClickSheet(hset::kInc(hset::coop::kUse));
    CHECK(s.settings.what == hset::Do::Coop && s.settings.coop.key == "use" && s.settings.coop.value == "D2R Rooms");
    auto used = std::make_shared<hset::coop::State>(*st);
    used->cfg.use = "D2R Rooms";
    used->written = s.settings.coop.sequence;
    w.host.ini.coop = used;
    w.Run(2);
    s = w.ClickSheet(hset::kDec(hset::coop::kUse));
    CHECK(s.settings.what == hset::Do::Coop && s.settings.coop.value.empty() && s.settings.coop.sequence == 4);
    CHECK(w.ClickSheet(hset::kDec(hset::coop::kMode)).settings.what == hset::Do::Nothing);   // (the "use" edit is still on its way)
    // the edit could not be handed over: the window says so, and shows the file's choice again
    w.c.SettingsCoopFailed(s.settings.coop.sequence);
    CHECK(!w.c.SettingsModel().CoopBusy() && w.c.SettingsModel().CoopResult() == hset::Result::Failed && w.c.SettingsModel().CoopShown().use == "D2R Rooms");
    // not installed: the page offers nothing, a click on the switch is nothing
    World n;
    auto missing = std::make_shared<hset::coop::State>(*st);
    missing->installed = false;
    n.host.ini.coop = missing;
    n.LongPress();
    n.ClickTab(1);
    n.ClickSheet(hset::kTab(hset::TabOf(hset::kPageCoop)));
    CHECK(n.ClickSheet(hset::kToggle(hset::coop::kEnabled)).settings.what == hset::Do::Nothing && !n.c.SettingsModel().CoopReady());
    const std::vector<uint32_t> none = Paint(n.c.ViewNow());
    CHECK(none != page);
    if (dumpDir) WriteBmp(std::string(dumpDir) + "/tabs_coop_missing.bmp", none, tiles::kW, tiles::kH);
    // the game look ([openxr] skill_panel_game_look): the same page in the game's art, when the art is there (GAME_UI_CACHE)
    char dir[400] = {};
    if (!GetEnvironmentVariableA("GAME_UI_CACHE", dir, sizeof dir)) {
        std::printf("  co-op page: GAME_UI_CACHE not set, our look only\n");
        return;
    }
    wchar_t wide[400] = {};
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, wide, 399);
    gameui::SetDir(wide);
    gameui::SetOn(true);
    CHECK(gameui::Ready());
    World g;
    g.host.ini.coop = st;
    g.LongPress();
    g.ClickTab(1);
    g.ClickSheet(hset::kTab(hset::TabOf(hset::kPageCoop)));
    View high = g.c.ViewNow();
    high.flipHigh = true;
    const std::vector<uint32_t> game = Paint(high);
    CHECK(game != page);
    if (dumpDir) WriteBmp(std::string(dumpDir) + "/game_coop.bmp", game, tiles::kW, tiles::kH);
    s = g.ClickSheet(hset::kToggle(hset::coop::kEnabled));
    CHECK(s.settings.what == hset::Do::Coop && s.settings.coop.value == "no");   // the same click in the game's look
    high = g.c.ViewNow();
    high.flipHigh = true;
    CHECK(Paint(high) != game);
    gameui::SetOn(false);
    std::printf("  co-op page: our look and the game's\n");
}

// The World tab on the window's Settings tab: the tab opens Contact, its buttons the other three pages, a row's switch is written by itself
// a moment later (live), alone.
static void TestWorldOnWindow(const char* dumpDir) {
    World w;
    w.LongPress();
    w.ClickTab(1);
    w.ClickSheet(hset::kTab(hset::kTabWorld));
    CHECK(w.c.SettingsModel().page == hset::kPageWorld && w.c.FlipPlace() == 1 + hset::kPageWorld);
    for (int i = 1; i < hset::kWorldPages; ++i) {
        w.ClickSheet(hset::kWorldSub(i));
        CHECK(w.c.SettingsModel().page == hset::kPageWorld + i && w.c.Open());
        if (dumpDir) WriteBmp(std::string(dumpDir) + "/tabs_world" + std::to_string(i) + ".bmp", Paint(w.c.ViewNow()), tiles::kW, tiles::kH);
    }
    w.ClickSheet(hset::kWorldSub(2));   // Grab
    const int grab = hset::IndexOf(L"weight", L"grab_items");
    const Step s = w.ClickSheet(hset::kToggle(4, hset::kPageWorldGrab));
    CHECK(s.settings.what == hset::Do::Nothing && w.c.SettingsModel().Get()[grab] == 1.0f && w.c.SettingsModel().LiveOnly());
    bool saved = false;
    for (int i = 0; i < 120 && !saved; ++i) {
        const Step t = w.Next();
        if (t.settings.what == hset::Do::Save) {
            saved = true;
            CHECK(t.settings.request.changed == hset::Bit(grab) && t.settings.request.values[grab] == 1.0f);
        }
    }
    CHECK(saved);   // (a moment after the click, by itself)
    w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageMelee)));
    CHECK(w.c.SettingsModel().page == hset::kPageMelee);
}

static void TestFlipController() {
    {   // along the pages: Skills, View, Options, Video, Body, Life, Look, HUD - and stops at the ends
        World w;
        w.LongPress();
        w.Run(2);
        CHECK(w.c.Open() && w.c.FlipPlace() == 0 && w.c.ViewNow().flipPlace == 0 && w.c.ViewNow().tab == Tab::Skills);
        Flick(w, 1.0f);
        CHECK(w.c.ViewNow().tab == Tab::Settings && w.c.SettingsModel().page == hset::kPageView && w.c.FlipPlace() == 1 && w.c.ViewNow().flipPlace == 1);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageOptions && w.c.FlipPlace() == 2);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageVideo && w.c.FlipPlace() == 3);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageKeysL && w.c.FlipPlace() == 4);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageKeysR && w.c.FlipPlace() == 5);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageVibration && w.c.FlipPlace() == 6);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageHud && w.c.FlipPlace() == 7);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageLife && w.c.FlipPlace() == 8);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageLook && w.c.FlipPlace() == 9);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageBody && w.c.FlipPlace() == 10);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageBelt && w.c.FlipPlace() == 11);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageMelee && w.c.FlipPlace() == 12);
        for (int i = 0; i < hset::kWorldPages; ++i) {   // the World tab's four pages, one flick each
            Flick(w, 1.0f);
            CHECK(w.c.SettingsModel().page == hset::kPageWorld + i && w.c.FlipPlace() == 13 + i);
        }
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageCoop && w.c.FlipPlace() == 17);
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageParty && w.c.FlipPlace() == 18);   // the Party page (the Co-op tab's second page): a flick passes it too
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageGuide && w.c.FlipPlace() == 19 && w.c.SettingsModel().GuideShown());
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == hset::kPageGuide && w.c.FlipPlace() == 19 && w.c.Open());   // the end: a stop
        Flick(w, -1.0f);
        for (int i = 0; i < hset::kWorldPages; ++i) Flick(w, -1.0f);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        CHECK(w.c.FlipPlace() == 5 && w.c.SettingsModel().page == hset::kPageKeysR);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        CHECK(w.c.FlipPlace() == 3 && w.c.SettingsModel().page == hset::kPageVideo);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        CHECK(w.c.FlipPlace() == 1 && w.c.ViewNow().tab == Tab::Settings);
        Flick(w, -1.0f);
        CHECK(w.c.FlipPlace() == 0 && w.c.ViewNow().tab == Tab::Skills);
        Flick(w, -1.0f);
        CHECK(w.c.FlipPlace() == 0 && w.c.ViewNow().tab == Tab::Skills && w.c.Open());   // the other end
        // the left stick flips too, and a flick of the Settings tab keeps its draft
        Flick(w, 1.0f, 0);
        CHECK(w.c.FlipPlace() == 1);
        w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageVideo)));
        w.ClickSheet(hset::kPreset(4));
        CHECK(w.c.SettingsModel().Dirty() && w.c.FlipPlace() == 3);
        Flick(w, 1.0f);
        Flick(w, -1.0f);
        Flick(w, -1.0f);
        CHECK(w.c.SettingsModel().Dirty() && w.c.FlipPlace() == 2);
    }
    {   // one flick = one page: held over, dead zone, re-arm
        World w;
        w.LongPress();
        w.Run(2);
        Stick(w, 1, 1.0f, 0.0f);
        w.Run(60);
        CHECK(w.c.FlipPlace() == 1);   // held for a second: one step
        Stick(w, 1, 0.5f, 0.0f);
        w.Run(5);
        CHECK(w.c.FlipPlace() == 1);
        Stick(w, 1, 1.0f, 0.0f);
        w.Run(5);
        CHECK(w.c.FlipPlace() == 1);   // (0.5 is not the middle: not re-armed)
        Stick(w, 1, 0.1f, 0.0f);
        w.Next();
        Stick(w, 1, 1.0f, 0.0f);
        w.Next();
        CHECK(w.c.FlipPlace() == 2);
        Stick(w, 1, 0.0f, 0.0f);
        w.Next();
        // not far enough, and up / down: nothing
        Stick(w, 1, 0.5f, 0.0f);
        w.Run(5);
        Stick(w, 1, 0.0f, 1.0f);
        w.Run(5);
        Stick(w, 1, 0.7f, 0.9f);
        w.Run(5);
        CHECK(w.c.FlipPlace() == 2);
        // the flip's tick
        Stick(w, 1, 0.0f, 0.0f);
        w.Next();
        Stick(w, 1, 1.0f, 0.0f);
        const Step s = w.Next();
        CHECK(s.feedback == Feedback::Hover && w.c.FlipPlace() == 3);
    }
    {   // a stick pushed as the window opens (the player was turning) is not a flick
        World w;
        Stick(w, 1, 1.0f, 0.0f);
        w.LongPress();
        w.Run(30);
        CHECK(w.c.Open() && w.c.FlipPlace() == 0);
        Stick(w, 1, 0.0f, 0.0f);
        w.Next();
        Stick(w, 1, 1.0f, 0.0f);
        w.Next();
        CHECK(w.c.FlipPlace() == 1);
    }
    {   // the window takes both thumbsticks while it is up (the hero neither walks nor turns), and after it closes until each is back in the middle
        World w;
        Step s = w.Next();
        CHECK((s.take[0] & di::kTakeStick) == 0 && (s.take[1] & di::kTakeStick) == 0);   // closed: the game's
        w.LongPress();
        s = w.Next();
        CHECK((s.take[0] & di::kTakeStick) != 0 && (s.take[1] & di::kTakeStick) != 0);
        Stick(w, 0, 0.0f, 1.0f);
        Stick(w, 1, 1.0f, 0.0f);
        w.Run(3);
        w.LongPress();   // closed with both sticks pushed
        CHECK(!w.c.Open());
        s = w.Next();
        CHECK((s.take[0] & di::kTakeStick) != 0 && (s.take[1] & di::kTakeStick) != 0);   // still taken: no walking off
        Stick(w, 0, 0.0f, 0.0f);
        s = w.Next();
        CHECK((s.take[0] & di::kTakeStick) == 0 && (s.take[1] & di::kTakeStick) != 0);   // each as it comes back
        Stick(w, 1, 0.1f, 0.0f);
        s = w.Next();
        CHECK((s.take[1] & di::kTakeStick) == 0);
        // without the tab strip the sticks are the game's as they ever were: walking and turning go on while the window is up
        World n(1, false);
        n.LongPress();
        Stick(n, 1, 1.0f, 0.0f);
        s = n.Next();
        CHECK(n.c.Open() && (s.take[1] & di::kTakeStick) == 0 && (s.take[0] & di::kTakeStick) == 0);
        World u;
        u.useHost = false;
        u.LongPress();
        s = u.Next();
        CHECK(u.c.Open() && (s.take[1] & di::kTakeStick) == 0);
        // and a flick does nothing there
        Flick(n, 1.0f);
        CHECK(n.c.FlipPlace() == 0 && n.c.ViewNow().tab == Tab::Skills);
    }
    {   // the grips: a tap of the right one forward, the left one back; the chord is no page
        World w;
        w.LongPress();
        w.Run(3);
        Tap(w, 1);
        CHECK(w.c.FlipPlace() == 1);
        Tap(w, 1);
        CHECK(w.c.FlipPlace() == 2);
        Tap(w, 0);
        CHECK(w.c.FlipPlace() == 1);
        Tap(w, 0);
        CHECK(w.c.FlipPlace() == 0);
        Tap(w, 0);
        CHECK(w.c.FlipPlace() == 0 && w.c.Open());   // a stop
        // a long squeeze is not a tap
        w.Hand(1, 0, 0.9f, 0);
        w.Run(40);
        w.Hand(1, 0, 0, 0);
        w.Next();
        CHECK(w.c.FlipPlace() == 0);
        // both grips - the chord's beat - and let go: no page
        w.Hand(0, 0, 0.9f, 0);
        w.Hand(1, 0, 0.9f, 0);
        w.Run(10);
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        w.Run(3);
        CHECK(w.c.FlipPlace() == 0);
        // the chord that opens the window on Settings, the grips let go after it: the place is Settings > View, and stays
        World c;
        c.Chord();
        CHECK(c.c.Open() && c.c.ViewNow().tab == Tab::Settings && c.c.FlipPlace() == 1);
        c.Run(5);
        CHECK(c.c.FlipPlace() == 1);
        // the chord that switches a Skills window to Settings; its grips do not page on
        World d;
        d.LongPress();
        d.Run(3);
        d.Chord();
        CHECK(d.c.Open() && d.c.FlipPlace() == 1);
        d.Run(5);
        CHECK(d.c.FlipPlace() == 1);
    }
    {   // the arrows at the edges: a click is a step; none where there is no place
        World w;
        w.LongPress();
        w.Run(3);
        const tiles::PRect prev = tiles::FlipRect(0), next = tiles::FlipRect(1);
        w.ClickPx(CentreX(prev), CentreY(prev));
        CHECK(w.c.FlipPlace() == 0 && w.c.Open());
        w.ClickPx(CentreX(next), CentreY(next));
        CHECK(w.c.FlipPlace() == 1 && w.c.ViewNow().tab == Tab::Settings);
        for (int i = 0; i < 18; ++i) w.ClickPx(CentreX(next), CentreY(next));
        CHECK(w.c.FlipPlace() == 19);
        w.ClickPx(CentreX(next), CentreY(next));
        CHECK(w.c.FlipPlace() == 19);
        w.ClickPx(CentreX(prev), CentreY(prev));
        CHECK(w.c.FlipPlace() == 18 && w.c.SettingsModel().page == hset::kPageParty);
        // the laser's click on a page tab of the sheet moves the place too, the same order
        w.ClickSheet(hset::kTab(hset::TabOf(hset::kPageView)));
        CHECK(w.c.FlipPlace() == 1 && w.c.ViewNow().flipPlace == 1);
        // and on the Skills tab of the strip: place 0; the Settings tab of the strip: the page it was on
        w.ClickTab(0);
        CHECK(w.c.FlipPlace() == 0);
        w.ClickTab(1);
        CHECK(w.c.FlipPlace() == 1);
    }
    {   // the editor's pages: no flipping there (they have their own buttons), the sticks stay the window's
        World w;
        w.LongPress();
        w.Run(3);
        const tiles::PRect eb = tiles::ButtonRect(Page::Wheel, 0);
        w.ClickPx(CentreX(eb), CentreY(eb));
        CHECK(w.c.ViewNow().page == Page::PickSlot);
        Flick(w, 1.0f);
        Tap(w, 1);
        CHECK(w.c.FlipPlace() == 0 && w.c.ViewNow().tab == Tab::Skills && w.c.ViewNow().page == Page::PickSlot);
        const Step s = w.Next();
        CHECK((s.take[1] & di::kTakeStick) != 0);
    }
    {   // closed: nothing flips, and the next opening has a quiet first frame again
        World w;
        Flick(w, 1.0f);
        CHECK(!w.c.Open() && w.c.FlipPlace() == 0);
        w.LongPress();
        w.Run(3);
        Flick(w, 1.0f);
        CHECK(w.c.FlipPlace() == 1);
        w.LongPress();   // closed on Settings > View
        w.Run(3);
        w.LongPress();
        CHECK(w.c.Open() && w.c.FlipPlace() == 1 && w.c.ViewNow().tab == Tab::Settings);   // the tab is where it was left
    }
    {   // not in F4 / measuring: nothing
        World w;
        w.LongPress();
        w.Run(3);
        w.f4 = false;
        w.Next();
        w.f4 = true;
        w.Run(3);
        Flick(w, 1.0f);
        CHECK(!w.c.Open() && w.c.FlipPlace() == 0);
    }
}

static void TestFlipPaint() {
    World w;
    w.LongPress();
    w.Run(3);
    View v = w.c.ViewNow();
    std::vector<uint32_t> places[kFlipPlaces];
    for (int i = 0; i < kFlipPlaces; ++i) {
        v.flipPlace = i;
        v.flipAt = i;
        places[i] = Paint(v);
        for (int j = 0; j < i; ++j) CHECK(places[i] != places[j]);   // the dots, the arrows
    }
    // the dots and the arrows are the only difference between two places of the same tab (the Skills tab at 0 and the same picture told it is at 1)
    {
        v.flipPlace = 0;
        v.flipAt = 0;
        const std::vector<uint32_t> a = Paint(v);
        v.flipPlace = 1;
        v.flipAt = 1;
        const std::vector<uint32_t> b = Paint(v);
        int x0 = tiles::kW, y0 = tiles::kH, x1 = -1, y1 = -1;
        for (int y = 0; y < tiles::kH; ++y)
            for (int x = 0; x < tiles::kW; ++x)
                if (a[(size_t)y * tiles::kW + x] != b[(size_t)y * tiles::kW + x]) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
        CHECK(x1 >= 0 && y1 >= 0);
        // everything that changed is a dot (the foot) or an arrow (an edge)
        bool onlyHints = true;
        for (int y = 0; y < tiles::kH && onlyHints; ++y)
            for (int x = 0; x < tiles::kW; ++x)
                if (a[(size_t)y * tiles::kW + x] != b[(size_t)y * tiles::kW + x]) {
                    const bool dot = y > 1020 && x > 480 && x < 1120, arrow = y >= 470 && y <= 690 && (x < 110 || x > 1490);
                    if (!dot && !arrow) { onlyHints = false; break; }
                }
        CHECK(onlyHints);
    }
    // a lit arrow: the changed pixels stay in its zone
    v.flipPlace = 2;
    v.flipAt = 2;
    const std::vector<uint32_t> cold = Paint(v);
    for (int i = 0; i < 2; ++i) {
        View lit = v;
        lit.hover = tiles::FlipCell(i);
        const std::vector<uint32_t> hot = Paint(lit);
        CHECK(hot != cold);
        const tiles::PRect z = tiles::FlipRect(i);
        bool inZone = true;
        for (int y = 0; y < tiles::kH && inZone; ++y)
            for (int x = 0; x < tiles::kW; ++x)
                if (cold[(size_t)y * tiles::kW + x] != hot[(size_t)y * tiles::kW + x] && !(x >= z.l && x < z.r && y >= z.t && y < z.b)) { inZone = false; break; }
        CHECK(inZone);
    }
    // the hints are on the Settings tab too, and not in the editor
    v.flipPlace = 3;
    v.flipAt = 3;
    v.tab = Tab::Settings;
    const std::vector<uint32_t> onSettings = Paint(v);
    View noTabs = v;
    noTabs.tabs = false;
    noTabs.tab = Tab::Skills;
    const std::vector<uint32_t> plainWindow = Paint(noTabs);
    CHECK(onSettings != plainWindow);
    View editor = w.c.ViewNow();
    editor.page = Page::PickSlot;
    editor.flipPlace = 2;
    editor.flipAt = 2;
    View editorPlain = editor;
    editorPlain.tabs = false;
    CHECK(Paint(editor) == Paint(editorPlain));   // (the editor's pages: no strip, no hints)
}

// -- the shared hands ----------------------------------------------------------------------------------------------------------------------

static void TestHands() {
    {   // the click: a rise on a button presses it, a held stepper repeats, a press on nothing is nothing
        hset::Model m;
        hset::Snapshot sn;
        sn.ready = true;
        m.Open(sn);
        hset::Clicker k;
        const hset::Hit inc{hset::Kind::Change, 0, 1};
        hset::Action a = k.Step(m, inc, true, true, true, true, 0, 1000.0);
        CHECK(m.Dirty() && a.what == hset::Do::Nothing);
        const float once = m.Get()[0];
        k.Step(m, inc, true, false, true, true, 0, 1200.0);
        CHECK(m.Get()[0] == once);
        k.Step(m, inc, true, false, true, true, 0, 1000.0 + hset::kRepeatFirstMs + 5.0);
        CHECK(m.Get()[0] > once);
        k.Step(m, hset::Hit{}, true, false, true, true, 0, 3000.0);   // the beam moved off the button: no repeat
        const float now = m.Get()[0];
        k.Step(m, hset::Hit{}, true, false, true, true, 0, 4000.0);
        CHECK(m.Get()[0] == now);
        k.Step(m, hset::Hit{}, false, true, true, true, 0, 5000.0);   // a rise off the sheet is nothing
        CHECK(m.Get()[0] == now);
        k.Reset();
    }
    {   // the chord: both grips, the beat, the click; one grip or too soon is not
        hset::Chord c;
        const float both[2] = {0.9f, 0.9f}, one[2] = {0.9f, 0.0f}, none[2] = {0, 0};
        const bool click[2] = {false, true}, still[2] = {false, false};
        hset::Chord::Out o = c.Step(1000.0, true, both, still);
        CHECK(!o.armed && !o.edge && c.Arming());
        o = c.Step(1100.0, true, both, click);
        CHECK(!o.armed && o.edge);   // too soon
        o = c.Step(1200.0, true, both, still);
        o = c.Step(1400.0, true, both, still);
        CHECK(o.armed && !o.edge);
        o = c.Step(1411.0, true, both, click);
        CHECK(o.armed && o.edge);
        o = c.Step(1422.0, true, both, click);
        CHECK(o.armed && !o.edge);   // held: not a new click
        o = c.Step(1433.0, true, one, still);
        CHECK(!o.armed && !c.Arming());
        o = c.Step(1444.0, true, none, still);
        c.Step(2000.0, false, both, still);   // disabled: counts as let go
        CHECK(!c.Arming());
        c.Step(2100.0, true, both, still);
        c.Reset();
        CHECK(!c.Arming());
    }
}

// The overlay in every VR view (Serg, 2026-11-02: "игровой оверлей ... во все режимы ф1 ф2 ф3, убрать только то что не относится"): which pages and rows each
// view shows, and the window in VR F1 - F3 - opened by the same long press, the Settings tab's pages those views have, nothing picked for the trigger,
// nothing cast; F4 as it was.
static void TestViews() {
    using namespace hset;
    // the pages: F4 (and 0, not known) every one; F1 - F3 not F4's body
    const int bodyPages[] = {kPageVibration, kPageLife, kPageLook, kPageBody, kPageBelt, kPageMelee, kPageWorld, kPageWorldReact, kPageWorldGrab, kPageWorldMore};
    const int allPages[] = {kPageView, kPageOptions, kPageVideo, kPageKeysL, kPageKeysR, kPageHud, kPageCoop, kPageParty, kPageGuide};
    for (int view = 0; view <= 4; ++view) {
        const bool flat = view >= 1 && view <= 3;
        for (int p : bodyPages) CHECK(PageShown(p, view) == !flat);
        for (int p : allPages) CHECK(PageShown(p, view));
        int tabs = 0;
        for (int t = 0; t < kTabCount; ++t) tabs += TabShown(t, view) ? 1 : 0;
        CHECK(tabs == (flat ? 7 : 14));   // View, Options, Video, Controls, HUD, Co-op, Guide - or all fourteen
        // the strip: the shown tabs inside it and apart, in order; one not shown has no place
        int lastEnd = 0;
        for (int t = 0; t < kTabCount; ++t) {
            const hset::Rect r = TabRect(t, view);
            if (!TabShown(t, view)) { CHECK(r.w == 0 && r.h == 0); continue; }
            CHECK(r.x >= 32 && r.x + r.w <= 32 + 960 && r.x > lastEnd && r.w >= kTabNameW[t] + 8);
            lastEnd = r.x + r.w;
            float u = (r.x + 0.5f * r.w) / kW, v = (r.y + 0.5f * r.h) / kH;
            CHECK((HitTest(kPageView, u, v, view) == Hit{Kind::Tab, TabPage(t), 0}));
        }
        // the rows: Options without "Menus as a panel", HUD with the toolbar type alone (the plates are F4's hook), the rest as in F4
        CHECK(RowsOn(kPageOptions, view) == RowsOn(kPageOptions) - (flat ? 1 : 0));
        CHECK(RowsOn(kPageHud, view) == (flat ? 1 : 3) && KeyAt(kPageHud, 0, view) == kKeyBarPlace);
        CHECK(RowsOn(kPageView, view) == RowsOn(kPageView) && RowsOn(kPageKeysL, view) == 6 && RowsOn(kPageKeysR, view) == 6);
        for (int r = 0; KeyAt(kPageOptions, r, view) >= 0; ++r) CHECK(!flat || KeyAt(kPageOptions, r, view) != IndexOf(L"openxr", L"menu_3d"));
        // the flips: Skills and the pages shown (Party with it, while it is on)
        CHECK(FlipCount(view) == (flat ? 10 : kFlipPlaces) && FlipCount(view, false) == (flat ? 9 : kFlipPlaces - 1));
    }
    // Controls: one tab, two pages, a button each on top
    CHECK(TabOf(kPageKeysL) == TabOf(kPageKeysR) && TabPage(kTabControls) == kPageKeysL && !std::strcmp(kTabNames[kTabControls], "Controls") && HasSubRow(kPageKeysR));
    for (int i = 0; i < kKeysPages; ++i) {
        const hset::Rect b = kSubBtn(i, kKeysPages);
        const float u = (b.x + 0.5f * b.w) / kW, v = (b.y + 0.5f * b.h) / kH;
        CHECK((HitTest(kPageKeysL, u, v) == Hit{Kind::Tab, kPageKeysL + i, 0}) && (HitTest(kPageKeysR, u, v) == Hit{Kind::Tab, kPageKeysL + i, 0}));
    }
    // the model: a page the view does not show gives way to View, and cannot be clicked to
    {
        hset::Model m;
        hset::Snapshot snap;
        snap.ready = true;
        m.Open(snap);
        m.SetView(4);
        m.Click({Kind::Tab, kPageMelee, 0});
        CHECK(m.page == kPageMelee);
        m.SetView(2);
        CHECK(m.page == kPageView);
        m.Click({Kind::Tab, kPageBelt, 0});
        CHECK(m.page == kPageView);
        m.Click({Kind::Tab, kPageHud, 0});
        CHECK(m.page == kPageHud);
        const int plates = IndexOf(L"hud", L"ordinary_mob_bars");
        m.Click({Kind::Change, plates, 1});
        CHECK(!m.Dirty());   // (a row F2 does not show)
    }

    // the window in VR F1, F2, F3: the frame as the input layer makes it there (another view: kInMenu with kOtherView; no direct mode)
    for (int view = 1; view <= 3; ++view) {
        World w;
        w.f4 = false;
        w.c.SetView(view);
        w.f.flags = di::kFocused | di::kInMenu | di::kOtherView;
        w.Run(2);
        w.Hand(0, 0, 0.9f, 0);   // the left grip + the right click: the controller's swap there, not the wheel's chord (the long press is the way in)
        w.Next();
        w.Hand(1, 0, 0, di::kStick);
        Step s = w.Next();
        CHECK(!w.c.Open() && !(s.take[1] & di::kStick));
        w.Hand(0, 0, 0, 0);
        w.Hand(1, 0, 0, 0);
        w.Run(2);
        s = w.LongPress();
        CHECK(w.c.Open() && s.feedback == Feedback::Opened && w.c.ViewNow().tabs);
        s = w.Next();
        CHECK((s.take[1] & di::kTakeTrigger) && (s.take[0] & di::kTakeStick));   // the window has the hands, as in F4
        w.ClickTab(1);
        CHECK(w.c.ViewNow().tab == Tab::Settings && w.c.SettingsModel().View() == view && w.c.SettingsModel().page == kPageView);
        CHECK(w.c.ViewNow().flipCount == 10 && w.c.ViewNow().flipAt == 1);
        const int along[] = {kPageView, kPageOptions, kPageVideo, kPageKeysL, kPageKeysR, kPageHud, kPageCoop, kPageParty, kPageGuide};
        for (size_t i = 1; i < sizeof along / sizeof along[0]; ++i) {
            Flick(w, 1.0f);
            CHECK(w.c.SettingsModel().page == along[i] && w.c.ViewNow().flipAt == (int)i + 1);
        }
        Flick(w, 1.0f);
        CHECK(w.c.SettingsModel().page == kPageGuide && w.c.Open());   // the end
        w.ClickPage(kPageKeysR);   // Controls, then its Right button
        CHECK(w.c.SettingsModel().page == kPageKeysR);
        // closed again (B): nothing picked, the trigger casts nothing
        w.Hand(1, 0, 0, di::kSecondary);
        w.Next();
        w.Hand(1, 0, 0, 0);
        w.Run(3);
        CHECK(!w.c.Open());
        w.Hand(1, 1.0f, 0, 0);
        s = w.Next();
        s = w.Next();
        CHECK(!s.casting && w.c.Selected() < 0 && !(s.take[1] & di::kTakeTrigger));
        w.Hand(1, 0, 0, 0);
        w.Next();
        // a game menu open: no window
        w.f.flags |= di::kGameMenu;
        w.LongPress();
        CHECK(!w.c.Open());
    }
    {   // a view that is not VR (the flat game): stood down, as ever
        World w;
        w.f4 = false;
        w.c.SetView(0);
        w.f.flags = di::kFocused | di::kInMenu | di::kOtherView;
        w.LongPress();
        CHECK(!w.c.Open());
    }
    {   // F4 as it was: every page along the flips
        World w;
        w.LongPress();
        w.Run(2);
        w.ClickTab(1);
        CHECK(w.c.SettingsModel().View() == 4 && w.c.ViewNow().flipCount == kFlipPlaces);
        w.ClickSheet(kTab(TabOf(kPageMelee)));
        CHECK(w.c.SettingsModel().page == kPageMelee);
    }
}

int main(int argc, char** argv) {
    const char* dump = argc == 3 && !std::strcmp(argv[1], "--dump") ? argv[2] : nullptr;
    TestStrip();
    TestSheet();
    TestModelTabs();
    TestLongPressTabs();
    TestKeyOffLive();
    TestChordTabs();
    TestPagesOnWindow();
    TestLivePagesOnWindow();
    TestPaint(dump);
    TestSpriteSheets();
    TestFlipHigh();
    TestGameLook(dump);
    TestScene();
    TestHands();
    TestFlipper();
    TestFlipGeometry();
    TestFlipModel();
    TestFlipController();
    TestPreviewButton();
    TestCoopOnWindow(dump);
    TestWorldOnWindow(dump);
    TestFlipPaint();
    TestViews();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
