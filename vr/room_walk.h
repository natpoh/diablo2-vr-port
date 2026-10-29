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
//     (ours, or the controller's, a lag before), kFullGuessMps until then. A steady walk is then one steady tilt: the hero walks with you, no stop
//     and go (the first cut used a fixed 1.4 m/s for a full stick: the hero, several times faster, ran ahead, stopped, waited for the
//     dead zone and went again - "рывками", 2026-10-18). The least tilt: through the pad the game's own dead zone (0.316) - and his log
//     showed that even that is ~2 m/s or more in room metres at a hero's scale (a full stick ~7 m/s), faster than anyone walks in a
//     room; through the game's walking-stick hook ([input] direct_walk, vrcam's HookStickGet: the vector the game's walk code reads,
//     after the pad's dead zone) kMinTiltHook - the server takes the strength in 31 steps. A brisk stride is a full stick - a run when
//     the game's Run / Run Lock is on (its own threshold and timers).
//   - The game's least walk. The same server move ends "cmp edi, 0x19; cmovl edi, eax" (0x351047 of the 1.3.1 image): the speed in
//     percent (100 + faster run/walk + "velocitypercent") times the strength is never under 25 - whatever the tilt, the hero never
//     walks slower than a quarter of his full walk (kGameFloor; ~1.7 m/s in room metres at the author's hero scale, faster than a step
//     in a room). The cut of 2026-10-18 took the hook's least tilt for the least speed: a walk of 0.4 - 1.5 m/s asked a tilt of 0.07 -
//     0.2, the hero went at the floor, ran past the head, the walker stopped him ("he is ahead"), waited for the dead zone behind and
//     went again: 6 - 12 stops in 4 s, the hero 0.2 - 0.35 m ahead and behind ("рывками" again, 2026-10-22); and his floor speed at
//     tilts 0.1 - 0.25 taught a full-stick speed far too high. Now: the floor is in the model (learned from our own pulses at the least
//     tilt, a quarter of the full-stick speed until then), the full-stick speed is learned only at tilts above any floor (kLearnMinTilt),
//     both from his motion against the stick of a lag before it (Learn, FloorPulse). Faster than the floor the follow is one steady
//     tilt; slower, the hero walks at his least speed in short pulses: from as soon as he is off by the dead zone (0: the least offset
//     he can be placed by - half of what a pulse read on two ticks walks), let go once he will stop under the head - or, while the head
//     walks on, as far past it as he started behind, which halves the stops for the same offset.
//   - The lag. Our stick reaches his motion a game tick and the server's answer later (learned from how long he takes to start, kLeadS
//     until then): every decision is on the offset as it will be once he has walked out what we already pushed (Pending - the push
//     history times the modelled speed) and the head has walked on for as long - else, with a lag, a pulse just made looks like nothing
//     and is pushed again (a to and fro round a head standing still). Never walks him back against a head that walks on (an overshoot
//     is waited out); the other way within kRefractoryS of a stop only past kReverseFar starts.
//   - The controller's stick wins: held (pushed, or let go less than kStickReleaseS ago - a stick passing through its middle is still
//     held), the room walk pushes nothing and credits nothing (the stick walks the hero, the camera goes with him); let go, the room walk
//     closes what is left. While the room walk is walking the hero (or between its pulses) a push takes over only when it means it: past
//     kStickTakeTilt, or held kStickTakeS - until then the pad keeps the room walk's stick (Output::roomFirst, Merge). A thumb resting
//     on the stick while you walk the room (2026-10-22: "holds" / "live" several times a second, each a lurch of the hero by the thumb
//     with the camera on him, the stick just past the game's dead zone) no longer cancels it.
//   - Turning: never. The stick only moves the hero; his facing is whatever it was.
//   - A recentre (F11, the headset's own) or a gap in the frames (a loading screen, another view): start fresh, the hero under the head.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace roomwalk {

struct Config {
    bool on = false;      // [openxr] roomscale_walk
    float deadM = 0.12f;  // [openxr] roomscale_dead_zone_m: the neck this far off the hero's place before he walks (0: as soon as he can be placed)
    float capM = 0.6f;    // [openxr] roomscale_cap_m: farther than this off him (he is blocked) the room slides with the head
};

