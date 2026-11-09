// grab_share.h - [multiplayer] pose_share_grab: a body one player's hand holds is seen held by everyone in the game. The author (2026-11-08): «там была точка касания
// и куклу должен был рисовать регдол самого зрителя» - the touch point goes over the wire, and each viewer's OWN ragdoll (the knock-down library, vr/knock_down.h:
// [weight] grab_throw's hold) draws the doll hanging from the holder's hand.
//
// What goes: while a hand holds a body for the knock-down library, every pose tick a "hold" - the monster's unit id (the server's, the same on every PC), the limb
// (a D2RF_* joint of the humanoid map - knockpose's, made from the model's own bones, the same on every PC for the same model), the touch point in that limb's own
// frame (in limb lengths: each PC draws the world at its own player's scale), the hand, the palm (metres in the holder's hero's frame, vr/pose_share.h - as the
// pose's hands), the frame's heading and the holder's eye height, the palm's velocity; on the let-go a "release" with the palm's velocity, repeated a few ticks
// (a message may be lost). On its own message ids on the pose_share channel (kMsgGrab to the host, kMsgGrabRelay from it), with its own first byte (kFormat):
// an older build ignores both ids (its host counts them as bad and drops them, its client drops any id it does not know), and its poses are untouched - the
// pose format (poseshare::kVersion, kToken) is not bumped, so builds with and without this still share poses.
//
// What a viewer does (vr/mp/pose_share.cpp, F4 only): plays the holds back as far behind as the holder's poses (GrabTrack), puts the palm in its own world
// beside the holder's hero (FeetOf, as the pose's markers), and hands it to its own library (knockdown::RemoteTake / RemoteHandAt / RemoteLetGo through Viewer):
// the body taken at the same limb and point, held at the palm each tick, let go with the palm's velocity. A viewer whose game has not got that monster near (not
// loaded, too far, not a humanoid it read) takes nothing - it tries again a few times a second while the hold lasts and lets the hold pass.
// Visual only, on every PC (host or client): the monster's real place, the game's state and the server are never touched - the viewer asks no knockback for it.
//
// Knocks (the author, 2026-11-08: «если один игрок толкнул или кинул монстра, другие тоже должны это видеть»): a throw is the release above; a blow of ours that knocks
// a humanoid down, staggers it or shoves it (the knock-down library's KnockDown from our weapon's push, a leap's landing too) goes as ONE event on the same messages
// (state kKnock): the monster, the limb and the point the blow met (as a hold's touch point), the velocity it gives the body (the impulse over its weight - each PC
// weighs the body itself), its kind (blow, sweep, shove, stagger), the time. Sent kKnockRepeats ticks running, taken once (by its serial); played on the viewer as
// far behind as the striker's poses (KnockQueue) - its own ragdoll falls the same way. Not a stream of the body: the fall, the lying and the getting up are each
// viewer's library's.
//
// Pure: no game calls, no globals, no network. Tested by cleanroom/tests/grab_share_test.cpp.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "pose_share.h"

