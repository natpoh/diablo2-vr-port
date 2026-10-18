// flat_raw.h - FlatVR's controllers as the plugin's own controls ([direct] flatvr_controls): BodyWalk's Game Link Tracking with the raw
// controls (version 2, BW_LINK_TRACK_CONTROLS - FlatVR's controllers as held, never BodyWalk's Mapping) made into the frame xr.cpp makes from
// the game's own OpenXR session (D2RVR_XrInput). Direct input's layout ([direct], rebindable in D2R VR Settings) and every F4 gesture then run
// on it in FlatVR as in native OpenXR, and BodyWalk's Mapping is not needed (Serg, 2026-10-17: "у нас в игре уже есть маппинг свой").
// Pure: no clock, no shared memory; the unit test is cleanroom/tests/flat_raw_test.cpp, the wiring vrcam.cpp (FlatRawTick) and link.cpp
// (link::RawTracking, and Head() keeping BodyWalk's head for the camera while it feeds).
//
// What BodyWalk sends (its universal_output.cpp, then game_link.cpp): the head's place in its room (y the height over the floor) and its
// angles - the yaw since its recenter, the room's own yaw, pitch and roll; each hand's place FROM THE HEAD in the room's axes and its
// rotation in the room; trigger and grip 0..1, the stick, and OpenVR's button numbers. Made here: the head in the room (the room yaw, the
// pitch, the roll put back together the way BodyWalk took them apart), each grip at head + offset with its own rotation, the aim = the grip
// (BodyWalk sends no aim pose: the plugin's laser is native OpenXR's, FlatVR has its own), the buttons in Game Link's XrInput bits.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <bodywalk/game_link.h>

#include "d2r_vr_shared.h"   // D2RVR_XrInput

