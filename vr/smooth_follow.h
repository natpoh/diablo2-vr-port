#pragma once
// [camera] smooth_follow (our camera's views: first and third person, VR and flat - Eases below): the camera's world anchor - the game camera's look-at, the hero's point on his ground - eased
// over a jerk instead of jumping with it.
//
// Serg, 2026-11-22 (a paladin in Act 3's jungle, Quest 3 over Virtual Desktop): "в игре есть неровности, по которым персонаж прыгает, и камеру
// трясёт; надо, чтобы она плавно поднималась и опускалась или в стороны съезжала, а не резко, если персонажа дёргает." F4's camera stands on the
// look-at (vr/view/camera.cpp VrViewInner: the pivot is the look-at, up by the eye height), and the look-at's height is the floor the game finds
// under the hero (vr/terrain_heights.h: LevelTranslationLayer's "floor under a cell"), which jumps on a bump, a step, a stair, a ledge, a room
// stitched at another height (vr/room_heights.h); across the floor the game shoves him aside now and then (a path corrected, a knockback). All of
// it came into the headset as a jerk of the whole world.
//
// The height follows the floor's always, on the spring (below: Step, "Up / down"). Across the floor what is eased is only the part of a frame's move no
// walking makes: the look-at's move this frame is held against the move its own recent
// velocity predicts, and a change of velocity in one frame within what a hero does (kDvH / kDvV: starting to run, stopping, a right-angle turn
// at a run, a charge, a slope begun - D2's hero does all of these from one frame to the next) is taken at once and exactly; the rest - the jerk -
// is taken off the camera (an offset; that frame the camera does not move with it) and from the next frame that offset goes back to zero on a
// critically damped spring in about `ms` (no overshoot, it starts gently). Walking, running, a charge, a slope: followed exactly, no lag (the offset stays
// 0). A move too big for a jerk - past kSnapH across the floor or kSnapV up / down in a frame (a teleport, a waypoint, a town portal, an area
// change), a frame after a long gap (a loading screen), a recentre (the caller's `snap`) - is taken at once: no glide. The offset never grows
// past kMaxOffH / kMaxOffV (a run of jerks).
//
// Only the anchor (where the hero is) is eased, never the headset's own pose: the head moves 1:1 on top of it. The hands, the weapon and the
// laser hang off the camera and go with it; in VR F4 the drawn body is put under the same eased anchor ([body] under_camera, vr/body/hero_facing.cpp);
// in third person the hero is drawn where the game has him and the camera glides after him.
// Off: the look-at as before. Pure: no game reads, no clock of its own (smooth_follow_test).

#include <algorithm>
#include <cmath>

