// vr/sky/world_light.h - the game's own light and fog as the VR layer reads and sets them (docs/plan_modules.md M8):
//   env        the game's day and night (vr/sky/day_night.cpp, with DayNightK / DayNightMul)
//   gamefog    the game's height and depth fog taken out of its areas' definitions (vr/sky/game_fog.cpp)
//   cavelight  [cave_light]: the light that fills caves and dungeons (vr/sky/lights.cpp)
//   worldobj   the lights near the hero, from the game's own objects (vr/sky/lights.cpp)
// (gamefogvr, [fog] game_fog, is still vrcam.cpp's: seen from vrcam.cpp's top its names would hide vr/game_fog_vr.h's.)
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace vrc {

namespace env {
bool SafeRead(const void* p, void* out, size_t n);
bool PageReadable(uintptr_t a);
void Find();
bool Now(int* light, int* period);   // the game's light 0..255 and its period (0..5); false: not found
}  // namespace env
float DayNightKRaw(int* light, int* period);   // the daylight 0 (night) .. 1 (day) from the game's light; -1 not known
float DayNightK();                             // ... unknown = 0
float DayNightMul();                           // what the sky and the open-air fog are multiplied by now

namespace gamefog {
void SetPoke(const std::wstring& w);   // [debug] fog_poke
void Tick();
}  // namespace gamefog

namespace cavelight {
void Load();
void Install();
void Tick(const std::string& biome, bool underground);
}  // namespace cavelight

namespace worldobj {
struct Light { float x, y, z, lit, rgb[3]; bool monster = false; };   // world point (vrcam's, absolute); Objects.txt Lit; colour; a monster's
constexpr int kMaxLights = 16;
extern std::atomic<bool> g_known;   // the walk worked last time: the shader takes these, not the picture's fire
void Gather();
int Lights(Light* out);
}  // namespace worldobj

}  // namespace vrc
