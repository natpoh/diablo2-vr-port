// vr/input/walk.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M10); what it gives: vr/input/input.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {
// BodyWalk's pad mirror (vrcam.cpp; declared here only: its defaults are its definition's)
bool PadMirrorRead(BWPadMirror* out, bool* walkOnly = nullptr, bool* pluginPad = nullptr);


// Mouse look and W A S D ([input] mouse_look, F9): the first-person game
// without a headset. Live while our camera shows a game area and no menu is open.
std::atomic<bool> g_mouseLookOn{false};
bool ShooterActive() { return g_mouseLookOn.load() && g_enabled.load() && g_inWorld.load() && !gamestate::MenuOpen(); }

// The game's chat line (Enter) is open: then W A S D are letters typed, not
// ours. D2's UI-state array, one byte per panel at 0x2A2ADA0 (GetUIState
// 0xCE500), index 5 = ChatPanel: the HUD's chat key toggles it (0xCDE00(5)),
// the chat's Esc / Enter close it (0xC7D30(5)) - docs/move_recon.md "chat input".
// Both byte checks below pin the address and the index on this build; another
// build reads nothing and keeps the old behaviour.
constexpr uint64_t RVA_GET_UI_STATE = 0xCE500, RVA_HUD_CHAT_CHECK = 0x2E4AA3, RVA_UI_STATES = 0x2A2ADA0;
constexpr int kUiChat = 5;
const uint8_t kSigGetUiState[15] = {0x48,0x63,0xC1,0x48,0x8D,0x0D,0x96,0xC8,0x95,0x02,0x0F,0xB6,0x04,0x08,0xC3};  // movsxd rax, ecx; lea rcx, [0x2A2ADA0]; movzx eax, byte [rax+rcx]; ret
const uint8_t kSigHudChatCheck[10] = {0xB9,0x05,0x00,0x00,0x00,0xE8,0x53,0x9A,0xDE,0xFF};                         // mov ecx, 5; call 0xCE500
std::atomic<ULONGLONG> g_chatEnterAt{0};   // Enter pressed with the line shut: the game may not have opened it yet

bool ChatFlagFound() {
    static std::atomic<int> ok{-1};
    if (ok.load() < 0) {
        const bool found = Matches(RVA_GET_UI_STATE, kSigGetUiState, sizeof kSigGetUiState) &&
                           Matches(RVA_HUD_CHAT_CHECK, kSigHudChatCheck, sizeof kSigHudChatCheck) && d2rsig::Addr(RVA_UI_STATES);
        if (ok.exchange(found ? 1 : 0) < 0)
            LogF(found ? "vrcam: chat line flag found - W A S D go to the game while typing"
                       : "vrcam: chat line flag NOT found on this build - W A S D stay ours while typing");
    }
    return ok.load() == 1;
}

bool ChatFlag() {
    uint8_t v = 0;
    return ChatFlagFound() && SafeRead(&v, (void*)(d2rsig::Addr(RVA_UI_STATES) + kUiChat), 1) && v == 1;
}

bool ChatOpen() {
    const bool open = ChatFlag() || GetTickCount64() - g_chatEnterAt.load() < 250;
    static std::atomic<int> told{-1};
    if (told.exchange(open ? 1 : 0) != (open ? 1 : 0) && ChatFlagFound())
        LogF("vrcam: chat line %s", open ? "open - W A S D go to the game" : "closed - W A S D walk");
    return open;
}

// -- Flat mode without any pad: the game's own keyboard move ------------------
// docs/move_recon.md. In mouse/keyboard mode the per-player controller update
// 0x14C540 takes the walking vector from 0x8A960 (its only caller, 0x14C667):
// a float2 in RAX (low dword x right, high dword y up - the pad stick's
// convention after the game's dead zone), built from the four "Move Up/Right/
// Down/Left" bytes 0x2A23754..57, or 0 when a gate says no (the option
// 0x77D80, no game, [0x2A2347C], the control lock 0x13DE60, the loading
// screen 0xCC1E0, 0x92A10). The vector then runs the very pipe the pad stick
// runs (run hysteresis, the 0x42 command, the stop when it goes back to 0),
// and the UI stays on the mouse: nothing here ever asks for controller mode.
// So in flat mode ([mode] platform=0, [input] flat_keyboard_move=1) W A S D
// (or BodyWalk's stick) go in here, turned by our camera like a pad stick;
// [input] flat_no_pad=1 shows the game no pad at all.
constexpr uint64_t RVA_KEY_MOVE = 0x8A960, RVA_MOVE_KEYS = 0x2A23754;
// sub rsp, 0x48; movaps [rsp+0x30], xmm6; xorps xmm6, xmm6; movaps [rsp+0x20], xmm7 (no rip, no rel32)
const uint8_t kSigKeyMove[17] = {0x48,0x83,0xEC,0x48,0x0F,0x29,0x74,0x24,0x30,0x0F,0x57,0xF6,0x0F,0x29,0x7C,0x24,0x20};
using KeyMoveFn = uint64_t (*)();
KeyMoveFn OrigKeyMove;
std::atomic<int> g_keyMove{0};               // the hook: 0 not in yet, 1 in, 2 not possible (another build) - the old pad way then
std::atomic<uint32_t> g_keyMoveCalls{0};     // how often the game asked: still while W is held = its UI is on the pad

