# What's new in D2R VR

## 0.156

- **Native OpenXR (prototype)**: the game shows itself in the headset - no
  FlatVR screen in between (D2R VR Settings > Performance > 3D in the headset
  > Native OpenXR; needs BodyWalk 1.78). Stereo from the very first moment in
  first person, the hero's body facing where you look from the start, your
  controllers through BodyWalk even when BodyWalk was started after the game,
  and BodyWalk's belt zones (gesture zones ticked "VR") as see-through balls
  in the headset. Bare hands (no controller) press nothing. A sharper picture
  by default: each eye keeps the game window's shape, black above and below
  (Stereo > Native OpenXR, "Sharper: the picture keeps the window's shape").
  Tip: switch Virtual Desktop's Synchronous Spacewarp off, or it may hold the
  game at 45 fps.
- **Launch Diablo II starts BodyWalk too**, and FlatVR once BodyWalk is up -
  no need to know to start them first.
- **BodyWalk 1.78 required**: FlatVR can leave out a late picture instead of
  flashing an old frame on head turns ("Leave out late pictures"), and
  BodyWalk follows the game's wishes while minimised. Setup puts BodyWalk
  Portable 1.78 beside the game when yours is older.
- **Medium** quality now draws models out to 200 (was 300): about 2 ms less
  CPU a frame for little that you would miss.

## 0.155

- **Collect logs for support** (D2R VR Settings > Home): one zip on your
  desktop with the logs, the settings, the versions and your graphics card -
  while the game runs it first records 10 seconds of how the frames are
  drawn. Post the zip in our Discord (#diablo-2-vr) when something is wrong.
- **D2R VR needs BodyWalk 1.77** (FlatVR's frame conveyor). Setup checks the
  BodyWalk you have: if it is older - Steam's is 1.74 for now - it stays as
  it is and Setup puts BodyWalk Portable 1.77 in the game's folder.
- **Start BodyWalk starts the newest BodyWalk** it finds, never one older than
  D2R VR needs; Home > Status says when the running BodyWalk is too old.
- FlatVR's add-on 2.19 with the picture's present moment is back (it goes
  with BodyWalk 1.77).
- **Cave ceilings for everyone**: their stone pictures now come with the
  installer - without them ReShade said "There were errors loading some
  effects" and the caves had no ceiling.
- Setup switches FlatVR's background and glow round the screen off (BodyWalk
  > FlatVR > Background & Glow).

## 0.154

- **The eyes stay in line in Stereo - one pass, two pictures** with the
  BodyWalk that comes with the installer (1.76): 0.153 brought a newer FlatVR
  add-on, and with BodyWalk 1.76 the eyes went out of line after about 10
  seconds. The add-on is the one from 0.152 again; the newer one comes back
  with BodyWalk 1.77.
- **Pipeline depth is 0 by default again** (D2R VR Settings > Stereo >
  Timing): with 1 (0.153) head turns lagged and the sky floated in mono and
  in eyes by turns.

## 0.153

- **A new 3D mode: Stereo - one pass, two pictures** (the default). The game
  draws each frame once and the right eye is drawn again on the graphics card
  from the same work, with its own camera: true 3D with the sky and the
  shadows right in both eyes, at nearly the mono frame rate (on the author's
  PC 127-159 stereo pairs a second, against 108 with two passes). D2R VR
  Settings > Performance > **3D in the headset** has four modes now:
  Stereo - one pass, two pictures; Stereo - eyes by turns; 3D from the depth
  (ReShade); Mono. If the new one is unstable on your PC, take Eyes by turns.
- **Frames paced to the headset**: the game's frames are held to the
  headset's refresh rate - a game faster or slower than the headset juddered
  in head turns. D2R VR Settings sets the game's frame cap to the headset
  rate (twice it for Eyes by turns).
- **Smoother head turns** in mono, 3D from the depth and Eyes by turns when
  the game and the headset run at different rates: Pipeline depth is 1 by
  default, and the sky follows the frame that is shown.
- The game's temporal anti-aliasing is set to FXAA when the game starts in
  real stereo - it mixed the two eyes into a ghost behind everything that
  moves ([stereo] keep_taa=1 keeps it).
- **No hitch every 10 seconds**: the log's thread count (0.152) stopped the
  game for 30-50 ms every 10 s; it no longer runs on the game's thread.
- Known limits: with DLSS, Stereo - one pass shares one DLSS between the two
  eyes (a DLSS for each eye still hangs the graphics card there - off). When
  the graphics card cannot keep the headset's rate, the picture judders -
  lower the headset rate or the resolution.
- **For developers - help is welcome**: the roadmap, the known problems (what
  we know about each one and where in the code to start) and notes for
  developers are in [docs/](docs/README.md) - fork the project, fix one and
  send a pull request. The weapon weight work for first person is published
  too: a model with tests and a Unity bench, not in the game yet
  ([docs/WEAPON_WEIGHT.md](docs/WEAPON_WEIGHT.md)).
- **Next version**: D2R VR moves to BodyWalk's Game Link standard (the open
  BodyWalk plugin SDK) instead of its own bridge to BodyWalk.