constexpr float kNeckM = 0.09f;       // the top of the neck behind the eyes' middle
constexpr float kGameDead = 10349.0f / 32767.0f;   // the game's stick dead zone: below it no walk at all
constexpr float kMinTilt = 0.33f;     // the least stick through the pad: just past it - the game's slowest walk there
constexpr float kMinTiltHook = 0.07f; // ... through the walking-stick hook: 2 of the 31 steps the server takes (a step lost to rounding is still one)
constexpr float kGameFloor = 0.25f;   // the game's least walk: a quarter of a full walk at speed 100 % (its server move's "at least 25")
constexpr float kLearnMinTilt = 0.3f; // the hero's speed at tilts from this up teaches his full-stick speed (above the floor's tilt, 0.25)
constexpr float kFloorMaxTilt = 0.1f; // ... at our tilts up to this it is his least walk (the floor's tilt is 0.25 / (1 + faster walk))
constexpr float kLearnMinS = 0.25f;   // either speed is taken once this long of it is seen ...
constexpr float kLearnFadeS = 8.0f;   // ... older walks fading out over about this long
constexpr double kFloorChunkS = 0.3;  // a long pulse at the least tilt teaches the floor in pieces this long
constexpr float kStartMarginM = 0.01f;// the least start: half the least walk's coast and this
constexpr float kAheadShare = 0.8f;   // a pulse while the head walks on: released to stop this share of the start past the head
constexpr double kMinPulseS = 0.05;   // a pulse at least this long (the game reads the stick once a 40 ms tick) ...
constexpr float kTickS = 0.04f;       // ... so it is read for up to this much more
constexpr float kLeadMinS = 0.03f, kLeadMaxS = 0.4f;
constexpr double kLagProbeS = 0.6;    // a start's lag: the hero still at the push and moving within this long
constexpr float kCloseS = 0.35f;      // the offset is closed in about this long (on top of matching the head's speed)
constexpr float kFullGuessMps = 7.0f; // the hero's speed at a full stick, room metres a second, until learned (the author's hero; high is the
                                      // safe side: the floor taken high walks in pulses, never faster than asked)
constexpr float kFullMinMps = 1.0f, kFullMaxMps = 12.0f;
constexpr float kLearnTiltTol = 0.08f; // a sample's stick held within this
constexpr float kLearnMaxTilt = 0.55f; // above this the game may run (Run / Run Lock - the author's log: 0.6 and 0.8 both ~7 m/s): no samples there
constexpr double kStickReleaseS = 0.15;
constexpr double kStickTakeS = 0.12;     // the room walk busy: a push of the controller's stick takes over once held this long ...
constexpr float kStickTakeTilt = 0.6f;   // ... or at once past this tilt
constexpr double kStickBusyS = 0.5;      // the room walk counts as busy this long after its last push (between its pulses)
constexpr float kHeadTauS = 0.25f;    // the head's velocity smoothing (a walk's sway is no direction)
constexpr float kLeadS = 0.10f;       // the lag from our stick to the hero's motion (a tick's read, the server's answer) until it is seen
constexpr double kTailS = 0.35;       // the hero's motion counts as ours this long after our last push (he stops a tick or two late) - or the lead and 0.2 s
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
    bool roomFirst = false;      // the room walk is walking the hero and the controller's push (if any) has not taken over: the pad gets
                                 // the room walk's stick, zero between its pulses (Merge)
    bool stickPending = false;   // ... a push of the controller's is there, not taken over yet
    bool slow = false;           // slower than the game's slowest walk: walk / wait at the least tilt
    float minTilt = 0.0f;        // the least tilt on this path
    float leastMps = 0.0f;       // the hero's least walk on this path (his floor, or the least tilt's speed), as known so far
    bool floorLearned = false;   // ... the floor seen (else a quarter of the full-stick speed)
    float leadS = 0.0f;          // the lag from our stick to his motion, as learned
    float startM = 0.0f;         // the offset a walk starts at (the dead zone, or the least he can be placed by)
};

