// player_bodies.h - [multiplayer] pose_share_bodies: the other VR players' bodies and held weapons as capsules - drawn in F4, met by our weight model
// (a weapon stops on another player's body, steel knocks on steel: the game's own block sound, [weight] clash_sound) - from the poses that came
// through D2RLoader's plugin network (vr/pose_share.h format 2). Visual and physics of the DRAWN weapon only: nothing is hit, nobody is damaged,
// nothing of the game changes. Pure maths (cleanroom/tests/player_bodies_test.cpp): no game calls, no globals.
//
// The capsules, in the sender's hero's frame (metres of its body, eye height E):
//   the head      a ball on the headset's place, 0.07 E
//   the arms      shoulder -> elbow (0.035 E), elbow -> the controller (0.03 E): the shoulders and elbows as the sender's skeleton hook drew them
//   the trunk     under the shoulders' middle, from the hip down from it to just under the neck (0.095 E), and the legs from the hip to the floor (0.075 E)
//   the weapons   butt -> tip, as thick as hit by touch sweeps them (the weapon bone as drawn, the model's own mesh bounds)
// The trunk's and the legs' places and every thickness but the weapons' are a person's proportions of the eye height (no skeleton of theirs is
// read for them): the fallback where the model says nothing yet. Without the sender's body (its arms not posed, or an older sender) the neck is
// 0.126 E under the head and there are no arms.
//
// In our world: the sender's metres over its eye height times our hero's eye height (world units), its feet where its hero stands (pose_share.h
// FeetOf), turned by its heading; then over OUR units per metre, the render metres the touch's monster bodies are in (vr/monster_bodies.h).
// Their ids are a range no game unit has (kIdBase): a weight-model strike on one is never matched to a monster (hit by touch presses only what its
// own scan found), and held 1 (a weapon) / 2 (a body: as a monster's shield - it stops the weapon, takes no hit and pushes no doll).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "clash_loud.h"
#include "monster_bodies.h"
#include "parry.h"
#include "pose_share.h"

