// Offline checks of vr/pose_share.h ([multiplayer] pose_share): the pose message's packing and quantizing, the relay and ping messages, the
// host's relay table, the stats, the receiver's playback buffer and the frame maths. Run: pose_share_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "pose_share.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace poseshare;

static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static float QuatAngleDeg(const float a[4], const float b[4]) {   // the turn between two unit quaternions (from the chord: acos is blunt near 1)
    const double d = (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2] + (double)a[3] * b[3];
    const double s = d < 0.0 ? -1.0 : 1.0;
    double c2 = 0.0;
    for (int i = 0; i < 4; ++i) c2 += ((double)a[i] - s * b[i]) * ((double)a[i] - s * b[i]);
    return (float)(4.0 * std::asin(std::min(1.0, std::sqrt(c2) * 0.5)) * 180.0 / 3.14159265358979);
}
static void AxisAngle(float ax, float ay, float az, float deg, float q[4]) {
    const float n = std::sqrt(ax * ax + ay * ay + az * az), h = deg * kPi / 360.0f;
    q[0] = ax / n * std::sin(h); q[1] = ay / n * std::sin(h); q[2] = az / n * std::sin(h); q[3] = std::cos(h);
}

static Pose Sample(uint16_t seq, uint32_t ms, float x) {
    Pose p;
    p.flags = kHead | kRight | kLeft | kF4;
    p.seq = seq;
    p.ms = ms;
    p.heading = 1.0f;
    p.eyeM = 1.65f;
    p.head.p[0] = 0.0f; p.head.p[1] = 1.65f; p.head.p[2] = 0.0f;
    p.right.p[0] = x; p.right.p[1] = 1.2f; p.right.p[2] = -0.4f;
    p.left.p[0] = -0.3f; p.left.p[1] = 1.1f; p.left.p[2] = -0.2f;
    AxisAngle(1, 0, 0, 20, p.head.q);
    AxisAngle(0, 1, 0, 30, p.right.q);
    AxisAngle(1, 1, 0, -70, p.left.q);
    return p;
}

