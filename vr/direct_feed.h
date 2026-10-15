// direct_feed.h - direct input from the plugin's own side: what xr.cpp, link.cpp and vrcam.cpp call (the gesture code
// uses direct_input.h only). See direct_input.h for what the mode is.
#pragma once

#include <cstdint>

#include "d2r_vr_shared.h"
#include "direct_input.h"
#include "walk_flat.h"

namespace directinput {

// The log line sink (xr.cpp's); null = silent.
void SetLogger(void (*log)(const char*));

// [openxr] direct_input and the ini's [direct] table (see direct_layout.h), on every change of d2r_vr.ini. Any thread.
void LoadSettings(const wchar_t* iniPath);

// [openxr] direct_bodywalk_walk (default 1) with direct input on: BodyWalk's walking passes in direct mode (vr/walk_path.h). BodyWalk is still
// given the head and the hands (link::PublishXr - with the controls zeroed: its Mapping presses nothing a second time), it stays on the game's
// headset (xr.cpp's presenter request), and ITS LEFT STICK is mixed into the controller's own (vrcam's PadMirrorRead); nothing else of its pad,
// its actions or its commands is read. 0 = direct mode ignores BodyWalk whole, as it did. Any thread.
bool BodyWalkWalks();

// xr.cpp: a native session began (frames follow) / ended (everything the session held is let go, the newest frame gone).
void SessionBegin();
void SessionEnd();

// xr.cpp, once per headset frame in Publish, after the laser took what it takes: the frame to the gesture listeners and,
// with direct mode on, to the layout. `menu` = a panel is open in the game or there is no game (vrcam's SetLaserWanted).
// *in is changed: a control a gesture took (Take) reads as let go in it, so BodyWalk does not see it either.
void Feed(D2RVR_XrInput* in, bool menu);

// vrcam, every millisecond (its timer): the game shows a menu or panel now, or no game (the title and character screens).
// Between the game opening a menu and the next headset frame the layout in force is still the world's: until Feed has
// caught up, the pad, the actions and the commands read as let go (the game's pad poll must not get an A press meant for
// the world while a panel has just opened - and the other way round).
void SetMenuNow(bool menu);

// What the game's XInput poll gets in direct mode (vrcam: PadMirrorRead). False unless Active(). `counter` moves with
// every frame.
struct PadOut {
    uint16_t buttons;
    uint8_t lt, rt;
    int16_t lx, ly, rx, ry;
    uint32_t counter;
};
bool PadRead(PadOut* out);
// Why PadRead gave nothing, in words for the log ("" while Active()); *ageMs: how old the newest headset frame is (0 = none yet).
const char* WhyNotActive(uint32_t* ageMs);
// Walking in VR F1, F2, F3 ([direct] walk_flat_views, vr/walk_flat.h): the one thing direct input does outside F4 - the controller's LEFT
// stick, and nothing else, for the game's walking code (vrcam: PadMirrorRead). Feed keeps the stick per headset frame in these views
// (through a release gate: a stick held when the view, the focus or a panel changes walks nothing until it is let go).
// Fills the facts this layer knows into *in (the key, direct input on, a flat view, a frame in the last second) and *out with the stick
// (XInput's range, `counter` moves with every frame); returns false - *out not filled - unless the key, direct input and the view all
// say yes. The caller adds what only it knows (BodyWalk running, a game, a panel, the chat) and asks walkflat::Decide. Any thread.
struct WalkOut {
    int16_t lx, ly;
    uint32_t counter;
};
bool WalkRead(WalkOut* out, walkflat::Inputs* in);
// The same facts without the stick, whatever the keys say: keyOn = [direct] walk_flat_views, directOn, flatView, framesFresh (the rest of *in untouched).
void FlatFacts(walkflat::Inputs* in);

// Belt potions in VR F1-F3 ([direct] belt_flick_flat_views, vr/belt_flick.h): with the left trigger held, a flick of either thumbstick drinks
// belt column 1 (left), 2 (up), 3 (right) or 4 (down). Feed runs the machine in these views (never in F4: there the layout is in charge) and
// calls the sink with the column 0..3 - vrcam's: the game's own belt key command (gamecmd::Tap(0x17 + column)), after its own check that no
// BodyWalk is running, a game is on and no panel or chat is open (the sink says no by doing nothing). Called on the headset frame's thread.
using BeltSink = void (*)(uint32_t column, int stick, int dir);   // stick 0 left / 1 right; dir 1 left, 2 up, 3 right, 4 down (beltflick::Dir)
void SetBeltSink(BeltSink sink);
bool BeltFlickOn();   // [direct] belt_flick_flat_views (the sink asks: it replaces walkflat's key for its own verdict)
// The pad buttons the gestures pulse now (PadPulse), to be ORed into the pad the game is about to get - any mode, any
// pad source, a pad that is not there too. False when there are none (the outputs are zero then).
bool PadPulses(uint16_t* buttons, uint8_t* lt, uint8_t* rt);

// link.cpp: BodyWalk's Actions / Commands / Head / gesture zones, replaced in direct mode. The D2RVRAction bits and the
// game's commands (bit n of out = command n) the layout holds, and what the gestures hold, apart: link.cpp ORs the
// injected ones over whichever of the two it has. Any thread.
uint32_t LayoutActions();
uint32_t InjectedActions();
void LayoutCommands(uint64_t out[2]);
void InjectedCommands(uint64_t out[2]);
// The head and hands of the newest frame as vrcam reads them (a ring of copies: a pointer stays good for a few frames).
// Null before the first frame.
const D2RVR_Shared* HeadShared();

// The shared block from a frame, as vr/vrcam.cpp's XrIntoShared makes it (public for the unit test). *out is
// overwritten in full; `counter` is the previous block's counter.
void SharedFromXr(const D2RVR_XrInput& in, uint32_t counter, D2RVR_Shared* out);

}  // namespace directinput
