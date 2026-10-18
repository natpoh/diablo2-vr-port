// room_walk.h - room-scale walking in VR first person ([openxr] roomscale_walk): you walk in the room, the hero walks the same way.
// Pure: no game, no XInput, no clock of its own. The unit test is cleanroom/tests/room_walk_test.cpp, the wiring vr/room_walk_wire.cpp
// (the view build in vr/vrcam.cpp steps it, the game's pad poll reads its stick, vr/xr.cpp holds the body still for it).
//
// The model. In native F4 the camera is the hero's eyes plus the headset's place in the room, measured from a "body" point (xr::Eyes:
// metres, x right, y up, z back, in the turn of the last recentre). Without this the body follows the head past [openxr] lean_m, so walking
// in the room never moves the hero and the camera stays in him. With it the body is held still (xr::HoldBody) and this walker keeps its
// own point c in that frame: the place in the room the hero stands on.
//   - The offset D = n - c, with n the top of the neck (the eyes' middle less the head's lever, kNeckM behind the eyes in the head's own
//     turn): a head turned or nodded in place does not walk the hero, a step does.
//   - Past the dead zone the hero is walked toward D through the game's OWN walk: the same analog left stick the controller gives
//     (direction and tilt; the game's pathing, collisions, walk / run and speed stay the game's). Nothing sets a position.
//   - While it walks the hero, his own motion is credited to c (c moves with him, converted into the room): the camera = the hero's
//     eyes + (eyes - c) then stays exactly where the head is - the head drives the view, the hero catches up under it, they never pull
//     against each other. Any other motion of the hero (the left stick, a skill, a knock-back, a teleport) is NOT credited: the camera
//     goes with the hero, as it does today.
//   - Blocked (a wall, a monster) and the player walks on: D grows. At [openxr] roomscale_cap_m c is dragged along with the head
//     ("the room slides"): the view never leaves the hero by more than the cap, exactly as lean_m does today - no jump, no fade, no
//     camera inside the wall's far side. (A fade at the limit would need a pass of its own over the eyes and darkens the fight too;
//     stopping the walk would leave the hero standing while the view walks off.)
//   - The speed. The game's walk speed FOLLOWS the stick's tilt (its server move, 0x351000 - 0x351051 of the 1.3.1 image: the unit's
//     velocity + stat 0x43 "velocitypercent", times the stick's strength while its path is in stick mode 0x11, at least 25), down to
//     its dead zone (0.316): below that it does not walk at all. So the stick asks for a VELOCITY: the head's own (smoothed) plus the
//     offset closed in kCloseS, divided by the hero's speed at a full stick - learned live from how fast he really goes at a known tilt
//     (ours, or the controller's), kFullGuessMps until then. A steady walk is then one steady tilt: the hero walks with you, no stop
//     and go (the first cut used a fixed 1.4 m/s for a full stick: the hero, several times faster, ran ahead, stopped, waited for the
//     dead zone and went again - "рывками", 2026-10-18). The least tilt: through the pad the game's own dead zone (0.316) - and his log
//     showed that even that is ~2 m/s or more in room metres at a hero's scale (a full stick ~7 m/s), faster than anyone walks in a
//     room; through the game's walking-stick hook ([input] direct_walk, vrcam's HookStickGet: the vector the game's walk code reads,
//     after the pad's dead zone) kMinTiltHook - the server takes the strength in 31 steps - so a room walk can be one slow steady tilt.
//     Slower than the least tilt it cannot follow continuously: it walks while the hero is behind you and waits while he is there or
//     ahead; and it never walks him back against a head that walks on (an overshoot is waited out). A brisk stride is a full stick - a
//     run when the game's Run / Run Lock is on (its own threshold and timers).
//   - The controller's stick wins: held (pushed, or let go less than kStickReleaseS ago - a stick passing through its middle is still
//     held), the room walk pushes nothing and credits nothing (the stick walks the hero, the camera goes with him); let go, the room walk
//     closes what is left.
//   - Turning: never. The stick only moves the hero; his facing is whatever it was.
//   - A recentre (F11, the headset's own) or a gap in the frames (a loading screen, another view): start fresh, the hero under the head.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace roomwalk {

struct Config {
    bool on = false;      // [openxr] roomscale_walk
    float deadM = 0.12f;  // [openxr] roomscale_dead_zone_m: the neck this far off the hero's place before he walks
    float capM = 0.6f;    // [openxr] roomscale_cap_m: farther than this off him (he is blocked) the room slides with the head
};

