// aim_source.h - where a staff's aim comes from (vrcam's StaffAim / HandRay). Pure.
//
// The aim of a spell or an attack must come from the CONTROLLERS - the real hands - never from the weapon as it is drawn: with [weight] on the weapon
// is a body of its own that lags, sags and slides between the hands, and none of that may move where the spells go.
//
//   a staff, [hands] staff_free_left on (the right hand holds it, the left takes it with the grip):
//     [weight] off  - the shaft as the skeleton hook posed it (skel::StaffAxis): the staff's own axis, turned toward the left controller while that hand
//                     is on it.  Drawn.
//     [weight] on   - the skeleton hook no longer poses the staff that way (the weight model holds it), so that axis is not made: it went stale after
//                     0.3 s, the staff's aim was dropped and the spells went where the plain hand points (the user, 2026-10-14). Now:
//                     both hands on it, both tracked: the line from the left controller to the right one (LeftToRight);
//                     one hand on it: the shaft as that hand grips it, in its controller's frame (Shaft) - fixed by the grip, so the sag and the lag of the
//                     drawn weapon cannot move it;
//                     no hand on it (dropped, lying, on the back): the staff is not what aims (the plain hand's direction).
//   a staff, [bow] staff_two_hands on (and staff_free_left off), or a crossbow held the staff's way: the line from the left controller to the right one
//     whenever both are tracked, whatever the weight model does.
//   anything else: not a staff's aim (the aiming hand's own direction, as for every weapon).
#pragma once

namespace aimsource {

enum class Src { None, LeftToRight, Shaft, Drawn };

struct In {
    bool fullBody = false;          // F4 with the body on the controllers
    bool sharedOk = false;          // the controllers' block is valid
    unsigned handsValid = 0;        // bit 0 the right controller, bit 1 the left
    bool staffWeapon = false;       // a staff is in hand
    bool xbowLike = false;          // a crossbow held the staff's way ([bow] xbow_two_hands)
    bool staffTwoHands = false;     // [bow] staff_two_hands
    bool staffFreeLeft = false;     // [hands] staff_free_left
    bool armsOnControllers = false; // the arms mode where the controllers hold the weapon (2)
    bool drawnFresh = false;        // the skeleton hook's staff axis is fresh (skel::StaffAxis)
    bool weightHas = false;         // [weight] has the weapon (in a hand or not)
    bool holds[2] = {};             // [weight]: that hand holds it
    int carrying = -1;              // [weight]: the hand that carries it, -1 none
};

struct Out {
    bool aims = false;      // the staff aims (else the aiming hand's own direction)
    Src src = Src::None;
    int hand = 0;           // the controller the aim starts at (and, for Shaft, whose frame the shaft is given in)
    bool leftOn = false;    // both hands are on it (the finer range)
    const char* why = "";
};

inline Out Choose(const In& in) {
    Out o;
    if (!in.fullBody) { o.why = "not in the body (F4): the aiming hand's own direction"; return o; }
    const bool staffNow = (in.staffTwoHands || in.staffFreeLeft) && in.staffWeapon;
    if (!(staffNow || in.xbowLike)) { o.why = "no staff in hand: the aiming hand's own direction"; return o; }
    if (!in.sharedOk) { o.why = "no controllers' block"; return o; }
    if (in.staffFreeLeft && in.armsOnControllers) {
        if (in.weightHas) {
            const bool right = (in.handsValid & 1u) != 0, left = (in.handsValid & 2u) != 0;
            if (in.holds[0] && in.holds[1] && right && left) {
                o.aims = true; o.src = Src::LeftToRight; o.hand = 0; o.leftOn = true;
                o.why = "[weight] on, both hands on the staff: from the left controller to the right one";
            } else if (in.carrying >= 0 && in.carrying <= 1 && (in.handsValid & (1u << in.carrying))) {
                o.aims = true; o.src = Src::Shaft; o.hand = in.carrying; o.leftOn = false;
                o.why = "[weight] on, one hand on the staff: along the shaft as that controller grips it";
            } else {
                o.why = "[weight] on, no hand holds the staff (dropped, lying, on the back): the aiming hand's own direction";
            }
            return o;
        }
        if ((in.handsValid & 1u) && in.drawnFresh) {
            o.aims = true; o.src = Src::Drawn; o.hand = 0; o.leftOn = true;   // (leftOn is read from the hook's own answer by the caller)
            o.why = "[weight] off: the staff's axis as the skeleton hook posed it";
        } else {
            o.why = "[weight] off, no fresh staff axis from the skeleton hook: the aiming hand's own direction";
        }
        return o;
    }
    if ((in.handsValid & 3u) == 3u) {
        o.aims = true; o.src = Src::LeftToRight; o.hand = 0; o.leftOn = true;
        o.why = "both controllers: from the left one to the right one";
    } else {
        o.why = "a controller is not tracked: the aiming hand's own direction";
    }
    return o;
}

// A direction given in a controller's own frame, turned by its orientation q (x y z w) into the frame the controllers come in (v' = q v q*).
inline void TurnBy(const float q[4], const float v[3], float out[3]) {
    const float tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

}  // namespace aimsource
