// rune_test - [openxr] runes_draw: BodyWalk's gesture runes in the headset, without a headset and without BodyWalk.
//
//   rune_test [--dump <folder>]       exit code 0 = every check passed
//
// What is checked: the colours (the block's bytes into linear light and back), which segments of a picture are lit, the picture's panel
// (its turn from right/up), the trail projected from the head onto a billboard that faces it (every projected point lies on the ray from
// the head through the hand's point), the layer budget, the 0.2 s ease-out, the constant buffer the shader reads - and the shader
// itself: vr/rune_shader.h's HLSL is compiled and run on a WARP device, cell by cell, and compared pixel by pixel with the CPU mirror
// (vr/rune_mirror.h). With --dump the shader's own output is also drawn as pictures (PNG): the cells, and what a head at the origin would
// see of the runes hung in a dark and a bright scene at three progress values, through the same geometry the headset gets.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "rune_mirror.h"

using Microsoft::WRL::ComPtr;
using namespace runes;

namespace {

int g_fail = 0, g_checks = 0;
void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_fail; printf("FAIL  %s\n", what); }
}
bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
bool Near3(V3 a, V3 b, float eps) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

constexpr float kPi = 3.14159265358979f;

// -- a made-up rune: a circle on a panel 0.85 m ahead of the head, the hand drawing it in front of the chest ---------------------------
const V3 kHead{0.0f, 1.65f, 0.0f};
BW_LinkVrRune CircleRune(float progress, float flash = 0.0f, float fade = 1.0f, uint32_t id = 1, float handR = 0.10f) {
    BW_LinkVrRune r{};
    r.id = id;
    r.state = progress >= 1.0f ? BW_LINK_RUNE_FIRED : BW_LINK_RUNE_DRAWING;
    r.progress = progress;
    r.score = 0.4f + 0.5f * progress;
    r.fade = fade;
    r.flash = flash;
    r.runeRgba = 0xFFC8A078u;    // R 0x78 G 0xA0 B 0xC8: a cool grey-blue, opaque
    r.litRgba = 0xFF2EB8FFu;     // R 0xFF G 0xB8 B 0x2E: gold
    r.trailRgba = 0xFF2828F0u;   // R 0xF0 G 0x28 B 0x28: red
    r.lineWidth = 0.012f;
    r.trailWidth = 0.010f;
    r.glow = 0.35f + 0.5f * progress;
    const V3 c{0.0f, 1.70f, -0.85f};
    r.centre[0] = c.x; r.centre[1] = c.y; r.centre[2] = c.z;
    r.right[0] = 1.0f; r.up[1] = 1.0f;
    r.halfW = r.halfH = 0.13f;
    r.pointCount = kPoints;
    for (uint32_t i = 0; i < kPoints; ++i) {
        const float a = 2.0f * kPi * (float)i / (float)(kPoints - 1);
        r.pts[i][0] = 0.12f * std::sin(a);
        r.pts[i][1] = 0.12f * std::cos(a);
    }
    // the hand: a circle of radius handR round (0, 1.60, -0.55), from its top, clockwise as seen by the player; `progress` of it drawn
    r.trailCount = std::min<uint32_t>(kTrailPoints, std::max<uint32_t>(2u, (uint32_t)std::lround(progress * 95.0f) + 1u));
    for (uint32_t i = 0; i < r.trailCount; ++i) {
        const float a = 2.0f * kPi * progress * (float)i / (float)(r.trailCount - 1);
        r.trail[i][0] = handR * std::sin(a);
        r.trail[i][1] = 1.60f + handR * std::cos(a);
        r.trail[i][2] = -0.55f;
    }
    return r;
}

// -- the pure parts ----------------------------------------------------------------------------------------------------------------------
void TestColours() {
    const Rgba red = Unpack(0xFF0000FFu), green = Unpack(0x8000FF00u), blue = Unpack(0x00FF0000u);
    Expect(Near(red.r, 1, 1e-6f) && red.g == 0 && red.b == 0 && Near(red.a, 1, 1e-6f), "R is the low byte: 0xFF0000FF is opaque red");
    Expect(green.r == 0 && Near(green.g, 1, 1e-6f) && Near(green.a, 128.0f / 255.0f, 1e-6f), "G is the second byte, alpha the top one");
    Expect(blue.r == 0 && blue.g == 0 && Near(blue.b, 1, 1e-6f) && blue.a == 0, "B is the third byte");
    Expect(Near(SrgbToLinear(0.5f), 0.21404f, 1e-4f), "sRGB 0.5 is 0.214 linear");
    float worst = 0;
    for (int i = 0; i < 256; ++i) worst = std::max(worst, std::fabs(LinearToSrgb(SrgbToLinear((float)i / 255.0f)) - (float)i / 255.0f));
    Expect(worst < 1e-5f, "every byte goes into linear light and back as itself");
    const Rgba f = Shown(0xFF0000FFu, 0.5f, 1.0f);
    Expect(Near(f.r, 1, 1e-6f) && Near(f.g, 1, 1e-6f) && Near(f.b, 1, 1e-6f) && Near(f.a, 0.5f, 1e-6f), "flash 1 = white, fade 0.5 halves the alpha");
    const Rgba h = Shown(0xFF000000u, 1.0f, 0.5f);
    Expect(Near(h.r, 0.5f, 1e-6f) && Near(h.g, 0.5f, 1e-6f) && Near(h.a, 1.0f, 1e-6f), "flash 0.5 mixes half way toward white");
}

void TestLook() {
    BW_LinkVrRune r = CircleRune(0.5f);
    Look k = LookOf(r);
    Expect(k.shown && Near(k.lineW, 0.012f, 1e-6f) && Near(k.trailW, 0.010f, 1e-6f), "a plain rune is shown with its widths");
    r.fade = 0.0f;
    Expect(!LookOf(r).shown, "fade 0: nothing to see");
    r = CircleRune(0.5f);
    r.fade = NAN; r.glow = NAN; r.lineWidth = NAN;
    k = LookOf(r);
    Expect(!k.shown && k.glow == 0.0f && Near(k.lineW, 0.012f, 1e-6f), "numbers that are not numbers become a safe default");
    r = CircleRune(0.5f);
    r.lineWidth = 5.0f; r.trailWidth = 0.0f; r.glow = 7.0f; r.fade = 3.0f;
    k = LookOf(r);
    Expect(Near(k.lineW, kMaxWidth, 1e-6f) && Near(k.trailW, kMinWidth, 1e-6f) && k.glow == 1.0f && k.fade == 1.0f, "widths, glow and fade are held in their ranges");
    r = CircleRune(0.5f);
    r.runeRgba = r.litRgba = r.trailRgba = 0x00FFFFFFu;
    Expect(!LookOf(r).shown, "no colour with any opacity: nothing to see");
}

