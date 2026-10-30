// vr/view/camera.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M16); what it gives: vr/view/camera.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

std::atomic<float> g_gameDownDeg{60.0f};   // the game camera's own downward angle, degrees (the flat view's last build)
void FlatPortalFrame();   // [input] portal_look_horizon / portal_auto (below)
void VrFrame() { prevscan::FrameDone(); AfrNextFrame(); engitem::Frame(g_enabled.load() && FullBody()); FlatPortalFrame(); }

// The game's view from above, per eye: orthographic, so moving the camera
// aside would only slide the picture. Each eye's view is turned instead, half
// the angle either way about the screen's vertical through the hero: he stays
// put, nearer and farther things part.
bool TopStereoView(const d2rcam::WorldView& in, float out[16]) {
    if (!in.lookAtValid) return false;
    // the hero in view space
    const float* V = in.gameView;
    const float* L = in.lookAt;
    const float hx = L[0] * V[0] + L[1] * V[4] + L[2] * V[8] + V[12];
    const float hz = L[0] * V[2] + L[1] * V[6] + L[2] * V[10] + V[14];
    const float a = (g_eye.load() == 0 ? 0.5f : -0.5f) * g_set.topAngle.load() * 3.14159265f / 180.0f;
    const float c = cosf(a), sn = sinf(a);
    float turn[16] = {c, 0, -sn, 0,  0, 1, 0, 0,  sn, 0, c, 0,  0, 0, 0, 1};
    turn[12] = hx - (hx * c + hz * sn);
    turn[14] = hz - (hz * c - hx * sn);
    d2rcam::m4::Mul(V, turn, out);
    static bool told = false;
    if (!told) { told = true; LogF("vrcam: stereo from above - eyes turned %.1f deg apart about the hero", g_set.topAngle.load()); }
    return true;
}

// The table view (F5): the game laid on the room's floor. When the view is
// taken (F5, F11, the room's head first seen) the hero is put on the floor
// [table] ahead_m in front of the head, the game's own "up the screen" pointing
// away from the player; from then on the camera is where the head is in the
// room, turned as it is turned, at the size [table] hero_cm gives. The
// hero stays on his spot and the world slides under him, as in the game's own
// view; walking round him or crouching is the camera walking round or crouching.
// The anchor: the floor point under the hero (room x, z) and the room-to-game turn.
struct TableAnchor { float fx, fz, phi, floorY, headX, headZ, yaw, ahead; bool ok; };   // head and yaw: where it was put from
TableAnchor g_tableAnchor{};             // render thread only
std::atomic<bool> g_tableReanchor{true}; // set by F5 and F11, taken by the next view build

// The head in the room: stage position, metres, and its room yaw
// (degrees, + left), carried on to now with the angles' prediction.
bool RoomHead(float pos[3], float ang[3]) {
    const D2RVR_Shared* sh = Shared();
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || sh->roomMagic != D2RVR_ROOM_MAGIC) return false;
    for (int i = 0; i < 3; ++i) pos[i] = sh->headRoom[i];
    PredictHead(sh, ang);
    // PredictHead carries headYawDeg, the head as the output service sends it; the
    // room's yaw may sit apart from it by a recenter - the same turn, its own zero.
    ang[0] += WrapDeg(sh->headYawRoomDeg - sh->headYawDeg);
    return std::isfinite(pos[0] + pos[1] + pos[2] + ang[0] + ang[1] + ang[2]);
}

// [body] crouch: how far the head stands below its standing height, metres (+ down) - what the F4 camera has just been lowered
// by: native, the eyes' middle from the standing eyes' height (VrViewInner; xr.cpp PairEyes: the floor + the measured eye height
// with [calibration] use=1 and a STAGE floor, else the recentre's height); FlatVR with [flatvr] head_position, the room head
// since the recentre (FlatVrHeadMove). Where the camera does not follow the head's height nothing is written, and PushArms finds
// it stale: the body stands (vr/crouch.h).
std::atomic<float> g_headDownM{0.0f};
std::atomic<ULONGLONG> g_headDownAt{0};

// [flatvr] head_position, VR F4 on FlatVR: the head's move in the room since the recentre, in the game's world - the camera moves with
// the head as native OpenXR's does (its eyes' place, NativeEyes), on top of the neck model as there. Before: turns only, and leaning
// or crouching carried the whole world along with the head. `heading`: the camera's (Facing's) - the room is turned so that the head's
// room yaw lands on it (flatvrpose::RoomToWorld; the default [head] yaw_sign -1, yaw_scale 1). Held within 2 m.
V3 FlatVrHeadMove(float heading) {
    if (!g_set.flatPosition.load() || g_set.platform.load() != 1 || xr::On() || !FullBody()) return {0.0f, 0.0f, 0.0f};
    const D2RVR_Shared* sh = Shared();
    if (!sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || sh->roomMagic != D2RVR_ROOM_MAGIC) return {0.0f, 0.0f, 0.0f};
    const float pos[3] = {sh->headRoom[0], sh->headRoom[1], sh->headRoom[2]};
    if (!std::isfinite(pos[0] + pos[1] + pos[2]) || !std::isfinite(sh->headYawRoomDeg)) return {0.0f, 0.0f, 0.0f};
    if (!g_recenterPosOk.load()) {   // never centred yet: from where the head is first seen
        for (int i = 0; i < 3; ++i) g_recenterPos[i].store(pos[i]);
        g_recenterPosOk.store(true);
    }
    float p[3]; PredictHead(sh, p);
    const float roomYaw = (p[0] + WrapDeg(sh->headYawRoomDeg - sh->headYawDeg)) * flatvrpose::kDeg;
    const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units per metre, as native's
    const flatvrpose::V d{pos[0] - g_recenterPos[0].load(), pos[1] - g_recenterPos[1].load(), pos[2] - g_recenterPos[2].load()};
    g_headDownM.store(-d.y);   // [body] crouch
    g_headDownAt.store(GetTickCount64());
    const flatvrpose::V w = flatvrpose::RoomToWorld(d, heading, roomYaw, upm, 2.0f);
    return {w.x, w.y, w.z};
}

// VR on FlatVR, every 10 s in a world: how far the camera's pitch and roll stand apart from the head's own (FlatVR hangs the picture at
// the head's). Apart by a constant, the world leans on head turns - [flatvr] head_level makes it 0. Render thread.
void FlatVrPoseLine(float downRad, float rollRad, bool level) {
    if (g_set.platform.load() != 1 || xr::On() || !FullBody()) return;
    static ULONGLONG at = 0;
    const ULONGLONG now = GetTickCount64();
    if (at && now - at < 10000) return;
    const D2RVR_Shared* sh = Shared();
    if (!sh || !sh->headValid || !sh->pitchValid) return;
    at = now;
    const float camPitch = -downRad / flatvrpose::kDeg, camRoll = rollRad / flatvrpose::kDeg;
    const float headPitch = g_set.pitchSign.load() * sh->headPitchDeg;
    const float headRoll = sh->rollValid ? flatvrpose::TrueRollDeg(sh->headRollDeg, sh->headPitchDeg) : 0.0f;
    LogF("vrcam: flatvr - camera pitch %.1f deg, the head's %.1f: apart %.1f; roll %.1f, the head's %.1f (BodyWalk sent %.1f)%s",
         camPitch, headPitch, camPitch - headPitch, camRoll, g_set.rollSign.load() * headRoll, sh->rollValid ? sh->headRollDeg : 0.0f,
         level ? " - [flatvr] head_level" : " - apart = the world leans on head turns; [flatvr] head_level=1 takes the head's own");
}