namespace grabshare {

// ---- the wire ---------------------------------------------------------------------------------------------------------------------------------

constexpr uint16_t kMsgGrab = 6;        // a player's holds to the host (on poseshare::kChannel; poseshare::MsgId 1 - 5 are the poses')
constexpr uint16_t kMsgGrabRelay = 7;   // the host's: the other players' holds
constexpr uint8_t kFormat = 1;          // the first byte of both (the grab's own; the poses keep theirs)
constexpr int kGrabRelayMax = 4;        // holds in one relay at most

enum State : uint8_t { kHold = 1, kRelease = 2, kKnock = 3 };

// The humanoid's joints (shared/d2r_vr_fall.h's D2RF_*: knock_down.cpp checks they match).
constexpr int kJoints = 18;
enum Joint : int {
    kPelvis = 0, kChest, kNeck, kHead, kLShoulder, kLElbow, kLWrist, kRShoulder, kRElbow, kRWrist,
    kLHip, kLKnee, kLAnkle, kLToe, kRHip, kRKnee, kRAnkle, kRToe
};

struct Hold {
    uint8_t side = 0;          // the holder's hand: 0 right, 1 left
    uint8_t state = kHold;
    uint8_t serial = 0;        // this hold's number (a new hold of the same hand - another body, a re-grip - has a new one)
    uint8_t joint = 0;         // the limb held (Joint)
    uint32_t monster = 0;      // the game's unit id
    uint16_t seq = 0;          // the sender's count of holds sent (wraps)
    uint32_t ms = 0;           // the sender's clock (posewire::NowMs's low 32 bits, as the poses'): a release's is the let-go's
    float local[3] = {};       // the touch point in the limb's frame (ToLimb), limb lengths
    float palm[3] = {};        // metres in the holder's hero's frame (vr/pose_share.h)
    float heading = 0.0f;      // radians: that frame's turn in the game's world
    float eyeM = 1.6f;         // the holder's eye height, metres (the receiver scales the palm by its hero's eye over it)
    float vel[3] = {};         // m/s: the palm's velocity in the hero frame's axes (the throw's, on a release); kKnock: the velocity the blow gives the body
    uint8_t kind = 0;          // kKnock: D2RF_BLOW 0, SWEEP 1, SHOVE 2, STAGGER 3 (palm and side unused)
};

constexpr uint32_t kHoldBytes = 35;   // side / state, serial, joint, monster, seq, ms, local, palm, heading, eye, velocity
constexpr int kGrabMsgMax = 4;                                                      // entries in our message: both hands and two knocks
constexpr uint32_t kGrabMsgMaxBytes = 6 + kGrabMsgMax * kHoldBytes;               // format, count, unit, the entries
constexpr uint32_t kGrabRelayMaxBytes = 2 + kGrabRelayMax * (4 + kHoldBytes);     // format, count, (unit, hold) each
static_assert(kGrabMsgMaxBytes <= poseshare::kMaxPayload && kGrabRelayMaxBytes <= poseshare::kMaxPayload, "the grab's messages fit D2RLoader's payload");

inline int16_t Q16(float v, float scale) {
    if (!std::isfinite(v)) return 0;
    return (int16_t)std::clamp(std::round(v * scale), -32767.0f, 32767.0f);
}

inline void PackHold(const Hold& h, uint8_t* b) {
    using namespace poseshare;
    b[0] = (uint8_t)((h.side & 1) | ((h.state & 3) << 1) | ((h.kind & 3) << 3));
    b[1] = h.serial;
    b[2] = h.joint;
    Put32(b + 3, h.monster);
    Put16(b + 7, h.seq);
    Put32(b + 9, h.ms);
    for (int i = 0; i < 3; ++i) Put16(b + 13 + 2 * i, (uint16_t)Q16(h.local[i], 1000.0f));
    for (int i = 0; i < 3; ++i) Put16(b + 19 + 2 * i, (uint16_t)QuantMm(h.palm[i]));
    Put16(b + 25, QuantAngle(h.heading));
    Put16(b + 27, (uint16_t)std::lround(std::isfinite(h.eyeM) ? std::clamp(h.eyeM * 1000.0f, 0.0f, 65535.0f) : 0.0f));
    for (int i = 0; i < 3; ++i) Put16(b + 29 + 2 * i, (uint16_t)Q16(h.vel[i], 1000.0f));
}
// False for what is no hold (a state, a hand or a limb it cannot be).
inline bool UnpackHold(const uint8_t* b, Hold* h) {
    using namespace poseshare;
    *h = Hold{};
    h->side = b[0] & 1;
    h->state = (uint8_t)((b[0] >> 1) & 3);
    h->kind = (uint8_t)((b[0] >> 3) & 3);
    h->serial = b[1];
    h->joint = b[2];
    h->monster = Get32(b + 3);
    h->seq = Get16(b + 7);
    h->ms = Get32(b + 9);
    for (int i = 0; i < 3; ++i) h->local[i] = (float)(int16_t)Get16(b + 13 + 2 * i) * 0.001f;
    for (int i = 0; i < 3; ++i) h->palm[i] = DequantMm((int16_t)Get16(b + 19 + 2 * i));
    h->heading = DequantAngle(Get16(b + 25));
    h->eyeM = (float)Get16(b + 27) * 0.001f;
    for (int i = 0; i < 3; ++i) h->vel[i] = (float)(int16_t)Get16(b + 29 + 2 * i) * 0.001f;
    return (h->state == kHold || h->state == kRelease || h->state == kKnock) && h->joint < kJoints && h->monster != 0;
}

// Our holds and knocks to the host: unit our hero's id (as the pose message's). n 1..kGrabMsgMax. Returns the bytes, 0 when they do not fit or there are none.
inline uint32_t BuildGrabMsg(uint32_t unit, const Hold* h, int n, uint8_t* out, uint32_t cap) {
    if (n < 1 || n > kGrabMsgMax || cap < 6 + (uint32_t)n * kHoldBytes) return 0;
    out[0] = kFormat;
    out[1] = (uint8_t)n;
    poseshare::Put32(out + 2, unit);
    for (int i = 0; i < n; ++i) PackHold(h[i], out + 6 + i * kHoldBytes);
    return 6 + (uint32_t)n * kHoldBytes;
}
// How many holds (0: not a grab message of this format, or a bad one).
inline int ParseGrabMsg(const void* data, uint32_t size, uint32_t* unit, Hold* out, int cap) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size < 6 || b[0] != kFormat) return 0;
    const int n = b[1];
    if (n < 1 || n > kGrabMsgMax || n > cap || size < 6 + (uint32_t)n * kHoldBytes) return 0;
    *unit = poseshare::Get32(b + 2);
    for (int i = 0; i < n; ++i)
        if (!UnpackHold(b + 6 + i * kHoldBytes, &out[i])) return 0;
    return *unit ? n : 0;
}

