// shot_trace_wire.h - [debug] shot_trace on the game (vr/shot_trace_wire.cpp; the window and the texts are vr/shot_trace.h).
#pragma once

#include <cstddef>
#include <cstdint>

namespace shottrace {

void SetLogger(void (*log)(const char*));
// [debug] shot_trace, live (vrcam's LoadSettings). Read only: it logs, it changes nothing the game does.
void SetWanted(bool on);
bool Wanted();

// vr/stand_still_wire.cpp's pad poll, every poll: an attack of ours is pressed (the machine is waiting for / delivering / in the tail of it). It opens and
// closes the shot's window; with attack_in_place off no shot is ever seen.
void Observe(bool busy);
// A shot is being watched now (and the tracer is wanted). Cheap; any thread.
bool Active();

// "a monster, class 12, id 345, mode 1, at (5651.0, 4713.0), 7.2 cells from the hero" for a unit the game answered with (or "none"); `hero` may be null.
// Reads the unit's header and path only, guarded. For the lookups' answers in vrcam's hooks.
const char* UnitText(const void* unit, const void* hero, char* out, size_t n);

// A line in the shot's log, from any hook of vrcam (the cast resolver, the click's target lookup). Nothing when no shot is watched.
void Note(const char* fmt, ...);

// vrcam's HookMapClick around the game's click on the map: the click, and when it returns.
struct ClickTrace {
    ClickTrace(void* unit, int type, int x, int y, uint64_t flags);
    ~ClickTrace();
    ClickTrace(const ClickTrace&) = delete;
    ClickTrace& operator=(const ClickTrace&) = delete;

private:
    bool on_ = false;
    int type_ = 0;
};

// vr/stray_walk_wire.cpp's hook of the command funnel 0xFABE0, before the command goes on: `ctx` = what the click on the stack is (stray_walk.h CtxName),
// `clickType` = its type (-1: none).
void OnCommand(int cmd, void* unit, int a1, int a2, const char* ctx, int clickType);

// vrcam's timer, twice a second: puts the hook on the game's click-command sender once the tracer is wanted and its page is there.
void Install();

}  // namespace shottrace