constexpr float kNeckM = 0.09f;       // the top of the neck behind the eyes' middle
constexpr float kGameDead = 10349.0f / 32767.0f;   // the game's stick dead zone: below it no walk at all
constexpr float kMinTilt = 0.33f;     // the least stick through the pad: just past it - the game's slowest walk there
constexpr float kMinTiltHook = 0.07f; // ... through the walking-stick hook: 2 of the 31 steps the server takes (a step lost to rounding is still one)
constexpr float kLearnMinTilt = 0.1f; // the hero's speed at tilts from this up teaches his full-stick speed
constexpr float kCloseS = 0.35f;      // the offset is closed in about this long (on top of matching the head's speed)
constexpr float kFullGuessMps = 4.0f; // the hero's speed at a full stick, room metres a second, until learned (high is the safe side)
constexpr float kFullMinMps = 1.0f, kFullMaxMps = 12.0f;
constexpr float kLearnTauS = 1.5f;    // the learned full-stick speed's smoothing
constexpr double kLearnSteadyS = 0.3; // a tilt held this steady (within kLearnTiltTol) before the hero's speed is a sample of it
constexpr float kLearnTiltTol = 0.08f;
constexpr float kLearnMaxTilt = 0.55f; // above this the game may run (Run / Run Lock - the author's log: 0.6 and 0.8 both ~7 m/s): no samples there
constexpr double kStickReleaseS = 0.15;
constexpr float kHeadTauS = 0.25f;    // the head's velocity smoothing (a walk's sway is no direction)
constexpr float kLeadS = 0.10f;       // let go this long before the hero is there at his speed (the game stops a tick late)
constexpr float kMinStopM = 0.03f;    // ... and never farther than this from it
constexpr double kTailS = 0.35;       // the hero's motion counts as ours this long after our last push (he stops a tick or two late)
constexpr double kGapS = 2.0;         // no step for this long: start fresh. Shorter pauses (a hitch, a slow pair) keep the place: one continuous
                                      // follow, no re-anchor (xr holds the body 2.5 s, so the frame does not move under it meanwhile)
constexpr float kJumpM = 0.75f;       // the hero moved farther than this in one step: no walk (a teleport, a new area) - never credited
constexpr double kBlockedS = 0.35;    // walking this long with the hero (almost) still = blocked
constexpr float kBlockedMps = 0.15f;
constexpr double kRefractoryS = 0.4;  // just stopped: a start back the other way waits this long (an overshoot is not a step back) ...
constexpr float kReverseFar = 2.0f;   // ... unless the offset is past this many dead zones
constexpr float kSpeedTauS = 0.15f;   // the speeds' smoothing

struct V2 { float x = 0.0f, z = 0.0f; };
inline float Len(V2 v) { return std::sqrt(v.x * v.x + v.z * v.z); }

// A vector turned by a unit quaternion (x y z w).
inline void Rotate(const float q[4], const float v[3], float out[3]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float tx = 2.0f * (y * v[2] - z * v[1]), ty = 2.0f * (z * v[0] - x * v[2]), tz = 2.0f * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

// The top of the neck on the floor plane: the eyes' middle and kNeckM behind them in the head's own turn.
inline V2 NeckPoint(const float eyes[3], const float q[4]) {
    const float back[3] = {0.0f, 0.0f, kNeckM};
    float b[3];
    Rotate(q, back, b);
    return {eyes[0] + b[0], eyes[2] + b[2]};
}

// The head's yaw in the frame (radians, + turned left) - exactly as the native camera takes it (vrcam: atan2(-hf.x, -hf.z) of the
// head's forward), so the stick below and the camera's turn the game's stick by agree.
inline float HeadYaw(const float q[4]) {
    const float f[3] = {0.0f, 0.0f, -1.0f};
    float hf[3];
    Rotate(q, f, hf);
    return std::atan2(-hf[0], -hf[2]);
}

// A floor vector of the frame (x right, z back) as a stick in the view (x right, y ahead) of a head turned by yaw.
inline void ToView(V2 d, float yaw, float* sx, float* sy) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    *sx = d.x * c - d.z * s;      // . (cos, -sin): the head's right
    *sy = -d.x * s - d.z * c;     // . (-sin, -cos): the head's forward
}

// The hero's move in the world (x, z) as metres in the eyes' frame: the inverse of the native camera's toWorld (a turn by the
// room's base, cb sb its cos and sin) and of its world units a metre (upm).
inline V2 WorldToRoom(float wx, float wz, float cb, float sb, float upm) {
    if (!(upm > 1e-6f)) return {};
    return {(cb * wx - sb * wz) / upm, (sb * wx + cb * wz) / upm};
}

