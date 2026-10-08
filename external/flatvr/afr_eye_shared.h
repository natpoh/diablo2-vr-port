#pragma once

// Alternate-frame stereo from a game mod, read by this addon.
//
// A game-side mod (first: the D2R vrcam plugin) moves its camera to the left
// eye on one frame and to the right eye on the next, and says here which eye
// the frame it has just built is for. The addon then copies each presented
// frame into its half of a double-wide colour texture, so FlatVR receives a
// finished side-by-side pair through the colour share it already reads.
//
// Its own mapping, for the reason the addon's other blocks are separate: a mod
// or an addon that predates it simply does not find it. Written by the mod,
// read by the addon; both live in the game process.
//
// The eye is the one of the view built LAST, not of the frame being presented.
// A game that presents a frame after it has started building the next one sees
// them shifted by one, constantly - which `swap` (set by the mod, from the
// user's setting) undoes.

#include <cstdint>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define FLATVR_AFR_EYE_NAME L"Local\\FlatVR_AfrEye"
#define FLATVR_AFR_EYE_VERSION 1u

#pragma pack(push, 4)
struct FlatVRAfrEye {
  uint32_t version;  // FLATVR_AFR_EYE_VERSION
  uint32_t enabled;  // 0: the mod renders one eye only; publish the plain frame
  uint32_t eye;      // 0 = left, 1 = right: the eye of the view built last
  uint32_t frame;    // bumped with every view the mod builds
  uint32_t swap;     // 1: the presented frame is the OTHER eye (pipelined game)
  // When the mod built each eye's view, and so which head pose it is drawn
  // for: FLATVR_AFR_STAMP units of the QueryPerformanceCounter clock (shared
  // by every process on the machine), wrapping. Valid while stamp_magic says
  // so - a mod that predates them leaves the three reserved words at zero.
  uint32_t stamp[2];
  uint32_t stamp_magic;  // FLATVR_AFR_STAMP_MAGIC
};
#pragma pack(pop)

static_assert(sizeof(FlatVRAfrEye) == 32, "FlatVRAfrEye is a wire format");

#define FLATVR_AFR_STAMP_MAGIC 0x504D5453u  // "STMP"
#define FLATVR_AFR_STAMP_PER_SECOND 10000u   // 0.1 ms: wraps every 5 days, differences stay right

// The stamp clock: QPC in 0.1 ms, low 32 bits.
inline uint32_t FlatVRAfrStampNow() {
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return (uint32_t)((unsigned long long)c.QuadPart / ((unsigned long long)f.QuadPart / FLATVR_AFR_STAMP_PER_SECOND));
}

// What the addon actually put into each half of the shared pair, written by
// the addon when it copies a half, read by FlatVR with the picture: the stamp
// of the view each half holds now. FlatVR places the screen at the head pose
// of that moment, and the headset's own reprojection then moves it to where
// the head is at display - the way a native VR game submits the pose it
// rendered with. Its own mapping, so an older FlatVR or addon simply misses it.
#define FLATVR_AFR_HALVES_NAME L"Local\\BodyWalkVR_AfrHalves"
#define FLATVR_AFR_HALVES_VERSION 1u

#pragma pack(push, 4)
struct FlatVRAfrHalves {
  uint32_t version;        // FLATVR_AFR_HALVES_VERSION
  uint32_t frame_counter;  // the colour block's frame_counter this was written with
  uint32_t stamp[2];       // each half's view stamp (FlatVRAfrStampNow units)
  uint32_t valid;          // bit 0 / 1: that half has a stamp
  uint32_t reserved[3];
};
#pragma pack(pop)

static_assert(sizeof(FlatVRAfrHalves) == 32, "FlatVRAfrHalves is a wire format");

// The head FlatVR hands BodyWalk on each of its frames is the headset's
// PREDICTION for that frame's display, not the head of the moment it is
// handed over. So FlatVR names each one - a stamp, in the clock above - keeps
// what it handed over under that name, and publishes the name here just
// before handing it over. A game-side mod (through BodyWalk's D2R Bridge,
// which runs inside BodyWalk) carries the name with every frame drawn from
// that head, and FlatVR places the screen at exactly the pose it handed out.
#define FLATVR_HEAD_SAMPLE_NAME L"Local\\BodyWalkVR_HeadSample"
#define FLATVR_HEAD_SAMPLE_VERSION 1u

#pragma pack(push, 4)
struct FlatVRHeadSample {
  uint32_t version;   // FLATVR_HEAD_SAMPLE_VERSION
  uint32_t stamp;     // the name of the head being handed to BodyWalk now
  uint32_t counter;   // bumped with every one
  uint32_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(FlatVRHeadSample) == 16, "FlatVRHeadSample is a wire format");

// The screen FlatVR shows the game on, as it is on this frame, written by
// FlatVR every frame, read by a game-side mod that renders its own view (the
// D2R first-person camera): a perspective whose frustum is exactly this
// screen puts the world at its true angular size, whatever the screen's
// width, height (the picture's aspect, any crop) or distance. Its own
// mapping, so an older FlatVR or mod simply misses it.
#define FLATVR_SCREEN_GEOM_NAME L"Local\\BodyWalkVR_ScreenGeom"
#define FLATVR_SCREEN_GEOM_VERSION 1u

#pragma pack(push, 4)
struct FlatVRScreenGeom {
  uint32_t version;    // FLATVR_SCREEN_GEOM_VERSION
  uint32_t counter;    // bumped with every write; a stalled one means FlatVR stopped
  float width_m;       // the screen across, metres
  float height_m;      // and up: width times the shown picture's height / width
  float distance_m;    // ahead of the head
  uint32_t shape;      // 0 flat, 1 cylinder, 2 sphere, 3 fisheye
  float bend;          // that shape's curvature / strength; 0 = effectively flat
  uint32_t head_locked;
};
#pragma pack(pop)

static_assert(sizeof(FlatVRScreenGeom) == 32, "FlatVRScreenGeom is a wire format");

// The camera the shared depth was drawn with, written every frame by a mod
// that builds the game's camera itself (D2R VR's vrcam), read by FlatVR: with
// it, 3D from ReShade's depth comes out at the world's own size - the second
// eye shifted for the user's IPD by each pixel's distance in metres - instead
// of the depth sliders' guess. The depth is reversed and infinite: a pixel's
// distance along the view is near_world / depth, in world units. Its own
// mapping; a stalled counter means the mod's camera is off.
#define FLATVR_DEPTH_CAMERA_NAME L"Local\\BodyWalkVR_DepthCamera"
#define FLATVR_DEPTH_CAMERA_VERSION 1u

#pragma pack(push, 4)
struct FlatVRDepthCamera {
  uint32_t version;        // FLATVR_DEPTH_CAMERA_VERSION
  uint32_t counter;        // bumped with every frame the mod's camera is on
  float near_world;        // distance = near_world / depth, world units
  float units_per_metre;   // world units in a metre of the user's room
  float tan_half_w;        // the camera's tan(horizontal FOV / 2)
  float tan_half_h;        // and vertical
  uint32_t flags;          // FLATVR_DEPTH_CAMERA_*; was reserved, so an older mod sends 0
  uint32_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(FlatVRDepthCamera) == 32, "FlatVRDepthCamera is a wire format");

// The depth is upside down against the picture: a Unity game on Direct3D
// draws into a texture flipped and turns it back only on the way to the
// screen (the Heroes Olden Era mod sets it). FlatVR flips it where it takes
// the buffer in, as its own "Flip Depth Map Vertically" box would.
#define FLATVR_DEPTH_CAMERA_UPSIDE_DOWN 1u