// Flat play through the keyboard move: on while the hook is in or may still go in.
bool FlatKeyMode() { return g_set.platform.load() == 0 && g_set.flatKeyMove.load() && g_keyMove.load() != 2; }
// ... and the game sees no pad at all.
std::atomic<bool> g_flatPadTouched{false};   // [input] flat_pad_on_touch: the real pad was touched in flat mode - the pads go through from then on
bool FlatNoPad() { return FlatKeyMode() && g_set.flatNoPad.load() && !g_flatPadTouched.load(); }
// VR third person (F2) played with the mouse and keyboard ([input] vr_keys_walk):
// W A S D walk through the same keyboard move, ahead = where our camera looks.
// With mouse look (F2 starts with it, F9 frees the pointer) it is flat's F2:
// the mouse turns the camera, the pointer is our crosshair in the middle and a
// click goes there (KeyMoveMode below). The game's own Move keys go north on its screen whatever
// the camera does ("W walks backwards when the camera faces south", 2026-10-07).
// The mouse stays the game's: a click attacks, picks up or walks there as ever.
// Only while the game's UI is in mouse mode - it asks 0x8A960 only then; a pad
// stick (BodyWalk's) flips it to the controller until the next click.
bool VrKeyWalk() {
    return g_set.platform.load() == 1 && g_set.vrKeyWalk.load() && g_keyMove.load() != 2 && g_enabled.load() &&
           g_inWorld.load() && ViewNow() == 2;
}
// W A S D as the keyboard move wants them, turned by our camera; false = nothing held.
bool VrKeyInput(float* px, float* py) {
    if (!GameFocused() || ChatOpen()) return false;
    auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
    float y = (held('W') ? 1.0f : 0.0f) - (held('S') ? 1.0f : 0.0f);
    float x = (held('D') ? 1.0f : 0.0f) - (held('A') ? 1.0f : 0.0f);
    if (x == 0.0f && y == 0.0f) return false;
    if (x != 0.0f && y != 0.0f) { x *= 0.70710678f; y *= 0.70710678f; }
    ViewStickToGame(&x, &y);
    const float l = sqrtf(x * x + y * y);
    if (l > 1.0f) { x /= l; y /= l; }
    *px = x; *py = y;
    return l > 1e-4f;
}
// The game in mouse mode with W A S D through its keyboard move: flat, or VR F2.
// With mouse look on top the mouse is the game's, held in the middle (our
// crosshair, a click goes where the camera looks); the old pad stick
// (KeysAsStick) only where neither is.
bool KeyMoveMode() { return FlatKeyMode() || VrKeyWalk(); }

