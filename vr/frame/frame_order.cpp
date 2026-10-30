// vr/frame/frame_order.cpp - the frame's order (docs/plan_modules.md M17): the game's draw of a frame, run twice for a stereo pair with
// the pair's head held, its eyes, native OpenXR's pair and the timing lines (HookDrawGameScreen and its parts), and the 1 ms update
// thread (UpdateThread: keys, the arms, the interface, the sky, the hit by touch, pose share, the settings' reload and the installs) -
// cut whole from vr/vrcam.cpp. The present's order is vr/render/effects/effects.cpp's PresentCommon. What it gives: vr/frame/frame_order.h.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// A stereo pair from one game frame ([stereo] pair_per_tick, AFR only).
// The engine draws with one camera per frame (RenderGraph::BeginFrameWork
// takes one Camera*), so a true second camera is out of reach. Instead the
// game's whole draw runs twice per game frame: left eye, then right eye with
// the frame time held at zero, so nothing moves on between them. Both eyes
// then share one game state and one head, and which eye each Present carries
// is known, not guessed.
// The unit drawn twice is sDrawGameScreen (0x93B40), which the main loop calls
// right after its timer (0x8C862 -> 0x8C869): the old D2 draw builds the HUD
// and its upload lists, then PrismEndDraw -> PrismBlit draws the world and
// presents. Doubling PrismBlit alone (0.48.0) removed the device: its second
// pass drew the HUD from upload lists the first pass had already reset. The
// view and projection are rebuilt inside PrismBlit (UpdateSystems 0xE39390 ->
// GetProj / ViewRebuild), so each pass gets its eye's camera.
// D2RLoader layout, game 3.3.93787; recon in docs/HANDOVER.md.
const uint8_t kSigDrawGameScreen[16] = {0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x60,0x48};
// The frame time: GetFrameTime (0xA19200) and GetRawFrameTime (0xA19210) read
// two floats the main loop's timer (0xA19220) writes once per game frame.
constexpr uint64_t RVA_GET_FRAME_TIME = 0xA19200, RVA_GET_RAW_FRAME_TIME = 0xA19210;
const uint8_t kSigGetFrameTime[9] = {0xF3,0x0F,0x10,0x05,0xC8,0x9F,0xDB,0x01,0xC3};      // movss xmm0, [0x27D31D0]; ret
const uint8_t kSigGetRawFrameTime[9] = {0xF3,0x0F,0x10,0x05,0xBC,0x9F,0xDB,0x01,0xC3};   // movss xmm0, [0x27D31D4]; ret
DrawGameScreenFn OrigDrawGameScreen;
extern std::atomic<bool> g_inWorld;

bool FrameTimeFound() {
    static int ok = -1;
    if (ok < 0) ok = Matches(RVA_GET_FRAME_TIME, kSigGetFrameTime, sizeof kSigGetFrameTime) &&
                     Matches(RVA_GET_RAW_FRAME_TIME, kSigGetRawFrameTime, sizeof kSigGetRawFrameTime) &&
                     d2rsig::Addr(RVA_FRAME_TIME) && d2rsig::Addr(RVA_RAW_FRAME_TIME) ? 1 : 0;
    return ok == 1;
}

// Native OpenXR with the eyes by turns (D2R VR Settings: "Native OpenXR - eyes by turns"): one pass
// a game frame through HookDrawGameScreen, which opens the runtime's frame for it (DrawByTurns).
bool NativeByTurns() { return g_set.platform.load() == 1 && xr::ByTurns() && !g_set.pairPerTick.load(); }
bool PairOfOneFrame() { return g_pairNow.load() && !NativeByTurns(); }

// Without the frame time held, the second pass would play the frame's
// animation and particles a second time: the pair is drawn only with it.
bool PairWanted() {
    return g_h.blit && AfrOn() && (g_set.pairPerTick.load() || NativeByTurns()) && g_inWorld.load() && FrameTimeFound();
}

void SetEye(int eye) {
    g_eye.store(eye);
    if (!g_afrBlock) return;
    g_afrBlock->swap = g_set.afrSwap.load() ? 1u : 0u;
    g_afrBlock->eye = (uint32_t)eye;
    g_afrBlock->frame++;
    g_afrBlock->enabled = 1u;
}

void TurnTick();
// Where a pair's time goes (pairtime), [debug] profile_draw (drawprof), cb_compare (cbcmp), replay_trace (rtrace): vr/diag/pair_probes.cpp

// The right eye's camera in the replayed lists, the depth fix (camfix): vr/render/depth/depth_fix.cpp

// One CPU pass, two GPU frames - the replay (replay): vr/render/stereo/replay.cpp
// Crash reports: vr/diag/crash.cpp

// Native OpenXR: the pair's head and hands from the game's own session, at the moment the
// frame will be shown - what Game Link makes of BodyWalk's frame (hands from the head in
// the frame of its turn alone), without the trip out to BodyWalk and back. The rest of the
// block (the user's height) stays BodyWalk's.
bool XrIntoShared(D2RVR_Shared* out, const D2RVR_Shared* bridge) {
    D2RVR_XrInput in;
    if (!xr::PairInput(&in) || !in.head.valid) return false;
    if (!bridge) memset(out, 0, sizeof *out);
    const float r2d = 57.2957795f;
    const V3 f = d2rcam::m4::Rotate(in.head.rot, {0.0f, 0.0f, -1.0f});
    const V3 r = d2rcam::m4::Rotate(in.head.rot, {1.0f, 0.0f, 0.0f});
    const V3 u = d2rcam::m4::Rotate(in.head.rot, {0.0f, 1.0f, 0.0f});
    const float yaw = atan2f(-f.x, -f.z);   // in the room, + turned left
    out->version = D2RVR_SHARED_VERSION;
    out->headValid = 1;
    out->headYawDeg = yaw * r2d;
    out->headHeightM = in.head.pos[1];
    out->headPitchDeg = asinf(std::clamp(f.y, -1.0f, 1.0f)) * r2d; out->pitchValid = 1;
    out->headRollDeg = atan2f(-r.y, u.y) * r2d; out->rollValid = 1;
    const float c = cosf(yaw), sn = sinf(yaw), qs = sinf(-yaw * 0.5f), qc = cosf(-yaw * 0.5f);
    auto put = [&](const D2RVR_XrPose& g, float* pos, float* rot) {
        const float dx = g.pos[0] - in.head.pos[0], dy = g.pos[1] - in.head.pos[1], dz = g.pos[2] - in.head.pos[2];
        pos[0] = dx * c - dz * sn;
        pos[1] = dy;
        pos[2] = dx * sn + dz * c;
        const float x = g.rot[0], y = g.rot[1], z = g.rot[2], w = g.rot[3];   // the head's turn taken off
        rot[0] = qc * x + qs * z;
        rot[1] = qc * y + qs * w;
        rot[2] = qc * z - qs * x;
        rot[3] = qc * w - qs * y;
    };
    uint32_t hands = 0;
    // a hand only with its place too (valid 1): PoseOut gives none else, this keeps it so
    if (in.hand[1].grip.valid == 1) { put(in.hand[1].grip, out->rightHand, out->rightRot); hands |= 1u; }
    if (in.hand[0].grip.valid == 1) { put(in.hand[0].grip, out->leftHand, out->leftRot); hands |= 2u; }
    out->handsValid = hands;
    out->rightGrip = (hands & 1u) ? std::clamp(in.hand[1].squeeze, 0.0f, 1.0f) : 0.0f;
    out->leftGrip = (hands & 2u) ? std::clamp(in.hand[0].squeeze, 0.0f, 1.0f) : 0.0f;
    out->gripMagic = D2RVR_GRIP_MAGIC;
    for (int i = 0; i < 3; ++i) out->headRoom[i] = in.head.pos[i];
    out->headYawRoomDeg = yaw * r2d;
    out->roomMagic = D2RVR_ROOM_MAGIC;
    out->sampleStamp = D2RVRStampNow();
    out->sampleStampMagic = D2RVR_SAMPLE_STAMP_MAGIC;
    out->counter++;
    return true;
}

