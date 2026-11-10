// key_capture.h - "press a key" for a binding (Serg, 2026-11-09: "binding keys is not hard, it can even be done in our overlay"; "in the gestures
// we wanted key binds too"). One capture for every overlay that binds a key: the flat in-game settings panel's key rows (vr/flat_menu.h Kind::Key:
// [input] flat_pickup_key) and the Gestures tab's "Keyboard key" tile (vr/skill_wheel.cpp: the key's game command onto the gesture). A row arms it;
// the next key pressed in the game's window (vr/hud/window.cpp -> vr/key_capture_wire.cpp) is the answer, never the game's; Esc cancels; a key that
// cannot be bound for that use is said and the capture waits on; 10 s with no key and it lets go.
// Pure (no Windows calls; the virtual-key codes are Windows' numbers): cleanroom/tests/key_capture_test.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace keycap {

constexpr uint64_t kTimeoutMs = 10000;
constexpr unsigned kEsc = 0x1B;

// What the key is for: each use refuses the keys it cannot have.
enum class Use : uint8_t {
    PickupKey,   // [input] flat_pickup_key: a key of the flat first-person play (not W A S D, not Alt - the names, not F1 - F4 / F9 - F12 - ours)
    GameKey,     // a gesture's key: any key the game may have bound (its own command is what the gesture presses)
    MapKey,      // [hud] flat_map_*_key: the flat game's map keys - turn, place, zoom (any view: not W A S D, not Tab - the game's own map -,
                 // not F1 - F4 / F9 - F12, not Esc; a keyboard key, the window's key messages are what is read)
};

// A key's name as a keyboard shows it; nullptr: none known (Text writes its number then).
inline const char* Name(unsigned vk) {
    static char letters[26][2], digits[10][2];
    static char fkeys[24][4], pad[10][7];
    static bool made = false;
    if (!made) {
        made = true;
        for (int i = 0; i < 26; ++i) { letters[i][0] = char('A' + i); letters[i][1] = 0; }
        for (int i = 0; i < 10; ++i) { digits[i][0] = char('0' + i); digits[i][1] = 0; }
        for (int i = 0; i < 24; ++i) std::snprintf(fkeys[i], sizeof fkeys[i], "F%d", i + 1);
        for (int i = 0; i < 10; ++i) std::snprintf(pad[i], sizeof pad[i], "Num %d", i);
    }
    if (vk >= 'A' && vk <= 'Z') return letters[vk - 'A'];
    if (vk >= '0' && vk <= '9') return digits[vk - '0'];
    if (vk >= 0x70 && vk <= 0x87) return fkeys[vk - 0x70];
    if (vk >= 0x60 && vk <= 0x69) return pad[vk - 0x60];
    switch (vk) {
        case 0x04: return "Middle mouse";
        case 0x05: return "Mouse 4";
        case 0x06: return "Mouse 5";
        case 0x08: return "Backspace";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case 0x10: return "Shift";
        case 0x11: return "Ctrl";
        case 0x12: return "Alt";
        case 0x13: return "Pause";
        case 0x14: return "Caps Lock";
        case 0x1B: return "Esc";
        case 0x20: return "Space";
        case 0x21: return "Page Up";
        case 0x22: return "Page Down";
        case 0x23: return "End";
        case 0x24: return "Home";
        case 0x25: return "Left";
        case 0x26: return "Up";
        case 0x27: return "Right";
        case 0x28: return "Down";
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";
        case 0x6A: return "Num *";
        case 0x6B: return "Num +";
        case 0x6D: return "Num -";
        case 0x6E: return "Num .";
        case 0x6F: return "Num /";
        case 0xA0: return "Left Shift";
        case 0xA1: return "Right Shift";
        case 0xA2: return "Left Ctrl";
        case 0xA3: return "Right Ctrl";
        case 0xA4: return "Left Alt";
        case 0xA5: return "Right Alt";
        case 0xBA: return ";";
        case 0xBB: return "=";
        case 0xBC: return ",";
        case 0xBD: return "-";
        case 0xBE: return ".";
        case 0xBF: return "/";
        case 0xC0: return "`";
        case 0xDB: return "[";
        case 0xDC: return "\\";
        case 0xDD: return "]";
        case 0xDE: return "'";
        default: return nullptr;
    }
}
inline void Text(unsigned vk, char* out, size_t n) {
    if (!out || !n) return;
    if (const char* s = Name(vk)) std::snprintf(out, n, "%s", s);
    else std::snprintf(out, n, "Key %u", vk);
}

