#pragma once
// perf_sweep.h - [debug] perf_sweep: one automatic A/B of what makes the processor's left pass long, in the headset, while the player stands
// still (2026-11-10, after 0.211: Medium is CPU-bound - "надо будет расковырять эти 3100 вызовов").
//
// Turning the key 0 -> 1 while the game runs starts one run (a 1 already in the file at the game's start is only remembered); 1 -> 0 stops
// it (everything back at once). A run walks a list of steps; each step hands the plugin's
// own ini reads other values (as Auto does: vrc::IniF asks Forced first) and has the settings read again - NOTHING is written to d2r_vr.ini.
// Each step: a few seconds to settle (models re-sorted, pipelines made), then the pairs are measured - only in VR first person (F4); a pair
// outside F4 (a menu, another view) pauses the step's clock. At the end one table in the log: pairs/s, the processor's ms a pair, the left
// pass, the right eye, the game frame between pairs, and the game's calls a pair (lit, its kinds, the depth pre-pass, shadows). The first step
// also takes one 5 s profile of the draw thread ([debug] profile_draw's, d2rloader\logs\d2r_vr_profile.txt).
//
// Steps (each from the player's own settings; a step that would change nothing is left out):
//   as set | big objects 300 | big objects off | models 150 | models 150 + big objects off | models 100 + big objects off | room rings 1 |
//   the bench's call counting idle (the outline drawn by the game: the pipe hunt's per-bind lookup not made) | every game draw skipped
//   (bench_skip_all: the driver's share) | ours off (bench_bare) | as set again (drift)
// Visual only, a diagnostic: the game's state and mechanics are never touched (only what is drawn, for a few seconds each).

#include <cstdint>

namespace perfsweep {

void SetLogger(void (*log)(const char*));
// From LoadSettings: the key's value ([debug] perf_sweep) and the ini's path (the player's own values, read when a run starts)
void Configure(bool on, const wchar_t* iniPath);
// The run's value for an ini key while a step is in force (false: the file's)
bool Forced(const wchar_t* sec, const wchar_t* key, float* v);
// A step changed the values: read the settings again now (true once a change)
bool TakeReload();
// The draw thread, after each stereo pair: its processor time, the left pass (without the XR begin), the right eye, the game frame between
// the last pair and this one (us), and whether it was VR first person
void OnPair(double cpuUs, double leftUs, double rightUs, double gapUs, bool f4);
bool Running();

}  // namespace perfsweep
