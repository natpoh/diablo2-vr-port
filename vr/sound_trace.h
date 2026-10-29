// sound_trace.h - [debug] sound_trace: what the game's sounds are, by name, as it makes and plays them - to find the game's own shield-hit (block) sound
// and its metal clash, for [weight] monster_shields' sound of a blocked blow (docs/notes/monster_bodies.md). Reading only: two hooks on the game's sound
// engine (FMOD4SoundEngine, its vtable 0x1FE6CE8 in 3.3.93787 under D2RLoader 1.3.1, read off the decrypted image of the running game, 2026-10-24):
//   - CreateInstance 0x1576D40 ("Attempted to create AudioInstance with incompatible options"): (engine, AudioInstanceRef* out, the sound's resource,
//     bool 3D, byte, int) - each sound's resource is named the first time it is made (a path the resource points at, read guarded);
//   - Play 0x157CB90 ("Attempted to play a invalid/blank AudioInstance"): (engine, AudioInstanceRef*) - each play of a named sound is told, at most once
//     in 2 s for each name.
// Both pass everything on unchanged: no sound is made, changed or kept from playing. Off: the hooks are not installed.
#pragma once

namespace soundtrace {

using LogFn = void (*)(const char*);
void SetLog(LogFn fn);
// [debug] sound_trace (live): on - the hooks go in (once, the first time) and the names are told; off - nothing is told (the hooks, once in, only pass through).
void SetWanted(bool on);
// Twice a second from vrcam's timer: puts the hooks in once they are wanted and their code is there.
void Install();

}  // namespace soundtrace
