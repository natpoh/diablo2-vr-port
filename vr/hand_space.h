#pragma once
// hand_space.h - where a controller's grip is taken from when the runtime's locate of it in the room (STAGE) is no whole pose. Pure: no
// OpenXR, no Windows (cleanroom/tests/hand_space_test.cpp).
//
// A Quest 2 player over Link, Oculus runtime 1.115, 0.183 (2026-10-26: "my hands were bound to my waist again", the wrist orbs gone):
// with no hand trackers made ([openxr] hand_trackers=2) both grips, located in the STAGE space, came back P--r for the whole run - the
// place valid, the turn "tracked" yet not valid, flags no runtime should give together - the session focused, the actions active, Touch
// bound. The pose was dropped (a hand needs a valid turn), the arms stayed at the belt and the orbs hung on the hands had no hand. In the
// same frames the aim pose located in LOCAL was whole (the laser, which locates it there, played the menus and never said "the laser
// hand is not tracked") and the head located in STAGE was whole (crouching worked). 0.165 - 0.175 with the trackers: right ----, left
// --Rr / -pR- / Pp--, the same kind of broken set. A second run with his Quest's hand tracking off (18:31): both grips P--- all the run
// (a place, no turn at all), the laser again whole, no focus lost. "The hands moved at the start" was the menus' laser (the aim); in
// the game the arms ride the grip.
//
// So on Meta's runtime, when the grip in the room is no whole pose, the hand is looked for in this order:
//   Room      the grip in the room, whole (valid turn and place) and sane (no TRACKED bit without its VALID bit) - as before
//   Local     the grip located in LOCAL, whole and sane, carried into the room by where LOCAL is in the room (the same frame or the last good)
//   AimLearned the aim (in the room, or LOCAL carried), with the grip's offset from it learned in frames where both were whole - the
//             runtime's own poses, no number of ours
//   Repaired  the room's own locate with each TRACKED bit read as its VALID bit too, if the place it gives is near the aim's (or there is
//             no aim to hold it against): the data was there, only a bit missing
//   AimRaw    the aim as it is, no learned offset - the hand at the controller's pointer rather than at the belt
//   AsBefore  nothing whole: the room's locate as before (a turn alone, held by the head, or no hand)
//
// [openxr] hand_space: 2 (Auto, the default) = on Meta's runtime only; every other runtime as before (SteamVR / PS VR2 / Virtual
// Desktop give whole grips - they never get here). 1 = on any runtime; 0 = never (0.183).

#include <cmath>
#include <cstdint>

#include "hand_trackers.h"   // MetaRuntime: the runtime's name

