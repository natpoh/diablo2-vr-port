#pragma once

// BodyWalk's gamepad as it is right now - the very report it would hand the
// virtual Xbox pad (ViGEm): locomotion, mapped buttons and gestures, merged.
// Written on every update of VirtualJoystickService, so a game-side mod that
// hooks XInput can give the game this pad directly, with no virtual device and
// no driver at all (first: the D2R vrcam plugin). Its own mapping, so an older
// BodyWalk or mod simply misses it.

#include <cstdint>

#define BW_PAD_MIRROR_NAME L"Local\\BodyWalkVR_PadMirror"
#define BW_PAD_MIRROR_VERSION 1u

#pragma pack(push, 4)
struct BWPadMirror {
  uint32_t version;     // BW_PAD_MIRROR_VERSION
  uint32_t counter;     // bumped with every write; still for a second = BodyWalk stopped
  uint32_t writer_pid;  // BodyWalk's process
  uint16_t buttons;     // XINPUT_GAMEPAD_* bits
  uint8_t left_trigger;
  uint8_t right_trigger;
  int16_t thumb_lx, thumb_ly, thumb_rx, thumb_ry;
  uint32_t reserved[2];
};
#pragma pack(pop)

static_assert(sizeof(BWPadMirror) == 32, "BWPadMirror is a wire format");
