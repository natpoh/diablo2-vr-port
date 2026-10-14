// bow_rig.h - the bow's own rig drawn by the pull. Pure math: no game calls, no globals.
//
// A bow in the hero's hand is a skeleton of its own (bow_layouts.h says which bones are its limbs, string and arrow). The game
// animates its string and limbs when he shoots; with the hands driving the draw (skeletons.cpp, [bow] rig_pose) those bones are
// posed from the string hand instead: the string goes back to the hand, the limbs fold toward it, the arrow bones sit on the
// string, and with no pull it is the bind pose again. Nothing but the bow's bone matrices is touched; the shot, its aim, its
// arrow and its damage stay the game's.
//
// The construction - a limb tip is where a circle of the limb's length about its hinge meets a circle of the string's length
// about the nock, the hand pulled back toward rest when it is out of reach - is the idea of bearstonem/d2r-pcvr, vr/bow_pose.h
// (MIT, https://github.com/bearstonem/d2r-pcvr), written again here and generalised from the one rig it knew to every bow's.
//
// Matrices are row-major 4x4 in the row-vector convention (p' = p * M, translation in m[12..14]) - the layout of the game's bone
// matrices, as skeletons.cpp has them.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "bow_layouts.h"

namespace bowrig {

struct V3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(V3 a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
// a / |a|; the zero vector when a has no direction
inline V3 Unit(V3 a) { const float l = Length(a); return l > 1e-9f ? a * (1.0f / l) : V3{}; }

struct Mat { float m[16] = {}; };
using Pose = std::array<Mat, kMaxBones>;

inline Mat Identity() { Mat r; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }
// a, then b
inline Mat Mul(const Mat& a, const Mat& b) {
    Mat r;
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k)
            for (int j = 0; j < 4; ++j) r.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
    return r;
}
inline V3 Position(const Mat& a) { return {a.m[12], a.m[13], a.m[14]}; }
inline void SetPosition(Mat& a, V3 p) { a.m[12] = p.x; a.m[13] = p.y; a.m[14] = p.z; }
// v through the 3x3 part only (a direction)
inline V3 Direction(const Mat& a, V3 v) {
    return {v.x * a.m[0] + v.y * a.m[4] + v.z * a.m[8], v.x * a.m[1] + v.y * a.m[5] + v.z * a.m[9], v.x * a.m[2] + v.y * a.m[6] + v.z * a.m[10]};
}
inline V3 Point(const Mat& a, V3 v) { return Direction(a, v) + Position(a); }
// Finite, of a sane size, and an affine matrix (last column 0 0 0 1).
inline bool Affine(const Mat& a) {
    for (const float x : a.m) if (!std::isfinite(x) || std::fabs(x) > 10000.0f) return false;
    return std::fabs(a.m[3]) + std::fabs(a.m[7]) + std::fabs(a.m[11]) < 0.001f && std::fabs(a.m[15] - 1.0f) < 0.001f;
}
inline bool Inverse(const Mat& a, Mat* out) {
    if (!out || !Affine(a)) return false;
    const float* m = a.m;
    const float c0 = m[5] * m[10] - m[6] * m[9], c1 = m[6] * m[8] - m[4] * m[10], c2 = m[4] * m[9] - m[5] * m[8];
    const float det = m[0] * c0 + m[1] * c1 + m[2] * c2;
    // singular relative to its own scale (a bow in other units has a small determinant and is fine)
    const float l0 = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]), l1 = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]),
                l2 = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    if (!(l0 > 0.0f) || !(l1 > 0.0f) || !(l2 > 0.0f) || !(std::fabs(det) > 1e-6f * l0 * l1 * l2)) return false;
    Mat r = Identity();
    r.m[0] = c0 / det;  r.m[1] = (m[2] * m[9] - m[1] * m[10]) / det; r.m[2] = (m[1] * m[6] - m[2] * m[5]) / det;
    r.m[4] = c1 / det;  r.m[5] = (m[0] * m[10] - m[2] * m[8]) / det; r.m[6] = (m[2] * m[4] - m[0] * m[6]) / det;
    r.m[8] = c2 / det;  r.m[9] = (m[1] * m[8] - m[0] * m[9]) / det;  r.m[10] = (m[0] * m[5] - m[1] * m[4]) / det;
    SetPosition(r, Direction(r, Position(a)) * -1.0f);
    if (!Affine(r)) return false;
    *out = r;
    return true;
}