namespace smoothfollow {

// World units (2 a grid cell, ~0.25 m at a man's height) and seconds.
constexpr float kDvH = 45.0f;   // across the floor: a change of velocity up to this (units/s) from one frame to the next is walking's - a stand to a run
                                //   (Serg's hero ~28 units/s), a right-angle turn at a run (28 * 1.41), a charge
constexpr float kDvV = 6.0f;    // up / down: a slope begun; a floor's step (a bump, a stair) is far past it
constexpr float kSnapH = 4.0f;       // a frame's move across the floor past this (~1 m) is no jerk: a teleport, a waypoint, a portal - at once
constexpr float kSnapV = 12.0f;      // ... and up / down past this (~3 m)
constexpr float kMaxOffH = 4.0f;     // the eased offset is held within these (a run of jerks never leaves the camera far behind)
constexpr float kMaxOffV = 4.0f;
constexpr float kMaxDt = 0.25f;      // a frame after a longer gap than this (a loading screen, a pause) starts afresh
constexpr float kMinDt = 0.002f;     // a step closer than this to the last (a second call in the same game frame) changes nothing
// Never a jerk under these (units), whatever the frame time: a run's frame-to-frame unevenness (the game's frame time against ours, a floor that is
// not quite flat) is followed 1:1 as before; only a real step - a bump, a stair, a shove - is eased (Serg on 0.222.4, flat F3: "бегаю все трясется").
constexpr float kMinJerkH = 0.25f;   // ~6 cm across the floor
constexpr float kMinJerkV = 0.1f;    // ~2.5 cm up / down: no longer a threshold (the height always follows on the spring), only what the log counts as a step
// The walking velocity a frame's move is held against: the walking parts of the last frames, low-passed over this (seconds) - one frame's
// unevenness never becomes the next frame's expected move (taken as it stood, an uneven floor or a frame-time wobble was expected again, the
// next frame "missed" it by twice as much and read as a jerk - the shake of a steady run in 0.222.4's flat F3).
constexpr float kVelTau = 0.06f;
constexpr float kMinMs = 30.0f, kMaxMs = 1000.0f, kDefMs = 500.0f;   // [camera] smooth_follow_ms: the ease's time, its range

// Which views are eased (Serg 2026-11-23: "она должна работать и в 2д виде"): every view our camera builds standing on the look-at - first person
// (VR F4, VR F5, flat F3 on the monitor) and third person (VR F2, flat F2), native OpenXR or FlatVR. Not eased:
//  - the game's own camera (flat F1, VR F1 without Real geometry): vrcam does not build it, and it is orthographic from above - the game draws a
//    height along its own view (flat_ground.h), so a floor's step moves the look-at along the view and the picture not at all;
//  - the table (VR F3: the diorama laid on the room's floor) and the view from above through our camera (F1 Real geometry, native 3D F1): their
//    cameras are the room's / the game's look from above, not the hero's eyes.
enum class ViewKind { GameOwn, FirstPerson, ThirdPerson, Table, Above };
inline bool Eases(ViewKind v) { return v == ViewKind::FirstPerson || v == ViewKind::ThirdPerson; }

// The views from inside (Serg 2026-11-23, after 0.222.3: "плавная камера это не только голова но и вся верхняя часть тела на виде изнутри", "до пояса"):
// VR F4, VR F5 and flat F3 - there the drawn upper body, down to the waist, glides with the camera (UpperWeights below), the hips and legs stay on
// the hero. Behind him (VR F2, flat F2) the hero is drawn where the game has him and only the camera glides.
inline bool Inside(ViewKind v) { return v == ViewKind::FirstPerson; }

// A world direction (the eased offset) in the hero's model axes: dotted with each row of his model-to-world `m` (row vectors, p' = p * M) over its
// squared length - the rows carry his scale. False (out zero): a row with no length, or not a number.
inline bool ToModel(const float m[16], const float d[3], float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    float r[3];
    for (int i = 0; i < 3; ++i) {
        const float* row = m + i * 4;
        const float l2 = row[0] * row[0] + row[1] * row[1] + row[2] * row[2];
        if (!(l2 > 1e-8f)) return false;
        r[i] = (d[0] * row[0] + d[1] * row[1] + d[2] * row[2]) / l2;
    }
    if (!std::isfinite(r[0]) || !std::isfinite(r[1]) || !std::isfinite(r[2])) return false;
    out[0] = r[0]; out[1] = r[1]; out[2] = r[2];
    return true;
}

// How much of the offset a bone at bind height `y` takes, between the pelvis (0) and the chest (1), smoothly (no kink at either end): the trunk
// stretches over the belly, nothing tears at the waist. A chest not above the pelvis: all of it.
inline float WaistWeight(float y, float pelvisY, float chestY) {
    if (!(chestY > pelvisY + 1e-4f)) return 1.0f;
    const float t = std::clamp((y - pelvisY) / (chestY - pelvisY), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Each bone's share of the offset (w, n of them): 0 for the hips and the legs (upper[i] 0: not above the pelvis), for a bone above them its own
// height's WaistWeight, and never less than its parent's - the collarbones, arms, hands and what they hold, the neck and the head take the chest's
// whole share even where they hang lower than it. Parents come before their children (as the rig's own order); parent -1: none.
inline void UpperWeights(int n, const int* parent, const char* upper, const float* bindY, float pelvisY, float chestY, float* w) {
    for (int i = 0; i < n; ++i) {
        if (!upper[i]) { w[i] = 0.0f; continue; }
        float own = WaistWeight(bindY[i], pelvisY, chestY);
        const int p = parent[i];
        if (p >= 0 && p < i && upper[p]) own = std::max(own, w[p]);
        w[i] = own;
    }
}

// The ease's time for [camera] smooth_follow_ms, within its range; not a number: the default.
inline float Ms(float ms) { return std::isfinite(ms) ? std::clamp(ms, kMinMs, kMaxMs) : kDefMs; }

enum class Did { Started, Followed, Eased, Snapped };

struct State {
    bool has = false;
    float last[3] = {};   // the look-at the last step had
    float vel[3] = {};    // the velocity of the look-at's walking part (jerks taken out, low-passed over kVelTau), units/s
    float off[3] = {};    // the camera's anchor minus the look-at (the eased jerk)
    float offVel[3] = {}; // ... and its velocity, units/s (the spring's)
    // Across the floor, after the jerks (Track): the camera's own place and velocity (x, z), a second-order tracker of the walking look-at - a steady
    // run is followed with no lag, its frame-to-frame unevenness (the game's ticks, its frame pacing) smoothed out.
    bool trackOk = false;
    float track[2][2] = {}, trackVel[2][2] = {};   // [stage][x, z]: two trackers one after the other (Track)
    float total[3] = {};  // what the frame's anchor is off the look-at, all of it (the readers Apply this)
    float prevOff[2] = {};  // the jerk offset across the floor (x, z) the last frame had: the tracker's last target
};

inline void Reset(State& s) { s = State{}; }

// The anchor for a reader of the frame (the view's passes, the pose): the look-at it has, plus the offset the frame's step left. No state set:
// the look-at as it is.
// The game draws a height pushed along its own view (vr/flat_ground.h): the look-at stands at (x + kShear y, y, z + kShear y) over the ground's (x, z).
// So a step of the floor's height moves it across the floor too - a 0.85 step onto a bridge ~1 unit along the view's diagonal, sideways to most
// runs (Serg on 0.222.6: "рывки вправо влево", "при ходьбе когда я захожу на мост"; "в игре много может быть таких мест"). The state works on the
// ground's own place (Unshear) and puts the push back by the eased height (Shear): the sideways part of a step glides with its height, everywhere.
constexpr float kShear = 1.22474487139f;   // = flatground::kShear (camera.cpp holds them equal)
inline void Unshear(const float p[3], float g[3]) { g[0] = p[0] - kShear * p[1]; g[1] = p[1]; g[2] = p[2] - kShear * p[1]; }
inline void Shear(const float g[3], float p[3]) { p[0] = g[0] + kShear * g[1]; p[1] = g[1]; p[2] = g[2] + kShear * g[1]; }
inline void Apply(const State& s, const float raw[3], float out[3]) {
    float d[3] = {0.0f, 0.0f, 0.0f};
    if (s.has) Shear(s.total, d);   // (the offset kept on the ground's terms, pushed as the game pushes a height: linear)
    for (int i = 0; i < 3; ++i) out[i] = raw[i] + d[i];
}

// The drawn body from inside, against the eased camera (Serg on 0.222.7, VR F4: "тело не плавное и трясется", F5 - no body - fine): the offset in two
// parts that add up to the camera's exactly (world units). `whole` - across the floor, the tracker's evenness and the glide of a snap aside: the whole
// body takes it, the legs too (left on the hero they stepped in the game's 25 Hz ticks under a camera that no longer did, the belly stretching
// between); `upper` - the height and the push a height has along the game's view: the upper body takes it down to the waist (UpperWeights), the legs
// stay on the floor. Nothing set: both zero.
inline void Split(const State& s, float whole[3], float upper[3]) {
    whole[0] = whole[1] = whole[2] = upper[0] = upper[1] = upper[2] = 0.0f;
    if (!s.has) return;
    whole[0] = s.total[0]; whole[2] = s.total[2];
    const float h[3] = {0.0f, s.total[1], 0.0f};
    Shear(h, upper);
}

// Across the floor, the evenness (Serg on 0.222.6 at smooth_follow_ms 1000, flat: "всё равно всё дёргается как и раньше" - his log: the height damped
// to 0.13 a frame while the floor stepped 0.85, but his run's speed spread 15 - 35 % frame to frame, 28 units/s mean): the jerk-eased look-at (x, z)
// followed by a critically damped tracker of the second order (an alpha-beta filter, Brown's double smoothing: theta = e^(-w dt), alpha = 1 - theta^2,
// beta = (1 - theta)^2): it predicts the place by its velocity and takes alpha of what the frame is off that, its velocity beta of it - a steady run
// is predicted exactly and followed with no lag at all, whatever the frame time; what is uneven from frame to frame is smoothed; a start or a stop
// lags or overshoots by a fraction of dv / w, then settles. Two of them one after the other (each passes a share of a frame's unevenness straight
// on - its alpha - two pass its square). Their time is half the ease's (TrackMs): 100 ms at the default 200, 500 at 1000.
constexpr float kTrackShare = 0.5f;
inline float TrackMs(float ms) { return Ms(ms) * kTrackShare; }
inline void Track(State& s, const float from[2], const float to[2], float dt, float ms) {
    if (!s.trackOk || !(dt > 0.0f)) {
        s.trackOk = true;
        for (int st = 0; st < 2; ++st)
            for (int i = 0; i < 2; ++i) {
                s.track[st][i] = to[i];
                s.trackVel[st][i] = dt > 0.0f ? (to[i] - from[i]) / dt : 0.0f;   // (this frame's walking move: a run met mid-stride is not started from rest)
            }
        return;
    }
    const float w = 4.0f / (TrackMs(ms) * 0.001f);
    const float th = std::exp(-w * dt), alpha = 1.0f - th * th, beta = (1.0f - th) * (1.0f - th);
    for (int st = 0; st < 2; ++st)   // the second takes the first's place: a tracker passes a step's share at once, two pass its square
        for (int i = 0; i < 2; ++i) {
            const float target = st == 0 ? to[i] : s.track[0][i];
            const float pred = s.track[st][i] + s.trackVel[st][i] * dt;
            const float r = target - pred;
            s.track[st][i] = pred + alpha * r;
            s.trackVel[st][i] += beta * r / dt;
        }
}

// What a step did, for the caller's log: the jerk taken off (units, the part eased) and the move this frame.
struct Result {
    Did did = Did::Started;
    float jerkH = 0.0f, jerkV = 0.0f;   // this frame's jerk eased: across the floor (length), up (+) / down (-)
    float moveH = 0.0f, moveV = 0.0f;   // this frame's whole move of the look-at
};

// The anchor for this frame: `raw` the look-at now, `dt` the seconds since the last step, `ms` the ease's time, `snap` take it at once (a recentre,
// the view just taken). `out` = raw + the offset. Once a game frame, at the same point of it (vr/view/camera.cpp VrFrame: the frame time is the
// game's own, never the moment some reader came - a reader's time jumped about the frame and a steady run read as jerks, 0.222.4); the readers Apply.
inline Result StepGround(State& s, const float raw[3], float dt, float ms, bool snap, float out[3]) {   // (on the ground's terms: Step below)
    Result r;
    const bool finite = std::isfinite(raw[0]) && std::isfinite(raw[1]) && std::isfinite(raw[2]);
    if (!finite) {   // no anchor to ease: the look-at as it is, and the next good one starts afresh
        Reset(s);
        out[0] = raw[0]; out[1] = raw[1]; out[2] = raw[2];
        r.did = Did::Snapped;
        return r;
    }
    const float d[3] = {raw[0] - s.last[0], raw[1] - s.last[1], raw[2] - s.last[2]};
    r.moveH = std::sqrt(d[0] * d[0] + d[2] * d[2]);
    r.moveV = d[1];
    const bool fresh = !s.has;
    if (!fresh && !snap && dt > 0.0f && dt < kMinDt && r.moveH <= kSnapH && std::fabs(r.moveV) <= kSnapV) {   // the same game frame again: as it stands
        for (int i = 0; i < 3; ++i) out[i] = raw[i] + (s.has ? s.total[i] : 0.0f);
        r.did = Did::Followed;
        return r;
    }
    if (fresh || snap || !(dt > 0.0f) || !std::isfinite(dt) || dt > kMaxDt || r.moveH > kSnapH || std::fabs(r.moveV) > kSnapV) {
        const bool had = s.has;
        Reset(s);
        s.has = true;
        s.last[0] = raw[0]; s.last[1] = raw[1]; s.last[2] = raw[2];
        out[0] = raw[0]; out[1] = raw[1]; out[2] = raw[2];
        r.did = had ? Did::Snapped : Did::Started;
        if (!had) r.moveH = r.moveV = 0.0f;
        return r;
    }
    // The move its walking predicts, and how far this frame's is off it.
    float dev[3];
    for (int i = 0; i < 3; ++i) dev[i] = d[i] - s.vel[i] * dt;
    // Across the floor, sideways: the part of the deviation across his way (all of it while he stands) past kDvH * dt is a jerk - a snap aside
    // (a bridge's or a ledge's edge, a path node, a push off a wall or a monster): it goes to the offset and glides from rest. Along his way the
    // deviation is left to the tracker below: a run moved in the game's ticks stands still a frame and jumps the next, along the way, again and
    // again (an offset of its own took those for jerks one after another and left the camera metres behind at a long ease, 0.222.7's first try).
    float jerk[3] = {0.0f, 0.0f, 0.0f};
    float asideJerk = 0.0f;
    {
        const float vl = std::sqrt(s.vel[0] * s.vel[0] + s.vel[2] * s.vel[2]);
        float ax = 0.0f, az = 0.0f;   // his way across the floor (none while he stands: all of a move is across it)
        if (vl > 2.0f) { ax = s.vel[0] / vl; az = s.vel[2] / vl; }
        const float along = dev[0] * ax + dev[2] * az;
        const float cx = dev[0] - along * ax, cz = dev[2] - along * az;
        const float tol = std::max(kDvH * dt, kMinJerkH), len = std::sqrt(cx * cx + cz * cz);
        if (len > tol) {
            const float k = 1.0f - tol / len;
            jerk[0] = cx * k; jerk[2] = cz * k;
            asideJerk = len - tol;
        }
    }
    // Up / down, all of it (Serg on 0.222.4: "нет плавной камеры", "на виде ф2 тоже все трясется"): the hero never moves up or down by his own will -
    // the height is the floor the game finds under him, and on the jungle's uneven ground it changes in small steps nearly every frame of a run,
    // each under any jerk's threshold, so the camera shook with them. The camera's height follows the floor's on the spring alone (critically
    // damped, ~ms: no overshoot); a slope is followed a few centimetres behind (2 v / w), a step or a bump glides. A Leap's arc is not in the
    // look-at (vrcam's JumpLift, under_camera) and is not touched.
    jerk[1] = d[1];
    // The offset of the jerks before this frame back toward zero, critically damped: x(t) = (x0 + (v0 + w x0) t) e^(-w t), w so that ~1 % is left at `ms` (exact for any dt).
    const float w = 6.6f / (Ms(ms) * 0.001f), e = std::exp(-w * dt);
    for (int i = 0; i < 3; ++i) {
        const float x0 = s.off[i], v0 = s.offVel[i], c = v0 + w * x0;
        s.off[i] = (x0 + c * dt) * e;
        s.offVel[i] = (v0 - w * c * dt) * e;
    }
    // The walking part feeds the velocity (low-passed, kVelTau); the jerk goes to the offset (the camera does not move with it this frame; the spring
    // takes it from the next).
    const float a = dt / (kVelTau + dt);
    for (int i = 0; i < 3; ++i) {
        s.vel[i] += ((d[i] - jerk[i]) / dt - s.vel[i]) * a;
        s.off[i] -= jerk[i];
    }
    r.jerkH = asideJerk;
    r.jerkV = jerk[1];
    {   // held within the bounds (the spring's velocity with it, so it does not push on past)
        const float h = std::sqrt(s.off[0] * s.off[0] + s.off[2] * s.off[2]);
        if (h > kMaxOffH) { const float k = kMaxOffH / h; s.off[0] *= k; s.off[2] *= k; s.offVel[0] = s.offVel[2] = 0.0f; }
        if (std::fabs(s.off[1]) > kMaxOffV) { s.off[1] = std::copysign(kMaxOffV, s.off[1]); s.offVel[1] = 0.0f; }
    }
    // across the floor: the jerk-eased look-at, made even by the tracker (its target moved from the last frame's to this one's)
    {
        const float from[2] = {s.last[0] + s.prevOff[0], s.last[2] + s.prevOff[1]};
        const float to[2] = {raw[0] + s.off[0], raw[2] + s.off[2]};
        Track(s, from, to, dt, ms);
        s.prevOff[0] = s.off[0]; s.prevOff[1] = s.off[2];
        float dx = s.track[1][0] - raw[0], dz = s.track[1][1] - raw[2];
        const float l = std::sqrt(dx * dx + dz * dz);
        if (l > kMaxOffH) {   // held within the bounds: both stages pulled in by as much
            const float k = kMaxOffH / l;
            for (int st = 0; st < 2; ++st) {
                s.track[st][0] = raw[0] + (s.track[st][0] - raw[0]) * k;
                s.track[st][1] = raw[2] + (s.track[st][1] - raw[2]) * k;
            }
            dx *= k; dz *= k;
        }
        s.total[0] = dx; s.total[1] = s.off[1]; s.total[2] = dz;
    }
    s.last[0] = raw[0]; s.last[1] = raw[1]; s.last[2] = raw[2];
    for (int i = 0; i < 3; ++i) out[i] = raw[i] + s.total[i];
    r.did = (r.jerkH > 1e-4f || std::fabs(r.jerkV) > kMinJerkV) ? Did::Eased : Did::Followed;   // (the floor's small steps every frame: followed)
    return r;
}


// The anchor for this frame (as StepGround, on the look-at as the game has it): unpushed to the ground's terms, stepped, pushed back by the eased height.
inline Result Step(State& s, const float raw[3], float dt, float ms, bool snap, float out[3]) {
    float g[3], og[3];
    Unshear(raw, g);
    const Result r = StepGround(s, g, dt, ms, snap, og);
    Shear(og, out);
    return r;
}

}  // namespace smoothfollow
