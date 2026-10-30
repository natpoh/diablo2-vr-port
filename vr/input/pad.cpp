// vr/input/pad.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M10); what it gives: vr/input/input.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {
// (vrcam.cpp; declared here only: their defaults are their definitions')
bool HandRay(float* o, float* d, float* dRaw = nullptr);
spellmodel::Out SpellModelAim();

// XInput: turn the left stick by the camera yaw
using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
struct XHook { const wchar_t* dll; const char* name; int ordinal; XInputGetStateFn orig; bool done; std::atomic<uint32_t> calls; };
XHook g_xhooks[] = {
    {L"xinput1_4.dll",   "XInputGetState",   0,   nullptr, false, {0}},
    {L"xinput1_4.dll",   "XInputGetStateEx", 100, nullptr, false, {0}},
    {L"xinput1_3.dll",   "XInputGetState",   0,   nullptr, false, {0}},
    {L"xinput1_3.dll",   "XInputGetStateEx", 100, nullptr, false, {0}},
    {L"xinput9_1_0.dll", "XInputGetState",   0,   nullptr, false, {0}},
};

// What the pad does while shooting, for the log: every change of buttons or
// triggers, and four times a second while the bow aim holds the stick - the
// stick before us, after the bow, and as the game finally gets it.
struct PadTrace { WORD buttons = 0; BYTE lt = 0, rt = 0; ULONGLONG lastAim = 0; };
PadTrace g_padTrace;

// [debug] bow_latency: the hero's model heading as the camera code last read it (deg, with the steady-clock second it was read at), and the game's pad A as the
// input hook last made it (the seconds of its last press and release).
std::atomic<float> g_latYaw{0.0f};
std::atomic<double> g_latYawS{0.0}, g_latPadDownS{0.0}, g_latPadUpS{0.0};
void BowLatencyPad(bool a) {
    static bool was = false;
    if (!g_set.bowLatency.load(std::memory_order_relaxed) || a == was) { was = a; return; }
    was = a;
    (a ? g_latPadDownS : g_latPadUpS).store(throwborn::NowS());
}

// A stick in our view (x right, y ahead, length <= 1) made the same stick on
// the game's screen - the convention of XInput after the game's dead zone, and
// of the keyboard move 0x8A960 (x right, y up): the camera's yaw taken in, or
// with [stick] exact the screen's own axes. False = left as it was (turning
// off, no view yet, or no length).
bool ViewStickToGame(float* px, float* py) {
    if (!g_set.stickRotate.load()) return false;
    const float x = *px, y = *py;
    const float mag = sqrtf(x*x + y*y);
    if (mag < 1e-4f) return false;
    if (g_set.stickExact.load()) {
        // Stick in our view -> direction on the ground -> that direction on the game's screen.
        AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
        if (!f.valid) return false;
        float w[3];
        for (int i = 0; i < 3; ++i) w[i] = x * f.camR[i] + y * f.camF[i];
        float sx = w[0]*f.scrR[0] + w[1]*f.scrR[1] + w[2]*f.scrR[2];
        float sy = w[0]*f.scrU[0] + w[1]*f.scrU[1] + w[2]*f.scrU[2];
        const float sm = sqrtf(sx*sx + sy*sy);
        if (sm < 1e-6f) return false;
        *px = sx * f.mirX * mag / sm; *py = sy * f.mirY * mag / sm;
        return true;
    }
    return walkpath::TurnByYaw(px, py, g_camYaw.load(), g_set.stickSign.load(), g_set.stickAngleScale.load(), g_set.stickSquash.load());
}

bool DirectWalkNow();
bool StickHookIn();
std::atomic<ULONGLONG> g_pad0At{0};   // pad 0 last answered the game's poll (XDetour): another pad's report is not kept meanwhile (padpresence::KeepsReport)
void TurnStick(XINPUT_STATE* s, bool fromBodyWalk, DWORD padIndex) {
    if (!s || !g_enabled.load()) return;
    // [hands] bow_draw: the string hand's trigger is the draw's while the string is held - BodyWalk's A for it stays from the game
    // (with direct input the layout never gets it: directinput::Take). The draw's own press comes after this, as the release.
    if (bowdrawwire::Owned()) s->Gamepad.wButtons &= ~XINPUT_GAMEPAD_A;
    const XINPUT_GAMEPAD in = s->Gamepad;
    // What is kept (held buttons, the right stick, the log) only from BodyWalk's pad while it is live - and, with none, from pad 0 while it answers:
    // the game polls four pads, and another pad's empty report must not put the right stick back to 0 (vr/pad_presence.h KeepsReport)
    const bool own = padpresence::KeepsReport(fromBodyWalk, padIndex, GetTickCount64(), g_padMirrorOkAt.load(), g_pad0At.load());
    if (own) {
        g_aHeld.store((in.wButtons & XINPUT_GAMEPAD_A) != 0 && !gamestate::MenuOpen());
        // (the right trigger too: the game's RT slot - [openxr] skill_rt_a's trigger, BodyWalk's RT - vr/pad_skill.h)
        g_skillHeld.store(padskill::Pressed(in.wButtons, in.bRightTrigger) && !gamestate::MenuOpen());
    }
    const bool lt = in.bLeftTrigger >= 128, rt = in.bRightTrigger >= 128;
    if (lt && walkpath::PushedRaw(in.sThumbLX, in.sThumbLY)) {
        // The user's report (2026-10-15): the left trigger held and the left stick pushed drank a belt potion, from column 1 whatever the way. Nothing in the
        // plugin makes a belt press out of a stick (a belt command is pressed only by the hand's reach and by BodyWalk's actions, both logged as "game command
        // 0x17.."): if this line is there and no "game command 0x17" line is, the game's own pad bindings did it from the pad it was given. Rate-limited.
        static std::atomic<int> said{0};
        static std::atomic<ULONGLONG> lastAt{0};
        const ULONGLONG now = GetTickCount64();
        if (said.load() < 12 || now - lastAt.load() > 10000) {
            said.fetch_add(1);
            lastAt.store(now);
            LogF("pad diag: the left trigger (%u) held with the left stick pushed (%d %d) - buttons %04X, view %d (%s), the pad is %s, the walking hook %s the stick, direct input %s",
                 in.bLeftTrigger, in.sThumbLX, in.sThumbLY, in.wButtons, ViewNow(), g_set.platform.load() == 1 ? "VR" : "flat",
                 fromBodyWalk ? (directinput::Active() ? "the plugin's own" : "BodyWalk's, or the plugin's walk-only (F1-F3)") : "the real XInput pad",
                 DirectWalkNow() && StickHookIn() ? "keeps" : "does not keep", directinput::Active() ? "feeding" : "not feeding");
        }
    }
    if (own && (in.wButtons != g_padTrace.buttons || lt != (g_padTrace.lt >= 128) || rt != (g_padTrace.rt >= 128))) {
        LogF("pad: buttons %04X LT %u RT %u stick %d %d", in.wButtons, in.bLeftTrigger, in.bRightTrigger, in.sThumbLX, in.sThumbLY);
        g_padTrace.buttons = in.wButtons; g_padTrace.lt = in.bLeftTrigger; g_padTrace.rt = in.bRightTrigger;
    }
    // A menu (inventory, trade, stash...) gets the pad as it is: the left stick
    // moves its cursor on the screen, so turning it by the camera's yaw sent the
    // cursor the wrong way, and the right stick is the menu's too.
    if (gamestate::MenuOpen()) { if (own) { g_rightX.store(0.0f); g_rightY.store(0.0f); } return; }
    AimByMouse(s);
    AimByTurn(s);
    const bool aimed = BowStick(s);
    struct Out { const XINPUT_GAMEPAD in, bow; XINPUT_STATE* s; bool aimed; ~Out() {
        if (!aimed || GetTickCount64() - g_padTrace.lastAim < 250) return;
        g_padTrace.lastAim = GetTickCount64();
        LogF("bow aim: stick in %d %d, hand %d %d, to the game %d %d (camera yaw %.1f)", in.sThumbLX, in.sThumbLY,
             bow.sThumbLX, bow.sThumbLY, s->Gamepad.sThumbLX, s->Gamepad.sThumbLY, g_camYaw.load());
    } } out{in, s->Gamepad, s, aimed};
    if (g_set.rightTurn.load()) {
        // The right stick is ours while on: it turns the body, the game never sees it.
        if (own) { g_rightX.store(s->Gamepad.sThumbRX / 32767.0f); g_rightY.store(s->Gamepad.sThumbRY / 32767.0f); }
        s->Gamepad.sThumbRX = 0; s->Gamepad.sThumbRY = 0;
    }
    // Walking straight from BodyWalk (HookStickGet / HookKeyMove): the pad's left
    // stick is kept from the game in the world - it would walk twice, and a stick
    // pushed flips the game's interface to the controller.
    if (DirectWalkNow() && StickHookIn()) { s->Gamepad.sThumbLX = 0; s->Gamepad.sThumbLY = 0; return; }
    float x = s->Gamepad.sThumbLX / 32767.0f, y = s->Gamepad.sThumbLY / 32767.0f;
    const SHORT inX = s->Gamepad.sThumbLX, inY = s->Gamepad.sThumbLY;
    const bool turned = ViewStickToGame(&x, &y);
    if (turned) {
        s->Gamepad.sThumbLX = (SHORT)std::clamp(x * 32767.0f, -32768.0f, 32767.0f);
        s->Gamepad.sThumbLY = (SHORT)std::clamp(y * 32767.0f, -32768.0f, 32767.0f);
    }
    // Flat with a real pad (a player, 2026-10-24: "the left stick seems to work absolute rather than relative to what direction I'm facing"): what the
    // stick was and what the game got, every 2 s while it is pushed - turned or not, by which way, in which view, and the game's UI mode then.
    if (g_set.platform.load() == 0 && !fromBodyWalk && walkpath::PushedRaw(inX, inY)) {
        static ULONGLONG lastAt = 0;
        if (const ULONGLONG now = GetTickCount64(); now - lastAt >= 2000) {
            lastAt = now;
            LogF("flat pad: the left stick %d %d -> the game %d %d (%s; view %d, camera yaw %.1f, [stick] rotate %d exact %d)", inX, inY, s->Gamepad.sThumbLX,
                 s->Gamepad.sThumbLY, turned ? "turned by the camera" : "NOT turned", ViewNow(), g_camYaw.load(), g_set.stickRotate.load() ? 1 : 0, g_set.stickExact.load() ? 1 : 0);
        }
    }
}

