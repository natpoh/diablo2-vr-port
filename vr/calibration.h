// calibration.h - the T-pose measurement of the player's eye height and arm span (native OpenXR), and what the rest of
// the mod takes from it. The detector is vr/body_calibration.h (pure); this is its wiring: the ini, the headset frame
// in, the prompt out.
//
// Ours today: the world scale from BodyWalk's Body Height or [stereo] user_height_m, the arms' reach from [arms] scale
// by hand. With [calibration] use=1 and a stored measurement, the measured eye height is the world scale (everywhere
// UserEyeHeightM() is read) and the measured span fits the hero's arm bones to it ([arms] scale then counts as 1:
// the hands stay 1:1 with the controllers). use=0 - or nothing measured - and none of this is read: the numbers above,
// exactly as before.
//
// [calibration] in d2r_vr.ini:
//   use=0|1            D2R VR Settings: "Use the measured height and arm span"
//   seated=0|1         how the game is played (D2R VR Settings > Body, the headset panel's Body page): 0 standing - the F4 camera's
//                      standing height is the floor plus the measured eye height; 1 seated - the recentre's head height is the hero's
//                      eyes, and a measurement reads the arm span only (the stored eye height stays). Live.
//   measure=N          the Settings button adds one; a change (not the first value seen) starts the measurement
//   version=1          a stored measurement; 0 = none ("Forget the measurement" in Settings)
//   eye_height_m, arm_span_m   what was measured (written by the plugin, atomically, after a good T-pose)
//
// Thread: Feed and PanelNow run on the headset frame's threads (xr.cpp); LoadSettings on vrcam's timer; the two
// Measured* getters on any.
#pragma once

#include <cstdint>

// F4 ONLY (vr/view_gate.h): the measurement runs, and its numbers are in force, in VR first person only; elsewhere the old numbers.
namespace calib {

void SetLogger(void (*log)(const char*));

// [calibration] from d2r_vr.ini, on every change of the file (vrcam's LoadSettings).
void LoadSettings(const wchar_t* iniPath);

// Start a measurement at the next headset frame (D2R VR Settings does it through the ini's `measure` counter; another
// panel - an in-headset settings page - calls this). Let go after a minute with no native session to take it.
void RequestMeasure();
// A measurement is asked for and not yet over: waiting for its first headset frame (up to a minute), or under way - the prompt
// is up, the Saved / could-not-save message included, until the player has let go of the triggers. The in-headset settings panel
// hides while this is true and comes back when it ends.
bool Running();

// What the mod uses: 0 = not (unticked, nothing measured, or not valid) - then the old numbers apply.
float MeasuredEyeM();    // the eyes over the floor, standing, metres
float MeasuredSpanM();   // grip to grip with the arms out, metres
// [calibration] seated: played seated - the camera's standing height is the recentre's, a measurement reads the arm span only.
bool Seated();

// -- the measurement itself ---------------------------------------------------------------------------------------------

// One headset frame, in the room's STAGE space (the floor at y = 0), OpenXR axes, as xr.cpp located it.
struct Frame {
    double time = 0;            // seconds, monotonic (the display time)
    bool floor = false;         // a STAGE space exists - LOCAL has no floor to measure from
    bool focused = false;       // the session has the input focus
    bool headTracked = false;
    bool handTracked[2] = {};   // 0 left, 1 right: the controller really located, not a held place
    float head[3] = {};
    float headRot[4] = {0, 0, 0, 1};
    float hand[2][3] = {};
    float trigger[2] = {};
    bool cancel = false;        // the right hand's B
};
void Feed(const Frame& f);
// The session ended (or is being torn down): a measurement under way is dropped.
void Stop();

// True while the measurement owns the triggers: they are held on purpose, and must not also be an attack (the input
// layer takes them from the layout while this is true).
bool OwnsTriggers();
// Once, at the plugin's start: the one wiring to vr/direct_input.h (calibration_input.cpp) - while the measurement runs, and until
// the player has let go once after it, both triggers and B are taken from the layout. Nothing without that header.
void RegisterInput();

// -- the prompt: a panel in front of the face, drawn by xr.cpp --------------------------------------------------------

constexpr int kPanelMessages = 11;                    // one image per message, standing ...
constexpr int kPanelImages = 2 * kPanelMessages;     // ... and the same seated (image + kPanelMessages: "sit upright", the arm span only)
constexpr uint32_t kPanelW = 960, kPanelH = 440;   // pixels of one image
constexpr uint32_t kTrackX = 60, kTrackY = 352, kTrackW = 840, kTrackH = 22;   // the progress bar's track in it
constexpr float kPanelWidthM = 0.80f;               // how wide it hangs, metres
constexpr float kPanelDistanceM = 1.25f;            // and how far ahead of the face

struct Panel {
    bool show = false;
    int image = 0;           // 0 .. kPanelImages - 1
    float progress = 0;      // the hold, 0..1
};
Panel PanelNow();

// Paints image `image` (premultiplied RGBA, R in the low byte, kPanelW x kPanelH, top row first) with GDI. False if it
// could not.
bool PaintPanel(int image, uint32_t* pixels);

}  // namespace calib