// F1 in true perspective: the game camera's own direction, from far enough back
// along it that the ground round the hero fills the FlatVR screen as the game's
// own picture fills the monitor. The hero sits on the screen's plane (TopScale).
// Native OpenXR ([openxr] views_3d, xe: the eyes as a window, xr.h kViewsWindow): the screen is the window of the room - its
// size and distance - and the eye stands where the head is in front of it: the camera moves with the head the way the window
// shows (vr/views3d.h WindowWorldEye), turned as the game turns it. FlatVR's screen is not asked.
bool TopCamera(const d2rcam::WorldView& in, V3* eye, V3* fwd, V3* up, V3* right, V3* ahead, const xr::Eyes* xe = nullptr) {
    const float* G = in.gameView;
    const float* P = in.gameProj;
    const float* L = in.lookAt;
    float sw, sh, sd;
    if (xe) { sw = xe->winW; sh = xe->winH; sd = xe->winD; if (!(sw > 0.0f && sh > 0.0f && sd > 0.0f)) return false; }
    else if (!LiveScreen(&sw, &sh, &sd)) { sh = 2.0f * tanf(0.5f * CameraFov() * 3.14159265f / 180.0f); sd = 1.0f; }
    // The game's half height of view at the hero, world units: orthographic, or a perspective's at his distance.
    const float m5 = fabsf(P[5]);
    if (!(m5 > 1e-6f)) return false;
    const bool ortho = fabsf(P[11]) < 0.5f;
    const float hz = L[0] * G[2] + L[1] * G[6] + L[2] * G[10] + G[14];   // the hero in the game's view space (looks down -z)
    const float half = ortho ? 1.0f / m5 : std::max(-hz, 1.0f) / m5;
    const float dist = half * 2.0f * sd / sh;   // our camera that far back sees the same height on the screen
    *fwd = {-G[2], -G[6], -G[10]};
    *up = {G[1], G[5], G[9]};
    *right = {G[0], G[4], G[8]};
    // [top] tilt: turned about the hero, about the camera's right - + raises the view
    // toward the horizon (more of the world ahead, and the sky past it)
    if (const float tilt = g_set.topTilt.load(); tilt != 0.0f) {
        const float t = tilt * 3.14159265f / 180.0f, c = cosf(t), s = sinf(t);
        const V3 f = *fwd, u = *up;
        *fwd = f * c + u * s;
        *up = u * c - f * s;
    }
    const float heading = atan2f(-fwd->x, -fwd->z);
    *ahead = Facing(heading, 0.0f);
    *eye = V3{L[0], L[1], L[2]} - *fwd * dist;
    if (xe) {   // the head in front of the window (its frame: x along it, y up, z out towards the viewer), the hero on its middle
        const views3d::Vec mid{0.5f * (xe->pos[0][0] + xe->pos[1][0]), 0.5f * (xe->pos[0][1] + xe->pos[1][1]), 0.5f * (xe->pos[0][2] + xe->pos[1][2])};
        const views3d::Vec w = views3d::WindowWorldEye({L[0], L[1], L[2]}, {right->x, right->y, right->z}, {up->x, up->y, up->z},
                                                       {fwd->x, fwd->y, fwd->z}, dist / sd, mid);
        *eye = V3{w.x, w.y, w.z};
    }
    g_topUnitsPerM.store(dist / sd);
    static bool told = false;
    if (!told) {
        told = true;
        LogF("vrcam: from above in true perspective - %s game view, camera %.1f units back, %.2f units per metre%s", ortho ? "orthographic" : "perspective", dist,
             dist / sd, xe ? " - native OpenXR: a window of the room, the eyes look through it" : "");
    }
    return true;
}

// Native OpenXR ([openxr] views_3d, xe: the eyes as the room has them, xr.h kViewsRoom): the head is the runtime's - its
// place and its exact turn in the room the layer is submitted in, and the floor the runtime's own when it has a STAGE - so the
// diorama stands in the room and the picture is what the real eyes see of it (vr/views3d.h). No BodyWalk is asked.
bool TableCamera(const d2rcam::WorldView& in, V3* eye, V3* fwd, V3* up, V3* right, V3* ahead, float* yawOut, const xr::Eyes* xe = nullptr) {
    const float d2r = 3.14159265f / 180.0f;
    float p[3], ang[3];
    V3 headF{0, 0, -1}, headU{0, 1, 0}, headR{1, 0, 0};   // native: the head's axes in the room
    if (xe) {
        const float* q = xe->quat[0];   // the left eye's turn (the eyes are parallel on most headsets: logged at the start)
        headF = d2rcam::m4::Rotate(q, {0.0f, 0.0f, -1.0f});
        headU = d2rcam::m4::Rotate(q, {0.0f, 1.0f, 0.0f});
        headR = d2rcam::m4::Rotate(q, {1.0f, 0.0f, 0.0f});
        for (int i = 0; i < 3; ++i) p[i] = 0.5f * (xe->pos[0][i] + xe->pos[1][i]);
        ang[0] = atan2f(-headF.x, -headF.z) / d2r; ang[1] = ang[2] = 0.0f;
        if (!std::isfinite(ang[0] + p[0] + p[1] + p[2])) return false;
    } else if (!RoomHead(p, ang)) {
        static bool told = false;
        if (!told) { told = true; Log("vrcam: the game on the floor (F3) needs the head in the room - BodyWalk 1.72+ with the D2R bridge 0.15+"); }
        return false;
    }
    const float* G = in.gameView;
    const V3 gameFwd = {-G[2], -G[6], -G[10]};
    const float heading0 = atan2f(-gameFwd.x, -gameFwd.z);
    TableAnchor& A = g_tableAnchor;
    // BodyWalk's room can move under the head at once - seen 2026-10-05: the head at
    // 1.83 m, then 0.67 m, in one session, and the game a metre off the floor until
    // F11. No head moves half a metre between two views: such a jump is the room's.
    static float lastP[3] = {};
    static bool lastOk = false;
    const float jump = sqrtf((p[0] - lastP[0]) * (p[0] - lastP[0]) + (p[1] - lastP[1]) * (p[1] - lastP[1]) + (p[2] - lastP[2]) * (p[2] - lastP[2]));
    const bool roomMoved = lastOk && jump > 0.5f;
    memcpy(lastP, p, sizeof lastP);
    lastOk = true;
    if (roomMoved) LogF("vrcam: the table - BodyWalk's room moved %.2f m under the head: the game put down again", jump);
    // The settings program's button, as F11 (the first value seen is only remembered).
    static int placeSeen = INT_MIN;
    const int place = g_set.tablePlace.load();
    const bool pressed = placeSeen != INT_MIN && place != placeSeen;
    placeSeen = place;
    // Native: the runtime's space was recentred (its button, a jump it never told, F11) - the room under the diorama moved.
    static uint32_t epochSeen = 0;
    static bool epochKnown = false;
    bool epochMoved = false;
    if (xe) { epochMoved = epochKnown && xe->epoch != epochSeen; epochSeen = xe->epoch; epochKnown = true; }
    else epochKnown = false;
    if (epochMoved) Log("vrcam: the table - the headset's space was recentred: the game put down again in front of you");
    if (g_tableReanchor.exchange(false) || !A.ok || roomMoved || pressed || epochMoved) {
        const float t = ang[0] * d2r, ah = g_set.tableAheadM.load();
        A.fx = p[0] - sinf(t) * ah;   // room forward at yaw t is (-sin t, 0, -cos t)
        A.fz = p[2] - cosf(t) * ah;
        A.phi = heading0 - t;
        A.headX = p[0]; A.headZ = p[2]; A.yaw = t; A.ahead = ah;
        // The real floor: BodyWalk's room is not always the floor's (seen 2026-10-05: the
        // head at 0.72 m standing, the game a metre up). The player stands when the view
        // is taken, so the floor is their standing eye height below the head.
        A.floorY = xe && xe->floorOk ? xe->floorY : p[1] - UserEyeHeightM();
        A.ok = true;
        LogF("vrcam: the table - the hero on the floor %.2f m ahead (room %.2f, %.2f), the floor %.2f m below the head (room y %.2f%s), %.1f units per metre",
             ah, A.fx, A.fz, p[1] - A.floorY, A.floorY, xe ? (xe->floorOk ? ", the headset's own floor" : ", from your height") : "", g_set.tableScale.load());
    }
    // [table] ahead_m moved: the game slides along the line it was put on, at once.
    if (const float ah = g_set.tableAheadM.load(); fabsf(ah - A.ahead) > 1e-4f) {
        A.fx = A.headX - sinf(A.yaw) * ah;
        A.fz = A.headZ - cosf(A.yaw) * ah;
        A.ahead = ah;
    }
    // [table] turn: the world about the hero, the board where it is - so the room-to-game
    // turn grows by it, and the bounds undo it (TableTurn in the shader).
    const float phi = A.phi + g_set.tableTurnDeg.load() * d2r;
    float heading = phi + ang[0] * d2r;
    if (xe) {   // the exact turn of the head, turned about the vertical into the game's axes (the F4 branch's toWorld)
        auto toWorld = [&](V3 v) { const views3d::Vec w = views3d::RoomToWorldDir(phi, {v.x, v.y, v.z}); return V3{w.x, w.y, w.z}; };
        *fwd = toWorld(headF);
        *up = toWorld(headU);
        *right = toWorld(headR);
        heading = atan2f(-fwd->x, -fwd->z);
        *ahead = Facing(heading, 0.0f);
    } else {
        const float down = std::clamp(-g_set.pitchSign.load() * ang[1] * d2r, -1.55f, 1.55f);
        const float roll = std::clamp(g_set.rollSign.load() * ang[2], -80.0f, 80.0f) * d2r;
        *fwd = Facing(heading, down);
        *ahead = Facing(heading, 0.0f);
        *right = {cosf(heading), 0.0f, -sinf(heading)};
        const V3 up0 = d2rcam::m4::Cross(*right, *fwd);
        *up = up0 * cosf(roll) + *right * sinf(roll);
    }
    // The head from the anchor, turned into the game's axes (about +y by phi) and scaled (vr/views3d.h TableEye).
    const float* L = in.lookAt;
    const views3d::Table tbl{A.fx, A.fz, A.floorY, phi, g_set.tableScale.load(), g_set.tableHeightM.load(), 0.05f};
    const views3d::Vec e3 = views3d::TableEye(tbl, {L[0], L[1], L[2]}, {p[0], p[1], p[2]});
    *eye = V3{e3.x, e3.y, e3.z};
    g_lastTable = tbl;
    g_lastTableHero = {L[0], L[1], L[2]};
    *yawOut = (heading0 - heading) / d2r;   // as TargetYaw: + turned right of the game's camera
    {
        TableBox b{};
        d2rcam::m4::Mul(in.gameView, in.gameProj, b.vp);
        memcpy(b.hero, L, sizeof b.hero);
        b.ok = true;
        AcquireSRWLockExclusive(&g_skyLock); g_tableBox = b; ReleaseSRWLockExclusive(&g_skyLock);
    }
    return true;
}

