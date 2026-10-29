// Offline checks of [debug] melee_boxes (vr/melee_boxes.h, melee_boxes.cpp): the list of capsules, the frame conversion to the numbers
// the effect takes (checked against a CPU copy of D2R_DepthFog.fx's projection, both eyes), the struck flash, the off state, the log
// line's numbers and the hand-speed meter. No game, no headset.
//
//   melee_boxes_test.exe                 the checks (exit code 0 = all passed)
//   melee_boxes_test.exe --dump out.png  also draws a synthetic scene as the effect would, left and right eye side by side
//
// The CPU copy of the effect (PixelRayDir, CapsuleHit, Hitboxes) is the HLSL, line for line; if the effect changes, change it here too.

#include "../../vr/melee_bodies.h"
#include "../../vr/melee_boxes.h"
#include "../../vr/stroke_tuning.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace stroke;
using namespace meleeboxes;

static int g_failures = 0;
static void Expect(bool ok, const char* what, double value = 0.0) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s (%g)\n", what, value);
    }
}
static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static bool NearV(Vec a, Vec b, float tol) { return Length(a - b) <= tol; }

// ---- a CPU copy of the effect's side of it -----------------------------------------------------------------------------------------
struct Camera {
    Vec right, up, back;      // CamRight / CamUp / CamBack: the camera's axes in the world
    float sp[4];              // SkyProj: x, y scale, z, w offset
    Vec eye;                  // PhantomEye: this eye's camera relative to where the hands hang
    int width = 800, height = 800;
};
struct Pixel { float x, y, ahead; };
// PhantomPixel: a point relative to where the hands hang -> its pixel; ahead < 0 behind the eye.
static Pixel ToPixel(const Camera& c, Vec v) {
    v = v - c.eye;
    const Vec k{Dot(v, c.right), Dot(v, c.up), Dot(v, c.back)};
    const float w = std::fmax(-k.z, 1e-4f);
    const float ndcx = k.x / w * c.sp[0] - c.sp[2], ndcy = k.y / w * c.sp[1] - c.sp[3];
    return {(ndcx * 0.5f + 0.5f) * c.width, (0.5f - ndcy * 0.5f) * c.height, -k.z};
}
// PixelRayDir
static Vec RayDir(const Camera& c, float px, float py) {
    const float nx = px / c.width * 2.0f - 1.0f, ny = 1.0f - py / c.height * 2.0f;
    return Normalized(c.right * ((nx + c.sp[2]) / c.sp[0]) + c.up * ((ny + c.sp[3]) / c.sp[1]) - c.back);
}
// CapsuleHit (Inigo Quilez's), as the effect has it
static float CapsuleHit(Vec ro, Vec rd, Vec pa, Vec pb, float r) {
    const Vec ba = pb - pa, oa = ro - pa;
    const float baba = Dot(ba, ba), bard = Dot(ba, rd), baoa = Dot(ba, oa), rdoa = Dot(rd, oa), oaoa = Dot(oa, oa);
    const float a = baba - bard * bard, b = baba * rdoa - baoa * bard, cc = baba * oaoa - baoa * baoa - r * r * baba;
    float h = b * b - a * cc;
    if (h >= 0.0f && std::fabs(a) > 1e-8f) {
        const float t = (-b - std::sqrt(h)) / a;
        const float y = baoa + t * bard;
        if (y > 0.0f && y < baba) return t;
        const Vec oc = y <= 0.0f ? oa : ro - pb;
        const float b2 = Dot(rd, oc), c2 = Dot(oc, oc) - r * r;
        h = b2 * b2 - c2;
        if (h > 0.0f) return -b2 - std::sqrt(h);
    }
    return -1.0f;
}
struct Rgb { float r, g, b; };
static Rgb Lerp(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
static float Sat(float x) { return std::fmin(1.0f, std::fmax(0.0f, x)); }
// Hitboxes
static Rgb Hitboxes(Rgb c, const Camera& cam, float px, float py, const float (*a)[4], const float (*b)[4], int n) {
    const Vec ro = cam.eye, rd = RayDir(cam, px, py);
    for (int i = 0; i < n && i < kMaxCapsules; ++i) {
        const Vec pa{a[i][0], a[i][1], a[i][2]}, pb{b[i][0], b[i][1], b[i][2]};
        const float r = a[i][3], kind = b[i][3];
        const float t = CapsuleHit(ro, rd, pa, pb, r);
        if (t <= 0.0f) continue;
        const Vec q = ro + rd * t, ab = pb - pa;
        const float k = Sat(Dot(q - pa, ab) / std::fmax(Dot(ab, ab), 1e-6f));
        const Vec nrm = Normalized(q - (pa + ab * k));
        const float rim = 1.0f - std::fabs(Dot(nrm, rd));
        Rgb col = kind < 0.5f ? Rgb{0.1f, 0.9f, 1.0f} : kind < 1.5f ? Rgb{1.0f, 0.85f, 0.1f} : kind < 2.5f ? Rgb{1.0f, 0.1f, 0.05f}
                  : kind < 3.5f ? Rgb{1.0f, 0.1f, 0.9f} : kind < 4.5f ? Rgb{0.75f, 0.8f, 0.9f} : kind < 5.5f ? Rgb{0.2f, 1.0f, 0.6f}
                  : kind < 6.5f ? Rgb{0.15f, 1.0f, 0.15f} : kind < 7.5f ? Rgb{1.0f, 1.0f, 1.0f} : kind < 8.5f ? Rgb{1.0f, 0.5f, 0.0f}
                  : kind < 9.5f ? Rgb{0.1f, 0.9f, 1.0f} : kind < 10.5f ? Rgb{0.15f, 1.0f, 0.15f} : Rgb{0.6f, 0.35f, 1.0f};
        float alpha = kind > 1.5f && kind < 3.5f ? 0.45f : kind > 3.5f && kind < 4.5f ? 0.3f : kind > 5.5f && kind < 6.5f ? 0.35f : kind > 6.5f && kind < 8.5f ? 0.85f
                      : kind > 8.5f && kind < 9.5f ? 0.3f : kind > 9.5f && kind < 10.5f ? 0.6f : 0.15f;
        if (kind > 15.5f) {   // the strike rules' colours: kind = 16 + 4 * colour + style (the effect's, line for line)
            const float cs = kind - 16.0f + 0.5f, ci = std::floor(cs / 4.0f), st = std::floor(cs - ci * 4.0f);
            col = ci < 0.5f ? Rgb{0.15f, 0.3f, 1.0f} : ci < 1.5f ? Rgb{1.0f, 0.72f, 0.1f} : ci < 2.5f ? Rgb{0.7f, 0.7f, 0.7f} : ci < 3.5f ? Rgb{0.15f, 1.0f, 0.15f} : Rgb{1.0f, 0.15f, 0.1f};
            alpha = st < 0.5f ? 0.85f : st < 1.5f ? 0.35f : 0.6f;
        }
        c = Lerp(c, col, Sat(alpha + 0.6f * rim * rim));
    }
    return c;
}

// ---- a synthetic scene ---------------------------------------------------------------------------------------------------------------
// Four game units to the metre. The hero's model stands at (1000, floor, 2000) world units; his eyes (where the hands hang) 1.6 m above
// it; the camera looks along +x, 12 degrees down. Cell numbers are game WORLD units (twice the cell).
constexpr float kUpm = 4.0f;
struct World {
    float floorM = 0.0f;
    Vec hero{1000.0f, 0.0f, 2000.0f};   // world units: the hero model's origin
    Vec eyeWorld;                       // world units: where the hands hang (vrcam's g_eyeWorld)
    Camera cam[2];
};
static World MakeWorld(float floorM) {
    World w;
    w.floorM = floorM;
    w.hero = {1000.0f + floorM * kUpm * meleetouch::kGroundShear, floorM * kUpm, 2000.0f + floorM * kUpm * meleetouch::kGroundShear};
    w.eyeWorld = w.hero + Vec{0.0f, 1.6f * kUpm, 0.0f};
    const float pitch = -12.0f * 0.0174532925f;
    const Vec fwd{std::cos(pitch), std::sin(pitch), 0.0f};
    const Vec worldUp{0.0f, 1.0f, 0.0f};
    // right = fwd x up, up = right x fwd, back = -fwd
    auto cross = [](Vec a, Vec b) { return Vec{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    const Vec right = Normalized(cross(fwd, worldUp)), up = cross(right, fwd);
    for (int e = 0; e < 2; ++e) {
        Camera& c = w.cam[e];
        c.right = right;
        c.up = up;
        c.back = fwd * -1.0f;
        c.sp[0] = 1.0f; c.sp[1] = 1.0f; c.sp[2] = e ? -0.05f : 0.05f; c.sp[3] = 0.0f;   // an asymmetric projection, as the runtime's eyes have
        c.eye = right * ((e ? 0.032f : -0.032f) * kUpm);                                // half an eye gap (32 mm) each way
    }
    return w;
}
static Vec Metres(Vec worldUnits) { return worldUnits * (1.0f / kUpm); }

static Monster MakeMonster(uint32_t id, uint32_t cls, const meleetouch::MonsterRow& row, float gridX, float gridZ, const World& w, Verdict v = Verdict::Hostile) {
    Monster m;
    m.id = id;
    m.cls = cls;
    const Vec grid = meleetouch::GridPlane(w.hero);
    m.d2 = (gridX - grid.x) * (gridX - grid.x) + (gridZ - grid.z) * (gridZ - grid.z);
    m.verdict = v;
    Expect(meleetouch::BodyFor(row, gridX, gridZ, w.floorM, kUpm, m.body), "synthetic monster body");
    return m;
}

// The sword in the right hand, 0.45 m forward and 0.25 m to the right of the eyes, pointing forward and a little up and left.
static Blade MakeBlade(const World& w) {
    const Vec base = Metres(w.eyeWorld) + Vec{0.45f, -0.45f, 0.25f};
    const Vec axis = Normalized(Vec{1.0f, 0.25f, -0.35f});
    const meleetouch::BladeShape shape = meleetouch::WeaponBlade(D2RVR_TYPE_SWORD, false);
    return {base, base + axis * shape.length, shape.radius};
}

static Frame MakeFrame(const World& w, uint64_t now, const Blade& blade, const Monster* mons, size_t n) {
    Frame f;
    f.directionColour = kCBlue;   // (the scene's tests below judge by blue; the module's own default is red - tested apart)
    f.nowMs = now;
    f.upm = kUpm;
    f.blades[0].have = true;
    f.blades[0].blade = blade;
    f.floorY = w.floorM;
    f.monsters = mons;
    f.count = n;
    return f;
}

static int g_sinkCalls = 0;
static std::string g_sinkLast;
static void Sink(const char* line) { ++g_sinkCalls; g_sinkLast = line; }

// ---- PNG for the dump (stored deflate: no compression, no zlib) ----------------------------------------------------------------------
static uint32_t Crc32(const uint8_t* d, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}
static void Put32(std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<uint8_t>(x >> s)); }
static void Chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> head;
    Put32(head, static_cast<uint32_t>(data.size()));
    std::fwrite(head.data(), 1, 4, f);
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    std::fwrite(body.data(), 1, body.size(), f);
    std::vector<uint8_t> crc;
    Put32(crc, ~Crc32(body.data(), body.size()));
    std::fwrite(crc.data(), 1, 4, f);
}
static bool WritePng(const char* path, int w, int h, const std::vector<uint8_t>& rgb) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    Put32(ihdr, static_cast<uint32_t>(w)); Put32(ihdr, static_cast<uint32_t>(h));
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Chunk(f, "IHDR", ihdr);
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + static_cast<size_t>(y) * w * 3, rgb.begin() + static_cast<size_t>(y + 1) * w * 3);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t at = 0; at < raw.size();) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n)); z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n)); z.push_back(static_cast<uint8_t>((~n) >> 8));
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
        at += n;
    }
    uint32_t a = 1, b = 0;
    for (const uint8_t c : raw) { a = (a + c) % 65521u; b = (b + a) % 65521u; }
    Put32(z, (b << 16) | a);
    Chunk(f, "IDAT", z);
    Chunk(f, "IEND", {});
    std::fclose(f);
    return true;
}

