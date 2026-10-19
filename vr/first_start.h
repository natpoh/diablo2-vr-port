#pragma once
// first_start.h - the first-start guide ([onboarding] in d2r_vr.ini): WHEN it comes up. The guide itself is a page of the headset settings -
// the "Guide" tab of the Settings tab of the skill panel's window (the long press of the right stick click) or of the settings panel of its
// own: it is drawn by the same code as every other page (vr/hset_scene.h), hangs where that window hangs, and is paged with the laser
// (Back / Next) like the other pages. Pure (no Windows, no OpenXR, no ini), so cleanroom/tests/first_start_test.cpp runs it without a headset.
// The wiring is vr/first_start.cpp (the ini, the input frame, the request to the window), the pages' words vr/first_start_pages.h, the page's
// picture vr/hset_scene.h, the window vr/wheel_controller.h (or vr/hset_panel.h).
//
// What it does. The first time the player stands in the world in VR first person (F4) with the plugin's own controls (native OpenXR, [openxr]
// direct_input), and the guide is not done yet, it ASKS THE WINDOW to come up on Settings > Guide:
//   - without a stored measurement ([calibration] version=1) on the page that offers the T-pose measurement first (Measure now / Not now: the
//     window hides for the measurement and comes back on the guide's first page);
//   - otherwise on the first page: a few pages of what to press to open what (the inventory, the skill panel, the map, the belt, the settings,
//     the pause menu, the F views). Next / Back with the laser; the last page's Done ends it.
// Finished or closed (the window closed while the guide was on show), it is done: [onboarding] done=1, and it does not come up by itself again.
// The Body page's "First-start guide" button (or the Guide tab) shows it again, from its first page, in the same window;
// D2R VR Authentic Settings > Body unticks "First-start guide seen" to have it at the next start; "Show the first-start guide" unticked
// ([onboarding] show=0) keeps it from coming up by itself at all.
//
// It takes nothing and presses nothing: the window is the skill panel's / the settings panel's own (it takes the hands while it is up, as it
// always does, and lets them go as it closes). The game runs on behind it. It waits - the window is not asked for - while a menu or a panel of
// the game is open, a panel of the plugin's own is up, the measurement runs, the headset has no input focus, or the view is not F4; and
// while no window is there to host it (the skill panel's Settings tab, or the settings panel, switched off).
//
// The idea of offering the T-pose measurement on the first start is bearstonem/d2r-pcvr's (MIT: steamvr/transport.cpp, the first
// focused world frame without a stored calibration opens it; steamvr/BODY_CALIBRATION.md). The pages and this flow are ours.

#include <cmath>