namespace playerbodies {

constexpr uint32_t kIdBase = 0xFFFFFE00u;   // [kIdBase, kIdBase + 2 * kMaxPlayers): player p's body kIdBase + 2p, its weapons + 1 (below weight.cpp's kDualId)
constexpr int kMaxPlayers = 15;
inline uint32_t BodyId(int player) { return kIdBase + 2u * static_cast<uint32_t>(player); }
inline uint32_t WeaponId(int player) { return kIdBase + 2u * static_cast<uint32_t>(player) + 1u; }
inline bool IsPlayerId(uint32_t id) { return id >= kIdBase && id < kIdBase + 2u * kMaxPlayers; }
inline bool IsWeaponId(uint32_t id) { return IsPlayerId(id) && ((id - kIdBase) & 1u); }

enum Part : int { kHeadPart = 0, kTrunk = 1, kLegs = 2, kUpperR = 3, kUpperL = 4, kForeR = 5, kForeL = 6, kWeaponPart0 = 8, kWeaponPart1 = 9 };
inline bool WeaponPart(int part) { return part >= kWeaponPart0; }

// A person's proportions, of the eye height (the fallback - see above)
constexpr float kNeckUnderHead = 0.126f;   // the shoulders' height is 0.874 of the eyes'
constexpr float kHipOfEye = 0.566f;
constexpr float kHeadR = 0.07f, kTrunkR = 0.095f, kLegsR = 0.075f, kUpperArmR = 0.035f, kForearmR = 0.03f;
constexpr int kMaxCaps = 9;

struct Cap {
    float a[3] = {}, b[3] = {};
    float r = 0.0f;
    int part = 0;
};

inline void Set3(float d[3], float x, float y, float z) { d[0] = x; d[1] = y; d[2] = z; }

// The capsules of one pose in its hero's frame (metres): the body when `body`, the weapons when `weapons`. Returns how many (up to max).
inline int Capsules(const poseshare::Pose& p, bool body, bool weapons, Cap* out, int max) {
    using namespace poseshare;
    int n = 0;
    auto put = [&](const float a[3], const float b[3], float r, int part) {
        if (n >= max || !(r > 0.0f) || !std::isfinite(a[0] + a[1] + a[2] + b[0] + b[1] + b[2] + r)) return;
        Cap& c = out[n++];
        for (int i = 0; i < 3; ++i) { c.a[i] = a[i]; c.b[i] = b[i]; }
        c.r = r;
        c.part = part;
    };
    const float E = std::clamp(p.eyeM, 0.5f, 2.5f);
    if (body && (p.flags & kHead)) {
        put(p.head.p, p.head.p, kHeadR * E, kHeadPart);
        float neck[3];
        const bool arms = (p.flags & kBody) != 0;
        if (arms) for (int i = 0; i < 3; ++i) neck[i] = 0.5f * (p.shoulder[0][i] + p.shoulder[1][i]);
        else Set3(neck, p.head.p[0], p.head.p[1] - kNeckUnderHead * E, p.head.p[2]);
        // a crouch: the hip goes down as the head does (never under the legs' own thickness)
        const float crouch = std::clamp(p.head.p[1] / E, 0.4f, 1.1f);
        const float hipY = std::max(kHipOfEye * E * crouch, 2.0f * kLegsR * E);
        const float trunkTop = std::max(hipY, neck[1] - 0.5f * kTrunkR * E);
        float a[3], b[3];
        Set3(a, neck[0], hipY, neck[2]);
        Set3(b, neck[0], trunkTop, neck[2]);
        put(a, b, kTrunkR * E, kTrunk);
        Set3(a, neck[0], kLegsR * E, neck[2]);
        Set3(b, neck[0], hipY, neck[2]);
        put(a, b, kLegsR * E, kLegs);
        if (arms) {
            const Joint* hands[2] = {&p.right, &p.left};
            const uint8_t bits[2] = {kRight, kLeft};
            for (int s = 0; s < 2; ++s) {
                put(p.shoulder[s], p.elbow[s], kUpperArmR * E, s ? kUpperL : kUpperR);
                if (p.flags & bits[s]) put(p.elbow[s], hands[s]->p, kForearmR * E, s ? kForeL : kForeR);
            }
        }
    }
    if (weapons)
        for (int w = 0; w < 2; ++w)
            if (p.flags & (w ? poseshare::kWeapon1 : poseshare::kWeapon0)) put(p.weapon[w].butt, p.weapon[w].tip, p.weapon[w].radius, w ? kWeaponPart1 : kWeaponPart0);
    return n;
}

// A point of the sender's hero's frame into our render metres (the touch's frame): its feet (our world units), heading, its units per metre (our hero's
// eye units over its eye height) and ours.
inline void ToRender(const float local[3], const float feet[3], float heading, float upmTheirs, float upmOurs, float out[3]) {
    float w[3];
    poseshare::ToWorld(local, feet, heading, upmTheirs, w);
    for (int i = 0; i < 3; ++i) out[i] = w[i] / upmOurs;
}

// The capsules as bodies of the weight model's snapshot (appended while there is room). Returns how many were added.
inline int AddBodies(const Cap* c, int n, int player, const float feet[3], float heading, float upmTheirs, float upmOurs, monsterbodies::Snapshot* snap) {
    if (!snap || player < 0 || player >= kMaxPlayers || !(upmTheirs > 0.0f) || !(upmOurs > 0.0f)) return 0;
    int k = 0;
    for (int i = 0; i < n && snap->n < monsterbodies::kMax; ++i) {
        monsterbodies::Body& b = snap->b[snap->n];
        b = monsterbodies::Body{};
        const bool weapon = WeaponPart(c[i].part);
        b.id = weapon ? WeaponId(player) : BodyId(player);
        ToRender(c[i].a, feet, heading, upmTheirs, upmOurs, b.bottom);
        ToRender(c[i].b, feet, heading, upmTheirs, upmOurs, b.top);
        b.radius = c[i].r * upmTheirs / upmOurs;
        b.rig = true;          // (its radius as it is: not the touch's capsule less a margin)
        b.bone = c[i].part;
        b.doll = false;
        b.hostile = false;
        b.held = weapon ? 1 : 2;
        b.mode = -1;
        ++snap->n;
        ++k;
    }
    return k;
}

// The bodies drawn (D2R_DepthFog.fx Hitboxes, world units from the eye, as melee_boxes.h Pack): a weapon in kindWeapon, the rest in kindBody. A
// ball (both ends the same) gets a 1 cm stub: the shader draws no capsule of no length. Returns how many were packed.
inline int Pack(const monsterbodies::Body* b, int n, float upmOurs, const float eye[3], int kindBody, int kindWeapon, float (*a)[4], float (*bb)[4], int max) {
    int k = 0;
    for (int i = 0; i < n && k < max; ++i) {
        if (!IsPlayerId(b[i].id)) continue;
        float top[3] = {b[i].top[0], b[i].top[1], b[i].top[2]};
        if (top[0] == b[i].bottom[0] && top[1] == b[i].bottom[1] && top[2] == b[i].bottom[2]) top[1] += 0.01f;
        for (int j = 0; j < 3; ++j) { a[k][j] = b[i].bottom[j] * upmOurs - eye[j]; bb[k][j] = top[j] * upmOurs - eye[j]; }
        a[k][3] = b[i].radius * upmOurs;
        bb[k][3] = static_cast<float>(b[i].held == 1 ? kindWeapon : kindBody);
        ++k;
    }
    return k;
}

// [debug] pose_share_echo_ahead_cm: our own echoed pose stood `distM` ahead of our hero and turned to face us - a second player to meet on one PC.
inline void EchoAhead(const float feet[3], float heading, float distM, float upm, float outFeet[3], float* outHeading) {
    float r[3], a[3];
    poseshare::Axes(heading, r, a);
    for (int i = 0; i < 3; ++i) outFeet[i] = feet[i] + a[i] * distM * upm;
    *outHeading = poseshare::WrapPi(heading + poseshare::kPi);
}

// Our weapon on another player's weapon: one knock per meeting (parry::Clashes - within 2 cm a clash, then none until they part beyond 6 cm and
// 350 ms have gone), as loud and as hard as they closed (vr/clash_loud.h: their two velocities, each from its capsule's middle; under 0.5 m/s neither
// heard nor felt). All in render metres.
class Clash {
public:
    struct Blade { parry::Capsule c; int hand = 0; };
    struct Theirs { parry::Capsule c; uint32_t id = 0; };
    struct Event { int hand = 0; uint32_t id = 0; float closing = 0.0f; clashloud::Clash loud; };