// What the picture shows: a dark floor with 1 m lines on the game's grid, a thin pole where every "model" stands (feet on the floor, 1.8 m
// tall - a stand-in for the drawn monster, so the capsule can be seen to enclose it), then the capsules as the effect draws them.
struct Marker { Vec feetWorld; float heightM; };
static std::vector<uint8_t> Render(const World& w, const std::vector<Marker>& poles, int n, const float (*a)[4], const float (*b)[4]) {
    const int W = w.cam[0].width, H = w.cam[0].height;
    std::vector<uint8_t> img(static_cast<size_t>(W) * 2 * H * 3);
    for (int e = 0; e < 2; ++e) {
        const Camera& cam = w.cam[e];
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const Vec rd = RayDir(cam, x + 0.5f, y + 0.5f);
                const Vec ro = cam.eye;   // relative to where the hands hang
                Rgb c{0.10f, 0.12f, 0.16f};
                const float floorRel = w.floorM * kUpm - w.eyeWorld.y;   // the floor's height relative to the eyes
                if (rd.y < -1e-4f) {
                    const float t = (floorRel - ro.y) / rd.y;
                    if (t > 0.0f) {
                        // the point on the floor, in the world: back to the grid plane (the render position less its height times the shear)
                        const Vec p = w.eyeWorld + ro + rd * t;
                        const Vec g = meleetouch::GridPlane(p);
                        const float fx = std::fabs(std::fmod(g.x, kUpm)), fz = std::fabs(std::fmod(g.z, kUpm));
                        const bool line = fx < 0.06f * kUpm || fx > kUpm - 0.06f * kUpm || fz < 0.06f * kUpm || fz > kUpm - 0.06f * kUpm;
                        c = line ? Rgb{0.32f, 0.34f, 0.40f} : Rgb{0.16f, 0.17f, 0.20f};
                    }
                }
                for (const Marker& m : poles) {   // the model stand-in
                    const Vec feet = m.feetWorld - w.eyeWorld;
                    const Vec head = feet + Vec{0.0f, m.heightM * kUpm, 0.0f};
                    if (CapsuleHit(ro, rd, feet, head, 0.04f * kUpm) > 0.0f) c = {0.85f, 0.85f, 0.85f};
                }
                c = Hitboxes(c, cam, x + 0.5f, y + 0.5f, a, b, n);
                uint8_t* px = &img[(static_cast<size_t>(y) * W * 2 + (e * W + x)) * 3];
                px[0] = static_cast<uint8_t>(Sat(c.r) * 255.0f); px[1] = static_cast<uint8_t>(Sat(c.g) * 255.0f); px[2] = static_cast<uint8_t>(Sat(c.b) * 255.0f);
            }
    }
    return img;
}

// ---- the tests ----------------------------------------------------------------------------------------------------------------------
static void SpeedTests() {
    // the peak since the last look (a 5 Hz line of a 90 Hz motion)
    PeakTracker pk;
    pk.Note(1.0f);
    pk.Note(3.5f);
    pk.Note(2.0f);
    Expect(Near(pk.Take(0.4f), 3.5f, 1e-6f), "peak: the highest speed since the last look");
    Expect(Near(pk.Take(0.0f), 0.4f, 1e-6f), "peak: then it restarts from the speed now");
    pk.Note(std::nanf(""));
    Expect(Near(pk.Take(0.0f), 0.0f, 1e-6f), "peak: a speed that is not a number is nothing");

    // THE SPEED IS THE ENGINE'S: a real engine driven by a hand moving at a given speed, its LastSpeed / Counting handed to the scene. The
    // blade is green exactly when the engine's step swept something (a contact would count), never by a second estimate.
    const World w = MakeWorld(0.0f);
    const Blade blade0 = MakeBlade(w);
    for (const float minSpeed : {0.8f, 2.0f}) {
        stroke::Engine eng(stroke::ParamsFor({minSpeed, 8.0f}));
        Scene scene;
        double t = 1.0;
        Vec hand{0.0f, 1.0f, 0.0f};
        int disagree = 0, hotSteps = 0, coldFast = 0;
        for (int phase = 0; phase < 3; ++phase) {
            const float v = phase == 0 ? 0.3f : phase == 1 ? 1.5f : 3.0f;   // m/s: rest-ish, a gentle swing, a hard swing
            for (int i = 0; i < 40; ++i) {
                t += 1.0 / 90.0;
                hand.x += v / 90.0f;
                Sample s;
                s.time = t;
                s.active = true;
                s.epoch = 1;
                s.context = 1;
                s.hand = {hand, {}};
                s.blade = {hand, hand + Vec{0.0f, 0.0f, 0.65f}, 0.04f};
                s.anchor = {hand.x, 1.0f, 0.0f};
                s.scale = kUpm;
                Sweep sw;
                const bool swept = eng.Step(s, sw);
                Frame f = MakeFrame(w, 1000 + static_cast<uint64_t>(t * 100.0), blade0, nullptr, 0);
                f.minSpeed = minSpeed;
                f.blades[0].inStroke = eng.InStroke();
                f.blades[0].speed = eng.LastSpeed();
                f.blades[0].counting = eng.Counting();
                scene.Set(f);
                Capsule c[kMaxCapsules];
                const int n = scene.Capsules(f.nowMs, c, kMaxCapsules);
                const bool hot = n >= 1 && c[0].kind == kBladeHot;
                disagree += hot != swept;
                hotSteps += hot;
                coldFast += !hot && eng.LastSpeed() >= minSpeed && eng.InStroke() ? 1 : 0;
                if (hot) Expect(eng.LastSpeed() >= minSpeed - 1e-4f, "speed: a green blade is moving at or above the least speed", eng.LastSpeed());
                if (n >= 1 && !hot) Expect(c[0].kind == kBlade, "speed: below it the blade is cyan", c[0].kind);
            }
        }
        Expect(disagree == 0, "speed: the blade is green exactly when the engine's step swept something", disagree);
        Expect(hotSteps > 20, "speed: a hard swing (3 m/s) counts at any of these least speeds", hotSteps);
        Expect(coldFast == 0, "speed: in a stroke at or above the least speed the blade is never cyan", coldFast);
    }
    // the same swing at 1.5 m/s: counts with a least speed of 0.8, not with 2.0
    for (const float minSpeed : {0.8f, 2.0f}) {
        stroke::Engine eng(stroke::ParamsFor({minSpeed, 8.0f}));
        double t = 1.0;
        Vec hand{0.0f, 1.0f, 0.0f};
        int counted = 0;
        for (int i = 0; i < 60; ++i) {
            t += 1.0 / 90.0;
            hand.x += 1.5f / 90.0f;
            Sample s;
            s.time = t; s.active = true; s.epoch = 1; s.context = 1;
            s.hand = {hand, {}};
            s.blade = {hand, hand + Vec{0.0f, 0.0f, 0.65f}, 0.04f};
            s.anchor = {hand.x, 1.0f, 0.0f};
            s.scale = kUpm;
            Sweep sw;
            eng.Step(s, sw);
            counted += eng.Counting();
        }
        Expect(minSpeed < 1.0f ? counted > 20 : counted == 0, "speed: a 1.5 m/s swing counts with a least speed of 0.8 and not with 2.0", counted);
    }
    // the log line shows the engine's speed, its peak, what it needs and the state
    {
        Frame f = MakeFrame(w, 1000, blade0, nullptr, 0);
        const Monster m = MakeMonster(5, 152, {2, 2, 80}, 1012.0f, 2000.0f, w);
        f.monsters = &m;
        f.count = 1;
        f.minSpeed = 2.5f;
        f.blades[0].inStroke = true;
        f.blades[0].speed = 3.2f;
        f.blades[0].counting = true;
        Scene s;
        s.Set(f);
        char line[768];
        Expect(s.Describe(line, sizeof line, 1000), "speed: a line with a monster in it");
        const std::string text = line;
        Expect(text.find("3.2 m/s (peak 3.2, needs 2.5) COUNTS") != std::string::npos, "speed: the line has the speed, its peak, what is needed and COUNTS", 0.0);
        f.blades[0].speed = 1.1f;
        f.blades[0].counting = false;
        s.Set(f);
        s.Describe(line, sizeof line, 1000);
        const std::string below = line;
        Expect(below.find("1.1 m/s (peak 3.2, needs 2.5) below") != std::string::npos, "speed: slower, in a stroke: below, the peak kept");
        f.blades[0].inStroke = false;
        s.Set(f);
        s.Describe(line, sizeof line, 1000);
        Expect(std::string(line).find("no stroke") != std::string::npos, "speed: not in a stroke: no stroke");
    }
}