// The row-vector turn taking direction `from` onto direction `to` (about their common normal).
inline Mat Rotation(V3 from, V3 to) {
    from = Unit(from);
    to = Unit(to);
    const V3 axis = Cross(from, to);
    const float s = Length(axis), c = std::clamp(Dot(from, to), -1.0f, 1.0f);
    Mat r = Identity();
    if (s < 1e-6f) {
        if (c > 0.0f) return r;
        // opposite: a half turn about any axis across them, 2 p p^T - I
        const V3 p = Unit(Cross(from, std::fabs(from.x) < 0.9f ? V3{1, 0, 0} : V3{0, 1, 0}));
        const float v[3] = {p.x, p.y, p.z};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = 2.0f * v[i] * v[j] - (i == j ? 1.0f : 0.0f);
        return r;
    }
    const V3 k = axis * (1.0f / s);
    const float kv[3] = {k.x, k.y, k.z}, t = 1.0f - c;
    const float cross[3][3] = {{0, -k.z, k.y}, {k.z, 0, -k.x}, {-k.y, k.x, 0}};   // [k]x, the column-vector form
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.m[i * 4 + j] = (i == j ? c : 0.0f) + s * cross[j][i] + t * kv[i] * kv[j];   // transposed: rows
    return r;
}

// bone `a` turned by `rotation` about its own origin, then put at `position`
inline Mat Turn(Mat a, const Mat& rotation, V3 position) {
    SetPosition(a, V3{});
    a = Mul(a, rotation);
    SetPosition(a, position);
    return a;
}

// Where a limb's tip goes with the nock at `nock`: the point a limb's length from its hinge and the string's length from the
// nock, on the side the tip was at rest (so each half folds inward and never crosses the other). False when the two circles do
// not meet.
inline bool LimbTip(V3 hinge, V3 restTip, V3 restNock, V3 nock, V3* tip) {
    const float limb = Length(restTip - hinge), string = Length(restNock - restTip), d = Length(nock - hinge);
    if (!(limb > 1e-4f) || !(string > 1e-4f) || !(d > 1e-4f) || d >= limb + string || d <= std::fabs(limb - string)) return false;
    const V3 along = (nock - hinge) * (1.0f / d);
    const float a = (limb * limb - string * string + d * d) / (2.0f * d);
    const V3 across = (restTip - hinge) - along * Dot(restTip - hinge, along);
    if (!(Length(across) > 1e-5f * limb)) return false;   // the rest tip on the hinge-nock line: no side to fold to
    *tip = hinge + along * a + Unit(across) * std::sqrt(std::max(0.0f, limb * limb - a * a));
    return std::isfinite(tip->x + tip->y + tip->z);
}

// The most the string is drawn, as a part of the tip-to-tip span of the bow. (The game's own draw animation of the hunter's bow
// pulls the nock 2.0 back from rest over a span of 4.34: 0.46.)
inline constexpr float kFullDraw = 0.46f;

// How the game hides the bow's arrow: the arrow bone's size, drawn at a thousandth. The arrow is part of the bow's own mesh,
// skinned to the arrow bone and the release helper below it (about half each), so the whole arrow group is scaled - as the
// game's own animation does at the release, when the arrow's size goes to nothing for a quarter of a second.
inline constexpr float kArrowHidden = 0.001f;

struct Rig {
    Resolved r;           // which bone is what
    Pose rest{};          // the bind pose in the cog's frame: rest[i] * (the cog's matrix now) = the bone at rest
    V3 restNock{};        // the nock at rest, cog frame
    V3 back{};            // unit, cog frame: from the grip toward the archer, across the limbs' axis
    V3 up{};              // unit, cog frame: along the limbs, from the lower tip to the upper
    float span = 0.0f;    // tip to tip
    float reach = 0.0f;   // from the grip to the nock at rest, along `back`
    bool valid = false;

