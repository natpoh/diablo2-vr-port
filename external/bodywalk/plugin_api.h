#ifndef BWVR_PLUGIN_API_H
#define BWVR_PLUGIN_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define BW_EXPORT __declspec(dllexport)
#define BW_CALLBACK __cdecl
#else
#define BW_EXPORT __attribute__((visibility("default")))
#define BW_CALLBACK
#endif

// Plugin Types
typedef enum {
    BW_PLUGIN_TYPE_INPUT = 1,  // e.g. Treadmill, custom trackers
    BW_PLUGIN_TYPE_OUTPUT = 2, // e.g. SteamVR Hook, OpenXR Layer
    BW_PLUGIN_TYPE_BOTH = 3    // Can do both
} BW_PluginType;

#pragma pack(push, 8)

// Plugin Info structure returned during initialization
typedef struct {
    const char* name;
    const char* version;
    const char* author;
    BW_PluginType type;
    const char* output_mode_name;  // For output plugins: the mode name shown in dropdowns (e.g. "SteamVR Hook")
    const char* input_source_name; // For input plugins: the source name shown in dropdowns (e.g. "OpenTrack")
} BW_PluginInfo;

#pragma pack(pop)

// Structs for tracking data broadcasting
#pragma pack(push, 1)
typedef struct {
    float pos[3];          // X, Y, Z
    float rot[4];          // X, Y, Z, W (Quaternion)
    
    // Input data
    float trigger;         // 0.0 to 1.0
    float grip;            // 0.0 to 1.0
    float stickX;          // -1.0 to 1.0
    float stickY;          // -1.0 to 1.0
    uint64_t buttons;      // Bitmask of pressed buttons

    uint32_t valid;        // 1 if tracking, 0 otherwise
    // Bit 0 (BW_POSE_ROT_ABSOLUTE): the rotation is a heading in the
    // consumer's own room, not a deviation from the head. Only BW_BodyTrackers
    // sets it; everything else leaves the word zero, which is what it used to
    // be called padding for.
    uint32_t flags;
} BW_Pose;

#define BW_POSE_ROT_ABSOLUTE 1u

typedef struct {
    uint32_t version;          // Struct version, currently 4
    uint32_t updateCounter;    // Increments every frame
    BW_Pose head;
    BW_Pose leftHand;
    BW_Pose rightHand;
    // Version 2: how far the head looks up (+) or down (-), in degrees from the
    // horizon. `head.rot` carries the yaw alone and stays that way for the
    // plugins built on it; a reader that predates this field never reaches it.
    float headPitchDeg;
    // Version 3: the head tilted toward the right shoulder (+) or the left (-),
    // in degrees.
    float headRollDeg;
    // Version 4: the head's turn in the room itself, not since the recenter,
    // in degrees, positive to the left. The hands' positions are in room axes;
    // turning them by this puts them in "ahead of me" terms.
    float headYawRoomDeg;
} BW_TrackingData;

// The avatar's torso trackers, offered to whatever wants to republish them as
// devices of its own -- the virtual SteamVR driver being the one that does.
//
// POSITION is said relative to the head, never as a place in the room: metres
// from the HMD, in the HMD's yaw frame. The host reads its own poses from
// whichever runtime it happens to be on, and a consumer living inside a
// different one cannot be assumed to share an origin with it.
//
// ROTATION is a heading in the consumer's room when BW_POSE_ROT_ABSOLUTE is
// set: the head's yaw as SteamVR saw it at the RESET, plus the tracker's own
// turn since. The head is consulted once, at the reset, and never live -- a
// trunk hung on the live head turns with every glance. Without the flag the
// rotation is the old head-relative form, for a reset taken where SteamVR
// could not be asked.
// Head-relative, the consumer adds its own live HMD pose back and the two
// frames cancel, whatever they were.
//
// This is a separate channel from BW_TrackingData on purpose: that one is
// pumped only while an OpenXR or FlatVR frame is arriving, and a body tracker
// has to go on existing when neither is.
typedef struct {
    uint32_t version;          // Struct version, currently 1
    uint32_t updateCounter;    // Increments every frame

    // Whether the user asked for each device to EXIST. Separate from the poses'
    // own `valid`, which only says whether anything is arriving right now: a
    // role that is enabled but momentarily quiet is a tracker with no signal,
    // and a role that is not enabled is not a tracker at all. A consumer that
    // creates devices needs to be able to tell those apart.
    uint32_t chestEnabled;
    uint32_t waistEnabled;

    BW_Pose chest;
    BW_Pose waist;
} BW_BodyTrackers;
#pragma pack(pop)