// BodyWalk's pad, read straight from BodyWalk (pad_mirror_shared.h): the
// report it would hand the virtual Xbox pad, so the game needs neither the
// virtual device nor its driver. Taken for the first pad while BodyWalk keeps
// writing it (its counter moved in the last second), whatever is plugged in.
// Read from the XInput hooks (VR) and from the keyboard move hook on the
// game's thread (flat), so the mapping and the counter's watch are atomics.
std::atomic<const BWPadMirror*> g_padMirror{nullptr};

// BodyWalk's own report, while it is moving (its counter moved in the last second) and a version we know. `evenIfOff`: read it whatever
// [input] bodywalk_pad says (direct mode's walking, [openxr] direct_bodywalk_walk: the report is the only way its stick comes in there).
bool BodyWalkReport(BWPadMirror* out, bool evenIfOff) {
    if (!evenIfOff && !g_set.bodywalkPad.load()) return false;
    const BWPadMirror* p = g_padMirror.load();
    if (!p) {
        static std::atomic<ULONGLONG> lastTry{0};
        const ULONGLONG now = GetTickCount64();
        ULONGLONG was = lastTry.load();
        if (now - was < 1000 || !lastTry.compare_exchange_strong(was, now)) return false;   // one opener a second
        if (HANDLE m = OpenFileMappingW(FILE_MAP_READ, FALSE, BW_PAD_MIRROR_NAME)) {
            p = (const BWPadMirror*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, sizeof(BWPadMirror));
            if (!p) CloseHandle(m);   // else kept: see KeepBlockName
            if (p) { g_padMirror.store(p); Log("vrcam: BodyWalk's pad is read directly - no virtual pad needed"); }
        }
        if (!p) return false;
    }
    static std::atomic<uint32_t> seen{0};
    static std::atomic<ULONGLONG> movedAt{0};
    const BWPadMirror m = *p;
    if (m.counter != seen.load()) { seen.store(m.counter); movedAt.store(GetTickCount64()); }
    if (m.version != BW_PAD_MIRROR_VERSION || GetTickCount64() - movedAt.load() > 1000) return false;
    *out = m;
    // BodyWalk's Output "Game Link": its walk is not on this report but in Game Link's Walk block - put back on the left stick as its
    // Xbox output would have had it (vr/walk_path.h WithLinkWalk), so every reader below takes it as before. Said once per change.
    float wx = 0.0f, wy = 0.0f;
    const bool linkWalk = link::Walk(&wx, &wy);
    walkpath::WithLinkWalk(&out->thumb_lx, &out->thumb_ly, linkWalk, wx, wy);
    static std::atomic<int> saidLinkWalk{-1}, linkWalkLines{0};
    if (saidLinkWalk.exchange(linkWalk ? 1 : 0) != (linkWalk ? 1 : 0) && linkWalkLines.fetch_add(1) < 20)
        Log(linkWalk ? "vrcam: BodyWalk's walk comes through Game Link (its Output \"Game Link\"): onto its pad's left stick, as its Xbox output's"
                     : "vrcam: BodyWalk's walk - not through Game Link (its Output is another, or it stopped): its pad's left stick as it is");
    return true;
}

// Direct mode ([openxr] direct_input, F4): what became of BodyWalk's walking stick, said once per change - combined, ignored, dropped and why
// (vr/walk_path.h StateLog: the first 40 changes, then at most one line in 5 s). The pad's pollers and the walking hooks all come through here.
std::atomic<uint8_t> g_walkState{(uint8_t)walkpath::State::NotDirect};
std::atomic<uint64_t> g_walkRaw{0};   // the two raw left sticks of the last read (controller x y, BodyWalk x y), 16 bits each, for the walking hook's log line
void NoteWalk(const walkpath::Out& r, const walkpath::Inputs& in) {
    g_walkState.store((uint8_t)r.state);
    g_walkRaw.store((uint64_t)(uint16_t)in.ownLX | ((uint64_t)(uint16_t)in.ownLY << 16) | ((uint64_t)(uint16_t)in.bwLX << 32) | ((uint64_t)(uint16_t)in.bwLY << 48));
    static SRWLOCK lock = SRWLOCK_INIT;
    static walkpath::StateLog log;
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    walkpath::State st = walkpath::State::NotDirect;
    uint32_t changes = 0;
    const bool say = log.Step(r.state, GetTickCount64(), &st, &changes);
    ReleaseSRWLockExclusive(&lock);
    if (say)
        LogF("vrcam: BodyWalk's walk - %s (BodyWalk's stick %d %d, the controller's %d %d%s)", walkpath::StateName(st), in.bwLX, in.bwLY, in.ownLX, in.ownLY,
             changes > 1 ? "; several changes since the last line" : "");
}

// BodyWalk is running: its Game Link is alive, or its pad report moves. Asked at most five times a second (the pad is polled much more often).
bool BodyWalkRunning() {
    static std::atomic<ULONGLONG> checkedAt{0};
    static std::atomic<bool> running{false};
    const ULONGLONG now = GetTickCount64();
    if (now - checkedAt.load() < 200) return running.load();
    checkedAt.store(now);
    BWPadMirror bw{};
    const bool r = link::HostAlive() || BodyWalkReport(&bw, true);
    running.store(r);
    return r;
}

bool ChatOpen();

// What became of the walk in VR F1-F3, said once per change (vr/walk_flat.h VerdictLog).
void NoteWalkFlat(walkflat::Verdict v) {
    static SRWLOCK lock = SRWLOCK_INIT;
    static walkflat::VerdictLog log;
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    walkflat::Verdict say = v;
    uint32_t changes = 0;
    const bool due = log.Step(v, GetTickCount64(), &say, &changes);
    ReleaseSRWLockExclusive(&lock);
    if (due) LogF("vrcam: walking in VR F1-F3 - %s%s", walkflat::Name(say), changes > 1 ? " (several changes since the last line)" : "");
}

