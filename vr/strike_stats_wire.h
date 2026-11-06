// vr/strike_stats_wire.h - [stats] strike_stats on the game (vr/strike_stats_wire.cpp; the counting itself is vr/strike_stats.h): the touch's
// blows counted per hero and weapon - this session's and all time's - and kept in Saved Games\Diablo II Resurrected\D2R VR Stats\
// strike_stats.txt (written every minute while they change and at the game's exit: a temp file, flushed, then renamed - a crash or a power
// cut mid-write leaves the last good file). Counting only: nothing of the game's attack, the touch's decisions or the log changes.
// VR first person (F4): the touch only presses there. Off ([stats] strike_stats=0, live): nothing counted, no file written.
#pragma once

#include <cstdint>

#include "strike_stats.h"

namespace strikestatswire {

void Start();   // vrcam, after g_iniPath is set: the file read, this session begun, the writer's thread (also when off: the tick is live)
void Stop();    // vrcam's shutdown: the last numbers written, the thread joined
bool On();      // the tick, as the writer's thread last read it

// The touch (vrcam's timer thread). Cheap: a lock and a few additions, no log line, no file.
void SetHero(const char* name, int cls);   // the hero the next blows are his (a name not valid: the unnamed hero)
void Blow(int side, uint64_t strokeKey, uint32_t code, uint32_t kind, float speed, float force);
void Refused(int side, uint64_t strokeKey, uint32_t code, uint32_t kind, bool slow, uint64_t nowMs);
void Tick(uint64_t nowMs);   // the missed swings that have settled

// The pages: a copy of the numbers and the session's start (seconds since 1970). False: off or nothing yet.
bool Snapshot(strikestats::Store* out, int64_t* started);
uint64_t Revision();   // grows with every change of the numbers (a copy is taken again only then)

}  // namespace strikestatswire
