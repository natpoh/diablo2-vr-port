# Known issues

What is broken or unfinished right now (D2R VR 0.182), what we already know
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

### 1. A haze band across the screen in dungeons [repo]

**What you see:** a band across half the screen in dungeons, from first
person. It is not D2R VR's fog. It is also not the game's fog density or
volumetric fog: changing those does nothing to it.

**Guess:** it is the edge of the hero's light radius. The game darkens
everything beyond the light circle, and from first person that edge reads as
a straight line on the floor. Check this without writing to the game's memory.

**Likely the same as the Act 2 town's veil, fixed in 0.164 (2026-10-13):** the
2026-10-08 frame (a dungeon) already had all its linear depth at 0. Check with
`[render] depth_fix=1`.

### 2. Cave ceilings are only in some dungeons [repo]

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

### 3. The fog eats fire [repo]

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

**A test since 0.170** (off): `[fog] spare_effects` (World > Fog > *Fog spares
fire, spells and smoke*). The game's transparent layers of each frame are copied at
its transparent resolve, and the fog covers only the share of each pixel that is
the scene behind them. VR F4; read at the game's start. Not on by default yet.

**Where to start:** the fog in `DepthFogWorld`, `vr/fog_spare.cpp` and the `[fog]`
section of `d2r_vr.ini`.

### 4. Some dungeons need vertical walls up to the ceiling (the Act 2 sewers) [repo]

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

### 5. Day and night need tuning [repo]

- The light levels that count as day and night (`light_day=128`,
  `light_night=65`) were measured once.
- Act 1's fog cannot get brighter by day.
- Some areas' skies have not been checked.

### 6. The interface cannot be brought nearer in stereo [repo]

The game draws its interface into the same picture as the world, with no
camera of its own. Moving it towards the player with a shader mask also moved
the ground under it (`[stereo] ui_near`).

So far, the toolbar and the map are handed to FlatVR as separate pictures
instead (the forearm toolbar and the map orb). The rest of the interface is
still on the screen.

---

## Stereo, frame rate and DLSS

### 7. DLSS in "Stereo - one pass, two pictures" uses one DLSS for both eyes [repo]

- In "Stereo - eyes by turns" each eye has its own DLSS. The code is
  `fx::dlsseyes`, which hooks the Create, Evaluate and Release calls in
  `_nvngx.dll`.
- In the one-pass mode, the second DLSS for the right eye is run with D3D12
  predication (`[stereo] replay_dlss=1`). This hangs the GPU, so it is off and
  both eyes share one DLSS.

### 8. DLSS jitter is split between the eyes [repo]

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

### 9. "Stereo - one pass" hangs: the cure works, the cause is unknown [repo]

- Without a fix, the GPU stopped after 20-70 seconds: the game's draw thread
  waited forever for its own frame fence.
- **The cure, found by trying:** a GPU signal after every replayed command
  list. Nobody knows yet why it works. Understanding it may also unlock issue
  8.
- **Since 0.168** the replayed lists go in the game's own submit calls, with no
  signals of ours but the queue switches' (`[debug] replay_marks=3`, the shipped
  value). `replay_marks=1` (a signal after each list) is the safe fallback. All the
  lists in one batch with no signals (`replay_marks=0`) still hangs.
- **Also open:**
  - Copying the left eye's depth (`[stereo] replay_depth`) is broken: one
    resource state lost the shadows, another removed the device.
  - The right eye still redoes work it could take from the left one (the
    shadow maps, parts of the render graph).

**Code:** `vr/vrcam.cpp`, namespaces `camfix` and `replay`, and
`HookDrawGameScreen`.

### 10. The picture judders when the GPU cannot keep up with the headset [repo + BodyWalk/FlatVR]

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

### 11. The pixel frame stamp is lost after ReShade reloads the effect [repo + BodyWalk/FlatVR]

*Status unclear.*

- When something makes ReShade compile `D2R_DepthFog.fx` again, FlatVR may
  fall back to the frame stamps in shared memory. Those are one pair off,
  which shows as judder in head turns.
- **Start at:** `PS_Stamp` and `StampWanted`.

### 12. Resizing the window freezes the game for ~7 s each time [repo + BodyWalk/FlatVR]

- ReShade compiles the effect again on every resize, and each compile takes
  about 7 seconds.
