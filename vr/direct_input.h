// direct_input.h - the plugin's own input for native OpenXR: the controllers are read here and what BodyWalk
// supplied (the virtual pad, the D2R actions, the game's commands, the head and hands) is made here. API 1.
//
// [openxr] direct_input=1 (D2R VR Settings: "Controls without BodyWalk"; native OpenXR only). With it on and a
// native session running, the plugin reads the sticks, triggers, grips, face buttons, menu and stick clicks
// from xr.cpp's own OpenXR actions, and from them
//   - hands the game's XInput hook a pad (left stick walks, right stick turns through TurnTick, buttons by the
//     default layout or the ini's [direct] table; in a menu the pad as the game's own controller layout),
//   - holds the D2R actions (attack, interact, inventory, map, swap, views, recenter),
//   - holds the game's commands through the game's own executor (vr/gamecmd.cpp: skills, potions, Alt ...),
//   - fills the D2RVR_Shared head and hands the skeleton and the camera read.
// BodyWalk's blocks are ignored while it is on (no double input). Off: exactly as before; BodyWalk keeps working
// for FlatVR and for native OpenXR with the box unticked.
//
// F4 ONLY (vr/view_gate.h): all of this acts in VR first person only. In F1, F2, F3, the flat modes the layer behaves as if
// `direct_input` were 0 at once - no layout, no gesture press, command, pad pulse, take or haptic - and BodyWalk's input is in
// charge; a view change lets go of everything held, and coming back to F4 starts fresh (the release gate holds what the hands
// hold at that moment until it is let go).
//
// This header is what the gesture code wires to (swing, draw, throw, shoulder swap, calibration...): a gesture is
// a pure detector on the poses of a Frame, and its effect goes out through Command / PadPulse / Action / Haptic
// below. None of them bypass the game: a command is the game's key command, a pad pulse is a pad button the
// game reads in its own XInput poll, an action is the game's own attack / interact / swap. The calls work in
// direct mode AND in native OpenXR with BodyWalk's input (direct_input off) - a gesture has its own tick box
// either way.
//
// Threads: Listen callbacks run on the game's draw thread, once per headset frame, inside xr.cpp's Publish.
// Latest, On, Active, Session, Command, Tap, Action, PadPulse, Haptic: any thread.
#pragma once

#include <cstdint>

