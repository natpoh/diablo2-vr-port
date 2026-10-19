# Developer notes

What we learned the hard way, so you don't have to. Read this before you
change the camera, the stereo or anything that touches the game's memory.

## How the pieces fit

| Part | Where | What it does |
|---|---|---|
| `d2rl-vrcam.dll` | `vr/` | The D2RLoader plugin: the camera, stereo, first person, HUD, the game's commands. Also a ReShade add-on: it talks to `D2R_DepthFog.fx` and draws the right eye in "one pass" stereo. |
| `D2R_DepthFog.fx` | `reshade/` | Fog, sky, cave ceilings, the frame stamp for FlatVR. Built from the game's depth. |
| `D2R_VR_Settings.exe` | `tools/settings/` | The settings app. It writes `d2r_vr.ini`; vrcam reads it again while the game runs. |
| BodyWalk Game Link | `vr/link.cpp`, `shared/` | How the mod talks to BodyWalk (`external/bodywalk/game_link.h`): the game's actions and Mapping tabs out, the head, the hands and the actions fired back. BodyWalk's side is its own `game_link` plugin, which the installer puts in. |
| clean-room code | `cleanroom/` | The camera maths, the draw distance, the signature scanner and the weapon weight model - no game code. |

BodyWalk, FlatVR and FlatVR's ReShade add-on are separate products. Their
shared-memory headers are in `external/`.

## Debugging

- **The log:** `d2r-vr-vrcam.log`, D2RLoader's log of the plugin: `<game>\mods\D2R-VR\d2rloader\logs\` or `<game>\d2rloader\logs\`
  (where D2RLoader writes it for a mod's plugin is not checked in the game yet; the launcher manifest names both). What vrcam writes itself (`d2r_vr_start.txt`, `d2r_vr_crash.txt`,
  `d2r_vr_hang.txt`, `d2r_vr_gpu_fault.txt`, the profile and trace files) and D2R VR Settings' `d2r_vr_launch.txt` are in `<game>\mods\D2R-VR\d2rloader\logs`
  (the `..\logs` of the plugin's folder); ReShade's log is `<game>\mods\D2R-VR\reshade\ReShade.log` (a ReShade of your own as `dxgi.dll`: the game's folder).
  - Every 10 s it prints the presents per second and the game's busiest
    threads, and in stereo the pairs per second and where a pair's time goes.
  - It names every biome you enter (`vrcam: biome '...'`).
- **A hang:** a watchdog writes every thread's stack to `d2r_vr_hang.txt`
  in `<game>\mods\D2R-VR\d2rloader\logs`.
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

Skipping a pass tells you less than it seems: skipping the act 2 town's line
"removed" it only because that pass was FXAA, and without it the picture before
the tone mapping showed, too dark to see the line. Read the shaders instead:

- **The game ships its shaders with names** (`data/hd/shaders/*.bin` in its
  CASC archives, readable with `tools/casc_extract`). Each `.bin` holds DXBC
  containers (one per permutation); the FNV-1a 64 of a container's bytes is
  the trace's `ps` / `vs`. Hash them into a `<hash> <name> ...` list on your
  own machine and pass it to `tools/trace_passes.py --names`; `fxc /dumpbin`
  disassembles a container. These are Blizzard's: never commit them.
- **The trace writes each full-screen draw's constants:** a `full-screen:` line
  with both shaders, the depth / blend state and the root constant buffers by
  register (`b10=cb1 ...`), and `d2r_vr_uitrace_cb.txt` with each distinct
  buffer's floats.
- **`b10` is the frame's view constants** (the camera at `[1]`, the linear
  depth range at `[53]`); `b3` the pass's camera (inverse view at `[9..12]`,
  inverse projection at `[17..18]`, near and the ortho flag at `[37]`).
  Settings sized for the game's camera from above show up here first.

## The game's light culling at non-16:10 sizes