static void ListTests() {
    const World w = MakeWorld(0.0f);
    const Blade blade = MakeBlade(w);
    const Monster mons[3] = {MakeMonster(11, 155, {2, 2, 80}, 1012.0f, 2000.0f, w), MakeMonster(12, 40, {1, 1, 60}, 1016.0f, 2003.0f, w),
                             MakeMonster(13, 77, {4, 4, 110}, 1010.0f, 1996.0f, w, Verdict::Refused)};
    Scene s;
    s.Set(MakeFrame(w, 1000, blade, mons, 3));
    Capsule out[kMaxCapsules];
    int n = s.Capsules(1050, out, kMaxCapsules);
    Expect(n == 4, "list: the blade and three monsters", n);
    Expect(out[0].kind == kBlade && NearV(out[0].a, blade.base, 1e-6f) && NearV(out[0].b, blade.tip, 1e-6f) && Near(out[0].radius, blade.radius, 1e-6f),
           "list: the blade first, as the engine sweeps it");
    // nearest the hero first: monster 13 (10 units... its place 1010,1996: d2 = 100+16), 11 (12,0: 144), 12 (16,3: 265)
    Expect(out[1].kind == kRefused && Near(out[1].radius, mons[2].body.radius, 1e-6f), "list: the nearest first, a refused one pale");
    Expect(out[2].kind == kMonster && NearV(out[2].a, mons[0].body.bottom, 1e-6f), "list: then the next nearest, yellow");
    Expect(out[3].kind == kMonster && NearV(out[3].b, mons[1].body.top, 1e-6f), "list: the farthest last");

    {   // [gameplay] melee_head_volume: the head's own volume drawn right after its blade, in the blade's colour
        Frame hf = MakeFrame(w, 1000, blade, mons, 3);
        hf.blades[0].nHeads = 3;   // (an axe's head, or a three-ball flail's other balls: each its own)
        hf.blades[0].heads[0] = {blade.tip + Vec{0.0f, 0.0f, 0.05f}, blade.tip + Vec{0.0f, 0.0f, 0.2f}, 0.07f};
        hf.blades[0].heads[1] = {blade.tip + Vec{0.3f, 0.0f, 0.0f}, blade.tip + Vec{0.3f, 0.01f, 0.0f}, 0.09f};
        hf.blades[0].heads[2] = {blade.tip + Vec{-0.3f, 0.0f, 0.0f}, blade.tip + Vec{-0.3f, 0.01f, 0.0f}, 0.09f};
        Scene hs;
        hs.Set(hf);
        const int hn = hs.Capsules(1050, out, kMaxCapsules);
        Expect(hn == 7 && out[1].kind == out[0].kind && NearV(out[1].a, hf.blades[0].heads[0].base, 1e-6f) && Near(out[1].radius, 0.07f, 1e-6f), "list: the head volumes after their blade, its colour", hn);
        Expect(NearV(out[2].a, hf.blades[0].heads[1].base, 1e-6f) && NearV(out[3].a, hf.blades[0].heads[2].base, 1e-6f) && !NearV(out[2].a, out[3].a, 0.1f), "list: each ball its own, never one");
    }
    // too old: nothing
    Expect(s.Capsules(1000 + kStaleMs + 1, out, kMaxCapsules) == 0, "list: a scene older than 300 ms is not drawn");
    Expect(s.Capsules(999, out, kMaxCapsules) == 0, "list: time going back draws nothing");

    // the struck flash: red, last in the list (drawn on top), for half a second
    s.Struck(11, kStruckKey, 1100);
    n = s.Capsules(1150, out, kMaxCapsules);
    Expect(n == 4 && out[3].kind == kStruckKey && NearV(out[3].a, mons[0].body.bottom, 1e-6f), "struck: the monster of the key route red, on top");
    int yellow = 0;
    for (int i = 0; i < n; ++i) yellow += out[i].kind == kMonster;
    Expect(yellow == 1, "struck: it is not drawn twice", yellow);
    s.Set(MakeFrame(w, 1590, blade, mons, 3));   // keep the scene fresh
    Expect(s.Capsules(1100 + kStruckMs - 1, out, kMaxCapsules) == 4 && out[3].kind == kStruckKey, "struck: still red just before half a second");
    n = s.Capsules(1100 + kStruckMs, out, kMaxCapsules);
    int red = 0;
    for (int i = 0; i < n; ++i) red += out[i].kind == kStruckKey;
    Expect(red == 0 && n == 4, "struck: back to yellow after half a second", red);
    // the Direct hit route: magenta
    s.Struck(12, kStruckDirect, 1600);
    s.Set(MakeFrame(w, 1610, blade, mons, 3));
    n = s.Capsules(1620, out, kMaxCapsules);
    Expect(n == 4 && out[3].kind == kStruckDirect && NearV(out[3].b, mons[1].body.top, 1e-6f), "struck: the Direct hit route magenta");
    // the monster dies at once (gone from the scan): the flash goes on from the copy of its capsule
    const Monster gone[2] = {mons[0], mons[2]};   // ids 11 and 13: monster 12 is gone
    s.Set(MakeFrame(w, 1710, blade, gone, 2));
    n = s.Capsules(1720, out, kMaxCapsules);
    Expect(n == 4 && out[3].kind == kStruckDirect && NearV(out[3].a, mons[1].body.bottom, 1e-6f), "struck: a monster gone from the scan keeps its flash from the copy");
    // a strike on an id not in the scan at the time (nothing to copy) draws nothing
    s.Struck(99, kStruckKey, 1730);
    n = s.Capsules(1735, out, kMaxCapsules);
    Expect(n == 4, "struck: an unknown id is not invented", n);
    // the strike of one monster re-armed replaces its own, not another's
    s.Struck(11, kStruckKey, 1740);
    s.Struck(11, kStruckDirect, 1750);
    n = s.Capsules(1760, out, kMaxCapsules);
    int flashed = 0;
    for (int i = 0; i < n; ++i) flashed += out[i].kind == kStruckKey || out[i].kind == kStruckDirect;
    Expect(flashed == 2 && out[n - 1].kind == kStruckDirect, "struck: one entry per monster, the newest wins", flashed);

    // no blade: the monsters alone
    Frame nb = MakeFrame(w, 2000, blade, mons, 3);
    nb.blades[0].have = false;
    Scene s2;
    s2.Set(nb);
    n = s2.Capsules(2010, out, kMaxCapsules);
    Expect(n == 3 && out[0].kind != kBlade, "list: without a blade only the monsters", n);
    // nothing at all
    Scene s3;
    Expect(s3.Capsules(10, out, kMaxCapsules) == 0, "list: an empty scene draws nothing");
    // a crowd: capped, the blade and the nearest kept, a struck monster too
    std::vector<Monster> crowd;
    for (uint32_t i = 0; i < 40; ++i) crowd.push_back(MakeMonster(100 + i, 5, {1, 1, 60}, 1002.0f + i * 0.5f, 2000.0f, w));
    Scene s4;
    s4.Set(MakeFrame(w, 3000, blade, crowd.data(), crowd.size()));
    s4.Struck(139, kStruckKey, 3000);   // the farthest of them
    n = s4.Capsules(3010, out, kMaxCapsules);
    Expect(n == kMaxCapsules, "crowd: filled to the shader's places", n);
    Expect(out[0].kind == kBlade, "crowd: the blade stays");
    Expect(out[n - 1].kind == kStruckKey && NearV(out[n - 1].a, crowd[39].body.bottom, 1e-6f), "crowd: the struck monster stays, on top, though the farthest");
    Expect(NearV(out[1].a, crowd[0].body.bottom, 1e-6f), "crowd: the nearest first");
    int ordinary = 0;
    for (int i = 0; i < n; ++i) ordinary += out[i].kind == kMonster;
    Expect(ordinary == kMaxCapsules - 2, "crowd: all the other places are ordinary monsters, beside the blade and the struck one", ordinary);

    // The nearest three are drawn whatever the game says: 40 refused monsters, two blades and four struck ones fill the places, the nearest
    // three of the scan (refused, pale) are still among them, the farthest are what is left out.
    {
        std::vector<Monster> refused;
        for (uint32_t i = 0; i < 40; ++i) refused.push_back(MakeMonster(200 + i, 152, {2, 2, 80}, 1002.0f + i * 0.5f, 2000.0f, w, Verdict::Refused));
        Frame fr = MakeFrame(w, 4000, blade, refused.data(), refused.size());
        fr.blades[1].have = true;
        fr.blades[1].side = 1;
        fr.blades[1].blade = blade;
        Scene s5;
        s5.Set(fr);
        for (uint32_t i = 0; i < 4; ++i) s5.Struck(236 + i, kStruckKey, 4000);   // the four farthest
        Capsule c5[kMaxCapsules];
        const int n5 = s5.Capsules(4010, c5, kMaxCapsules);
        Expect(n5 == kMaxCapsules, "nearest: the places are full", n5);
        Expect(c5[0].kind == kBlade && c5[1].kind == kBlade2, "nearest: the two blades, the first cyan and the second teal");
        int paleNear = 0, flashedFour = 0;
        for (int i = 2; i < n5; ++i) {
            flashedFour += c5[i].kind == kStruckKey;
            for (int k = 0; k < 3; ++k)
                if (c5[i].kind == kRefused && NearV(c5[i].a, refused[k].body.bottom, 1e-6f)) ++paleNear;
        }
        Expect(paleNear == 3, "nearest: the three nearest monsters are drawn though the game refuses them, pale", paleNear);
        Expect(flashedFour == 4, "nearest: the four struck ones are drawn too", flashedFour);
        Expect(c5[n5 - 1].kind == kStruckKey, "nearest: the struck ones last, on top");
        bool farthest = false;
        for (int i = 2; i < n5; ++i) farthest = farthest || NearV(c5[i].a, refused[35].body.bottom, 1e-6f) && c5[i].kind != kStruckKey;
        Expect(!farthest, "nearest: what does not fit is the farthest, not the nearest");
    }
}

