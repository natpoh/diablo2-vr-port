// head_hang.h - the Necromancer's shrunken head hanging from its cord under the hand. Pure math: no game calls, no globals.
//
// The heads in the shield slot (Preserved, Zombie, Unraveller, Gargoyle and Demon Head, their exceptional and elite versions - items.json gives
// all fifteen codes one of five models, data/hd/items/armor/voodoo_head/<name>/) are skeletons of their own: `cog` under root/translate/anim,
// the head and its strings, tassels or feathers below it. The game moves them with the hero's arm and with animations of their own made for
// that arm (<anim>_<name>_head.animation); with the arm on a controller (F4) the head keeps its pose against the hand - it sticks out at the
// hand's angle, stiff. Here ([shield] heads_hang, skeletons.cpp PoseHead) the hero's own head is turned, as a whole, about the point the game
// hangs it from - the model's origin, where the cord's knot or handle sits in the hand - so that it hangs under gravity and swings: the hand's
// motion drags the cord's top, the head lags, swings out and settles, and turning the hand about the cord twists the head after it. Only the
// head's bone matrices; the game's item, its block and its stats are not touched.
//
// Where it hangs from and how long the cord is, from the model itself. The five models are authored hanging: the head below the origin along
// the model's -Y, what is above it light (the zombie head's loop, the gargoyle's and unraveller's tassels, the demon head's loop top). Each mesh
// carries its bounding box (granny_mesh.h, read off the game's model in memory); the boxes' parts below the origin, joined, are the hanging
// part - its centre is the head's weight, its direction from the origin the line the head hangs along, its distance the pendulum's length
// (offline, in the model's units: preserved 0.59, zombie 0.81, demon 0.69, gargoyle 0.90, unraveller 1.07 - 0.2 .. 0.35 m on the hero).
// The skeleton the game poses a head by has no meshes of its own (a .skeleton file; the meshes are the .model's), so in the game the hang comes
// from the skeleton's bones instead (HangOfBones: the bones below the origin span the hanging part) - a head always hangs.
//
// The swing is flail_chain.h's: a chain of one link (flailchain::Sim with two particles - the anchor on the hand, the weight), position-based,
// 240 Hz substeps, the cord no longer than it is (it may go slack), damped against the hand. The twist is a torsion spring on top: what the
// hand turns about the cord line winds the cord, the head follows that wind with its own inertia (about `twistHz`, a little under-damped:
// it overshoots a little and settles). The head's frame is carried along as the cord swings by the least turn of the cord's line (it never
// flips as the hand turns over: the frame is the head's own, not one rebuilt from the hand's each frame).
//
// Matrices are row-major 4x4 in the row-vector convention (p' = p * M, translation in m[12..14]) - the layout of the game's bone matrices.
#pragma once

#include "flail_chain.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace headhang {

using flailchain::Cross;
using flailchain::Dot;
using flailchain::Finite;
using flailchain::Length;
using flailchain::Mat;
using flailchain::Unit;
using flailchain::V3;

constexpr float kPi = 3.14159265358979f;

// A Necromancer's head model (the skeleton's file path): under items/armor/voodoo_head/. The one lying on the ground has the same model; it is
// not the hero's own, so the hook leaves it (its entity hangs off no hero).
inline bool IsHeadModel(std::string_view path) { return flailchain::ContainsPath(path, "/items/armor/voodoo_head/"); }

// ---- where it hangs from ----------------------------------------------------------------------------------------------------------

struct Box { V3 lo, hi; };
struct Hang {
    V3 dir{0.0f, -1.0f, 0.0f};   // from the origin to the hanging part's centre, unit, the model's space
    float length = 0.0f;         // the model's units
    bool ok = false;
};
// The hanging part of a model from its meshes' boxes (the model's bind space): each box's part below the origin (y <= 0, the model's up being
// +Y as the heads are authored), joined; its centre. A box wholly above the origin (a loop, a tassel) is no part of it. None below: not ok.
inline Hang HangOf(const Box* boxes, int count) {
    V3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (int i = 0; boxes && i < count; ++i) {
        Box b = boxes[i];
        if (!Finite(b.lo) || !Finite(b.hi) || b.lo.x > b.hi.x || b.lo.y > b.hi.y || b.lo.z > b.hi.z) continue;
        if (!(b.lo.y < 0.0f)) continue;
        b.hi.y = std::min(b.hi.y, 0.0f);
        lo = {std::min(lo.x, b.lo.x), std::min(lo.y, b.lo.y), std::min(lo.z, b.lo.z)};
        hi = {std::max(hi.x, b.hi.x), std::max(hi.y, b.hi.y), std::max(hi.z, b.hi.z)};
        any = true;
    }
    if (!any) return {};
    const V3 centre = (lo + hi) * 0.5f;
    const float l = Length(centre);
    if (!(l > 1e-3f) || !(l < 1e4f)) return {};
    Hang h;
    h.dir = centre * (1.0f / l);
    h.length = l;
    h.ok = true;
    return h;
}

