// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Copyright (c) 2026 bearstonem and the D2R VR Unleashed contributors (https://github.com/bearstonem/d2r-pcvr).
// From D2R VR Unleashed commit 64e723b30c402a0d9b164b3038ef61a5f30847dd ("Heads turn to you"), its steamvr/look_at.h, taken into D2R VR
// Authentic for [world] town_heads_look: the namespace and the vector type changed; only the town's people (a skeleton under character\npc,
// in a town) and only one in `chance` of them (decided once per character and visit: Chooses); the model's ahead taken from its face's bones
// when it has them (ModelAhead), not assumed +z; the ease a critically damped one (in and out, no jump at the start: Step); the limits from the
// ini. The turn is applied by the skinning to its copy of the pose (vr/head_shadow.cpp, the composite), never written into the game's pose.
// The rest of D2R VR Authentic is MIT; this file stays MPL 2.0.
//
// Heads turn to you: a townsperson near the player turns its neck and head to the player's eyes in the world (the headset, not the hero's spot),
// as far as a neck turns and no farther, eased in and out; let go of when the player walks out of range or round behind it. Drawn only: where
// the character stands, what it does, its dialogue and every game state stay the game's.
//
// Bone matrices are the model's own (row vectors, translation in the last row); the placement takes the model to the world (render units,
// y up). A bone's own "ahead" and "up" are the model's ahead and up seen from its bind matrix, so they follow the bone through any animation.
// Every angle is taken with atan2(x, z) about the world's up and turned back with the same rule, so the world's handedness does not matter.
// Pure: no game, no Windows, no clock; cleanroom/tests/unleashed_head_look_test.cpp drives it.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace headlook {

inline constexpr int kMaxBones = 256;
inline constexpr float kPi = 3.14159265358979f, kRad = kPi / 180.0f;