static void ConversionTests() {
    // 1. the numbers the effect gets: world units from where the hands hang
    const World w = MakeWorld(0.0f);
    const Blade blade = MakeBlade(w);
    const Monster m = MakeMonster(11, 155, {2, 2, 80}, 1012.0f, 2000.0f, w);   // 3 m ahead of the hero, 0.40 m radius, 0.4 .. 1.6 m centres
    Capsule caps[2] = {{blade.base, blade.tip, blade.radius, kBlade}, {m.body.bottom, m.body.top, m.body.radius, kMonster}};
    float a[4][4], b[4][4];
    const float eye[3] = {w.eyeWorld.x, w.eyeWorld.y, w.eyeWorld.z};
    const int n = Pack(caps, 2, kUpm, eye, a, b);
    Expect(n == 2, "pack: two capsules", n);
    // the monster: bottom centre at world (1012, 0.4 m * 4 = 1.6, 2000) -> relative to the eyes (1000, 6.4, 2000): (12, -4.8, 0), radius 1.6 units
    Expect(Near(a[1][0], 12.0f, 1e-3f) && Near(a[1][1], -4.8f, 1e-3f) && Near(a[1][2], 0.0f, 1e-3f) && Near(a[1][3], 1.6f, 1e-4f), "pack: the monster's bottom end and radius in world units from the eyes", a[1][1]);
    // top centre 1.6 m = 6.4 units: relative (12, 0, 0); the colour is the kind
    Expect(Near(b[1][0], 12.0f, 1e-3f) && Near(b[1][1], 0.0f, 1e-3f) && Near(b[1][2], 0.0f, 1e-3f) && Near(b[1][3], 1.0f, 0.0f), "pack: the top end, kind yellow", b[1][1]);
    // the blade: base is 0.45 m ahead, 0.45 m below, 0.25 m aside of the eyes, in world units x4
    Expect(Near(a[0][0], 1.8f, 1e-3f) && Near(a[0][1], -1.8f, 1e-3f) && Near(a[0][2], 1.0f, 1e-3f) && Near(a[0][3], 0.065f * 4.0f, 1e-4f), "pack: the blade's base from the eyes", a[0][0]);
    Expect(Near(b[0][3], 0.0f, 0.0f), "pack: kind cyan for the blade");
    // a non-finite capsule is left out, not drawn as a smear
    Capsule bad[2] = {caps[1], caps[0]};
    bad[0].a.x = NAN;
    Expect(Pack(bad, 2, kUpm, eye, a, b) == 1 && Near(b[0][3], 0.0f, 0.0f), "pack: a capsule that is not finite is skipped");
    bad[0] = caps[1];
    bad[0].radius = 0.0f;
    Expect(Pack(bad, 1, kUpm, eye, a, b) == 0, "pack: a capsule with no radius is skipped");

    // 2. the capsule stands where the monster is drawn: the model's feet and head, in the world, against the capsule's axis, in both eyes.
    // A monster's model is drawn at the render place of its grid place: grid + height * shear on both axes, standing up from there.
    for (const float floorM : {0.0f, 1.0f, -0.6f}) {
        const World r = MakeWorld(floorM);
        const Monster q = MakeMonster(7, 155, {2, 2, 80}, 1013.0f, 1998.0f, r);
        const Vec feetWorld = meleetouch::RenderGround({1013.0f, 0.0f, 1998.0f}, floorM * kUpm);   // where the game draws the monster's feet (world units)
        Capsule c1 = {q.body.bottom, q.body.top, q.body.radius, kMonster};
        const float eyeR[3] = {r.eyeWorld.x, r.eyeWorld.y, r.eyeWorld.z};
        Pack(&c1, 1, kUpm, eyeR, a, b);
        const Vec axisA{a[0][0], a[0][1], a[0][2]}, axisB{b[0][0], b[0][1], b[0][2]};
        for (int e = 0; e < 2; ++e) {
            // a point of the model at 1.0 m (world: feet + up), against the capsule's axis at the same height
            const Vec modelPoint = feetWorld + Vec{0.0f, 1.0f * kUpm, 0.0f} - r.eyeWorld;
            const float t = (modelPoint.y - axisA.y) / (axisB.y - axisA.y);
            const Vec onAxis = axisA + (axisB - axisA) * t;
            const Pixel pm = ToPixel(r.cam[e], modelPoint), pc = ToPixel(r.cam[e], onAxis);
            Expect(pm.ahead > 0.0f && Near(pm.x, pc.x, 0.05f) && Near(pm.y, pc.y, 0.05f), "scene: the capsule's axis passes through the model's body in both eyes", pm.x - pc.x);
            // and the pixel is inside the drawn capsule: a ray from the eye through it hits
            const Vec rd = RayDir(r.cam[e], pm.x, pm.y);
            Expect(CapsuleHit(r.cam[e].eye, rd, axisA, axisB, a[0][3]) > 0.0f, "scene: the ray through the model's pixel hits the capsule", floorM);
            // the model's feet are inside the capsule's footprint circle, its head under the top
            const Vec feet = feetWorld - r.eyeWorld;
            const Pixel pf = ToPixel(r.cam[e], feet);
            const Vec rf = RayDir(r.cam[e], pf.x, pf.y);
            Expect(CapsuleHit(r.cam[e].eye, rf, axisA, axisB, a[0][3]) > 0.0f, "scene: the ray through the model's feet hits the capsule", floorM);
        }
        // the grid place round trip (the same shear the detector uses)
        const Vec g = meleetouch::GridPlane(feetWorld);
        Expect(Near(g.x, 1013.0f, 1e-3f) && Near(g.z, 1998.0f, 1e-3f), "scene: render place -> grid place", g.x);
    }

    // 3. a pixel and its ray agree (the effect's two directions of the projection)
    const World r = MakeWorld(0.0f);
    for (int e = 0; e < 2; ++e) {
        const Vec p{14.0f, -3.0f, 2.5f};
        const Pixel px = ToPixel(r.cam[e], p);
        const Vec rd = RayDir(r.cam[e], px.x, px.y);
        const Vec toP = Normalized(p - r.cam[e].eye);
        Expect(Dot(rd, toP) > 0.99999f, "projection: a pixel's ray runs through the point it came from", Dot(rd, toP));
    }
    // and the two eyes see the same capsule at different pixels (parallax), the right eye's to the left of the left eye's, a monster 3 m ahead
    const Pixel pl = ToPixel(r.cam[0], {12.0f, -4.8f, 0.0f}), pr = ToPixel(r.cam[1], {12.0f, -4.8f, 0.0f});
    Expect(pl.x != pr.x && std::fabs(pl.x - pr.x) < 200.0f, "projection: the eyes differ by a parallax", pl.x - pr.x);

    // The drawn radius is the radius we hand over - the effect draws no twice-thick capsule (the user's staff capsule looked twice the shaft: its
    // radius was 2.5 cm against a shaft of ~1.6, the effect is exact): a ray passing 0.98 r from the axis hits, one passing 1.02 r misses, in both eyes.
    for (int e = 0; e < 2; ++e) {
        const float rM = 0.016f;                                // metres, a staff's shaft
        const float rW = rM * kUpm;                             // world units, as Pack makes it
        Capsule thin = {Metres(r.eyeWorld + Vec{12.0f, -4.8f, 0.0f}), Metres(r.eyeWorld + Vec{12.0f, 0.0f, 0.0f}), rM, kBlade};   // 3 m ahead, from the floor to the eyes' height
        float pa[2][4], pb[2][4];
        const float eyeR[3] = {r.eyeWorld.x, r.eyeWorld.y, r.eyeWorld.z};
        Expect(Pack(&thin, 1, kUpm, eyeR, pa, pb) == 1 && Near(pa[0][3], rW, 1e-6f), "radius: Pack hands the effect the radius times the units per metre", pa[0][3]);
        const Vec A{pa[0][0], pa[0][1], pa[0][2]}, B{pb[0][0], pb[0][1], pb[0][2]};
        const Vec mid = (A + B) * 0.5f;                         // a point of the axis, relative to where the hands hang
        const Vec toMid = mid - r.cam[e].eye;
        const Vec dir = Normalized(toMid);
        const Vec side = Normalized(Vec{dir.z, 0.0f, -dir.x}); // horizontal, across the ray: the axis is vertical, so this is the capsule's widest way
        const float dist = Length(toMid);
        const auto miss = [&](float k) {   // a ray through the point k * r to the side of the axis' middle
            const Vec target = mid + side * (k * rW);
            return CapsuleHit(r.cam[e].eye, Normalized(target - r.cam[e].eye), A, B, rW);
        };
        (void)dist;
        Expect(miss(0.98f) > 0.0f, "radius: a ray 0.98 r from the axis hits the drawn capsule", e);
        Expect(miss(1.02f) <= 0.0f, "radius: a ray 1.02 r from the axis misses it - the drawn capsule is not wider than its radius", e);
        Expect(miss(0.0f) > 0.0f, "radius: a ray down the middle hits", e);
    }
}