## 0.152

- **Smooth head turns again in tabletop and first person**: FlatVR's frame
  ring (new in 0.150) is now off by default - with it the picture juddered
  and doubled when you turned your head. Players who tried it: "a night and
  day difference". The switch stays in D2R VR Settings > Stereo > Timing
  (Newest finished picture); keep **Frame stamps** on - without them it is
  as bad as before.
- The log tells where a frame's time goes (presents a second, the game's
  busiest threads, and in real stereo each eye's pass).

## 0.151

- **Creating a character works in first person (F4)**: the Create New screen
  was taken for a game area - the heroes stood there without heads and the
  name field and buttons slid off. It is a menu again, with the game's own
  camera.
- **Two switches for judder in head turns** (tabletop and first person),
  D2R VR Settings > Stereo > Timing, both on by default and live:
  **Frame stamps** - FlatVR places each frame at the head pose it was drawn
  for; untick and the screen stays at the head as it is, like in the other
  views. **Newest finished picture (FlatVR frame ring)** - untick and the
  picture goes over to FlatVR the old way. If tabletop or
  first person doubles or judders when you turn your head, untick them one
  at a time and tell us which one helped.
- FlatVR's ReShade add-on 2.19 (the frame ring can be switched off from the
  game).

## 0.150

- **Much faster with DLSS in real stereo**: the motion fix for DLSS (0.145)
  also ran a heavy self-check every frame. It no longer does - 50 stereo
  pairs a second became 79 on the author's PC (100 -> 158 fps).

## 0.149

- **A desktop shortcut**: Setup can put "D2R VR" on the desktop (ticked by
  default) - D2R VR Settings starts BodyWalk, installs ReShade and D2RLoader
  and launches the game, so there is no need to open the game's folder.

## 0.148

- **Setup's "Open D2R VR Settings" works**: on the finish screen it failed with
  "CreateProcess failed; code 740 - the requested operation requires
  elevation" when Setup ran as administrator.

## 0.147

- **Launch the game from D2R VR Settings**: Home > **Launch Diablo II
  (D2RLoader)**, under Start BodyWalk - shown while D2RLoader is installed and
  the game is not running.

## 0.146

- **ReShade installs from D2R VR Settings now**: Home > Status > **Install
  ReShade** downloads it from reshade.me and sets it up for the game by
  itself - no renaming files. Setup no longer installs it: its ReShade step
  failed ("did not finish, code 1") when D2RLoader was not in the game's
  folder yet.

## 0.145

- **DLSS works with real stereo**: each eye gets a DLSS of its own, fed the
  motion against that same eye's last picture - no shaking standing still, no
  smear in head turns. The Status tab no longer asks to switch DLSS off.
- **Character screen**: the heroes keep their heads (the mod's VR camera stays
  out of the menus).
- **Performance tab**: Potato is Low with the 3D made from ReShade's depth
  (one picture a frame) instead of real stereo. New "Headset refresh rate"
  (72-120 Hz) sets the game's frame cap - the rate for one picture a frame,
  twice it for real stereo. Render distance, rate and cap need the game
  restarted (said under the 3D choice).
- **3D in the headset follows the mod**: FlatVR's 3D source (depth, stereo
  pair or none) is set by the mod whenever BodyWalk starts and whenever it
  changes; FlatVR's Head Lock comes back after such a switch. The mod's
  BodyWalk profile no longer forces the stereo pair on, and keeps the depth
  unflipped.
- **The toolbar is on your left forearm now** (first person, F4): orbs,
  belt and skills lie along the inside of the arm, cut in two - turn the palm
  up to read it. To have it on the body again: D2R VR Settings > **UI: F4
  body** > Toolbar in the headset > **Where: Low in front** (or Chest, low).
  The forearm and the body each keep sliders of their own.