    // From the bind pose (each bone's world matrix, model space of the bow) and the layout it was matched to. False, and the rig
    // unusable, when it is not a bow's bind pose: a bone without an inverse, no string behind the grip, a limb the string
    // cannot fold. *why says which.
    bool Learn(const Resolved& resolved, const Mat* bind, int count, const char** why = nullptr) {
        *this = Rig{};
        auto fail = [&](const char* w) { if (why) *why = w; return false; };
        if (!resolved.layout || !bind || count != resolved.count || count > kMaxBones) return fail("no layout");
        Mat inverseCog;
        if (!Inverse(bind[resolved.cog], &inverseCog)) return fail("the cog has no inverse");
        Rig n;
        n.r = resolved;
        for (int i = 0; i < count; ++i) {
            if (resolved.role[i] != kNone) {
                Mat inverse;
                if (!Inverse(bind[i], &inverse)) return fail("a bone without an inverse");   // a degenerate bone is not a rig
            }
            n.rest[i] = Mul(bind[i], inverseCog);
        }
        const Resolved& q = n.r;
        n.restNock = Position(n.rest[q.nock]);
        const V3 upper = Position(n.rest[q.tip[0]]), lower = Position(n.rest[q.tip[1]]);
        n.span = Length(upper - lower);
        if (!(n.span > 1e-3f) || !(n.span < 1e4f)) return fail("the limbs have no span");
        const V3 limbAxis = Unit(upper - lower);
        // The string rests behind the grip, across the limbs' axis; the grip is where the limbs meet.
        const V3 grip = (Position(n.rest[q.bend[0]]) + Position(n.rest[q.bend[1]])) * 0.5f;
        const V3 offset = n.restNock - grip;
        const V3 across = offset - limbAxis * Dot(offset, limbAxis);
        if (!(Length(across) > 0.01f * n.span)) return fail("no string behind the grip");
        n.back = Unit(across);
        n.up = limbAxis;
        n.reach = Length(across);
        // The layout knows which way the bow lies in its own frame; a bind pose that lies another way is not this layout's.
        const V3 forward = {q.layout->forward[0], q.layout->forward[1], q.layout->forward[2]}, upward = {q.layout->up[0], q.layout->up[1], q.layout->up[2]};
        if (Dot(n.back * -1.0f, forward) < 0.866f || Dot(n.up, upward) < 0.866f) return fail("the bow lies another way in its own frame than its layout says");
        // Refuse a rig the string cannot fold before any live pose is changed.
        for (int s = 0; s < 2; ++s) {
            V3 t;
            if (!LimbTip(Position(n.rest[q.bend[s]]), Position(n.rest[q.tip[s]]), n.restNock, n.restNock + n.back * (n.span * kFullDraw), &t))
                return fail("a limb the string cannot fold");
        }
        n.valid = true;
        *this = n;
        return true;
    }

    // The nock for a draw of `amount` 0..1 straight back from rest, cog frame.
    V3 NockAt(float amount) const { return restNock + back * (span * kFullDraw * std::clamp(amount, 0.0f, 1.0f)); }

    // The pose with the nock at `nock` and the arrow along `direction`, both in the cog's frame (as `rest` is). `cog`: the cog
    // bone's matrix now; the bones that have a role are written to `out`, in the bow's model space like the game's own bone
    // matrices, and no other. `arrowScale`: the arrow bones' size, 1 = as the bind pose has it (shown), kArrowHidden (hidden).
    // A nock out of reach is pulled toward rest until the limbs can meet the string; a hand that cannot be used at all leaves
    // `out` alone and returns false.
    bool DrawAt(const Mat& cog, V3 nock, V3 direction, float arrowScale, Pose* out) const {
        if (!valid || !out || !Finite(nock) || !Finite(direction) || !(Length(direction) > 0.01f) || !Affine(cog)) return false;
        if (!std::isfinite(arrowScale) || arrowScale < 0.0f) return false;
        V3 tips[2];
        bool reachable = false;
        for (int attempt = 0; attempt < 16 && !reachable; ++attempt) {
            reachable = LimbTip(Position(rest[r.bend[0]]), Position(rest[r.tip[0]]), restNock, nock, &tips[0]) &&
                        LimbTip(Position(rest[r.bend[1]]), Position(rest[r.tip[1]]), restNock, nock, &tips[1]);
            if (!reachable) nock = restNock + (nock - restNock) * 0.75f;
        }
        if (!reachable) return false;
        V3 hinge[2], oldTip[2];
        Mat bendTurn[2], stringTurn[2];
        for (int s = 0; s < 2; ++s) {
            hinge[s] = Position(rest[r.bend[s]]);
            oldTip[s] = Position(rest[r.tip[s]]);
            bendTurn[s] = Rotation(oldTip[s] - hinge[s], tips[s] - hinge[s]);       // the limb bends about its hinge
            stringTurn[s] = Rotation(restNock - oldTip[s], nock - tips[s]);         // the string's half follows the string
        }
        // The arrow lies on the string: rest points ahead (away from the archer), it follows `direction`.
        const Mat arrowTurn = Rotation(back * -1.0f, direction);
        return Build(cog, nock, arrowTurn, arrowTurn, nock, hinge, oldTip, tips, bendTurn, stringTurn, arrowScale, out);
    }

