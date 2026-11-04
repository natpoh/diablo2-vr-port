## What this changes

<!-- One or two sentences: what and why. Link the issue or Discord thread if there is one. -->

## How it was checked

- [ ] Built (`cmake --build build --config Release`)
- [ ] Tests run (which `*_test.exe` / scripts, and their result):
- [ ] Tried in the game - headset, runtime, view (F1 - F4 / flat), class and weapon, single player or co-op:
- [ ] Not tried in the game (say so - we will try it)

## Checklist

- [ ] A new feature has its own ini key and Settings control; with it off, everything is as before
- [ ] It steers the game's own commands (or only changes what is drawn) - no direct calls that change the game's rules
- [ ] No game files, saves, dumps, build output or private data in the change
- [ ] New Settings texts go through `TR()` and `lang/en.txt` was made again with `tools/lang_sync.py en`
- [ ] I have the right to contribute this under the file's licence (see `LICENSE`)

<!-- This repository is made from our development repository at each release: an accepted pull request is brought in by
     hand, credited, and appears with the next build (see CONTRIBUTING.md). -->
