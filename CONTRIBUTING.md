# Contributing to D2R VR Authentic

Thank you for wanting to help. Bug reports, ideas, fixes, translations and documentation are all welcome.

## How this repository works

This public repository is **made from our development repository at every release**: each build replaces `main` with the
new source. A pull request is therefore **not merged on GitHub**. When we accept it, we bring your change into our
repository by hand, credit you in the commit, in `CHANGELOG.md` and in the release notes, and it appears here with the next
build; then we close the pull request with a link to that release. Please do not be surprised by the closed state - it is
how an accepted change looks here.

## Before you start

- **Look first**: [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md), [docs/ROADMAP.md](docs/ROADMAP.md), the open issues and pull
  requests, and our Discord (the link is in the README).
- **Ask before a big change**: a new feature, a new dependency, another game build, the installer or the co-op code. Open an
  issue (or ask on Discord) first - we may already be working on it, or know why it is hard.
- Small fixes, typos and documentation can go straight to a pull request.
- Be kind and specific. Disagreements are fine; explain them with an example.

## Fork, branch, pull request

1. Fork [natpoh/diablo2-vr-port](https://github.com/natpoh/diablo2-vr-port) and clone your fork.
2. Branch from the newest `main`:

   ```sh
   git remote add upstream https://github.com/natpoh/diablo2-vr-port.git
   git fetch upstream
   git switch -c fix/what-you-change upstream/main
   ```

3. Make one focused change. Leave out unrelated clean-ups, reformatting and generated files.
4. Build it and run the tests of what you changed (below). Say what you ran.
5. Push the branch to your fork and open a pull request against `natpoh/diablo2-vr-port:main`. Fill in the template. A draft
   pull request is fine for work in progress.

## Build and test

Windows, Visual Studio 2022 or newer with its CMake:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The README's "Building" section says what the source needs that is not in this repository (the Game Link SDK header) and how
to point CMake at it (`-DBODYWALK_SDK_INCLUDE=<folder>`).

- The build makes the plugin (`d2rl-vrcam.dll`), the settings program (`D2R_VR_Settings.exe`) and many `*_test.exe` in
  `build\Release`. Run the tests that cover the files you touched; each prints its checks and exits 0 when all pass.
- Installer changes: `python installer\tests\mod_layout_test.py <empty scratch folder> --standin <fake_launcher_setup.exe>
  --standin-fail <fake_launcher_setup_fail.exe>` (the stand-ins are built from `installer\tests\fake_launcher_setup.cpp`).
  It never touches your game.
- Settings texts: new words in the settings program go through `TR()`; then run
  `python tools\lang_sync.py en --exe build\Release\D2R_VR_Settings.exe`. Write English only - the other languages are
  translated together before a release.
- If you could not run a check, say so in the pull request rather than leaving it out.

**In-game testing.** Tell us the headset, the runtime (SteamVR, Oculus/Meta, Virtual Desktop, ALVR...), the view (F1 - F4,
or the flat game on the monitor), the class and weapon, single player or co-op, and the mod version. Say clearly whether you
tried it in the headset or only built it. Test with offline characters (single player or TCP/IP co-op) - never in online
Battle.net games.

## How we want changes made

- **Never change the game's rules behind its back.** The mod steers the game's own commands (its attack, walk, pick-up,
  skills); it does not call the game's server to hit, move or give items directly. What the mod draws differently (bodies,
  ragdolls, corpses, the sky) is drawing only. The rare exception is a clearly marked, optional setting in Settings >
  Strikes > Attention.
- **Every new feature has its own setting** - an ini key with a control in Settings - and with it off (or the key missing)
  everything works as before.
- **Points and lines come from the game's own model** (bones, helpers, mesh bounds), not from hand-tuned offsets; a slider is
  the fallback, not the method.
- Debug switches (`[debug]`) may only draw or log; they ship off.
- Keep the surrounding code's style: its naming, comments and idioms.

## Files, licences and player data

- You need your own copy of Diablo II: Resurrected to test. Do not commit or attach game files: executables, archives,
  models, textures, sounds or anything extracted from them, nor memory dumps or character saves.
- Do not commit build output, downloaded tools, installers, passwords, tokens or anyone's private data. Read logs before you
  post them: they can hold your Windows user name, paths and character names.
- D2R VR is MIT ([LICENSE](LICENSE)); some parts come from D2R VR Unleashed (bearstonem) under MIT or MPL 2.0 and keep their
  notices - `LICENSE` lists them. Contribute only what you have the right to, under the licence of the file you change, and
  credit where your code or idea comes from.
- The installer must keep the player's saves and settings, run again safely, and never delete files it did not put in.

## Reporting a bug without code

That helps a lot too. Open an issue (or post in our Discord's support channel) with the mod version, your headset and
runtime, what you did and what happened. D2R VR Settings > Home > **Collect logs for support** makes one zip of everything we
need; please attach it. If the game crashed, collect the logs right away, before you start the game again.
