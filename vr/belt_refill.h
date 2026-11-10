// belt_refill.h - [belt] refill_key: the belt filled from the inventory with one button (Settings > Hands > Belt refill), in every view.
//
// A player (a sorceress drinks mana potions all the time): refilling the belt by hand is a pain with a controller in VR - a long press of X on each
// potion, or the laser with Shift held on the other grip. Serg (2026-11-09): "заместо шифт клик можно забиндить какуюто клавишу", "например B";
// "когда открыт инвентарь наводишь лучем и нажимаешь b"; "зажатие b заполняет весь пояс".
//
// With the inventory open:
//  - a TAP of the button (B on the right controller by default; keyboard B too) is the game's own Shift + left click where the pointer is - the laser's
//    point in VR (every view: the laser is the mouse on the menus), the mouse cursor in the flat game. On a potion that is the game's own "to the belt";
//    on anything else whatever Shift + click does there (the game decides). The keys go in as the keyboard's and the mouse's own (SendInput), a few ms
//    apart, so Shift is down when the click is read.
//  - a HOLD of it (refill_hold_ms) is the game's own "hold to populate belt" - what its controller layout does on a long press over the belt
//    (ControllerPromptHoldToPopulateBelt): BeltWidget's input handler (0x2283E0) asks its own gate (0x226060: an empty slot a potion of the inventory can
//    go to), has the game plan the moves (0x386FF0: each column the potion it already holds, the game's own rule) and sends that plan as ONE command
//    (0x5D, 0xECA70) - the server checks and moves every potion. We call the same functions in the same order, on the game's UI thread
//    (vr/belt_refill.cpp); nothing of the item tables is written, nothing else is sent.
// One log line per press: "belt refill: ..." with how many went into the belt, of which kind (the belt read before and after, through the SDK).
// Off (unticked): the button and the key are the game's, as before.
//
// The pure parts (tested in cleanroom/tests/belt_refill_test.cpp): the press (tap or hold), the Shift + click's steps, the kinds of potion, the game's
// plan read back column by column, the belt before / after.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace beltrefill {

// ---- the settings ----
// [belt] refill_button: 0 none, 1 B (right controller), 2 A (right), 3 Y (left), 4 X (left). [belt] refill_keyboard: 0 none, 1 B, 2 V, 3 G, 4 H, 5 Y.
constexpr int kButtons = 5, kKeys = 6;
constexpr uint32_t kPrimary = 1u << 0, kSecondary = 1u << 1;   // BW_LINK_XRB_PRIMARY / SECONDARY (A / X, B / Y)
struct Pad { int hand = -1; uint32_t bit = 0; };   // hand 0 left, 1 right; bit 0 = none
inline Pad PadOf(int button) {
    switch (button) {
        case 1: return {1, kSecondary};
        case 2: return {1, kPrimary};
        case 3: return {0, kSecondary};
        case 4: return {0, kPrimary};
        default: return {};
    }
}
inline const char* PadName(int button) {
    static const char* const k[kButtons] = {"none", "B", "A", "Y", "X"};
    return button >= 0 && button < kButtons ? k[button] : "none";
}
inline unsigned KeyVk(int key) {
    static const unsigned k[kKeys] = {0, 'B', 'V', 'G', 'H', 'Y'};
    return key >= 0 && key < kKeys ? k[key] : 0;
}
constexpr uint32_t kHoldDefaultMs = 500, kHoldMinMs = 200, kHoldMaxMs = 2000;
inline uint32_t ClampHold(float ms) {
    if (!(ms == ms)) return kHoldDefaultMs;   // NaN
    if (ms < (float)kHoldMinMs) return kHoldMinMs;
    if (ms > (float)kHoldMaxMs) return kHoldMaxMs;
    return (uint32_t)ms;
}

