#pragma once
// The skill wheel's hands: the chord that opens it, the panel hung in front of the head, the pointing hand's ray against
// it, what the rest of the mod may see of the controllers meanwhile, the release gate after it closes, and the trigger's
// cast. A headset frame (vr/direct_input.h) in, the things to do out. No Windows, no OpenXR, no game: a test drives it with
// made-up poses (cleanroom/tests/skill_wheel_test.cpp), vr/skill_wheel.cpp does what it says.
//
// Two ways to open it ([openxr] skill_wheel_open): the LONG PRESS of the right thumbstick click (mode 1; the input layer's hold-or-tap
// machine turns it into a request, directinput::TakeSkillPanelToggle, which arrives here as `toggleRequest`) or the CHORD (mode 0, the
// original: the other hand's grip held + the pointing hand's thumbstick click). Either closes it again; in the long-press mode the same
// long press of the right click closes it too (the layout cannot see that click while the panel is up). Mode 1 keeps the chord as a
// fallback where the plugin is not the input (no direct mode: the request can never come).
//
// The Settings tab ([openxr] headset_settings, tiles look): the window gets a tab strip, SKILLS | SETTINGS, and the Settings tab shows the
// headset's settings pages (vr/hset_*.h: the game view F1 - F4, the render size, the T-pose measurement) on the same window, with the same
// laser and trigger. The long press of the right stick click opens the window on the tab it was last on (the tab is kept while the session
// runs); the old settings panel's chord - both grips a third of a second, then a thumbstick click - opens it on the Settings tab (on the
// Skills tab it switches to Settings, on Settings it closes). Closing is the window's own: the long press again, B / Y / menu, or the
// pages' Close button. Calibrate body closes it for the measurement and brings it back on the Body page when that is over.
//
// The window flips like pages (vr/wheel_flip.h): a flick of a thumbstick left / right, or a tap of the left / right grip, moves one place along
// SKILLS | SETTINGS: View | Options | Video | Body | Life | Look | HUD | Belt | Melee | Left | Right (stopping at the ends), and so does a click on the arrow at the window's edge; dots at its foot
// show where it stands. With the tab strip the window takes both thumbsticks while it is up (the hero neither walks nor turns with them) and after it
// closes until each is back in the middle.
//
// Only in VR first person (F4: viewgate::F4(), passed in as `f4`): in any other view the panel stands down - closed, nothing taken from
// the layout, no press, no flag - and starts fresh when F4 comes back.
//
// The chord: the other hand's grip held + the pointing hand's thumbstick click. In direct input's layout the pointing
// (right) stick click is "swap weapons" and the left grip is the chord modifier; nothing is bound to the two together, and
// the settings panel's chord needs BOTH grips, so the wheel ignores a click while the pointing hand's grip is down too.
// From the moment the other grip is down the click is taken from the layout (the first one cannot leak into a swap).
// While the wheel is up both hands' trigger, grip, A B X Y, menu and stick clicks are the wheel's (the sticks go on
// walking and turning); what is held when it closes stays taken until it is let go.

#include <cmath>
#include <cstdint>

#include "direct_input.h"
#include "hset_click.h"
#include "hset_model.h"
#include "slot_controls.h"
#include "tiles_geom.h"
#include "wheel_cast.h"
#include "wheel_geom.h"
#include "wheel_model.h"
#include "wheel_ray.h"