namespace flatraw {

constexpr uint32_t kFreshStamp = 5000;   // 0.1 ms units: a sample older than half a second is no frame

// OpenVR's button numbers as BodyWalk sends them (game_link.h BW_LinkHand::buttons)
constexpr uint64_t kOvrMenu = 1ull << 0, kOvrSecondary = 1ull << 1, kOvrGrip = 1ull << 2, kOvrPrimary = 1ull << 7;
constexpr uint64_t kOvrStickClick = 1ull << 32, kOvrTriggerClick = 1ull << 33;

// A block that carries the raw controllers: version 2 or later with BW_LINK_TRACK_CONTROLS, and the head tracked (the hands are placed from it).
inline bool Usable(const BW_LinkTracking& t) {
    return t.header.version >= 2 && (t.flags & BW_LINK_TRACK_CONTROLS) != 0 && (t.flags & BW_LINK_TRACK_HEAD) != 0 && t.head.valid != 0;
}

// Fresh: sampled within kFreshStamp of now (both on BW_LinkStampNow's clock; a 32-bit difference, so it wraps with the clock).
inline bool Fresh(uint32_t sampleStamp, uint32_t nowStamp) {
    const int32_t age = (int32_t)(nowStamp - sampleStamp);
    return age >= -(int32_t)kFreshStamp && age < (int32_t)kFreshStamp;
}

inline void QMul(const float a[4], const float b[4], float o[4]) {
    const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    o[0] = x; o[1] = y; o[2] = z; o[3] = w;
}

// The head's rotation in BodyWalk's room (OpenXR axes: x right, y up, z back) from its angles: q = Y(yaw) * X(pitch) * Z(r). BodyWalk took
// yaw = atan2(-fwd.x, -fwd.z), pitch = asin(fwd.y) and roll = -asin(right.y); for that q, fwd.y = sin(pitch) and right.y = sin(r) cos(pitch),
// so r = asin(-sin(roll) / cos(pitch)) (clamped: a head looking straight up has no roll to tell).
inline void HeadRot(float yawDeg, float pitchDeg, float rollDeg, float q[4]) {
    constexpr float kRad = 0.0174532925f;
    const float y = yawDeg * kRad, p = std::clamp(pitchDeg, -89.9f, 89.9f) * kRad;
    const float s = std::clamp(-std::sin(rollDeg * kRad) / std::cos(p), -1.0f, 1.0f);
    const float r = std::asin(s);
    const float qy[4] = {0.0f, std::sin(y * 0.5f), 0.0f, std::cos(y * 0.5f)};
    const float qx[4] = {std::sin(p * 0.5f), 0.0f, 0.0f, std::cos(p * 0.5f)};
    const float qz[4] = {0.0f, 0.0f, std::sin(r * 0.5f), std::cos(r * 0.5f)};
    float t[4];
    QMul(qy, qx, t);
    QMul(t, qz, q);
}

// BodyWalk's buttons in the native frame's bits (BW_LINK_XRB_*), the way xr.cpp sets them: the trigger past its click - 0.8 of its pull, or
// BodyWalk's click bit when no pull came with it - and the grip past 0.8 of its squeeze.
inline uint32_t Buttons(uint64_t b, float trigger, float grip) {
    uint32_t o = 0;
    if (b & kOvrPrimary) o |= BW_LINK_XRB_PRIMARY;
    if (b & kOvrSecondary) o |= BW_LINK_XRB_SECONDARY;
    if (b & kOvrStickClick) o |= BW_LINK_XRB_STICK;
    if (b & kOvrMenu) o |= BW_LINK_XRB_MENU;
    if (trigger > 0.8f || ((b & kOvrTriggerClick) && trigger <= 0.0f)) o |= BW_LINK_XRB_TRIGGER;
    if (grip > 0.8f || ((b & kOvrGrip) && grip <= 0.0f)) o |= BW_LINK_XRB_SQUEEZE;
    return o;
}

// The plugin's 32-bit stamp (0.1 ms) as a running time in nanoseconds: direct input's Frame.time takes speeds from it.
struct Clock {
    bool have = false;
    uint32_t last = 0;
    int64_t ns = 0;
};
inline int64_t Step(Clock& c, uint32_t stamp) {
    if (!c.have) { c.have = true; c.last = stamp; c.ns = 0; return c.ns; }
    const int32_t d = (int32_t)(stamp - c.last);   // wraps with the clock
    c.last = stamp;
    if (d > 0) c.ns += (int64_t)d * 100000;          // a stamp that went back (another sender's) adds nothing
    return c.ns;
}

inline float Unit(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }
inline float Axis(float v) { return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f; }

// The frame. focused: a hand tracks - a FlatVR without the headset's input focus reads its hands as not tracked and its controls as let go,
// and direct input then starts a new context (the release gate) as it does when a native session loses the focus.
inline void ToXrInput(const BW_LinkTracking& t, int64_t displayTimeNs, uint32_t counter, uint32_t stampNow, D2RVR_XrInput* out) {
    std::memset(out, 0, sizeof *out);
    out->version = D2RVR_XR_INPUT_VERSION;
    out->counter = counter;
    out->stamp = stampNow;
    out->displayTime = displayTimeNs;
    if (!Usable(t)) return;
    std::memcpy(out->head.pos, t.head.pos, sizeof out->head.pos);
    const float roomYaw = (t.flags & BW_LINK_TRACK_ROOM) ? t.head.yawRoomDeg : t.head.yawDeg;
    HeadRot(roomYaw, (t.flags & BW_LINK_TRACK_PITCH) ? t.head.pitchDeg : 0.0f, (t.flags & BW_LINK_TRACK_ROLL) ? t.head.rollDeg : 0.0f, out->head.rot);
    out->head.valid = 1;
    bool any = false;
    for (int h = 0; h < 2; ++h) {
        const BW_LinkHand& in = h ? t.right : t.left;
        D2RVR_XrHand& o = out->hand[h];
        o.trigger = Unit(in.trigger);
        o.squeeze = Unit(in.grip);
        o.stickX = Axis(in.stickX);
        o.stickY = Axis(in.stickY);
        o.buttons = Buttons(in.buttons, o.trigger, o.squeeze);
        if (in.valid) {
            for (int i = 0; i < 3; ++i) o.grip.pos[i] = t.head.pos[i] + in.pos[i];
            std::memcpy(o.grip.rot, in.rot, sizeof o.grip.rot);
            o.grip.valid = 1;
            o.aim = o.grip;
            any = true;
        }
        o.active = in.valid || o.buttons || o.trigger > 0.0f || o.squeeze > 0.0f || o.stickX != 0.0f || o.stickY != 0.0f ? 1u : 0u;
    }
    out->focused = any ? 1u : 0u;
}

}  // namespace flatraw