struct RelayEntry { uint32_t unit = 0; Hold hold; };
inline uint32_t BuildGrabRelay(const RelayEntry* e, int n, uint8_t* out, uint32_t cap, int* taken = nullptr) {
    if (taken) *taken = 0;
    const int k = std::min(n, kGrabRelayMax);
    if (k < 1 || cap < 2 + (uint32_t)k * (4 + kHoldBytes)) return 0;
    out[0] = kFormat;
    out[1] = (uint8_t)k;
    for (int i = 0; i < k; ++i) {
        uint8_t* b = out + 2 + i * (4 + kHoldBytes);
        poseshare::Put32(b, e[i].unit);
        PackHold(e[i].hold, b + 4);
    }
    if (taken) *taken = k;
    return 2 + (uint32_t)k * (4 + kHoldBytes);
}
inline int ParseGrabRelay(const void* data, uint32_t size, RelayEntry* out, int cap) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size < 2 || b[0] != kFormat) return 0;
    const int n = b[1];
    if (n < 1 || n > kGrabRelayMax || n > cap || size < 2 + (uint32_t)n * (4 + kHoldBytes)) return 0;
    for (int i = 0; i < n; ++i) {
        const uint8_t* e = b + 2 + i * (4 + kHoldBytes);
        out[i].unit = poseshare::Get32(e);
        if (!out[i].unit || !UnpackHold(e + 4, &out[i].hold)) return 0;
    }
    return n;
}

// ---- the limb's frame: the touch point from the model's own joints ------------------------------------------------------------------------------

// The limb of joint j: from it to the joint below it (the pelvis to the chest, the shoulder to the elbow, the knee to the ankle), an end joint (the head,
// a wrist, a toe) on along the bone that comes into it. -1: none.
inline int ChildOf(int j) {
    static const int8_t c[kJoints] = {kChest, kNeck, kHead, -1, kLElbow, kLWrist, -1, kRElbow, kRWrist, -1, kLKnee, kLAnkle, kLToe, -1, kRKnee, kRAnkle, kRToe, -1};
    return j >= 0 && j < kJoints ? c[j] : -1;
}
inline int ParentOf(int j) {
    static const int8_t p[kJoints] = {-1, kPelvis, kChest, kNeck, kNeck, kLShoulder, kLElbow, kNeck, kRShoulder, kRElbow, kPelvis, kLHip, kLKnee, kLAnkle,
                                      kPelvis, kRHip, kRKnee, kRAnkle};
    return j >= 0 && j < kJoints ? p[j] : -1;
}

