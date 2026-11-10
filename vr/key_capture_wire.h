// key_capture_wire.h - the one "press a key" capture of the overlays (vr/key_capture.h), fed by the game window's procedure (vr/hud/window.cpp,
// first of all: the key that answers never reaches the game, nor does Esc that cancels). An overlay arms it for one of its rows (an owner number
// of its own), shows "Press a key" while Armed, and Takes the answer when it comes (any thread: one lock).
#pragma once

#include <cstdint>

#include "key_capture.h"

namespace keycapwire {

// Owners: the flat panel's rows (kFlatMenu + row), the Gestures tab (kGestures: the gesture is the tab's own to remember).
constexpr int kFlatMenu = 1000, kGestures = 2000;

void Arm(int owner, keycap::Use use);
void Cancel(int owner);
bool Armed(int owner);
// The capture's last word for this owner (Took / Cancelled / Refused / TimedOut), once; false: nothing new.
bool Take(int owner, keycap::Res* out);
// The game window's procedure: true = the message is the capture's (the game does not see it).
bool OnWindowMessage(unsigned msg, uintptr_t wParam, intptr_t lParam);

}  // namespace keycapwire