class Walker {
public:
    Output Step(const Config& cfg, const Input& in) {
        Output o;
        if (!cfg.on || !in.engaged) { have_ = false; walking_ = false; return o; }
        const V2 n = NeckPoint(in.eyes, in.quat);
        const double dt = have_ ? in.t - t_ : 0.0;
        const float moved = Len(in.heroMove);
        // the controller's push: at once while the room walk rests, meant (far or long) while it walks the hero
        if (!in.stickHeld) pushing_ = false;
        else if (!pushing_) { pushing_ = true; pushedSince_ = in.t; }
        const bool busy = in.canWalk && (walking_ || in.t - lastPush_ < kStickBusyS);
        // (pushed again inside the release hold: the same push, through the stick's middle)
        const bool taken = in.stickHeld && (!busy || in.ownTilt >= kStickTakeTilt || in.t - pushedSince_ >= kStickTakeS || in.t - stickAt_ < kStickReleaseS);
        if (taken) stickAt_ = in.t;
        const bool held = taken || in.t - stickAt_ < kStickReleaseS;
        if (!have_ || in.epoch != epoch_ || dt > kGapS || dt < 0.0) {
            have_ = true; epoch_ = in.epoch;
            c_ = n; walking_ = false; lastPush_ = -1e9; stoppedAt_ = -1e9; heroMps_ = 0.0f; headV_ = {}; probeAt_ = -1e9;
            for (Push& p : pushes_) p = {};   // (another frame, or long ago: no motion still to come)
            fpOn_ = false;
            o.fresh = true;
        } else {
            if (dt > 1e-4) {
                const float a = 1.0f - std::exp(-(float)dt / kSpeedTauS), ah = 1.0f - std::exp(-(float)dt / kHeadTauS);
                if (moved < kJumpM) heroMps_ += a * (moved / (float)dt - heroMps_);
                headV_.x += ah * ((n.x - n_.x) / (float)dt - headV_.x);
                headV_.z += ah * ((n.z - n_.z) / (float)dt - headV_.z);
            }
            // the lag: a start of ours with the hero still, and his first motion after it (a hitch between is no sample)
            if (probeAt_ > -1e8 && moved > 1e-5f && moved < kJumpM && !held) {
                if (in.t - probeAt_ <= kLagProbeS && dt < 0.1) {
                    const float lag = std::clamp((float)(in.t - probeAt_ - 0.5 * dt), kLeadMinS, kLeadMaxS);
                    lead_ += (leadLearned_ ? 0.3f : 1.0f) * (lag - lead_);
                    leadLearned_ = true;
                }
                probeAt_ = -1e9;
            }
            if (in.t - probeAt_ > kLagProbeS) probeAt_ = -1e9;
            // ours: the stick of the LAST step was ours (or in its tail) - judged at that step, so a hitch between does not lose his motion
            if (!held && t_ - lastPush_ <= std::max(kTailS, (double)lead_ + 0.2) && moved < kJumpM) { c_.x += in.heroMove.x; c_.z += in.heroMove.z; }
        }
        const bool stillBefore = moved < 1e-5f && heroMps_ < 0.05f;
        n_ = n; t_ = in.t;
        V2 d{n.x - c_.x, n.z - c_.z};
        float off = Len(d);
        const float dead = std::max(cfg.deadM, 0.0f);
        const float cap = std::max(cfg.capM, dead + 0.05f);
        if (off > cap) {   // blocked and walked on: the room slides with the head
            const float k = cap / off;
            c_.x = n.x - d.x * k; c_.z = n.z - d.z * k;
            d = {n.x - c_.x, n.z - c_.z};
            off = cap;
            o.capped = true;
        }
        // Every decision is on the offset as it will be once the game has answered our pushes of the last lag (he still walks them out,
        // whatever we push now) and the head has walked on for as long: with a lag the offset now says nothing about a pulse just made.
        const V2 pend = Pending(in.t);
        const V2 dp{d.x + headV_.x * lead_ - pend.x, d.z + headV_.z * lead_ - pend.z};
        const float offP = Len(dp);
        // the velocity wanted: the head's own and that offset closed in kCloseS; the tilt that asks the game for it
        const V2 want{headV_.x + dp.x / kCloseS, headV_.z + dp.z / kCloseS};
        const float wantMps = Len(want);
        const float need = wantMps / full_;
        const float minTilt = std::clamp(in.minTilt, 0.02f, 1.0f);
        // the hero's least walk on this path: the game's floor (as seen, or a quarter of his full walk), or the least tilt's speed
        const float leastMps = std::max(floor_ > 0.0f ? floor_ : kGameFloor * full_, full_ * minTilt);
        const bool slow = wantMps < leastMps;
        // the least he can be placed by: a pulse is read for a tick or two, so it walks him about this far at the least; a start past
        // half of it never overshoots by more than a start (no to and fro), whatever the dead zone
        const float startM = std::max(dead, 0.5f * leastMps * ((float)kMinPulseS + kTickS) + kStartMarginM);
        // the head walks on and the velocity wanted points back at it: the hero is ahead (he overshot, or walks faster than asked) - wait
        const bool against = Len(headV_) > 0.2f && want.x * headV_.x + want.z * headV_.z < 0.0f;
        // a pulse at his least walk (slow): let go once he will stop under the head - or, the head walking on along it, this far past it
        // (he started as far behind: half the stops for the same offset)
        const float along = dp.x * pulseDir_.x + dp.z * pulseDir_.z, headAlong = headV_.x * pulseDir_.x + headV_.z * pulseDir_.z;
        const float aheadM = headAlong > 0.2f ? kAheadShare * startM : 0.0f;
        const bool was = walking_;
        if (!in.canWalk || held) walking_ = false;
        else if (!walking_ && offP > startM) {
            const bool back = (dp.x * lastDir_.x + dp.z * lastDir_.z) / offP < 0.0f;
            // ahead of a head that walks on (an overshoot of the game's least walk): the head comes to him - never walk him back at it
            const bool ahead = Len(headV_) > 0.2f && dp.x * headV_.x + dp.z * headV_.z < 0.0f;
            if (!ahead && !(back && in.t - stoppedAt_ < kRefractoryS && offP < kReverseFar * startM)) {
                walking_ = true; walkSince_ = in.t;
                if (stillBefore) probeAt_ = in.t;
                pulseDir_ = {dp.x / offP, dp.z / offP};
            }
        } else if (walking_ && (slow ? in.t - walkSince_ >= kMinPulseS && along < -aheadM : against)) walking_ = false;   // there / he is ahead: wait
        if (was && !walking_) stoppedAt_ = in.t;
        o.blocked = false;
        V2 pushV;   // the hero's velocity this push asks for, as the model has it
        if (walking_) {
            // continuous: toward the velocity wanted; slow: toward the hero's place at the least tilt (along the pulse once he is close)
            V2 u = slow || wantMps < 1e-4f ? (offP > startM ? dp : pulseDir_) : want;
            const float ul = Len(u);
            if (ul > 1e-6f) {
                u = {u.x / ul, u.z / ul};
                const float tilt = slow ? minTilt : std::clamp(need, minTilt, 1.0f);
                float sx = 0.0f, sy = 0.0f;
                ToView(u, HeadYaw(in.quat), &sx, &sy);
                o.stickX = sx * tilt; o.stickY = sy * tilt;
                o.tilt = tilt;
                lastDir_ = u;
                if (!slow) pulseDir_ = u;
                lastPush_ = in.t;
                o.blocked = in.t - walkSince_ > kBlockedS && heroMps_ < kBlockedMps;
                const float sp = o.blocked ? 0.0f : std::max(leastMps, full_ * tilt);
                pushV = {u.x * sp, u.z * sp};
            }
        }
        // the push of this step - its tilt, ours or the controller's (not past the run's tilt, not blocked: -1), for the learning a lag
        // later; and the motion it asks of the hero, ours only, for the offset to come
        Record(in.t, o.blocked ? -1.0f : taken ? (in.ownTilt <= kLearnMaxTilt ? in.ownTilt : -1.0f) : held ? -1.0f : o.tilt, pushV,
               o.fresh ? 0.0f : moved, o.fresh ? 0.0f : (float)dt);
        Learn(in.t);
        FloorPulse(in.t, held ? -1.0f : o.tilt, o.fresh ? 0.0f : moved, o.blocked);
        lastBlocked_ = o.blocked;
        o.engaged = true;
        o.walking = walking_;
        o.camX = c_.x; o.camZ = c_.z - kNeckM;
        o.off = d; o.offM = off; o.stopM = Len(pend);
        o.headMps = Len(headV_); o.heroMps = heroMps_;
        o.fullMps = full_; o.need = need; o.stickHeld = held; o.slow = slow; o.minTilt = minTilt;
        o.roomFirst = !held && busy;
        o.stickPending = in.stickHeld && !taken;
        o.leastMps = leastMps; o.floorLearned = floor_ > 0.0f; o.leadS = lead_; o.startM = startM;
        return o;
    }
    void Reset() { have_ = false; walking_ = false; }
    float FullMps() const { return full_; }

private:
    // The steps of the last second or so: the stick pushed at each (in force until the next) and the hero's motion seen at it (over the
    // step before) - his motion is the answer to the stick of a lag ago.
    struct Push { double t = -1e9; float tilt = -1.0f; V2 v; float moved = 0.0f, dt = 0.0f; };
    static constexpr int kPushes = 256;
    Push pushes_[kPushes];
    int pushAt_ = 0;   // the next slot
    double learnedTo_ = -1e9;   // the steps up to this time are learned from
    void Record(double t, float tilt, V2 v, float moved, float dt) { pushes_[pushAt_] = {t, tilt, v, moved, dt}; pushAt_ = (pushAt_ + 1) % kPushes; }
    // The hero's motion still to come from our pushes of the last lag (each in force from its time to the next one's)
    V2 Pending(double now) const {
        V2 s;
        const double from = now - lead_;
        double end = now;
        for (int k = 1; k <= kPushes; ++k) {
            const Push& p = pushes_[(pushAt_ - k + kPushes) % kPushes];
            if (p.t <= -1e8) break;
            const double a = std::max(p.t, from);
            if (end > a) { s.x += p.v.x * (float)(end - a); s.z += p.v.z * (float)(end - a); }
            if (p.t <= from) break;
            end = p.t;
        }
        return s;
    }
    // The tilts pushed over [a, b], as their least and most (false: a gap in the record, or one not to learn from)
    bool TiltsOver(double a, double b, float* lo, float* hi) const {
        *lo = 1e9f; *hi = -1e9f;
        for (int k = 1; k <= kPushes; ++k) {
            const Push& p = pushes_[(pushAt_ - k + kPushes) % kPushes];
            if (p.t <= -1e8) return false;
            if (p.t <= b) {
                if (p.tilt < 0.0f) return false;
                *lo = std::min(*lo, p.tilt); *hi = std::max(*hi, p.tilt);
            }
            if (p.t <= a) return true;   // the push in force at a: the span is covered
        }
        return false;
    }

