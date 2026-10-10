# Known issues

What is broken or unfinished right now (D2R VR 0.153), what we already know
about each problem and where in the code to start. Pick one, fork, fix it and
send a pull request - or tell us on Discord what you found.

Each issue is tagged:

- **[repo]** - the fix belongs in this repository.
- **[BodyWalk/FlatVR]** - the problem is in BodyWalk, FlatVR or FlatVR's ReShade
  add-on. They are separate products and are not in this repo. Report what you
  find on Discord.

How to look into the game's frames, find a pass or read the log:
[DEVELOPER_NOTES.md](DEVELOPER_NOTES.md).

---

## Picture

### 1. Haze with a straight edge in the Act 2 town (Lut Gholein) [repo]

**What you see:** at night in Lut Gholein a straight line runs across the
whole picture, with haze on one side of it and a darker picture on the other.
The line stays when D2R VR's own fog and the interface mask are switched off.

**What we know:**

- The game draws it. It comes from one of the game's own passes: in a frame
  trace it is pass `#747`, one draw into the full-size R11G11B10 lighting
  target, the third of three such passes in a row. The game's height fog is
  not the cause: vrcam already sets it to zero (`gamefog::`).
- Skipping the pass removes the line (`[debug] skip_pipeline=<pipe>`), but the
  whole scene goes darker. That pass also carries the game's light, so it
  cannot simply go into `[render] skip_shaders`.

**Idea:** keep the pass and change its constants instead. Its height and
border are probably set for the game's camera from above and do not suit our
camera.

**Where to start:**

- `vr/uitrace.cpp`: `OnInitPipeline`, `SetSkipShaders`.
- The frame trace (Ctrl+F10 in the game), `tools/uitex_to_png.py` and
  `tools/trace_passes.py --depth`. The steps are in DEVELOPER_NOTES.md,
  "Finding a game pass".

### 2. A haze band across the screen in dungeons [repo]

**What you see:** a band across half the screen in dungeons, from first
person. It is not D2R VR's fog. It is also not the game's fog density or
volumetric fog: changing those does nothing to it.

**Guess:** it is the edge of the hero's light radius. The game darkens
everything beyond the light circle, and from first person that edge reads as
a straight line on the floor. Check this without writing to the game's memory.

### 3. Cave ceilings are only in some dungeons [repo]

From first person (and in other views where you see the top of the walls), the
black void over a dungeon gets a stone vault. The vault is drawn by ReShade
from the depth.

**Done:**

- Act 1: caves, crypts, barracks / the jail, the cathedral and the catacombs.
- Act 2: the sewers and the palace cellar.

**Not done:** every other dungeon - the Act 2 tombs, the Maggot Lair, the Claw
Viper Temple, the Act 3 sewers, dungeons and temples, and the Act 5 caves,
ice caves and Nihlathak's Temple.

**How to add a dungeon:**

1. Add its biome to `[ceiling] biomes` in `d2r_vr.ini`. The log names every
   biome you walk into (`vrcam: biome '...'`).
2. Tune its own keys: `height_<biome>`, `brightness_<biome>`,
   `light_radius_<biome>`, `relief_<biome>`, `texture_<biome>` and the rest.
   The existing biomes are examples.
3. Give it a stone picture in `reshade/sky/`. It must be your own work, never
   art taken from the game.

**Also open:**

- **Torches on the ceiling are off** (`[ceiling] torches=0`). Finding the
  torches meant reading the game's heap, which cost too much CPU; the scan was
  narrowed, but it is still off by default. Wall torches in crypts are not
  found at all: they are stored differently.
- **A seam** where the top of a wall meets the vault.
- **Dark blocks** (8 px) near cut wall tops by a fire (`VoidGlow`).
- **Warm light shows through fog** and lights arches that should stay dark.
- **Frame cost** has not been measured.

**Code:**

- `reshade/D2R_DepthFog.fx`: `Ceiling`, `CeilUnder`, `DepthFogWorld`.
- vrcam: `LoadCeilingBiomes`.
- The settings app: the Ceiling group in `tools/settings/settings.cpp`.

### 4. The fog eats fire [repo]

**What you see:** torches, braziers and fire spells fade
into D2R VR's fog with distance, like everything else. A fire down a dark
corridor should still glow through the haze; instead, it goes dull and grey.

**What we know:**

- The fog in `reshade/D2R_DepthFog.fx` is applied to every pixel by its
  depth. A flame's pixels are fogged like the stone around them.
