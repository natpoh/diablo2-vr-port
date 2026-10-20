# Flat first / third person: monsters stop being targetable "after a while"

The player's report (verbatim): *"I'm having great fun just playing the mod in desktop first person. But a somewhat moderate issue keeps
happening, I'm not sure what the cause is, but after a fashion of normal gameplay, the targeting of monsters becomes very out of whack.
Aiming at their feet to begin with is totally fine, but randomly it'll just make monsters impossible to actively target, and sometimes it
extends to 3rd person, but it's totally fine in the normal camera."*

Scope: `[mode] platform=0` (flat desktop), our first-person and third-person views with mouse look (`ShooterActive()`), W A S D through
the game's keyboard move (`KeyMoveMode()`). The game's own top-down view (our camera off) is the "normal camera" that stays fine.

This is code reading only. Nothing here was run against the game. Line numbers are for the commit that adds this file.

---

## 1. How a target under the crosshair is found

### 1.1 Where the crosshair is (the screen point)

* The crosshair is not drawn by us. It is the Windows cursor: `user32!SetCursor` is hooked and the game's gauntlet handle is replaced
  with ours while `CrosshairNow()` holds (`vr/vrcam.cpp:7144`, `HookSetCursor` `vr/vrcam.cpp:7312`, `WM_SETCURSOR` in `GameWndProc`
  `vr/vrcam.cpp:7393`). The picture's hot spot is its centre.
* `MouseTick()` (`vr/vrcam.cpp:7579`), on our timer, reads `GetCursorPos`, turns the camera by the travel from the client rect's
  middle (`g_mouseYaw` / `g_mousePitch`, pitch clamped to ±90°, `vr/vrcam.cpp:7602`) and puts the cursor back on that middle with
  `SetCursorPos`. It does nothing while the game's window is not in front (`GameFocused()`, `vr/vrcam.cpp:2884`) or our camera is off.
  So the visible crosshair is the middle of the client rect only while the game has the focus and the tick has run.

### 1.2 How that point becomes the game's mouse position

* `GameWndProc` (`vr/vrcam.cpp:7349`), branch `if (ShooterActive() && KeyMoveMode())` (`vr/vrcam.cpp:7392`): every message from
  `WM_MOUSEFIRST` to `WM_MOUSELAST` except the wheels has its `lParam` replaced with the **client rect's middle**. With
  `[render] vr_resolution` (VR only) the middle of the game's picture size is used instead. Raw mouse input is dropped.
* So the game's mouse position is "the middle" because every mouse message says so. It does not depend on where the real cursor is.
  The comment there says so too: *"the pointer's travel between two MouseTicks never moves the game's point."*
* The game may also read the pointer somewhere other than these messages (`GetCursorPos`, `GetMessagePos`, the `MSG` in its own
  pump). If it does, it gets the real cursor. That is near the middle while `MouseTick` runs, and wherever the mouse went when it
  doesn't run. Not confirmed either way; §3 logs it.

### 1.3 How the game turns its mouse pixel into a world point

* The game's mouse code calls `Camera::ScreenToRay(px, origin, dir)` (RVA `0xED62B0`, `cleanroom/camera/game_layout.h`). We hook it in
  `HookScreenToRay` (`cleanroom/camera/d2rcam.cpp:372`). While our camera is on, the ray goes through **our** view and projection:
  `m4::RayThrough(invView, proj, ViewportW, ViewportH, px)` (`cleanroom/camera/mat4.h:164`), with the viewport taken from the camera
  fields `+0x150 / +0x154`. The shadow fit's calls get the game's own pose. A ray override (`VrRay`, `vr/vrcam.cpp:4272`) answers
  first when set. It is only set by `[bow] mode=2`'s pad aim (`AimByMouse`, `vr/vrcam.cpp:4718`), so in flat it's never set unless a
  pad is used with that mode.
* Our projection (`VrProjInner`, `vr/vrcam.cpp:4184`) uses `CameraFov()` and the camera's own viewport aspect, the same numbers the
  picture is drawn with. For the **middle pixel** the projection doesn't matter at all: NDC (0,0) is the camera's forward axis
  whatever the FOV or aspect. So the pick ray is exactly where the camera looks, **provided `px` is the viewport's middle**. The
  WM rewrite uses the *client rect* middle, while `RayThrough` divides by the *camera viewport*. The two agree when the game scales
  client pixels to viewport pixels before it calls (it must, for its own top-down view). If it ever doesn't, the ray leaves the
  crosshair.
