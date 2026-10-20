# D2R VR

Diablo II: Resurrected in a VR headset: the game's own view from above in true
3D, a free camera behind the hero, and the game laid on the floor of your room
(mixed reality), and first person with your body as the hero's: arms on the
controllers, the weapon in your hands - the blade you swing picks the target,
weapons have weight, you throw with your hand and draw a bow with both - with a
painted sky, depth fog and the game's day and night. On native OpenXR the mod
can read your controllers itself, with no BodyWalk needed for the controls (a
prototype).

**[Download D2R VR](https://bodywalkvr.com/api/download/latest?product=d2r_vr)** -
the ready installer, always the latest version. It sets up everything,
including a free BodyWalk Lite if you don't have BodyWalk.

**[Open the mod's page on BodyWalkVR](https://bodywalkvr.com/mods/d2r-vr)** - the
four views in video, the controls and the setup.

**What's new in 0.174:** your height measured right standing, and a seated mode; the hero crouches when you crouch; the game's sounds turn with your view; the bow's orbs no longer shine through, the arrow's fire goes out at the shot; the map in the hand at any window size. **In 0.173:** melee skills armed by their button - every blow is the skill, its icon in the mana medallion; the map in the hand with the game's map in the middle; the right stick tilts the view in F2; Setup always asks for the game's folder. **In 0.172:** a first-start guide in the headset for a new player (a page of the skill panel's Settings window); thumbstick dead zones against stick drift; the Kurast sewer stairs taken standing with A; a shield put away behind the shoulder can stop blocking (its own tick, off); the launcher shows each mod's own icon. **In 0.171:** a staff held in both hands aims the spell along the staff again. **In 0.170:** D2R VR is a D2RLoader mod now - everything of it in
`mods\D2R-VR`, so it no longer shares files with the other VR mod and D2RLoader's
`-mod` picks which one runs (Setup moves an old install, your settings with it);
the shared D2R VR Launcher comes with Setup; the Arcane Sanctuary's own sky all
round, made from your own game's files, with an optional fog that fades into it; SteamVR thumbsticks that are bound to
nothing are told and read through a spare action, and a real gamepad joins the
plugin's pad; the ReShade settings of another mod no longer stop ours, and a ReShade
you already have (dxgi.dll) is used as it is; native OpenXR never hangs the game on a
busy headset, takes your own eye distance and draws canted displays right; a spell's
aim distance can follow your arm's reach (off); Hit by touch in the headset's panel,
and with it the trigger no longer attacks (a bow or a crossbow keeps it); skills on the right trigger and on A (off);
the wrist orbs under a shield
can be set per hero class (depth, along the arm and turn); the hero's own effects (Bone Armor) can
be faded (in the build, off, not working yet); a Barbarian's Double Swing by the two hands (off); the fog can
spare fire, spells and smoke (a test, off); no more hang at a portal into a new area. **In 0.169:** the weight and grip
libraries out of the plugins folder. **In 0.168:** no more frozen game in native OpenXR (the right eye goes
to the GPU in the game's own calls); weapons with weight by the hero's Strength
and a two-handed hilt grip; a flail's ball on a real chain; weapons, the bow, the
crossbow and the shield put away behind your shoulders; energy orbs along the bow
that pick the arrow skill, and arrows that burn, freeze or crackle; the crossbow
held by its grip; spells aimed by the hand or staff; buffs, auras, curses and
summons on your wrist; skills by the class's trees; Run / Walk on any button.
**In 0.167:** a faster picture in native OpenXR (our own depth, picked
once), the full controller in VR F1, F2 and F3 without BodyWalk, a shield on the
forearm, and settings from the fork d2r-pcvr (turning, the Steam Frame's layout,
vibration for each hand, menus that follow the VR render size). **In 0.166:** the
features of 0.165 are on by default. The settings
that come with the mod are now the author's own tuned configuration (0.165 went
out with the old ones, with everything new off). **In 0.165:** first person on
native OpenXR grows up. The plugin can read your controllers itself (*Controls without BodyWalk*, a prototype); the
blade you swing picks the target (*hit by touch*); weapons have weight; you throw
with your hand and draw a bow with both; a skill wheel and a settings panel hang
in the headset; life and mana sit on your wrists; F1 and F3 are in real 3D; and an
attack no longer runs the hero forward. The hand and weapon features are for
first person (F4). All versions:
[CHANGELOG.md](CHANGELOG.md).

**Want to help?** The roadmap, the known problems with what we know about
each, and notes for developers are in [docs/](docs/README.md) - fork it, fix
one and send a pull request.

**Discussion, bugs and ideas:** our Discord, channel
[#diablo-2-vr](https://discord.com/channels/1481909961897279562/1556377249484251298)
under *Game Mods* - not on the server yet? [Join it here](https://discord.gg/kVYjEdJx3e).
Tell us what works, what doesn't and what you would like to see.

> Offline single-player only. Injecting code into the game online can get the
> account banned. This project is not made, endorsed or supported by Blizzard
> Entertainment.

It is a [D2RLoader](https://d2rloader.net) mod, `mods\D2R-VR` in the game's folder: a plugin
(`d2rl-vrcam.dll`) and a ReShade effect (`D2R_DepthFog.fx`) with everything that goes with them
(`docs/d2rloader_mod.md`). For the headset it talks to BodyWalk through BodyWalk
Game Link (`external/bodywalk/game_link.h`, BodyWalk's `game_link` plugin), which
hands it the head and the hands; FlatVR (in BodyWalk) shows the picture in the
headset. Without a headset it plays flat on a monitor with mouse
and keyboard (F1 the game's camera, F2 third person, F3 first person).

## Views (F1 - F4 in the game, F12 goes round)

- **F1 From above** - the game's own camera; *Real geometry* draws it through our
  camera in true perspective and stereo.
- **F2 Third person** - a free camera behind the hero, turned with the head.
- **F3 The game on your floor** - look down at it and walk round it.
- **F4 First person** - the hero's arms follow the controllers and hold the
  weapon; the hero turns with you.

## The game's video settings

In Diablo II: Resurrected, Options > Video:

| Setting | Set to |
|---|---|
| **Vertical Sync** | **Off - important**: the mod draws the two eyes in turn, and VSync halves the frames each eye gets |
| NVIDIA DLSS | On or off - both work in stereo: each eye gets its own DLSS (in *Stereo - one pass, two pictures* the two eyes share one) |
| Framerate Cap | 180 (90 for each eye) |
| **Anti-Aliasing** | FXAA or MSAA - **not TAA**: it builds each frame from the ones before it and mixes the two eyes |
| Ambient Occlusion Quality | Medium |

![Options > Video](screenshots/game_video_settings.jpg)

![Vertical Sync off, Framerate Cap 180](screenshots/game_video_vsync.jpg)

### A square screen

For a square picture in the headset, either:

- **Native OpenXR's VR render size:** in D2R VR Authentic Settings > Home > In the headset
  drag *Resolution (VR render size)* to a size from 1024 to 4096 pixels a side,
  or tick *Use the headset's recommended size*. The game draws at that size
  whatever its window is; the window keeps its own size, no dragging (new: if
  it misbehaves, drag the slider to its left end, "window's size", and use one
  of the ways below); or
- **Windowed mode:** set the game to *Windowed* in Options > Video and drag
  the window's edges until it is square; or
- **FlatVR's virtual monitor:** in BodyWalk's FlatVR tab create a virtual
  monitor with a square resolution, move the game onto it, then switch the
  game to full screen there.

### The screen's size in the headset

How big the screen is, how far away and how curved: in BodyWalk, on the
**FlatVR** tab - or right in the headset, in FlatVR's overlay (**F10** by
default).

### The game in your room (F3, on the floor)

In D2R VR Authentic Settings > Camera > *F3 The game on your floor*, pick a **Background
round the game** colour (pure black or green). In Virtual Desktop turn on
passthrough with that colour as its colour key: the background is cut out and
the game stands in your real room.

## Controls in F1 - F3

From above, from behind and on the floor the game is played as with a gamepad:
BodyWalk turns the VR controllers into an Xbox pad.

| On the pad | On the VR controllers |
|---|---|
| Left stick (walk) | Left stick |
| D-pad up / down / left / right | Hold the left grip + left stick up / down / left / right |
| Map | Click the left stick |
| Menu | Hold the left stick down |
| Reset the centre ("ahead" is where I look) | Hold the right stick down |

Everything else is where the game's own gamepad layout puts it.

## Controls in F4 (first person)

> **Preview.** F4 is an early look, not a finished way to play the game: the
> weapons are not calibrated yet, so how a weapon sits in the hands and where
> it aims may be off for many of them. Weapon Adjust in D2R VR Authentic Settings lets you
> fix one by hand meanwhile.

| Action | How |
|---|---|
| Potions | On your belt: reach for them - set up in BodyWalk's Mapping as gestures |
| Bow, crossbow, fireballs and other spells | Right trigger - the shot goes where your hand points |
| Sword, axe and other melee | Swing it: a Strike Zone in BodyWalk's Mapping |
| Inventory | Hold the left stick down |
| Map | Click the left stick |
| Swap weapon sets | Click the right stick |
| Reset the centre | Hold the right stick down |

With **Controls without BodyWalk** (D2R VR Authentic Settings > Stereo > Native OpenXR,
on by default since 0.166, a prototype) the plugin reads the controllers itself in F4, and
BodyWalk's Mapping is not needed for them:

| Control | Does |
|---|---|
| Left stick, right stick | walk, turn |
| Right trigger | attack |
| Right A | pick up, open, talk |
| Right B, left X, left Y, left trigger | the game's skill buttons B, X, Y and LT |
| Right stick click | swap weapon sets; held half a second: the skill panel |
| Left stick click | show items (Alt); a quick click: the map; held half a second: the inventory |
| Menu button (PS VR2: the right controller's Options) | the inventory; held half a second: the pause menu (Esc) |
| Grips | nothing on the pad: the belt's potions, the staff's second hand and, with Weapon Weight, the weapon read them |

Every line is in `[direct]` of d2r_vr.ini and can be rebound - by hand, or from a list
in D2R VR Authentic Settings > Controls > Buttons of the controllers. In F1, F2 and F3
(without BodyWalk running) the same controller is the game's gamepad too, without
first person's skill panel, settings panel and gestures; the left trigger held with a
flick of a thumbstick drinks a belt potion (left = 1, up = 2, right = 3, down = 4).

**The skill panel and the settings in the headset** (prototype, on by default
since 0.166; D2R VR Authentic Settings > Stereo > Native OpenXR). **Hold the right stick click for half a
second** and the skill panel opens in front of you - a short click is still the
weapon swap; hold it again to close the panel. Point a controller at a slot and
pull the trigger: from then on that trigger casts it. EDIT SKILLS puts a skill
into a slot. With the tiles' look and *Settings panel in the headset* ticked, the
window has a second tab, **Settings**, and the same hold opens it on the tab you
used last. Flip along Skills and the Settings pages View, Options, Video and Body
with a flick of a thumbstick, a tap of a grip (left back, right on) or the arrow at
the window's edge. Without *Controls without BodyWalk* the panel opens with the
left grip held and a click of the right stick.

**Height calibration** (prototype; D2R VR Authentic Settings > Body > *Measure my height and
arm span*, or *Calibrate body* on the headset panel's Body page). Stand upright
with your arms out level at shoulder height (a T-pose), let go of both triggers,
then squeeze both and hold still for 1.2 seconds; a panel in the headset leads
you, and the right B cancels. It saves your eye height and arm span (`[calibration]`
in d2r_vr.ini): the eye height sets the world's scale and the span fits the hero's
arms, while the hands stay 1:1 with the controllers. *Use the measured height and
arm span* switches it on and off; *Forget the measurement* clears it. It needs the
headset's floor (a room set up) and first person (F4).

## Requirements

- Diablo II: Resurrected 3.3.93787 under D2RLoader 1.3.1 (other builds need
  their addresses checked; the plugin says so in its log)
- ReShade with add-on support, its Generic Depth add-on on
- For the headset: BodyWalk 1.78 or later with FlatVR and its Game Link plugin (Setup puts it in).
  Native OpenXR with *Controls without BodyWalk* needs no BodyWalk

## Settings

`D2R_VR_Settings.exe`, next to `D2R.exe` (Setup also puts a **D2R VR** shortcut on
the desktop). Changes apply at once while the game runs. Start the game from it
too: Home > **Launch Diablo II (D2RLoader)** - no need to open the game's folder.
Every tab, with a picture and what it is for: [SETTINGS.md](SETTINGS.md).

![D2R VR Authentic Settings](screenshots/settings_00_home.png)

## Building

Visual Studio 2022+ with CMake:

    cmake -S . -B build -A x64
    cmake --build build --config Release

D2RLoader's plugin SDK is fetched from GitHub. The headers of BodyWalk, FlatVR
and ReShade the build needs are in `external/`. `installer/` builds the setup
(Inno Setup 6).

### Known problem: the source does not build as it is yet

**What fails.** The plugin draws the gesture runes BodyWalk scores
(`[openxr] runes_draw`). That code (`vr/rune_geom.h`, `vr/link.h`, `vr/link.cpp`)
uses a section of BodyWalk's Game Link header, `bodywalk/game_link.h`, called
*VrRunes*: `BW_LinkVrRune`, `BW_LinkVrRunes`, `BW_LINK_VRRUNES_*`,
`BW_LINK_RUNE_*` and `BW_LINK_CAP_VR_RUNES`. The header in `external/bodywalk/`
comes from BodyWalk's released SDK, which does not have that section yet, so the
build stops with "undeclared identifier" errors in `vr/link.h` and
`vr/rune_geom.h`. The installer's DLL is built with a header that has it.

**What to do meanwhile.** Build against a `game_link.h` that has the VrRunes
section: put it in a folder as `bodywalk/game_link.h` and give CMake that folder,

    cmake -S . -B build -A x64 -DBODYWALK_SDK_INCLUDE=<that folder>

The section only adds to the header, so the rest of it is the same.

**It will be fixed in a following release:** BodyWalk publishes the VrRunes
section in its Game Link SDK header, and the tree builds as it is. The runes
themselves also need a BodyWalk that sends them, which is not out yet.

## Thanks

The idea of a free 3D camera in D2R came from
[emmericp/D2R-3D](https://github.com/emmericp/D2R-3D). D2R VR has its own
camera and contains none of its code.

Thanks to **bearstonem** for [d2r-pcvr](https://github.com/bearstonem/d2r-pcvr)
(MIT), a fork of D2R VR: the laser pointer on menus and panels, the menus as a
panel in first person and the spells that fly along the hand with no
auto-target follow what that fork found in the game - where to hook, the extra
menu panels, the spell resolver and its call sites. So, in 0.165, do reading the
controllers directly, the skill wheel, the weapon swap over the shoulder, the
shield's Smite, the item tooltips at once, the controller vibration, hiding the
plates of ordinary monsters, the T-pose measurement, and the bow's arrow skills
and bones. And, in 0.167, the turn settings, the Steam Frame's layout, the Smite
press, the one-press interact, the vibration channels, the menus that wait for the
headset and follow the VR render size, the depth set at its source, the shield on
the forearm and our own depth bound once.

## Licences

D2R VR is MIT (`LICENSE`): use it, change it, build your own and share it -
keep the copyright notice. The licence does not cover the name "D2R VR" as a
mark, nor BodyWalk / FlatVR, which are separate products.

Third-party: MinHook (BSD 2-clause, `third_party/minhook`), the ReShade SDK
headers (BSD 3-clause, `external/reshade`), the D2RLoader plugin SDK (MIT).
Diablo II: Resurrected is Blizzard Entertainment's; this project is not affiliated
with or endorsed by Blizzard and ships none of the game's files.

## Say thanks

D2R VR is free and stays free - no paywalls, no exclusive builds. If you like
the project and want to thank the author, you can support it on
**[Patreon](https://www.patreon.com/c/d2vr)**: it pays for development time and
VR hardware for testing.