static void ModuleTests() {
    const World w = MakeWorld(0.0f);
    const Blade blade = MakeBlade(w);
    const Monster mons[2] = {MakeMonster(11, 155, {2, 2, 80}, 1012.0f, 2000.0f, w), MakeMonster(12, 40, {1, 1, 60}, 1016.0f, 2003.0f, w)};
    float a[kMaxCapsules][4], b[kMaxCapsules][4];
    const float eye[3] = {w.eyeWorld.x, w.eyeWorld.y, w.eyeWorld.z};
    SetLogSink(&Sink);

    // the off state: nothing kept, nothing returned, nothing logged
    Expect(!Wanted(), "module: off by default");
    Update(MakeFrame(w, 1000, blade, mons, 2));
    Struck(11, kStruckKey, 1000);
    Expect(Take(1010, eye, a, b) == 0, "off: nothing to draw");
    Expect(g_sinkCalls == 0, "off: nothing logged");
    SetWanted(true);
    Expect(Wanted(), "module: on");
    Expect(Take(1010, eye, a, b) == 0, "on: but nothing from while it was off (and nothing fed yet)");

    // on: the first update logs at once, then 5 Hz
    Update(MakeFrame(w, 2000, blade, mons, 2));
    Expect(g_sinkCalls == 1, "log: the first line comes at once", g_sinkCalls);
    Update(MakeFrame(w, 2100, blade, mons, 2));
    Update(MakeFrame(w, 2199, blade, mons, 2));
    Expect(g_sinkCalls == 1, "log: not faster than 5 Hz", g_sinkCalls);
    Update(MakeFrame(w, 2200, blade, mons, 2));
    Expect(g_sinkCalls == 2, "log: the next one 200 ms later", g_sinkCalls);
    int n = Take(2210, eye, a, b);
    Expect(n == 3, "module: the blade and two monsters", n);
    Struck(12, kStruckDirect, 2210);
    n = Take(2220, eye, a, b);
    Expect(n == 3 && Near(b[n - 1][3], 3.0f, 0.0f), "module: the struck monster last, magenta", n);
    Expect(Take(2200 + kStaleMs + 1, eye, a, b) == 0, "module: stale after 300 ms without an update");
    // a frame with no monsters says nothing in the log (but the blade is still drawn)
    g_sinkCalls = 0;
    Update(MakeFrame(w, 5000, blade, mons, 0));
    Expect(g_sinkCalls == 0 && Take(5010, eye, a, b) == 1, "module: no monsters - the blade only, no log line");
    // off again: cleared
    SetWanted(false);
    Expect(Take(5010, eye, a, b) == 0, "off again: nothing");
    SetWanted(true);
    Expect(Take(5010, eye, a, b) == 0, "on again: nothing left over from before");
    SetWanted(false);
    SetLogSink(nullptr);
}

static void LogTests() {
    const World w = MakeWorld(0.0f);
    // a blade standing straight up from 0.5 to 1.5 m at (253 m, 500 m); a monster 3 m from the hero on the grid whose capsule axis stands
    // 0.9 m beside it, from 0.4 to 1.6 m (radius 0.4, so it reaches 0.0 .. 2.0 m)
    Blade blade{{1012.0f / kUpm, 0.5f, 500.0f}, {1012.0f / kUpm, 1.5f, 500.0f}, 0.065f};
    Monster m = MakeMonster(11, 155, {2, 2, 80}, 1012.0f, 2000.0f, w);
    m.body.bottom = {1012.0f / kUpm, 0.4f, 500.0f + 0.9f};
    m.body.top = {1012.0f / kUpm, 1.6f, 500.0f + 0.9f};
    m.body.radius = 0.4f;
    Monster far = MakeMonster(12, 40, {1, 1, 60}, 1030.0f, 2000.0f, w, Verdict::Refused);
    far.body.bottom = {1030.0f / kUpm, 0.4f, 500.0f};
    far.body.top = {1030.0f / kUpm, 1.0f, 500.0f};
    Monster mons[2] = {far, m};   // the nearer to the blade second: the log sorts
    Scene s;
    Frame f = MakeFrame(w, 1000, blade, mons, 2);
    f.blades[0].sweepSpeed = 2.4f;
    f.blades[0].sweepMs = 900;
    f.blades[0].inStroke = true;
    s.Set(f);
    Scene::Reading r[3];
    const int n = s.Readings(r, 3);
    Expect(n == 2 && r[0].cls == 155 && r[1].cls == 40, "log: the monster nearest the blade's tip first", n);
    Expect(Near(r[0].radius, 0.4f, 1e-5f), "log: the capsule's radius");
    Expect(Near(r[0].lowM, 0.0f, 1e-5f) && Near(r[0].highM, 2.0f, 1e-5f), "log: the whole capsule over the floor, from its lowest point to its highest", r[0].highM);
    Expect(Near(r[0].tipToAxis, 0.9f, 1e-4f), "log: the tip's distance to the axis", r[0].tipToAxis);
    Expect(Near(r[0].gap, 0.9f - 0.4f - 0.065f, 1e-4f), "log: the gap between the blade and the capsule, less the two radii", r[0].gap);
    Expect(Near(r[0].heroM, std::sqrt(m.d2) / kUpm, 1e-5f) && Near(r[0].heroM, 3.0f, 1e-3f), "log: the distance from the hero in metres", r[0].heroM);
    // a blade inside the capsule: negative gap
    Blade inside = blade;
    inside.base = {1012.0f / kUpm, 0.5f, 500.0f + 0.7f};
    inside.tip = {1012.0f / kUpm, 1.5f, 500.0f + 0.7f};
    f.blades[0].blade = inside;
    s.Set(f);
    s.Readings(r, 3);
    Expect(r[0].gap < 0.0f && Near(r[0].tipToAxis, 0.2f, 1e-4f), "log: inside the capsule the gap is negative", r[0].gap);
    // the text
    char line[768];
    Expect(s.Describe(line, sizeof line, 1000), "log: a line when there is a monster");
    const std::string text = line;
    std::printf("example log line: %s\n", line);
    Expect(text.find("melee boxes:") == 0 && text.find("cls 155") != std::string::npos && text.find("cls 40") != std::string::npos, "log: the line names the classes");
    Expect(text.find("refused") != std::string::npos && text.find("hostile") != std::string::npos, "log: and what the game's checks said of them");
    Expect(text.find("sweep 2.4 m/s 0.1 s ago") != std::string::npos && text.find(") below,") != std::string::npos, "log: the stroke is on and below the speed needed");
    Expect(text.find("r 0.40, 0.00..2.00 m,") != std::string::npos, "log: the capsule's size and height");
    // three monsters shown at most, and the whole line short enough for a log line
    const Monster four[4] = {m, far, MakeMonster(13, 77, {4, 4, 110}, 1010.0f, 1996.0f, w), MakeMonster(14, 78, {3, 3, 96}, 1020.0f, 2002.0f, w)};
    Scene s3;
    Frame f4 = MakeFrame(w, 1000, blade, four, 4);
    f4.blades[0].sweepSpeed = 12.3f;
    f4.blades[0].sweepMs = 100;
    f4.blades[0].inStroke = true;
    s3.Set(f4);
    s3.Describe(line, sizeof line, 1000);
    const std::string t3 = line;
    size_t classes = 0;
    for (size_t at = t3.find("cls "); at != std::string::npos; at = t3.find("cls ", at + 1)) ++classes;
    Expect(classes == 3, "log: the three nearest only", static_cast<double>(classes));
    Expect(t3.size() < 340, "log: three monsters fit in one short line", static_cast<double>(t3.size()));
    // dual wield: both blades in the line, each with its hand, and the tip distance is the nearest blade's
    {
        Blade second{{1012.0f / kUpm, 0.5f, 500.0f + 0.5f}, {1012.0f / kUpm, 1.5f, 500.0f + 0.5f}, 0.05f};   // 0.5 m from the monster's axis: nearer than the first
        Frame fd = MakeFrame(w, 1000, blade, mons, 2);
        fd.blades[1].have = true;
        fd.blades[1].side = 1;
        fd.blades[1].blade = second;
        fd.blades[1].sweepSpeed = 1.5f;
        fd.blades[1].sweepMs = 700;
        Scene sd;
        sd.Set(fd);
        Scene::Reading rd[3];
        const int nd = sd.Readings(rd, 3);
        Expect(nd == 2 && rd[0].cls == 155 && rd[0].side == 1 && Near(rd[0].tipToAxis, 0.4f, 1e-4f), "dual: the nearer blade (the left's) is the one the line speaks of", rd[0].tipToAxis);
        Expect(Near(rd[0].gap, 0.4f - 0.4f - 0.05f, 1e-4f), "dual: its gap, less its own radius", rd[0].gap);
        char ld[768];
        sd.Describe(ld, sizeof ld, 1000);
        const std::string td = ld;
        Expect(td.find(" R 1.00 m r 0.065") != std::string::npos && td.find(" L 1.00 m r 0.050") != std::string::npos, "dual: both blades named with their hand, length and radius");
        Expect(td.find("sweep 1.5 m/s 0.3 s ago") != std::string::npos && td.find(", L tip>axis") != std::string::npos, "dual: the left's sweep and the left's tip in the line");
    }
    Scene empty;
    Expect(!empty.Describe(line, sizeof line, 10), "log: nothing to say without a scene");
    Frame none = MakeFrame(w, 1000, blade, mons, 0);
    Scene s2;
    s2.Set(none);
    Expect(!s2.Describe(line, sizeof line, 1000), "log: nothing to say without a monster");
    char tiny[24];
    s.Describe(tiny, sizeof tiny, 1000);   // a small buffer is cut short, never overrun
    Expect(std::strlen(tiny) < sizeof tiny, "log: a small buffer is cut short and ends", static_cast<double>(std::strlen(tiny)));
}