// ---------------------------------------------------------
// Callbacks passed FROM BodyWalkVR TO the Plugin
// The plugin uses these to send data back to BodyWalkVR
// ---------------------------------------------------------

#pragma pack(push, 8)

typedef struct {
    // Logging
    void (BW_CALLBACK *log_info)(const char* message);
    void (BW_CALLBACK *log_warn)(const char* message);
    void (BW_CALLBACK *log_error)(const char* message);

    // Send input data (For Input plugins like Reality Runner)
    // Send joystick data (x, y from -1.0 to 1.0)
    void (BW_CALLBACK *send_joystick_input)(float x, float y);
    // Send button state
    void (BW_CALLBACK *send_button_input)(const char* button_name, bool pressed);

    // Send tracking data
    void (BW_CALLBACK *send_position)(const char* source, float x, float y, float z);
    void (BW_CALLBACK *send_rotation_euler)(const char* source, float pitch, float yaw, float roll);
    void (BW_CALLBACK *send_rotation_quat)(const char* source, float qx, float qy, float qz, float qw);
    // Register a new action category for mapping
    void (BW_CALLBACK *register_mapping_category)(const char* category_name, const char* category_description);

    // Set the currently active category (so BodyWalk uses bindings from it)
    void (BW_CALLBACK *set_active_mapping_category)(const char* category_name);

    // Register a game/plugin specific action that can be mapped to gestures
    void (BW_CALLBACK *register_action)(const char* action_name);

    // ---- added in host API version 2 ----
    // The struct only ever grows at the end, so a plugin built against an older
    // header keeps working: it reads the prefix it knows and never touches the
    // rest.
    //
    // The other direction is the dangerous one. A plugin built against THIS
    // header, loaded by a host that predates these fields, would read past the
    // end of the host's smaller struct -- whatever happens to sit in memory
    // after it, which a null check cannot catch. So a plugin must not touch
    // anything below this line until BW_Plugin_SetHostApiVersion has told it
    // the host is at least version 2. A host that never calls it is a host
    // where these fields do not exist.

    // Acceleration in the device's own frame, m/s^2. Step and jump detection
    // run on this. Sending position instead makes the host derive it, and a
    // sensor slower than the host's poll rate turns that derivation into noise
    // -- a plugin that knows its device's real update instants should do the
    // differentiation itself and send the result here.
    void (BW_CALLBACK *send_acceleration)(const char* source, float ax, float ay, float az);

    // Borrow the host's OpenVR session, as a vr::IVRSystem*. There is one
    // session per process and the host reference-counts it; a plugin that calls
    // VR_Init itself is the second module fighting over it, and VR_Shutdown
    // from either one tears down the other's. Returns NULL when SteamVR is not
    // running or the host has suppressed OpenVR. Call release_openvr() once for
    // every non-NULL acquire, and re-acquire after a NULL rather than caching.
    // The returned pointer is only valid until the matching release.
    void* (BW_CALLBACK *acquire_openvr)(void);
    void (BW_CALLBACK *release_openvr)(void);

    // ---- added in host API version 3 ----
    // Same rule as the version 2 block above: a plugin must not touch
    // anything below this line until BW_Plugin_SetHostApiVersion has
    // reported 3 or more. A null check is no substitute -- on a version 2
    // host this field is past the end of the struct that host allocated.

    // Tell the host which devices this plugin can offer, so its dropdowns can
    // list them the way the built-in sources do instead of showing a fixed pair
    // of names the user cannot correct.
    //   group   the plugin's own input_source_name, e.g. "Pico Trackers"
    //   id      what the plugin will use as the `source` in send_acceleration
    //           and send_position, e.g. "Pico:LeftFoot" -- this is what gets
    //           written into the user's settings, so keep it stable
    //   name    what to show, e.g. "swift [LeftFoot]"
    // Call it for every device on every scan. The host expires entries it has
    // not heard about for a while, so a device going away needs no separate
    // call, and re-registering the same id just refreshes it.
    void (BW_CALLBACK *register_input_device)(const char* group, const char* id,
                                              const char* name);

    // ---- added in host API version 5 ----
    // Same rule again: not before BW_Plugin_SetHostApiVersion has reported 5.

    // Ask FlatVR to switch Head Lock: locked != 0 puts the screen on the head,
    // 0 leaves it where it is in the room. Meant for a game's own state - the
    // D2R bridge locks it while playing and frees it while the inventory is
    // open, so the panel can be looked around. Call it when the wish CHANGES,
    // not every frame: FlatVR acts on each call once, so the user's own F8 in
    // between is kept until the next call. Ignored unless the user has ticked
    // "Head Lock follows the game" in the FlatVR tab. Only the latest wish is
    // kept: one made while FlatVR is stopped is applied when it starts.
    void (BW_CALLBACK *request_flatvr_head_lock)(int locked);

    // ---- added in host API version 6 ----
    // Same rule again: not before BW_Plugin_SetHostApiVersion has reported 6.

    // Ask FlatVR to show the screen this many metres away for now instead of
    // at the user's own Distance - the D2R bridge pushes it back while the
    // inventory or another panel is open in first person, so the whole panel
    // fits in view. 0 or less withdraws the wish and the screen goes back to
    // the user's own distance. FlatVR eases either way in about 0.4 s, keeps
    // the screen's direction and size, clamps to its own range (0.5 - 20 m)
    // and never writes the wish into the user's settings. Call it when the
    // wish CHANGES, not every frame. Ignored while the user has unticked
    // "Screen distance follows the game" in the FlatVR tab (ticked by
    // default). Only the latest wish is kept: one made while FlatVR is stopped
    // is applied when it starts.
    void (BW_CALLBACK *request_flatvr_screen_distance)(float meters);

    // ---- added in host API version 7 ----
    // Same rule again: not before BW_Plugin_SetHostApiVersion has reported 7.

    // Start (on != 0) or stop (0) FlatVR, as the FlatVR tab's START FLATVR /
    // STOP FLATVR buttons do - so a game's own settings program can offer the
    // button and the player never has to find it in BodyWalk (the D2R bridge
    // passes on D2R VR Settings' Start FlatVR). Done on BodyWalk's next GUI
    // frame; ignored when FlatVR is already in that state. Only the latest
    // wish is kept.
    void (BW_CALLBACK *request_flatvr_running)(int on);

    // ---- added in host API version 8 ----
    // Same rule again: not before BW_Plugin_SetHostApiVersion has reported 8.

    // Where FlatVR's 3D comes from, as the FlatVR tab sets it: 0 none (a flat
    // screen), 1 the game's depth through ReShade, 2 the stereo pair the game
    // draws (two frames, no depth - the ReShade tab's "Stereo pair from the
    // game"). For a game's settings program to offer the choice beside its own
    // stereo switch (D2R VR Settings). Done on BodyWalk's next GUI frame and
    // saved; only the latest wish is kept.
    void (BW_CALLBACK *request_flatvr_stereo_source)(int source);

} BW_HostCallbacks;

