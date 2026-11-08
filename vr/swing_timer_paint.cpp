// swing_timer_paint.cpp - [hud] swing_timer: the picture (vr/swing_timer_paint.h). Our own drawing.

#include "paint_kit.h"   // (windows.h, the canvas and GDI text)
#include "swing_timer_paint.h"

#include <string>

namespace swingtimerpaint {
namespace {

using namespace wheelpaint::kit;
using swingtimer::Look;
using Kind = swingtimer::Text;   // (kit's Text() is the drawing)
using swingtimer::Zone;

// The gate's colours: red refused, amber kept for the end, green in (the mock-up Serg agreed, 2026-11-05).
Col ZoneCol(Zone z) { return z == Zone::Red ? Col{226, 75, 74} : z == Zone::Amber ? Col{239, 159, 39} : Col{110, 196, 64}; }
Col TextCol(const Look& k) {
    switch (k.text) {
    case Kind::Damage: return k.crit ? Col{255, 200, 60} : Col{242, 240, 232};
    case Kind::Miss: return {176, 176, 170};
    case Kind::Blocked:
    case Kind::Dodged:
    case Kind::Avoided: return {140, 188, 238};
    case Kind::TooSoon: return {240, 92, 80};
    // [hud] swing_timer_reasons, by what to change: where it met the body (the wood's brown-orange), how it moved (yellow), which way (cyan)
    case Kind::Shaft:
    case Kind::Handle:
    case Kind::NotTip: return {232, 146, 66};
    case Kind::Slow:
    case Kind::Weak:
    case Kind::Push:
    case Kind::Short: return {244, 220, 92};
    case Kind::Away: return {96, 212, 222};
    case Kind::Shield: return {140, 188, 238};
    default: return {255, 255, 255};
    }
}
std::string Words(const Look& k) {
    switch (k.text) {
    case Kind::Damage: return std::to_string(k.value);
    case Kind::Miss: return uitext::Tr("vr.swing.miss", "miss");
    case Kind::Blocked: return uitext::Tr("vr.swing.blocked", "blocked");
    case Kind::Dodged: return uitext::Tr("vr.swing.dodged", "dodged");
    case Kind::Avoided: return uitext::Tr("vr.swing.avoided", "avoided");
    case Kind::TooSoon: return uitext::Tr("vr.swing.too_soon", "too soon");
    case Kind::Shaft: return uitext::Tr("vr.swing.shaft", "shaft");
    case Kind::Handle: return uitext::Tr("vr.swing.handle", "handle");
    case Kind::NotTip: return uitext::Tr("vr.swing.not_tip", "not the tip");
    case Kind::Slow: return uitext::Tr("vr.swing.slow", "slow");
    case Kind::Weak: return uitext::Tr("vr.swing.weak", "weak");
    case Kind::Push: return uitext::Tr("vr.swing.push", "push");
    case Kind::Short: return uitext::Tr("vr.swing.short", "short");
    case Kind::Away: return uitext::Tr("vr.swing.away", "wrong way");
    case Kind::Shield: return uitext::Tr("vr.swing.shield", "shield");
    default: return {};
    }
}
// A refusal's numbers, the swing's against the gate's ("3.1/5.0"): empty for the rest.
std::string Under(const Look& k) {
    char b[32];
    swingtimer::Numbers(k.text, k.got, k.need, b, sizeof b);
    return b;
}

// The text with a dark rim, so it reads over any ground.
void Outlined(Canvas& cv, Fonts& fonts, const std::string& s, Rect box, int size, Col c, float a) {
    static const float off[4][2] = {{-2.0f, 0.0f}, {2.0f, 0.0f}, {0.0f, -2.0f}, {0.0f, 2.0f}};
    for (const auto& o : off) Text(cv, fonts, s.c_str(), {box.l + o[0], box.t + o[1], box.r + o[0], box.b + o[1]}, size, true, {0, 0, 0}, 0.7f * a);
    Text(cv, fonts, s.c_str(), box, size, true, c, a);
}

// The ring: a dark track, the fill clockwise from the top in the zone's colour, a faint disc behind the number. Anti-aliased by distance.
void Ring(Canvas& cv, float cx, float cy, float outer, float width, float fill, Col col, float a) {
    constexpr float kTwoPi = 6.28318531f;
    const float mid = outer - 0.5f * width;
    for (int y = std::max(0, int(cy - outer) - 2); y < std::min(cv.h, int(cy + outer) + 3); ++y)
        for (int x = std::max(0, int(cx - outer) - 2); x < std::min(cv.w, int(cx + outer) + 3); ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = std::hypot(dx, dy);
            const float disc = Clamp01(mid - 0.5f * width - d + 0.5f);
            if (disc > 0.0f) cv.Over(x, y, {10, 10, 12}, 0.28f * a * disc);
            const float band = Clamp01(0.5f * width + 0.5f - std::fabs(d - mid));
            if (band <= 0.0f) continue;
            cv.Over(x, y, {18, 18, 22}, 0.5f * a * band);
            float turn = std::atan2(dx, -dy) / kTwoPi;   // 0 at the top, clockwise
            if (turn < 0.0f) turn += 1.0f;
            const float edge = fill >= 1.0f ? 1.0f : Clamp01((fill - turn) * kTwoPi * mid + 0.5f);
            if (edge > 0.0f) cv.Over(x, y, col, a * band * edge);
        }
}

}  // namespace

void Render(const Look& k, std::vector<uint32_t>& px) {
    px.assign(static_cast<size_t>(kW) * kH, 0u);
    if (!k.visible) return;
    Canvas cv{px, kW, kH};
    static Fonts fonts;   // (the painter's thread alone)
    const float timerA = k.timerAlpha / float(swingtimer::kAlphaSteps), textA = k.textAlpha / float(swingtimer::kAlphaSteps);
    const float fill = k.step / float(swingtimer::kSteps);
    std::string words = Words(k);
    const std::string under = Under(k);
    const bool reason = swingtimer::IsReason(k.text);
    const int size = k.crit ? 54 : k.text == Kind::Damage ? 44 : reason ? 32 : 36;
    if (k.style == swingtimer::Style::Ring) {
        const float cx = kW * 0.5f, cy = kH * 0.5f, outer = kRingOuterPx;
        if (k.timer) Ring(cv, cx, cy, outer, kRingWidthPx, fill, ZoneCol(k.zone), timerA);
        if (k.textAlpha > 0 && !words.empty()) {
            const bool many = k.text == Kind::Damage && k.count > 1;
            const float lift = many || !under.empty() ? 12.0f : 0.0f;
            Outlined(cv, fonts, words, {cx - outer + 14.0f, cy - 36.0f - lift, cx + outer - 14.0f, cy + 36.0f - lift}, size, TextCol(k), textA);
            if (many) Outlined(cv, fonts, "\xC3\x97" + std::to_string(k.count), {cx - 50.0f, cy + 22.0f, cx + 50.0f, cy + 50.0f}, 24, {200, 200, 196}, textA);
            else if (!under.empty()) Outlined(cv, fonts, under, {cx - 64.0f, cy + 20.0f, cx + 64.0f, cy + 50.0f}, 24, {214, 212, 204}, textA);
        }
    } else {
        // as thick as the ring's band where it is shown (Serg 2026-11-07: "той же толщины что у кружка в центре"), about its middle line
        const float th = k.barThick10 > 0 ? std::clamp(k.barThick10 / 10.0f, 2.0f, kBarMaxPx) : kBarDefaultPx;
        const float l = 24.0f, r = kW - 24.0f, t = kBarMidPx - 0.5f * th, b = kBarMidPx + 0.5f * th, round = 0.5f * th;
        if (k.timer) {
            RoundRect(cv, {l, t, r, b}, round, {18, 18, 22}, 0.55f * timerA, {0, 0, 0}, 0.0f, 0.0f);
            const float w = (r - l) * fill;
            if (w >= 1.0f) RoundRect(cv, {l, t, l + w, b}, std::min(round, 0.5f * w), ZoneCol(k.zone), timerA, {0, 0, 0}, 0.0f, 0.0f);
        }
        if (k.textAlpha > 0 && !words.empty()) {
            if (k.text == Kind::Damage && k.count > 1) words += " \xC3\x97" + std::to_string(k.count);
            if (!under.empty()) words += " " + under;
            const float top = t - 70.0f - 90.0f * (k.rise / float(swingtimer::kRiseSteps));
            Outlined(cv, fonts, words, {0.0f, top, float(kW), top + 64.0f}, size, TextCol(k), textA);
        }
    }
}

}  // namespace swingtimerpaint
