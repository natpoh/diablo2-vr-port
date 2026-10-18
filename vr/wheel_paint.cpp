// wheel_paint.cpp - the skill wheel's picture (vr/wheel_paint.h).
//
// The shapes are drawn by the pixel from a table of each pixel's radius and angle about the wheel's centre (made once), so the
// wedges, the separators and the rims are anti-aliased and are exactly the wedges vr/wheel_geom.h hits. Text is drawn by GDI
// into a small coverage mask and laid over; icons are laid over scaled. The design (a ring of eight wedges with the skill's
// icon and name, a centre for the basic attack, a gold highlight for the wedge under the beam and for the active one, an editor
// as a list) follows bearstonem/d2r-pcvr's wheel (MIT); every colour, shape and number here is ours.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include "paint_kit.h"
#include "tiles_paint.h"
#include "wheel_geom.h"
#include "wheel_icons.h"
#include "wheel_paint.h"

namespace wheelpaint {
namespace {

using namespace wheel;
using namespace kit;   // (the canvas, the rounded rectangle, text and icons: vr/paint_kit.h, shared with the tiles)

constexpr Col kGold{200, 152, 76}, kGoldBright{238, 196, 108}, kGoldDim{122, 94, 51}, kInk{22, 19, 16}, kText{236, 220, 184}, kTextDim{150, 139, 118};

Rect Px(Rect u) { return {u.l * kSize, u.t * kSize, u.r * kSize, u.b * kSize}; }

// -- the wheel's pixels --------------------------------------------------------------------------------------------------------------

constexpr float kCxPx = kCx * kSize, kCyPx = kCy * kSize;
constexpr float kRingInPx = kRingIn * kSize, kRingOutPx = kRingOut * kSize, kCentrePx = kCentreR * kSize;
constexpr float kBackdropPx = kRingOutPx + 28.0f;

struct Polar {
    std::vector<uint16_t> r16, a16;   // radius in 1/16 px, angle clockwise from up in 1/65536 of a turn
    Polar() : r16((size_t)kSize * kSize), a16((size_t)kSize * kSize) {
        for (int y = 0; y < kSize; ++y)
            for (int x = 0; x < kSize; ++x) {
                const float dx = x + 0.5f - kCxPx, dy = y + 0.5f - kCyPx;
                const float r = std::hypot(dx, dy) * 16.0f;
                float a = std::atan2(dx, -dy);
                if (a < 0.0f) a += 2.0f * kPi;
                r16[(size_t)y * kSize + x] = (uint16_t)std::min(65535.0f, r);
                a16[(size_t)y * kSize + x] = (uint16_t)std::min(65535.0f, a / (2.0f * kPi) * 65536.0f);
            }
    }
};
const Polar& GetPolar() {
    static const Polar p;
    return p;
}

Col WedgeColour(const View& v, int k) {
    if (!v.ready) return {34, 31, 28};
    const Skill& s = v.slot[k];
    if (!s.known || s.id < 0) return {32, 29, 26};
    const Col base = k < 4 ? Col{62, 51, 38} : Col{40, 48, 62};   // the right half is the first row, the left half the LT row
    return s.available ? base : Mix(base, Col{30, 28, 26}, 0.6f);
}

void PaintRing(Canvas& cv, const View& v) {
    const Polar& P = GetPolar();
    const bool slotPage = v.page != Page::PickSkill;
    if (!slotPage) return;
    const int x0 = int(kCxPx - kBackdropPx) - 2, x1 = int(kCxPx + kBackdropPx) + 2, y0 = int(kCyPx - kBackdropPx) - 2, y1 = int(kCyPx + kBackdropPx) + 2;
    const bool wheelPage = v.page == Page::Wheel;
    for (int y = std::max(0, y0); y < std::min(kSize, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(kSize, x1); ++x) {
            const size_t i = (size_t)y * kSize + x;
            const float r = P.r16[i] * (1.0f / 16.0f);
            if (r > kBackdropPx + 1.0f) continue;
            const float a = P.a16[i] * (2.0f * kPi / 65536.0f);
            // the dark disc under it all, with a gold rim
            cv.Over(x, y, Mix(Col{36, 31, 26}, Col{16, 14, 11}, Clamp01(r / kBackdropPx)), 0.90f * Clamp01(kBackdropPx - r + 0.5f));
            cv.Over(x, y, kGoldDim, 0.9f * Clamp01(1.6f - std::fabs(r - (kBackdropPx - 3.0f)) + 0.5f));
            // the wedges
            const float band = Clamp01(std::min(r - kRingInPx, kRingOutPx - r) + 0.5f);
            if (band > 0.0f) {
                const int k = int(a / kWedge) % kSlots;
                const float within = a - k * kWedge;
                const float db = std::min(within, kWedge - within) * r;   // pixels to the wedge's straight edge
                const float body = Clamp01(db - 1.0f) * band;
                const float edge = std::min(db, std::min(r - kRingInPx, kRingOutPx - r));   // pixels to its outline
                Col c = WedgeColour(v, k);
                const bool hot = v.hover == k && v.hover >= 0;
                const bool active = wheelPage && v.selected == k;
                if (hot) c = Mix(c, kGold, 0.30f);
                else if (active) c = Mix(c, kGold, 0.14f);
                cv.Over(x, y, c, 0.96f * body);
                if (active) cv.Over(x, y, kGoldBright, body * Clamp01(3.0f - edge + 0.5f));
                if (hot) cv.Over(x, y, kGoldBright, 0.9f * body * Clamp01(2.0f - edge + 0.5f));
            }
            // the rims of the ring
            cv.Over(x, y, kGoldDim, Clamp01(1.5f - std::fabs(r - (kRingOutPx + 2.0f)) + 0.5f));
            cv.Over(x, y, kGoldDim, Clamp01(1.5f - std::fabs(r - (kRingInPx - 2.0f)) + 0.5f));
            // the centre button (the wheel page only)
            if (wheelPage && r < kCentrePx + 3.0f) {
                const bool hot = v.hover == kCentre;
                const bool active = v.selected < 0;
                Col c = hot ? Mix(Col{44, 37, 29}, kGold, 0.35f) : Col{40, 34, 28};
                cv.Over(x, y, c, 0.97f * Clamp01(kCentrePx - r + 0.5f));
                cv.Over(x, y, active || hot ? kGoldBright : kGold, Clamp01(2.2f - std::fabs(r - kCentrePx) + 0.5f));
            }
        }
}

const char* FooterText(Cell c) {
    switch (c) {
    case kEdit: return "EDIT SKILLS";
    case kRunWalk: return "RUN / WALK";
    case kClose: return "CLOSE";
    case kBack: return "BACK";
    case kPrev: return "PREVIOUS";
    case kNext: return "NEXT";
    default: return "";
    }
}

// What the game says its run / walk switch is on ("" when it is not known).
const char* RunState(const View& v) { return v.run == kRunning ? "now: RUN" : v.run == kWalking ? "now: WALK" : ""; }

void PaintFooter(Canvas& cv, Fonts& fonts, const View& v) {
    RoundRect(cv, Px({0.02f, 0.894f, 0.98f, 0.99f}), 16.0f, Col{16, 14, 11}, 0.88f, kGoldDim, 0.8f, 1.5f);
    for (int i = 0; i < FooterCount(v.page, v.runWalk); ++i) {
        const Cell c = FooterCell(v.page, i, v.runWalk);
        bool enabled = true;
        if (c == kEdit) enabled = v.editable;
        if (c == kPrev) enabled = v.editPage > 0;
        if (c == kNext) enabled = v.editPage + 1 < v.editPages;
        const bool hot = v.hover == c && enabled;
        const Rect r = Px(FooterRect(v.page, i, v.runWalk));
        RoundRect(cv, r, 12.0f, hot ? Col{74, 56, 32} : Col{38, 33, 27}, 0.97f, hot ? kGoldBright : kGoldDim, 1.0f, 2.0f);
        if (c == kRunWalk && RunState(v)[0]) {   // the button and, under it, what the game's switch is on now
            const float mid = 0.5f * (r.t + r.b);
            Text(cv, fonts, FooterText(c), Rect{r.l, r.t + 2.0f, r.r, mid + 4.0f}, 24, true, enabled ? kText : Col{110, 102, 88});
            Text(cv, fonts, RunState(v), Rect{r.l, mid - 2.0f, r.r, r.b - 2.0f}, 18, false, kGoldBright);
            continue;
        }
        Text(cv, fonts, FooterText(c), r, 28, true, enabled ? kText : Col{110, 102, 88});
    }
}

void PaintWheelContent(Canvas& cv, Fonts& fonts, const View& v) {
    const bool wheelPage = v.page == Page::Wheel;
    for (int k = 0; k < kSlots; ++k) {
        float u, w;
        WedgePoint(k, 0.27f, &u, &w);
        const float px = u * kSize, py = w * kSize;
        const Skill& s = v.slot[k];
        const bool active = wheelPage && v.selected == k;
        // the slot's label on a small plate
        const Rect plate{px - 52.0f, py - 84.0f, px + 52.0f, py - 52.0f};
        RoundRect(cv, plate, 14.0f, active ? kGold : kInk, 0.92f, kGoldDim, 1.0f, 1.5f);
        Text(cv, fonts, kSlotDefs[k].label, plate, 22, true, active ? kInk : kGoldBright);
        if (!v.ready) continue;
        if (s.known && s.id >= 0) {
            Icon(cv, wheelicons::Get(s.id), Rect{px - 42.0f, py - 46.0f, px + 42.0f, py + 38.0f}, !s.available);
            Text(cv, fonts, s.name, Rect{px - 86.0f, py + 40.0f, px + 86.0f, py + 94.0f}, 22, false, s.available ? kText : kTextDim);
        } else if (s.known) {
            Text(cv, fonts, "Empty", Rect{px - 86.0f, py - 20.0f, px + 86.0f, py + 20.0f}, 22, false, Col{110, 102, 88});
        }
    }
    if (wheelPage) {
        const Rect centre{kCxPx - 92.0f, kCyPx - 50.0f, kCxPx + 92.0f, kCyPx + 28.0f};
        Text(cv, fonts, "BASIC\nATTACK", centre, 26, true, v.hover == kCentre ? kGoldBright : kText);
        if (v.selected < 0) Text(cv, fonts, "ACTIVE", Rect{kCxPx - 60.0f, kCyPx + 28.0f, kCxPx + 60.0f, kCyPx + 58.0f}, 20, true, kGoldBright);
    } else {
        Text(cv, fonts, "CHOOSE\nA SLOT", Rect{kCxPx - 100.0f, kCyPx - 50.0f, kCxPx + 100.0f, kCyPx + 50.0f}, 28, true, kText);
    }
}

// The category strip: All, the class's three trees, General / Items - the one shown gold, a tab with no skill in it dim.
void PaintStrip(Canvas& cv, Fonts& fonts, const View& v) {
    for (int c = 0; c < kCategories; ++c) {
        const Rect r = Px(CategoryRect(c));
        const int count = v.catCount[c];
        const bool on = v.category == c, enabled = count > 0 || on, hot = v.hover == CategoryCell(c) && count > 0;
        RoundRect(cv, r, 10.0f, on ? Col{70, 54, 30} : hot ? Col{58, 46, 28} : Col{32, 28, 23}, 0.97f, on || hot ? kGoldBright : kGoldDim, 1.0f, on ? 3.0f : 2.0f);
        char name[kNameLen + 4];
        if (c == kCatAll) std::snprintf(name, sizeof name, "All");
        else if (c == kCatGeneral) std::snprintf(name, sizeof name, "General / Items");
        else std::snprintf(name, sizeof name, "%s", v.treeName[c - 1][0] ? v.treeName[c - 1] : "-");
        const Col ink = on ? kGoldBright : enabled ? kText : Col{100, 93, 80};
        Text(cv, fonts, name, Rect{r.l + 4.0f, r.t + 2.0f, r.r - 4.0f, r.b - 20.0f}, 19, true, ink);
        char n[16];
        std::snprintf(n, sizeof n, "%d", count);
        Text(cv, fonts, n, Rect{r.l, r.b - 22.0f, r.r, r.b - 3.0f}, 16, false, enabled ? kTextDim : Col{90, 84, 72});
    }
}

void PaintList(Canvas& cv, Fonts& fonts, const View& v) {
    RoundRect(cv, Px({0.01f, 0.01f, 0.99f, 0.855f}), 22.0f, Col{20, 17, 14}, 0.93f, kGoldDim, 1.0f, 2.5f);
    char title[96];
    std::snprintf(title, sizeof title, "CHOOSE SKILL  -  %s", v.editSlot >= 0 ? kSlotDefs[v.editSlot].label : "");
    Text(cv, fonts, title, Px({0.05f, 0.035f, 0.62f, 0.095f}), 32, true, kText, 1.0f, true);
    if (v.editSlot >= 0 && v.slot[v.editSlot].known) {
        char now[96];
        std::snprintf(now, sizeof now, "Now: %s", v.slot[v.editSlot].id >= 0 ? v.slot[v.editSlot].name : "nothing");
        Text(cv, fonts, now, Px({0.05f, 0.095f, 0.62f, 0.145f}), 22, false, kTextDim, 1.0f, true);
    }
    if (v.catStrip) PaintStrip(cv, fonts, v);
    const bool hotClear = v.hover == kCentre;
    const Rect clear = Px(kClearRect);
    RoundRect(cv, clear, 12.0f, hotClear ? Col{74, 56, 32} : Col{38, 33, 27}, 0.97f, hotClear ? kGoldBright : kGoldDim, 1.0f, 2.0f);
    Text(cv, fonts, "CLEAR SLOT", clear, 26, true, kText);
    for (int i = 0; i < v.options; ++i) {
        const Skill& s = v.option[i];
        const Rect r = Px(OptionRect(i));
        const bool hot = v.hover == i;
        RoundRect(cv, r, 12.0f, hot ? Col{62, 48, 28} : Col{34, 30, 25}, 0.97f, hot ? kGoldBright : kGoldDim, 1.0f, hot ? 3.0f : 2.0f);
        const auto icon = wheelicons::Get(s.id);
        Icon(cv, icon, Rect{r.l + 12.0f, r.t + 12.0f, r.l + 12.0f + 108.0f, r.b - 12.0f}, false);
        const float textLeft = r.l + (icon ? 130.0f : 22.0f);   // (the icon's column is only there when there is an icon)
        Text(cv, fonts, s.name, Rect{textLeft, r.t + 8.0f, r.r - 10.0f, r.t + 78.0f}, 24, false, kText, 1.0f, true);
        char detail[48];
        if (s.id == kSkillInteract) std::snprintf(detail, sizeof detail, "Command");
        else if (s.owner >= 0) std::snprintf(detail, sizeof detail, "Item skill");
        else std::snprintf(detail, sizeof detail, "Level %d", (int)s.level);
        Text(cv, fonts, detail, Rect{textLeft, r.t + 82.0f, r.r - 10.0f, r.b - 8.0f}, 20, false, kTextDim, 1.0f, true);
    }
    if (!v.options) Text(cv, fonts, "No skills to choose from", Px({0.1f, 0.3f, 0.9f, 0.6f}), 30, false, kTextDim);
}

void PaintStatus(Canvas& cv, Fonts& fonts, const View& v) {
    char line[160] = {};
    if (v.status[0]) {
        std::snprintf(line, sizeof line, "%s", v.status);
    } else if (!v.ready) {
        std::snprintf(line, sizeof line, "Skills unavailable");
    } else if (v.page == Page::Wheel) {
        if (v.selected >= 0 && v.slot[v.selected].known)
            std::snprintf(line, sizeof line, "Trigger casts: %s  (%s)", v.slot[v.selected].name, kSlotDefs[v.selected].label);
        else
            std::snprintf(line, sizeof line, "Trigger: basic attack");
    } else if (v.page == Page::PickSlot) {
        std::snprintf(line, sizeof line, "Choose the slot to change");
    } else if (v.editPages > 1) {
        std::snprintf(line, sizeof line, "Page %d / %d", v.editPage + 1, v.editPages);
    }
    const Col c = v.result == Result::Refused ? Col{232, 140, 110} : kGoldBright;
    Text(cv, fonts, line, Px({0.04f, 0.862f, 0.96f, 0.892f}), 24, false, c);
}

}  // namespace

void SizeOf(wheel::Style style, int* width, int* height) {
    if (style == wheel::Style::Tiles) tilespaint::Size(width, height);
    else *width = *height = kSize;
}

void Render(const View& v, std::vector<uint32_t>& px) {
    if (v.style == wheel::Style::Tiles) {   // the other look: vr/tiles_paint.cpp (a wide window, its own size)
        tilespaint::Render(v, px);
        return;
    }
    px.assign((size_t)kSize * kSize, 0u);
    Canvas cv{px, kSize, kSize};
    Fonts fonts;
    if (v.page == Page::PickSkill) {
        PaintList(cv, fonts, v);
    } else {
        PaintRing(cv, v);
        PaintWheelContent(cv, fonts, v);
    }
    PaintStatus(cv, fonts, v);
    PaintFooter(cv, fonts, v);
}

// -- the worker ---------------------------------------------------------------------------------------------------------------------------

struct Async::Impl {
    std::mutex m;
    std::condition_variable cv;
    std::thread th;
    bool stop = false;
    View want;
    bool haveWant = false;
    uint32_t paintedRev = 0;
    bool painted = false;
    uint32_t paintedIcons = 0;
    std::vector<uint32_t> done;
    uint32_t doneRev = 0;
    int doneW = kSize, doneH = kSize;
    bool haveDone = false;