// A pair as the headset's own eyes (native OpenXR's views, in a game; the runtime's frame opened by
// XrBegin, last). A menu open: the flat picture - or with [openxr] menu_3d in F4 (MenuAs3d) still the
// pair, the interface taken out of the picture by hud.cpp as a panel for xr.cpp (set here, before the
// pass draws it, so the frame the menu opens has it already).
// The world's pair: the camera's follow within the second (d2rcam::InWorld), or ([stereo] slow_pair_world) since the last pair began -
// a pass of over a second on a slow card left every next pair "no world", and its right eye went to the headset as the flat picture
// between two pairs in stereo (vr/pair_world.h). Told every 10 s while it keeps pairs so.
bool PairWorld() {
    static pairworld::Gate gate;
    static uint32_t kept = 0;
    static ULONGLONG toldAt = 0;
    const ULONGLONG now = GetTickCount64();
    const pairworld::Why why = gate.Begin(now, d2rcam::InWorld(), d2rcam::LastSyncTick(), g_set.slowPairWorld.load());
    if (why == pairworld::Why::SincePrevious) {
        ++kept;
        if (!toldAt || now - toldAt > 10000) {
            toldAt = now;
            LogF("vrcam: a pair after a pass of over a second kept in the world (the camera's follow ran in the last pair; [stereo] slow_pair_world) - "
                 "%u time(s): a graphics card this far behind shows it as stereo, not the flat picture", kept);
            kept = 0;
        }
    }
    return pairworld::World(why);
}

bool NativePairBegin() {
    const bool menu = gamestate::MenuOpen();
    xr::SetViewsMode(NativeMode());   // F1 / F3 in 3D (views_3d): what this pair is drawn as
    const bool world = PairWorld();   // (every pair: its begin is the next one's "last pair")
    const bool native = NativeView() && (!menu || MenuAs3d()) && world && replay::XrBegin();
    const bool out = menu && (native || (!xr::Running() && FlatVrMenuPanel()));   // native's panel, or FlatVR's
    g_menu3dNow.store(out);
    hud::SetPanelOut(out);
    {   // [hud] low_hp_vignette: its beat moves on once a pair, so both eyes get the same opacity (F4 on native only: vr/low_hp.h InView)
        lowhp::Where where;
        where.cameraOn = g_enabled.load();
        where.fullBody = FullBody();
        where.thirdPerson = ThirdPerson();
        where.native = native;
        where.inWorld = g_inWorld.load();
        float tanX = 0.0f, tanY = 0.0f;   // how wide the eyes' view is (the runtime's field of view): the vignette's size is an angle of it
        xr::Eyes eyes{};
        if (native && lowhp::Enabled() && NativeEyes(&eyes)) {
            tanX = lowhp::HalfTan(eyes.tanL[0], eyes.tanR[0], eyes.tanL[1], eyes.tanR[1]);
            tanY = lowhp::HalfTan(eyes.tanD[0], eyes.tanU[0], eyes.tanD[1], eyes.tanU[1]);
        }
        lowhp::Frame(where, menu, tanX, tanY);
    }
    return native;
}

// A pair (or, by turns, a pass) that takes more than 80 ms: one line, at most one a second, with where its
// time went - after bearstonem/d2r-pcvr's "Frame hitch" line (MIT), made of our own clocks (vr/frame_hitch.h).
// It tells a headset asleep, a stream stalled and a slow pass from each other, which the 10 s averages
// cannot. Microseconds: the whole cycle from the last one's end, the game's frame before this pair, XR begin
// (xrWaitFrame and the rest), the left pass, the right one, XR end; the runtime's shares come from xr.cpp's
// clocks. Only clock reads on the way - everything else is done when the pair was long.
void PairHitch(double cycle, double gap, double begin, double left, double right, double end, bool native, bool turns) {
    if (!(cycle > hitch::kLongUs)) return;
    static uint64_t lastTold = 0;
    if (!hitch::Due(cycle, GetTickCount64(), &lastTold)) return;
    hitch::Pair p;
    p.cycle = cycle; p.gap = gap; p.begin = begin; p.left = left; p.right = right; p.end = end;
    p.xr = native; p.turns = turns;
    if (native) {
        const xr::PairTiming x = xr::LastPairTiming();
        p.wait = x.wait; p.submit = x.submit; p.fence = x.fence; p.image = x.image;
    }
    char line[640];
    hitch::Format(line, sizeof line, p);
    Log(line);
}