namespace directinput {

constexpr uint32_t kVersion = 1;

// -- the per-frame input -------------------------------------------------------------------------------------------

// A pose in the room as the runtime locates it (the STAGE space, the floor at y = 0; LOCAL without a stage),
// OpenXR axes - x right, y up, z back - not recentred; rot is a unit quaternion x y z w.
// valid: 1 = position and orientation, 2 = orientation only (the place is a held one or none), 0 = none.
struct Pose {
    float pos[3];
    float rot[4];
    uint32_t valid;
};

// xr.cpp's buttons as pressed (after the laser took its share on a menu: a trigger or grip the laser holds reads
// as let go here). Same bits as Game Link's, kept apart so a detector needs nothing of BodyWalk.
enum Button : uint32_t {
    kPrimary = 1u << 0,     // A on the right controller, X on the left
    kSecondary = 1u << 1,   // B / Y
    kStick = 1u << 2,       // the thumbstick pressed in
    kMenuButton = 1u << 3,  // the menu button (the left controller on Touch)
    kTrigger = 1u << 4,     // the trigger past its click (0.8 where the runtime has no click)
    kSqueeze = 1u << 5,     // the grip past 0.8
};

struct Hand {
    Pose grip;              // the controller as held
    Pose aim;               // where it points
    float trigger, squeeze; // 0..1
    float stick[2];         // x right, y up, -1..1
    uint32_t buttons;       // Button bits
    uint32_t active;        // 1 while a controller is in the hand (0: bare hands, or not tracked)
};

enum FrameFlags : uint32_t {
    kFocused = 1u << 0,     // the session has the input focus: without it the poses are held ones and nothing is pressed
    kInMenu = 1u << 1,      // a menu or panel is open in the game, or there is no game yet (the title and character
                            // screens): the layout is the menu's, a gesture must not fire
    kDirect = 1u << 2,      // direct mode is feeding the game (the layout and the gates are the plugin's)
    kUiUp = 1u << 3,        // a panel of the plugin's own hangs in the headset (the skill wheel, the settings panel): it
                            // takes the controls with Take, but a detector must not fire either - treat it like kInMenu
    kOtherView = 1u << 4,   // the view is not F4 (viewgate::F4() false): nothing added after the base mod acts - a detector
                            // drops what it has under way and does nothing. kInMenu is set with it, so a detector that only
                            // knows kInMenu stands down too
};

struct Frame {
    uint32_t version;       // kVersion
    uint32_t counter;       // + 1 with every headset frame
    uint32_t session;       // + 1 whenever a native session starts or direct mode is switched on; a detector
                            // that sees a new value forgets everything it has under way
    uint32_t epoch;         // + 1 whenever the player's context changes - the session starts, the input focus is
                            // lost or comes back, a menu opens or closes: the controls held at that moment do
                            // nothing until they are let go (the release gate); a detector drops its stroke
    uint32_t flags;         // FrameFlags
    int64_t displayTime;    // the XrTime the poses are for (ns)
    double time;            // the same in seconds - monotonic, for speeds
    Pose head;              // between the eyes
    Hand hand[2];           // 0 left, 1 right
};

// A copy of the newest frame; false before the first one or after the session ended. Any thread.
bool Latest(Frame* out);

// Called with every new frame, on the draw thread, after the laser took its share and before the layout is made
// from it - a gesture that takes a control (a grip over the shoulder) calls Take below from in here. Up to 16
// listeners; false when the table is full or the same fn and user are in it already.
using FrameFn = void (*)(const Frame& f, void* user);
bool Listen(FrameFn fn, void* user);
void Unlisten(FrameFn fn, void* user);

// -- state ------------------------------------------------------------------------------------------------------------

// [openxr] direct_input from the ini, whatever the session does.
bool On();
// On(), the view is F4 and a native session feeds frames now: the plugin is the input (BodyWalk's blocks are ignored - except its walking
// stick, which direct_feed.h's BodyWalkWalks() mixes into the controller's: [openxr] direct_bodywalk_walk).
// False for a second after the frames stop, so a stopped game does not hold a key - and at once in any other view.
bool Active();
// On() and the view is F4: the layer is in charge of the controls, as far as the view goes (no frames needed). False in
// every other view even with the key set: BodyWalk's input is the input there.
bool Governing();
// The session id of the newest frame (0 = none yet).
uint32_t Session();

// A panel of the plugin's own (a wheel, a settings panel) is up: tell it every frame it runs with true, once with false when
// it closes. The flag reaches every listener in Frame.flags (kUiUp); a lease of 250 ms, so a panel that stops calling
// does not hold the detectors off. Any thread.
void SetUiUp(bool up);

// A long press of a stick click asked for the skill panel (the wheel, the tiles; [direct] right_stick_hold = action:skillpanel):
// true ONCE per request, then false until the next. The panel's owner asks every frame it runs (and only when
// its own open gesture is the long press, [openxr] skill_wheel_open=1). A request older than a second is dropped, so a
// panel that was not listening does not open late. Any thread.
bool TakeSkillPanelToggle();

// -- what a gesture does ---------------------------------------------------------------------------------------------
// Outside F4 (viewgate::F4() false) Command, Tap, Action (down), PadPulse, Take and Haptic do nothing and what is held is
// let go: a gesture that forgets to stand down in another view still cannot press, take or pulse anything.

// Who is asking: a command, a pad button or an action stays held while ANY source holds it, so two gestures (or a
// gesture and the layout) never let go of each other's press. kSrcLayout is the plugin's own layout.
enum Source : uint8_t {
    kSrcLayout = 0,
    kSrcShoulderSwap, kSrcBowDraw, kSrcJavelin, kSrcMelee, kSrcShield, kSrcCalibration, kSrcHaptics,
    kSrcOther0, kSrcOther1, kSrcOther2, kSrcOther3, kSrcOther4, kSrcOther5, kSrcOther6, kSrcOther7,
    kSrcCount
};

// A key command of the game (shared/d2r_vr_shared.h kD2RVRCommands: potions 0x17..0x1A, skills F1..F16 0x0E..0x15
// and 0x2E..0x35, swap weapons 0x2C, Alt 0x25 ...) held down or let go. The game's own executor presses it on its UI
// thread (vr/gamecmd.cpp) with every one of the game's gates, as a key would: a command the game refuses is not
// taken and nothing else happens. A source that holds a command is let go when the session ends.
void Command(uint32_t cmd, bool down, Source src = kSrcOther0);
// One press and release as a tap of a key (a potion drunk from the hand): gamecmd's Tap.
void Tap(uint32_t cmd);

// A D2RVRAction bit set (shared/d2r_vr_shared.h: D2RVR_ACT_ATTACK, INTERACT, INVENTORY, MAP, SWAP, VIEW_*,
// RECENTER) held or let go: what BodyWalk's "D2R: ..." actions do. Attack is the game's own attack on the pad
// (A, never picking up), interact the pick-up / open / talk onto a target only.
void Action(uint32_t actBits, bool down, Source src = kSrcOther0);

// A pad button pulse the game reads in its own XInput poll: XInput wButtons bits (pad::k* below) and trigger
// values, held `holdMs` (at least one game poll; 65 ms is a press the game never misses), then let go. ORed over
// whatever else the pad has (direct mode's layout, BodyWalk's pad, a real one). Used for the skill slots on the
// pad (B/X/Y/RB, with LT for the second bank).
struct PadPress {
    uint16_t buttons;
    uint8_t lt, rt;
};
void PadPulse(const PadPress& p, uint32_t holdMs = 65);
// Ends at once every pulse of exactly these buttons and triggers (the game has taken the press: no need to hold it longer).
void PadPulseEnd(const PadPress& p);

// A gesture that takes a control from the layout (the dominant grip held behind the head belongs to the shoulder
// swap, not to the belt or to RB): while it is taken the layout sees it as let go. Hand 0 left, 1 right; mask of
// Button bits plus kTakeTrigger / kTakeSqueeze for the analog values. Call from a Listen callback, every frame it
// should hold.
constexpr uint32_t kTakeTrigger = 1u << 8, kTakeSqueeze = 1u << 9;
// The shots only (a bow wielded, [hands] bow_draw_only): the control stays the layout's - what is bound to it works - but the attack-type
// outputs of its binding (action:attack, pad A, RT: whatever would shoot), plain, chord and hold lines alike, are dropped this frame.
// A full take (above) on the same control wins: the layout sees it as let go.
constexpr uint32_t kNoShotTrigger = 1u << 10, kNoShotSqueeze = 1u << 11;
// The thumbstick's two axes (x and y; its click is kStick above): taken, the layout, BodyWalk and every later reader see the stick centred - the hero
// neither walks nor turns with it. The skill panel's Settings tab takes both sticks while it is up (they flip its tabs and pages).
constexpr uint32_t kTakeStick = 1u << 12;
void Take(int hand, uint32_t buttons, Source src);

// A pulse in a controller: amplitude 0..1, duration in ms, frequency in Hz (0 = the runtime's default). Out of
// direct mode too. Forwarded to the sink xr.cpp registers (xrApplyHapticFeedback on /user/hand/{left,right}/output/
// haptic); nothing happens where there is no sink or the runtime has no haptics. Never blocks.
void Haptic(int hand, float amplitude, float durationMs, float frequencyHz = 0.0f);
using HapticSink = void (*)(int hand, float amplitude, float durationMs, float frequencyHz);
void SetHapticSink(HapticSink sink);

// -- XInput bits (the same as the Windows header's, so a detector needs no Windows header) -----------------------------
namespace pad {
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008, kStart = 0x0010, kBack = 0x0020,
                   kLeftStick = 0x0040, kRightStick = 0x0080, kLeftBumper = 0x0100, kRightBumper = 0x0200,
                   kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
}

}  // namespace directinput
