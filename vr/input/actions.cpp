// vr/input/actions.cpp - cut from vr/vrcam.cpp (docs/plan_modules.md M10); what it gives: vr/input/input.h. It sees vrcam.cpp's headers.
#include "../vrcam_headers.h"

using namespace D2RL;
using namespace vrc;   // as in vrcam.cpp

namespace vrc {

// Shift goes in as a scan code (left Shift, 0x2A): the game reads keys by scan
// code, and a virtual-key-only Shift never reached it - the click came through
// alone and the hero walked to the point instead of shooting at it (0.19.2 log).
void SendShift(bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_LSHIFT;
    in.ki.wScan = 0x2A;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    in.ki.dwExtraInfo = typedkeys::kOwnKeyTag;   // never the player's typing ([openxr] laser_typing)
    SendInput(1, &in, sizeof in);
}
void SendLeftButton(bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof in);
}
// The press in two steps a frame apart, so Shift is already down when the
// click is read: 0 idle, 1 Shift down, 2 Shift and button down.
int g_aimStage = 0;
ULONGLONG g_aimStageAt = 0;
void SendKeysMouse(bool down) {
    if (down) { SendShift(true); g_aimStage = 1; g_aimStageAt = GetTickCount64(); }
    else {
        if (g_aimStage == 2) SendLeftButton(false);
        if (g_aimStage >= 1) SendShift(false);
        g_aimStage = 0;
    }
}

// The name of the program whose window has the keyboard (the log: why an Esc did not go the usual way).
std::string ForegroundProgram() {
    char name[MAX_PATH] = "none";
    DWORD pid = 0;
    if (HWND fg = GetForegroundWindow()) GetWindowThreadProcessId(fg, &pid);
    if (pid) {
        if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            char full[MAX_PATH] = {};
            DWORD n = (DWORD)sizeof full;
            if (QueryFullProcessImageNameA(p, 0, full, &n)) {
                const char* base = strrchr(full, '\\');
                strcpy_s(name, base ? base + 1 : full);
            }
            CloseHandle(p);
        }
    }
    return name;
}

// [direct] <control>_hold = action:pause (the left menu button held by default; on PS VR2 that is the right controller's Options button): the keyboard's Esc, tapped. The game has no command for it - its table
// ends with "close all panels" (0x26) - and Esc is what closes a panel or opens the pause menu, so the game's own key does it. The key goes down on the
// request and up 50 ms later (the game reads the press on a frame). F4 only (TakePauseToggle).
//  - The game's window has the keyboard: the key goes in as the keyboard's own, a scan code (as Shift above).
//  - It has not - the player's BodyWalk window is in front of it, as in a headset session it often is ("the game window behind BodyWalk", the log of
//    2026-10-17: the long press asked for Esc and the old version dropped it, "nothing happens") - the key is posted to the game's own window instead,
//    the very WM_KEYDOWN / WM_KEYUP its window procedure gets from the keyboard. Never into another program's window.
// Each request says which way it went, or why not.
std::atomic<ULONGLONG> g_postedEscAt{0};   // PauseKeyTick posted its Esc to the game's window then ([openxr] laser_typing: not the player's)
void PauseKeyTick() {
    static bool down = false, byMessage = false;
    static ULONGLONG downAt = 0;
    static int told = 0;
    const ULONGLONG now = GetTickCount64();
    const LPARAM scan = (LPARAM)(0x01u << 16);   // Esc's scan code; repeat count 1
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_ESCAPE;
    in.ki.wScan = 0x01;
    in.ki.dwExtraInfo = typedkeys::kOwnKeyTag;   // never the player's typing ([openxr] laser_typing)
    if (down) {
        if (now - downAt < 50) return;
        down = false;
        if (byMessage) {
            if (HWND w = GameWindow()) PostMessageW(w, WM_KEYUP, VK_ESCAPE, scan | (1 << 30) | (LPARAM)(1u << 31));
        } else {
            in.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
            SendInput(1, &in, sizeof in);
        }
        return;
    }
    if (!directinput::TakePauseToggle()) return;
    const bool focused = GameFocused();
    HWND w = focused ? nullptr : GameWindow();
    if (!focused && !w) {
        if (told++ < 60) Log("vrcam: direct input - pause: Esc NOT sent - the game's window is not the one with the keyboard and it was not found");
        return;
    }
    if (focused) {
        in.ki.dwFlags = KEYEVENTF_SCANCODE;
        SendInput(1, &in, sizeof in);
    } else {
        g_postedEscAt.store(now);   // (a posted key has no extra information: GameWndProc knows it by this)
        PostMessageW(w, WM_KEYDOWN, VK_ESCAPE, scan);
    }
    down = true;
    byMessage = !focused;
    downAt = now;
    if (told++ < 60)
        LogF("vrcam: direct input - pause: Esc sent to the game %s", focused ? "as the keyboard's (its window has the keyboard)"
                                                                           : ("posted to its window (the keyboard is in " + ForegroundProgram() + ")").c_str());
}