// Why a key cannot be bound for this use; nullptr: it can. Esc is the capture's cancel, never an answer.
inline const char* Refusal(unsigned vk, Use use) {
    if (vk == 0 || vk > 0xFE) return "not a key";
    if (vk == 0x01 || vk == 0x02) return "the mouse's left and right buttons are the game's clicks";
    if (vk == 0x10 || vk == 0x11 || vk == 0x12 || (vk >= 0xA0 && vk <= 0xA5) || vk == 0x5B || vk == 0x5C) return "Shift, Ctrl, Alt and Windows alone are not bound";
    if (vk == 0x0D) return "Enter opens the chat line";
    if (use == Use::PickupKey) {
        if (vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D') return "W A S D walk";
        if ((vk >= 0x70 && vk <= 0x73) || (vk >= 0x78 && vk <= 0x7B)) return "F1 - F4 pick the view, F9 - F12 are the mod's own keys";
        if (vk == 0x05 || vk == 0x06 || vk == 0x04) return "a mouse button is not taken from the game here - a keyboard key";
    }
    if (use == Use::MapKey) {
        if (vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D') return "W A S D walk";
        if (vk == 0x09) return "Tab opens and shuts the game's own map";
        if ((vk >= 0x70 && vk <= 0x73) || (vk >= 0x78 && vk <= 0x7B)) return "F1 - F4 pick the view, F9 - F12 are the mod's own keys";
        if (vk == 0x05 || vk == 0x06 || vk == 0x04) return "a mouse button is not taken from the game here - a keyboard key";
    }
    return nullptr;
}
inline bool Bindable(unsigned vk, Use use) { return Refusal(vk, use) == nullptr; }

// The capture: armed by a row (its owner: any number the overlay chooses), fed every key message of the game's window.
enum class R : uint8_t { Pass, Swallow, Took, Cancelled, Refused, TimedOut };
struct Res { R r = R::Pass; unsigned vk = 0; };

class Capture {
public:
    void Arm(int owner, Use use, uint64_t now) { owner_ = owner; use_ = use; at_ = now; armed_ = true; }
    void Cancel() { armed_ = false; }
    bool Armed(uint64_t now) const { return armed_ && now - at_ < kTimeoutMs; }
    int Owner() const { return owner_; }
    Use UseOf() const { return use_; }
    // One key message: down (a press or its repeat) or up. Pass: not ours; Swallow: ours, nothing new; the rest: what the owner is told.
    Res Feed(unsigned vk, bool down, bool repeat, uint64_t now) {
        if (!down) {   // the release of the key that answered (or cancelled) is ours too; every other release goes through (a key held before
                       // the capture - W walking - must come up in the game)
            if (eatUp_ && vk == eatUp_) { eatUp_ = 0; return {R::Swallow, vk}; }
            return {R::Pass, vk};
        }
        if (!armed_) return {R::Pass, vk};
        if (now - at_ >= kTimeoutMs) { armed_ = false; return {R::TimedOut, 0}; }   // (this press is the game's)
        if (repeat) return {R::Swallow, vk};
        if (vk == kEsc) { armed_ = false; eatUp_ = vk; return {R::Cancelled, vk}; }
        if (!Bindable(vk, use_)) return {R::Refused, vk};
        armed_ = false;
        eatUp_ = vk;
        return {R::Took, vk};
    }
    // Once a frame with no key: timed out?
    bool Expire(uint64_t now) {
        if (armed_ && now - at_ >= kTimeoutMs) { armed_ = false; return true; }
        return false;
    }

private:
    bool armed_ = false;
    int owner_ = -1;
    Use use_ = Use::PickupKey;
    uint64_t at_ = 0;
    unsigned eatUp_ = 0;
};

}  // namespace keycap