void TestLit() {
    Expect(LitSegments(0.0f, 48) == 0 && LitSegments(1.0f, 48) == 47, "progress 0 lights none, 1 lights all 47 segments");
    Expect(LitSegments(0.5f, 48) == 24, "progress 0.5: segments i < 23.5, so 24");
    Expect(LitSegments(0.001f, 48) == 1, "the first segment is lit as soon as there is any progress");
    Expect(LitSegments(NAN, 48) == 0 && LitSegments(-1.0f, 48) == 0 && LitSegments(5.0f, 48) == 47, "a NaN, negative or over-1 progress is held to 0..1");
    Expect(LitSegments(1.0f, 1) == 0 && LitSegments(1.0f, 0) == 0 && LitSegments(1.0f, 2) == 1, "fewer than two points have no segment");
    bool same = true;
    uint32_t prev = 0;
    bool monotone = true;
    for (int s = 0; s <= 1000; ++s) {
        const float p = (float)s / 1000.0f;
        const uint32_t n = LitSegments(p, 48);
        if (n < prev) monotone = false;
        prev = n;
        for (uint32_t i = 0; i < 47; ++i) if (SegmentLit(i, p, 48) != ((float)i < p * 47.0f)) same = false;
    }
    Expect(same, "SegmentLit is the block's rule: i < progress x (pointCount - 1)");
    Expect(monotone, "more progress never lights fewer segments");
}

void TestTurns() {
    // FromAxes / Rotate: the basis' columns come back
    const float yaws[] = {0.0f, 0.5f, 1.3f, 2.9f, -2.2f};
    bool good = true;
    for (float yaw : yaws)
        for (float pitch : {-1.1f, 0.0f, 0.7f}) {
            const V3 x{std::cos(yaw), 0.0f, -std::sin(yaw)};
            const V3 y0{0.0f, 1.0f, 0.0f};
            const V3 z = Cross(x, y0);
            const V3 yy{std::cos(pitch) * y0.x, std::cos(pitch), std::sin(pitch)};
            const V3 yo = yy - x * Dot(yy, x);
            const V3 y = yo * (1.0f / Len(yo));
            const V3 z2 = Cross(x, y);
            (void)z;
            const Quat q = FromAxes(x, y, z2);
            if (!Near3(Rotate(q, {1, 0, 0}), x, 1e-5f) || !Near3(Rotate(q, {0, 1, 0}), y, 1e-5f) || !Near3(Rotate(q, {0, 0, 1}), z2, 1e-5f)) good = false;
            if (!Near(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0f, 1e-5f)) good = false;
        }
    Expect(good, "FromAxes(x, y, z) is the turn that takes the unit axes to x, y, z (a unit quaternion)");
    // the four branches of the conversion: half turns about each axis
    const Quat a = FromAxes({1, 0, 0}, {0, -1, 0}, {0, 0, -1}), b = FromAxes({-1, 0, 0}, {0, 1, 0}, {0, 0, -1}), c = FromAxes({-1, 0, 0}, {0, -1, 0}, {0, 0, 1});
    Expect(Near3(Rotate(a, {0, 1, 0}), {0, -1, 0}, 1e-5f) && Near3(Rotate(b, {1, 0, 0}), {-1, 0, 0}, 1e-5f) && Near3(Rotate(c, {0, 0, 1}), {0, 0, 1}, 1e-5f) &&
           Near3(Rotate(c, {1, 0, 0}), {-1, 0, 0}, 1e-5f), "half turns about each axis (the conversion's other branches)");
}

void TestPanel() {
    BW_LinkVrRune r = CircleRune(0.5f);
    Look look = LookOf(r);
    Panel p;
    Expect(PicturePanel(r, look, &p), "the contract's panel (0.85 m ahead, right +x, up +y) is accepted");
    Expect(Near3(p.centre, {0.0f, 1.70f, -0.85f}, 1e-6f), "centred on the block's centre");
    Expect(Near3(Rotate(p.q, {1, 0, 0}), {1, 0, 0}, 1e-5f) && Near3(Rotate(p.q, {0, 1, 0}), {0, 1, 0}, 1e-5f), "the quad's right and up are the block's");
    const V3 front = Rotate(p.q, {0, 0, 1});
    Expect(Near3(front, {0, 0, 1}, 1e-5f) && Dot(front, kHead - p.centre) > 0.0f, "its front is cross(right, up), toward the head: the panel faces the player");
    Expect(Near(p.side, 2.0f * 0.13f + 2.0f * Reach(0.012f, look.glow), 1e-6f), "its side holds the points and the halo");
    // turned 30 degrees about the vertical, and an up that is not square to right
    const float yaw = 30.0f * kPi / 180.0f;
    BW_LinkVrRune t = r;
    t.right[0] = std::cos(yaw); t.right[1] = 0; t.right[2] = -std::sin(yaw);
    t.up[0] = 0.2f; t.up[1] = 1.0f; t.up[2] = 0.1f;
    Panel pt;
    Expect(PicturePanel(t, LookOf(t), &pt), "a turned panel with a slanted up is accepted");
    const V3 rr = Rotate(pt.q, {1, 0, 0}), uu = Rotate(pt.q, {0, 1, 0}), ff = Rotate(pt.q, {0, 0, 1});
    Expect(Near3(rr, {t.right[0], t.right[1], t.right[2]}, 1e-5f) && Near(Dot(rr, uu), 0.0f, 1e-5f) && Near3(ff, Cross(rr, uu), 1e-5f), "right kept, up made square to it, front = cross");
    // rejects
    BW_LinkVrRune bad = r; bad.right[0] = NAN;
    Expect(!PicturePanel(bad, look, &p), "a right that is not a number: no panel");
    bad = r; bad.up[0] = 1.0f; bad.up[1] = 0.0f;
    Expect(!PicturePanel(bad, look, &p), "up along right: no panel");
    bad = r; bad.right[0] = bad.right[1] = bad.right[2] = 0.0f;
    Expect(!PicturePanel(bad, look, &p), "a zero right: no panel");
    bad = r; bad.halfW = 0.0f;
    Expect(!PicturePanel(bad, look, &p), "a panel with no width: none");
    bad = r; bad.halfH = NAN;
    Expect(!PicturePanel(bad, look, &p), "a height that is not a number: none");
    bad = r; bad.halfW = 50.0f;
    Expect(!PicturePanel(bad, look, &p), "a panel of 100 m: none");
    bad = r; bad.pointCount = 1;
    Expect(!PicturePanel(bad, look, &p), "one point is no picture");
    bad = r; bad.centre[1] = INFINITY;
    Expect(!PicturePanel(bad, look, &p), "a centre that is not finite: none");
}

