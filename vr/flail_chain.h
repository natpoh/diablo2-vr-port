// flail_chain.h - a flail's head swinging free on its chain. Pure math: no game calls, no globals.
//
// The game's flails are skeletons of their own (data/hd/items/weapon/mace/<model>/skeleton): the handle on `cog`, and the chain a line of
// bones hung off it - `chain_bnd_jnt_01..18` for the Flail, Knout and Scourge (models flail / flail_lh: each link's mesh rigid on its own bone,
// the spiked ball on the last), three chains `L_ / C_ / R_chain_root > _chain_01 > _bindJoint_02 > ...` under `mainChain` or `cog` for Khalim's
// Flail and Khalim's Will. The game moves those bones only by its own keyframed animations, made for the hero's arm as the game swings it; with the
// arm on a controller (F4) the chain keeps that pose against the handle whatever the hand does - the head stands stiff. Here ([weight]
// flail_chain, skeletons.cpp) the chain bones of the hero's own flail are posed from a small simulation instead: the chain's first bone stays
// where the handle has it, gravity pulls the rest down, the hand's motion swings it, damping calms it, and a hit by touch kicks the head back.
// Only those bones' matrices; the game's attack, its reach, its rolls and the handle are not touched.
//
// The simulation: position-based dynamics. A chain is a line of particles - the bones' places, two bones at one place (a chain's root and its
// first link) one particle - with the first pinned to the handle. The mass is the head's (the ball, or the chain's end piece): it moves under
// gravity and its own inertia, held no farther from the anchor than the chain is long - nearer it may come, the chain going slack. The links
// are a light chain between the two ends: on the straight line while it is taut, a few sweeps of their lengths while it is slack (they sag and
// bend, never stretch). Velocities are read off the positions. Small substeps (240 Hz) with the anchor moved along its way between two frames
// keep it stable at any frame rate. Units are whatever the caller's frame has: the lengths come from the model, gravity in the same units
// per second squared. (A first try laid the links out from the anchor at their lengths after mass-weighted sweeps: that pulled the heavy head
// in at every step, which pumps a swing like a child on a swing - the energy grew. The head's own constraint has no such pull.)
//
// Matrices are row-major 4x4 in the row-vector convention (p' = p * M, translation in m[12..14]) - the layout of the game's bone matrices.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace flailchain {

struct V3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(V3 a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline V3 Unit(V3 a) { const float l = Length(a); return l > 1e-12f ? a * (1.0f / l) : V3{}; }
inline V3 Lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }

struct Mat { float m[16] = {}; };
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
inline V3 Row(const Mat& a, int r) { return {a.m[r * 4], a.m[r * 4 + 1], a.m[r * 4 + 2]}; }
inline void SetRow(Mat& a, int r, V3 v) { a.m[r * 4] = v.x; a.m[r * 4 + 1] = v.y; a.m[r * 4 + 2] = v.z; }
// v through the 3x3 part only (a direction)
inline V3 Direction(const Mat& a, V3 v) {
    return {v.x * a.m[0] + v.y * a.m[4] + v.z * a.m[8], v.x * a.m[1] + v.y * a.m[5] + v.z * a.m[9], v.x * a.m[2] + v.y * a.m[6] + v.z * a.m[10]};
}
inline V3 Point(const Mat& a, V3 v) { return Direction(a, v) + Position(a); }
inline bool FiniteMat(const Mat& a) { for (const float x : a.m) if (!std::isfinite(x)) return false; return true; }
// The inverse of an affine matrix (last column 0 0 0 1, any scale); false when it is singular or not affine.
inline bool Inverse(const Mat& a, Mat* out) {
    if (!out || !FiniteMat(a) || std::fabs(a.m[3]) + std::fabs(a.m[7]) + std::fabs(a.m[11]) > 1e-3f || std::fabs(a.m[15] - 1.0f) > 1e-3f) return false;
    const float* m = a.m;
    const float c0 = m[5] * m[10] - m[6] * m[9], c1 = m[6] * m[8] - m[4] * m[10], c2 = m[4] * m[9] - m[5] * m[8];
    const float det = m[0] * c0 + m[1] * c1 + m[2] * c2;
    const float l0 = Length(Row(a, 0)), l1 = Length(Row(a, 1)), l2 = Length(Row(a, 2));
    if (!(l0 > 0.0f) || !(l1 > 0.0f) || !(l2 > 0.0f) || !(std::fabs(det) > 1e-6f * l0 * l1 * l2)) return false;
    Mat r = Identity();
    r.m[0] = c0 / det;  r.m[1] = (m[2] * m[9] - m[1] * m[10]) / det; r.m[2] = (m[1] * m[6] - m[2] * m[5]) / det;
    r.m[4] = c1 / det;  r.m[5] = (m[0] * m[10] - m[2] * m[8]) / det; r.m[6] = (m[2] * m[4] - m[0] * m[6]) / det;
    r.m[8] = c2 / det;  r.m[9] = (m[1] * m[8] - m[0] * m[9]) / det;  r.m[10] = (m[0] * m[5] - m[1] * m[4]) / det;
    SetPosition(r, Direction(r, Position(a)) * -1.0f);
    if (!FiniteMat(r)) return false;
    *out = r;
    return true;
}
// v turned by the least rotation that takes the unit direction `from` onto the unit direction `to` (half a turn about some axis square to
// `from` when they are opposite).
inline V3 TurnLeast(V3 from, V3 to, V3 v) {
    const float c = std::clamp(Dot(from, to), -1.0f, 1.0f);
    V3 k = Cross(from, to);
    float s = Length(k);
    if (s < 1e-6f) {
        if (c > 0.0f) return v;
        // opposite: half a turn about an axis square to `from`
        k = Unit(Cross(from, std::fabs(from.x) < 0.9f ? V3{1.0f, 0.0f, 0.0f} : V3{0.0f, 1.0f, 0.0f}));
        return k * (2.0f * Dot(k, v)) - v;
    }
    k = k * (1.0f / s);
    return v * c + Cross(k, v) * s + k * (Dot(k, v) * (1.0f - c));
}

constexpr int kMaxBones = 64;    // the skeletons it reads: the Flail's has 23, Khalim's Flail 51
constexpr int kMaxChains = 4;
constexpr int kMaxLinks = 24;    // bones of one chain
constexpr float kTaut = 0.995f;  // a chain this near its length between its ends is laid straight (its links 0.5 % short at most)

// ---- which skeletons ------------------------------------------------------------------------------------------------------------