#pragma pack(pop)

// ---------------------------------------------------------
// Functions exported BY the Plugin
// BodyWalkVR will search for these using GetProcAddress
// ---------------------------------------------------------

typedef enum {
    BW_STATUS_OK = 0,
    BW_STATUS_WARNING = 1,
    BW_STATUS_ERROR = 2,
    BW_STATUS_CONNECTING = 3
} BW_PluginStatusCode;

#pragma pack(push, 8)

typedef struct {
    BW_PluginStatusCode code;
    char message[256];
    bool has_action;
    char action_name[64];
} BW_PluginStatus;

#pragma pack(pop)

// Required: Initialize the plugin and provide info
// Should return true on success.
// BW_EXPORT bool BW_CALLBACK BW_Plugin_Initialize(const BW_HostCallbacks* callbacks, BW_PluginInfo* out_info);

// Required: Update loop (called every tick ~120Hz)
// BW_EXPORT void BW_CALLBACK BW_Plugin_Update();

// Required: Cleanup resources
// BW_EXPORT void BW_CALLBACK BW_Plugin_Shutdown();

// Optional: Get the current status of the plugin for the Status tab
// BW_EXPORT void BW_CALLBACK BW_Plugin_GetStatus(BW_PluginStatus* out_status);

// Optional: Execute the action associated with the status (e.g. Install/Fix)
// BW_EXPORT void BW_CALLBACK BW_Plugin_ExecuteAction();