// A point of the board in the room.
V3 OnBoard(const TrailBoard& b, float u, float v) {
    return b.centre + Rotate(b.q, {1, 0, 0}) * u + Rotate(b.q, {0, 1, 0}) * v;
}

void TestTrail() {
    BW_LinkVrRune r = CircleRune(0.75f);
    const Look look = LookOf(r);
    static TrailBoard b;
    const V3 ahead{0.0f, 0.0f, -1.0f};   // the head looks down -z
    Expect(ProjectTrail(r, look, kHead, ahead, &b), "a trail 0.55 m ahead of the head projects");
    const V3 n = Rotate(b.q, {0, 0, 1});
    const V3 toHead = kHead - b.centre;
    Expect(Dot(n, toHead * (1.0f / Len(toHead))) > 0.985f && Near(Dot(n, Rotate(b.q, {1, 0, 0})), 0.0f, 1e-5f), "the board faces the head (within 10 degrees of the line to its centre)");
    Expect(Near(Rotate(b.q, {0, 1, 0}).y, 1.0f, 1e-4f) || Rotate(b.q, {0, 1, 0}).y > 0.9f, "and stands upright");
    Expect(Near(Dot(Rotate(b.q, {1, 0, 0}), {0, 1, 0}), 0.0f, 1e-5f), "its right is level");
    // every projected point lies on the ray from the head through the hand's point, on the board's plane
    bool onRay = true, onPlane = true;
    const float dist = -Dot(b.centre - kHead, n);   // the board's distance from the head, square to it
    uint32_t checked = 0;
    for (uint32_t i = 0; i + 1 < r.trailCount; ++i) {   // all trail steps are long enough here, so segment i is point i -> i + 1
        if (i >= b.segs) break;
        for (int e = 0; e < 2; ++e) {
            const V3 q = OnBoard(b, b.seg[i][e * 2], b.seg[i][e * 2 + 1]);
            const V3 p = From3(r.trail[i + e]);
            if (Len(Cross(p - kHead, q - kHead)) > 1e-4f * Len(p - kHead) * Len(q - kHead) + 1e-6f) onRay = false;
            if (!Near(-Dot(q - kHead, n), dist, 1e-4f)) onPlane = false;
            ++checked;
        }
    }
    Expect(checked > 100 && onRay, "each projected point is on the ray from the head through the hand's point");
    Expect(onPlane, "and on the board's plane, the board's distance from the head");
    Expect(b.blob && Near3(OnBoard(b, b.blobAt[0], b.blobAt[1]) - kHead, (From3(r.trail[r.trailCount - 1]) - kHead) * (dist / -Dot(From3(r.trail[r.trailCount - 1]) - kHead, n)), 1e-4f),
           "the blob is the hand's last point, projected");
    // the board holds the line, the blob and the halo
    float lo = 1e9f, hi = -1e9f;
    for (uint32_t i = 0; i < b.segs; ++i)
        for (int e = 0; e < 2; ++e) { lo = std::min(lo, std::min(b.seg[i][e * 2], b.seg[i][e * 2 + 1])); hi = std::max(hi, std::max(b.seg[i][e * 2], b.seg[i][e * 2 + 1])); }
    Expect(std::max(std::fabs(lo), std::fabs(hi)) + BlobReach(look.trailW) <= 0.5f * b.side + 1e-5f, "the board is large enough for the line and the blob's reach");
    Expect(b.side < 0.6f, "and not much larger");

    // a head that has moved: the line is where the hand drew it as seen from the new head; the board turned to it
    const V3 head2{0.35f, 1.55f, 0.20f};
    static TrailBoard b2;
    const V3 ahead2{-0.35f, -0.05f, -0.75f};
    Expect(ProjectTrail(r, look, head2, ahead2, &b2), "the same trail seen from another place");
    const V3 n2 = Rotate(b2.q, {0, 0, 1});
    Expect(Dot(n2, (head2 - b2.centre) * (1.0f / Len(head2 - b2.centre))) > 0.985f && !Near3(n, n2, 0.05f), "the board turned to the new head");
    bool ray2 = true;
    for (uint32_t i = 0; i < std::min<uint32_t>(b2.segs, 60); ++i) {
        const V3 q = OnBoard(b2, b2.seg[i][0], b2.seg[i][1]);
        const V3 p = From3(r.trail[i]);
        if (Len(Cross(p - head2, q - head2)) > 1e-4f * Len(p - head2) * Len(q - head2) + 1e-6f) ray2 = false;
    }
    Expect(ray2, "every point still on the ray from the new head through the hand's point");

    // a point nearer the head than the board is magnified by the ratio of the depths
    BW_LinkVrRune two{};
    two.trailCount = 3;
    two.trail[0][2] = -1.0f; two.trail[0][1] = 1.65f; two.trail[0][0] = 0.0f;
    two.trail[1][2] = -1.0f; two.trail[1][1] = 1.65f; two.trail[1][0] = 0.4f;
    two.trail[2][2] = -0.5f; two.trail[2][1] = 1.65f; two.trail[2][0] = 0.1f;   // half the depth: on screen as far out as (0.2, -1.0)
    static TrailBoard b3;
    Expect(ProjectTrail(two, look, kHead, ahead, &b3) && b3.segs == 2, "a trail with points at two depths projects");
    const V3 n3 = Rotate(b3.q, {0, 0, 1});
    const float plane3 = -Dot(b3.centre - kHead, n3);   // the board's distance from the head, square to it
    const V3 q2 = OnBoard(b3, b3.seg[1][2], b3.seg[1][3]);
    const V3 want = kHead + (From3(two.trail[2]) - kHead) * (plane3 / -Dot(From3(two.trail[2]) - kHead, n3));
    Expect(Near3(q2, want, 1e-4f), "a nearer point lands where its ray meets the board, not where it is");

    // gaps: a point behind the head, a point that is not a number, the last point lost
    BW_LinkVrRune g = CircleRune(0.5f);
    static TrailBoard bg;
    const uint32_t full = g.trailCount;
    g.trail[10][2] = 0.5f;   // behind the head (the head looks to -z)
    Expect(ProjectTrail(g, look, kHead, ahead, &bg), "a trail with a point behind the head projects");
    static TrailBoard bf;
    ProjectTrail(CircleRune(0.5f), look, kHead, ahead, &bf);
    Expect(bg.side < 0.6f && Len(kHead - bg.centre) > 0.5f, "that point does not drag the board toward the head");
    Expect(bg.segs == bf.segs - 2, "that point leaves a gap: the two segments at it are not drawn");
    g = CircleRune(0.5f);
    g.trail[5][0] = NAN;
    Expect(ProjectTrail(g, look, kHead, ahead, &bg) && bg.segs == bf.segs - 2, "a point that is not a number leaves a gap too");
    g = CircleRune(0.5f);
    g.trail[full - 1][2] = 0.5f;
    Expect(ProjectTrail(g, look, kHead, ahead, &bg) && !bg.blob && bg.segs == bf.segs - 1, "the hand behind the head: no blob, its segment gone");
    g = CircleRune(0.5f);
    for (uint32_t i = 0; i < g.trailCount; ++i) g.trail[i][2] = 0.5f;
    Expect(!ProjectTrail(g, look, kHead, ahead, &bg), "a trail behind the head: nothing");
    Expect(ProjectTrail(g, look, kHead, {0.0f, 0.0f, 1.0f}, &bg), "... and the same trail is on a board when the head looks round to it");
    g = CircleRune(0.5f);
    g.trailCount = 0;
    Expect(!ProjectTrail(g, look, kHead, ahead, &bg), "no points: nothing");
    g = CircleRune(0.5f);
    g.trailCount = 5000;
    Expect(ProjectTrail(g, look, kHead, ahead, &bg) && bg.segs <= kTrailSegs, "a count past the array is held to it");
    g = CircleRune(0.5f);
    Expect(!ProjectTrail(g, look, {0.0f, 1.60f, -0.50f}, ahead, &bg), "a head right at the trail: nothing");
    Expect(!ProjectTrail(g, look, {NAN, 0, 0}, ahead, &bg), "a head that is not a number: nothing");
    Expect(!ProjectTrail(g, look, kHead, {0.0f, 0.0f, 0.0f}, &bg) && !ProjectTrail(g, look, kHead, {NAN, 0, 0}, &bg), "a look direction that is none: nothing");
    BW_LinkVrRune one{};
    one.trailCount = 1;
    one.trail[0][1] = 1.6f; one.trail[0][2] = -0.6f;
    Expect(ProjectTrail(one, look, kHead, ahead, &bg) && bg.segs == 0 && bg.blob, "one point: the blob alone");
    BW_LinkVrRune still{};
    still.trailCount = 10;
    for (uint32_t i = 0; i < 10; ++i) { still.trail[i][1] = 1.6f; still.trail[i][2] = -0.6f; }
    Expect(ProjectTrail(still, look, kHead, ahead, &bg) && bg.segs == 0 && bg.blob, "a hand standing still: no zero-length segments, the blob");
    // straight above the head: the board's up is taken from the hint, not NaN
    BW_LinkVrRune up{};
    up.trailCount = 3;
    up.trail[0][1] = 2.6f; up.trail[1][1] = 2.6f; up.trail[1][0] = 0.1f; up.trail[2][1] = 2.6f; up.trail[2][0] = 0.1f; up.trail[2][2] = 0.1f;
    Expect(ProjectTrail(up, look, kHead, {0.0f, 1.0f, 0.0f}, &bg) && std::isfinite(bg.q.x) && std::isfinite(bg.q.w) && Near(Len(Rotate(bg.q, {0, 0, 1})), 1.0f, 1e-5f), "a trail straight above the head still makes a board");
    // a trail so large that its board would be past the limit
    BW_LinkVrRune huge = CircleRune(0.5f);
    huge.trail[3][0] = 50.0f;
    Expect(!ProjectTrail(huge, look, kHead, ahead, &bg), "a trail 50 m across: none");
}