inline char Lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
inline bool Contains(std::string_view hay, std::string_view needle) {
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        size_t j = 0;
        while (j < needle.size() && Lower(hay[i + j]) == Lower(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}
inline bool ContainsPath(std::string_view hay, std::string_view needle) {   // '/' in the needle matches '\' too
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        size_t j = 0;
        for (; j < needle.size(); ++j) {
            const char a = Lower(hay[i + j]), b = Lower(needle[j]);
            if (!(a == b || (b == '/' && a == '\\'))) break;
        }
        if (j == needle.size()) return true;
    }
    return false;
}
// A hero's flail model (the game's skeleton's file path): under items/weapon/mace/ and a flail's - flail, flail_lh, khalim_flail(_lh),
// super_khalim_flail(_lh). The monsters' flails (_monsters/) and the ones lying on the ground (dropped) are not.
inline bool IsFlailModel(std::string_view path) {
    return ContainsPath(path, "/items/weapon/mace/") && Contains(path, "flail") && !ContainsPath(path, "/_monsters/") && !Contains(path, "dropped");
}
// A bone of a chain by its name: chain_bnd_jnt_NN, X_chain_root / X_chain_NN / mainChain, X_bindJoint_NN.
inline bool ChainName(std::string_view n) { return Contains(n, "chain") || Contains(n, "bindjoint") || Contains(n, "bnd_jnt"); }

struct Chain { int count = 0; int bone[kMaxLinks] = {}; };
struct Layout { int chains = 0; Chain chain[kMaxChains]; };

// The chains of a skeleton: each a line of chain-named bones, each the only chain-named child of the one before; it starts at a chain-named bone
// whose parent is not one, or whose parent has more than one (Khalim's mainChain: its three chains start below it). Lines of fewer than three
// bones are not chains (mainChain itself). A chain that hangs off a bone of another chain is left out (that bone is moved by us). None: false,
// with why.
inline bool FindChains(const std::string_view* names, const int* parents, int count, Layout* out, const char** why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (!out || !names || !parents || count < 2 || count > kMaxBones) return no("not a skeleton of a size a flail has");
    *out = Layout{};
    bool chainy[kMaxBones] = {};
    int kids[kMaxBones] = {};      // chain-named children
    int kid[kMaxBones];            // the last of them
    for (int i = 0; i < count; ++i) { chainy[i] = ChainName(names[i]); kid[i] = -1; }
    for (int i = 0; i < count; ++i) {
        const int p = parents[i];
        if (p < -1 || p >= count || p == i) return no("a bone's parent is not one of the skeleton's");
        if (p >= 0 && chainy[i]) { ++kids[p]; kid[p] = i; }
    }
    int owner[kMaxBones];
    for (int i = 0; i < count; ++i) owner[i] = -1;
    Layout found;
    for (int i = 0; i < count && found.chains < kMaxChains; ++i) {
        if (!chainy[i]) continue;
        const int p = parents[i];
        if (p >= 0 && chainy[p] && kids[p] == 1) continue;   // the middle of a line
        Chain c;
        int b = i;
        while (b >= 0 && c.count < kMaxLinks) {
            c.bone[c.count++] = b;
            b = kids[b] == 1 ? kid[b] : -1;
        }
        if (c.count < 3) continue;
        for (int k = 0; k < c.count; ++k) owner[c.bone[k]] = found.chains;
        found.chain[found.chains++] = c;
    }
    // a chain that starts below a bone of another chain would be moved twice: left out
    Layout kept;
    for (int k = 0; k < found.chains; ++k) {
        const int p = parents[found.chain[k].bone[0]];
        if (p >= 0 && owner[p] >= 0 && owner[p] != k) continue;
        kept.chain[kept.chains++] = found.chain[k];
    }
    if (!kept.chains) return no("no chain of bones (chain_bnd_jnt / _chain_ / _bindJoint_) in it");
    *out = kept;
    if (why) *why = nullptr;
    return true;
}

// ---- one chain's rig: the bind pose against its parent, the particles ---------------------------------------------------------------

struct ChainRig {
    int bones = 0;
    int bone[kMaxLinks] = {};       // skeleton indices, in order from the handle
    int parent = -1;                // the bone the chain hangs off (not posed here: the game's pose of it stands)
    Mat local[kMaxLinks];           // each bone's bind pose in the frame of the one before (the first's in the parent's)
    int particles = 0;              // places along the chain; bones at one place share one
    int part[kMaxLinks] = {};       // bone -> its particle
    int first[kMaxLinks] = {};      // particle -> its first bone
    float bindLength = 0.0f;        // the chain's length in the bind pose, the model's units
};

// The rig of one chain from the skeleton's bind pose (each bone's model-space matrix). False, with why, when it cannot be one.
inline bool Learn(const Chain& c, const int* parents, const Mat* bind, int count, ChainRig* out, const char** why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (!out || c.count < 2 || c.count > kMaxLinks) return no("a chain of no bones");
    ChainRig r;
    r.bones = c.count;
    r.parent = parents[c.bone[0]];
    if (r.parent < 0 || r.parent >= count) return no("the chain hangs off nothing");
    for (int k = 0; k < c.count; ++k) {
        r.bone[k] = c.bone[k];
        const int up = k == 0 ? r.parent : c.bone[k - 1];
        if (k > 0 && parents[c.bone[k]] != up) return no("a chain bone's parent is not the bone before it");
        Mat inv;
        if (!Inverse(bind[up], &inv)) return no("a bind pose that has no inverse");
        r.local[k] = Mul(bind[c.bone[k]], inv);
        if (!FiniteMat(r.local[k])) return no("a bind pose that is not numbers");
    }
    // the particles: a bone at the place of the one before it (a chain's root and its first link) shares its particle
    float span = 0.0f;
    for (int k = 1; k < c.count; ++k) span += Length(Position(bind[c.bone[k]]) - Position(bind[c.bone[k - 1]]));
    if (!(span > 1e-6f)) return no("a chain of no length");
    r.part[0] = 0;
    r.first[0] = 0;
    r.particles = 1;
    for (int k = 1; k < c.count; ++k) {
        if (Length(Position(bind[c.bone[k]]) - Position(bind[c.bone[k - 1]])) > span * 1e-4f) { r.first[r.particles] = k; ++r.particles; }
        r.part[k] = r.particles - 1;
    }
    if (r.particles < 2) return no("a chain whose bones are all in one place");
    r.bindLength = span;
    *out = r;
    if (why) *why = nullptr;
    return true;
}

// The chain carried rigidly by its parent as the game posed it: the bind pose of every bone against the one before.
inline void Rigid(const ChainRig& r, const Mat& parentPose, Mat* out) {
    for (int k = 0; k < r.bones; ++k) out[k] = Mul(r.local[k], k == 0 ? parentPose : out[k - 1]);
}
// The particles' places in that pose.
inline void RigidPlaces(const ChainRig& r, const Mat* rigid, V3* out) {
    for (int p = 0; p < r.particles; ++p) out[p] = Position(rigid[r.first[p]]);
}

// The chain's bones from the particles' places (the model's space): each bone keeps the frame the rigid pose gives it, turned by the least
// rotation that takes its piece of the rigid chain onto the same piece of the simulated one, at its particle's place. A bone's piece is from
// its particle to the next; the last particle's (the head's) is the one before it. Writes out[k] for the chain's bones only.
inline void PoseBones(const ChainRig& r, const Mat* rigid, const V3* places, Mat* out) {
    V3 rest[kMaxLinks];
    RigidPlaces(r, rigid, rest);
    for (int k = 0; k < r.bones; ++k) {
        const int p = r.part[k];
        const int a = p + 1 < r.particles ? p : p - 1, b = a + 1;
        const V3 from = Unit(rest[b] - rest[a]), to = Unit(places[b] - places[a]);
        const bool turn = Length(from) > 0.5f && Length(to) > 0.5f;
        Mat m = rigid[k];
        if (turn) for (int row = 0; row < 3; ++row) SetRow(m, row, TurnLeast(from, to, Row(rigid[k], row)));
        const V3 off = Position(rigid[k]) - rest[p];   // 0 but for a bone that shares its particle with one before it
        SetPosition(m, places[p] + (turn ? TurnLeast(from, to, off) : off));
        out[k] = m;
    }
}

// ---- the simulation -------------------------------------------------------------------------------------------------------------

struct Params {
    float damping = 0.6f;       // 1/s: the head's velocity against the hand's decays this fast (the links rubbing) - a free swing halves in about 2 s
    float linkDamping = 4.0f;   // 1/s more on the light links: no wiggle of their own
    float swing = 1.0f;         // the share of the hand's motion the chain feels: 0 it is carried along stiffly and only hangs, 1 real, up to 2 more
    float hit = 1.0f;           // a hit's kick: 0 none, 1 the head bounces back off the target, up to 2 harder
    float restitution = 0.35f;  // of the head's speed against the hand, bounced back at a hit (hit 1)
    // [flail] ball_weight_pct / 100 (0.25..4, 1 = the model's own): the ball heavier than the model's. Gravity and the hand's pull move a heavy ball as
    // they move a light one (a pendulum's swing does not depend on its mass); what a mass changes are the forces that are not its weight - the links
    // rubbing and the air (damping: the same force slows a ball `weight` times heavier `weight` times less, so it swings on longer) and a hit (the
    // same blow turns it back `weight` times less: a heavy ball pushes through, a light one bounces). On the weight model it is the flail's mass
    // (WeaponMassFactor): the whole weapon lags and sags more.
    float weight = 1.0f;
    int iterations = 24;        // rounds at most that lay a slack chain's links between its two ends, per substep (a few, mostly)
    float maxStep = 1.0f / 240.0f;
    int maxSubsteps = 24;
};

struct Sim {
    int n = 0;                      // particles, [0] the anchor on the handle
    bool live = false;
    V3 x[kMaxLinks], v[kMaxLinks];
    float len[kMaxLinks] = {};      // len[i]: from x[i] to x[i + 1]
    V3 anchorVel;

    // At rest at these places (the chain as the game draws it): the lengths are theirs.
    void Start(const V3* places, int count) {
        n = std::clamp(count, 0, kMaxLinks);
        for (int i = 0; i < n; ++i) { x[i] = places[i]; v[i] = {}; }
        for (int i = 0; i + 1 < n; ++i) len[i] = Length(places[i + 1] - places[i]);
        anchorVel = {};
        live = n >= 2;
    }
    // The frame's origin moved by -by (the hook keeps it near the hero, so that the places stay small numbers): nothing else changes.
    void Shift(V3 by) { for (int i = 0; i < n; ++i) x[i] = x[i] + by; }
    // The lengths as the model has them now (a scale change of the item carries them).
    void SetLengths(const float* lengths) { for (int i = 0; i + 1 < n; ++i) len[i] = std::max(0.0f, lengths[i]); }
    float Total() const { float t = 0.0f; for (int i = 0; i + 1 < n; ++i) t += len[i]; return t; }

    // One frame: the anchor goes from `from` to `to` over dt seconds, gravity g (the frame's units / s^2).
    void Step(V3 from, V3 to, float dt, V3 g, const Params& p) {
        if (!live || !(dt > 0.0f) || !Finite(from) || !Finite(to) || !Finite(g)) return;
        const int sub = std::clamp(static_cast<int>(std::ceil(dt / std::max(1e-4f, p.maxStep))), 1, std::max(1, p.maxSubsteps));
        const float h = dt / static_cast<float>(sub);
        const float swing = std::clamp(p.swing, 0.0f, 2.0f);
        const float keepHead = std::exp(-std::max(0.0f, p.damping) / std::clamp(p.weight, 0.25f, 4.0f) * h);   // a heavier ball: the same rubbing slows it less
        const float keepLink = std::exp(-(std::max(0.0f, p.damping) + std::max(0.0f, p.linkDamping)) * h);
        const int head = n - 1;
        const float total = Total();
        V3 prev[kMaxLinks];
        for (int s = 1; s <= sub; ++s) {
            const V3 a0 = Lerp(from, to, static_cast<float>(s - 1) / sub), a1 = Lerp(from, to, static_cast<float>(s) / sub);
            const V3 carry = (a1 - a0) * (1.0f - swing);   // the part of the hand's motion the chain does not feel moves it stiffly
            const V3 felt = (a1 - a0) * (swing / h);       // the hand's velocity as the chain feels it: the damping is against it (the links rubbing), not the air's
            x[0] = a1;
            for (int i = 1; i < n; ++i) {
                prev[i] = x[i] + carry;
                v[i] = felt + (v[i] + g * h - felt) * (i == head ? keepHead : keepLink);
                x[i] = prev[i] + v[i] * h;
            }
            // the head: no farther from the anchor than the chain is long (it can come nearer: the chain goes slack)
            {
                const V3 d = x[head] - a1;
                const float l = Length(d);
                if (l > total && l > 1e-9f) x[head] = a1 + d * (total / l);
            }
            // the links between the two ends: taut (within half a percent), on the straight line, evenly; slack, from where their own motion
            // took them, laid between the anchor and the head at their lengths by reaching from one end and back from the other (FABRIK).
            // Near taut that comes slowly: what the rounds leave is shared out along the whole chain (a percent or so of a link each), not
            // put on the last link.
            const V3 span = x[head] - a1;
            const float spanLen = Length(span);
            if (spanLen >= total * kTaut && spanLen > 1e-9f) {
                float along = 0.0f;
                for (int i = 1; i < head; ++i) { along += len[i - 1]; x[i] = a1 + span * (along / total); }
            } else if (head > 1) {
                const V3 target = x[head];
                const V3 fallback = spanLen > 1e-9f ? span * (1.0f / spanLen) : V3{0.0f, -1.0f, 0.0f};
                auto toward = [&](V3 from, V3 at) { const V3 d = Unit(at - from); return Length(d) > 0.5f ? d : fallback; };
                for (int it = 0; it < std::max(1, p.iterations); ++it) {
                    x[head] = target;
                    for (int i = head - 1; i >= 1; --i) x[i] = x[i + 1] + toward(x[i + 1], x[i]) * len[i];   // back from the head
                    for (int i = 1; i <= head; ++i) x[i] = x[i - 1] + toward(x[i - 1], x[i]) * len[i - 1];   // out from the anchor
                    if (Length(x[head] - target) < total * 1e-6f) break;
                }
                const V3 miss = target - x[head];
                float along = 0.0f;
                for (int i = 1; i <= head; ++i) { along += len[i - 1]; x[i] = x[i] + miss * (along / total); }
                x[head] = target;
            }
            for (int i = 1; i < n; ++i) v[i] = (x[i] - prev[i]) * (1.0f / h);
        }
        anchorVel = (to - from) * (1.0f / dt);
        for (int i = 0; i < n; ++i)
            if (!Finite(x[i]) || !Finite(v[i])) { live = false; return; }   // never: but a chain that is not numbers is started again
    }

    // A hit: the chain's speed against the hand's is turned back (the head bounces off the target), by Params::hit - and by the ball's weight: the
    // blow's change of speed is weight times smaller on a heavier ball (it keeps on through), larger on a lighter one, never more than a full bounce.
    void Kick(const Params& p) {
        if (!live) return;
        const float turn = (1.0f + std::clamp(p.restitution, 0.0f, 1.0f)) * std::clamp(p.hit, 0.0f, 2.0f) / std::clamp(p.weight, 0.25f, 4.0f);
        const float k = 1.0f - std::min(turn, 2.0f);
        for (int i = 1; i < n; ++i) v[i] = anchorVel + (v[i] - anchorVel) * k;
    }

    // The head's kinetic + potential energy per unit of its mass, against g (the chain has none): for the tests.
    float Energy(V3 g) const { return n < 2 ? 0.0f : 0.5f * Dot(v[n - 1], v[n - 1]) - Dot(g, x[n - 1]); }
};

// ---- the head: the ball hit by touch strikes with ------------------------------------------------------------------------------------

// The ball at a chain's end, as each model's mesh has it: the centre of its bounding sphere past the chain's LAST bone along the last link,
// and the sphere's radius (the spikes' tips), in the model's units. Read offline off the lod0 meshes (the ball's vertices are all on the last
// bone; the .model files cannot be read at run time - BitKnit-packed - so the numbers are kept here, per model):
//   flail / flail_lh (Flail, Knout, Scourge): 0.299 past chain_bnd_jnt_18, radius 0.340 (the chain 1.52 long);
//   khalim_flail(_lh): 0.273 past X_chain_05, radius 0.293; super_khalim_flail(_lh): 0.289, radius 0.340.
// share: the ball's (Khalim's: the three balls') part of the whole weapon's mass, from the same meshes - the balls' cores (the median radius
// off the centre: 0.222 / 0.174 / 0.201) as iron (7.8 g/cm3), the links as iron rings, the handle (its length, the median radius off its
// axis) as wood (0.7): flail 0.93, Khalim's Flail 0.91, Khalim's Will 0.94. What [flail] ball_weight_pct puts on the weight model's flail.
struct Head { float along = 0.0f, radius = 0.0f; bool ok = false; float share = 0.0f; };
inline Head HeadOf(std::string_view model) {
    if (Contains(model, "super_khalim_flail")) return {0.289f, 0.340f, true, 0.94f};
    if (Contains(model, "khalim_flail")) return {0.273f, 0.293f, true, 0.91f};
    if (Contains(model, "flail")) return {0.299f, 0.340f, true, 0.93f};
    return {};
}
// The same by the item in hand (gamestate::HandCode: four characters, the first in the low byte, space-padded): Flail fla, Knout 9fl, Scourge 7fl
// (the flail model), Khalim's Flail qf1, Khalim's Will qf2. Anything else: none.
inline Head HeadOfCode(uint32_t code) {
    auto is = [&](const char* c) { return code == ((uint32_t)(uint8_t)c[0] | (uint32_t)(uint8_t)c[1] << 8 | (uint32_t)(uint8_t)c[2] << 16 | (uint32_t)(uint8_t)' ' << 24); };
    if (is("fla") || is("9fl") || is("7fl")) return HeadOf("flail");
    if (is("qf1")) return HeadOf("khalim_flail");
    if (is("qf2")) return HeadOf("super_khalim_flail");
    return {};
}
// [flail] ball_weight_pct on the weight model ([weight] on): the whole flail's mass is its handle's and links' plus the ball(s) times the slider -
// 1 + share x (weight - 1) of the model's own. 1 at 100 %.
inline float WeaponMassFactor(const Head& h, float weight) {
    if (!h.ok || !std::isfinite(weight)) return 1.0f;
    return std::max(0.1f, 1.0f + h.share * (std::clamp(weight, 0.25f, 4.0f) - 1.0f));
}
// Of a skeleton's chains, the one whose ball strikes: the middle one of Khalim's three (C_), else the first.
inline int StrikingChain(const ChainRig* rigs, int chains, const std::string_view* names, int count) {
    for (int c = 0; c < chains; ++c) {
        const int b = rigs[c].bone[0];
        if (b >= 0 && b < count && names[b].size() > 1 && (names[b][0] == 'C' || names[b][0] == 'c') && names[b][1] == '_') return c;
    }
    return 0;
}
// The ball's centre and the last link's direction (toward the ball) in the skeleton's model space, from the pose as written.
inline bool Ball(const ChainRig& r, const Mat* pose, const Head& h, V3* centre, V3* dir) {
    if (!h.ok || r.particles < 2 || !centre || !dir) return false;
    const V3 last = Position(pose[r.bone[r.bones - 1]]), before = Position(pose[r.bone[r.first[r.particles - 2]]]);
    const V3 d = Unit(last - before);
    if (!(Length(d) > 0.5f) || !Finite(last)) return false;
    *dir = d;
    *centre = last + d * h.along;
    return true;
}

// ---- the room -------------------------------------------------------------------------------------------------------------------

// The hero's model space -> the room (OpenXR stage: metres, y up), where the chain swings: the hands' own mapping run backwards - skeletons.cpp's
// ControllerPoint puts a point v of the head's turn-only frame (metres, x right, y up, z back) at eye + upm * ToModel(v, c, s), ToModel being
// (x, y, z) -> (-x, y, -z) turned about y by the camera's yaw in the model (c, s its cosine and sine) - then the head's place and turn in the room
// (weight.cpp's ToRoom: headRoom + the head frame turned by headYawDeg about +y). False when it is not numbers (upm not above 0).
inline bool RoomFrame(V3 eye, float upm, float c, float s, V3 headRoom, float headYawDeg, Mat* out) {
    if (!out || !(upm > 1e-6f)) return false;
    const float yaw = headYawDeg * 0.0174532925f, cy = std::cos(yaw), sy = std::sin(yaw);
    auto room = [&](V3 p) {
        const V3 w = (p - eye) * (1.0f / upm);
        const float mx = w.x * c - w.z * s, mz = w.x * s + w.z * c;   // the turn about y undone
        const V3 v{-mx, w.y, -mz};                                     // the head's frame
        return V3{headRoom.x + v.x * cy + v.z * sy, headRoom.y + v.y, headRoom.z - v.x * sy + v.z * cy};
    };
    const V3 o = room({});
    Mat m = Identity();
    SetRow(m, 0, room({1.0f, 0.0f, 0.0f}) - o);
    SetRow(m, 1, room({0.0f, 1.0f, 0.0f}) - o);
    SetRow(m, 2, room({0.0f, 0.0f, 1.0f}) - o);
    SetPosition(m, o);
    if (!FiniteMat(m)) return false;
    *out = m;
    return true;
}

// ---- one frame of one chain, as the pose hook runs it ---------------------------------------------------------------------------

// Whether the hook poses a skeleton's chain at all: the tick (with F4: vrcam asks viewgate), a flail's model, the hero's own. Anything else:
// nothing is written, the game's pose of it stands - as it was before the feature.
inline bool Wanted(bool tickedInF4, bool flailModel, bool herosOwn) { return tickedInF4 && flailModel && herosOwn; }

enum class Done { Started, Stepped, Held };
// One frame. `pose`: the skeleton's matrices as the game posed them (model space); the chain's parent in it is where the handle has the chain.
// toSim / fromSim: the model's space to the simulation's frame and back (the world: gravity there is `gravity`); dt: seconds since the last
// frame. A first frame, one after a pause (dt over 0.25 s), or an anchor that jumped more than `jump` in the frame's units (a teleport, the
// weapon swapped to the other hand) starts the chain where the game has it, at rest. Two calls inside half a millisecond (the same frame posed
// twice) step nothing and pose it the same. kick: a hit since the last frame. The chain's bones in `pose` are replaced; nothing else in it.
inline Done Drive(const ChainRig& r, Sim& s, const Mat& toSim, const Mat& fromSim, float dt, V3 gravity, float jump, bool kick, const Params& p, Mat* pose) {
    Mat rigid[kMaxLinks];
    Rigid(r, pose[r.parent], rigid);
    V3 rest[kMaxLinks], restSim[kMaxLinks];
    RigidPlaces(r, rigid, rest);
    for (int i = 0; i < r.particles; ++i) restSim[i] = Point(toSim, rest[i]);
    Done done = Done::Held;
    if (!s.live || s.n != r.particles || !(dt <= 0.25f) || !(Length(restSim[0] - s.x[0]) <= jump)) {
        s.Start(restSim, r.particles);
        done = Done::Started;
    } else if (dt >= 0.0005f) {
        float lengths[kMaxLinks];
        for (int i = 0; i + 1 < r.particles; ++i) lengths[i] = Length(restSim[i + 1] - restSim[i]);
        s.SetLengths(lengths);
        s.Step(s.x[0], restSim[0], dt, gravity, p);
        done = Done::Stepped;
        if (!s.live) { s.Start(restSim, r.particles); done = Done::Started; }
    }
    if (kick) s.Kick(p);
    V3 places[kMaxLinks];
    places[0] = rest[0];   // the anchor exactly where the handle has it
    for (int i = 1; i < r.particles; ++i) places[i] = Point(fromSim, s.x[i]);
    Mat out[kMaxLinks];
    PoseBones(r, rigid, places, out);
    for (int k = 0; k < r.bones; ++k) pose[r.bone[k]] = out[k];
    return done;
}

}  // namespace flailchain