// The limb's frame: its origin (joint j), x along the limb (unit), y across the body (the shoulders' line for the trunk and arms, the hips' for the legs - the
// other pair, then the world's up, when that lies along the limb), z = x cross y; *len the limb's length (m). False: no frame (joints that coincide).
inline bool LimbFrame(const float (*jt)[3], int j, float o[3], float x[3], float y[3], float z[3], float* len) {
    if (!jt || j < 0 || j >= kJoints) return false;
    const int c = ChildOf(j), p = ParentOf(j);
    float a[3];
    if (c >= 0) for (int k = 0; k < 3; ++k) a[k] = jt[c][k] - jt[j][k];
    else if (p >= 0) for (int k = 0; k < 3; ++k) a[k] = jt[j][k] - jt[p][k];
    else return false;
    const float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (!(l > 1e-4f) || !std::isfinite(l)) return false;
    for (int k = 0; k < 3; ++k) { o[k] = jt[j][k]; x[k] = a[k] / l; }
    *len = l;
    const bool legs = j >= kLHip;
    const int pairs[2][2] = {{legs ? kRHip : kRShoulder, legs ? kLHip : kLShoulder}, {legs ? kRShoulder : kRHip, legs ? kLShoulder : kLHip}};
    for (int t = 0; t < 3; ++t) {
        float s[3];
        if (t < 2) for (int k = 0; k < 3; ++k) s[k] = jt[pairs[t][0]][k] - jt[pairs[t][1]][k];
        else { s[0] = 0.0f; s[1] = 1.0f; s[2] = 0.0f; }
        const float d = s[0] * x[0] + s[1] * x[1] + s[2] * x[2];
        for (int k = 0; k < 3; ++k) s[k] -= d * x[k];
        const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
        if (!(sl > 1e-3f * std::max(1.0f, l)) || !std::isfinite(sl)) continue;
        for (int k = 0; k < 3; ++k) y[k] = s[k] / sl;
        z[0] = x[1] * y[2] - x[2] * y[1];
        z[1] = x[2] * y[0] - x[0] * y[2];
        z[2] = x[0] * y[1] - x[1] * y[0];
        return true;
    }
    return false;
}
// A point (the joints' world) into limb j's frame, limb lengths - and back.
inline bool ToLimb(const float (*jt)[3], int j, const float p[3], float out[3]) {
    float o[3], x[3], y[3], z[3], l = 0.0f;
    if (!LimbFrame(jt, j, o, x, y, z, &l)) return false;
    const float d[3] = {p[0] - o[0], p[1] - o[1], p[2] - o[2]};
    out[0] = (d[0] * x[0] + d[1] * x[1] + d[2] * x[2]) / l;
    out[1] = (d[0] * y[0] + d[1] * y[1] + d[2] * y[2]) / l;
    out[2] = (d[0] * z[0] + d[1] * z[1] + d[2] * z[2]) / l;
    return std::isfinite(out[0] + out[1] + out[2]);
}
inline bool FromLimb(const float (*jt)[3], int j, const float local[3], float out[3]) {
    float o[3], x[3], y[3], z[3], l = 0.0f;
    if (!LimbFrame(jt, j, o, x, y, z, &l)) return false;
    for (int k = 0; k < 3; ++k) out[k] = o[k] + (local[0] * x[k] + local[1] * y[k] + local[2] * z[k]) * l;
    return std::isfinite(out[0] + out[1] + out[2]);
}
// The limb nearest a point (its segment: the joint to the one below it, an end joint's as long as the bone into it). have: bit j - joint j is the model's own
// (a limb with either end guessed is taken only when no other is). -1: none.
inline int NearestLimb(const float (*jt)[3], uint32_t have, const float p[3]) {
    int best = -1;
    float bestD = 1e30f;
    bool bestOwn = false;
    for (int j = 0; j < kJoints; ++j) {
        float o[3], x[3], y[3], z[3], l = 0.0f;
        if (!LimbFrame(jt, j, o, x, y, z, &l)) continue;
        const int c = ChildOf(j);
        const bool own = (have >> j & 1u) && (c < 0 || (have >> c & 1u));
        float t = 0.0f;
        for (int k = 0; k < 3; ++k) t += (p[k] - o[k]) * x[k];
        t = std::clamp(t, 0.0f, l);
        float d2 = 0.0f;
        for (int k = 0; k < 3; ++k) { const float q = o[k] + x[k] * t - p[k]; d2 += q * q; }
        if ((own && !bestOwn) || (own == bestOwn && d2 < bestD)) { best = j; bestD = d2; bestOwn = own; }
    }
    return best;
}

// ---- the holder: what to send each tick ---------------------------------------------------------------------------------------------------------

constexpr int kReleaseRepeats = 4;     // a release is sent this many ticks running (a message may be lost; the viewer takes it once)
constexpr float kVelEaseS = 0.05f;     // the palm's velocity eased over this (the controllers' jitter)

// One hand as the holder has it this tick.
struct Own {
    bool held = false;
    uint32_t monster = 0;
    int joint = 0;
    float local[3] = {};
    float palm[3] = {};      // metres, our hero's frame
    float heading = 0.0f;
    float eyeM = 1.6f;
    float feet[3] = {};      // the hero frame's origin in the world, metres (world units over the units a metre): the palm's velocity is the world's - the frame
                             // turns with the head (a head turned is no throw) and walks with the hero (a throw on the run carries the run)
};

