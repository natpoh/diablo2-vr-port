// bow_pitch.h - [bow] pitch_aim: a bow's or crossbow's shot aimed by its whole 3D line - up and down, the stairs - not by its heading on the
// ground alone (docs/notes/bow_pitch.md). Pure maths, no game: cleanroom/tests/bow_pitch_test.cpp. The game's side is vr/bow_pitch_wire.cpp.
//
// Why the heading alone misses on the stairs. The game's world is a 2D grid; height is drawn. The renderer puts a thing that stands on ground h high
// at render (gx + h*k, h, gz + h*k), k = sqrt(3/2) (meleetouch::kGroundShear, melee_bodies.h): raised ground is drawn pushed along both horizontal
// axes. Monsters stand upright on it. So a monster on a floor 1.5 m below the hero is DRAWN 1.8 m off its cell on each axis, against the hero, and the
// heading of the line the player aims through it on the ground is not the heading from the hero to the monster in the game. On flat ground the push is
// the same for the hero and the monster and cancels; down or up the stairs it does not.
//
// What is decided here: what the line meets, in the render world (metres: world units over upm, the frame the drawn hands are put in):
//   - the first hostile monster's body on it (its limbs from its own skeleton, vr/monster_rig.h, or the capsule the game's numbers give it) - the
//     target is that monster's cell on the grid;
//   - else the first place it goes into the ground as drawn, known from where things stand (the hero, the monsters and players whose models were matched to them, Sample) - the
//     target is the cell under that point, the push taken off by the point's own height;
//   - else nothing (a line into the air): the line's own heading, as before.
// The shot's heading is the one from the hero's cell to the target's cell (the game's arrow is a 2D missile from the hero; it flies on past its point
// and meets the first monster on its way - so the heading is all that decides it). Nothing else of the shot is ours.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bowpitch {

constexpr float kShear = 1.22474487139f;   // = meleetouch::kGroundShear: render = grid + height * kShear on x and on z (the test holds them equal)

