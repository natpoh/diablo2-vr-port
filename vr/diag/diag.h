// vr/diag/diag.h - the VR layer's observers (docs/plan_modules.md, step M1): they read, time and log; nothing in play depends on them.
//   flog        [debug] frame_log: every pair, view, hero pose and present to d2r_vr_frames.csv (vr/diag/flog.cpp)
//   budgetasks  the game's asks of its video memory budget, counted for the 10 s line (vr/diag/budget_asks.cpp)
//   pairtime    where a pair's time goes; drawprof [debug] profile_draw; cbcmp [debug] cb_compare; rtrace [debug] replay_trace
//               (vr/diag/pair_probes.cpp)
//   crash       the start's steps and the log's first minutes to a file, an exception the game dies of to d2r_vr_crash.txt (vr/diag/crash.cpp)
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace vrc {

namespace flog {
extern std::atomic<bool> g_on;
extern std::atomic<ULONGLONG> g_diagUntil;   // [debug] diag_go: the frame log runs until then
void StartDiag();                            // (vrcam.cpp: it tells the state the capture ran in)
double UsNow();                              // QueryPerformanceCounter in microseconds - the clock FlatVR's trace uses too
uint32_t Pass();                             // the game's draw counter
float Dt();                                  // the game's frame time
void Line(const char* fmt, ...);
void Tick(bool want);                        // the timer thread: open / close with the setting, write what collected
}  // namespace flog

namespace budgetasks {
extern std::atomic<uint32_t> g_gameAsks, g_frontPresents;
extern std::atomic<DWORD> g_ourThread;
extern std::atomic<bool> g_in;
void Install();
}  // namespace budgetasks

namespace pairtime {
extern std::atomic<double> g_effBegin, g_effSum, g_presentAt;
extern std::atomic<uint32_t> g_presents;   // real presents (ReShade's), never cleared: the replay watchdog's
extern std::atomic<DWORD> g_drawThread;
double UsNow();
double CpuUs();
std::string BusyThreads(double spanUs);
}  // namespace pairtime

namespace drawprof {
extern std::atomic<bool> g_want;
constexpr int kDepth = 24;
std::string ModuleOf(DWORD64 pc, DWORD64* base);
int UnwindCopy(CONTEXT c, uint8_t* copy, size_t copied, DWORD64* out);
void Tick();
}  // namespace drawprof

namespace cbcmp {
extern std::atomic<bool> g_go;
void NoteView(int eye, const float v[16]);
void Register();
void PairBegin();
void PairMiddle();
void PairEnd();
}  // namespace cbcmp

namespace rtrace {
extern std::atomic<bool> g_go;
void Register();
void PairBegin();
void PairMiddle();
void PairEnd();
}  // namespace rtrace

namespace crash {
void Install(const char* version);
void Uninstall();
void Keep();
void Mirror(const char* text);
void Step(const char* name);
void Mark(const char* what);
uint32_t AvTotal();
}  // namespace crash

}  // namespace vrc