struct Input {
    double t = 0.0;              // seconds, monotonic
    bool engaged = false;        // the key, VR first person, native eyes, a game: the walker keeps its place (false: it lets go, then starts fresh)
    bool canWalk = false;        // ... and no panel, no chat: it may push the stick
    bool stickHeld = false;      // the controller's own walk is pushed (past the game's dead zone): it wins
    float ownTilt = 0.0f;        // ... its tilt 0..1 (the hero's speed at it teaches the full-stick speed)
    float minTilt = kMinTilt;    // the least tilt the game walks at on this path (kMinTilt: the pad; kMinTiltHook: the walking-stick hook)
    uint32_t epoch = 0;          // the runtime space's recentre count (xr): another one = start fresh
    float eyes[3] = {};          // the eyes' middle, xr::Eyes' frame
    float quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // the head's turn in it
    V2 heroMove;                 // the hero's move since the last step, in that frame, metres
};

struct Output {
    bool engaged = false;        // false: the camera as without room walking (the eyes as they are)
    float camX = 0.0f, camZ = 0.0f;     // take these from the eyes' x and z for the camera: c and the eyes' lever at the recentre's gaze
    float stickX = 0.0f, stickY = 0.0f; // the room walk's stick in the view (x right, y ahead, length <= 1); 0 0 = none
    bool walking = false, blocked = false, capped = false, fresh = false;
    V2 off;                      // the neck past the hero's place
    float offM = 0.0f, stopM = 0.0f, tilt = 0.0f;
    float headMps = 0.0f, heroMps = 0.0f;
    float fullMps = 0.0f;        // the hero's speed at a full stick as learned so far
    float need = 0.0f;           // the tilt the wanted velocity asks for (may be under the game's dead zone)
    bool stickHeld = false;      // the controller's stick held, with its release hold
    bool slow = false;           // slower than the game's slowest walk: walk / wait at the least tilt
    float minTilt = 0.0f;        // the least tilt on this path
};

