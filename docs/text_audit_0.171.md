# Text audit for 0.171

Every tooltip and label in `tools/settings/settings.cpp` and every key comment in
`d2r_vr.ini` was compared with what the plugin does: the default when a key is
missing, the range it is clamped to, which views it works in (F4 only or not),
whether it is live or read at the start, and whether the feature is wired at all.
The docs (`README.md`, `SETTINGS.md`, `docs/*.md`) were checked against the code and
CHANGELOG 0.165 - 0.170 at the same time.

How to read it: *program default* is the plugin's value when the key is missing
from the file. *Shipped* is the value in the `d2r_vr.ini` that comes with the mod
(the author's tuned settings). When the two differ that is normal, and only a text
that states the wrong one was changed. The evidence is `file:line` as of the branch.

## Changed in tools/settings/settings.cpp

| Where | Key | Was | Now | Evidence |
|---|---|---|---|---|
| Body > Effects around the hero | `[render] dim_own_effects` | label "Draw the hero's own effects fainter"; tip "Not yet seen working ... so it is off" | label ends "(not working yet)"; tip starts "NOT WORKING YET (0.170): the log shows the effect's "Fade" being set, but nothing changes on screen" | CHANGELOG 0.170; docs/KNOWN_ISSUES.md 25; vrcam.cpp:1471 (default off) |
| Performance > Quality | `@preset` | Low "no ceiling"; "Low, Medium and High keep real stereo" | Low has the ceiling with 8 relief steps (only Potato has none); High's fog 30 - 250; every preset also sets *3D in the headset* and so switches Native OpenXR off; with Native OpenXR on the preset shows Custom | settings.cpp preset table (`kPreset`), `WriteStereo3D` (writes `[openxr] on=0`), `ReadStereo3D` |
| Camera | `[camera] pitch` | shown default -45 | -60, the plugin's default (the tip already said -60 is level) | vrcam.cpp:1467 |
| Camera | `[camera] height` | shown default 6.5 | 8, the plugin's default | vrcam.cpp:1462 |
| Stereo | `[stereo] ipd` | shown default 0.2 | 0.27, the plugin's default (the tip already quoted 0.27) | vrcam.cpp:1732 |
| Controls | `[input] mouse_look` | "Flat and VR view F3 always start with it" (the view numbers from before 2026-10-05) | flat F2 and F3; VR F2 with the switch below; never VR F3 (the floor) or F1 in 3D | vrcam.cpp `MouseLookForView`, `Overhead` |
| UI: F4 body | `[hud] map_place` | "Where FlatVR hangs the corner map" | FlatVR hangs it, native OpenXR hangs it itself | xr.cpp `HangHud` |
| Ceiling | `[ceiling] enabled` | "for now the caves of act 1" | act 1's caves, crypts, barracks, catacombs and cathedral, act 2's sewers and palace | vrcam.cpp:718 (default `biomes`), shipped ini; KNOWN_ISSUES 2 |
| Ceiling, 7 areas | `[ceiling] texture_<area>` | "Empty (x) = D2R_Ceiling_<area>.png, else the caves' stone" | Empty = the common picture (`[ceiling] texture`, the act 1 caves' stone); missing too, the shader's own stone. (The area's own file name is the default only while the key is not in the file at all.) | vrcam.cpp:921-933; the x button writes an empty value |
| Strikes | `[gameplay] melee_touch_thrown` | "("Hands": throw by the grip ...)" - no such tab | Weapon Adjust > Throws | settings.cpp, group Throws |
| Strikes | `[hands] double_swing_by_hands` | - | adds "Not tried in the game yet." | CHANGELOG 0.170 |
| Weapon Weight | `[weight] thrown_model` | "(Javelin tab)" | (Weapon Adjust > Throws) | no Javelin tab |
| Weapon Weight | `[weight] throw_reach_pct` | "the javelin tab's throws" | the throws of Weapon Adjust > Throws | as above |
| Weapon Adjust | `[hands] spell_aim_model` | "aim correction sliders (Bow tab)" | (Weapon Adjust) | no Bow tab; `aim_yaw`, `xbow_aim_yaw`, `staff_aim_yaw` are on Weapon Adjust |
| Weapon Adjust / Strikes | `[bow] quick_release`, `[hands] quick_throw`, `[hands] quick_strike` | "A CHANGE OF THE GAME'S TIMING, off by default." | "(off when the key is missing; on in the settings that come with the mod)" | shipped ini: quick_release=1, quick_throw=1, quick_strike=1 |
| Weapon Adjust | `[hands] potion_hide_game` | "EXPERIMENTAL, off by default" | "EXPERIMENTAL (off when the key is missing; on in the settings that come with the mod)" | shipped ini: potion_hide_game=1 |
| Body (new) | `[onboarding] show`, `[onboarding] done` | - | the first-start guide's two ticks (see below) | vr/first_start.h |

## Changed in d2r_vr.ini (comments only, no value changed)

| Where | Was | Now | Evidence |
|---|---|---|---|
| line 1 | "put next to d2rl-vrcam.dll and d2rl-renderdistance.dll" | next to d2rl-vrcam.dll (`mods\D2R-VR\d2rloader\plugins`); there is no renderdistance plugin, `[render] rings` and `model_radius` are vrcam's | docs/d2rloader_mod.md; vrcam.cpp:7399 |
| line 2 | "vrcam re-reads [camera] [head] [stick] the moment you save" | the whole file within half a second of a save; a key read at the start waits for one | vrcam.cpp `ReloadIfChanged` (twice a second) |
| line 3 | `background_full_speed` described as if read there | NOT READ HERE (above any section): vrcam reads `[render] background_full_speed`, missing = 1 | vrcam.cpp:1744 |
| line 5 | a stray text line without `;` | made a comment | - |
| `[hands] bow_draw_nock` | "The touch radius round it is 28 cm either way" | `bow_draw_reach_cm` either way (28 when missing) | vrcam.cpp:2054; bow_draw_wire.cpp:527 |
| `[hands] double_swing_by_hands` | - | "(0.170: not yet tried in the game)" | CHANGELOG 0.170 |
| `[render] build_rate` | "rooms built per pass at most" | NOT READ by this version (a key of the old renderdistance plugin) | no reader in the tree |
| `[render] depth_fix` | - | read only while `depth_fix_mode` is not in the file | vrcam.cpp:1908 |
| `[render] dim_own_effects` | - | starts "NOT WORKING YET (0.170): in the build, off - Fade is set on the effect, nothing changes on screen" | KNOWN_ISSUES 25; vrcam.cpp:1471 |
| `[debug] low_hp_inner`, `low_hp_roundness` | "Live" | old keys: `low_hp_inner` is not read; `low_hp_roundness` only while `[hud] low_hp_roundness` is missing | vrcam.cpp:1489 |
| `[table]` | "VR F4, the game on the floor (F4, F11)" | VR F3 (F3, F11) | vrcam.cpp:2704 (`TableView` is view 3) |
| `[gameplay]` header | "Every key is off until ticked in D2R VR Settings" | `melee_touch`, `shield_smite`, `melee_direct` are off until ticked; a key marked "1 (default)" is on when missing | vrcam.cpp:2174 - 2208 |
| `[openxr] headset_settings` | "Two more pages: Video ... Body"; "flips ... View \| Options \| Video \| Body" | the eleven pages; the Body page's *First-start guide* button | vr/hset_defs.h `kPageNames` |
| `[onboarding]` (new) | - | the first-start guide's section: `show=1`, `done=0` | vr/first_start_wire.h |

## Changed in the docs

- **README.md** (its changed text is also in [README_public_text.md](README_public_text.md)):
  the game's Ambient Occlusion (any quality, Max included - see below); the
  controls rewritten from the code: the F4 layout with long presses and grip
  chords, a menu, rebinding, Hit by touch, the skill panel and RT / A slots,
  the settings in the headset and its eleven pages, the belt, weapons behind the
  shoulders, the shield, the bow's quiver and orbs, the crossbow, throws and the
  javelin quiver, F1 - F3 with and without BodyWalk, the F keys; a new *First
  start in the headset* section; Requirements (BodyWalk 1.79, 1.83 for Game
  Link direct; native OpenXR needs none); Settings (the name D2R VR Authentic
  Settings, the START button).
- **SETTINGS.md**: new Performance and Status sections; the headset panel's
  eleven pages; the panel's tick is in UI: F4 body > Menus (it said Stereo >
  Native OpenXR); `dim_own_effects` not working yet, with both sets of
  percentages (program 30 / 50, shipped 50 / 60); the bow's quiver, orbs and
  arrow effects; the javelin quiver; "Bow tab" (there is none) replaced by
  Weapon Adjust; `throw_trace`'s default; the first-start guide.
- **docs/KNOWN_ISSUES.md**: 0.165 -> 0.170; the fog-and-fire test of 0.170; the
  one-pass hang since 0.168 (`replay_marks=3`); Smite and Double Swing as skill
  gestures; Hit by touch speeds (program 0.8 m/s, shipped 6 m/s and 15 cm);
  the plate opacity slider; potion hide, quick release (shipped on) and the RT
  slots (cast with `skill_rt_a`); a missing doc named.
- **docs/ROADMAP.md**: the AO item (Max is fine, see below); weapon weight and
  Hit by touch on in the shipped settings since 0.166; aim distance by the arm,
  the F1 - F3 controller and room-scale walking marked done; dim own effects in
  the build but not working.
- **docs/WEAPON_WEIGHT.md**: where the tick is; `shoulder_draw` described as the
  ini has it (a dropped weapon lies in the world, a grip calls it back, a second
  later it is on the back, a grip behind a shoulder draws it); a link to a doc
  that is not in the tree taken out.
- **docs/DEVELOPER_NOTES.md**: the log and hang-file folders under `mods\D2R-VR`;
  ReShade.log with the player's own dxgi.dll ReShade.
- **docs/README.md**: the weapon weight row; rows for `d2rloader_mod.md` and
  `launcher_authentic.md`.
- **vr/xr.cpp** (a log line): "Switch it on again in ... Performance > 3D in the
  headset" -> Home > 3D in the headset (that choice is on the Home tab).

### The game's Ambient Occlusion: Medium no longer needed

The Medium advice came in with the first public README and ROADMAP ("Ground
darkening at grazing angles ... Today the fix is the game's AO set to Medium").
The game's AO pass reads its linear depth, which the game sets for its own camera
from above (550 - 1060 units): from our camera every pixel read as one plane.
Since 0.164 `[render] depth_fix=1` (shipped on, `depth_fix_mode=1`) gives the
game's fog, height fog, AO, reflections and water the depth measured from our
camera (d2r_vr.ini `[render] depth_fix`, CHANGELOG 0.164). Nothing in `vr/`,
`reshade/`, `shared/` or `tools/` reads or checks the AO quality (the Settings
program's game-video status checks only VSync and TAA). So the README now says
any quality, Max included. Not checked by us in the game at grazing angles.

## Found, not changed (for Serg to decide)

**Behaviour, not text:**
- **A Quality preset switches Native OpenXR off.** Every preset writes
  `@stereo3d source` (FlatVR stereo or 3D depth), and `WriteStereo3D` writes
  `[openxr] on=0` for anything but native. With the shipped Native OpenXR,
  picking Potato, Low, Medium or High turns it off. Only the tooltip says so now.
  The `[stereo] headset_rate` row is written for native OpenXR too.
- `[render] model_radius`: Settings shows 200 (two Items, Performance and World),
  the plugin's default is 1000 (vrcam.cpp:262, 1469), shipped 200. Left alone so
  the two Items agree.
- `[hud] monster_near_body` with the key missing falls back to `labels_near_body`
  (vrcam.cpp:1645), not to the 1.6 the Settings item shows.
- `[hud_floor] labels_alpha`: Settings falls back to `[hud] labels_alpha`, the
  plugin to 0.5 (vrcam.cpp:1622). Harmless (shipped 0.15).
- Shown defaults that are the shipped value, not the plugin's: `[head] yaw_sign`
  -1 / +1, `[stick] sign` -1 / +1, `[stick] exact` 0 / 1.
- `[fog] color_caves` missing -> #100D0B (vrcam.cpp:847), Settings shows "same as
  the act"; `[fog] color_act*` missing -> the plugin's table, Settings "from the sky".
- The per-area ceiling items' defaults are per area; for a missing key the plugin
  uses the common `[ceiling]` values (vrcam.cpp:730-743). Several ceiling file
  defaults differ from the plugin's `D2R_Ceiling_<area>.png`.

**Unsure, left as they are:**
- `[openxr] busy_guard` "needs a game restart", but it is re-read live
  (xr.cpp:3702) and checked at every call; the CHANGELOG also says restart.
- The Performance status "render distance ... when the game starts again": the
  rings and `model_radius` are re-applied on an ini change (vrcam.cpp:7402, 17029).
- `[openxr] ipd_fallback_mm` says 40 - 80, the code clamps 0 - 80 (xr.cpp:3707).
- `quick_release_frame`, `quick_throw_frame`, `quick_strike_frame` say 1 - 12, the
  code clamps 1 - 40 (anim_cut.cpp:211); 1 - 12 is the slider's range.
- `throw_potion_reach_pct` "48 cells (about 30 m)" against `throw_max_cells`'
  0.5 m a cell (about 24 m); metres per cell follow the hero's scale.
- KNOWN_ISSUES 26 (a staff in both hands aims from the wrist) is not mentioned in
  the `spell_aim_model` comments.

**Keys the plugin never reads** (left in the ini): `[camera] shadow_slope`;
`[render] build_rate` (now marked); `[hands] xbow_model_roll`, `xbow_model_pitch`,
`xbow_model_turn`, `potion_action_ms`; `[hud] bar_zoom`, `labels_tilt_body`;
`[debug] low_hp_inner` (now marked); `[touch] on`, `debug`; the act 1 cathedral's
and act 2 sewer's `vault_`, `bay_`, `rib_`, `pillars_`, `column_*`, `walls_`, `arch_`
keys (taken out of the code on 2026-10-07). Read only as a fallback the ini
overrides: `[hud] low_hp_strength`, `bar_width` / `bar_along` / `bar_lift` /
`bar_roll` / `bar_tip` / `bar_side` / `bar_split`.

**Docs named but not in the repository:** docs/direct_input.md, doll_binding.md,
flatvr_sync.md, plan_laser_menus.md, plan_melee_by_touch.md,
plan_native_openxr.md, skill_wheel.md, wrist_status.md, double_swing.md,
own_effects_recon.md, SHOULDER_DRAW.md (the last one's link removed from
WEAPON_WEIGHT.md).

**CHANGELOG** (history, not edited): 0.170 says "Bow tab > A spell's aim distance"
(it is Weapon Adjust > Aim distance by the arm); 0.166 says "Weapon Adjust > Melee"
for Quick strike (now on Strikes); one 0.170 line calls the hero's own effects
"switched on" in the shipped settings, but the ini has `dim_own_effects=0`. The
0.171 section notes the corrections.

## New in 0.171: the first-start guide's keys

| Key | Default | Settings | What |
|---|---|---|---|
| `[onboarding] show` | 1 | Body > *Show the first-start guide* | 1 = the guide comes up by itself while it is not done |
| `[onboarding] done` | 0 shipped; missing = 1 if a measurement is stored, else 0 | Body > *First-start guide seen* | written 1 by the game when the guide is finished or closed; untick to see it again |