// The hanging part from the skeleton's own bones (their bind places, the model's space) when the meshes' boxes cannot be read - and in the
// game they cannot: the head's skeleton comes from a .skeleton file of its own whose Granny model has no meshes; the meshes are the .model
// file's, drawn by another instance. The bones at or below the origin (the head's, its strings', its lower tassels', the root's at the origin
// itself) span the hanging part; the centre of their box is its weight. Offline, against the meshes: preserved 0.36 (meshes 0.59), zombie 0.72
// (0.81), demon 0.62 (0.69), gargoyle 0.83 (0.90), unraveller 0.88 (1.07) - the bones end where the meshes still go on a little. Never not ok:
// with no bone below the origin it hangs along the model's -Y, half as far as its farthest bone (one unit with none).
inline Hang HangOfBones(const V3* places, int count) {
    V3 lo{0.0f, 0.0f, 0.0f}, hi{0.0f, 0.0f, 0.0f};   // the origin, the cord's top, is always in it
    float farthest = 0.0f;
    for (int i = 0; places && i < count; ++i) {
        const V3 p = places[i];
        if (!Finite(p) || !(Length(p) < 1e4f)) continue;
        farthest = std::max(farthest, Length(p));
        if (p.y > 1e-4f) continue;
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const V3 centre = (lo + hi) * 0.5f;
    const float l = Length(centre);
    Hang h;
    h.ok = true;
    if (lo.y < -1e-3f && l > 1e-3f) {
        h.dir = centre * (1.0f / l);
        h.length = l;
    } else {
        h.dir = {0.0f, -1.0f, 0.0f};
        h.length = farthest > 1e-3f ? farthest * 0.5f : 1.0f;
    }
    return h;
}

// ---- small 3x3 pieces: a frame as three unit rows ---------------------------------------------------------------------------------

// v through the frame (row-vector: v.x * r[0] + v.y * r[1] + v.z * r[2])
inline V3 Through(const V3* r, V3 v) { return r[0] * v.x + r[1] * v.y + r[2] * v.z; }
inline float Det(const V3* r) { return Dot(r[0], Cross(r[1], r[2])); }
// The 3x3 part of a matrix as three unit rows; false when a row is no direction or the rows are far from square to each other (a skew).
inline bool Rows(const Mat& m, V3* out) {
    for (int i = 0; i < 3; ++i) {
        const V3 r = flailchain::Row(m, i);
        const float l = Length(r);
        if (!(l > 1e-6f) || !Finite(r)) return false;
        out[i] = r * (1.0f / l);
    }
    return std::fabs(Dot(out[0], out[1])) < 0.05f && std::fabs(Dot(out[1], out[2])) < 0.05f && std::fabs(Dot(out[0], out[2])) < 0.05f;
}
// v turned about the unit axis k by a radians (right-handed)
inline V3 TurnAbout(V3 k, float a, V3 v) {
    const float c = std::cos(a), s = std::sin(a);
    return v * c + Cross(k, v) * s + k * (Dot(k, v) * (1.0f - c));
}
// Rows made square again, each unit, the handedness kept.
inline void Square(V3* r) {
    const float hand = Det(r) < 0.0f ? -1.0f : 1.0f;
    r[0] = Unit(r[0]);
    r[1] = Unit(r[1] - r[0] * Dot(r[0], r[1]));
    r[2] = Cross(r[0], r[1]) * hand;
}
// The turn of a frame from `was` to `now` (both unit rows, the same handedness) as a rotation vector: its axis times its angle, radians.
inline V3 TurnBetween(const V3* was, const V3* now) {
    float R[3][3];   // was^T * now: v * R takes v through `was`'s rows to `now`'s
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) {
            float s = 0.0f;
            for (int i = 0; i < 3; ++i) {
                const float pa = a == 0 ? was[i].x : a == 1 ? was[i].y : was[i].z;
                const float nb = b == 0 ? now[i].x : b == 1 ? now[i].y : now[i].z;
                s += pa * nb;
            }
            R[a][b] = s;
        }
    // the rotation as a column matrix is R's transpose: its axis * 2 sin(angle) = (R12 - R21, R20 - R02, R01 - R10)
    const V3 w{R[1][2] - R[2][1], R[2][0] - R[0][2], R[0][1] - R[1][0]};
    const float s2 = Length(w), c = std::clamp((R[0][0] + R[1][1] + R[2][2] - 1.0f) * 0.5f, -1.0f, 1.0f);
    if (!(s2 > 1e-9f)) return {};
    const float angle = std::atan2(s2 * 0.5f, c);
    return w * (angle / s2);
}

