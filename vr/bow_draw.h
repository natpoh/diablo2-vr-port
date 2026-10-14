#pragma once
// bow_draw.h - the two-hand bow draw as a gesture: pure logic, no game calls, no globals.
//
// A hand holds the bow, the other takes the string: its trigger down at the nock point, pulled back
// along the line to the bow, and let go. The release is ONE short press of the game's own key for
// the selected skill (the basic attack or an Amazon arrow skill), aimed along the draw line. The game
// does the rest - its animation, its attack rate, its mana and its arrows. The draw adds NOTHING:
// no damage, no extra arrows, no speed (the length of the pull only feeds the feel: Output::draw).
//
// The thresholds and the shape of the gesture - the nock 0.28 m round a point 0.16 m behind the bow
// hand, the pull of 0.28 m and 0.12 m more than at the nock held for 0.12 s, the line against the
// bow within dot 0.15, the cancel at 1.10 m, the 65 ms press, trigger 0.55 on / 0.20 off - are the
// idea of bearstonem/d2r-pcvr (MIT; steamvr/gameplay.h Engine::Step, weapon_skills.h): the code
// here is written fresh, to the same numbers.
//
// The host (vr/bow_draw_wire.cpp) steps one Detector per frame with both hands' places in ONE frame
// of any kind (metres, +Y up: the room, the head's turn - the answer comes back in it) and acts on
// the Output: the plain trigger is held back while `owned`, and a `shoot` presses the key.

#include <cmath>
#include <cstdint>

