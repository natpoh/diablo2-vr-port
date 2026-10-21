// sky_view_eyes.h - which eye's sky camera (vrcam's g_skyView: the effect's CamRight/CamUp/CamBack/SkyProj 0 and 1) and FlatVR stamp
// an eye's camera build writes, in a stereo pair of one game frame.
//
// The right eye's picture is drawn either by a pass of its own or by the replay (the left pass's lists again with the right eye's
// camera constants: the left camera moved by the eye gap). Its sky must be drawn with the camera its picture was drawn with, and no
// build may write it before that eye's effects are on their way:
//  - a pass of its own: that pass's camera. ReShade keeps ONE constant buffer per effect and writes both eyes' arrays into it at
//    every present's effects (D3D12: in place, no renaming), and the graphics card drew the right eye's effects after the next
//    pair's left present had written its values (as the eyes-by-turns overlays showed, 2026-10-20). The left build copied the right
//    eye's view whenever the replay was merely WANTED, so the next pair's left values carried a right-eye camera one pair ahead and
//    the right eye's sky shook against its world as the head turned - while walking, when the game sets its biome at every line of
//    its 80-unit grid and the replay's settle (replay_pause.h) keeps every pair in two passes (the author, 2026-10-21: "the sky
//    judders in the right eye while walking and turning the head"; the biome sets: bearstonem/d2r-pcvr PERFORMANCE.md).
//  - replayed: the left build's camera moved across. The left build keeps it aside (KeepForRight) and the replay puts it in when it
//    draws the right eye (vrcam replay::RightFromLeft), after the left eye's effects; the right eye's own build (d2rcam::Refresh
//    before the replay) does not write it.
// Pure; cleanroom/tests/sky_view_eyes_test.cpp proves it against a model of that buffer. Eyes by turns: FenceEffects, below.
#pragma once

namespace skyeyes {

// eye: the eye of the build (0 left, 1 right); rightReplayed: this pair's right eye is drawn by the replay (vrcam replay::g_capturing:
// from the pair's left pass until the replay draws the right eye).
// The build writes its own eye's sky view and stamp.
constexpr bool OwnEye(int eye, bool rightReplayed) { return !(eye == 1 && rightReplayed); }
// The build keeps the right eye's (its own moved by the eye gap) aside, for the replay to put in.
constexpr bool KeepForRight(int eye, bool rightReplayed) { return eye == 0 && rightReplayed; }

// With the rule above, two writes of the buffer in a row agree on what the earlier one's eye reads: the right eye's effects leave the
// left eye's camera as it was, the next pair's left eye leaves the right eye's. So one present's lead is safe - but no more: with
// the graphics card two presents behind (pairs in two passes have nothing else bounding it: the replay's hold waits for the last
// replay, replay_fx_sync) the next pair's values reached this pair's effects in either eye, and the sky still shook now and then
// (the author on 0.176, 2026-10-21, after the fix above: "the sky still shakes now and then"). So in a pair a present's effects
// write the buffer only once the graphics card has drawn the effects of the present two before them - the same eye's last
// ([stereo] pair_fx_sync, vrcam pairfx::). The CPU still runs a present ahead; the replay's own waits are stronger already.
struct FxRing {
    unsigned long long last = 0, beforeLast = 0;   // the fence values signalled after the last two effects (0: none)
    void Rendered(unsigned long long v) { beforeLast = last; last = v; }
    unsigned long long WaitBeforeWriting() const { return beforeLast; }   // done on the GPU before this present's effects write
};

// Eyes by turns (native OpenXR, one eye a game frame, the other made on the GPU - xr.h TurnKeep): every build writes BOTH eyes' views -
// the eye drawn its own, the eye made this one moved across (its overlays) - and every present renders the effect three times: the
// eye drawn's effects (its sky, its fog), its overlays (D2R_Overlay), the eye made's overlays (D2R_OverlayMade) over the warped picture.
// One eye a frame is about half the graphics card's work, and the game's thread runs ahead of it: the next present's effects overwrote
// the cameras before the graphics card drew this one's - the eye drawn's sky came out with the next frame's head turn, a frame off
// its world, as often as the card was behind (the author, test-0176: "the sky shakes in both eyes when I turn my head" - the eye made
// is warped from the eye drawn's picture, sky and all). So by turns each present's effects wait until the graphics card is past the
// last present's renders ([stereo] turn_fx_fence, on; vrcam scenedepth::MarkFx / WaitLastFx), and the mark goes after the LAST of
// them - the eye made's overlays - not after the effects alone (where [render] fx_fence marks a frame of one picture): marked there,
// the eye made's overlays would still read the next frame's cameras. The wait costs little: the next frame's scene is on the queue
// behind the mark, so the graphics card stays busy.
// fxFence: [render] fx_fence (F4); byTurns: native OpenXR by turns with its overlays apart; turnFence: [stereo] turn_fx_fence.
constexpr bool FenceEffects(bool fxFence, bool byTurns, bool turnFence) { return fxFence || (byTurns && turnFence); }
// The mark after this present's overlays (by turns), else right after its effects (as before).
constexpr bool MarkAfterOverlays(bool byTurns, bool turnFence) { return byTurns && turnFence; }

}  // namespace skyeyes