void TestBudget() {
    uint32_t p = 0, t = 0;
    Budget(4, 4, 8, &p, &t);
    Expect(p == 4 && t == 4, "room for eight: all four pictures and four trails");
    Budget(4, 4, 6, &p, &t);
    Expect(p == 4 && t == 2, "room for six: the trails go first");
    Budget(4, 4, 3, &p, &t);
    Expect(p == 3 && t == 0, "room for three: three pictures, no trail");
    Budget(2, 2, 0, &p, &t);
    Expect(p == 0 && t == 0, "no room: nothing");
    Budget(1, 4, 2, &p, &t);
    Expect(p == 1 && t == 1, "one picture and a trail that fits");
    Budget(0, 3, 2, &p, &t);
    Expect(p == 0 && t == 2, "trails alone take the room there is");
}

void TestEase() {
    Ease e;
    BW_LinkVrRune out[kMax];
    BW_LinkVrRune a = CircleRune(0.5f, 0.0f, 1.0f, 7), b = CircleRune(0.3f, 0.0f, 0.8f, 9);
    BW_LinkVrRune cur[2] = {a, b};
    Expect(e.Step(cur, 2, 1000, out) == 2 && out[0].id == 7 && out[1].id == 9 && out[0].fade == 1.0f, "runes that are there are drawn as they are");
    // both vanish: they ease out over 200 ms
    uint32_t n = e.Step(nullptr, 0, 1010, out);
    Expect(n == 2 && out[0].fade > 0.99f && out[1].fade > 0.7f, "gone while showing: held, at nearly full fade at the start");
    n = e.Step(nullptr, 0, 1110, out);
    Expect(n == 2 && Near(out[0].fade, 0.25f, 0.02f) && Near(out[1].fade, 0.8f * 0.25f, 0.02f), "100 ms into the ease-out: a quarter of the fade (the square)");
    n = e.Step(nullptr, 0, 1209, out);
    Expect(n == 2 && out[0].fade < 0.01f, "just before 200 ms: almost nothing");
    Expect(e.Step(nullptr, 0, 1215, out) == 0, "after 200 ms: gone");
    Expect(e.Step(nullptr, 0, 1300, out) == 0, "and stays gone");
    // a rune the engine had already faded out just goes
    BW_LinkVrRune dim = CircleRune(0.5f, 0.0f, 0.02f, 3);
    cur[0] = dim;
    e.Step(cur, 1, 2000, out);
    Expect(e.Step(nullptr, 0, 2010, out) == 0, "a rune whose fade was already under 5% does not linger");
    // one of two goes, the other stays
    cur[0] = a; cur[1] = b;
    e.Step(cur, 2, 3000, out);
    cur[0] = b;
    n = e.Step(cur, 1, 3016, out);
    Expect(n == 2 && out[0].id == 9 && out[0].fade == 0.8f && out[1].id == 7 && out[1].fade == 1.0f, "one of two vanishes: the other is as sent, the first is held (starting at its last fade)");
    n = e.Step(cur, 1, 3046, out);
    Expect(n == 2 && out[0].id == 9 && out[0].fade == 0.8f && out[1].id == 7 && out[1].fade < 1.0f && out[1].fade > 0.5f, "... and eases out after the other");
    // it comes back during the ease-out
    cur[0] = a; cur[1] = b;
    n = e.Step(cur, 2, 3050, out);
    Expect(n == 2 && out[0].fade == 1.0f && out[1].fade == 0.8f, "back within the ease-out: as sent again, no ghost");
    // nothing left over: after the pair stops and the time is up the slots are free for four new ones
    e.Step(nullptr, 0, 3060, out);
    e.Step(nullptr, 0, 4000, out);
    BW_LinkVrRune four[4];
    for (uint32_t i = 0; i < 4; ++i) four[i] = CircleRune(0.5f, 0.0f, 1.0f, 20 + i);
    Expect(e.Step(four, 4, 4100, out) == 4, "four runes at once");
    n = e.Step(four, 3, 4116, out);
    Expect(n == 4 && out[3].id == 23, "three are sent, the fourth is held in the room that is left");
    n = e.Step(four, 3, 4156, out);
    Expect(n == 4 && out[3].id == 23 && out[3].fade < 1.0f && out[3].fade > 0.3f, "... and eases out");
    BW_LinkVrRune five[5];
    for (uint32_t i = 0; i < 5; ++i) five[i] = CircleRune(0.5f, 0.0f, 1.0f, 30 + i);
    Expect(e.Step(five, 5, 4200, out) == kMax, "a list longer than four is held to four");
    e.Step(four, 4, 4300, out);
    e.Reset();
    Expect(e.Step(nullptr, 0, 4310, out) == 0, "Reset: nothing is held (another view, switched off)");
}

