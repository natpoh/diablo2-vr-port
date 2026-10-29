// pose_share.h - [multiplayer] pose_share: the head and both controllers of every VR player in a D2RLoader TCP/IP game, sent to the others
// through D2RLoader's plugin network (D2RLPlugin/network.h), so each of them can be seen waving, pointing, holding the weapon. Step 1, a
// probe: the poses are logged and drawn as three small markers at the other hero (vrcam.cpp, F4 only). Step 2, later: the other hero's
// model posed from them (the arms by IK, as our own body).
//
// Pure part: the wire format (packing, quantizing), the host's relay table, the stats of what came through (rate, loss, round trip), the
// receiver's buffer that plays a player's poses back smoothly, and the frame maths (our pose into the hero's frame, theirs into our world).
// No game calls, no globals, no network calls (those are pose_share_wire.cpp's). Tested by cleanroom/tests/pose_share_test.cpp.
//
// The topology is D2RLoader's star: every player - the host too, in process - connects its channel to the host and sends its own pose
// there (kMsgPose); the host keeps the latest pose of each peer and, kSendHz times a second, sends each peer the poses of the others
// that changed, up to kRelayMax in one message tagged with their hero's unit id (kMsgRelay). A ping / pong pair measures the round trip.
//
// The hero's frame (one for the three joints): its origin is the hero's feet as drawn (vrcam's look-at L), its axes the head's turn on
// the ground - x right, y up, z back, OpenXR's axes - turned by `heading` in the game's world (the world's axes are the game's, the same
// on every PC). Metres of the player's own body; the receiver scales them by its hero's eye height over the sender's (eyeM). Rotations
// are in the same frame: the head's (its pitch and roll only - the turn is the frame's), the controllers' as the runtime has them.
// That is exactly the frame the shared block (shared/d2r_vr_shared.h) gives the hands in, so a sender needs no rotation of its own.
//
// Format 2 ([multiplayer] pose_share_bodies): the pose may also carry the hero's body as drawn - both shoulders and elbows (the arms the skeleton
// hook posed, skel::ArmReach / HeroForearm) - and each held weapon's capsule as hit by touch sweeps it (the weapon bone as drawn, its ends
// from the model's own mesh bounds, vr/weapon_extents.h), butt to tip, in the same hero's frame. What a pose carries is in its flags, so a
// pose is 48 to 100 bytes; a relay holds as many as fit D2RLoader's 224 (two full ones, four bare). vr/player_bodies.h makes the capsules.
// Format 3 ([multiplayer] pose_share_hero): + the head's yaw in the sender's own hero's model space (kModelYaw, 2 bytes - what its own arms are posed by,
// skel::HeroYawInModel), so a receiver can pose the sender's hero model (vr/remote_pose.h): up to 102 bytes.
// Format 5 (the author: "pass the weapon's absolute place against the headset - 3 for its position, 3 for its turn"; "and how hard each hand grips, and
// whether it holds the weapon"): every pose carries each shown weapon slot's attach bone AS THE SENDER DRAWS IT - in a hand, falling, lying, on the back - its
// place from the head and the turn of its rows, mirrored or not (the second flags byte: kAttach0 / kAttach1, 12 bytes each); and each hand's grip (kGrips,
// a byte each) and whether it holds the weapon (kHoldR / kHoldL). The receiver hangs the other hero's weapon exactly there every frame, after the game's
// own animation; a hand that holds it is put where the weapon says (the game's own grip of that hand), its fingers closed by the grip. Up to 128 bytes.
// Format 6 (the 2026-10-26 two-PC test: "the other player holds the weapon wrong - his wrists, hands and arms in the wrong place, the weapon not in the hand
// the way the owner holds it"): the hand put by the RECEIVER's game grip of this frame is not where the sender's hand is - the sender holds the weapon in a grip
// taken on a calm frame, a staff by its own hold, the weight model's hand fitted onto the weapon after the attach bones were read. So each pose carries both hand
// bones and elbows AS THE SENDER DRAWS THEM, all the bones read at the end of its pose (the shield strapped, the hands fitted, the body moved under the camera),
// with where its head bone is against its eye and its model's units a metre (kHands, the second flags byte's last bit: 45 bytes); the receiver hangs every one
// of them from its rig's head bone the sender's way, in the sender's own units. Up to 173 bytes (alone in a relay).
// Format 7 ([multiplayer] pose_share_held; the author: "when he takes a potion or a jar, I must see it in his hand, and see it fly when he throws it"): what each
// hand has, as the sender draws it - a copy of an item by its code (a belt potion, an emptied bottle, an item taken off the ground) or a prop of the level (a
// jar, a book, a cup: by its catalogue name's hash and where it stood) - in the hand, flying from it or come to rest, with its place and turn every pose (the
// flight as the sender's own physics flies it, not thrown again on the receiver), and the sender's world units a metre (21 bytes a hand + 2; the flags are
// the eye height's top four bits). Drawn only on the receiver: the game's potion use, its items and its props are each PC's game's. Up to 217 bytes.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace poseshare {

// ---- the channel ------------------------------------------------------------------------------------------------------------------------

constexpr uint16_t kChannel = 0x5053;                   // "PS": the plugin's own channel number (D2RLoader adds the plugin id and its scope)
constexpr uint64_t kToken = 0x56525F504F534507ull;      // "VR_POSE" + format 7: client and host connect only with the same token
constexpr uint8_t kVersion = 7;                         // the first byte of every message
constexpr uint32_t kMaxPayload = 224;                   // D2RLoader's Network::MaxPayloadSize

// kMsgBurst: [debug] pose_share_burst's measuring messages, sent back to back once a connection is up (a pose's size, the payload a ping's):
// how many a sender gets out before RateLimited, how many of them arrive.
enum MsgId : uint16_t { kMsgPose = 1, kMsgRelay = 2, kMsgPing = 3, kMsgPong = 4, kMsgBurst = 5 };

constexpr int kSendHzDefault = 30;   // [debug] pose_share_hz: our pose to the host, and the host's relays to each peer
constexpr int kRelayMax = 4;         // poses in one relay at most (four bare ones: 2 + 4 * 52 = 210 bytes; two with bodies and weapons)

// ---- the pose -------------------------------------------------------------------------------------------------------------------------

