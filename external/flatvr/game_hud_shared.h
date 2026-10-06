#pragma once

// Pieces of a game's interface handed to FlatVR as textures of their own, so
// FlatVR can hang them in the room - the toolbar on a wrist, the map on the
// other hand - instead of leaving them painted into the flat picture.
//
// Written by a game-side mod (first: the D2R vrcam plugin, vr/hud.cpp), read by
// FlatVR (game_hud_source.cpp). Its own mapping, for the reason every block
// beside it is separate: a FlatVR or a mod that predates it simply misses it.
//
// Each panel is a shared D3D12 texture made the way the addon makes its colour
// share (NT handle, owned by source_pid, duplicated by FlatVR), copied into on
// the game's own command list. There is no fence: FlatVR reads whatever the
// texture holds when it looks, as with the picture.
//
// The colour is PREMULTIPLIED and alpha is TRANSMITTANCE - how much of what is
// behind shows through: (0, 0, 0, 1) is empty, alpha 0 is fully covered. That
// is how D2R composes its interface (world * a + ui) and it is passed on
// untouched; the reader turns it into whatever its compositor wants.

#include <cstdint>

#define FLATVR_GAME_HUD_NAME L"Local\\BodyWalkVR_GameHud"
// 2: the look (FlatVRGameHudLook) and the hero's own poses (FlatVRGameHudPose)
// appended; a reader of 1 never reaches them, a reader of 2 seeing 1 uses its
// own defaults.
// 3: the pointer's depth in a stereo pair (FlatVRGameHudPointerDepth) appended.
#define FLATVR_GAME_HUD_VERSION 3u
#define FLATVR_GAME_HUD_PANELS 4u

// What a panel is, so FlatVR can pick where to hang it.
#define FLATVR_HUD_KIND_NONE 0u
#define FLATVR_HUD_KIND_TOOLBAR 1u  // orbs, belt, skills: a wide strip
#define FLATVR_HUD_KIND_MAP 2u      // the corner map

#pragma pack(push, 4)
struct FlatVRGameHudPanel {
  uint64_t nt_handle;  // in source_pid; 0 = no texture (yet)
  uint32_t width;
  uint32_t height;
  uint32_t format;     // DXGI_FORMAT of the texture (D2R: R16G16B16A16_FLOAT)
  uint32_t kind;       // FLATVR_HUD_KIND_*
  // 1 while the game has taken the panel out of its own picture and wants it
  // shown elsewhere; 0 while it draws the panel itself (a menu, the inventory)
  // or has nothing for it - FlatVR then hides it.
  uint32_t visible;
  uint32_t reserved;
};

// How FlatVR shows the panels - all of it the game mod's settings (D2R VR
// Settings > Interface), none of it FlatVR's: FlatVR only carries the textures
// and hangs them where it is told.
struct FlatVRGameHudLook {
  uint32_t bar_anchor;   // 0 hidden, 1 the left forearm, 2 the right forearm, 3 low in front
  float bar_width_m;     // the toolbar strip's length
  float bar_along_cm;    // + further up the arm, - towards the hand
  uint32_t map_anchor;   // 0 hidden, 1 the left hand, 2 the right hand, 3 low in front
  uint32_t map_orb;      // 1 a glass orb in the palm, 0 a flat panel
  float map_width_m;     // the flat panel's width
  float orb_size_m;      // the orb's diameter
  float orb_zoom;        // the map on the orb: 1 = its width across the ball
  float orb_glow;        // halo and rim, 0..1
  uint32_t orb_rgb;      // 0xRRGGBB: halo, rim and mist
  uint32_t encoded;      // 1: the game's colours are display-encoded, not linear light
  float orb_alpha;       // the orb's glass, rim and halo, 0..1 (the map's lines stay); 0 = unset (a mod before it) = 1
  // The toolbar on a forearm, turned and moved in its own frame (+x along the
  // arm towards the hand, +z off the inside of the arm): applied in this order.
  float bar_roll_deg;    // round the arm (about x)
  float bar_tip_deg;     // the far end up or down (about y)
  float bar_spin_deg;    // in its own plane (about z); 180 = the other way round
  float bar_lift_cm;     // further off the arm (+z)
  float bar_side_cm;     // across the arm (+y)
  // The Windows mouse pointer in the headset, as the game wants it now:
  // 0 FlatVR's own rule (drawn for 3 s after it moved), 1 drawn whenever it is
  // shown (a menu, a trader - a pad parks it on an item; the views from above
  // and behind, where it aims), 2 never (first person: it aims otherwise, and
  // a pointer in the middle of the view only gets in the way).
  uint32_t pointer;
  // The toolbar on a forearm cut in two at the middle, the halves side by side
  // (half as long, twice as wide): the half with the right end (blue orb) stays
  // where it was, the other turned end for end beside it, so both orbs meet at
  // one end. 0 one strip, 1 the turned half below it (-y), 2 above it (+y).
  // Was reserved: 0 from an older mod.
  uint32_t bar_split;
  uint32_t reserved;
};

