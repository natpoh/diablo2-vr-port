#pragma once
// The in-headset settings panel's hands: the chord that opens it (both grips squeezed a third of a second, then a
// thumbstick click - the one silent control in the layouts), the laser against its plane, clicks,
// what the rest of the mod is allowed to see of the controllers while it is up, and the release gate
// after it closes. Pure (no Windows, no OpenXR): a frame of input in, a frame of output out, so a test
// drives it with made-up poses (tools/hset_test.cpp). vr/xr_headset_settings.h feeds it from the
// session's actions.
//
// It never sends anything to the game - no mouse, no key. While it is open the controllers' buttons,
// triggers, grips and sticks it hands on are all zero (the poses go on); after it closes each control
// that was held at that moment stays at zero until it is let go (a click on Close must not be a click
// in the game). After bearstonem/d2r-pcvr's InputGate / ReleaseGate (MIT), written again for this layout.

#include <cmath>
#include <cstdint>

#include "hset_click.h"
#include "hset_model.h"
#include "hset_ray.h"

namespace hset {

struct FrameIn {
    double ms = 0;                 // a clock, milliseconds
    bool enabled = false;          // [openxr] headset_settings
    bool focused = false;          // the session has the input focus
    Pose head;                     // between the eyes, in the panel's space
    Pose aim[2];                   // each hand's laser, in the panel's space (valid: tracked)
    bool present[2] = {false, false};   // a controller is in the hand
    Pad pad[2];                    // as pressed
    int laserHand = 1;             // [openxr] laser_hand: the hand the beam starts on
    Snapshot snapshot;             // the ini as the store reads it now
    bool calibrating = false;      // the T-pose measurement runs (or is about to): the hands are its, the chord is ignored, and a panel that
                                   // handed over to it comes back when this goes false
    video::Env env;                // the picture's sizes, for the Video page
    int view = 0;                  // the game's view now (1 = F1 ... 4 = F4, 0 = not known), for the lit button of the View row
    int openGuide = -1;            // the first-start guide asks the panel to come up on its Guide page: the page to start on (vr/first_start.h), -1 none
};

struct FrameOut {
    Pad pad[2];                    // what the rest of the mod may see
    bool open = false;
    bool justOpened = false, justClosed = false;
    Action action;                 // Save / Recentre / Close to carry out (Close is done already; for the log)
    bool guideOpened = false;      // it came up on the guide's page, as asked (openGuide)
    int guideEnd = 0;              // it closed with the Guide page having been on show: 1 Done was pressed, 2 it was closed otherwise
    Pose panel;                    // where it stands
    bool beam = false;             // draw a beam from `hand`
    bool hit = false;              // and a dot where it meets the panel
    int hand = 1;
    V3 from, dir, at;
    float len = kBeamM;
};

class Controller {
    ReleaseGate gate_;
    bool open_ = false;
    Pose panel_;
    Chord chord_;                             // both grips a third of a second, then a thumbstick click
    Clicker clicker_;                         // the trigger on a button: a click, a repeat, the slider's drag
    bool down_[2] = {false, false};           // a hand's trigger as of the frame before
    uint32_t prevButtons_[2] = {0, 0};
    bool primed_ = false;                     // the triggers were let go since it opened
    int ptr_ = 1;
    bool resume_ = false;                     // "Calibrate body" closed it: it comes back on the Body page when the measurement is done

    void Close(const Pad raw[2], FrameOut* o, Do how = Do::Close) {
        if (model.GuideShown() && how != Do::Calibrate) o->guideEnd = model.GuideFinished() ? 1 : 2;   // (a measurement's hiding is no end: the panel comes back)
        model.Close();
        open_ = false;
        gate_.Arm(raw);
        hover = {};
        clicker_.Reset();
        o->justClosed = true;
    }
public:
    Model model;
    Hit hover;

    bool Open() const { return open_; }
    // Something of it is going on: up, both grips squeezed (the chord is armed), or controls still held back.
    bool Busy() const { return open_ || chord_.Arming() || gate_.Active() || resume_; }
    // Closed for a measurement, and will come back.
    bool Resuming() const { return resume_; }
    const Pose& PanelPose() const { return panel_; }

    // The store's answer to an Apply, or its latest reading.
    void Observe(const Snapshot& s) { model.Observe(s); }
    void SubmitFailed(uint64_t sequence) { model.SubmitFailed(sequence); }

