// vr/view/views.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M16); what it gives: vr/view/camera.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// The game put into the view [mode] names. View 1 is the game's own camera
// (vrcam off), 2 third person, 3 and 4 first person (FullBody, InsideFree).
// False while the camera's hooks are not in: tried again on the next tick.
bool ApplyView(const char** said) {
    const bool vr = g_set.platform.load() == 1;
    const int v = ViewNow();
    // F1 is the game's own view from above: vrcam off. The table (F5) is our
    // camera where the head is in the room (TableCamera), the hero whole as from
    // behind; the void black, the headset shows the room there (docs/plan_tabletop_mr.md).
    const bool table = vr && v == 3;   // the game on the floor is F3 since vr_mode_order 2 (as TableView)
    const bool on = v != 1 || (vr && (g_set.topPersp.load() || xr::Views3dOn()));   // (F1 through our camera: [top] perspective, or native OpenXR's 3D F1)
    if (on && !AllHooksIn()) InstallHooks();
    if (on && !g_h.camera) { *said = "vrcam: no camera yet - its hooks are not in (see the log)"; return false; }
    g_view.store(v == 2 || table || (vr && v == 1) ? 2 : 1);
    if (table) g_tableReanchor.store(true);   // the hero put down in front of where the head is now
    g_enabled.store(on);
    g_mouseLookOn.store(MouseLookForView());
    // 4: the game on the floor - our camera follows the head, so FlatVR's screen must too.
    // F1 in true perspective is sent as the game's own view (0): its camera is the game's,
    // not the head's, and with Head Lock on (vr/link.cpp locks 1, 3 and 4) the picture
    // stuck to the head ("the camera sticks to my head, it must not", 2026-10-05).
    gamestate::SetViewMode(!on ? 0u : table ? 4u : vr && v == 1 ? 0u : (uint32_t)g_view.load());
    g_gen.fetch_add(1);
    static const char* const kFlat[] = {"", "flat: the game's own camera (F1)", "flat: third person (F2)",
                                        "flat: first person (F3) - mouse look, W A S D"};
    static const char* const kVr[] = {"", "VR: from above (F1)", "VR: third person (F2)",
                                      "VR: the game on the floor (F3) - walk round it; F11 puts it in front of you again",
                                      "VR: first person, the body is yours (F4)"};
    *said = vr ? kVr[v] : kFlat[v];
    return true;
}

// Called from the update thread on every tick and after a view key: applies
// the view once whenever [mode] names another one (the Home page writes it,
// the keys below write it too).
void FollowMode() {
    static int applied = -1;
    static bool toldWait = false;
    const int want = g_set.platform.load() * 10 + ViewNow() + (TopPersp() ? 100 : 0);
    if (want == applied) return;
    const char* said = "";
    if (ApplyView(&said)) { applied = want; toldWait = false; Say(said); }
    else if (!toldWait) { toldWait = true; Log(said); }
}

// A view of the current platform, by number: remembered in the ini, applied at once.
void SetView(int v) {
    if (v < 1 || v > ViewCount()) return;
    const bool vr = g_set.platform.load() == 1;
    (vr ? g_set.vrView : g_set.flatView).store(v);
    wchar_t b[8];
    swprintf_s(b, L"%d", v);
    WritePrivateProfileStringW(L"mode", vr ? L"vr_mode" : L"flat_view", b, g_iniPath);
    FollowMode();
}

// F12 (and the console's "vrcam"): the next view of this platform, round.
const char* Toggle() {
    SetView(ViewNow() % ViewCount() + 1);
    return "vrcam: next view (F12)";
}

void Recenter();
// [flatvr] room_centre, the update thread: BodyWalk's own Reset Center (its RESET CENTER button, its key, a mapped gesture) moved the
// zero of the yaw it sends - taken as our F11, so that one press centres the game as before, and the world stays with FlatVR's screen.
void FlatVrCentreTick() {
    const D2RVR_Shared* sh = link::Head();
    if (!RoomCentreOn() || !sh || sh->version != D2RVR_SHARED_VERSION || !sh->headValid || sh->roomMagic != D2RVR_ROOM_MAGIC) {
        g_bwBaseOk.store(false);
        return;
    }
    const float base = WrapDeg(sh->headYawRoomDeg - sh->headYawDeg);
    if (!std::isfinite(base)) return;
    if (g_bwBaseOk.load() && std::fabs(WrapDeg(base - g_bwBase.load())) > 1.0f) {
        LogF("vrcam: flatvr - BodyWalk's centre was reset (its yaw's zero moved %.1f deg): recentred here too ([flatvr] room_centre)",
             WrapDeg(base - g_bwBase.load()));
        Recenter();   // (sets g_bwBase from this head)
        return;
    }
    g_bwBase.store(base);
    g_bwBaseOk.store(true);
}

