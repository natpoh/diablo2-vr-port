// phantom_rect_test - vr/phantom_rect.h: where the vignette pass can change the picture. The projection against the shader's own ray
// (PixelRayDir: a pixel's ray passes through the point projected to it), the rectangles of each carrier, the cases that go full screen, the
// merge, the entry points' eyes. The pass drawn both ways on WARP: tools/phantom_rect_gpu_test.cpp.
#include <cmath>
#include <cstdio>
#include <string>

#include "phantom_rect.h"

using namespace phantomrect;
static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

// the shader's PixelRayDir, here: the ray of the pixel centre (px, py)
static V3 Ray(const Eye& e, float px, float py, int w, int h) {
    const float nx = px / w * 2.0f - 1.0f, ny = 1.0f - py / h * 2.0f;
    V3 d = Add(Add(Mul(e.r, (nx + e.sp[2]) / e.sp[0]), Mul(e.u, (ny + e.sp[3]) / e.sp[1])), Mul(e.b, -1.0f));
    const float l = std::sqrt(Dot(d, d));
    return Mul(d, 1.0f / l);
}

// D2R_DepthFog.fx's PointArcLight, its largest channel's light before the strength (PaLook.w) - the shader's numbers, the colour at 1
static float ArcLight(const Inputs& in, V3 ro, V3 rd, float time) {
    float best = 1e9f, at = 0.0f;
    for (int i = 0; i < 16; ++i) {
        const V3 a{in.paPts[i][0], in.paPts[i][1], in.paPts[i][2]}, b{in.paPts[i + 1][0], in.paPts[i + 1][1], in.paPts[i + 1][2]};
        const V3 ab = Sub(b, a), w = Sub(ro, a);
        const float abab = Dot(ab, ab), abd = Dot(ab, rd), wd = Dot(w, rd), wab = Dot(w, ab);
        const float den = abab - abd * abd;
        float s = den > 1e-8f ? std::clamp((wab - wd * abd) / den, 0.0f, 1.0f) : 0.0f;
        const float t = std::max(0.0f, s * abd - wd);
        s = abab > 1e-8f ? std::clamp(Dot(Sub(Add(ro, Mul(rd, t)), a), ab) / abab, 0.0f, 1.0f) : 0.0f;
        const V3 q = Sub(Add(ro, Mul(rd, t)), Add(a, Mul(ab, s)));
        const float dist = std::sqrt(Dot(q, q));
        if (dist < best) { best = dist; at = t; }
    }
    float light = 0.0f;
    const float r = std::max(in.paSize[0], at * in.paSize[3]);
    const float q = best / r;
    if (q < 14.0f) light += (std::exp(-q * 0.45f) * 0.55f + std::exp(-q * q * 1.5f)) * 1.0f * 1.0f * 1.0f;   // (pulse, surge, flicker at most 1)
    if (std::fabs(rd.y) > 1e-4f) {
        const float t = (in.paRing[1] - ro.y) / rd.y;
        if (t > 0.0f) {
            const float hx = ro.x + rd.x * t - in.paRing[0], hz = ro.z + rd.z * t - in.paRing[2];
            const float rr = std::sqrt(hx * hx + hz * hz);
            const float w = std::max(in.paSize[1], t * in.paSize[3]);
            const float R = in.paRing[3];
            const float edge = std::fabs(rr - R) / w;
            const float wave = time - std::floor(time);
            const float waveEdge = std::fabs(rr - R * wave) / w;
            float ring = std::exp(-edge * edge * 1.5f) * 1.3f + std::exp(-edge * 0.5f) * 0.35f;
            ring = ring + std::exp(-waveEdge * waveEdge * 1.5f) * (1.0f - wave) * 0.6f;
            const float fill = rr < R ? 0.12f : 0.0f;
            light += ring + fill + ring * ring * 0.15f;
        }
    }
    return light;
}