class Sender {
public:
    // Both hands now (own[0] right, [1] left), our clock ms. The holds to send this tick into out (room for 2); how many.
    int Feed(const Own own[2], uint32_t ms, Hold out[2]) {
        int n = 0;
        for (int s = 0; s < 2; ++s) {
            Side& sd = s_[s];
            const Own& o = own[s];
            if (o.held && o.monster && o.joint >= 0 && o.joint < kJoints) {
                if (!sd.was || sd.last.monster != o.monster || sd.last.joint != (uint8_t)o.joint) {   // a new hold (or another body): a new number, the velocity afresh
                    sd.last = Hold{};
                    sd.last.serial = ++serial_;
                    sd.havePrev = false;
                    for (float& v : sd.vel) v = 0.0f;
                }
                float w[3];   // the palm in the world's axes, metres (the velocity is taken there)
                poseshare::DirToWorld(o.palm, o.heading, w);
                for (int k = 0; k < 3; ++k) w[k] += o.feet[k];
                if (sd.havePrev && ms != sd.prevMs) {
                    const float dt = (float)(uint32_t)(ms - sd.prevMs) * 0.001f;
                    if (dt > 0.0f && dt < 0.5f) {
                        for (int k = 0; k < 3; ++k) {
                            const float v = (w[k] - sd.prevPalm[k]) / dt;
                            sd.vel[k] += (v - sd.vel[k]) * std::min(1.0f, dt / kVelEaseS);
                        }
                    } else {
                        for (float& v : sd.vel) v = 0.0f;
                    }
                }
                std::memcpy(sd.prevPalm, w, sizeof sd.prevPalm);
                sd.prevMs = ms;
                sd.havePrev = true;
                Hold& h = sd.last;
                h.side = (uint8_t)s;
                h.state = kHold;
                h.joint = (uint8_t)o.joint;
                h.monster = o.monster;
                h.ms = ms;
                std::memcpy(h.local, o.local, sizeof h.local);
                std::memcpy(h.palm, o.palm, sizeof h.palm);
                h.heading = o.heading;
                h.eyeM = o.eyeM;
                poseshare::DirToHero(sd.vel, o.heading, h.vel);   // (sent in the hero frame's axes of this moment, as the palm)
                sd.was = true;
                sd.releaseLeft = 0;
            } else if (sd.was) {   // let go this tick: the release, with the palm's last velocity (and the palm where it left)
                sd.was = false;
                sd.last.state = kRelease;
                sd.last.ms = ms;
                poseshare::DirToHero(sd.vel, sd.last.heading, sd.last.vel);
                sd.releaseLeft = kReleaseRepeats;
                sd.havePrev = false;
            }
            if (sd.was || sd.releaseLeft > 0) {
                if (!sd.was) --sd.releaseLeft;
                sd.last.seq = ++seq_;
                out[n++] = sd.last;
            }
        }
        return n;
    }
    bool Busy() const { return s_[0].was || s_[1].was || s_[0].releaseLeft > 0 || s_[1].releaseLeft > 0; }

private:
    struct Side {
        bool was = false;
        bool havePrev = false;
        int releaseLeft = 0;
        uint32_t prevMs = 0;
        float prevPalm[3] = {};   // the world's axes, metres
        float vel[3] = {};        // ... m/s
        Hold last;
    };
    Side s_[2];
    uint16_t seq_ = 0;
    uint8_t serial_ = 0;
};

// ---- the host: the latest hold of each peer's hand, relayed to the others -----------------------------------------------------------------------

class HostGrabs {
public:
    struct E { uint64_t peer = 0; uint32_t unit = 0; Hold hold; bool fresh = false; };
    static constexpr int kMax = 4 * poseshare::kMaxPeers;
    std::array<E, kMax> e{};

    // A hold (or a knock) that came from peer (whose hero is unit - the caller checked it against the poses' table). False: no room. A hand's holds share one
    // entry (the newest); each knock has its own (by its serial) until relayed.
    bool Take(uint64_t peer, uint32_t unit, const Hold& h) {
        E* free = nullptr;
        const bool knock = h.state == kKnock;
        for (auto& x : e) {
            const bool same = x.peer == peer && (knock ? x.hold.state == kKnock && x.hold.serial == h.serial
                                                       : x.hold.state != kKnock && x.hold.side == h.side);
            if (same) { x.unit = unit; x.hold = h; x.fresh = true; return true; }
            if (!x.peer && !free) free = &x;
        }
        if (!free) return false;
        *free = E{peer, unit, h, true};
        return true;
    }
    // The fresh holds for dest (the others'; its own too with echo), up to max. How many.
    int Build(uint64_t dest, bool echo, RelayEntry* out, int max) const {
        int n = 0;
        for (const auto& x : e)
            if (x.peer && x.fresh && (echo || x.peer != dest) && n < max) out[n++] = RelayEntry{x.unit, x.hold};
        return n;
    }
    void Flush() {
        for (auto& x : e) {
            x.fresh = false;
            if (x.peer && (x.hold.state == kRelease || x.hold.state == kKnock)) x = E{};   // (a release or a knock relayed: its slot is free; its repeats come as fresh ones)
        }
    }
    void Remove(uint64_t peer) {
        for (auto& x : e) if (x.peer == peer) x = E{};
    }
    bool AnyFresh() const {
        for (const auto& x : e) if (x.peer && x.fresh) return true;
        return false;
    }
};

