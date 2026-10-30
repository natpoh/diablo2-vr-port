// vr/frame/frame_order.h - what vrcam.cpp uses of the frame's order (vr/frame/frame_order.cpp, docs/plan_modules.md M17).
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>

namespace vrc {

// the game's draw of a frame (sDrawGameScreen), twice for a stereo pair - its hook is put in by vrcam.cpp's InstallHooks
constexpr uint64_t RVA_DRAW_GAME_SCREEN = 0x93B40;
using DrawGameScreenFn = uintptr_t (*)(int);
extern const uint8_t kSigDrawGameScreen[16];
extern DrawGameScreenFn OrigDrawGameScreen;
uintptr_t HookDrawGameScreen(int a);
// the 1 ms update thread (D2RLoaderLoadPlugin starts it)
extern DWORD g_updateThreadId;
DWORD WINAPI UpdateThread(void*);

}  // namespace vrc
