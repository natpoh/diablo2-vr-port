# Developer notes

What we learned the hard way, so you don't have to. Read this before you
change the camera, the stereo or anything that touches the game's memory.

## How the pieces fit

| Part | Where | What it does |
|---|---|---|
| `d2rl-vrcam.dll` | `vr/` | The D2RLoader plugin: the camera, stereo, first person, HUD, the game's commands. Also a ReShade add-on: it talks to `D2R_DepthFog.fx` and draws the right eye in "one pass" stereo. |
| `D2R_DepthFog.fx` | `reshade/` | Fog, sky, cave ceilings, the frame stamp for FlatVR. Built from the game's depth. |
| `D2R_VR_Settings.exe` | `tools/settings/` | The settings app. It writes `d2r_vr.ini`; vrcam reads it again while the game runs. |
| `d2r_bridge.dll` | `bodywalk_bridge/` | The BodyWalk plugin: the head and controllers into shared memory (`shared/`). |
| clean-room code | `cleanroom/` | The camera maths, the draw distance, the signature scanner and the weapon weight model - no game code. |

BodyWalk, FlatVR and FlatVR's ReShade add-on are separate products. Their
shared-memory headers are in `external/`.

## Debugging

- **The log:** `<game>\d2rloader\logs\d2r-vr-vrcam.log`.
  - Every 10 s it prints the presents per second and the game's busiest
    threads, and in stereo the pairs per second and where a pair's time goes.
  - It names every biome you enter (`vrcam: biome '...'`).
- **A hang:** a watchdog writes every thread's stack to `d2r_vr_hang.txt`
  beside the log.
- **The live ini:** most keys in `d2r_vr.ini` apply while the game runs, so
  you can test without restarting. The `[debug]` section has the switches for
  experiments.

## Finding a game pass

Use this to find the game pass behind a line, a veil or an artefact:

1. Stand where you can see it and press **Ctrl + F10**. vrcam writes
   `d2r_vr_uitrace.txt` beside the ini, and snapshots of the render targets
   as `uitex_*.raw`.
2. Run `python tools/uitex_to_png.py <folder> <out>` and find the first target
   where the artefact is already there.
3. Run `python tools/trace_passes.py --depth`. It lists the runs of draws into
   full-size targets that have a depth buffer bound, with each run's pipeline
   (`pipe`, this session's handle) and its pixel shader's hash (`ps`, the same
   every run).
4. Set `[debug] skip_pipeline=<pipe>` in the live ini, one pass at a time, and
   see whether the artefact goes. Skipping only stops the draw; it writes
   nothing into the game.
5. If it is that pass, put its `ps` in `[render] skip_shaders` (up to 8,
   separated by spaces) - **but only if the picture does not get darker.** A
   pass that also carries the game's light has to be fixed, not skipped.

## The game's image

- **All addresses are for the image D2RLoader rebuilds** (base 0x140000000),
  not for a plain D2R.exe. Those are different compiles, with different code
  and different field offsets. A new D2RLoader build moves them too.
- **The game's code is encrypted on disk** and decrypted page by page the
  first time it runs. A hook must wait until its page is there: the signature
  scanner does that and the Status tab shows "waiting". Searching the file on
  disk finds nothing; search a memory snapshot.
- **The signature scanner** (`cleanroom/sigscan`, `game_sigs.h`) covers the
  code addresses only. Field offsets are still fixed numbers.

## Things that crash or break the game - do not

- **Do not write the game's heap at addresses you found in an earlier run.**
  An old fog experiment did that and crashed the game.
- **Do not write a held weapon item's own skeleton pose.** The item went
  translucent. Move the hand's bones instead.
- **Do not scan the game's heap or walk its threads on the game's thread or
  the present thread.** Both made 30-50 ms hitches. Use a thread of your own,
  and keep it cheap.
- **Do not write numbers into the game's `Settings.json` with a regex
  replacement like `"$1" + number`.** `"$188"` means group 18; it corrupted
  the file once.
- **Do not draw a frame twice by repeating `PrismBlit`.** The per-thread
  upload lists were already reset, and it removed the device. Repeat the
  whole game-screen draw (`sDrawGameScreen`) instead.
- **Do not give the second pass zero time.** Effects that skip a frame of no
  time (butterflies, some particles) then show in one eye only.
  `[stereo] right_dt_ms` is 0.01 ms.

## Stereo

- **ReShade on D3D12 has one constant buffer per effect**, rewritten at every
  present without waiting for the GPU. The second eye overwrote the first
  eye's sky and HUD values. The fix: two techniques, `D2R_DepthFog` and
  `D2R_DepthFog_R`, the eye a constant of the technique, and both eyes' values
  in the constants. **Ship the shader and the DLL together.**
- ReShade renders effects once per present. In the one-pass mode, vrcam
  renders the right eye's effects itself.
- **The game's camera constant buffers** hold 5 camera records (192 floats:
  view, inverse view, projection, inverse projection, view-projection and its
  inverse; then the previous view-projection at +96, the jitter at +112 and
  the pass before at +130).
  - The same buffers also carry the **shadow cascades' light cameras, with an
    orthographic projection. Never move those** - the right eye's shadows go
    wrong.