    void Run() {
        std::vector<uint32_t> px;
        (void)GetPolar();   // (the pixel table, made before the first wanted picture)
        for (;;) {
            View v;
            uint32_t icons;
            {
                std::unique_lock<std::mutex> lock(m);
                for (;;) {
                    if (stop) return;
                    icons = wheelicons::Revision();
                    if (haveWant && (!painted || want.revision != paintedRev || icons != paintedIcons)) break;
                    cv.wait_for(lock, std::chrono::milliseconds(40));
                }
                v = want;
                paintedRev = v.revision;
                paintedIcons = icons;
                painted = true;
            }
            try { Render(v, px); } catch (...) { continue; }
            std::lock_guard<std::mutex> lock(m);
            done.swap(px);
            doneRev = v.revision;
            SizeOf(v.style, &doneW, &doneH);
            haveDone = true;
        }
    }
};

Async::Async() : impl_(new Impl) {}
Async::~Async() {
    Stop();
    delete impl_;
}

void Async::Submit(const View& v) {
    Impl& I = *impl_;
    std::lock_guard<std::mutex> lock(I.m);
    if (!I.haveWant || v.revision != I.want.revision) {
        I.want = v;
        I.haveWant = true;
        I.cv.notify_one();
    }
    if (!I.th.joinable()) {
        I.stop = false;
        I.th = std::thread([this] { impl_->Run(); });
    }
}

void Async::Warm() {
    Impl& I = *impl_;
    std::lock_guard<std::mutex> lock(I.m);
    if (!I.th.joinable()) {
        I.stop = false;
        I.th = std::thread([this] { impl_->Run(); });
    }
}

bool Async::Fetch(std::vector<uint32_t>& out, uint32_t* revision, int* width, int* height) {
    Impl& I = *impl_;
    std::lock_guard<std::mutex> lock(I.m);
    if (!I.haveDone) return false;
    out.swap(I.done);
    *revision = I.doneRev;
    if (width) *width = I.doneW;
    if (height) *height = I.doneH;
    I.haveDone = false;
    return true;
}

void Async::Stop() {
    Impl& I = *impl_;
    {
        std::lock_guard<std::mutex> lock(I.m);
        I.stop = true;
        I.cv.notify_all();
    }
    if (I.th.joinable()) I.th.join();
    std::lock_guard<std::mutex> lock(I.m);
    I.haveWant = I.haveDone = I.painted = false;
    I.stop = false;
}

}  // namespace wheelpaint