// ---- the press: a tap (let go before holdMs) or a hold (once, at holdMs) ----
enum class Ev { None, Tap, Hold };
class Press {
public:
    Ev Step(bool down, uint64_t nowMs, uint32_t holdMs) {
        Ev e = Ev::None;
        if (down && !down_) { down_ = true; at_ = nowMs; held_ = false; }
        else if (down && !held_ && nowMs - at_ >= holdMs) { held_ = true; e = Ev::Hold; }
        else if (!down && down_) { if (!held_) e = Ev::Tap; down_ = false; held_ = false; }
        return e;
    }
    void Cancel() { down_ = false; held_ = false; }   // the press under way is forgotten (the context went away under it): no tap, no hold
    bool Down() const { return down_; }
private:
    bool down_ = false, held_ = false;
    uint64_t at_ = 0;
};

// ---- the Shift + click: Shift down, the left button down, up, Shift up - kStepMs apart (the game reads keys once a frame) ----
enum class Send { None, ShiftDown, ButtonDown, ButtonUp, ShiftUp };
constexpr uint64_t kStepMs = 30;
class Click {
public:
    void Start(uint64_t nowMs) { stage_ = 1; at_ = nowMs; }
    bool Busy() const { return stage_ != 0; }
    // The next thing to send now (None: not yet / nothing). After ShiftUp it is idle again.
    Send Step(uint64_t nowMs) {
        switch (stage_) {
            case 1: stage_ = 2; at_ = nowMs; return Send::ShiftDown;
            case 2: if (nowMs - at_ < kStepMs) return Send::None; stage_ = 3; at_ = nowMs; return Send::ButtonDown;
            case 3: if (nowMs - at_ < kStepMs) return Send::None; stage_ = 4; at_ = nowMs; return Send::ButtonUp;
            case 4: if (nowMs - at_ < kStepMs) return Send::None; stage_ = 0; return Send::ShiftUp;
            default: return Send::None;
        }
    }
    // Stopped half-way (the feature switched off, the session gone): what is still held down, to let go of - Shift and / or the button.
    void Abort(bool* shiftHeld, bool* buttonHeld) {
        *shiftHeld = stage_ >= 2;
        *buttonHeld = stage_ == 3;   // (stage 3: the button went down, its release not yet sent)
        stage_ = 0;
    }
private:
    int stage_ = 0;
    uint64_t at_ = 0;
};

// ---- the kinds of potion (Misc.txt codes: "hp1".."hp5", "mp1".."mp5", "rvs", "rvl") ----
enum Kind { kNone = 0, kHealth, kMana, kRejuv, kOther, kKinds };
inline Kind KindOf(uint32_t code) {
    if (!code) return kNone;
    const char a = (char)(code & 0xFF), b = (char)((code >> 8) & 0xFF), c = (char)((code >> 16) & 0xFF);
    if (a == 'h' && b == 'p' && c >= '1' && c <= '5') return kHealth;
    if (a == 'm' && b == 'p' && c >= '1' && c <= '5') return kMana;
    if (a == 'r' && b == 'v' && (c == 's' || c == 'l')) return kRejuv;
    return kOther;   // antidote, thawing, stamina, a scroll... whatever the game lets into the belt
}
inline const char* KindName(int k) {
    static const char* const n[kKinds] = {"nothing", "health", "mana", "rejuvenation", "other"};
    return k >= 0 && k < kKinds ? n[k] : "?";
}
constexpr uint32_t Code3(char a, char b, char c) { return (uint32_t)(uint8_t)a | (uint32_t)(uint8_t)b << 8 | (uint32_t)(uint8_t)c << 16 | (uint32_t)' ' << 24; }

// ---- the belt: 16 slots, slot = column + 4 * row (the game's own numbering: column slot % 4, row slot / 4) ----
constexpr int kSlots = 16, kColumns = 4;
inline int ColumnOf(int slot) { return slot >= 0 && slot < kSlots ? slot % kColumns : -1; }
inline int RowOf(int slot) { return slot >= 0 && slot < kSlots ? slot / kColumns : -1; }
struct Belt {
    uint32_t code[kSlots] = {};
    uint32_t id[kSlots] = {};   // the game's unit id, 0 empty
};
// What went in: per kind (index Kind) and in all - an item in a slot after that was not there before (a new id in that slot).
struct Moved {
    int count[kKinds] = {};
    int total = 0;
};
inline Moved Diff(const Belt& before, const Belt& after) {
    Moved m;
    for (int s = 0; s < kSlots; ++s) {
        if (!after.id[s] || after.id[s] == before.id[s]) continue;
        ++m.count[KindOf(after.code[s])];
        ++m.total;
    }
    return m;
}

