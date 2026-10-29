// remote_pose.h - [multiplayer] pose_share_hero: another player's hero model posed from the pose that came through D2RLoader's plugin network
// (vr/pose_share.h format 3) - its head as the headset is held, its arms on the controllers by our own arm IK (skeletons.cpp: Elbow, AimChain,
// PlanWrist / ApplyWrist, as our F4 body is posed), the weapon in hand following the wrist, the upper body turned to where the player looks.
// Visual only, on each receiver: the drawn bones of that rig, nothing of the game's.
//
// Pure maths (cleanroom/tests/remote_pose_test.cpp), in the hero's MODEL space (skeletons.cpp's: y up, faces +z, its left +x):
//   - which rig is whose: another player's rig (one the doll did not take as ours) is the received pose whose hero stands nearest it on the
//     grid, within kMatchUnits (both read in the look-at's units: ecs::BodyPlace and the unit's path, as OursNow does for ours);
//   - the turn into the model: the sender sends its head's yaw in its own hero's model space (skel::HeroYawInModel - the very yaw its own
//     arms are posed by), so the head's turn-only frame goes into the model as on the sender (ToModel, PoseBody's): the game shows the
//     unit facing the same way on both PCs;
//   - the hands: from the rig's own head bone, the sender's hand offsets (metres from its head, the head's frame) over the model's units
//     per metre - the head bone's height over the sender's eye height (ControllerPoint's rule);
//   - the head: the look and its up from the headset's pitch and roll (pose_share.h HeadQuat), the pitch held within kMaxPitchDeg; the
//     bone is turned by the least turn taking its forward and up (as the bind pose has them) there (FrameTurn).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "pose_share.h"