// which joints are tracked; kF4: the sender plays VR first person; kBody: the shoulders and elbows as drawn; kWeapon0 / 1: a held weapon's capsule
// kModelYaw: the head's yaw in the sender's hero's model space came too
enum Flags : uint8_t { kHead = 1, kRight = 2, kLeft = 4, kF4 = 8, kBody = 16, kWeapon0 = 32, kWeapon1 = 64, kModelYaw = 128 };
constexpr uint8_t kAllFlags = kHead | kRight | kLeft | kF4 | kBody | kWeapon0 | kWeapon1 | kModelYaw;
// the second flags byte (the pose's byte 1): a slot's attach bone as drawn comes (and it is mirrored); a hand holds the weapon; the grips come
// kHands (format 6): both hand bones and elbows as drawn, the head bone against the eye, the model's units a metre
enum Flags2 : uint8_t { kAttach0 = 1, kAttach1 = 2, kMirror0 = 4, kMirror1 = 8, kHoldR = 16, kHoldL = 32, kGrips = 64, kHands = 128 };
constexpr uint8_t kAllFlags2 = kAttach0 | kAttach1 | kMirror0 | kMirror1 | kHoldR | kHoldL | kGrips | kHands;
// the third flags (format 7: the eye height's top four bits) - what a hand has comes (Pose::held)
enum Flags3 : uint8_t { kHeldR = 1, kHeldL = 2 };
constexpr uint8_t kAllFlags3 = kHeldR | kHeldL;
enum HeldWhat : uint8_t { kHeldNone = 0, kHeldItem = 1, kHeldProp = 2 };
enum HeldState : uint8_t { kInHand = 0, kFlying = 1, kAtRest = 2 };

struct Joint {
    float p[3] = {0.0f, 0.0f, 0.0f};          // metres in the hero's frame
    float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};    // x y z w, the same frame
};

struct Pose {
    uint8_t flags = 0;
    uint16_t seq = 0;          // the sender's count of its poses (wraps)
    uint32_t ms = 0;           // the sender's clock (GetTickCount64, low 32 bits) at the sample
    float heading = 0.0f;      // radians: the frame's turn in the game's world (HeadingOf)
    float eyeM = 1.6f;         // the sender's own eye height, metres
    Joint head, right, left;
    float shoulder[2][3] = {}, elbow[2][3] = {};   // kBody: [0] right, [1] left, metres in the hero's frame
    struct Blade {
        float butt[3] = {}, tip[3] = {};   // metres in the hero's frame
        float radius = 0.04f;              // metres (1 mm steps on the wire, up to 25 cm)
        uint8_t kind = 0;                  // the weapon's kind (D2RVR_TYPE_*, shared/d2r_vr_state.h)
        uint8_t hand = 0;                  // the hand that swings it: 0 right, 1 left
    };
    Blade weapon[2];                       // kWeapon0, kWeapon1
    float yawInModel = 0.0f;               // kModelYaw: radians, the camera's yaw in the hero's model space (skel's PoseBody convention: atan2(x, z))
    uint8_t flags2 = 0;                    // kAttach0 / kAttach1, kMirror0 / kMirror1, kHoldR / kHoldL, kGrips
    Joint attach[2];                       // [slot 0 right, 1 left]: the weapon's attach bone as drawn - its origin (metres, the hero's frame) and its axes as a
                                           // turn (q maps x, y, z onto the bone's rows; a mirrored bone's z row is the turn's -z)
    float grip[2] = {0.0f, 0.0f};          // kGrips: [0] right, [1] left, how closed the hand is 0..1
    // kHands: [0] right, [1] left - the hand bone as drawn (as attach: its origin, the turn of its rows, mirrored or not) and the elbow (the lower arm bone's
    // origin); the head bone's origin (all three metres in the hero's frame); the sender's model units a metre (the hands' and weapons' offsets times it are
    // its own model's)
    Joint hand[2];
    bool handMirror[2] = {false, false};
    float handElbow[2][3] = {};
    float headBone[3] = {};
    float modelUpm = 0.0f;
    // kHeldR / kHeldL (format 7): what hand [0] right, [1] left has - in it, flying from it, or come to rest - as the sender draws it
    uint8_t flags3 = 0;
    struct Held {
        uint8_t what = kHeldNone, state = kInHand;
        uint32_t code = 0;        // kHeldItem: the item's code (its four characters, the game's own order)
        float scale = 1.0f;       // kHeldItem: the copy's size (its matrix's row length), steps of 0.02 up to 5.1
        uint16_t nameHash = 0;    // kHeldProp: the catalogue's name of it, hashed (NameHash)
        float home[3] = {};       // kHeldProp: where it stood (metres, the hero's frame): which of the level's props it is
        Joint pose;               // where it is and its turn (metres, the hero's frame)
    };
    Held held[2];
    float worldUpm = 0.0f;        // with any held: the sender's world units a metre (the flying and resting ones are put in the world by it)
};

// A prop's catalogue name, hashed (FNV-1a folded to 16 bits): the same name on every PC.
inline uint16_t NameHash(const char* s) {
    uint32_t h = 2166136261u;
    for (; s && *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
    return (uint16_t)(h ^ (h >> 16));
}

constexpr uint32_t kPoseBytes = 48;                     // the bare pose on the wire: head and hands
constexpr uint32_t kBodyBytes = 24;                     // kBody: 4 points, mm
constexpr uint32_t kBladeBytes = 14;                    // a weapon: 2 points mm, radius mm, the kind and the hand (bit 7)
constexpr uint32_t kYawBytes = 2;                       // kModelYaw: 65536 a turn
constexpr uint32_t kAttachBytes = 12;                   // kAttach0 / 1: a point mm, a turn 48 bits
constexpr uint32_t kGripBytes = 2;                      // kGrips: a byte a hand
constexpr uint32_t kHandsBytes = 45;                    // kHands: the mirror bits, the units a metre (1/1000), the head bone, 2 hands (a point and a turn), 2 elbows
constexpr uint32_t kHeldBytes = 21;                     // kHeldR / kHeldL: what and its state, its code and size or its name and home, its place and turn
constexpr uint32_t kHeldUpmBytes = 2;                   // with any held: the sender's world units a metre (1/1000)
constexpr uint32_t kPoseMaxBytes = kPoseBytes + kBodyBytes + 2 * kBladeBytes + kYawBytes + 2 * kAttachBytes + kGripBytes + kHandsBytes + 2 * kHeldBytes + kHeldUpmBytes;   // 217
constexpr uint32_t kPoseMsgBytes = 6 + kPoseBytes;      // version, 0, the sender's own unit id, the pose (bare)
constexpr uint32_t kPoseMsgMaxBytes = 6 + kPoseMaxBytes;
constexpr uint32_t kRelayEntryBytes = 4 + kPoseBytes;   // unit id, pose (bare)
constexpr uint32_t kPingBytes = 8;                      // version, 0, ping seq, client ms
constexpr uint32_t kPongBytes = 12;                     // the ping, host ms
constexpr uint32_t kBurstBytes = kPoseMsgMaxBytes;      // a ping's fields, padded to a full pose message's size
static_assert(2 + kRelayMax * kRelayEntryBytes <= kMaxPayload, "a relay of bare poses must fit D2RLoader's payload");
static_assert(2 + 4 + kPoseMaxBytes <= kMaxPayload && 6 + kPoseMaxBytes <= kMaxPayload, "a pose with everything must fit a message");

// ---- little-endian bytes ----------------------------------------------------------------------------------------------------------------

inline void Put16(uint8_t* b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
inline void Put32(uint8_t* b, uint32_t v) { for (int i = 0; i < 4; ++i) b[i] = (uint8_t)(v >> (8 * i)); }
inline uint16_t Get16(const uint8_t* b) { return (uint16_t)(b[0] | (b[1] << 8)); }
inline uint32_t Get32(const uint8_t* b) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24); }