void TestWatch() {
    Watch w;
    Expect(w.Step(false, 0) == Event::None, "no block: silent");
    Expect(w.Step(true, 1) == Event::Appeared, "a live block with a rune: told once");
    Expect(w.Step(true, 1) == Event::None && w.Step(true, 2) == Event::None, "going on: silent");
    Expect(w.Step(true, 0) == Event::None, "the engine's own count 0: the ordinary end, silent");
    Expect(w.Step(false, 0) == Event::None && w.Step(false, 0) == Event::None, "then the stamp stands still with nothing to draw: silent, however long");
    Expect(w.Step(true, 1) == Event::None, "the next gesture: not told again");
    Expect(w.Step(false, 1) == Event::WentStale, "a block that still says runes and has stood still half a second: told it went stale");
    Expect(w.Step(false, 1) == Event::None && w.Step(false, 1) == Event::None, "and only once");
    Expect(w.Step(true, 3) == Event::None, "back again: silent (told already)");
    Expect(w.Step(true, 0) == Event::None && w.Step(false, 0) == Event::None, "ended normally again: silent");
}

void TestCb() {
    BW_LinkVrRune r = CircleRune(0.5f);
    const Look look = LookOf(r);
    Panel p;
    PicturePanel(r, look, &p);
    static CellCb cb;
    FillPicture(&cb, r, look, p, 2);
    Expect(cb.cell[0] == 2.0f * kCell && cb.cell[1] == 0.0f && cb.cell[2] == (float)kCell, "a picture's cell: column 2 of the top row");
    Expect(cb.ext[3] == 47.0f && cb.misc[0] == 24.0f && cb.misc[1] == 0.0f, "47 segments, 24 lit, a picture");
    Expect(cb.seg[0][0] == r.pts[0][0] && cb.seg[0][3] == r.pts[1][1] && cb.seg[46][2] == r.pts[47][0], "its segments are the block's points one to one");
    Expect(Near(cb.ext[0], p.side, 1e-6f) && Near(cb.ext[1], 0.012f, 1e-6f) && Near(cb.ext[2], r.glow, 1e-6f), "the panel's side, the width and the glow");
    Expect(Near(cb.lit[0], SrgbToLinear(1.0f), 1e-6f) && Near(cb.unlit[0], SrgbToLinear(120.0f / 255.0f), 1e-6f), "lit in litRgba, the rest in runeRgba");
    BW_LinkVrRune nan = r;
    nan.pts[5][0] = NAN;
    FillPicture(&cb, nan, look, p, 0);
    Expect(std::isfinite(cb.seg[4][2]) && std::isfinite(cb.seg[5][0]), "a point that is not a number does not reach the shader as one");
    static TrailBoard tb;
    ProjectTrail(r, look, kHead, {0.0f, 0.0f, -1.0f}, &tb);
    FillTrail(&cb, look, tb, 3);
    Expect(cb.cell[0] == 3.0f * kCell && cb.cell[1] == (float)kCell && cb.misc[1] == 1.0f && cb.misc[0] == 0.0f, "a trail's cell: column 3 of the bottom row, nothing lit");
    Expect(cb.ext[3] == (float)tb.segs && cb.blob[2] == kBlobSize && cb.blob[0] == tb.blobAt[0], "its segments, and the blob at the hand");
    Expect(cb.lit[3] == look.trail.a && cb.unlit[0] == look.trail.r, "in the trail's colour");
    Expect(sizeof(CellCb) <= kCbSlot && kCbSlot % 256 == 0, "a cell's constants fit their 256-aligned slot");
}

// The numbers the HLSL carries are the C++ ones.
float HlslConst(const char* name) {
    const std::string h = Hlsl();
    const std::string key = std::string("static const float ") + name + " = ";
    const size_t at = h.find(key);
    return at == std::string::npos ? -1.0f : (float)std::atof(h.c_str() + at + key.size());
}
void TestShaderConstants() {
    Expect(HlslConst("kTailW") == kTailWidth && HlslConst("kTailA") == kTailAlpha && HlslConst("kRim") == kRimAlpha && HlslConst("kAdd") == kHaloAdd &&
               HlslConst("kWhite") == kWhiteCore,
           "the shader's look constants are the C++ ones");
    Expect(Hlsl() != nullptr && std::string(Hlsl()).find("1.6 * w") != std::string::npos && kRimReach == 1.6f, "the rim's reach in the shader is the C++ one");
    const std::string h = Hlsl();
    Expect(h.find("float4 g_seg[96]") != std::string::npos && h.find("0.45 * w") != std::string::npos && kBlobHalo == 0.45f, "the segment array and the blob's halo scale agree");
}