// Attack apart from pick-up / interact. Pad A is the default attack, and on a
// target it picks up, opens or talks instead - the game's
// allowInteractOnDefaultAttack (controllersettings.json, read into RVA
// 0x22BD840; asked by 0x145CB0 for skill slot 0 on a pad). With it off, A only
// attacks. BodyWalk's two actions press A with it off, or on.
constexpr uint64_t RVA_INTERACT_ON_ATTACK = 0x22BD840;

// What the pad would pick up / open / talk to now: the controller's interact
// target, InteractTarget (RVA 0x18D960, (controller manager, player id)),
// which the tooltip panel asks every frame (TooltipsPanel::Draw, 0x880ACE) for
// its "(A) Interact" prompt, and the A-press decision asks through 0x18D370.
// Hooked to remember the answer: "pick up" presses A only while there is a
// target, so it never turns into an attack. With the game's flag off (A only
// attacks) the target is asked again with it on, for us alone - the panel
// keeps its own answer.
InteractTargetFn OrigInteractTarget;
const uint8_t kSigInteractTarget[16] = {0x40,0x53,0x55,0x56,0x57,0x41,0x55,0x48,0x83,0xEC,0x20,0x4C,0x89,0x64,0x24,0x50};
std::atomic<uintptr_t> g_interactTarget{0};
std::atomic<ULONGLONG> g_interactTargetAt{0};
std::atomic<bool> g_interactHooked{false};   // = g_h.interact, which is declared further down
uintptr_t HookInteractTarget(uintptr_t mgr, uint32_t player);

bool InteractTargetNow() {
    return g_interactHooked.load() && g_interactTarget.load() != 0 && GetTickCount64() - g_interactTargetAt.load() < 200;
}

// [weight] grab_items (vr/item_grab.h): an empty hand's grip on an item lying on the ground picks it up through the game's own pick-up - the press of the layout's
// interact (pad A, the game's allowInteractOnDefaultAttack on for it, ApplyActions) with the controller's interact target answered with that item (HookInteractTarget)
// until the item has left the ground. The skeleton hook's thread steps it (ItemGrabStep, every pose); the pad hook and the target hook read the atomics. F4 only.
namespace itemgrabwire {
itemgrab::Picker g_picker;                      // the skeleton hook's thread
std::atomic<bool> g_pressing{false};
std::atomic<uintptr_t> g_target{0};
std::atomic<uintptr_t> g_gameTarget{0};         // what the game's own lookup answered during the press (the log)
std::atomic<int> g_heldSide{-1};                // the hand that shows our copy of the taken item ...
std::atomic<uint32_t> g_heldId{0};              // ... and the item
std::atomic<uint32_t> g_heldCode{0};            // ... its code (gamestate::CodeOfItemClass at the take; 0 not known: no copy drawn)
std::atomic<uint32_t> g_asks{0};                // the target lookups answered with ours in this press
double g_gripWas[2] = {};
int g_noneLines = 0;
double g_noneAt[2] = {-1e9, -1e9};
const char* Side(int s) { return s ? "left" : "right"; }
double Now() {
    static const double inv = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1.0 / (double)f.QuadPart; }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * inv;
}
void Publish() {
    g_pressing.store(g_picker.Pressing());
    g_target.store(g_picker.Target());
    g_heldSide.store(g_picker.HoldingHand());
    g_heldId.store(g_picker.HoldingHand() >= 0 ? g_picker.item().id : 0);
}
// The press in flight, for the pad hook and the target hook: F4 and a target.
bool Pressing() { return g_pressing.load() && g_target.load() != 0 && viewgate::F4(); }
}  // namespace itemgrabwire