// ---- quantizing ---------------------------------------------------------------------------------------------------------------------------

constexpr float kPi = 3.14159265358979f;

inline int16_t QuantMm(float m) {
    if (!std::isfinite(m)) return 0;
    const float mm = std::round(m * 1000.0f);
    return (int16_t)std::clamp(mm, -32767.0f, 32767.0f);
}
inline float DequantMm(int16_t v) { return (float)v * 0.001f; }

inline float WrapPi(float a) {
    if (!std::isfinite(a)) return 0.0f;
    a = std::fmod(a + kPi, 2.0f * kPi);
    if (a < 0.0f) a += 2.0f * kPi;
    return a - kPi;
}
inline uint16_t QuantAngle(float rad) {   // 65536 steps a turn (0.0055 degrees)
    const float t = (WrapPi(rad) + kPi) / (2.0f * kPi);
    return (uint16_t)((uint32_t)std::lround(t * 65536.0f) & 0xFFFFu);
}
inline float DequantAngle(uint16_t v) { return WrapPi((float)v / 65536.0f * 2.0f * kPi - kPi); }

inline bool Normalize(float q[4]) {
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!std::isfinite(n) || n < 1e-6f) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; return false; }
    for (int i = 0; i < 4; ++i) q[i] /= n;
    return true;
}

// A unit quaternion in 48 bits, "smallest three": the index of the largest component (2 bits, the component made positive and left
// out - it is sqrt(1 - the others)), the other three in [-1/sqrt2, 1/sqrt2] at 15 bits each (about 0.004 degrees).
constexpr float kQuatSpan = 0.70710678f;
constexpr uint32_t kQuatSteps = 32767u;
inline void PackQuat(const float in[4], uint8_t out[6]) {
    float q[4] = {in[0], in[1], in[2], in[3]};
    Normalize(q);
    int big = 0;
    for (int i = 1; i < 4; ++i) if (std::fabs(q[i]) > std::fabs(q[big])) big = i;
    const float s = q[big] < 0.0f ? -1.0f : 1.0f;
    uint64_t v = (uint64_t)big << 45;
    int shift = 30;
    for (int i = 0; i < 4; ++i) {
        if (i == big) continue;
        const float c = std::clamp(q[i] * s, -kQuatSpan, kQuatSpan);
        const uint64_t u = (uint64_t)std::lround((c + kQuatSpan) / (2.0f * kQuatSpan) * (float)kQuatSteps);
        v |= (u & 0x7FFFu) << shift;
        shift -= 15;
    }
    for (int i = 0; i < 6; ++i) out[i] = (uint8_t)(v >> (8 * i));
}
inline void UnpackQuat(const uint8_t in[6], float q[4]) {
    uint64_t v = 0;
    for (int i = 0; i < 6; ++i) v |= (uint64_t)in[i] << (8 * i);
    const int big = (int)((v >> 45) & 3u);
    int shift = 30;
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (i == big) continue;
        const uint32_t u = (uint32_t)((v >> shift) & 0x7FFFu);
        q[i] = (float)u / (float)kQuatSteps * (2.0f * kQuatSpan) - kQuatSpan;
        sum += q[i] * q[i];
        shift -= 15;
    }
    q[big] = std::sqrt(std::max(0.0f, 1.0f - sum));
    Normalize(q);
}

// ---- the pose on the wire -----------------------------------------------------------------------------------------------------------------

inline void PackJoint(const Joint& j, uint8_t* b) {
    for (int i = 0; i < 3; ++i) Put16(b + 2 * i, (uint16_t)QuantMm(j.p[i]));
    PackQuat(j.q, b + 6);
}
inline void UnpackJoint(const uint8_t* b, Joint* j) {
    for (int i = 0; i < 3; ++i) j->p[i] = DequantMm((int16_t)Get16(b + 2 * i));
    UnpackQuat(b + 6, j->q);
}

// The bytes this pose takes on the wire.
inline uint32_t PoseBytes(const Pose& p) {
    return kPoseBytes + ((p.flags & kBody) ? kBodyBytes : 0) + ((p.flags & kWeapon0) ? kBladeBytes : 0) + ((p.flags & kWeapon1) ? kBladeBytes : 0) +
           ((p.flags & kModelYaw) ? kYawBytes : 0) + ((p.flags2 & kAttach0) ? kAttachBytes : 0) + ((p.flags2 & kAttach1) ? kAttachBytes : 0) + ((p.flags2 & kGrips) ? kGripBytes : 0) +
           ((p.flags2 & kHands) ? kHandsBytes : 0) + ((p.flags3 & kHeldR) ? kHeldBytes : 0) + ((p.flags3 & kHeldL) ? kHeldBytes : 0) +
           ((p.flags3 & kAllFlags3) ? kHeldUpmBytes : 0);
}
inline void PackPoint(const float v[3], uint8_t* b) { for (int i = 0; i < 3; ++i) Put16(b + 2 * i, (uint16_t)QuantMm(v[i])); }
inline void UnpackPoint(const uint8_t* b, float v[3]) { for (int i = 0; i < 3; ++i) v[i] = DequantMm((int16_t)Get16(b + 2 * i)); }