- On the cut wall tops under the ceiling, a pixel already keeps its own colour
  when it is bright flame (see `VoidGlow`). The fog does not have that test.
- Softer thresholds and blurring the glow were tried for the void glow, and
  were worse: the outlines of the wall caps showed.

**Ideas:**

- Let bright, warm pixels (flame) through the fog partly, by their brightness
  and hue.
- Or take the game's own emissive or bloom target as the mask of what glows.
- Keep the game's halo round a flame as well, not only the flame itself.

**Where to start:** the fog in `DepthFogWorld` and the `[fog]` section of
`d2r_vr.ini`.

### 5. Some dungeons need vertical walls up to the ceiling (the Act 2 sewers) [repo]

**What you see:** in dungeons such as the Act 2 sewers, the game's walls are
low. They are built for the camera from above, and their tops are cut off.
From first person, under a ceiling, you see the gap between the top of a wall
and the vault instead of a wall that goes up to it.

**What we know:**

- Today the gap is covered by tricks:
  - whatever stands higher than the ceiling is cut, and a glow of the void is
    drawn there;
  - a ring of the same stone stands at `[ceiling] wall_distance` round the
    hero;
  - there is stone under the floor's edge (`floor_depth`).
- None of this makes the real walls taller, and the seam where a wall meets
  the vault shows.

**Open question:** how to raise a wall to the ceiling. Ideas:

- **In the shader:** find wall pixels by the depth's normal (vertical
  surfaces), and continue each wall upward to the vault along the line where
  it meets the floor, as a ray-marched surface.
- **From the game's data:** the wall tiles of the current area give the wall
  lines in the world, and the shader could draw tall walls on them.
- **In the game's own draw:** stretch the wall models' height in their vertex
  data or their constant buffers, if a wall is a model that can be scaled.

**Where to start:** `Ceiling`, `CeilUnder` and the cut in `DepthFogWorld` in
`reshade/D2R_DepthFog.fx`; the `[ceiling]` keys in `d2r_vr.ini`.

### 6. Day and night need tuning [repo]

- The light levels that count as day and night (`light_day=128`,
  `light_night=65`) were measured once.
- Act 1's fog cannot get brighter by day.
- Some areas' skies have not been checked.

### 7. The interface cannot be brought nearer in stereo [repo]

The game draws its interface into the same picture as the world, with no
camera of its own. Moving it towards the player with a shader mask also moved
the ground under it (`[stereo] ui_near`).

So far, the toolbar and the map are handed to FlatVR as separate pictures
instead (the forearm toolbar and the map orb). The rest of the interface is
still on the screen.

---

## Stereo, frame rate and DLSS

### 8. DLSS in "Stereo - one pass, two pictures" uses one DLSS for both eyes [repo]

- In "Stereo - eyes by turns" each eye has its own DLSS. The code is
  `fx::dlsseyes`, which hooks the Create, Evaluate and Release calls in
  `_nvngx.dll`.
- In the one-pass mode, the second DLSS for the right eye is run with D3D12
  predication (`[stereo] replay_dlss=1`). This hangs the GPU, so it is off and
  both eyes share one DLSS.

### 9. DLSS jitter is split between the eyes [repo]

- The game moves its picture by a sub-pixel jitter that changes every frame
  (Halton 2). Frames alternate between the eyes, so the left eye only ever
  gets jitter to one side and the right eye only to the other. Each eye's DLSS
  sees half of the pattern, which may make the picture softer.
- **Idea:** advance the game's jitter index once per stereo pair, not once per
  eye.
- **Also not handled** in `vr/dlss_mv.cpp` (the stereo fix for the motion
  vectors):
  - the camera moving while you walk, in the far part of the picture, which
    has no real depth;
  - objects moving along the line between the eyes.

### 10. "Stereo - one pass" hangs: the cure works, the cause is unknown [repo]

- Without a fix, the GPU stopped after 20-70 seconds: the game's draw thread
  waited forever for its own frame fence.
- **The cure, found by trying:** a GPU signal after every replayed command
  list. Nobody knows yet why it works. Understanding it may also unlock issue
  8.
- **Also open:**
  - Copying the left eye's depth (`[stereo] replay_depth`) is broken: one
    resource state lost the shadows, another removed the device.
  - The right eye still redoes work it could take from the left one (the
    shadow maps, parts of the render graph).

**Code:** `vr/vrcam.cpp`, namespaces `camfix` and `replay`, and
`HookDrawGameScreen`.

