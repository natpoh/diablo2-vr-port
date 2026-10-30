// vr/view/head.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M16); what it gives: vr/view/camera.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// The head and the hands from BodyWalk (Game Link's Tracking, link::Head); while a pair is
// drawn, the pair's own copy - one head for both eyes (HookDrawGameScreen).
std::atomic<const D2RVR_Shared*> g_pairShared{nullptr};
const D2RVR_Shared* Shared() {
    const D2RVR_Shared* p = g_pairShared.load();
    return p ? p : link::Head();
}

bool HeadYaw(float* yaw) {
    const D2RVR_Shared* s = Shared();
    if (!s || s->version != D2RVR_SHARED_VERSION || !s->headValid) return false;
    *yaw = s->headYawDeg;
    return std::isfinite(*yaw);
}

float WrapDeg(float a) { return std::remainder(a, 360.0f); }   // into -180..180

// The head between BodyWalk's updates. They come ~100 times a second, the
// game draws up to 240: frames in between got the same angles, the camera
// turned in steps, and each step held for two frames read as a double image
// in a head turn. Each angle now goes on at the speed of the last two updates
// for up to 15 ms past the latest ([head] predict=0: as sent).
struct HeadTrack { uint32_t counter; double t; float v[3], vel[3]; bool ok; };
SRWLOCK g_headLock = SRWLOCK_INIT;
HeadTrack g_head{};

double NowSeconds() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

// A pair is being drawn from one game frame (HookPrismBlit): both eyes take the
// head as sent, never carried on - the second eye is drawn later, and would
// otherwise get a head a few milliseconds further along than the first.
std::atomic<bool> g_pairNow{false};

// yaw, pitch, roll as sent (degrees), advanced to now.
void PredictHead(const D2RVR_Shared* s, float out[3]) {
    float raw[3] = {s->headYawDeg, s->pitchValid ? s->headPitchDeg : 0.0f, s->rollValid ? s->headRollDeg : 0.0f};
    for (float& v : raw) if (!std::isfinite(v)) v = 0.0f;   // one NaN kept in the track would stay there for good
    if (!g_set.headPredict.load() || g_pairNow.load()) { memcpy(out, raw, sizeof raw); return; }
    const double now = NowSeconds();
    AcquireSRWLockExclusive(&g_headLock);
    HeadTrack& h = g_head;
    if (!h.ok) { h = {s->counter, now, {raw[0], raw[1], raw[2]}, {0, 0, 0}, true}; }
    else if (s->counter != h.counter) {
        const double dt = now - h.t;
        for (int i = 0; i < 3; ++i) {
            const float v = dt > 0.002 && dt < 0.1 ? WrapDeg(raw[i] - h.v[i]) / (float)dt : 0.0f;
            h.vel[i] = 0.5f * h.vel[i] + 0.5f * v;   // a little smoothing: arrival times jitter by a frame
            h.v[i] = raw[i];
        }
        h.counter = s->counter; h.t = now;
    }
    const float ahead = (float)std::min(now - h.t, 0.015);
    for (int i = 0; i < 3; ++i) out[i] = h.v[i] + h.vel[i] * ahead;
    ReleaseSRWLockExclusive(&g_headLock);
}

// [flatvr] room_centre in VR without a native session (FlatVR holds the headset).
bool RoomCentreOn() { return g_set.flatRoomCentre.load() && g_set.platform.load() == 1 && !xr::On(); }

// The head's yaw from straight ahead (degrees, + left), `yawSent` = headYawDeg as sent (or carried on). [flatvr] room_centre: the
// room's yaw against the one kept at our F11 - BodyWalk's Reset Center moves the zero of headYawDeg, not the room's, and the world
// turned against FlatVR's screen (which stands in the room's own zero) until the next F11. Otherwise BodyWalk's yaw against ours.
float HeadYawFromCentre(const D2RVR_Shared* sh, float yawSent) {
    if (RoomCentreOn() && sh && sh->roomMagic == D2RVR_ROOM_MAGIC && std::isfinite(sh->headYawRoomDeg)) {
        const float base = WrapDeg(sh->headYawRoomDeg - sh->headYawDeg);
        if (!g_recenterRoomOk.load()) {   // not centred on the room yet: where the old way has straight ahead
            g_recenterRoom.store(WrapDeg(base + g_recenter.load()));
            g_recenterRoomOk.store(true);
        }
        return WrapDeg(yawSent + base - g_recenterRoom.load());
    }
    return WrapDeg(yawSent - g_recenter.load());
}

