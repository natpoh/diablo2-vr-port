# D2R VR Authentic and the shared VR mod launcher

D2R VR Authentic is the D2RLoader mod `mods\D2R-VR` - THE layout of the regular installer from 0.170 on (see `d2rloader_mod.md`, with the table of where every
file of ours is, and "Upgrading from 0.169 or earlier" there). The shared **D2R VR Launcher** lists the VR mods of a game folder
by scanning `<game>\mods\*\vrmod-manifest.json` and starts one with `D2RLoader.exe -mod <name>`; choosing the mod is the whole switch, it moves no
files. The launcher's side of the contract is its `docs/MOD_MANIFEST.md` (the manifest) and `docs/INSTALLER_CONTRACT.md` (what a mod's Setup does for
the launcher); this is ours.

## What Setup writes for the launcher

Into the mod's own folder, with the rest of the mod, and nowhere else:

```
<game>\mods\D2R-VR\vrmod-manifest.json     schema vrmod-manifest/1 with d2rloader_mod {"name": "D2R-VR"}
<game>\mods\D2R-VR\launcher_icon.png       the crystal-visor icon (256 px), named by the manifest's "icon"
```

- Both are **made, not hand-written**: `tools\make_vrmod_manifest.py` fills `installer\launcher\vrmod-manifest.template.json` (a copy of the
  launcher's example for D2R-VR with `@VERSION@` and `@MOD_FILES@`) with the version of `g_info_version` in `vr\vrcam.cpp` and the list of files
  (below), checks it, and writes it with `installer\launcher\launcher_icon.png`. `installer\build_installer.ps1` runs it last, when the payload is
  staged, and stages the result as `payload\launcher\`; `installer\mod_files.inc` installs it. A test build may show another version
  (`-VersionLabel 0.168.0-launcher-test`, it must start with the plugin's) and a commit and date are added as `build`.
- The checks (nothing is written when one fails): the launcher's JSON Schema (`installer\launcher\vrmod-manifest.1.schema.json`, a copy of the
  launcher repository's plus the `x-mod-files` entry below, checked by a small draft-07 validator inside the tool); what a schema cannot say (a
  D2RLoader mod has no `files` and no `exclusive_group`, the icon is a PNG or ICO next to the manifest); this mod's contract (id `d2r-vr`, folder
  `D2R-VR`, START is `{game}/D2R_VR_Settings.exe --start`, the uninstaller `{game}/unins000.exe`, every path under `{game}`, the D2RLoader archive
  hashes are the two the installer accepts). The launcher's built `manifest_test --validate` accepts the generated file too.
  `python tools\make_vrmod_manifest.py --check <file>` checks an installed manifest, `--verify <game>\mods\D2R-VR` also every file it lists.
- **`x-mod-files`** (an `x-` key: free in the launcher's schema, ignored by its validator): the files Setup puts in the mod's folder, each
  `{ "path": relative to the mod folder with '/', "sha256": of the shipped file, "optional": may be absent, "user_edits": the player's copy may differ }`:
  the plugin, the optional `d2r_vr_weight.dll` and `d2r_vr_grip.dll`, `d2r_vr.ini` (user_edits) and `d2r_vr.default.ini`, the shader, the sky pictures,
  FlatVR's add-on and keepalive (VR only: optional). It is not written by hand: it is `installer\mod_files.inc` - the very `[Files]` lines the installer
  compiles - read against the staged payload, so it is what Setup installs (a library the build did not stage is not listed, and Setup does not ship it).
  The manifest's `logs` name `ReShade.log` twice: `mods\D2R-VR\reshade\` (ReShade's base is the mod's own folder: the plugin sets
  `RESHADE_BASE_PATH_OVERRIDE`, Setup puts ReShade there) and the game's root (the plugin falls back to the game's ReShade when the mod has none, and an
  earlier install keeps it there).
  The manifest and its icon are not in it (a manifest cannot hold its own hash), nor are files made at install time or at run time (`modinfo.json`,
  `d2r_vr_settings.ini`, `d2r_vr_about.json`, logs). `D2R_VR_Settings.exe` lives beside `D2R.exe`, outside the mod, and is not in it.
