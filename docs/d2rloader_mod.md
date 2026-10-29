# D2R VR as a D2RLoader mod (D2R-VR)

D2R VR is a **D2RLoader mod**: everything of it is in one folder, `<game>\mods\D2R-VR`, and the mod is switched on by starting D2RLoader with it
(`D2RLoader.exe -mod D2R-VR`, or `[d2rloader] default_mod` in `d2rloader\config\d2rloader.toml`). This is THE layout of the regular installer from
0.170 on; the global layout of 0.169 and older (the plugin in `d2rloader\plugins`, ReShade beside `D2R.exe`) is only what Setup moves from.

Shown to players as **D2R VR Authentic** - "Diablo II as it is, in VR: the game's own rules, your body and hands."
(bearstonem's d2r-pcvr: **D2R VR Unleashed**, "Diablo II reimagined for physical, immersive VR", the mod `D2R-PCVR`). The ids stay: the mod
folder `D2R-VR`, `d2rl-vrcam.dll`, `d2r_vr.ini` (his: `D2R-PCVR`, `d2rl-pcvr.dll`, `d2r_pcvr.ini`). The installer's
name, Installed apps, the shortcuts ("D2R VR Authentic" runs `D2RLoader.exe -mod D2R-VR`) and `modinfo.json`'s
`description` carry the display name; `modinfo.json`'s `name` is the id (D2R and D2RLoader find the mod by it).

D2RLoader loads **global** plugins from `<game>\d2rloader\plugins` always, and a **mod's** plugins from
`<game>\mods\<mod>\d2rloader\plugins` only while that mod is active (`allow_mod_extensions = true`, the default). Mod plugins load first, and a mod
plugin replaces a global one of the same id while its mod is active (the SDK's README) - so bearstonem's older global `d2rl-vrcam.dll`, which has our plugin's
id, does not run beside ours. Up to 0.169 D2R VR was a global plugin with ReShade beside D2R.exe - and d2r-pcvr (up to 0.3.15) used the same file
names, so either installer replaced the other. As the mod **D2R-VR** nothing of it is in another mod's way.

## Where everything is (old = 0.169 and older, new = the mod)

Who reads and writes it: **P** the plugin, **S** D2R VR Authentic Settings, **I** Setup, **L** the shared launcher, **D** D2RLoader / the game, **R** ReShade.

| What | Old | New (under `<game>\mods\D2R-VR\`) | Read / written by |
|---|---|---|---|
| the plugin | `d2rloader\plugins\d2rl-vrcam.dll` | `d2rloader\plugins\d2rl-vrcam.dll` | D (only while the mod is active); I |
| settings | `d2rloader\plugins\d2r_vr.ini` | `d2rloader\plugins\d2r_vr.ini` | P (reloads it every 2 s), S (`FindIni`), I |
| the shipped defaults, the ini's backups | `d2r_vr.default.ini`, `d2r_vr.ini.before-<ver>`, the player's own `d2r_vr.ini.*` | `d2rloader\plugins\` | I (IniChoice; the backups move with the ini) |
| Settings' window file | `d2r_vr_settings.ini` beside the ini | `d2rloader\plugins\` | S |
| headset / depth status | `d2r_vr_headset.ini`, `d2r_vr_depth.ini` beside the ini | `d2rloader\plugins\` | P writes, S reads |
| identity for the launcher / a support report | `d2r_vr_about.json` | `d2rloader\plugins\` | P writes, L / support read |
| the game's addresses (Status tab) | `d2r_vr_game_code.txt` | `d2rloader\plugins\` | P writes, S reads |
| diagnostics (debug keys) | `d2r_vr_frames.csv`, `_uitrace.txt`, `_herofind.txt`, `_skeletons.txt`, `_matrix_writers.csv`, `_memdiff.txt`, `d2r_vr_openxr_starting.txt` | `d2rloader\plugins\` | P |
| ReShade is ours (mark) | `d2r_vr_reshade_ours.txt` | `d2rloader\plugins\` | S (Install ReShade), I |
| skill-icon cache | `d2rloader\plugins\d2r_vr_skill_icons\` | `d2rloader\plugins\d2r_vr_skill_icons\` | P (`wheel_icons`: the plugin's folder + `d2r_vr_skill_icons`) |
| weight and grip libraries (optional) | `d2rloader\d2r_vr_libs\` (0.169), `d2rloader\plugins\` (0.168.0) | `d2rloader\d2r_vr_libs\` | P (`<plugin folder>\..\d2r_vr_libs` first, beside itself second), S (`LibFileFound`) |
| the plugin's own logs and crash files | `d2rloader\logs\d2r_vr_start.txt`, `_crash.txt/.dmp`, `_hang.txt`, `_gpu_fault.txt`, `_profile.txt`, `_replaytrace.txt`, `_cbdump.bin` | `d2rloader\logs\` (the `..\logs` of the plugin's folder) | P |
| Settings' launch log (`--start`) | `d2rloader\logs\d2r_vr_launch.txt` | `d2rloader\logs\d2r_vr_launch.txt` | S |
| D2RLoader's log of the plugin | `d2rloader\logs\d2r-vr-vrcam.log` | wherever D2RLoader writes it (not ours to move; the manifest names both) | D |
| ReShade, its ini, presets, log | the game's root | `reshade\ReShade64.dll`, `ReShade.ini`, `ReShadePreset.ini`, `ReShade.log` | P loads it and sets `RESHADE_BASE_PATH_OVERRIDE` to this folder; R; S; I |
| the fog / sky shader | `reshade-shaders\Shaders\D2R_DepthFog.fx` | `reshade\reshade-shaders\Shaders\` | R |
| sky, ceiling and crosshair pictures | `reshade-shaders\Textures\D2R_Sky_ours\` | `reshade\reshade-shaders\Textures\D2R_Sky_ours\` | P (`SkyFileExists`, `ReShadeDir`), R |
| the Arcane Sanctuary's sky pictures (`stars.png`, `clouds.png`, `made.json`) | `reshade-shaders\Textures\D2R_Arcane\` (a test build only) | `reshade\reshade-shaders\Textures\D2R_Arcane\` | S makes them from the player's own game (`--prepare-assets`, the Home button, at its start), P (`SkyFileExists`), R |
| the Arcane Sanctuary's visual files without the flat star cards | - | `D2R-VR.mpq\data\hd\env\vis\act2_arcane_*.json` (7) | S writes them (a file the player changed stays), D reads them |
| FlatVR's add-on, its keepalive effect, its log | the game's root | `reshade\FlatVR_DepthProvider.addon64`, `FlatVR_Keepalive.addonfx`, `FlatVR_DepthProvider.log` (it logs beside itself) | R loads it; FlatVR |
| BodyWalk Portable (VR, no BodyWalk installed) | `<game>\BodyWalkVR\` | `BodyWalkVR\` | S (`BodyWalkExe`: the newest it finds), I |
| the licence | `<game>\D2R_VR_LICENSE.txt` | `D2R_VR_LICENSE.txt` | - |
| for D2RLoader / the game | - | `D2R-VR.mpq\modinfo.json` | D |
| the player's other data mods (D2RMM's output), merged in | - | `D2R-VR.mpq\data\...` (what `data_mods.txt` names), D2R VR's own files they replace in `data_mods_ours\`, the log `d2rloader\logs\d2r_vr_data_mods.txt` | S (`[mods] merge_data_mods`, below), D reads them |
| for the shared launcher | - | `vrmod-manifest.json`, `launcher_icon.png` | L |
| Collect logs (Settings) | the game's root and `d2rloader` | the mod's plugin, logs and reshade folders, D2RLoader's own logs | S |

Not in the mod's folder, on purpose: `D2R_VR_Settings.exe` beside `D2R.exe` (the manifest's START and Settings button name it there); D2RLoader's own files
(`d2rloader\config\d2rloader.toml` - Setup sets `allow_global_extensions`, and `default_mod = "D2R-VR"` when no mod was the default - `d2rloader\logs`,
`d2rloader\crashes`); the game's `Settings.json` in Saved Games; BodyWalk's files in `%LOCALAPPDATA%\BodyWalkVR` (Game Link plugin, manifest, profile, settings);
a transient `d2r_vr_write_probe.tmp` in the game's root while the Install ReShade button checks the folder is writable (deleted at once).

## How the pieces find each other

- **The plugin** takes its own folder (`d2rl-vrcam.dll`'s) as the anchor: `d2r_vr.ini` and every file beside it, `..\logs` and `..\d2r_vr_libs` from it. Nothing
  assumes the folder is the global one. **ReShade is one per process**: the plugin loads the mod's own `reshade\ReShade64.dll` and sets
  `RESHADE_BASE_PATH_OVERRIDE` to that folder first, so ReShade's ini, presets, shaders, textures and add-ons are the mod's (vr/vrcam.cpp `ReShadeDir`,
  `LoadReShade`). Without one there it takes the game folder's, as before. **Another mod's `RESHADE_*` variables are cleared first** (`shared/reshade_env.h`:
  d2r-pcvr's `RESHADE_BASE_PATH_OVERRIDE` sent our ReShade to its folder and it refused to start): `reshadeenv::PrepareProcess` clears every inherited `RESHADE_*`
  variable of the game's process and only then sets ours - the order is pinned by `tools/reshade_env_test.cpp`.
- **A `dxgi.dll` next to `D2R.exe` is the player's own ReShade, and D2R VR uses it as it is** - Setup, the uninstaller and the Home buttons never rename, move, replace or
  delete it (the old rule, "a dxgi.dll becomes ReShade64.dll or goes", and `[reshade] as_dxgi` are gone; an old ini that has the key is read and ignored). The game loads such a
  ReShade itself, at process start (before D2RLoader loads our plugin), with the **game's folder as its base**: its `ReShade.ini`, `reshade-shaders` and add-on folder
  are the game's. D2R VR's files are in `mods\D2R-VR\reshade`. What a real ReShade 6.8.0 does with them was measured with a probe program (`tools/reshade_probe/`: a D3D11 test
  exe that lets the ReShade initialise, a test add-on, and ReShade's own log):
  - **files only in the mod's folder are not found** (no effect, no add-on, no texture);
  - **two list items in his `ReShade.ini` are enough for the effect and the pictures**: `EffectSearchPaths` and `TextureSearchPaths` (the `[GENERAL]` lists, comma separated) get
    `.\mods\D2R-VR\reshade\reshade-shaders\Shaders\**` and `...\Textures\**` after his own (an absolute path works too; ReShade keeps them when it rewrites the file). With the
    first only, a texture of the effect is "not found in any of the texture search paths" - both are needed. The real `D2R_DepthFog.fx` compiles through that path;
  - **the add-on cannot be added that way** (ReShade has ONE add-on folder, `[ADDON] AddonPath`, default the base; moving it would hide the player's own add-ons), so **the
    plugin loads `FlatVR_DepthProvider.addon64` itself from the mod's folder** when it joins a dxgi.dll ReShade (`LoadOurAddonForDxgiReShade`): an add-on that registers
    itself before the game makes its device is picked up like one in ReShade's folder (probed with a test add-on loaded from another folder);
  - the ways to move ReShade's whole base instead - `[INSTALL] BasePath` in the ini next to the exe, or `RESHADE_BASE_PATH_OVERRIDE` in the process environment (ReShade's source,
    `get_base_path`: that ini first, then the variable, then the dll's folder) - work, but then ITS `ReShade.ini` and presets are not used any more: the player's own setup
    would be gone from the normal game too (the ini variant) or only for starts through our START (the variable). Neither is used.
  D2RLoader and its SDK know nothing of ReShade (no string in `D2RLoader.exe`, `D2RCore.dll`, the SDK), so no base path comes from there.
  So Setup runs `D2R_VR_Settings.exe --reshade-paths add` at its end (`shared/reshade_paths.h`: append-only, once, idempotent; a copy of his file as `ReShade.ini.before_d2rvr`; the
  ini ReShade really reads - `[INSTALL] BasePath` is followed; refused while the game runs, because ReShade writes its ini back when the game ends; the Home button
  *Add D2R VR's folders to my ReShade* does the same later), and the uninstaller runs `--reshade-paths remove` (his file comes back byte for byte). The plugin returns before it loads
  ReShade64.dll when a dxgi.dll is there - a second ReShade in the process refuses to start (error 1114) - and looks for the pictures in the mod's folder (`OurFilesDir`).
  With no dxgi.dll D2R VR has its own ReShade: *Install ReShade* puts `ReShade64.dll` in `mods\D2R-VR\reshade` (the plain `D2R.exe` refuses an unsigned dxgi.dll - the
  reason it is renamed), the plugin loads it with that folder as its base. The one explicit choice left is a Home button that puts his dxgi.dll aside as `dxgi.dll.off` (after
  a question; nothing deleted or replaced), so that button can be offered; *Bring my dxgi.dll back* undoes it.
- **D2R VR Authentic Settings** finds `d2r_vr.ini` next to itself (the oldest place), then in `mods\D2R-VR\d2rloader\plugins`, then in `d2rloader\plugins` (an install from
  before the mod layout); everything else follows from where the ini is (`ModInstall`, `ReShadeFolder`, `LibFileFound`). START runs `D2RLoader.exe -mod D2R-VR`.
- **D2R VR Setup** installs there, writes `modinfo.json`, sets `default_mod = "D2R-VR"` when no mod was the default (a plain `D2RLoader.exe` start runs D2R VR; another
  mod's launcher names its own mod), and moves an earlier install once (below). It never writes into another mod's folder. The uninstaller removes the mod's folder
  whole (`RemoveMod`) and the `default_mod` it set, and leaves the launcher installed.
- **The shared VR mod launcher** finds the mod by `mods\D2R-VR\vrmod-manifest.json` (+ `launcher_icon.png`); Play runs `D2R_VR_Settings.exe --start`
  (= `D2RLoader.exe -mod D2R-VR`). See `launcher_authentic.md`.
- **The other mod** (d2r-pcvr as `mods\D2R-PCVR`, its own plugin name `d2rl-pcvr.dll` and `d2r_pcvr.ini`) does the same on its side; then the choice of mod in
  D2RLoader is the whole switch.

## Other data mods with D2R VR (D2RMM)

D2R loads ONE `-mod`, and D2RLoader 1.3.1 does not layer mods: one active mod (`-mod`, `default_mod`) and below it only the loose `-direct` data root
(its D2RCore.dll strings: "remount the -direct data root below the active mod overlay"; no docs in the game folder say more). So the mod D2RMM builds -
olegbl's D2R Mod Manager writes `<game>\mods\<name>\<name>.mpq\data\...` and `modinfo.json` (`<name>` = its "Output Mod Name", default `D2RMM`, kept in
its own browser storage) and starts the game as `-mod D2RMM -txt` - is not loaded while D2R VR is the mod. Pointing D2RMM's output at `D2R-VR` is no way
out: **D2RMM deletes `<game>\mods\<name>\<name>.mpq` whole before every build** (src/main/worker/BridgeAPI.ts), with our own files in it.

So D2R VR Authentic Settings merges them (`shared/data_mods.h`, `[mods] merge_data_mods`, on; Home > Other mods): at START, at `--start` (the launcher's
Play), when it opens and when the tick changes - never while the game runs - it copies the `data\` files of every other folder mod in `<game>\mods` into
`D2R-VR.mpq\data` and keeps a record of each (`data_mods.txt`: the file, the mod, its size and time, ours as we left it). The next run compares: a file
D2RMM built again is copied again, one it dropped is taken out, everything goes when the tick is off. Only what the record names is removed, and only while
it is still as we left it.

- **Not merged:** another VR mod (a `vrmod-manifest.json`: D2R-PCVR's data is for its own plugin), a mod with D2RLoader plugins or patches of its own
  (they load only when it is started: its data alone would be half of it), a packed `.mpq` (no MPQ reader here), the names in `[mods] merge_skip`, and
  D2RMM's `data\global\dataversionbuild.txt` (the game's build stamp, not the mod).
- **The same file in two mods:** D2RMM first, then by name; the first keeps it (logged).
- **The same file in D2R VR's mod** (the Arcane Sanctuary's visual files, the Kurast Docks' preset - this program's own): D2R VR's one change is made on
  top of theirs where it is known (`datamods::OurChange`: the canoe on the water, the flat star cards out), theirs as it is when theirs is not as known;
  any other file of ours stays ours. Ours is moved to `data_mods_ours\` meanwhile and put back when their file goes.
- **Saves:** D2RMM's `modinfo.json` names its own save folder (`mods\D2RMM` by default); D2R VR's `savepath "../"` stays - the characters are the ones
  played with D2R VR. Said in the log and on Home.
- Every choice is written to `d2rloader\logs\d2r_vr_data_mods.txt` (again at each run), the launch's lines to `d2r_vr_launch.txt`.
- A plain `D2RLoader.exe` start (its `default_mod`) runs the mod as the last START left it: D2RMM built since is taken at the next START.

`tools/data_mods_test.cpp` runs the merge on a made-up game folder; `tools/start_helper_test.py` case 12 runs `--start` with a D2RMM mod.

## Upgrading from 0.169 or earlier

Setup moves what is ours once, before the new files go in (`MoveEarlierInstall`, `MoveLibsIntoMod`, `MovePortableIntoMod` in `installer\mod_layout.inc`), and
only when the global `d2rl-vrcam.dll` is ours (a d2r-pcvr install record, `d2rloader\pcvr-install.json`, there: everything is left alone):

- into `mods\D2R-VR\d2rloader\plugins`: `d2r_vr.ini` (byte for byte - Setup then only adds the keys the new version has) and **every other file whose name starts with
  `d2r_vr`**: its backups (`d2r_vr.ini.before-<ver>`, the player's own `d2r_vr.ini.before_<what>`), the defaults, the headset / depth / window inis, the dumps, the skill icons.
  Folders and `.dll` / `.exe` files are not part of this; d2r-pcvr's `d2r_vr_icons` and `d2r_vr_potions` folders stay;
- into `d2rloader\d2r_vr_libs` of the mod: the weight and grip libraries from `d2rloader\d2r_vr_libs` (0.169) or `d2rloader\plugins` (0.168.0, where the leftover is removed);
- into the mod's `reshade\`: ReShade, its ini, presets, log and ini backups - only when `d2r_vr_reshade_ours.txt` says it was ours (a player's own ReShade stays beside
  the game) - and FlatVR's add-on with its log and backups, its keepalive effect, the shader, the sky pictures (and `D2R_Arcane`, if a test build made it there); the game's
  `reshade-shaders` folders that are empty now are removed;
- `BodyWalkVR\` (BodyWalk Portable, when its `portable.txt` is there) into the mod's folder; the player's LITE / PRO choice in it stays;
- the global `d2rl-vrcam.dll` is deleted (the launcher warns about a leftover), `D2R_VR_LICENSE.txt` beside `D2R.exe` is deleted (the mod has it).

What is not ours by name stays where it is: the plugin's dev backups (`d2rl-vrcam.dll.<what>`: D2RLoader does not load them), the `uitex_*.raw` dumps, the
`D2R_DepthFog.fx.before_*` backups of the effect, `D2R_VR_Settings.exe.before_*` beside the program. What is in the mod already wins over what an earlier install left.
`installer\tests\mod_layout_test.py` runs this on a game folder derived from the 0.169 Setup script and on a copy of the small files of a real, long-used folder.

Checked in the game (2026-10-16, D2RLoader 1.3.1-beta, Steam 3.3.93787): a folder `mods\D2R-Test\D2R-Test.mpq\` holding only
`modinfo.json` `{ "name": "D2R-Test", "savepath": "../" }`, started as `D2RLoader.exe -mod D2R-Test`. D2RLoader's log: "Active mod:
name D2R-Test, source folder .mpq, mod build null, modinfo.savepath ../, D2RLoader Integration not found"; the global plugin loaded
as before; the game used the usual save folder (`Saved Games\Diablo II Resurrected`) and the usual character loaded. Two notes:
D2RLoader's automatic character backups are skipped with `savepath "../"` (it expects a mod save folder under `...\Mods`; the
launch goes on), and "D2RLoader Integration" means the mod's `d2rloader\` folder (`d2rloader\config\mod.toml`, plugins), which
D2R VR's install has. Its console's `init` also writes `<mod>.mpq\data\global\dataversionbuild.txt` (the "mod build") and
`d2rloader\config\mod.toml`; neither is needed to run. **Not checked yet in the game:** the plugin loaded from the mod's own `d2rloader\plugins`
(what D2RLoader logs for it, and where it writes `d2r-vr-vrcam.log`), ReShade started with `mods\D2R-VR\reshade` as its base (its log, the effect and the sky loading),
FlatVR's add-on loaded from there, and the libraries found in `d2r_vr_libs` - all of it is the code path of this layout, run only in tests so far. Code: `shared/mod_layout.h`
(+ `cleanroom/tests/mod_layout_test.cpp`).