// ---- knocks: ours sent a few ticks running, theirs played once, as far behind as the striker's poses ---------------------------------------------------

constexpr int kKnockRepeats = 3;
constexpr uint32_t kKnockStaleMs = 1500;   // a knock not played this long after it came is dropped (the body has moved on)

class KnockSender {
public:
    // A knock of ours (its monster, joint, local, vel, kind, heading, eye height; ms our clock): numbered and sent the next kKnockRepeats ticks.
    void Add(const Hold& k) {
        Slot* s = &q_[0];
        for (auto& x : q_) if (x.left < s->left) s = &x;   // (a free one, else the one nearest done)
        s->h = k;
        s->h.state = kKnock;
        s->h.side = 0;
        s->h.serial = ++serial_;
        s->left = kKnockRepeats;
    }
    // This tick's knocks into out (up to max), each counted down. How many.
    int Take(Hold* out, int max, uint16_t* seq) {
        int n = 0;
        for (auto& x : q_) {
            if (x.left <= 0 || n >= max) continue;
            --x.left;
            out[n] = x.h;
            if (seq) out[n].seq = ++*seq;
            ++n;
        }
        return n;
    }
    bool Busy() const {
        for (const auto& x : q_) if (x.left > 0) return true;
        return false;
    }

private:
    struct Slot { Hold h; int left = 0; };
    std::array<Slot, 4> q_{};
    uint8_t serial_ = 0;
};

// The others' knocks, each played once when its time comes: the sender's moment put on our clock by the least (arrival - sent) seen of that sender, delayMs behind.
class KnockQueue {
public:
    struct Knock { uint32_t unit = 0; Hold h; };
    // A knock that came now (our clock); its repeats and one already played are dropped. True: taken.
    bool Add(uint64_t nowMs, uint32_t unit, const Hold& h) {
        if (h.state != kKnock || !unit) return false;
        for (const auto& d : done_) if (d.unit == unit && d.serial == h.serial && d.ms == h.ms) return false;
        for (const auto& x : q_) if (x.unit && x.unit == unit && x.h.serial == h.serial && x.h.ms == h.ms) return false;
        Offset* o = OffsetOf(unit);
        const int64_t off = (int64_t)nowMs - (int64_t)h.ms;
        if (o) { if (!o->unit || off < o->off) { o->unit = unit; o->off = off; } }
        Entry* free = nullptr;
        for (auto& x : q_) if (!x.unit) { free = &x; break; }
        if (!free) return false;
        *free = Entry{unit, h, nowMs};
        return true;
    }
    // The knocks due now, played (each once) into out, up to max: delayFor(unit) the playback's delay of that sender. Stale ones are dropped. How many.
    template <class DelayFn> int Due(uint64_t nowMs, DelayFn delayFor, Knock* out, int max) {
        int n = 0;
        for (auto& x : q_) {
            if (!x.unit) continue;
            if (nowMs > x.recvMs && nowMs - x.recvMs > kKnockStaleMs) { Done(x); x = Entry{}; continue; }
            const Offset* o = OffsetOf(x.unit);
            const int64_t off = o && o->unit == x.unit ? o->off : (int64_t)x.recvMs - (int64_t)x.h.ms;
            if ((int64_t)nowMs < (int64_t)x.h.ms + off + (int64_t)delayFor(x.unit) || n >= max) continue;
            out[n++] = Knock{x.unit, x.h};
            Done(x);
            x = Entry{};
        }
        return n;
    }

private:
    struct Entry { uint32_t unit = 0; Hold h; uint64_t recvMs = 0; };
    struct Offset { uint32_t unit = 0; int64_t off = 0; };
    struct DoneOne { uint32_t unit = 0; uint8_t serial = 0; uint32_t ms = 0; };
    Offset* OffsetOf(uint32_t unit) {
        for (auto& o : off_) if (o.unit == unit) return &o;
        for (auto& o : off_) if (!o.unit) return &o;
        return nullptr;
    }
    const Offset* OffsetOf(uint32_t unit) const {
        for (const auto& o : off_) if (o.unit == unit) return &o;
        return nullptr;
    }
    void Done(const Entry& x) {
        done_[doneAt_] = DoneOne{x.unit, x.h.serial, x.h.ms};
        doneAt_ = (doneAt_ + 1) % (int)done_.size();
    }
    std::array<Entry, 16> q_{};
    std::array<Offset, poseshare::kMaxPeers> off_{};
    std::array<DoneOne, 32> done_{};
    int doneAt_ = 0;
};