int main() {
    const int W = 1000, H = 800;
    Eye e;
    e.o = {0.3f, 0.1f, 0.0f};
    const float c = std::cos(0.2f), s = std::sin(0.2f);
    e.r = {c, 0, s}; e.u = {0, 1, 0}; e.b = {-s, 0, c};
    e.sp[0] = 0.9f; e.sp[1] = 1.1f; e.sp[2] = 0.15f; e.sp[3] = -0.05f;
    {   // a point projected, and the ray of that pixel passes through it (the inverse of the shader's own ray)
        const V3 p{0.8f, -0.4f, -6.0f};
        Rect r;
        CHECK(Project(e, p, p, W, H, &r));
        const float px = 0.5f * (r.x0 + r.x1), py = 0.5f * (r.y0 + r.y1);
        const V3 d = Ray(e, px, py, W, H), v = Sub(p, e.o);
        const float t = Dot(v, d);
        const V3 miss = Sub(v, Mul(d, t));
        const float pixel = std::sqrt(Dot(v, v)) * 2.0f / (W * 0.9f);   // about a pixel's width at that distance
        CHECK(std::sqrt(Dot(miss, miss)) < 1.5f * pixel);
        CHECK(r.x1 - r.x0 == 2 * kMarginPx + 1 || r.x1 - r.x0 == 2 * kMarginPx + 2);
    }
    {   // behind the eye: an empty rectangle (no pixel); the eye inside a box: none (full screen)
        Rect r;
        CHECK(Project(e, {0, 0, 5}, {0, 0, 5}, W, H, &r) && Area(r) == 0);
        CHECK(!ProjectSphere(e, Add(e.o, Mul(e.b, -0.1f)), 0.5f, W, H, &r));
    }
    Inputs in;
    Result res = Compute(in, e, W, H);
    CHECK(!res.full && res.n == 0);   // nothing on: nothing drawn
    in.orbs = true;
    const float orbs[2][4] = {{-1.0f, -1.0f, -5.0f, 0.2f}, {2.0f, -1.0f, -5.0f, 0.2f}};
    for (int i = 0; i < 2; ++i) for (int k = 0; k < 4; ++k) in.orbPos[i][k] = orbs[i][k];
    in.orbLook[0][1] = 1.0f; in.orbLook[1][1] = 1.0f;
    res = Compute(in, e, W, H);
    CHECK(!res.full && res.n == 2);
    {   // every pixel whose ray comes within 3.6 radii of an orb's middle (the halo's end) lies in a rectangle
        bool inside = true;
        int reached = 0;
        for (int y = 0; y < H; y += 2)
            for (int x = 0; x < W; x += 2) {
                const V3 d = Ray(e, x + 0.5f, y + 0.5f, W, H);
                for (int i = 0; i < 2; ++i) {
                    const V3 oc = Sub(e.o, {orbs[i][0], orbs[i][1], orbs[i][2]});
                    const float b = Dot(oc, d);
                    const float dmin = b < 0.0f ? std::sqrt(std::max(Dot(oc, oc) - b * b, 0.0f)) : std::sqrt(Dot(oc, oc));
                    if (dmin > 3.6f * orbs[i][3]) continue;
                    ++reached;
                    bool in1 = false;
                    for (int k = 0; k < res.n; ++k) in1 = in1 || (x >= res.r[k].x0 && x < res.r[k].x1 && y >= res.r[k].y0 && y < res.r[k].y1);
                    inside = inside && in1;
                }
            }
        CHECK(reached > 100 && inside);
        CHECK(Area(res.r[0]) + Area(res.r[1]) < W * H / 4);
    }
    in.orbLook[1][1] = 0.0f;   // an orb not shown: no rectangle for it
    CHECK(Compute(in, e, W, H).n == 1);
    in.orbLook[1][1] = 1.0f;
    in.orbPos[1][0] = -0.9f;   // overlapping ones joined
    res = Compute(in, e, W, H);
    CHECK(!res.full && res.n == 1);
    in.orbPos[1][0] = 2.0f;
    for (int k = 0; k < 6; ++k) {   // the full-screen ones, and their names for the log
        Inputs f = in;
        if (k == 0) f.lowHp = 0.3f;
        if (k == 1) f.lowMp = 0.2f;
        if (k == 2) f.hitbox = true;
        if (k == 3) f.phantom = true;
        if (k == 4) f.bone = true;
        if (k == 5) f.dbg = true;
        const int want[6] = {kWhyLowHp, kWhyLowMp, kWhyHitbox, kWhyPhantom, kWhyBone, kWhyDbg};
        const Result rf = Compute(f, e, W, H);
        CHECK(rf.full && rf.why == want[k]);
    }
    CHECK(res.why == kWhyNone || !res.full);
    CHECK(std::string(WhyName(kWhyEyeOrb)) == "eye at a wrist orb" && std::string(WhyName(99)) == "?");
    {   // an orb at the eye: full screen
        Inputs f = in;
        f.orbPos[0][0] = e.o.x; f.orbPos[0][1] = e.o.y; f.orbPos[0][2] = e.o.z - 0.1f;
        const Result rf = Compute(f, e, W, H);
        CHECK(rf.full && rf.why == kWhyEyeOrb);
    }
    {   // a hand hanging at the side: its orb's halo half behind the eye - cut at the eye's plane: rectangles, and every pixel whose ray (ahead
        // of the eye) comes within 3.6 radii of it lies in one; both behind the eye: nothing
        Inputs f;
        f.orbs = true;
        f.orbLook[0][1] = 1.0f;
        const float o[4] = {e.o.x, e.o.y - 1.0f, e.o.z + 0.05f, 0.22f};
        for (int k = 0; k < 4; ++k) f.orbPos[0][k] = o[k];
        const Result rh = Compute(f, e, W, H);
        CHECK(!rh.full);
        bool inside = true;
        int reached = 0;
        for (int y = 0; y < H; y += 2)
            for (int x = 0; x < W; x += 2) {
                const V3 d = Ray(e, x + 0.5f, y + 0.5f, W, H);
                const V3 oc = Sub(e.o, {o[0], o[1], o[2]});
                const float b = Dot(oc, d);
                const float dmin = b < 0.0f ? std::sqrt(std::max(Dot(oc, oc) - b * b, 0.0f)) : std::sqrt(Dot(oc, oc));
                if (dmin > 3.6f * o[3]) continue;
                ++reached;
                bool in1 = false;
                for (int k = 0; k < rh.n; ++k) in1 = in1 || (x >= rh.r[k].x0 && x < rh.r[k].x1 && y >= rh.r[k].y0 && y < rh.r[k].y1);
                inside = inside && in1;
            }
        CHECK(reached > 50 && inside);
        CHECK(rh.n >= 1 && Area(rh.r[0]) < W * H * 6 / 10);
        if (!(reached > 50 && inside) || rh.n < 1) std::printf("  hand at the side: reached %d, n %d\n", reached, rh.n);
        // a sweep of hands round the eye's plane (left, right, low, a little ahead and behind): never a pixel the halo reaches left out
        int placed = 0, escaped = 0;
        for (float dx = -0.9f; dx <= 0.91f; dx += 0.3f)
            for (float dy = -0.9f; dy <= -0.29f; dy += 0.3f)
                for (float dz = -0.3f; dz <= 0.31f; dz += 0.15f) {
                    Inputs g;
                    g.orbs = true;
                    g.orbLook[0][1] = 1.0f;
                    const float q[4] = {e.o.x + dx, e.o.y + dy, e.o.z + dz, 0.08f};
                    for (int k = 0; k < 4; ++k) g.orbPos[0][k] = q[k];
                    const Result rq = Compute(g, e, W / 4, H / 4);
                    if (rq.full) continue;
                    ++placed;
                    for (int y = 0; y < H / 4; ++y)
                        for (int x = 0; x < W / 4; ++x) {
                            const V3 d = Ray(e, x + 0.5f, y + 0.5f, W / 4, H / 4);
                            const V3 oc = Sub(e.o, {q[0], q[1], q[2]});
                            const float b = Dot(oc, d);
                            const float dmin = b < 0.0f ? std::sqrt(std::max(Dot(oc, oc) - b * b, 0.0f)) : std::sqrt(Dot(oc, oc));
                            if (dmin > 3.6f * q[3]) continue;
                            bool in1 = false;
                            for (int k = 0; k < rq.n; ++k) in1 = in1 || (x >= rq.r[k].x0 && x < rq.r[k].x1 && y >= rq.r[k].y0 && y < rq.r[k].y1);
                            escaped += in1 ? 0 : 1;
                        }
                }
        if (escaped) std::printf("  hands round the eye's plane: %d placements, %d pixels left out\n", placed, escaped);
        CHECK(placed > 60 && escaped == 0);
        f.orbPos[0][2] = e.o.z + 3.0f;   // behind
        const Result rb = Compute(f, e, W, H);
        CHECK(!rb.full && rb.n == 0);
    }
    {   // the point skill's arc: every pixel the shader lights by more than 1e-6 lies in a rectangle; some are lit; far from the whole picture
        Inputs f;
        f.pointArc = true;
        for (int i = 0; i < 17; ++i) {
            const float t = i / 16.0f;
            const float p[4] = {e.o.x + 0.3f + 0.7f * t, e.o.y - 0.5f + 1.2f * t - 2.7f * t * t, e.o.z - 0.8f - 7.2f * t, t};
            for (int k = 0; k < 4; ++k) f.paPts[i][k] = p[k];
        }
        const float ring[4] = {f.paPts[16][0], f.paPts[16][1], f.paPts[16][2], 0.5f}, size[4] = {0.012f, 0.03f, 1.3f, 0.0012f};
        for (int k = 0; k < 4; ++k) { f.paRing[k] = ring[k]; f.paSize[k] = size[k]; }
        const Result ra = Compute(f, e, W, H);
        CHECK(!ra.full && ra.n >= 1);
        int lit = 0, outside = 0;
        float worst = 0.0f;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                bool in1 = false;
                for (int k = 0; k < ra.n; ++k) in1 = in1 || (x >= ra.r[k].x0 && x < ra.r[k].x1 && y >= ra.r[k].y0 && y < ra.r[k].y1);
                const float l = ArcLight(f, e.o, Ray(e, x + 0.5f, y + 0.5f, W, H), 0.05f);   // (the wave near the circle's middle)
                if (l > 1e-3f) ++lit;
                if (!in1) { worst = std::max(worst, l); outside += l > 1e-6f; }
            }
        long long area = 0;
        for (int k = 0; k < ra.n; ++k) area += Area(ra.r[k]);
        std::printf("  point arc: %d rectangle(s), %.1f%% of the picture, %d pixels lit, the most light outside %.2g\n", ra.n, 100.0 * area / (W * H), lit, worst);
        CHECK(lit > 200 && outside == 0);
        CHECK(area < (long long)W * H * 6 / 10);
        Inputs g = f;
        g.paPts[3][1] = NAN;
        CHECK(Compute(g, e, W, H).why == kWhyNumbers);
        g = f;
        g.paPts[0][0] = e.o.x; g.paPts[0][1] = e.o.y; g.paPts[0][2] = e.o.z - 0.05f;   // the staff's tip at the eye
        CHECK(Compute(g, e, W, H).why == kWhyEyeArc);
    }
    {   // the quiver, the arrow, the merc: a rectangle each, apart
        Inputs f;
        f.qvN = 1; f.qvPos[0][0] = -2.0f; f.qvPos[0][1] = 1.0f; f.qvPos[0][2] = -4.0f; f.qvPos[0][3] = 0.05f;
        f.afOn = 1; f.afA[0] = 0.5f; f.afA[1] = 0.5f; f.afA[2] = -3.0f; f.afA[3] = 0.05f; f.afB[0] = 0.6f; f.afB[1] = 0.6f; f.afB[2] = -3.5f; f.afB[3] = 1.0f;
        f.merc = true; f.mercA[0] = 6.0f; f.mercA[1] = -2.0f; f.mercA[2] = -10.0f; f.mercA[3] = 0.8f;
        f.mercB[0] = 6.0f; f.mercB[1] = 0.0f; f.mercB[2] = -10.0f; f.mercB[3] = 0.5f;
        res = Compute(f, e, W, H);
        if (res.full || res.n != 3) { std::printf("  full %d n %d:", res.full ? 1 : 0, res.n); for (int k = 0; k < res.n; ++k) std::printf(" [%d %d %d %d]", res.r[k].x0, res.r[k].y0, res.r[k].x1, res.r[k].y1); std::printf("\n"); }
        CHECK(!res.full && res.n == 3);
        f.afA[0] = NAN;   // numbers that are not numbers: full screen (as before)
        CHECK(Compute(f, e, W, H).full);
    }
    {   // more rectangles than drawn: their one box
        std::vector<Rect> v;
        for (int i = 0; i < 12; ++i) v.push_back({i * 30, 0, i * 30 + 10, 10});
        Merge(v);
        CHECK(v.size() == 1 && v[0].x0 == 0 && v[0].x1 == 340);
    }
    CHECK(EyeOfEntry("F__PS_Phantom") == 0 && EyeOfEntry("PS_PhantomR") == 1 && EyeOfEntry("PS_OvMadePhantomR") == 1 && EyeOfEntry("PS_OvPhantom") == 0);
    CHECK(EyeOfEntry("PS_GameHud") == -1 && EyeOfEntry(nullptr) == -1);
    std::printf("phantom_rect_test: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