// The picture of a synthetic scene as the effect would draw it.
static void Dump(const char* path) {
    const World w = MakeWorld(0.0f);
    const Blade blade = MakeBlade(w);
    struct Spec { uint32_t id, cls; meleetouch::MonsterRow row; float gx, gz; Verdict v; };
    const Spec specs[5] = {{1, 100, {2, 2, 80}, 1011.0f, 1999.0f, Verdict::Hostile}, {2, 101, {1, 1, 60}, 1014.0f, 2003.0f, Verdict::Hostile},
                           {3, 102, {4, 4, 110}, 1018.0f, 1995.0f, Verdict::Refused}, {4, 103, {2, 2, 80}, 1008.0f, 2006.0f, Verdict::Hostile},
                           {5, 104, {3, 3, 96}, 1020.0f, 2002.0f, Verdict::Hostile}};
    std::vector<Monster> mons;
    std::vector<Marker> poles;
    for (const Spec& sp : specs) {
        mons.push_back(MakeMonster(sp.id, sp.cls, sp.row, sp.gx, sp.gz, w, sp.v));
        poles.push_back({meleetouch::RenderGround({sp.gx, 0.0f, sp.gz}, w.floorM * kUpm), 1.8f});   // where the game draws the monster's feet
    }
    Scene s;
    Frame frame = MakeFrame(w, 1000, blade, mons.data(), mons.size());
    frame.blades[1].have = true;   // a second blade in the other hand (a Barbarian's two weapons), the mirror of the first about the view's middle
    frame.blades[1].side = 1;
    frame.blades[1].blade = {Metres(w.eyeWorld) + Vec{0.45f, -0.45f, -0.25f}, Metres(w.eyeWorld) + Vec{0.45f, -0.45f, -0.25f} + Normalized(Vec{1.0f, 0.4f, 0.35f}) * 0.65f, 0.05f};
    for (int i = 0; i < 40; ++i) {   // the right blade swung across the view first (poses 10 ms apart, the last one at 1000): a contact counts for poses 12..28
        Frame fs = frame;
        fs.nowMs = 1000 - static_cast<uint64_t>(39 - i) * 10;
        const Vec shift{0.0f, 0.12f * std::sin(0.0805f * static_cast<float>(i - 39)), 0.02f * static_cast<float>(i - 39)};
        fs.blades[0].blade = {blade.base + shift, blade.tip + shift, blade.radius};
        fs.blades[0].counting = i >= 12 && i <= 28;
        fs.blades[0].inStroke = fs.blades[0].counting;
        s.Set(fs);
    }
    s.Set(frame);
    s.Struck(1, kStruckKey, 990);
    s.Struck(5, kStruckDirect, 990);
    Capsule caps[kMaxCapsules];
    const int nc = s.Capsules(1010, caps, kMaxCapsules);
    float a[kMaxCapsules][4], b[kMaxCapsules][4];
    const float eye[3] = {w.eyeWorld.x, w.eyeWorld.y, w.eyeWorld.z};
    const int n = Pack(caps, nc, kUpm, eye, a, b);
    const std::vector<uint8_t> img = Render(w, poles, n, a, b);
    if (WritePng(path, w.cam[0].width * 2, w.cam[0].height, img)) std::printf("wrote %s (%d capsules)\n", path, n);
    else std::printf("could not write %s\n", path);
}

// The effect's kinds beyond melee_boxes' own: 11 is vr/skill_triggers.h's (violet; the shield bash), and 6 stays green. (A CPU copy of the effect, above.)
static void TriggerKindTests() {
    const World w = MakeWorld(0.0f);
    const Camera& cam = w.cam[0];
    const Vec mid = Metres(w.eyeWorld) + Vec{1.0f, -0.1f, 0.0f};   // straight ahead of the eye, a metre out
    Capsule shield[1] = {{mid - Vec{0.0f, 0.2f, 0.0f}, mid + Vec{0.0f, 0.2f, 0.0f}, 0.15f, static_cast<Kind>(11)}};
    float a[kMaxCapsules][4], b[kMaxCapsules][4];
    const float eye[3] = {w.eyeWorld.x, w.eyeWorld.y, w.eyeWorld.z};
    const int n = Pack(shield, 1, kUpm, eye, a, b);
    Expect(n == 1 && static_cast<int>(b[0][3]) == 11, "kind 11: packs as 11", b[0][3]);
    const Pixel p = ToPixel(cam, Vec{a[0][0], a[0][1], a[0][2]});
    const Rgb out = Hitboxes({0.0f, 0.0f, 0.0f}, cam, p.x, p.y, a, b, n);
    Expect(out.b > out.r && out.r > out.g && out.b > 0.1f, "kind 11: violet (blue > red > green)", out.b);
    shield[0].kind = kBladeHot;
    const int n2 = Pack(shield, 1, kUpm, eye, a, b);
    const Rgb hot = Hitboxes({0.0f, 0.0f, 0.0f}, cam, p.x, p.y, a, b, n2);
    Expect(hot.g > hot.r && hot.g > hot.b, "kind 6: the same capsule green", hot.g);
    shield[0].kind = kTrailHot;
    const int n3 = Pack(shield, 1, kUpm, eye, a, b);
    const Rgb trail = Hitboxes({0.0f, 0.0f, 0.0f}, cam, p.x, p.y, a, b, n3);
    Expect(trail.g > trail.r && trail.g > trail.b, "kind 10: still green (a hot trail is not violet)", trail.g);
    // Take's share: skill_triggers takes its capsules first, melee_boxes gets what is left of the effect's 32
    const Monster mons[2] = {MakeMonster(1, 100, {2, 2, 80}, 1011.0f, 1999.0f, w), MakeMonster(2, 101, {2, 2, 80}, 1014.0f, 2003.0f, w)};
    SetWanted(true);
    Update(MakeFrame(w, 20000, MakeBlade(w), mons, 2));
    Expect(Take(20010, eye, a, b) == 3, "share: the blade and two monsters");
    Expect(Take(20010, eye, a, b, 2) == 2, "share: a share of two is kept to");
    Expect(Take(20010, eye, a, b, 0) == 0, "share: none left, none drawn");
    SetWanted(false);
}