// ---- the viewer: a holder's hand played back -----------------------------------------------------------------------------------------------------

constexpr int kGrabTrackSamples = 24;
constexpr uint32_t kGrabStaleMs = 700;   // a hold heard of no more this long: lost - let go where it was (no throw)

// One holder's hand: its holds as they came, played back delayMs behind (the holder's poses' delay), the palm blended between two holds of the same hold.
class GrabTrack {
public:
    struct Entry { uint64_t recvMs = 0; int64_t sentMs = 0; Hold h; };

    // A hold that came now (our clock). A copy of one already here (a release's repeats) or one older than the newest is dropped. True: taken.
    bool Add(uint64_t nowMs, const Hold& h) {
        int64_t sent = (int64_t)h.ms;
        if (n_ > 0) sent = last_ + (int64_t)(int32_t)(h.ms - (uint32_t)last_);
        if (n_ > 0) {
            const Entry& nw = At(n_ - 1);
            if (sent < nw.sentMs) return false;
            if (sent == nw.sentMs && nw.h.serial == h.serial && nw.h.state == h.state) return false;
        }
        last_ = sent;
        s_[head_] = {nowMs, sent, h};
        head_ = (head_ + 1) % kGrabTrackSamples;
        n_ = std::min(n_ + 1, kGrabTrackSamples);
        return true;
    }
    bool Empty() const { return n_ == 0; }
    uint64_t NewestRecv() const { return n_ ? At(n_ - 1).recvMs : 0; }

    // The hold to show now, delayMs behind: *lost true when its holder fell silent while holding (then out is the last hold as a release with no velocity).
    // False: nothing yet at the playback's moment (the hold begins later), or nothing at all.
    bool Sample(uint64_t nowMs, uint32_t delayMs, Hold* out, bool* lost = nullptr) const {
        if (lost) *lost = false;
        if (!n_) return false;
        const Entry& newest = At(n_ - 1);
        if (nowMs > newest.recvMs && nowMs - newest.recvMs > kGrabStaleMs) {   // (one that came after nowMs was read, on another thread, is fresh)
            *out = newest.h;
            if (out->state == kHold) {
                out->state = kRelease;
                for (float& v : out->vel) v = 0.0f;
                if (lost) *lost = true;
            }
            return true;
        }
        int64_t offset = INT64_MAX;
        for (int i = 0; i < n_; ++i) offset = std::min(offset, (int64_t)At(i).recvMs - At(i).sentMs);
        const int64_t target = (int64_t)nowMs - offset - (int64_t)delayMs;
        if (target < At(0).sentMs) return false;
        int i = n_ - 1;
        while (i > 0 && At(i).sentMs > target) --i;
        const Entry& a = At(i);
        *out = a.h;
        if (a.h.state == kHold && i + 1 < n_) {   // blended toward the next of the same hold (a release carries the palm where it left)
            const Entry& b = At(i + 1);
            const int64_t span = b.sentMs - a.sentMs;
            if (b.h.serial == a.h.serial && span > 0) {
                const float t = std::clamp((float)(target - a.sentMs) / (float)span, 0.0f, 1.0f);
                for (int k = 0; k < 3; ++k) {
                    out->palm[k] = a.h.palm[k] + (b.h.palm[k] - a.h.palm[k]) * t;
                    out->vel[k] = a.h.vel[k] + (b.h.vel[k] - a.h.vel[k]) * t;
                }
                out->heading = poseshare::WrapPi(a.h.heading + poseshare::WrapPi(b.h.heading - a.h.heading) * t);
            }
        }
        return true;
    }

private:
    const Entry& At(int i) const { return s_[(head_ - n_ + i + kGrabTrackSamples) % kGrabTrackSamples]; }
    std::array<Entry, kGrabTrackSamples> s_{};
    int head_ = 0, n_ = 0;
    int64_t last_ = 0;
};

