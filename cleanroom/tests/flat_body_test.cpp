// [input] flat_body_pick (vr/flat_body.h): a body or an object clicked by its body from a first-person eye - which one, and the cell the
// game's mouse point gets for it (on raised ground too).
#include "flat_body.h"

#include <cmath>
#include <cstdio>

static int g_fail = 0;
static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
}
static bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

using namespace flatbody;
using flatground::kShear;

// The ray from the eye to a point.
static void RayTo(const float eye[3], const float p[3], float d[3]) {
    for (int i = 0; i < 3; ++i) d[i] = p[i] - eye[i];
    const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int i = 0; i < 3; ++i) d[i] /= l;
}

int main() {
    // sizes
    float h, r;
    BodySize(2, 3, 90, true, &h, &r);
    Check(Near(h, 9.0f) && Near(r, 2.1f), "a row's height and footprint");
    BodySize(0, 0, 0, false, &h, &r);
    Check(Near(h, 7.0f) && Near(r, 1.8f), "no row: 7 high, 1.8 round");
    BodySize(8, 8, 250, true, &h, &r);
    Check(Near(h, 7.0f) && Near(r, 3.5f), "an odd height falls back, a big footprint is capped");
    Check(ObjKind(43) == 3 && Near(ObjRadius(43), 3.6f) && Near(ObjHeight(3), 5.5f), "a chest's kind and size");
    Check(!ObjTarget(0, 0) && ObjTarget(3, 1) && !ObjTarget(3, 2) && ObjTarget(4, 2) && ObjTarget(5, 3), "used doors / chests are no target, portals stay");

    for (const float floor : {0.0f, 6.0f, -4.0f}) {   // base ground, a ledge, a pit: the hero and the units on the same floor
        const float eye[3] = {100.0f + floor * kShear, floor + 5.3f, 100.0f + floor * kShear};
        Cap caps[3];
        caps[0] = {120.0f, 100.0f, floor, 9.0f, 2.1f, true, 1};    // a monster 20 ahead (+x)
        caps[1] = {140.0f, 100.0f, floor, 9.0f, 2.1f, true, 2};    // another behind it
        caps[2] = {100.0f, 130.0f, floor, 5.5f, 3.6f, false, 3};   // a chest to the side (+z)
        float base[3], chest[3], d[3];
        Hit hit;
        // the crosshair on the monster's chest (6 up): it, and its cell is its place less 0.6 * 9 * k (the clamp)
        Base(caps[0], base);
        chest[0] = base[0] - 2.0f; chest[1] = base[1] + 6.0f; chest[2] = base[2];
        RayTo(eye, chest, d);
        Check(Pick(caps, 3, eye, d, INFINITY, &hit) && hit.index == 0, "the chest of the nearer monster: it");
        const float up = std::fmin(std::fmax(eye[1] + hit.t * d[1] - floor, 0.25f * 9.0f), 0.6f * 9.0f);
        Check(Near(hit.cellX, (120.0f - up * kShear) / 2.0f) && Near(hit.cellZ, (100.0f - up * kShear) / 2.0f), "its cell: its place, back by the height met");
        Check(up > 0.25f * 9.0f && up <= 0.6f * 9.0f, "met on its body, within 25 - 60 %");
        // the head (above 60 %): the cell stops at 60 %
        chest[0] = base[0] - 2.0f; chest[1] = base[1] + 8.8f;
        RayTo(eye, chest, d);
        Check(Pick(caps, 3, eye, d, INFINITY, &hit) && hit.index == 0 && Near(hit.cellX, (120.0f - 5.4f * kShear) / 2.0f, 2e-3f), "the head: 60 % of its height");
        // the chest object: its own place, no height taken
        Base(caps[2], base);
        chest[0] = base[0]; chest[1] = base[1] + 3.0f; chest[2] = base[2] - 3.0f;
        RayTo(eye, chest, d);
        Check(Pick(caps, 3, eye, d, INFINITY, &hit) && hit.index == 2 && Near(hit.cellX, 50.0f) && Near(hit.cellZ, 65.0f), "an object: its own cell");
        // the ground met first: no pick (the floor hides it)
        Check(!Pick(caps, 3, eye, d, hit.t * 0.5f, &hit), "the ground before it: nothing picked");
        // a ray past everything: nothing
        const float sky[3] = {eye[0] - 10.0f, eye[1] + 3.0f, eye[2] - 10.0f};
        RayTo(eye, sky, d);
        Check(!Pick(caps, 3, eye, d, INFINITY, &hit), "the air: nothing");
    }
    // the eye inside a capsule (third person's camera behind a monster's back is not, but a body pressed to the hero is): it, at once
    {
        const float eye[3] = {0.0f, 5.0f, 0.0f}, d[3] = {1.0f, 0.0f, 0.0f};
        const Cap c[1] = {{0.5f, 0.0f, 0.0f, 9.0f, 2.0f, true, 7}};
        Hit hit;
        Check(Pick(c, 1, eye, d, INFINITY, &hit) && hit.index == 0 && hit.t < 0.1f, "inside a capsule: that one");
    }

    if (g_fail) { std::printf("flat_body_test: %d FAILED\n", g_fail); return 1; }
    std::printf("flat_body_test: all passed\n");
    return 0;
}