namespace firststart {

// The pages: 0 the measurement's offer, 1 .. kGuidePages the guide.
constexpr int kOfferPage = 0;
constexpr int kGuidePages = 5;
constexpr int kPageCount = 1 + kGuidePages;

// How long the context must hold before the guide asks for the window (a loading screen, a town portal: not for a moment; the count starts
// again after a menu); how long it waits for the window to show up before it asks again, and how often it tries.
constexpr double kSettleS = 4.0;
constexpr double kOpenWaitS = 3.0;   // ... for the window to show up after the request
constexpr double kLostS = 2.0;       // ... and how long a window that was up and is gone without a report (another view, a new session) is waited for
constexpr int kMaxAttempts = 3;

// [onboarding] done as the ini has it: -1 = the key is not there (an ini from before 0.171). Then the guide counts as done for a player who
// has a stored measurement already (one who has played in F4) and not done for anyone else.
inline bool DoneOf(int doneKey, bool calibrated) { return doneKey < 0 ? calibrated : doneKey != 0; }

// The frame's context, from the input layer's frame (vr/direct_input.h), the measurement and the windows.
struct Context {
    double time = 0;          // seconds, monotonic
    bool focused = false;     // the session has the input focus
    bool f4 = false;          // VR first person (not kOtherView)
    bool direct = false;      // the plugin's own controls feed the game (kDirect): the pages describe them
    bool menu = false;        // a menu or panel of the game is open, or there is no game yet (kInMenu)
    bool uiUp = false;        // a panel of the plugin's own hangs in the headset (kUiUp, or the settings panel of its own)
    bool measuring = false;   // the T-pose measurement runs (calib::Running)
    bool host = false;        // a window to host the guide is there: the skill panel's Settings tab, or the settings panel of its own
};
inline bool Shown(const Context& c) { return c.focused && c.f4 && c.direct && c.host && !c.menu && !c.uiUp && !c.measuring; }

enum class Phase { Idle, Waiting, Hosted, Closed };
enum class End { None, Finished, Skipped };

struct Out {
    bool open = false;   // ask the window to come up on Settings > Guide, on `page` (kOfferPage or 1) - until it has (OpenRequest stays)
    int page = 0;
    bool gaveUp = false; // it asked kMaxAttempts times and no window came: it stays away this session (for the log)
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
            if (!wasDone && phase_ == Phase::Waiting) phase_ = Phase::Idle;   // ticked "seen" in D2R VR Authentic Settings before it came up
        } else if (!endedStale_ && phase_ == Phase::Closed) {
            phase_ = Phase::Idle;   // "First-start guide seen" unticked: it comes up at the next chance
            attempts_ = 0;
        }
        if (!show_ && phase_ == Phase::Waiting) phase_ = Phase::Idle;   // "Show the first-start guide" unticked while it waited
    }
    // The window's side: the guide page was on show and the window closed (Skipped), or Done was pressed (Finished). Whatever opened it.
    void Ended(End how) {
        if (how == End::None) return;
        phase_ = Phase::Closed;
        endedStale_ = !done_;   // until the file says done=1, a stale done=0 does not bring it back
    }

    Phase phase() const { return phase_; }
    int attempts() const { return attempts_; }

    Out Step(const Context& c) {
        Out o;
        if (phase_ == Phase::Idle) {
            if (!show_ || done_ || endedStale_ || gaveUp_) return o;
            phase_ = Phase::Waiting;
            Hide(kSettleS);
        }
        if (phase_ == Phase::Closed) return o;
        if (phase_ == Phase::Hosted) {
            // the window is up (or it was a moment ago): the panel's side has it, and Ended tells how it ends; if it never showed up, or vanished
            // without a word (the view left F4, a new session), ask again after a while
            if (c.uiUp) { sawWindow_ = true; lastUi_ = c.time; return o; }
            if (c.time - (sawWindow_ ? lastUi_ : openAt_) < (sawWindow_ ? kLostS : kOpenWaitS)) return o;
            if (attempts_ >= kMaxAttempts) {
                gaveUp_ = true;
                phase_ = Phase::Idle;
                o.gaveUp = true;
                return o;
            }
            phase_ = Phase::Waiting;
            Hide(kSettleS);
        }
        // Waiting
        if (!Shown(c)) {   // out of the way; the context must hold again before it asks
            visibleSince_ = -1;
            return o;
        }
        if (visibleSince_ < 0) visibleSince_ = c.time;
        if (c.time - visibleSince_ < settle_) return o;
        phase_ = Phase::Hosted;
        openAt_ = c.time;
        sawWindow_ = false;
        ++attempts_;
        o.open = true;
        o.page = calibrated_ ? 1 : kOfferPage;
        return o;
    }

private:
    // Not before the context has held for `settle` seconds.
    void Hide(double settle) {
        visibleSince_ = -1;
        settle_ = settle;
    }

    Phase phase_ = Phase::Idle;
    bool show_ = true, done_ = false, calibrated_ = false;
    bool endedStale_ = false;   // ended this session; the ini has not said done=1 since
    bool gaveUp_ = false;
    bool sawWindow_ = false;    // a panel of ours has been up since it asked
    int attempts_ = 0;
    double visibleSince_ = -1;
    double settle_ = kSettleS;
    double openAt_ = 0, lastUi_ = 0;
};

}  // namespace firststart