// ---- the head on its cord ---------------------------------------------------------------------------------------------------------

struct Params {
    float damping = 1.5f;        // 1/s on the head's speed against the hand: a swing's reach halves in about 0.9 s, is gone in about five
    float twistHz = 1.0f;        // the cord's torsion: the head turns back after the hand's twist about this many times a second
    float twistDamping = 0.3f;   // of critical: a little under, so it overshoots a little and settles
    float maxStep = 1.0f / 240.0f;
    int maxSubsteps = 24;
};

struct State {
    flailchain::Sim sim;    // [0] the cord's top on the hand, [1] the head's weight
    bool live = false;
    V3 head[3];             // the carrier bone's axes as the head hangs (unit rows, the simulation's frame)
    V3 hand[3];             // the carrier bone's axes as the hand carries it, last frame
    float wind = 0.0f;      // radians the hand has turned the cord's top about the cord that the head has not followed yet
    float spin = 0.0f;      // the head's turn about the cord, radians per second
    float length = 0.0f;    // the cord's length, the simulation's units

    // At rest, hanging straight down from `anchor` (the hand's place of the cord's top): the head as the hand carries it (`hand`, unit rows)
    // turned by the least turn that takes its hanging line (`down`, the carrier's own axes) onto gravity's.
    void Start(const V3* handRows, V3 down, V3 anchor, float cord, V3 gravity) {
        const V3 g = Unit(gravity), c = Unit(Through(handRows, down));
        for (int i = 0; i < 3; ++i) { hand[i] = handRows[i]; head[i] = flailchain::TurnLeast(c, g, handRows[i]); }
        Square(head);
        length = std::max(cord, 1e-4f);
        const V3 places[2] = {anchor, anchor + g * length};
        sim.Start(places, 2);
        wind = spin = 0.0f;
        live = sim.live && Finite(head[0]) && Finite(head[1]) && Finite(head[2]);
    }
    V3 Line() const { return sim.n >= 2 ? Unit(sim.x[1] - sim.x[0]) : V3{0.0f, -1.0f, 0.0f}; }

    // One frame: the cord's top goes to `anchor` over dt seconds, the hand's frame is now `handRows`; gravity g (the frame's units / s^2).
    void Step(const V3* handRows, V3 down, V3 anchor, float dt, V3 g, const Params& p) {
        if (!live || !(dt > 0.0f) || !Finite(anchor) || !Finite(g)) return;
        flailchain::Params fp;
        fp.damping = std::max(0.0f, p.damping);
        fp.maxStep = p.maxStep;
        fp.maxSubsteps = p.maxSubsteps;
        sim.SetLengths(&length);
        sim.Step(sim.x[0], anchor, dt, g, fp);
        if (!sim.live) { live = false; return; }
        // the head's frame carried along with the cord's line (its least turn: no twist of its own)
        const V3 was = Unit(Through(head, down)), line = Line();
        const V3 now = Length(line) > 0.5f ? line : was;
        for (int i = 0; i < 3; ++i) head[i] = flailchain::TurnLeast(was, now, head[i]);
        // what the hand turned about the cord winds it; the head follows the wind (a damped torsion spring), substep by substep
        wind = std::clamp(wind + Dot(TurnBetween(hand, handRows), now), -2.0f * kPi, 2.0f * kPi);
        const int sub = std::clamp(static_cast<int>(std::ceil(dt / std::max(1e-4f, p.maxStep))), 1, std::max(1, p.maxSubsteps));
        const float h = dt / static_cast<float>(sub);
        const float w0 = 2.0f * kPi * std::max(0.0f, p.twistHz), k = w0 * w0, c = 2.0f * std::max(0.0f, p.twistDamping) * w0;
        float turned = 0.0f;
        for (int s = 0; s < sub; ++s) {
            spin += (k * wind - c * spin) * h;
            const float a = spin * h;
            wind -= a;
            turned += a;
        }
        for (int i = 0; i < 3; ++i) head[i] = TurnAbout(now, turned, head[i]);
        Square(head);
        {   // what rounding left: the hanging line exactly on the cord
            const V3 off = Unit(Through(head, down));
            for (int i = 0; i < 3; ++i) head[i] = flailchain::TurnLeast(off, now, head[i]);
        }
        for (int i = 0; i < 3; ++i) hand[i] = handRows[i];
        if (!Finite(head[0]) || !Finite(head[1]) || !Finite(head[2]) || !std::isfinite(wind) || !std::isfinite(spin)) live = false;
    }