namespace wheel {

struct Settings {
    int pointerHand = 1;        // [openxr] laser_hand: 0 left, 1 right
    float distanceM = 0.80f;    // [openxr] skill_wheel_distance_m: the panel this far ahead of the head (either look)
    float widthM = 0.80f;       // [openxr] skill_wheel_width_m: the wheel's side (the wheel is square)
    float belowM = 0.08f;       // and this far below the eyes
    Style style = Style::Wheel; // [openxr] skill_panel_style: 0 the wheel, 1 the tiles
    float tilesWidthM = 0.90f;  // [openxr] skill_tiles_width_m: the tiles' window is this wide (its height follows: tiles::kAspect)
    float tilesBelowM = 0.15f;  // and hangs this far below the eyes (a window of two rows sits lower than the wheel)
    Hints hints;                // the captions under the tiles' slots (tiles::BuildHints); empty = none
    int openMode = 0;           // [openxr] skill_wheel_open: 0 the chord (left grip + click), 1 the long press of the right stick click
    float holdS = 0.50f;        // how long a long press is ([direct] hold_ms: the same time that opened it closes it)
    bool settingsTab = false;   // [openxr] headset_settings: the tiles' window has the Settings tab (the wheel's look has none)
    bool categories = true;     // [openxr] skill_categories: the editor has the category strip (All, the class's trees, General / Items) when the cache knows the trees
    bool runWalk = false;       // [openxr] skill_run_walk (off by default): a RUN / WALK control (a tile in the tiles' look, a button on the wheel): the game's own run / walk switch
    bool slotControls = false;  // [openxr] skill_rt_a (off by default): the right trigger is the game's RT slot and the right A its A slot (vr/slot_controls.h)
};

// The panel's size in metres for these settings: the wheel is square, the tiles' window is wide.
inline float PanelWidthM(const Settings& c) { return c.style == Style::Tiles ? c.tilesWidthM : c.widthM; }
inline float PanelHeightM(const Settings& c) { return c.style == Style::Tiles ? tiles::HeightFor(c.tilesWidthM) : c.widthM; }

// The tab strip and the Settings tab are there: the key is on, the tiles' look, and the host hands in the settings' side.
inline bool TabsOn(const Settings& c) { return c.settingsTab && c.style == Style::Tiles; }

// What the Settings tab needs of its host each frame (skill_wheel.cpp fills it).
struct SettingsIn {
    hset::Snapshot ini;           // d2r_vr.ini as the settings' store reads it now
    hset::video::Env env;         // the picture's sizes, for the Video page
    int view = 4;                 // the game's view, 1 = F1 ... 4 = F4 (the window is F4's)
    bool calibrating = false;     // the T-pose measurement runs (or is about to): the window is not to be had, and comes back when it is over
    bool chordOn = true;          // both grips and a thumbstick click open it on the Settings tab
};

constexpr float kBeamM = 1.5f;   // the beam's length where it hits nothing

// What the headset layer needs of the wheel this frame.
struct Layer {
    bool show = false;
    Pose panel;
    float widthM = 0.8f, heightM = 0.8f;
    Style style = Style::Wheel;
    bool beam = false, hit = false;
    V3 from, dir, at;
    float len = kBeamM;
};

struct Step {
    uint32_t take[2] = {0, 0};            // directinput::Take masks for the layout
    bool casting = false;                 // the picked slot's pad buttons are down (CastGate), or a slot control's press is the game's (SlotControls)
    uint16_t buttons = 0;
    uint8_t lt = 0, rt = 0;
    int slotStarted[2] = {-1, -1};        // a slot control's press began this frame: the slot (8..11) it pressed, per control (A, right trigger); for the log
    Feedback feedback = Feedback::None;
    bool runWalk = false;                 // RUN / WALK was clicked: press the game's run / walk switch (kRunWalkCommand) once
    bool request = false;                 // a request for the game's UI thread
    Request req;
    Layer layer;
    hset::Action settings;                // the Settings tab asks for: Save (a request for the ini's thread), Recentre, Calibrate, View
    bool wantStore = false;               // the ini's thread should be running: the window is up, or comes back, or its chord is arming
};

class Controller {
    Settings cfg_;
    Model model_;
    CastGate cast_;
    SlotControls slots_;
    Pose panel_;
    bool gripDown_[2] = {false, false};
    bool stickWas_ = false, triggerWas_ = false, backWas_ = false;
    bool standing_ = false;            // not in F4 (stood down)
    bool castBlocked_ = false;         // SetCastBlocked
    bool triggerTaken_ = false, aTaken_ = false;   // SetControlsTaken
    bool triggerSkill_ = false;        // TriggerCastsSkill
    bool closeArmed_ = false;          // the right stick click was let go since the panel opened: a long press of it closes the panel
    double closeHoldAt_ = -1.0;        // when that long press began (< 0: not pressed)
    struct Held {
        uint32_t buttons = 0;
        bool trigger = false, squeeze = false, stick = false;   // (stick: the thumbstick's axes, with the tab strip)
    } gate_[2];
    uint32_t session_ = 0;
    hset::Model settings_;             // the Settings tab's pages
    hset::Clicker clicker_;            // the trigger on them
    hset::Chord chord_;                // both grips + a thumbstick click
    Flipper flip_;                     // a flick of a stick, a tap of a grip: one place along the window's pages
    bool resume_ = false;              // Calibrate body closed the window: it comes back on the Body page when the measurement is over

