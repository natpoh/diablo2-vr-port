# The minimap in the hand: Bear's way and ours (2026-10-19)

Player report: native OpenXR draws the minimap across the view, no map in the hand; FlatVR (BodyWalk) with the
"same settings" shows it in the hand. His game option was probably the map in the middle ("center").

Source compared: bearstonem/d2r-pcvr (D2R VR Unleashed, MIT) at `a458c7e` (2026-10-19).

## 1. How d2r-pcvr gets the minimap into VR

Not its own automap draw from game data, and no hand-tuned rectangle. Three parts:

1. **The cut** - the same scissor redirect as ours (shared heritage): `vr/hud.cpp:199` `MapRect`,
   `vr/hud.cpp:219` `IsMapScissor` (a box in either top corner of the safe area), `vr/hud.cpp:507` `OnScissor`
   sends the map's draws to its own target, `vr/hud.cpp:406-412` copies the map's box into the share
   (`mapCaptured`), `vr/hud.cpp:642` `MapTexture` hands it to the XR side.
2. **The game's map forced into a corner while the wrist map is wanted** - this is why it does not care about the
   player's option: `vr/gamestate.cpp:67` `MaintainMapLayout`:
   - checks the bytes of D2R 3.3.93787's map-mode setter at RVA `0xd3d80` (`void(int)`: 0 whole screen,
     1 left corner, 2 right corner; transient layout, not the saved options);
   - reads the current mode from two globals, `0x2a2add0` (int "small") and `0x2a2add5` (byte "left");
   - `steamvr/wrist_hud.h:7` `MapLayoutLease`: while capturing it sets mode **1 (left)** and restores the
     player's mode afterwards;
   - called from the UI-thread callback `vr/gamestate.cpp:494` (`ReadItems`), driven by
     `MenuMap(...)` from `steamvr/transport.cpp:1696` (`interaction::MapWanted`).
   It also opens the AutoMap panel itself while wanted (`MapLease`, `vr/gamestate.cpp:491-495`) and
   `hud.cpp:113` `MapNow()` redirects the map whenever the wrist map is wanted (`g_menuMap`).
3. **To the headset** - `steamvr/transport.cpp:2199` `CopyMap` blits the share (`mapBlit`, optional top-down
   rotation) into a 512x512 OpenXR swapchain; quad layer on the left wrist `steamvr/transport.cpp:2565`.

## 2. How ours does it

- One cut for both paths: `vr/hud.cpp` - `IsMapScissor` (`vr/hud.cpp:202`, top-left or top-right corner of the safe
  area, `[hud] map_corner` 0/1/2), `OnScissor` redirect, `Publish` (`vr/hud.cpp:~548`) copies the box into
  `g_share[kMap]`.
- FlatVR: the share is published in `BodyWalkVR_GameHud` (`Publish`, `panel[kMap].visible`), FlatVR hangs it.
- Native OpenXR: `hud::NativeHud` (`vr/hud.cpp:820`) -> `xr.cpp` `HangHud` (`vr/xr.cpp:2338`, orb or flat panel).
- The side: only `MiniMapForHand` (`vr/vrcam.cpp:1093`) - at plugin start, VR and `[hud] map=1`, rewrites
  `"AutoMapMode"` in Settings.json to 2 (right).

### Why the two paths can differ

The code after the cut is the same in both paths; what decides is the game's live map mode. In the middle (mode 0)
the game draws its whole-screen map with no corner scissor: nothing is redirected, nothing comes into the hand, and
the map stays in the picture across the view - exactly the report. The only thing that moved it was the one-shot
Settings.json rewrite, which:
- holds only if the plugin runs before the game reads Settings.json (launch order differs between launches/paths);
- is undone for the session as soon as the game's options are touched (the game's in-memory mode; it saves that
  back at quit, so a late plugin start never wins).
Nothing in the repo shows a native-only branch for the map; the most likely difference is that one of the two runs
started with the game already holding mode 0 (load order or an options change), not the render path. Unverified:
the player's log line "the game's AutoMapMode was ..." / its absence would confirm.

## 3. What was done (branch `claude/minimap-extract`)

Bear's part 2, adapted, with no hand-written addresses:
- `vr/minimap_side.h` (pure): the setter's 35 checked bytes; the globals' RVAs **derived from the setter's own
  RIP-relative operands** (`MapGlobals`: sete -> left, mov -> small; the test proves they are Bear's
  `0x2a2add5`/`0x2a2add0`); `Mode(small,left)`; `Keeper` - asks for a corner only when the game reads the middle,
  at most once a second.
- `vr/gamestate.cpp` `MaintainMapSide` (UI thread, `ReadItems`): `d2rsig::Check` of the setter, read the mode
  (`__try`), call the setter. Wanted in VR with `[hud] map=1`, a hero in game, no menu (`gamestate::SetMapSide`
  from `vrcam.cpp` `LoadSettings`). Both paths (native OpenXR and FlatVR) - same code.
- Differences from Bear: we put the map on the **right** (left with `[hud] map_corner=1`), matching our existing
  Settings.json policy; and we **never touch a map already in a corner** (Bear forces left and restores), so a
  player with the map on the right sees no change -> no new ini key. Nothing is restored afterwards (same policy as
  the start rewrite; the setter does not write the options).
- Another build: the check fails -> logged once, only the start rewrite remains.
- Test: `cleanroom/tests/minimap_side_test.cpp` (built with g++ here, passes). The game side is not built or run here.

## For the in-game test

- Game option: map in the middle, `[hud] map=1`, native OpenXR, F4: the log says "the game's map was in the
  middle ... put in the right corner" and the map is in the hand. Switch the option to the middle mid-game: back in
  the hand within a second.
- Map on the right: no such log line, behaviour as before.
- Does the game save mode 2 at quit after our setter? (Bear says the setter is transient; harmless either way.)

## Not ported (would be larger)

Bear's own wrist swapchain + blit (`transport.cpp` `CopyMap`, top-down view) and his panel lease opening the
AutoMap by itself; ours already hangs the share (orb/panel) in both paths.