// The pose into out (room for PoseBytes); returns the bytes written.
inline uint32_t PackPose(const Pose& p, uint8_t* out) {
    out[0] = p.flags & kAllFlags;
    out[1] = p.flags2 & kAllFlags2;
    Put16(out + 2, p.seq);
    Put32(out + 4, p.ms);
    Put16(out + 8, QuantAngle(p.heading));
    const float eyeMm = std::isfinite(p.eyeM) ? std::clamp(p.eyeM * 1000.0f, 0.0f, 4095.0f) : 0.0f;   // 12 bits (to 4.095 m); the top four the third flags
    Put16(out + 10, (uint16_t)((uint16_t)std::lround(eyeMm) | (uint16_t)((p.flags3 & kAllFlags3) << 12)));
    PackJoint(p.head, out + 12);
    PackJoint(p.right, out + 24);
    PackJoint(p.left, out + 36);
    uint32_t n = kPoseBytes;
    if (p.flags & kBody) {
        PackPoint(p.shoulder[0], out + n); PackPoint(p.shoulder[1], out + n + 6);
        PackPoint(p.elbow[0], out + n + 12); PackPoint(p.elbow[1], out + n + 18);
        n += kBodyBytes;
    }
    for (int w = 0; w < 2; ++w) {
        if (!(p.flags & (w ? kWeapon1 : kWeapon0))) continue;
        const Pose::Blade& bl = p.weapon[w];
        PackPoint(bl.butt, out + n);
        PackPoint(bl.tip, out + n + 6);
        const float mm = std::isfinite(bl.radius) ? std::clamp(bl.radius * 1000.0f, 1.0f, 250.0f) : 40.0f;
        out[n + 12] = (uint8_t)std::lround(mm);
        out[n + 13] = (uint8_t)((bl.kind & 0x7F) | (bl.hand ? 0x80 : 0));
        n += kBladeBytes;
    }
    if (p.flags & kModelYaw) {
        Put16(out + n, QuantAngle(p.yawInModel));
        n += kYawBytes;
    }
    for (int s = 0; s < 2; ++s)
        if (p.flags2 & (s ? kAttach1 : kAttach0)) { PackJoint(p.attach[s], out + n); n += kAttachBytes; }
    if (p.flags2 & kGrips) {
        for (int s = 0; s < 2; ++s) out[n + s] = (uint8_t)std::lround(std::isfinite(p.grip[s]) ? std::clamp(p.grip[s], 0.0f, 1.0f) * 255.0f : 0.0f);
        n += kGripBytes;
    }
    if (p.flags2 & kHands) {
        out[n] = (uint8_t)((p.handMirror[0] ? 1 : 0) | (p.handMirror[1] ? 2 : 0));
        const float upm = std::isfinite(p.modelUpm) ? std::clamp(p.modelUpm * 1000.0f, 0.0f, 65535.0f) : 0.0f;
        Put16(out + n + 1, (uint16_t)std::lround(upm));
        PackPoint(p.headBone, out + n + 3);
        for (int s = 0; s < 2; ++s) {
            PackJoint(p.hand[s], out + n + 9 + 12 * s);
            PackPoint(p.handElbow[s], out + n + 33 + 6 * s);
        }
        n += kHandsBytes;
    }
    for (int s = 0; s < 2; ++s) {
        if (!(p.flags3 & (s ? kHeldL : kHeldR))) continue;
        const Pose::Held& h = p.held[s];
        uint8_t* b = out + n;
        std::memset(b, 0, kHeldBytes);
        b[0] = (uint8_t)((h.what & 0x0F) | ((h.state & 0x0F) << 4));
        if (h.what == kHeldProp) {
            Put16(b + 1, h.nameHash);
            PackPoint(h.home, b + 3);
        } else {
            Put32(b + 1, h.code);
            b[5] = (uint8_t)std::lround(std::isfinite(h.scale) ? std::clamp(h.scale / 0.02f, 1.0f, 255.0f) : 50.0f);
        }
        PackJoint(h.pose, b + 9);
        n += kHeldBytes;
    }
    if (p.flags3 & kAllFlags3) {
        Put16(out + n, (uint16_t)std::lround(std::isfinite(p.worldUpm) ? std::clamp(p.worldUpm * 1000.0f, 0.0f, 65535.0f) : 0.0f));
        n += kHeldUpmBytes;
    }
    return n;
}
// The pose from `in` (size bytes there); returns the bytes read, 0 when they are too few for what its flags say.
inline uint32_t UnpackPose(const uint8_t* in, uint32_t size, Pose* p) {
    if (size < kPoseBytes) return 0;
    *p = Pose{};
    p->flags = in[0] & kAllFlags;
    p->flags2 = in[1] & kAllFlags2;
    const uint16_t eye = Get16(in + 10);
    p->flags3 = (uint8_t)((eye >> 12) & kAllFlags3);
    if (size < PoseBytes(*p)) return 0;
    p->seq = Get16(in + 2);
    p->ms = Get32(in + 4);
    p->heading = DequantAngle(Get16(in + 8));
    p->eyeM = (float)(eye & 0x0FFFu) * 0.001f;
    UnpackJoint(in + 12, &p->head);
    UnpackJoint(in + 24, &p->right);
    UnpackJoint(in + 36, &p->left);
    uint32_t n = kPoseBytes;
    if (p->flags & kBody) {
        UnpackPoint(in + n, p->shoulder[0]); UnpackPoint(in + n + 6, p->shoulder[1]);
        UnpackPoint(in + n + 12, p->elbow[0]); UnpackPoint(in + n + 18, p->elbow[1]);
        n += kBodyBytes;
    }
    for (int w = 0; w < 2; ++w) {
        if (!(p->flags & (w ? kWeapon1 : kWeapon0))) continue;
        Pose::Blade& bl = p->weapon[w];
        UnpackPoint(in + n, bl.butt);
        UnpackPoint(in + n + 6, bl.tip);
        bl.radius = (float)in[n + 12] * 0.001f;
        bl.kind = in[n + 13] & 0x7F;
        bl.hand = (in[n + 13] & 0x80) ? 1 : 0;
        n += kBladeBytes;
    }
    if (p->flags & kModelYaw) {
        p->yawInModel = DequantAngle(Get16(in + n));
        n += kYawBytes;
    }
    for (int s = 0; s < 2; ++s)
        if (p->flags2 & (s ? kAttach1 : kAttach0)) { UnpackJoint(in + n, &p->attach[s]); n += kAttachBytes; }
    if (p->flags2 & kGrips) {
        for (int s = 0; s < 2; ++s) p->grip[s] = (float)in[n + s] / 255.0f;
        n += kGripBytes;
    }
    if (p->flags2 & kHands) {
        p->handMirror[0] = (in[n] & 1) != 0;
        p->handMirror[1] = (in[n] & 2) != 0;
        p->modelUpm = (float)Get16(in + n + 1) * 0.001f;
        UnpackPoint(in + n + 3, p->headBone);
        for (int s = 0; s < 2; ++s) {
            UnpackJoint(in + n + 9 + 12 * s, &p->hand[s]);
            UnpackPoint(in + n + 33 + 6 * s, p->handElbow[s]);
        }
        n += kHandsBytes;
    }
    for (int s = 0; s < 2; ++s) {
        if (!(p->flags3 & (s ? kHeldL : kHeldR))) continue;
        Pose::Held& h = p->held[s];
        const uint8_t* b = in + n;
        h.what = b[0] & 0x0F;
        h.state = (uint8_t)(b[0] >> 4);
        if (h.what == kHeldProp) {
            h.nameHash = Get16(b + 1);
            UnpackPoint(b + 3, h.home);
        } else {
            h.code = Get32(b + 1);
            h.scale = (float)b[5] * 0.02f;
        }
        UnpackJoint(b + 9, &h.pose);
        n += kHeldBytes;
    }
    if (p->flags3 & kAllFlags3) {
        p->worldUpm = (float)Get16(in + n) * 0.001f;
        n += kHeldUpmBytes;
    }
    return n;
}