// The hero's forearm a panel lies on, as the game draws it - the game's rig,
// not BodyWalk's controllers: the elbow (forearm bone), the wrist (hand bone)
// and the hand's own axis that lies across the arm (it turns as the wrist
// turns, so the panel turns round the forearm with it). In BodyWalk's hand
// frame: metres from the head, x right, y up, z back, turned by the head's
// yaw only - the frame the controllers reach the game in, so the points land
// where the hero's arm is drawn. valid 0 = FlatVR places the panel from the
// controllers instead.
struct FlatVRGameHudPose {
  uint32_t valid;
  uint32_t reserved0;
  float elbow[3];
  float wrist[3];
  float across[3];
  float reserved1;
};

// The Windows pointer's depth when FlatVR lays it into a side-by-side stereo pair
// (a mod's alternate-frame stereo): each eye's copy moved across by
// base + tilt * v + curve * v * v, v the pointer's height on the picture (0 top, 1 bottom), in
// fractions of one eye's width - the left eye's to the right and the right
// eye's to the left, so + is nearer. A view from above has the ground nearer at
// the bottom of the picture than at the top, and a pointer at the screen's own
// depth came apart over it. 0, 0: on the screen, as before version 3.
struct FlatVRGameHudPointerDepth {
  float base;
  float tilt;
  // Was reserved (0): a straight line fitted the bottom and the middle of D2R's
  // ground and came apart far off at the top - the mod sends three heights.
  float curve;
  // Was reserved (0): the pointer's size at the top of the picture, a fraction
  // of its own; 1 at the bottom, a straight line between - far off it is small,
  // as what it points at is. 0 (unset) = 1, its own size everywhere.
  float top_scale;
};

struct FlatVRGameHud {
  uint32_t version;      // FLATVR_GAME_HUD_VERSION
  uint32_t source_pid;   // the process that owns the handles
  uint32_t counter;      // bumped after every frame's copies are recorded; a stalled one = the game stopped
  uint32_t panel_count;  // used entries in panel[]
  FlatVRGameHudPanel panel[FLATVR_GAME_HUD_PANELS];
  // version 2
  FlatVRGameHudLook look;
  FlatVRGameHudPose pose[FLATVR_GAME_HUD_PANELS];
  // version 3
  FlatVRGameHudPointerDepth pointer_depth;
};
#pragma pack(pop)

static_assert(sizeof(FlatVRGameHudPanel) == 32, "FlatVRGameHudPanel is a wire format");
static_assert(sizeof(FlatVRGameHudLook) == 80, "FlatVRGameHudLook is a wire format");
static_assert(sizeof(FlatVRGameHudPose) == 48, "FlatVRGameHudPose is a wire format");
static_assert(sizeof(FlatVRGameHudPointerDepth) == 16, "FlatVRGameHudPointerDepth is a wire format");
static_assert(sizeof(FlatVRGameHud) == 16 + 32 * FLATVR_GAME_HUD_PANELS + 80 + 48 * FLATVR_GAME_HUD_PANELS + 16,
              "FlatVRGameHud is a wire format");

// What FlatVR uses when the mod sends no look (version 1): the defaults the
// mod's settings start from too.
inline FlatVRGameHudLook FlatVRGameHudDefaultLook() {
  FlatVRGameHudLook l{};
  l.bar_anchor = 2;
  l.bar_width_m = 0.22f;
  l.map_anchor = 1;
  l.map_orb = 1;
  l.map_width_m = 0.22f;
  l.orb_size_m = 0.12f;
  l.orb_zoom = 1.0f;
  l.orb_glow = 0.35f;
  l.orb_rgb = 0xA0C3FFu;
  l.encoded = 1;
  l.orb_alpha = 1.0f;
  return l;
}