- **Per-object constant buffers carry the left eye's view-space positions.**
  The eyes are parallel, so the right eye's shift is folded into its
  projection: P_right = P · T(dx).
- **One present per stereo pair.** Two presents drifted the swap chain's
  latency until it deadlocked.
- **Temporal effects mix the two eyes:**
  - TAA: a ghost behind everything that moves;
  - FlatVR's frame generation: triple images;
  - the game's motion vectors, which in stereo point at the other eye's
    picture - `vr/dlss_mv.cpp` corrects them for DLSS.
- **DLSS:**
  - its "depth" input is a 0/1 mask, not real depth;
  - it draws into the top-left part of screen-size targets, so reading the
    depth needs a scale (`DepthScale`).

## First person

- **The hero's facing** comes from the renderer's ECS registry.
  - `TransformComponent` (0xD0 bytes): +0x00 the local matrix, written before
    the poses; +0x40 world, written after the poses, so a frame old at the
    left eye; +0x80 the previous one; +0xC0 the parent.
  - Using a late copy made the left eye's body shake.
  - The component storages are found by the hash of the type's name, not by a
    vtable.
- **Skeleton poses** (Granny): the world pose is in model space, with a bone
  stride of 0xA4. Bone names differ between classes (`vr/skeletons.cpp`,
  `Learn()`). The hero is the model whose path has `/character/player/`.
- **The bridge's controller axes:** forward is -Y, up is -Z.
- **XInput:** xinput9_1_0 calls xinput1_4, so hook only the outer call, or the
  stick turns twice.

## The interface

- **All of D2R's interface is drawn into one RGBA16F layer:**
  - cleared to (0,0,0,1);
  - colour premultiplied;
  - alpha = how much of the world shows through.
- The map draws under its own scissor rectangles.
- The toolbar's rectangle is computed from the game's Safe Screen Percent.
- **Menus:** the `*frontend*` and `ui_*` biomes are menus (character select,
  create new). The game's own camera must stay there.

## Shared memory with BodyWalk / FlatVR

- **Readers check the version exactly**, so a block never grows in place: add
  fields behind a new magic value, or in a new mapping. Older FlatVR rejects a
  newer HUD block.
- **D3D12 textures shared between processes:**
  - keep a retired handle alive for 120 frames, or the device hangs;
  - R16G16B16A16_FLOAT works, while R32G8X24 came out tiled.

## Building and testing

    cmake -S . -B build -A x64
    cmake --build build --config Release

- `build\Release\heft_test.exe` tests the weapon weight model.
- `sigscan_test.exe` tests the signature scanner.
- Install your build by copying `d2rl-vrcam.dll` into
  `<game>\d2rloader\plugins` and `D2R_DepthFog.fx` into ReShade's shader
  folder, with the game closed.
