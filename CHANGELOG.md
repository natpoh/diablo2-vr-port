# What's new in D2R VR

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
