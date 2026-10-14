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

An attack is still a gesture: swing fast, and the game strikes its own
target. The weapon is only drawn where the hands have it.

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