// [debug] frame_trace (vr/frame_trace.h): at every pair's end, with the key on - the costs the plugin's probes added up since the last
// pair (the pose hook, the hit observer, the aim hooks, the server's lookups, the probe job, the haptic call, the log), the pair's own
// clocks, and the situation it was in (idle, walking, a swing, a hit landed). A pair over [debug] frame_trace_ms (15) is told, the worst of
// each second; every 10 s a summary says how the long pairs divide over the situations. Measures only: nothing of it reaches the game.
extern float g_heroPosNow[3];   // (defined with the hero's facing, below)
namespace ftrace {
frametrace::Recorder g_rec;
}
void ftraceSetOverMs(float ms) { ftrace::g_rec.SetOverMs((double)ms); }
void PairTrace(double cycle, double gap, double begin, double left, double right, double end, double cpu, bool native, bool turns) {
    if (!frametrace::On()) return;
    static float lastHero[3] = {};
    static bool haveHero = false;
    static uint32_t lastAv = 0;
    frametrace::Sample s;
    s.p.cycle = cycle; s.p.gap = gap; s.p.begin = begin; s.p.left = left; s.p.right = right; s.p.end = end; s.p.cpu = cpu;
    s.p.xr = native; s.p.turns = turns;
    if (native) s.p.wait = xr::LastPairTiming().wait;
    {   // the hero moved since the last pair (the camera's look-at is on him)
        const float dx = g_heroPosNow[0] - lastHero[0], dz = g_heroPosNow[2] - lastHero[2];
        s.p.moving = haveHero && dx * dx + dz * dz > 1e-4f;
        memcpy(lastHero, g_heroPosNow, sizeof lastHero);
        haveHero = true;
    }
    frametrace::Global().Take(&s.s);
    const frametrace::LastTimes last{frametrace::Global().Last(frametrace::kSweep), frametrace::Global().Last(frametrace::kPress),
                                     frametrace::Global().Last(frametrace::kHit)};
    s.ctx = frametrace::Classify(last, frametrace::NowNs(), s.p.moving);
    const uint32_t av = crash::AvTotal();
    s.caughtAv = av - lastAv;
    lastAv = av;
    frametrace::Recorder::Lines out;
    ftrace::g_rec.Pair(GetTickCount64(), s, &out);
    for (int i = 0; i < out.n; ++i) {
        if (g_ctx != nullptr) logasync::Info(g_ctx, out.text[i]);   // (not Log: that is a probe, and the line is not what is measured)
        crash::Mirror(out.text[i]);
    }
}

// [flatvr] frame_sync: a frame's picture on FlatVR's next frame (flatsync): vr/render/stereo/frame_sync.cpp

// Native OpenXR, eyes by turns (2026-10-11): one pass a game frame, the eyes in turn. The runtime's
// frame gets this eye fresh and the other one as it was last drawn, at the pose it was drawn with -
// the runtime turns it to the head. No replay: the right eye is a pass of its own, never made from
// the left one's lists (players' right eye had its depth to the left in "one pass, two pictures").
// Each eye at half the game's rate, one pass of work a frame. A panel open, or a view the headset
// does not take as eyes: the flat picture, one eye's camera kept.
uintptr_t DrawByTurns(int a) {
    static int eye = 1;
    static D2RVR_Shared held;
    static bool told = false;
    static double lastEnd = 0.0;   // the long-pass line (PairHitch)
    const double t0 = pairtime::UsNow();
    const D2RVR_Shared* live = link::Head();
    if (live) { memcpy(&held, live, sizeof held); g_pairShared.store(&held); }
    TurnTick();
    g_heldYaw.store(g_mouseYaw.load() + g_turnYaw.load());
    g_heldPitch.store(g_mousePitch.load());
    g_heldStickPitch.store(g_stickPitch.load());
    pairtime::g_drawThread.store(GetCurrentThreadId());
    frametrace::MarkDrawThread();   // [debug] frame_trace: the probes on this thread are told apart
    const bool native = NativePairBegin();
    const double tb = pairtime::UsNow();
    if (native && XrIntoShared(&held, live)) g_pairShared.store(&held);
    eye = native ? eye ^ 1 : 0;
    g_pairNow.store(native);   // its present goes to the runtime as this eye (XrPresent)
    SetEye(eye);
    d2rcam::Refresh();
    const uintptr_t r = OrigDrawGameScreen(a);
    const double tr = pairtime::UsNow();
    if (native) replay::XrEnd();   // this eye and the other's last to the runtime
    xr::PairOver();
    const double t2 = pairtime::UsNow();
    if (lastEnd > 0.0) PairHitch(t2 - lastEnd, t0 - lastEnd, tb - t0, tr - tb, 0.0, t2 - tr, native, true);
    if (lastEnd > 0.0) PairTrace(t2 - lastEnd, t0 - lastEnd, tb - t0, tr - tb, 0.0, t2 - tr, -1.0, native, true);   // [debug] frame_trace (the draw thread's CPU is not measured by turns)
    lastEnd = t2;
    if (live || native) g_pairShared.store(nullptr);
    g_pairNow.store(false);
    if (!told && native) { told = true; Log("vrcam: openxr - eyes by turns ON - one pass a game frame, left and right in turn"); }
    return r;
}