- When the VR runtime dropped, three resizes in a row froze the game for about
  20 seconds.
- A shader that compiles faster would help - for example, fewer of the
  ceiling's ray steps that the compiler unrolls.

---

## First person (F4)

### 13. Weapons are not calibrated [repo]

- How a weapon sits in the hands and where it aims is tuned for a few weapons
  only. Weapon Adjust in D2R VR Settings fixes one by hand; the defaults for
  all 17 weapon kinds still need doing.
- Which hand holds a two-handed weapon is a fixed table
  (`[hands] spear_hand`, `sword_hand`, `axe_hand`). Spears have never been
  tested.
- The crossbow's left-hand pose needs a default.
- **The blades for hit by touch come from the weapons' own models.** The two ends
  of every weapon's capsule (and the side its point is on) are the bounds of the
  game's own weapon model, one pair per base item (`vr/weapon_extents.h`), in the
  metres the weapon is drawn in. The spear, the pole-arms, the staves, the javelin
  and the rest were not measured one by one on screenshots: the javelin's point
  (model 1.057 m, screenshots 1.04 m) and the spear's head (2.1 - 2.4 m on the
  screenshot, 2.08 m by the model) agree. The Assassin's katars and claws lie along
  the weapon bone's Z (yellow in the debug), not its Y, and their capsule runs from
  the knuckles to the point; which way the Z runs is read from the game's own pose
  (against the forearm), for each hand. Not read from the models: the Hatchet
  Hands family (blades spread sideways, the kind's constants along Z) and the flails
  (their own chain logic), and the capsule's thickness is still per kind. The javelin's
  capsule may lie a few centimetres to one side of the spearhead (the weapon
  bone's Y axis is about 3 - 4 degrees off the model's long axis; not corrected).
  Tune the blades with the sliders on the Debug tab while "Draw the capsules of
  Hit by touch" is on.

### 14. The whole body sometimes turns against the controllers [repo]

*Status unclear.*

Rarely, the whole body turned away from where the controllers are, while the
facing in the log stayed smooth. Taking the facing from the skeleton
(`[hands] facing_source=1`, since 0.135) may have cured it. We need a
reproduction.

### 15. Gestures for skills (Zeal and others) [repo + BodyWalk/FlatVR]

Hit by touch, the throw, the bow's draw and the swap over the shoulder are
gestures for the game's attack, throw and weapon swap, but there is no gesture
for a particular skill (Zeal, say). The skill panel casts a skill from a slot,
not from a movement. Two touches cast a skill: the shield bash casts Smite, and
since 0.170 *Double Swing by the two hands* (`[hands] double_swing_by_hands`, off,
not yet tried in the game) makes the other hand's touch on the same monster the
game's Double Swing or Frenzy. See [ROADMAP.md](ROADMAP.md).

### 16. The new controls and options are mostly for first person (F4) [repo]

*By design, and open to change.* Everything added after the base mod - Controls
without BodyWalk, hit by touch, weapon weight, the throws, the bow's draw, the
skill panel, the wrist medallions, the guards that keep attacks from walking
the hero - acts in F4 only, so F1, F2, F3 and the flat modes play as they always
did. The price: in F1 - F3 the controllers are BodyWalk's. Since 0.166 the
controller's left stick walks the hero there and the left trigger with a flick of a
thumbstick drinks a belt potion (both without BodyWalk running); since 0.167 the
plugin's own controller is the game's gamepad in F1 - F3 too, without first person's
skill panel, settings panel and gestures (BodyWalk running: its gamepad whole).
The gate is `vr/view_gate.h` (`viewgate::F4()`).

### 17. Hit by touch and weapon weight need tuning [repo]

- The least blade speed that hits (`[gameplay] melee_min_speed`): the plugin's
  own default is 0.8 m/s, which counts nearly every movement (in a measured
  session a swing's tip read about 2 - 6 m/s and a poke about 1 m/s). The
  settings that come with the mod have 6 m/s and a 15 cm start
  (`melee_stroke_start_cm`) since 0.166; the slider goes up to 20 m/s since
  0.170. Tell us what works for you. The
  blade turns green while a contact would count; the marks in a swing
  (`[debug] melee_boxes`) show where.
- The feel of weapon weight (`[weight]`: a sword sags about 4 degrees held out
  at the defaults) has been tuned on paper and on a bench; Gravity % and
  Hands' strength are the first knobs. The pop when a second hand takes a
  weapon that has sagged is not looked at.

### 18. A click on bare ground carries Stand Still, whatever skill is selected [repo]

With "Attacks never walk the hero" (on) every click on bare ground in F4 gets the
game's Stand Still, so it is an attack or a cast in place. The plugin does not
read the mouse skill, so with Teleport, Leap, Charge or Whirlwind selected the
click carries Stand Still too. Stand Still should only stop the walk-up fallback
and not the cast, but it has not been checked for each of these: if a skill that
moves the hero stops working on a mouse click, untick the option and tell us.
(The pad's own presses of those skills are left alone.)

### 24. The crossbow's new hold and the hand-over are off, and do not work yet [repo]

**What you see:** in the author's test of 2026-10-18 the crossbow's grip in the hand is
still off (the hand does not sit on the crossbow right), and passing the crossbow from
hand to hand does not work.

**Where it stands:** the code of a new hold is in the 0.170 build but **off** in the
settings that come with it: the barrel along the controller (`[hands] xbow_barrel_forward`),
the left hand on the game's own fore-grip and closed on the stock (`xbow_support_game`,
`xbow_support_close`) and the hand-over (`xbow_handover`: take it with the free hand at
the fore-grip and let go with the other, or a new squeeze of the free hand's grip within
25 cm of the holding palm). 0.170 ships the old crossbow settings (`xbow_hold_grip` 1,
`xbow_aim_barrel` 1, `xbow_line_yaw` -46, `xbow_left_at_cm` 5, `xbow_left_x/y/z`
-5.5 / 2.5 / -1.5). Note that `xbow_hold_grip` (on) now puts the holding palm on the
game's *trigger-hand* palm (the roles were crossed before), which may be what is off.

**To fix, later:** `vr/skeletons.cpp` (`PoseBody`: the hold; `XbowHolderStep`: the
hand-over) and `vr/xbow_grip.h` (the pure parts, `cleanroom/tests/xbow_grip_test.cpp`).
The log says what happened: "crossbow holding hand: ...", "crossbow hand-over: ...",
"crossbow laid by its own barrel ..." and, for the stow behind the shoulder, "ranged
holster - the right hand's grip at ... outside the shoulder box".

### 25. The hero's own effects: "Fade" is set, but nothing changes on screen [repo] - cause found, fix to check in the game

**What you see:** with *Draw the hero's own effects fainter* ticked, Bone Armor still wraps round
the eyes as before. In the author's test (2026-10-18) the log shows the whole chain to its end - the
string object at the global reads "Fade", and "bonearmorfront" (entity ...) - body group - "Fade"
set to 30 % on the effect (VfxSetFloat 0xF6A380) - and yet the picture does not change.

**Where it stands:** in the 0.170 build, but **off** in the settings that come with it
(`[render] dim_own_effects` 0; `dim_own_effects_pct` and `dim_own_auras_pct` are the percentages).
The first version took the effect's playback speed for its opacity (the bones slowed down, nothing
faded); the second sets the effect's named float parameter "Fade" the way the game's own per-frame
unit update does, and that call is accepted.

**Why (read 2026-10-24 from the game's HD files):** Bone Armor's effect
(`vfx_BoneArmor_Front.particles`) draws the bones as an **opaque lit mesh**
(`Diablo_Default_Mesh` material) and a thin ribbon trail. Only the trail binds the "Fade"
parameter (`MonsterFade.Fade`); the mesh binds nothing that fades, so no value of "Fade" can
make the bones fainter. The game itself never fades them either: it switches the whole effect
off when its unit's alpha is 0.

**The fix (branch `claude/dim-own-effects-2`, not yet seen in the game):** the game's HD particle
files now mark which of the table's effects have such a solid part (`tools/gen_own_effects.py
--game`): Bone Armor's front, Blade Shield, Energy Shield, the shrines' overlays, Holy Freeze's
ice burst. In F4 those are **not drawn** whenever the slider asks for anything under 100 % (the
game's own switch, VfxSetVisible 0xF6A830, the one it uses at alpha 0). They are switched off
again if the game switches them back on. The rest (Frozen / Shiver / Chilling Armor, the aura
rings ...) are still faded with "Fade". The log says per effect which was done, and whether the
effect takes the switch. Cyclone Armor's HD overlay is empty, so there is nothing to fade or hide.
**The fourth test (2026-10-24, 30333e5e): the bones were still there.** The log showed the switch
taken by all three "bonearmorfront" effects. But the bones are not drawn by the effect: its script
makes them through the game's `CreateAnimatedModelInstance` callback as plain **ModelInstances** of
the renderer, kept in the effect's object effects, which the effect's switch never reaches. Now
those models are hidden too, the way the game hides an overlay's model at alpha 0
(`ModelInstance::SetVisibleLayers` 0xF3F840, bit 31 = the whole model), only the ones at the
hero's place and only while the effect is not to be drawn; they are given back exactly.
**The fifth test (b2f6f070): still there** - the log said the models were NOT found from the effect:
the object the game's callback walks is the PopcornFX medium's descriptor, not the D2R effect's
`+0x40`. Since then the models are found where the renderer keeps every model it made (its list at
`+0x2EFB8`, read under the game's own lock on it): those whose model file is a solid effect's
(`bonearmor.model`, from the game's files), only under `data/hd/vfx/`, only at the hero's place.
The log line "the renderer's models: ..." says how many there are, how many are Bone Armor's, how
many stand at the hero, and how many were hidden.
**The sixth test (7a7cefe3): not run at all** - the four new game functions were fixed addresses
of 3.3.93787 and the player's game is 3.3.94995 ("the game's model functions ... are not as in
3.3.93787"). They are signatures now like the rest (`ModelSetLayers`, `GetRenderer`,
`ModelListLock` / `ModelListUnlock` in game_sigs.h; the lock pair found at the model factory's own
calls, whose pattern holds the list's offset), and the log says once where each was found.
**The seventh test (a46f1787): found, but no Bone Armor model matched.** Read in the running game
(read only): the list holds `.../bonearmor/bonearmor_lod0.model` x3 - the loaded file carries its
level of detail, so the key `bonearmor` never matched - and these models keep no place (the effect
places them per particle), so "at the hero's place" never held. Now `_lod<n>` is not part of the
key, and a solid effect's model is the hero's when it first appears within 3 s after his overlay of
that effect was made (or is there at the first look while his overlay is live); one made at
another time (another player's Bone Armor) is left alone.
To check: a Necromancer with Bone Armor, the option ticked - the bones are gone in F4 and are
there in F1 - F3 and to other players. Start in `vr/own_effects.cpp` (`Tick`); the log lines
are "own effects: ..."; the notes are in `docs/own_effects_recon.md`.

### 26. A staff in both hands: the spell's ray started at the wrist and went to the side [repo] - FIXED in 0.171

**What it was:** with a staff (or another weapon the spell is aimed along) held in both hands, the ray of
the spell started at the wrist instead of the palm and pointed off to the side instead of along the staff.
A regression from the 0.170 grip changes: the two-handed hold took the line between the two controllers, and
each grip sits a fist's width from its controller, so with the hands near each other that line ran far off
the drawn staff.

**What was fixed (0.171):** the aim now follows the staff as the weight model draws it between the grips, butt
to tip (`weight::ShaftEnds`), and starts at its tip; with no drawn shaft lately, or one under 5 cm, the carrying
hand's grip aims as before. One hand on the staff is unchanged. `vr/spell_aim_model.h`, `vr/vrcam.cpp`
(`SpellModelAim`), `spell_aim_model_test` (43 checks). To check in the game.

### 27. The Kurast sewer stairs: the sky shows at the far end of the stairwell [repo]

**What you see:** going down the sewer stairs in Kurast Bazaar or Upper Kurast in VR first person, the far, open end
of the stairwell shows the sky (a flat grey-blue) instead of darkness. The model has no wall there: the flat game's
camera never looks into it.

**What we know:** bearstonem/d2r-pcvr closes it with a black backdrop it generates at install time from the
player's own game archives; that part was not ported in 0.172 (the walls and stairs drawn two-sided were:
`vr/roof_sides.h`, [render] roofs_two_sided). Also not ported: a VR "ENTER SEWERS" prompt.

---

## Settings app and smaller things

### 19. "Monster name plate opacity" does nothing, so it is hidden [repo] - DONE 2026-10-12, to check in the game

*Now:* the opacity slider is shown on UI: F4 body > Monster plates; on the UI: F1,
F2 and F3 tabs it is still hidden.

- `[hud] monster_alpha` fades the rectangle of the `HUDMonsterHealth` panel,
  but that panel is 0 x 0. The plate is its child widget "Health" (layout
  `HUDMonsterHealthHD.json`).
- **Fix:** take the child's rectangle (find the widget "Health" under the
  panel) and scale it from the layout to the screen.
- Then show the sliders again: they are wrapped in `Hide(...)` in
  `tools/settings/settings.cpp`.

### 20. The Update button falls back to the general mods page [repo]

*Good first issue.* `kModsPage` in `tools/settings/settings.cpp` points at the
general mods page instead of the D2R VR page.

### 21. Field offsets are not covered by the signature scanner [repo]

- The scanner (`cleanroom/sigscan`, the Status tab, the Scan button) finds the
  game code the mod hooks, so a game update moves nothing there.
- Field offsets inside the game's structures are still fixed numbers:
  - `game_layout.h`;
  - the skeleton and ECS offsets in `vr/skeletons.cpp`;
  - the controller manager.
- Only D2RLoader 1.3.1's image has been checked.

### 22. Small things to check

We have seen each of these once and not since:

- **Top-down stereo hover:** the mouse ray is built from the current eye's
  view, so the hover may flicker off the hero's plane.
- **Twice-drawn frames run the gamepad's targeting code twice:** drawing the
  game screen twice per tick (stereo pairs) also runs that code twice.
- **A brown rectangle under a torch's halo** over the void.
- **Two potions fly with Weapon Weight on.** A thrown potion's model flies to
  where it lands (on) and the game's own flying potion is drawn beside it. Hiding
  the game's one (`[hands] potion_hide_game`, experimental) is off in the program
  but on in the settings that come with the mod since 0.166 (done with the game's
  own switch for its effect); not proven in the game yet.
- **The skill tiles' RT slots** (RT and LT + RT) are shown and editable, but
  cast only with *Skill panel: skills on the right trigger and on A* (0.170,
  `[openxr] skill_rt_a`; on in the shipped settings since 0.171). Without it no control of the layout is bound to
  RT.
- **The bow's arrow comes about 0.3 s after the release.** The game makes it at
  an action frame of its own attack animation (an Amazon's: frame 6 of 14). Quick
  release (experimental, `[bow] quick_release`) moves that frame earlier; it
  changes the game's timing, so it is off in the program, but on in the settings
  that come with the mod since 0.166.
- **Grass and clutter** are placed around the game's original camera from
  above.

### 23. The source does not build against the released BodyWalk SDK header [repo + BodyWalk/FlatVR]

*A developer-side item, found at the 0.165 release.*

**What fails.** The plugin draws the gesture runes BodyWalk scores
(`[openxr] runes_draw`). That code - `vr/rune_geom.h`, `vr/link.h`, `vr/link.cpp` -
uses a section of BodyWalk's Game Link header, `bodywalk/game_link.h`, called
VrRunes: `BW_LinkVrRune`, `BW_LinkVrRunes`, `BW_LINK_VRRUNES_*`,
`BW_LINK_RUNE_*` and `BW_LINK_CAP_VR_RUNES`. The header in `external/bodywalk/`
(from BodyWalk's released plugin SDK) does not have the section, so the build stops
with "undeclared identifier" errors in `vr/link.h` and `vr/rune_geom.h`. The
installer's DLL is built with a header that has it.

**Workaround meanwhile.** Build against a `game_link.h` that has the section: put
it in a folder as `bodywalk/game_link.h` and run
`cmake -S . -B build -A x64 -DBODYWALK_SDK_INCLUDE=<that folder>`. The section only
adds to the header; nothing else in it changes, so BodyWalk 1.80 and the shipped
Game Link plugin work with a plugin built this way.

**To fix:** BodyWalk must publish the VrRunes section in its Game Link SDK header;
then drop the workaround (this note, the README's and the CHANGELOG line) and the
tree builds as it is. The runes themselves also need a BodyWalk that writes the
block, which is not out yet.

---

## In BodyWalk / FlatVR (not in this repo)

Report these on Discord with your BodyWalk log.

- **The forearm toolbar's rotation sliders are probably in the wrong units:**
  degrees passed as radians.
- **FlatVR's Head Lock screen can stay tilted after a menu.**
