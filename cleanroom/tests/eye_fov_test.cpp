// The fov each eye of native OpenXR's pair is drawn with and the camera's off-axis projection from it (vr/eye_fov.h): [openxr] runtime_fov
// drawn with one centred frustum holding both eyes' views and each eye's part submitted, symmetric_fov / fov_crop as before, the projection's edges on
// the frustum's, and what a compositor that takes only the headset's own view (ALVR) makes of each. The numbers of a player's
// headset (an Apple Vision Pro on ALVR into SteamVR, 0.179 log:
// left -60.1 45.1 50.1 -45.1, right -45.1 60.1 50.1 -45.1 deg; a square window: projection x 0.9092 y 0.9092, off-axis 0). Run: eye_fov_test.exe
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "eye_fov.h"

namespace {
int g_fails = 0, g_checks = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fails; std::printf("  FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

using namespace eyefov;
constexpr float kD = 3.14159265f / 180.0f;
bool Near(float a, float b, float eps = 2e-4f) { return std::fabs(a - b) <= eps; }
Tan Deg(float l, float r, float u, float d) { return FromAngles(l * kD, r * kD, u * kD, d * kD); }

const Tan kLeft = Deg(-60.1f, 45.1f, 50.1f, -45.1f), kRight = Deg(-45.1f, 60.1f, 50.1f, -45.1f);

void TestRuntime() {
    // runtime_fov on: symmetric_fov and fov_crop do nothing; both eyes drawn with one centred frustum (xr.cpp takes Centred of both views)
    const Tan c = Centred(kLeft, kRight);
    CHECK(Near(c.l, kLeft.l) && Near(c.r, -kLeft.l) && Near(c.u, kLeft.u) && Near(c.d, -kLeft.u));   // the widest side each way, the tallest each way
    CHECK(c.l <= kLeft.l && c.r >= kLeft.r && c.u >= kLeft.u && c.d <= kLeft.d);   // it holds the left eye's view
    CHECK(c.l <= kRight.l && c.r >= kRight.r && c.u >= kRight.u && c.d <= kRight.d);   // and the right eye's
    const Tan one = Drawn(kLeft, {true, false, true, 1.0f});   // (from one eye alone: the same here, the eyes mirrored)
    CHECK(Near(one.l, c.l) && Near(one.r, c.r) && Near(one.u, c.u) && Near(one.d, c.d));
    // not mirrored eyes: still one frustum, holding both
    const Tan odd = Deg(-40.0f, 55.0f, 42.0f, -52.0f), both = Centred(kLeft, odd);
    CHECK(Near(both.l, -std::tan(60.1f * kD)) && Near(both.r, -both.l) && Near(both.d, -std::tan(52.0f * kD)) && Near(both.u, -both.d));
    // no off-axis term on either axis (the game's light binning takes the projection as centred: drawn off-axis, a seam down the picture,
    // 2026-10-25), the same projection in both eyes
    const Proj p = Projection(c);
    CHECK(Near(p.offX, 0.0f) && Near(p.offY, 0.0f) && Near(p.sx, 2.0f / (c.r - c.l)));
    // where each eye's view lies in the picture: the left eye's from its left edge, the right eye's to its right edge, both from the top
    const Window wl = Inside(c, kLeft), wr = Inside(c, kRight);
    CHECK(Near(wl.x0, 0.0f) && wl.x1 < 0.8f && wl.x1 > 0.75f && Near(wl.y0, 0.0f) && wl.y1 < 0.95f && wl.y1 > 0.85f);
    CHECK(Near(wr.x1, 1.0f) && Near(wr.x0, 1.0f - wl.x1) && Near(wr.y0, 0.0f) && Near(wr.y1, wl.y1));
    CHECK(!wl.Whole() && !wr.Whole());
    // in his 1920 x 1920 picture: whole pixels, and their fov within half a pixel of the runtime's at every edge
    const int W = 1920, H = 1920;
    const Sub sl = Pixels(c, wl, W, H), sr = Pixels(c, wr, W, H);
    CHECK(sl.x == 0 && sl.y == 0 && sl.w > 1400 && sl.w < 1560 && sl.h > 1650 && sl.h < 1820);
    CHECK(sr.x + sr.w == W && sr.w == sl.w && sr.x == W - sl.w && sr.y == 0 && sr.h == sl.h);
    const float px = (c.r - c.l) / W, py = (c.u - c.d) / H;   // one pixel's tangent across, and down
    CHECK(Near(sl.tan.l, kLeft.l, 0.5f * px) && Near(sl.tan.r, kLeft.r, 0.5f * px) && Near(sl.tan.u, kLeft.u, 0.5f * py) && Near(sl.tan.d, kLeft.d, 0.5f * py));
    CHECK(Near(sr.tan.l, kRight.l, 0.5f * px) && Near(sr.tan.r, kRight.r, 0.5f * px) && Near(sr.tan.d, kRight.d, 0.5f * py));
    // the rectangle's fov is exactly its pixels': its edges at the drawn frustum's tangent of those pixel columns and rows
    CHECK(Near(sl.tan.r, c.l + (c.r - c.l) * (sl.x + sl.w) / W, 1e-6f) && Near(sl.tan.d, c.u - (c.u - c.d) * (sl.y + sl.h) / H, 1e-6f));
    // a runtime view wider than the drawn one is clamped to the picture; a degenerate frustum leaves the whole picture
    const Window big = Inside(Deg(-30.0f, 30.0f, 30.0f, -30.0f), Deg(-40.0f, 40.0f, 40.0f, -40.0f));
    CHECK(big.Whole());
    CHECK(Inside({0.0f, 0.0f, 1.0f, -1.0f}, kLeft).Whole());
    // a whole window in whole pixels is the whole picture with the drawn fov
    const Sub all = Pixels(c, Window{}, W, H);
    CHECK(all.x == 0 && all.y == 0 && all.w == W && all.h == H && Near(all.tan.l, c.l, 1e-6f) && Near(all.tan.r, c.r, 1e-6f));
}

void TestAsBefore() {
    // the 0.179 default: symmetric, cropped to a square window - his log's x 0.9092 y 0.9092, off-axis 0 (and up/down off-axis kept)
    const Tan a = Drawn(kLeft, {false, true, true, 1.0f});
    const Proj p = Projection(a);
    CHECK(Near(p.sx, 0.912f, 0.004f) && Near(p.sy, p.sx, 1e-4f));
    CHECK(Near(p.offX, 0.0f));
    CHECK(Near(a.u, kLeft.u) && Near(a.d, kLeft.d));   // a frustum wider than the window: cut at the sides, the height kept
    // symmetric, no crop: as wide as the wider side
    const Tan s = Drawn(kLeft, {false, true, false, 1.0f});
    CHECK(Near(s.l, kLeft.l) && Near(s.r, -kLeft.l) && Near(s.u, kLeft.u));
    // neither: the runtime's own
    const Tan n = Drawn(kLeft, {false, false, false, 1.0f});
    CHECK(Near(n.l, kLeft.l) && Near(n.r, kLeft.r));
    // a window narrower than the frustum: cut at the sides about the frustum's own middle, the height kept
    const Tan t = Drawn(kLeft, {false, false, true, 0.5f});
    CHECK(Near((t.r - t.l) / (t.u - t.d), 0.5f) && Near(t.u, kLeft.u) && Near(t.r + t.l, kLeft.r + kLeft.l));
    // a window wider than the frustum: cut above and below about the middle, the width kept
    const Tan w = Drawn(Deg(-30.0f, 30.0f, 50.0f, -50.0f), {false, false, true, 2.0f});
    CHECK(Near((w.r - w.l) / (w.u - w.d), 2.0f) && Near(w.u, -w.d) && Near(w.l, std::tan(-30.0f * kD)));
    // no window yet: no crop
    const Tan z = Drawn(kLeft, {false, false, true, 0.0f});
    CHECK(Near(z.l, kLeft.l) && Near(z.u, kLeft.u));
}

// Where a direction drawn into an eye's picture is shown. The picture is drawn with frustum `drawn` (a texel's u from the projection) and
// handed in as the part `sub` of a W-wide picture. A compositor that honours the submitted fov reads that part over the sub's own fov; one
// that takes only the headset's own view right - ALVR's driver draws each texture as a quad over the headset's own projection, placed by
// its texture bounds only - reads it over the runtime's (`headset`). Returns how far the shown direction is from the drawn one, in degrees,
// across the eye at height 0.
float ShownOffDeg(const Tan& drawn, const Sub& sub, int W, const Tan& readOver, float tanX) {
    const Proj p = Projection(drawn);
    const float u = 0.5f * (Ndc(tanX, p.sx, p.offX) + 1.0f);   // 0 left .. 1 right of the whole picture
    const float us = (u * W - sub.x) / sub.w;                  // .. of the part handed in
    const float shown = readOver.l + us * (readOver.r - readOver.l);
    return (std::atan(shown) - std::atan(tanX)) / kD;
}
void TestCompositor() {
    const Tan eye[2] = {kLeft, kRight};
    const int W = 1920, H = 1920;
    for (int e = 0; e < 2; ++e) {
        const Tan own = Drawn(eye[e], {false, true, true, 1.0f});   // 0.179's default: symmetric, cropped to his square window, the whole picture
        const Sub ownSub = Pixels(own, Window{}, W, H);
        const Tan rt = Centred(kLeft, kRight);                      // runtime_fov: the centred frustum of both, this eye's part handed in
        const Sub rtSub = Pixels(rt, Inside(rt, eye[e]), W, H);
        float ownHonoured = 0.0f, ownHeadsetOnly = 0.0f, rtHonoured = 0.0f, rtHeadsetOnly = 0.0f;
        for (float deg = -40.0f; deg <= 40.0f; deg += 5.0f) {
            const float t = std::tan(deg * kD);
            ownHonoured = std::max(ownHonoured, std::fabs(ShownOffDeg(own, ownSub, W, ownSub.tan, t)));
            ownHeadsetOnly = std::max(ownHeadsetOnly, std::fabs(ShownOffDeg(own, ownSub, W, eye[e], t)));
            rtHonoured = std::max(rtHonoured, std::fabs(ShownOffDeg(rt, rtSub, W, rtSub.tan, t)));
            rtHeadsetOnly = std::max(rtHeadsetOnly, std::fabs(ShownOffDeg(rt, rtSub, W, eye[e], t)));
        }
        CHECK(ownHonoured < 0.01f);     // our own view is right where the fov is honoured (SteamVR's own compositing: the dashboard up)
        CHECK(ownHeadsetOnly > 10.0f);  // and far off where it is not - each eye's middle pushed outward: crossed, stretched
        CHECK(rtHonoured < 0.01f);      // runtime_fov: right where it is honoured
        CHECK(rtHeadsetOnly < 0.05f);   // and where only the headset's own view is (under half a pixel: ~0.03 deg here)
        // A compositor that ignores the imageRect too - the whole swapchain image laid over the headset's own view (0.181, the Apple Vision Pro on
        // ALVR: no better with the part submitted as an imageRect). The part as an imageRect of the whole picture is then as wrong as before;
        // the part as a swapchain of its own (xr.cpp's crop: the box copied, the image submitted whole) is right.
        const Sub whole{0, 0, W, H, rt};
        const Sub box{0, 0, rtSub.w, rtSub.h, rtSub.tan};   // (the cropped image: the box is all of it)
        float rectIgnored = 0.0f, cropIgnored = 0.0f;
        for (float deg = -40.0f; deg <= 40.0f; deg += 5.0f) {
            const float t = std::tan(deg * kD);
            rectIgnored = std::max(rectIgnored, std::fabs(ShownOffDeg(rt, whole, W, eye[e], t)));
            // the cropped image's own pixels: a direction lands at (its picture column - the box's x) / the box's width, read over the headset's
            const Proj p = Projection(rt);
            const float col = 0.5f * (Ndc(t, p.sx, p.offX) + 1.0f) * W - rtSub.x;
            const float shown = eye[e].l + col / box.w * (eye[e].r - eye[e].l);
            cropIgnored = std::max(cropIgnored, std::fabs((std::atan(shown) - std::atan(t)) / kD));
        }
        CHECK(rectIgnored > 5.0f);     // the imageRect route: wrong there
        CHECK(cropIgnored < 0.05f);    // the crop: right even there
        // straight ahead: the left eye's shown to the left, the right eye's to the right - the eyes no longer meet
        const float ahead = ShownOffDeg(own, ownSub, W, eye[e], 0.0f);
        CHECK(e == 0 ? ahead < -10.0f : ahead > 10.0f);
        std::printf("  %s eye, his numbers: our own view shown up to %.1f deg off by a headset-only compositor (%.1f deg straight ahead); "
                    "runtime_fov %.3f deg (picture x %d-%d of %d); with the imageRect ignored too: the part as an imageRect %.1f deg, "
                    "the cropped image %.3f deg\n", e == 0 ? "left" : "right", ownHeadsetOnly, ahead, rtHeadsetOnly, rtSub.x,
                    rtSub.x + rtSub.w, W, rectIgnored, cropIgnored);
    }
}

// [openxr] fov_override: the hand's angles (the left eye's, degrees) made each eye's fov and back - the right eye the mirror, clamped
void TestHand() {
    const HandAngles h{60.1f, 45.1f, 50.1f, 45.1f};   // his runtime's left eye
    const Angles l = FromHand(h, 0), r = FromHand(h, 1);
    CHECK(Near(l.l, -60.1f * kD) && Near(l.r, 45.1f * kD) && Near(l.u, 50.1f * kD) && Near(l.d, -45.1f * kD));
    CHECK(Near(r.l, -45.1f * kD) && Near(r.r, 60.1f * kD) && Near(r.u, l.u) && Near(r.d, l.d));
    // drawn with exactly them: the projection's edges on the hand's angles, the eyes mirrored (the replayed right eye needs it)
    const Tan tl = FromAngles(l.l, l.r, l.u, l.d), tr = FromAngles(r.l, r.r, r.u, r.d);
    CHECK(Near(tl.l, kLeft.l, 1e-4f) && Near(tl.r, kLeft.r, 1e-4f) && Near(tr.r, kRight.r, 1e-4f));
    CHECK(Mirrored(tl, tr));
    // back: the runtime's left eye as the sliders' numbers
    const HandAngles b = ToHand(-60.1f * kD, 45.1f * kD, 50.1f * kD, -45.1f * kD);
    CHECK(Near(b.outer, 60.1f, 1e-3f) && Near(b.inner, 45.1f, 1e-3f) && Near(b.up, 50.1f, 1e-3f) && Near(b.down, 45.1f, 1e-3f));
    // clamped to 10 .. 85, a broken number taken as 45
    const Angles c = FromHand({5.0f, 89.0f, NAN, 45.0f}, 0);
    CHECK(Near(c.l, -10.0f * kD) && Near(c.r, 85.0f * kD) && Near(c.u, 45.0f * kD));
}

// The right eye's interface layer moved by RightLayerShift puts a label in the same direction in both eyes (off-centre eyes: his numbers);
// centred eyes need none
void TestLayerShift() {
    const float s = RightLayerShift(kLeft, kRight);
    const Proj pl = Projection(kLeft), pr = Projection(kRight);
    for (float deg = -40.0f; deg <= 40.0f; deg += 10.0f) {
        const float t = std::tan(deg * kD);
        const float uL = 0.5f * (Ndc(t, pl.sx, pl.offX) + 1.0f), uR = 0.5f * (Ndc(t, pr.sx, pr.offX) + 1.0f);
        CHECK(Near(uL + s, uR, 1e-4f));   // the label laid out at uL (the left eye's) shown at uL + s in the right eye: where that eye sees it
    }
    CHECK(s < -0.2f);   // his eyes: about a quarter of the width to the left in the right eye (~520 px at 1920 - "so far off")
    // fov_override's angles to the hundredth: the labels follow them (the eyes drawn with exactly them, the shift from those eyes)
    const Angles hl = FromHand({62.37f, 44.91f, 51.06f, 46.13f}, 0), hr = FromHand({62.37f, 44.91f, 51.06f, 46.13f}, 1);
    const Tan tl = FromAngles(hl.l, hl.r, hl.u, hl.d), tr = FromAngles(hr.l, hr.r, hr.u, hr.d);
    const float sh = RightLayerShift(tl, tr);
    const Proj ql = Projection(tl), qr = Projection(tr);
    for (float deg = -40.0f; deg <= 40.0f; deg += 10.0f) {
        const float t = std::tan(deg * kD);
        CHECK(Near(0.5f * (Ndc(t, ql.sx, ql.offX) + 1.0f) + sh, 0.5f * (Ndc(t, qr.sx, qr.offX) + 1.0f), 1e-4f));
    }
    const Tan c = Centred(kLeft, kRight);
    CHECK(Near(RightLayerShift(c, c), 0.0f));
}

// [openxr] fov_override_centred: the hand's angles drawn with one centred frustum and each eye's part cut whole (Serg's PS VR2 numbers, martin's
// Quest 3 on ALVR). The headset gets each eye with its own angles, as fov_override always gave it; the interface, laid out once on the centred
// picture, is one direction in both eyes - drawn off-axis it was not (the labels doubled).
void TestHandCentred() {
    const HandAngles hands[2] = {{50.0f, 45.0f, 50.0f, 50.0f}, {54.0f, 40.0f, 44.0f, 55.0f}};
    const int sizes[2][2] = {{2000, 2040}, {3056, 3212}};   // (Serg's, martin's picture)
    for (int k = 0; k < 2; ++k) {
        const HandAngles& h = hands[k];
        const int W = sizes[k][0], H = sizes[k][1];
        const HandDraw d = HandCentred(h);
        const Tan eye[2] = {HandTan(h, 0), HandTan(h, 1)};
        // drawn centred: no off-axis term (the camera's projection the same in both eyes - and the game's light bins take it as centred),
        // holding both eyes' views
        const Proj p = Projection(d.drawn);
        CHECK(Near(p.offX, 0.0f) && Near(p.offY, 0.0f));
        for (int e = 0; e < 2; ++e) CHECK(d.drawn.l <= eye[e].l && d.drawn.r >= eye[e].r && d.drawn.u >= eye[e].u && d.drawn.d <= eye[e].d);
        // the same window xr.cpp computes from the angles it has (WindowOf: OpenFrame's drawn fov and the hand's, radians)
        const Angles dr{std::atan(d.drawn.l), std::atan(d.drawn.r), std::atan(d.drawn.u), std::atan(d.drawn.d)};
        for (int e = 0; e < 2; ++e) {
            const Window w = WindowOf(dr, FromHand(h, e));
            CHECK(Near(w.x0, d.win[e].x0, 1e-5f) && Near(w.x1, d.win[e].x1, 1e-5f) && Near(w.y0, d.win[e].y0, 1e-5f) && Near(w.y1, d.win[e].y1, 1e-5f));
        }
        // each eye's cut in whole pixels: its fov within half a pixel of the hand's - what fov_override submitted before, the 3D unchanged
        const float px = (d.drawn.r - d.drawn.l) / W, py = (d.drawn.u - d.drawn.d) / H;
        Sub s[2];
        for (int e = 0; e < 2; ++e) {
            s[e] = Pixels(d.drawn, d.win[e], W, H);
            CHECK(Near(s[e].tan.l, eye[e].l, 0.5f * px) && Near(s[e].tan.r, eye[e].r, 0.5f * px) && Near(s[e].tan.u, eye[e].u, 0.5f * py) &&
                  Near(s[e].tan.d, eye[e].d, 0.5f * py));
        }
        CHECK(s[0].x == 0 && s[1].x + s[1].w == W && s[0].w == s[1].w && s[0].h == s[1].h && s[0].y == s[1].y);   // mirrored: left from the left edge
        // a world direction lands in the cut image where it landed in the off-axis whole image (columns of the image, under a pixel apart)
        for (int e = 0; e < 2; ++e)
            for (float deg = -35.0f; deg <= 35.0f; deg += 5.0f) {
                const float t = std::tan(deg * kD);
                const float cut = (t - s[e].tan.l) / (s[e].tan.r - s[e].tan.l), offAxis = (t - eye[e].l) / (eye[e].r - eye[e].l);
                CHECK(std::fabs(cut - offAxis) * s[e].w < 1.0f);
            }
        // the interface: a pixel of the one layout (column x of the drawn picture) is ONE direction in both eyes - the direction it has in the
        // centred picture; drawn off-axis the same pixel of each eye's own layout was a direction apart by the off-axis terms (both outward:
        // beyond the infinite, never meeting - doubled)
        float apartOld = 0.0f;
        for (float x = 0.25f; x <= 0.75f; x += 0.05f) {
            const float c = DirectionAt(x, d.drawn);
            for (int e = 0; e < 2; ++e) {
                const float u = (x * W - s[e].x) / s[e].w;   // its column in the eye's cut image
                CHECK(Near(DirectionAt(u, s[e].tan), c, 1e-5f));
            }
            apartOld = std::max(apartOld, std::fabs(std::atan(DirectionAt(x, eye[0])) - std::atan(DirectionAt(x, eye[1]))) / kD);
        }
        CHECK(apartOld > 5.0f);   // (11 deg at 50 / 45, 30 at 54 / 40, over the middle half of the picture)
        std::printf("  hand %.0f/%.0f/%.0f/%.0f: drawn centred %.1f %.1f %.1f %.1f deg, left cut x %d-%d y %d-%d of %dx%d (%.0f %% of the width); "
                    "the interface was up to %.1f deg apart off-axis, now 0\n", h.outer, h.inner, h.up, h.down, std::atan(d.drawn.l) / kD,
                    std::atan(d.drawn.r) / kD, std::atan(d.drawn.u) / kD, std::atan(d.drawn.d) / kD, s[0].x, s[0].x + s[0].w, s[0].y, s[0].y + s[0].h, W, H,
                    100.0f * s[0].w / W, apartOld);
    }
}

// runtime_fov=1 cut with the angles handed to Inside as if they were tangents (OpenFrame, 0.181 - 0.197.4): martin's Quest 3 runtime view
// (-54 40 44 -55, the eyes mirrored) cut from the wrong place - the box and its fov still agree (right where the fov is honoured), but over the
// headset's own view (ALVR) straight ahead shows ~11 deg outward in each eye. WindowOf (tangents) cuts exactly the headset's view.
void TestRuntimeCutInAngles() {
    const Angles view[2] = {{-54.0f * kD, 40.0f * kD, 44.0f * kD, -55.0f * kD}, {-40.0f * kD, 54.0f * kD, 44.0f * kD, -55.0f * kD}};
    const Tan headset[2] = {FromAngles(view[0].l, view[0].r, view[0].u, view[0].d), FromAngles(view[1].l, view[1].r, view[1].u, view[1].d)};
    const Tan c = Centred(headset[0], headset[1]);
    const Angles drawn{std::atan(c.l), std::atan(c.r), std::atan(c.u), std::atan(c.d)};
    const int W = 3056, H = 3212;
    float before[2] = {}, now[2] = {};
    for (int e = 0; e < 2; ++e) {
        const Window wrong = Inside({drawn.l, drawn.r, drawn.u, drawn.d}, headset[e]);   // (the old call: radians as the drawn tangents)
        const Window right = WindowOf(drawn, view[e]);
        const Window ref = Inside(c, headset[e]);
        CHECK(Near(right.x0, ref.x0, 1e-5f) && Near(right.x1, ref.x1, 1e-5f) && Near(right.y0, ref.y0, 1e-5f) && Near(right.y1, ref.y1, 1e-5f));
        if (e == 0) CHECK(wrong.x1 > 0.9f && right.x1 < 0.82f && right.x1 > 0.79f && Near(wrong.y0, 0.0f) && right.y0 > 0.15f);
        const Sub sw = Pixels(c, wrong, W, H), sr = Pixels(c, right, W, H);
        // shown over the headset's own view (ALVR): the cut image laid over it whatever fov it came with
        before[e] = ShownOffDeg(c, sw, W, headset[e], 0.0f);
        now[e] = ShownOffDeg(c, sr, W, headset[e], 0.0f);
        float worst = 0.0f;
        for (float deg = -35.0f; deg <= 35.0f; deg += 5.0f) worst = std::max(worst, std::fabs(ShownOffDeg(c, sr, W, headset[e], std::tan(deg * kD))));
        CHECK(worst < 0.05f);
    }
    CHECK(before[0] < -8.0f && before[1] > 8.0f);   // straight ahead: the left eye's to the left, the right eye's to the right - never meeting
    std::printf("  runtime_fov=1 cut, martin's Quest 3: straight ahead shown %.1f / %.1f deg off by a headset-only compositor before, %.3f / %.3f now\n",
                before[0], before[1], now[0], now[1]);
}

void TestNotMirrored() {
    const Tan odd = Deg(-60.1f, 45.1f, 40.0f, -50.0f);
    CHECK(!Mirrored(kLeft, odd));   // a different height: the replayed right eye would be off (vrcam says so in the log)
}
}  // namespace

int main() {
    TestRuntime();
    TestAsBefore();
    TestCompositor();
    TestHand();
    TestLayerShift();
    TestHandCentred();
    TestRuntimeCutInAngles();
    TestNotMirrored();
    std::printf("eye_fov_test: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