// -- the shader on WARP --------------------------------------------------------------------------------------------------------------------
struct Gpu {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> cb;
    ComPtr<ID3D11Texture2D> rt, stage;
    ComPtr<ID3D11RenderTargetView> rtv;
    std::string why;

    bool Compile(const char* entry, const char* target, ComPtr<ID3DBlob>* out) {
        ComPtr<ID3DBlob> err;
        const std::string src = Hlsl();
        const HRESULT hr = D3DCompile(src.data(), src.size(), "rune", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out->GetAddressOf(), err.GetAddressOf());
        if (FAILED(hr)) { why = std::string(entry) + " did not compile: " + (err ? (const char*)err->GetBufferPointer() : "?"); return false; }
        return true;
    }
    bool Init() {
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) { why = "no WARP device"; return false; }
        ComPtr<ID3DBlob> vsb, psb;
        if (!Compile("VSMain", "vs_5_0", &vsb) || !Compile("PSRune", "ps_5_0", &psb)) return false;
        if (FAILED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs)) ||
            FAILED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps))) { why = "shader objects"; return false; }
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = (UINT)sizeof(CellCb);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &cb))) { why = "constant buffer"; return false; }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = kAtlasW; td.Height = kAtlasH; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &rt)) || FAILED(dev->CreateRenderTargetView(rt.Get(), nullptr, &rtv))) { why = "render target"; return false; }
        td.Width = td.Height = kCell; td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &stage))) { why = "staging texture"; return false; }
        return true;
    }
    // One cell drawn the way HangRunes draws it (a full-cell triangle in the cell's viewport, the cell's constants), read back.
    bool Draw(const CellCb& c, std::vector<mirror::F4>* out) {
        ctx->UpdateSubresource(cb.Get(), 0, nullptr, &c, 0, 0);
        const float clear[4] = {0.3f, 0.6f, 0.9f, 0.5f};   // (not zero: a cell the shader does not write whole would show)
        ctx->ClearRenderTargetView(rtv.Get(), clear);
        ID3D11RenderTargetView* rtvs[1] = {rtv.Get()};
        ctx->OMSetRenderTargets(1, rtvs, nullptr);
        const D3D11_VIEWPORT vp{c.cell[0], c.cell[1], c.cell[2], c.cell[2], 0.0f, 1.0f};
        ctx->RSSetViewports(1, &vp);
        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ID3D11Buffer* cbs[1] = {cb.Get()};
        ctx->PSSetConstantBuffers(0, 1, cbs);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->Draw(3, 0);
        const D3D11_BOX box{(UINT)c.cell[0], (UINT)c.cell[1], 0, (UINT)c.cell[0] + kCell, (UINT)c.cell[1] + kCell, 1};
        ctx->CopySubresourceRegion(stage.Get(), 0, 0, 0, 0, rt.Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
        out->resize((size_t)kCell * kCell);
        for (uint32_t y = 0; y < kCell; ++y) std::memcpy(out->data() + (size_t)y * kCell, (const uint8_t*)m.pData + (size_t)y * m.RowPitch, kCell * sizeof(mirror::F4));
        ctx->Unmap(stage.Get(), 0);
        return true;
    }
};

float MaxDiff(const std::vector<mirror::F4>& a, const std::vector<mirror::F4>& b, size_t* at = nullptr) {
    float worst = 0.0f;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const float d = std::max(std::max(std::fabs(a[i].r - b[i].r), std::fabs(a[i].g - b[i].g)), std::max(std::fabs(a[i].b - b[i].b), std::fabs(a[i].a - b[i].a)));
        if (d > worst) { worst = d; if (at) *at = i; }
    }
    return worst;
}