// The turn of a bone's three rows (unit, head's frame or any): q such that Rotate(q, x / y / z) = row 0 / 1 / 2 - with a mirrored set (left-handed:
// its z row against x cross y) the z row is turned round first (*mirrored true). False for rows that are not near a turn.
inline bool QuatOfRows(const float r0[3], const float r1[3], const float r2in[3], float q[4], bool* mirrored) {
    float r2[3] = {r2in[0], r2in[1], r2in[2]};
    const float cx[3] = {r0[1] * r1[2] - r0[2] * r1[1], r0[2] * r1[0] - r0[0] * r1[2], r0[0] * r1[1] - r0[1] * r1[0]};
    const bool m = cx[0] * r2[0] + cx[1] * r2[1] + cx[2] * r2[2] < 0.0f;
    if (m) for (float& c : r2) c = -c;
    if (mirrored) *mirrored = m;
    // the matrix whose columns are the rows (M e_i = row i)
    const float m00 = r0[0], m10 = r0[1], m20 = r0[2], m01 = r1[0], m11 = r1[1], m21 = r1[2], m02 = r2[0], m12 = r2[1], m22 = r2[2];
    const float tr = m00 + m11 + m22;
    if (tr > 0.0f) {
        const float k = std::sqrt(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * k; q[0] = (m21 - m12) / k; q[1] = (m02 - m20) / k; q[2] = (m10 - m01) / k;
    } else if (m00 > m11 && m00 > m22) {
        const float k = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q[3] = (m21 - m12) / k; q[0] = 0.25f * k; q[1] = (m01 + m10) / k; q[2] = (m02 + m20) / k;
    } else if (m11 > m22) {
        const float k = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q[3] = (m02 - m20) / k; q[0] = (m01 + m10) / k; q[1] = 0.25f * k; q[2] = (m12 + m21) / k;
    } else {
        const float k = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q[3] = (m10 - m01) / k; q[0] = (m02 + m20) / k; q[1] = (m12 + m21) / k; q[2] = 0.25f * k;
    }
    return Normalize(q);
}

// A pose that may be drawn: an eye height a person has (0.5 - 2.5 m; standing or seated).
inline bool Plausible(const Pose& p) { return p.eyeM >= 0.5f && p.eyeM <= 2.5f; }

// Our pose to the host. unit: our hero's unit id as our client has it (the game's ids are the server's, the same on every PC).
inline uint32_t BuildPoseMsg(uint32_t unit, const Pose& p, uint8_t* out, uint32_t cap) {
    if (cap < 6 + PoseBytes(p)) return 0;
    out[0] = kVersion;
    out[1] = 0;
    Put32(out + 2, unit);
    return 6 + PackPose(p, out + 6);
}
inline bool ParsePoseMsg(const void* data, uint32_t size, uint32_t* unit, Pose* p) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size < kPoseMsgBytes || b[0] != kVersion) return false;
    *unit = Get32(b + 2);
    return UnpackPose(b + 6, size - 6, p) != 0;
}

struct RelayEntry { uint32_t unit = 0; Pose pose; };

// A relay of as many of the entries (from the first) as fit `cap` bytes (at most kRelayMax); *taken: how many went in. Returns its bytes, 0 none.
inline uint32_t BuildRelay(const RelayEntry* e, int n, uint8_t* out, uint32_t cap, int* taken = nullptr) {
    if (taken) *taken = 0;
    if (n < 1 || cap < 2) return 0;
    out[0] = kVersion;
    uint32_t at = 2;
    int k = 0;
    while (k < n && k < kRelayMax && at + 4 + PoseBytes(e[k].pose) <= cap) {
        Put32(out + at, e[k].unit);
        at += 4 + PackPose(e[k].pose, out + at + 4);
        ++k;
    }
    if (!k) return 0;
    out[1] = (uint8_t)k;
    if (taken) *taken = k;
    return at;
}
// The entries of a relay: their count, 0 for a message that is not one.
inline int ParseRelay(const void* data, uint32_t size, RelayEntry* out, int cap) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size < 2 || b[0] != kVersion) return 0;
    const int n = b[1];
    if (n < 1 || n > kRelayMax || n > cap) return 0;
    uint32_t at = 2;
    for (int i = 0; i < n; ++i) {
        if (at + 4 > size) return 0;
        out[i].unit = Get32(b + at);
        const uint32_t used = UnpackPose(b + at + 4, size - at - 4, &out[i].pose);
        if (!used) return 0;
        at += 4 + used;
    }
    return n;
}

inline uint32_t BuildPing(uint16_t seq, uint32_t clientMs, uint8_t* out, uint32_t cap) {
    if (cap < kPingBytes) return 0;
    out[0] = kVersion; out[1] = 0;
    Put16(out + 2, seq);
    Put32(out + 4, clientMs);
    return kPingBytes;
}
inline bool ParsePing(const void* data, uint32_t size, uint16_t* seq, uint32_t* clientMs) {
    const uint8_t* b = static_cast<const uint8_t*>(data);
    if (!b || size < kPingBytes || b[0] != kVersion) return false;
    *seq = Get16(b + 2);
    *clientMs = Get32(b + 4);
    return true;
}
inline uint32_t BuildPong(uint16_t seq, uint32_t clientMs, uint32_t hostMs, uint8_t* out, uint32_t cap) {
    if (cap < kPongBytes || !BuildPing(seq, clientMs, out, cap)) return 0;
    Put32(out + 8, hostMs);
    return kPongBytes;
}
inline bool ParsePong(const void* data, uint32_t size, uint16_t* seq, uint32_t* clientMs, uint32_t* hostMs) {
    if (size < kPongBytes || !ParsePing(data, size, seq, clientMs)) return false;
    *hostMs = Get32(static_cast<const uint8_t*>(data) + 8);
    return true;
}

// ---- what came through ------------------------------------------------------------------------------------------------------------------

// One sender's sequence: how many came, how many were skipped (lost or rate-limited at the sender), how many came late or twice.
struct SeqStats {
    bool any = false;
    uint16_t last = 0;
    uint32_t got = 0, lost = 0, late = 0;
    // true: newer than any before (use it); false: late or a duplicate (drop it)
    bool Add(uint16_t seq) {
        if (!any) { any = true; last = seq; ++got; return true; }
        const int16_t d = (int16_t)(uint16_t)(seq - last);
        if (d <= 0) { ++late; return false; }
        lost += (uint32_t)(d - 1);
        last = seq;
        ++got;
        return true;
    }
};

// The round trip of the pings: the last, the least and a running average, milliseconds.
struct RttStats {
    uint32_t n = 0;
    float last = 0.0f, least = 0.0f, avg = 0.0f;
    void Add(uint32_t sentMs, uint32_t nowMs) {
        const float rtt = (float)(uint32_t)(nowMs - sentMs);
        if (rtt > 60000.0f) return;   // not one of ours (a clock gone round, a stray)
        last = rtt;
        least = n == 0 ? rtt : std::min(least, rtt);
        avg = n == 0 ? rtt : avg + (rtt - avg) * 0.1f;
        ++n;
    }
};