struct V { float x = 0.0f, y = 0.0f, z = 0.0f; };
inline V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V operator*(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V Cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V Unit(V v) {
    const float l = std::sqrt(Dot(v, v));
    return l > 1e-12f && std::isfinite(l) ? v * (1.0f / l) : V{};
}
inline bool Finite(V v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

struct Limits {
    float yaw = 70 * kRad;           // [world] town_heads_look_yaw: the head's turn to either side of where the body faces
    float up = 30 * kRad;            // [world] town_heads_look_up: its tilt above level
    float down = 40 * kRad;          // [world] town_heads_look_down: and below
    float behindFrom = 105 * kRad;   // a player further round than this is let go of...
    float behindTo = 135 * kRad;     // ...and past this the head is the animation's
    float turnMost = 95 * kRad, tiltMost = 50 * kRad;   // the most ever added to the animation's own head
    float range = 7.0f;              // [world] town_heads_look_range_m: metres from the head to the player's eyes, full within range - fade
    float fade = 1.5f;
    float inside = 0.2f;             // metres: eyes this close to the head (inside it) turn nothing
    float uprightFrom = 0.35f, uprightTo = 0.7f;   // the body's up against the world's: a body lying down is left alone
    float neckShare = 0.35f;         // the share of the turn taken at the neck's root
    float settle = 0.35f;            // seconds: the ease's smooth time (a head turn takes about twice this)
    float fastest = 300 * kRad;      // radians a second, the quickest a head turns
};

// The ini's degrees and metres, kept to what a neck can do (a head turned past its shoulder or folded back is not a person's).
inline Limits FromIni(float yawDeg, float upDeg, float downDeg, float rangeM) {
    Limits l;
    l.yaw = std::clamp(std::isfinite(yawDeg) ? yawDeg : 70.0f, 0.0f, 90.0f) * kRad;
    l.up = std::clamp(std::isfinite(upDeg) ? upDeg : 30.0f, 0.0f, 60.0f) * kRad;
    l.down = std::clamp(std::isfinite(downDeg) ? downDeg : 40.0f, 0.0f, 60.0f) * kRad;
    l.range = std::clamp(std::isfinite(rangeM) ? rangeM : 7.0f, 2.0f, 20.0f);
    return l;
}

// ---- Who ----
inline std::string Lower(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = (char)std::tolower((unsigned char)c);
    for (auto& c : r) if (c == '\\') c = '/';
    return r;
}
// The town's people: a model of the game's NPC folder (data/hd/character/npc/<name>/skeleton/<name>.skeleton - akara, charsi, warriv1,
// act2guard1, act3female, ...). Monsters, summons, other heroes, items are elsewhere. (The rogue and the desert mercenaries share some of these
// models: the caller leaves out a skeleton standing where a hireling is.)
inline bool TownNpcPath(std::string_view path) { return Lower(path).find("/character/npc/") != std::string::npos; }

// Does this character look at you this visit? Decided once from its entity and the visit's salt (a new one each time the town is entered):
// the same answer every frame, `percent` of them yes. 0 none, 100 all.
inline bool Chooses(uint32_t entity, uint32_t salt, int percent) {
    if (percent <= 0) return false;
    if (percent >= 100) return true;
    uint32_t h = entity * 0x9E3779B1u ^ salt * 0x85EBCA77u;   // a mix (murmur's finaliser): neighbouring ids land apart
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return (int)(h % 100u) < percent;
}

// ---- Names ----
// A head's own joint: head_bind_jnt, head, head_main_00_M_JNT, M_head_bind_jnt, C_NeckHead_head; not what hangs from it or is only named for it.
inline bool HeadName(std::string_view name) {
    const std::string n = Lower(name);
    if (n.find("head") == std::string::npos) return false;
    for (const char* word : {"nub", "_end", "attach", "helper", "dummy", "effect", "fx", "eye", "jaw", "hair", "helm", "crown", "band", "dress", "gear",
                             "piece", "weapon", "arrow", "spear", "axe", "hammer", "shield", "tentacle", "horn", "ear", "tongue", "teeth", "lip", "brow"})
        if (n.find(word) != std::string::npos) return false;
    return true;
}
inline bool NeckName(std::string_view name) {
    const std::string n = Lower(name);
    return n.find("neck") != std::string::npos && n.find("necklace") == std::string::npos && n.find("head") == std::string::npos;
}
// The face's own bones, in front of the head's joint: what says where a model looks (ModelAhead).
inline bool FaceName(std::string_view name) {
    const std::string n = Lower(name);
    for (const char* word : {"eye", "jaw", "nose", "mouth", "lip", "brow", "chin", "teeth", "tongue"})
        if (n.find(word) != std::string::npos) return n.find("attach") == std::string::npos;
    return false;
}

// ---- Row-vector helpers ----
inline V Direction(const float* m, V v) { return {v.x * m[0] + v.y * m[4] + v.z * m[8], v.x * m[1] + v.y * m[5] + v.z * m[9], v.x * m[2] + v.y * m[6] + v.z * m[10]}; }
inline V Point(const float* m, V v) { return Direction(m, v) + V{m[12], m[13], m[14]}; }
inline float Wrap(float a) {
    if (!std::isfinite(a)) return 0.0f;
    a = std::fmod(a + kPi, 2 * kPi);
    if (a < 0) a += 2 * kPi;
    return a - kPi;
}
inline float Smooth(float from, float to, float x) {
    if (!(to > from)) return x >= to ? 1.0f : 0.0f;
    const float t = std::clamp((x - from) / (to - from), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
// v turned about the unit axis by angle (right-hand rule, as plain vector algebra).
inline V About(V axis, float angle, V v) {
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + Cross(axis, v) * s + axis * (Dot(axis, v) * (1 - c));
}
inline float YawOf(V v) { return std::atan2(v.x, v.z); }
inline float TiltOf(V v) { return std::atan2(v.y, std::sqrt(v.x * v.x + v.z * v.z)); }
// A world turn: yaw about the world's up, then tilt upward about the level axis square to `ahead` (the head's level heading after the yaw).
// Rows are the turned axes (v' = v . R).
inline void WorldTurn(V ahead, float yaw, float tilt, float r[9]) {
    const V up{0, 1, 0};
    V level = Unit(About(up, yaw, V{ahead.x, 0, ahead.z}));
    if (Dot(level, level) < 0.5f) level = About(up, yaw, V{0, 0, 1});
    const V axis = Cross(level, up);   // turning about it lifts `level` (the triple product rule)
    const V basis[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; ++i) {
        const V v = About(axis, tilt, About(up, yaw, basis[i]));
        r[i * 3] = v.x; r[i * 3 + 1] = v.y; r[i * 3 + 2] = v.z;
    }
}
inline bool Invert3(const float* m, float out[9]) {
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], k = m[10];
    const float A = e * k - f * h, B = f * g - d * k, C = d * h - e * g, det = a * A + b * B + c * C;
    if (!std::isfinite(det) || std::fabs(det) < 1e-12f) return false;
    const float s = 1 / det;
    out[0] = A * s; out[1] = (c * h - b * k) * s; out[2] = (b * f - c * e) * s;
    out[3] = B * s; out[4] = (a * k - c * g) * s; out[5] = (c * d - a * f) * s;
    out[6] = C * s; out[7] = (b * g - a * h) * s; out[8] = (a * e - b * d) * s;
    return true;
}
// The bind pose's place of a bone from its inverse bind matrix (row vectors): the model's point that the inverse takes to the bone's origin.
inline bool BindPlace(const float* inverseBind, V* out) {
    float r[9];
    if (!inverseBind || !Invert3(inverseBind, r)) return false;
    const V t{inverseBind[12], inverseBind[13], inverseBind[14]};
    // p . R + t = 0  ->  p = -t . R^-1
    *out = V{-(t.x * r[0] + t.y * r[3] + t.z * r[6]), -(t.x * r[1] + t.y * r[4] + t.z * r[7]), -(t.x * r[2] + t.y * r[5] + t.z * r[8])};
    return Finite(*out);
}

// ---- A model's head ----
struct Bone {
    std::string_view name;
    int parent = -1;                      // below its own index (Granny's order), -1 the root
    const float* inverseBind = nullptr;   // 16 floats: the model's bind space to the bone's own
};
struct Rig {
    int head = -1;   // the head's joint
    int root = -1;   // the lowest neck joint under it in one run, or the head itself
    int body = -1;   // root's parent: what the head turns against (-1: the model itself)
    V modelAhead{0, 0, 1};                    // where the model looks in its bind pose (its face's bones, else +z)
    int faceBones = 0;                        // how many face bones said so (0: +z, as the heroes')
    V headAhead{0, 0, 1}, headUp{0, 1, 0};    // the head bone's own directions for the model's ahead and up
    V bodyAhead{0, 0, 1}, bodyUp{0, 1, 0};    // the body bone's
    bool Usable() const { return head >= 0 && root >= 0; }
};
enum class Refusal : uint8_t { None, Bones, NoHead, Bind };
inline const char* Name(Refusal r) {
    return r == Refusal::Bones ? "its bones do not read" : r == Refusal::NoHead ? "no bone named as a head" : r == Refusal::Bind ? "its head's bind matrix does not read" : "";
}
// A bone's own directions for the model's ahead and up (+y): the rows of its inverse bind matrix carry the model's axes into the bone, made unit.
inline bool BindAxes(const float* inverseBind, V modelAhead, V& ahead, V& up) {
    if (!inverseBind) return false;
    for (int i = 0; i < 16; ++i) if (!std::isfinite(inverseBind[i])) return false;
    ahead = Unit(Direction(inverseBind, modelAhead));
    up = Unit(Direction(inverseBind, {0, 1, 0}));
    return Dot(ahead, ahead) > 0.5f && Dot(up, up) > 0.5f;
}
// Where the model looks, from its bones: the face's bones under the head (eyes, jaw, nose, brow) stand in front of the head's joint in the bind
// pose. Level, unit; the count of bones that said so. None (or them all at the joint): +z, as the heroes' models face.
inline V ModelAhead(const Bone* bones, int count, int head, int* used) {
    *used = 0;
    V headAt;
    if (head < 0 || head >= count || !BindPlace(bones[head].inverseBind, &headAt)) return {0, 0, 1};
    V sum{};
    for (int i = head + 1; i < count; ++i) {
        bool under = false;
        for (int p = bones[i].parent, up = 0; p >= 0 && up < 8; p = bones[p].parent, ++up) if (p == head) { under = true; break; }
        V at;
        if (!under || !FaceName(bones[i].name) || !BindPlace(bones[i].inverseBind, &at)) continue;
        const V d = at - headAt;
        sum = sum + V{d.x, 0, d.z};
        ++*used;
    }
    const V level = Unit(sum);
    if (*used == 0 || Dot(level, level) < 0.5f) { *used = 0; return {0, 0, 1}; }
    return level;
}
// The first bone named as a head whose parent is not one (a rig's second "head" is a leaf under the first); the neck joints straight under it.
inline Refusal Learn(const Bone* bones, int count, Rig& out) {
    out = Rig{};
    if (!bones || count < 2 || count > kMaxBones) return Refusal::Bones;
    for (int i = 0; i < count; ++i) if (bones[i].parent >= i || bones[i].parent < -1) return Refusal::Bones;
    for (int i = 0; i < count && out.head < 0; ++i) if (HeadName(bones[i].name)) out.head = i;
    if (out.head < 0) return Refusal::NoHead;
    out.root = out.head;
    for (int b = bones[out.head].parent; b >= 0 && NeckName(bones[b].name); b = bones[b].parent) out.root = b;
    out.body = bones[out.root].parent;
    out.modelAhead = ModelAhead(bones, count, out.head, &out.faceBones);
    if (!BindAxes(bones[out.head].inverseBind, out.modelAhead, out.headAhead, out.headUp)) { out = Rig{}; return Refusal::Bind; }
    // A body bone whose bind does not read: the model's own axes stand in.
    if (out.body >= 0 && !BindAxes(bones[out.body].inverseBind, out.modelAhead, out.bodyAhead, out.bodyUp)) out.body = -1;
    if (out.body < 0) { out.bodyAhead = out.modelAhead; out.bodyUp = {0, 1, 0}; }
    return Refusal::None;
}

// ---- Each frame ----
struct Aim {
    float yaw = 0, tilt = 0;   // radians to add to the animation's head, already scaled by weight
    float weight = 0;          // 0..1: range, standing and not behind
    float metres = 0;          // the head to the eyes
    float animYaw = 0;         // the animation's own head from where the body faces (the head as drawn: animYaw + the eased yaw, within the limit)
};
// How far the head should turn from where the game's pose has it, to look at eye (world render units). pose: count bone matrices. False when the
// pose or placement does not read.
inline bool Want(const Rig& rig, const float* pose, int count, const float* placement, V eye, float unitsPerMetre, const Limits& l, Aim& out) {
    out = Aim{};
    if (!rig.Usable() || !pose || !placement || rig.head >= count || rig.body >= count || !(unitsPerMetre >= 1 && unitsPerMetre <= 12) || !Finite(eye)) return false;
    const float* head = pose + size_t(rig.head) * 16;
    for (int i = 0; i < 16; ++i) if (!std::isfinite(head[i]) || !std::isfinite(placement[i])) return false;
    const V headAhead = Unit(Direction(placement, Direction(head, rig.headAhead)));
    V bodyAhead, bodyUp;
    if (rig.body >= 0) {
        const float* body = pose + size_t(rig.body) * 16;
        for (int i = 0; i < 16; ++i) if (!std::isfinite(body[i])) return false;
        bodyAhead = Direction(placement, Direction(body, rig.bodyAhead));
        bodyUp = Direction(placement, Direction(body, rig.bodyUp));
    } else {
        bodyAhead = Direction(placement, rig.modelAhead);
        bodyUp = Direction(placement, {0, 1, 0});
    }
    bodyAhead = Unit(bodyAhead);
    bodyUp = Unit(bodyUp);
    if (Dot(headAhead, headAhead) < 0.5f || Dot(bodyAhead, bodyAhead) < 0.5f || Dot(bodyUp, bodyUp) < 0.5f) return false;
    const V at = Point(placement, V{head[12], head[13], head[14]});
    const V to = eye - at;
    const float away = std::sqrt(Dot(to, to)) / unitsPerMetre;
    if (!std::isfinite(away)) return false;
    out.metres = away;
    // Standing: a body fallen over or one whose heading is straight up or down.
    const float level = std::sqrt(bodyAhead.x * bodyAhead.x + bodyAhead.z * bodyAhead.z);
    float weight = Smooth(l.uprightFrom, l.uprightTo, bodyUp.y) * Smooth(0.3f, 0.6f, level);
    weight *= 1 - Smooth(l.range - l.fade, l.range, away);
    if (away < l.inside) weight = 0;
    const float round = Wrap(YawOf(to) - YawOf(bodyAhead));
    out.animYaw = Wrap(YawOf(headAhead) - YawOf(bodyAhead));
    weight *= 1 - Smooth(l.behindFrom, l.behindTo, std::fabs(round));
    // A head the animation has pointing straight up or down has no heading to turn from.
    const float headLevel = std::sqrt(headAhead.x * headAhead.x + headAhead.z * headAhead.z);
    weight *= Smooth(0.2f, 0.5f, headLevel);
    if (!(weight > 0)) { out.weight = 0; return true; }
    // The look is held to the neck's reach from where the BODY faces, whatever the animation does with the head.
    const float wantYaw = YawOf(bodyAhead) + std::clamp(round, -l.yaw, l.yaw);
    const float wantTilt = std::clamp(TiltOf(to), -l.down, l.up);
    out.yaw = std::clamp(Wrap(wantYaw - YawOf(headAhead)), -l.turnMost, l.turnMost) * weight;
    out.tilt = std::clamp(wantTilt - TiltOf(headAhead), -l.tiltMost, l.tiltMost) * weight;
    out.weight = weight;
    return true;
}

// What a head has been turned by, eased toward what is wanted: a critically damped spring each way (it starts slowly, slows down before it
// arrives, never overshoots), no faster than `fastest`.
struct Eased { float yaw = 0, tilt = 0, yawSpeed = 0, tiltSpeed = 0; };
inline void Damp(float& now, float& speed, float want, float seconds, const Limits& l) {
    if (!std::isfinite(now) || !std::isfinite(speed)) { now = 0; speed = 0; }
    if (!std::isfinite(want)) want = 0;
    if (!(seconds > 0)) return;
    seconds = std::min(seconds, 0.1f);
    const float omega = 2.0f / std::max(l.settle, 1e-3f);
    const float x = omega * seconds, e = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = now - want;
    const float temp = (speed + omega * change) * seconds;
    float next = want + (change + temp) * e;
    speed = (speed - omega * temp) * e;
    const float most = l.fastest * seconds;
    if (std::fabs(next - now) > most) {   // the quickest a head turns
        next = now + std::clamp(next - now, -most, most);
        speed = std::clamp(speed, -l.fastest, l.fastest);
    }
    if ((want - now > 0) == (next > want)) { next = want; speed = 0; }   // never past it
    now = next;
}
inline void Step(Eased& e, const Aim& want, float seconds, const Limits& l) {
    Damp(e.yaw, e.yawSpeed, want.yaw, seconds, l);
    Damp(e.tilt, e.tiltSpeed, want.tilt, seconds, l);
    e.yaw = std::clamp(e.yaw, -l.turnMost, l.turnMost);
    e.tilt = std::clamp(e.tilt, -l.tiltMost, l.tiltMost);
}
// Too little to see: the pose is left as the game's.
inline bool Still(const Eased& e) { return std::fabs(e.yaw) < 0.2f * kRad && std::fabs(e.tilt) < 0.2f * kRad; }

// Turns the neck and head in pose by e: neckShare of it about the neck's root, carried by everything under the root, and the rest about the
// head's joint, carried by everything under the head (hair, jaw, helmet). parents[i] < i. False when refused: the pose is then not to be used.
inline bool Apply(const Rig& rig, const int16_t* parents, float* pose, int count, const float* placement, const Eased& e, const Limits& l) {
    if (!rig.Usable() || !parents || !pose || !placement || count < 1 || count > kMaxBones || rig.head >= count || rig.root > rig.head ||
        !std::isfinite(e.yaw) || !std::isfinite(e.tilt))
        return false;
    float inverse[9];
    if (!Invert3(placement, inverse)) return false;
    const float x3[9]{placement[0], placement[1], placement[2], placement[4], placement[5], placement[6], placement[8], placement[9], placement[10]};
    // The head's level heading in the world, as the game posed it: what the tilt is square to.
    const V ahead = Direction(placement, Direction(pose + size_t(rig.head) * 16, rig.headAhead));
    if (!Finite(ahead)) return false;
    const float share = rig.root == rig.head ? 0.0f : std::clamp(l.neckShare, 0.0f, 1.0f);
    bool under[kMaxBones];
    auto turn = [&](int from, float yaw, float tilt, V heading) {
        float world[9], t[9], model[9];
        WorldTurn(heading, yaw, tilt, world);
        // In the model's space: X . R . X^-1.
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { float s = 0; for (int k = 0; k < 3; ++k) s += x3[i * 3 + k] * world[k * 3 + j]; t[i * 3 + j] = s; }
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { float s = 0; for (int k = 0; k < 3; ++k) s += t[i * 3 + k] * inverse[k * 3 + j]; model[i * 3 + j] = s; }
        for (int i = 0; i < 9; ++i) if (!std::isfinite(model[i])) return false;
        const V pivot{pose[size_t(from) * 16 + 12], pose[size_t(from) * 16 + 13], pose[size_t(from) * 16 + 14]};
        for (int b = from; b < count; ++b) {
            const int p = parents[b];
            under[b] = b == from || (p >= from && p < b && under[p]);
            if (!under[b]) continue;
            float* m = pose + size_t(b) * 16;
            for (int row = 0; row < 4; ++row) {
                V v{m[row * 4], m[row * 4 + 1], m[row * 4 + 2]};
                if (row == 3) v = v - pivot;
                V r{v.x * model[0] + v.y * model[3] + v.z * model[6], v.x * model[1] + v.y * model[4] + v.z * model[7], v.x * model[2] + v.y * model[5] + v.z * model[8]};
                if (row == 3) r = r + pivot;
                m[row * 4] = r.x; m[row * 4 + 1] = r.y; m[row * 4 + 2] = r.z;
            }
        }
        return true;
    };
    if (share > 0 && !turn(rig.root, e.yaw * share, e.tilt * share, ahead)) return false;
    // The neck's share has turned the heading too.
    const V after = About({0, 1, 0}, e.yaw * share, ahead);
    return turn(rig.head, e.yaw * (1 - share), e.tilt * (1 - share), after);
}

}  // namespace headlook