    FrameOut Step(const FrameIn& in) {
        FrameOut o;
        for (int h = 0; h < 2; ++h) o.pad[h] = in.pad[h];
        o.hand = ptr_;
        // The chord: both grips squeezed for 0.35 s, then a thumbstick click (either). A grip alone does nothing in the game's
        // layouts, so from the moment the chord is armed the thumbstick clicks are the panel's, not the game's - the first
        // click cannot leak (the clicks do swap weapons, Alt, recentre there). A click inside the 0.35 s, or with one grip,
        // is the game's as it always was.
        // While the T-pose measurement runs the panel is not to be had (its prompt needs the triggers and the room): the chord is
        // ignored, an open panel closes. The key off, or another view than F4, forgets that it was to come back.
        if (!in.enabled) resume_ = false;
        const bool enabled = in.enabled && !in.calibrating;
        const float squeeze[2] = {in.pad[0].squeeze, in.pad[1].squeeze};
        const bool stick[2] = {(in.pad[0].buttons & kStick) != 0, (in.pad[1].buttons & kStick) != 0};
        const Chord::Out chord = chord_.Step(in.ms, enabled, squeeze, stick);
        const bool armed = chord.armed, clickEdge = chord.edge;

        if (!enabled) {
            if (open_) Close(in.pad, &o);
            gate_.Apply(o.pad);
            return o;
        }
        if (!open_) {
            if (armed) { gate_.Hold(0, kStick); gate_.Hold(1, kStick); }   // (a click already down stays the game's own, let go)
            const bool back = resume_ && in.head.valid;   // the measurement is done: the panel stands in front of the player again
            const bool guide = !back && in.openGuide >= 0 && in.head.valid && in.snapshot.ready;   // the first-start guide asks for it (the ini is read: the offer can measure)
            if (back || guide || (armed && clickEdge && in.head.valid)) {
                open_ = true;
                panel_ = {};
                const Q q = YawQ(YawOf(in.head.q));
                panel_.q = q;
                panel_.p = Add(in.head.p, Rot(q, {0.0f, -kBelowM, -kAheadM}));
                panel_.valid = true;
                if (back) model.Resume(in.snapshot); else model.Open(in.snapshot);
                if (guide) {
                    model.OpenGuide(in.openGuide);
                    o.guideOpened = true;
                }
                resume_ = false;
                hover = {};
                clicker_.Reset();
                ptr_ = in.laserHand ? 1 : 0;
                primed_ = false;
                for (int h = 0; h < 2; ++h) {
                    down_[h] = in.pad[h].trigger > kPressAt;
                    prevButtons_[h] = in.pad[h].buttons;
                }
                gate_.Clear();
                o.justOpened = true;
            }
        }
        if (!open_) {
            gate_.Apply(o.pad);
            return o;
        }

        // -- open: the panel has both hands ------------------------------------------------------
        o.open = true;
        o.panel = panel_;
        model.SetEnv(in.env);
        model.SetView(in.view);
        model.Observe(in.snapshot);

        // closed from here: its controls stay zero until let go
        auto shut = [&](Do how) {
            o.action.what = how;
            Close(in.pad, &o, how);
            o.open = false;
            for (int h = 0; h < 2; ++h) o.pad[h] = in.pad[h];
            gate_.Apply(o.pad);
            return o;
        };

        if (armed && clickEdge && !o.justOpened) return shut(Do::Close);   // the chord again (a click that opened it is not one)

        // the buttons that close it: B / Y or Menu, either hand, a fresh press
        bool closeNow = false;
        for (int h = 0; h < 2; ++h) {
            const uint32_t fresh = in.pad[h].buttons & ~prevButtons_[h];
            if (fresh & (kSecondary | kMenu)) closeNow = true;
            prevButtons_[h] = in.pad[h].buttons;
        }

        // the lasers against the panel
        PanelHit ph[2];
        for (int h = 0; h < 2; ++h) {
            if (!in.present[h] || !in.aim[h].valid) continue;
            ph[h] = IntersectPanel(in.aim[h].p, Unit(Rot(in.aim[h].q, {0.0f, 0.0f, -1.0f})), panel_, kWidthM, kHeightM);
        }
        bool rise[2] = {false, false}, held[2] = {false, false};
        for (int h = 0; h < 2; ++h) {
            const float t = in.pad[h].trigger;
            const bool dn = in.focused && t > (down_[h] ? kReleaseAt : kPressAt);
            rise[h] = dn && !down_[h];
            held[h] = dn;
            down_[h] = dn;
        }
        if (!primed_ && !held[0] && !held[1]) primed_ = true;
        // the hand that pulls its trigger while it points at the panel takes the beam
        for (int h = 0; h < 2; ++h) if (primed_ && rise[h] && ph[h].hit) ptr_ = h;
        o.hand = ptr_;
        hover = ph[ptr_].hit ? HitTest(model.page, ph[ptr_].u, ph[ptr_].v) : Hit{};

        Action act;
        if (primed_ && in.focused) {
            act = clicker_.Step(model, hover, ph[ptr_].hit, rise[ptr_], held[ptr_], ph[ptr_].dist > 0.0f, ph[ptr_].u * (float)kW, in.ms);
        }
        if (act.what == Do::Nothing) act = model.Live(in.ms);   // the belt writes itself a moment after the last change
        if (act.what == Do::Calibrate) {   // the hands go to the measurement; the panel comes back when it is done
            FrameOut r = shut(Do::Calibrate);
            resume_ = true;
            return r;
        }
        if (act.what == Do::View) {   // another view of the game: the panel closes with it (what is held stays the panel's until let go)
            FrameOut r = shut(Do::View);
            r.action.view = act.view;
            return r;
        }
        if (act.what == Do::GuideDone) return shut(Do::GuideDone);   // the guide's last page: the panel closes, the guide is over
        if (act.what == Do::Close || closeNow) return shut(Do::Close);
        o.action = act;

        // everything the rest of the mod would read is zero while the panel is up (poses are not in a Pad)
        for (int h = 0; h < 2; ++h) o.pad[h] = Pad{};

        const int p = ptr_;
        if (in.present[p] && in.aim[p].valid && in.focused) {
            o.beam = true;
            o.from = in.aim[p].p;
            o.dir = Unit(Rot(in.aim[p].q, {0.0f, 0.0f, -1.0f}));
            o.hit = ph[p].hit;
            o.at = ph[p].point;
            o.len = ph[p].hit ? ph[p].dist : kBeamM;
        }
        return o;
    }
};

}  // namespace hset