// Our view: the game camera's heading and tilt, turned by TargetYaw / Pitch /
// Roll (head, mouse, right stick), from the hero's eyes (ViewHeight) or behind
// them ([camera] distance, third person).
// Where the game keeps the views of frames past ([debug] find_prev_matrix, prevscan): vr/render/stereo/prev_scan.cpp

// Native OpenXR: the eyes this pass is drawn with - the open pair's (PairEyes), when they are
// eyes at all (finite, their turns of unit length). A pair that did not open, or eyes that came
// back broken, keep the last good ones up to 2 s while the session runs in the world: falling
// back to BodyWalk's head for those frames meant its other zero and the camera swung between
// the two (2026-10-10). False: no native eyes - the camera is built as on FlatVR.
SRWLOCK g_goodEyesLock = SRWLOCK_INIT;
xr::Eyes g_goodEyes{};
ULONGLONG g_goodEyesAt = 0;
bool EyesSane(const xr::Eyes& e) {
    for (int i = 0; i < 2; ++i) {
        const float* q = e.quat[i];
        const float n2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
        if (!std::isfinite(n2) || fabsf(n2 - 1.0f) > 0.05f) return false;
        for (int k = 0; k < 3; ++k) if (!std::isfinite(e.pos[i][k]) || fabsf(e.pos[i][k]) > 20.0f) return false;
        if (!std::isfinite(e.tanL[i]) || !std::isfinite(e.tanR[i]) || !std::isfinite(e.tanU[i]) || !std::isfinite(e.tanD[i]) ||
            !(e.tanR[i] - e.tanL[i] > 0.01f) || !(e.tanU[i] - e.tanD[i] > 0.01f))
            return false;
    }
    return true;
}
// Why the last NativeEyes said no (the F4 flat-camera count, VrProjInner): 0 = it said yes
enum class NoEyes { None, NotNative, NoPair, OtherMode, NotSane, Stale };
std::atomic<int> g_noEyesWhy{0};
bool NativeEyes(xr::Eyes* out) {
    if (!NativeView()) { g_noEyesWhy.store((int)NoEyes::NotNative); return false; }
    const int want = NativeMode();   // F4 / F2: the head's; F1: a window's; F3: the room's (views_3d)
    xr::Eyes e{};
    const bool pair = xr::PairEyes(&e);
    const bool got = pair && e.mode == want && EyesSane(e);
    g_noEyesWhy.store((int)(got ? NoEyes::None : !pair ? NoEyes::NoPair : e.mode != want ? NoEyes::OtherMode : NoEyes::NotSane));
    const ULONGLONG now = GetTickCount64();
    bool ok = got;
    AcquireSRWLockExclusive(&g_goodEyesLock);
    if (got) {
        g_goodEyes = e;
        g_goodEyesAt = now;
        *out = e;
    } else if (g_goodEyesAt && g_goodEyes.mode == want && now - g_goodEyesAt < 2000 && xr::Running() && (!gamestate::MenuOpen() || MenuAs3d())) {
        *out = g_goodEyes;
        ok = true;
        g_noEyesWhy.store((int)NoEyes::None);
        static ULONGLONG toldAt = 0;
        if (!toldAt || now - toldAt > 10000) {
            toldAt = now;
            LogF("vrcam: openxr - no eyes from the runtime for this pass: the last good ones kept (%llu ms old) - the camera stays put",
                 (unsigned long long)(now - g_goodEyesAt));
        }
    }
    ReleaseSRWLockExclusive(&g_goodEyesLock);
    if (!ok && g_noEyesWhy.load() == (int)NoEyes::NoPair) g_noEyesWhy.store((int)NoEyes::Stale);   // (no pair open, and the last good eyes not taken)
    return ok;
}

// VR F4 with the native session running: how many of the game's passes took the headset's eyes, how many the flat camera ([camera]
// fov over the pass's aspect, [stereo] ipd) - those reach the headset drawn wrong, as if through the headset's own frustum. A Quest 2
// player's 0.175 log (2026-10-21, "F1 perfect, F4 strong cross-eyed", eye distance and swap eyes changed nothing) had the replay's
// right eye shifted by his [stereo] ipd and an x scale of fov 64 over his old ultrawide window, not the headset's. Every 10 s, only
// when a pass went flat.
void TellF4Camera(bool native, float aspect) {
    if (!FullBody() || ThirdPerson() || !xr::Running()) return;
    static uint32_t nativeN = 0, flatN = 0, why[6] = {};
    static ULONGLONG since = 0;
    static float flatAspect = 0.0f;
    if (native) ++nativeN;
    else { ++flatN; ++why[std::clamp(g_noEyesWhy.load(), 0, 5)]; flatAspect = aspect; }
    const ULONGLONG now = GetTickCount64();
    if (!since) since = now;
    if (now - since < 10000) return;
    if (flatN)
        LogF("vrcam: openxr - F4 passes, 10 s: %u with the headset's eyes, %u with the FLAT camera ([camera] fov over the aspect %.2f, [stereo] ipd: "
             "wrong in the headset) - no eyes because: not native %u, another view's eyes %u, eyes not sane %u, no pair open and the last good eyes "
             "over 2 s old or a menu open %u", nativeN, flatN, flatAspect, why[1], why[3], why[4], why[5]);
    nativeN = flatN = 0;
    for (uint32_t& w : why) w = 0;
    since = now;
}