    static constexpr uint32_t kWheelButtons = directinput::kPrimary | directinput::kSecondary | directinput::kStick | directinput::kMenuButton;
    static uint32_t Mask(const Held& h) {
        return h.buttons | (h.trigger ? directinput::kTakeTrigger : 0u) | (h.squeeze ? directinput::kTakeSqueeze : 0u) | (h.stick ? directinput::kTakeStick : 0u);
    }

    static Pose PanelFor(const directinput::Pose& head, const Settings& c) {
        const Q yaw = YawQ(YawOf(Q{head.rot[0], head.rot[1], head.rot[2], head.rot[3]}));
        Pose p;
        p.valid = true;
        p.q = yaw;
        p.p = Add(V3{head.pos[0], head.pos[1], head.pos[2]}, Rot(yaw, V3{0.0f, c.style == Style::Tiles ? -c.tilesBelowM : -c.belowM, -c.distanceM}));
        return p;
    }

public:
    void Configure(const Settings& s) { cfg_ = s; cfg_.pointerHand = s.pointerHand ? 1 : 0; }
    // The pointing hand's trigger belongs to something else this frame (the bow draw keeps it: a bow in hand shoots by a draw only, or the
    // string is taken with it): the picked slot is not cast with it. Set before every Frame; false = as before.
    void SetCastBlocked(bool blocked) { castBlocked_ = blocked; }
    // A gesture other than this panel holds the right trigger / the right A this frame (a bottle in the hand, a javelin, the bow's string: the frame
    // before's takes, directinput::TakenLastFrame): the slot controls leave that control to it. Set before every Frame; false = free.
    void SetControlsTaken(bool trigger, bool a) { triggerTaken_ = trigger; aTaken_ = a; }
    const Settings& Config() const { return cfg_; }
    // This frame the right trigger is the game's RT slot (or LT + RT with the left trigger held) and that slot holds a skill: a pull of it casts that skill,
    // and the layout's attack does not see it (the melee rule asks - vr/melee_trigger.h). False where the slot controls are off, in another view, with a
    // panel up, a slot picked, a gesture or the grip holding the trigger: the trigger is the layout's.
    bool TriggerCastsSkill() const { return triggerSkill_; }
    const View& ViewNow() const { return model_.Current(); }
    int Selected() const { return model_.Selected(); }
    bool Open() const { return model_.Open(); }
    Pose PanelPose() const { return panel_; }
    const hset::Model& SettingsModel() const { return settings_; }
    // Where the window stands along its pages (vr/wheel_flip.h): 0 the Skills tab, 1 + the page on the Settings tab.
    int FlipPlace() const { return model_.Current().tab == Tab::Settings ? 1 + std::clamp(settings_.page, 0, hset::PageCount() - 1) : 0; }
    // The Settings tab's Save could not even be handed to the ini's thread.
    void SettingsSubmitFailed(uint64_t sequence) { settings_.SubmitFailed(sequence); }

