# Roadmap

Where D2R VR is going, and which parts anyone can pick up.

- The current problems are in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
- What you need to know before touching the code is in
  [DEVELOPER_NOTES.md](DEVELOPER_NOTES.md).

Tags:

- **[repo]** - the work is in this repository.
- **[BodyWalk/FlatVR]** - the work needs BodyWalk or FlatVR, which are
  separate products. Talk to us on Discord first.

---

## Now: make the new stereo solid

"Stereo - one pass, two pictures" (0.153) draws each frame once on the CPU and
twice on the GPU. It is the fastest true 3D so far, and it still has loose
ends:

- **A DLSS for each eye** in the one-pass mode
  ([issue 8](KNOWN_ISSUES.md)).
  [repo]
- **Why a signal after every replayed list keeps the GPU from hanging**
  ([issue 10](KNOWN_ISSUES.md)).
  [repo]
- **Reuse the left eye's work in the right eye's replay:** the shadow maps
  and the render graph's passes that do not depend on the eye. [repo]
- **DLSS jitter shared fairly between the eyes**
  ([issue 9](KNOWN_ISSUES.md)).
  [repo]
- **FlatVR's frame conveyor:** each frame shown at the head pose it was drawn
  for, so a slow frame does not judder. [BodyWalk/FlatVR]

## The picture

- **Ceilings in every dungeon.** Today: the Act 1 caves, crypts, barracks,
  cathedral and catacombs, and the Act 2 sewers and palace. Every other
  dungeon still shows a black void overhead. It is mostly tuning and a stone
  picture per dungeon
  ([issue 3](KNOWN_ISSUES.md)).
  [repo]
- **Vertical walls up to the ceiling** in dungeons whose walls are low and
  cut, such as the Act 2 sewers
  ([issue 5](KNOWN_ISSUES.md)). [repo]
- **Fire that glows through the fog** instead of fading with it
  ([issue 4](KNOWN_ISSUES.md)). [repo]
- **Torchlight on the ceilings**, found cheaply - not by reading the game's
  whole heap. [repo]
- **Lut Gholein's haze** at night: fix the game's own pass instead of skipping
  it ([issue 1](KNOWN_ISSUES.md)).
  [repo]
- **A night sky in layers:** today the stars drift with the clouds. The plan
  is still stars and moon with clouds drifting over them, 12 pictures, and the
  single band as a fallback. The shader already draws procedural stars
  (`SkyStars`). [repo]
- **Ground darkening at grazing angles**, fixed in the code. Today the fix is
  the game's Ambient Occlusion set to Medium. [repo]

## First person (F4)

- **Weapon weight:** the model and its benches are in, not yet in the game.
  See [WEAPON_WEIGHT.md](WEAPON_WEIGHT.md). [repo]
- **Hit by touch:** the blade's touch on a monster's body picks the target and
  the moment, instead of a swing gesture letting the game choose. [repo]
- **The weapon stops at monsters** instead of passing through them. [repo]
- **Gestures for skills:**
  - a gesture or a sequence of gestures for a given skill - first request:
    the Paladin's Zeal;
  - players recording their own movements and mapping them to skills;
  - a guide for it.

  The mapping should be a table players can edit, not code for each skill.
  Recognising gestures is BodyWalk's part; the skills are ours.
  [repo + BodyWalk/FlatVR]
- **Weapon calibration:** good defaults for all 17 Weapon Adjust kinds, and
  two-handed grips tested with every weapon kind
  ([issue 14](KNOWN_ISSUES.md)). [repo]
- **Attack and pick up as direct game commands**, not through the virtual
  pad's A button. 68 other commands already go straight to the game (0.144).
  [repo]
- **The upper body from body trackers** (chest, waist) and forearm twist bones
  that follow the wrist. [repo + BodyWalk/FlatVR]
- **Walking in the room moves the hero** (native OpenXR): a real step forward or
  to the side presses the stick that way, as VR games do. Today the camera is
  held to the hero's body: a lean of 10 cm, then the body follows. [repo]

## Interface

- **The map orb on the hero's palm**, not at the controller. [repo]
- **The rest of the interface nearer than the screen** in stereo
  ([issue 7](KNOWN_ISSUES.md)).
  [repo]
- **Real font size for item labels on the ground**, instead of a scale.
  [repo]
- **Monster name plate opacity**
  ([issue 17](KNOWN_ISSUES.md)).
  [repo]

## BodyWalk Game Link

Today D2R VR talks to BodyWalk through its own bridge (`bodywalk_bridge/`,
`d2r_bridge.dll`) and its own shared-memory blocks. BodyWalk is replacing the
per-game bridges with one standard, **Game Link**: a shared `game_link.dll`
plugin in BodyWalk and one header, `bodywalk/game_link.h`, for game mods. It
is MIT-licensed, in the public SDK
[integralab-am/bodywalkvr-plugin-sdk](https://github.com/integralab-am/bodywalkvr-plugin-sdk).
The SDK is just starting.

For D2R VR this means:

- **vrcam and D2R VR Settings move to `game_link.h`:**
  - a manifest of the mod's actions (`D2R: ...`, `D2R key: ...`) and gesture
    tabs;
  - a GameState block instead of `D2R_State`;
  - a Requests block instead of the five named FlatVR events.
- **The game-specific logic moves from the bridge into vrcam:** which view
  uses which gesture tab, Head Lock, and the screen distance for the panels.
- **`bodywalk_bridge/` and `external/bodywalk` go;** the SDK comes in through
  CMake's FetchContent.
- **The installer ships `game_link`** instead of `d2r_bridge`.

[repo + BodyWalk/FlatVR]

## Game updates

- **Field offsets found by signature too**, like the code addresses already
  are, so a game update needs no new build
  ([issue 19](KNOWN_ISSUES.md)).
  [repo]