void Recenter() {
    float head = 0.0f;
    if (HeadYaw(&head)) g_recenter.store(head);
    if (const D2RVR_Shared* sh = Shared(); sh && sh->pitchValid && std::isfinite(sh->headPitchDeg)) g_recenterPitch.store(sh->headPitchDeg);
    if (const D2RVR_Shared* sh = Shared(); sh && sh->rollValid && std::isfinite(sh->headRollDeg)) g_recenterRoll.store(sh->headRollDeg);
    // [flatvr] room_centre / head_position: straight ahead as the room's yaw, and where the head stands, from this head
    if (const D2RVR_Shared* sh = Shared(); sh && sh->roomMagic == D2RVR_ROOM_MAGIC && std::isfinite(sh->headYawRoomDeg)) {
        g_recenterRoom.store(sh->headYawRoomDeg);
        g_recenterRoomOk.store(true);
        g_bwBase.store(WrapDeg(sh->headYawRoomDeg - sh->headYawDeg));
        g_bwBaseOk.store(std::isfinite(sh->headYawDeg));
        if (std::isfinite(sh->headRoom[0] + sh->headRoom[1] + sh->headRoom[2])) {
            for (int i = 0; i < 3; ++i) g_recenterPos[i].store(sh->headRoom[i]);
            g_recenterPosOk.store(true);
        }
    }
    g_mouseYaw.store(0.0f); g_mousePitch.store(0.0f);
    g_stickPitch.store(0.0f);    // and the right stick's tilt ([camera] stick_pitch_f2)
    g_facingReset.store(true);   // and the hero's facing is looked for again
    g_tableReanchor.store(true); // and the table view puts the game in front of the head again
    xr::Recenter();              // and native OpenXR's straight ahead taken from the next views
    bowdrawwire::NoteRecenter(); // and a bow draw under way ends
#if D2RVR_FIRST_PERSON
    skel::ResetGrip();           // and the weapon's grip taken again
#endif
    g_gen.fetch_add(1);
    Log("vrcam: recentered");
}

// Console: "vrcam" = F12.
ConsoleCommandResult __cdecl CmdVrcam(D2R::Game::Client*, const ConsoleCommandContext*, void*) noexcept {
    Say(Toggle());
    return ConsoleCommandResult::Handled;
}

// Right stick turn: smooth at turnSpeed, or one snap per flick. Run on the 1 ms
// timer and once more at the start of every pair, so a pair's turn is exactly
// the time since the last one. The time is QPC's: GetTickCount64 moves in
// ~16 ms steps, and the turn went on in steps of 0 or 16 ms - an uneven
// speed that read as judder on stick turns.
SRWLOCK g_turnLock = SRWLOCK_INIT;

void TurnTick() {
    AcquireSRWLockExclusive(&g_turnLock);
    static double last = NowSeconds();
    static bool armed = true;
    const double now = NowSeconds();
    const float dt = (float)std::min(now - last, 0.1);
    last = now;
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&g_turnLock); } } unlock;
    if (!g_enabled.load() || !g_set.rightTurn.load()) { g_rightX.store(0.0f); g_rightY.store(0.0f); return; }
    // [camera] stick_pitch_f2 (VR F2): the stick's up / down tilts, at the turn's dead zone; a push more sideways than up / down
    // is a turn and does not tilt (vr/stick_pitch.h). Off: the tilt forgotten, the camera as before.
    if (!g_set.stickPitchF2.load()) g_stickPitch.store(0.0f);
    else if (StickPitchOn()) {
        const float step = stickpitch::Step(g_rightX.load(), g_rightY.load(), dt, g_set.stickPitchSpeed.load(), g_set.stickPitchSign.load());
        if (step != 0.0f) {
            const stickpitch::Limits l{g_set.stickPitchMin.load(), g_set.stickPitchMax.load()};
            g_stickPitch.store(stickpitch::Apply(g_stickPitch.load(), step, g_set.pitch.load(), l));
            g_gen.fetch_add(1);
        }
    }
    // [camera] stick_pitch_flat (flat F2 / F3, a player with an Xbox pad: "the right stick pitch doesn't change the camera angle up and down"): the
    // right stick's up / down is the mouse's up / down - the same look, the same limits; the stick's shaping as the VR tilt's (vr/stick_pitch.h).
    if (g_set.stickPitchFlat.load() && g_set.platform.load() == 0 && ViewNow() != 1) {
        const float step = stickpitch::Step(g_rightX.load(), g_rightY.load(), dt, g_set.stickPitchSpeed.load(), g_set.stickPitchSign.load());
        if (step != 0.0f) {
            g_mousePitch.store(std::clamp(g_mousePitch.load() + step, -90.0f, 90.0f));   // (+ looks lower, as the mouse's dy)
            g_gen.fetch_add(1);
        }
    }
    const float x = g_set.turnSign.load() * g_rightX.load();
    const float dead = 0.25f, a = fabsf(x);
    const float snap = g_set.snapAngle.load();
    if (snap > 0.0f) {
        if (a > 0.7f && armed) { g_turnYaw.store(WrapDeg(g_turnYaw.load() - (x > 0 ? snap : -snap))); armed = false; g_gen.fetch_add(1); }
        if (a < dead) armed = true;
        return;
    }
    if (a < dead) return;
    const float k = (a - dead) / (1.0f - dead);   // 0..1 past the dead zone
    g_turnYaw.store(WrapDeg(g_turnYaw.load() - (x > 0 ? 1.0f : -1.0f) * k * k * g_set.turnSpeed.load() * dt));
    g_gen.fetch_add(1);
}

}  // namespace vrc
