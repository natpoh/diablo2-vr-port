// knock_pose.h - [weight] knock_down: a humanoid's drawn pose laid on the knock-down's joints (docs/notes/knock_down.md). Pure
// (cleanroom/tests/knock_pose_test.cpp).
//
// The knock-down library (shared/d2r_vr_fall.h) says where each of 18 joints of a struck humanoid is drawn - falling, lying, getting up. The game's own pose
// of it (its world pose, the model's frame: row vectors, a bone's axes in rows 0-2, its origin in row 3) is turned bone by bone until its joints lie there: the
// whole body moved and turned so its pelvis and hips are the joints'; the trunk turned about the pelvis to the shoulders and the neck; the head about the neck;
// each arm about the shoulder to the elbow, the forearm about the elbow to the wrist; each thigh about the hip to the knee, the shin about the knee to the
// ankle, the foot about the ankle to the toe. Every bone hangs below its joint's bone, so it goes with it (a weapon in the hand, the fingers). Only the
// drawing: the unit's place and everything the game knows stay the game's.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "d2r_vr_fall.h"

namespace knockpose {

constexpr int kMaxBones = 256;

namespace detail {
struct V { float x = 0, y = 0, z = 0; };
inline V Add(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V Sub(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V Mul(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Len(V a) { return std::sqrt(Dot(a, a)); }
inline V Unit(V a, V fallback) {
    const float l = Len(a);
    return l > 1e-9f && std::isfinite(l) ? Mul(a, 1.0f / l) : fallback;
}
inline V Lerp(V a, V b, float t) { return Add(a, Mul(Sub(b, a), t)); }
inline V Get(const float* p) { return {p[0], p[1], p[2]}; }
// A 3x3 rotation, r[row][col], applied to a vector as r * v.
struct R3 { float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; };
inline V Apply(const R3& r, V v) {
    return {r.m[0][0] * v.x + r.m[0][1] * v.y + r.m[0][2] * v.z, r.m[1][0] * v.x + r.m[1][1] * v.y + r.m[1][2] * v.z,
            r.m[2][0] * v.x + r.m[2][1] * v.y + r.m[2][2] * v.z};
}
// The rotation that takes unit u onto unit v by the shortest way.
inline R3 Swing(V u, V v) {
    R3 r;
    const V axisRaw = Cross(u, v);
    const float s = Len(axisRaw), c = Dot(u, v);
    if (!(s > 1e-7f)) {
        if (c > 0.0f) return r;
        // half a turn about any axis square to u
        const V a = Unit(Cross(u, std::fabs(u.x) < 0.9f ? V{1, 0, 0} : V{0, 1, 0}), {0, 0, 1});
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                const float ai = i == 0 ? a.x : i == 1 ? a.y : a.z, aj = j == 0 ? a.x : j == 1 ? a.y : a.z;
                r.m[i][j] = 2.0f * ai * aj - (i == j ? 1.0f : 0.0f);
            }
        return r;
    }
    const V a = Mul(axisRaw, 1.0f / s);
    const float k = 1.0f - c;
    const float x = a.x, y = a.y, z = a.z;
    r.m[0][0] = c + x * x * k;     r.m[0][1] = x * y * k - z * s; r.m[0][2] = x * z * k + y * s;
    r.m[1][0] = y * x * k + z * s; r.m[1][1] = c + y * y * k;     r.m[1][2] = y * z * k - x * s;
    r.m[2][0] = z * x * k - y * s; r.m[2][1] = z * y * k + x * s; r.m[2][2] = c + z * z * k;
    return r;
}
// The frame whose z is `axis` and whose x is `ref` made square to it (columns x, y, z).
inline R3 Frame(V axis, V ref) {
    const V z = Unit(axis, {0, 1, 0});
    V x = Sub(ref, Mul(z, Dot(ref, z)));
    if (Len(x) < 1e-6f * std::max(1.0f, Len(ref))) x = Cross(std::fabs(z.y) < 0.9f ? V{0, 1, 0} : V{1, 0, 0}, z);
    x = Unit(x, {1, 0, 0});
    const V y = Cross(z, x);
    R3 r;
    const V c[3] = {x, y, z};
    for (int j = 0; j < 3; ++j) {
        r.m[0][j] = c[j].x;
        r.m[1][j] = c[j].y;
        r.m[2][j] = c[j].z;
    }
    return r;
}
// b * a^T: what takes frame a onto frame b.
inline R3 Between(const R3& a, const R3& b) {
    R3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i][j] = b.m[i][0] * a.m[j][0] + b.m[i][1] * a.m[j][1] + b.m[i][2] * a.m[j][2];
    return r;
}
}  // namespace detail

namespace detail {
// The farthest origin of a bone below b from b's own (parents come first), or false when none is below it at least `atLeast` away.
inline bool FarthestBelow(const int16_t* parent, const float (*mats)[16], int n, int b, float atLeast, V* out) {
    if (!parent || b < 0 || b >= n) return false;
    static thread_local uint8_t in[256];
    if (n > 256) return false;
    std::memset(in, 0, static_cast<size_t>(n));
    in[b] = 1;
    const V o{mats[b][12], mats[b][13], mats[b][14]};
    float best = atLeast;
    bool found = false;
    for (int i = b + 1; i < n; ++i) {
        if (parent[i] < 0 || parent[i] >= n || !in[parent[i]]) continue;
        in[i] = 1;
        const V p{mats[i][12], mats[i][13], mats[i][14]};
        const float d = Len(Sub(p, o));
        if (d > best) { best = d; *out = p; found = true; }
    }
    return found;
}
}  // namespace detail

// The joints of the pose (the model's frame) by the map (bone per joint, -1: none). A joint with no bone is put by its neighbours: the head's top over the
// neck along the trunk; the wrist at the farthest bone below the elbow (parent given), else past the elbow as far as the elbow is from the shoulder; the
// ankle alike below the knee (a leg of two bones, "leg_01" / "leg_02"), else as far below the knee as the knee is below the hip; the toe ahead of the ankle.
// *have: bit j the pose's own. False when a joint it cannot do without has no bone (or its bone is past n).
inline bool JointsOf(const int16_t map[D2RF_JOINTS], const float (*mats)[16], int n, float out[D2RF_JOINTS][3], uint32_t* have = nullptr,
                     const int16_t* parent = nullptr) {
    using namespace detail;
    V j[D2RF_JOINTS];
    uint32_t got = 0;
    for (int k = 0; k < D2RF_JOINTS; ++k) {
        const int b = map[k];
        if (b < 0 || b >= n) continue;
        j[k] = {mats[b][12], mats[b][13], mats[b][14]};
        if (!std::isfinite(j[k].x + j[k].y + j[k].z)) return false;
        got |= 1u << k;
    }
    const auto hasJ = [&](int k) { return (got >> k & 1u) != 0; };
    static const int kNeeded[] = {D2RF_PELVIS, D2RF_CHEST, D2RF_NECK, D2RF_L_SHOULDER, D2RF_L_ELBOW, D2RF_R_SHOULDER, D2RF_R_ELBOW,
                                  D2RF_L_HIP, D2RF_L_KNEE, D2RF_R_HIP, D2RF_R_KNEE};
    for (int k : kNeeded)
        if (!hasJ(k)) return false;
    // The pelvis joint is the middle of the hips, wherever the pelvis's bone has its origin: a rig whose legs hang from a "root" on the floor (the paladin's)
    // put the pelvis at the floor, and the library's body, its get-up and its walk with it - Serg on the first build: «по пояс в землю идут».
    j[D2RF_PELVIS] = Mul(Add(j[D2RF_L_HIP], j[D2RF_R_HIP]), 0.5f);
    for (int s = 0; s < 2; ++s) {   // the wrist and the ankle a limb's bones do not name
        const int sh = s ? D2RF_R_SHOULDER : D2RF_L_SHOULDER, hp = s ? D2RF_R_HIP : D2RF_L_HIP;
        if (!hasJ(sh + 2) && !FarthestBelow(parent, mats, n, map[sh + 1], 0.4f * Len(Sub(j[sh + 1], j[sh])), &j[sh + 2]))
            j[sh + 2] = Add(j[sh + 1], Mul(Sub(j[sh + 1], j[sh]), 0.9f));
        if (!hasJ(hp + 2) && !FarthestBelow(parent, mats, n, map[hp + 1], 0.4f * Len(Sub(j[hp + 1], j[hp])), &j[hp + 2]))
            j[hp + 2] = Add(j[hp + 1], Sub(j[hp + 1], j[hp]));
    }
    const V up = Unit(Sub(j[D2RF_NECK], j[D2RF_PELVIS]), {0, 1, 0});
    const V lat = Unit(Add(Sub(j[D2RF_R_SHOULDER], j[D2RF_L_SHOULDER]), Sub(j[D2RF_R_HIP], j[D2RF_L_HIP])), {1, 0, 0});
    const V fwd = Unit(Cross(lat, up), {0, 0, 1});
    if (!hasJ(D2RF_HEAD)) j[D2RF_HEAD] = Add(j[D2RF_NECK], Mul(up, std::max(0.5f * Len(Sub(j[D2RF_NECK], j[D2RF_CHEST])), 0.06f * Len(Sub(j[D2RF_NECK], j[D2RF_L_ANKLE])))));
    for (int s = 0; s < 2; ++s) {
        const int an = s ? D2RF_R_ANKLE : D2RF_L_ANKLE;
        if (!hasJ(an + 1)) {
            const float shin = Len(Sub(j[an], j[an - 1]));
            j[an + 1] = Sub(Add(j[an], Mul(fwd, 0.3f * shin)), Mul(up, 0.1f * shin));
        }
    }
    for (int k = 0; k < D2RF_JOINTS; ++k) {
        out[k][0] = j[k].x;
        out[k][1] = j[k].y;
        out[k][2] = j[k].z;
    }
    if (have) *have = got;
    return true;
}

namespace detail {
// Bone b and every bone below it (parents come first: the game keeps them so).
inline void Below(const int16_t* parent, int n, int b, uint8_t* in) {
    std::memset(in, 0, static_cast<size_t>(n));
    if (b < 0 || b >= n) return;
    in[b] = 1;
    for (int i = b + 1; i < n; ++i)
        if (parent[i] >= 0 && parent[i] < n && in[parent[i]]) in[i] = 1;
}
// The bones in `in` turned by r about p.
inline void Turn(float (*mats)[16], int n, const uint8_t* in, const R3& r, V p) {
    for (int b = 0; b < n; ++b) {
        if (!in[b]) continue;
        float* m = mats[b];
        for (int row = 0; row < 3; ++row) {
            const V v = Apply(r, Get(m + row * 4));
            m[row * 4 + 0] = v.x;
            m[row * 4 + 1] = v.y;
            m[row * 4 + 2] = v.z;
        }
        const V o = Add(p, Apply(r, Sub(Get(m + 12), p)));
        m[12] = o.x;
        m[13] = o.y;
        m[14] = o.z;
    }
}
inline V At(const float (*mats)[16], int b) { return {mats[b][12], mats[b][13], mats[b][14]}; }
// bone `turn` and all below it swung about the joint `from` (where it is now) so the way from it to the joint `to` (where it is now) points along
// target - targetFrom.
inline void Aim(float (*mats)[16], int n, const int16_t* parent, int turn, V from, V to, V target, V targetFrom, uint8_t* scratch) {
    if (turn < 0 || turn >= n) return;
    const V cur = Sub(to, from), want = Sub(target, targetFrom);
    if (Len(cur) < 1e-7f || Len(want) < 1e-7f) return;
    Below(parent, n, turn, scratch);
    Turn(mats, n, scratch, Swing(Unit(cur, {0, 1, 0}), Unit(want, {0, 1, 0})), from);
}
}  // namespace detail

// The pose (n matrices, the model's frame) laid on the joints `target` (the model's frame), weight 1 all of it, 0 none (the game's own; between: each joint
// that share of the way from the game's). map / parent: the bones of the joints and the skeleton's parents. False when the pose's joints do not read
// (JointsOf) or the bone count is out of range.
inline bool Apply(float (*mats)[16], int n, const int16_t* parent, const int16_t map[D2RF_JOINTS], const float target[D2RF_JOINTS][3], float weight) {
    using namespace detail;
    if (n <= 0 || n > kMaxBones || !parent) return false;
    weight = std::clamp(weight, 0.0f, 1.0f);
    if (!(weight > 0.0f)) return true;
    float cur[D2RF_JOINTS][3];
    uint32_t have = 0;
    if (!JointsOf(map, mats, n, cur, &have, parent)) return false;
    V t[D2RF_JOINTS];
    for (int k = 0; k < D2RF_JOINTS; ++k) {
        const V tk = Get(target[k]);
        if (!std::isfinite(tk.x + tk.y + tk.z)) return false;
        t[k] = Lerp(Get(cur[k]), tk, weight);
    }
    static thread_local uint8_t in[kMaxBones];
    const auto joint = [&](int k) { return Get(cur[k]); };
    const auto reread = [&]() { JointsOf(map, mats, n, cur, nullptr, parent); };
    // 1. the whole body: its pelvis onto the target's, turned so the trunk's line and the hips' lie as the target's
    {
        const R3 a = Frame(Sub(joint(D2RF_NECK), joint(D2RF_PELVIS)), Sub(joint(D2RF_R_HIP), joint(D2RF_L_HIP)));
        const R3 b = Frame(Sub(t[D2RF_NECK], t[D2RF_PELVIS]), Sub(t[D2RF_R_HIP], t[D2RF_L_HIP]));
        const R3 r = Between(a, b);
        const V p = joint(D2RF_PELVIS);
        std::memset(in, 1, static_cast<size_t>(n));
        Turn(mats, n, in, r, p);
        const V shift = Sub(t[D2RF_PELVIS], p);
        for (int i = 0; i < n; ++i) {
            mats[i][12] += shift.x;
            mats[i][13] += shift.y;
            mats[i][14] += shift.z;
        }
        reread();
    }
    // 2. the trunk, bent at the chest as the target's: the bone under the pelvis on the way to the chest swung about the pelvis so the chest lies the
    // target's way; then the chest's bone (or, when the chest is the pelvis's own bone, the one under it toward the neck) turned about the chest so the neck
    // and the shoulders lie as the target's
    {
        const int pelvis = map[D2RF_PELVIS], chest = map[D2RF_CHEST];
        const auto under = [&](int top, int to) {   // the bone just under `top` on the way up from `to` (-1: `to` is not below it)
            int b = to;
            for (int g = 0; g < n && b >= 0 && parent[b] != top; ++g) b = parent[b];
            return b >= 0 && parent[b] == top ? b : -1;
        };
        const int spine = chest != pelvis ? under(pelvis, chest) : -1;
        if (spine >= 0) Aim(mats, n, parent, spine, joint(D2RF_PELVIS), joint(D2RF_CHEST), t[D2RF_CHEST], t[D2RF_PELVIS], in);
        reread();
        const int upper = chest != pelvis ? chest : under(pelvis, map[D2RF_NECK]);
        if (upper >= 0) {
            const R3 a = Frame(Sub(joint(D2RF_NECK), joint(D2RF_CHEST)), Sub(joint(D2RF_R_SHOULDER), joint(D2RF_L_SHOULDER)));
            const R3 b = Frame(Sub(t[D2RF_NECK], t[D2RF_CHEST]), Sub(t[D2RF_R_SHOULDER], t[D2RF_L_SHOULDER]));
            Below(parent, n, upper, in);
            Turn(mats, n, in, Between(a, b), joint(D2RF_CHEST));
            reread();
        }
    }
    // 3. the head about the neck
    Aim(mats, n, parent, map[D2RF_NECK], joint(D2RF_NECK), joint(D2RF_HEAD), t[D2RF_HEAD], t[D2RF_NECK], in);
    reread();
    // 4. the clavicles (a bone between the chest and the upper arm): swung about their own origins toward where the target has the shoulders - the
    // get-up's shrug as it pushes itself up from the ground
    for (int s = 0; s < 2; ++s) {
        const int sh = s ? D2RF_R_SHOULDER : D2RF_L_SHOULDER;
        const int clav = map[sh] >= 0 && map[sh] < n ? parent[map[sh]] : -1;
        if (clav >= 0 && clav != map[D2RF_CHEST] && clav != map[D2RF_NECK]) {
            const V o = At(mats, clav);
            Aim(mats, n, parent, clav, o, joint(sh), t[sh], o, in);
            reread();
        }
    }
    // 5. the arms and the legs, each bone of a limb after the one above it
    for (int s = 0; s < 2; ++s) {
        const int sh = s ? D2RF_R_SHOULDER : D2RF_L_SHOULDER, hp = s ? D2RF_R_HIP : D2RF_L_HIP;
        const int chain[2][4] = {{sh, sh + 1, sh + 2, -1}, {hp, hp + 1, hp + 2, hp + 3}};
        for (const auto& c : chain)
            for (int k = 0; k + 1 < 4 && c[k + 1] >= 0; ++k) {
                const int from = c[k], to = c[k + 1];
                Aim(mats, n, parent, map[from], joint(from), joint(to), t[to], t[from], in);   // (a joint no bone names is put by its neighbours: JointsOf)
                reread();
            }
    }
    return true;
}

namespace detail {
// 4x4 row-vector affine matrices (a point p maps to p x M: rows 0-2 the axes, row 3 the place).
inline void MulM(const float* a, const float* b, float* o) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) o[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
}
inline bool InvAffine(const float* m, float* o) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
    const float det = a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
    if (!(std::fabs(det) > 1e-12f) || !std::isfinite(det)) return false;
    const float r[9] = {(e * k - f * h) / det, (c * h - b * k) / det, (b * f - c * e) / det,
                        (f * g - d * k) / det, (a * k - c * g) / det, (c * d - a * f) / det,
                        (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) o[i * 4 + j] = r[i * 3 + j];
        o[i * 4 + 3] = 0.0f;
    }
    for (int j = 0; j < 3; ++j) o[12 + j] = -(m[12] * r[j] + m[13] * r[3 + j] + m[14] * r[6 + j]);
    o[15] = 1.0f;
    return true;
}
}  // namespace detail