namespace remotepose {

constexpr float kMatchUnits = 2.0f;     // a rig and a pose's hero this near on the grid are the same player (a unit is ~0.27 m)
constexpr float kMaxPitchDeg = 70.0f;   // the head looks no further up or down than this (a neck)
constexpr int kMaxHeroes = 8;

// What the skeleton hook needs of one received pose (vrcam makes it on its update thread; the hook reads a copy).
struct Target {
    uint32_t unit = 0;
    float x = 0.0f, z = 0.0f;               // where its hero stands on the grid (the look-at's units)
    float yawInModel = 0.0f;                // radians, PoseBody's convention
    float heading = 0.0f;                   // radians: the sender's head's turn in the game's world (pose_share.h)
    float worldUpm = 10.0f;                 // the world's units (the look-at's) in a metre of the sender's body - vrcam's: our hero's eyes over his eye height
    bool legs = false;                      // [multiplayer] pose_share_legs: his legs step along his travel (the gait library), his whole body turned to his look
    float eyeM = 1.6f;
    uint8_t flags = 0;                      // poseshare::kHead / kRight / kLeft
    float headQ[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // the head's pitch and roll, its turn-only frame
    float hand[2][3] = {};                  // [0] right, [1] left: metres from the head, the head's turn-only frame (x right, y up, z back)
    float rot[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};   // the controllers, the same frame
    // [slot 0 right, 1 left] the weapon's attach bone as the sender draws it (format 5, every pose): origin from the head (metres, the head's frame), the turn of
    // its rows, mirrored or not. The receiver hangs that slot's attach bone there; a hand that holds the weapon is put where the weapon says.
    bool attach[2] = {false, false};
    bool holds[2] = {false, false};         // [0] right, [1] left: the hand holds the weapon
    bool grips = false;                     // the grips came
    float grip[2] = {0.0f, 0.0f};           // how closed each hand is, 0..1
    bool attachMirror[2] = {false, false};
    float attachPos[2][3] = {};
    float attachQ[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    // [format 6] the hands as the sender draws them: each hand bone (from the head, metres, the head's frame; the turn of its rows, mirrored or not) and elbow,
    // where its head bone is from its eye, and its model's units a metre - the receiver hangs all of it, the weapon's bones too, from its rig's head bone the
    // sender's way (EyeOnRig) in the sender's units
    bool drawn = false;
    float modelUpm = 0.0f;
    float headBone[3] = {};
    float handPos[2][3] = {};
    float handQ[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    bool handMirror[2] = {false, false};
    float handElbow[2][3] = {};
    // [format 7] what each hand has (pose_share.h Pose::Held: its pose in the hero's frame, from the feet), and the sender's world units a metre
    bool hasHeld[2] = {false, false};
    poseshare::Pose::Held held[2];
    float heldPos[2][3] = {};               // ... its place from the head (metres, the head's frame), as the hands
    float heldWorldUpm = 0.0f;
};

constexpr float kMinModelUpm = 1.0f, kMaxModelUpm = 20.0f;   // a sender's model units a metre that may be used (its own clamp is 2..8 times its body scale)

// A received pose as a target: it needs its head and the model's yaw (a sender of format 2 has none). False: not one to pose by.
inline bool FromPose(const poseshare::Pose& p, uint32_t unit, float gridX, float gridZ, Target* t) {
    using namespace poseshare;
    if (!t || !(p.flags & kHead) || !(p.flags & kModelYaw) || !Plausible(p) || !std::isfinite(p.yawInModel)) return false;
    *t = Target{};
    t->unit = unit;
    t->x = gridX;
    t->z = gridZ;
    t->yawInModel = p.yawInModel;
    t->heading = p.heading;
    t->eyeM = p.eyeM;
    t->flags = p.flags & (kHead | kRight | kLeft);
    for (int i = 0; i < 4; ++i) t->headQ[i] = p.head.q[i];
    const Joint* hands[2] = {&p.right, &p.left};
    for (int s = 0; s < 2; ++s) {
        for (int i = 0; i < 3; ++i) t->hand[s][i] = hands[s]->p[i] - p.head.p[i];
        for (int i = 0; i < 4; ++i) t->rot[s][i] = hands[s]->q[i];
        t->attach[s] = (p.flags2 & (s ? kAttach1 : kAttach0)) != 0;
        t->attachMirror[s] = (p.flags2 & (s ? kMirror1 : kMirror0)) != 0;
        for (int i = 0; i < 3; ++i) t->attachPos[s][i] = p.attach[s].p[i] - p.head.p[i];
        for (int i = 0; i < 4; ++i) t->attachQ[s][i] = p.attach[s].q[i];
        t->holds[s] = (p.flags2 & (s ? kHoldL : kHoldR)) != 0;
        t->grip[s] = p.grip[s];
    }
    t->grips = (p.flags2 & kGrips) != 0;
    if ((p.flags2 & kHands) && std::isfinite(p.modelUpm) && p.modelUpm >= kMinModelUpm && p.modelUpm <= kMaxModelUpm) {
        t->drawn = true;
        t->modelUpm = p.modelUpm;
        for (int i = 0; i < 3; ++i) t->headBone[i] = p.headBone[i] - p.head.p[i];
        for (int s = 0; s < 2; ++s) {
            for (int i = 0; i < 3; ++i) t->handPos[s][i] = p.hand[s].p[i] - p.head.p[i];
            for (int i = 0; i < 3; ++i) t->handElbow[s][i] = p.handElbow[s][i] - p.head.p[i];
            for (int i = 0; i < 4; ++i) t->handQ[s][i] = p.hand[s].q[i];
            t->handMirror[s] = p.handMirror[s];
        }
    }
    for (int s = 0; s < 2; ++s) {
        t->hasHeld[s] = (p.flags3 & (s ? kHeldL : kHeldR)) != 0 && p.held[s].what != kHeldNone;
        if (!t->hasHeld[s]) continue;
        t->held[s] = p.held[s];
        for (int i = 0; i < 3; ++i) t->heldPos[s][i] = p.held[s].pose.p[i] - p.head.p[i];
    }
    t->heldWorldUpm = p.worldUpm;
    return true;
}

// [format 7] What hand `s` has, against that hand as he draws it (Target::drawn): its place along the hand bone's own axes (metres) and its rows in them - so it sits in
// the hand here as in his, wherever this rig's hand is. False: no hand as drawn, or nothing in it.
inline bool HeldInHand(const Target& t, int s, float relP[3], float relR[3][3]) {
    if (s < 0 || s > 1 || !t.drawn || !t.hasHeld[s] || t.held[s].state != poseshare::kInHand) return false;
    const float e[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    float h[3][3], w[3][3];
    for (int i = 0; i < 3; ++i) {
        poseshare::Rotate(t.handQ[s], e[i], h[i]);
        if (i == 2 && t.handMirror[s]) for (float& v : h[i]) v = -v;
        poseshare::Rotate(t.held[s].pose.q, e[i], w[i]);
    }
    const float d[3] = {t.heldPos[s][0] - t.handPos[s][0], t.heldPos[s][1] - t.handPos[s][1], t.heldPos[s][2] - t.handPos[s][2]};
    for (int i = 0; i < 3; ++i) {
        relP[i] = d[0] * h[i][0] + d[1] * h[i][1] + d[2] * h[i][2];
        for (int j = 0; j < 3; ++j) relR[j][i] = w[j][0] * h[i][0] + w[j][1] * h[i][1] + w[j][2] * h[i][2];
    }
    return true;
}
// ... on a hand as drawn here (its world matrix, row vectors: the bone's rows, scaled, and its origin), the world's units a metre of his body there, the thing's size:
// the thing's world matrix. False: a hand bone with no rows.
inline bool OnHand(const float hand[16], const float relP[3], const float relR[3][3], float perM, float scale, float out[16]) {
    float u[3][3];
    for (int i = 0; i < 3; ++i) {
        const float l = std::sqrt(hand[i * 4] * hand[i * 4] + hand[i * 4 + 1] * hand[i * 4 + 1] + hand[i * 4 + 2] * hand[i * 4 + 2]);
        if (!(l > 1e-6f) || !std::isfinite(l)) return false;
        for (int k = 0; k < 3; ++k) u[i][k] = hand[i * 4 + k] / l;
    }
    for (int j = 0; j < 3; ++j) {
        for (int k = 0; k < 3; ++k) out[j * 4 + k] = (relR[j][0] * u[0][k] + relR[j][1] * u[1][k] + relR[j][2] * u[2][k]) * scale;
        out[j * 4 + 3] = 0.0f;
    }
    for (int k = 0; k < 3; ++k) out[12 + k] = hand[12 + k] + (relP[0] * u[0][k] + relP[1] * u[1][k] + relP[2] * u[2][k]) * perM;
    out[15] = 1.0f;
    return true;
}

// Which target is this rig's: the one nearest it within kMatchUnits; -1 none.
inline int Match(float rigX, float rigZ, const Target* t, int n) {
    int best = -1;
    float bestD = kMatchUnits * kMatchUnits;
    for (int i = 0; i < n; ++i) {
        const float dx = t[i].x - rigX, dz = t[i].z - rigZ, d = dx * dx + dz * dz;
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

// The head's turn-only frame (x right, y up, z back) -> model space (its right is -x, ahead is +z), turned by the yaw in model space
// (c = cos yaw, s = sin yaw) - skeletons.cpp ToModel.
inline void ToModel(const float v[3], float c, float s, float out[3]) {
    const float mx = -v[0], my = v[1], mz = -v[2];
    out[0] = mx * c + mz * s;
    out[1] = my;
    out[2] = -mx * s + mz * c;
}

// The model's units per metre of the sender's body: the head bone's height over its eye height (ControllerPoint's clamp).
inline float ModelUpm(float headY, float eyeM) { return std::clamp(headY / std::max(0.8f, eyeM), 2.0f, 8.0f); }

// Where a controller is, model space: the rig's head bone plus the sender's offset from its head.
inline void HandTarget(const float head[3], const float hand[3], float c, float s, float upm, float out[3]) {
    float m[3];
    ToModel(hand, c, s, m);
    for (int i = 0; i < 3; ++i) out[i] = head[i] + m[i] * upm;
}

inline void Rot(const float q[4], const float v[3], float out[3]) { poseshare::Rotate(q, v, out); }
inline float Len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
inline bool Unit3(float v[3]) {
    const float l = Len3(v);
    if (!(l > 1e-6f) || !std::isfinite(l)) return false;
    for (int i = 0; i < 3; ++i) v[i] /= l;
    return true;
}
inline void Cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

// Where the head looks and its top, model space, from the headset's turn in its own frame: the pitch held within maxPitchDeg.
inline bool LookFrame(const float q[4], float c, float s, float maxPitchDeg, float fwd[3], float up[3]) {
    const float f0[3] = {0.0f, 0.0f, -1.0f}, u0[3] = {0.0f, 1.0f, 0.0f};
    float fh[3], uh[3];
    Rot(q, f0, fh);
    Rot(q, u0, uh);
    ToModel(fh, c, s, fwd);
    ToModel(uh, c, s, up);
    if (!Unit3(fwd)) return false;
    const float lim = std::sin(maxPitchDeg * poseshare::kPi / 180.0f);
    if (std::fabs(fwd[1]) > lim) {   // too far up or down: the same heading, the pitch at the limit, the top turned along
        const float h = std::sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]);
        const float y = fwd[1] > 0.0f ? lim : -lim, k = std::sqrt(1.0f - lim * lim);
        float nf[3] = {h > 1e-6f ? fwd[0] / h * k : 0.0f, y, h > 1e-6f ? fwd[2] / h * k : k};
        if (!Unit3(nf)) return false;
        float side[3];
        Cross3(up, nf, side);   // the top square to the new look, the side kept
        if (!Unit3(side)) return false;
        Cross3(nf, side, up);
        for (int i = 0; i < 3; ++i) fwd[i] = nf[i];
    }
    // the top made square to the look
    const float d = up[0] * fwd[0] + up[1] * fwd[1] + up[2] * fwd[2];
    for (int i = 0; i < 3; ++i) up[i] -= d * fwd[i];
    return Unit3(up);
}

// The turn (row vectors, v' = v R, 3x3 row-major) taking the frame (f0, u0) onto (f1, u1): f0 R = f1, u0 R = u1 (each pair made square,
// right-handed). False for a degenerate frame.
inline bool FrameTurn(const float f0in[3], const float u0in[3], const float f1in[3], const float u1in[3], float R[9]) {
    float A[3][3], B[3][3];
    auto frame = [](const float f[3], const float u[3], float M[3][3]) {
        float a[3] = {f[0], f[1], f[2]};
        if (!Unit3(a)) return false;
        float b[3] = {u[0], u[1], u[2]};
        const float d = b[0] * a[0] + b[1] * a[1] + b[2] * a[2];
        for (int i = 0; i < 3; ++i) b[i] -= d * a[i];
        if (!Unit3(b)) return false;
        float c[3];
        Cross3(a, b, c);
        for (int i = 0; i < 3; ++i) { M[0][i] = a[i]; M[1][i] = b[i]; M[2][i] = c[i]; }
        return true;
    };
    if (!frame(f0in, u0in, A) || !frame(f1in, u1in, B)) return false;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) R[i * 3 + j] = A[0][i] * B[0][j] + A[1][i] * B[1][j] + A[2][i] * B[2][j];   // A^T B
    return true;
}

// The weapon's attach bone in the model: its origin (from the rig's head bone, as the hands) and its three rows as unit directions
// (scale them by the bone's own row lengths), the mirrored z put back.
// A bone sent as drawn (its origin from the head, metres, the head's frame; the turn of its rows, mirrored or not) in the model: the origin from `head`, the
// rows as unit directions.
inline void BoneInModel(const float off[3], const float q[4], bool mirror, const float head[3], float c, float s, float upm, float origin[3], float rows[3][3]) {
    HandTarget(head, off, c, s, upm, origin);
    const float e[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    for (int i = 0; i < 3; ++i) {
        float r[3];
        Rot(q, e[i], r);
        if (i == 2 && mirror) for (float& v : r) v = -v;
        ToModel(r, c, s, rows[i]);
    }
}
inline void AttachInModel(const Target& t, int slot, const float head[3], float c, float s, float upm, float origin[3], float rows[3][3]) {
    BoneInModel(t.attachPos[slot], t.attachQ[slot], t.attachMirror[slot], head, c, s, upm, origin, rows);
}

// The sender's side of it (skeletons.cpp PoseBody, the bones as drawn): model space -> the head's turn-only frame (ToModel undone) - a direction, and a point
// from the eye in metres (upm the model's units a metre).
inline void DirToHead(const float d[3], float c, float s, float out[3]) {
    const float mx = d[0] * c - d[2] * s, mz = d[0] * s + d[2] * c;
    out[0] = -mx;
    out[1] = d[1];
    out[2] = -mz;
}
inline void PointToHead(const float p[3], const float eye[3], float c, float s, float upm, float out[3]) {
    const float k = upm > 1e-6f ? 1.0f / upm : 0.0f;
    const float d[3] = {(p[0] - eye[0]) * k, (p[1] - eye[1]) * k, (p[2] - eye[2]) * k};
    DirToHead(d, c, s, out);
}

// [format 6] Where the sender's eye is on THIS rig: its head bone (as posed here) less the sender's head bone from its eye, in the sender's units - so every
// offset from the eye lands where it is from the sender's head bone.
inline void EyeOnRig(const float headBone[3], const Target& t, float c, float s, float upm, float eye[3]) {
    float m[3];
    ToModel(t.headBone, c, s, m);
    for (int i = 0; i < 3; ++i) eye[i] = headBone[i] - m[i] * upm;
}

// [multiplayer] pose_share_legs: the hero's travel over the ground (the look-at's units a second, as his rig moves) in his model space, the model's units a second:
// into the frame of his head's turn by the pose's heading (x right, z back), into the model by his yaw there (ToModel), from world units to the model's by the
// two units per metre. y is 0.
inline void TravelInModel(float vxWorld, float vzWorld, float heading, float c, float s, float modelUpm, float worldUpm, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (!(worldUpm > 1e-4f) || !std::isfinite(vxWorld + vzWorld + heading + modelUpm)) return;
    float r[3], a[3];
    poseshare::Axes(heading, r, a);
    const float head[3] = {vxWorld * r[0] + vzWorld * r[2], 0.0f, -(vxWorld * a[0] + vzWorld * a[2])};
    ToModel(head, c, s, out);
    for (int i = 0; i < 3; ++i) out[i] *= modelUpm / worldUpm;
}

// The sender's look (the pose's world heading: ahead is (-sin h, 0, -cos h)) in THIS rig's model space as the game has it now (its model -> world matrix, row
// vectors: the rows are the model's axes in the world, scaled): the yaw PoseBody's way (atan2(x, z), the model's forward +z). The 2026-10-26 test: turning by
// the sender's own yaw in his model made the other hero "swing round to face back, then come forward" whenever he walked back - each PC's game turns its copy
// of the hero to the path at its own moment, so his yaw was against a facing ours did not have yet. False: a matrix that turns nothing.
inline bool ModelYawOfHeading(const float m[16], float heading, float* yaw) {
    if (!m || !yaw || !std::isfinite(heading)) return false;
    const float ax = -std::sin(heading), az = -std::cos(heading);
    const float r0 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2], r2 = m[8] * m[8] + m[9] * m[9] + m[10] * m[10];
    if (!(r0 > 1e-10f) || !(r2 > 1e-10f) || !std::isfinite(r0 + r2)) return false;
    const float vx = (ax * m[0] + az * m[2]) / r0, vz = (ax * m[8] + az * m[10]) / r2;   // (y's share of the look left out: a yaw)
    if (!(vx * vx + vz * vz > 1e-12f)) return false;
    *yaw = std::atan2(vx, vz);
    return std::isfinite(*yaw);
}

// [multiplayer] pose_share_legs, a hero with no pose: his facing as the game turns him, followed at a bounded rate (a unit ground direction x, z toward the target,
// at most maxRad of turn) - the game turns a hero round to his path at once when he walks back; his drawn body comes round over a moment instead of flipping.
// A first call (x and z 0) takes the target as it is.
inline void TurnToward(float* x, float* z, float tx, float tz, float maxRad) {
    const float tl = std::sqrt(tx * tx + tz * tz);
    if (!(tl > 1e-6f) || !std::isfinite(tl)) return;
    tx /= tl; tz /= tl;
    if (*x == 0.0f && *z == 0.0f) { *x = tx; *z = tz; return; }
    const float now = std::atan2(*x, *z), want = std::atan2(tx, tz);
    float d = want - now;
    while (d > poseshare::kPi) d -= 2.0f * poseshare::kPi;
    while (d < -poseshare::kPi) d += 2.0f * poseshare::kPi;
    const float step = std::clamp(d, -std::max(0.0f, maxRad), std::max(0.0f, maxRad));
    *x = std::sin(now + step);
    *z = std::cos(now + step);
}
// A world ground direction (x, z) in a rig's model as the game has it (its matrix's rows), as a yaw PoseBody's way - ModelYawOfHeading for a direction.
inline bool ModelYawOfDir(const float m[16], float dx, float dz, float* yaw) {
    if (!m || !yaw) return false;
    const float r0 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2], r2 = m[8] * m[8] + m[9] * m[9] + m[10] * m[10];
    if (!(r0 > 1e-10f) || !(r2 > 1e-10f) || !std::isfinite(r0 + r2 + dx + dz)) return false;
    const float vx = (dx * m[0] + dz * m[2]) / r0, vz = (dx * m[8] + dz * m[10]) / r2;
    if (!(vx * vx + vz * vz > 1e-12f)) return false;
    *yaw = std::atan2(vx, vz);
    return true;
}

// [multiplayer] pose_share_legs: the gait library's way of the steps (D2rfState dirX / dirZ) kept in the WORLD between steps, not in the rig's model. The 2026-10-27
// report: "walking backwards his legs cross over each other". Walking back, the game turns the hero's model round to face his path at once - his model space turns
// half a turn under the library's memory of the steps' way, which it then turns back toward the travel at its bounded rate (6 rad/s: half a second), sweeping
// through sideways: a foot stepped up to half a stride across to the other's side. The way handed back is carried through the rig's matrix: a ground direction of
// the model (unit, x z) into the world and back by the matrix as it is now (its rows; a uniform scale). False: a matrix that turns nothing.
inline bool DirModelToWorld(const float m[16], float dx, float dz, float* wx, float* wz) {
    if (!m || !wx || !wz || !std::isfinite(dx + dz)) return false;
    const float x = dx * m[0] + dz * m[8], z = dx * m[2] + dz * m[10];
    const float l = std::sqrt(x * x + z * z);
    if (!(l > 1e-8f) || !std::isfinite(l)) return false;
    *wx = x / l;
    *wz = z / l;
    return true;
}
inline bool DirWorldToModel(const float m[16], float wx, float wz, float* dx, float* dz) {
    float yaw = 0.0f;
    if (!dx || !dz || !ModelYawOfDir(m, wx, wz, &yaw)) return false;
    *dx = std::sin(yaw);
    *dz = std::cos(yaw);
    return true;
}

// ... and a REVERSAL: the travel more than kReverseDeg from the steps' way (he went forward, now back - or our smoothed travel, which lags the game's at once turn
// of his model, comes round). The library turns its way toward the travel at a bounded rate, so a reversal swept it half a turn through sideways, the feet striding
// across each other's place (the report). Instead the way is turned round at once and the stride's phase mirrored (phi -> pi - phi): a planted foot's share of
// the stride and a swinging one's are the same along the reversed way, so both feet stay where they are and from now step along the new way. (The caller checks
// that against the library: its feet for the state as it is and as turned, at dt 0, must be the same - skeletons.cpp RemoteLegs.) False: no reversal.
constexpr float kReverseDeg = 120.0f;
inline bool ReverseSteps(float* dirX, float* dirZ, float* phase, float tx, float tz) {
    if (!dirX || !dirZ || !phase || !std::isfinite(*dirX + *dirZ + *phase + tx + tz)) return false;
    const float tl = std::sqrt(tx * tx + tz * tz), dl = std::sqrt(*dirX * *dirX + *dirZ * *dirZ);
    if (!(tl > 1e-6f) || !(dl > 1e-6f)) return false;
    if ((*dirX * tx + *dirZ * tz) / (tl * dl) > std::cos(kReverseDeg * poseshare::kPi / 180.0f)) return false;
    *dirX = -*dirX;
    *dirZ = -*dirZ;
    float p = std::fmod(poseshare::kPi - *phase, 2.0f * poseshare::kPi);
    if (p < 0.0f) p += 2.0f * poseshare::kPi;
    *phase = p;
    return true;
}

// [multiplayer] pose_share_hero with no gait (the library or the legs not there): the look split between the whole body and the trunk over the hips - the trunk turns
// at most maxTwist over them (a waist), the rest of the look turns him whole. The trunk alone turned half a turn over the game's hips walking back (the game faces him
// down his path), his left leg hung from the right of his trunk: crossed. Radians, PoseBody's convention; body + upper = the look.
constexpr float kMaxTwistDeg = 60.0f;
inline void TurnSplit(float look, float maxTwist, float* body, float* upper) {
    const float a = poseshare::WrapPi(look);
    const float u = std::clamp(a, -std::max(0.0f, maxTwist), std::max(0.0f, maxTwist));
    if (upper) *upper = u;
    if (body) *body = a - u;
}

// [body] gait_cone_deg: the game's own walk where the hero goes about the way he faces, our gait elsewhere (the author: "within about 45 degrees forward
// or to the side the game's own animation, more than 45 ours" - "90 of the 360, 45 to one side and 45 to the other"). The share of our gait (0 the game's
// legs, 1 ours) for a travel at angleDeg from the facing (0 ahead, 180 behind) and a cone of +-coneDeg: out of the cone past coneDeg + kHystDeg, back in under
// coneDeg - kHystDeg (no flicker along the edge), the share following at kRate a second (a crossfade over about 0.2 s). Standing (too slow to tell a way):
// the game's own.
// A reversal (forward, then sharply back - the author's test: "the old animation still has its way") passes through no speed at all: the decision is
// held through kHoldStandS of it, so the game's walk never shows for those frames; into our gait faster (kRateOut) than back out (kRateIn).
struct ConeBlend {
    static constexpr float kHystDeg = 4.0f;
    static constexpr float kRateOut = 10.0f;    // into our gait: over 0.1 s
    static constexpr float kRateIn = 5.0f;      // back to the game's walk: over 0.2 s
    static constexpr float kHoldStandS = 0.35f; // too slow to tell a way this long: still the last way's decision
    float share = 0.0f;
    float still = 0.0f;   // seconds too slow to tell a way
    bool out = false;
    float Step(float angleDeg, bool moving, float coneDeg, float dt) {
        const float cone = std::clamp(std::isfinite(coneDeg) ? coneDeg : 45.0f, 0.0f, 180.0f);
        dt = std::clamp(std::isfinite(dt) ? dt : 0.0f, 0.0f, 0.25f);
        if (!moving || !std::isfinite(angleDeg)) {
            still += dt;
            if (still > kHoldStandS) out = false;   // standing: the game's own
        } else {
            still = 0.0f;
            out = angleDeg > cone + (out ? -kHystDeg : kHystDeg);
        }
        const float step = (out ? kRateOut : kRateIn) * dt;
        share = std::clamp(share + std::clamp((out ? 1.0f : 0.0f) - share, -step, step), 0.0f, 1.0f);
        return share;
    }
};
// The angle (degrees, 0 .. 180) between a travel on the ground and the facing (model space x, z), -1 when the travel has no length.
inline float TravelAngleDeg(float vx, float vz, float fx, float fz) {
    const float lv = std::sqrt(vx * vx + vz * vz), lf = std::sqrt(fx * fx + fz * fz);
    if (!(lv > 1e-6f) || !(lf > 1e-6f) || !std::isfinite(lv + lf)) return -1.0f;
    const float d = std::clamp((vx * fx + vz * fz) / (lv * lf), -1.0f, 1.0f);
    return std::acos(d) * 180.0f / poseshare::kPi;
}

}  // namespace remotepose
