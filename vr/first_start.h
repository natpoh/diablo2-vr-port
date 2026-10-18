#pragma once
// first_start.h - the first-start guide ([onboarding] in d2r_vr.ini): when it comes up in the headset, which page it shows, how it is paged
// and closed, and when it is done. Pure (no Windows, no OpenXR, no ini), so cleanroom/tests/first_start_test.cpp runs it without a headset.
// The wiring is vr/first_start.cpp (the ini, the input frame, the T-pose measurement), the pictures vr/first_start_panel.cpp, the layer xr.cpp.
//
// What it does. The first time the player stands in the world in VR first person (F4) with the plugin's own controls (native OpenXR, [openxr]
// direct_input), and the guide is not done yet:
//   1. without a stored measurement ([calibration] version=1) it offers the T-pose measurement (vr/calibration.h) first: the right stick
//      flicked up measures now, flicked down goes on without it (the headset panel's Body page has it later);
//   2. then a few pages of what to press to open what (the inventory, the skill panel, the map, the belt, the settings panel, the pause
//      menu, the F views). The right stick flicked up goes on, flicked down goes back; on the last page up closes it. The right stick held
//      up for a second closes it on any page.
// Finished or closed, it is done: [onboarding] done=1, and it does not come up by itself again. The headset settings panel's Body page
// ("First-start guide") opens it again on its first page; D2R VR Authentic Settings > Body unticks "First-start guide seen" to have it at the
// next start; "Show the first-start guide" unticked ([onboarding] show=0) keeps it from coming up by itself at all.
//
// It NEVER takes a control and never presses anything: the game runs on behind it, and every control does what it always does. It only
// READS the right thumbstick's up and down, which nothing in the world uses: with [turn] right_stick=1 (the default) the game never gets the
// right stick, and the plugin turns with its x alone (vrcam.cpp, the pad hook). With right_stick=0 the game has the stick, and the guide stays
// away (Context::stickOurs). Not the grips: with Weapon Weight on, both grips let go drop the held weapon. It stands down - hidden, its page
// kept - while a menu or a panel of the game is open, a panel of the plugin's own is up (the skill panel, the settings panel), the
// measurement runs, the headset has no input focus, or the view is not F4; a stick pushed as it comes back counts only once it has been
// back in the middle.
//
// The idea of offering the T-pose measurement on the first start is bearstonem/d2r-pcvr's (MIT: steamvr/transport.cpp, the first
// focused world frame without a stored calibration opens it; steamvr/BODY_CALIBRATION.md). The pages and this flow are ours.

#include <cmath>

namespace firststart {

// The pages: 0 the measurement's offer, 1 .. kGuidePages the guide. One picture a page (first_start_panel.cpp).
constexpr int kOfferPage = 0;
constexpr int kGuidePages = 5;
constexpr int kPageCount = 1 + kGuidePages;

// How long the context must hold before the guide comes up by itself (a loading screen, a town portal: not for a moment), and again after it
// was out of sight; how short a push of the stick is a flick, and how long held up closes it.
constexpr double kSettleS = 4.0;
constexpr double kQuickSettleS = 0.5;    // back from a menu, the measurement or the headset panel's button
constexpr double kFlickMaxS = 0.6;
constexpr double kCloseHoldS = 1.0;
constexpr double kMeasureStartS = 3.0;   // a measurement asked for that never shows up (no floor yet) is not waited for longer
// A push counts with the stick this far up or down and not much to the side (a turn is the side), and ends back inside the middle.
constexpr float kPushAt = 0.7f, kSideMax = 0.5f, kMiddle = 0.3f;

// [onboarding] done as the ini has it: -1 = the key is not there (an ini from before 0.171). Then the guide counts as done for a player who
// has a stored measurement already (one who has played in F4) and not done for anyone else.
inline bool DoneOf(int doneKey, bool calibrated) { return doneKey < 0 ? calibrated : doneKey != 0; }

// The frame's context, from the input layer's frame (vr/direct_input.h), the measurement and the ini.
struct Context {
    double time = 0;          // seconds, monotonic
    bool focused = false;     // the session has the input focus
    bool f4 = false;          // VR first person (not kOtherView)
    bool direct = false;      // the plugin's own controls feed the game (kDirect): the pages describe them
    bool stickOurs = false;   // [turn] right_stick: the game never gets the right stick
    bool menu = false;        // a menu or panel of the game is open, or there is no game yet (kInMenu)
    bool uiUp = false;        // a panel of the plugin's own hangs in the headset (kUiUp)
    bool measuring = false;   // the T-pose measurement runs (calib::Running)
    float stick[2] = {};      // the right thumbstick: x right, y up, -1..1
};
inline bool Shown(const Context& c) { return c.focused && c.f4 && c.direct && c.stickOurs && !c.menu && !c.uiUp && !c.measuring; }

enum class Phase { Idle, Offer, Measuring, Pages, Closed };
enum class End { None, Finished, Skipped };

struct Out {
    bool show = false;      // the panel hangs this frame
    int page = 0;           // which picture (kOfferPage, 1 .. kGuidePages)
    bool measure = false;   // ask for the T-pose measurement now (once)
    End end = End::None;    // the guide ended this frame: write [onboarding] done=1
};

class Guide {
public:
    // The ini, on every read of it. show: [onboarding] show; done: DoneOf(...); calibrated: a measurement is stored.
    void Configure(bool show, bool done, bool calibrated) {
        const bool wasDone = done_;
        show_ = show;
        done_ = done;
        calibrated_ = calibrated;
        if (done) {
            endedStale_ = false;   // the file has our done=1 now (or someone set it)
            if (!wasDone && !reopened_ && Up()) Close();   // ticked "seen" in D2R VR Settings while it was up
        } else if (!endedStale_ && phase_ == Phase::Closed) {
            phase_ = Phase::Idle;   // "First-start guide seen" unticked: it comes up at the next chance
        }
        if (!show_ && !reopened_ && Up()) Close();   // "Show the first-start guide" unticked while it was up
    }
    // The headset panel's "First-start guide": the guide's pages from the first, now (the measurement is on the same page as the button).
    void Reopen() {
        phase_ = Phase::Pages;
        page_ = 1;
        reopened_ = true;
        Hide(kQuickSettleS);
    }

