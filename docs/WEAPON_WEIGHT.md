# Weapon weight (work in progress)

**Goal:** in first person (F4) a weapon should feel heavy, not like a picture
glued to the controller.

- A dagger stays in your fist.
- A polearm or a sledgehammer lags behind your hand, is slow to start and to
  stop, and its point sinks when you hold it level in one hand.
- With two hands far apart on the shaft you hold it better.

Later, a hit should land where the blade actually touches the monster.

**Status:** in the game behind a setting, being tested: `[weight] on=1` in
d2r_vr.ini, or Weapon Adjust > "Weapon weight" in D2R VR Settings (off by
default). With it on, in F4 with the arms on the controllers:

- a melee weapon held alone is heavy: it lags behind a hard swing, and a
  two-handed one in one hand swings round like a flail - it wants both hands;
- a hand takes the weapon where its grip closes on the shaft and holds it
  there; a spear held by its end is slower than held where the game holds it;
- a hand holds it while its grip is squeezed; let a grip go and that hand
  lets go, the other keeps it - from hand to hand;
- let both go and it falls to the ground you see - the hero's ground in the game's
  world, under his feet, `vr/weight_floor.h`, not the room's floor (a shorter or
  seated player used to see it lie 30 cm and more over the ground) - and lies a
  radius over it; take it up there, or squeeze the grip
  of the hand the game holds it in and it flies back into that fist;
- `[weight] after_drop` (0 lies on the floor, 1 goes to the back - the default, 2 vanishes):
  a weapon that has landed and lain for `to_back_delay_ms` disappears from the floor.
  On the back (`vr/weapon_back.h`: a long weapon diagonally, a short one low across
  the back, relative to his spine bone so it follows his body; live tuning per class)
  it hangs there; vanishing, it is not drawn (the same place is kept, so it flies
  from there when taken). A hand reached behind either shoulder (the shoulder swap's
  volume) with its grip squeezed takes it (`vr/shoulder_grip.h`: with a weapon away
  that press is a take, never also the weapon swap), with a short pulse in the
  hand as it enters the zone (`take_haptic`, `take_haptic_pct`); the grip of any
  free hand anywhere also calls it (`recall_any_hand`), the game's hand always does.
  Only its drawing moves - the weapon stays the game's equipped one - and away it
  strikes nothing.

- `[hands] shoulder_draw` (Settings > Weapon Weight, off by default) makes the shoulders holsters instead (`docs/SHOULDER_DRAW.md`): a weapon let go lies where it
  fell - never to the back, no grip calls it back from afar - and a grip pressed behind the shoulder of its side draws it into the hand that reached (the left hand
  at the right shoulder too: a cross draw). The weapon swap is not pressed from the zone then.

An attack is still a gesture: swing fast, and the game strikes its own
target. The weapon is only drawn where the hands have it.

## Who takes a hand's grip, and the belt potions (`[belt] grab_nearest`, `[belt] grab_trigger`)

