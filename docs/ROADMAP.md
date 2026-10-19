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
  ([issue 7](KNOWN_ISSUES.md)).
  [repo]
- **Why a signal after every replayed list keeps the GPU from hanging**
  ([issue 9](KNOWN_ISSUES.md)). Since 0.168 the lists go in the game's own
  submit calls (`[debug] replay_marks=3`) and do not hang; the cause is still
  unknown.
  [repo]
- **Reuse the left eye's work in the right eye's replay:** the shadow maps
  and the render graph's passes that do not depend on the eye. [repo]
- **DLSS jitter shared fairly between the eyes**
  ([issue 8](KNOWN_ISSUES.md)).
  [repo]
- **FlatVR's frame conveyor:** each frame shown at the head pose it was drawn
  for, so a slow frame does not judder. [BodyWalk/FlatVR]

## The picture

- **Ceilings in every dungeon.** Today: the Act 1 caves, crypts, barracks,
  cathedral and catacombs, and the Act 2 sewers and palace. Every other
  dungeon still shows a black void overhead. It is mostly tuning and a stone
  picture per dungeon
  ([issue 2](KNOWN_ISSUES.md)).
  [repo]
- **Vertical walls up to the ceiling** in dungeons whose walls are low and
  cut, such as the Act 2 sewers
  ([issue 4](KNOWN_ISSUES.md)). [repo]
- **Fire that glows through the fog** instead of fading with it
  ([issue 3](KNOWN_ISSUES.md)). A test is in 0.170: `[fog] spare_effects`
  (World > Fog > *Fog spares fire, spells and smoke*, off). [repo]
- **Torchlight on the ceilings**, found cheaply - not by reading the game's
  whole heap. [repo]
- **A night sky in layers:** today the stars drift with the clouds. The plan
  is still stars and moon with clouds drifting over them, 12 pictures, and the
  single band as a fallback. The shader already draws procedural stars
  (`SkyStars`). [repo]
- **Ground darkening at grazing angles**, fixed in the code. The advice was
  the game's Ambient Occlusion set to Medium. Since 0.164 the game's ambient
  occlusion reads its depth from our camera (`[render] depth_fix`, on), and
  nothing in the mod depends on the AO quality: Max is fine. Not yet checked
  in the game at grazing angles. [repo]

## First person (F4)

- **Weapon weight:** in the game since 0.165 (the Weapon Weight tab; on in the
  settings that come with the mod since 0.166). To do: the feel of the defaults, two-handed grips on every
  weapon kind. See [WEAPON_WEIGHT.md](WEAPON_WEIGHT.md)
  ([issue 17](KNOWN_ISSUES.md)). [repo]
- **Hit by touch:** in the game since 0.165 (on in the settings that come with
  the mod since 0.166; since 0.170 the trigger no longer attacks while it runs,
  `[gameplay] melee_touch_only`): the blade's touch on a
  monster's body picks the target and the moment, instead of a swing gesture
  letting the game choose. To do: the speed and start defaults, the blades of
  the thrown kinds ([issue 13](KNOWN_ISSUES.md), [issue 17](KNOWN_ISSUES.md)).
  [repo]