    Phase phase() const { return phase_; }
    int page() const { return page_; }

    Out Step(const Context& c) {
        Out o;
        if (phase_ == Phase::Idle) {
            if (!show_ || done_ || endedStale_) return o;
            phase_ = calibrated_ ? Phase::Pages : Phase::Offer;
            page_ = calibrated_ ? 1 : kOfferPage;
            Hide(kSettleS);
        }
        if (phase_ == Phase::Closed) return o;
        if (phase_ == Phase::Measuring) {
            if (c.measuring) measureSeen_ = true;
            const bool over = measureSeen_ ? !c.measuring : c.time - measureAt_ > kMeasureStartS;
            if (!over) return o;
            phase_ = Phase::Pages;   // measured, cancelled or never started: on to the guide
            page_ = 1;
            Hide(kQuickSettleS);
        }
        if (!Shown(c)) {   // hidden, its page kept; the context must hold again before it shows
            if (!hidden_) Hide(kQuickSettleS);
            visibleSince_ = -1;
            return o;
        }
        if (visibleSince_ < 0) visibleSince_ = c.time;
        if (c.time - visibleSince_ < settle_) return o;
        if (hidden_) {   // it comes up (again): a stick pushed now counts once it has been back in the middle
            hidden_ = false;
            push_ = 0;
            armed_ = Middle(c);
        }
        int flick = 0;   // +1 up, -1 down
        const float x = std::isfinite(c.stick[0]) ? c.stick[0] : 0.0f, y = std::isfinite(c.stick[1]) ? c.stick[1] : 0.0f;
        if (push_ == 0) {
            if (armed_ && std::fabs(x) <= kSideMax && std::fabs(y) >= kPushAt) {
                push_ = y > 0 ? 1 : -1;
                pushAt_ = c.time;
                spent_ = false;
            }
        } else if (Middle(c)) {
            if (!spent_ && c.time - pushAt_ <= kFlickMaxS) flick = push_;
            push_ = 0;
        } else if (push_ > 0 && !spent_ && c.time - pushAt_ >= kCloseHoldS) {
            spent_ = true;   // held up: closed, and the push ends with nothing more when the stick comes back
            o.end = End::Skipped;
            Close();
            return o;
        }
        if (Middle(c)) armed_ = true;
        if (phase_ == Phase::Offer) {
            if (flick > 0) {   // measure now
                phase_ = Phase::Measuring;
                measureAt_ = c.time;
                measureSeen_ = false;
                o.measure = true;
                return o;
            }
            if (flick < 0) {   // not now: on to the guide
                phase_ = Phase::Pages;
                page_ = 1;
            }
        } else if (flick > 0) {
            if (page_ >= kGuidePages) {
                o.end = End::Finished;
                Close();
                return o;
            }
            ++page_;
        } else if (flick < 0 && page_ > 1) {
            --page_;
        }
        o.show = true;
        o.page = page_;
        return o;
    }

private:
    static bool Middle(const Context& c) {
        const float x = std::isfinite(c.stick[0]) ? c.stick[0] : 0.0f, y = std::isfinite(c.stick[1]) ? c.stick[1] : 0.0f;
        return std::sqrt(x * x + y * y) <= kMiddle;
    }
    bool Up() const { return phase_ == Phase::Offer || phase_ == Phase::Pages; }
    void Close() {
        if (phase_ != Phase::Closed) endedStale_ = !done_;   // until the file says done=1, a stale done=0 does not bring it back
        phase_ = Phase::Closed;
        reopened_ = false;
        Hide(kSettleS);
    }
    // Out of sight until the context has held for `settle` seconds again.
    void Hide(double settle) {
        hidden_ = true;
        visibleSince_ = -1;
        settle_ = settle;
    }

    Phase phase_ = Phase::Idle;
    int page_ = 0;
    bool show_ = true, done_ = false, calibrated_ = false;
    bool reopened_ = false;     // opened from the headset panel: it shows whatever show and done say
    bool endedStale_ = false;   // ended this session; the ini has not said done=1 since
    bool hidden_ = true;
    double visibleSince_ = -1;
    double settle_ = kSettleS;
    double measureAt_ = 0;
    bool measureSeen_ = false;
    int push_ = 0;              // the stick is pushed: +1 up, -1 down, 0 in the middle (or to the side)
    double pushAt_ = 0;
    bool spent_ = false;        // this push closed the guide already
    bool armed_ = false;        // the stick has been in the middle since the guide came up
};

}  // namespace firststart
