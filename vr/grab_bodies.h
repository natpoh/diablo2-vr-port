// grab_bodies.h - [weight] grab_bodies: the mod's glue to the weight library's grab (d2r_vr_weight.dll's D2rgb* exports, shared/d2r_vr_grab.h, vr/grab_lib.h). Pure
// (cleanroom/tests/grab_bodies_test.cpp); the game side is vr/grab_wire.cpp, the bodies and the doll vr/melee_touch.cpp.
//
// What it is: an empty hand's grip on a monster's or an NPC's body holds it. The grab is the library's, all of it - which body the hand is over, taking it, the joint
// between the hand and the body, the hand's strength against the body's weight (a humanoid's limb and its monster give way, anything else holds the hand), letting go
// and tearing loose, the pulses. Here only what the mod hands over: the bodies as the game poses them (render metres, as the weapon meets them; the shields and weapons the
// monsters hold are not bodies to take; a limb gives way only with [weight] monster_ragdoll, whose doll draws it) and the hand's turn in the render world.
//
// Never the game's mechanics: nothing here or in the library moves a unit, attacks, hurts or commands anything. The monster stays where the server puts it; only its
// capsules (and with [weight] monster_ragdoll_pose its drawn bones) and the drawn hand move. Pulling a monster's real position would need the server (a knock-back
// command) - not built.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "d2r_vr_grab.h"
#include "monster_bodies.h"

