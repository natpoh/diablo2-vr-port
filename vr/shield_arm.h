// shield_arm.h - [hands] shoulder_draw, the shield: on which forearm it is strapped. Pure: no game calls, no globals (cleanroom/tests/shield_arm_test.cpp feeds it).
//
// The shield is the game's left-slot item; it never leaves that slot. Only where it is DRAWN changes, so a tired arm can hand the sword and the shield to the other pair:
//   - it starts strapped to the left forearm (as [shield] forearm has it) and needs no grip to stay there;
//   - WEAPON AND SHIELD ARE ALWAYS ON OPPOSITE ARMS: whenever the weapon ends up in the hand whose forearm wears the shield - drawn from either shoulder into it, taken up off the floor or
//     out of the air by it, passed into it from the other hand, put in it by the game (a new weapon) - the shield jumps to the other forearm at that moment. The weapon in the right hand,
//     the shield on the left arm (as ever); the weapon in the left hand (a cross draw), the shield on the right arm, mirrored (RightArmMatrix below). That rule is Follower::Step and nothing
//     else decides where the shield is, so every way the weapon changes hands goes through it;
//   - the shield never moves for any other reason: a weapon let go leaves it where it is; with both hands on the weapon it stays;
//   - two ticks, independent: FOLLOW (the rule above) and STOW ([hands] shoulder_draw_shield_stow, off by default): with STOW a grip pressed behind the LEFT shoulder by a hand that holds
//     nothing PUTS THE SHIELD AWAY (it is not drawn; still the game's equipped shield, it simply is not on an arm - no bash, no block animation of ours), and the same grip while it is away DRAWS
//     it onto the arm of the hand that reached (the right hand: strapped to the right forearm, mirrored). The rule above then holds again at once: with FOLLOW, a shield drawn while the
//     weapon is in the other hand sits opposite it. Without STOW the shield is never put away: it is always on one forearm.
// Nothing is pressed in the game and no slot changes.
#pragma once

#include <cmath>

#include "shoulder_draw.h"
#include "weapon_back.h"

namespace shieldarm {

// The arm the shield is on: the hand sides of vr/skeletons.h (0 right, 1 left).
enum class Arm : int { Right = 0, Left = 1 };

enum class Event : int { None, PutAway, Drawn };

struct HandIn {
    bool tracked = false;
    bool press = false;      // the grip was pressed in this step (an edge)
    bool atShield = false;   // the hand is behind the LEFT shoulder (the shield's)
    bool busy = false;       // holds the weapon, or a belt potion: it neither puts the shield away nor draws it
    bool release = false;    // the grip was let go in this step, after the hand had held it free of weapon and potion (the wire: carried the shield with it, anywhere)
};

struct In {
    bool shield = false;     // the game's left slot holds a shield
    bool follow = true;      // FOLLOW: weapon and shield on opposite arms
    bool stow = false;       // STOW: the shield can be put away at the left shoulder and drawn from it
    unsigned hands = 0;      // the hands that hold the weapon or that it flies into, a bit each (1 right, 2 left); 0 when it is let go, lying, falling, none
    HandIn hand[2];          // 0 right, 1 left
};

struct Out {
    Arm arm = Arm::Left;     // the arm it is on - or was on, while it is put away
    bool stowed = false;     // put away: not drawn
    bool changed = false;    // it moved to the other arm in this step (the follow rule)
    Event event = Event::None;
    int by = -1;             // the hand that put it away or drew it
    bool byRelease = false;  // it was put away by letting go of the grip behind the shoulder (carried there), not by squeezing it there
};

class Follower {
public:
    void Reset() { arm_ = Arm::Left; stowed_ = false; }
    Arm Now() const { return arm_; }
    bool Stowed() const { return stowed_; }

    Out Step(const In& in) {
        Out o;
        const Arm was = arm_;
        if (!in.shield) {   // no shield (a new one next time): it starts on the left arm
            arm_ = Arm::Left;
            stowed_ = false;
        } else {
            if (!in.stow) stowed_ = false;   // the gesture is off: the shield is back wherever it was
            if (in.stow) {
                for (int h = 0; h < 2; ++h) {
                    const HandIn& hi = in.hand[h];
                    if (!hi.tracked || hi.busy || !hi.atShield) continue;
                    // the hand that WEARS the shield carries it behind the shoulder with its grip held (as a weapon is carried to the back) and lets go there: put away. A squeeze there by a hand
                    // that holds nothing does it too (the old way), and draws it when it is away - a draw is a squeeze, never a release.
                    const bool wears = !stowed_ && arm_ == (h == 0 ? Arm::Right : Arm::Left);
                    const bool viaRelease = hi.release && wears;
                    if (!hi.press && !viaRelease) continue;
                    if (stowed_) {
                        stowed_ = false;
                        arm_ = h == 0 ? Arm::Right : Arm::Left;   // onto the arm that reached
                        o.event = Event::Drawn;
                    } else {
                        stowed_ = true;
                        o.event = Event::PutAway;
                        o.byRelease = !hi.press;
                    }
                    o.by = h;
                    break;   // one change in a step
                }
            }
            // the weapon is in the shield's hand ALONE: the shield goes to the other forearm (a shield put away has no arm to leave)
            if (in.follow && !stowed_ && in.hands == (1u << (unsigned)arm_)) arm_ = arm_ == Arm::Right ? Arm::Left : Arm::Right;
        }
        o.arm = arm_;
        o.stowed = stowed_;
        o.changed = arm_ != was && o.event != Event::Drawn;
        return o;
    }

    // shield: the game's left slot holds a shield. hands: as In::hands. (The follow rule alone.)
    Out Step(bool shield, unsigned hands) {
        In in;
        in.shield = shield;
        in.hands = hands;
        return Step(in);
    }

private:
    Arm arm_ = Arm::Left;
    bool stowed_ = false;
};

}  // namespace shieldarm

