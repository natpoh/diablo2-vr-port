# Weapon weight (work in progress)

**Goal:** in first person (F4) a weapon should feel heavy, not like a picture
glued to the controller.

- A dagger stays in your fist.
- A polearm or a sledgehammer lags behind your hand, is slow to start and to
  stop, and its point sinks when you hold it level in one hand.
- With two hands far apart on the shaft you hold it better.

Later, a hit should land where the blade actually touches the monster.

**Status:** the model is written and tested outside the game. **It is not
wired into the game yet.** Today the hero's hand in F4 is exactly the
controller, and an attack is a gesture: swing fast, and the game strikes its
own target.

## What is here

| Path | What |
|---|---|
| `cleanroom/heft/heft.h`, `heft.cpp` | The model. The weapon and the hand(s) on it are one rigid body. Each controller pulls its hand through a "muscle": a spring and a damper with a ceiling on force and on torque, all muscles solved together, implicitly. Gravity pulls only the steel: a hand has mass but no weight. Past 1.2 m of lag the weapon snaps back into the fist. |
| `cleanroom/heft/bench.h`, `bench.cpp` | The test weapons and hand movements shared by both benches. Weapons: dagger, broadsword 1.5 kg, two-handed sword 3 kg, spear 1 kg, polearm 3.5 kg, sledgehammer 3.5 kg. Movements: hold, a 0.4 m jerk in 50 ms, a 140° swing in 0.3 s, a chop, a 90° wrist flick in 80 ms, and the same with two hands. |
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