struct V3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(V3 a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// The cell (grid metres) of a point drawn ON the ground at render p: the push taken off by its own height. And back: a cell on ground h high, as drawn.
inline void GroundToGrid(V3 p, float* gx, float* gz) {
    *gx = p.x - p.y * kShear;
    *gz = p.z - p.y * kShear;
}
inline V3 GridToGround(float gx, float gz, float h) { return {gx + h * kShear, h, gz + h * kShear}; }

// The ray o + d*t (d a unit vector) against a sphere: the first t >= 0 on it; false: none.
inline bool RaySphere(V3 o, V3 d, V3 c, float r, float* t) {
    const V3 oc = o - c;
    const float b = Dot(d, oc), cc = Dot(oc, oc) - r * r;
    const float h = b * b - cc;
    if (!(h >= 0.0f)) return false;
    const float s = -b - std::sqrt(h);
    if (!(s >= 0.0f)) return false;
    *t = s;
    return true;
}

// The squared distance from p to the segment a-b.
inline float SegmentDist2(V3 p, V3 a, V3 b) {
    const V3 ab = b - a;
    const float l2 = Dot(ab, ab);
    const float s = l2 > 1e-12f ? std::clamp(Dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
    const V3 q = a + ab * s - p;
    return Dot(q, q);
}

// The ray o + d*t (d a unit vector) against the capsule a-b of radius r: the first t in [0, maxT] at which it is in it (0 when o already is);
// false: none. The capsule is the union of its round body and its two end balls - the first entry into it is the first into any of them.
inline bool RayCapsule(V3 o, V3 d, V3 a, V3 b, float r, float maxT, float* t) {
    if (!(r > 0.0f) || !Finite(o) || !Finite(d) || !Finite(a) || !Finite(b)) return false;
    if (SegmentDist2(o, a, b) <= r * r) { *t = 0.0f; return true; }
    float best = maxT;
    bool found = false;
    const V3 ba = b - a, oa = o - a;
    const float baba = Dot(ba, ba), bard = Dot(ba, d), baoa = Dot(ba, oa), rdoa = Dot(d, oa), oaoa = Dot(oa, oa);
    const float A = baba - bard * bard;   // 0: the ray runs along the axis (the balls answer)
    if (baba > 1e-12f && A > 1e-9f * baba) {
        const float B = baba * rdoa - baoa * bard, C = baba * oaoa - baoa * baoa - r * r * baba;
        const float h = B * B - A * C;
        if (h >= 0.0f) {
            const float s = (-B - std::sqrt(h)) / A;
            const float y = baoa + s * bard;   // where along the axis, times baba
            if (s >= 0.0f && y > 0.0f && y < baba && s <= best) { best = s; found = true; }
        }
    }
    float s;
    if (RaySphere(o, d, a, r, &s) && s <= best) { best = s; found = true; }
    if (RaySphere(o, d, b, r, &s) && s <= best) { best = s; found = true; }
    if (found) *t = best;
    return found;
}

// ---- the ground as drawn ------------------------------------------------------------------------------------------------------------------------
// Where something is known to stand: drawn at render (x, z) on ground y high (metres). The hero's feet, and the models matched to monsters and players.
struct Sample {
    float x = 0.0f, z = 0.0f, y = 0.0f;
};
// The ground's height under render (x, z): the nearest sample's within nearM across the ground, else `fallback` (the hero's own ground: where nothing is
// known the ground is taken as flat - the old aim, no change).
// known (optional): a sample answered (false: the fallback - a guess).
inline float GroundAt(const Sample* s, int n, float x, float z, float nearM, float fallback, bool* known = nullptr) {
    float best = nearM * nearM, y = fallback;
    bool k = false;
    for (int i = 0; i < n; ++i) {
        const float dx = s[i].x - x, dz = s[i].z - z, d2 = dx * dx + dz * dz;
        if (d2 < best) { best = d2; y = s[i].y; k = true; }
    }
    if (known) *known = k;
    return y;
}

// The unit a model stands for: the nearest of the units' cells (gx[i], gz[i], grid metres) to the model's place (its transform's translation, render
// metres) with the push taken off by its own height, within tolM. -1: none. *dist: how far that one is (metres; the shear's own check in the log).
inline int MatchUnit(V3 root, const float* gx, const float* gz, int n, float tolM, float* dist = nullptr) {
    float cx, cz;
    GroundToGrid(root, &cx, &cz);
    int best = -1;
    float bd = tolM * tolM;
    for (int i = 0; i < n; ++i) {
        const float dx = gx[i] - cx, dz = gz[i] - cz, d2 = dx * dx + dz * dz;
        if (d2 <= bd) { bd = d2; best = i; }
    }
    if (dist) *dist = best >= 0 ? std::sqrt(bd) : -1.0f;
    return best;
}

// ---- the scene a shot is decided in -------------------------------------------------------------------------------------------------------------
constexpr int kMaxMonsters = 48, kMaxCapsules = 192, kMaxSamples = 64;

struct Capsule {
    V3 a, b;
    float r = 0.0f;
    int monster = -1;    // its Monster
    bool limb = false;   // one of its limbs (its own skeleton); false: the capsule the game's numbers give it
};
struct Monster {
    uint32_t id = 0;
    float gx = 0.0f, gz = 0.0f;   // its cell: the game's grid in metres (game world units over upm)
    bool placed = false;          // its model was matched to it: its height is its own (else the hero's ground is assumed)
    float feetY = 0.0f;           // the ground it stands on (render metres)
};
struct Scene {
    Monster m[kMaxMonsters];
    int nm = 0;
    Capsule c[kMaxCapsules];
    int nc = 0;
    Sample s[kMaxSamples];
    int ns = 0;
    bool hero = false;                          // the hero's cell and ground are known (nothing is decided without them)
    float heroGx = 0.0f, heroGz = 0.0f, heroY = 0.0f;
};

struct Params {
    float maxRangeM = 40.0f;   // across the ground: no farther than this is looked (the game's own shot point is 40 cells out)
    float maxT = 60.0f;        // along the line
    float stepM = 0.1f;        // the walk along the line for the ground
    float nearM = 3.0f;        // a ground sample counts this far across (GroundAt)
    float minT = 0.3f;         // the ground is not looked for nearer the start than this (the bow is in the hand, the hand over the ground)
    float minGridM = 0.5f;     // a target nearer the hero's cell than this gives no heading: the line's own stays
};

enum class Hit { None, Monster, Floor };
inline const char* HitName(Hit h) { return h == Hit::Monster ? "a monster" : h == Hit::Floor ? "the ground" : "nothing"; }

struct Result {
    Hit hit = Hit::None;
    int monster = -1;             // Scene::m index (Hit::Monster)
    uint32_t id = 0;
    bool limb = false;
    float t = 0.0f;               // along the line, metres
    V3 at;                        // the point met, render metres
    float groundY = 0.0f;         // the ground under it (the monster's feet; the ground met)
    bool groundKnown = false;     // Hit::Floor: that ground is a sample's (something stands near), not the hero's ground taken where nothing is known
    float gx = 0.0f, gz = 0.0f;   // the target's cell, grid metres
    float gridDist = 0.0f;        // from the hero's cell, metres
    bool turned = false;          // the heading is the target's; false: the line's own (theta = lineTheta)
    float theta = 0.0f;           // the shot's heading on the grid, atan2(z, x)
    float lineTheta = 0.0f;       // the line's own heading on the ground, atan2(d.z, d.x) - what the shot went along before
    const char* why = "";
};

// The first ground crossing of the line o + d*t from above to below in [minT, maxT], walked at stepM and closed in on; false none. The walk stops early
// once the line climbs above every height known (it can meet no ground after that). known (optional): the ground met is a sample's (GroundAt).
inline bool FloorHit(const Scene& sc, V3 o, V3 d, const Params& p, float maxT, float* t, float* y, bool* known = nullptr) {
    float top = sc.heroY;
    for (int i = 0; i < sc.ns; ++i) top = std::max(top, sc.s[i].y);
    const auto ground = [&](float s) {
        const V3 q = o + d * s;
        return GroundAt(sc.s, sc.ns, q.x, q.z, p.nearM, sc.heroY);
    };
    const float step = std::max(0.01f, p.stepM);
    float prev = std::max(0.0f, p.minT);
    if (prev > maxT) return false;
    bool above = (o + d * prev).y > ground(prev);
    for (float s = prev + step; prev < maxT; s += step) {
        s = std::min(s, maxT);
        const V3 q = o + d * s;
        const float g = ground(s);
        const bool nowAbove = q.y > g;
        if (above && !nowAbove) {
            float lo = prev, hi = s;   // above at lo, on or below at hi
            for (int k = 0; k < 12; ++k) {
                const float mid = 0.5f * (lo + hi);
                if ((o + d * mid).y > ground(mid)) lo = mid;
                else hi = mid;
            }
            *t = hi;
            const V3 h = o + d * hi;
            *y = GroundAt(sc.s, sc.ns, h.x, h.z, p.nearM, sc.heroY, known);
            return true;
        }
        above = nowAbove;
        prev = s;
        if (d.y >= 0.0f && q.y > top) break;   // climbing over everything known: nothing more to meet
    }
    return false;
}

// What the line o + d (render metres; d need not be unit) meets first, and the heading the shot takes for it.
inline Result Decide(const Scene& sc, V3 o, V3 dIn, const Params& p) {
    Result r;
    const float dl = Length(dIn);
    if (!Finite(o) || !(dl > 1e-6f) || !std::isfinite(dl)) { r.why = "no line"; return r; }
    const V3 d = dIn * (1.0f / dl);
    r.lineTheta = r.theta = std::atan2(d.z, d.x);
    if (!sc.hero) { r.why = "the hero's cell is not known"; return r; }
    // as far along the line as maxRangeM across the ground (a line straight up or down: maxT)
    const float horiz = std::sqrt(d.x * d.x + d.z * d.z);
    const float maxT = horiz > 1e-4f ? std::min(p.maxT, p.maxRangeM / horiz) : p.maxT;
    // the monsters' bodies: the nearest entry
    float best = maxT;
    for (int i = 0; i < sc.nc; ++i) {
        const Capsule& c = sc.c[i];
        if (c.monster < 0 || c.monster >= sc.nm) continue;
        float t;
        if (RayCapsule(o, d, c.a, c.b, c.r, best, &t) && t <= best) {
            best = t;
            r.hit = Hit::Monster;
            r.monster = c.monster;
            r.limb = c.limb;
        }
    }
    // the ground, only when no body is met: the bodies are where the monsters are drawn, the ground is a guess between the places known (a line
    // down the stairs to a monster crosses the hero's own floor where the guess still has it - the player aims at what they see, the body)
    float ft, fy;
    bool fk = false;
    if (r.hit == Hit::None && FloorHit(sc, o, d, p, best, &ft, &fy, &fk)) {
        r.hit = Hit::Floor;
        best = ft;
        r.groundY = fy;
        r.groundKnown = fk;
    }
    if (r.hit == Hit::None) { r.why = "the line meets nothing within reach"; return r; }
    r.t = best;
    r.at = o + d * best;
    if (r.hit == Hit::Monster) {
        const Monster& m = sc.m[r.monster];
        r.id = m.id;
        r.gx = m.gx;
        r.gz = m.gz;
        r.groundY = m.placed ? m.feetY : sc.heroY;
    } else {
        GroundToGrid({r.at.x, r.groundY, r.at.z}, &r.gx, &r.gz);
    }
    const float dx = r.gx - sc.heroGx, dz = r.gz - sc.heroGz;
    r.gridDist = std::sqrt(dx * dx + dz * dz);
    if (!(r.gridDist >= p.minGridM) || !std::isfinite(r.gridDist)) { r.why = "the target is on the hero's own cell"; return r; }
    r.theta = std::atan2(dz, dx);
    r.turned = true;
    r.why = r.hit == Hit::Monster ? (r.limb ? "a limb" : "the game's capsule") : "the ground";
    return r;
}

// The heading's change, degrees in (-180, 180]: what the shot turned by against the line's own.
inline float TurnDeg(const Result& r) {
    float a = (r.theta - r.lineTheta) * 57.2957795f;
    while (a > 180.0f) a -= 360.0f;
    while (a <= -180.0f) a += 360.0f;
    return a;
}

}  // namespace bowpitch