In first person (F4) a grip press can belong to several things: the weapon (the second hand within `grab_cm` = 12 cm of the shaft of a weapon the other hand holds or that
lies; the weapon stowed on the back - behind a shoulder or within `back_take_cm` = 25 cm of its drawn shaft; and the game's hand - or any hand with `recall_any_hand`, for a weapon
on the back - calls it back from afar), the shoulder swap (a hand behind a shoulder: `vr/shoulder_swap.h`'s volume), the belt potions (a palm within 12 cm of a bottle), the throw
by grip, the bow's string hand, the chords. Before, a bottle in reach lit and the weight model was told "this hand is busy": a hand in the weapon's zone that was also near a bottle
(a staff held low in front of the belt, a hand at the hip) took the potion (a player's report, 0.167). Now (`vr/grab_priority.h`, `weight::TakeZone` = `hh::Hold::TakeZone`):

- `[belt] grab_nearest` (on): the NEARER of the potion (the palm's distance to the bottle) and the weapon (the fist's distance to the shaft) gets the press; a tie goes to the weapon; behind a
  shoulder it is the swap's. A weapon that is away and would only be CALLED BACK from afar counts by its distance too, so a hand at the belt takes the potion. Off: a potion in reach wins.
- `[belt] grab_trigger` (off): a potion needs trigger + grip; the grip alone never takes one (the bottle is lit but does not hold the grip from the weapon). Needs direct input's trigger.
- every grip press says in the log who got it and why: `vrcam: grip press - the left hand: WEAPON - the weapon is nearer than the potion (grab_nearest); the weapon's shaft is 5 cm
  from the fist (a grip takes it within 12 cm); belt potion 2 (hp1) is 9 cm away - not lit, not taken` (at most four a second a hand).
- `[belt] hold_takes_trigger` (on; 2026-10-17, `vr/belt_trigger.h`, `vr/belt_trigger_wire.cpp`): while a bottle is in a hand (taken, or being drunk) the LEFT trigger is taken from the
  layout, whichever hand holds the bottle, and a right hand's own trigger too. Why: a potion taken with trigger + grip and brought to the mouth was NOT drunk (a player's test of test-54), the
  grip alone drank. The belt key command asks the game for a "shift" first (`0x11B230` -> `0x228D10` drink -> `0x160AC0`: the Shift key, or in controller mode `0x14CF40` = the game's held
  code 0x400, which its pad poll `0x1210740` makes of the LEFT trigger past 30; the right trigger is 0x800) and passes the answer to the drink call - Shift + a belt key is not a plain
  drink. The layout passes the squeezed left trigger on as the pad's LT, so the game saw Shift. Taken, the trigger reads as let go for the pad; it stays masked after the drink
  until the player lets it go (the layout's release gate), so no LT or attack appears behind the drink. Log: `belt potions - a bottle in the hand: the left trigger is the belt's now`.
  F4 only. Off: the triggers pass on as before.

## What is here

| Path | What |
|---|---|
| `cleanroom/heft/heft.h`, `heft.cpp` | The model. The weapon and the hand(s) on it are one rigid body. Each controller pulls its hand through a "muscle": a spring and a damper with a ceiling on force and on torque, all muscles solved together, implicitly. Gravity pulls only the steel: a hand has mass but no weight. Past 1.2 m of lag the weapon snaps back into the fist. |
| `cleanroom/heft/bench.h`, `bench.cpp` | The test weapons and hand movements shared by both benches. Weapons: dagger, broadsword 1.5 kg, two-handed sword 3 kg, spear 1 kg, polearm 3.5 kg, sledgehammer 3.5 kg. Movements: hold, a 0.4 m jerk in 50 ms, a 140° swing in 0.3 s, a chop, a 90° wrist flick in 80 ms, and the same with two hands. |
| `cleanroom/heft/hold.h`, `hold.cpp` | The hands that take the weapon: where a grip closes on the shaft, from hand to hand, the last hand never drops it. D2R's weapon kinds by shape and weight, and the muscles for one hand and for each of two (stiff, and the soft ones of the gravity feel). `heft_test hands` and `heft_test gravity` check it. |
| `vr/weight.cpp` | The same in the game: the controllers put in the room by the head, one step per stereo pair, the hands on the weapon moved to where it has them; `vr/skeletons.cpp` hangs the weapon on the hand that carries it. |
| `cleanroom/heft/heft_capi.cpp` | `heft_bench.dll`: the model with a C interface, for Unity. |
| `cleanroom/tests/heft_test.cpp` | `heft_test`: synthetic checks (see below). `heft_test compare <dir>` compares the model with the Unity bench's runs. |
| `tools/heft_bench_unity` | A Unity 6000.6 project, run in batch mode. It builds a PhysX reference that holds a weapon the way HurricaneVR does, with none of HurricaneVR's code: a kinematic wrist on the controller, a 2 kg hand on joint drives, the weapon locked to the hand, 90 Hz. Our model runs on the same movements beside it. |

`heft_test` checks that:

- a dagger stays in the fist;
- a heavier weapon lags and sinks more;
- two hands far apart hold better;
- the result converges as the step gets smaller;
- the weapon snaps back into the fist.

Build and run the tests:

    cmake --build build --config Release
    build\Release\heft_test.exe

Run the Unity bench. Copy `build\Release\heft_bench.dll` into
`tools/heft_bench_unity/Assets/Plugins/x86_64/` first.

    Unity -batchmode -projectPath tools/heft_bench_unity -executeMethod HeftBench.RunAll -heftOut <dir>
    build\Release\heft_test.exe compare <dir>

`HeftBench.Identify` measures the drive laws, and `HeftBench.Diagnose` shows
where softness comes from.

## What the reference showed

- **PhysX's slerp drive is a spring on the quaternion's vector part, not on
  the angle.** For small angles it is k/4 and c/4. The torque is (k/4)·sin θ,
  and the ceiling is max/2·cos(θ/2). The model follows the same law.
- The linear drive is an honest k·x.
- **Where they agree:** holding still, jerks, wrist flicks and every
  two-handed movement agree with PhysX within PhysX's own solver noise, or
  twice it.
- **Where they disagree:** one-handed swings of a long weapon (a "flail")
  differ more. But there PhysX disagrees with itself too, at 16 against 24
  iterations, by 10-90 cm.

## Gravity, not viscosity (2026-10-14, `claude/weight-gravity-a1adc2`)

The first test in the headset said the weight "felt like viscosity". Measured on the bench (`heft_test feel legacy`), the old model had gravity
(a constant force on the steel's own centre of mass, `heft::Step`) but the wrist was so stiff that gravity could not show:

| held out level, one hand | weight torque | old wrist (k/4 = 375 N m/rad) |
|---|---|---|
| sword 1.2 kg | 4 N m | sinks 0.6 deg |
| two-handed sword 2.5 kg | 11 N m | 1.8 deg |
| polearm 3.5 kg | 31 N m | 4.7 deg |

The force ceiling (60 N m at the wrist) was never reached by a weight, so a chop down and a chop up differed by about a degree. What did show
was inertia and the ceilings: a heavy weapon swung in one hand saturates the wrist and trails like a flail, the same up and down. The dampers
(0.8-0.9 of critical on the load, `4 zeta sqrt(k I)`) follow the controller's velocity, they are not a drag - measured: a damper at 30 % lets a
sword trail a hard cut by 47 cm instead of 34 (it is what makes the weapon follow), so they were **not** reduced, only made a live slider; what
brakes the weapon in every direction alike is a spring so stiff that nothing but inertia can move the weapon against it.

What changed (`[weight] gravity_feel`, on; off = the old stiff wrists, bit for bit):

- **A soft wrist that stiffens with the turn** (`heft::Muscle::torqueSoft`, `hold.cpp OneHandSoft`): the torque spring is 200 (k/4 = 50 N m/rad) near
  the controller's line and its stiffness (and, by the root, its damper, so the damping ratio stays) is multiplied by `1 + (turn/8 deg)^2` for the
  turn still to make. Held out, the weight's torque sags it (sword 3.8 deg, mace 5.3, two-handed sword 7.3, polearm 11.5, dagger 0.4); in a hard cut
  the turn is far off the line, the wrist is firm and the cut is followed as before (a sword's tip 34 cm behind, was 29). The force ceilings are
  200 N m (100 N m at the wrist): a sword never meets them, a two-hander does.
- **Two hands** keep the old wrists (750, 60) - the lever between the palms holds a weight: a polearm sinks 3.0 deg in two hands, 11.5 in one.
- **Gravity, %** (`gravity_pct`, 0-300, 100): the steel's weight x this. A swing down is helped (the weapon is ahead of the controller), a lift is
  slowed (it trails): a sword chopped 60 deg in 0.4 s trails 5.3 deg going down, 9.1 going up; lifted slowly (60 deg in 1 s) 0.0 down, 5.0 up. At 0 the
  model is symmetric (7.6 deg both ways): the lag is inertia and the ceilings alone. A weapon let go keeps the real g (at 0 it would never land).
- **Damping, %** (`damping_pct`, 0-200, 100): both dampers x this. 100 is nearly critical; 30 rings (settles in 0.6 s, was 0.12), 200 is tighter.
- **Strength** now also scales the spring with the soft wrists (stronger hands sag less); with the old wrists it moves only the ceilings, as ever.

Checked without a headset in `heft_test` (the `Gravity` checks, `heft_test gravity`; `heft_test feel [g=% d=% s=% legacy ...]` prints the table) and
`weight_back_wire_test` (the three keys reach the model through `weight::Apply`): the asymmetry, the sag by kind and by hands, gravity 0 symmetric,
the sliders monotone, stable at 72-120 Hz frame times (no jitter, the swing after a jump never grows, the energy of the body and its springs only goes
down, a hand shaken for 20 s keeps the weapon finite and near), the old wrists pinned.

## The hero's strength (`[weight] hero_strength`)

Off by default: every hero then has the same weight. Ticked, the hero's Strength decides how heavy a weapon feels (`vr/hero_strength.h`, pure maths):

| | |
|---|---|
| his Strength | the game's own stat (base, points, gear), read through the unit stat getter on the UI thread five times a second (`gamestate::HeroStrength`) |
| the weapon's requirement | the game's Weapons table, `reqstr`, in the item's Items row: the field is found at run time by the offset and width at which twelve vanilla weapons all read their known requirements (the way the type and the two-handed field are found); a build or a mod that moves them finds nothing, and the weapon then uses a typical requirement for its kind (the median of the vanilla table: knives 25, one-handed swords 70, two-handed 108, mauls 146, polearms 104, spears 80...) |
| his class | `[weight] str_<class>`, a multiplier of his stat in %: Amazon 100, Assassin 100, Barbarian 130, Druid 95, Necromancer 90, Paladin 110, Sorceress 80, Warlock 95 (the game starts a Barbarian at 30 Strength and a Sorceress at 10) |

```
capacity = Str x class %
ratio    = (capacity + 50) / (requirement + 50)     50: a weapon without a requirement is not infinitely easy, a hero with little Strength is not helpless
x        = ratio / 1.3                              1.3: a hero a little above the requirement feels today's weight
steel    = clamp(x ^ -p, 0.6, 1.5)                  the weapon's mass ("Weight %"): the lag behind a swing, the sag held out, the swing's inertia
hands    = clamp(x ^ +p, 0.7, 1.5)                  the wrists' strength ("Hands' strength %"): a two-hander in one hand sags and lags less - the second hand is needed less
p        = 0.5 x [weight] strength_effect / 100     ("Strength matters", 100 by default: twice the margin over the requirement halves the lag)
```

Both numbers are the model's own, so both models take them the same way: ours in its body and its muscles, the weight-physics library as the masses of the steel and its `strength` -
**no interface changed**, an older library works as it is. A Strength that cannot be read leaves the weight as it is; the log says it once. One log line for each weapon (and again when
the factor moves by more than 3 %): `vrcam: weight - the hero's strength: Str 150 x 130 % (his class) = 195 against the weapon's requirement 100 (the game's table) -> ratio 1.63, the steel x0.89, the hands' strength x1.12`.
F4 only. The body is built again with the new steel the moment the factor changes (a level, a new piece of gear), which also makes the Weight % slider live.

Factors for a hero of about level 45 (steel / hands), `hero_strength_test table`:

| class (Str) | Dagger (0) | Long Sword (55) | Great Sword (100) | Great Maul (99) | Colossus Blade (189) |
|---|---|---|---|---|---|
| Amazon (90) | 0.68 / 1.47 | 0.99 / 1.01 | 1.18 / 0.85 | 1.18 / 0.85 | 1.49 / 0.70 |
| Paladin (110) | 0.62 / 1.50 | 0.89 / 1.12 | 1.07 / 0.94 | 1.06 / 0.94 | 1.35 / 0.74 |
| Barbarian (160) | 0.60 / 1.50 | 0.73 / 1.37 | 0.87 / 1.15 | 0.87 / 1.15 | 1.10 / 0.91 |
| Sorceress (40) | 0.89 / 1.12 | 1.29 / 0.78 | 1.50 / 0.70 | 1.50 / 0.70 | 1.50 / 0.70 |

Checked without a headset in `hero_strength_test` (the maths: not read, the reference, monotone and smooth and clamped both ways, the classes, "Strength matters", the requirement from the
game and a typical one) and `weight_strength_test` (`weight::Apply` end to end with the library's stand-in: unticked = today's masses; ticked = the scaled masses and strength in the library
and in our body; a level-up live; one line for each weapon; and on the hold alone in fixed steps - a polearm held in one hand sinks 11.5 deg today, 10.4 in a Barbarian's hand, 15.1 in a
Sorceress'; a sword chopped 60 deg in 0.4 s trails 9.9 deg today, 8.0 with the factors of a strong hand, 12.3 with a weak one - and factors of 1 are today's weight to the last bit).

## An optional weight-physics library (`[weight] hands_model`)

The mod's own physics above is all the mod needs. A separate library can move the held weapon instead: `d2r_vr_weight.dll`, a file that is **not part of this
repository** and not needed to build or run the mod. It sits beside the plugin (`d2rl-vrcam.dll`); when it is there, `[weight] hands_model=1` (Settings >
Weapon Weight > "Move the weapon with the weight-physics library (A/B)") hands the step of the held body to it. Without the file the tick is greyed out, a
status line says so and `hands_model=1` is our own physics, step for step.

- The interface is plain C, `shared/d2r_vr_weight.h` (version 1): per step the mod gives the weapon's pieces and masses, the hands on it (palm, the controller's
  pose and velocity across the frame), g, damping and strength, and the body's state; the library gives the state back (and, for a thrown weapon, a launch
  velocity). It keeps nothing between calls. A library of another version is not used (the log and the Settings line say which).
- `vr/weight_lib.cpp` finds the file, checks the version and the exports, and puts it behind `heft::hold::Engine` (`cleanroom/heft/hold.h`); a step the library refuses is
  taken by our own physics. Only F4 uses it; taking, passing, letting go, falling, the back, the aim and the hit by touch stay the mod's.
- `[weight] hands_model_palm=1` asks the library for the weapon's weight in the palm (a comfort setting; the library may ignore it).
- Checked without a headset in `weight_lib_test` (a stand-in library, `cleanroom/tests/weight_lib_standin.cpp`: no file, a file without the exports, another
  version, the adapter against a reference body, a refused step, a snap, the throw, and "no library = our own, bit for bit") and `weight_lib_compare` (our
  hands and a library side by side on the same motions: sag, chops, a hard cut, settling).

## An optional grip library (`[weight] grip_model`)

Like the weight library above, a second optional file, `d2r_vr_grip.dll` beside the plugin, can decide how a hand takes hold of the shaft and looks while it does. Its C interface is
`shared/d2r_vr_grip.h` (version 2; a library of version 1 is still used, see "Where the hand sits on the shaft" below); the library is not part of this repository and not needed. Settings > Weapon Weight > "Hold the weapon the grip library's way (A/B)" is greyed out, with a
status line saying so, while the file is not there; `grip_model=1` is then our own way, step for step.

- Per step and hand the mod gives the library the hand's rig (its finger bones in the hand's own proper frame, open and closed), the hand as the weight model has it on the weapon, the
  shaft as drawn (axis, ends, the wood's cylinders), the buttons and the controller; it gets back which buttons hold a hand and how the second hand of a long shaft sits (tight, or loose:
  sliding along the shaft and turning about it), where to lay the hand and the rotation of each finger bone, and the pulses (a hand coming within reach, closing, letting go).
- `vr/grip_lib.cpp` finds the file and checks the version; `heft::hold::GripEngine` (`cleanroom/heft/hold.h`) is the hold's seam; `vr/grip_rig.h` turns the hook's bone matrices (any axes, a
  mirror left hand) into the library's rig and back; the hook (`vr/skeletons.cpp`) lays the answer with the same `Reach` of the arm. A step the library refuses is taken by our own rule.
- The second hand is held by its grip, or - the library's rule - by its trigger too: grip tight (the point and the turn fixed), trigger alone loose. The first hand is held by its grip alone.
  The weight, the aim, the hit by touch and the game's own attacks are not touched; F4 only.
- Checked without a headset in `grip_lib_test` (a stand-in library: the loader, the hold's seam - tight, loose, the first hand, the pulses, "no library = our own, step for step" - and the
  hook on a toy rig with real hands, mirror and relabelled axes included) and `grip_lib_compare` (our way and a library's side by side on the same weapons).

### Where the hand sits on the shaft (interface version 2)

A first test of the library showed the hands holding "somewhere above" the sword's line, "by the wrist, not by the hand". The cause: version 1 laid the palm's *surface* on the wood's surface
(the palm's middle at the wood's radius plus 1.5 cm from the axis, the palm turned to face it) with the knuckles dead along the shaft. The mod's own grip fit (`[weight] grip_fit`) lays the
palm's middle - halfway from the wrist to the middle knuckle, the point every grip in the hook is laid by - ON the axis, plus, when the game's own hand has been measured on this weapon
(`grip_fit_like_base`), how that hand lies on the shaft: the axis under the fingers rather than under the palm, and the knuckles leaning on the shaft (the log of a Two-Handed Sword said 26
degrees). The library ignored both, so its hand stood a few centimetres toward the wrist from where the fingers wrap the hilt.

Version 2 of the interface lets the mod say how the shaft lies against the hand, and the library lays the hand that way:

- `vr/grip_fit.h` `AxisOf` reads it off a hand: the shaft's direction in the hand's own frame (fingers, knuckles, out of the palm - the same numbers on a hand and on its mirror image) and the
  vector from the palm's middle to the nearest point of the axis. The skeleton hook measures it on the base hand (the game's hand, as placed) next to the grip fit's `Relation`, and gives
  the hand that takes the weapon `D2rgHandIn::lineInHand` and `axisInHand` (the library's frame turns the knuckle axis over on a right hand; the hook converts).
- `axisInHand` zero - no base hand measured on this weapon yet - is the grip fit's own rule: the palm's middle ON the axis. Never "radius plus palm depth" any more.
- The library turns the hand the smallest turn that lays the line along the shaft, then about the shaft so that the vector points to the side the shaft is on now (a palm 2 cm or nearer to
  the axis keeps its roll), and puts the palm where that vector reaches the axis. The fingers close from the game's grip pose on the wood as before.
- A library of version 1 is given none of this (`griplib::HandStep` clears the new fields) and lays the hand its old way; the log says so once. A version-2 library given a version-1 struct
  reads the new fields as zero.
- The log says, once for each hand, weapon and way of laying, what was done: `grip library - the right hand is laid as the game's own grip has the shaft: ... the palm's middle stands N cm
  from the axis` or `... with the palm's middle on the shaft's axis (no base grip measured on this weapon yet)`.
- Checked in `grip_lib_test` (the toy rig: the vector the hook gives is the base hand's own, in the library's frame, on either hand; with the stand-in laying the hand by it, the palm stands
  0.1-0.3 mm from where the grip fit puts it and the knuckles lean the same; `grip_lib_test v1` for the version-1 library), `grip_fit_test` (`AxisOf`: mirror image, any place and turn, the
  lean, the Relation) and the library's own `grip_physics_test` (on the axis for a zero vector, the vector reaching the axis, the lean, the roll, a version-1 struct, refusals).

### Which weapons' second hand slides (`[weight] slide_second_hand_<kind>`)

The second hand slid along any long shaft while its grip was squeezed (`slide_second_hand`), and with the grip library also by the trigger. That is right for a staff - the left hand runs up the
pole - and for a spear or a polearm, but a two-handed sword's second hand should sit on the hilt where it closed. A tick for each kind of weapon with a shaft long enough to slide on
(`vr/slide_kinds.h`; Settings > Weapon Weight, under "The second hand slides along a long shaft"): swords (two-handed), axes (two-handed), maces (two-handed), staffs, polearms, spears,
javelins and the unnamed kind. Staffs, polearms and spears slide by default, the rest hold where the hand closed (tight, held by the grip alone; the trigger holds nothing). A weapon with
a short shaft (a dagger, a wand, a scepter, a throwing weapon, a one-handed sword) never slid and has no tick. `slide_second_hand=0` switches all of them off.

- The same list drives the mod's own way of holding and the grip library's: `weight::Apply` sets `Settings::slide` from the master key and the weapon's tick, and both the slide and the
  library's second-hand modes (`Hold::Step`, `lineSecond`) are gated by it. The log line for each weapon says "its second hand slides along the shaft" or "holds where it closed".
- Checked in `slide_kinds_test` (the defaults, the mask, the list against the weight model's own shapes - a kind with a long shaft always has a tick and no tick is idle - and the hold itself for
  every long kind) and `grip_lib_test` (through `weight::Apply` and the hook: the staff's left palm follows the controller 25 cm, the sword's stays; the library's trigger holds the staff's
  second hand loose and lets go of the sword's).

### The hands' fine tune with the grip library (`right_grip_*`, `left_grip_*`)

The live per-hand corrections of the grip fit (Settings > Weapon Weight: Right / Left hand X Y Z in cm, rotation X Y Z in degrees) work with the grip library too - the same keys, the same
frame and signs, so a value found in one way of holding is right in the other. They are the shaft's own frame at the grasp point, turned as the hand holds it: X along the shaft (toward the tip),
Y across (toward the fingertips), Z up (out of the palm); the rotations X the roll about the shaft, Y the tilt about the across axis, Z the tilt about the up axis (the left hand's turns
mirrored). 0 is the placement the library derives (the palm's middle on the axis, or the game's own grip); the sliders only tune it.

- `gripfit::WithCorrections` (`vr/grip_fit.h`) lays them on the hand as the library left it - one transform of the hand, the library's and then the corrections' - and `libFit` in the
  skeleton hook uses it; the arm is reached to the corrected palm as for ours, the weapon never moves with them.
- The moves put the hand exactly where they put ours (0.1-0.3 mm in `grip_lib_test`, both hands). The turns orbit the hand about the shaft's axis (as ours does for the game's own base
  hand); ours turns a hand that took the weapon in place about its palm's middle, so for such a hand the two ways differ by the palm's lever (its distance from the axis) times the angle -
  5-8 mm for 10-20 degrees of a palm 2 cm from the axis in the test, 16 mm at most for the test's three turns together.
- The base hand's corrections are part of what a hand that takes the weapon copies, as in ours: the relation is measured again on the base hand after the library laid it and corrected it.
- The sliders sit under "Grip fit" in Settings and are greyed with it; they act with `grip_model` 1 whether `grip_fit` is ticked or not (a Settings greying only).
- Checked in `grip_fit_test` (laid by the library's rule and corrected = laid and corrected in one go, to 0.2 mm; the mirror image; each correction does what it says) and `grip_lib_test`.

### Where the second hand takes the weapon (`[weight] grip_hilt_only`)

The second hand took a weapon the other hand held anywhere within `grab_cm` of its shaft - on a sword's blade as well as its hilt. With the tick a sword's, a dagger's, a scepter's, an axe's or
a mace's second hand takes it only on the hilt (the haft, for an axe or a mace): from the pommel to the guard and `hiltMargin` (3 cm) past it. A grip beyond takes nothing; `TakeZone`
(so the belt potions' priority, `vr/grab_priority.h`) says nothing there either, and the press is free for a potion. A staff, a spear, a polearm, a javelin, a wand, a throwing weapon and
the unnamed kind keep the whole shaft. Unticked (the default): as ever. The first hand and a weapon that lies or falls are not touched; a second hand that slides (a ticked
`slide_second_hand_<kind>`) slides over the whole shaft.

- Where the guard is: the game does not say. The weight model's shape for the kind has the hilt as its first piece (`ShapeOf`: a two-handed sword's handle 36 cm, a one-handed one's 20, a
  dagger's 12, an axe's or a mace's haft 55 cm, a two-handed one's about 110) and the blade or head after it; `Hold::SetDrawnShaft` scales the pieces onto the drawn weapon's extents
  (`bladeshape`'s measure), so the hilt follows the weapon's real length (a two-handed sword whose log says "butt 0.15 m, tip 1.10 m" has a 30 cm hilt). `[weight] grip_hilt_cm_<kind>`
  (Settings: one slider for each of sword, axe, mace, scepter, dagger; 0 = the model's) sets the length from the pommel by hand for the player who finds it wrong.
- The log line for each weapon says where it ends: `... ; its second hand takes it only on the hilt (30 cm from the pommel, the model's)`.
- `vr/grip_zone.h` is the list of kinds (the ini keys); `Hold::HiltEnd`, `Hold::PastHilt` (`cleanroom/heft/hold.cpp`) are the rule; both the mod's own hold and the grip library's use `TryGrab`.
- Checked in `grip_zone_test` (the list against the shapes, every kind one- and two-handed: the hilt and the pommel taken, past the margin not; the whole-shaft kinds; a length of its own;
  no margin; the drawn extents; `TakeZone` agrees; the first hand and a lying weapon) and `grip_lib_test` (the whole chain: `weight::Apply`, the hold, the hook - ours and the library's).

## What is next (help welcome)

1. **Profiles for D2R.** With HurricaneVR's strengths, a long weapon swung in
   one hand tumbles like a flail. D2R probably needs a stronger wrist and a
   lower ceiling on angular speed. Tune them on the bench, by eye.
2. **A visual mode for the bench:** two weapons side by side on one movement,
   to look at rather than only count.
3. **Wire it into the game.** In the game, the model would work like this:
   - The hero's hand stands on the virtual grip, not on the controller
     (`vr/skeletons.cpp`).
   - Mass and centre of mass come from the weapon kind. They could be scaled
     by the weapon's strength requirement and speed in the game's
     Weapons.txt.
   - The muscle's strength comes from the hero's Strength.
   - It is stepped once per stereo pair, or the two eyes see different
     weapons.
   - It lives in room space, not game world space, or walking would drag the
     weapon along.
   - There is a "feel the weight" slider, which can switch it off. A hand that
     is not where you feel it can make people sick.
4. **Hit by touch.** The blade is a capsule along the weapon's bone. Monster
   hit boxes are capsules on their skeletons: vrcam already hooks every
   model's skeleton pose. A swept test between frames finds the touch. The
   hard part is getting from a skeleton back to the game's unit, so the
   attack goes to that monster (the controller's attack-target hook is
   already there).
5. **Steel stops at bodies.** After a touch, turn the blade about the grip out
   of the monster's capsules and kill the speed going into the body.