// The strike rules' marks (vr/strike_direction.h, melee_touch.cpp): the blade's look and the trail in the verdict's colour, and at a contact a ball, an arrow, a line to the monster's axis and
// (a refusal) the monster's tint. The colours: green counts, blue not toward the monster (red when asked), amber a swing too small or too slow, grey not the head.
static int CountKind(const Capsule* c, int n, int kind) {
    int r = 0;
    for (int i = 0; i < n; ++i) r += static_cast<int>(c[i].kind) == kind;
    return r;
}
static ContactMark MakeMark(Vec at, Vec dir, Vec onAxis, int colour, bool tint, uint32_t id, uint64_t ms) {
    ContactMark m;
    m.at = at; m.dir = dir; m.onAxis = onAxis; m.haveAxis = true; m.colour = colour; m.tint = tint; m.monsterId = id; m.ms = ms;
    return m;
}
static void TouchMarkTests() {
    const World w = MakeWorld(0.0f);
    const Blade blade = MakeBlade(w);
    const Monster mons[3] = {MakeMonster(11, 155, {2, 2, 80}, 1012.0f, 2000.0f, w), MakeMonster(12, 40, {1, 1, 60}, 1016.0f, 2003.0f, w),
                             MakeMonster(13, 77, {4, 4, 110}, 1010.0f, 1996.0f, w, Verdict::Refused)};
    const Vec at = Metres(w.eyeWorld) + Vec{0.8f, -0.3f, 0.1f}, axisPt = at + Vec{0.0f, 0.0f, 0.35f}, up{0.0f, 1.0f, 0.0f};
    Capsule out[kMaxCapsules];
    {   // a contact refused as "not toward": a ball, an arrow, a line to the axis in blue and the monster's capsule tinted
        Scene s;
        s.Set(MakeFrame(w, 1000, blade, mons, 3));
        s.Touched(MakeMark(at, up, axisPt, kCBlue, true, 11, 1010));
        const int n = s.Capsules(1020, out, kMaxCapsules);
        Expect(n == 4 + 4, "contact: the blade and three monsters, and four marks", n);
        Expect(CountKind(out, n, TouchKind(kCBlue, kBall)) == 1 && CountKind(out, n, TouchKind(kCBlue, kTube)) == 2 && CountKind(out, n, TouchKind(kCBlue, kGlass)) == 1, "contact: a ball, two tubes (the arrow and the line), a glass capsule - all blue");
        Expect(static_cast<int>(out[n - 1].kind) == TouchKind(kCBlue, kBall), "contact: the ball is the very last, on top");
        bool ball = false, arrow = false, line = false, tint = false;
        for (int i = 0; i < n; ++i) {
            const int k = static_cast<int>(out[i].kind);
            if (k == TouchKind(kCBlue, kBall) && NearV((out[i].a + out[i].b) * 0.5f, at, 1e-5f) && Near(out[i].radius, kTouchRadiusM, 1e-6f)) ball = true;
            if (k == TouchKind(kCBlue, kTube) && NearV(out[i].a, at, 1e-5f) && NearV(out[i].b, at + up * kTouchArrowM, 1e-5f) && Near(out[i].radius, kTouchArrowRadiusM, 1e-6f)) arrow = true;
            if (k == TouchKind(kCBlue, kTube) && NearV(out[i].a, at, 1e-5f) && NearV(out[i].b, axisPt, 1e-5f) && Near(out[i].radius, kTouchLineRadiusM, 1e-6f)) line = true;
            if (k == TouchKind(kCBlue, kGlass) && NearV(out[i].a, mons[0].body.bottom, 1e-5f) && NearV(out[i].b, mons[0].body.top, 1e-5f) && out[i].radius > mons[0].body.radius) tint = true;
        }
        Expect(ball, "contact: the ball sits on the touching point");
        Expect(arrow, "contact: the arrow runs from the point along its velocity, 18 cm");
        Expect(line, "contact: the thin line runs from the point to the nearest point of the monster's axis");
        Expect(tint, "contact: the monster's capsule is tinted (a little larger than itself)");
        // for melee_mark_ms (800), then gone
        s.Set(MakeFrame(w, 1800, blade, mons, 3));
        int m = s.Capsules(1809, out, kMaxCapsules);
        Expect(CountKind(out, m, TouchKind(kCBlue, kBall)) == 1, "contact: still there at 799 ms");
        s.Set(MakeFrame(w, 1815, blade, mons, 3));
        m = s.Capsules(1816, out, kMaxCapsules);
        Expect(m == 4 && CountKind(out, m, TouchKind(kCBlue, kBall)) == 0, "contact: gone after 800 ms", m);
    }
    {   // a contact that counted: green ball, arrow and line, no tint (the monster's own red flash is the attack pressed)
        Scene s;
        s.Set(MakeFrame(w, 1000, blade, mons, 3));
        s.Touched(MakeMark(at, up, axisPt, kCGreen, false, 11, 1010));
        const int n = s.Capsules(1020, out, kMaxCapsules);
        Expect(n == 4 + 3 && CountKind(out, n, TouchKind(kCGreen, kBall)) == 1 && CountKind(out, n, TouchKind(kCGreen, kTube)) == 2 && CountKind(out, n, TouchKind(kCGreen, kGlass)) == 0, "counted: green ball, arrow and line, no tint", n);
        // amber and grey; and with no way and no axis: the ball alone
        Scene a;
        a.Set(MakeFrame(w, 1000, blade, mons, 3));
        ContactMark amber = MakeMark(at, {0, 0, 0}, axisPt, kCAmber, false, 11, 1010);
        amber.haveAxis = false;
        a.Touched(amber);
        a.Touched(MakeMark(at + Vec{0.5f, 0.0f, 0.0f}, up, axisPt, kCGrey, true, 12, 1010));
        const int na = a.Capsules(1020, out, kMaxCapsules);
        Expect(CountKind(out, na, TouchKind(kCAmber, kBall)) == 1 && CountKind(out, na, TouchKind(kCAmber, kTube)) == 0, "amber: a ball alone when there is no way and no axis", na);
        Expect(CountKind(out, na, TouchKind(kCGrey, kBall)) == 1 && CountKind(out, na, TouchKind(kCGrey, kGlass)) == 1, "grey: a ball and the tint (not the head)");
        // melee_mark_ms 0: no marks at all
        Frame f = MakeFrame(w, 1030, blade, mons, 3);
        f.markMs = 0;
        Scene z;
        z.Set(f);
        z.Touched(MakeMark(at, up, axisPt, kCBlue, true, 11, 1031));
        Expect(z.Capsules(1040, out, kMaxCapsules) == 4, "melee_mark_ms 0 draws none of them");
    }
    {   // "not toward" is red (the default) or blue ([debug] melee_direction_red=0): the same marks, the blade and the trail
        Frame f = MakeFrame(w, 1000, blade, mons, 3);
        f.directionColour = kCRed;
        Scene s;
        s.Set(f);
        s.Touched(MakeMark(at, up, axisPt, kCBlue, true, 11, 1010));
        const int n = s.Capsules(1020, out, kMaxCapsules);
        Expect(CountKind(out, n, TouchKind(kCRed, kBall)) == 1 && CountKind(out, n, TouchKind(kCBlue, kBall)) == 0 && CountKind(out, n, TouchKind(kCRed, kGlass)) == 1, "red: the direction verdict drawn red when asked", n);
        Expect(DirectionColour() == kCRed, "the module's default is red (the user's wish)");
        SetDirectionRed(false);
        Expect(DirectionColour() == kCBlue, "the module: blue with melee_direction_red=0");
        SetDirectionRed(true);
        Expect(Frame{}.directionColour == kCRed, "a frame starts red as well");
    }
    {   // the blade's look: green while a contact would count, blue (not toward the nearest monster) / amber (a swing too small yet) while the engine counts but the rules do not, cyan otherwise
        const strikedir::Look looks[4] = {strikedir::Look::Hot, strikedir::Look::Away, strikedir::Look::Small, strikedir::Look::Cold};
        const int want[4] = {kBladeHot, TouchKind(kCBlue, kGlass), TouchKind(kCAmber, kGlass), kBlade};
        for (int i = 0; i < 4; ++i) {
            Frame f = MakeFrame(w, 1000, blade, mons, 3);
            f.blades[0].counting = i != 3;
            f.blades[0].look = looks[i];
            Scene s;
            s.Set(f);
            const int n = s.Capsules(1010, out, kMaxCapsules);
            Expect(static_cast<int>(out[0].kind) == want[i], "blade: its colour follows its look", i);
            (void)n;
        }
        Frame f = MakeFrame(w, 1000, blade, mons, 3);   // a feeder that only says "counting" (look left Cold): green, as it always was
        f.blades[0].counting = true;
        Scene s;
        s.Set(f);
        s.Capsules(1010, out, kMaxCapsules);
        Expect(static_cast<int>(out[0].kind) == kBladeHot, "blade: counting without a look is green");
        // the log line says it
        char line[768];
        Frame g = MakeFrame(w, 2000, blade, mons, 3);
        g.blades[0].counting = true;
        g.blades[0].inStroke = true;
        g.blades[0].look = strikedir::Look::Away;
        Scene d;
        d.Set(g);
        Expect(d.Describe(line, sizeof line, 2000) && std::strstr(line, "COUNTS but not toward") != nullptr, "log: the line says the movement counts but is not toward a monster");
    }
    {   // the trail takes the look's colour, and the speed marks follow green only
        Track tr;
        const strikedir::Look seq[3] = {strikedir::Look::Away, strikedir::Look::Small, strikedir::Look::Hot};
        uint64_t ms = 1000;
        Vec tip{0.0f, 1.0f, 1.0f};
        for (int phase = 0; phase < 3; ++phase)
            for (int i = 0; i < 12; ++i) {
                tr.Feed(ms, tip, seq[phase]);
                ms += 11;
                tip = tip + Vec{0.04f, 0.0f, 0.0f};
            }
        TrailSegment seg[kTrailSegments];
        const size_t n = tr.Trail(ms, 800, seg, kTrailSegments);
        bool away = false, small = false, hot = false;
        for (size_t i = 0; i < n; ++i) {
            away = away || seg[i].look == strikedir::Look::Away;
            small = small || seg[i].look == strikedir::Look::Small;
            hot = hot || (seg[i].look == strikedir::Look::Hot && seg[i].hot);
        }
        Expect(away && small && hot, "trail: the segments take the look: away, small and hot");
        Mark mk[kMarkSlots];
        const size_t nm = tr.Marks(ms, 800, mk, kMarkSlots);
        Expect(nm == 1 && mk[0].reached, "marks: only the turn to Hot leaves a mark (a movement the rules refuse is not 'a contact would count')", static_cast<double>(nm));
        // in the scene: the trail's capsules are blue / amber tubes and green
        Scene s;
        const Blade bl = MakeBlade(w);
        uint64_t t = 1000;
        Vec p = bl.tip;
        for (int phase = 0; phase < 3; ++phase)
            for (int i = 0; i < 12; ++i) {
                Frame f = MakeFrame(w, t, Blade{bl.base, p, bl.radius}, mons, 3);
                f.blades[0].counting = true;
                f.blades[0].look = seq[phase];
                s.Set(f);
                t += 11;
                p = p + Vec{0.0f, 0.04f, 0.0f};
            }
        const int nc = s.Capsules(t, out, kMaxCapsules);
        Expect(CountKind(out, nc, TouchKind(kCBlue, kTube)) >= 1 && CountKind(out, nc, TouchKind(kCAmber, kTube)) >= 1 && CountKind(out, nc, kTrailHot) >= 1, "trail: blue, amber and green tubes in the scene", nc);
    }
    {   // one pass's sweeps are one mark; another colour, or another monster, or another place, is another
        Scene s;
        s.Set(MakeFrame(w, 1000, blade, mons, 3));
        s.Touched(MakeMark(at, up, axisPt, kCBlue, true, 11, 1010));
        s.Touched(MakeMark(at + Vec{0.02f, 0.01f, 0.0f}, up, axisPt, kCBlue, true, 11, 1020));   // 2 cm on, 10 ms later: the same one
        int n = s.Capsules(1030, out, kMaxCapsules);
        Expect(CountKind(out, n, TouchKind(kCBlue, kBall)) == 1, "dedupe: the next sweep of one pass is the same mark", CountKind(out, n, TouchKind(kCBlue, kBall)));
        s.Touched(MakeMark(at + Vec{0.02f, 0.01f, 0.0f}, up, axisPt, kCAmber, false, 11, 1025));   // another colour
        s.Touched(MakeMark(at + Vec{0.02f, 0.01f, 0.0f}, up, axisPt, kCBlue, false, 12, 1026));   // another monster
        s.Touched(MakeMark(at + Vec{0.30f, 0.0f, 0.0f}, up, axisPt, kCBlue, false, 11, 1030));    // 30 cm on
        n = s.Capsules(1040, out, kMaxCapsules);
        Expect(CountKind(out, n, TouchKind(kCBlue, kBall)) == 3 && CountKind(out, n, TouchKind(kCAmber, kBall)) == 1, "dedupe: another colour, monster or place is a mark of its own", CountKind(out, n, TouchKind(kCBlue, kBall)));
    }
    {   // a crowd: the marks come before the farther monsters, never before the nearest three; at most the effect's 32; a point that is not a number is dropped
        std::vector<Monster> crowd;
        for (int i = 0; i < 40; ++i) crowd.push_back(MakeMonster(100 + i, 100, {2, 2, 80}, 1010.0f + static_cast<float>(i % 8) * 2.0f, 1990.0f + static_cast<float>(i / 8) * 4.0f, w));
        Scene s;
        s.Set(MakeFrame(w, 1000, blade, crowd.data(), crowd.size()));
        for (int i = 0; i < static_cast<int>(kTouchSlots); ++i) s.Touched(MakeMark(at + Vec{0.4f * static_cast<float>(i), 0.0f, 0.0f}, up, axisPt, kCBlue, true, 100 + static_cast<uint32_t>(i), 1010 + 10 * static_cast<uint64_t>(i)));
        ContactMark bad = MakeMark({NAN, 0.0f, 0.0f}, up, axisPt, kCAmber, false, 100, 1100);
        s.Touched(bad);
        const int n = s.Capsules(1120, out, kMaxCapsules);
        Expect(n <= kMaxCapsules && CountKind(out, n, TouchKind(kCBlue, kBall)) == static_cast<int>(kTouchSlots) && CountKind(out, n, TouchKind(kCAmber, kBall)) == 0, "crowd: every contact drawn, the bad one dropped", n);
        int monsters = 0;
        for (int i = 0; i < n; ++i) monsters += out[i].kind == kMonster || out[i].kind == kRefused;
        Expect(monsters >= kNearestAlways, "crowd: the nearest monsters are still drawn", monsters);
        Expect(static_cast<int>(out[n - 1].kind) == TouchKind(kCBlue, kBall), "crowd: the newest contact's ball last, on top");
    }
    {   // the newest are kept when there are more than the slots
        Scene s;
        s.Set(MakeFrame(w, 1000, blade, mons, 3));
        for (size_t i = 0; i < kTouchSlots + 2; ++i) s.Touched(MakeMark(at + Vec{0.5f * static_cast<float>(i), 0.0f, 0.0f}, up, axisPt, kCBlue, false, 11, 1010 + 10 * i));
        const int n = s.Capsules(1100, out, kMaxCapsules);
        Expect(CountKind(out, n, TouchKind(kCBlue, kBall)) == static_cast<int>(kTouchSlots), "slots: no more than the slots", CountKind(out, n, TouchKind(kCBlue, kBall)));
        bool newest = false, oldest = false;
        for (int i = 0; i < n; ++i)
            if (static_cast<int>(out[i].kind) == TouchKind(kCBlue, kBall)) {
                const float x = out[i].a.x - at.x;
                if (Near(x, 0.5f * static_cast<float>(kTouchSlots + 1), 1e-4f)) newest = true;
                if (Near(x, 0.0f, 1e-4f)) oldest = true;
            }
        Expect(newest && !oldest, "slots: the newest is kept, the oldest is the one that goes");
    }
    {   // the effect's colours (the CPU copy): green, blue, amber, grey, red; the style: solid ball, see-through glass, thin tube; and the old kinds as they were
        const Camera& cam = w.cam[0];
        const Vec mid = Metres(w.eyeWorld) + Vec{1.0f, -0.1f, 0.0f};
        const float eye[3] = {w.eyeWorld.x, w.eyeWorld.y, w.eyeWorld.z};
        float a[kMaxCapsules][4], b[kMaxCapsules][4];
        const auto look = [&](int kind) {
            Capsule c[1] = {{mid - Vec{0.0f, 0.05f, 0.0f}, mid + Vec{0.0f, 0.05f, 0.0f}, 0.04f, static_cast<Kind>(kind)}};
            const int n = Pack(c, 1, kUpm, eye, a, b);
            Expect(n == 1 && static_cast<int>(b[0][3]) == kind, "kind packs as itself", kind);
            const Pixel p = ToPixel(cam, Vec{(a[0][0] + b[0][0]) * 0.5f, (a[0][1] + b[0][1]) * 0.5f, (a[0][2] + b[0][2]) * 0.5f});
            return Hitboxes({0.0f, 0.0f, 0.0f}, cam, p.x, p.y, a, b, n);
        };
        const Rgb green = look(TouchKind(kCGreen, kBall)), blue = look(TouchKind(kCBlue, kBall)), amber = look(TouchKind(kCAmber, kBall)), grey = look(TouchKind(kCGrey, kBall)), red = look(TouchKind(kCRed, kBall));
        Expect(green.g > 0.5f && green.g > 2.0f * green.r && green.g > 2.0f * green.b, "colour green", green.g);
        Expect(blue.b > 0.5f && blue.b > 2.0f * blue.r && blue.b > 1.5f * blue.g, "colour blue", blue.b);
        Expect(amber.r > 0.5f && amber.g > 0.4f && amber.r > 1.2f * amber.g && amber.b < 0.15f, "colour amber (red high, green a little under, no blue)", amber.r);
        Expect(grey.r > 0.4f && Near(grey.r, grey.g, 0.06f) && Near(grey.g, grey.b, 0.06f), "colour grey", grey.r);
        Expect(red.r > 0.5f && red.r > 3.0f * red.g && red.r > 3.0f * red.b, "colour red", red.r);
        const Rgb ball = look(TouchKind(kCBlue, kBall)), glass = look(TouchKind(kCBlue, kGlass)), tube = look(TouchKind(kCBlue, kTube));
        Expect(ball.b > tube.b && tube.b > glass.b && glass.b > 0.05f, "style: a ball is the most solid, a tube less, glass the least", glass.b);
        const Rgb violet = look(11), hot = look(kBladeHot), cold = look(kTrailCold);
        Expect(violet.b > violet.r && violet.r > violet.g, "kind 11 is still violet", violet.b);
        Expect(hot.g > hot.r && hot.g > hot.b, "kind 6 is still green", hot.g);
        Expect(cold.b > cold.r && cold.g > cold.r, "kind 9 is still cyan", cold.g);
    }
    {   // the module: with the debug off nothing is kept; on, a contact is drawn
        const float eye[3] = {0.0f, 0.0f, 0.0f};
        float a[kMaxCapsules][4], b[kMaxCapsules][4];
        SetWanted(false);
        Touched(MakeMark(at, up, axisPt, kCBlue, true, 11, 5000));
        SetWanted(true);
        Update(MakeFrame(w, 5010, blade, mons, 3));
        Expect(Take(5020, eye, a, b) == 4, "module: a contact made while it was off is not there", 0);
        Touched(MakeMark(at, up, axisPt, kCBlue, true, 11, 5030));
        Update(MakeFrame(w, 5040, blade, mons, 3));
        Expect(Take(5050, eye, a, b) == 4 + 4, "module: on, the contact's four capsules are in what the effect takes");
        SetWanted(false);
    }
}
int main(int argc, char** argv) {
    SpeedTests();
    ListTests();
    ConversionTests();
    ModuleTests();
    LogTests();
    TriggerKindTests();
    TouchMarkTests();
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--dump")) Dump(argv[i + 1]);
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("melee_boxes_test: all passed\n");
    return 0;
}