// ---- the game's plan for its fill (0x386FF0's answer, what its command 0x5D carries): per move the item's id and the belt slot it goes to ----
constexpr int kPlanMax = 16;   // the command has room for 16 (0x62 bytes: 2 + 16 * 6)
struct Plan {
    int n = 0;
    uint32_t id[kPlanMax] = {};
    uint8_t slot[kPlanMax] = {};
};
// Which potion of the inventory goes to which column, of which kind: `codeOf(id)` is the item's code (0 not known). Per column and kind.
struct PlanView {
    int column[kColumns][kKinds] = {};
    int total = 0, badSlot = 0;
};
template <class CodeOf> PlanView View(const Plan& p, CodeOf codeOf) {
    PlanView v;
    const int n = p.n < 0 ? 0 : p.n > kPlanMax ? kPlanMax : p.n;
    for (int i = 0; i < n; ++i) {
        const int c = ColumnOf(p.slot[i]);
        if (c < 0) { ++v.badSlot; continue; }
        ++v.column[c][KindOf(codeOf(p.id[i]))];
        ++v.total;
    }
    return v;
}

// "2 health, 3 mana" (nothing: "none") - into out (n bytes).
inline void KindsText(const int count[kKinds], char* out, size_t n) {
    if (!n) return;
    out[0] = 0;
    size_t at = 0;
    for (int k = kHealth; k < kKinds; ++k) {
        if (count[k] <= 0) continue;
        char part[40];
        const int len = std::snprintf(part, sizeof part, "%s%d %s", at ? ", " : "", count[k], KindName(k));
        if (len <= 0 || at + (size_t)len + 1 > n) break;
        std::memcpy(out + at, part, (size_t)len + 1);
        at += (size_t)len;
    }
    if (!at) std::snprintf(out, n, "none");
}

// ---- when the button is ours ----
// The VR button (the laser's): ticked, the inventory open in a game, the laser usable (the game's window in front, the hand tracked, the picture a menu).
// The keyboard key: ticked, the inventory open in a game, the chat line closed. Off any of these: the button / key is the game's, as before.
struct When {
    bool on = false, inventoryOpen = false, inWorld = false, laserOk = false, chatOpen = false;
};
inline bool PadOurs(const When& w) { return w.on && w.inventoryOpen && w.inWorld && w.laserOk; }
inline bool KeyOurs(const When& w) { return w.on && w.inventoryOpen && w.inWorld && !w.chatOpen; }

}  // namespace beltrefill

// ---- the game's side (vr/belt_refill.cpp) ----
namespace D2RL { struct PluginContext; }
namespace beltrefill {
void Init(const D2RL::PluginContext* ctx);                     // D2RLoaderLoadPlugin
void Configure(bool on, int button, int key, float holdMs);    // LoadSettings: [belt] refill_key, refill_button, refill_keyboard, refill_hold_ms
Pad PadButton();                                               // the XR thread (LaserStep): the controller button, bit 0 when off or none
bool PadWanted(bool laserOk);                                  // ... is it ours now (decided while it is let go)
void FeedPad(bool down, bool pointing);                        // ... held (while ours), and the laser on the picture as the mouse
bool KeyMessage(unsigned msg, uintptr_t wParam, intptr_t lParam, bool chatOpen);   // the game window's procedure: true = the key is ours, swallowed
void Tick(bool inWorld);                                       // vrcam's frame: the press, the Shift + click's steps, the UI thread's jobs, the log
}  // namespace beltrefill