int main() {
    // sizes: everything fits D2RLoader's 224 bytes
    CHECK(kPoseMsgBytes == 54);
    CHECK(2 + kRelayMax * kRelayEntryBytes == 210);
    CHECK(2 + kRelayMax * kRelayEntryBytes <= kMaxPayload);
    CHECK(kPoseMaxBytes == 217 && kPoseMsgMaxBytes == 223 && kPoseMsgMaxBytes <= kMaxPayload);
    CHECK(2 + 4 + kPoseMaxBytes <= kMaxPayload);   // a full pose in a relay (alone: format 6's hands as drawn, 7's held)
    CHECK(2 + 2 * (4 + kPoseBytes + kYawBytes + kAttachBytes + kGripBytes) <= kMaxPayload);   // two poses with no hands as drawn, a weapon each

    // millimetres, clamped; angles round the turn
    CHECK(QuantMm(1.2345f) == 1235 || QuantMm(1.2345f) == 1234);
    CHECK(QuantMm(-0.0004f) == 0);
    CHECK(QuantMm(100.0f) == 32767 && QuantMm(-100.0f) == -32767);
    CHECK(QuantMm(NAN) == 0);
    for (float a = -7.0f; a < 7.0f; a += 0.37f) CHECK(Near(WrapPi(DequantAngle(QuantAngle(a)) - a), 0.0f, 1e-4f));
    CHECK(QuantAngle(kPi) == QuantAngle(-kPi));

    // quaternions: every axis and turn back within a hundredth of a degree, q and -q alike
    {
        float worst = 0.0f;
        const float axes[6][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {-1, 2, 3}, {0.2f, -1, 0.1f}};
        for (const auto& ax : axes)
            for (float deg = -350.0f; deg <= 350.0f; deg += 13.0f) {
                float q[4], back[4];
                uint8_t b[6];
                AxisAngle(ax[0], ax[1], ax[2], deg, q);
                PackQuat(q, b);
                UnpackQuat(b, back);
                worst = std::max(worst, QuatAngleDeg(q, back));
                for (float& c : q) c = -c;
                PackQuat(q, b);
                UnpackQuat(b, back);
                worst = std::max(worst, QuatAngleDeg(q, back));
            }
        std::printf("quaternion: worst %.5f deg\n", worst);
        CHECK(worst < 0.01f);
        const float bad[4] = {0, 0, 0, 0};   // not a turn: identity
        float back[4];
        uint8_t b[6];
        PackQuat(bad, b);
        UnpackQuat(b, back);
        CHECK(Near(back[3], 1.0f, 1e-3f));
    }

    // a pose there and back
    {
        const Pose p = Sample(65535, 0xFFFFFF00u, 0.31f);
        uint8_t msg[kMaxPayload];
        const uint32_t n = BuildPoseMsg(0x12345678u, p, msg, sizeof msg);
        CHECK(n == kPoseMsgBytes);
        uint32_t unit = 0;
        Pose q;
        CHECK(ParsePoseMsg(msg, n, &unit, &q));
        CHECK(unit == 0x12345678u);
        CHECK(q.flags == p.flags && q.seq == p.seq && q.ms == p.ms);
        CHECK(Near(q.heading, p.heading, 1e-3f));
        CHECK(Near(q.eyeM, 1.65f, 1e-3f));
        CHECK(Near(q.right.p[0], 0.31f, 6e-4f) && Near(q.right.p[1], 1.2f, 6e-4f) && Near(q.right.p[2], -0.4f, 6e-4f));
        CHECK(QuatAngleDeg(q.left.q, p.left.q) < 0.01f);
        CHECK(Plausible(q));
        CHECK(!ParsePoseMsg(msg, n - 1, &unit, &q));   // short
        msg[0] = 9;
        CHECK(!ParsePoseMsg(msg, n, &unit, &q));       // another format
        CHECK(BuildPoseMsg(1, p, msg, kPoseMsgBytes - 1) == 0);
        Pose odd = p;
        odd.eyeM = 0.1f;
        CHECK(!Plausible(odd));
    }

    // format 2: the body and two weapons there and back (100 bytes), a bare pose stays 48
    {
        Pose p = Sample(9, 77, 0.2f);
        CHECK(PoseBytes(p) == kPoseBytes);
        p.flags |= kBody | kWeapon0 | kWeapon1;
        const float sh[2][3] = {{0.18f, 1.45f, 0.02f}, {-0.19f, 1.44f, 0.03f}}, el[2][3] = {{0.25f, 1.15f, -0.1f}, {-0.3f, 1.2f, 0.05f}};
        std::memcpy(p.shoulder, sh, sizeof sh);
        std::memcpy(p.elbow, el, sizeof el);
        const float butt[3] = {0.31f, 1.1f, -0.42f}, tip[3] = {0.35f, 1.9f, -1.1f};
        std::memcpy(p.weapon[0].butt, butt, sizeof butt);
        std::memcpy(p.weapon[0].tip, tip, sizeof tip);
        p.weapon[0].radius = 0.034f;
        p.weapon[0].kind = 12;
        p.weapon[0].hand = 0;
        p.weapon[1] = p.weapon[0];
        p.weapon[1].hand = 1;
        p.weapon[1].kind = 127;
        p.weapon[1].radius = 0.6f;   // clamped to 25 cm
        CHECK(PoseBytes(p) == kPoseMaxBytes - kYawBytes - 2 * kAttachBytes - kGripBytes - kHandsBytes - 2 * kHeldBytes - kHeldUpmBytes);
        p.flags |= kModelYaw;
        p.yawInModel = -2.5f;
        CHECK(PoseBytes(p) == kPoseMaxBytes - 2 * kAttachBytes - kGripBytes - kHandsBytes - 2 * kHeldBytes - kHeldUpmBytes);
        uint8_t msg[kMaxPayload];
        const uint32_t n = BuildPoseMsg(42, p, msg, sizeof msg);
        CHECK(n == kPoseMsgMaxBytes - 2 * kAttachBytes - kGripBytes - kHandsBytes - 2 * kHeldBytes - kHeldUpmBytes);
        uint32_t unit = 0;
        Pose q;
        CHECK(ParsePoseMsg(msg, n, &unit, &q) && unit == 42);
        CHECK(q.flags == p.flags);
        CHECK(Near(q.shoulder[1][0], -0.19f, 6e-4f) && Near(q.elbow[0][1], 1.15f, 6e-4f));
        CHECK(Near(q.weapon[0].tip[2], -1.1f, 6e-4f) && Near(q.weapon[0].radius, 0.034f, 6e-4f) && q.weapon[0].kind == 12 && q.weapon[0].hand == 0);
        CHECK(q.weapon[1].hand == 1 && q.weapon[1].kind == 127 && Near(q.weapon[1].radius, 0.25f, 1e-4f));
        CHECK((q.flags & kModelYaw) && Near(q.yawInModel, -2.5f, 2e-4f));
        CHECK(!ParsePoseMsg(msg, n - 1, &unit, &q));   // its flags ask for more than came
        Pose w0 = p;
        w0.flags &= ~(kBody | kWeapon0 | kModelYaw);   // only the second weapon: it is read as the second
        const uint32_t n2 = BuildPoseMsg(43, w0, msg, sizeof msg);
        CHECK(n2 == 6 + kPoseBytes + kBladeBytes);
        CHECK(ParsePoseMsg(msg, n2, &unit, &q) && !(q.flags & kWeapon0) && (q.flags & kWeapon1) && q.weapon[1].hand == 1);
        CHECK(BuildPoseMsg(1, p, msg, kPoseMsgMaxBytes - 2 * kAttachBytes - kGripBytes - kHandsBytes - 2 * kHeldBytes - kHeldUpmBytes - 1) == 0);
    }

    // format 4: a weapon out of the hand - its attach bone's place and turn per slot, a mirrored bone told; the turn of a bone's rows
    {
        // rows of a proper turn: 30 degrees about y
        const float c = std::cos(0.5236f), sn = std::sin(0.5236f);
        const float r0[3] = {c, 0.0f, -sn}, r1[3] = {0.0f, 1.0f, 0.0f}, r2[3] = {sn, 0.0f, c};
        float q[4], v[3];
        bool mir = true;
        CHECK(QuatOfRows(r0, r1, r2, q, &mir) && !mir);
        const float ex[3] = {1, 0, 0}, ez[3] = {0, 0, 1};
        Rotate(q, ex, v);
        CHECK(Near(v[0], r0[0], 1e-5f) && Near(v[2], r0[2], 1e-5f));
        Rotate(q, ez, v);
        CHECK(Near(v[0], r2[0], 1e-5f) && Near(v[2], r2[2], 1e-5f));
        // a mirrored bone (a left one): its z row against x cross y
        const float m2[3] = {-sn, 0.0f, -c};
        CHECK(QuatOfRows(r0, r1, m2, q, &mir) && mir);
        Rotate(q, ez, v);
        CHECK(Near(v[0], -m2[0], 1e-5f) && Near(v[2], -m2[2], 1e-5f));   // the turn's z is the row turned round
        // every turn of a set of axes comes back (the 4 branches of the matrix -> quaternion)
        float worst = 0.0f;
        for (int k = 0; k < 40; ++k) {
            float qa[4];
            AxisAngle(std::sin(k * 1.3f), std::cos(k * 0.7f), 0.3f + std::sin(k * 2.1f), -170.0f + 9.0f * (float)k, qa);
            float a0[3], a1[3], a2[3], qb[4];
            const float ey[3] = {0, 1, 0};
            Rotate(qa, ex, a0); Rotate(qa, ey, a1); Rotate(qa, ez, a2);
            CHECK(QuatOfRows(a0, a1, a2, qb, &mir) && !mir);
            worst = std::max(worst, QuatAngleDeg(qa, qb));
        }
        CHECK(worst < 0.01f);
        // on the wire: the second flags byte, the slot's bone after everything else
        Pose p = Sample(5, 50, 0.1f);
        p.flags |= kModelYaw;
        p.flags2 = kAttach1 | kMirror1;
        p.attach[1].p[0] = 0.8f; p.attach[1].p[1] = 0.05f; p.attach[1].p[2] = -1.2f;   // lying on the floor ahead, right
        AxisAngle(1, 0, 0, 90, p.attach[1].q);
        CHECK(PoseBytes(p) == kPoseBytes + kYawBytes + kAttachBytes);
        uint8_t msg[kMaxPayload];
        const uint32_t n = BuildPoseMsg(9, p, msg, sizeof msg);
        uint32_t unit = 0;
        Pose back;
        CHECK(ParsePoseMsg(msg, n, &unit, &back) && back.flags2 == (kAttach1 | kMirror1));
        CHECK(Near(back.attach[1].p[2], -1.2f, 6e-4f) && QuatAngleDeg(back.attach[1].q, p.attach[1].q) < 0.01f);
        CHECK(!ParsePoseMsg(msg, n - 1, &unit, &back));
        Pose all = p;
        all.flags |= kBody | kWeapon0 | kWeapon1;
        all.flags2 = kAttach0 | kAttach1 | kGrips | kHoldR | kHands;
        all.grip[0] = 0.8f; all.grip[1] = 2.0f;   // (clamped)
        // format 6: the hands as drawn - the left one mirrored - the elbows, the head bone, the units a metre
        all.handMirror[1] = true;
        all.modelUpm = 4.837f;
        all.headBone[0] = 0.01f; all.headBone[1] = 1.62f; all.headBone[2] = 0.09f;
        all.hand[0].p[0] = 0.31f; all.hand[0].p[1] = 1.12f; all.hand[0].p[2] = -0.42f;
        all.hand[1].p[0] = -0.27f; all.hand[1].p[1] = 0.98f; all.hand[1].p[2] = -0.05f;
        AxisAngle(0, 1, 0, 35, all.hand[0].q);
        AxisAngle(1, 0, 1, -120, all.hand[1].q);
        all.handElbow[0][0] = 0.24f; all.handElbow[0][1] = 1.21f; all.handElbow[0][2] = 0.03f;
        all.handElbow[1][0] = -0.25f; all.handElbow[1][1] = 1.19f; all.handElbow[1][2] = 0.11f;
        // format 7: a belt potion in the right hand, a prop flying from the left
        all.flags3 = kHeldR | kHeldL;
        all.worldUpm = 4.62f;
        all.held[0].what = kHeldItem;
        all.held[0].state = kInHand;
        all.held[0].code = 0x20337068u;   // "hp3 "
        all.held[0].scale = 0.56f;
        all.held[0].pose.p[0] = 0.33f; all.held[0].pose.p[1] = 1.08f; all.held[0].pose.p[2] = -0.41f;
        AxisAngle(1, 1, 0, 65, all.held[0].pose.q);
        all.held[1].what = kHeldProp;
        all.held[1].state = kFlying;
        all.held[1].nameHash = NameHash("vase_small");
        all.held[1].home[0] = -2.4f; all.held[1].home[1] = 0.8f; all.held[1].home[2] = 3.1f;
        all.held[1].pose.p[0] = 4.25f; all.held[1].pose.p[1] = 2.2f; all.held[1].pose.p[2] = -9.75f;   // far out: thrown
        AxisAngle(0, 0, 1, 170, all.held[1].pose.q);
        CHECK(PoseBytes(all) == kPoseMaxBytes && BuildPoseMsg(9, all, msg, sizeof msg) == kPoseMsgMaxBytes);
        CHECK(ParsePoseMsg(msg, kPoseMsgMaxBytes, &unit, &back) && (back.flags2 & kHoldR) && !(back.flags2 & kHoldL) && (back.flags2 & kGrips));
        CHECK(Near(back.grip[0], 0.8f, 0.003f) && back.grip[1] == 1.0f);
        CHECK((back.flags2 & kHands) && !back.handMirror[0] && back.handMirror[1] && Near(back.modelUpm, 4.837f, 6e-4f));
        CHECK(Near(back.headBone[1], 1.62f, 6e-4f) && Near(back.headBone[2], 0.09f, 6e-4f));
        CHECK(Near(back.hand[0].p[2], -0.42f, 6e-4f) && Near(back.hand[1].p[0], -0.27f, 6e-4f) && Near(back.handElbow[1][2], 0.11f, 6e-4f));
        CHECK(QuatAngleDeg(back.hand[0].q, all.hand[0].q) < 0.01f && QuatAngleDeg(back.hand[1].q, all.hand[1].q) < 0.01f);
        CHECK(back.flags3 == (kHeldR | kHeldL) && Near(back.eyeM, all.eyeM, 6e-4f) && Near(back.worldUpm, 4.62f, 6e-4f));   // (the flags in the eye's top bits)
        CHECK(back.held[0].what == kHeldItem && back.held[0].state == kInHand && back.held[0].code == 0x20337068u && Near(back.held[0].scale, 0.56f, 0.011f));
        CHECK(Near(back.held[0].pose.p[2], -0.41f, 6e-4f) && QuatAngleDeg(back.held[0].pose.q, all.held[0].pose.q) < 0.01f);
        CHECK(back.held[1].what == kHeldProp && back.held[1].state == kFlying && back.held[1].nameHash == NameHash("vase_small") && back.held[1].code == 0);
        CHECK(Near(back.held[1].home[0], -2.4f, 6e-4f) && Near(back.held[1].home[2], 3.1f, 6e-4f) && Near(back.held[1].pose.p[2], -9.75f, 6e-4f));
        CHECK(QuatAngleDeg(back.held[1].pose.q, all.held[1].pose.q) < 0.01f);
        CHECK(!ParsePoseMsg(msg, kPoseMsgMaxBytes - 1, &unit, &back));   // the held cut short: no pose
        {   // one hand only: one record and the units a metre; none: neither (and an eye height up to 4.095 m untouched by the flags)
            Pose one = all;
            one.flags3 = kHeldL;
            CHECK(PoseBytes(one) == kPoseMaxBytes - kHeldBytes);
            Pose none = all;
            none.flags3 = 0;
            CHECK(PoseBytes(none) == kPoseMaxBytes - 2 * kHeldBytes - kHeldUpmBytes);
            one.eyeM = 2.5f;
            const uint32_t n1 = BuildPoseMsg(9, one, msg, sizeof msg);
            CHECK(n1 && ParsePoseMsg(msg, n1, &unit, &back) && back.flags3 == kHeldL && Near(back.eyeM, 2.5f, 6e-4f) && back.held[1].what == kHeldProp &&
                  back.held[0].what == kHeldNone);
            one.eyeM = 9.0f;   // (clamped to the field's 4.095 m: the flags are not touched)
            const uint32_t n2 = BuildPoseMsg(9, one, msg, sizeof msg);
            CHECK(n2 && ParsePoseMsg(msg, n2, &unit, &back) && back.flags3 == kHeldL && Near(back.eyeM, 4.095f, 6e-4f));
        }
        // the playback blends a held thing's place where both poses have the same thing
        {
            Pose a = all, b = all;
            b.held[1].pose.p[0] = 5.25f;
            const Pose mid = Blend(a, b, 0.5f);
            CHECK(Near(mid.held[1].pose.p[0], 4.75f, 1e-5f));
            b.held[1].nameHash = (uint16_t)(a.held[1].nameHash + 1);   // another prop: the nearer one's, not a mix
            const Pose nearer = Blend(a, b, 0.25f);
            CHECK(Near(nearer.held[1].pose.p[0], 4.25f, 1e-5f));
            b = all;
            b.hand[0].p[0] = all.hand[0].p[0] + 0.2f;
            b.attach[0].p[1] = all.attach[0].p[1] + 0.1f;
            const Pose h = Blend(a, b, 0.5f);
            CHECK(Near(h.hand[0].p[0], all.hand[0].p[0] + 0.1f, 1e-5f) && Near(h.attach[0].p[1], all.attach[0].p[1] + 0.05f, 1e-5f));   // (formats 5 and 6 blended too)
        }
        Pose noHands = all;
        noHands.flags2 &= (uint8_t)~kHands;
        CHECK(PoseBytes(noHands) == kPoseMaxBytes - kHandsBytes);
        CHECK(NameHash("vase_small") == NameHash("vase_small") && NameHash("vase_small") != NameHash("vase_large") && NameHash("") == NameHash(nullptr));
        RelayEntry e[2];
        e[0].unit = 1; e[0].pose = all;
        e[1].unit = 2; e[1].pose = all;
        int taken = 0;
        CHECK(BuildRelay(e, 2, msg, sizeof msg, &taken) == 2 + 4 + kPoseMaxBytes && taken == 1);   // alone in its relay
    }

    // relays: four bare poses in one, two full ones; what does not fit is left for the next
    {
        RelayEntry e[6];
        for (int i = 0; i < 6; ++i) { e[i].unit = 100u + i; e[i].pose = Sample((uint16_t)i, 1000u + i, 0.1f * i); }
        uint8_t msg[kMaxPayload];
        for (int n = 1; n <= kRelayMax; ++n) {
            int taken = 0;
            const uint32_t size = BuildRelay(e, n, msg, sizeof msg, &taken);
            CHECK(size == 2 + (uint32_t)n * kRelayEntryBytes && taken == n);
            RelayEntry got[kRelayMax];
            CHECK(ParseRelay(msg, size, got, kRelayMax) == n);
            CHECK(got[n - 1].unit == 100u + (uint32_t)(n - 1) && got[n - 1].pose.seq == n - 1);
            CHECK(ParseRelay(msg, size - 1, got, kRelayMax) == 0);
        }
        int taken = 0;
        CHECK(BuildRelay(e, 0, msg, sizeof msg, &taken) == 0 && taken == 0);
        CHECK(BuildRelay(e, 6, msg, sizeof msg, &taken) == 2 + 4 * kRelayEntryBytes && taken == kRelayMax);   // (at most four)
        for (int i = 0; i < 6; ++i) e[i].pose.flags |= kBody | kWeapon0 | kWeapon1;
        int at = 0, relays = 0;
        while (at < 6) {
            const uint32_t size = BuildRelay(e + at, 6 - at, msg, sizeof msg, &taken);
            CHECK(size > 0 && size <= kMaxPayload && taken == 2);
            RelayEntry got[kRelayMax];
            CHECK(ParseRelay(msg, size, got, kRelayMax) == taken && got[1].unit == 100u + (uint32_t)(at + 1) && (got[1].pose.flags & kWeapon1));
            if (!taken) break;
            at += taken;
            ++relays;
        }
        CHECK(relays == 3);   // six full poses: three relays
        // one full and two bare: all three in one (2 + 104 + 2 * 52 = 210)
        e[1].pose.flags &= ~(kBody | kWeapon0 | kWeapon1);
        e[2].pose.flags &= ~(kBody | kWeapon0 | kWeapon1);
        CHECK(BuildRelay(e, 3, msg, sizeof msg, &taken) == 210 && taken == 3);
        // format 3: two full poses with the model's yaw too still fit (2 + 2 * 106 = 214)
        for (int i = 0; i < 2; ++i) e[i].pose.flags |= kBody | kWeapon0 | kWeapon1 | kModelYaw;
        CHECK(BuildRelay(e, 2, msg, sizeof msg, &taken) == 214 && taken == 2);
        RelayEntry got[kRelayMax];
        CHECK(ParseRelay(msg, 214, got, kRelayMax) == 2 && (got[1].pose.flags & kModelYaw));
    }

    // ping and pong
    {
        uint8_t msg[16];
        CHECK(BuildPong(7, 123456u, 999u, msg, sizeof msg) == kPongBytes);
        uint16_t seq = 0;
        uint32_t c = 0, h = 0;
        CHECK(ParsePong(msg, kPongBytes, &seq, &c, &h) && seq == 7 && c == 123456u && h == 999u);
        CHECK(ParsePing(msg, kPingBytes, &seq, &c) && seq == 7 && c == 123456u);
        CHECK(!ParsePong(msg, kPingBytes, &seq, &c, &h));
    }

    // the sequence: gaps are lost, old ones late, the wrap is no gap
    {
        SeqStats s;
        CHECK(s.Add(65534));
        CHECK(s.Add(65535));
        CHECK(s.Add(0));
        CHECK(s.lost == 0);
        CHECK(s.Add(3));
        CHECK(s.lost == 2);
        CHECK(!s.Add(2) && !s.Add(3));
        CHECK(s.late == 2 && s.got == 4);
    }

    // the round trip
    {
        RttStats r;
        r.Add(1000, 1040);
        r.Add(0xFFFFFFF0u, 20);   // over the clock's wrap: 36 ms
        CHECK(r.n == 2 && Near(r.least, 36.0f, 0.01f) && Near(r.last, 36.0f, 0.01f));
        r.Add(5000, 4000);        // "negative": not ours
        CHECK(r.n == 2);
    }

    // the rate window
    {
        RateWindow w;
        CHECK(!w.Roll(10));
        w.ok = 5; w.limited = 2;
        CHECK(!w.Roll(500));
        CHECK(w.Roll(1010));
        CHECK(w.lastOk == 5 && w.lastLimited == 2 && w.ok == 0);
    }

    // the host's table: ids held to, others' poses relayed, an echo for a one-PC test, four in a relay
    {
        HostTable t;
        CHECK(t.Take(11, 501, Sample(1, 10, 0), 0) == Accept::Ok);
        CHECK(t.Take(11, 502, Sample(2, 20, 0), 0) == Accept::WrongUnit);   // the same peer cannot speak for another hero
        CHECK(t.Take(11, 501, Sample(1, 20, 0), 0) == Accept::Old);
        CHECK(t.Take(11, 0, Sample(3, 20, 0), 0) == Accept::WrongUnit);
        CHECK(t.Take(12, 502, Sample(1, 10, 0), 0) == Accept::Ok);
        RelayEntry out[kMaxPeers];
        CHECK(t.Build(11, false, out, kMaxPeers) == 1 && out[0].unit == 502);
        CHECK(t.Build(11, true, out, kMaxPeers) == 2);
        t.Flush();
        CHECK(t.Build(12, false, out, kMaxPeers) == 0);   // nothing new
        for (uint64_t peer = 20; peer < 26; ++peer) CHECK(t.Take(peer, (uint32_t)(600 + peer), Sample(1, 0, 0), 0) == Accept::Ok);
        CHECK(t.Build(11, false, out, kMaxPeers) == 6);
        CHECK(t.Build(11, false, out, 4) == 4);
        CHECK(t.Count() == 8);
        t.Remove(12);
        CHECK(t.Count() == 7 && !t.Find(12));
        // the 2026-10-26 two-PC test: the host in VR sends poses, the flat client only pings - the client must still get the host's pose
        {
            HostTable h;
            const uint64_t hostPeer = 1, flatPeer = 2;
            CHECK(h.Take(hostPeer, 900, Sample(1, 0, 0), 1000) == Accept::Ok);
            h.Heard(flatPeer, 1000);   // a ping
            CHECK(h.Count() == 2 && h.Senders() == 1);
            int dests = 0, toFlat = 0, toHost = 0;
            for (const auto& s : h.p) {
                if (!s.peer) continue;
                ++dests;
                RelayEntry e[kMaxPeers];
                const int n = h.Build(s.peer, false, e, kMaxPeers);
                if (s.peer == flatPeer) { toFlat = n; CHECK(n == 1 && e[0].unit == 900); }
                if (s.peer == hostPeer) toHost = n;
            }
            CHECK(dests == 2 && toFlat == 1 && toHost == 0);   // the host gets nothing back without the echo
            // still heard by its pings: kept; silent for 5 s: gone
            h.Flush();
            h.Heard(flatPeer, 5500);
            CHECK(h.Take(hostPeer, 900, Sample(2, 33, 0), 5500) == Accept::Ok);
            h.Forget(6000, 5000);
            CHECK(h.Find(flatPeer) && h.Find(hostPeer));
            h.Forget(11000, 5000);
            CHECK(h.Count() == 0);   // (both last heard at 5500)
            // a flat peer's first message is a ping: Heard alone makes it a destination, with no unit to relay from it
            h.Heard(3, 20000);
            RelayEntry e[kMaxPeers];
            CHECK(h.Build(3, true, e, kMaxPeers) == 0);
        }
        HostTable full;
        for (uint64_t peer = 1; peer <= kMaxPeers; ++peer) CHECK(full.Add(peer) != nullptr);
        CHECK(full.Take(999, 1, Sample(1, 0, 0), 0) == Accept::Full);
    }

    // the playback: between two poses at the right moment, the newest held, stale gone, the sender's clock wrap carried
    {
        Track tr;
        Pose s;
        CHECK(!tr.Sample(0, 100, &s));
        const uint32_t base = 0xFFFFFF00u;   // the sender's clock wraps in the middle
        // poses every 33 ms of the sender's, each arriving 20 ms (+ up to 15 ms jitter) later on our clock 5000 ahead
        for (int i = 0; i < 6; ++i) {
            const uint32_t sent = base + 33u * (uint32_t)i;
            const uint64_t recv = 5000 + 33ull * i + 20 + (i % 2 ? 15 : 0);
            tr.Add(recv, Sample((uint16_t)i, sent, 0.1f * i));
        }
        // our moment 5000 + 33*3 + 20 + 100 (the delay) = the sender's pose 3 exactly
        CHECK(tr.Sample(5000 + 99 + 20 + 100, 100, &s) && Near(s.right.p[0], 0.3f, 2e-3f));
        // half way between 3 and 4
        CHECK(tr.Sample(5000 + 99 + 16 + 20 + 100, 100, &s) && Near(s.right.p[0], 0.35f, 4e-3f));
        // after the newest: the newest, no more
        CHECK(tr.Sample(5000 + 400, 100, &s) && Near(s.right.p[0], 0.5f, 1e-4f));
        // long silent: none
        CHECK(!tr.Sample(5000 + 200 + kStaleMs + 50, 100, &s));
    }

    // a blend: halfway turns, a joint only in one pose
    {
        Pose a = Sample(1, 0, 0.0f), b = Sample(2, 33, 1.0f);
        AxisAngle(0, 1, 0, 0, a.right.q);
        AxisAngle(0, 1, 0, 90, b.right.q);
        a.heading = 3.1f;
        b.heading = -3.1f;   // across the wrap: the short way
        b.flags &= ~kLeft;
        const Pose m = Blend(a, b, 0.5f);
        float half[4];
        AxisAngle(0, 1, 0, 45, half);
        CHECK(QuatAngleDeg(m.right.q, half) < 0.05f);
        CHECK(Near(m.right.p[0], 0.5f, 1e-5f));
        CHECK(Near(std::fabs(m.heading), kPi, 0.01f));
        CHECK(!(m.flags & kLeft));       // from b (the nearer at 0.5), which lost it
        CHECK(Blend(a, b, 0.2f).flags & kLeft);
        // the body and a weapon blended where both have them; a weapon that changed hands is not blended across
        a.flags |= kBody | kWeapon0;
        b.flags |= kBody | kWeapon0;
        a.shoulder[0][1] = 1.4f; b.shoulder[0][1] = 1.6f;
        a.weapon[0].tip[0] = 0.0f; b.weapon[0].tip[0] = 1.0f;
        const Pose mb = Blend(a, b, 0.25f);
        CHECK((mb.flags & kBody) && Near(mb.shoulder[0][1], 1.45f, 1e-5f));
        CHECK((mb.flags & kWeapon0) && Near(mb.weapon[0].tip[0], 0.25f, 1e-5f));
        b.weapon[0].hand = 1;
        a.flags |= kModelYaw; b.flags |= kModelYaw;
        a.yawInModel = 3.0f; b.yawInModel = -3.0f;   // the short way, across the wrap
        CHECK(Near(std::fabs(Blend(a, b, 0.5f).yawInModel), kPi, 0.01f));
        const Pose mh = Blend(a, b, 0.25f);
        CHECK((mh.flags & kWeapon0) && mh.weapon[0].hand == 0 && Near(mh.weapon[0].tip[0], 0.0f, 1e-6f));   // a's, the nearer
    }

    // the frames: into the hero's frame and back; heading 0 looks along the world's -z; the head's pitch looks up
    {
        float r[3], ah[3];
        Axes(0.0f, r, ah);
        CHECK(Near(r[0], 1, 1e-6f) && Near(ah[2], -1, 1e-6f));
        const float fwd[3] = {-std::sin(0.7f), 0.0f, -std::cos(0.7f)};
        CHECK(Near(HeadingOf(fwd), 0.7f, 1e-5f));
        const float feet[3] = {100.0f, 5.0f, -40.0f};
        const float world[3] = {103.0f, 20.0f, -47.0f};
        float local[3], back[3];
        ToHero(world, feet, 0.7f, 9.0f, local);
        ToWorld(local, feet, 0.7f, 9.0f, back);
        CHECK(Near(back[0], world[0], 1e-3f) && Near(back[1], world[1], 1e-3f) && Near(back[2], world[2], 1e-3f));
        // a point straight ahead of the frame is at -z
        const float aheadPt[3] = {feet[0] + fwd[0] * 9.0f, feet[1], feet[2] + fwd[2] * 9.0f};
        ToHero(aheadPt, feet, 0.7f, 9.0f, local);
        CHECK(Near(local[0], 0, 1e-4f) && Near(local[2], -1.0f, 1e-4f));
        float q[4], v[3];
        HeadQuat(30.0f, 0.0f, q);
        const float f0[3] = {0, 0, -1};
        Rotate(q, f0, v);
        CHECK(Near(v[1], 0.5f, 1e-4f) && v[2] < 0.0f);   // 30 degrees up
        HeadQuat(0.0f, 20.0f, q);
        const float up0[3] = {0, 1, 0};
        Rotate(q, up0, v);
        CHECK(v[0] > 0.3f);                               // the right ear down: the top of the head goes right
        float other[3];
        const float own[3] = {10.0f, 2.0f, 30.0f};
        FeetOf(own, 400.0f, 500.0f, 404.0f, 497.0f, other);
        CHECK(Near(other[0], 14.0f, 1e-5f) && Near(other[1], 2.0f, 1e-5f) && Near(other[2], 27.0f, 1e-5f));
    }

    // the markers: three balls and two lines, relative to the eye, kinds as given; only what is tracked
    {
        Pose p = Sample(1, 0, 0.3f);
        AxisAngle(0, 1, 0, 0, p.right.q);
        p.heading = 0.0f;
        const float feet[3] = {10, 0, 10}, eye[3] = {10, 15, 20};
        float a[8][4], b[8][4];
        const MarkerKinds k{40, 41, 42};
        CHECK(Markers(p, feet, 10.0f, eye, k, a, b, 8) == 5);
        CHECK(Near(a[0][1], 16.5f - 15.0f, 1e-3f) && Near(a[0][3], kHeadBallM * 10.0f, 1e-5f) && b[0][3] == 40.0f);
        CHECK(Near(a[1][0], 3.0f, 1e-3f) && b[1][3] == 41.0f);               // the right hand 0.3 m right of the feet
        CHECK(Near(b[2][2] - a[2][2], -kPointM * 10.0f, 1e-3f) && b[2][3] == 42.0f);   // pointing ahead (-z)
        p.flags = kRight;
        CHECK(Markers(p, feet, 10.0f, eye, k, a, b, 8) == 2);
        CHECK(Markers(p, feet, 10.0f, eye, k, a, b, 1) == 1);
        CHECK(Markers(p, feet, NAN, eye, k, a, b, 8) == 0);
    }

    std::printf("pose_share_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