// What hangs on a bone (a weapon, a shield: its world matrix `world`, as the game made it from the bone as the game has it, `game` - the model's frame) carried
// with the bone as drawn (`drawn`): world x X^-1 x game^-1 x drawn x X into out, X the model's place in the world (m3 rows, root; world units). False when a
// matrix does not invert or the result is not finite.
inline bool Carry(const float world[16], const float game[16], const float drawn[16], const float m3[9], const float root[3], float out[16]) {
    using namespace detail;
    float x[16] = {}, xi[16], b[16], b2[16], bi[16], t1[16], t2[16], t3[16];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) x[i * 4 + j] = m3[i * 3 + j];
    for (int j = 0; j < 3; ++j) x[12 + j] = root[j];
    x[15] = 1.0f;
    std::memcpy(b, game, sizeof b);
    std::memcpy(b2, drawn, sizeof b2);
    b[3] = b[7] = b[11] = b2[3] = b2[7] = b2[11] = 0.0f;
    b[15] = b2[15] = 1.0f;
    if (!InvAffine(x, xi) || !InvAffine(b, bi)) return false;
    MulM(world, xi, t1);
    MulM(t1, bi, t2);
    MulM(t2, b2, t3);
    MulM(t3, x, out);
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(out[i])) return false;
    return true;
}