- **The weapon stops at monsters** instead of passing through them. [repo]
- **Dim the hero's own effects around him** (a player's request, 2026-10-18): in first person
  Bone Armor wraps the eyes and is blinding. In the 0.170 build, but off and not working yet
  (`[render] dim_own_effects`, Body > *Draw the hero's own effects fainter*): the effect's
  "Fade" is set and nothing changes on screen ([issue 25](KNOWN_ISSUES.md)). [repo]
- **Aim distance by arm** (players' request, 2026-10-18): DONE in 0.170 (`[bow]
  aim_distance_by_arm`, Weapon Adjust > *Aim distance by the arm*; on in the shipped settings
  since 0.171). Spells only; a
  staff in both hands always far; bows, crossbows and throws unchanged. [repo]
- **Gestures for skills** (the swing, the throw, the bow's draw and the swap
  over the shoulder are in; the shield bash casts Smite, and Double Swing by the
  two hands is in 0.170, off and not yet tried; a gesture for any skill is not,
  [issue 15](KNOWN_ISSUES.md)):
  - a gesture or a sequence of gestures for a given skill - first request:
    the Paladin's Zeal;
  - players recording their own movements and mapping them to skills;
  - a guide for it.

  The mapping should be a table players can edit, not code for each skill.
  Recognising gestures is BodyWalk's part; the skills are ours.
  [repo + BodyWalk/FlatVR]
- **Weapon calibration:** good defaults for all 17 Weapon Adjust kinds, and
  two-handed grips tested with every weapon kind
  ([issue 13](KNOWN_ISSUES.md)). [repo]
- **Attack and pick up as direct game commands**, not through the virtual
  pad's A button. 68 other commands already go straight to the game (0.144);
  Controls without BodyWalk (0.165) gives the game its own gamepad, and Direct
  hit (experimental) calls the game's attack itself. [repo]
- **The new controls in F1 - F3 too**, not in first person only
  ([issue 16](KNOWN_ISSUES.md)). DONE for the controller in 0.167
  (`[direct] flat_controller`: the F4 layout without its panels); gestures, hit
  by touch, the bow's draw, throws and the skill panel stay F4 only. [repo]
- **The upper body from body trackers** (chest, waist) and forearm twist bones
  that follow the wrist. [repo + BodyWalk/FlatVR]
- **Walking in the room moves the hero** (native OpenXR): DONE in 0.168 as
  room-scale walking (`[openxr] roomscale_walk`, Stereo > Native OpenXR, off; F4):
  your steps are the game's own walking stick. Off, the camera is held to the
  hero's body: a lean of 10 cm, then the body follows. [repo]

## Interface

- **The map orb on the hero's palm**, not at the controller. [repo]
- **The rest of the interface nearer than the screen** in stereo
  ([issue 6](KNOWN_ISSUES.md)).
  [repo]
- **Real font size for item labels on the ground**, instead of a scale.
  [repo]
- **Monster name plate opacity**
  ([issue 19](KNOWN_ISSUES.md)).
  [repo]
- **Thumbstick dead zones against stick drift** (built, not yet tried with a
  controller): a radial dead zone and an outer edge per stick, in %, for the VR
  controllers' sticks (`[direct] stick_deadzone_*_pct`, `stick_outer_*_pct`;
  Controls > Thumbsticks: dead zone). Left: a test on controllers that really
  drift, and the same sliders in the headset's settings panel. [repo]

## BodyWalk Game Link

D2R VR used to talk to BodyWalk through its own bridge (`d2r_bridge.dll`) and
its own shared-memory blocks. BodyWalk replaces the per-game bridges with one
standard, **Game Link**: a shared `game_link.dll` plugin in BodyWalk and one
header, `bodywalk/game_link.h`, for game mods. It is MIT-licensed and goes into
the public SDK
[integralab-am/bodywalkvr-plugin-sdk](https://github.com/integralab-am/bodywalkvr-plugin-sdk).

Done (after 0.156):

- **vrcam and D2R VR Settings speak `game_link.h`** (`vr/link.cpp`): a manifest
  of the mod's actions (`D2R: ...`, `D2R key: ...`) and gesture tabs, a
  GameState block, Requests instead of the named FlatVR events, and for native
  OpenXR the game's headset to BodyWalk (XrInput) and BodyWalk's gesture zones
  back (VrZones).
- **The game-specific logic moved from the bridge into vrcam:** which view uses
  which gesture tab, Head Lock, and the screen distance for the panels.
- **The installer ships `game_link`** instead of `d2r_bridge`, and the game's
  manifest, so its actions are in BodyWalk's Mapping from its first start.

Still to do:

- **`game_link.h` through CMake's FetchContent** once the SDK repository is
  out; until then it is copied into `external/`.
- **The VrRunes section of `game_link.h` in the public SDK header**, so the tree
  builds as it is ([issue 23](KNOWN_ISSUES.md)).
- **`game_link` inside BodyWalk itself**, so no mod has to install it.

[repo + BodyWalk/FlatVR]

## Game updates

- **Field offsets found by signature too**, like the code addresses already
  are, so a game update needs no new build
  ([issue 21](KNOWN_ISSUES.md)).
  [repo]