`[render] light_bounds_fix` (`vr/light_bounds.cpp`, `vr/light_bounds_shader.h`). The game's lighting cuts the
picture into 32 x 20 tiles and a compute shader (`cs_light_bin_depth_bounds_c`) finds each tile's smallest and
largest depth. It splits a lane's running index into a 2x2-block column and row, and takes the row from the
tile's block *height*: right only where tiles are square (16:10). At a square window a tile is 1.6 times as tall
as wide and its lower rows are never read; at a wide one the walk runs past the tile. The lights binned against a
wrong range vanish in hard rectangles (a roof's shading flickering as the head moves).

It is not a VR matter: any size that is not 16:10 has it. 16:9 (a monitor: tiles 60 x 54 px at 1920x1080) gets a mild
form - the walk runs a few rows past the tile and skips some blocks inside it (about 9 % of the tiles of a worst-case test
picture); a square window gets the full one (60 x 96 px at 1920x1920: 581 of 640 tiles wrong in the same picture). So
`[render] light_bounds_flat` (on) binds the replacement in flat mode ([mode] platform=0) too. What the game draws at in
flat mode: the window's own size, and with native OpenXR on and a render size set ([render] vr_resolution_px) the
size asked of the game while the session runs, flat picture or not. The log says it once per change: `light bounds -
flat mode, the game draws WxH: a light tile is A x B px, square|not square ...; the replacement is bound|ready but not bound`.

- **The match** is by the shader's size (2164) and the FNV-1a 64 of its DXBC container (`7C22D74C89A5305C`) as
  ReShade hands it over in `init_pipeline`. Check it on your machine: `tools/casc_extract` takes
  `data:data\hd\shaders\cs_light_bin_depth_bounds_c.bin` out of the archives (one container, 2164 bytes at offset
  236); `[debug] log_compute_shaders=1` writes a line with the size and hash of every compute shader a running game
  makes. Another build of the game matches nothing and keeps its own shader.
- **The replacement** is our own HLSL (the game's disassembly, `fxc /dumpbin`, shows what it does; nothing of the
  game's is in the repository), compiled with `d3dcompiler_47` at the first match, made as a second pipeline
  beside the game's and bound in its place (`bind_pipeline`) while the key is on in VR, and in flat mode with
  `light_bounds_flat` - live, the mode can change while the game runs; the replacement is made whenever the key was
  on at the start. Any failure keeps the game's own shader.
- **It must join ReShade before the game makes its pipelines**: `init_pipeline` is not replayed for those made before
  an add-on joined, so vrcam registers it right after loading `ReShade64.dll` in the plugin's load, not on the update
  thread's first tick. The log says `light bounds - the game's light-bin depth-bounds shader found` when it caught
  the pipeline, and `... has not been seen` after a while of compute work when it did not.
- **The test**: `light_bounds_test.exe [<the game's container>]` runs the shader on WARP (the GPU with
  `D2RVR_TEST_HARDWARE=1`) against a scan of every pixel at 16:10, square, wide and atlas sizes. Given the game's
  own container it also shows that its numbers equal ours at 16:10 and miss at the others. Take the container out
  of the .bin yourself (it starts at the `DXBC` magic); it is Blizzard's, never commit it.
  `tools/light_bounds_reshade_test.exe <container> <ReShade64.dll>` does the same through the real ReShade on a
  D3D12 device (the game's pipeline in, ReShade's `init_pipeline` and `bind_pipeline` in between): it proves the
  hand-over, the match, the swap and the teardown without the game. It copies ReShade beside itself; run it from a
  scratch folder.

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

## A render size apart from the window (`[render] vr_resolution_px`, `vr_resolution`)

Native OpenXR only. The game is told a size through its own `WM_SIZE` while its
window keeps the size it has; the idea and the shape are d2r-pcvr's (MIT).

- **The ini:** `vr_resolution_px` is a square's side (1024 - 4096; the Home page's
  slider, steps of 64, its leftmost stop "window's size" = 0); `vr_resolution`
  is the older mode (0 Desktop, 1 Headset = the slider's tick box, 2..6 squares,
  still read from older inis). `shared/render_size.h` reads the two the same way
  for the plugin and for D2R VR Settings.
- **Dragging the slider** writes the ini at every step: the controller waits
  `kDebounceMs` (0.7 s) for a wish that has stopped changing before the game is
  asked, so the picture (and ReShade's effects) are rebuilt once.
- **A size the runtime refuses** (`xrCreateSwapchain` fails for it): `Fallback`
  takes the last size that worked, or the window's own size, and says so once.
- **What the headset says** (recommended size, the most it takes, its name) is
  written by `xr.cpp` to `d2r_vr_headset.ini` beside the ini when a session
  starts, for the settings' tooltip and status line; not into `d2r_vr.ini`,
  which vrcam reloads on every change.
- **Where:** `vr/vr_resolution.h` (sizes, and when to ask - no Windows in it),
  `vr/render_window.h` (the request and the window procedure's half, over a
  window "environment" so tests use fake windows), `vr/render_resolution.cpp`
  (the user32 hooks and the glue). Hooks into the rest: `GameWndProc` (first
  lines), `HookGameWindow`, `xr.cpp` (`OnPresent` ticks it, `Teardown` gives the
  size back), `gamestate.cpp` (the game's own graphics Apply).
- **Never `SendMessage` from the draw thread.** It posts; the window's thread
  runs `Handle`, which calls the game's procedure with the `WM_SIZE`.
- **The game reads its cursor as `GetCursorPos` + `ScreenToClient` and moves it as
  `ClientToScreen` + `SetCursorPos`.** Those two calls are mapped between the
  window's pixels and the game's, but only when the caller is inside D2R.exe's
  image: this plugin's laser (`xr.cpp`), mouse look and ReShade keep the window's
  real pixels. A call from a new place in this DLL must keep that in mind.
- **A render size can be a shadow map's size** (2048, 4096). While one is in
  force the effects' own depth pick is on and an exact-size depth-stencil needs
  a colour target bound with it (`scenedepth`).
- **The window's own size comes back** on Desktop, when the session ends, when
  the headset has been away 30 s, and at `WM_CLOSE` (before the game can write a
  windowed size into its options).
- Tests: `vr_resolution_test` (fake windows, the policy over time) and
  `render_pointer_test` (the real hooks on a window of its own).

## Flat mode (`[mode] platform=0`) is not VR

The shipped ini is made for VR (`[openxr] on=1`, `square_window=1`, `[render] vr_resolution_px`, `[stereo] afr=1`). A flat player's
game must use the window and resolution the player set in the game, and the mod chooses and writes none:

- `xr::LoadSettings` reads `[mode] platform` itself: flat = `on` is 0 whatever the ini says - no OpenXR session, so no render size
  either (`vrres::Update` gets Desktop).
- `BigWindowTick` (the square window at run time, native OpenXR), `NoTemporalAA` (the game's TAA -> FXAA in Settings.json) and
  `SafeGameWindow` (the cut of a tall window) act in VR only.
- **The square window is VR's** (platform 1, native OpenXR and FlatVR alike), as it was; flat mode has none.
- **The player's own window comes back in flat mode**, `shared/window_json.h` (`ForPlatform`, tested by `window_json_test`): a window
  that is not square is the player's own and is kept in `[openxr] window_before` / `window_mode_before` in every mode (so before any
  square is written); a square one in the file in flat mode (written by the settings for VR, or by the game itself at its quit: it
  saves the size its window had) is replaced by the kept window, the mode too. **Nothing kept = nothing written** (the game uses what
  the file says; the log / the settings' status line say so) - the mod never picks a resolution in flat mode. Who calls it: vrcam at
  its start (`ReconcileGameWindow`, before the game reads the file) and D2R VR Settings on a switch of the platform and on START
  (`ApplyWindowForPlatform`; VR with the box ticked gets the square written again). A read-only Settings.json has its flag taken off first.
- **The frame cap is VR's too** (`shared/frame_cap.h`, `frame_cap_test`): the headset's rate (`[stereo] headset_rate`) writes the game's own
  `Framerate Cap` in Settings.json, and `pace::Tick` holds the game's presents to it - both VR only (`pace::Tick` returns in flat mode).
  The player's own cap is kept before VR writes its own (`[stereo] cap_before`), what VR wrote last too (`cap_written`: it tells VR's
  cap from one the player set); flat mode puts the kept cap back - on a switch of the platform and on START in D2R VR Settings
  (`ApplyFrameCapForPlatform`, `RestoreFrameCapForFlat`), and at the plugin's start (`ReconcileFrameCap`: also repairs what the game
  writes back at its quit). Nothing kept = nothing written. The settings' "Game frame cap" slider in VR counts as VR's cap.
  What VR writes (`framecap::VrCapFor`, one rule for the settings' write and status line and vrcam's log): none (0) under native OpenXR,
  twice the headset's rate for eyes by turns (afr on, pair_per_tick off: one eye a game frame), the rate itself for one pass two
  pictures (the right eye is replayed: ONE present a pair - `pace::Tick` counts it so, the log's `presents/s` equals `pairs/s`) and for mono.
- **The interface size starts at 80 in VR** (`shared/safe_screen.h`, `safe_screen_test`): the game's `Safe Screen Percent` is 100 by default and at
  100 the interface is cut off at the edges in the headset (a player lowered it to 70 by hand). VR (platform 1, native OpenXR and FlatVR alike) sets
  80 when the file says 100 - any other number is the player's own and stays, an 80 of his included - with `[hud] safe_screen_vr=1` (the box "VR: start
  the interface at 80 %", on by default). The 100 he had is kept in `[hud] safe_screen_before`, what VR wrote in `safe_screen_written`; flat mode (and
  the box off) puts the kept number back, and forgets that VR wrote anything once nothing of it can come back (a game running at the switch writes its
  80 back at its quit - the plugin's start deals with it), so an 80 the player sets in flat later is his. Who calls it: vrcam at its start
  (`ReconcileSafeScreen`, before the game reads the file; `hud::SetInterfaceScale` is read again after a write: the toolbar's cut and the hand's
  globes follow the file's number - `safescreen::Scale`) and D2R VR Settings on a switch of the platform, START and the box (`ApplySafeScreenForPlatform`) -
  only with the game CLOSED (the game reads the file at its start and writes its own copy back at its quit, and the toolbar's cut would follow a
  number a running game does not have). A size picked on the slider is the player's (VR's mark is forgotten; a 100 comes back to 80 at the next VR
  start unless the box is off). No key in the file (a new player's first run: the game writes Settings.json when it quits) = nothing written, the
  log says so; the installer does not touch Settings.json. `hud_rects_test` and `hand_orbs_test` pin the 80 % layouts (toolbar rows 1397..1735 at
  1920, 1490..1851 at 2048; the globes).
  **Why 80 and not less / more** - the numbers of the game's own panel layouts (`data\global\ui\Layouts\_profileHD.json`, `Controller\_profileHD.json`,
  the panels' layouts; read from its archives with `tools\casc_extract`, only the numbers are here): units of a 2160-high screen, `k = H / 2160 * s` pixels
  a unit (measured on a real dump, see `hud_globes.h`), in a frame as wide as the PICTURE: `W / k = aspect * 2160 / s` units. Two side panels at once - a
  trader / the mercenary / a player's trade left, the inventory right - are 1162 units wide each in the PC (mouse) layout, the left at 236 from the frame's
  left edge, the right at 1394 from its right edge; the left's content ends ~1396 in, the inventory's starts 93 into its frame: they meet at a frame
  2697 units wide (`kTwoPanelsUnits`) - a SQUARE picture at `2160 / s = 2697`: **80.09 %**. Above it the panels lie over one another (the outer columns of
  one are under the other); in the controller layout (panels about the middle: the inventory's rect from -8 to +1400 from the middle, its grid's last
  column ending at +1188; the trader's from -1366, its grid at -1204) the picture's half width, `1080 * aspect / s` units, must reach 1204: **89 %** - at
  100 % the inventory's last column (`+1090 .. +1188`) is off the picture, and the cursor cannot go there either. Our own cursor path was checked end to
  end and is not the cause: the laser maps its whole picture to the whole client area, the game's own `ScreenToClient` / `ClientToScreen` calls are mapped
  by `vrres::Coordinate` (first and last pixel to first and last), its cursor clamp and its interface root get the whole picture (`CanvasAnswer`), the menu
  panel shows the whole interface layer. `safescreen::FitPercent(aspect)` is that limit (square 80, the headset's own 2496 x 2688: 74, 16:9: no limit);
  VR starts at `VrValueFor(aspect)` = the smaller of 80 and it, and rewrites its own number (never a player's) when the picture's shape changes; the
  plugin's log (`interface size N%: two open side panels ... fit the picture / are too big`) and the settings' line under the box say whether the size in
  the file fits the picture now.
- D2R VR Settings reads its Settings.json from `D2RVR_GAME_SETTINGS_JSON` when that is set (tests; never the player's file; the game running or not
  is then nobody's concern: `ApplySafeScreenForPlatform` does not wait for it).

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
- `light_bounds_test.exe` tests the light-bounds replacement shader (see above).
- `vr_resolution_test.exe` and `render_pointer_test.exe` test the render size ([render] vr_resolution).
- `window_json_test.exe` tests the game's windowed size in Settings.json and what each platform does to it (flat mode: no square).
- The shared VR mod launcher (`docs/launcher_authentic.md`): `python tools/start_helper_test.py <D2R_VR_Settings.exe> <fake_loader.exe>` tests
  `D2R_VR_Settings.exe --start` in a scratch folder with a fake D2RLoader (the game must be closed); `python tools/make_vrmod_manifest.py --selftest`
  tests the generator of the mod's `vrmod-manifest.json`; `python installer/tests/mod_layout_test.py <short scratch folder>` runs Setup and the
  uninstaller (`installer/mod_layout.inc` and friends) for real, in a scratch folder.
- `frame_cap_test.exe` tests the game's frame cap in Settings.json: VR's rate is VR's, flat mode has the player's own.
- Install your build by copying `d2rl-vrcam.dll` into
  `<game>\mods\D2R-VR\d2rloader\plugins` and `D2R_DepthFog.fx` into the mod's
  `reshade\reshade-shaders\Shaders`, with the game closed (the mod layout: `docs/d2rloader_mod.md`).