// The yaw the camera should have now: head (if on) plus the mouse's extra.
float TargetYaw() {
    float yaw = g_pairNow.load() ? g_heldYaw.load() : g_mouseYaw.load() + g_turnYaw.load(), head = 0.0f;
    // Native OpenXR (F4/F2 drawn with the runtime's eyes): the head yaw those eyes were drawn with,
    // from xr's own recentre - the native view's own turn. Not D2RVR_Shared's: in a pair that is the
    // room's raw yaw, against g_recenter from whichever block was there at the last F11 - the body
    // stood ~100 deg off the view at game start until a lucky F11 (2026-10-10).
    if (float rel = 0.0f; NativeHeadView() && xr::PairHeadYaw(&rel)) return WrapDeg(yaw - rel * 57.2957795f);
    if (g_set.headYaw.load() && HeadYaw(&head)) {
        float p[3]; PredictHead(Shared(), p);
        yaw += g_set.yawSign.load() * g_set.yawScale.load() * HeadYawFromCentre(Shared(), p[0]);
    }
    return WrapDeg(yaw);
}

// In the headset the camera's roll is the head's own, against gravity - never from the
// recentre (2026-10-11). FlatVR shows each picture on a screen at the very head pose it
// was drawn with, so a camera rolled by the head's roll at the recentre stood that much
// askew on it: the horizon leaned by the roll the head happened to have then (1-3
// degrees, a new amount after every restart), and showed as a lean "only turning right"
// - turning left the head leans that way by itself. On a monitor the recentre's upright
// stays: there the head only steers a screen that does not move with it. (The pitch
// keeps its recentre: off level it moves the horizon up or down, it does not lean it,
// and [camera] pitch is set against it.)
bool HeadLevelIsGravity() { return g_set.platform.load() == 1; }

// Roll about the line of sight from the head tilting sideways, 0 when off.
float TargetRoll() {
    const D2RVR_Shared* sh = Shared();
    if (!g_set.headRoll.load() || !sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || !sh->rollValid || !std::isfinite(sh->headRollDeg))
        return 0.0f;
    float p[3]; PredictHead(sh, p);
    const float level = HeadLevelIsGravity() ? 0.0f : g_recenterRoll.load();
    return std::clamp(g_set.rollSign.load() * (p[2] - level), -80.0f, 80.0f);
}

// [camera] stick_pitch_f2: the right stick tilts the camera in VR F2 only (not F1 in perspective, the table, F4, nor flat mode).
bool StickPitchOn() { return g_set.stickPitchF2.load() && g_set.rightTurn.load() && g_set.platform.load() == 1 && g_set.vrView.load() == 2; }

// Pitch relative to the game's own steep camera: ini level point, the mouse's
// extra, the right stick's in VR F2 ([camera] stick_pitch_f2), and the head
// looking up (more negative) or down. Kept short of the vertical, where the
// rotation would flip.
float TargetPitch() {
    float pitch = g_set.pitch.load() + (g_pairNow.load() ? g_heldPitch.load() : g_mousePitch.load());
    if (StickPitchOn()) pitch += g_pairNow.load() ? g_heldStickPitch.load() : g_stickPitch.load();
    const D2RVR_Shared* sh = Shared();
    if (g_set.headPitch.load() && sh && sh->version == D2RVR_SHARED_VERSION && sh->headValid && sh->pitchValid && std::isfinite(sh->headPitchDeg))
    {
        float p[3]; PredictHead(sh, p);
        pitch -= g_set.pitchSign.load() * (p[1] - g_recenterPitch.load());
    }
    return std::clamp(pitch, -150.0f, 60.0f);
}

// [flatvr] head_level, VR F4 on FlatVR: the camera's pitch and roll are the head's own against gravity, as native OpenXR takes them
// from the runtime's eyes. FlatVR hangs each picture on a screen at the full head pose it was drawn with; a camera pitched apart
// from the head by any constant ([camera] pitch on top of the game's steep camera, the head's pitch at the last F11) stood that much
// apart on it - and on a screen that turns with the head a constant pitch is a lean once the head looks to the side: the world
// tilted and swam with every turn (the roll had the same fault, fixed 2026-10-11; HeadLevelIsGravity). The roll back from what
// BodyWalk sends (flatvrpose::TrueRollDeg): looking down 60 degrees it sent half the roll.
bool FullBody();
bool FlatVrLevel() {
    if (!g_set.flatLevel.load() || g_set.platform.load() != 1 || xr::On() || !FullBody() || !g_set.headPitch.load()) return false;
    const D2RVR_Shared* sh = Shared();
    return sh && sh->version == D2RVR_SHARED_VERSION && sh->headValid && sh->pitchValid && std::isfinite(sh->headPitchDeg);
}
// How far below the horizon the camera looks (degrees, + down): the head's pitch, the mouse's extra on top.
float LevelDownDeg() {
    float p[3]; PredictHead(Shared(), p);
    return std::clamp(-g_set.pitchSign.load() * p[1] + (g_pairNow.load() ? g_heldPitch.load() : g_mousePitch.load()), -89.0f, 89.0f);
}
float LevelRollDeg() {
    const D2RVR_Shared* sh = Shared();
    if (!g_set.headRoll.load() || !sh || !sh->rollValid || !std::isfinite(sh->headRollDeg)) return 0.0f;
    float p[3]; PredictHead(sh, p);
    return std::clamp(g_set.rollSign.load() * flatvrpose::TrueRollDeg(p[2], p[1]), -80.0f, 80.0f);
}

}  // namespace vrc