    // The hero's speed at a full stick and his least walk (the floor), from his motion over a step and the stick pushed a lag before
    // it, a tick either side (the game reads the stick once a tick: he starts and stops up to a tick off the lag) - ours or the
    // controller's: at tilts from kLearnMinTilt (above any floor) to kLearnMaxTilt (a run's above) his speed is the full-stick speed
    // times the tilt; at our least tilts (up to kFloorMaxTilt) it is the floor. A step is learned from once the stick a tick after it is
    // known. Summed with a slow fade, so a client that moves him once a tick averages out. Until the full-stick speed is seen, it is
    // four floors (the floor is a quarter of a full walk at speed 100 %). Kept across fresh starts (they are the hero's), as the lag.
    void Learn(double now) {
        double newest = learnedTo_;
        for (int k = 1; k <= kPushes; ++k) {
            const Push& p = pushes_[(pushAt_ - k + kPushes) % kPushes];
            if (p.t <= learnedTo_ || p.t <= -1e8) break;
            if (p.t - lead_ + kTickS > now) continue;   // the stick a tick past its lag not pushed yet
            newest = std::max(newest, p.t);
            if (p.dt > 1e-4f && p.dt < 0.1f && p.moved < kJumpM) LearnStep(p.t, p.moved, p.dt);
        }
        learnedTo_ = newest;
    }
    void LearnStep(double t, float moved, float dt) {
        float lo = 0.0f, hi = 0.0f;
        if (!TiltsOver(t - dt - lead_ - kTickS, t - lead_ + kTickS, &lo, &hi)) return;
        const float k = std::exp(-dt / kLearnFadeS);
        if (lo >= kLearnMinTilt && hi <= kLearnMaxTilt && hi - lo <= kLearnTiltTol) {
            linDist_ = linDist_ * k + moved;
            linTiltTime_ = linTiltTime_ * k + 0.5f * (lo + hi) * dt;
            linTime_ = linTime_ * k + dt;
            if (linTime_ >= kLearnMinS) { full_ = std::clamp(linDist_ / linTiltTime_, kFullMinMps, kFullMaxMps); fullLearned_ = true; }
        }
    }
    // The floor from whole pulses of ours at the least tilt: all the motion one makes (from a lag less a tick after its start to a lag
    // and a tick after its end) over how long it was pushed - the game reads it on as many ticks as fit in that time on average, so a
    // pulse of 50 ms teaches as well as a long one. Only a pulse two ticks clear of any other push (no motion of another in its span),
    // never blocked, never past the floor's tilt.
    void FloorPulse(double t, float tilt, float moved, bool blocked) {
        const bool atFloor = tilt > 0.0f && tilt <= kFloorMaxTilt;
        if (fpOn_) {
            if (t > fpT0_ + lead_ - kTickS) fpDist_ += moved;
            // the lag without the tick's read (the lag seen at a start has half a tick of waiting for the read in it)
            const double lag = std::max(0.0, (double)lead_ - 0.5 * kTickS);
            if (blocked || tilt < 0.0f || moved >= kJumpM || (tilt > 0.0f && !atFloor) || (fpT1_ > 0.0 && tilt > 0.0f && t < fpT1_ + 2.0 * kTickS)) fpOn_ = false;
            else if (fpT1_ < 0.0 && tilt <= 0.0f) fpT1_ = t;
            else if (fpT1_ > 0.0 && t >= fpT1_ + lead_ + kTickS) {   // all of its motion in
                FloorSample(fpDist_, (float)(fpT1_ - fpT0_));
                fpOn_ = false;
            } else if (fpT1_ < 0.0 && t - lag - fpT0_ >= kFloorChunkS) {   // a long one: what it has made so far, for what of it has been answered
                FloorSample(fpDist_, (float)(t - lag - fpT0_));
                fpT0_ = t - lag;
                fpDist_ = 0.0f;
            }
        } else if (atFloor && !(prevTilt_ > 0.0f) && t - lastOn_ >= 2.0 * kTickS && !blocked) {
            fpOn_ = true; fpT0_ = t; fpT1_ = -1.0; fpDist_ = 0.0f;
        }
        if (tilt != 0.0f) lastOn_ = t;   // (the controller's walk too)
        prevTilt_ = tilt;
    }
    void FloorSample(float dist, float pushed) {
        if (!(pushed > 1e-3f)) return;
        const float k = std::exp(-pushed / kLearnFadeS);
        floorDist_ = floorDist_ * k + dist;
        floorTime_ = floorTime_ * k + pushed;
        if (floorTime_ >= kLearnMinS) {
            floor_ = std::clamp(floorDist_ / floorTime_, 0.1f, kFullMaxMps);
            if (!fullLearned_) full_ = std::clamp(floor_ / kGameFloor, kFullMinMps, kFullMaxMps);
        }
    }