void ItemGrabStep(skel::Input* in) {
    namespace ig = itemgrabwire;
    if (in->along) return;   // (a second rig posed along: the hero's pose stepped it)
    if (!in->itemsOn || !in->grabWorldOk) {
        if (ig::g_picker.phase() != itemgrab::Phase::Idle) LogF("grab items: the %s hand's pick-up ends - %s", ig::Side(ig::g_picker.side()), !in->itemsOn ? "the grab is off or not F4" : "no world");
        ig::g_picker.Reset();
        ig::Publish();
        return;
    }
    monsterbodies::View view;
    for (int k = 0; k < 3; ++k) { view.eye[k] = in->grabEye[k]; view.camR[k] = in->grabCamR[k]; view.camF[k] = in->grabCamF[k]; }
    view.upm = std::clamp(in->grabUpm, 1.0f, 12.0f);
    itemgrab::Hand hands[2];
    for (int side = 0; side < 2; ++side) {
        float head[3];
        for (int k = 0; k < 3; ++k) head[k] = in->hand[side][k] + in->handOffset[k];
        hands[side].tracked = (in->handsValid & (1u << side)) && in->grip[side] >= 0.0f && monsterbodies::HeadToRender(view, head, hands[side].palm);
        hands[side].free = in->itemsFree[side];
        hands[side].grip = in->grip[side];
    }
    itemgrab::Item items[itemgrab::kMaxItems];
    bool fresh = false;
    const int n = meleetouch::GroundItems(items, itemgrab::kMaxItems, &fresh);
    const double now = ig::Now();
    const itemgrab::Phase was = ig::g_picker.phase();
    const unsigned ev = ig::g_picker.Step(now, hands, items, n, fresh, in->itemsReachM);
    const int s = ig::g_picker.side();
    const uint32_t id = ig::g_picker.item().id;
    if (ev & itemgrab::kAsked) {
        ig::g_asks.store(0);
        ig::g_gameTarget.store(0);
        LogF("grab items: the %s hand's grip asks the game to pick up item %u (%.0f cm from the palm, %d items within 3 m) - the layout's interact pressed, the game's interact "
             "target answered with it", ig::Side(s), id, ig::g_picker.distance() * 100.0f, n);
    }
    if (ev & itemgrab::kTaken) {
        const uint32_t code = gamestate::CodeOfItemClass(ig::g_picker.item().cls);
        ig::g_heldCode.store(code);
        char c4[5] = {(char)(code & 0xFF), (char)(code >> 8 & 0xFF), (char)(code >> 16 & 0xFF), (char)(code >> 24 & 0xFF), 0};
        LogF("grab items: item %u is class %u, code '%s'%s", ig::g_picker.item().id, ig::g_picker.item().cls, code ? c4 : "?",
             code ? "" : " (the item codes by class are not known: no copy in the hand)");
        LogF("grab items: the game took item %u after %.2f s (its interact target answered with ours %u times)%s", id, now - (ig::g_picker.since() > 0 ? ig::g_picker.since() : now),
             ig::g_asks.load(), (ev & itemgrab::kDropped) ? " - the hand was let go: no copy in it" : " - the hand shows our copy while the grip is held");
        haptics::Play(haptics::Event::Interact, haptics::FromSkel(s));
    }
    if (ev & itemgrab::kNotTaken)
        LogF("grab items: the game did not take item %u in %.1f s (a full inventory? another player's? the hero could not reach it?) - our target was asked %u times, the game's own "
             "answer was %p", id, itemgrab::kPressSec, ig::g_asks.load(), (void*)ig::g_gameTarget.load());
    if (ev & itemgrab::kLetGoEarly) LogF("grab items: the %s hand let go before the game took item %u (asked %u times)", ig::Side(s), id, ig::g_asks.load());
    if ((ev & itemgrab::kDropped) && was == itemgrab::Phase::Holding) LogF("grab items: the %s hand opens - our copy of item %u goes (the item is the game's, in the inventory)", ig::Side(s), id);
    // a squeeze that asked nothing: why (throttled, 30 lines a session)
    for (int side = 0; side < 2; ++side) {
        const bool rising = hands[side].tracked && hands[side].grip >= itemgrab::kGripOn && ig::g_gripWas[side] < itemgrab::kGripOn;
        ig::g_gripWas[side] = hands[side].tracked ? hands[side].grip : 0.0;
        if (!rising || ((ev & itemgrab::kAsked) && side == s) || was != itemgrab::Phase::Idle || ig::g_noneLines >= 30 || now - ig::g_noneAt[side] < 1.5) continue;
        if (!hands[side].free && (grabwire::Holding() >> side & 1u)) continue;   // (the bodies' grab took it)
        float d = 0.0f;
        const int i = itemgrab::Nearest(items, n, hands[side].palm, 1e9f, &d);
        if (i < 0 && n == 0) continue;   // no items near: a grip for something else
        ig::g_noneAt[side] = now;
        ++ig::g_noneLines;
        if (!hands[side].free)
            LogF("grab items: the %s hand's grip picked up nothing - the hand may take nothing now (%s, or over or holding a body or a prop)", ig::Side(side), grabbodies::BusyText(in->grabBusy[side]));
        else
            LogF("grab items: the %s hand's grip picked up nothing - the nearest item (%u) is %.0f cm from the palm, the reach is %.0f cm (%d items within 3 m, the scan %s; "
                 "the palm at %.2f %.2f %.2f, the item at %.2f %.2f %.2f, render metres)%s", ig::Side(side), items[i].id, d * 100.0f, in->itemsReachM * 100.0f, n,
                 fresh ? "fresh" : "OLD", hands[side].palm[0], hands[side].palm[1], hands[side].palm[2], items[i].p[0], items[i].p[1], items[i].p[2],
                 ig::g_noneLines == 30 ? " (no more of these lines this session)" : "");
    }
    ig::Publish();
}