// VR F1, F2, F3 without BodyWalk ([direct] walk_flat_views, vr/walk_flat.h): the controller's left stick for the game's walking code - the
// report has that stick and nothing else. It goes the way BodyWalk's stick goes in these views: the pad the game polls (TurnStick turns it
// into the game's frame while our camera is on, F2 and the 3D F1 and F3; the game's own camera, F1 flat, takes it as it is: up is up on the
// screen) and the walking hooks' vector (BodyWalkWalk). False = nothing of ours: BodyWalk's pad (or none) as before.
// ... and the whole controller where [direct] flat_controller is on: the F4 layout's pad (directinput::FlatPadRead - nothing unless every condition holds, BodyWalk
// not running among them) with the left stick the walk's. False = nothing of ours: the walk alone (WalkFlatRead), or BodyWalk's pad / the real one as before.
// Game Link direct ([direct] flatvr_controls, FlatRawTick): BodyWalk's raw controllers drive the plugin's own layout through Game Link, and
// BodyWalk's gamepad report is no input at all - neither its buttons nor its stick; its walk comes from Game Link's Walk block (link::Walk:
// BodyWalk's Output "Game Link"). Serg, 2026-10-17: "BodyWalk будет использоваться напрямую, без эмуляции джойстика, и маппинг тоже".
bool GameLinkDirect() { return directinput::FedByLink(); }
// BodyWalk's gamepad is in charge of the controls in VR F1-F3 (walkflat's "BodyWalk running"): BodyWalk runs, and not Game Link direct.
bool BodyWalkPadInCharge() { return BodyWalkRunning() && !GameLinkDirect(); }
// Game Link direct: BodyWalk's walk (Game Link's Walk block) mixed into the controller's left stick the way direct mode mixes it
// (vr/walk_path.h Combine: each past the game's dead zone, the stronger wins when they pull apart). Nothing to mix: the stick as it is.
void MixLinkWalk(int16_t* lx, int16_t* ly) {
    float wx = 0.0f, wy = 0.0f;
    if (!GameLinkDirect() || !directinput::BodyWalkWalks() || !link::Walk(&wx, &wy)) return;
    const walkpath::Mixed m = walkpath::Combine(walkpath::FromPad(*lx, *ly), walkpath::FromPad(walkpath::LinkAxis(wx), walkpath::LinkAxis(wy)));
    *lx = walkpath::Axis(m.out.x);
    *ly = walkpath::Axis(m.out.y);
}

bool FlatControllerRead(BWPadMirror* out) {
    directinput::FlatPad p{};
    static std::atomic<int> told{-1};
    if (!directinput::FlatPadRead(&p)) {
        if (directinput::FlatControllerOn() && viewgate::Flat() && directinput::On() && told.exchange(0) == 1)
            Log("vrcam: VR F1-F3 - the plugin's own controller stands down (BodyWalk is running, or no headset frames): its pad as before");
        return false;
    }
    if (told.exchange(1) != 1)
        Log("vrcam: VR F1-F3, no BodyWalk running - the plugin's own controller is the game's pad (the F4 layout: right trigger attacks, A picks up, B / X / Y skills, "
            "triggers, sticks, menu button; the left stick walks, the left trigger + a flick of a stick drinks a belt potion)");
    *out = BWPadMirror{};
    out->version = BW_PAD_MIRROR_VERSION;
    out->counter = p.counter;
    out->buttons = p.buttons;
    out->left_trigger = p.lt; out->right_trigger = p.rt;
    out->thumb_lx = p.lx; out->thumb_ly = p.ly; out->thumb_rx = p.rx; out->thumb_ry = p.ry;
    MixLinkWalk(&out->thumb_lx, &out->thumb_ly);   // Game Link direct: BodyWalk's walk with the controller's stick
    g_walkState.store((uint8_t)walkpath::State::FlatOwn);
    g_walkRaw.store((uint64_t)(uint16_t)p.lx | ((uint64_t)(uint16_t)p.ly << 16));
    return true;
}

bool WalkFlatRead(BWPadMirror* out) {
    directinput::WalkOut w{};
    walkflat::Inputs in;
    if (!directinput::WalkRead(&w, &in)) return false;   // the key off, direct input off or another view: the common poll ends here
    in.bodyWalkRunning = BodyWalkPadInCharge();
    in.inWorld = g_inWorld.load();
    in.menuOpen = gamestate::MenuOpen();
    in.chatOpen = ChatOpen();
    const walkflat::Verdict v = walkflat::Decide(in);
    NoteWalkFlat(v);
    if (v != walkflat::Verdict::Walk) return false;
    *out = BWPadMirror{};
    out->version = BW_PAD_MIRROR_VERSION;
    out->counter = w.counter;
    out->thumb_lx = w.lx; out->thumb_ly = w.ly;
    MixLinkWalk(&out->thumb_lx, &out->thumb_ly);   // Game Link direct: BodyWalk's walk with the controller's stick
    g_walkState.store((uint8_t)walkpath::State::FlatOwn);
    g_walkRaw.store((uint64_t)(uint16_t)w.lx | ((uint64_t)(uint16_t)w.ly << 16));
    return true;
}

// Belt potions in VR F1-F3 ([direct] belt_flick_flat_views, vr/belt_flick.h): directinput::Feed found the left trigger held and a flick of a stick; this
// is the last word. The same conditions as the walk (no BodyWalk running, a game on, no panel or chat open, headset frames arriving) - then the game's
// own belt key command through gamecmd::Tap, the very press the hand that drinks a potion from the belt makes (0x17 .. 0x1A: Belt 1 .. 4).
void BeltFlickSink(uint32_t column, int stick, int dir) {
    if (column > 3) return;
    walkflat::Inputs in;
    directinput::FlatFacts(&in);
    in.keyOn = directinput::BeltFlickOn();
    in.bodyWalkRunning = BodyWalkPadInCharge();
    in.inWorld = g_inWorld.load();
    in.menuOpen = gamestate::MenuOpen();
    in.chatOpen = ChatOpen();
    const walkflat::Verdict v = walkflat::Decide(in);
    if (v != walkflat::Verdict::Walk) {   // (Walk = nothing in the way)
        static std::atomic<int> refused{0};
        if (refused.fetch_add(1) < 10)
            LogF("vrcam: belt flick - the %s stick flicked %s with the left trigger held, NOT taken: %s", stick ? "right" : "left",
                 beltflick::DirName((beltflick::Dir)dir), walkflat::Name(v));
        return;
    }
    gamecmd::Tap(0x17u + column);
    LogF("vrcam: belt flick - the left trigger held, the %s stick flicked %s: belt %u (VR F1-F3, the game's own belt key command 0x%X)", stick ? "right" : "left",
         beltflick::DirName((beltflick::Dir)dir), column + 1, 0x17u + column);
}

// A control was pressed in VR F1-F3 that nothing of the plugin takes there (vr/walk_flat.h kHintEveryMs): with no BodyWalk running the plugin does the walk and
// the belt there and nothing more - the rest of the controls is F4's (its own layout) or BodyWalk's Mapping. Said in the log, so "the right controller does nothing
// in F1, F2, F3" has its answer (2026-10-15: BodyWalk was not running for the first four minutes of that run). False = not said (BodyWalk runs, a panel is open...).
bool FlatPressSink(const char* what) {
    walkflat::Inputs in;
    directinput::FlatFacts(&in);
    in.bodyWalkRunning = BodyWalkPadInCharge();
    in.inWorld = g_inWorld.load();
    in.menuOpen = gamestate::MenuOpen();
    in.chatOpen = ChatOpen();
    if (walkflat::Decide(in) != walkflat::Verdict::Walk) return false;
    LogF("vrcam: VR F1-F3, no BodyWalk running: %s pressed - nothing of the plugin takes it here: [direct] flat_controller=0 (Settings: \"Play with the controllers in F1, F2, F3 "
         "without BodyWalk\") - it does the left stick's walk and the belt's trigger + flick only", what);
    return true;
}

// The gate of the plugin's own controller in VR F1-F3 (directinput::SetFlatGate): the key, direct input, a flat view and headset frames are directinput's own; this is
// BodyWalk - running (its Game Link alive, or its pad report moving) means its pad, its actions and its commands are the game's, as before, and ours stand down.
// A game on, a panel open, the chat open do not matter to it: the controller plays the menus too (the F4 layout's menu layer is the game's own pad).
bool FlatControllerGate() {
    walkflat::Inputs in;
    directinput::FlatFacts(&in);
    in.keyOn = directinput::FlatControllerOn();
    in.bodyWalkRunning = BodyWalkPadInCharge();
    return walkflat::Decide(in) == walkflat::Verdict::Walk;
}