uintptr_t HookDrawGameScreen(int a) {
    static thread_local int depth = 0;
    static bool told = false, toldWhy = false;
    if (!depth && PairWanted() && NativeByTurns()) {
        ++depth;
        const uintptr_t r = DrawByTurns(a);
        --depth;
        return r;
    }
    if (depth || !PairWanted()) {
        if (!depth && !AfrOn()) flatsync::Wait();   // [flatvr] frame_sync: one picture a frame (mono, 3D from the depth) on FlatVR's frames too
        if (!depth) {   // no pair: a menu stays in the picture - but for FlatVR's panel (its eyes by turns)
            const bool out = gamestate::MenuOpen() && FlatVrMenuPanel();
            g_menu3dNow.store(out);
            hud::SetPanelOut(out);
        }
        if (!toldWhy && g_h.blit && AfrOn() && g_set.pairPerTick.load() && !FrameTimeFound()) {
            toldWhy = true;
            Log("vrcam: pair per game frame NOT possible - the frame time was not where expected (another build?); eyes go by turns");
        }
        if (!depth && frametrace::On()) {   // [debug] frame_trace: a plain game frame (no pair: a menu, FlatVR's own eyes) is a pass too
            static double lastEnd = 0.0;
            frametrace::MarkDrawThread();
            const double t0 = pairtime::UsNow();
            const uintptr_t plain = OrigDrawGameScreen(a);
            const double t1 = pairtime::UsNow();
            if (lastEnd > 0.0 && t0 - lastEnd < 5e6) PairTrace(t1 - lastEnd, t0 - lastEnd, 0.0, t1 - t0, 0.0, 0.0, -1.0, false, true);
            lastEnd = t1;
            return plain;
        }
        return OrigDrawGameScreen(a);
    }
    ++depth;
    static bool toldTry = false;   // before the first pair: if the game dies on it, the log says where
    if (!toldTry) { toldTry = true; Log("vrcam: pair per game frame - drawing the first pair"); crash::Mark("drawing the first stereo pair"); }
    flatsync::Wait();   // [flatvr] frame_sync: on FlatVR's next frame, with its head
    // One head for the pair: BodyWalk's block may change between the passes.
    static D2RVR_Shared held;
    const D2RVR_Shared* live = link::Head();
    if (live) { memcpy(&held, live, sizeof held); g_pairShared.store(&held); }
    TurnTick();   // the turn up to this very moment, not to the last timer tick
    g_heldYaw.store(g_mouseYaw.load() + g_turnYaw.load());
    g_heldPitch.store(g_mousePitch.load());
    g_heldStickPitch.store(g_stickPitch.load());
    g_pairNow.store(true);
    if (const D2RVR_Shared* hs = Shared())
        flog::Line("S,%u,%.4f,%.3f,%.3f,%.3f,%.3f,%u", flog::Pass(), flog::Dt(), g_heldYaw.load(), hs->headYawDeg, hs->headPitchDeg, hs->headRollDeg, hs->sampleStamp);

    static double lastEnd = 0.0, sumGap = 0.0, sumPass[2] = {}, sumPresent[2] = {}, sumEff[2] = {}, maxPair = 0.0, sumCpu = 0.0, sumWall = 0.0;
    const double t0 = pairtime::UsNow(), c0 = pairtime::CpuUs();
    pairtime::g_drawThread.store(GetCurrentThreadId());
    frametrace::MarkDrawThread();   // [debug] frame_trace: the probes on this thread are told apart
    if (lastEnd > 0.0) sumGap += t0 - lastEnd;
    pairtime::g_presentAt.store(0.0); pairtime::g_effSum.store(0.0);
    cbcmp::PairBegin();
    rtrace::PairBegin();
    const bool replayRight = replay::Wanted();
    replay::g_rightReplayed.store(replayRight);
    // Native OpenXR (first person, one pass two pictures): the runtime's frame - its wait paces
    // the pair, its views are the eyes the camera draws with (VrViewInner, VrProjInner). Before
    // the left pass is taken: the game's lists from other threads while it blocks stay out of it.
    // (a panel open - inventory, stash, trade, the pause menu: the flat picture, where the head looks;
    // with [openxr] menu_3d in F4 the pair still, the menu hung in front as a panel - NativePairBegin)
    // Two passes too (before the replay is on, 240 pairs into an area): the eyes then come with
    // each pass's present (XrPresent) - stereo at once, not 5 s of the flat picture eye after eye
    // (2026-10-10). Only with the world camera running, as the replay itself.
    const bool native = NativePairBegin();
    const double tb = pairtime::UsNow();   // XR begin is over (the long-pair line)
    if (native && XrIntoShared(&held, live)) g_pairShared.store(&held);   // the game's own head and hands for this pair
    if (replayRight) replay::BeginLeft();
    SetEye(0);
    d2rcam::Refresh();
    const uintptr_t leftR = OrigDrawGameScreen(a);
    cbcmp::PairMiddle();
    rtrace::PairMiddle();
    const double t1 = pairtime::UsNow();
    sumPass[0] += t1 - t0;
    if (const double pa = pairtime::g_presentAt.load(); pa > t0) sumPresent[0] += t1 - pa;
    sumEff[0] += pairtime::g_effSum.load();
    pairtime::g_presentAt.store(0.0); pairtime::g_effSum.store(0.0);

    float* dt = (float*)d2rsig::Addr(RVA_FRAME_TIME);
    float* rawDt = (float*)d2rsig::Addr(RVA_RAW_FRAME_TIME);
    const float keep = *dt, keepRaw = *rawDt;
    // [stereo] right_dt_ms: the right pass gets this sliver of time instead of none - effects
    // that skip a frame of no time (butterflies seen in the left eye only, 2026-10-04) draw then.
    const float rightDt = g_set.rightDtMs.load() * 0.001f;
    *dt = rightDt; *rawDt = rightDt;
    SetEye(1);
    d2rcam::Refresh();
    const uintptr_t r = replayRight && replay::RightFromLeft() ? leftR : OrigDrawGameScreen(a);
    const double tr = pairtime::UsNow();   // the right pass is over (the long-pair line)
    if (native) replay::XrEnd();   // both eyes to the runtime, or an empty frame if one went missing
    xr::PairOver();
    *dt = keep; *rawDt = keepRaw;
    cbcmp::PairEnd();
    rtrace::PairEnd();
    const double t2 = pairtime::UsNow();
    sumPass[1] += t2 - t1;
    if (const double pa = pairtime::g_presentAt.load(); pa > t1) sumPresent[1] += t2 - pa;
    sumEff[1] += pairtime::g_effSum.load();
    if (lastEnd > 0.0) PairHitch(t2 - lastEnd, t0 - lastEnd, tb - t0, t1 - tb, tr - t1, t2 - tr, native, false);
    if (lastEnd > 0.0) PairTrace(t2 - lastEnd, t0 - lastEnd, tb - t0, t1 - tb, tr - t1, t2 - tr, pairtime::CpuUs() - c0, native, false);   // [debug] frame_trace
    if (lastEnd > 0.0) maxPair = std::max(maxPair, t2 - lastEnd);
    lastEnd = t2;
    sumCpu += pairtime::CpuUs() - c0; sumWall += t2 - t0;

    // BodyWalk's head back before the pair is over: the timer thread, seeing no pair, would
    // otherwise read the held one (the game's own head in native: another zero) meanwhile
    if (live || native) g_pairShared.store(nullptr);
    g_pairNow.store(false);
    --depth;

    static ULONGLONG since = GetTickCount64();
    static uint32_t pairs = 0;
    ++pairs;
    if (!told) { told = true; Log("vrcam: pair per game frame ON - left eye, then right eye with the frame time held"); }
    if (const ULONGLONG now = GetTickCount64(); now - since >= 10000) {
        LogF("vrcam: %.1f pairs/s from one game frame each", pairs * 1000.0 / (double)(now - since));
        if (pairs) {
            const double k = 0.001 / pairs;   // us summed -> ms a pair
            LogF("vrcam: a pair, ms: game %.2f | left %.2f (effects %.2f, Present %.2f) | right %.2f (effects %.2f, Present %.2f) | longest %.1f",
                 sumGap * k, sumPass[0] * k, sumEff[0] * k, sumPresent[0] * k, sumPass[1] * k, sumEff[1] * k, sumPresent[1] * k,
                 maxPair * 0.001);
            LogF("vrcam: the two passes busy the game's thread %.0f%% of their time (%.2f ms CPU a pair; the rest waits - GPU, fences, locks)",
                 sumWall > 0.0 ? 100.0 * sumCpu / sumWall : 0.0, sumCpu * k);
        }
        if (replay::g_stepPairs) {
            const double k2 = 0.001 / replay::g_stepPairs;
            double* u = replay::g_stepUs;
            LogF("vrcam: replay steps, ms a pair: wait prev replay %.2f | wait before restore %.2f | hold left %.2f | wait last pair (effects) %.2f | camera %.2f | submit %.2f | present R %.2f | copy L %.2f | present L %.2f",
                 u[0] * k2, u[1] * k2, u[2] * k2, u[3] * k2, u[4] * k2, u[5] * k2, u[6] * k2, u[7] * k2, u[8] * k2);
            LogF("vrcam: replay submissions, a pair: %.3f ms in the ExecuteCommandLists loop (queue switches included) for %.1f lists in %.1f calls, %.1f signals after them, %.1f lists with no game call seen - [debug] replay_marks %d",
                 replay::g_eclUs * k2, replay::g_eclLists / (double)replay::g_stepPairs, replay::g_eclCalls / (double)replay::g_stepPairs,
                 replay::g_eclSignals / (double)replay::g_stepPairs, replay::g_eclUntagged / (double)replay::g_stepPairs, replay::g_marksOn.load());
            replay::g_eclUs = 0.0; replay::g_eclLists = replay::g_eclCalls = replay::g_eclSignals = replay::g_eclUntagged = 0;
            for (int i = 0; i < replay::kSteps; ++i) u[i] = 0.0;
            replay::g_stepPairs = 0;
        }
        sumGap = 0.0; maxPair = 0.0; sumCpu = 0.0; sumWall = 0.0;
        for (int e = 0; e < 2; ++e) sumPass[e] = sumPresent[e] = sumEff[e] = 0.0;
        since = now; pairs = 0;
    }
    return r;
}