// -- pictures --------------------------------------------------------------------------------------------------------------------------------
uint32_t Crc(const uint8_t* d, size_t n, uint32_t c = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) { uint32_t v = i; for (int k = 0; k < 8; ++k) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1; table[i] = v; }
        made = true;
    }
    for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c;
}
void Put32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back((uint8_t)(x >> s)); }
void Chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    Put32(png, (uint32_t)data.size());
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    png.insert(png.end(), td.begin(), td.end());
    Put32(png, Crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
}
// A PNG with no compression (stored deflate blocks): big, and needs nothing.
bool WritePng(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * (1 + 3 * w));
    for (int y = 0; y < h; ++y) { raw.push_back(0); raw.insert(raw.end(), rgb.begin() + (size_t)y * 3 * w, rgb.begin() + (size_t)(y + 1) * 3 * w); }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521u; b = (b + a) % 65521u; }
    for (size_t pos = 0; pos < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n >= raw.size() ? 1 : 0);
        z.push_back((uint8_t)(n & 0xFF)); z.push_back((uint8_t)(n >> 8)); z.push_back((uint8_t)(~n & 0xFF)); z.push_back((uint8_t)((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    Put32(z, (b << 16) | a);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A}, ihdr;
    Put32(ihdr, (uint32_t)w); Put32(ihdr, (uint32_t)h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
    fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return true;
}

struct Img {
    int w, h;
    std::vector<uint8_t> rgb;
    Img(int W, int H) : w(W), h(H), rgb((size_t)W * H * 3, 0) {}
    void Put(int x, int y, float r, float g, float b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* p = &rgb[((size_t)y * w + x) * 3];
        p[0] = (uint8_t)(LinearToSrgb(r) * 255.0f + 0.5f);
        p[1] = (uint8_t)(LinearToSrgb(g) * 255.0f + 0.5f);
        p[2] = (uint8_t)(LinearToSrgb(b) * 255.0f + 0.5f);
    }
};

float Hash(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFFu) / 65535.0f;
}
float Noise(float x, float y) {
    const int ix = (int)std::floor(x), iy = (int)std::floor(y);
    const float fx = x - (float)ix, fy = y - (float)iy;
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float a = Hash(ix, iy), b = Hash(ix + 1, iy), c = Hash(ix, iy + 1), d = Hash(ix + 1, iy + 1);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}
// The scene behind the runes: a dim dungeon with two torches' glow, a bright day on sand under a pale sky (linear light).
void Scene(bool bright, int w, int h, int x, int y, float* rgb) {
    const float fy = (float)y / (float)h, n = Noise((float)x * 0.04f, (float)y * 0.04f) * 0.6f + Noise((float)x * 0.15f, (float)y * 0.15f) * 0.4f;
    if (bright) {
        const float sky = fy < 0.45f ? 1.0f - fy / 0.45f : 0.0f;
        rgb[0] = 0.62f + 0.18f * sky + 0.10f * n; rgb[1] = 0.55f + 0.30f * sky + 0.08f * n; rgb[2] = 0.38f + 0.52f * sky + 0.05f * n;
        return;
    }
    float torch = 0.0f;
    static const float kTorch[2][2] = {{0.15f, 0.55f}, {0.88f, 0.45f}};
    for (const float* t : kTorch) {
        const float dx = (float)x / (float)w - t[0], dy = (fy - t[1]) * 0.75f;
        torch += std::exp(-(dx * dx + dy * dy) * 30.0f);
    }
    const float base = 0.010f + 0.030f * n;
    rgb[0] = base * 1.1f + 0.10f * torch; rgb[1] = base * 0.9f + 0.045f * torch; rgb[2] = base * 0.8f + 0.012f * torch;
}

// A layer as the compositor would show it to a head at kHead looking down -z: a pinhole of `fov` across, the quad found by the ray's
// intersection with its plane, its cell sampled with bilinear filtering; front side only; premultiplied over what is behind.
struct Hung {
    V3 centre;
    Quat q;
    float side;
    const std::vector<mirror::F4>* cell;
};
mirror::F4 Sample(const std::vector<mirror::F4>& c, float u, float v) {   // u, v in 0..1, v down
    const float x = std::min(std::max(u * (float)kCell - 0.5f, 0.0f), (float)kCell - 1.001f), y = std::min(std::max(v * (float)kCell - 0.5f, 0.0f), (float)kCell - 1.001f);
    const int x0 = (int)x, y0 = (int)y;
    const float fx = x - (float)x0, fy = y - (float)y0;
    const mirror::F4 a = c[(size_t)y0 * kCell + x0], b = c[(size_t)y0 * kCell + x0 + 1], cc = c[(size_t)(y0 + 1) * kCell + x0], d = c[(size_t)(y0 + 1) * kCell + x0 + 1];
    auto mix = [&](float A, float B, float C, float D) { return (A + (B - A) * fx) + ((C + (D - C) * fx) - (A + (B - A) * fx)) * fy; };
    return {mix(a.r, b.r, cc.r, d.r), mix(a.g, b.g, cc.g, d.g), mix(a.b, b.b, cc.b, d.b), mix(a.a, b.a, cc.a, d.a)};
}
Img View(const std::vector<Hung>& layers, bool bright, int w, int h, float fovDeg) {
    Img img(w, h);
    const float half = std::tan(0.5f * fovDeg * kPi / 180.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float px[3];
            Scene(bright, w, h, x, y, px);
            const V3 d{(((float)x + 0.5f) / (float)w * 2.0f - 1.0f) * half, (1.0f - ((float)y + 0.5f) / (float)h * 2.0f) * half * (float)h / (float)w, -1.0f};
            for (const Hung& L : layers) {
                const V3 n = Rotate(L.q, {0, 0, 1});
                const float dn = Dot(d, n);
                if (!(dn < 0.0f)) continue;   // a quad is seen from its front only
                const float t = Dot(L.centre - kHead, n) / dn;
                if (!(t > 0.0f)) continue;
                const V3 local = d * t + kHead - L.centre;
                const float u = Dot(local, Rotate(L.q, {1, 0, 0})) / L.side + 0.5f, v = Dot(local, Rotate(L.q, {0, 1, 0})) / L.side + 0.5f;
                if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) continue;
                const mirror::F4 s = Sample(*L.cell, u, 1.0f - v);
                const float k = 1.0f - s.a;
                px[0] = s.r + px[0] * k; px[1] = s.g + px[1] * k; px[2] = s.b + px[2] * k;
            }
            img.Put(x, y, px[0], px[1], px[2]);
        }
    return img;
}

void Blit(Img& dst, const Img& src, int ox, int oy) {
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x)
            if (ox + x < dst.w && oy + y < dst.h) std::memcpy(&dst.rgb[((size_t)(oy + y) * dst.w + ox + x) * 3], &src.rgb[((size_t)y * src.w + x) * 3], 3);
}

// A cell over a flat colour, for looking at the shader's own pixels.
Img CellOver(const std::vector<mirror::F4>& c, float bg) {
    Img img((int)kCell, (int)kCell);
    for (uint32_t y = 0; y < kCell; ++y)
        for (uint32_t x = 0; x < kCell; ++x) {
            const mirror::F4 s = c[(size_t)y * kCell + x];
            const float k = 1.0f - s.a;
            img.Put((int)x, (int)y, s.r + bg * k, s.g + bg * k, s.b + bg * k);
        }
    return img;
}