// The pad the game's XInput poll and its walking hooks get from the plugin. [openxr] direct_input (F4, headset frames arriving): the plugin's
// own pad, and in it - [openxr] direct_bodywalk_walk - BodyWalk's LEFT STICK mixed into the controller's (vr/walk_path.h: each past the game's
// dead zone, the sum on the unit disc, the stronger one wins when they pull apart); nothing else of BodyWalk's report. Otherwise BodyWalk's
// report whole, while it moves ([input] bodywalk_pad). VR F1-F3 with no BodyWalk running: the controller's left stick alone (WalkFlatRead;
// *walkOnly says so).
// A physical gamepad next to the VR controllers (vr/walk_flat.h RealPadJoins; the Discord report of 2026-10-18: "every button works except the thumbsticks"). The game's
// poll of XInput pad 0 notes the real pad's sticks here (PadFromBodyWalk, before the plugin's pad is read), and the pad the plugin gives the walking hook follows the real
// pad's left stick where it is pushed - the hook is how the game walks while the pad's own left stick is kept from it. Cleared (zero, no time) when none joins.
std::atomic<uint64_t> g_realSticks{0};   // left x, left y, right x, right y of the real pad's last report, 16 bits each
std::atomic<ULONGLONG> g_realAt{0};      // when (GetTickCount64); 0 = no real pad joins
void NoteRealPad(const XINPUT_GAMEPAD* pad) {
    if (!pad) { g_realAt.store(0); return; }
    g_realSticks.store((uint64_t)(uint16_t)pad->sThumbLX | ((uint64_t)(uint16_t)pad->sThumbLY << 16) | ((uint64_t)(uint16_t)pad->sThumbRX << 32) | ((uint64_t)(uint16_t)pad->sThumbRY << 48));
    g_realAt.store(GetTickCount64());
}
// Said when a real pad really joins the plugin's pad (not whenever one answers: a flat game with a pad of its own and no VR controllers is nobody's news): once when it
// appears, again after 5 s without it, at most 6 lines.
void TellRealPad() {
    static ULONGLONG lastAt = 0;
    static int lines = 0;
    const ULONGLONG now = GetTickCount64();
    const bool fresh = lastAt == 0 || now - lastAt > 5000;
    lastAt = now;
    if (!fresh || lines >= 6) return;
    ++lines;
    Log("vrcam: a physical gamepad answers the game's XInput poll (pad 0) next to the VR controllers: its buttons are added to the controllers', the larger trigger counts, "
        "and its sticks win where they are pushed (the left one walks, the right one turns) - it used to be dropped while the plugin's own pad was the pad");
}
void FoldRealLeftStick(BWPadMirror* out) {
    const uint64_t v = g_realSticks.load(std::memory_order_relaxed);
    int16_t x = out->thumb_lx, y = out->thumb_ly;
    if (walkflat::FoldRealStick(&x, &y, walkflat::RealPadFresh(GetTickCount64(), g_realAt.load()), (int16_t)(v & 0xFFFF), (int16_t)((v >> 16) & 0xFFFF))) {
        out->thumb_lx = x;
        out->thumb_ly = y;
    }
}

// BodyWalk's own left stick as its report had it (before any lift), packed x | y << 16, with whether the last read lengthened it: for the
// walk-start line (HookStickGet). 0 while BodyWalk's report is not live.
std::atomic<uint32_t> g_bwStickRaw{0};
std::atomic<bool> g_bwStickLifted{false};
// [input] bodywalk_walk_lift (vr/walk_path.h Lift): F4, in the world, no panel - BodyWalk's stick between the key's length and the game's
// dead zone is lengthened to just past it, so the hero starts while BodyWalk's averages are still climbing. 0 = off, the stick as it is.
void BodyWalkStickLift(BWPadMirror* bw, bool live) {
    g_bwStickRaw.store(live ? ((uint32_t)(uint16_t)bw->thumb_lx | ((uint32_t)(uint16_t)bw->thumb_ly << 16)) : 0u, std::memory_order_relaxed);
    const float from = g_set.bwWalkLift.load(std::memory_order_relaxed);
    bool lifted = false;
    if (live && from > 0.0f && viewgate::F4() && g_inWorld.load() && !gamestate::MenuOpen())
        lifted = walkpath::Lift(&bw->thumb_lx, &bw->thumb_ly, from);
    g_bwStickLifted.store(lifted, std::memory_order_relaxed);
}

bool PadMirrorRead(BWPadMirror* out, bool* walkOnly = nullptr, bool* pluginPad = nullptr) {
    if (walkOnly) *walkOnly = false;
    if (pluginPad) *pluginPad = false;
    directinput::PadOut d{};
    const bool direct = directinput::PadRead(&d);
    if (!direct && (FlatControllerRead(out) || WalkFlatRead(out))) {   // VR F1-F3 without BodyWalk: the controller, or the walk alone
        FoldRealLeftStick(out);   // (a real pad's pushed left stick walks - the walking hook reads this)
        if (walkOnly) *walkOnly = true;
        return true;
    }
    if (!direct && g_walkState.load(std::memory_order_relaxed) != (uint8_t)walkpath::State::NotDirect)
        g_walkState.store((uint8_t)walkpath::State::NotDirect);   // (BodyWalk's pad is the pad, or none: no stale word for the walking hook's log)
    const bool walks = direct && directinput::BodyWalkWalks();
    BWPadMirror bw{};
    walkpath::Inputs in;
    in.directFeeding = direct;
    in.settingOn = walks;
    in.inWorld = g_inWorld.load();
    in.menuOpen = gamestate::MenuOpen() && !g_handWalk.load();   // ([openxr] hand_inventory: the stick walks with the inventory on the hand)
    in.bodywalkPadOn = g_set.bodywalkPad.load();
    if (GameLinkDirect()) {   // no gamepad report of BodyWalk's is read: its walk from Game Link's Walk block
        float wx = 0.0f, wy = 0.0f;
        in.reportLive = direct && walks && link::Walk(&wx, &wy);
        if (in.reportLive) { bw.version = BW_PAD_MIRROR_VERSION; bw.thumb_lx = walkpath::LinkAxis(wx); bw.thumb_ly = walkpath::LinkAxis(wy); }
    } else
        in.reportLive = (direct ? walks : in.bodywalkPadOn) && BodyWalkReport(&bw, walks);
    BodyWalkStickLift(&bw, in.reportLive);   // [input] bodywalk_walk_lift / [debug] walk_start_log
    in.ownLX = d.lx; in.ownLY = d.ly;
    if (direct) RoomStick(&in.ownLX, &in.ownLY);   // [openxr] roomscale_walk: the room walk's stick where the controller's rests
    in.bwLX = bw.thumb_lx; in.bwLY = bw.thumb_ly;
    const walkpath::Out r = walkpath::Resolve(in);
    if (direct) {
        NoteWalk(r, in);
        *out = BWPadMirror{};
        out->version = BW_PAD_MIRROR_VERSION;
        out->counter = d.counter;
        out->buttons = d.buttons;
        out->left_trigger = d.lt; out->right_trigger = d.rt;
        out->thumb_lx = r.lx; out->thumb_ly = r.ly; out->thumb_rx = d.rx; out->thumb_ry = d.ry;
        FoldRealLeftStick(out);   // (a real pad's pushed left stick walks - the walking hook reads this)
        if (pluginPad) *pluginPad = true;
        return true;
    }
    if (r.source != walkpath::Source::BodyWalkPad || GameLinkDirect()) return false;   // (Game Link direct: BodyWalk's gamepad is never the game's)
    *out = bw;
    RoomStick(&out->thumb_lx, &out->thumb_ly);   // (BodyWalk's pad is the pad: the room walk's stick where its stick rests)
    return true;
}