// VR walk straight from BodyWalk ([input] direct_walk): its stick, read from its
// own report (pad_mirror_shared.h), goes into the two places the game takes the
// walking vector from - 0x8A960 in mouse mode (HookKeyMove) and the walk's call of
// ControllerInputHandler's stick getter 0x13CF10 in controller mode (HookStickGet)
// - so the game's pad left stick is not needed and is kept from it in the world.
// Our camera's views only (F2-F4: the stick turned like any other), no menu, no chat.
std::atomic<int> g_stickHook{0};   // HookStickGet: 0 not in yet, 1 in, 2 not possible
bool DirectWalkNow() {
    return g_set.platform.load() == 1 && g_set.directWalk.load() && g_enabled.load() && g_inWorld.load() &&
           (!gamestate::MenuOpen() || g_handWalk.load()) && !ChatOpen();   // ([openxr] hand_inventory: the inventory on the hand walks on)
}
// BodyWalk's stick as the game takes a walking vector (x right, y up on its screen,
// length <= 1, its dead zone 10349 of 32767 with no rescale), turned by our camera.
// own: it is the controller's left stick alone (direct mode) - not the room walk's stick, not BodyWalk's walk or the two mixed.
bool BodyWalkWalk(float* px, float* py, bool* own = nullptr) {
    if (own) *own = false;
    BWPadMirror m;
    if (!PadMirrorRead(&m)) return false;
    const float lx = (float)m.thumb_lx, ly = (float)m.thumb_ly;
    const bool room = roomwalkwire::IsRoomStick(m.thumb_lx, m.thumb_ly);
    // (the room walk's own stick passes under the pad's dead zone: its slow steady walk, the game's walk code and server strength as for any tilt)
    if (lx * lx + ly * ly <= 10349.0f * 10349.0f && !room) return false;
    if (own) {
        using walkpath::State;
        const State st = (State)g_walkState.load();
        *own = !room && st != State::NotDirect && st != State::BodyWalkAlone && st != State::Sum && st != State::OwnStronger &&
               st != State::BodyWalkStronger && st != State::SameWay;
    }
    float x = std::clamp(lx / 32767.0f, -1.0f, 1.0f), y = std::clamp(ly / 32767.0f, -1.0f, 1.0f);
    ViewStickToGame(&x, &y);
    const float l = sqrtf(x * x + y * y);
    if (l > 1.0f) { x /= l; y /= l; }
    *px = x; *py = y;
    return l > 1e-4f;
}

// What the player asks for now, as the game's keyboard move wants it (x right,
// y up on the game's screen, length <= 1); false = nothing (the game then
// stops the hero itself). W A S D while our mouse look is on, the window in
// front and the chat line shut, relative to the camera; else BodyWalk's stick,
// with the game's own dead zone (0x1210DA0: 10349, no rescale), relative to
// the camera while ours is on.
bool FlatMoveInput(float* px, float* py, const char** from) {
    float x = 0.0f, y = 0.0f;
    if (ShooterActive() && GameFocused() && !ChatOpen()) {
        auto held = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        y = (held('W') ? 1.0f : 0.0f) - (held('S') ? 1.0f : 0.0f);
        x = (held('D') ? 1.0f : 0.0f) - (held('A') ? 1.0f : 0.0f);
        if (x != 0.0f && y != 0.0f) { x *= 0.70710678f; y *= 0.70710678f; }
        *from = "keys";
    }
    if (x == 0.0f && y == 0.0f) {
        BWPadMirror m;
        if (!PadMirrorRead(&m)) return false;
        const float lx = (float)m.thumb_lx, ly = (float)m.thumb_ly;
        if (lx * lx + ly * ly <= 10349.0f * 10349.0f) return false;
        x = std::clamp(lx / 32767.0f, -1.0f, 1.0f);
        y = std::clamp(ly / 32767.0f, -1.0f, 1.0f);
        *from = "BodyWalk stick";
    }
    if (g_enabled.load()) ViewStickToGame(&x, &y);
    const float l = sqrtf(x * x + y * y);
    if (l > 1.0f) { x /= l; y /= l; }
    *px = x; *py = y;
    return l > 1e-4f;
}