// Optional: For Output Plugins to receive Locomotion data
// BW_EXPORT void BW_CALLBACK BW_Plugin_ReceiveLocomotion(float x, float y);

// Optional: the same locomotion, told which controller it belongs to.
// `side` is "Left" or "Right". Buttons have always carried a side; locomotion
// did not, so every output plugin assumed the left hand and users who wanted
// movement on the right one had no way to ask for it.
// A plugin that exports this is never also called through
// BW_Plugin_ReceiveLocomotion, so it does not have to guard against seeing the
// same movement twice. Plugins that do not export it keep receiving the old
// call unchanged, which is what lets an already-installed plugin go on working
// against a newer host.
// BW_EXPORT void BW_CALLBACK BW_Plugin_ReceiveLocomotionSide(const char* side, float x, float y);

// Optional: hold a controller's stick at centre in game, so the player moves
// only when BodyWalkVR says so rather than by pushing the stick.
// Sent on every tick, to every output plugin that exports it, whatever the
// selected mode -- the plugin being switched away from is exactly the one that
// has to let the stick go, and it stops being the selected mode at the same
// moment. Both flags false means release. A plugin that does not export this
// simply never suppresses anything, which is how an already-installed plugin
// goes on working against a newer host.
// BW_EXPORT void BW_CALLBACK BW_Plugin_ReceiveStickBlock(bool block_left, bool block_right);

// Optional: For Output Plugins to receive Button events
// BW_EXPORT void BW_CALLBACK BW_Plugin_ReceiveButton(const char* button_name, bool pressed, const char* side);

// Optional: Told whether the user currently has this plugin's input source or
// output mode selected, and re-told whenever either changes. Locomotion and
// button events are broadcast to every output plugin regardless of the selected
// mode, so a plugin that drives a real runtime should gate on this rather than
// act on every event it receives. Also lets a plugin keep an external process
// idle until it is actually wanted.
// BW_EXPORT void BW_CALLBACK BW_Plugin_SetActive(bool input_active, bool output_active);

// Optional: settings the user edited in BodyWalkVR's own UI, as "key=value"
// lines separated by '\n'. Pushed once when the plugin loads and again whenever
// any of them changes, so a plugin never has to store or reload them itself --
// the host's settings file stays the single copy. Keys are the plugin's own;
// the host only carries them. A plugin that does not export this is simply
// never configured this way, which is how an already-installed plugin goes on
// working against a newer host.
// BW_EXPORT void BW_CALLBACK BW_Plugin_SetSettings(const char* settings);

// Optional: For Output Plugins to receive custom Plugin Actions
// BW_EXPORT void BW_CALLBACK BW_Plugin_ReceiveAction(const char* action_name, bool active);

// Optional: For Output Plugins to receive full Tracking Data
// BW_EXPORT void BW_CALLBACK BW_Plugin_ReceiveTracking(const BW_TrackingData* data);

// Optional: the avatar's chest and waist, for a plugin that republishes them as
// devices. Pushed on the host's own clock, not on a runtime's frame.
// BW_EXPORT void BW_CALLBACK BW_Plugin_ReceiveBodyTrackers(const BW_BodyTrackers* data);

// Optional: how far down BW_HostCallbacks this host actually fills in. Called
// once, before BW_Plugin_Initialize, so a plugin can decide during its own
// startup instead of deferring. Version 1 is everything up to register_action;
// version 2 adds send_acceleration, acquire_openvr and release_openvr;
// version 3 adds register_input_device; version 4 adds nothing to the struct
// and only says the host pushes BW_BodyTrackers; version 5 adds
// request_flatvr_head_lock; version 6 adds request_flatvr_screen_distance;
// version 7 adds request_flatvr_running; version 8 adds
// request_flatvr_stereo_source.
//
// A plugin that uses anything from version 2 or later MUST export this and must
// treat "never called" as version 1: on an older host those fields are past the
// end of the struct, so reading them is undefined and checking them for null
// proves nothing. Exporting it costs a plugin nothing on either host.
// BW_EXPORT void BW_CALLBACK BW_Plugin_SetHostApiVersion(uint32_t version);
#define BW_HOST_API_VERSION 8u

#ifdef __cplusplus
}
#endif

#endif // BWVR_PLUGIN_API_H