// FlatVR's controllers as the plugin's own controls ([direct] flatvr_controls, vr/flat_raw.h): with direct input on, no native session and a
// BodyWalk that sends its raw controllers through Game Link (Tracking version 2, BW_LINK_TRACK_CONTROLS), each new sample is made into the
// frame a native session makes and fed to direct input - its layout, the F4 gestures, the belt, the bow, the quivers - so BodyWalk's Mapping is
// not needed (its pad buttons, actions and commands are ignored while this feeds, as in native OpenXR; its left stick still walks, [openxr]
// direct_bodywalk_walk). The head and the hands of the camera and the arms stay BodyWalk's (link::Head). Once a present, on the present's
// thread - where native OpenXR feeds from too. Half a second without a new sample, or a native session starting: the feed ends.
void FlatRawTick() {
    static bool fed = false;
    static uint32_t seq = 0, counter = 0;
    static ULONGLONG newAt = 0;
    static flatraw::Clock clock;
    static int toldOld = 0;
    // a native session (running, or ending): its own controllers - not [openxr] on alone, a start that found no headset waits over FlatVR
    const bool native = xr::Running() || xr::StopPending();
    const bool want = !native && directinput::FlatVrControlsOn();
    const ULONGLONG now = GetTickCount64();
    BW_LinkTracking t;
    if (want && link::RawTracking(&t) && t.header.sequence != seq && flatraw::Fresh(t.stamp, D2RVRStampNow())) {
        seq = t.header.sequence;
        if (!flatraw::Usable(t)) {
            if (!toldOld && (t.flags & BW_LINK_TRACK_HEAD)) {
                toldOld = 1;
                Log("vrcam: FlatVR controls - BodyWalk sends no raw controllers through Game Link (an older BodyWalk or game_link.dll): its Mapping and pad "
                    "stay the controls in FlatVR ([direct] flatvr_controls needs BodyWalk with Game Link Tracking 2)");
            }
        } else {
            newAt = now;
            if (!fed) {
                fed = true;
                clock = flatraw::Clock{};
                directinput::SessionBegin(true);
                Log("vrcam: FlatVR controls - BodyWalk's raw controllers drive the plugin's own layout and the F4 gestures (BodyWalk's Mapping, pad buttons, "
                    "actions and commands are ignored; its head and hands stay the camera's)");
            }
            D2RVR_XrInput in;
            flatraw::ToXrInput(t, flatraw::Step(clock, t.stamp), ++counter, D2RVRStampNow(), &in);
            directinput::ShapeSticks(&in);   // the thumbsticks' dead zones ([direct] stick_deadzone_*_pct, vr/stick_shape.h), as native OpenXR's
            directinput::Feed(&in, gamestate::MenuOpen() || !g_inWorld.load());
        }
    }
    if (fed && (!want || now - newAt > 500)) {
        fed = false;
        if (!native) directinput::SessionEnd();   // (a native session: xr.cpp's own SessionBegin has taken over)
        Log(native ? "vrcam: FlatVR controls - a native session starts: its own controllers from here"
                   : "vrcam: FlatVR controls - no new sample from BodyWalk for half a second (or switched off): the feed ended");
    }
}

bool PadFromBodyWalk(XINPUT_STATE* s, bool realPad = false) {
    BWPadMirror m;
    bool walkOnly = false, pluginPad = false;
    // a physical gamepad answered the poll too, and BodyWalk's own virtual one is not what answered: its report joins the plugin's pad (noted first, so the walking hook's stick
    // - read below and from the hooks - follows its pushed left stick)
    const bool real = walkflat::RealPadJoins(realPad, BodyWalkPadInCharge());
    NoteRealPad(real ? &s->Gamepad : nullptr);
    if (!PadMirrorRead(&m, &walkOnly, &pluginPad)) return false;
    if (real && pluginPad) {
        // direct input's own pad (F4, FlatVR's raw controls; the F1-F3 controller comes through walkOnly below) is the pad the game polls - and the real pad's report used to be
        // thrown away whole: its buttons, its triggers, both sticks (the 2026-10-18 report: "every button works except the thumbsticks" - the buttons were the VR controllers').
        // The same merge as in F1-F3 (vr/walk_flat.h MergePads): buttons ORed, the larger trigger, a stick of ours only where the real pad's own stick rests.
        walkflat::PadBits p{s->Gamepad.wButtons, s->Gamepad.bLeftTrigger, s->Gamepad.bRightTrigger,
                            s->Gamepad.sThumbLX, s->Gamepad.sThumbLY, s->Gamepad.sThumbRX, s->Gamepad.sThumbRY};
        const walkflat::PadBits ours{m.buttons, m.left_trigger, m.right_trigger, m.thumb_lx, m.thumb_ly, m.thumb_rx, m.thumb_ry};
        walkflat::MergePads(&p, ours);
        TellRealPad();
        s->dwPacketNumber += m.counter | 1u;   // (ours moves every headset frame: a new packet for a game that skips equal ones)
        s->Gamepad.wButtons = p.buttons; s->Gamepad.bLeftTrigger = p.lt; s->Gamepad.bRightTrigger = p.rt;
        s->Gamepad.sThumbLX = p.lx; s->Gamepad.sThumbLY = p.ly; s->Gamepad.sThumbRX = p.rx; s->Gamepad.sThumbRY = p.ry;
        g_padMirrorOkAt.store(GetTickCount64());
        return true;   // the plugin's pad with the real one in it (the sticks turned below as the plugin's own are)
    }
    if (walkOnly && realPad) {
        // VR F1-F3 and a real (or virtual) pad answers as well: the plugin's controller is added to it, never over it (vr/walk_flat.h MergePads) - buttons ORed,
        // the larger trigger, a stick of ours only where that pad's own stick rests. It is the real pad's report still (false), the sticks turned below like any pad's.
        walkflat::PadBits p{s->Gamepad.wButtons, s->Gamepad.bLeftTrigger, s->Gamepad.bRightTrigger,
                            s->Gamepad.sThumbLX, s->Gamepad.sThumbLY, s->Gamepad.sThumbRX, s->Gamepad.sThumbRY};
        const walkflat::PadBits ours{m.buttons, m.left_trigger, m.right_trigger, m.thumb_lx, m.thumb_ly, m.thumb_rx, m.thumb_ry};
        TellRealPad();
        if (walkflat::MergePads(&p, ours)) {
            s->Gamepad.wButtons = p.buttons; s->Gamepad.bLeftTrigger = p.lt; s->Gamepad.bRightTrigger = p.rt;
            s->Gamepad.sThumbLX = p.lx; s->Gamepad.sThumbLY = p.ly; s->Gamepad.sThumbRX = p.rx; s->Gamepad.sThumbRY = p.ry;
            s->dwPacketNumber += m.counter | 1u;   // (the state changed with ours: a new packet for a game that skips equal ones)
        }
        return false;
    }
    s->dwPacketNumber = m.counter;
    s->Gamepad.wButtons = m.buttons;
    s->Gamepad.bLeftTrigger = m.left_trigger;
    s->Gamepad.bRightTrigger = m.right_trigger;
    s->Gamepad.sThumbLX = m.thumb_lx; s->Gamepad.sThumbLY = m.thumb_ly;
    s->Gamepad.sThumbRX = m.thumb_rx; s->Gamepad.sThumbRY = m.thumb_ry;
    g_padMirrorOkAt.store(GetTickCount64());
    return true;
}