* The game then meets that ray with its ground. Our own reproduction of this for the VR pointer (`vr/pointer3d.h`, `groundY = L[1]`
  at `vr/vrcam.cpp:4162`) assumes the plane at the hero's look-at height. The game's own choice of plane has not been disassembled.

### 1.4 Which game function picks the unit

* `HoverUnit(clientIndex)` = `0xF1900(0x9A820(unit))` is "the unit under the pointer, null on bare ground" (comment above
  `RVA_MAP_CLICK`, `vr/vrcam.cpp:6319`). It reads the game's **hover state**, which the game updates from its own pick. We don't see
  how the game builds it. The report's *"aiming at their feet is fine"* fits a pick that finds the **ground point** first and then
  tests units near it with the game's own isometric-era boxes, rather than a ray against each model's 3D bounds.
* The click: `HookMapClick` (`vr/vrcam.cpp:6369`) wraps the game's map click `0xFE3B0(unit, type, x, y, flags)`. With
  `[input] flat_click_shoot=1` and nothing under the pointer (`bare()`: `HoverUnit` returns null), it sets the Stand Still byte, so
  the click is a shot at the crosshair and never a walk. With a unit under the pointer it changes nothing: the game's own click
  attacks, picks up or talks. The existing line `vrcam: flat click type %d, no target - a shot at the crosshair (Stand Still)`
  (`vr/vrcam.cpp:6392`, at most every 2 s) is printed exactly when the **game's** hover was empty at a click.
* The controller target and cast-resolver hooks (`HookSpellResolve` `vr/vrcam.cpp:6510`, `HookSpellHover` `vr/vrcam.cpp:6723`,
  `DispatchTargetOff` `vr/vrcam.cpp:6582`, `[autotarget]` in `SpellCastFor` `vr/vrcam.cpp:6496`) all act only in **VR F4**
  (`viewgate::F4()`), or after a directed cast that only F4 makes. In flat they pass the game's answer through untouched.
  `AimByVector()` (`vr/vrcam.cpp:4905`) needs pad buttons held with `[bow] mode=4`. With no pad (`[input] flat_no_pad=1`, the default,
  `vr/vrcam.cpp:419`) it is off.

### 1.5 What the game's own picking assumes

The game's camera is a steep, fixed-pitch isometric-like perspective that looks down on the hero from a fixed height. Every pixel's
ray meets the ground near the hero, at a steep angle. Two consequences matter here:

* **Height errors are not amplified.** If the plane the game meets the ray with is `dh` above or below the floor the picture shows,
  the ground point slides by `dh / tan(down)` along the look. That is about `dh` in the game's camera. It is about 5.7·`dh` looking
  10° down in first person, and infinite at the horizon (`flatpick::SlipPerUnit`, `vr/flat_pick.h`).
* **Only what is on the game's own screen is hoverable.** The isometric-era hover tests units of the game's on-screen set around the
  hero, which is a rectangle of the *game's* view. Our first-person crosshair can point at ground far outside that rectangle. How far
  depends on the heading, because the rectangle is wider across than it is deep. This is inferred from how Diablo II hovers (units
  tested as they are laid out for drawing), not read in D2R's code.

---

## 2. What could drift "after a while": hypotheses, ranked

Fit criteria: (a) fine at first, (b) breaks at random later, even aiming at the feet, (c) sometimes also in our third person, (d) never
in the game's top-down view, (e) consistent with the code above.

