// typed_keys.h - [openxr] laser_typing: a key the player presses on a real keyboard lets the game take its keyboard layout for a moment,
// although the laser holds the controller layout (vr/panel_input.cpp Keep). In the controller layout the game's text boxes take no typed
// letters - D2RLoader's Host Game and Browse Games, a character's name, any password - and the laser refused the game's switch for a real
// key as for its own mouse: nothing could be typed in VR "even using the keyboard" (bearstonem/d2r-pcvr's owner, 2026-10-25). Once the
// game has switched nothing holds it back; the thumbstick brings the controller back, as ever.
// Keys the plugin sends itself (SendInput: the laser's Ctrl / Shift, the pause Esc, the aim's Shift) carry kOwnKeyTag in dwExtraInfo and
// never count. The idea and the tag are bearstonem/d2r-pcvr's (MIT, 34f20a8, PR #347); written again here. Pure: cleanroom/tests/typed_keys_test.cpp.
#pragma once

#include <atomic>
#include <cstdint>

namespace typedkeys {

constexpr uintptr_t kOwnKeyTag = uintptr_t(0x44325256'4b455953ull);   // "D2RVKEYS" (64-bit; the high half dropped on a 32-bit build)
constexpr uint64_t kTypingMs = 300;   // how long a real key lets the keyboard layout in: the laser's own mouse, the panel_input lease

class Typing {   // Key on the window's thread, Active on the game's: the moment is atomic
public:
    // A key-down from the game's window with its message's (GetMessageExtraInfo) or raw input's extra information. True: a real key.
    bool Key(uint64_t now, uintptr_t extra) {
        if (extra == kOwnKeyTag) return false;
        until_.store(now + kTypingMs);
        return true;
    }
    bool Active(uint64_t now) const { return now < until_.load(); }

private:
    std::atomic<uint64_t> until_{0};
};

}  // namespace typedkeys