// Calls of one kind and what they answered, counted over one window (a second): the send rate and how much of it was rate-limited.
struct RateWindow {
    uint64_t start = 0;
    uint32_t ok = 0, limited = 0, other = 0;
    uint32_t lastOk = 0, lastLimited = 0, lastOther = 0;   // the window before
    bool Roll(uint64_t now, uint64_t spanMs = 1000) {   // true when a window was just closed
        if (!start) { start = now; return false; }
        if (now - start < spanMs) return false;
        lastOk = ok; lastLimited = limited; lastOther = other;
        ok = limited = other = 0;
        start = now;
        return true;
    }
};

// ---- the host's table ---------------------------------------------------------------------------------------------------------------------

// The host keeps each connected peer's latest pose, and the unit id it first said it was: a peer cannot speak for another hero later on
// (D2RLoader's PlayerHandle is opaque - it cannot be turned into a unit id - so the id is the sender's word, held to). Visual only.
constexpr int kMaxPeers = 16;

struct HostPeer {
    uint64_t peer = 0;     // 0 = a free slot
    uint32_t unit = 0;     // 0 until its first pose
    bool changed = false;  // a pose newer than the last relay
    uint64_t atMs = 0;     // when its last pose came (the host's clock)
    uint64_t heardMs = 0;  // when anything of it came last - a pose, a ping: a peer is relayed TO while it is heard, whether it sends poses or not
    Pose pose;
    SeqStats seq;
};

enum class Accept { Ok, Old, WrongUnit, Full };

class HostTable {
public:
    std::array<HostPeer, kMaxPeers> p{};

    HostPeer* Find(uint64_t peer) {
        for (auto& s : p) if (s.peer == peer && peer) return &s;
        return nullptr;
    }
    HostPeer* Add(uint64_t peer) {
        if (!peer) return nullptr;
        if (HostPeer* s = Find(peer)) return s;
        for (auto& s : p) if (!s.peer) { s = HostPeer{}; s.peer = peer; return &s; }
        return nullptr;
    }
    void Remove(uint64_t peer) { if (HostPeer* s = Find(peer)) *s = HostPeer{}; }
    int Count() const { int n = 0; for (const auto& s : p) n += s.peer ? 1 : 0; return n; }

    Accept Take(uint64_t peer, uint32_t unit, const Pose& pose, uint64_t nowMs) {
        HostPeer* s = Add(peer);
        if (!s) return Accept::Full;
        if (!unit || (s->unit && s->unit != unit)) return Accept::WrongUnit;
        if (!s->seq.Add(pose.seq)) return Accept::Old;
        s->unit = unit;
        s->pose = pose;
        s->changed = true;
        s->atMs = s->heardMs = nowMs;
        return Accept::Ok;
    }
    // Anything else of a peer came (a ping): it is a destination from now on, even if it sends no pose of its own (a player not in VR F4, a
    // flat one: the 2026-10-26 two-PC test - the host's table held only the peers that sent poses, so the flat client never got the host's).
    void Heard(uint64_t peer, uint64_t nowMs) {
        if (HostPeer* s = Add(peer)) s->heardMs = nowMs;
    }
    // Peers not heard of for silentMs: out (not relayed to or from).
    void Forget(uint64_t nowMs, uint64_t silentMs) {
        for (auto& s : p)
            if (s.peer && nowMs - s.heardMs > silentMs) s = HostPeer{};
    }
    int Senders() const { int n = 0; for (const auto& s : p) n += s.peer && s.unit ? 1 : 0; return n; }

    // What one destination is to be sent: every OTHER peer's pose that changed since the last Flush (echo: its own too - a one-PC test of the
    // whole path). Returns how many entries were written (BuildRelay then packs them into as few relays as fit).
    int Build(uint64_t dest, bool echo, RelayEntry* out, int max) const {
        int k = 0;
        for (const auto& s : p) {
            if (!s.peer || !s.changed || !s.unit || (s.peer == dest && !echo) || k >= max) continue;
            out[k].unit = s.unit;
            out[k].pose = s.pose;
            ++k;
        }
        return k;
    }
    void Flush() { for (auto& s : p) s.changed = false; }
};

// ---- the receiver's buffer: a player's poses played back smoothly -------------------------------------------------------------------------

// The poses come at the sender's rate with the network's jitter on top. The buffer keeps the last few and plays them back kDelayMs
// behind the newest moment it can be sure of: the sender's clock is put on ours by the smallest (arrival - sent) seen, the poses
// either side of that moment are blended. Nothing newer than the newest pose is made up (no extrapolation); a player not heard of for
// kStaleMs is gone.
constexpr int kTrackSamples = 8;
constexpr uint32_t kDelayMsDefault = 100;
constexpr uint64_t kStaleMs = 1000;

inline void Slerp(const float a[4], const float b0[4], float t, float out[4]) {
    float b[4] = {b0[0], b0[1], b0[2], b0[3]};
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0.0f) { for (float& c : b) c = -c; d = -d; }
    if (d > 0.9995f) {   // near: a plain blend, normalized
        for (int i = 0; i < 4; ++i) out[i] = a[i] + (b[i] - a[i]) * t;
        Normalize(out);
        return;
    }
    const float th = std::acos(std::clamp(d, -1.0f, 1.0f)), s = std::sin(th);
    const float wa = std::sin((1.0f - t) * th) / s, wb = std::sin(t * th) / s;
    for (int i = 0; i < 4; ++i) out[i] = a[i] * wa + b[i] * wb;
    Normalize(out);
}

inline void BlendJoint(const Joint& a, const Joint& b, float t, Joint* out) {
    for (int i = 0; i < 3; ++i) out->p[i] = a.p[i] + (b.p[i] - a.p[i]) * t;
    Slerp(a.q, b.q, t, out->q);
}