bool VrViewInner(const d2rcam::WorldView& in, float out[16]);
extern std::atomic<bool> g_inWorld;
bool BodyWalkReport(BWPadMirror* out, bool evenIfOff);
bool BodyWalkRunning();
bool ChatOpen();
bool DirectWalkNow();
bool StickHookIn();
// [openxr] roomscale_walk (vr/room_walk_wire.cpp): the native head view's eyes (xr::Eyes' frame) moved by the room walk's point - the
// hero walks under the head through the game's own walking stick, the view stays with the head. Nothing changes while it is off.
void RoomWalkView(const xr::Eyes& xe, const float* L, float cb, float sb, float upm, float* eyesX, float* eyesZ) {
    if (!roomwalkwire::On()) return;
    roomwalkwire::Facts f;
    f.firstPerson = !ThirdPerson();
    f.inWorld = g_inWorld.load();
    f.menuOpen = gamestate::MenuOpen();
    f.chatOpen = ChatOpen();
    f.hookWalk = DirectWalkNow() && StickHookIn();   // the game's walk reads HookStickGet's vector: a slow tilt under the pad's dead zone reaches it
    directinput::PadOut d{};
    BWPadMirror bw{};
    if (directinput::PadRead(&d)) {
        // (never the room walk's own stick read back as the controller's)
        f.stickHeld = walkpath::PushedRaw(d.lx, d.ly) && !roomwalkwire::IsRoomStick(d.lx, d.ly);
        f.ownLX = d.lx; f.ownLY = d.ly;
        // BodyWalk's body walking mixed in ([openxr] direct_bodywalk_walk) walks the hero too: a stick held as well
        if (!f.stickHeld && directinput::BodyWalkWalks() && BodyWalkReport(&bw, true))
            f.stickHeld = walkpath::PushedRaw(bw.thumb_lx, bw.thumb_ly) && !roomwalkwire::IsRoomStick(bw.thumb_lx, bw.thumb_ly);
        f.pad = "the plugin's own (direct input)";
        if (f.stickHeld) f.ownTilt = std::min(1.0f, sqrtf((float)d.lx * d.lx + (float)d.ly * d.ly) / 32767.0f);
    }
    else if (BodyWalkReport(&bw, false)) {
        f.stickHeld = walkpath::PushedRaw(bw.thumb_lx, bw.thumb_ly) && !roomwalkwire::IsRoomStick(bw.thumb_lx, bw.thumb_ly);
        f.ownLX = bw.thumb_lx; f.ownLY = bw.thumb_ly;
        f.pad = "BodyWalk's";
        if (f.stickHeld) f.ownTilt = std::min(1.0f, sqrtf((float)bw.thumb_lx * bw.thumb_lx + (float)bw.thumb_ly * bw.thumb_ly) / 32767.0f);
    }
    else f.pad = BodyWalkRunning() ? "none - BodyWalk runs but writes no pad ([input] bodywalk_pad=0?): the walk cannot reach the game"
                                   : "none - direct input is not feeding and no BodyWalk pad: the walk cannot reach the game";
    roomwalkwire::View(xe, L, cb, sb, upm, f, eyesX, eyesZ);
}
// ... and its stick into the pad the game's walk reads, only where the controller's own left stick rests - or, while the room walk walks the
// hero, until a push of the controller's means it (vr/room_walk.h Merge, roomFirst) - and never with a panel open or no game.
void RoomStick(int16_t* lx, int16_t* ly) {
    int16_t rx = 0, ry = 0;
    bool first = false;
    if (!roomwalkwire::Stick(&rx, &ry, &first) || !g_inWorld.load() || gamestate::MenuOpen()) return;
    roomwalk::Merge(*lx, *ly, rx, ry, lx, ly, first);
}
bool VrView(const d2rcam::WorldView& in, float out[16]) {
    prevscan::Watch();
    const bool ours = VrViewInner(in, out);
    if (ours) {
        prevscan::Record(out);
        memcpy(prevscan::g_lastView, out, sizeof prevscan::g_lastView);
        prevscan::g_lastViewOk = true;
        prevscan::g_lastEye = g_eye.load() & 1;
        prevscan::FixPrev();
    }
    return ours;
}
bool VrViewInner(const d2rcam::WorldView& in, float out[16]) {
    AcquireSRWLockExclusive(&g_lookLock);
    memcpy(g_lookAt, in.lookAt, sizeof g_lookAt);
    g_lookOk = in.lookAtValid;
    ReleaseSRWLockExclusive(&g_lookLock);
    // The menus (the character screen's 3D scene too: it has a hero point) keep the game's camera.
    if (!g_inWorld.load()) return false;
    if (TopStereoOn()) return TopStereoView(in, out);
    if (!g_enabled.load() || !in.lookAtValid) return false;
    const float* G = in.gameView;   // row-major, v * M: column i is the game camera's axis i in the world
    const float* L = in.lookAt;
    V3 fwd, ahead, right, up, eye, hang;
    V3 mapAhead{};   // [hud] map_rotate / map_compass: as `ahead` in native F4, the head's turn held while it looks down at the hand (mapturn::HeadHold)
    bool mapAheadOk = false;
    float yaw = 0.0f;
    // Native OpenXR (vr/xr.cpp): the runtime's eyes for the moment this frame is shown.
    xr::Eyes xe{};
    const bool native = NativeEyes(&xe);
    float nativeIpd = 0.0f;   // world units
    float turnBody = 0.0f;    // native first person: the room's -z in the world (xr::TurnView, eyes by turns)
    bool turnFirst = false;   // ... this view is a native first-person pair's
    int ptrMode = -1;                 // F1 / F3 in 3D: the laser's map (PointerMap)
    pointer3d::Similarity ptrSim;
    if (native && xe.mode == xr::kViewsRoom) {
        // F3 in the room ([openxr] views_3d): the runtime's eyes where they are in the room, the diorama's map (TableCamera);
        // the eyes' gap at the diorama's scale, so the parallax is right for a world this small on the floor
        if (!TableCamera(in, &eye, &fwd, &up, &right, &ahead, &yaw, &xe)) return false;
        hang = eye;
        g_floorEyeH.store(eye.y - L[1]); g_floorFwdY.store(fwd.y); g_floorUpY.store(up.y); g_floorCamOk.store(true);
        const V3 apart = {xe.pos[1][0] - xe.pos[0][0], xe.pos[1][1] - xe.pos[0][1], xe.pos[1][2] - xe.pos[0][2]};
        nativeIpd = d2rcam::m4::Length(apart) * g_set.tableScale.load();
        ptrMode = xr::kViewsRoom;
        ptrSim = pointer3d::ForTable(g_lastTable, g_lastTableHero);
    } else if (native && xe.mode == xr::kViewsWindow) {
        // F1 in 3D ([openxr] views_3d): the game's view from above (TopCamera) through a window of the room, the head in front of it
        if (!TopCamera(in, &eye, &fwd, &up, &right, &ahead, &xe)) return false;
        hang = eye;
        nativeIpd = (xe.pos[1][0] - xe.pos[0][0]) * g_topUnitsPerM.load();
        ptrMode = xr::kViewsWindow;
        ptrSim = pointer3d::ForWindow({L[0], L[1], L[2]}, {right.x, right.y, right.z}, {up.x, up.y, up.z}, {fwd.x, fwd.y, fwd.z}, g_topUnitsPerM.load());
    } else if (native) {
        // The head as the runtime has it - its turn, tilt and place in the room, metres from
        // where it was at the recentre - on top of the body's turn (stick, mouse); no neck
        // model, no head prediction: the runtime predicts for the display time.
        const V3 gameFwd = {-G[2], -G[6], -G[10]};
        const float heading0 = atan2f(-gameFwd.x, -gameFwd.z);
        const float d2r = 3.14159265f / 180.0f;
        const float body = WrapDeg(g_pairNow.load() ? g_heldYaw.load() : g_mouseYaw.load() + g_turnYaw.load());
        const float base = heading0 - body * d2r;   // the room's -z in the world
        turnBody = base;
        turnFirst = !ThirdPerson();
        const float cb = cosf(base), sb = sinf(base);
        auto toWorld = [&](V3 v) { return V3{v.x * cb + v.z * sb, v.y, -v.x * sb + v.z * cb}; };
        const float* q = xe.quat[0];   // the left eye's turn (the eyes are parallel on most headsets: logged at the start)
        const V3 hf = d2rcam::m4::Rotate(q, {0.0f, 0.0f, -1.0f});
        fwd = toWorld(hf);
        up = toWorld(d2rcam::m4::Rotate(q, {0.0f, 1.0f, 0.0f}));
        right = toWorld(d2rcam::m4::Rotate(q, {1.0f, 0.0f, 0.0f}));
        yaw = WrapDeg(body - atan2f(-hf.x, -hf.z) / d2r);   // + turns right, as TargetYaw
        const float heading = heading0 - yaw * d2r;
        ahead = Facing(heading, 0.0f);
        if (g_set.mapTurn.load()) {   // (heading = base + the head's turn in the room: that turn held while the head looks straight down)
            static mapturn::HeadHold hold;
            if (hold.Update(hf.x, hf.z)) { mapAhead = Facing(base + hold.roomYaw, 0.0f); mapAheadOk = true; }
        }
        const V3 side = {cosf(heading), 0.0f, -sinf(heading)};
        const V3 pivot = V3{L[0], L[1] + ViewHeight() + JumpLift(), L[2]} + side * g_set.camSide.load() + ahead * g_set.camForward.load();
        const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units per metre, as TrueScale
        V3 mid = {0.5f * (xe.pos[0][0] + xe.pos[1][0]), 0.5f * (xe.pos[0][1] + xe.pos[1][1]), 0.5f * (xe.pos[0][2] + xe.pos[1][2])};
        if (!ThirdPerson()) { g_headDownM.store(-mid.y); g_headDownAt.store(GetTickCount64()); }   // [body] crouch: the camera goes down by this
        {   // what the eyes' height is counted from (xr::Eyes standMeasured), on change
            static int told = -1;
            const int how = xe.standSeated ? 2 : (int)xe.standMeasured;
            if (!ThirdPerson() && how != told) {
                told = how;
                if (xe.standSeated)
                    Log("vrcam: openxr - standing height: played seated ([calibration] seated=1) - the head's height at the recentre is the "
                        "hero's eyes: sit as you play and recentre (F11)");
                else if (xe.standMeasured)
                    LogF("vrcam: openxr - standing height: the floor + your measured eye height %.2f m - standing upright, the camera is the hero's eyes "
                         "whatever height a recentre was taken at", calib::MeasuredEyeM());
                else
                    Log("vrcam: openxr - standing height: the head's height at the recentre (no T-pose measurement in use, or no STAGE floor) - "
                        "recentre (F11) standing upright, or measure your height in D2R VR Settings");
            }
        }
        RoomWalkView(xe, L, cb, sb, upm, &mid.x, &mid.z);   // [openxr] roomscale_walk: the hero walks under the head, the view stays with it
        V3 head = pivot + toWorld(mid) * upm;
        if (!ThirdPerson() && g_set.neckModel.load()) {
            // the head turns about the top of the neck, as with FlatVR (VrViewInner's neck model): looking
            // down the eyes go forward and down over the chest. On top of the headset's own place since
            // 2026-10-10 - only with lean_m 0 before, so with the default 0.1 "Head turns about the neck"
            // did nothing in native and looking down saw the chest's armour from behind it (PS VR2).
            const float nu = g_set.neckUpCm.load() * 0.01f * upm, nf = g_set.neckFwdCm.load() * 0.01f * upm;
            head = head - V3{0.0f, nu, 0.0f} + up * nu + fwd * nf;
        }
        if (ThirdPerson()) {
            // behind the hero: on the body's turn, the head free to look round (or with the gaze)
            eye = head - (g_xrThirdGaze.load() ? fwd : Facing(base, 0.0f)) * ViewDistance();
            hang = pivot;
        } else {
            eye = head - fwd * ViewDistance();
            hang = head + (eye - head) * g_set.handsFollowCam.load();
        }
        const V3 apart = {xe.pos[1][0] - xe.pos[0][0], xe.pos[1][1] - xe.pos[0][1], xe.pos[1][2] - xe.pos[0][2]};
        nativeIpd = d2rcam::m4::Length(apart) * upm;
    } else if (TableView()) {
        if (!TableCamera(in, &eye, &fwd, &up, &right, &ahead, &yaw)) return false;
        hang = eye;
        g_floorEyeH.store(eye.y - L[1]); g_floorFwdY.store(fwd.y); g_floorUpY.store(up.y); g_floorCamOk.store(true);
    } else if (TopPersp()) {
        if (!TopCamera(in, &eye, &fwd, &up, &right, &ahead)) return false;
        hang = eye;
    } else {
    const V3 gameFwd = {-G[2], -G[6], -G[10]};
    const float heading0 = atan2f(-gameFwd.x, -gameFwd.z);
    const float down0 = asinf(std::clamp(-gameFwd.y, -1.0f, 1.0f));
    g_gameDownDeg.store(down0 * 57.2957795f);   // [input] portal_look_horizon: the horizon is this much up from the game camera's own look
    const float d2r = 3.14159265f / 180.0f;
    yaw = TargetYaw();                                   // + turns right
    const float heading = heading0 - yaw * d2r;
    // [flatvr] head_level (VR F4 on FlatVR): the head's own pitch and roll, level = level (FlatVrLevel)
    const bool level = FlatVrLevel();
    const float down = level ? LevelDownDeg() * d2r : std::clamp(down0 + TargetPitch() * d2r, -1.55f, 1.55f);   // + looks further down
    fwd = Facing(heading, down);
    ahead = Facing(heading, 0.0f);
    right = {cosf(heading), 0.0f, -sinf(heading)};
    const V3 up0 = d2rcam::m4::Cross(right, fwd);
    const float roll = (level ? LevelRollDeg() : TargetRoll()) * d2r;   // + = the right ear down
    up = up0 * cosf(roll) + right * sinf(roll);
    FlatVrPoseLine(down, roll, level);

    // The eye point: the hero's eyes, moved along where we look, flattened on the ground.
    V3 pivot = V3{L[0], L[1] + ViewHeight() + JumpLift(), L[2]} + right * g_set.camSide.load() + ahead * g_set.camForward.load();
    // [flatvr] head_position (VR F4 on FlatVR): and where the head is in the room since the recentre, as native moves its camera
    pivot = pivot + FlatVrHeadMove(heading);
    // Neck model: the head turns about the top of the neck - over the body's
    // middle, below and behind the eyes - not about the eyes. Level, the eyes
    // sit in front of the neck; looking down they go forward and down with the
    // head, and see the chest instead of the neck below them. The headset sends
    // only turns, so this is where the eyes' own travel comes from.
    V3 eyes = pivot;
    if (!ThirdPerson() && g_set.neckModel.load()) {
        const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();   // world units per metre at this hero's size
        const float nu = g_set.neckUpCm.load() * 0.01f * upm, nf = g_set.neckFwdCm.load() * 0.01f * upm;
        const V3 neck = pivot - V3{0.0f, nu, 0.0f};
        eyes = neck + up * nu + fwd * nf;
    }
    eye = eyes - fwd * ViewDistance();

    // The hands and the aim ray hang off the camera itself, as the real ones
    // hang off the headset: [camera] distance moves the camera off the pivot
    // along the view, and hands hung off the pivot slid against the real ones.
    // But moved ahead, the camera takes the hands out of the arms' reach:
    // [arms] follow_camera takes that share of the move only (0 = the eyes).
    // Third person keeps the pivot (the camera is far behind the hero there).
    // The head centre, before the AFR half-eye shift.
    hang = ThirdPerson() ? pivot : pivot + (eye - pivot) * g_set.handsFollowCam.load();
    }
    {   // a view that is no view (an angle come through not finite): the last good one, never a
        // frame of stretched triangles (2026-10-10). The render thread only.
        struct Good { V3 fwd, ahead, right, up, eye, hang; float yaw, ipd; bool ok; };
        static Good good{};
        auto fin = [](const V3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        if (fin(fwd) && fin(ahead) && fin(right) && fin(up) && fin(eye) && fin(hang) && std::isfinite(yaw) && std::isfinite(nativeIpd)) {
            good = {fwd, ahead, right, up, eye, hang, yaw, nativeIpd, true};
        } else {
            static ULONGLONG toldAt = 0;
            if (!toldAt || GetTickCount64() - toldAt > 10000) { toldAt = GetTickCount64(); Log("vrcam: a view came out not finite - the last good one kept"); }
            if (!good.ok) return false;
            fwd = good.fwd; ahead = good.ahead; right = good.right; up = good.up; eye = good.eye; hang = good.hang; yaw = good.yaw;
            nativeIpd = good.ipd;
        }
    }
    g_viewBuilds.fetch_add(1);
    g_viewFwdY.store(fwd.y);
    g_viewRightY.store(right.y);
    g_viewUpY.store(up.y);
    g_gazeFwd[0].store(fwd.x); g_gazeFwd[1].store(fwd.y); g_gazeFwd[2].store(fwd.z);
    g_gazeEyeH.store(eye.y - L[1]);
    for (int i = 0; i < 3; ++i) g_gazeLook[i].store(L[i]);
    g_gazeAt.store(GetTickCount64());
    float view[16];
    d2rcam::m4::LookTo(eye, fwd, up, view);
    const float hangA[3] = {hang.x, hang.y, hang.z};
    for (int i = 0; i < 3; ++i) g_eyeWorld[i].store(hangA[i]);
    g_headCentre[0].store(eye.x); g_headCentre[1].store(eye.y); g_headCentre[2].store(eye.z);
    g_headAhead[0].store(ahead.x); g_headAhead[1].store(ahead.y); g_headAhead[2].store(ahead.z);
    g_headOk.store(true);
    for (int i = 0; i < 3; ++i) g_eyeFromLook[i].store(hangA[i] - L[i]);
    g_eyeOk.store(true);

    V3 camPos = eye;   // this eye's camera, for the table's bounds
    if (AfrOn()) {
        // Camera half an eye to the left (eye 0) or right (eye 1): the
        // world then sits that much to the right or left in view space.
        float ipd = g_set.ipd.load(), conv = 0.0f;
        if (native) ipd = nativeIpd;   // the runtime's eyes, at the world's scale
        else TrueScale(&ipd, &conv);
        const float half = 0.5f * ipd;
        view[12] += g_eye.load() == 0 ? half : -half;
        g_eyeHalf.store(half);
        camPos = eye + right * (g_eye.load() == 0 ? -half : half);
        // eyes by turns ([stereo] turn_warp, vr/xr.cpp): both eyes' cameras as the head is now, for the eye not drawn
        if (native && xe.mode == xr::kViewsHead) {
            xr::TurnView tv{};
            tv.ok = turnFirst && !g_set.pairPerTick.load();
            if (tv.ok) {
                const V3 at[2] = {eye - right * half, eye + right * half};
                for (int e = 0; e < 2; ++e) { tv.at[e][0] = at[e].x; tv.at[e][1] = at[e].y; tv.at[e][2] = at[e].z; }
                const turnwarp::Quat q = turnwarp::FromAxes({right.x, right.y, right.z}, {up.x, up.y, up.z}, {-fwd.x, -fwd.y, -fwd.z});
                tv.turn[0] = q.x; tv.turn[1] = q.y; tv.turn[2] = q.z; tv.turn[3] = q.w;
                memcpy(tv.hero, L, sizeof tv.hero);
                tv.body = turnBody;
                tv.nearPlane = g_lastNear.load();
                tv.upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
            }
            xr::SetTurnView(tv);
        }
    }
    // When this eye's view was built: the head pose it is drawn for is the
    // one of this moment (PredictHead carries it on to now). FlatVR places
    // the screen at the head pose of this stamp and the headset's own
    // reprojection does the rest - see FlatVRAfrHalves.
    // [stereo] stamps=0 (a D2R VR Settings box, 2026-10-08): no stamp at all - FlatVR
    // then keeps its screen at the head as it is, as in the views without a head-turned
    // camera, which stayed smooth while tabletop and first person juddered for players.
    if (g_afrBlock && !g_set.stamps.load()) g_afrBlock->stamp_magic = 0;
    if (g_afrBlock && g_set.stamps.load()) {
        // Without prediction the head in this view is BodyWalk's last
        // sample as it was: its own stamp is the exact moment. With it,
        // the head carried on to now.
        uint32_t stamp = FlatVRAfrStampNow();
        const D2RVR_Shared* sh = Shared();
        if (!g_set.headPredict.load() && sh && sh->version == D2RVR_SHARED_VERSION && sh->sampleStampMagic == D2RVR_SAMPLE_STAMP_MAGIC)
            stamp = sh->sampleStamp;
        const int e = g_eye.load() & 1;
        // a replayed right eye is this view too, no view of its own: its stamp kept aside until the replay draws it (CommitRightSky);
        // a pair in two passes leaves the right eye's alone - its own pass writes it (vr/sky_view_eyes.h)
        const bool rightReplayed = replay::g_capturing.load() == 1;
        if (skyeyes::OwnEye(e, rightReplayed)) {
            g_afrBlock->stamp[e] = stamp;
            // and remembered: the frame presented may be an older view of this eye
            g_stampHist[e][g_stampHistAt[e] = (g_stampHistAt[e] + 1) & 3] = stamp;
        }
        if (skyeyes::KeepForRight(e, rightReplayed)) { g_stampRightNext = stamp; g_stampRightNextOk = true; }
        g_afrBlock->stamp_magic = FLATVR_AFR_STAMP_MAGIC;
    }
    {   // Row-major, v*M: the columns are the camera's axes in the world.
        AcquireSRWLockExclusive(&g_skyLock);
        const int e = g_eye.load() & 1;
        // a pair whose right eye the replay draws (vr/sky_view_eyes.h): the right eye's own build leaves its view alone - the replay puts
        // the left one moved across in (CommitRightSky); a pair in two passes: each pass its own eye only
        const bool rightReplayed = AfrOn() && replay::g_capturing.load() == 1;
        SkyView built = g_skyView[e];   // (its projection kept: VrProjInner's)
        SkyView& sv = skyeyes::OwnEye(e, rightReplayed) ? g_skyView[e] : built;
        for (int i = 0; i < 3; ++i) { sv.axes[i] = view[i*4]; sv.axes[3 + i] = view[i*4 + 1]; sv.axes[6 + i] = view[i*4 + 2]; }
        sv.eyeRel[0] = camPos.x - L[0]; sv.eyeRel[1] = camPos.y - L[1]; sv.eyeRel[2] = camPos.z - L[2];
        memcpy(sv.hero, L, sizeof sv.hero);
        sv.axes_ok = true;
        if (skyeyes::KeepForRight(e, rightReplayed)) {
            // replayed: the right eye's sky view is the left one moved by the eye separation, kept aside until the replay draws it
            SkyView& r1 = g_skyRightNext;
            const bool projOk = r1.proj_ok;
            float proj[4]; memcpy(proj, r1.proj, sizeof proj);
            r1 = sv;   // the projection: VrProjInner's (the left one shifted the other way)
            if (projOk) memcpy(r1.proj, proj, sizeof proj); else r1.proj_ok = sv.proj_ok;
            const float sh = 2.0f * g_eyeHalf.load();
            r1.eyeRel[0] += right.x * sh; r1.eyeRel[1] += right.y * sh; r1.eyeRel[2] += right.z * sh;
            g_skyRightNextOk = true;
        } else if (native && turnFirst && NativeByTurns()) {
            // eyes by turns, the other eye made on the GPU (xr::TurnMake): its overlays are drawn for where it is now (D2R_Overlay) - its view is
            // this one moved across by the eye gap, its projection its own (the last it was drawn with: the same fov)
            const int o = (g_eye.load() & 1) ^ 1;
            SkyView& ro = g_skyView[o];
            const bool projOk = ro.proj_ok;
            float proj[4]; memcpy(proj, ro.proj, sizeof proj);
            ro = sv;
            if (projOk) memcpy(ro.proj, proj, sizeof proj); else ro.proj_ok = false;
            const turnwarp::Vec at = turnwarp::OtherEyeAt({sv.eyeRel[0], sv.eyeRel[1], sv.eyeRel[2]}, {right.x, right.y, right.z}, g_eyeHalf.load(), o ^ 1);   // (turn_math_test: overlay camera)
            ro.eyeRel[0] = at.x; ro.eyeRel[1] = at.y; ro.eyeRel[2] = at.z;
        }
        ReleaseSRWLockExclusive(&g_skyLock);
    }
    g_camYaw.store(yaw);
    // What the stick needs: our right and forward on the ground, and the game
    // camera's screen axes in the world (it reads its stick on ITS screen).
    StickFrame f{};
    const V3 r = right, a = ahead;
    f.camR[0] = r.x; f.camR[1] = r.y; f.camR[2] = r.z;
    f.camF[0] = a.x; f.camF[1] = a.y; f.camF[2] = a.z;
    f.scrR[0] = G[0]; f.scrR[1] = G[4]; f.scrR[2] = G[8];
    f.scrU[0] = G[1]; f.scrU[1] = G[5]; f.scrU[2] = G[9];
    // The game's own projection may mirror an axis; the stick is read on
    // its SCREEN, so a mirrored axis turns the screen direction around.
    f.mirX = in.gameProj[0] < 0.0f ? -1.0f : 1.0f;
    f.mirY = in.gameProj[5] < 0.0f ? -1.0f : 1.0f;
    f.yawAtBuild = yaw;
    f.valid = true;
    if (g_set.mapDirection.load()) {   // [hud] map_direction: the way this view looks, on the game's map (hud.cpp paints it)
        const mapdir::Dir md = mapdir::OnGround(f.camF, f.scrR, f.scrU, f.mirX, f.mirY);   // (hud lays the arrow on the map: mapdir::IsoArrow)
        if (md.ok) hud::SetMapHeading(md.x, md.y);
    }
    if (mapAheadOk) {   // [hud] map_rotate / map_compass: the same way for the map in the hand, held while looking down at it (xr.cpp turns it)
        const float a[3] = {mapAhead.x, mapAhead.y, mapAhead.z};
        const mapdir::Dir md = mapdir::OnGround(a, f.scrR, f.scrU, f.mirX, f.mirY);
        if (md.ok) hud::SetMapTurnHeading(md.x, md.y);
    }
    AcquireSRWLockExclusive(&g_stickLock); g_stickFrame = f; ReleaseSRWLockExclusive(&g_stickLock);
    flog::Line("V,%u,%.4f,%d,%.3f,%.3f,%.3f,%.3f,%u", flog::Pass(), flog::Dt(), g_eye.load() & 1, yaw,
               atan2f(f.camF[0], f.camF[2]) * 57.2957795f, L[0], L[2],
               g_afrBlock ? g_afrBlock->stamp[g_eye.load() & 1] : 0u);
    memcpy(out, view, sizeof view);
    cbcmp::NoteView(g_eye.load(), view);
    if (g_eye.load() == 0) { AcquireSRWLockExclusive(&g_leftViewLock); memcpy(g_leftView, view, sizeof view); ReleaseSRWLockExclusive(&g_leftViewLock); }
    if (g_eye.load() == 0) {   // the laser's map (F1 / F3 in 3D; any other view: none)
        AcquireSRWLockExclusive(&g_ptrLock);
        g_ptrMap.ok = ptrMode >= 0;
        g_ptrMap.mode = ptrMode;
        if (ptrMode >= 0) {
            g_ptrMap.sim = ptrSim;
            g_ptrMap.groundY = L[1];
            memcpy(g_ptrMap.view, view, sizeof view);
            g_ptrMap.viewAt = GetTickCount64();
        }
        ReleaseSRWLockExclusive(&g_ptrLock);
    }
    return true;
}

// Our projection: the FlatVR screen's frustum (or [camera] fov), the near
// clip that cuts the hero's own head away, and in AFR each eye's off-axis
// shift so what lies at `convergence` sits on the screen.
// [camera] flat_shift_x / _y: where the hero stands on the flat picture (F2, F3), every 10 s when it moved by over a percent, and at once when
// the shift or the view changes - his drawn feet (the game's look-at point) and his cell at the base height (the point the game's 2D place
// is drawn at from above: his feet with the height's push taken off, vr/flat_ground.h), against the middle. The numbers for a default from
// the game: the shift that puts his feet in the middle. One line; this frame's view (VrView ran just before).
void FlatShiftLog(const d2rcam::WorldView& in, const float M[16]) {
    if (!in.lookAtValid || !prevscan::g_lastViewOk) return;
    const float* L = in.lookAt;
    const float cell[3] = {L[0] - L[1] * flatground::kShear, 0.0f, L[2] - L[1] * flatground::kShear};
    float fx = 0.0f, fy = 0.0f, cx = NAN, cy = NAN;
    if (!flatshift::OnPicture(prevscan::g_lastView, M, L, &fx, &fy)) return;
    flatshift::OnPicture(prevscan::g_lastView, M, cell, &cx, &cy);
    static ULONGLONG at = 0;
    static float lastX = 1e9f, lastY = 1e9f, lastSX = 1e9f, lastSY = 1e9f;
    static int lastView = -1;
    const float sx = g_set.flatShiftX.load(), sy = g_set.flatShiftY.load();
    const ULONGLONG now = GetTickCount64();
    const bool changed = sx != lastSX || sy != lastSY || ViewNow() != lastView;
    if (!changed && (now - at < 10000 || (fabsf(fx - lastX) < 1.0f && fabsf(fy - lastY) < 1.0f))) return;
    at = now; lastX = fx; lastY = fy; lastSX = sx; lastSY = sy; lastView = ViewNow();
    LogF("vrcam: flat picture (F%d): the hero's feet %.1f%% right, %.1f%% below the middle (%.2f up); his cell at the base height %.1f%%, %.1f%%; "
         "moved %.1f / %.1f ([camera] flat_shift_x / _y) - his feet in the middle at flat_shift_x %.1f, flat_shift_y %.1f",
         ViewNow(), fx, fy, L[1], cx, cy, sx, sy, sx - fx, sy - fy);
}

bool VrProjInner(const d2rcam::WorldView& in, float M[16]);
bool VrProj(const d2rcam::WorldView& in, float M[16]) {
    const bool ours = VrProjInner(in, M);
    if (ours) {
        memcpy(prevscan::g_lastProj, M, sizeof prevscan::g_lastProj);
        prevscan::g_lastProjOk = true;
        if (prevscan::g_lastViewOk) dlssmv::SetPending(prevscan::g_lastView, M, g_eye.load() & 1);
    }
    return ours;
}
bool VrProjInner(const d2rcam::WorldView& in, float M[16]) {
    if (!g_enabled.load() || !g_inWorld.load()) return false;   // the menus: the game's own (no near clip through the heroes' heads)
    const float ratio = in.viewportH > 0.0f ? in.viewportW / in.viewportH : 0.0f;
    const float aspect = ratio > 0.1f && ratio < 10.0f ? ratio : 16.0f / 9.0f;
    mobbars::NoteViewport(in.viewportW, in.viewportH);   // [hud] monster_height is a percent of it
    g_lastAspect.store(aspect);
    // The table: nothing of the hero to cut away, and the head may come down close to the game - 5 cm.
    const float nearZ = TableView() ? 0.05f * g_set.tableScale.load() : TopPersp() ? 0.5f : std::max(g_set.nearClip.load(), 0.05f);
    g_lastNear.store(nearZ);
    float shift = 0.0f;
    xr::Eyes xe{};
    const bool nativeEyes = NativeEyes(&xe);
    TellF4Camera(nativeEyes, aspect);
    if (nativeEyes) {
        // Native OpenXR: this eye's own frustum, from the runtime's fov (tangents; left and down
        // negative). The replay makes the right eye from the left projection with its x offset
        // moved by twice `shift` (camfix): the right eye's own offset, with the mirrored fovs
        // headsets have - each eye wider on its outer side.
        const int e = g_eye.load() & 1;
        // (vr/eye_fov.h Projection: the frustum's edges on the picture's - also an eye drawn with the runtime's own fov, [openxr] runtime_fov)
        auto proj = [&](int i) { return eyefov::Projection({xe.tanL[i], xe.tanR[i], xe.tanU[i], xe.tanD[i]}); };
        auto offX = [&](int i) { return proj(i).offX; };
        d2rcam::m4::PerspectiveRevZ(1.0f, 1.0f, nearZ, M);
        const eyefov::Proj pe = proj(e);
        M[0] = pe.sx;
        M[5] = pe.sy;
        M[8] = pe.offX;
        M[9] = pe.offY;
        shift = 0.5f * (offX(0) - offX(1));
        static bool told = false;
        if (!told && e == 0) {
            told = true;
            const float r0 = 2.0f / (xe.tanR[1] - xe.tanL[1]), r5 = 2.0f / (xe.tanU[1] - xe.tanD[1]);
            LogF("vrcam: openxr - projection x %.4f y %.4f, off-axis %.4f / %.4f (left / right); the right eye's x %.4f y %.4f%s", M[0], M[5], M[8],
                 offX(1), r0, r5, fabsf(r0 - M[0]) > 0.01f * M[0] || fabsf(r5 - M[5]) > 0.01f * M[5] ? " - NOT mirrored: the replayed right eye is a little off" : "");
        }
        g_projSx.store(M[0]); g_projSy.store(M[5]); g_stereoIpd.store(2.0f * g_eyeHalf.load()); g_stereoConv.store(0.0f);
    } else {
    d2rcam::m4::PerspectiveRevZ(CameraFov() * 3.14159265f / 180.0f, aspect, nearZ, M);
    // The frustum IS the FlatVR screen: each side by its own size at its distance.
    if (float sw, sh, sd; g_set.fovFromFlatVR.load() && LiveScreen(&sw, &sh, &sd)) { M[0] = 2.0f * sd / sw; M[5] = 2.0f * sd / sh; }
    // Off-axis frustum per eye: what lies at `convergence` gets no parallax and
    // sits on the screen, farther goes in behind it. With parallel frusta the
    // whole world was in front of the screen and read as shallow.
    float ipd = g_set.ipd.load(), conv = g_set.convergence.load();
    TrueScale(&ipd, &conv);
    g_projSx.store(M[0]); g_projSy.store(M[5]); g_stereoIpd.store(ipd); g_stereoConv.store(conv);
    if (AfrOn() && conv > 0.0f) {
        shift = M[0] * 0.5f * ipd / conv;
        M[8] += g_eye.load() == 0 ? shift : -shift;
    }
    if (g_set.platform.load() == 0) {   // [camera] flat_shift_x / _y: flat F2 / F3, the picture moved (vr/flat_shift.h); and where the hero is on it
        flatshift::Apply(M, g_set.flatShiftX.load(), g_set.flatShiftY.load());
        FlatShiftLog(in, M);
    }
    }
    if ((g_eye.load() & 1) == 0) {
        g_projShift.store(shift);
        AcquireSRWLockExclusive(&g_ptrLock);
        memcpy(g_ptrMap.proj, M, sizeof g_ptrMap.proj);   // (the laser's map, left eye)
        g_ptrMap.projAt = GetTickCount64();
        ReleaseSRWLockExclusive(&g_ptrLock);
    }
    AcquireSRWLockExclusive(&g_skyLock);
    const int eye = g_eye.load() & 1;
    const bool rightReplayed = AfrOn() && replay::g_capturing.load() == 1;   // (as VrViewInner: vr/sky_view_eyes.h)
    const bool own = skyeyes::OwnEye(eye, rightReplayed);
    if (own) {
        SkyView& sv = g_skyView[eye];
        sv.proj[0] = M[0]; sv.proj[1] = M[5]; sv.proj[2] = M[8]; sv.proj[3] = M[9]; sv.proj_ok = true;
    }
    if (skyeyes::KeepForRight(eye, rightReplayed)) {
        // replayed: no right pass builds the right eye's projection - the left one, shifted the other way (kept aside with its view)
        SkyView& r1 = g_skyRightNext;
        r1.proj[0] = M[0]; r1.proj[1] = M[5]; r1.proj[2] = M[8] - 2.0f * shift; r1.proj[3] = M[9]; r1.proj_ok = true;
    }
    const bool turned = NativeByTurns() && xr::TurnsNow();   // eyes by turns: the other eye's view moved with this one (VrViewInner)
    for (int e = 0; e < 2; ++e)   // (a replayed right eye's: when the replay puts it in, CommitRightSky)
        if ((e == eye && own) || turned)
            g_skyHist[e][g_skyHistAt[e] = (g_skyHistAt[e] + 1) & 3] = g_skyView[e];
    ReleaseSRWLockExclusive(&g_skyLock);
    return true;
}

// The replay draws the right eye now (replay::RightFromLeft, after the left eye's effects): its sky view and stamp - the left ones moved
// across, kept aside by the left build - go in for its effects (vr/sky_view_eyes.h). The draw thread.
void CommitRightSky() {
    AcquireSRWLockExclusive(&g_skyLock);
    if (g_skyRightNextOk) {
        g_skyView[1] = g_skyRightNext;
        g_skyHist[1][g_skyHistAt[1] = (g_skyHistAt[1] + 1) & 3] = g_skyView[1];
        g_skyRightNextOk = false;
    }
    ReleaseSRWLockExclusive(&g_skyLock);
    if (g_stampRightNextOk) {
        if (g_afrBlock) g_afrBlock->stamp[1] = g_stampRightNext;
        g_stampHist[1][g_stampHistAt[1] = (g_stampHistAt[1] + 1) & 3] = g_stampRightNext;
        g_stampRightNextOk = false;
    }
}

}  // namespace vrc