namespace handspace {

enum class Mode { Never = 0, Always = 1, Auto = 2 };
inline Mode ModeOf(int v) { return v <= 0 ? Mode::Never : v == 1 ? Mode::Always : Mode::Auto; }
inline bool On(Mode m, const char* runtimeName) {
    return m == Mode::Always || (m == Mode::Auto && handtrackers::MetaRuntime(runtimeName));
}

// XrSpaceLocationFlagBits, as OpenXR has them (xr.cpp checks they match)
constexpr uint64_t kOriValid = 0x1, kPosValid = 0x2, kOriTracked = 0x4, kPosTracked = 0x8;

// a TRACKED bit without its VALID bit: not a set a runtime should give
inline bool Sane(uint64_t f) { return !((f & kOriTracked) && !(f & kOriValid)) && !((f & kPosTracked) && !(f & kPosValid)); }
// a turn and a place to use, and sane
inline bool Whole(uint64_t f) { return (f & kOriValid) && (f & kPosValid) && Sane(f); }
// each TRACKED bit read as its VALID bit too
inline uint64_t Repair(uint64_t f) { return f | ((f & kOriTracked) ? kOriValid : 0) | ((f & kPosTracked) ? kPosValid : 0); }

enum class Source { Room, Local, AimLearned, Repaired, AimRaw, AsBefore };

struct Facts {
    uint64_t room = 0;          // the grip in the room: its flags
    bool localKnown = false;    // where LOCAL is in the room is known (this frame or the last good)
    uint64_t local = 0;         // the grip in LOCAL: its flags (0 = not looked at)
    bool aim = false;           // the aim is whole this frame (in the room, or LOCAL carried)
    bool learned = false;       // the grip's offset from the aim is learned for this hand
    bool roomNumbers = false;   // the room's locate gave numbers to use: a turn that can be made unit length, a finite place
    bool repairedNearAim = false;   // ... and that place is within kNearM of the aim's
};

constexpr float kNearM = 0.3f;   // one controller: its grip and its pointer this close at most (a wrong place is metres off, or at 0)

inline Source Pick(const Facts& f) {
    if (Whole(f.room)) return Source::Room;
    if (f.localKnown && Whole(f.local)) return Source::Local;
    if (f.aim && f.learned) return Source::AimLearned;
    const bool repairable = f.roomNumbers && Whole(Repair(f.room));
    if (repairable && (!f.aim || f.repairedNearAim)) return Source::Repaired;
    if (f.aim) return Source::AimRaw;
    return Source::AsBefore;
}

inline const char* Name(Source s) {
    switch (s) {
        case Source::Room: return "the grip in the room, as the runtime gave it";
        case Source::Local: return "the grip located in LOCAL, carried into the room";
        case Source::AimLearned: return "the aim pose with the grip's offset learned from it";
        case Source::Repaired: return "the room's grip with its TRACKED bits read as VALID";
        case Source::AimRaw: return "the aim pose as it is (no grip offset learned yet)";
        default: return "nothing whole - as before";
    }
}

// -- poses: turn (x, y, z, w) and place --------------------------------------
struct Pose { float q[4] = {0.0f, 0.0f, 0.0f, 1.0f}; float p[3] = {}; };

inline void QMul(const float* a, const float* b, float* o) {   // b first, then a
    const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    o[0] = x; o[1] = y; o[2] = z; o[3] = w;
}
inline void QRot(const float* q, const float* v, float* o) {
    const float u[3] = {q[1] * v[2] - q[2] * v[1] + q[3] * v[0], q[2] * v[0] - q[0] * v[2] + q[3] * v[1], q[0] * v[1] - q[1] * v[0] + q[3] * v[2]};
    const float r[3] = {v[0] + 2.0f * (q[1] * u[2] - q[2] * u[1]), v[1] + 2.0f * (q[2] * u[0] - q[0] * u[2]), v[2] + 2.0f * (q[0] * u[1] - q[1] * u[0])};
    o[0] = r[0]; o[1] = r[1]; o[2] = r[2];
}
// b given in a's frame, put in a's parent: a then b
inline Pose Then(const Pose& a, const Pose& b) {
    Pose o;
    QMul(a.q, b.q, o.q);
    QRot(a.q, b.p, o.p);
    o.p[0] += a.p[0]; o.p[1] += a.p[1]; o.p[2] += a.p[2];
    return o;
}
inline Pose Inverse(const Pose& a) {
    Pose o;
    o.q[0] = -a.q[0]; o.q[1] = -a.q[1]; o.q[2] = -a.q[2]; o.q[3] = a.q[3];
    const float m[3] = {-a.p[0], -a.p[1], -a.p[2]};
    QRot(o.q, m, o.p);
    return o;
}
// b in a's frame (both in one space)
inline Pose Relative(const Pose& a, const Pose& b) { return Then(Inverse(a), b); }
// a turn of unit length (false: not one at all)
inline bool Normalize(float* q) {
    const float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (!std::isfinite(n2) || n2 < 0.5f || n2 > 2.0f) return false;
    const float k = 1.0f / std::sqrt(n2);
    for (int i = 0; i < 4; ++i) q[i] *= k;
    return true;
}
inline float Distance(const float* a, const float* b) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

}  // namespace handspace