// The game's gates are its own: the Up byte set for the call, the original
// run, the bytes put back. Its answer non-zero = every gate passed (it also
// marks "moved by keys", 0x2A23745, which the game's stop reads) - then our
// vector goes back instead. With no input the original runs untouched.
uint64_t HookKeyMove() {
    g_keyMoveCalls.fetch_add(1, std::memory_order_relaxed);
    // A move key let go while another window was in front (the settings window, its
    // sliders) never reached the game: its Move byte stayed set and the hero walked on
    // by himself (2026-10-07). With the game not in front its move bytes are cleared;
    // a key still held sets them again as soon as it is back.
    if ((FlatKeyMode() || VrKeyWalk()) && !GameFocused()) {
        if (volatile uint8_t* k = (volatile uint8_t*)d2rsig::Addr(RVA_MOVE_KEYS)) k[0] = k[1] = k[2] = k[3] = 0;
    }
    // [input] portal_auto (from sajmonsoft's flat fork): after a portal the hero did not walk on with the key still held - the move bytes of
    // before the load are cleared once when the new area is set, and a key held sets them again below
    if (g_keysReset.exchange(false)) {
        if (volatile uint8_t* k = (volatile uint8_t*)d2rsig::Addr(RVA_MOVE_KEYS)) k[0] = k[1] = k[2] = k[3] = 0;
    }
    float x = 0.0f, y = 0.0f;
    const char* from = "";
    bool input = false;
    if (FlatKeyMode()) input = FlatMoveInput(&x, &y, &from);
    else if (VrKeyWalk() && VrKeyInput(&x, &y)) { input = true; from = "VR keys"; }
    else if (DirectWalkNow() && BodyWalkWalk(&x, &y)) { input = true; from = "BodyWalk, straight"; }
    if (!input) return OrigKeyMove();
    volatile uint8_t* keys = (volatile uint8_t*)d2rsig::Addr(RVA_MOVE_KEYS);
    if (!keys) return OrigKeyMove();
    const uint8_t keep[4] = {keys[0], keys[1], keys[2], keys[3]};
    keys[0] = 1; keys[1] = 0; keys[2] = 0; keys[3] = 0;
    const uint64_t r = OrigKeyMove();
    for (int i = 0; i < 4; ++i) keys[i] = keep[i];
    const bool ok = r != 0;
    static ULONGLONG lastLog = 0;
    if (const ULONGLONG now = GetTickCount64(); now - lastLog >= 1000) {
        lastLog = now;
        LogF("vrcam: flat move v=(%.2f,%.2f) gate=%s (%s)", x, y, ok ? "ok" : "blocked", from);
    }
    if (!ok) return r;
    uint64_t out = 0;
    const float v[2] = {x, y};
    memcpy(&out, v, sizeof out);
    return out;
}

// Only in flat mode with the key on: VR never has this hook. Tried twice a
// second; the page decrypts the first time the game runs it (in a game area,
// in mouse mode), so only 10 s in a game area without the bytes means another build.
void InstallKeyMoveHook() {
    static int misses = 0;
    // Flat with flat_keyboard_move, or VR with vr_keys_walk (third person, VrKeyWalk).
    const bool flat = g_set.platform.load() == 0;
    if (!g_ctx || g_keyMove.load() != 0 || !(flat ? g_set.flatKeyMove.load() : g_set.vrKeyWalk.load() || g_set.directWalk.load())) return;
    if (!Matches(RVA_KEY_MOVE, kSigKeyMove, sizeof kSigKeyMove)) {
        // In VR the game may stay on the pad for a long time, its keyboard move never
        // run and so never decrypted: no verdict there, only tried again.
        if (flat && g_inWorld.load() && ++misses >= 20) {
            g_keyMove.store(2);
            Log("vrcam: flat: keyboard move hook NOT possible (0x8A960 not as expected - another build?) - W A S D walk the old way, through a pad");
        }
        return;
    }
    const bool in = d2rsig::Hook(RVA_KEY_MOVE, kSigKeyMove, sizeof kSigKeyMove, (void*)&HookKeyMove, (void**)&OrigKeyMove);
    g_keyMove.store(in ? 1 : 2);
    if (flat)
        Log(in ? "vrcam: flat: keyboard move hook in - W A S D walk through the game's own keyboard move, the mouse clicks where you look"
               : "vrcam: flat: keyboard move hook FAILED - W A S D walk the old way, through a pad");
    else
        Log(in ? "vrcam: VR: keyboard move hook in - in third person (F2) W A S D walk where the camera looks"
               : "vrcam: VR: keyboard move hook FAILED - W A S D in third person stay the game's own keys");
}

// ControllerInputHandler's walking stick, 0x13CF10(handler, float2* out, player):
// copies the stick its events left (handler + 0x20 + player * 0x1C8) into out and
// returns out. Six callers (the walk, the attack's target, panels); only the walk's
// (0x14C642, returning to 0x14C647) gets BodyWalk's stick instead - nothing while it
// is in its dead zone, so the pad's stick never walks on its own.
constexpr uint64_t RVA_STICK_GET = 0x13CF10, RVA_WALK_STICK_RET = 0x14C647;
// mov [rsp+8], rbx; mov [rsp+0x18], rsi; push rdi; sub rsp, 0x20; mov edi, r8d (no rip, no rel32)
const uint8_t kSigStickGet[15] = {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20};
using StickGetFn = float* (*)(void* handler, float* out, uint32_t player);
StickGetFn OrigStickGet;
bool StickHookIn() { return g_stickHook.load() == 1; }

