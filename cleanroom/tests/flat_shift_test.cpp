// [camera] flat_shift_x / flat_shift_y (vr/flat_shift.h): the flat picture moved by a share of its size, and where a point lands on it. The mouse's
// ray through a pixel (d2rcam's RayThrough, the same inverse the game's ScreenToRay gets) still meets what is drawn there.
#include "flat_shift.h"
#include "mat4.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

using namespace d2rcam::m4;

int main() {
    float view[16], proj[16];
    LookTo({10.0f, 20.0f, 30.0f}, Normalize({0.3f, -0.6f, -1.0f}), {0.0f, 1.0f, 0.0f}, view);
    PerspectiveRevZ(60.0f * 3.14159265f / 180.0f, 16.0f / 9.0f, 0.5f, proj);
    float inv[16];
    Check(Inverse(view, inv), "the view inverts");

    // a point straight ahead is the middle; after the move it is where the slider says
    const V3 f = Normalize({0.3f, -0.6f, -1.0f});
    const float ahead[3] = {10.0f + f.x * 25.0f, 20.0f + f.y * 25.0f, 30.0f + f.z * 25.0f};
    float x = 0.0f, y = 0.0f;
    Check(flatshift::OnPicture(view, proj, ahead, &x, &y) && Near(x, 0.0f) && Near(y, 0.0f), "straight ahead is the middle");
    float moved[16];
    for (int i = 0; i < 16; ++i) moved[i] = proj[i];
    flatshift::Apply(moved, 12.5f, -7.0f);
    Check(flatshift::OnPicture(view, moved, ahead, &x, &y) && Near(x, 12.5f) && Near(y, -7.0f), "moved 12.5 % right and 7 % up");

    // any point moves by exactly the slider's amount (a shift, not a turn: no parallax)
    const float off[3] = {3.0f, 4.0f, 9.0f};
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    Check(flatshift::OnPicture(view, proj, off, &x0, &y0) && flatshift::OnPicture(view, moved, off, &x1, &y1), "an off-centre point is on the picture");
    Check(Near(x1 - x0, 12.5f) && Near(y1 - y0, -7.0f), "every point moves by the same share");

    // minus where a point is puts it in the middle (what the log's suggestion means)
    for (int i = 0; i < 16; ++i) moved[i] = proj[i];
    flatshift::Apply(moved, -x0, -y0);
    Check(flatshift::OnPicture(view, moved, off, &x1, &y1) && Near(x1, 0.0f) && Near(y1, 0.0f), "moved by minus its place, the point is the middle");

    // the mouse: the ray through the pixel a point is drawn at goes through the point, moved or not
    const float vpW = 1920.0f, vpH = 1080.0f;
    const float* const projs[2] = {proj, moved};
    for (const float* m : projs) {
        float px, py;
        Check(flatshift::OnPicture(view, m, off, &px, &py), "the point is on the picture");
        const float sx = (0.5f + px / 100.0f) * vpW, sy = (0.5f + py / 100.0f) * vpH;
        float o[3], d[3];
        Check(RayThrough(inv, m, vpW, vpH, sx, sy, o, d), "a ray through the pixel");
        const float to[3] = {off[0] - o[0], off[1] - o[1], off[2] - o[2]};
        const float t = to[0] * d[0] + to[1] * d[1] + to[2] * d[2];
        const float miss = std::hypot(to[0] - d[0] * t, std::hypot(to[1] - d[1] * t, to[2] - d[2] * t));
        Check(miss < 1e-3f, "the mouse's ray through where it is drawn meets it");
    }

    // the limits
    Check(flatshift::Clamp(80.0f) == 50.0f && flatshift::Clamp(-80.0f) == -50.0f && flatshift::Clamp(NAN) == 0.0f, "clamped to half the picture, NaN 0");
    const float behind[3] = {10.0f - f.x * 5.0f, 20.0f - f.y * 5.0f, 30.0f - f.z * 5.0f};
    Check(!flatshift::OnPicture(view, proj, behind, &x, &y), "behind the eye: not on the picture");

    if (g_fail) { std::printf("flat_shift_test: %d FAILED\n", g_fail); return 1; }
    std::printf("flat_shift_test: all passed\n");
    return 0;
}
