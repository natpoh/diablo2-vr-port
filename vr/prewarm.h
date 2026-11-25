#pragma once
// prewarm.h - [openxr] prewarm: when the menu panel's / the quest notice's swapchain is made ahead (vr/xr.cpp PrewarmChains), and whether the
// shown-now-and-then ones are kept (FreeIdle). Pure (tools/prewarm_test.cpp).
//
// 2026-11-25, PC1 on Virtual Desktop: the waypoint panel's 2048x2048 swapchain was made as it opened (a 241 ms hitch), the turned big map's 1397
// (112 ms), the quest notice's 896x192 (88 ms) - and the notice's again at every notice (FreeIdle gave it back after 10 s unused: 7 times). Now the
// panel's is made where no frame is seen waiting: on a loading screen (no game area loaded) when there is none, or in the frames right after the
// game's own resize (the picture changed size: the game is frozen for it anyway) when it is of the old size and not in use; the notice's on a loading
// screen once its picture's size is known. One a frame. The notice's and the turned map's are kept once made while our swapchains are few.
#include <cstddef>
#include <cstdint>

namespace prewarm {

constexpr uint64_t kResizeMs = 1500;    // the frames after the picture changed size: the game's own freeze
constexpr uint64_t kInUseMs = 2000;     // a panel chain drawn this recently is in use (the big map not turned shares it): left to its user
constexpr std::size_t kKeepUnder = 12;  // the shown-now-and-then ones are kept only under this many of ours alive (SteamVR gives a session 16)

enum class Make { None, Panel, Notice };

struct In {
    uint64_t now = 0;           // ms
    uint32_t pw = 0, ph = 0;    // the game's picture (the flat chain's whole size; 0 = none yet)
    bool loading = false;       // no game area loaded (a loading screen, the menus)
    bool panelMade = false;
    uint32_t panelW = 0, panelH = 0;
    uint64_t panelUsedAt = 0;
    bool noticeMade = false;
    uint32_t noticeW = 0, noticeH = 0;   // the notice picture's size (0: not known yet)
};

class Plan {
  public:
    Make Step(const In& in) {
        if (in.pw != lastPw_ || in.ph != lastPh_) {
            if (lastPw_ && in.pw) resizedAt_ = in.now;
            lastPw_ = in.pw;
            lastPh_ = in.ph;
        }
        const bool resizing = resizedAt_ && in.now - resizedAt_ < kResizeMs;
        if (in.pw && in.ph) {
            if (!in.panelMade && in.loading) return Make::Panel;
            if (in.panelMade && (in.panelW != in.pw || in.panelH != in.ph) && resizing && in.now - in.panelUsedAt > kInUseMs) return Make::Panel;
        }
        if (in.loading && !in.noticeMade && in.noticeW && in.noticeH) return Make::Notice;
        return Make::None;
    }

  private:
    uint32_t lastPw_ = 0, lastPh_ = 0;
    uint64_t resizedAt_ = 0;
};

// FreeIdle: the notice card's and the turned big map's swapchains kept (not given back when unused) - with prewarm on and few of ours alive.
inline bool KeepShown(bool on, std::size_t alive) { return on && alive < kKeepUnder; }

}  // namespace prewarm