### 11. The picture judders when the GPU cannot keep up with the headset [repo + BodyWalk/FlatVR]

- Since 0.153 the game's frames are paced to the headset's refresh rate. When
  the graphics card cannot hold that rate, the picture judders.
- **For players:** lower the headset rate or the resolution.
- **The real limit is one CPU thread:** in real stereo everything waits on the
  game's draw thread, about 5-6 ms of CPU for each eye's pass. Inside the
  game's renderer, about a third of that time is the job system spinning, and
  about a tenth is driver locks when constant buffers are bound.
- **Planned in FlatVR [BodyWalk/FlatVR]:** a "frame conveyor" - FlatVR shows
  each frame at the head pose it was drawn for, so a slow frame does not
  judder.

### 12. The pixel frame stamp is lost after ReShade reloads the effect [repo + BodyWalk/FlatVR]

*Status unclear.*

- When something makes ReShade compile `D2R_DepthFog.fx` again, FlatVR may
  fall back to the frame stamps in shared memory. Those are one pair off,
  which shows as judder in head turns.
- **Start at:** `PS_Stamp` and `StampWanted`.

### 13. Resizing the window freezes the game for ~7 s each time [repo + BodyWalk/FlatVR]

- ReShade compiles the effect again on every resize, and each compile takes
  about 7 seconds.
- When the VR runtime dropped, three resizes in a row froze the game for about
  20 seconds.
- A shader that compiles faster would help - for example, fewer of the
  ceiling's ray steps that the compiler unrolls.

---

## First person (F4)

### 14. Weapons are not calibrated [repo]

- How a weapon sits in the hands and where it aims is tuned for a few weapons
  only. Weapon Adjust in D2R VR Settings fixes one by hand; the defaults for
  all 17 weapon kinds still need doing.
- Which hand holds a two-handed weapon is a fixed table
  (`[hands] spear_hand`, `sword_hand`, `axe_hand`). Spears have never been
  tested.
- The crossbow's left-hand pose needs a default.

### 15. The whole body sometimes turns against the controllers [repo]

*Status unclear.*

Rarely, the whole body turned away from where the controllers are, while the
facing in the log stayed smooth. Taking the facing from the skeleton
(`[hands] facing_source=1`, since 0.135) may have cured it. We need a
reproduction.

### 16. Gestures for skills (Zeal and others) [repo + BodyWalk/FlatVR]

Swinging a weapon attacks, but there is no gesture for a particular skill.
See [ROADMAP.md](ROADMAP.md).

---

## Settings app and smaller things

### 17. "Monster name plate opacity" does nothing, so it is hidden [repo]

- `[hud] monster_alpha` fades the rectangle of the `HUDMonsterHealth` panel,
  but that panel is 0 x 0. The plate is its child widget "Health" (layout
  `HUDMonsterHealthHD.json`).
- **Fix:** take the child's rectangle (find the widget "Health" under the
  panel) and scale it from the layout to the screen.
- Then show the sliders again: they are wrapped in `Hide(...)` in
  `tools/settings/settings.cpp`.

### 18. The Update button falls back to the general mods page [repo]

*Good first issue.* `kModsPage` in `tools/settings/settings.cpp` points at the
general mods page instead of the D2R VR page.

### 19. Field offsets are not covered by the signature scanner [repo]

- The scanner (`cleanroom/sigscan`, the Status tab, the Scan button) finds the
  game code the mod hooks, so a game update moves nothing there.
- Field offsets inside the game's structures are still fixed numbers:
  - `game_layout.h`;
  - the skeleton and ECS offsets in `vr/skeletons.cpp`;
  - the controller manager.
- Only D2RLoader 1.3.1's image has been checked.

### 20. Small things to check

We have seen each of these once and not since:

- **Top-down stereo hover:** the mouse ray is built from the current eye's
  view, so the hover may flicker off the hero's plane.
- **Twice-drawn frames run the gamepad's targeting code twice:** drawing the
  game screen twice per tick (stereo pairs) also runs that code twice.
- **A brown rectangle under a torch's halo** over the void.
- **Grass and clutter** are placed around the game's original camera from
  above.

---

## In BodyWalk / FlatVR (not in this repo)

Report these on Discord with your BodyWalk log.

- **The forearm toolbar's rotation sliders are probably in the wrong units:**
  degrees passed as radians.
- **FlatVR's Head Lock screen can stay tilted after a menu.**