    int Step(uint64_t nowMs, const Blade* ours, int nOurs, const Theirs* theirs, int nTheirs, Event* out, int max) {
        stroke::Vec vo[2] = {}, vt[kTheirsMax] = {};
        nOurs = std::min(nOurs, 2);
        nTheirs = std::min(nTheirs, kTheirsMax);
        for (int i = 0; i < nOurs; ++i) vo[i] = own_.Feed(kOwnKey + static_cast<uint32_t>(ours[i].hand & 1), (ours[i].c.a + ours[i].c.b) * 0.5f, nowMs);
        for (int j = 0; j < nTheirs; ++j) vt[j] = motion_.Feed(theirs[j].id * 31u + static_cast<uint32_t>(j), (theirs[j].c.a + theirs[j].c.b) * 0.5f, nowMs);
        int k = 0;
        for (int i = 0; i < nOurs; ++i)
            for (int j = 0; j < nTheirs; ++j) {
                const float gap = parry::Gap(ours[i].c, theirs[j].c);
                const uint32_t key = (theirs[j].id * 31u + static_cast<uint32_t>(j)) * 4u + static_cast<uint32_t>(ours[i].hand & 1);
                if (!clashes_.Step(key, true, gap, nowMs) || k >= max) continue;
                const stroke::Vec d = vo[i] - vt[j];
                Event& e = out[k++];
                e.hand = ours[i].hand & 1;
                e.id = theirs[j].id;
                e.closing = std::sqrt(stroke::Dot(d, d));
                e.loud = clashloud::Of(e.closing);
            }
        return k;
    }

private:
    static constexpr int kTheirsMax = 16;
    static constexpr uint32_t kOwnKey = 0xB1ADE100u;
    parry::Clashes clashes_;
    parry::WeaponMotion own_, motion_;
};

// ---- [multiplayer] pose_share_bodies: the other players among hit by touch's own bodies (melee_touch.cpp PublishBodies) -------------------------------------
// Each other player near is a unit of that list as an NPC is: the whole-unit capsule below, and - the touch's Assemble matching the nearest skeleton to it - his
// own rig's limbs (skel::MonsterRigs walks every skeleton but our hero's), a doll when the rig is a humanoid's; not hostile, held 0. So the weight model meets
// him, the debug view draws him and grab_bodies takes him, as an NPC. His id is one no game unit has (PlayerBodyId): hit by touch presses only what its own
// monster scan found, so nothing of his is ever hit.
constexpr int kTouchPlayers = 7;   // other players at once (the ids 8 .. 14 of the player range; 0 .. 7 are the poses')
inline uint32_t PlayerBodyId(int slot) { return BodyId(8 + slot); }
// A hero's footprint and height as a MonStats row would give them (melee_bodies.h BodyFor: size 2 x 2, 17 units tall - a man at the game's ~10 units a metre).
constexpr meleetouch::MonsterRow kPlayerRow{2, 2, 170};

// Which slot each player's unit id has, kept while he is near (a slot not seen for kForgetMs is free again): the id of his body stays the same from scan
// to scan, so a doll's sway and a grab follow him.
class PlayerSlots {
public:
    static constexpr uint64_t kForgetMs = 3000;
    int Slot(uint32_t unit, uint64_t nowMs) {
        int free = -1;
        for (int i = 0; i < kTouchPlayers; ++i) {
            if (s_[i].unit == unit && unit) { s_[i].seen = nowMs; return i; }
            if (free < 0 && (!s_[i].unit || nowMs - s_[i].seen > kForgetMs)) free = i;
        }
        if (free < 0 || !unit) return -1;
        s_[free] = {unit, nowMs};
        return free;
    }
private:
    struct E { uint32_t unit = 0; uint64_t seen = 0; };
    E s_[kTouchPlayers];
};

}  // namespace playerbodies
