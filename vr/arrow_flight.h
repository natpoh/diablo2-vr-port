// arrow_flight.h - [bow] arrow_flight: the hero's shot arrow DRAWN by us - from the bow, along the shot's line, up or down with it ([bow] pitch_aim), to what
// the line met - while the game's own arrow missile flies, hits and does its damage exactly as ever. Visual only (docs/notes/arrow_flight.md).
//
// The game's arrow is a 2D missile from the hero: the client draws it flat at its own height, starting at the hero's body, along the heading the shot was
// given. What we change is only WHERE the renderer puts its entities - the game's own arrow model, its own mesh: the world-matrix function (0xE3A8C0, hooked
// by vr/potion_hide.cpp for the thrown potion) computes the arrow's matrix every frame, and for the hero's arrow missile of a shot of ours the matrix is
// written again - at our place on our line, the game's own scale and roll kept, turned from the game's flat heading to our line. The game's missile, its
// speed, its collision, its hit and its damage are untouched; so is everything about any other missile.
//
// Where our arrow is drawn: by how far the game's own arrow has come (its drawn travel across the ground since its first frame, `s`), never by our clock -
// so ours arrives when the game's arrives, and the game's hit is seen where ours is:
//   - Aimed (the shot's line met a monster or the ground, [bow] pitch_aim, and this arrow is the shot's own - its heading is the one the game was given, not
//     a Multiple Shot's side arrow, not Guided Arrow which homes): from the release point straight to the point met, reached when the game's arrow has come
//     as far as the target's cell is from it. A monster: on along the line past it (the game kills its missile on the hit; a miss flies on). The ground,
//     where something stands near (the ground is known there): it stays there, stuck - the game's missile flies on unseen to its range.
//   - Line (the shot's own arrow, the line met nothing - pitch_aim off, a line into the air - and [bow] arrow_flight_straight): straight along the shot's
//     line from the release point, as far along it as the game's has come across the ground - no bend onto the game's path, which runs beside it (the
//     game's arrow starts at the hero's body, ours at the bow), its heading the same ([bow] shot_line_hold keeps the game's aim on this line).
//   - Blend (a side arrow of Multiple Shot or Strafe, Guided Arrow, or Line's box off): it leaves from the release point and joins the game's own
//     path over the first kBlendM metres - out of the bow, not out of the hero's chest.
//
// Pure: no game, no Windows. The game's side is vr/arrow_flight_wire.cpp; the test cleanroom/tests/arrow_flight_test.cpp.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "bow_pitch.h"