- **The weight and grip libraries** (`d2r_vr_weight.dll`, `d2r_vr_grip.dll`; private binaries from `C:\wsl\weight_physics`, `build_installer.ps1
  -WeightLibs`) go into **`mods\D2R-VR\d2rloader\d2r_vr_libs\`**, not among the plugins (0.169: D2RLoader took them for plugins and logged "2 plugins
  rejected"). The plugin (`vr\weight_lib.cpp`, `vr\grip_lib.cpp`) looks in `<its folder>\..\d2r_vr_libs` first, beside itself second; D2R VR Authentic Settings
  (`LibFileFound`) the same way from `d2r_vr.ini`'s folder (0.169 left Settings looking only beside the ini, so with the libraries in d2r_vr_libs it said
  "not found" and greyed the ticks out: fixed here). Both with the plugin in `mods\D2R-VR\d2rloader\plugins` land in the mod's `d2rloader\d2r_vr_libs`.
  From an earlier install they are moved there: out of the global `d2rloader\d2r_vr_libs` (0.169) or `d2rloader\plugins` (0.168.0, the leftover is
  removed), and out of the mod's own plugins folder (a 0.168 test build of the mod; a duplicate is removed, what is already in d2r_vr_libs wins). The
  uninstaller removes them with the mod's folder.
- **Setup never writes into another mod's folder** (d2r-pcvr as `mods\D2R-PCVR`) and the uninstaller removes **only `mods\D2R-VR`** (the manifest and
  icon go with it) and the `default_mod` it set. Setup sets `default_mod = "D2R-VR"` only when none was set; another mod's default stays, also
  after our uninstall. bearstonem's older global install (`d2rloader\plugins\d2rl-vrcam.dll` with `d2rloader\pcvr-install.json`) is left as it is.
  Note it stays loaded under every mod, ours included (D2RLoader loads a global plugin always): his installer's move into `mods\D2R-PCVR` solves that.
- The last page of Setup offers **Open D2R VR Launcher** (unchecked, not in a silent run) when the launcher is installed: its own Setup writes
  `HKCU\Software\D2R VR Launcher\InstallDir`; without that the default folder `%LOCALAPPDATA%\Programs\D2R VR Launcher` is looked at.

## The launcher inside Setup

Serg's design (the launcher's `docs/INSTALLER_CONTRACT.md`, case 2): D2R VR Authentic's Setup **carries the launcher's own Setup** and installs the
launcher silently when it is missing or older, then installs only its mod. The player is asked nothing new; the wizard is as before.

- **Carried unmodified and pinned**: `installer\launcher\launcher_pin.txt` names the launcher's Setup that goes in (`version=`, `file=` D2R_VR_Launcher_Setup_<version>.exe,
  `sha256=` as the launcher's release lists it); `build_installer.ps1` looks for that file in `installer\launcher\cache\` (git-ignored; put it there once per release) or
  takes `-LauncherSetup <file>` (another version then needs `-LauncherSetupSha256 <pin>` too). Before anything is staged the script checks the pin (a different file stops
  the build), the name and that the version inside the file is the one in its name. The file is packed with `dontcopy` (`installer\launcher_bundle_files.inc`): it is
  unpacked into Setup's temporary folder only when needed and removed with it. **A build that cannot carry the launcher fails** (no pin file, no file, a wrong hash); only an
  explicit `-NoLauncher` builds without it (a test build, the build output says so twice - never a release). To change the launcher: put the new Setup in `cache\`
  and change the three values.
- **What Setup does** (`installer\launcher_bundle.inc`, `BundleLauncher`, called at the end of the install step after the mod's files are in; also in a silent
  run; never raises, never asks, never blocks the mod):
  1. *Detect*: `HKCU\Software\D2R VR Launcher` values `Version` and `InstallDir`, and `<InstallDir>\D2RVRLauncher.exe`. Our Setup is elevated, but the launcher is
     per user: the question is asked by a small batch file run **as the original user** (`ExecAsOriginalUser`; `reg query` and `if exist` in one console code
     page, so a folder with Cyrillic letters is found), which answers `version=` / `exe=` in ASCII. Where that cannot be started, Setup's own user's registry
     is read.
  2. *Compare*: dotted numbers, up to four parts, a missing part is 0 (`0.2` = `0.2.0.0`; `0.10.0` is newer than `0.2.0`). Only a launcher that is missing, whose exe is
     missing or whose version is older gets the carried Setup; the same or a newer one is left alone - never a downgrade.
  3. *Install*: the carried Setup **as the original user** (a per-user program: it lands in the player's profile, not the administrator's):
     `D2R_VR_Launcher_Setup_<version>.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS=` (no desktop shortcut). Its exit code is written to Setup's log and nothing
     else (its own Setup stops with code 1 without touching anything when a newer launcher is installed).
- **The uninstaller leaves the launcher** and its registry key (the other mod, or the player, may use it); it removes only `mods\D2R-VR`.
- A launcher whose Setup started ours (its install view) is already installed with the same version: nothing is run.

The plugin and Settings carry the identity the launcher and a support log read: plugin id `d2r-vr-vrcam` (the log file, the plugin's records are keyed
by it), shown name `D2R VR`, and a VERSIONINFO (`vr\vrcam.rc`, `tools\settings\settings.rc`: product `D2R VR`, internal name `d2r-vr` /
`d2r-vr-settings`, comments `mod-id=d2r-vr edition=full`; `build_installer.ps1` stops on a build without it). The plugin logs one line saying which
mod it is, in which layout (D2RLoader mod / global plugin) and who started it, and writes `d2r_vr_about.json` beside `d2r_vr.ini`.

## `D2R_VR_Settings.exe --start`

The START button without the window; the launcher's Play runs it (`launch.options[0]` of the manifest) with `XR_RUNTIME_JSON` and `VRMOD_*` in the
environment, which `D2RLoader.exe` and the game inherit.

- Reads `[mode] platform` from `d2r_vr.ini` (`mods\D2R-VR\d2rloader\plugins`, else the global plugins folder of an earlier install), sets the game's
  window, frame cap and interface size for it (the game's `Settings.json`, as the button does), starts **`D2RLoader.exe -mod D2R-VR`** from the game's folder
  (an install from before the mod layout: without `-mod`, as it always was), in VR also BodyWalk (not with native OpenXR and "Controls without BodyWalk")
  and asks FlatVR to start once BodyWalk's Game Link is up (waits up to a minute for that; native OpenXR returns at once). It does not wait for the game
  to end, opens no window and no message box.
- Exit code: **0** started, **1** no `d2r_vr.ini`, **2** no `D2RLoader.exe`, **3** `D2RLoader.exe` could not be started, **4** the game runs already.
- It says what it does on stdout (a pipe or a console, when there is one) and in `<game>\mods\D2R-VR\d2rloader\logs\d2r_vr_launch.txt` (`d2rloader\logs` of the game for an earlier install; appended, one
  time-stamped line per stdout line): its version, the ini it read, who started it, what it started, the exit reason.

## Tests

```
python tools\make_vrmod_manifest.py --selftest                  the generator: schema, contract, x-mod-files, settings_app.label, refusals (49 checks)
python tools\start_helper_test.py <D2R_VR_Settings.exe> <fake_loader.exe> [scratch]    --start with a fake D2RLoader, --prepare-assets on the mod's folders, --reshade-paths on a dxgi.dll ReShade's ini (75 checks)
python installer\tests\mod_layout_test.py <short scratch folder> --standin <fake_launcher_setup.exe> --standin-fail <fake_launcher_setup_fail.exe>
        [--payload169 <0.169's staged payload>] [--real-game <a real game folder, read only>] [--settings-exe <the real D2R_VR_Settings.exe>]
                                                                 Setup + uninstaller for real, in a scratch folder, the carried launcher included (237 checks; 295 with both real fixtures; section 2d - the player's dxgi.dll ReShade through Setup and the uninstaller - needs --settings-exe)
mod_layout_test.exe, window_json_test.exe, weight_lib_test.exe, grip_lib_test.exe, bw_path_test.exe, bw_output_test.exe, safe_screen_test.exe, frame_cap_test.exe,
reshade_env_test.exe (the order: inherited RESHADE_* cleared, then ours set; and a dxgi.dll beside the game is joined, ReShade64.dll not loaded as well),
reshade_as_test.exe (the Home buttons for a dxgi.dll: only ever a rename to dxgi.dll.off and back), reshade_paths_test.exe (D2R VR's two folders in a ReShade.ini: append-only, byte-exact),
arcane_assets_test.exe <game folder> <empty scratch> (the Arcane sky maker on the mod's folders; reads the game read only)
```

Not automatic: `tools\reshade_probe\run_probe.py` lets a REAL ReShade 6.8 (as a dxgi.dll in a scratch folder) initialise on a small D3D11 program and reads its log - the evidence
behind "A dxgi.dll next to D2R.exe" in `d2rloader_mod.md`.

- `start_helper_test.py` runs the real Settings exe in a scratch "game folder" with `fake_loader.exe` (`tools\settings\fake_loader.cpp`) as
  `D2RLoader.exe`: the exit codes, `-mod D2R-VR`, the environment the child gets, stdout = the launch log, no window, nothing written but the log.
  Only the VR + native OpenXR + "Controls without BodyWalk" path runs (no BodyWalk is started). START also sets the game's interface size in `Settings.json`
  (0.168): the test gives Settings a file of its own (`D2RVR_GAME_SETTINGS_JSON`), checks that VR changes that file, and that the player's own
  `Saved Games\Diablo II Resurrected\Settings.json` is the same before and after. The game must be closed.
- `installer\tests\mod_layout_test.iss` is a stand-in for Setup that includes the real `mod_names.inc`, `mod_files.inc` (the `[Files]` lines),
  `launcher_bundle_files.inc`, `mod_layout.inc` (MoveEarlierInstall, modinfo.json, default_mod, ReShade placement, RemoveMod, launcher lookup),
  `launcher_bundle.inc` and `ini_choice.inc`, and calls them in the real order; `CreateUninstallRegKey=no`, `PrivilegesRequired=lowest`, silent, nothing
  outside the scratch folder. It checks: a clean game (VR and flat; the libraries in the mod, beside the plugin; `x-mod-files` is exactly what was installed
  and `--verify` passes; the arcane-sky folders `D2R-VR.mpq\data` and `reshade\reshade-shaders\Textures\D2R_Arcane` go with the mod); an earlier global install (moved into the mod, the player's `d2r_vr.ini` byte for byte, the libraries too, ReShade only when it was
  ours, what is in the mod already wins); a build that ships no libraries; `mods\D2R-PCVR` next to it (every file hashed before and after Setup and after
  the uninstall); bearstonem's legacy global files (hashed the same way); an update (the player's ini kept, new keys added; an unchanged ini takes the
  new defaults; the manifest and libraries are the new ones); the uninstaller (exactly what was there before, `default_mod` back, a default that is not ours stays).
- Sections 9 and 10 are the updates from a REAL layout, not one built by hand. **9**: the game folder is built by replaying the `[Files]` lines of the 0.169 Setup script
  (`installer\tests\fixtures\D2R_VR_Setup_0.169.iss` = master d919f8b's script; `installer\tests\fixture_0169.py`) with the real 0.169 payload files, then what the program
  wrote while running (the player's tuned ini and its backups, ReShade as the button leaves it, dumps, logs) - plus d2r-pcvr's `mods\D2R-PCVR` (hashed before / after Setup and
  after the uninstall). Checks: the ini and every backup byte for byte in the mod, the libraries, ReShade (ours), the add-on, the sky, BodyWalk Portable, no plugin left globally,
  D2RLoader's files and `d2rloader.toml` (only `default_mod` changes) untouched, nothing of ours left in the game folder, the uninstaller. **10**: a copy of the small files of
  a real, long-used folder (read only; the 0.168-era global install of the author plus weeks of test builds: 69 `d2r_vr.ini.*` backups, 251 skill icons, 33 backups of the effect,
  dev backups of the plugin, d2r-pcvr's empty folders): what moved, what stayed (the plugin's dev backups, the `uitex` dumps, the effect's backups, the Settings program's backups) and
  what the uninstaller leaves.
- Section 8 of it is the launcher inside Setup. The carried Setup is a stand-in (`installer\tests\fake_launcher_setup.cpp`; `_fail` exits with 5; a third
  installer carries a file that is not a program) that records how it was started and, like the real Setup, leaves `Version` / `InstallDir` and an exe behind.
  The registry is a throw-away key `HKCU\Software\D2R VR Launcher Test <pid>` (the test installer refuses the launcher's own key), deleted at the end. Cases:
  nothing installed (the stand-in runs with exactly `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /TASKS=`; a second Setup then leaves it); older versions
  (`0.1.0`, `0.1.9.9`, `0.1`, an unreadable one) run it; the same or a newer one (`0.2`, `0.2.0.0`, `0.2.0.1`, `0.2.1`, `0.10.0`, `1.0`, `10.0.0`) does not; a
  version without its exe, an exe without a version; a folder with spaces, brackets and Cyrillic letters; the carried Setup exiting with 5 or not startable: the
  mod is installed all the same; a Setup that carries none; flat mode; the mod's uninstaller leaves the launcher's key and exe.
- The real `D2R_VR_Setup.iss` is compiled by `installer\build_installer.ps1` (also with `-OutputName` / `-VersionLabel` / `-ReleasesDir` /
  `-BuildDir` for a test build that must not touch `<main checkout>\releases`; the default is `D2R_VR_Setup_v<version>.exe`). It asks for administrator rights like every D2R VR Setup, so it is not run
  silently by the tests; the elevated case (the launcher landing in the original user's profile) is therefore not exercised, only the call that makes it so.

## The Arcane Sanctuary's sky in this layout (`claude/arcane-sky`, merged into 0.170)

That branch (the Arcane Sanctuary's own sky) was written for the global layout; merged here, three places changed, nothing else (done, and tested: `cleanroom/tests/arcane_assets_test.cpp`
on the mod's folders, `start_helper_test.py` section 10 for `--prepare-assets`, `mod_layout_test.py` sections 1 and 2 for Setup / the uninstaller / the move of an earlier folder):

- `tools/settings/arcane_assets.cpp`, `Prepare(game, reshadeFolder, modRoot)`: the pictures go to `<ReShade's folder>\reshade-shaders\Textures\D2R_Arcane` - Settings' `ReShadeFolder()` (the mod's
  `reshade\` when the mod has its own ReShade64.dll, else the game's folder; `kNotHere` tests that `Textures` folder exists, and Setup has made it by then) - not
  `<game>\reshade-shaders\Textures\D2R_Arcane`; the level's visual files into `ModRoot(game) + "D2R-VR.mpq\data\..."` (the `mods\D2R-VR\D2R-VR.mpq` it looks for is the folder Setup's
  `WriteModInfo` makes, so `modFolder` is true in the mod layout and false only in an install that is not the mod). Pass the two folders in (`Prepare(game, reshadeFolder, modRoot)`), resolved the
  way `ModInstall()` / `ReShadeFolder()` resolve them.
- `settings.cpp` `--prepare-assets` (and the Home button "Make the Arcane Sanctuary sky now"): they read `[sky] arcane_sky` from `FindIni()`'s `g_ini` (the mod's ini), not from the hard-coded
  `GameFolder() + "d2rloader\plugins\d2r_vr.ini"` of the branch, and use `PrepareArcane()` = `Prepare(game, ReShadeFolder(), ModInstall() ? ModRoot(game) : "")`.
- the `.iss` hunk: `PrepareAssets(Dir)` stays where it is (after `ApplyIni`, when the mod's folders and ini exist; `D2R_VR_Settings.exe --prepare-assets` is beside `D2R.exe`);
  the branch's `[UninstallDelete] ...\reshade-shaders\Textures\D2R_Arcane` is dropped (inside the mod's folder, `RemoveMod` takes it, with `D2R-VR.mpq\data\...`), and
  `reshade-shaders\Textures\D2R_Arcane` is in `MoveEarlierInstall` (a tester's folder from a build of that branch lies in the game's `reshade-shaders` and would stay otherwise). The plugin
  needs nothing: `SkyFileExists(L"D2R_Arcane\stars.png")` already looks through `ReShadeDir()`.
- CascLib (MIT; Settings only, never the plugin) comes from `third_party/CascLib` or, when that is not there, is fetched at the commit `tools/casc_extract` was built with
  (`-DFETCHCONTENT_SOURCE_DIR_CASCLIB=<a checkout>` makes the build offline).