- **New defaults from the author's own play**: cave ceilings on with the tuned cathedral and sewer
  vaults, near fog (30-200), the crosshair and the left hand's hold retuned,
  the inventory screen nearer (2.5 m).
- **The staff in the right hand works again**: the left hand takes it with
  the grip and slides along it, closes and opens with the grip while it is off
  it, and spells go along the staff - all of it had been off whenever "aim
  from the left hand to the right" was off (the default since 0.140).

## 0.144

- **The belt works again**: the potion zones at your waist (first person, F4)
  show in the headset and fire, and so do the sword, axe and throwing swings.
  The mod's BodyWalk profile had shipped without their recorded positions.
- **Every game control from BodyWalk**: 68 new actions "D2R key: ..." in
  BodyWalk's Mapping tab - skills F1-F16, potions 1-4, Alt, Run, Shift,
  character, inventory, map, mercenary, cube and the rest - pressed straight
  in the game, no gamepad and no key needed.
- **Third person (F2) like flat mode**: the mouse turns the camera, the pointer
  is a crosshair in the middle (on/off, size, and its depth looking ahead and
  looking down on the UI: F2 tab), a click goes
  there, W A S D walk where the camera looks; F9 frees the pointer for menus.
- **The game on the floor (F3)**: the corner map gets size, depth and place
  like the toolbar (UI: F3 tab), and the mouse pointer lies on the game's
  ground by itself.
- **Turning with the right stick in first person (F4) fixed**: the body
  turned not at all when some of the game's controller polls came without
  BodyWalk's pad and reset the stick to the middle.

## 0.143

- **Smoother in dungeons on weaker PCs.** The torch light on the ceiling is off
  for now: finding the torches read all of the game's memory every 1.5 seconds
  and cost about a third of a CPU core while a ceiling was drawn.
- **Ceilings in act 2**: the Lut Gholein sewers (grey-beige brick) and the
  palace harem (Moorish coffers). Backgrounds done: 25 - sky over 18 open-air
  area types, a ceiling in 7 dungeon types.
- **The cathedral** keeps its flat ceiling with one dome over the altar.
- **Third person in VR**: W A S D move the hero relative to the camera.

## 0.142

- **Ceilings in the cathedral and the catacombs**, anything the game draws above
  the ceiling cut cleanly, ceiling heights from the area's floor. Settings >
  Ceiling.

## 0.141

- **Sky fixed in several areas.** Tristram, Kurast (Lower Kurast, the Bazaar,
  Upper Kurast, the Causeway), Travincal, the River of Flame, the Chaos
  Sanctuary and Nihlathak's Temple now have their sky, like the other
  open-air areas.
- **The barbarian's left hand fixed.** In first person his second weapon sits
  properly in the left hand, and he can strike with it.
- **Daggers can be thrown** - a Throw action in the mod's BodyWalk profile, plus the left-hand attack and D-pad actions.
- **"D2R Left Hand" tab** in BodyWalk's Mapping (D2R Bridge 0.23): actions for
  the left hand while it holds a weapon.
- **Ready for game updates.** Every time the game starts the mod looks for the
  game code it needs (54 places) by signature, so a game or D2RLoader update
  that moves the code does not switch the mod off. A new **Status** tab in
  D2R VR Settings shows each one - found, not found and what goes off without
  it - and a **Scan** button looks again for whatever is missing.

## 0.140

- **The author's tuned settings are the defaults**: depth from above, the body
  in F4, fog, sky, the toolbar and map in the headset, the weapons.
- **ReShade works right after installing.** The installer fixes ReShade's
  effect search path, which ReShade's own setup writes in a form it cannot read
  (no fog and no sky before).

## 0.139

- **D2R VR Settings checks the game's video settings** in its Status column:
  DLSS, Vertical Sync and TAA anti-aliasing must be off (they mix or halve the
  two eyes' frames), and a frame cap under 180 is flagged.

## 0.138 - first public release

- Four views, F1 - F4: from above in real 3D, third person, the game on your
  floor, and first person (preview - the weapons are not calibrated yet).
- Flat mode: a monitor, mouse and W A S D, no headset and no BodyWalk.
- The installer sets up ReShade, the mod, FlatVR's add-on and, if needed, a
  free BodyWalk Lite inside the game folder; an update keeps your tuned
  settings, and uninstalling removes everything the mod put in.
- D2R VR Settings: Start BodyWalk / Start FlatVR buttons, a status of the whole
  VR chain, an update check.