    bool have_ = false, walking_ = false, lastBlocked_ = false, leadLearned_ = false, fullLearned_ = false;
    float linDist_ = 0.0f, linTiltTime_ = 0.0f, linTime_ = 0.0f, floorDist_ = 0.0f, floorTime_ = 0.0f;
    bool fpOn_ = false;   // a floor pulse followed: pushed from fpT0_ to fpT1_ (< 0: still pushed), the motion it made so far
    double fpT0_ = 0.0, fpT1_ = -1.0, lastOn_ = -1e9;
    float fpDist_ = 0.0f, prevTilt_ = 0.0f;
    uint32_t epoch_ = 0;
    V2 c_, n_, lastDir_, headV_, pulseDir_;
    bool pushing_ = false;
    double pushedSince_ = 0.0;
    double t_ = 0.0, lastPush_ = -1e9, stoppedAt_ = -1e9, walkSince_ = 0.0, stickAt_ = -1e9, probeAt_ = -1e9;
    float heroMps_ = 0.0f;
    float full_ = kFullGuessMps;
    float floor_ = 0.0f;   // the hero's least walk as seen (0: not yet - a quarter of full_)
    float lead_ = kLeadS;  // the lag from our stick to his motion as seen (kLeadS until then)
};

// The stick for the game's pad: the room walk's only where the controller's own stick rests (past the game's dead zone it wins) -
// or, roomFirst, while the room walk walks the hero and the controller's push has not taken over (Output::roomFirst): the room walk's
// then, zero too (between its pulses). Raw XInput units. True = the room walk's stick is the one.
inline bool Merge(int16_t ownLX, int16_t ownLY, int16_t roomLX, int16_t roomLY, int16_t* lx, int16_t* ly, bool roomFirst = false) {
    const bool ownPushed = (long long)ownLX * ownLX + (long long)ownLY * ownLY > 10349ll * 10349ll;
    const bool roomPushed = roomLX != 0 || roomLY != 0;
    if (!roomFirst && (ownPushed || !roomPushed)) { *lx = ownLX; *ly = ownLY; return false; }
    *lx = roomLX; *ly = roomLY;
    return true;
}

inline int16_t Axis(float v) { return (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }

}  // namespace roomwalk