// What our library is asked to do for the others' holds (vr/knock_down.h RemoteTake / RemoteHandAt / RemoteLetGo; a fake in the test). Points in our render world.
class Bodies {
public:
    virtual ~Bodies() = default;
    // holder's hand `side` took monster at limb `joint`, the point `local` on it (ToLimb), its palm at `palm`: true when our library took our drawing of it.
    virtual bool Take(uint32_t holder, int side, uint32_t monster, int joint, const float local[3], const float palm[3]) = 0;
    virtual void Hand(uint32_t holder, int side, const float palm[3]) = 0;
    virtual void Release(uint32_t holder, int side, const float palm[3], const float vel[3]) = 0;
};

// A holder's hand as played back now, in our world (the caller put the palm and the velocity there).
struct Seen {
    uint32_t holder = 0;
    Hold hold;
    float palm[3] = {};   // our render world
    float vel[3] = {};    // ... m/s
    bool lost = false;
};

constexpr uint64_t kRetryMs = 250;   // a hold our game could not take (the monster not near): tried again this often while it lasts

class Viewer {
public:
    struct Counts { int took = 0, missed = 0, released = 0, lost = 0, ignored = 0; };   // (this step's: the log's)

    // Every holder's hand as played back now (one each), our clock. Holds of hands not in the list any more are let go (no throw).
    Counts Step(const Seen* s, int n, uint64_t nowMs, Bodies& b) {
        Counts c;
        for (auto& sl : slots_) sl.seen = false;
        for (int i = 0; i < n; ++i) {
            const Seen& x = s[i];
            Slot* sl = Find(x.holder, x.hold.side, true);
            if (!sl) continue;
            sl->seen = true;
            if (sl->serial != x.hold.serial || !sl->live) {   // another hold of this hand: the old one let go
                if (sl->taken) { b.Release(sl->holder, sl->side, x.palm, kZero); ++c.released; }
                sl->serial = x.hold.serial;
                sl->monster = x.hold.monster;
                sl->taken = false;
                sl->done = false;
                sl->nextTryMs = 0;
                sl->live = true;
            }
            if (sl->done) continue;
            if (x.hold.state == kHold) {
                if (!sl->taken && nowMs >= sl->nextTryMs) {
                    sl->taken = b.Take(x.holder, x.hold.side, x.hold.monster, x.hold.joint, x.hold.local, x.palm);
                    if (sl->taken) ++c.took;
                    else { ++c.missed; sl->nextTryMs = nowMs + kRetryMs; }
                }
                if (sl->taken) b.Hand(x.holder, x.hold.side, x.palm);
            } else {
                if (sl->taken) {
                    b.Release(x.holder, x.hold.side, x.palm, x.lost ? kZero : x.vel);
                    if (x.lost) ++c.lost;
                    else ++c.released;
                } else {
                    ++c.ignored;   // (a hold our game never took: nothing to let go)
                }
                sl->taken = false;
                sl->done = true;
            }
        }
        for (auto& sl : slots_) {
            if (!sl.holder || sl.seen) continue;
            if (sl.taken) { b.Release(sl.holder, sl.side, nullptr, kZero); ++c.lost; }
            sl = Slot{};
        }
        return c;
    }
    // Every hold let go at once (the view changed, the box unticked).
    int ReleaseAll(Bodies& b) {
        int k = 0;
        for (auto& sl : slots_) {
            if (sl.taken) { b.Release(sl.holder, sl.side, nullptr, kZero); ++k; }
            sl = Slot{};
        }
        return k;
    }
    int Taken() const {
        int k = 0;
        for (const auto& sl : slots_) k += sl.taken ? 1 : 0;
        return k;
    }

private:
    static constexpr float kZero[3] = {0.0f, 0.0f, 0.0f};
    struct Slot {
        uint32_t holder = 0;
        uint8_t side = 0, serial = 0;
        uint32_t monster = 0;
        bool live = false, taken = false, done = false, seen = false;
        uint64_t nextTryMs = 0;
    };
    Slot* Find(uint32_t holder, uint8_t side, bool add) {
        for (auto& sl : slots_) if (sl.holder == holder && sl.side == side) return &sl;
        if (!add) return nullptr;
        for (auto& sl : slots_) if (!sl.holder) { sl = Slot{}; sl.holder = holder; sl.side = side; return &sl; }
        return nullptr;
    }
    std::array<Slot, 2 * poseshare::kMaxPeers> slots_{};
};

}  // namespace grabshare
