// Offline checks of vr/remote_pose.h ([multiplayer] pose_share_hero): a received pose as a target for the skeleton hook, which rig is whose, the turn into the
// hero's model space, the hands' places, the head's look (its pitch held) and the head bone's turn. Run: remote_pose_test.exe (exit 0 = all passed).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "remote_pose.h"

static int g_failures = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

using namespace remotepose;
using poseshare::Pose;
using poseshare::kPi;

static bool Near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
static void RowTimes(const float v[3], const float R[9], float out[3]) {   // v R (row vector)
    for (int j = 0; j < 3; ++j) out[j] = v[0] * R[j] + v[1] * R[3 + j] + v[2] * R[6 + j];
}

int main() {
    // a pose as a target: the head's offsets taken off the hands; needs the head and the model's yaw
    {
        Pose p;
        p.flags = poseshare::kHead | poseshare::kRight | poseshare::kLeft | poseshare::kModelYaw | poseshare::kWeapon0;
        p.eyeM = 1.7f;
        p.yawInModel = 0.4f;
        p.head.p[1] = 1.7f;
        p.right.p[0] = 0.3f; p.right.p[1] = 1.3f; p.right.p[2] = -0.4f;
        p.left.p[0] = -0.2f; p.left.p[1] = 1.0f;
        p.right.q[2] = 0.6f; p.right.q[3] = 0.8f;
        Target t;
        CHECK(FromPose(p, 77, 10.0f, 20.0f, &t));
        CHECK(t.unit == 77 && t.x == 10.0f && t.z == 20.0f && Near(t.yawInModel, 0.4f, 1e-6f) && Near(t.eyeM, 1.7f, 1e-6f));
        CHECK(t.flags == (poseshare::kHead | poseshare::kRight | poseshare::kLeft));   // only the joints
        CHECK(Near(t.hand[0][0], 0.3f, 1e-6f) && Near(t.hand[0][1], -0.4f, 1e-6f) && Near(t.hand[0][2], -0.4f, 1e-6f));
        CHECK(Near(t.rot[0][2], 0.6f, 1e-6f) && Near(t.rot[0][3], 0.8f, 1e-6f));
        Pose old = p;
        old.flags &= ~poseshare::kModelYaw;   // a sender of format 2: no yaw to turn it by
        CHECK(!FromPose(old, 77, 0, 0, &t));
        Pose headless = p;
        headless.flags &= ~poseshare::kHead;
        CHECK(!FromPose(headless, 77, 0, 0, &t));
        Pose odd = p;
        odd.eyeM = 0.2f;
        CHECK(!FromPose(odd, 77, 0, 0, &t));
    }

    // a weapon out of the hand: the slot's bone from the head, its rows turned into the model, a mirrored z put back
    {
        Pose p;
        p.flags = poseshare::kHead | poseshare::kModelYaw;
        p.flags2 = poseshare::kAttach0 | poseshare::kMirror0 | poseshare::kHoldR | poseshare::kGrips;
        p.grip[0] = 0.7f;
        p.eyeM = 1.6f;
        p.head.p[1] = 1.6f;
        p.attach[0].p[0] = 0.0f; p.attach[0].p[1] = 0.1f; p.attach[0].p[2] = -1.0f;   // on the floor, a metre ahead
        Target t;
        CHECK(FromPose(p, 5, 0, 0, &t));
        CHECK(t.attach[0] && !t.attach[1] && t.attachMirror[0]);
        CHECK(t.holds[0] && !t.holds[1] && t.grips && Near(t.grip[0], 0.7f, 1e-6f));
        CHECK(Near(t.attachPos[0][1], -1.5f, 1e-5f) && Near(t.attachPos[0][2], -1.0f, 1e-5f));   // from the head
        const float head[3] = {0.0f, 8.0f, 0.0f};
        float o[3], rows[3][3];
        AttachInModel(t, 0, head, 1.0f, 0.0f, 5.0f, o, rows);
        CHECK(Near(o[1], 0.5f, 1e-4f) && Near(o[2], 5.0f, 1e-4f));   // 1.5 m down, 1 m ahead (+z) at 5 units a metre
        CHECK(Near(rows[0][0], -1.0f, 1e-5f) && Near(rows[1][1], 1.0f, 1e-5f));   // x -> the model's -x, y up
        CHECK(Near(rows[2][2], 1.0f, 1e-5f));   // z -> the model's -z, then mirrored back: +z
    }

    // [multiplayer] pose_share_legs: the hero's travel in his model - heading 0 looks along the world's -z, the model's ahead is +z, its right -x
    {
        float v[3];
        TravelInModel(0.0f, -20.0f, 0.0f, 1.0f, 0.0f, 5.0f, 10.0f, v);   // 2 m/s where he looks, at 5 model units a metre
        CHECK(Near(v[0], 0.0f, 1e-5f) && Near(v[1], 0.0f, 1e-6f) && Near(v[2], 10.0f, 1e-4f));
        TravelInModel(0.0f, 20.0f, 0.0f, 1.0f, 0.0f, 5.0f, 10.0f, v);    // backward
        CHECK(Near(v[2], -10.0f, 1e-4f));
        TravelInModel(20.0f, 0.0f, 0.0f, 1.0f, 0.0f, 5.0f, 10.0f, v);    // to his right
        CHECK(Near(v[0], -10.0f, 1e-4f) && Near(v[2], 0.0f, 1e-4f));
        // the world turned: heading a quarter (he looks along the world's -x); travel along -x is still ahead
        TravelInModel(-20.0f, 0.0f, kPi * 0.5f, 1.0f, 0.0f, 5.0f, 10.0f, v);
        CHECK(Near(v[2], 10.0f, 1e-3f) && Near(v[0], 0.0f, 1e-3f));
        // the model turned: his look is at yaw 0.6 in his model - travel where he looks goes there in the model
        TravelInModel(0.0f, -20.0f, 0.0f, std::cos(0.6f), std::sin(0.6f), 5.0f, 10.0f, v);
        CHECK(Near(std::atan2(v[0], v[2]), 0.6f, 1e-4f) && Near(std::sqrt(v[0] * v[0] + v[2] * v[2]), 10.0f, 1e-3f));
        TravelInModel(0.0f, 20.0f, 0.0f, 1.0f, 0.0f, 5.0f, 0.0f, v);     // no scale: none
        CHECK(v[0] == 0.0f && v[2] == 0.0f);
    }

    // the look in the rig's model from the world heading through its matrix: the model's forward +z where the matrix puts it, its left +x
    {
        // a rig turned so its forward (+z) points along the world's -x, scaled 2, moved
        const float th = 1.5707963f;   // a quarter turn about y
        const float cth = std::cos(th), sth = std::sin(th);
        float m[16] = {cth * 2.0f, 0, -sth * 2.0f, 0, 0, 2.0f, 0, 0, sth * 2.0f, 0, cth * 2.0f, 0, 50.0f, 3.0f, -20.0f, 1.0f};
        // the model's forward in the world: row 2 = (sin th, 0, cos th) * 2 = (2, 0, 0) -> +x; a look along the world's +x: heading with -sin h = 1 -> h = -pi/2
        float yaw = 9.0f;
        CHECK(ModelYawOfHeading(m, -kPi * 0.5f, &yaw) && Near(yaw, 0.0f, 1e-4f));    // looking where the model faces: yaw 0
        CHECK(ModelYawOfHeading(m, kPi * 0.5f, &yaw) && Near(std::fabs(yaw), kPi, 1e-4f));   // behind it
        // the model's left (+x) is row 0 = (cos th, 0, -sin th) * 2 = (0, 0, -2) -> world -z: a look along -z (heading 0) is yaw +90 (atan2(x, z) with x left)
        CHECK(ModelYawOfHeading(m, 0.0f, &yaw) && Near(yaw, kPi * 0.5f, 1e-4f));
        const float zero[16] = {};
        CHECK(!ModelYawOfHeading(zero, 0.0f, &yaw));
        CHECK(!ModelYawOfHeading(m, NAN, &yaw));
        // the same as the sender's own yaw when his model faces as ours does: its yaw in an unturned model at heading 0 (ahead -z) is pi (the model faces +z)
        const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        CHECK(ModelYawOfHeading(ident, 0.0f, &yaw) && Near(std::fabs(yaw), kPi, 1e-4f));
    }

    // a facing followed at a bounded rate (a hero with no pose turned round by the game): never more than the step, the short way, at once the first time
    {
        float x = 0.0f, z = 0.0f;
        TurnToward(&x, &z, 0.0f, 3.0f, 0.1f);
        CHECK(Near(x, 0.0f, 1e-6f) && Near(z, 1.0f, 1e-6f));   // first: as it is (made unit)
        TurnToward(&x, &z, 0.0f, -1.0f, 0.1f);                // turned round: 0.1 rad of it
        CHECK(Near(std::fabs(std::atan2(x, z)), 0.1f, 1e-4f));
        for (int i = 0; i < 40; ++i) TurnToward(&x, &z, 0.0f, -1.0f, 0.1f);
        CHECK(Near(z, -1.0f, 1e-4f));                         // there in the end
        TurnToward(&x, &z, 1.0f, 0.0f, 0.1f);                 // the short way: from -z to +x is a quarter, not three
        const float a = std::atan2(x, z);
        CHECK(Near(std::fabs(a), kPi - 0.1f, 1e-4f));
        float y = 0.0f;
        const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        CHECK(ModelYawOfDir(ident, 0.0f, 1.0f, &y) && Near(y, 0.0f, 1e-6f));
        CHECK(ModelYawOfDir(ident, 1.0f, 0.0f, &y) && Near(y, kPi * 0.5f, 1e-5f));
    }

    // [body] gait_cone_deg: the game's walk within the cone, the gait outside, a crossfade in time, hysteresis at the edge, standing the game's
    {
        CHECK(Near(TravelAngleDeg(0.0f, 1.0f, 0.0f, 1.0f), 0.0f, 1e-3f));
        CHECK(Near(TravelAngleDeg(1.0f, 0.0f, 0.0f, 2.0f), 90.0f, 1e-3f));
        CHECK(Near(TravelAngleDeg(0.0f, -3.0f, 0.0f, 1.0f), 180.0f, 1e-3f));
        CHECK(TravelAngleDeg(0.0f, 0.0f, 0.0f, 1.0f) < 0.0f);
        ConeBlend b;
        const float dt = 0.01f;
        for (int i = 0; i < 100; ++i) b.Step(30.0f, true, 45.0f, dt);
        CHECK(b.share == 0.0f && !b.out);                     // within: the game's
        float s1 = 0.0f;
        for (int i = 0; i < 5; ++i) s1 = b.Step(120.0f, true, 45.0f, dt);
        CHECK(b.out && Near(s1, 0.5f, 1e-4f));                 // out: half way after 0.05 s (10 a second)
        for (int i = 0; i < 10; ++i) b.Step(120.0f, true, 45.0f, dt);
        CHECK(b.share == 1.0f);                               // ours
        b.Step(43.0f, true, 45.0f, dt);                       // just inside the edge: still out (back in only under 41)
        CHECK(b.out);
        b.Step(40.0f, true, 45.0f, dt);
        CHECK(!b.out);
        CHECK(Near(b.share, 1.0f - 5.0f * dt, 1e-4f));      // back in at 5 a second
        ConeBlend e;                                          // coming in: out only past 49
        e.Step(48.0f, true, 45.0f, dt);
        CHECK(!e.out);
        e.Step(50.0f, true, 45.0f, dt);
        CHECK(e.out);
        for (int i = 0; i < 30; ++i) e.Step(170.0f, false, 45.0f, dt);   // a reversal's moment of no speed (0.3 s): still ours
        CHECK(e.out);
        e.Step(0.0f, true, 45.0f, dt);                        // ... and on the way back into the cone, the cone's
        CHECK(!e.out);
        e.Step(170.0f, true, 45.0f, dt);
        for (int i = 0; i < 40; ++i) e.Step(170.0f, false, 45.0f, dt);   // standing (past 0.35 s): the game's
        CHECK(!e.out);
        ConeBlend wide;
        for (int i = 0; i < 30; ++i) wide.Step(100.0f, true, 120.0f, dt);   // a cone of +-120: sideways is still the game's
        CHECK(wide.share == 0.0f);
    }

    // which rig is whose: the nearest within two units
    {
        Target t[3];
        t[0].x = 0.0f; t[0].z = 0.0f;
        t[1].x = 5.0f; t[1].z = 5.0f;
        t[2].x = 5.5f; t[2].z = 5.0f;
        CHECK(Match(0.5f, 0.5f, t, 3) == 0);
        CHECK(Match(5.4f, 5.0f, t, 3) == 2);
        CHECK(Match(10.0f, 10.0f, t, 3) == -1);
        CHECK(Match(0.0f, 2.1f, t, 3) == -1);
        CHECK(Match(0.0f, 0.0f, t, 0) == -1);
    }

    // into the model: its right is -x, ahead +z; the yaw turns "ahead of me" to where the camera looks in the model (atan2(x, z))
    {
        const float ahead[3] = {0.0f, 0.0f, -1.0f}, right[3] = {1.0f, 0.0f, 0.0f}, up[3] = {0.0f, 1.0f, 0.0f};
        float m[3];
        ToModel(ahead, 1.0f, 0.0f, m);
        CHECK(Near(m[0], 0.0f, 1e-6f) && Near(m[2], 1.0f, 1e-6f));
        ToModel(right, 1.0f, 0.0f, m);
        CHECK(Near(m[0], -1.0f, 1e-6f));
        ToModel(up, 1.0f, 0.0f, m);
        CHECK(Near(m[1], 1.0f, 1e-6f));
        const float yaw = 0.7f;
        ToModel(ahead, std::cos(yaw), std::sin(yaw), m);
        CHECK(Near(std::atan2(m[0], m[2]), yaw, 1e-5f));
    }

    // the hands: from the head bone, at the model's units per metre of the sender
    {
        CHECK(Near(ModelUpm(8.0f, 1.6f), 5.0f, 1e-6f));
        CHECK(ModelUpm(100.0f, 1.6f) == 8.0f && ModelUpm(1.0f, 1.6f) == 2.0f);
        CHECK(Near(ModelUpm(8.0f, 0.3f), 8.0f, 1e-6f));   // (the eye height not under 0.8 m)
        const float head[3] = {1.0f, 8.0f, 2.0f}, hand[3] = {0.0f, -0.3f, -0.5f};   // 0.3 m under, 0.5 m ahead
        float out[3];
        HandTarget(head, hand, 1.0f, 0.0f, 5.0f, out);
        CHECK(Near(out[0], 1.0f, 1e-5f) && Near(out[1], 6.5f, 1e-5f) && Near(out[2], 4.5f, 1e-5f));
        HandTarget(head, hand, 0.0f, 1.0f, 5.0f, out);   // turned a quarter: ahead is the model's +x
        CHECK(Near(out[0], 3.5f, 1e-5f) && Near(out[2], 2.0f, 1e-5f));
    }

    // the head's look: level ahead, nodding down, the pitch held at its limit, the top square to the look
    {
        float q[4], f[3], u[3];
        poseshare::HeadQuat(0.0f, 0.0f, q);
        CHECK(LookFrame(q, 1.0f, 0.0f, kMaxPitchDeg, f, u));
        CHECK(Near(f[2], 1.0f, 1e-5f) && Near(u[1], 1.0f, 1e-5f));
        poseshare::HeadQuat(-30.0f, 0.0f, q);   // 30 degrees down
        CHECK(LookFrame(q, 1.0f, 0.0f, kMaxPitchDeg, f, u));
        CHECK(Near(f[1], -0.5f, 1e-4f) && f[2] > 0.8f);
        poseshare::HeadQuat(85.0f, 0.0f, q);    // too far up: held at 70
        CHECK(LookFrame(q, 1.0f, 0.0f, kMaxPitchDeg, f, u));
        CHECK(Near(std::asin(f[1]) * 180.0f / kPi, kMaxPitchDeg, 0.05f) && f[2] > 0.0f);
        CHECK(Near(f[0] * u[0] + f[1] * u[1] + f[2] * u[2], 0.0f, 1e-4f) && u[1] > 0.0f);
        poseshare::HeadQuat(0.0f, 20.0f, q);    // the right ear down: the top leans to the model's right (-x)
        CHECK(LookFrame(q, 1.0f, 0.0f, kMaxPitchDeg, f, u));
        CHECK(u[0] < -0.3f && Near(f[2], 1.0f, 1e-4f));
    }

    // the head bone's turn: takes its forward and top onto the look's, a proper turn
    {
        const float f0[3] = {0.0f, 0.0f, 1.0f}, u0[3] = {0.0f, 1.0f, 0.0f};
        const float f1[3] = {std::sin(0.5f), 0.0f, std::cos(0.5f)}, u1[3] = {0.0f, 1.0f, 0.0f};
        float R[9], o[3];
        CHECK(FrameTurn(f0, u0, f1, u1, R));
        RowTimes(f0, R, o);
        CHECK(Near(o[0], f1[0], 1e-5f) && Near(o[2], f1[2], 1e-5f));
        RowTimes(u0, R, o);
        CHECK(Near(o[1], 1.0f, 1e-5f));
        const float det = R[0] * (R[4] * R[8] - R[5] * R[7]) - R[1] * (R[3] * R[8] - R[5] * R[6]) + R[2] * (R[3] * R[7] - R[4] * R[6]);
        CHECK(Near(det, 1.0f, 1e-4f));
        // a bone whose axes are not the model's (a rig's head bone): its own forward and top go where they should
        const float g0[3] = {0.6f, 0.0f, 0.8f}, v0[3] = {0.0f, 0.8f, 0.6f};   // (not square: made so)
        float u2[3] = {0.1f, 0.9f, -0.2f};
        CHECK(FrameTurn(g0, v0, f1, u2, R));
        RowTimes(g0, R, o);
        CHECK(Near(o[0], f1[0], 1e-4f) && Near(o[1], f1[1], 1e-4f) && Near(o[2], f1[2], 1e-4f));
        const float zero[3] = {0.0f, 0.0f, 0.0f};
        CHECK(!FrameTurn(zero, u0, f1, u1, R));
        CHECK(!FrameTurn(f0, f0, f1, u1, R));   // a top along the look: no frame
    }

    // [format 6] the sender's bones as drawn (two hands - the left one mirrored - an attach bone, the elbows), over the wire, on another rig: each lands where it
    // was from the sender's head bone, in his units, turned as it was - his eye is not that rig's head bone, nor are his units its head bone's height over his eye
    {
        using namespace poseshare;
        const float yS = 0.9f, cS = std::cos(yS), sS = std::sin(yS), U = 4.83f;
        const float eye[3] = {0.31f, 8.21f, 0.92f}, hb[3] = {0.02f, 7.64f, 0.18f};
        const float bone[3][3] = {{-1.52f, 5.31f, 2.44f}, {1.61f, 5.02f, 1.87f}, {-1.48f, 5.6f, 3.9f}};   // right hand, left hand, the right attach bone
        const float elbow[2][3] = {{-1.3f, 6.1f, 0.7f}, {1.4f, 6.0f, 0.4f}};
        const bool mirror[3] = {false, true, false};
        const float axes[3][4] = {{0.3f, 1.0f, -0.2f, 40.0f}, {-0.7f, 0.2f, 0.6f, 135.0f}, {1.0f, 0.1f, 0.0f, -80.0f}};
        float rowsM[3][3][3];
        for (int b = 0; b < 3; ++b) {
            const float n = std::sqrt(axes[b][0] * axes[b][0] + axes[b][1] * axes[b][1] + axes[b][2] * axes[b][2]), hh = axes[b][3] * kPi / 360.0f;
            const float q[4] = {axes[b][0] / n * std::sin(hh), axes[b][1] / n * std::sin(hh), axes[b][2] / n * std::sin(hh), std::cos(hh)};
            const float e[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
            for (int i = 0; i < 3; ++i) Rotate(q, e[i], rowsM[b][i]);
            if (mirror[b]) for (float& v : rowsM[b][2]) v = -v;
        }
        Pose p;
        p.flags = kHead | kRight | kLeft | kModelYaw;
        p.flags2 = kAttach0 | kHands;
        p.eyeM = 1.7f;
        p.yawInModel = yS;
        p.head.p[0] = 0.05f; p.head.p[1] = 1.7f; p.head.p[2] = -0.03f;
        p.modelUpm = U;
        auto send = [&](int b, Joint* j, bool* mir) {
            float o[3], r[3][3];
            PointToHead(bone[b], eye, cS, sS, U, o);
            for (int i = 0; i < 3; ++i) DirToHead(rowsM[b][i], cS, sS, r[i]);
            CHECK(QuatOfRows(r[0], r[1], r[2], j->q, mir));
            for (int i = 0; i < 3; ++i) j->p[i] = p.head.p[i] + o[i];
        };
        bool attachMirror = false;
        send(0, &p.hand[0], &p.handMirror[0]);
        send(1, &p.hand[1], &p.handMirror[1]);
        send(2, &p.attach[0], &attachMirror);
        CHECK(!p.handMirror[0] && p.handMirror[1] && !attachMirror);
        float o[3];
        for (int s = 0; s < 2; ++s) {
            PointToHead(elbow[s], eye, cS, sS, U, o);
            for (int i = 0; i < 3; ++i) p.handElbow[s][i] = p.head.p[i] + o[i];
        }
        PointToHead(hb, eye, cS, sS, U, o);
        for (int i = 0; i < 3; ++i) p.headBone[i] = p.head.p[i] + o[i];
        uint8_t msg[kMaxPayload];
        const uint32_t len = BuildPoseMsg(3, p, msg, sizeof msg);
        uint32_t unit = 0;
        Pose back;
        CHECK(len && ParsePoseMsg(msg, len, &unit, &back));
        Target t;
        CHECK(FromPose(back, 3, 0, 0, &t) && t.drawn && Near(t.modelUpm, U, 1e-3f) && t.handMirror[1] && !t.handMirror[0]);
        const float hr[3] = {0.4f, 7.1f, -0.6f};   // that rig's head bone, elsewhere (its own animation)
        for (int turned = 0; turned < 2; ++turned) {
            const float yR = yS + (turned ? 0.7f : 0.0f), cR = std::cos(yR), sR = std::sin(yR);
            float eR[3];
            EyeOnRig(hr, t, cR, sR, t.modelUpm, eR);
            float worstP = 0.0f, worstR = 0.0f, worstLen = 0.0f, worstY = 0.0f;
            bool handed = true;
            auto check = [&](int b, const float off[3], const float q[4], bool mir) {
                float ob[3], rows[3][3];
                BoneInModel(off, q, mir, eR, cR, sR, t.modelUpm, ob, rows);
                float d0 = 0.0f, d1 = 0.0f;
                for (int i = 0; i < 3; ++i) {
                    d0 += (bone[b][i] - hb[i]) * (bone[b][i] - hb[i]);
                    d1 += (ob[i] - hr[i]) * (ob[i] - hr[i]);
                    if (!turned) worstP = std::max(worstP, std::fabs(ob[i] - (hr[i] + bone[b][i] - hb[i])));
                    for (int j = 0; !turned && j < 3; ++j) worstR = std::max(worstR, std::fabs(rows[i][j] - rowsM[b][i][j]));
                    worstY = std::max(worstY, std::fabs(rows[i][1] - rowsM[b][i][1]));   // (a turn about the up keeps every row's height)
                }
                worstLen = std::max(worstLen, std::fabs(std::sqrt(d0) - std::sqrt(d1)));
                worstY = std::max(worstY, std::fabs((ob[1] - hr[1]) - (bone[b][1] - hb[1])));
                float cx[3];
                Cross3(rows[0], rows[1], cx);
                handed = handed && ((cx[0] * rows[2][0] + cx[1] * rows[2][1] + cx[2] * rows[2][2] < 0.0f) == mirror[b]);
            };
            check(0, t.handPos[0], t.handQ[0], t.handMirror[0]);
            check(1, t.handPos[1], t.handQ[1], t.handMirror[1]);
            check(2, t.attachPos[0], t.attachQ[0], t.attachMirror[0]);
            CHECK(worstP < 0.01f && worstR < 1e-3f);   // (the same yaw: exactly the sender's offsets from its head bone - mm on the wire, ~2 mm here)
            CHECK(worstLen < 0.01f && worstY < 0.01f && handed);
            for (int s = 0; s < 2; ++s) {
                float el[3];
                HandTarget(eR, t.handElbow[s], cR, sR, t.modelUpm, el);
                float d0 = 0.0f, d1 = 0.0f;
                for (int i = 0; i < 3; ++i) { d0 += (elbow[s][i] - hb[i]) * (elbow[s][i] - hb[i]); d1 += (el[i] - hr[i]) * (el[i] - hr[i]); }
                CHECK(std::fabs(std::sqrt(d0) - std::sqrt(d1)) < 0.01f);
                for (int i = 0; !turned && i < 3; ++i) CHECK(Near(el[i], hr[i] + elbow[s][i] - hb[i], 0.01f));
            }
        }
        // a sender's units a metre out of reason: no hands as drawn (the format 5 way stands)
        back.modelUpm = 0.2f;
        CHECK(FromPose(back, 3, 0, 0, &t) && !t.drawn);
        back.modelUpm = U;
        back.flags2 &= (uint8_t)~kHands;
        CHECK(FromPose(back, 3, 0, 0, &t) && !t.drawn);
    }

    // [format 7] what a hand has: a world matrix into the hero's frame and back; in a hand, against his drawn hand and onto a hand here - where the hand here is where
    // his was in the world the thing lands where his was; a hand here moved and turned carries it along, as it sits in the hand
    {
        using namespace poseshare;
        const float feet[3] = {120.5f, 3.2f, -48.0f}, heading = 2.3f, U = 4.62f;   // his feet in the world, his heading, the world's units a metre
        auto world = [&](const Joint& j, bool mirror, float scale, float m[16]) {   // a joint of the hero's frame as a world matrix (a mirrored set: its z row turned)
            HeldToWorld(j, feet, heading, U, scale, m);
            if (mirror) for (int k = 0; k < 3; ++k) m[8 + k] = -m[8 + k];
        };
        // a world matrix (a bottle at 0.56 of its size) into the hero's frame and back
        {
            Joint j;
            j.p[0] = 0.4f; j.p[1] = 1.1f; j.p[2] = -0.6f;
            const float ax[3] = {0.2f, 0.9f, -0.4f}, n = std::sqrt(0.04f + 0.81f + 0.16f), hh = 1.1f;
            for (int k = 0; k < 3; ++k) j.q[k] = ax[k] / n * std::sin(hh);
            j.q[3] = std::cos(hh);
            float m[16], back[16];
            HeldToWorld(j, feet, heading, U, 0.56f, m);
            Joint j2;
            float scale = 0.0f;
            CHECK(HeldFromWorld(m, feet, heading, U, &j2, &scale) && Near(scale, 0.56f, 1e-5f));
            HeldToWorld(j2, feet, heading, U, scale, back);
            float worst = 0.0f;
            for (int k = 0; k < 16; ++k) worst = std::max(worst, std::fabs(m[k] - back[k]));
            CHECK(worst < 1e-3f);
            float mm[16];
            std::memcpy(mm, m, sizeof mm);
            for (int k = 0; k < 3; ++k) mm[8 + k] = -mm[8 + k];
            CHECK(!HeldFromWorld(mm, feet, heading, U, &j2, &scale));   // a mirrored set is no turn
        }
        for (int side = 0; side < 2; ++side) {
            Pose p;
            p.flags = kHead | kRight | kLeft | kModelYaw;
            p.flags2 = kHands;
            p.flags3 = side ? kHeldL : kHeldR;
            p.eyeM = 1.75f;
            p.heading = heading;
            p.head.p[1] = 1.75f;
            p.modelUpm = 4.8f;
            p.worldUpm = U;
            p.handMirror[1] = true;   // (the left hand bone is mirrored)
            Joint& hj = p.hand[side];
            hj.p[0] = side ? -0.3f : 0.3f; hj.p[1] = 1.15f; hj.p[2] = -0.35f;
            const float hq[2][4] = {{0.2f, -0.3f, 0.1f, 0.927f}, {-0.5f, 0.1f, 0.4f, 0.76f}};
            for (int k = 0; k < 4; ++k) hj.q[k] = hq[side][k];
            Normalize(hj.q);
            Pose::Held& th = p.held[side];
            th.what = kHeldItem;
            th.state = kInHand;
            th.code = 0x20337068u;
            th.scale = 0.56f;
            th.pose.p[0] = hj.p[0] + 0.03f; th.pose.p[1] = hj.p[1] - 0.06f; th.pose.p[2] = hj.p[2] - 0.02f;
            const float tq[4] = {0.6f, 0.0f, 0.0f, 0.8f};
            for (int k = 0; k < 4; ++k) th.pose.q[k] = tq[k];
            Target t;
            CHECK(FromPose(p, 9, 0, 0, &t) && t.drawn && t.hasHeld[side] && !t.hasHeld[side ^ 1] && Near(t.heldWorldUpm, U, 1e-6f));
            float relP[3], relR[3][3];
            CHECK(HeldInHand(t, side, relP, relR));
            CHECK(!HeldInHand(t, side ^ 1, relP, relR));
            float hand[16], thing[16], out[16];
            world(hj, p.handMirror[side], 3.7f, hand);   // the hand here exactly where his was (its bone's own size: 3.7)
            HeldToWorld(th.pose, feet, heading, U, th.scale, thing);
            CHECK(OnHand(hand, relP, relR, U, th.scale, out));
            float worst = 0.0f;
            for (int k = 0; k < 16; ++k) worst = std::max(worst, std::fabs(out[k] - thing[k]));
            CHECK(worst < 1e-3f);
            // the hand here elsewhere and turned (a quarter about the up, then tipped): the thing as far from it and along its axes as before
            float moved[16];
            const float c = 0.0f, s = 1.0f;
            for (int r = 0; r < 3; ++r) {
                const float x = hand[r * 4], y = hand[r * 4 + 1], z = hand[r * 4 + 2];
                moved[r * 4] = x * c + z * s; moved[r * 4 + 1] = y; moved[r * 4 + 2] = -x * s + z * c; moved[r * 4 + 3] = 0.0f;
            }
            moved[12] = hand[12] + 7.0f; moved[13] = hand[13] - 1.0f; moved[14] = hand[14] + 2.5f; moved[15] = 1.0f;
            CHECK(OnHand(moved, relP, relR, U, th.scale, out));
            float worstAlong = 0.0f, worstRow = 0.0f;
            for (int i = 0; i < 3; ++i) {
                const float li = std::sqrt(hand[i * 4] * hand[i * 4] + hand[i * 4 + 1] * hand[i * 4 + 1] + hand[i * 4 + 2] * hand[i * 4 + 2]);
                float a0 = 0.0f, a1 = 0.0f;
                for (int k = 0; k < 3; ++k) {
                    a0 += (thing[12 + k] - hand[12 + k]) * hand[i * 4 + k] / li;
                    a1 += (out[12 + k] - moved[12 + k]) * moved[i * 4 + k] / li;
                }
                worstAlong = std::max(worstAlong, std::fabs(a0 - a1));
                for (int j = 0; j < 3; ++j) {
                    float r0 = 0.0f, r1 = 0.0f;
                    for (int k = 0; k < 3; ++k) { r0 += thing[j * 4 + k] * hand[i * 4 + k] / li; r1 += out[j * 4 + k] * moved[i * 4 + k] / li; }
                    worstRow = std::max(worstRow, std::fabs(r0 - r1));
                }
            }
            CHECK(worstAlong < 1e-3f && worstRow < 1e-3f);
            float cx[3];
            const float r0[3] = {out[0], out[1], out[2]}, r1[3] = {out[4], out[5], out[6]};
            Cross3(r0, r1, cx);
            CHECK(cx[0] * out[8] + cx[1] * out[9] + cx[2] * out[10] > 0.0f);   // a bottle in the mirrored left hand is still no mirror image
            // flying: not in a hand any more - no place against it
            t.held[side].state = kFlying;
            CHECK(!HeldInHand(t, side, relP, relR));
        }
    }

    // [multiplayer] pose_share_legs, walking BACKWARDS (the 2026-10-27 report: "his legs cross over each other"). His body faces the look (the world's -z) all along;
    // he walks forward (-z) half a second, then back (+z). The feet as the gait library puts them - a copy of its step (gait_physics 1.0: the speed smoothed, the
    // steps' way turned toward the travel at 6 rad/s, the stride's phase from the distance, each foot its rest place plus the way times its share of the stride,
    // the legs half a cycle apart) - must keep his left foot on his left of the right one through the reversal. As before (the way turned round at its rate) it
    // swept through sideways and the feet crossed; with the reversal turned at once (ReverseSteps) they stay where they stand and step back along the new way.
    {
        struct Ref { float phase = 0.0f, dirX = 0.0f, dirZ = 0.0f, speed = 0.0f, move = 0.0f; };
        auto footOf = [](float phi, float* along, float* lift) {   // the library's FootOfPhase
            phi = std::fmod(phi, 2.0f * kPi);
            if (phi < 0.0f) phi += 2.0f * kPi;
            if (phi < kPi) { *along = 0.5f - phi / kPi; *lift = 0.0f; }
            else { const float t = (phi - kPi) / kPi; *along = -0.5f + t * t * (3.0f - 2.0f * t); *lift = std::sin(kPi * t); }
        };
        auto refStep = [&](Ref& st, float vx, float vz, float dt, float feet[2][2]) {   // the library's Step, on the ground (metres), a man's leg
            const float raw = std::sqrt(vx * vx + vz * vz), a = dt > 0.0f ? 1.0f - std::exp(-dt / 0.1f) : 0.0f;
            st.speed += (raw - st.speed) * a;
            if (raw > 0.075f) {
                const float want = std::atan2(vx, vz);
                if (st.dirX == 0.0f && st.dirZ == 0.0f) { st.dirX = std::sin(want); st.dirZ = std::cos(want); }
                else {
                    const float now = std::atan2(st.dirX, st.dirZ);
                    float d = want - now;
                    while (d > kPi) d -= 2.0f * kPi;
                    while (d < -kPi) d += 2.0f * kPi;
                    const float step = std::clamp(d, -6.0f * dt, 6.0f * dt);
                    st.dirX = std::sin(now + step); st.dirZ = std::cos(now + step);
                }
            }
            const float x = std::clamp((st.speed - 0.15f) / 0.45f, 0.0f, 1.0f), want = x * x * (3.0f - 2.0f * x);
            st.move = std::clamp(st.move + std::clamp(want - st.move, -4.0f * dt, 4.0f * dt), 0.0f, 1.0f);
            const float stride = std::clamp(0.25f + 0.22f * st.speed, 0.25f, 0.75f);
            st.phase += kPi * st.speed / stride * dt;
            const float rest[2][2] = {{0.1f, 0.0f}, {-0.1f, 0.0f}};   // [0] right, [1] left, under the hips: facing -z, his right is the world's +x
            for (int i = 0; i < 2; ++i) {
                float along = 0.0f, lift = 0.0f;
                footOf(st.phase + (i ? kPi : 0.0f), &along, &lift);
                const float k = along * stride * st.move;
                feet[i][0] = rest[i][0] + st.dirX * k;
                feet[i][1] = rest[i][1] + st.dirZ * k;
            }
        };
        for (int fix = 0; fix < 2; ++fix) {
            Ref st;
            float was[2][2] = {};
            bool crossed = false, jumped = false, back = false;
            const float dt = 1.0f / 90.0f;
            float vx = 0.0f, vz = 0.0f;   // the travel as the mod smooths it (0.08 s), m/s
            for (int f = 0; f < 180; ++f) {
                const float wantZ = f < 45 ? -1.2f : 1.2f;   // forward, then back
                const float k = 1.0f - std::exp(-dt / 0.08f);
                vz += (wantZ - vz) * k;
                if (fix) {
                    const float speedNow = std::sqrt(vx * vx + vz * vz);
                    Ref flipped = st;
                    if (speedNow > 0.1f && ReverseSteps(&flipped.dirX, &flipped.dirZ, &flipped.phase, vx, vz)) {
                        float a[2][2], b[2][2];
                        Ref sa = st, sb = flipped;
                        refStep(sa, vx, vz, 0.0f, a);   // (the check the mod makes against the library: the feet at dt 0 the same)
                        refStep(sb, vx, vz, 0.0f, b);
                        float worst = 0.0f;
                        for (int i = 0; i < 2; ++i) worst = std::max(worst, std::hypot(a[i][0] - b[i][0], a[i][1] - b[i][1]));
                        CHECK(worst < 1e-4f);
                        st = flipped;
                    }
                }
                float feet[2][2];
                refStep(st, vx, vz, dt, feet);
                if (!(feet[1][0] < feet[0][0] - 0.02f)) crossed = true;   // his left foot no longer on his left of the right one
                if (f > 0)
                    for (int i = 0; i < 2; ++i) if (std::hypot(feet[i][0] - was[i][0], feet[i][1] - was[i][1]) > 0.08f) jumped = true;   // (more than a swing's frame)
                std::memcpy(was, feet, sizeof was);
                if (f > 120 && st.dirZ > 0.99f) back = true;   // stepping back along his travel by the end
            }
            if (fix) CHECK(!crossed && !jumped && back);
            else CHECK(crossed);   // (as before: crossed - what the report saw)
        }
        CHECK(!ReverseSteps(nullptr, nullptr, nullptr, 1.0f, 0.0f));
        float dx = 0.0f, dz = 1.0f, ph = 0.4f;
        CHECK(!ReverseSteps(&dx, &dz, &ph, 0.5f, 0.5f) && dz == 1.0f && ph == 0.4f);   // 45 degrees off: the library's own turn
        CHECK(ReverseSteps(&dx, &dz, &ph, -0.2f, -1.0f) && Near(dz, -1.0f, 1e-6f) && Near(ph, kPi - 0.4f, 1e-5f));
        // the way kept in the world between steps (the game turns his model round at once walking back): a ground direction of the model into the world and back
        float M[16] = {};
        const float c = std::cos(1.1f), s = std::sin(1.1f), u = 4.6f;
        M[0] = c * u; M[2] = -s * u; M[5] = u; M[8] = s * u; M[10] = c * u; M[15] = 1.0f;
        float wx = 0.0f, wz = 0.0f, mx = 0.0f, mz = 0.0f;
        CHECK(DirModelToWorld(M, 0.6f, 0.8f, &wx, &wz) && DirWorldToModel(M, wx, wz, &mx, &mz) && Near(mx, 0.6f, 1e-5f) && Near(mz, 0.8f, 1e-5f));
        const float zero[16] = {};
        CHECK(!DirModelToWorld(zero, 0.6f, 0.8f, &wx, &wz) && !DirWorldToModel(zero, 0.6f, 0.8f, &mx, &mz));
    }
    // ... and with no gait: the look split between his whole body and his trunk over the hips - the trunk never more than kMaxTwistDeg over them, so walking back
    // (the look half a turn from the game's facing) his left hip is still on his trunk's left
    {
        const float max = kMaxTwistDeg * kPi / 180.0f;
        bool allLeft = true, upperOnlyCrossed = false, sums = true;
        for (int deg = -180; deg <= 180; deg += 5) {
            const float look = (float)deg * kPi / 180.0f;
            float body = 0.0f, upper = 0.0f;
            TurnSplit(look, max, &body, &upper);
            sums = sums && std::fabs(upper) <= max + 1e-6f && std::fabs(poseshare::WrapPi(body + upper - look)) < 1e-5f;
            // the hips' left (+x of the model) turned with the body, against the trunk's left turned by the whole look: on the same side
            const float hipLeft[2] = {std::cos(body), -std::sin(body)}, trunkLeft[2] = {std::cos(look), -std::sin(look)};
            allLeft = allLeft && hipLeft[0] * trunkLeft[0] + hipLeft[1] * trunkLeft[1] > 0.4f;
            if (deg == 180) upperOnlyCrossed = trunkLeft[0] * 1.0f + trunkLeft[1] * 0.0f < 0.0f;   // (the trunk alone turned, the hips the game's: crossed)
        }
        CHECK(sums && allLeft && upperOnlyCrossed);
        float body = 1.0f, upper = 1.0f;
        TurnSplit(0.3f, max, &body, &upper);
        CHECK(Near(body, 0.0f, 1e-6f) && Near(upper, 0.3f, 1e-6f));   // (within the waist's turn: the trunk alone, the legs walk as the game has them)
    }

    std::printf("remote_pose_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
