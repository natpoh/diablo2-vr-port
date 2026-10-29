// clash_sound.h - [weight] clash_sound: the knock of steel on steel heard - the game's own block sound (sounds.txt "block_weapon_1": the game picks one of
// its three, the HD build plays impact_block_blade_*_hd), through the game's own two calls, as loud as the knock was hard (vr/clash_loud.h). For the two
// weapons of a pair striking each other ([weight] dual_collide) and a parry's clash (a monster's weapon meeting the weapon or the shield). F4 only.
//
// The game's calls (D2RLoader.exe, 3.3.93787 under D2RLoader 1.3.1, read off the decrypted image of the running game, 2026-10-24): what the game's own
// animation events do for a footstep's foley (D2Prism translation_animation_events.cpp) - SoundPlay 0x1A0C00 (the sound's name, the unit it sounds at,
// 0, 0; the name looked up in sounds.txt, the start queued for the sound thread; returns the instance's handle, 0 when the game did not start it - its
// own limits: no voice free, the sound's volume 0, ...) and SoundSetVolume 0x19F900 (the handle, 0..255; the instance starts at 255). Both on the game's
// thread (D2RLoader's runOnGameThread), the same thread the game makes its sounds on; the sound at the hero's client unit (3D, as the game's own block
// sound sounds at its defender). Nothing of the game's is changed: whether anything hit or was blocked stays the game's; this is a sound only.
#pragma once

#include <cstdint>

namespace D2RL { struct PluginContext; }

namespace clashsound {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);
void Init(const D2RL::PluginContext* ctx);
// [weight] clash_sound (live): off - nothing is played (the knock is felt as before).
void SetWanted(bool on);
bool Wanted();
// The hero's client unit (the game's thread asks it, 0 = not known: nothing is played - a 3D sound at no place is not heard).
using HeroFn = void* (*)();
void SetHero(HeroFn fn);
// One knock, from any thread: speed (m/s, for the log), the game's volume 0..255 (clashloud::Of(speed).gameVolume) and who knocked (for the log;
// a string that lives on). Queued for the game's thread (a few at most; one not played within 150 ms is dropped - never a late clang). False: not wanted,
// not F4, or no game thread to run on.
bool Play(float speedMps, uint32_t gameVolume, const char* who);

}  // namespace clashsound