namespace arrowflight {

using bowpitch::V3;
using bowpitch::Dot;
using bowpitch::Length;

// -- which missiles: the hero's arrows and bolts, by their class (the game's Missiles.txt *ID, the client unit's +0x4 - the same numbering potion_hide's 44 - 49
// are). The explosions, clouds and fires they make on a hit are missiles of other classes (explodingarrowexp 42, freezingarrowexp1 88, the immolation's
// fire...): never ours to move.
constexpr uint32_t kArrow = 0, kFireArrow = 12, kMagicArrow = 27, kIceArrow = 28, kBolt = 31, kColdArrow = 40, kExplodingArrow = 41, kImmolationArrow = 85,
                   kGuidedArrow = 86, kFreezingArrow = 87, kMultiArrow = 213, kMultiBolt = 214, kStrafeArrow = 682, kStrafeBolt = 683;
inline bool IsArrowClass(uint32_t c) {
    switch (c) {
        case kArrow: case kFireArrow: case kMagicArrow: case kIceArrow: case kBolt: case kColdArrow: case kExplodingArrow: case kImmolationArrow:
        case kGuidedArrow: case kFreezingArrow: case kMultiArrow: case kMultiBolt: case kStrafeArrow: case kStrafeBolt:
            return true;
        default:
            return false;
    }
}
// Guided Arrow homes on its target: the game's path bends, a straight line of ours would leave it - always Blend.
inline bool Homes(uint32_t c) { return c == kGuidedArrow; }

// -- the unit (potion_hide.h's fields: a missile is type 3, its owner recorded when +0x128 has 0x400)
constexpr uint32_t kTypePlayer = 0, kTypeMissile = 3, kFlagHasOwner = 0x400;
struct Unit {
    uint32_t type = ~0u, cls = 0, id = 0, flags = 0, ownerType = ~0u, ownerId = 0;
};
// The hero's arrow: heroId 0 = not known (any player's counts: offline that is the hero's).
inline bool IsHeroArrow(const Unit& u, uint32_t heroId) {
    if (u.type != kTypeMissile || !IsArrowClass(u.cls)) return false;
    if (!(u.flags & kFlagHasOwner) || u.ownerType != kTypePlayer) return false;
    return heroId == 0 || u.ownerId == heroId;
}

// -- the shot, as we aimed it (vrcam, at the game's own read of the shot's aim). Everything in WORLD units (the render world the matrices are in: metres x upm).
struct Shot {
    bool ok = false;
    uint64_t atMs = 0;       // when it was noted (the press)
    unsigned shot = 0;       // the bow draw's shot count (0: a line of the hand's - a crossbow, a bow without the draw)
    V3 start;                // the arrow's release point (the bow model's arrow rest, else the bow hand)
    V3 dir;                  // the line, a unit vector
    float theta = 0.0f;      // the heading the game's shot was given, atan2(z, x) on the ground
    bool target = false;     // the line met something ([bow] pitch_aim): `at`
    V3 at;                   // the point met
    bool monster = false;    // ... on a monster's body (else the ground)
    bool stick = false;      // the ground met is known there (something stands near): our arrow stays in it
    float gridDist = 0.0f;   // the target's cell from the hero's cell
    V3 hero;                 // the hero as drawn at the press (his cell, as drawn on his ground)
    float upm = 1.0f;        // world units a metre
};
inline constexpr uint64_t kWindowMs = 1500;   // an arrow of the hero's made this long after the press is that shot's (the animation, the server's create, Strafe's last)

inline bool Armed(const Shot& s, uint64_t nowMs) { return s.ok && nowMs >= s.atMs && nowMs - s.atMs <= kWindowMs; }

struct Params {
    bool straight = true;     // Line for the shot's own arrow with nothing met ([bow] arrow_flight_straight); false: Blend, as 0.179 drew it
    float minAcross = 0.2f;   // Line: a line this steep or steeper goes along it no faster than this part of a metre across the ground per metre
    float blendM = 2.5f;      // Blend: over this many metres of the game's travel ours joins its path
    float decideM = 0.15f;    // after this many metres of travel the arrow's heading is read: is it the shot's own
    float aimTolDeg = 4.0f;   // ... the shot's own when it is within this many degrees of the heading the game was given
    float minSpanM = 0.5f;    // Aimed: the game's arrow counts at least this far to the target's cell
};

// One arrow (a missile unit) of a shot. The renderer's thread steps it; its state is only ever moved on by the game's own travel.
struct Track {
    Shot shot;
    uint32_t cls = 0;
    bool started = false;
    V3 g0;                   // the game's arrow's first place
    bool decided = false, own = false, aimed = false;   // own: the shot's own arrow (its heading the one given, not homing); aimed: own and the line met something
    float span = 0.0f;       // Aimed: the game's travel at which ours reaches the point met (world units)
};

enum class Mode : uint8_t { Aimed, Line, Blend };
inline const char* ModeName(Mode m) { return m == Mode::Aimed ? "aimed" : m == Mode::Line ? "along the line" : "blended onto the game's path"; }

struct Pose {
    Mode mode = Mode::Blend;
    V3 pos;          // where ours is drawn
    V3 dir;          // the way it points (unit)
    V3 gameDir;      // the way the game's own points (unit; its flat heading)
    float travel = 0.0f;   // the game's arrow's travel across the ground so far (world units)
    float u = 0.0f;        // Aimed: the way to the point met (1 = there); Blend: how far joined (1 = on the game's path)
    bool aimed = false;
    bool stuck = false;    // Aimed at known ground and there: it stays
};

inline V3 Unit3(V3 v, V3 fallback) {
    const float l = Length(v);
    return l > 1e-6f && std::isfinite(l) ? v * (1.0f / l) : fallback;
}
inline V3 Heading(float theta) { return {std::cos(theta), 0.0f, std::sin(theta)}; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// The rotation that takes the unit vector a onto the unit vector b, applied to v (the shortest one; a half turn about any axis across a when they are opposed).
inline V3 RotateFromTo(V3 a, V3 b, V3 v) {
    const float c = std::clamp(Dot(a, b), -1.0f, 1.0f);
    V3 k = Cross(a, b);
    float s = Length(k);
    if (s < 1e-6f) {
        if (c > 0.0f) return v;   // the same way
        k = Unit3(Cross(a, std::fabs(a.y) < 0.9f ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f}), V3{0.0f, 0.0f, 1.0f});
        s = 0.0f;
    } else {
        k = k * (1.0f / s);
    }
    // Rodrigues: v cos + (k x v) sin + k (k.v)(1 - cos)
    const V3 kv = Cross(k, v);
    return v * c + kv * s + k * (Dot(k, v) * (1.0f - c));
}

inline float SmoothStep(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// The game's arrow is at `game` now (its root entity's place, world units): where ours is drawn, and which way it points.
inline Pose Step(Track& tr, V3 game, const Params& p = Params{}) {
    Pose o;
    const Shot& sh = tr.shot;
    const float upm = sh.upm > 1e-4f && std::isfinite(sh.upm) ? sh.upm : 1.0f;
    if (!tr.started) { tr.started = true; tr.g0 = game; }
    const V3 moved = game - tr.g0;
    V3 flat = moved;
    flat.y = 0.0f;
    o.travel = Length(flat);
    const V3 given = Heading(sh.theta);
    o.gameDir = o.travel > 0.02f * upm ? Unit3(moved, given) : given;
    if (!tr.decided && o.travel >= p.decideM * upm) {
        tr.decided = true;
        const V3 h = Unit3(flat, given);
        tr.own = !Homes(tr.cls) && Dot(h, given) >= std::cos(p.aimTolDeg * 0.0174532925f);
        tr.aimed = tr.own && sh.target;
        if (tr.aimed) {   // the game's arrow reaches the target's cell after the cell's distance less where it began along the heading
            V3 off = tr.g0 - sh.hero;
            off.y = 0.0f;
            tr.span = std::max(p.minSpanM * upm, sh.gridDist - Dot(off, given));
        }
    }
    const bool own = tr.decided ? tr.own : !Homes(tr.cls);   // (before its heading is read: the shot's own, presumed - every arrow is at the bow then)
    o.aimed = own && sh.target;
    if (o.aimed) {
        o.mode = Mode::Aimed;
        const float span = tr.decided ? tr.span : std::max(p.minSpanM * upm, sh.gridDist);
        o.u = span > 1e-6f ? o.travel / span : 1.0f;
        const V3 way = sh.at - sh.start;
        o.stuck = !sh.monster && sh.stick && o.u >= 1.0f;
        const float u = o.stuck ? 1.0f : o.u;
        o.pos = sh.start + way * u;
        o.dir = Unit3(way, sh.dir);
        return o;
    }
    if (own && p.straight) {   // Line: along the shot's own line, as far as the game's has come across the ground
        o.mode = Mode::Line;
        const float across = std::sqrt(sh.dir.x * sh.dir.x + sh.dir.z * sh.dir.z);
        const float t = o.travel / std::max(across, p.minAcross);
        o.u = 1.0f;
        o.pos = sh.start + sh.dir * t;
        o.dir = Unit3(sh.dir, o.gameDir);
        return o;
    }
    o.mode = Mode::Blend;
    const float L = p.blendM * upm;
    const float x = L > 1e-6f ? o.travel / L : 1.0f;
    const float b = SmoothStep(x);
    const float db = x < 1.0f && L > 1e-6f ? 6.0f * x * (1.0f - x) / L : 0.0f;   // d b / d travel
    const V3 lag = sh.start - tr.g0;   // ours begins this far from the game's
    o.u = b;
    o.pos = game + lag * (1.0f - b);
    // the travel goes along the game's heading at one unit per unit: our motion is that, less the lag being taken up
    o.dir = Unit3(o.gameDir + lag * (-db), o.gameDir);
    return o;
}

// -- the matrices: row vectors (a point is local * M), the 3 x 3 rows the axes, m[12..14] the place - the TransformComponent's world matrix (potion_hide.h).

inline bool FiniteM(const float m[16]) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}
inline V3 Place(const float m[16]) { return {m[12], m[13], m[14]}; }

// Our matrix for the arrow's root: the game's own (its scale and its roll about the arrow) turned from the game's heading `from` to ours `to` (units), at `pos`.
inline bool Compose(const float game[16], V3 from, V3 to, V3 pos, float out[16]) {
    if (!FiniteM(game) || !bowpitch::Finite(from) || !bowpitch::Finite(to) || !bowpitch::Finite(pos)) return false;
    std::memcpy(out, game, sizeof(float) * 16);
    for (int r = 0; r < 3; ++r) {
        const V3 a = RotateFromTo(from, to, {game[r * 4 + 0], game[r * 4 + 1], game[r * 4 + 2]});
        out[r * 4 + 0] = a.x;
        out[r * 4 + 1] = a.y;
        out[r * 4 + 2] = a.z;
    }
    out[12] = pos.x;
    out[13] = pos.y;
    out[14] = pos.z;
    return FiniteM(out);
}

// The inverse of an affine row-vector matrix (the 3 x 3 any invertible one, the last column 0 0 0 1). False: not invertible.
inline bool InverseAffine(const float m[16], float out[16]) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (!(std::fabs(det) > 1e-20f) || !std::isfinite(det)) return false;
    const float r = 1.0f / det;
    float inv[9] = {A * r, -(b * i - c * h) * r, (b * f - c * e) * r,
                    B * r, (a * i - c * g) * r, -(a * f - c * d) * r,
                    C * r, -(a * h - b * g) * r, (a * e - b * d) * r};
    std::memset(out, 0, sizeof(float) * 16);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) out[row * 4 + col] = inv[row * 3 + col];
    for (int col = 0; col < 3; ++col) out[12 + col] = -(m[12] * inv[0 * 3 + col] + m[13] * inv[1 * 3 + col] + m[14] * inv[2 * 3 + col]);
    out[15] = 1.0f;
    return FiniteM(out);
}