// The input log ([debug] input_log: 1 = the first 400 lines of a run, 2 = all, 0 = off). Heroes
// attacked on head turns with nothing pressed in BodyWalk (two players and the author, 2026-10-12):
// every press that can make the hero act goes into the log with where it came from - a button or a
// trigger of the pad the game gets (BodyWalk's pad, an XInput pad, or vrcam's own for an action),
// BodyWalk's actions through Game Link (its gestures among them), a click or a key in the game's
// window (from a device or injected) - and how fast the head was turning just then.
std::atomic<int> g_inputLog{1};
std::atomic<float> g_headRate{0.0f};   // deg/s, sampled at the game's pad polls
void SampleHeadRate() {
    static SRWLOCK lock = SRWLOCK_INIT;
    static float last = 0.0f;
    static ULONGLONG lastAt = 0;
    float yaw;
    if (!HeadYaw(&yaw) || !TryAcquireSRWLockExclusive(&lock)) return;
    const ULONGLONG now = GetTickCount64();
    if (!lastAt || now - lastAt > 500) { last = yaw; lastAt = now; g_headRate.store(0.0f); }
    else if (now - lastAt >= 30) {
        g_headRate.store(WrapDeg(yaw - last) * 1000.0f / (float)(now - lastAt));
        last = yaw;
        lastAt = now;
    }
    ReleaseSRWLockExclusive(&lock);
}
void InputLogF(const char* fmt, ...) {
    const int mode = g_inputLog.load();
    if (mode <= 0) return;
    static std::atomic<int> lines{0};
    const int n = lines.fetch_add(1);
    if (mode == 1 && n >= 400) {
        if (n == 400) Log("input: 400 lines - quiet from here on ([debug] input_log=2 logs them all)");
        return;
    }
    char b[384];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    LogF("input: %s | the head turning %.0f deg/s%s", b, fabsf(g_headRate.load()), gamestate::MenuOpen() ? ", a panel open" : "");
}
// The pad as the game gets it against its last poll: buttons and triggers pressed, each with its
// source - in `got` (before vrcam's actions) from the pad read, else vrcam's own for an action.
void InputLogPad(const XINPUT_GAMEPAD& got, const XINPUT_GAMEPAD& fin, bool mirror) {
    if (g_inputLog.load() <= 0) return;
    static const char* const kNames[16] = {"Up", "Down", "Left", "Right", "Start", "Back", "LS", "RS", "LB", "RB", "0x0400", "0x0800", "A", "B", "X", "Y"};
    static SRWLOCK lock = SRWLOCK_INIT;
    static WORD was = 0;
    static bool ltWas = false, rtWas = false;
    static ULONGLONG downAt[16] = {};
    if (!TryAcquireSRWLockExclusive(&lock)) return;
    const char* from = mirror ? "BodyWalk's pad" : "an XInput pad (a device, or a virtual one)";
    const ULONGLONG now = GetTickCount64();
    for (int i = 0; i < 16; ++i) {
        const WORD bit = (WORD)(1u << i);
        if ((fin.wButtons & bit) && !(was & bit)) {
            downAt[i] = now;
            InputLogF("pad %s down - %s", kNames[i], (got.wButtons & bit) ? from : "vrcam's, for a BodyWalk action");
        } else if (!(fin.wButtons & bit) && (was & bit) && now - downAt[i] < 150)
            InputLogF("pad %s up after %llu ms", kNames[i], now - downAt[i]);
    }
    was = fin.wButtons;
    const bool lt = fin.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD, rt = fin.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    if (lt && !ltWas) InputLogF("pad LT pulled (%u) - %s", fin.bLeftTrigger, from);
    if (rt && !rtWas) InputLogF("pad RT pulled (%u) - %s", fin.bRightTrigger, from);
    ltWas = lt;
    rtWas = rt;
    ReleaseSRWLockExclusive(&lock);
}

// Twice a second: says when the game loses or gets back its pads, and when W A
// S D are held but the game never asks for a keyboard move (its UI is on a pad:
// the "controller" option, or a pad's press - a mouse click asks for the mouse).
void FlatPadTick() {
    static int told = -1;
    const int now = FlatNoPad() ? 1 : 0;
    if (now != told) {
        if (now == 1) Log("vrcam: flat: no pad for the game (W A S D and the mouse; [input] flat_no_pad=0 lets a real pad through)");
        else if (told == 1) Log("vrcam: flat: the game sees its pads again");
        told = now;
    }
    static uint32_t lastCalls = 0;
    static ULONGLONG lastTold = 0;
    const uint32_t calls = g_keyMoveCalls.load();
    const bool keys = (GetAsyncKeyState('W') | GetAsyncKeyState('A') | GetAsyncKeyState('S') | GetAsyncKeyState('D')) & 0x8000;
    if (g_keyMove.load() == 1 && ((FlatKeyMode() && ShooterActive()) || VrKeyWalk()) && GameFocused() && keys && calls == lastCalls &&
        GetTickCount64() - lastTold > 10000) {
        lastTold = GetTickCount64();
        Log("vrcam: flat: W A S D held, but the game does not ask for a keyboard move - its UI is in controller mode "
            "(a mouse click switches it back; or the game's Options: controller off)");
    }
    lastCalls = calls;
}

// W A S D as a left stick relative to the view (up = ahead); TurnStick then turns
// it onto the game's screen like any other stick. With no pad plugged in, the
// game gets a pad that has only this stick.
// The pad is there the whole time the shooter controls are: one that came and
// went with each key press was never taken up by the game. The left mouse
// button is A - the blow, the shot or the action, where the camera looks
// (HandRay, [bow] mode 4) - never a walk to the pointer: the mouse itself
// never reaches the game (GameWndProc), which so stays in controller mode,
// its pointer hidden.
// Flat mode with the keyboard move (FlatKeyMode) has no such pad: kept only
// for another game build, where the keyboard move hook cannot go in.
void KeysAsStick(XINPUT_STATE* s, DWORD* r) {
    if (KeyMoveMode() || !ShooterActive() || !GameFocused()) return;
    auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    if (*r != ERROR_SUCCESS) { memset(s, 0, sizeof *s); *r = ERROR_SUCCESS; }
    const int swap = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;   // the primary button
    if (held(swap)) s->Gamepad.wButtons |= XINPUT_GAMEPAD_A;
    const bool typing = ChatOpen();   // then W A S D are letters for the chat line
    const float ahead = typing ? 0.0f : (held('W') ? 1.0f : 0.0f) - (held('S') ? 1.0f : 0.0f);
    const float side = typing ? 0.0f : (held('D') ? 1.0f : 0.0f) - (held('A') ? 1.0f : 0.0f);
    if (ahead != 0.0f || side != 0.0f) {
        const float k = 32767.0f / sqrtf(ahead * ahead + side * side);
        s->Gamepad.sThumbLX = (SHORT)(side * k);
        s->Gamepad.sThumbLY = (SHORT)(ahead * k);
    }
    static DWORD packet = 0;
    s->dwPacketNumber = ++packet;
}

// xinput9_1_0's XInputGetState calls into xinput1_4's, and both are hooked.
// Only the outermost call on a thread touches the state: done at both levels
// the left stick was turned twice and the inner call's zeroing of the right
// stick reached the outer one, which then read the turn as 0.
thread_local int t_xDepth = 0;

// What answered the game's poll of pad 0, said when it changes (vr/pad_presence.h): the log of 2026-10-14 15:06 had the game's
// "PLEASE RECONNECT CONTROLLER" panel open right after a 1.36 s stall of the draw thread - one poll that came back "not connected".
void PadPresenceNote(size_t entry, padpresence::Source now, DWORD origResult, DWORD finalResult) {
    static padpresence::Tracker tracker;
    static padpresence::Said said, saidAbsent;
    padpresence::Source was = padpresence::Source::Absent;
    const bool changed = tracker.Step(now, &was);
    if (!changed && finalResult == ERROR_SUCCESS) return;   // (the common poll: nothing to say, nothing built)
    uint32_t age = 0;
    const char* why = directinput::WhyNotActive(&age);
    char direct[400] = "";
    if (directinput::On()) snprintf(direct, sizeof direct, "; direct input: %s (newest headset frame %u ms old)", why[0] ? why : "feeding", age);
    if (changed && said.Next())
        LogF("pad presence: pad 0 %s -> %s (tick %llu, %ls!%s, thread %lu, the real XInput said %lu%s)", padpresence::Name(was), padpresence::Name(now),
             GetTickCount64(), g_xhooks[entry].dll, g_xhooks[entry].name, GetCurrentThreadId(), origResult, direct);
    if (finalResult != ERROR_SUCCESS && saidAbsent.Next())
        LogF("pad presence: pad 0 answered %lu (NOT CONNECTED) to the game - it will open its reconnect panel (tick %llu, %ls!%s, thread %lu%s)",
             finalResult, GetTickCount64(), g_xhooks[entry].dll, g_xhooks[entry].name, GetCurrentThreadId(), direct);
}