// [direct] walk_by_lean (vr/lean_gait.h): the game's own stickRunToggleThreshold, the ControllerManager's float at +0x1F94 (0x14AA10 compares
// the stick's length with it), read-only; and the game's Run / walk switch (Always run, u32 RunLock: non-zero runs - 0x8AFD0 reads it so).
constexpr uint64_t RVA_CONTROLLER_MGR = 0x2A4EAB0, RVA_RUN_LOCK = 0x2A2375C;
constexpr uint32_t kRunThresholdOff = 0x1F94;
float GameRunThreshold() {
    const uintptr_t mgr = d2rsig::Addr(RVA_CONTROLLER_MGR);
    float t = 0.0f;
    return mgr && SafeRead(&t, (void*)(mgr + kRunThresholdOff), sizeof t) ? t : 0.0f;
}
bool GameRunSwitchOn() {
    const uintptr_t a = d2rsig::Addr(RVA_RUN_LOCK);
    uint32_t v = 0;
    return a && SafeRead(&v, (void*)a, sizeof v) && v != 0;
}
// The walking vector (x, y) for the walk's call, the controller's own stick leaning: walk up to walk_by_lean_pct, run past it - its length shaped
// to the game's own threshold, the game's Run key held past the limit while its switch is on walk. VR F4 with direct input only; off, or not the
// controller's stick alone (the room walk, BodyWalk's walk): as before. step: the call that keeps the Run key (player 0's, once a game frame).
void LeanGait(bool walking, bool own, float* x, float* y, bool step) {
    static leangait::RunKey key;
    static bool held = false;
    static int told = -1;
    static float toldLimit = -1.0f, toldT = -1.0f;
    const bool on = g_set.walkByLean.load() && g_set.platform.load() == 1 && viewgate::F4() && directinput::Active();
    const float limit = g_set.walkByLeanLimit.load();
    const float t = on ? GameRunThreshold() : 0.0f;
    const bool steer = on && leangait::SaneThreshold(t);
    float lean = 0.0f;
    if (steer && walking && own) {
        lean = sqrtf(*x * *x + *y * *y);
        if (lean > 1e-4f) {
            const float s = leangait::Shape(lean, limit, t) / lean;
            *x *= s; *y *= s;
        }
    }
    if (!step) return;
    const bool want = key.Step(lean, limit, GameRunSwitchOn());
    if (want != held) { held = want; directinput::Command(leangait::kRunCommand, held, directinput::kSrcOther5); }
    const int now = !on ? 0 : steer ? 1 : 2;
    if (now != told || (now == 1 && (limit != toldLimit || t != toldT))) {
        told = now; toldLimit = limit; toldT = t;
        if (now == 1)
            LogF("vrcam: walk by lean - the left stick walks up to %.0f %% of its lean and runs past it ([direct] walk_by_lean, walk_by_lean_pct): its length "
                 "shaped to the game's own run threshold %.2f (controllersettings), the game's Run key held past it while its Run / walk switch is on walk",
                 limit * 100.0f, t);
        else if (now == 2)
            LogF("vrcam: walk by lean - the game's run threshold not readable (%.3f): the stick walks and runs as the game has it ([direct] walk_by_lean)", t);
        else
            Log("vrcam: walk by lean - off (or not VR F4 with direct input): the stick walks and runs as the game has it");
    }
}