// a and b blended at t (0 = a, 1 = b). A joint tracked in only one of them is taken from the nearer one if that one tracks it.
inline Pose Blend(const Pose& a, const Pose& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    Pose o = t < 0.5f ? a : b;
    const Joint* ja[3] = {&a.head, &a.right, &a.left};
    const Joint* jb[3] = {&b.head, &b.right, &b.left};
    Joint* jo[3] = {&o.head, &o.right, &o.left};
    const uint8_t bit[3] = {kHead, kRight, kLeft};
    uint8_t flags = (uint8_t)(o.flags & kF4);
    for (int i = 0; i < 3; ++i) {
        const bool inA = (a.flags & bit[i]) != 0, inB = (b.flags & bit[i]) != 0;
        if (inA && inB) { BlendJoint(*ja[i], *jb[i], t, jo[i]); flags |= bit[i]; }
        else if ((t < 0.5f && inA) || (t >= 0.5f && inB)) flags |= bit[i];
    }
    // the body and the weapons: blended where both have them, else the nearer one's
    if ((a.flags & kBody) && (b.flags & kBody)) {
        for (int s = 0; s < 2; ++s)
            for (int i = 0; i < 3; ++i) {
                o.shoulder[s][i] = a.shoulder[s][i] + (b.shoulder[s][i] - a.shoulder[s][i]) * t;
                o.elbow[s][i] = a.elbow[s][i] + (b.elbow[s][i] - a.elbow[s][i]) * t;
            }
        flags |= kBody;
    } else if ((t < 0.5f ? a.flags : b.flags) & kBody) flags |= kBody;
    for (int w = 0; w < 2; ++w) {
        const uint8_t wbit = w ? kWeapon1 : kWeapon0;
        if ((a.flags & wbit) && (b.flags & wbit) && a.weapon[w].kind == b.weapon[w].kind && a.weapon[w].hand == b.weapon[w].hand) {
            for (int i = 0; i < 3; ++i) {
                o.weapon[w].butt[i] = a.weapon[w].butt[i] + (b.weapon[w].butt[i] - a.weapon[w].butt[i]) * t;
                o.weapon[w].tip[i] = a.weapon[w].tip[i] + (b.weapon[w].tip[i] - a.weapon[w].tip[i]) * t;
            }
            o.weapon[w].radius = a.weapon[w].radius + (b.weapon[w].radius - a.weapon[w].radius) * t;
            flags |= wbit;
        } else if ((t < 0.5f ? a.flags : b.flags) & wbit) flags |= wbit;
    }
    if ((a.flags & kModelYaw) && (b.flags & kModelYaw)) {
        o.yawInModel = WrapPi(a.yawInModel + WrapPi(b.yawInModel - a.yawInModel) * t);
        flags |= kModelYaw;
    } else if ((t < 0.5f ? a.flags : b.flags) & kModelYaw) flags |= kModelYaw;
    // formats 5 - 7: the attach bones, the grips, the hands as drawn, what the hands have - blended where both have the same (the nearer one's flags and the rest:
    // without it the weapon and the hands stepped at the poses' 30 a second while drawn at 90)
    for (int s = 0; s < 2; ++s) {
        const uint8_t at = s ? kAttach1 : kAttach0, mi = s ? kMirror1 : kMirror0;
        if ((a.flags2 & at) && (b.flags2 & at) && (a.flags2 & mi) == (b.flags2 & mi) && (o.flags2 & at)) BlendJoint(a.attach[s], b.attach[s], t, &o.attach[s]);
    }
    if ((a.flags2 & kGrips) && (b.flags2 & kGrips) && (o.flags2 & kGrips))
        for (int s = 0; s < 2; ++s) o.grip[s] = a.grip[s] + (b.grip[s] - a.grip[s]) * t;
    if ((a.flags2 & kHands) && (b.flags2 & kHands) && (o.flags2 & kHands) && a.handMirror[0] == b.handMirror[0] && a.handMirror[1] == b.handMirror[1]) {
        for (int s = 0; s < 2; ++s) {
            BlendJoint(a.hand[s], b.hand[s], t, &o.hand[s]);
            for (int i = 0; i < 3; ++i) o.handElbow[s][i] = a.handElbow[s][i] + (b.handElbow[s][i] - a.handElbow[s][i]) * t;
        }
        for (int i = 0; i < 3; ++i) o.headBone[i] = a.headBone[i] + (b.headBone[i] - a.headBone[i]) * t;
        o.modelUpm = a.modelUpm + (b.modelUpm - a.modelUpm) * t;
    }
    for (int s = 0; s < 2; ++s) {
        const uint8_t hb = s ? kHeldL : kHeldR;
        const Pose::Held &ha = a.held[s], &hbb = b.held[s];
        if ((a.flags3 & hb) && (b.flags3 & hb) && (o.flags3 & hb) && ha.what == hbb.what && ha.code == hbb.code && ha.nameHash == hbb.nameHash)
            BlendJoint(ha.pose, hbb.pose, t, &o.held[s].pose);
    }
    o.flags = flags;
    o.heading = WrapPi(a.heading + WrapPi(b.heading - a.heading) * t);
    o.eyeM = a.eyeM + (b.eyeM - a.eyeM) * t;
    return o;
}

class Track {
public:
    struct Entry { uint64_t recvMs = 0; int64_t sentMs = 0; Pose pose; };

    // A pose that came now (our clock). Late ones are the caller's to drop (SeqStats).
    void Add(uint64_t nowMs, const Pose& p) {
        // the sender's 32-bit clock carried on past its wrap, against the last one
        int64_t sent = (int64_t)p.ms;
        if (n_ > 0) sent = last_ + (int64_t)(int32_t)(p.ms - (uint32_t)last_);
        last_ = sent;
        s_[head_] = {nowMs, sent, p};
        head_ = (head_ + 1) % kTrackSamples;
        n_ = std::min(n_ + 1, kTrackSamples);
    }
    bool Empty() const { return n_ == 0; }
    uint64_t NewestRecv() const { return n_ ? At(n_ - 1).recvMs : 0; }

    // The pose to show now, delayMs behind. False: nothing (none yet, or stale).
    bool Sample(uint64_t nowMs, uint32_t delayMs, Pose* out) const {
        if (!n_ || nowMs - NewestRecv() > kStaleMs) return false;
        int64_t offset = INT64_MAX;   // our clock - theirs, the least seen (the fastest pose)
        for (int i = 0; i < n_; ++i) offset = std::min(offset, (int64_t)At(i).recvMs - At(i).sentMs);
        const int64_t target = (int64_t)nowMs - offset - (int64_t)delayMs;   // in the sender's clock
        const Entry& newest = At(n_ - 1);
        if (target >= newest.sentMs) { *out = newest.pose; return true; }
        const Entry& oldest = At(0);
        if (target <= oldest.sentMs) { *out = oldest.pose; return true; }
        for (int i = n_ - 1; i > 0; --i) {
            const Entry& a = At(i - 1);
            const Entry& b = At(i);
            if (target < a.sentMs) continue;
            const int64_t span = b.sentMs - a.sentMs;
            *out = span > 0 ? Blend(a.pose, b.pose, (float)(target - a.sentMs) / (float)span) : b.pose;
            return true;
        }
        *out = newest.pose;
        return true;
    }

private:
    const Entry& At(int i) const { return s_[(head_ - n_ + i + kTrackSamples) % kTrackSamples]; }   // 0 = oldest
    std::array<Entry, kTrackSamples> s_{};
    int head_ = 0, n_ = 0;
    int64_t last_ = 0;
};

// ---- the frames -----------------------------------------------------------------------------------------------------------------------------

// The heading of a forward direction on the ground in the game's world (vrcam's camF): its x and z are (-sin h, -cos h). The frame's
// right is (cos h, 0, -sin h) - as vrcam turns its camera.
inline float HeadingOf(const float fwd[3]) { return std::atan2(-fwd[0], -fwd[2]); }
inline void Axes(float heading, float right[3], float ahead[3]) {
    const float c = std::cos(heading), s = std::sin(heading);
    right[0] = c; right[1] = 0.0f; right[2] = -s;
    ahead[0] = -s; ahead[1] = 0.0f; ahead[2] = -c;
}