namespace bowdraw {

struct Vec { float x = 0, y = 0, z = 0; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };

inline Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec operator*(Vec a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float Length(Vec a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(Vec a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline Vec Normalized(Vec a) {
    const float l = Length(a);
    return l > 1e-6f ? a * (1.0f / l) : Vec{};
}
inline Vec Rotate(Quat q, Vec v) {   // q v q*
    const float tx = 2 * (q.y * v.z - q.z * v.y), ty = 2 * (q.z * v.x - q.x * v.z), tz = 2 * (q.x * v.y - q.y * v.x);
    return {v.x + q.w * tx + (q.y * tz - q.z * ty), v.y + q.w * ty + (q.z * tx - q.x * tz), v.z + q.w * tz + (q.x * ty - q.y * tx)};
}

// The way the bow points, from the bow hand's grip: the knuckles (-Y) through the bow, the controller's
// forward (-Z) when the knuckles point up or down - the convention the hand aim already has
// (vr/vrcam.cpp HandRay: a bow's upright grip or a flat one).
inline Vec BowForward(Quat grip) {
    const Vec knuckles = Rotate(grip, {0, -1, 0});
    return Normalized(std::fabs(knuckles.y) < 0.7f ? knuckles : Rotate(grip, {0, 0, -1}));
}

// The head's turn about the vertical, + to the left, from its orientation in the room (x right, y up, z back): the frame
// D2RVR_Shared's hands are in is the room's turned by this (vr/vrcam.cpp XrIntoShared). A direction in the room into that frame:
// the head's own forward comes out as (0, 0, -1).
inline float HeadYaw(Quat head) {
    const Vec f = Rotate(head, {0.0f, 0.0f, -1.0f});
    return std::atan2(-f.x, -f.z);
}
inline Vec ToHeadTurn(Vec room, float yaw) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {room.x * c - room.z * s, room.y, room.x * s + room.z * c};
}

// What a selected skill does with a drawn bow. Basic: the game's attack. Arrow: Magic, Fire, Cold, Ice,
// Exploding, Immolation, Freezing, Guided Arrow and Multiple Shot (the fan is the game's). Volley:
// Strafe, its own volley. Everything else keeps the trigger as it is.
enum class SkillKind { Native, Basic, Arrow, Volley };
inline constexpr int kAttackSkill = 0, kStrafeSkill = 26;
inline SkillKind Classify(int skill) {
    switch (skill) {
        case kAttackSkill: return SkillKind::Basic;
        case 6: case 7: case 11: case 12: case 16: case 21: case 22: case 27: case 31: return SkillKind::Arrow;
        case kStrafeSkill: return SkillKind::Volley;
        default: return SkillKind::Native;
    }
}
inline bool DrawsWith(int skill) { return Classify(skill) != SkillKind::Native; }

// What takes the string: the trigger (the first way), the grip (the natural grab), or whichever is pressed.
enum Start { kStartTrigger = 0, kStartGrip = 1, kStartEither = 2 };
inline int ClampStart(int v) { return v < kStartTrigger || v > kStartEither ? kStartTrigger : v; }
inline const char* StartName(int v) { return v == kStartGrip ? "grip" : v == kStartEither ? "trigger or grip" : "trigger"; }

struct Params {
    float triggerOn = 0.55f, triggerOff = 0.20f;   // the string hand's trigger (hysteresis: a small squeeze change does not let go)
    float gripOn = 0.60f, gripOff = 0.35f;         // and its grip, where the grip takes the string
    int start = 0;                                 // what takes the string: kStartTrigger, kStartGrip or kStartEither
    float reach = 0.28f;                           // the string hand to the nock point, to take the string
    float nockBehind = 0.16f;                      // the nock point behind the bow hand, along the bow
    // The pull that makes a draw ([hands] bow_draw_min_pull_cm): the string hand pulled back this far (minPull) from where it took the
    // string, with the hands at least pullFloor + minPull apart - 0.28 m and 0.12 m at the default, as it always was - for this long:
    float minPull = 0.12f;
    float pullFloor = 0.16f;
    float chargeSeconds = 0.12f;                   // ... held this long: drawn
    float minAlign = 0.15f;                        // the draw line against the bow: a shot only above this
    float cancelGap = 1.10f;                       // a pull this long is not a draw
    float pressSeconds = 0.065f;                   // the one press of the game's key
    float aimSeconds = 0.40f;                      // the line stays the aim this long after a shot
    float maxStep = 0.10f;                         // a frame this long after the last is a hitch: a draw does not go on over it
    float maxJump = 0.35f;                         // a hand this far from where it was a frame ago: a recentre or a lost pose
    float lossSeconds = 0.25f;                     // a hand lost this long or less (a controller out of the cameras' view) does not end a draw
};

// The draw amount (Output::draw, 0..1) under which the bow rig (vr/bow_rig.h ShortPull) reads a pull as one that came to nothing.
inline constexpr float kRigShortPull = 0.25f;

// What of the string hand's controls the draw keeps from the layout this frame (directinput::Take), and what it only strips of its shots.
// The control that took the string is the draw's while it is held: kept in full - nothing bound to it fires. With "a press away from the
// string shoots nothing" ([hands] bow_draw_only), while a bow is wielded and the draw may act (`allowed`: F4, focused, no menu or panel up):
//   - the trigger, if it can take the string (`start` is not the grip alone), is kept in full while the hand is in the grab zone
//     (`atString`) - the press there is the draw's, so nothing the table binds to that trigger fires - and, once a draw is under way, whatever
//     took it;
//   - everywhere else the trigger and the grip stay the layout's, but their SHOTS are dropped (noShot*: action:attack, pad A, RT, whichever
//     of the control's plain, chord or hold lines carries one) and every other output they are bound to works as bound: a pad skill button, a
//     key command, any other action, the belt, the shoulder swap, the wheel.
// The grip's press at the string needs no zone test: the draw owns it from its first frame (it is `owned` with hold 1 in the same frame).
struct Keep { bool trigger = false, grip = false, noShotTrigger = false, noShotGrip = false; };
inline Keep KeepFromLayout(bool allowed, bool drawOnly, bool owned, int hold, bool atString = false, int start = kStartTrigger) {
    Keep k;
    k.grip = owned && hold == 1;
    const bool zone = allowed && drawOnly && atString && start != kStartGrip;
    k.trigger = (owned && hold != 1) || (allowed && drawOnly && owned) || zone;
    k.noShotTrigger = allowed && drawOnly && !k.trigger;
    k.noShotGrip = allowed && drawOnly && !k.grip;
    return k;
}

// Why a draw ended with no shot (Output::why, with evCancel).
enum Why { kWhyNone = 0, kWhyNotAllowed, kWhyTracking, kWhyEvent, kWhyHitch, kWhyJump, kWhyContext, kWhyTooLong };
inline const char* WhyName(int w) {
    switch (w) {
        case kWhyNotAllowed: return "not allowed (a menu, the focus, the weapon)";
        case kWhyTracking: return "tracking lost";
        case kWhyEvent: return "an event (a recentre, the focus or a menu changing)";
        case kWhyHitch: return "a hitch between two frames";
        case kWhyJump: return "a hand jumped";
        case kWhyContext: return "the weapon, set or skill changed";
        case kWhyTooLong: return "a pull too long";
        default: return "";
    }
}

struct Input {
    double time = 0;        // seconds, steady
    bool allowed = false;   // a bow in hand, a skill that draws, no menu, the session focused: the host's word
    bool valid = false;     // both hands tracked
    bool cancel = false;    // an event that ends a draw without a shot: a recentre, a swap of weapons
    Vec bowHand;            // the hand with the bow: grip place and turn
    Quat bowRot;
    Vec stringHand;         // the other one
    float trigger = 0;      // the string hand's, 0..1
    float grip = 0;         // and its grip (squeeze), 0..1: it starts a draw only with Params::start asking for it
    bool hasNock = false;   // the host knows where the string really is (the bow's own model): the nock point is Input::nock
    Vec nock;               //   in the same frame as the hands; else a point nockBehind behind the bow hand
    uint64_t context = 0;   // anything that must stay the same through a draw (weapon, equipment, skill): a change ends it
};

struct Output {
    bool owned = false;     // the control that took the string belongs to the draw: the plain trigger (the attack) is held back
    int hold = -1;          // which one while owned: 0 the trigger, 1 the grip
    int phase = 0;          // Phase, for the log
    bool atString = false;  // the string hand is within reach of the nock point (feel: "string here")
    bool drawing = false;   // nocked, the trigger down
    bool charged = false;   // drawn far enough
    bool shoot = false;     // the frame of a release that shoots
    bool pressing = false;  // from the shoot frame for pressSeconds: the game's key is down
    bool aimOwned = false;  // `aim` is the line to shoot along: while drawing, and aimSeconds after a shot
    Vec aim;                // unit, from the string hand through the bow hand
    Vec origin;             // the string hand's place (where the line starts)
    Vec bowForward;
    Vec nock;               // the nock point
    float draw = 0;         // 0..1, the pull beyond the nock over 0.45 m - for feel only
    bool evNear = false, evNock = false, evCharged = false, evCancel = false;   // the frame each starts (haptics, a sound)
    bool evLoose = false;   // the frame a taken string was let go of and nothing was shot (a pull too short, too quick, across the bow)
    float pullNeed = 0;     // the hands' gap from which a release shoots (a sphere round the bow hand of this radius: the string hand
                            //   outside it has pulled far enough); the nock's gap + the minimum pull while the string is taken, else
                            //   the least it can be. Set when the string is near or taken
    bool pullEnough = false;   // the hands are that far apart now (the time it has to be held for aside): `charged` has both
    int why = kWhyNone;     // with evCancel (and on every frame of an ended draw's first): why it ended with no shot
    bool lost = false;      // a hand is lost for the moment and the draw is held as it was (up to Params::lossSeconds)
};

// Idle: nothing held. Plain: a control that could take the string is down and is not the draw's (pressed away from the string,
// or already down when the draw came back): the host passes it on as it always did. Nocked: the string is taken. Blocked: a draw
// that ended with no shot - the control is still the draw's, and held back, until it is let go.
enum PhaseId { kIdle = 0, kPlain = 1, kNocked = 2, kBlocked = 3 };
inline const char* PhaseName(int p) {
    return p == kPlain ? "plain trigger" : p == kNocked ? "string taken" : p == kBlocked ? "ended, wait for release" : "idle";
}

class Detector {
    enum class Phase { Idle, Plain, Nocked, Blocked };
    Phase phase_ = Phase::Idle;
    int hold_ = 0;   // 0 the trigger took the string, 1 the grip
    bool down_ = false, dT_ = false, dG_ = false, dTWas_ = false, dGWas_ = false, seen_ = false, atString_ = false, charged_ = false, have_ = false;
    double last_ = 0, nockedAt_ = 0, pressUntil_ = 0, aimUntil_ = 0, goodAt_ = 0;
    float nockGap_ = 0;
    uint64_t context_ = 0;
    Vec prevBow_, prevString_, shotAim_, shotOrigin_;
    Output held_;          // the last frame of a good draw: what a short loss of a hand repeats
    bool resume_ = false;  // the first frame back after such a loss: the hands may have moved a long way meanwhile

    // The draw ends with no shot; what the trigger still holds stays the draw's until it is let go.
    void Break() {
        if (phase_ == Phase::Nocked) phase_ = Phase::Blocked;
        else if (phase_ == Phase::Plain) phase_ = Phase::Idle;
        charged_ = atString_ = false;
        pressUntil_ = aimUntil_ = 0;
        goodAt_ = 0;
        resume_ = false;
    }

public:
    Output Step(const Input& in, const Params& p = Params{}) {
        Output out;
        // The controls are read whenever they are there, tracked or not: a draw is let go of by letting its control go.
        const bool enT = p.start != kStartGrip, enG = p.start != kStartTrigger;
        if (std::isfinite(in.trigger)) dT_ = in.trigger > (dT_ ? p.triggerOff : p.triggerOn);
        if (std::isfinite(in.grip)) dG_ = in.grip > (dG_ ? p.gripOff : p.gripOn);
        // a control already down at the first look is nobody's fresh press
        const bool edgeT = seen_ && enT && dT_ && !dTWas_, edgeG = seen_ && enG && dG_ && !dGWas_;
        const bool edge = edgeT || edgeG;
        dTWas_ = dT_; dGWas_ = dG_;
        seen_ = true;
        // What counts as held: the control that took the string while it is the draw's, else any that may take it.
        down_ = phase_ == Phase::Nocked || phase_ == Phase::Blocked ? (hold_ == 1 ? dG_ : dT_) : (enT && dT_) || (enG && dG_);

        const bool pose = in.valid && std::isfinite(in.time) && Finite(in.bowHand) && Finite(in.stringHand) &&
                          std::isfinite(in.bowRot.x) && std::isfinite(in.bowRot.y) && std::isfinite(in.bowRot.z) && std::isfinite(in.bowRot.w);
        // A hand lost for a moment (a controller out of the cameras' view, which the trigger of a lost controller reads as let go): the
        // draw is held as the last good frame had it - the string where it was - for lossSeconds. A hand back inside that goes on
        // with the draw; one that is not ends it ("tracking lost"). Nothing is shot over a loss.
        if (phase_ == Phase::Nocked && !pose && in.allowed && !in.cancel && std::isfinite(in.time) && goodAt_ > 0 && in.time >= goodAt_ &&
            in.time - goodAt_ <= p.lossSeconds && in.context == context_) {
            Output o = held_;
            o.shoot = o.pressing = false;
            o.evNear = o.evNock = o.evCharged = o.evCancel = false;
            o.lost = true;
            o.why = kWhyNone;
            resume_ = true;
            last_ = in.time;
            return o;
        }
        // Out of the draw's reach: the host says no, a pose is lost, an event (a recentre, a swap of weapons), a hitch, a hand that
        // jumped, or a weapon or skill that changed under the string.
        int why = !in.allowed ? kWhyNotAllowed : in.cancel ? kWhyEvent : !pose ? kWhyTracking : kWhyNone;
        bool broken = why != kWhyNone;
        if (!broken && have_) {
            const double dt = in.time - last_;
            const float jump = resume_ ? p.maxJump * 3.0f : p.maxJump;
            if (dt <= 0 || dt > p.maxStep) why = kWhyHitch;
            else if (Length(in.bowHand - prevBow_) > jump || Length(in.stringHand - prevString_) > jump) why = kWhyJump;
            else if (phase_ == Phase::Nocked && in.context != context_) why = kWhyContext;
            broken = why != kWhyNone;
        }
        if (broken) {
            out.evCancel = phase_ == Phase::Nocked;
            if (out.evCancel) out.why = why;
            Break();
            have_ = pose && in.allowed;   // the next frame is judged against this one
            if (have_) { prevBow_ = in.bowHand; prevString_ = in.stringHand; last_ = in.time; }
            if (!down_) phase_ = Phase::Idle;
            out.owned = phase_ == Phase::Blocked;
            out.hold = out.owned ? hold_ : -1;
            out.phase = (int)phase_;
            return out;
        }
        have_ = true;
        resume_ = false;
        prevBow_ = in.bowHand; prevString_ = in.stringHand; last_ = in.time;

        out.bowForward = BowForward(in.bowRot);
        out.nock = in.hasNock ? in.nock : in.bowHand - out.bowForward * p.nockBehind;
        const Vec line = in.bowHand - in.stringHand;   // from the string hand through the bow hand: the way the arrow goes
        const float gap = Length(line);
        out.atString = Length(in.stringHand - out.nock) < p.reach;
        out.evNear = out.atString && !atString_ && phase_ != Phase::Nocked;
        atString_ = out.atString;
        // the hands' gap a release shoots from: never under the floor (in double: 0.16 + 0.12 is the 0.28 of before to the last bit),
        // and with the string taken the nock's gap plus the minimum pull
        const float apart = (float)((double)p.pullFloor + (double)p.minPull);
        const auto drawn = [&] { return gap >= apart && gap - nockGap_ >= p.minPull && in.time - nockedAt_ >= p.chargeSeconds; };

        if (!down_) {
            if (phase_ == Phase::Nocked && drawn() && gap > 1e-4f && Dot(line * (1.0f / gap), out.bowForward) > p.minAlign) {
                out.shoot = true;
                pressUntil_ = in.time + p.pressSeconds;
                aimUntil_ = in.time + p.aimSeconds;
                shotAim_ = line * (1.0f / gap);
                shotOrigin_ = in.stringHand;
            } else if (phase_ == Phase::Nocked) {
                out.evLoose = true;   // a release with no draw: nothing is shot (the rig lets the string back to rest)
            }
            phase_ = Phase::Idle;
            charged_ = false;
        } else if (phase_ == Phase::Idle) {
            // Only a fresh press takes the string, and only at the string. Anywhere else - or already down when the draw came
            // back - it stays the plain trigger until it is let go. At the string while the last shot's press is still down: nothing.
            if (edge && out.atString) {
                hold_ = edgeT ? 0 : 1;
                if (in.time >= pressUntil_) {
                    phase_ = Phase::Nocked;
                    nockedAt_ = in.time;
                    nockGap_ = gap;
                    context_ = in.context;
                    charged_ = false;
                    out.evNock = true;
                } else phase_ = Phase::Blocked;
            } else phase_ = Phase::Plain;
        } else if (phase_ == Phase::Nocked && gap > p.cancelGap) {
            phase_ = Phase::Blocked;   // a pull this long is no draw
            charged_ = false;
            out.evCancel = true;
            out.why = kWhyTooLong;
        }

        if (phase_ == Phase::Nocked) {
            out.drawing = true;
            out.charged = drawn();
            out.evCharged = out.charged && !charged_;
            charged_ = out.charged;
            out.draw = std::fmin(std::fmax((gap - nockGap_) / 0.45f, 0.0f), 1.0f);
            // A pull that is enough to shoot is never a "short pull" to the bow rig, which eases the string back to rest for a draw
            // whose peak stayed under kRigShortPull: with a minimum pull under 11 cm the linear amount would be under it.
            if (gap - nockGap_ >= p.minPull) out.draw = std::fmax(out.draw, kRigShortPull);
            out.aimOwned = true;
            out.aim = gap > 0.05f ? line * (1.0f / gap) : out.bowForward;
            out.origin = in.stringHand;
        } else if (in.time < aimUntil_) {
            out.aimOwned = true;
            out.aim = shotAim_;
            out.origin = shotOrigin_;
        }
        out.owned = phase_ == Phase::Nocked || phase_ == Phase::Blocked;
        out.hold = out.owned ? hold_ : -1;
        out.phase = (int)phase_;
        out.pressing = in.time < pressUntil_;
        out.pullNeed = std::fmax(apart, (phase_ == Phase::Nocked ? nockGap_ : 0.0f) + p.minPull);
        out.pullEnough = gap >= out.pullNeed;
        goodAt_ = in.time;
        if (phase_ == Phase::Nocked) held_ = out;
        return out;
    }
    bool Owns() const { return phase_ == Phase::Nocked || phase_ == Phase::Blocked; }
};

}  // namespace bowdraw