| # | Hypothesis | a | b | c | d | Code evidence |
|---|---|---|---|---|---|---|
| 1 | **The game's ground plane is not the floor the picture shows, and the shallow look amplifies the gap.** In areas or spots where the floor is not at the height the game meets its ray with, the ground point the game hovers around slides along the look by `dh / tan(down)`. | ✓ town and flat early areas fine | ✓ depends on the area or spot and how steeply you look | ✓ third person is shallower than the game's camera, less than first | ✓ steep look, slip ≈ dh | Only the middle ray is used (§1.3). The plane is the game's own. The plugin's VR pointer assumes the look-at height (`vr/vrcam.cpp:4162`), unverified. |
| 2 | **Item labels / 2D UI under the crosshair take the hover.** Flat keeps the game's labels (`vr/vrcam.cpp:3201`). As loot piles up, or with labels toggled on (Alt toggle), labels laid out around the middle of a first-person picture sit on the crosshair. The game's hover prefers a label or item to a monster. | ✓ little loot at first | ✓ depends on what lies around and on the label toggle | ✓ | ✓ labels spread out in the game's view | Labels untouched in flat. Hover is the game's (§1.4). |
| 3 | **The game's mouse pixel is no longer the viewport's middle.** The WM rewrite uses the client rect, `RayThrough` the camera viewport (§1.3). A window-size, resolution, DLSS/FSR render-scale or DPI change mid-session, or the game reading the pointer outside the rewritten messages after a focus change, leaves the pick off the crosshair. Everything then misses, feet too. | ✓ | ✓ after alt-tab or a settings change | ✓ same rewrite in both views | ✓ our hook passes the game's own ray through there | `GameWndProc` `vr/vrcam.cpp:7392` vs `HookScreenToRay` `cleanroom/camera/d2rcam.cpp:372`. |
| 4 | **Far targets are outside the game's own on-screen set.** The crosshair points past the rectangle the game hovers in, depending on the heading. | ✓ near fights first | ~ "random" = heading and distance | ✓ | ✓ | §1.5, inferred. Doesn't explain the feet of a near monster. |
| 5 | **Stand Still stuck.** A Shift whose key-up was lost (focus change while held) makes every click an attack in place. A melee hero then swings at the air instead of walking up to the monster. | ✓ | ~ after a focus change | ✓ | ✗ would also show in top-down | `HookMapClick` only adds Stand Still on bare ground. The game's own Shift is not swallowed. |
| 6 | **The window loses the focus while still getting mouse messages** (an overlay or notification in front). `MouseTick` stops centring and turning, so the crosshair drifts with the mouse while the game still picks the middle. | ✓ | ✓ | ✓ | ✓ | `MouseTick` returns on `!GameFocused()` (`vr/vrcam.cpp:7579`). The camera would also stop turning, which the player would likely notice. |
| 7 | **The game's UI flips to controller mode** (a pad event), so the controller's auto-target replaces the mouse hover. | ✓ | ✓ | ✓ | ✗ | Blocked by `flat_no_pad=1` (default). W A S D would also stop (`FlatPadTick` logs "W A S D held, but the game does not ask for a keyboard move"). |

Ruled out by the code (flat never reaches them): `[gameplay] aim_no_chase`, `[hands] spell_aim` / `bow_aim` / `throw_aim`,
`[autotarget]` (all F4-only), `AimByVector` (pad only), the ray override (`[bow] mode=2` with a pad only), and a stale camera frame
after an area load (`HookReleaseCamera` publishes "none" for at most one frame, then `SyncFrame` takes the new camera). There is no
cached matrix or projection that survives a resize: both are rebuilt every frame from the camera's viewport fields.

Best fit overall: **1 and 2** (they explain "random later" without any state of ours going bad), then **3** (the only one where state of
ours, the WM rewrite against the game's viewport, could be wrong; and the new log can settle it at once).

---

## 3. What to log, and how to reproduce

### Lines that already exist

* `vrcam: flat click type N, no target - a shot at the crosshair (Stand Still)`: the game's hover was empty at a click (at most every
  2 s). If this appears while the player clicks a monster's feet, the hover is lost, and it's not something after the hover.
* `vrcam: flat move v=(...) gate=ok (keys)`: the game is in mouse mode. Its absence while walking, or
  `vrcam: flat: W A S D held, but the game does not ask for a keyboard move`, means controller mode (hypothesis 7).
* `vrcam: mouse look and W A S D on/off` (F9) and the input log (`[debug] input_log=1`, the default): clicks, and whether a device or
  a program sent them.

### New, behind `[debug] flat_pick=1` (off by default, live, read only)

Added with this note: `FlatPickClick` (`vr/vrcam.cpp:6335`, called from `HookMapClick`) and `FlatPickWatch` (`vr/vrcam.cpp:7608`, on the
timer after `MouseTick`). The game's last pick is recorded by `d2rcam::LastPick` (`cleanroom/camera/d2rcam.h`). The pure parts are in
`vr/flat_pick.h`, tested by `cleanroom/tests/flat_pick_test.cpp`.

At each mouse **press** (types 0 and 3, at most 4 a second), four lines:

```
vrcam: flat pick: click type 0 (x, y, flags 0x..), under the crosshair: a monster, class 12, id 345, mode 1, at (5651.0, 4713.0) | none | an item ...
vrcam: flat pick:   the game's pixel 960,540 of 1920x1080 (the middle, 0% off, 3 ms ago, from 0x..., our camera)
vrcam: flat pick:   its ray 12 deg down meets the hero's ground 8.1 ahead (4.7 per unit of height)
vrcam: flat pick:   the cursor +0,+0 from the window's middle (1920x1080), focus 1, Alt 0, Shift 0, menu 0
```

Once a second, while the mouse look is on, the watch adds a line (at most every 5 s while it lasts) when the game's last pixel is
**not the middle**, the game **asked no mouse ray for 1.5 s**, or the window **is not in front**. It says once when all is well again.

How each hypothesis reads in that log:

* **1 (plane)**: `under the crosshair: none` with the pixel at the middle, a shallow angle and a large "per unit of height", while
  the monster's feet are visibly on the crosshair. The same monster is picked when looked at steeply. A height difference is
  confirmed if the hover returns the monster when the crosshair is put a little *short* of or *beyond* its feet.
* **2 (labels)**: `under the crosshair: an item ...` while aiming at a monster, or `Alt 1`.
* **3 (pixel)**: `NOT the middle` on the click lines or the watch line. The `from 0x...` RVA tells the game's mouse code apart from any
  other caller of `ScreenToRay`. If the off-middle lines all come from another RVA, they are not the mouse code and hypothesis 3 is
  out. Compare `of WxH` (viewport) with the window size.
* **4 (on-screen set)**: `none` only for monsters far ahead ("meets the ground 20+ ahead"), depending on the heading.
* **5 (Shift)**: `Shift 1` on the click lines, or flags with Stand Still set while a monster is under the crosshair.
* **6 (focus)**: `focus 0`, or the watch's "not in front".

### For the player

1. In `d2r_vr.ini`, `[debug]`: `flat_pick=1` (live, no restart).
2. Play flat first person as usual. When targeting goes wrong, **don't change anything**. Click a monster's feet 3 or 4 times, then
   look steeply down at a monster near you and click it, then press Alt once and click again.
3. Press F1 (top-down), click the same monster once, go back to first person and click it again.
4. Note the time and send `d2r_vr.log` (the lines `vrcam: flat pick`). Also say what was on the ground (loot labels shown?),
   whether you'd alt-tabbed or changed a setting before it started, and the window mode and resolution.

---

## 4. Proposed fixes, smallest first

None is implemented: the code doesn't single out one cause, and each fix below changes how the game targets in some way. What was
added is only the read-only `[debug] flat_pick` log above.

1. **Hypothesis 2, labels**: nothing in the plugin. Turn the game's item labels off, or hold Alt rather than toggling it, in flat
   first person. If the log confirms it, a flat-only option could hide ground labels while the mouse look is on. That is UI only, but
   it hides information the player may want, so it should be off by default.
2. **Hypothesis 3, pixel**: if the watch shows the game's pixel off the middle **from the mouse code's RVA**, make the game's mouse
   ray the crosshair's ray. Answer `ScreenToRay` for that one caller (by its return RVA, as the shadow fit is told apart) with the ray
   through the *viewport's* middle while `ShooterActive() && KeyMoveMode()`. That's ~15 lines in `HookScreenToRay` through
   `SetRayOverride`, flat mouse look only. It would be a pure bug fix (the pick = the crosshair, which is the design), so on by
   default behind its own key, e.g. `[input] flat_pick_centre=1`. It needs the RVA from a log first.
3. **Hypothesis 1, plane**: once the log shows the slip, move the crosshair ray's origin along the up axis so that, at the game's
   plane, it lands where the picture's floor is under the crosshair. Same hook, same caller filter, flat mouse look only. This needs
   the game's plane height, read from a log (the hover found when aiming short of or past the feet), so it comes after 2.
4. **Hypothesis 4, range**: a softer cap on how far up the crosshair can look while a mouse button is held. Or, better, leave it and
   document "targets beyond N yards ahead aren't hoverable in first person". Anything that picks a monster for the game (an assist)
   changes the game's mechanics, which is out of scope.
5. **Hypothesis 5, Shift**: on focus regain, if Shift isn't physically down, nothing to do in the plugin (the game keeps its own key
   state). Tell the player to tap Shift. Only if the log shows it often: send the game a Shift key-up on focus regain, flat mouse
   look only.