    // Whether the limbs can meet the string with the nock at `nock` (cog frame): DrawAt pulls a nock that is out of reach back toward rest
    // by a quarter at a time, so a nock moved by a little at the very end of the draw must not be asked to cross that edge.
    bool CanReach(V3 nock) const {
        if (!valid || !Finite(nock)) return false;
        V3 tip;
        return LimbTip(Position(rest[r.bend[0]]), Position(rest[r.tip[0]]), restNock, nock, &tip) &&
               LimbTip(Position(rest[r.bend[1]]), Position(rest[r.tip[1]]), restNock, nock, &tip);
    }

    // The same for a plain draw of `amount` 0..1, straight back and along the bow.
    bool Draw(const Mat& cog, float amount, float arrowScale, Pose* out) const {
        if (!std::isfinite(amount)) return false;
        return DrawAt(cog, NockAt(amount), back * -1.0f, arrowScale, out);
    }

    // The string just let go: everything at rest, the arrow hidden, and the arrow bones - the release helper among them, from
    // which the game's own arrow starts when its shot gets to the release frame, a moment after the hands let go - at the grip,
    // where the game's own animation puts them as the arrow is shot.
    bool Released(const Mat& cog, Pose* out) const {
        if (!valid || !out || !Affine(cog)) return false;
        const Mat none = Identity();
        V3 hinge[2], tip[2];
        Mat turn[2] = {none, none};
        for (int s = 0; s < 2; ++s) hinge[s] = Position(rest[r.bend[s]]), tip[s] = Position(rest[r.tip[s]]);
        return Build(cog, restNock, none, none, restNock - back * reach, hinge, tip, tip, turn, turn, kArrowHidden, out);
    }

private:
    // The bones with a role, each from its rest pose: `arrowAt` is where the nock's neighbours (the arrow) are put.
    bool Build(const Mat& cog, V3 nock, const Mat& nockTurn, const Mat& arrowTurn, V3 arrowAt, const V3* hinge, const V3* oldTip,
               const V3* newTip, const Mat* bendTurn, const Mat* stringTurn, float arrowScale, Pose* out) const {
        Pose pose{};
        for (int i = 0; i < r.count; ++i) {
            if (r.role[i] == kNone) continue;
            const int s = r.side[i] >= 0 ? r.side[i] : 0;
            const V3 at = Position(rest[i]);
            switch (r.role[i]) {
                case kBend:      pose[i] = Turn(rest[i], bendTurn[s], hinge[s]); break;
                case kLimb:      pose[i] = Turn(rest[i], bendTurn[s], hinge[s] + Direction(bendTurn[s], at - hinge[s])); break;
                case kTip:       pose[i] = Turn(rest[i], stringTurn[s], newTip[s]); break;
                case kTipFollow: pose[i] = Turn(rest[i], stringTurn[s], newTip[s] + Direction(stringTurn[s], at - oldTip[s])); break;
                case kNock:      pose[i] = Turn(rest[i], nockTurn, nock); break;
                case kNockHelper:pose[i] = Turn(rest[i], stringTurn[s], nock + Direction(nockTurn, at - restNock)); break;
                case kNockFollow:pose[i] = Turn(rest[i], nockTurn, nock + Direction(nockTurn, at - restNock)); break;
                case kArrow:
                    pose[i] = Turn(rest[i], arrowTurn, arrowAt + Direction(arrowTurn, at - restNock));
                    for (int row = 0; row < 3; ++row)
                        for (int col = 0; col < 3; ++col) pose[i].m[row * 4 + col] *= arrowScale;
                    break;
                default: break;
            }
        }
        // Only a complete finite pose is handed out; a bad rig leaves the game's own matrices intact.
        for (int i = 0; i < r.count; ++i) {
            if (r.role[i] == kNone) continue;
            pose[i] = Mul(pose[i], cog);
            if (!Affine(pose[i])) return false;
        }
        for (int i = 0; i < r.count; ++i)
            if (r.role[i] != kNone) (*out)[i] = pose[i];
        return true;
    }
};