// A world point (world units) into the hero's frame (metres): from the hero's feet, over the units per metre.
inline void ToHero(const float world[3], const float feet[3], float heading, float upm, float out[3]) {
    float r[3], a[3];
    Axes(heading, r, a);
    const float d[3] = {(world[0] - feet[0]) / upm, (world[1] - feet[1]) / upm, (world[2] - feet[2]) / upm};
    out[0] = d[0] * r[0] + d[2] * r[2];
    out[1] = d[1];
    out[2] = -(d[0] * a[0] + d[2] * a[2]);
}
// ... and back: a direction (no feet, no scale) or a point.
inline void DirToWorld(const float local[3], float heading, float out[3]) {
    float r[3], a[3];
    Axes(heading, r, a);
    for (int i = 0; i < 3; ++i) out[i] = local[0] * r[i] - local[2] * a[i];
    out[1] += local[1];
}
inline void ToWorld(const float local[3], const float feet[3], float heading, float upm, float out[3]) {
    float d[3];
    DirToWorld(local, heading, d);
    for (int i = 0; i < 3; ++i) out[i] = feet[i] + d[i] * upm;
}

inline void Rotate(const float q[4], const float v[3], float out[3]) {   // q v q*
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float tx = 2.0f * (y * v[2] - z * v[1]), ty = 2.0f * (z * v[0] - x * v[2]), tz = 2.0f * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

// A world direction into the hero's frame (ToHero without the feet and the scale).
inline void DirToHero(const float d[3], float heading, float out[3]) {
    float r[3], a[3];
    Axes(heading, r, a);
    out[0] = d[0] * r[0] + d[2] * r[2];
    out[1] = d[1];
    out[2] = -(d[0] * a[0] + d[2] * a[2]);
}
// [format 7] A thing as drawn (its world matrix, row vectors, world units) in the hero's frame: its origin (metres from the feet), its turn (the rows made unit; a set
// that is not a turn gives false) and its size (the rows' mean length).
inline bool HeldFromWorld(const float m[16], const float feet[3], float heading, float upm, Joint* j, float* scale) {
    float rows[3][3], len = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float w[3] = {m[i * 4], m[i * 4 + 1], m[i * 4 + 2]};
        const float l = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
        if (!(l > 1e-6f) || !std::isfinite(l)) return false;
        const float u[3] = {w[0] / l, w[1] / l, w[2] / l};
        DirToHero(u, heading, rows[i]);
        len += l / 3.0f;
    }
    bool mirrored = false;
    if (!QuatOfRows(rows[0], rows[1], rows[2], j->q, &mirrored) || mirrored) return false;
    ToHero(m + 12, feet, heading, upm, j->p);
    if (scale) *scale = len;
    return std::isfinite(j->p[0] + j->p[1] + j->p[2]);
}
// ... and back into a world: its rows (scaled) and origin from the hero's frame by the feet, heading and units a metre there.
inline void HeldToWorld(const Joint& j, const float feet[3], float heading, float upm, float scale, float m[16]) {
    const float e[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    for (int i = 0; i < 3; ++i) {
        float r[3], w[3];
        Rotate(j.q, e[i], r);
        DirToWorld(r, heading, w);
        for (int k = 0; k < 3; ++k) m[i * 4 + k] = w[k] * scale;
        m[i * 4 + 3] = 0.0f;
    }
    ToWorld(j.p, feet, heading, upm, m + 12);
    m[15] = 1.0f;
}

// The head's turn in its own frame, from the shared block's pitch (+ up) and roll (+ the right ear down): pitch about x, then roll
// about the head's back axis (z).
inline void HeadQuat(float pitchDeg, float rollDeg, float q[4]) {
    const float p = pitchDeg * kPi / 180.0f * 0.5f, r = -rollDeg * kPi / 180.0f * 0.5f;
    const float cp = std::cos(p), sp = std::sin(p), cr = std::cos(r), sr = std::sin(r);
    q[0] = sp * cr; q[1] = -sp * sr; q[2] = cp * sr; q[3] = cp * cr;   // qPitch(x) * qRoll(z)
}

// Where another hero's feet are in our world: our hero's feet (L) moved by the grid's difference between the two heroes (both read the
// same way, so any constant offset of the read cancels); on our ground (flat between them is assumed).
inline void FeetOf(const float ownFeet[3], float ownGridX, float ownGridZ, float gridX, float gridZ, float out[3]) {
    out[0] = ownFeet[0] + (gridX - ownGridX);
    out[1] = ownFeet[1];
    out[2] = ownFeet[2] + (gridZ - ownGridZ);
}

// The markers of one player for the debug draw (D2R_DepthFog.fx's HitboxA / HitboxB: the ends relative to the eye, world units, the
// radius in a.w, the kind in b.w): a ball on the head, a ball on each controller, and a short line along each controller's pointing
// (-z). upm: our world units per metre of the sender's body. Returns the capsules written.
struct MarkerKinds { int head = 0, hand = 0, point = 0; };
constexpr float kHeadBallM = 0.09f, kHandBallM = 0.045f, kPointM = 0.18f, kPointRadiusM = 0.012f;
inline int Markers(const Pose& p, const float feet[3], float upm, const float eye[3], const MarkerKinds& k, float (*a)[4], float (*b)[4], int max) {
    int n = 0;
    auto put = [&](const float pa[3], const float pb[3], float radiusM, int kind) {
        if (n >= max) return;
        for (int i = 0; i < 3; ++i) { a[n][i] = pa[i] - eye[i]; b[n][i] = pb[i] - eye[i]; }
        a[n][3] = radiusM * upm;
        b[n][3] = (float)kind;
        ++n;
    };
    if (!std::isfinite(upm) || upm <= 0.0f) return 0;
    if (p.flags & kHead) {
        float w[3];
        ToWorld(p.head.p, feet, p.heading, upm, w);
        put(w, w, kHeadBallM, k.head);
    }
    const Joint* hands[2] = {&p.right, &p.left};
    const uint8_t bits[2] = {kRight, kLeft};
    for (int h = 0; h < 2; ++h) {
        if (!(p.flags & bits[h])) continue;
        float w[3], fl[3], fw[3], tip[3];
        ToWorld(hands[h]->p, feet, p.heading, upm, w);
        put(w, w, kHandBallM, k.hand);
        const float fwd[3] = {0.0f, 0.0f, -1.0f};
        Rotate(hands[h]->q, fwd, fl);
        DirToWorld(fl, p.heading, fw);
        for (int i = 0; i < 3; ++i) tip[i] = w[i] + fw[i] * kPointM * upm;
        put(w, tip, kPointRadiusM, k.point);
    }
    return n;
}

}  // namespace poseshare