void TestShaderOnWarp(const std::string& dump) {
    Gpu gpu;
    if (!gpu.Init()) { Expect(false, ("the shader on WARP: " + gpu.why).c_str()); return; }
    Expect(true, "the shader's HLSL compiles (vs_5_0, ps_5_0) and runs on a WARP device");
    struct Case { const char* name; float progress, flash; bool trail; float handR; };
    const Case cases[] = {{"picture, nothing matched", 0.0f, 0.0f, false, 0.10f}, {"picture, a fifth matched", 0.2f, 0.0f, false, 0.10f},
                          {"picture, 60% matched", 0.6f, 0.0f, false, 0.10f}, {"picture, complete and flashing", 1.0f, 0.8f, false, 0.10f},
                          {"trail, a fifth", 0.2f, 0.0f, true, 0.10f}, {"trail, 60%", 0.6f, 0.0f, true, 0.10f}, {"trail, complete and flashing", 1.0f, 0.8f, true, 0.10f},
                          {"trail, a wide circle", 0.9f, 0.0f, true, 0.22f}};
    float worstAll = 0.0f;
    for (const Case& cs : cases) {
        BW_LinkVrRune r = CircleRune(cs.progress, cs.flash, 0.9f, 1, cs.handR);
        const Look look = LookOf(r);
        static CellCb cb;
        static TrailBoard tb;
        bool built = false;
        if (cs.trail) {
            built = ProjectTrail(r, look, kHead, {0.0f, 0.0f, -1.0f}, &tb);
            if (built) FillTrail(&cb, look, tb, 1);
        } else {
            Panel p;
            built = PicturePanel(r, look, &p);
            if (built) FillPicture(&cb, r, look, p, 1);
        }
        if (!built) { Expect(false, cs.name); continue; }
        std::vector<mirror::F4> gpuPix, cpuPix;
        if (!gpu.Draw(cb, &gpuPix)) { Expect(false, "a cell could not be read back"); continue; }
        mirror::RenderCell(cb, &cpuPix);
        size_t at = 0;
        const float diff = MaxDiff(gpuPix, cpuPix, &at);
        worstAll = std::max(worstAll, diff);
        char what[160];
        snprintf(what, sizeof what, "the shader and the CPU mirror agree pixel by pixel: %s (worst %.5f at %zu,%zu)", cs.name, diff, at % kCell, at / kCell);
        Expect(diff < 3e-3f, what);
        // the cell is written whole (the clear colour is gone) and holds a picture: pixels with colour, and clear corners
        float most = 0.0f;
        for (const mirror::F4& p : gpuPix) most = std::max(most, p.a);
        Expect(most > 0.5f && gpuPix[0].a < 0.01f && gpuPix.back().a < 0.01f && gpuPix[0].r < 0.01f, "the cell is written whole: a line, and clear corners");
        // premultiplied: no channel above its alpha plus the halo's add-light share (the halo adds light: rgb may exceed alpha)
        bool sane = true;
        for (const mirror::F4& p : gpuPix) if (!(p.r >= 0 && p.r <= 1 && p.g >= 0 && p.g <= 1 && p.b >= 0 && p.b <= 1 && p.a >= 0 && p.a <= 1)) sane = false;
        Expect(sane, "every channel is in 0..1");
    }
    printf("      the shader against the mirror: worst difference %.6f over %zu cells\n", worstAll, sizeof cases / sizeof cases[0]);
    // what the lit/unlit split looks like to the shader: the matched part is brighter than the rest
    {
        BW_LinkVrRune r = CircleRune(0.5f);
        const Look look = LookOf(r);
        Panel p;
        PicturePanel(r, look, &p);
        static CellCb cb;
        FillPicture(&cb, r, look, p, 0);
        std::vector<mirror::F4> px;
        gpu.Draw(cb, &px);
        // the circle starts at the top and goes clockwise: at 50% the right-hand side (3 o'clock) is lit, the left (9 o'clock) is not
        const float ppm = (float)kCell / p.side;
        auto at = [&](float mx, float my) { return px[(size_t)(0.5f * kCell - my * ppm) * kCell + (size_t)(0.5f * kCell + mx * ppm)]; };
        const mirror::F4 right = at(0.12f, 0.0f), left = at(-0.12f, 0.0f);
        Expect(right.r > left.r && right.g > left.g && right.r > 0.5f, "half way round: the right side (3 o'clock) is lit in gold, the left is the dim rune colour");
        Expect(left.a > 0.5f && left.b > left.r * 0.8f, "the unmatched part is the cool grey-blue");
        // the glow: a pixel one line width out from the edge has some light, one far out none
        const mirror::F4 nearOut = at(0.12f + 0.012f, 0.0f), farOut = at(0.19f, 0.0f);
        Expect(nearOut.a > 0.02f && farOut.a < 0.002f, "a halo beside the line, gone before the cell's edge");
        // the dark rim: just outside the edge of an unlit line there is alpha but little light
        const mirror::F4 rim = at(-0.12f - 0.0085f, 0.0f);
        Expect(rim.a > 0.1f, "the dark rim just outside the edge of a line");
    }
    if (dump.empty()) return;

    // --- the pictures ---
    // cells, over a dark and a bright flat colour: the shader's own pixels, four progress values, picture and trail
    const float prog[4] = {0.2f, 0.6f, 1.0f, 1.0f};
    const float flash[4] = {0.0f, 0.0f, 0.0f, 0.8f};
    Img sheet(4 * (int)kCell, 4 * (int)kCell);
    std::vector<std::vector<mirror::F4>> pics(4), trails(4);
    Panel panels[4];
    static TrailBoard boards[4];
    for (int i = 0; i < 4; ++i) {
        BW_LinkVrRune r = CircleRune(prog[i], flash[i], i == 3 ? 0.8f : 1.0f);
        const Look look = LookOf(r);
        static CellCb cb;
        PicturePanel(r, look, &panels[i]);
        FillPicture(&cb, r, look, panels[i], 0);
        gpu.Draw(cb, &pics[i]);
        ProjectTrail(r, look, kHead, {0.0f, 0.0f, -1.0f}, &boards[i]);
        FillTrail(&cb, look, boards[i], 0);
        gpu.Draw(cb, &trails[i]);
        Blit(sheet, CellOver(pics[i], 0.02f), 0 * (int)kCell, i * (int)kCell);
        Blit(sheet, CellOver(pics[i], 0.80f), 1 * (int)kCell, i * (int)kCell);
        Blit(sheet, CellOver(trails[i], 0.02f), 2 * (int)kCell, i * (int)kCell);
        Blit(sheet, CellOver(trails[i], 0.80f), 3 * (int)kCell, i * (int)kCell);
    }
    Expect(WritePng(dump + "\\runes_cells.png", sheet.w, sheet.h, sheet.rgb), "runes_cells.png written");
    // what a head at (0, 1.65, 0) sees: the pictures (farther) then the trails over them, in a dark and a bright scene
    const char* names[4] = {"p20", "p60", "p100", "p100_fired"};
    for (int i = 0; i < 4; ++i) {
        std::vector<Hung> layers = {{panels[i].centre, panels[i].q, panels[i].side, &pics[i]}, {boards[i].centre, boards[i].q, boards[i].side, &trails[i]}};
        const Img dark = View(layers, false, 560, 420, 50.0f), light = View(layers, true, 560, 420, 50.0f);
        Img both(1120, 420);
        Blit(both, dark, 0, 0);
        Blit(both, light, 560, 0);
        const std::string path = dump + "\\runes_view_" + names[i] + ".png";
        Expect(WritePng(path, both.w, both.h, both.rgb), ("runes_view_" + std::string(names[i]) + ".png written").c_str());
    }
    printf("      pictures written to %s\n", dump.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    std::string dump;
    for (int i = 1; i + 1 < argc; ++i) if (std::string(argv[i]) == "--dump") dump = argv[i + 1];
    TestColours();
    TestLook();
    TestLit();
    TestTurns();
    TestPanel();
    TestTrail();
    TestBudget();
    TestEase();
    TestWatch();
    TestCb();
    TestShaderConstants();
    TestShaderOnWarp(dump);
    printf("rune_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