// The hands are known in the hero's model space; the rig works in the bow's cog frame. A point of the bow's model space p is
// p * cog * itemLocal * attach in the hero's model space (cog: the cog bone now; itemLocal: the bow entity's own transform,
// relative to the hand's attach bone; attach: that bone in the hero's model space, as skeletons.cpp left it). This takes the
// string hand and the bow hand the other way, to the nock and the direction the arrow lies along.
inline bool ToRigFrame(const Mat& cog, const Mat& itemLocal, const Mat& attach, V3 stringHand, V3 bowHand, V3* nock, V3* direction) {
    Mat toHero, fromHero;
    if (!nock || !direction || !Finite(stringHand) || !Finite(bowHand)) return false;
    if (!Affine(cog) || !Affine(itemLocal) || !Affine(attach)) return false;
    toHero = Mul(cog, Mul(itemLocal, attach));
    if (!Inverse(toHero, &fromHero)) return false;
    *nock = Point(fromHero, stringHand);
    *direction = Direction(fromHero, bowHand - stringHand);
    return Finite(*nock) && Finite(*direction);
}

// (The arrow's rest point - where the arrow passes the bow hand, fixed in the bow hand's own frame - is bow_aim.h's: the arrow's line
// is from the string hand through that point, which is what ToRigFrame is given as the bow hand.)

// When the rig overrides the game's bow animation: while the string is drawn, and a moment after it is let go - the game starts
// its own draw when the shot is sent, and that wind-up would be played again after the hands already let the string go.
// `lastDrawn`: when the string was last seen drawn (ms on any clock that does not run backwards), 0 never.
inline constexpr uint64_t kHoldMs = 700;
inline bool Holding(uint64_t lastDrawn, uint64_t nowMs) { return lastDrawn != 0 && nowMs >= lastDrawn && nowMs - lastDrawn < kHoldMs; }

// A pull that came to nothing (too short to shoot) lets the string go back to rest over this long, eased, with the arrow
// still on it - not a snap, and no wind-up to hold off, since no attack was started.
inline constexpr uint64_t kCancelMs = 130;
// The draw detector's amount (0..1 of a 0.45 m pull past the nock) below which a pull shoots nothing: its charge needs the hands
// 0.28 m apart and 0.12 m more than at the nock, 0.27 of the amount. A feed that does not say the pull was cancelled is read
// this way when the string is let go.
inline constexpr float kShortPull = 0.25f;
inline bool ShortPull(float lastAmount) { return std::isfinite(lastAmount) && lastAmount < kShortPull; }
inline float EaseOut(uint64_t sinceMs) {
    const float t = std::clamp((float)sinceMs / (float)kCancelMs, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
// The nock and the way the arrow points sinceMs after the pull was let go, from where they were drawn to rest.
inline V3 EasedNock(V3 drawn, V3 rest, uint64_t sinceMs) { return drawn + (rest - drawn) * EaseOut(sinceMs); }
inline V3 EasedDirection(V3 drawn, V3 rest, uint64_t sinceMs) {
    const float s = EaseOut(sinceMs);
    const V3 mix = Unit(drawn) * (1.0f - s) + Unit(rest) * s;
    return Length(mix) > 1e-3f ? Unit(mix) : rest;
}

}  // namespace bowrig