// [debug] walk_start_log (vr/walk_latency.h): fed from the walk's own stick call, once a game frame in the world. Reads only.
void WalkStartNote(bool given, float x, float y) {
    static walklat::Watcher w;
    static char head[96];
    const uint32_t raw = g_bwStickRaw.load(std::memory_order_relaxed);
    walklat::Sample s;
    s.t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    s.bw = std::min(1.0f, std::sqrt((float)(int16_t)(raw & 0xFFFF) * (float)(int16_t)(raw & 0xFFFF) +
                                    (float)(int16_t)(raw >> 16) * (float)(int16_t)(raw >> 16)) / 32767.0f);
    const walkpath::State st = (walkpath::State)g_walkState.load(std::memory_order_relaxed);
    if (st != walkpath::State::NotDirect) {   // direct mode: the controller's (or the room walk's) stick is in the mix
        const uint64_t both = g_walkRaw.load(std::memory_order_relaxed);
        s.ownPushed = walkpath::PushedRaw((int)(int16_t)(both & 0xFFFF), (int)(int16_t)((both >> 16) & 0xFFFF));
    }
    s.given = given;
    s.givenLen = std::sqrt(x * x + y * y);
    s.lifted = given && g_bwStickLifted.load(std::memory_order_relaxed);
    s.heroOk = true;
    s.heroX = g_heroPosNow[0]; s.heroZ = g_heroPosNow[2];
    walklat::Result res;
    switch (w.Feed(s, &res)) {
    case walklat::Event::Started: {
        SYSTEMTIME lt; GetLocalTime(&lt);
        snprintf(head, sizeof head, "walk start (BodyWalk): stick stirred %.2f at %02d:%02d:%02d.%03d", s.bw, lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds);
        break;
    }
    case walklat::Event::Done: {
        char tail[320];
        walklat::Text(res, tail, sizeof tail);
        LogF("vrcam: %s%s", head, tail);
        break;
    }
    default: break;
    }
}

float* HookStickGet(void* handler, float* out, uint32_t player) {
    float* r = OrigStickGet(handler, out, player);
    if (r && g_handWalk.load(std::memory_order_relaxed) && (uintptr_t)_ReturnAddress() == d2rsig::Addr(RVA_WALK_STICK_RET))
        g_handWalkReads.fetch_add(1, std::memory_order_relaxed);   // [openxr] hand_inventory: the game's walk got past its own gate to the stick
    if (r && (uintptr_t)_ReturnAddress() == d2rsig::Addr(RVA_WALK_STICK_RET) && !DirectWalkNow() && player == 0)
        LeanGait(false, false, nullptr, nullptr, true);   // (a menu, the chat: the Run key let go)
    if (r && (uintptr_t)_ReturnAddress() == d2rsig::Addr(RVA_WALK_STICK_RET) && DirectWalkNow()) {
        float x = 0.0f, y = 0.0f;
        bool own = false;
        const bool on = BodyWalkWalk(&x, &y, &own);
        LeanGait(on, own, &x, &y, player == 0);   // [direct] walk_by_lean
        r[0] = on ? x : 0.0f;
        r[1] = on ? y : 0.0f;
        if (g_set.walkStartLog.load(std::memory_order_relaxed)) WalkStartNote(on, x, y);   // [debug] walk_start_log
        static ULONGLONG lastLog = 0;
        if (on && GetTickCount64() - lastLog >= 2000) {
            lastLog = GetTickCount64();
            // whose stick it is: in direct mode this line used to say "BodyWalk" for the controller's own stick too
            const walkpath::State st = (walkpath::State)g_walkState.load();
            const uint64_t raw = g_walkRaw.load();
            if (st == walkpath::State::NotDirect)
                LogF("vrcam: walk straight from BodyWalk (controller mode) v=(%.2f,%.2f)", x, y);
            else
                LogF("vrcam: walk straight (controller mode) v=(%.2f,%.2f) - %s (raw sticks: the controller's %d %d, BodyWalk's %d %d)", x, y, walkpath::WhoWalks(st),
                     (int)(int16_t)(raw & 0xFFFF), (int)(int16_t)((raw >> 16) & 0xFFFF), (int)(int16_t)((raw >> 32) & 0xFFFF), (int)(int16_t)((raw >> 48) & 0xFFFF));
        }
    }
    return r;
}

// VR with direct_walk; tried with the others twice a second. The code decrypts the
// first time it runs (in controller mode), so no "not possible" verdict by time.
void InstallStickHook() {
    if (!g_ctx || g_stickHook.load() != 0 || g_set.platform.load() != 1 || !g_set.directWalk.load()) return;
    if (!Matches(RVA_STICK_GET, kSigStickGet, sizeof kSigStickGet) || !d2rsig::Addr(RVA_WALK_STICK_RET)) return;
    const bool in = d2rsig::Hook(RVA_STICK_GET, kSigStickGet, sizeof kSigStickGet, (void*)&HookStickGet, (void**)&OrigStickGet);
    g_stickHook.store(in ? 1 : 2);
    Log(in ? "vrcam: VR: walking stick hook in - BodyWalk's stick walks the hero straight, the pad's left stick is kept from the game"
           : "vrcam: VR: walking stick hook FAILED - the hero walks through the pad's left stick as before");
}

}  // namespace vrc