inline void Multiply(const float a[16], const float b[16], float out[16]) {
    float r[16];
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[row * 4 + k] * b[k * 4 + col];
            r[row * 4 + col] = s;
        }
    std::memcpy(out, r, sizeof r);
}

// Another entity of the same arrow (its mesh, its effect, its light) carried with the root: where it stood against the game's root it stands against ours -
// child * inverse(gameRoot) * ourRoot.
inline bool Follow(const float child[16], const float gameRoot[16], const float ourRoot[16], float out[16]) {
    float inv[16], rel[16];
    if (!FiniteM(child) || !FiniteM(ourRoot) || !InverseAffine(gameRoot, inv)) return false;
    Multiply(child, inv, rel);
    Multiply(rel, ourRoot, out);
    return FiniteM(out);
}

// Did the world-matrix function already compute this entity from the root we moved (it computes a parent first, through the same hook)? Then it is nearer
// our root than the game's. When the two roots are nearly one place, nothing is to be told apart - and nothing needs moving.
inline bool AlreadyFollows(const float child[16], const float gameRoot[16], const float ourRoot[16], float sameM) {
    const V3 c = Place(child), g = Place(gameRoot), o = Place(ourRoot);
    if (Length(g - o) <= sameM) return true;
    return Length(c - o) < Length(c - g);
}

}  // namespace arrowflight