    // The head's swing energy per unit of its mass, against g (for the tests).
    float Energy(V3 g) const { return sim.Energy(g); }
};

// The turn (3x3, in a 4x4 with no translation) that takes the hand's frame onto the head's: v * hand * M = v * head.
inline Mat Turn(const V3* handRows, const V3* headRows) {
    Mat m = flailchain::Identity();
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) {
            float s = 0.0f;
            for (int i = 0; i < 3; ++i) {
                const float ha = a == 0 ? handRows[i].x : a == 1 ? handRows[i].y : handRows[i].z;
                const float hb = b == 0 ? headRows[i].x : b == 1 ? headRows[i].y : headRows[i].z;
                s += ha * hb;
            }
            m.m[a * 4 + b] = s;
        }
    return m;
}
inline Mat Translate(V3 t) { Mat m = flailchain::Identity(); flailchain::SetPosition(m, t); return m; }

// ---- one frame, as the pose hook runs it ------------------------------------------------------------------------------------------

using Done = flailchain::Done;

// What the hook knows of one head model, learned once: the bone whose frame the head hangs by (its `cog`: the head's own animations turn it,
// the hanging overrides that) and the hanging line in that bone's own axes, the cord's length in the model's units.
struct Rig {
    int carrier = -1;           // -1: the model's root frame
    V3 down{0.0f, -1.0f, 0.0f};  // the hanging line, unit, the carrier's axes
    float length = 0.0f;        // model units
    bool ok = false;
};
// The rig from the hang (the model's space) and the carrier's bind pose (its model-space matrix; null: the model's own frame).
inline Rig RigOf(const Hang& h, int carrier, const Mat* carrierBind) {
    Rig r;
    if (!h.ok) return r;
    V3 d = h.dir;
    if (carrier >= 0 && carrierBind) {
        Mat inv;
        if (!flailchain::Inverse(*carrierBind, &inv)) return r;
        d = Unit(flailchain::Direction(inv, d));
        r.carrier = carrier;
    }
    if (!(Length(d) > 0.5f)) return {};
    r.down = d;
    r.length = h.length;
    r.ok = true;
    return r;
}

// One frame. `pose`: the head's bone matrices as the game posed them (its model space), `count` of them. toSim / fromSim: the model's space to
// the simulation's frame (the room: gravity there is `gravity`) and back. The head is turned about the model's origin - the point the game
// hangs it from - so that its carrier bone hangs as the state has it; every bone of it, rigidly (pose[i] * W). A first frame, one after a pause
// (dt over 0.25 s), an anchor that jumped more than `jump` (the frame's units), or a hand frame of the other handedness starts it at rest,
// hanging straight down; two calls inside half a millisecond (the same frame posed twice) step nothing and pose it the same.
inline Done Drive(const Rig& rig, State& s, const Mat& toSim, const Mat& fromSim, float dt, V3 gravity, float jump, const Params& p, Mat* pose, int count) {
    if (!rig.ok || !pose || count <= 0 || (rig.carrier >= count)) return Done::Held;
    const Mat carried = rig.carrier >= 0 ? flailchain::Mul(pose[rig.carrier], toSim) : toSim;
    V3 hand[3];
    if (!Rows(carried, hand)) return Done::Held;
    const V3 anchor = flailchain::Position(toSim);
    const float scale = (Length(flailchain::Row(toSim, 0)) + Length(flailchain::Row(toSim, 1)) + Length(flailchain::Row(toSim, 2))) / 3.0f;
    Done done = Done::Held;
    if (!s.live || !(dt <= 0.25f) || !(Length(anchor - s.sim.x[0]) <= jump) || (Det(hand) < 0.0f) != (Det(s.hand) < 0.0f)) {
        s.Start(hand, rig.down, anchor, rig.length * scale, gravity);
        done = Done::Started;
    } else if (dt >= 0.0005f) {
        s.length = std::max(rig.length * scale, 1e-4f);   // (a scale change of the item carries it)
        s.Step(hand, rig.down, anchor, dt, gravity, p);
        done = Done::Stepped;
        if (!s.live) { s.Start(hand, rig.down, anchor, rig.length * scale, gravity); done = Done::Started; }
    }
    if (!s.live) return done;
    const Mat W = flailchain::Mul(flailchain::Mul(flailchain::Mul(toSim, Translate(anchor * -1.0f)), flailchain::Mul(Turn(hand, s.head), Translate(anchor))), fromSim);
    if (!flailchain::FiniteMat(W)) return done;
    for (int i = 0; i < count; ++i) pose[i] = flailchain::Mul(pose[i], W);
    return done;
}

}  // namespace headhang
