#pragma once
// auto_tune_run.h - [autotune] the "Auto" graphics tuning in the game: the step logic of vr/auto_tune.h applied live and measured.
//
// While it runs, nothing is written to d2r_vr.ini: the settings it tries are values the plugin's own ini reads are handed instead
// of the file's (Forced: vrc::IniF and xr.cpp's render size / stereo reads), and the settings are read again at once (TakeReload).
// The frames are counted at each xrEndFrame (OnFrame). The result waits for the player's Keep (the keys written with
// WritePrivateProfileString, d2r_vr.ini copied to d2r_vr.ini.before_autotune first) or Undo (the values dropped: the file was never
// touched). A game closed in between is an Undo.
//
// Never a knob: frame generation (a restart, a lead in GPU hangs) - and with it on the stereo mode stays too (it feeds its settings).
// Not tried: the game's own graphics options (Settings.json is read at the game's start and written back at its exit).

#include <cstdint>
#include <string>
#include <vector>

namespace autotunerun {

void SetLogger(void (*log)(const char*));
// From LoadSettings: [autotune] enabled (0: the feature hidden - a run is undone) and run / keep / undo (D2R VR Settings' buttons add 1:
// a change starts a run, keeps its result, undoes it; the first values read are only remembered).
void Configure(const wchar_t* iniPath, bool enabled, int runSerial, int keepSerial, int undoSerial);
bool Enabled();

// The run's own value for an ini key while it measures or waits for Keep (false: the file's).
bool Forced(const wchar_t* sec, const wchar_t* key, float* v);
// The run changed its values: the settings are read again now (true once a change).
bool TakeReload();

// xr.cpp, each xrEndFrame: its time (us), what xrWaitFrame blocked this frame (us), the headset's display period (ns), and
// whether the frame counts (the game in a world, F4).
void OnFrame(double nowUs, double waitUs, double periodNs, bool counts);
// What the knobs are made from that the ini does not say. The headset's recommended eye size (xr.cpp, once a session): with
// [render] vr_resolution=1 its longer side is the render size to step down from.
void SetRecommended(uint32_t w, uint32_t h);
// The native effects' last 10 s (effects.cpp): their passes' time (ms a present) - only the order the probes run in - and whether
// the fog before effects was hooked at the start (only then can it be turned off live).
void NoteGpu(double depthFogMs, double distBlurMs, bool fogBeforeFxHooked);

enum class State { Idle, Running, Waiting };   // Waiting: a result is shown, Keep or Undo
struct Row {
    std::string what;            // "Sky", "Render size", ...
    std::string from, to;        // "on" / "off", "2048 px", ...
    float fpsBefore = 0, fpsAfter = 0;
    int phase = 0;               // autotune::Phase
    bool kept = false;
    bool trial = false;          // tried before Low / Potato (eyes by turns): kept says whether it stayed
};
struct View {
    State state = State::Idle;
    int measured = 0, left = 0;  // measures so far, still to come (rough)
    bool reached = false, aborted = false;
    bool belowMin = false;       // the ladder's last rung (Potato, 1200 px) still short: the PC is below the minimum here
    int limit = 0;               // ... held back by: 1 the graphics card (GPU), 2 the processor (CPU)
    float targetFps = 0, hz = 0, startFps = 0, nowFps = 0;
    std::vector<Row> rows;       // the measured steps (probes, down, up, final)
    std::vector<Row> changes;    // the result: what differs from the player's own settings
    uint64_t revision = 0;       // moves on any change of the above
};
View Now();
uint64_t Revision();

void Start();   // a run from the player's settings as they are (nothing happens while one runs or a result waits)
void Keep();    // the result written to the ini
void Undo();    // a run stopped / a result dropped: everything as it was

}  // namespace autotunerun