// The skeleton hook's step of the grabs: the items first (with the controllers' palms), then the bodies (which move a holding hand where it holds).
void GrabHandsStep(skel::Input* in) {
    ItemGrabStep(in);
    grabwire::Apply(in);
}

void ApplyActions(XINPUT_STATE* s) {
    static link::Presses presses;   // the game's pad polls only (index 0)
    uint32_t held = link::ActionsHeld(presses);
    if (bowdrawwire::Owned()) held &= ~D2RVR_ACT_ATTACK;   // [hands] bow_draw: the string hand's trigger is the draw's, not an attack
    {   // BodyWalk's actions as they are pressed - its gestures among them - for the input log
        static uint32_t was = 0;
        const uint32_t pressed = held & ~was;
        was = held;
        for (const D2RVRActionName& a : kD2RVRActions)
            if (pressed & a.bit) InputLogF("BodyWalk action '%s' (through Game Link: a button bound to it, or a gesture)", a.name);
    }
    // Pick up only onto a target: nothing there, nothing pressed - never an attack.
    // Without the hook (another build) it cannot tell, and stays a plain A.
    bool interact = (held & D2RVR_ACT_INTERACT) && (InteractTargetNow() || !g_interactHooked.load());
    static bool wasInteract = false, toldMiss = false;
    {   // [input] interact_one_press (F4 only, vr/interact_press.h): the target under the press is the press's - a chest opened by
        // a press does not also give up its loot, a hold over a pile takes one item; a menu opened or closed is a new context
        static interactpress::Press press;
        static uint32_t menuEpoch = 0;
        static bool menuWas = false;
        if (const bool menu = gamestate::MenuOpen(); menu != menuWas) { menuWas = menu; ++menuEpoch; }
        const bool held1 = (held & D2RVR_ACT_INTERACT) != 0;
        const uintptr_t target = InteractTargetNow() ? g_interactTarget.load() : 0;
        const interactpress::Press::Result one = press.Poll(held1, target, g_interactHooked.load(), gamestate::LocalPlayer(), menuEpoch);
        if (g_set.interactOnePress.load() && viewgate::F4()) {
            interact = one.down;
            if (one.lost) LogF("vrcam: pick up - the target changed during the press (now %p): A let go until pressed again", (void*)target);
            if (one.miss) toldMiss = false;   // (each spent press is said, below)
        }
    }
    if (itemgrabwire::Pressing()) interact = true;   // [weight] grab_items: the hand's grip on an item on the ground is the press of interact (its target answered below)
    if (viewgate::F4() && meleetouch::BarrelPressTarget()) interact = true;   // [gameplay] melee_touch_barrels: a swing through a barrel / pot is the press of interact on it
    if ((held & D2RVR_ACT_INTERACT) && !interact) {
        if (!toldMiss) { toldMiss = true; Log("vrcam: pick up - nothing to pick up or open here, A not pressed"); }
    } else toldMiss = false;
    if (interact != wasInteract) {
        wasInteract = interact;
        if (interact) {
            haptics::Play(haptics::Event::Interact);   // [openxr] haptics: a press with something there to pick up, open or talk to
            LogF("vrcam: pick up / interact - target %p", (void*)g_interactTarget.load());
        }
    }
    const bool touchPress = meleetouch::PadPressed();   // [gameplay] melee_touch, before the direct-input branch is in the tree
    // [gameplay] attack_in_place (F4): the game's own Stand Still is held with every attack press of ours, so a press with nothing
    // under the point is an attack in place and never a walk; the press waits the few ms the command takes to be down
    const bool attackA = (held & D2RVR_ACT_ATTACK) || touchPress;
    // (the touch's press, whichever route holds it: Stand Still reads it as the armed melee skill - a Whirlwind, a Leap Attack must move the hero - and names it "hit by touch")
    const standstill::Gate still = standstill::Poll(attackA, touchPress || (attackA && meleetouch::PressDown()), gamestate::MenuOpen(), s->Gamepad.wButtons, s->Gamepad.bLeftTrigger);
    if ((attackA && !still.holdBackA) || still.addA || interact) s->Gamepad.wButtons |= XINPUT_GAMEPAD_A;
    s->Gamepad.wButtons = (WORD)((s->Gamepad.wButtons & ~still.holdBack) | still.add);
    if (!gamestate::MenuOpen()) {   // [gameplay] move_while_attacking (F4, vr/move_attack.h): after a walk cut an attack short, the next attack or skill of ours waits
        // until that animation would have ended (a press in that time goes out then) - the game's attack rate; a pick-up's A is not held
        const uint16_t mask = (interact && !attackA) ? (uint16_t)(moveattack::kAttackButtons & ~moveattack::kPadA) : moveattack::kAttackButtons;
        const moveattack::PadOut ma = moveattack::Pad(GetTickCount64(), (uint16_t)(s->Gamepad.wButtons & mask));
        s->Gamepad.wButtons = (WORD)((s->Gamepad.wButtons & ~(ma.strip & mask)) | ma.add);
    }
    BowLatencyPad((s->Gamepad.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0);   // [debug] bow_latency: the attack or a skill button
    // A pad button, not the keyboard's I: a key press flips the game's UI to
    // mouse and keyboard, the way Shift + click did in bow mode 2. Menu (Start)
    // by default: View (Back) opens the map in D2R's pad layout - "the inventory
    // button opens the map" (2026-10-05); [input] inventory_button=1 puts it back.
    if (held & D2RVR_ACT_INVENTORY) s->Gamepad.wButtons |= g_set.inventoryPad.load() ? XINPUT_GAMEPAD_BACK : XINPUT_GAMEPAD_START;
    // Weapon swap: the right stick click, which is the game's swap on a pad
    // (log 2026-10-04: buttons 0080 -> "weapon set 2"). A pad button, so the
    // UI stays on the controller.
    if (held & D2RVR_ACT_SWAP) s->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
    // The map: a state of ours flipped on the press, never a button sent to the
    // game - a pad press only shuffles the game's map, and a key flips its UI to
    // keyboard. hud.cpp then publishes the map panel shown or hidden.
    static bool mapWas = false;
    if (const bool mapNow = (held & D2RVR_ACT_MAP) != 0; mapNow != mapWas) {
        mapWas = mapNow;
        // The game's own map, opened or closed by its own panel message (no
        // key); the orb follows whatever the game draws. Without the SDK's
        // widget service only the orb can be switched.
        if (mapNow) {
            const bool open = !gamestate::AutoMapOpen();
            if (gamestate::SetAutoMap(open)) LogF("vrcam: map %s in the game (D2R: Map action)", open ? "opening" : "closing");
            else LogF("vrcam: map %s - the orb only, the game's map cannot be set", hud::ToggleMapShown() ? "shown" : "hidden");
        }
    }
    // The game's flag, as it loaded it, kept to put back.
    static int original = -1;
    uint8_t* flag = (uint8_t*)d2rsig::Addr(RVA_INTERACT_ON_ATTACK);
    uint8_t now = 0;
    if (!flag || !SafeRead(&now, flag, 1) || now > 1) return;
    const bool separate = g_set.aAttackOnly.load() || (held & D2RVR_ACT_ATTACK) || touchPress;
    if (original < 0) { if (!separate && !interact) return; original = now; }
    const uint8_t want = interact ? 1 : separate ? 0 : (uint8_t)original;
    if (now != want) {
        *flag = want;
        static uint8_t told = 2;
        if (want != told) { told = want; LogF("vrcam: pad A %s", want ? "picks up / interacts on a target" : "only attacks"); }
    }
}

uintptr_t HookInteractTarget(uintptr_t mgr, uint32_t player) {
    static thread_local bool inside = false;
    const uintptr_t r = OrigInteractTarget(mgr, player);
    if (inside) return r;
    uintptr_t t = r;
    uint8_t* flag = (uint8_t*)d2rsig::Addr(RVA_INTERACT_ON_ATTACK);
    uint8_t f = 1;
    if (flag && SafeRead(&f, flag, 1) && f == 0) {   // A only attacks: ask as if it interacted, for us
        inside = true;
        *flag = 1;
        t = OrigInteractTarget(mgr, player);
        *flag = 0;
        inside = false;
    }
    if (const uintptr_t barrel = viewgate::F4() ? meleetouch::BarrelPressTarget() : 0) {   // [gameplay] melee_touch_barrels: the object the swing went through, while its press is in flight
        g_interactTarget.store(barrel);
        g_interactTargetAt.store(GetTickCount64());
        return barrel;
    }
    if (itemgrabwire::Pressing()) {   // [weight] grab_items: the item the hand's grip is on is the controller's interact target while the press is in flight
        const uintptr_t ours = itemgrabwire::g_target.load();
        itemgrabwire::g_gameTarget.store(t);
        itemgrabwire::g_asks.fetch_add(1);
        g_interactTarget.store(ours);
        g_interactTargetAt.store(GetTickCount64());
        return ours;
    }
    g_interactTarget.store(t);
    g_interactTargetAt.store(GetTickCount64());
    return r;
}

}  // namespace vrc