// The shield on the right forearm: the left arm's strap, mirrored. Row-vector matrices as vr/skeletons.cpp has them (v' = v * M), model space (the hero faces +z, his
// left is +x; the bind pose stands straight).
//
// The shield's attach bone is the game's LEFT one. On the left forearm its matrix is  bind[attach] * inv(bind[forearm]) * forearm(now)  (StrapShield): the shield as the
// rig was made, carried by the forearm. On the right one the same shield is its REFLECTION in the body's plane, carried by the right forearm the same way:
//     bind[attach] * Reflect * inv(bind[right forearm]) * right forearm(now)
// - nothing of the bones' own axes is needed, only that the rig's two forearms are each other's mirror images in the bind pose (they are: it is a symmetric body).
//
// The game draws what hangs on the left attach bone through a mirrored matrix (det < 0: an item put there with a mirror-less one is drawn inside out - skeletons.cpp
// attachFor). The reflection has the opposite sign, so one local axis of the matrix is negated again: it must be a symmetry axis of the shield, or the shield would be
// turned inside out. The shield's Y is its long axis (the smite capsule runs along it); of its other two axes the one that points from the forearm out to the shield
// (its face) must stay, and the one across the plate (the shield's left-right) is negated - FaceAxes finds which from the bind pose.
namespace shieldmirror {

using weaponback::Mat;
using weaponback::V3;

// The reflection in the plane x = x0 (row vectors).
inline Mat Reflect(float x0) {
    Mat r{};
    r.m[0] = -1.0f; r.m[5] = 1.0f; r.m[10] = 1.0f; r.m[15] = 1.0f;
    r.m[12] = 2.0f * x0;
    return r;
}

// The plane between the two forearms in the bind pose.
inline float PlaneX(const Mat& bindLeftForearm, const Mat& bindRightForearm) { return 0.5f * (bindLeftForearm.m[12] + bindRightForearm.m[12]); }

struct Axes {
    bool known = false;
    int normalRow = 2;   // the row (0 or 2) that points out of the forearm to the shield: kept
    int acrossRow = 0;   // the row across the plate: negated for the handedness
};

// Which of the shield bone's rows 0 and 2 is its face, from the bind pose: the face points from the forearm's axis (elbow -> wrist) toward the shield's origin, the plate's
// middle. A shield whose origin lies on the forearm's axis tells nothing (known = false: row 0 is across, as the weapons' other bone has it).
inline Axes FaceAxes(const Mat& bindShield, V3 elbow, V3 wrist) {
    Axes a;
    const V3 axis = weaponback::Norm(weaponback::Add(wrist, weaponback::Mul(elbow, -1.0f)));
    const V3 o = weaponback::Add(weaponback::Pos(bindShield), weaponback::Mul(elbow, -1.0f));
    const V3 out = weaponback::Add(o, weaponback::Mul(axis, -weaponback::Dot(o, axis)));   // the offset from the forearm's axis
    const float len = weaponback::Len(out);
    if (!(len > 1e-4f) || !weaponback::Finite(out)) return a;
    const V3 n = weaponback::Mul(out, 1.0f / len);
    const float d0 = std::fabs(weaponback::Dot(n, weaponback::Norm(weaponback::Row(bindShield, 0))));
    const float d2 = std::fabs(weaponback::Dot(n, weaponback::Norm(weaponback::Row(bindShield, 2))));
    if (std::fabs(d0 - d2) < 0.15f) return a;   // as near to one as to the other: not told
    a.known = true;
    a.normalRow = d0 > d2 ? 0 : 2;
    a.acrossRow = d0 > d2 ? 2 : 0;
    return a;
}

// The user's corrections of the left shield ([shield] x y z in cm, then pitch yaw roll in degrees, about the attach bone's own axes 0 1 2), as the same correction of the
// mirrored shield on the right forearm: a displacement along an axis is negated for the axis across the plate (that axis was reflected twice), a rotation is negated about
// every axis but that one (a reflection turns a rotation round).
inline void MirrorAdjust(const float a[6], int acrossRow, float out[6]) {
    for (int i = 0; i < 3; ++i) {
        out[i] = i == acrossRow ? -a[i] : a[i];
        out[3 + i] = i == acrossRow ? a[3 + i] : -a[3 + i];
    }
}

// The sign of a 3x3's determinant: +1, -1, 0 for a squashed one.
inline int DetSign(const Mat& a) {
    const float d = weaponback::Det3(a);
    return d > 1e-12f ? 1 : d < -1e-12f ? -1 : 0;
}

// The shield's attach matrix on the right forearm: the reflected bind shield carried by the right forearm as it is now (`armNow`, with whatever twist the caller put
// on it), with row `acrossRow` negated when that is what makes its handedness `wantSign` (the left attach bone's own: -1). False for numbers that are not usable.
inline bool RightArmMatrix(const Mat& bindShield, const Mat& bindRightForearm, const Mat& armNow, float planeX, int acrossRow, int wantSign, Mat* out) {
    *out = bindShield;
    if (acrossRow != 0 && acrossRow != 2) return false;
    const Mat reflected = weaponback::Mul(bindShield, Reflect(planeX));
    const Mat carried = weaponback::Mul(weaponback::Mul(reflected, weaponback::InvAffine(bindRightForearm)), armNow);
    Mat m = carried;
    const int got = DetSign(m);
    if (got == 0) return false;
    if (wantSign != 0 && got != wantSign)
        for (int c = 0; c < 3; ++c) m.m[acrossRow * 4 + c] = -m.m[acrossRow * 4 + c];
    *out = m;
    return weaponback::Finite(weaponback::Pos(m));
}

}  // namespace shieldmirror