// [gameplay] teleport_circle: the arc of a point skill's hold, every pad poll while it is up. It leaves the staff's tip as the game drew the staff - the weapon bone's
// origin and long axis, out to the tip by the model's own reach (vr/weapon_extents.h) - else the line the drawn caster or hand aims along (vr/spell_aim_model.h), else
// the controller's own (HandRay's line before it is tipped to the ground); thrown with the cap's reach at 45 degrees (the hand raised throws it farther), it comes
// down on the hero's ground, held to the skill's cap (PointCapCells), and is drawn [gameplay] point_arc_height of the throw's own rise above the line from the tip to
// the circle. World units; the circle against the hero as drawn (the look-at).
bool PointArcFor(int skill, pointcast::Arc* out) {
    AcquireSRWLockShared(&g_stickLock); const StickFrame f = g_stickFrame; ReleaseSRWLockShared(&g_stickLock);
    if (!out || !f.valid || !g_eyeOk.load()) return false;
    const float upm = std::max(1.0f, ViewHeight()) / UserEyeHeightM();
    const float eye[3] = {g_eyeWorld[0].load(), g_eyeWorld[1].load(), g_eyeWorld[2].load()};
    auto toWorld = [&](const float* v, float* w, bool point) {   // the head's turn-only frame (metres from the eyes) -> the world, as HandRay
        for (int i = 0; i < 3; ++i) w[i] = (point ? eye[i] : 0.0f) + upm * (v[0] * f.camR[i] - v[2] * f.camF[i]);
        w[1] += upm * v[1];
    };
    float o[3] = {}, d[3] = {};
    bool have = false;
    int source = 0;
#if D2RVR_FIRST_PERSON
    // [gameplay] leap_arc_head: Leap and Leap Attack leave from the middle of the drawn hero's belly (his own pelvis and spine bones: skel::BodyBelly) along
    // where the HEAD looks (the camera's forward, its pitch and turn): the head raised throws the landing farther, lowered nearer, turned turns it - held to the
    // leap's own range as ever. No fresh pose of the body: the weapon or the hand below, as without the key.
    float belly[3];
    if (g_set.leapArcHead.load() && leapgaze::IsLeap(skill) && skel::BodyBelly(belly) && GetTickCount64() - g_gazeAt.load() < 500) {
        toWorld(belly, o, true);
        // [gameplay] leap_head_gain: the head's tilt multiplied (a small raise reaches far), held to the farthest throw
        const pointcast::V3 head = pointcast::TiltGain({g_gazeFwd[0].load(), g_gazeFwd[1].load(), g_gazeFwd[2].load()}, g_set.leapHeadGain.load());
        d[0] = head.x;
        d[1] = head.y;
        d[2] = head.z;
        have = std::isfinite(o[0] + o[1] + o[2] + d[0] + d[1] + d[2]);
        if (have) source = 4;
    }
    skel::PosedWeaponPose pw;
    if (!have && skel::PosedWeapon(pw)) {   // the weapon as the finished pose draws it: its tip
        const int side = pw.side & 1;
        const skel::PosedBlade& bl = pw.blade[side];
        weaponextent::Reach rr;
        if (bl.ok && bl.unitM > 0.0f && weaponextent::ReachOf(gamestate::HandCode(side), &rr) && rr.axis == 1) {
            float tipH[3], dirH[3];
            for (int i = 0; i < 3; ++i) {
                dirH[i] = bl.axis[1][i] * (float)rr.tipSign;
                tipH[i] = bl.origin[i] + dirH[i] * rr.tip * bl.unitM;
            }
            toWorld(tipH, o, true);
            toWorld(dirH, d, false);
            have = true;
            source = 1;
        }
    }
    if (!have && viewgate::F4() && FullBody() && ArmsMode() == 2) {   // no posed weapon: the caster or the hand as drawn
        const spellmodel::Out m = SpellModelAim();
        if (m.src != spellmodel::Src::None) {
            toWorld(m.origin, o, true);
            toWorld(m.dir, d, false);
            have = true;
            source = 2;
        }
    }
#endif
    if (!have) {   // the controller itself
        float dRaw[3] = {NAN, NAN, NAN};
        if (!HandRay(o, d, dRaw) || !std::isfinite(dRaw[0] + dRaw[1] + dRaw[2])) return false;
        memcpy(d, dRaw, sizeof d);
        source = 3;
    }
    const float cap = PointCapCells(skill) * pointcast::kUnitsPerCell;
    *out = pointcast::MakeArc({o[0], o[1], o[2]}, {d[0], d[1], d[2]}, g_gazeLook[1].load(), g_gazeLook[0].load(), g_gazeLook[2].load(), cap, pointcast::kMinUnits, cap,
                              pointcast::HeightScale(g_set.pointArcHeightPct.load()));
    out->source = source;
    return out->ok;
}