namespace grabbodies {

constexpr float kReachM = 0.08f;     // [weight] grab_reach_cm default: the library's grab radius about the palm
constexpr float kBreakM = 0.80f;     // [weight] grab_break_cm default: the controller this far from the held point tears the grip loose

// May a hand take a body now - and if not, why (a bit each, for the log). A hand holding a bow or crossbow is busy with it; the string hand is free (it grabs when it is
// not on the string - the draw's grip - or at a quiver orb, which takes the grip from the layout itself). With the weight model a hand is empty when the weight model's
// weapon is not in it (dropped, on the back); a weapon or shield in its slot otherwise keeps it busy.
enum Busy : unsigned { kSlot = 1, kBowHand = 2, kWeapon = 4, kBottle = 8, kShoulder = 16, kBowString = 32, kThrow = 64, kTakeZone = 128 };
struct HandCase {
    bool weightOn = false, weightHolds = false, secondHere = false, weightSlot = false;   // the weight model: on, this hand on its weapon, the second weapon here, the weapon's slot hand
    bool slotFree = true;    // nothing in this hand's slot (vrcam's handFree before the bow's rule)
    bool ranged = false, bowHand = false;   // a bow or crossbow wielded, and this the hand that holds it
    bool bottle = false, shoulder = false, bowString = false, throwGrip = false, takeZone = false;
};
inline unsigned BusyWhy(const HandCase& c) {
    unsigned why = 0;
    if (c.weightOn) {
        if (c.weightHolds || c.secondHere) why |= kWeapon;
        else if (!c.slotFree && !c.weightSlot) why |= kSlot;
    } else if (c.ranged) {
        if (c.bowHand) why |= kBowHand;
    } else if (!c.slotFree) {
        why |= kSlot;
    }
    if (c.bottle) why |= kBottle;
    if (c.shoulder) why |= kShoulder;
    if (c.bowString) why |= kBowString;
    if (c.throwGrip) why |= kThrow;
    if (c.weightOn && c.takeZone) why |= kTakeZone;
    return why;
}
// The first reason in words.
inline const char* BusyText(unsigned why) {
    return why & kWeapon ? "it holds the weapon" : why & kSlot ? "its slot holds an item (a weapon, a shield)" : why & kBowHand ? "it holds the bow"
         : why & kBottle ? "it holds a bottle" : why & kShoulder ? "behind a shoulder (the weapon swap's grip)" : why & kBowString ? "it draws the string"
         : why & kThrow ? "the throw's grip" : why & kTakeZone ? "the weapon is in reach (the grip takes the weapon first)" : "free";
}

// [weight] grab_arm_reach: is a held point past the arm - must the grip let go? A holding hand is drawn where the grab holds it, and [arms] full_reach lengthens the arm
// (up to 35 %) to a hand farther from its shoulder than the arm is long: holding the other player as he walked off, the arms grew (the co-op test 2026-10-26: "the arm
// lengthened up to 35 %" for the seconds of each hold). An arm never changes its length, so the hold ends there. shoulder: the drawn shoulder joint, armM: the drawn arm
// to the grip (skel::ArmReach - the hero's own bones fitted to the player's span), held: the point held, ctrl: the controller's grip, one frame and unit. Past the arm
// AND past the controller: a controller out of the arm's reach on its own is drawn there by full_reach with nothing held, the hold adds no length to that. slackM: how far
// the hold may put the hand off the controller anyway - the grab's reach (the palm this near a surface is on it: the hand drawn on the held surface is that far off); a
// held point past the controller by less is the hand on what it holds, not the arm lengthening (the props, 2026-10-27: held 71 cm, the controller 70, the arm 63 - let go
// at once, every time, without it).
inline bool PastArm(const float shoulder[3], float armM, const float held[3], const float ctrl[3], float slackM = 0.0f) {
    if (!(armM > 0.0f) || !std::isfinite(armM)) return false;
    float dh = 0.0f, dc = 0.0f;
    for (int k = 0; k < 3; ++k) {
        dh += (held[k] - shoulder[k]) * (held[k] - shoulder[k]);
        dc += (ctrl[k] - shoulder[k]) * (ctrl[k] - shoulder[k]);
    }
    if (!std::isfinite(dh) || !std::isfinite(dc)) return false;
    const float slack = std::isfinite(slackM) && slackM > 0.0f ? slackM : 0.0f;
    return std::sqrt(dh) > std::max(armM, std::sqrt(dc)) + slack;
}

// The body nearest the palm among the library's: its surface distance (m, < 0 inside; 1e30 none), which one (index, -1 none). For the log of a grip that took nothing.
inline float Nearest(const D2rgbBody* b, int n, const float palm[3], int* which) {
    float best = 1e30f;
    if (which) *which = -1;
    for (int i = 0; i < n; ++i) {
        float ax[3], l2 = 0.0f, t = 0.0f;
        for (int k = 0; k < 3; ++k) { ax[k] = b[i].b[k] - b[i].a[k]; l2 += ax[k] * ax[k]; t += (palm[k] - b[i].a[k]) * ax[k]; }
        t = l2 > 1e-10f ? std::clamp(t / l2, 0.0f, 1.0f) : 0.5f;
        float d2 = 0.0f;
        for (int k = 0; k < 3; ++k) { const float q = b[i].a[k] + ax[k] * t - palm[k]; d2 += q * q; }
        const float d = std::sqrt(d2) - b[i].radius;
        if (d < best) { best = d; if (which) *which = i; }
    }
    return best;
}

// The capsule's radius as the hand meets it: a limb as thick as its mesh, the game's capsule less the touch's margin (as the weapon meets it).
inline float RadiusOf(const monsterbodies::Body& b) { return b.rig ? b.radius : monsterbodies::CoreRadius(b.radius); }

// The snapshot's bodies for the library (as the game poses them), up to D2RGB_MAX_BODIES; dollOn: [weight] monster_ragdoll draws a humanoid's limb giving way - without it
// nothing is marked to give. *of (may be null): the snapshot index of each. How many.
inline int ToGrab(const monsterbodies::Snapshot& s, bool dollOn, D2rgbBody* out, int* of = nullptr) {
    int k = 0;
    int at[monsterbodies::kMax];   // each snapshot body's index among the handed over (-1 left out): the parents are the library's indices
    for (int i = 0; i < monsterbodies::kMax; ++i) at[i] = -1;
    for (int i = 0; i < s.n && i < monsterbodies::kMax && k < D2RGB_MAX_BODIES; ++i)
        if (s.b[i].held == 0) at[i] = k++;
    k = 0;
    for (int i = 0; i < s.n && i < monsterbodies::kMax && k < D2RGB_MAX_BODIES; ++i) {
        const monsterbodies::Body& b = s.b[i];
        if (b.held != 0) continue;
        D2rgbBody& g = out[k];
        g = D2rgbBody{};
        g.id = b.id;
        g.bone = b.rig ? b.bone : -1;
        for (int j = 0; j < 3; ++j) { g.a[j] = b.bottom[j]; g.b[j] = b.top[j]; }
        g.radius = RadiusOf(b);
        g.doll = dollOn && b.doll && b.rig ? 1 : 0;   // (humanoids only: a held beast stands - its limbs bent shook, the headset 2026-10-26)
        g.parent = b.parent >= 0 && b.parent < s.n && b.parent < monsterbodies::kMax ? at[b.parent] : -1;
        if (of) of[k] = i;
        ++k;
    }
    return k;
}

// The held body in a snapshot by its monster and bone (-1: the game's capsule of it), the shields and weapons left out as ToGrab leaves them; -1 when it is not there.
inline int FindHeld(const monsterbodies::Snapshot& s, uint32_t id, int bone) {
    for (int i = 0; i < s.n && i < monsterbodies::kMax; ++i)
        if (s.b[i].held == 0 && s.b[i].id == id && (s.b[i].rig ? s.b[i].bone : -1) == bone) return i;
    return -1;
}

// [weight] grab_wrap without the grip library: the hand laid on a capsule by our own frame. palm: the palm's middle; fingers: the hand's axis toward the fingers (unit);
// the capsule a..b, radius r; depth: the palm's middle to its surface (the hook: a fifth of the knuckles' span). Out: where the palm's middle goes - a palm's depth off the
// surface, over the point of the axis nearest the palm - and the hand's new frame as rows (x toward the fingers, as near the old as the palm allows; y = z x x; z out of the
// palm, INTO the body). False when there is no frame (a capsule of no length, the fingers pointing into it). One unit throughout.
inline bool LayPalm(const float palm[3], const float fingers[3], const float a[3], const float b[3], float r, float depth, float outPalm[3], float rows[9]) {
    float ax[3], len = 0.0f;
    for (int k = 0; k < 3; ++k) { ax[k] = b[k] - a[k]; len += ax[k] * ax[k]; }
    len = std::sqrt(len);
    if (!(len > 1e-6f) || !std::isfinite(len)) return false;
    for (int k = 0; k < 3; ++k) ax[k] /= len;
    float t = 0.0f;
    for (int k = 0; k < 3; ++k) t += (palm[k] - a[k]) * ax[k];
    t = std::clamp(t, 0.0f, len);
    float q[3], n[3], nl = 0.0f;
    for (int k = 0; k < 3; ++k) { q[k] = a[k] + ax[k] * t; n[k] = palm[k] - q[k]; nl += n[k] * n[k]; }
    nl = std::sqrt(nl);
    if (nl > 1e-6f) {
        for (int k = 0; k < 3; ++k) n[k] /= nl;
    } else {   // (the palm on the axis: out the side square to it and up)
        const float c[3] = {-ax[2], 0.0f, ax[0]};   // (axis x up)
        const float cl = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        if (!(cl > 1e-6f)) return false;
        for (int k = 0; k < 3; ++k) n[k] = c[k] / cl;
    }
    for (int k = 0; k < 3; ++k) outPalm[k] = q[k] + n[k] * (r + depth);
    const float z[3] = {-n[0], -n[1], -n[2]};
    const float fz = fingers[0] * z[0] + fingers[1] * z[1] + fingers[2] * z[2];
    float x[3] = {fingers[0] - z[0] * fz, fingers[1] - z[1] * fz, fingers[2] - z[2] * fz};
    const float xl = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    if (!(xl > 1e-4f)) return false;
    for (int k = 0; k < 3; ++k) x[k] /= xl;
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    for (int k = 0; k < 3; ++k) { rows[k] = x[k]; rows[3 + k] = y[k]; rows[6 + k] = z[k]; }
    return true;
}

// Does body k of a snapshot hang below body i - is i on its chain of parents (LinkParents)?
inline bool HangsBelow(const monsterbodies::Snapshot& s, int k, int i) {
    const int n = std::min(s.n, monsterbodies::kMax);
    for (int j = k >= 0 && k < n ? s.b[k].parent : -1, guard = 0; j >= 0 && j < n && guard < n; j = s.b[j].parent, ++guard)
        if (j == i) return true;
    return false;
}

// The held limbs as the library has them (D2rgbLink: its joint end and far end) put into a snapshot in place of where the game poses them: each body of that monster
// and bone (its limbs, not what it holds), the end nearer the link's joint taking the joint - and every body of the monster hanging below it (its head, the other arm,
// the legs below a held trunk) turned with it the way the drawn model turns them (dollpose::Build: the limb's line, as the links before it left it, turned onto the
// held line about its joint, all its children with it), and carried to where the held joint is. Links in the library's order (the chain from its root). How many were put.
// (Before, only the held capsules moved: a held trunk leaned the drawn model whole while its head, its other arm and its legs stayed upright in the bodies - the
// headset 0.183 «часть блоков отклонилась, а часть осталась».)
inline int PutLinks(monsterbodies::Snapshot* s, const D2rgbLink* links, int n) {
    if (!s || !links) return 0;
    const int nb = std::min(s->n, monsterbodies::kMax);
    int put = 0;
    for (int l = 0; l < n; ++l) {
        const int i = FindHeld(*s, links[l].id, links[l].bone);
        if (i < 0 || !s->b[i].rig) continue;
        monsterbodies::Body& b = s->b[i];
        float da = 0.0f, db = 0.0f;
        for (int k = 0; k < 3; ++k) { da += (b.bottom[k] - links[l].joint[k]) * (b.bottom[k] - links[l].joint[k]); db += (b.top[k] - links[l].joint[k]) * (b.top[k] - links[l].joint[k]); }
        float* toJoint = da <= db ? b.bottom : b.top;
        float* toFar = da <= db ? b.top : b.bottom;
        // the turn: its line as it stands now onto the held line (Rodrigues' matrix about the axis between them), and the move of its joint
        float cur[3], want[3], j0[3];
        for (int k = 0; k < 3; ++k) { j0[k] = toJoint[k]; cur[k] = toFar[k] - toJoint[k]; want[k] = links[l].tip[k] - links[l].joint[k]; }
        const float cl = std::sqrt(cur[0] * cur[0] + cur[1] * cur[1] + cur[2] * cur[2]), wl = std::sqrt(want[0] * want[0] + want[1] * want[1] + want[2] * want[2]);
        float r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        if (cl > 1e-6f && wl > 1e-6f) {
            float ax[3] = {cur[1] * want[2] - cur[2] * want[1], cur[2] * want[0] - cur[0] * want[2], cur[0] * want[1] - cur[1] * want[0]};
            const float al = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
            if (al > 1e-9f * cl * wl) {
                const float angle = std::atan2(al, cur[0] * want[0] + cur[1] * want[1] + cur[2] * want[2]);
                for (float& c : ax) c /= al;
                const float x = ax[0], y = ax[1], z = ax[2], c = std::cos(angle), sn = std::sin(angle), k1 = 1.0f - c;
                const float m[9] = {c + x * x * k1, x * y * k1 - z * sn, x * z * k1 + y * sn, y * x * k1 + z * sn, c + y * y * k1, y * z * k1 - x * sn,
                                    z * x * k1 - y * sn, z * y * k1 + x * sn, c + z * z * k1};
                for (int k = 0; k < 9; ++k) r[k] = m[k];
            }
        }
        const auto carry = [&](float* pt) {
            const float v[3] = {pt[0] - j0[0], pt[1] - j0[1], pt[2] - j0[2]};
            for (int k = 0; k < 3; ++k) pt[k] = links[l].joint[k] + r[k * 3] * v[0] + r[k * 3 + 1] * v[1] + r[k * 3 + 2] * v[2];
        };
        for (int k = 0; k < nb; ++k)
            if (k != i && s->b[k].id == b.id && HangsBelow(*s, k, i)) { carry(s->b[k].bottom); carry(s->b[k].top); }
        for (int k = 0; k < 3; ++k) { toJoint[k] = links[l].joint[k]; toFar[k] = links[l].tip[k]; }
        ++put;
    }
    return put;
}

// Was monster id among the hostile ones (false: an NPC, a hireling - or not found)?
inline bool Hostile(const monsterbodies::Snapshot& s, uint32_t id) {
    for (int i = 0; i < s.n && i < monsterbodies::kMax; ++i)
        if (s.b[i].id == id) return s.b[i].hostile;
    return false;
}

// A turn in the head's frame (x right, y up, z back, the head's turn only: the controllers' frame) -> the render world, whose axes the head frame's are camR, up and
// -camF (monsterbodies::HeadToRender's mapping). q (x y z w) in, out.
inline void HeadRotToRender(const monsterbodies::View& v, const float q[4], float out[4]) {
    // the head frame's basis in the render world, as a rotation matrix's columns
    const float c0[3] = {v.camR[0], v.camR[1], v.camR[2]}, c1[3] = {0.0f, 1.0f, 0.0f};
    float c2[3] = {-v.camF[0], -v.camF[1], -v.camF[2]};
    // (a mirrored basis - the render world of the other handedness - is no turn: its back axis is flipped, the library only compares turns with each other)
    if (c0[0] * (c1[1] * c2[2] - c1[2] * c2[1]) - c0[1] * (c1[0] * c2[2] - c1[2] * c2[0]) + c0[2] * (c1[0] * c2[1] - c1[1] * c2[0]) < 0.0f)
        for (int k = 0; k < 3; ++k) c2[k] = -c2[k];
    const float m00 = c0[0], m01 = c1[0], m02 = c2[0], m10 = c0[1], m11 = c1[1], m12 = c2[1], m20 = c0[2], m21 = c1[2], m22 = c2[2];
    float b[4];   // the basis as a quaternion (x y z w)
    const float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        b[3] = 0.25f * s; b[0] = (m21 - m12) / s; b[1] = (m02 - m20) / s; b[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        b[3] = (m21 - m12) / s; b[0] = 0.25f * s; b[1] = (m01 + m10) / s; b[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        b[3] = (m02 - m20) / s; b[0] = (m01 + m10) / s; b[1] = 0.25f * s; b[2] = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        b[3] = (m10 - m01) / s; b[0] = (m02 + m20) / s; b[1] = (m12 + m21) / s; b[2] = 0.25f * s;
    }
    out[0] = b[3] * q[0] + b[0] * q[3] + b[1] * q[2] - b[2] * q[1];
    out[1] = b[3] * q[1] - b[0] * q[2] + b[1] * q[3] + b[2] * q[0];
    out[2] = b[3] * q[2] + b[0] * q[1] - b[1] * q[0] + b[2] * q[3];
    out[3] = b[3] * q[3] - b[0] * q[0] - b[1] * q[1] - b[2] * q[2];
    const float n = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    if (n > 1e-6f && std::isfinite(n))
        for (int k = 0; k < 4; ++k) out[k] /= n;
    else { out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; }
}

}  // namespace grabbodies