// Keys, mouse, settings reload, bridge connect, XInput hooks: one 1 ms timer
// (a high-resolution waitable one - SetTimer does not go below 10 ms). What
// need not run that often goes by the clock, not by the tick count.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
DWORD g_updateThreadId = 0;

// The floor's pointer (FloorPointerTick), the map's direction (MapDirectionTick), the hand's inventory (HandInventoryTick): vr/hud/hud_ticks.cpp

// The crosshair's depth in VR F2 (CrosshairDepthTick): vr/hud/hud_ticks.cpp

// A key's press, once per press, only while the game is in front.
struct Key {
    int vk;
    bool was = false;
    bool Pressed(bool focus) {
        const bool now = focus && (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool edge = now && !was;
        was = now;
        return edge;
    }
};

DWORD WINAPI UpdateThread(void*) {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    const bool highRes = timer != nullptr;
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    LARGE_INTEGER due; due.QuadPart = -10000;   // 1 ms, then every 1 ms
    SetWaitableTimer(timer, &due, 1, nullptr, nullptr, FALSE);
    LogF("vrcam: 1 ms update timer (%s)", highRes ? "high resolution" : "plain - may run at the system's timer resolution");
    Key f12{VK_F12}, f11{VK_F11}, f10{VK_F10}, f9{VK_F9};
    Key fView[5] = {{VK_F1}, {VK_F2}, {VK_F3}, {VK_F4}, {VK_F5}};   // the views, as on the Home page
    ULONGLONG nextState = 0, nextSlow = 0;
    bool toldWorld = false;
    for (;;) {
        const DWORD w = MsgWaitForMultipleObjectsEx(1, &timer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (w != WAIT_OBJECT_0) {
            MSG m;
            bool quit = false;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) quit = true; else DispatchMessageW(&m); }
            if (quit) break;
            continue;
        }
        frametrace::Scope round(frametrace::kUpdateTick);   // [debug] frame_trace: one round of this thread; a long one is a hand that lags
        const ULONGLONG nowMs = GetTickCount64();
        const bool focus = GameFocused();
        if (f12.Pressed(focus)) Toggle();
        {   // the same from BodyWalk's mapping: the view actions, at the press
            static uint32_t was = 0;
            static link::Presses presses;
            const uint32_t held = link::ActionsHeld(presses), pressed = held & ~was;
            was = held;
            // The bits keep the D2R Bridge's old numbering: 3 was the VR mouse view, gone;
            // 4 the body, now F3; 5 the floor, now F4.
            static const int kVrOf[5] = {1, 2, 0, 4, 3};   // the actions: above, third, (flat F3), body = F4, table = F3
            const bool vr = g_set.platform.load() == 1;
            for (int k = 0; k < 5; ++k)
                if (pressed & (D2RVR_ACT_VIEW_1 << k)) SetView(vr ? kVrOf[k] : k + 1);
            if (pressed & D2RVR_ACT_VIEW_NEXT) Toggle();
            if (pressed & D2RVR_ACT_RECENTER) Recenter();
        }
        for (int k = 0; k < 5; ++k)
            if (fView[k].Pressed(focus) && k < ViewCount()) SetView(k + 1);
        if (f11.Pressed(focus)) Recenter();
        FlatVrCentreTick();
        if (f10.Pressed(focus)) {
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {   // Ctrl + F10: one frame of render targets, to find the interface's
                std::wstring path = g_iniPath;
                path.resize(path.find_last_of(L'\\') + 1);
                path += L"d2r_vr_uitrace.txt";
                Log(uitrace::Arm(path) ? "vrcam: recording one frame of render targets to d2r_vr_uitrace.txt (Ctrl+F10)"
                    : uitrace::Busy() ? "vrcam: a frame recording is already under way"
                                      : "vrcam: Ctrl+F10 frame trace off - [debug] ui_trace=1 (Settings > Debug) and restart the game");
            } else DumpSkeletons();
        }
        if (f9.Pressed(focus)) {
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {   // Ctrl + F9: memdiff, to find a flag by flipping it (Ctrl+Shift: start over)
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) memdiff::Reset(&Log);
                else {
                    std::wstring path = g_iniPath;
                    path.resize(path.find_last_of(L'\\') + 1);
                    path += L"d2r_vr_memdiff.txt";
                    memdiff::Step(g_base, path, &Log);
                }
            } else ToggleShooter();
        }
        // our camera while on, and for the stereo view from above while off
        d2rcam::SetEnabled(g_enabled.load() || (g_set.topStereo.load() && g_set.afr.load()));
        MouseTick();
        FlatPickWatch();   // [debug] flat_pick
        TurnTick();
        PushArms();
#if D2RVR_FIRST_PERSON
        {   // the toolbar on the hero's own forearm (the game's rig): left forearm = skeleton side 1
            const int a = hud::Anchor(0);
            float e[3], w[3], x[3];
            const bool ok = (a == 1 || a == 2) && skel::HeroForearm(a == 1 ? 1 : 0, e, w, x);
            hud::SetForearm(0, ok, e, w, x);
        }
#endif
        // From above (vrcam off) and from behind the toolbar and the map stay in
        // the picture, at their own size; on the hands only in first person.
        hud::SetClassicView(ClassicNow());
        MapDirectionTick();   // [hud] map_direction
        if (g_inWorld.load()) {   // the corner map's box follows the game's own interface scale: a window, render size or interface size of any kind
            static ULONGLONG nextScale = 0;
            if (nowMs >= nextScale) { nextScale = nowMs + 250; hud::SetGameInterfaceScale(GameUiScaleNow()); }
        }
        // ... and our projection's shape: in F4 the game sizes the map's box and tiles by it, not by the picture's (vr/minimap_box.h Stretch;
        // a wide window with [openxr] runtime_fov's centred frustum: the box 0.66 of the picture wide, the map left in the picture at its top)
        const float projY = g_projSy.load();
        hud::SetMapProjection(FullBody() && projY > 0.0f ? g_projSx.load() / projY : 0.0f);
        // first person without the body: the map at a size of its own, from its corner
        // ... and on the floor (F3) its own size, depth and place, as the toolbar's
        hud::SetPictureZoom(BarSizeNow(), InsideFree() ? g_set.mapZoomInside.load() : TableView() ? g_set.mapSizeFloor.load() : 1.0f);
        hud::SetPictureBarMoved(BarNearWanted());
        hud::SetPictureMapMoved(MapNearWanted());
        if (ClassicNow() && TableView()) hud::SetPictureMapOffset(g_set.mapXFloor.load(), g_set.mapYFloor.load());
        else hud::SetPictureMapOffset(0.0f, 0.0f);
        {   // [hud_*] bar_x / bar_y: the toolbar somewhere else on the screen
            float bx = 0.0f, by = 0.0f;
            if (ClassicNow()) BarOffsetNow(&bx, &by);
            hud::SetPictureBarOffset(bx, by);
        }
        fx::PlateWorldTick(nowMs);   // [hud] monster_bar_room
        fx::NoticeCardTick(nowMs);   // [hud] notice_card
        HandInventoryTick(nowMs);    // [openxr] hand_inventory
        if (g_inWorld.load() && (PlateWanted() || AlliesMoved() || plateworld::Active() || plateworld::LiftWanted() || PlateTurnsWanted())) {   // the plate's place: over the monster, it moves with it
            static ULONGLONG nextPlate = 0;
            if (nowMs >= nextPlate) { nextPlate = nowMs + 10; gamestate::PollPlate(); }
        }
        if (g_inWorld.load() && DurabilityMoved()) {   // the figure's rect, from the game's own widgets
            static ULONGLONG nextDur = 0;
            if (nowMs >= nextDur) { nextDur = nowMs + 100; gamestate::PollDurability(); }
        }
        hud::SetLabels(LabelsWanted() || replay::OverlaysWanted());   // eyes by turns: the game's labels laid back by the overlays, per eye
        {   // [openxr] menu_3d on FlatVR: is a FlatVR there that hangs a menu panel, where it hangs it, and
            // Game Link keeps FlatVR's screen and Head Lock as with no menu while it does
            hud::PollMenuReader();
            float w = 0.0f, d = 0.0f;
            xr::MenuPanelSize(&w, &d);
            int hand = 1;
            const bool laser = xr::LaserWanted(&hand);
            hud::SetMenuPlace(w, d, 0.0f, laser ? (uint32_t)(hand ? 2 : 1) : 0u, (uint32_t)(uintptr_t)g_swapWnd.load());
            link::SetMenuPanel(FlatVrMenuPanel());
        }
        // the fog leaves the interface alone; the floor's key never takes its text for the void. Flat: the sky and the cave ceiling
        // too, and through a menu, whose panel stays over the world there (EffectsPaused; from sajmonsoft's flat fork)
        const bool flatFx = g_set.platform.load() == 0 && (fx::OurSkyWanted() || g_arcaneSky.load() || fx::CeilWanted());
        hud::SetUiMask((fx::FogWanted() || fx::TableKeyWanted() || flatFx) && !LabelsWanted() && !replay::OverlaysWanted());
        hud::SetUiMaskInMenu(g_set.platform.load() == 0);
        SkyTick();
        flog::Tick(g_set.frameLog.load() || GetTickCount64() < flog::g_diagUntil.load());
        directinput::SetMenuNow(gamestate::MenuOpen() || !g_inWorld.load());   // [openxr] direct_input: the layer in force against the game's
        gamecmd::Tick();   // skills, potions, Alt... held in BodyWalk: pressed on the UI thread
        beltdrop::Tick();  // [hands] belt_drop_ground: a drop of a belt potion under way, on the UI thread
        PauseKeyTick();    // [direct] action:pause - the pause menu: Esc, tapped (F4 only)
        if (shieldsmite::Watching()) {   // [gameplay] shield_smite: a hard bash of the left shield casts the game's own Smite (and [debug] melee_boxes draws it)
            uintptr_t hero = (uintptr_t)g_heroUnit.load();
            if (!hero || !IsHeroUnit((void*)hero)) hero = (uintptr_t)g_heroSeen.load();
            if (hero && !IsHeroUnit((void*)hero)) hero = 0;
            if (!hero) {   // the SDK's handle of the local player, when it is the unit itself
                const uintptr_t p = (uintptr_t)gamestate::LocalPlayer();
                if (p > 0x10000 && IsHeroUnit((void*)p)) hero = p;
            }
            shieldsmite::Tick(FullBody() && g_enabled.load() && g_inWorld.load() && !gamestate::MenuOpen(), std::max(1.0f, ViewHeight()) / UserEyeHeightM(), hero);
        }
        if (const parrywire::Settings ps = parrywire::Get(); (ps.blockShow || ps.clawBlock) && viewgate::F4() && g_inWorld.load()) {   // [weight] parry_block_show / parry_claw_block: the game's block, felt
            static ULONGLONG parryAt = 0;
            const ULONGLONG t = GetTickCount64();
            if (t - parryAt >= 10) { parryAt = t; parrywire::Tick(HeroAnimMode(), skel::HeroClass(), t); }
        }
        {
            frametrace::Scope melee(frametrace::kMeleeTick);   // [debug] frame_trace: hit by touch's share of this round
            MeleeTouchTick();   // [gameplay] melee_touch
        }
#if D2RVR_FIRST_PERSON
        fx::PoseShareTick();   // [multiplayer] pose_share: our pose to the others, their places for the markers (F4 only: first-person builds)
#endif
        skillwheel::Tick();   // [openxr] skill_wheel: the controller's skill slots read on the UI thread (a no-op when off)
        lowhp::Tick(lowhp::Enabled() && g_enabled.load() && FullBody() && g_inWorld.load());   // [hud] low_hp_vignette: the hero's life, 5 a second (a no-op when off)
        merctint::Tick(merctint::Enabled() && g_enabled.load() && FullBody() && g_inWorld.load());   // [hud] merc_tint: the merc and its life, 5 a second (a no-op when off)
        handvitals::Tick(g_enabled.load() && g_inWorld.load());   // [hud] hand_vitals: the hero's life and mana, 5 a second (a no-op when off or outside F4)
        wriststatus::Tick(g_enabled.load() && g_inWorld.load());   // [hud] wrist_status: the hero's companions and effects, 5 a second (a no-op when off or outside F4)
        FloorPointerTick();   // F3: the mouse pointer on the game's ground, as the head moves
        CrosshairDepthTick();   // F2's crosshair: nearer as the camera looks down
        AfrQuietWhenOff();
        if (nowMs >= nextState) {   // what the hero holds, 5 times a second
            nextState = nowMs + 200;
            gamestate::Tick();
            // A weapon change rebuilds the hero's model: the matrix the facing was
            // read from stays where it was and reads yaw 0 (log 2026-10-04: "model
            // 0.0", "8.00 above the look-at"), and the doll stopped following the
            // hands until a search much later. Looked for again at once, as F11 does.
            {
                static uint32_t lastSet = 0xFFFFFFFFu, lastType = 0xFFFFFFFFu;
                const uint32_t set = gamestate::WeaponSet(), type = gamestate::WeaponType();
                if (lastType != 0xFFFFFFFFu && (set != lastSet || type != lastType)) {
                    g_facingReset.store(true);
                    LogF("vrcam: weapon changed (set %u, kind %u) - looking for the hero's facing again", set, type);
                }
                lastSet = set; lastType = type;
            }
            // the interface is hidden in a game area, whatever the view, with no panel open;
            // the main menu and panels (inventory, trade...) keep all of it
            hud::SetMenuOpen(gamestate::MenuOpen() || !g_inWorld.load());
            hud::SetPanelNoBar(g_set.npcDialogNoBar.load() && g_inWorld.load() && gamestate::NpcDialogOnly());
            // The mouse pointer in the headset: in menus, at a trader, from above and
            // from behind it is wanted (it aims there, or picks items); in first person
            // it aims otherwise and the pointer only gets in the way.
            {
                const bool menu = MenuFlat() || !g_inWorld.load();   // (a menu as a panel: first person goes on round it)
                const bool firstPerson = g_enabled.load() && !ThirdPerson() && FullBody();   // the mouse views keep it: it is the crosshair
                hud::SetPointer((menu || !firstPerson) && !CrosshairHidden() ? 1u : 2u);   // [hud_third] crosshair=0: none in F2
                // its depth over the ground: F1 (from above) and F2 (behind), not over a panel
                // By the key, F1 or F2 (ViewNow): F1 whether our camera or the game's own is up there, and
                // never by ThirdPerson() - VR F1 in perspective runs as g_view 2, it read as F2 and took
                // [hud_third]'s zeros ("whatever tilt I set, nothing changes", 2026-10-06)
                const int key = ViewNow();
                const int view = menu ? -1 : key == 1 ? 0 : key == 2 ? 1 : -1;
                static int toldView = -2;
                if (view != toldView) {
                    toldView = view;
                    LogF("vrcam: mouse pointer depth for FlatVR: %s (top %.1f middle %.1f bottom %.1f)", view == 0 ? "F1" : view == 1 ? "F2" : "none - on the screen",
                         view < 0 ? 0.0f : g_set.pointerAt[view][0].load(), view < 0 ? 0.0f : g_set.pointerAt[view][1].load(),
                         view < 0 ? 0.0f : g_set.pointerAt[view][2].load());
                }
                // the curve through the top, middle and bottom depths: FlatVR takes a + b v + c v^2 (v 0 top, 1 bottom)
                // VR F2 with mouse look: the pointer is the crosshair, held in the middle - one depth, its own.
                const bool cross = view == 1 && g_set.platform.load() == 1 && ShooterActive() && KeyMoveMode();
                static int toldCross = -1;
                if ((int)cross != toldCross) {
                    toldCross = cross;
                    if (cross) LogF("vrcam: crosshair depth for FlatVR: %.1f ([hud_third] crosshair_depth)", g_set.crosshairDepth.load());
                }
                if (FloorPointerAuto()) {}   // F3: on the ground by itself, FloorPointerTick
                else if (view < 0) hud::SetPointerDepth(0.0f, 0.0f, 0.0f, 1.0f);
                else if (cross) {}   // F2's crosshair: CrosshairDepthTick, with the head's look down
                else {
                    const float T = g_set.pointerAt[view][0].load() * 0.005f, M = g_set.pointerAt[view][1].load() * 0.005f,
                                B = g_set.pointerAt[view][2].load() * 0.005f;
                    hud::SetPointerDepth(T, 4.0f * M - 3.0f * T - B, 2.0f * B - 4.0f * M + 2.0f * T, g_set.pointerTopSize[view].load() * 0.01f);
                }
            }
        }
        if (nowMs >= nextSlow) {   // twice a second
            nextSlow = nowMs + 500;
            HealthTick(nowMs);   // [debug] health: every 10 s
            static ULONGLONG nextCrashKeep = 0;
            if (nowMs >= nextCrashKeep) { nextCrashKeep = nowMs + 2000; crash::Keep(); }
            FlatVr3DTick();
            xr::FollowLink();
            fx::dlsseyes::Install();
            prevscan::Tick();
            const int ringsWas = g_set.rings.load();
            const float modelRadiusWas = g_set.modelRadius.load();
            const float bigRadiusWas = g_set.bigRadius.load();
            const bool iniChanged = ReloadIfChanged();
            if (iniChanged) {
                g_gen.fetch_add(1);
                // Only when they changed (2026-10-13): SetModelRadius re-sorts the models every frame for
                // 500 ms, and every save of the ini - any setting, a profile started - began that again.
                // A first apply still waiting for its hook stays wanted.
                if (g_set.rings.load() != ringsWas || g_set.modelRadius.load() != modelRadiusWas) g_renderDirty.store(true);
                if (g_set.bigRadius.load() != bigRadiusWas) g_bigDirty.store(true);
                g_mouseLookOn.store(MouseLookForView());
            }
            FollowMode();
            d2rsig::Resolve(g_ctx);   // what waits for its page to be decrypted
            GameCodeTick(iniChanged);
            if (!AllHooksIn()) InstallHooks();
            InstallClientSize(IniB(L"render", L"vr_resolution_ui", true));   // [render] vr_resolution_ui (live; the hook goes in once)
            ApplyRender();
            ApplyPools();
            HookGameWindow();
            UpdateFovFromFlatVR();
            HookXInput();
            HookXInputSet();   // [debug] frame_trace only
            ReportXInputUse();
            fx::TryRegister();
            InstallBiomeHook();
            roofhook::Install();   // only once [render] keep_roofs asks for it
            engitem::weather::Install();   // only once [sky] weather_first_person asks for it
            // only once [render] roofs_two_sided asks for it (asked again at each material build) - or [sky] game_sky, whose models' builds
            // are told to the log (Serg on 0.189: every act's dome drew the first act's sky - what does the game build for each? read only)
            if (g_set.roofsTwoSided.load() || g_set.gameSky.load()) {
                roofsides::Watch(+[](const char* model) {   // (our models: the game's test models, and the three unused ones the ceilings took)
                    return std::strstr(model, "data/hd/env/model/tests/") || std::strstr(model, "test_asset") || std::strstr(model, "_old_lod") ||
                           std::strstr(model, "/_pit02");
                });
                roofsides::Install(&Log, +[]() { return g_set.roofsTwoSided.load(std::memory_order_relaxed) && g_set.platform.load() == 1; });
            }
            prophook::Install();   // only once [stereo] props_once_a_pair asks for it
            cavelight::Install();   // only once [cave_light] enabled asks for it
            InstallLabelHooks();
            InstallLabelArea();   // only once [hud] labels_whole_eye asks for it
            mobbars::Install(g_ctx);   // only once [hud] ordinary_mob_bars asks for it
            InstallKeyMoveHook();
            InstallStickHook();
            InstallMapClickHook();
            InstallMouseGroundHook();   // [input] flat_aim_ground (flat only)
            PortalOvalTick();           // [input] flat_portal_no_oval (flat), [render] portal_no_oval (VR)
            InstallRoomHeightHook();    // [render] floors_stay, once asked for
            straywalk::Install();   // the guard on the game's command funnel, once block_stray_walk asks for it
            moveattack::Install();   // [gameplay] move_while_attacking: the server's attack gate and frame trigger, the controller's update - once asked for
            shottrace::Install();   // [debug] shot_trace: the game's click-command sender, once asked for
            InstallSpellAimHooks();
            InstallSlotSkillHook();   // [hands] throw_auto_skill
            haptics::Install();   // only once [openxr] haptics asks for it
            stowedblock::Install();   // only once [hands] shield_stow_no_block asks for it
            spatialaudio::Install(g_inWorld.load());   // only once [audio] head_follow asks for it (and at the plugin's load, below)
            soundtrace::Install();                     // [debug] sound_trace: only once it is ticked
            if (meleetouch::Wanted() || meleetouch::ThrowWanted() || meleetouch::BowWanted() || g_set.leapByGaze.load() || meleetouch::StatusWanted()) meleetouch::Install();   // only once melee_touch, a throw's auto-aim, a bow's pitch, leap_by_gaze or the wrist status asks for it
            engitem::InstallHook();   // only once belt_models or [debug] engine_unit asks for it
            beltglow::Install();      // only once [hands] belt_inner_glow or belt_empty_bottle (darker) asks for it
            owneffects::Install();    // [render] dim_own_effects: its two hooks only LOOK at the creation of overlays (in whether it is ticked or not, so a buff cast before it is ticked is known); a status line while it is on
            headshadow::Install();    // [arms] head_shadow: the model draw collector and Granny's skinning composite hooked once it is ticked (and the world-matrix hook asked for); a line when its state changes
            {   // native's laser, or FlatVR's on the menu panel
                const bool laser = xr::LaserOn() || FlatVrLaserWanted();
                gamestate::SetLaserPanels(laser);   // its extra modal panels count as menus only with the laser
                if (laser) panelinput::Install();   // the controller layout kept under the laser's mouse
            }
            HookCursor();
            CrosshairTick();
            FlatPadTick();
            ApplyBackgroundSleep();
            static bool toldLayer = false;
            if (!toldLayer && hud::LayerFound()) { toldLayer = true; Log("vrcam: the interface's layer found (R16G16B16A16_FLOAT cleared to 0,0,0,1) - [hud] hide works on it"); }
            if (const bool world = d2rcam::InWorld(); world != toldWorld) {
                toldWorld = world;
                Log(world ? "vrcam: the game's world camera is up" : "vrcam: no world camera (menu)");
                meleetouch::SetInWorld(world);   // a game left is forgotten: its pointer would be freed memory in the next game
            }
        }
    }
    if (timer) CloseHandle(timer);
    return 0;
}

}  // namespace vrc