// ... and a point (world units) that goes with a bone - a limb's capsule end: p x X^-1 x game^-1 x drawn x X. False when a matrix does not invert.
inline bool CarryPoint(const float p[3], const float game[16], const float drawn[16], const float m3[9], const float root[3], float out[3]) {
    float w[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, p[0], p[1], p[2], 1}, o[16];
    if (!Carry(w, game, drawn, m3, root, o)) return false;
    for (int k = 0; k < 3; ++k) out[k] = o[12 + k];
    return true;
}

// A point of the render world (metres) into the model's frame: (p * upm - root) times the inverse of m3 (rows: the model's axes in the world). False when
// m3 does not invert.
inline bool ToModel(const float m3[9], const float root[3], float upm, const float p[3], float out[3]) {
    const float* m = m3;
    const float c00 = m[4] * m[8] - m[5] * m[7], c01 = m[5] * m[6] - m[3] * m[8], c02 = m[3] * m[7] - m[4] * m[6];
    const float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (!(std::fabs(det) > 1e-12f)) return false;
    const float inv[9] = {c00 / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
                          c01 / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
                          c02 / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
    const float v[3] = {p[0] * upm - root[0], p[1] * upm - root[1], p[2] * upm - root[2]};
    for (int j = 0; j < 3; ++j) out[j] = v[0] * inv[j] + v[1] * inv[3 + j] + v[2] * inv[6 + j];
    return std::isfinite(out[0] + out[1] + out[2]);
}
// ... and back: the model's frame to the render world (metres).
inline void ToWorld(const float m3[9], const float root[3], float upm, const float p[3], float out[3]) {
    for (int j = 0; j < 3; ++j) out[j] = (p[0] * m3[j] + p[1] * m3[3 + j] + p[2] * m3[6 + j] + root[j]) / upm;
}

// [weight] knock_down: which way a body faces against the knock-down library's frame (the library's points: mirrored in x, knock_down.cpp Flip).
// Do its toes (when its bones name them) point the way the library's frame calls forward? The log's check of the mirror, each knock-down: 1 yes, 0 no, -1 not known.
inline int ToesForward(const float (*j)[3], uint32_t have) {
    if (!(have >> D2RF_L_TOE & 1u) || !(have >> D2RF_R_TOE & 1u)) return -1;
    float lat[3], up[3], toes[3], fwd[3];
    for (int k = 0; k < 3; ++k) {
        lat[k] = j[D2RF_R_HIP][k] - j[D2RF_L_HIP][k];
        up[k] = j[D2RF_NECK][k] - j[D2RF_PELVIS][k];
        toes[k] = j[D2RF_L_TOE][k] - j[D2RF_L_ANKLE][k] + j[D2RF_R_TOE][k] - j[D2RF_R_ANKLE][k];
    }
    fwd[0] = lat[1] * up[2] - lat[2] * up[1];
    fwd[1] = lat[2] * up[0] - lat[0] * up[2];
    fwd[2] = lat[0] * up[1] - lat[1] * up[0];
    return fwd[0] * toes[0] + fwd[2] * toes[2] > 0.0f ? 1 : 0;
}
// ... and its knees, standing as the game has it: each knee off the line from its hip to its ankle - forward of it, as a leg bends (1), behind it (0), too
// straight to tell (-1). For a skeleton without toe bones.
inline int KneesForward(const float (*j)[3], uint32_t have) {
    const uint32_t legs = 1u << D2RF_L_HIP | 1u << D2RF_L_KNEE | 1u << D2RF_L_ANKLE | 1u << D2RF_R_HIP | 1u << D2RF_R_KNEE | 1u << D2RF_R_ANKLE;
    if ((have & legs) != legs) return -1;
    float lat[3], up[3], fwd[3];
    for (int k = 0; k < 3; ++k) {
        lat[k] = j[D2RF_R_HIP][k] - j[D2RF_L_HIP][k];
        up[k] = j[D2RF_NECK][k] - j[D2RF_PELVIS][k];
    }
    fwd[0] = lat[1] * up[2] - lat[2] * up[1];
    fwd[1] = lat[2] * up[0] - lat[0] * up[2];
    fwd[2] = lat[0] * up[1] - lat[1] * up[0];
    const float fl = std::sqrt(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (!(fl > 1e-6f)) return -1;
    float sum = 0.0f;
    for (const int hip : {D2RF_L_HIP, D2RF_R_HIP}) {
        const float* h = j[hip];
        const float* kn = j[hip + 1];
        const float* a = j[hip + 2];
        float ax[3], l2 = 0.0f, t = 0.0f;
        for (int k = 0; k < 3; ++k) { ax[k] = a[k] - h[k]; l2 += ax[k] * ax[k]; t += (kn[k] - h[k]) * ax[k]; }
        if (!(l2 > 1e-8f)) return -1;
        t /= l2;
        for (int k = 0; k < 3; ++k) sum += (kn[k] - (h[k] + ax[k] * t)) * fwd[k] / fl;
    }
    return std::fabs(sum) < 0.01f ? -1 : sum > 0.0f ? 1 : 0;
}
// Does the library's frame take the way a body goes as its front? lib: its joints as handed to the library (mirrored in x, its own labels); way: where the
// game moves it (mirrored too). The library's front is cross(R - L, up) across the ground: 1 it goes that way, -1 against it, 0 too slow or too sideways to
// say. The game walks its units toward their front, so a body that goes against the library's front has its sides the other way round for the library.
inline int FacesGoing(const float (*lib)[3], const float way[3]) {
    float lat[3], up[3];
    for (int k = 0; k < 3; ++k) {
        lat[k] = lib[D2RF_R_HIP][k] - lib[D2RF_L_HIP][k];
        up[k] = lib[D2RF_NECK][k] - lib[D2RF_PELVIS][k];
    }
    const float fx = lat[1] * up[2] - lat[2] * up[1], fz = lat[0] * up[1] - lat[1] * up[0];
    const float fl = std::sqrt(fx * fx + fz * fz), wl = std::sqrt(way[0] * way[0] + way[2] * way[2]);
    if (!(fl > 1e-6f) || !(wl > 1e-6f)) return 0;
    const float c = (fx * way[0] + fz * way[2]) / (fl * wl);
    return c > 0.7f ? 1 : c < -0.7f ? -1 : 0;
}

// Is the body drawn mirrored - its model's place in the world (m3 rows) a mirror (a negative determinant)? Until the game has been seen walking a model
// (FacesGoing, knock_down.cpp: the game's own word, once a model) this says whether its sides are swapped for the library (SwapSides). Never the toes:
// 0.192.16 swapped by the toes (ToesForward) and turned the rogues' rig back to front (Serg: «опять инвертировалась кукла, одевает рагдол задом наперёд на
// себя») - its toe bones say BACKWARD on a body the library draws right, the Fallen's and the brute's said either from one knock to the next. The toes and
// the knees are the log's word only. (The spearwoman's knees bent back, 0.192.15, were the gait core's step back: fall_physics 1.8.)
inline bool Mirrored(const float m3[9]) {
    const float det = m3[0] * (m3[4] * m3[8] - m3[5] * m3[7]) - m3[1] * (m3[3] * m3[8] - m3[5] * m3[6]) + m3[2] * (m3[3] * m3[7] - m3[4] * m3[6]);
    return det < 0.0f;
}
// A body's left and right joints swapped (going to the library and coming back - the same swap undoes itself); have's bits with them (may be null).
inline void SwapSides(float (*j)[3], uint32_t* have) {
    static const int kPairs[][2] = {{D2RF_L_SHOULDER, D2RF_R_SHOULDER}, {D2RF_L_ELBOW, D2RF_R_ELBOW}, {D2RF_L_WRIST, D2RF_R_WRIST}, {D2RF_L_HIP, D2RF_R_HIP},
                                    {D2RF_L_KNEE, D2RF_R_KNEE},         {D2RF_L_ANKLE, D2RF_R_ANKLE}, {D2RF_L_TOE, D2RF_R_TOE}};
    for (const auto& pr : kPairs) {
        for (int k = 0; k < 3; ++k) std::swap(j[pr[0]][k], j[pr[1]][k]);
        if (have) {
            const uint32_t a = *have >> pr[0] & 1u, b = *have >> pr[1] & 1u;
            *have = (*have & ~(1u << pr[0] | 1u << pr[1])) | b << pr[0] | a << pr[1];
        }
    }
}
inline int SwapJoint(int j, bool swap) {
    if (!swap || j < 0) return j;
    static const int kPairs[][3] = {{D2RF_L_SHOULDER, D2RF_R_SHOULDER, 3}, {D2RF_L_HIP, D2RF_R_HIP, 4}};   // (an arm's three joints, a leg's four)
    for (const auto& pr : kPairs)
        for (int k = 0; k < pr[2]; ++k)
            if (j == pr[0] + k) return pr[1] + k;
            else if (j == pr[1] + k) return pr[0] + k;
    return j;
}

}  // namespace knockpose
