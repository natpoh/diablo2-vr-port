# "Show items (Alt)" on a controller button only flashes

Report: "when setting a button to "show items on the ground (alt)" it only shows them for a split second then goes away, even if you hold the
button down (my ingame is set to "hold" btw)".

## What the binding sends

The settings pick "Show items on the ground (Alt)" writes `cmd:0x25` (`vr/direct_remap.h:50`), a game command, not a key: no VK_MENU is
injected anywhere. The layout (`vr/direct_layout.h` `Layout::Step`) sets bit 0x25 in `Outputs.commands`; `link::CommandsHeld`
(`vr/link.cpp:239`) hands it to `gamecmd::Tick/Apply` (`vr/gamecmd.cpp`), which calls the game's own executor: press on the bit's rising
edge, release on its falling edge. So the game sees exactly as long a press as the bit is set.

How long the bit is set depends on the LINE the pick was put on:

| line (D2R VR Settings > Controls) | before | |
|---|---|---|
| plain ("Right B") | held as long as the button (`Step`, latch path) | ok |
| plain, on a button that also has a long press (left/right stick click, left menu by default) | held until hold_ms (500 ms), then off as the long press fires (`hold_tap.h` `immediate`) | by design |
| chord ("Right grip + B") | held as long as the button | ok |
| **long press ("Right B (long press)")** | **one 100 ms pulse at hold_ms** (`holdtap::kPulseMs`, `direct_layout.h` `if (m.hold) ...`) | **the bug** |
| **touch ("Right B (touch)")** | **one 100 ms pulse when the finger arrives** (`touchUntil_`) | **the bug** |

A long-press line matches the report word for word: you hold the button, the labels come up at half a second, show for 0.1 s and go,
"even if you hold the button down". The same layout runs in F4 and (the plugin's own controller, `FeedFlatController`) in VR F1-F3.

Nothing else releases it: the only other drops are a context change (menu / focus / F4 <-> other view: `Layout::Cancel`/`Reset`,
`ClearHeld`) - wanted - and the 1 s staleness of frames. `ShowItemsPanel` opening is not counted as a menu (`gamestate.cpp`
`IsMenuPanel`), so pressing Alt does not flip the layout into the menu layer.

## Fix (branch claude/alt-items-hold)

- `vr/hold_tap.h`: `Out.held` = the hold has fired and the control is still down.
- `vr/direct_layout.h`: a key command (`kOutCommand`) on the hold line is applied while `hold || held`; on a touch line while the touch
  that began with an edge lasts (`touchOn_`). Actions, pad buttons, the skill panel, pause, quiver stay one pulse. Cancel/menu/uiUp clear it.
- Game toggle mode ("Show items: toggle"): the command still has one down edge per press, so it still toggles once; the up just comes later.
- Test: `cleanroom/tests/hold_line_test.cpp` (fails on the old code at "hold line: Alt held ..." and "touch line: Alt held ...").

## If the player's binding was on a PLAIN line

Then the plugin already held the command and the cause is elsewhere. Check in the game:

1. `d2r-vr-vrcam.log`: `vrcam: game command 0x25 down (D2R key: Show items (Alt))` then `... up` - the time between them is how long the
   plugin held it. If `up` comes with the button release but the labels vanished earlier, the game itself drops it: in "hold" mode it may
   poll the real key state of the binding's key (Alt) each frame rather than the command's held byte; test with "toggle" mode - if
   toggle works and hold flashes, that is it (the fix then is a real key, e.g. SendInput VK_MENU, for this one command).
2. The player's `[direct]` lines in d2r_vr.ini: which key holds `cmd:0x25` (`*_hold`, `*_touch`, plain).
3. BodyWalk's Mapping instead of the plugin's controls: BodyWalk's own action hold (`link::HeldOrPressed`) - a BodyWalk "press" type
   action reads as held for 50 ms only (`link::kTapMs`).