template <int I> DWORD WINAPI XDetour(DWORD idx, XINPUT_STATE* s) {
    g_xhooks[I].calls++;
    // Flat without a pad: no controller anywhere, so no pad event ever asks
    // the game for its controller UI (docs/move_recon.md §6). Only the outermost call: xinput9_1_0 calls into xinput1_4, both hooked.
    if (t_xDepth == 0 && FlatNoPad()) {
        // [input] flat_pad_on_touch: the real pad is read (the game never sees it); touched once, the pads go through from then on
        if (g_set.flatPadOnTouch.load() && idx == 0) {
            XINPUT_STATE real{};
            ++t_xDepth;
            const DWORD got = g_xhooks[I].orig(idx, &real);
            --t_xDepth;
            const XINPUT_GAMEPAD& p = real.Gamepad;
            if (got == ERROR_SUCCESS && padpresence::RealPadTouched(p.wButtons, p.bLeftTrigger, p.bRightTrigger, p.sThumbLX, p.sThumbLY, p.sThumbRX, p.sThumbRY) &&
                !g_flatPadTouched.exchange(true))
                Log("vrcam: flat: the real pad was touched - the game sees it from now on ([input] flat_pad_on_touch; flat_no_pad hides pads only until a pad is used)");
        }
        if (FlatNoPad()) {
            if (s) memset(s, 0, sizeof *s);
            return ERROR_DEVICE_NOT_CONNECTED;
        }
    }
    frametrace::Scope trace(frametrace::kPadHook);   // [debug] frame_trace: ours, the real XInput left out - the poll is on the draw thread
    trace.Pause();
    const bool outer = t_xDepth == 0;   // (xinput9_1_0 calls into xinput1_4: the driver's time is told once)
    ++t_xDepth;
    DWORD r;
    {
        frametrace::Scope driver(idx == 0 ? frametrace::kXInputPad0 : frametrace::kXInputOther, outer);   // [debug] frame_trace: the Xbox driver's own answer
        r = g_xhooks[I].orig(idx, s);
    }
    const DWORD origResult = r;
    --t_xDepth;
    trace.Resume();
    // Flat with a real pad let through (flat_no_pad=0): that pad only, its stick
    // turned by the camera below; BodyWalk's stick walks through the keyboard move.
    bool mirror = false;
    if (t_xDepth == 0 && idx == 0 && s && !FlatKeyMode() && PadFromBodyWalk(s, origResult == ERROR_SUCCESS)) { r = ERROR_SUCCESS; mirror = true; }
    if (t_xDepth == 0 && idx == 0 && s) KeysAsStick(s, &r);
    // [gameplay] melee_skill_armed (F4): a skill button's press that arms a melee skill is taken out of the pad the game polls - before the gestures' own pulses are added, so a pulse of ours (the
    // shield bash's Smite, double swing's) is never taken. Armable while hit by touch swings a melee weapon (never a bow, a crossbow or a thrown weapon).
    if (t_xDepth == 0 && idx == 0 && s && r == ERROR_SUCCESS && meleearm::On() && g_set.platform.load() == 1)
        meleearm::FilterPad(&s->Gamepad.wButtons, s->Gamepad.bLeftTrigger, meleetouch::Live(500) && !ThrownInHand());
    if (t_xDepth == 0 && idx == 0 && s) {   // pad buttons the gestures pulse (directinput::PadPulse): over any pad, or a pad of their own
        uint16_t pb; uint8_t plt, prt;
        if (directinput::PadPulses(&pb, &plt, &prt)) {
            if (r != ERROR_SUCCESS) { memset(s, 0, sizeof *s); r = ERROR_SUCCESS; static DWORD packet = 0x20000000; s->dwPacketNumber = ++packet; }
            s->Gamepad.wButtons |= pb;
            s->Gamepad.bLeftTrigger = std::max(s->Gamepad.bLeftTrigger, plt);
            s->Gamepad.bRightTrigger = std::max(s->Gamepad.bRightTrigger, prt);
        }
    }
    // [gameplay] teleport_circle (F4): a press of a point skill's slot is held out of the pad while it is held (its arc and circle drawn meanwhile). After the
    // gestures' pulses: the right trigger's RT slot ([openxr] skill_rt_a) and the skill panel's picks come as pulses; the release's own pulse starts nothing
    if (t_xDepth == 0 && idx == 0 && s && r == ERROR_SUCCESS && pointwire::On() && g_set.platform.load() == 1) {
        pointwire::PollIn in;
        in.allowed = g_set.teleportCircle.load() && viewgate::F4() && g_enabled.load() && g_inWorld.load() && !gamestate::MenuOpen();
        pointwire::FilterPad(&s->Gamepad.wButtons, s->Gamepad.bLeftTrigger, &s->Gamepad.bRightTrigger, in, &PointArcFor);
    }
    if (t_xDepth == 0 && idx == 0 && s && g_set.platform.load() == 1) {
        // In VR, once the plugin is the pad, pad 0 is never "not connected": the game takes one such answer for a disconnect, opens
        // "PLEASE RECONNECT CONTROLLER" (blocks the game) and polls again only 4 s later. A neutral pad stands in while the plugin's
        // own is missing (a stalled draw thread, a session that lost focus or ended, controllers asleep, BodyWalk gone).
        using padpresence::Source;
        static padpresence::NeutralPacket neutral;
        Source src = Source::Real;
        if (r != ERROR_SUCCESS && padpresence::MustStayPresent(true, directinput::On(), g_padMirrorOkAt.load() != 0)) {
            memset(s, 0, sizeof *s);
            s->dwPacketNumber = neutral.Next();
            r = ERROR_SUCCESS;
        }
        if (r != ERROR_SUCCESS) src = Source::Absent;
        else if (mirror) src = directinput::Active() ? Source::Direct : g_walkState.load() == (uint8_t)walkpath::State::FlatOwn ? Source::Walk : Source::BodyWalk;
        else if (origResult != ERROR_SUCCESS) src = Source::Kept;
        PadPresenceNote(I, src, origResult, r);
    }
    if (r == ERROR_SUCCESS && t_xDepth == 0) {
        const XINPUT_GAMEPAD got = s->Gamepad;   // as the pad had it, before vrcam's actions (the input log)
        if (idx == 0) { SampleHeadRate(); ApplyActions(s); }
        {   // which entry polls what, with BodyWalk's pad or without (2026-10-07): at most 30 lines a session
            static std::atomic<int> lines{0};
            static ULONGLONG told[std::size(g_xhooks)] = {};
            if (lines.load() < 30 && GetTickCount64() - told[I] > 2000) {
                told[I] = GetTickCount64();
                lines.fetch_add(1);
                // (in direct mode the report is the plugin's own pad: "no", with what became of BodyWalk's walking stick)
                char bwWord[220] = "no";
                if (mirror && directinput::Active())
                    snprintf(bwWord, sizeof bwWord, "no - the plugin's own pad; BodyWalk's walk: %s", walkpath::StateName((walkpath::State)g_walkState.load()));
                else if (mirror && g_walkState.load() == (uint8_t)walkpath::State::FlatOwn) snprintf(bwWord, sizeof bwWord, "no - the plugin's left stick only (VR F1-F3, no BodyWalk)");
                else if (mirror) snprintf(bwWord, sizeof bwWord, "yes");
                LogF("pad diag: %ls!%s pad %u, BodyWalk's pad %s, buttons %04X, right X %d, thread %lu", g_xhooks[I].dll,
                     g_xhooks[I].name, idx, bwWord, s->Gamepad.wButtons, s->Gamepad.sThumbRX, GetCurrentThreadId());
            }
        }
        if (idx == 0) g_pad0At.store(GetTickCount64());
        TurnStick(s, mirror, idx);
        if (idx == 0) InputLogPad(got, s->Gamepad, mirror);
        if (idx == 0 && g_ptActive.load(std::memory_order_relaxed)) PressTracePoll(s->Gamepad.wButtons);   // [debug] melee_press_trace: the buttons as the game gets them
    }
    return r;
}
void* const kXDetours[] = {(void*)&XDetour<0>, (void*)&XDetour<1>, (void*)&XDetour<2>, (void*)&XDetour<3>, (void*)&XDetour<4>};

// [debug] frame_trace: the game's own rumble. The game has a RumbleManager (Fog\src\RumbleManager.cpp: OnHitRumble, OnHitPlayerRumble, SkillUseRumble...) and Settings.json
// 'Controller Rumble Enabled': it calls XInputSetState on a hit or a skill use as long as it believes a pad is there - and in VR it does, because pad 0 is kept
// present (vr/pad_presence.h). The plugin never calls XInputSetState itself (its haptics go to the headset's controllers through OpenXR). This only WATCHES the
// call - counted and timed in the pair it fell in, passed on untouched - and is put in only while frame_trace is on.
// Which of the three DLLs the game uses is told by whichever is called first (xinput9_1_0's SetState calls into xinput1_4's: counted once).
// (If the numbers say it is the driver: Settings.json 'Controller Rumble Enabled' 0, or unplug / close the pad, tells.)
using XInputSetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_VIBRATION*);
struct XSetHook { const wchar_t* dll; XInputSetStateFn orig; bool done; };
XSetHook g_xsets[] = {{L"xinput1_4.dll", nullptr, false}, {L"xinput1_3.dll", nullptr, false}, {L"xinput9_1_0.dll", nullptr, false}};
template <int I> DWORD WINAPI XSetDetour(DWORD idx, XINPUT_VIBRATION* v) {
    const bool outer = t_xDepth == 0;
    frametrace::Scope driver(frametrace::kXInputSet, outer);   // the driver's time is in it: that is what is measured
    ++t_xDepth;
    const DWORD r = g_xsets[I].orig(idx, v);
    --t_xDepth;
    return r;
}
void* const kXSetDetours[] = {(void*)&XSetDetour<0>, (void*)&XSetDetour<1>, (void*)&XSetDetour<2>};

// XInput DLLs are often loaded late, so this runs until each one is in.
void HookXInput() {
    for (size_t i = 0; i < std::size(g_xhooks); ++i) {
        XHook& h = g_xhooks[i];
        if (h.done) continue;
        HMODULE m = GetModuleHandleW(h.dll);
        if (!m) continue;
        h.done = true;   // one attempt per export
        void* fn = (void*)GetProcAddress(m, h.ordinal ? MAKEINTRESOURCEA(h.ordinal) : h.name);
        char b[160];
        if (!fn) { snprintf(b, sizeof b, "vrcam: %ls has no %s", h.dll, h.name); Log(b); continue; }
        const bool ok = MH_CreateHook(fn, kXDetours[i], (void**)&h.orig) == MH_OK && MH_EnableHook(fn) == MH_OK;
        snprintf(b, sizeof b, "vrcam: %ls!%s %s", h.dll, h.name, ok ? "hooked" : "hook FAILED");
        Log(b);
    }
}

void HookXInputSet() {
    if (!frametrace::On()) return;
    for (size_t i = 0; i < std::size(g_xsets); ++i) {
        XSetHook& h = g_xsets[i];
        if (h.done) continue;
        HMODULE m = GetModuleHandleW(h.dll);
        if (!m) continue;
        h.done = true;   // one attempt per export
        void* fn = (void*)GetProcAddress(m, "XInputSetState");
        char b[160];
        if (!fn) { snprintf(b, sizeof b, "vrcam: %ls has no XInputSetState", h.dll); Log(b); continue; }
        const bool ok = MH_CreateHook(fn, kXSetDetours[i], (void**)&h.orig) == MH_OK && MH_EnableHook(fn) == MH_OK;
        snprintf(b, sizeof b, "vrcam: %ls!XInputSetState %s ([debug] frame_trace: the game's rumble is counted and timed, passed on untouched)", h.dll, ok ? "watched" : "watch FAILED");
        Log(b);
    }
}

// Logs once which XInput entry the game actually polls - the proof the stick path works.
void ReportXInputUse() {
    static bool told[std::size(g_xhooks)] = {};
    for (size_t i = 0; i < std::size(g_xhooks); ++i)
        if (!told[i] && g_xhooks[i].calls.load()) {
            told[i] = true;
            char b[128]; snprintf(b, sizeof b, "vrcam: game polls %ls!%s", g_xhooks[i].dll, g_xhooks[i].name); Log(b);
        }
}

}  // namespace vrc
