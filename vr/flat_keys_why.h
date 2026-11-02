// flat_keys_why.h - why W A S D do not walk the hero in the flat game's mouse-look views (flat F2 / F3, [input] flat_keyboard_move) and in VR
// F2 with [input] vr_keys_walk (a monitor without a headset, the shipped ini: there the mouse look and a menu do not stop the keys). Pure: no
// game, no Windows, no clock of its own; the unit test is cleanroom/tests/flat_keys_why_test.cpp, the wiring is vr/input/pad.cpp
// (FlatPadTick, twice a second) with the facts from vr/input/walk.cpp (ShooterActive, HookKeyMove).
//
// Players report "W A S D stop working after a menu, switching views brings them back" (manager's PC and another player, 2026-11-01). The
// logs had nothing to say about it: the walk's own line ("flat move ... gate=ok/blocked") is written only when our walk is fed, and
// "the game does not ask for a keyboard move" only when the mouse look is on. So when the keys did nothing nobody could tell whether the
// plugin had stopped taking them (the mouse look off, our camera off, a menu it still counts open, the window not in front, the chat
// line) or the game had (its UI on the pad, one of its own gates). This names it in one line, when it changes while a key is held.
#pragma once

#include <cstdint>

namespace flatkeys {

// What the timer knows, twice a second, while flat F2 / F3 runs with the keyboard move.
struct Facts {
    bool keysHeld = false;   // W, A, S or D held now
    bool mouseLook = true;   // the mouse look is on (F9 turns it off; a view switch or a change of the ini sets it again)
    bool cameraOn = true;    // our camera is on (g_enabled)
    bool inWorld = true;     // a game area, not the title or character screens
    bool menuOpen = false;   // a panel the plugin counts as a menu is open (gamestate::MenuOpen)
    bool focused = true;     // the game's window is in front
    bool chatOpen = false;   // the chat line is open (the letters are typed)
    bool gameAsked = true;   // the game asked for its keyboard move since the last look (0x8A960 ran)
    bool gateOk = true;      // ... and its last answer with our vector passed its own gates
};

enum class Why : uint8_t {
    Walks,          // the keys walk the hero
    NoKeys,         // nothing held: no news
    MouseLookOff,   // the plugin's mouse look is off: the keys are the game's own (W swaps the weapons)
    CameraOff,      // our camera is off: the keys are the game's own
    NotInWorld,     // not in a game area
    Menu,           // a menu counted open: the keys are the game's own
    NotInFront,     // the game's window is not in front
    Chat,           // the chat line is open
    GameNotAsking,  // the plugin takes the keys, but the game does not ask for its keyboard move (its UI on the pad)
    GateBlocked,    // the game asks, and its own gates say no
};

// The plugin's own reasons first (they stop the keys before the game sees any), in the order ShooterActive and FlatMoveInput test them.
inline Why Decide(const Facts& f) {
    if (!f.keysHeld) return Why::NoKeys;
    if (!f.mouseLook) return Why::MouseLookOff;
    if (!f.cameraOn) return Why::CameraOff;
    if (!f.inWorld) return Why::NotInWorld;
    if (f.menuOpen) return Why::Menu;
    if (!f.focused) return Why::NotInFront;
    if (f.chatOpen) return Why::Chat;
    if (!f.gameAsked) return Why::GameNotAsking;
    if (!f.gateOk) return Why::GateBlocked;
    return Why::Walks;
}

inline const char* Text(Why w) {
    switch (w) {
    case Why::Walks: return "W A S D walk again";
    case Why::NoKeys: return "no key held";
    case Why::MouseLookOff: return "the mouse look is off (F9, or the view's own setting) - the keys go to the game as its own keys (W swaps the weapons)";
    case Why::CameraOff: return "our camera is off - the keys go to the game as its own keys";
    case Why::NotInWorld: return "not in a game area";
    case Why::Menu: return "a menu is counted open (gamestate) - the keys go to the game as its own keys";
    case Why::NotInFront: return "the game's window is not in front";
    case Why::Chat: return "the chat line is open - the keys are letters";
    case Why::GameNotAsking: return "the game does not ask for a keyboard move - its UI is in controller mode (a mouse click switches it back; or the game's Options: controller off)";
    case Why::GateBlocked: return "the game asks, but its own gates refuse the keyboard move";
    }
    return "?";
}

// One line when the verdict of a held key changes; letting go is no news, so pressing again into the same trouble says nothing new. The first
// failure after walking is said, and the walk coming back. At most kLines lines in a game's run (a key mashed against a closed door would
// fill the log - one line costs a player's PC up to 29 ms). Caller: one thread.
class Watch {
public:
    static constexpr uint32_t kLines = 200;
    bool Step(Why now, Why* say) {
        if (now == Why::NoKeys || now == last_) return false;
        last_ = now;
        if (lines_ >= kLines) return false;
        ++lines_;
        *say = now;
        return true;
    }
    uint32_t Lines() const { return lines_; }
private:
    Why last_ = Why::Walks;
    uint32_t lines_ = 0;
};

}  // namespace flatkeys