    void Reset() {
        model_.Reset();
        cast_.Reset();
        slots_.Reset();
        triggerSkill_ = false;
        settings_ = hset::Model{};
        clicker_.Reset();
        chord_.Reset();
        flip_.Reset();
        resume_ = false;
        for (Held& h : gate_) h = Held{};
        gripDown_[0] = gripDown_[1] = false;
        stickWas_ = triggerWas_ = backWas_ = false;
        closeArmed_ = false;
        closeHoldAt_ = -1.0;
    }

    // `f4`: the view is VR first person (viewgate::F4()). `toggleRequest`: the long press asked for the panel this frame
    // (directinput::TakeSkillPanelToggle(), taken by the caller every frame; used only in the long-press mode).
    // `host`: the Settings tab's side (null: no tab strip, no settings, the window as it was).
    Step Frame(const directinput::Frame& f, const Snapshot& snap, uint64_t nowMs, const Reply& reply, bool f4 = true, bool toggleRequest = false,
               const SettingsIn* host = nullptr) {
        using namespace directinput;
        Step out;
        out.layer.widthM = PanelWidthM(cfg_);
        out.layer.heightM = PanelHeightM(cfg_);
        out.layer.style = cfg_.style;
        if (f.session != session_) { session_ = f.session; Reset(); }
        if (!f4) {   // another view: nothing of ours acts. Whatever was under way ends (the panel closes, the pick is forgotten, the controls
                     // it held are let go, no press, no flag), and F4 starts fresh
            if (!standing_) { Reset(); standing_ = true; }
            return out;
        }
        standing_ = false;
        const bool focused = (f.flags & kFocused) != 0, inMenu = (f.flags & kInMenu) != 0;
        const bool allowed = focused && !inMenu;
        const bool tabsOn = TabsOn(cfg_) && host != nullptr;
        const bool measuring = tabsOn && host->calibrating;   // the T-pose measurement has the hands: no window, whatever is asked
        const bool allowedUi = allowed && !measuring;         // (the cast and the pointer are `allowed`'s: they are not the window's life)
        const int P = cfg_.pointerHand, M = 1 - P;
        const Hand& hp = f.hand[P];
        const Hand& hm = f.hand[M];

        for (int h = 0; h < 2; ++h) gripDown_[h] = f.hand[h].squeeze > (gripDown_[h] ? 0.40f : 0.60f);
        const bool stick = (hp.buttons & kStick) != 0;
        const bool stickEdge = stick && !stickWas_;
        stickWas_ = stick;
        const bool trig = hp.trigger > (triggerWas_ ? kTriggerOff : kTriggerOn);
        const bool click = trig && !triggerWas_;
        triggerWas_ = trig;
        const uint32_t closers = kSecondary | kMenuButton;
        const bool backNow = ((hp.buttons | hm.buttons) & closers) != 0;
        const bool back = backNow && !backWas_;
        backWas_ = backNow;

        // the chord is the way in mode 0, and the fallback in mode 1 where the plugin is not the input (the long press cannot come then)
        const bool chordOk = cfg_.openMode == 0 || (f.flags & kDirect) == 0;
        const bool chordHeld = chordOk && gripDown_[M] && !gripDown_[P] && hm.active && hp.active;
        // an unplaced wheel is placed where the head looks as it opens; no head, no wheel
        const bool wasOpen = model_.Open();
        // the long press of the right stick click closes it again: the click is the panel's while it is up, so it is timed here (only after
        // it has been let go once: the press that opened it is still down)
        bool holdClose = false;
        const bool rightStick = (f.hand[1].buttons & kStick) != 0;
        if (!wasOpen || cfg_.openMode != 1) {
            closeArmed_ = false;
            closeHoldAt_ = -1.0;
        } else if (!rightStick) {
            closeArmed_ = true;
            closeHoldAt_ = -1.0;
        } else if (closeArmed_) {
            if (closeHoldAt_ < 0.0) closeHoldAt_ = f.time;
            else if (f.time - closeHoldAt_ >= cfg_.holdS) { holdClose = true; closeArmed_ = false; closeHoldAt_ = -1.0; }
        }
        const bool request = cfg_.openMode == 1 && toggleRequest;
        // the Settings tab's own chord: both grips squeezed a third of a second, then a thumbstick click (the settings panel's old way in)
        bool chordArmed = false, settingsChord = false;
        if (tabsOn && host->chordOn) {
            const float squeeze[2] = {f.hand[0].squeeze, f.hand[1].squeeze};
            const bool sticks[2] = {(f.hand[0].buttons & kStick) != 0, (f.hand[1].buttons & kStick) != 0};
            const hset::Chord::Out ch = chord_.Step(f.time * 1000.0, allowedUi && f.hand[0].active && f.hand[1].active, squeeze, sticks);
            chordArmed = ch.armed;
            settingsChord = ch.armed && ch.edge;
        } else {
            chord_.Reset();
        }
        const Tab tabNow = model_.Current().tab;
        const bool topLevel = model_.Current().page == Page::Wheel;
        int setTab = -1;
        bool chordToggle = settingsChord;
        if (settingsChord && wasOpen && tabNow == Tab::Skills && topLevel) {   // up on the tiles: the chord brings the settings
            chordToggle = false;
            setTab = int(Tab::Settings);
        }
        const bool resumeNow = tabsOn && resume_ && !wasOpen && allowedUi && f.head.valid == 1;   // Calibrate body: the measurement is over
        const bool toggle = allowedUi && (wasOpen || f.head.valid == 1) &&   // (a held place is not a place to hang it)
                            ((stickEdge && chordHeld) || request || holdClose || chordToggle || resumeNow);
        if (toggle && !wasOpen) panel_ = PanelFor(f.head, cfg_);

        // the pointing hand's ray against the panel
        bool pointer = false;
        PanelHit ph;
        V3 from, dir;
        const bool aimOk = hp.aim.valid == 1 && allowed;
        if (aimOk) {
            from = V3{hp.aim.pos[0], hp.aim.pos[1], hp.aim.pos[2]};
            const Q aq{hp.aim.rot[0], hp.aim.rot[1], hp.aim.rot[2], hp.aim.rot[3]};
            if (Finite(from) && Finite(aq)) {
                dir = Unit(Rot(aq, V3{0.0f, 0.0f, -1.0f}));
                ph = IntersectPanel(from, dir, panel_, PanelWidthM(cfg_), PanelHeightM(cfg_));
                pointer = ph.hit;
            }
        }

        // the Settings tab: its pages follow the ini and the picture, take the beam and the trigger, and may ask for the window to close
        hset::Hit settingsHover;
        hset::Action settingsAct;
        bool closeAsked = false;
        if (tabsOn && wasOpen) {
            settings_.SetEnv(host->env);
            settings_.SetView(host->view);
            settings_.Observe(host->ini);
            if (tabNow == Tab::Settings && !toggle) {
                float su = 0.0f, sv = 0.0f;
                if (ph.dist > 0.0f) tiles::SheetPoint(ph.u, ph.v, &su, &sv);
                if (pointer) settingsHover = hset::HitTest(settings_.page, su, sv);
                settingsAct = clicker_.Step(settings_, settingsHover, pointer, click, trig, ph.dist > 0.0f, su * float(tiles::kSheetW), f.time * 1000.0);
                if (settingsAct.what == hset::Do::Close || settingsAct.what == hset::Do::Calibrate || settingsAct.what == hset::Do::View) closeAsked = true;
                if (settingsAct.what == hset::Do::Calibrate) resume_ = true;
            }
            if (settingsAct.what == hset::Do::Nothing) settingsAct = settings_.Live(f.time * 1000.0);   // the belt writes itself a moment after the last change
        }

        // a flick or a tap (the window up on the top level, the tab strip there): one place along the pages
        int flick = 0;
        if (tabsOn && wasOpen && allowedUi) {
            const float sx[2] = {f.hand[0].stick[0], f.hand[1].stick[0]}, sy[2] = {f.hand[0].stick[1], f.hand[1].stick[1]};
            const float sq[2] = {f.hand[0].squeeze, f.hand[1].squeeze};
            flick = flip_.Step(f.time, sx, sy, sq);
            if (!topLevel) flick = 0;   // (the editor's pages have their own buttons)
        } else {
            flip_.Reset();
        }

        In in;
        in.time = f.time;
        in.allowed = allowedUi;
        in.tabs = tabsOn;
        in.openTab = settingsChord || resumeNow ? int(Tab::Settings) : -1;
        in.setTab = setTab;
        in.close = closeAsked;
        in.session = f.session;
        in.epoch = f.epoch;
        in.toggle = toggle;
        in.back = back;
        in.pointer = pointer;
        in.u = ph.u;
        in.v = ph.v;
        in.click = click;
        in.nowMs = nowMs;
        in.snap = &snap;
        in.reply = reply;
        in.style = cfg_.style;
        in.hints = &cfg_.hints;
        in.categories = cfg_.categories;
        in.runWalk = cfg_.runWalk;
        in.slotControls = cfg_.slotControls;
        const Out mo = model_.Step(in);
        out.feedback = mo.feedback;
        out.request = mo.request;
        out.runWalk = mo.runWalk;
        out.req = mo.req;
        const bool open = model_.Open();
        if (tabsOn) {
            if (open && wasOpen && model_.Current().page == Page::Wheel && !closeAsked) {   // the flips: a flick, a tap, a click on an arrow
                const int step = flick != 0 ? flick : mo.flip;
                const int here = FlipPlace(), there = FlipTo(here, step);
                if (step != 0 && there != here) {
                    model_.SetTab(there == 0 ? Tab::Skills : Tab::Settings);
                    if (there > 0) settings_.Click({hset::Kind::Tab, there - 1, 0});
                    clicker_.Reset();
                    if (out.feedback == Feedback::None) out.feedback = Feedback::Hover;
                }
            }
            if (open && !wasOpen) {   // the pages start from the ini as it is now (or, back from the measurement, on the Body page with the draft kept)
                if (resumeNow) {
                    settings_.Resume(host->ini);
                    resume_ = false;
                } else {
                    settings_.Open(host->ini);
                }
                settings_.SetEnv(host->env);
                settings_.SetView(host->view);
                clicker_.Reset();
            } else if (!open && wasOpen) {
                settings_.Close();
                clicker_.Reset();
            }
            model_.SetSettings(settings_, open && model_.Current().tab == Tab::Settings ? settingsHover : hset::Hit{});
            model_.SetPlace(open ? FlipPlace() : 0);
            out.settings = settingsAct;
            if (settingsAct.what == hset::Do::Save && out.feedback == Feedback::None) out.feedback = Feedback::Applied;
            out.wantStore = open || resume_ || chord_.Arming() || toggle;
        }

        // the trigger's cast, while the wheel is closed
        CastGate::In ci;
        ci.time = f.time;
        ci.slot = model_.Selected();
        ci.trigger = hp.trigger;
        ci.allowed = allowed && !open && !castBlocked_;
        const CastGate::Out co = cast_.Step(ci);
        out.casting = co.casting;
        out.buttons = co.buttons;
        out.lt = co.lt;

        // the right trigger and the right A as the game's RT and A slots ([openxr] skill_rt_a): direct input in F4, the panel closed, the window not
        // measuring; the trigger is the picked slot's while one is picked (or the bow's, or a gesture's), the A a gesture's when one holds it
        uint32_t slotTake[2] = {0, 0};
        if (cfg_.slotControls) {
            SlotControls::In si;
            si.time = f.time;
            si.allowed = allowedUi && !open && !wasOpen && (f.flags & (kDirect | kUiUp | kOtherView)) == kDirect;
            si.triggerFree = !castBlocked_ && !triggerTaken_ && model_.Selected() < 0 && !co.casting;
            si.aFree = !aTaken_ && !co.casting;
            si.trigger = f.hand[1].trigger;
            si.leftTrigger = f.hand[0].trigger;
            si.a = (f.hand[1].buttons & kPrimary) != 0;
            si.gripHeld = gripDown_[1];
            si.skillMask = SkillMask(snap, nowMs, f.session);
            const SlotControls::Out so = slots_.Step(si);
            triggerSkill_ = si.allowed && si.triggerFree && !si.gripHeld && (si.skillMask & (1u << SlotOfControl(Control::Trigger, slots_.LeftDown()))) != 0;
            if (so.casting) {
                out.casting = true;
                out.buttons |= so.buttons;
                out.lt = std::max(out.lt, so.lt);
                out.rt = so.rt;
                if (so.takeA) slotTake[1] |= kPrimary;
                if (so.takeTrigger) slotTake[1] |= kTrigger | kTakeTrigger;
                if (so.takeLeftTrigger) slotTake[0] |= kTrigger | kTakeTrigger;
            }
            out.slotStarted[0] = so.started[0];
            out.slotStarted[1] = so.started[1];
        } else {
            slots_.Reset();
            triggerSkill_ = false;
        }

        // what is taken from the layout: the wheel's controls, the cast's triggers, the chord's click, then what was held
        for (int h = 0; h < 2; ++h) {
            const Hand& hd = f.hand[h];
            Held& g = gate_[h];
            const bool pushed = std::max(std::fabs(hd.stick[0]), std::fabs(hd.stick[1])) > kFlickOff;   // (the stick is out of the middle)
            if (open || wasOpen) {   // (the frame it closes in too: the click that closed it is still down)
                g.buttons = hd.buttons & kWheelButtons;
                g.trigger = hd.trigger > kTriggerOff;
                g.squeeze = hd.squeeze > kTriggerOff;
                g.stick = tabsOn && pushed;
                if (open) out.take[h] |= kWheelButtons | kTakeTrigger | kTakeSqueeze | (tabsOn ? kTakeStick : 0u);
            } else {
                g.buttons &= hd.buttons;
                g.trigger = g.trigger && hd.trigger > kTriggerOff;
                g.squeeze = g.squeeze && hd.squeeze > kTriggerOff;
                g.stick = g.stick && pushed;   // (a stick flicked as it closed does not walk the hero off until it is back in the middle)
            }
        }
        if (!open && allowed && chordOk && gripDown_[M] && !gripDown_[P]) {   // the chord is made: its click is not the game's
            out.take[P] |= kStick;
            if (stick) gate_[P].buttons |= kStick;
        }
        if (!open && chordArmed) {   // the Settings tab's chord is armed: both thumbstick clicks are its, not the game's (a swap, Alt)
            for (int h = 0; h < 2; ++h) {
                out.take[h] |= kStick;
                if (f.hand[h].buttons & kStick) gate_[h].buttons |= kStick;
            }
        }
        if (co.takeTrigger) out.take[P] |= kTakeTrigger;
        if (co.takeLeftTrigger) out.take[0] |= kTakeTrigger;
        for (int h = 0; h < 2; ++h) out.take[h] |= Mask(gate_[h]) | slotTake[h];

        // the headset layer
        out.layer.show = open;
        out.layer.panel = panel_;
        if (open && aimOk && Finite(from)) {
            out.layer.beam = true;
            out.layer.from = from;
            out.layer.dir = dir;
            out.layer.hit = ph.dist > 0.0f && ph.hit;
            out.layer.at = ph.point;
            out.layer.len = out.layer.hit ? ph.dist : kBeamM;
        }
        return out;
    }
};

}  // namespace wheel
