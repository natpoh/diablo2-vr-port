// main_cull.h - [weight] knock_down: where the game culls a pass, and what its cull reads (D2R 3.3.93787, the D2RLoader image; docs/notes/grab_hold_flicker.md
// "The body vanishes"). Facts only - the hook is vr/head_shadow.cpp's (SetBeforeCull), what runs there vr/knock_down.cpp BeforeCull. Proven against the
// image's snapshot by cleanroom/tests/main_cull_image_test.cpp.
//
// The world draw (0x650300) calls, in this order:
//   0x6504C9  Renderer::UpdateSystems (0xE39390) - the transforms' world matrices, and every entity's bounds made again from its own pose and place;
//   0x6504E3  the main cull (0xE37D20)           - the camera's matrix, then at 0xE37E76 the kick of the cull's jobs (0xD9B4D0).
// A job tests each entity's bounds against that matrix: its bounds component (0x60 bytes: local min / max, ..., world min at +0x40, world max at +0x50) is
// read straight from the storage at 0xE0E72B - index x 0x60 plus the storage's components, then the two vectors - and handed to the box test (0xF74420).
// So bounds written between UpdateSystems' return and the kick are the ones the cull tests; written earlier, the game's own are back by then.
#pragma once

#include <cstdint>

namespace maincull {

constexpr uint64_t kCullRva = 0xE37D20;
// mov r11,rsp; push rbx; push rsi; push rdi; sub rsp,0C0h - the whole instructions before the first that names an address of its own
constexpr uint8_t kCullSig[13] = {0x4C, 0x8B, 0xDC, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00};

// The order and the read, for the test: each a call (E8 rel32) at `at` to `to`.
struct Call { uint64_t at, to; };
constexpr Call kUpdateSystemsCall = {0x6504C9, 0xE39390};   // the world draw: the bounds made again ...
constexpr Call kCullCall = {0x6504E3, 0xE37D20};            // ... before the cull
constexpr Call kKickCall = {0xE37E76, 0xD9B4D0};            // the cull's jobs kicked inside it
// lea rcx,[rax+rax*2]; shl rcx,5; add rcx,[r15+38h]; movups xmm6,[rcx+40h]; movups xmm7,[rcx+50h] - a job's read of an entity's world box
constexpr uint64_t kJobReadRva = 0xE0E72B;
constexpr uint8_t kJobReadSig[20] = {0x48, 0x8D, 0x0C, 0x40, 0x48, 0xC1, 0xE1, 0x05, 0x49, 0x03, 0x4F, 0x38, 0x0F, 0x10, 0x71, 0x40, 0x0F, 0x10, 0x79, 0x50};
constexpr int kBoundsSize = 0x60, kWorldMinAt = 0x40, kWorldMaxAt = 0x50;   // (knockprops' bounds storage: 24 floats, the world box at floats 16 and 20)

}  // namespace maincull