class Walker {
public:
    Output Step(const Config& cfg, const Input& in) {
        Output o;
        if (!cfg.on || !in.engaged) { have_ = false; walking_ = false; return o; }
        const V2 n = NeckPoint(in.eyes, in.quat);
        const double dt = have_ ? in.t - t_ : 0.0;
        const float moved = Len(in.heroMove);
        if (in.stickHeld) stickAt_ = in.t;
        const bool held = in.stickHeld || in.t - stickAt_ < kStickReleaseS;
        if (!have_ || in.epoch != epoch_ || dt > kGapS || dt < 0.0) {
            have_ = true; epoch_ = in.epoch;
            c_ = n; walking_ = false; lastPush_ = -1e9; stoppedAt_ = -1e9; heroMps_ = 0.0f; headV_ = {};
            o.fresh = true;
        } else {
            if (dt > 1e-4) {
                const float a = 1.0f - std::exp(-(float)dt / kSpeedTauS), ah = 1.0f - std::exp(-(float)dt / kHeadTauS);
                if (moved < kJumpM) heroMps_ += a * (moved / (float)dt - heroMps_);
                headV_.x += ah * ((n.x - n_.x) / (float)dt - headV_.x);
                headV_.z += ah * ((n.z - n_.z) / (float)dt - headV_.z);
                Learn(in, held, (float)dt);
            }
            // ours: the stick of the LAST step was ours (or in its tail) - judged at that step, so a hitch between does not lose his motion
            if (!held && t_ - lastPush_ <= kTailS && moved < kJumpM) { c_.x += in.heroMove.x; c_.z += in.heroMove.z; }
        }
        n_ = n; t_ = in.t;
        V2 d{n.x - c_.x, n.z - c_.z};
        float off = Len(d);
        const float dead = std::max(cfg.deadM, 0.01f);
        const float cap = std::max(cfg.capM, dead + 0.05f);
        if (off > cap) {   // blocked and walked on: the room slides with the head
            const float k = cap / off;
            c_.x = n.x - d.x * k; c_.z = n.z - d.z * k;
            d = {n.x - c_.x, n.z - c_.z};
            off = cap;
            o.capped = true;
        }
        // the velocity wanted: the head's own and the offset closed in kCloseS; the tilt that asks the game for it
        const V2 want{headV_.x + d.x / kCloseS, headV_.z + d.z / kCloseS};
        const float wantMps = Len(want);
        const float need = wantMps / full_;
        const bool behind = d.x * headV_.x + d.z * headV_.z >= 0.0f;   // the hero is behind the head (or it stands)
        const float stop = std::clamp(heroMps_ * kLeadS, kMinStopM, dead * 0.8f);
        const float minTilt = std::clamp(in.minTilt, 0.02f, 1.0f);
        const bool slow = need < minTilt;
        // the head walks on and the velocity wanted points back at it: the hero is ahead (he overshot, or walks faster than asked) - wait
        const bool against = Len(headV_) > 0.2f && want.x * headV_.x + want.z * headV_.z < 0.0f;
        const bool was = walking_;
        if (!in.canWalk || held) walking_ = false;
        else if (!walking_ && off > dead) {
            const bool back = off > 1e-6f && (d.x * lastDir_.x + d.z * lastDir_.z) / off < 0.0f;
            if (!(back && in.t - stoppedAt_ < kRefractoryS && off < kReverseFar * dead)) { walking_ = true; walkSince_ = in.t; }
        } else if (walking_ && ((slow && (off < stop || !behind)) || against)) walking_ = false;   // the game cannot go this slowly / he is ahead: wait
        if (was && !walking_) stoppedAt_ = in.t;
        o.blocked = false;
        if (walking_) {
            // continuous: toward the velocity wanted; slow: toward the hero's place at the least tilt
            V2 u = slow || wantMps < 1e-4f ? d : want;
            const float ul = Len(u);
            if (ul > 1e-6f) {
                u = {u.x / ul, u.z / ul};
                const float tilt = std::clamp(need, minTilt, 1.0f);
                float sx = 0.0f, sy = 0.0f;
                ToView(u, HeadYaw(in.quat), &sx, &sy);
                o.stickX = sx * tilt; o.stickY = sy * tilt;
                o.tilt = tilt;
                lastDir_ = u;
                lastPush_ = in.t;
                o.blocked = in.t - walkSince_ > kBlockedS && heroMps_ < kBlockedMps;
            }
        }
        // the tilt pushed this step, for the learning on the next ones (the hero's speed then is the answer to it)
        if (std::fabs(o.tilt - lastTilt_) > kLearnTiltTol || o.blocked) tiltSince_ = in.t;
        lastTilt_ = o.tilt;
        lastBlocked_ = o.blocked;
        o.engaged = true;
        o.walking = walking_;
        o.camX = c_.x; o.camZ = c_.z - kNeckM;
        o.off = d; o.offM = off; o.stopM = stop;
        o.headMps = Len(headV_); o.heroMps = heroMps_;
        o.fullMps = full_; o.need = need; o.stickHeld = held; o.slow = slow; o.minTilt = minTilt;
        return o;
    }
    void Reset() { have_ = false; walking_ = false; }
    float FullMps() const { return full_; }

private:
    // The hero's speed at a full stick, from how fast he goes at a tilt held steady (ours, or the controller's), never at a run's tilt,
    // never blocked. Kept across fresh starts (it is the hero's, not the place's).
    void Learn(const Input& in, bool held, float dt) {
        float tilt = 0.0f;
        if (in.stickHeld) {
            if (std::fabs(in.ownTilt - ownTilt_) > kLearnTiltTol) ownSince_ = in.t;
            ownTilt_ = in.ownTilt;
            if (in.t - ownSince_ >= kLearnSteadyS) tilt = in.ownTilt;
        } else {
            ownSince_ = in.t;
            ownTilt_ = 0.0f;
            if (!held && walking_ && !lastBlocked_ && in.t - tiltSince_ >= kLearnSteadyS) tilt = lastTilt_;
        }
        if (tilt < kLearnMinTilt || tilt > kLearnMaxTilt || heroMps_ < 0.2f) return;
        const float a = 1.0f - std::exp(-dt / kLearnTauS);
        full_ = std::clamp(full_ + a * (heroMps_ / tilt - full_), kFullMinMps, kFullMaxMps);
    }

    bool have_ = false, walking_ = false, lastBlocked_ = false;
    uint32_t epoch_ = 0;
    V2 c_, n_, lastDir_, headV_;
    double t_ = 0.0, lastPush_ = -1e9, stoppedAt_ = -1e9, walkSince_ = 0.0, stickAt_ = -1e9, tiltSince_ = 0.0, ownSince_ = 0.0;
    float heroMps_ = 0.0f, lastTilt_ = 0.0f, ownTilt_ = 0.0f;
    float full_ = kFullGuessMps;
};

// The stick for the game's pad: the room walk's only where the controller's own stick rests (past the game's dead zone it wins).
// Raw XInput units. True = the room walk's stick is the one.
inline bool Merge(int16_t ownLX, int16_t ownLY, int16_t roomLX, int16_t roomLY, int16_t* lx, int16_t* ly) {
    const bool ownPushed = (long long)ownLX * ownLX + (long long)ownLY * ownLY > 10349ll * 10349ll;
    const bool roomPushed = roomLX != 0 || roomLY != 0;
    if (ownPushed || !roomPushed) { *lx = ownLX; *ly = ownLY; return false; }
    *lx = roomLX; *ly = roomLY;
    return true;
}

inline int16_t Axis(float v) { return (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }

}  // namespace roomwalk
