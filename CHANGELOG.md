# What's new in D2R VR

## 0.175

- **Thrust debug: the thrust's numbers in the headset** (the author: "the forward strikes need a debug of their own so I can tune them"). Settings > Strikes > Thrusts: *Thrust debug: the
  last thrust's numbers over the weapon hand* (`[debug] thrust_debug`, off). A small plate over the weapon hand (F4) shows the LAST TOUCH - a thrust's contact with a monster, COUNTED
  in blue or REJECTED in orange with the threshold that failed (not straight, too slow, too short, no accel, not toward the monster, not the head) - and the LAST JAB, touching or not:
  its peak speed, the cm along the blade, how straight, its peak acceleration, each beside its threshold (blue passes, orange fails; "(own)" = the kind's own number) and whether it
  would count, so a real jab can be read before a monster is near. The blade (drawn as with *Draw the capsules of Hit by touch*) turns BLUE (counted) / ORANGE (refused) for a moment
  after each thrust - the thrust's own colours, so a thrust is told from a swing (green / red) at a glance - and the log has a "thrust debug:" line per thrust and per jab. *Thrust
  debug: only the thrusts* (`[debug] thrust_debug_only`, off): the swing's colours, marks and log lines are left out, and a swing or an overhead blow is not shown at all. Only a drawing and a log; live (vr/thrust_debug.h, vr/thrust_debug_wire.cpp, its layer vr/xr_thrust_debug.h).
- **Fixed, native OpenXR on a slow graphics card: "a weird double vision thing, kinda looks like im drunk"** (a Quest 2 player over Link,
  GTX 1650 SUPER 4 GB, 0.174, 2026-10-21). A pair is drawn in stereo only while the game's world camera is there, and that was "its follow
  ran within the last second" (d2rcam::InWorld). The follow runs once in each pass, so on a card that takes over a second for a pair
  (his log: pairs of 1.2 - 1.8 s, a pass up to 6 s, video memory at its budget) every pair after a right pass of a second or more was
  taken for no world: drawn without its XR frame, its right eye went to the headset as the flat picture 1.1 m ahead, and the next pair
  was stereo again - the world jumped between its depth and a flat screen, one pair each (his log: 72 pairs and 64 flat in 10 s in
  the Rogue Encampment, F4; the author's own at 70 - 120 pairs a second: 0 flat). Now a pair is the world's too when the follow ran
  since the last pair began (vr/pair_world.h, `[stereo] slow_pair_world`, on; 0 = as before). At the game's own speed nothing changes
  (the follow is always within the second); a game left on a slow card is seen one pair later. The log says every 10 s how many pairs it
  kept so. Test: pair_world_test (his pairs' timings: 5 flat before, none now; mutation-checked). It does not make the card faster: at
  1 - 7 pairs a second the headset still holds each picture long - lower the render size or use the FlatVR 3D depth mode there.

- **Fixed: the sky still shook now and then** (the author on 0.176, after the right-eye fix below: "the sky still shakes now and
  then"; "one pass, two pictures"). That fix holds while the graphics card is at most one picture behind. In the pairs drawn in
  two passes - long stretches while walking, the replay settling after each biome set (19 s in his log) - nothing kept it so: two
  pictures behind, the next pair's values were in ReShade's one constant buffer before this pair's effects were drawn, in either
  eye. Now, in a pair, each eye's effects put their values in only once the graphics card has drawn that eye's last effects (a
  fence on ReShade's queue after each pair's effects; vr/sky_view_eyes.h FxRing, cleanroom/tests/sky_view_eyes_test.cpp at 2 and 3
  pictures behind). The CPU still runs a picture ahead; with the replay running its own waits are stronger, so nothing changes
  there. `[stereo] pair_fx_sync` (on; 0 = as before). The log tells every 10 s how many effects waited and how long.
  D2R_DepthFog.fx unchanged.
- **Fixed, eyes by turns: "the sky shakes in both eyes when I turn my head" and "the medallions show through the hands, flickering"**
  (the author, test-0176). (1) ReShade keeps one buffer of the effect's values (the cameras the sky, the fog and the overlays are drawn
  from) and writes it in place at every technique it renders. By turns the graphics card runs a frame behind (one eye a frame is half
  its work), so the next frame's effects overwrote both eyes' cameras before the card drew this frame's sky: the eye drawn got the next
  frame's head turn, a frame off its world, as often as the card was behind, and the eye made, warped from that picture, the same - the
  sky shook in both eyes in head turns; the eye made's overlays read the next frame's cameras too. Now each frame's effects wait until
  the card is past the last frame's effects AND the eye made's overlays before their values go in (`[stereo] turn_fx_fence`, on; Stereo
  tab *Eyes by turns: each frame's effects wait for the last frame's*; the fence of `[render] fx_fence`, marked after the overlays
  instead of after the effects alone). The wait costs little: the next frame's scene is already on the queue behind the mark, the card
  stays busy. (2) In the eye made nothing of the scene hid the overlays (0.175's `turn_overlay_depth=0`): each eye's medallions and the
  bow's orbs showed through its hands on every frame it was the one made - every second frame. The made eye's own depth, moved with its
  picture, hides them again (`[stereo] turn_overlay_hide`, on; replaces `turn_overlay_depth`, no longer read; Stereo tab *... its
  overlays hidden by its moved depth*). It doubled the medallions when tried on 2026-10-20 by two causes since gone: the depth was the
  drawn eye's at 4-pixel cells cut whole at the hand's edges with the older picture under the cuts (now to the pixel, d2r-pcvr v0.3.24's
  layers), and the eye made's flag was a uniform the next frame overwrote (now its own technique, D2R_OverlayMade). The made depth is
  written by the same pixel as the made picture's colour, so a medallion is hidden exactly where that picture shows the hand. D2R_DepthFog.fx
  unchanged. Tests: sky_view_eyes_test models the by-turns frames against the one buffer (0.175: both eyes' skies wrong; fenced after
  the effects alone: the eye made's overlays still wrong; now right at any lag); turn_warp_gpu_test reads the made depth back - the post's
  to both its edges, the wall's in the gap it uncovered (fails on the 4-pixel cut). Not yet seen in the headset.

- **Hit by touch: a thrust can ask for acceleration - a jab, not a slow push** (the author: a poke of its own with acceleration and distance, chosen per weapon). Settings > Strikes
  > Thrusts: *Thrust: the least acceleration (a jab, not a push), m/s²* (`[gameplay] melee_thrust_min_accel`, 0 - 300, default 0 = not asked, as before) and an *Accel* column in
  *Thrusts of each kind* (`melee_thrust_min_accel_<kind>`, 0 = the general one). It is the PEAK acceleration of the point along the blade over the thrust (its speed along the blade
  over 25 ms, from 0.25 s before the thrust began) - the burst that drove it - not the value at the touch: a jab meets the monster at its top speed, coasting, where a jab and a
  push both read about nothing. A sharp jab reads tens of m/s², a slow push a few; about 10 - 15 keeps the jabs. The log now tells one line per thrust, counted or refused, with its
  speed where it touched, the cm along the blade, the part of its path along it and its peak acceleration (the first ones always, every one with [debug] melee_boxes), and the
  10 s line counts "a push not a jab". Live. F4 only.
  - **Thrusts of each kind: a tick per kind** (the author: "where is the tick, use it or not") instead of the -1 / 0 / 1 number: *Thrust* ticked = the kind thrusts
    (`melee_thrust_on_<kind>`). Ticked to begin with: spears, daggers, javelins, throwing weapons - the game's own 1ht / 2ht kinds. An older settings file's `melee_thrust_<kind>`
    is still read (and shown as the tick) until the tick is written: -1 unticked, 1 ticked, 0 the kind's default.
- **Fixed: the doll let go on a weapon swap - "about half a second, sometimes more"** (the author, 2026-10-21: people report the doll letting go,
  "especially on a weapon swap"). A swap - the W set, a weapon taken from or put behind a shoulder, another shield or bow - rebuilds the hero's
  model: a new skeleton of the same entity, and for a moment the game poses the old one and the new one both, either of them the one drawn. The
  hook had one rig for the doll: (1) the new one was taken at its first pose and the old one, maybe still the one drawn, got the game's own pose
  (head shown, arms off the controllers); (2) a new one posed before the hero's SkeletonComponent held it was looked for again only after
  500 ms, and by then, the hero having moved more than a unit (a swap while running), it was "not a rebuild of ours" and waited out the old
  one's 3 s (cleanroom test: let go 515 ms after the swap); (3) a rig whose bones did not read at its first pose was "not a hero" for good;
  (4) the facing's rig went back and forth between the two (its chain failing for the one not in the component). New `[hands] doll_rebuild=1`
  (0 = as before): both rigs get the arms while the game poses both (the one taken keeps the facing; nothing is learned from the other's pose -
  not the weapon's grip); the new one is taken as far from the old as the hero ran meanwhile (20 units a second, 4 at most); an instance not
  in a SkeletonComponent yet is looked for again after 50 ms; a player's rig that did not read is learned again every 100 ms for 5 s; and the
  last facing is held, past `[hands] facing_hold_ms`, for as long as the hero is posed - until a facing reads, not for a fixed time. Also a menu's
  model (no entity of the game's world: seen at a game's exit, "facing bound ... (moved, no event)" then "the hero's SkeletonInstance is in no
  SkeletonComponent") no longer takes the hero over. New `[debug] doll_trace=1` (default 0): one line each time the doll lets go and is taken
  again, with why and how long - `vrcam: doll: facing let go - <which read failed: not posed for 2 s, no SkeletonComponent, the matrix N / M
  units off the look-at>; instead: <the last facing held / the search / nothing>`, `doll: facing bound again after N ms`, `doll: the hero's
  model rebuilt - rig B taken from A, N units apart, M ms after A was posed last`, `doll: rig A posed along with the hero's`, `doll: the rig
  ours was rebuilt from is not posed any more - the two were posed together for N ms`, `doll: let go - the hero's rig ... is not taken as
  ours: <its head down / N units from the look-at / another rig taken ... / none taken yet>`, `doll: taken again after N ms`, `doll: a player
  model whose rig is not learned (<model>)` / `learned on its try N`. The pose hook only notes; vrcam's 10 ms tick writes the lines. The logs
  at hand (2026-10-10 .. 10-21) hold no swap in play, so the cause is from the code and the test, not seen in the headset. bearstonem/d2r-pcvr:
  his facing binding is the same code as ours; his hero rig is the one within 3 units of the look-at (ours 12) - nothing there holds the doll
  better. Tests: skeleton_discovery_test (a fake renderer ECS of one entity: the swap with the new rig in the component at once - the old one
  posed along, the facing's rig not going back; the new one not in it yet and the hero running - taken 63 ms later; with the key off, let go
  515 ms later as before; a rig learned on its second try; the notes; the menu's model posed along, not taken).
- **The skeleton hook does less on every pose, after d2r-pcvr** (bearstonem/d2r-pcvr v0.3.24, MIT: his "crossbow and shield discovery without a call
  per letter", commits bd99126 / be02ea3). Nothing found or drawn changes; the same bones, the same rigs. Measured on this PC (2026-10-21):
  (1) each pose of the hero copied the whole learned rig - every bone name, the bind pose, the finger chains - out of the cache: 3-6 us a pose for
  60-160 bones (a 110-bone rig 4.3 us); the cache now holds it by pointer (8 ns). (2) A model's path and a rig's bone names are read a page at a time
  (vr/guarded_text.h, his), not one guarded copy per letter: a 62-letter path 346 -> 86 ns, a bone name 150 -> 57 ns - only when a rig is first met.
  (3) The bow's, the flail's and the crossbow's caches tell the skeletons that are none of theirs (most of what the game poses, while one of them is
  on) under a shared lock, so the render threads no longer queue for the exclusive one; a full cache drops those, not the hero's bow / flail /
  crossbow (the hero's cache likewise keeps him). His other two ideas were already so here: the crossbow's rig is looked for only while a crossbow
  is held (`gunStock`), the bow's only with a bow, and we have no per-skeleton shield scan. Test: cleanroom/tests/skeleton_discovery_test.cpp
  (Str against the letter-at-a-time loop at 85 000 places next to a page that does not read; Learn on a toy hero rig whose names cross pages; the
  caches).
- **New: a game panel nearer / farther with the right grip and the right stick** (the author: "меню можно сделать приближение отдаление
  оверлея при зажатом грипе правой руки и джойстиком в меню торговца"). Native OpenXR, F4, with the game's inventory, stash, a trader,
  the waypoints... hung in front as a panel (*Menus as a panel*): hold the right grip and push the right stick forward - the panel comes
  nearer; back - it goes farther, live; let go and it stays, written to the panels' distance (`[screen] menu_distance_m`). It stops where
  the panel stops being readable, worked out from the headset and the panel, not set by hand: nearer, part of it would be out of the eyes'
  field; farther, one headset pixel would cover more than two of the panel picture's pixels (it has no smaller copies, so the game's text
  breaks up) - and a distance already set past one of those is never moved by itself. While the grip is held the stick's forward / back
  is not the game's; the grip's own press in a menu (the right bumper, where the laser does not have the grip) comes as a tap when it is
  let go without moving the panel, so taking the panel never turns the trader's page; a trigger squeezed while the panel moves clicks
  nothing until let go. D2R VR Authentic Settings > UI: F4 body > Menus > *Right grip + right stick: a panel nearer / farther* (on,
  `[openxr] menu_zoom`) and its speed (`menu_zoom_speed_mps`, 1 m/s). The idea of taking a game menu by the right grip to push or pull
  it is bearstonem/d2r-pcvr's (v0.3.24, MIT; there the hand's own travel moves it). vr/menu_zoom.h, cleanroom/tests/menu_zoom_test.cpp.
- **Hit by touch: a thrust (a poke) is a hit of its own kind, with its own speed and distance** (the players, through the author: "for piercing weapons add a thrust /
  poke type with its own coefficients for speed and distance"). A stab - the blade moved along its own length, point first - of a weapon that pierces now counts by
  two numbers of its own, apart from a swing's: Settings > Strikes > *Thrusts: a poke along the blade*: *Thrust: the least speed that hits* (0.8 m/s - a poke reads about
  1 m/s, a swing's tip 2-6) and *Thrust: the least distance along the blade* (5 cm), with *how much of the movement is along the blade* (70 %). A short quick stab of a
  spear or a dagger hits while a swing keeps its own least speed, start distance and amplitude - with a high swing speed (only real swings) the pokes no longer go
  unanswered. Which weapons thrust is the game's own data: the weapon's attack class, the one the game animates it with - 1ht (daggers, javelins, throwing knives) and 2ht
  (spears, tridents, pikes), read from the item's row (the log says where it was found; if it is not, spears, daggers and javelins). A table *Thrusts of each kind* sets a kind
  to thrust always (a sword's tip, a polearm) or never, and gives it its own speed and distance. *Toward the monster* and *the head* judge a thrust as a swing; a pull-back or
  a stab beside the monster does not count; a monster is hit once per thrust, and a movement that is both a swing and a thrust hits once. The game's own attack is pressed
  as for a swing - its animation, reach, rolls and damage are the game's. A second swing engine runs on the same hand samples with the thrust's numbers (vr/thrust_stroke.h,
  cleanroom/tests/thrust_stroke_test.cpp). `[gameplay] melee_thrust` (on), `melee_thrust_min_speed`, `melee_thrust_cm`, `melee_thrust_along_pct`, per kind
  `melee_thrust_<kind>`, `melee_thrust_min_speed_<kind>`, `melee_thrust_cm_<kind>`. Unticked: a poke counts only as a swing, as before. Live. F4 only.
- **Battle.net copy: START no longer passes Steam's ids to the game** (a player: "You have not been online in the last 30 days" when started from D2R VR
  Settings). D2R VR Authentic Settings' Launch / START now gives the Battle.net game no SteamAppId / SteamGameId (with them it checks its license
  through Steam), and the Steam copy its own app id 2536520 - as bearstonem/d2r-pcvr's launcher and the D2R VR Launcher already do.
- **The fog drawn inside the game's frame** (test, off: [render] fog_in_frame, World > Fog). The fog is drawn into the
  game's own picture where the game composes it - after the scene is lit, before its fire, spells and smoke - and the
  game then lays them over the fogged scene: a torch, a Fire Bolt or an Inferno in front of a far wall keeps its light.
  The sky, the cave ceiling and the black beyond the world keep our fog as before. With an act's own fog colour (all
  acts have one in the shipped d2r_vr.ini), not in the Arcane Sanctuary (its fog takes the sky's colour); VR F4, both
  stereo modes (one pass two pictures, and eyes by turns - the eye made carries the fog of the picture it is made
  from); read at the game's start; over "Fog spares fire" when both are on. Step 1 of docs/game_render_sky_fog.md.
  Fixed after the author's test on 0.176: every object behind a fire had a thick grey rim - the fence's planks behind a
  torch, the flame's glow cut along their edges. The fog in the frame took each pixel's fog from the farthest of it and
  its neighbours (the post pass's rule for the finished picture), so an object's edge against the far ground or the void
  got the full fog - the flame with it, since it is drawn into those very pixels; the game's anti-aliasing and upscaling
  widened the rims. Each pixel is now fogged by its own depth, and the post pass leaves exactly those pixels to it.
  And then: the flame pale over the fence, its warm halo gone, bright streaks in the gaps between the planks. Part of the
  game's fire (the torch's flame among it) is drawn blended straight into the lit scene, and was fogged with it by the
  depth behind it. Such draws are now drawn twice with the game's own state - into a texture of ours on black, and their
  coverage into another on white - and the scene's fog leaves them out exactly (S0 K + E): the flame at its own light,
  the game's bloom halo back. What blends in a way that can not be taken apart stays fogged with the scene; the log
  names it.
  And the thin lines along the planks' edges: where the frame leaves a pixel to our post pass (no depth of its own - the
  void between two planks and its rim) there is only the fire's light over the black void, and the post pass fogged it
  away (the rim) or faded it (over the sky). The fog of the far now goes under that light, at full over the sky.
  And the main thing found in the author's log: in his game the fog in the frame had never run - it refused the frame
  because the game's light culling (compute) runs in the same command list, and our compute step would have left the
  game's compute bindings changed; every picture until then (the grey rims, the striped halo, the lines) was our old
  fog. The game's compute bindings are now given back after ours, and the fog in the frame runs there.
- **Fixed: the sky juddered in the right eye while walking and turning the head** (the author on 0.175, "one pass, two pictures";
  not in d2r-pcvr). While walking the game sets its biome at every line of its 80-unit map grid, the replay settles again each
  time and every pair goes in two passes (as bearstonem/d2r-pcvr measured). The left eye's camera build wrote the right eye's
  sky camera (the left one moved across) whenever the replay was merely on, not only when it drew the pair; ReShade keeps one
  constant buffer per effect and the graphics card drew the right eye's sky after the next pair's left present had written its
  values - the right eye's sky came out one pair ahead of its world and shook as the head turned. Now each pass writes only its
  own eye's sky camera (and FlatVR stamp); a replayed right eye's is kept aside by the left pass and put in by the replay just
  before it draws that eye (vr/sky_view_eyes.h, cleanroom/tests/sky_view_eyes_test.cpp). No wait, nothing to switch;
  D2R_DepthFog.fx unchanged.
- **Fixed: the Amazon's sword blow never landed with Quick strike at frame 1** (the author: every touch pressed the attack, the hero swung, no hit; the
  [debug] melee_press_trace line showed the one-hand swing AMA11HS acting at frame 1). Frame 1 is not read for her; at 2 the blow lands. Quick strike's frame
  is now 2 at the earliest (Settings > Strikes, `[hands] quick_strike_frame`, 2 - 12, default 2; a 1 in a settings file is read as 2).
- **Native OpenXR - eyes by turns, the other eye made from the depth, after d2r-pcvr** (the author: "his GPU takes only 10 ms at
  4096 x 4096" - bearstonem/d2r-pcvr's default stereo draws one eye a frame and rebuilds the other on the GPU, where ours draws the
  whole frame twice). D2R VR Authentic Settings > Home > *3D in the headset* has *Native OpenXR - eyes by turns* again (the ini's
  `[stereo] afr=1`, `pair_per_tick=0` as before): one eye drawn a game frame, left and right in turn - about half the graphics
  card's work of the default "one pass, two pictures", which draws everything twice (shadows, lighting, effects). New: the eye
  not drawn this frame no longer goes to the headset as it was last drawn, at its older pose (the headset turned it - rotation
  only, so a step or a moving monster showed a frame behind in that eye). It is made on the graphics card for this frame's
  head (vr/turn_warp.cpp, vr/turn_math.h; vr/xr.cpp keeps each eye's picture, the depth our effects copy and its camera): (1) its
  own last picture turned to the head; (2) moved with the depth it was drawn with - a grid of 4 pixels, each point put back at
  its distance and seen from where the eye is now, what the hero holds within 1 - 1.6 m going with his step and his body's turn;
  (3) the eye drawn this frame moved across with its own depth over it, so monsters and missiles stand where they are now in
  both eyes; a cell across an edge of its depth is cut, and the older picture shows there (what that eye cannot see behind an
  edge). Both eyes go at this frame's pose. Stereo tab: *Eyes by turns: make the other eye from the depth* (`[stereo]
  turn_warp`, on) and *... with the eye drawn this frame over it* (`turn_from_drawn`, on), live; unticked, as before. First person
  (F4); F2, the menus and the other views as before. A frame whose depth or camera is missing, or a picture older than 0.1 s,
  goes as before. The log says, every 10 s, how the other eye was made. Not yet: a snap turn is made from the turned picture, not
  drawn as a pair.
  - **Fixed after the author's first try: "everything we draw is doubled - the bow's energy orbs, the medallions on the hands, the
    labels over monsters".** They have no depth of the scene: moved with the depth behind them, the eye made showed them twice.
    Now they are left out of the picture the warp moves and drawn after it, for each eye on its own picture, from its own camera:
    D2R_DepthFog.fx has `OverlaysApart` and two techniques of its own, `D2R_Overlay` / `D2R_Overlay_R` (the HUD shift, the game's
    labels laid back, the hitboxes, the wrist orbs and medallions, the bow's orbs and the arrow's glow, the low-life vignette),
    which vrcam renders on the back buffer and on the eye made (`replay::DrawOverlays`, xr.h TurnKeep / TurnMake / TurnFinish).
    The game's labels are taken out of its picture for it (as with the labels' own settings) and laid back per eye. In the eye made
    nothing of the scene hides them (the depth bound there is the other eye's). Needs the new D2R_DepthFog.fx with the DLL; an older
    effect: as before.
  - **Fixed after the author's third try: "the depth broke in the by-turns mode, the medallions double again".** The try before
    (the overlays apart, nothing hiding them in the eye made) had no doubling - "it just worked"; the next one hid the orbs in the
    eye made by its own depth, moved with its picture (the author's idea: the warp moves the last frame's depth with the same grid
    and vectors into a depth picture of its own, D2RVR_DEPTH_MADE), and the wrist medallions doubled again. That depth is the grid's:
    the eye drawn's depth at 4-pixel cells, the nearest of each cell's texels, cut at every edge of the hand, with the eye's own
    older picture - the hand where it was a frame ago - under the cuts. A medallion lies on the wrist, right at the hand's edge, so
    there the depth it is set against is not the hand the picture shows, and it was cut otherwise in the eye made than in the eye
    drawn; and in any present the made depth was not bound for (the first, an effects reload, a new size) the eye made took the
    eye drawn's depth instead - the other eye's hand. Now, by default, as in the try that worked: in the eye made nothing hides the
    overlays, and the warp draws the picture alone again (one target, as before). The made depth is behind its own key,
    `[stereo] turn_overlay_depth` (Stereo tab: *... its overlays hidden by its moved depth (test)*), **off**; on, it is as the third
    try with two fixes - never the other eye's depth (nothing hides instead while it is not bound, and a new texture at the old
    one's address is bound again), and a frame whose eye was not made (its last picture as it was) clears it (nothing hides) rather
    than leave an older frame's. Copy the DLL and D2R_DepthFog.fx together. Unit test: turn_math_test *overlay* (which depth hides
    which eye's overlays); the effect compiles with ReShade's FX compiler (6.3.3).
  - **Fixed after the author's fourth try: "still doubled - always, standing still too; only the wrist medallions and the bow's
    orbs, the labels, the HUD and the doll not; they simply go into both frames".** The picture the warp moves had none of them
    (each eye's kept before they are drawn) and each eye's were drawn from its own camera - but which depth hides them was told
    to the effect by a flag per eye (`OverlayNoHide0/1`), and which eye is the made one flips every frame. On D3D12 ReShade keeps
    one constant buffer per effect, in memory the CPU writes, and copies the uniforms into it at every technique it renders -
    the next frame's effects overwrite it while the graphics card, running a frame behind, has this frame's overlays still to
    draw (ReShade 6.3.3 runtime.cpp `render_technique`: `map_buffer_region(write_discard)`, no second copy). So the eye made's
    overlays read the next frame's flags - "this eye is drawn, the game's depth hides" - and were cut by the game's depth, which
    is the other eye's: a medallion with the other eye's hand cut out of it, every frame, the second "copy" the author saw. Only
    what reads the depth (the medallions, the bow's orbs, the merc tint, the arrow's glow) was hit; the labels and the HUD read no
    flags that flip. Now the eye made has techniques of its own, `D2R_OverlayMade` / `D2R_OverlayMade_R` (the same passes,
    `made` in the effect's `Phantom`: nothing of the scene hides them there, or with `turn_overlay_depth` its moved depth) -
    which eye is which is the technique vrcam renders, never a uniform; the one flag left, `OverlayMadeOn`, changes only with
    that setting. **The DLL and D2R_DepthFog.fx go together** (an older effect has no `D2R_OverlayMade`: the overlays are then
    drawn into the picture as before eyes by turns, and the log says so). The log, every 10 s while the eyes go by turns
    (`vrcam: eyes by turns - overlays, 10 s`): how many presents left the overlays out of the picture the warp moves (else why
    not), how many drew them on the eye drawn and on the eye made, whether the eye the effects took as drawn was the present's,
    and how far each eye's overlay camera is from the camera its picture shows (0 cm; the eyes' gap would mean the other eye's
    place - shown twice). New test key `[stereo] turn_overlays_made` (1; 0 = the author's idea, "draw them only on their native
    frames": none on the eye made, each eye shows them every other frame). Also: `turn_warp.cpp` built with MSVC again (`far`
    is a Windows macro; the local is `farZ`). Unit test: turn_math_test *overlay camera* (the eye drawn's camera moved across
    the gap is the eye made's, and the wrist orb from it lands on the made picture's wrist; unmoved, a tenth of the picture
    away); the effect compiles with ReShade's FX compiler (6.3.3). Not seen in the headset.
  - **Fixed: "the doll keeps coming off and goes twice as big".** Three checks meant for the two passes of one game frame took the
    eyes by turns' two frames for one: the hero's facing given up for the memory search (`StructFreshness`, the facing candidates'
    late / bad counts - a wrong copy of his matrix, of another scale, could be picked: the camera height, the world's scale and the
    arms with it) and the hanging props stepped only every second frame (`props_once_a_pair`). Now only a pair of one game frame
    counts (`PairOfOneFrame`). And in the eye made what is near (the doll, the hands, the weapon) stays where it was in the picture -
    the doll is posed from the head - instead of being moved with the hero's step (`turnwarp::Held`). The default stereo stays "one pass, two pictures". The technique and the shaders'
  shape are bearstonem/d2r-pcvr's (MIT, steamvr/turn_warp.cpp, transport.cpp TurnEye). Unit test: cleanroom/tests/turn_math_test.cpp
  (a point moved with its depth lands where the other eye sees it, the turn, what the hero carries, the edge cut); not yet seen
  in the headset.
  - **Improved after bearstonem/d2r-pcvr v0.3.24 (MIT; his commits 84f560d, 4e2cc65, c72b90d, f4c181f, 6726947, 931d820, 9302744,
    0db1e3a): no hitch when the mode starts, the eye drawn's picture to the pixel along edges, the eye made's own picture where it
    shows the same surface.** (1) The eye made's passes were compiled and built on the draw thread at the first eye made of every
    session - about 0.1 - 0.2 s held (pcvr's measure: 112 ms, 106 of them the compiles). Now the shaders compile once a process
    and a worker builds the passes first (`turnwarp::Warm`); till it is done (a few frames) the other eye goes as it was last drawn,
    and then the draw thread's own build takes a few milliseconds. A shader that fails to compile (or an exception in the
    compiler) leaves its pass unavailable, as before, and never escapes. (2) The eye drawn this frame was moved across in 4-pixel
    cells and a cell across a depth edge was cut whole: the eye's older picture showed through in 4-pixel steps along every edge -
    stale where a head, a hand or a sword had moved since - and a blade thinner than a cell went as the ground behind it (pcvr's
    owner: "ghosting on monsters' heads and on swords"). Now such a cell goes as two flat layers, at its nearest depth and its
    farthest, and each pixel keeps to the layer its own depth is nearer; the edge test reads the cell's corners and its middle, so
    a thin blade is caught. Only what the eye drawn could not see keeps the older picture. (3) The game's glow round a bright
    blade, a monster's hair and its effects layer (spells, flames, Tyrael's wings) have no depth: moved across from the eye drawn
    with the depth behind them they stood where that eye saw them - a ghost blade, a second outline of hair, the scene through the
    wings. Now the eye made keeps its own older picture, moved with its own depth, wherever it shows the same surface as the eye
    drawn's (within 2 pixels of parallax), by `[stereo] turns_keep_own` (Stereo tab: *... the other eye's own picture kept*, live):
    **3 where the game's effects are (the default** - pcvr's owner compared them at Tyrael: the wings right everywhere, no ghost
    blade); 1 round near things (the hands, the weapon, a monster close by: within `turns_keep_reach` 96 px of something nearer by
    `turns_keep_gap` 48 px of parallax, a 4096-wide eye's pixels); 4 both; 2 everywhere (that eye then a frame behind the other all
    over - grass, flames, monsters' motion - which pcvr's owner liked less); 0 nowhere, as before. The effects layer is found by its
    binding (two half-float targets with a depth target, the scene's size; vr/effects_layer.cpp, looked at only while this mode is
    on) and read into 16-pixel blocks at each eye drawn's present (`turns_effects_level` 0.02, `turns_effects_reach` 32 px). The
    log's 10 s line says how often the own picture was kept and the effects read, and how many frames waited for the passes. pcvr's
    DEV menu switch for the own picture is this key (live). The overlays apart (D2R_OverlayMade) are unchanged - the medallions and
    the bow's orbs are drawn after the warp, from each eye's camera; D2R_DepthFog.fx unchanged. Not taken: pcvr's removal of Replay
    from its stereo choices (d973882 - its Replay halved the CPU but not the frame time on his owner's PC): our menu's choices stay.
    Unit tests: turn_math_test *split*, *keep*; new cleanroom/tests/turn_warp_gpu_test.cpp runs the passes on D3D12's software
    device (WARP) - the warm-up, a wall / the sky / a post moved with the depth, what the hero carries, the post and a two-texel
    blade to the pixel, the own picture kept where the same, near things, the effects (none below the level or unread), the made
    depth beside; after pcvr's turn_warp_test (both its edge cases fail on the old shader). Not yet seen in the headset.
- **New: `[debug] melee_press_trace` - one log line per attack press of hit by touch, for "the Amazon's touches press the attack but she never strikes"** (the author,
  2026-10-20, test build 0.175: a short sword, every touch detected and the attack pressed with Stand Still held, "presses=15 hits=0 lost=13"; "Direct hit" lands; a
  Barbarian or a Paladin on the same path hits; the A slot was already shown to hold the plain Attack with the bow - the sword set's own A, a slot of its own in the
  game's controller table, was never told). The line ("melee press N: monster M, R hand - <verdict> | ...") follows ONE press to what the game did with it and starts
  with the first step that did not happen: A never reached the game's pad poll / the A slot's skill was read as a skill, not the plain attack / the game never read the
  slot / the cast went along the hand / the game asked for its target but was not given the touched monster / the hero never went into an attack mode / no hit came.
  Then each step: the game's pad polls while the press was live (how many had A, when A went down and up, every button seen, the game's Stand Still at the first A);
  the game's two reads of the pressed slot (the cast's slot selection and the unit's hand-skill setter: the slot index, what it holds, what it was answered as); the
  controller's target lookups (the cast flags' own look that clears Stand Still, the resolver's, its hover's, the map click's: how often, what unit each was given and
  whether it was the touched monster); the resolver's verdict (directed along the hand or the game's own target); the hero's mode at the press and the first attack
  mode after it, with the animation row and its action frame (`[hands] quick_strike`'s cut shows there); the hit counter's verdict; and what the plugin held at the
  press - the weapon set, each hand's item code, the A slot and the weapon set's bank as the skill panel reads them, the armed melee skill, the bow quiver's armed
  shot, the javelin quiver's choice, the bow draw (owns the trigger, aim live), the aim mode, quick_strike's cut. The first 12 presses of a run are told whatever the
  key says; `melee_press_trace=1` tells every one. Read only: nothing a press does changes. Unit test: cleanroom/tests/press_trace_test.cpp (vr/press_trace.h).
- **Fixed: Stand Still never knew a press was hit by touch's** (found tracing the press above). With the plugin's own input the touch's attack goes out through
  direct_input, and the pad hook's "is this the touch's press" was always no: the line said "stand still - press (attack)" for a touch, and an armed melee skill that
  must move the hero (Whirlwind, Leap Attack, Charge) was still pressed with Stand Still held. Now the touch's press is known whichever way it goes: the line says
  "press (hit by touch)", and an armed skill that moves is pressed without Stand Still, as `[gameplay] melee_skill_armed` meant. The plain attack is as it was.

- **Changed: the bow's quiver ARMS its arrow skill instead of putting it on the attack button** (the author, 2026-10-20: "the quiver logic
  works wrong: it puts some skill on the attack button A. It must work the same way as our logic for the armed skills"). The quiver
  (`[bow] quiver` - the orbs, `quiver_next` / `quiver_prev`) set the chosen arrow skill on the A slot through the game's setter; the game
  kept it there after the bow left the hand, the touches press A, and with a sword in hand the game read every one as Multiple Shot or
  the like and did nothing ("presses=9 hits=0"; "Direct hit" bypasses A and did hit). Now, as `[gameplay] melee_skill_armed`: choosing
  only arms the skill - our own state, shown in the mana medallion (`quiver_icon`); nothing is written into the game's skill slots, the
  pad's slots or A. Each release of the bow's string (the draw; a crossbow has none) is read by the game as the armed skill through the
  slot-skill hook the armed melee skill uses (one release, one cast; the game's own mana, cooldown, missile) - the plain arrow when the
  hero cannot pay it (`quiver_plain_no_mana`), the icon faint then. With no bow or crossbow in hand the armed arrow skill does nothing:
  the touches, the trigger and A are the plain attack or the armed melee skill. The same orb again (or the plain shot's, or a button
  cycling onto it) disarms. Cleanup: with the quiver on, a bow skill on the A slot (the old quiver's, kept in the save) is given back as
  the plain attack at the hero's load and whenever no bow or crossbow is in hand - the log says "bow quiver - the A slot holds the plain
  attack again". The log: "bow quiver - Multiple Shot armed ...", "a release: the armed skill / the plain arrow - too little mana",
  "bow quiver: the slot's skill (Attack, 0) was read as the armed Multiple Shot". No key removed (all still read). Unit test:
  cleanroom/tests/bow_quiver_armed_test.cpp. Not yet tried in the game.
- **Fixed: hit by touch on the Amazon - "the monster's capsule turns red but the strike is not pressed"** (the author, with a spear and
  with a sword). The touch did press the attack; the game read it as a skill it would not cast with that weapon. Two ways it came there:
  (1) the bow's quiver loads its arrow skills onto the attack's slot (A) and nothing took them off when the hero took a spear or a sword:
  every attack went to the game as Multiple Shot or the like - refused for a melee weapon. A bow skill on that slot with no bow or
  crossbow in hand is now read as the plain attack (or the armed melee skill); the log says "bow quiver: the slot's bow skill (N) was
  read as ... - no bow in hand". (2) `[gameplay] melee_skill_armed`: a spear skill armed by its button (Jab, Power Strike, Impale,
  Charged Strike, Fend, Lightning Strike - a spear or a javelin; Poison Dagger - a dagger) stayed armed when the weapon changed, and
  every touch was that skill. A skill the weapon in hand cannot use is now the plain attack ("melee armed: ... needs a spear or javelin
  - the weapon in hand cannot: the plain attack"). Unit test: cleanroom/tests/melee_arm_fit_test.cpp (vr/melee_arm.h NeedOf / Fits).
  Not yet tried in the game.
- **Fixed: the hit-by-touch capsule of spears, katars and claws did not lie on the weapon as drawn** (the players: "the spear / the Assassin's
  katar does not hit"; the author's screenshots: the spear's capsule shifted back along the shaft, past the hand, the head bare; the katar's at
  right angles to the blade, over the fist, in both hands). The capsule is now the weapon's own model: its tip and butt from the game's mesh
  bounds (the spear's head on +Y); the Assassin's katars and claws lie along the bone's Z, from the knuckles to the point, the sign of that axis
  read from the game's own pose for each hand.

## 0.174

- **New, off by default: `[debug] flat_pick` - a log for "monsters cannot be targeted after a while" in flat first / third person** (a player: "aiming at
  their feet is fine, but randomly monsters become impossible to target, sometimes in 3rd person too, fine in the normal camera"). With the mouse look on,
  each mouse press logs what the game has under the crosshair (its own hover: a monster, an item, none), the pixel its mouse code last asked a ray for
  against the middle of its picture, where that ray meets the hero's ground and how steeply, and the cursor, focus, Alt / Shift and menu state; once a
  second it says when the game's pixel leaves the middle, it stops asking for a ray, or its window loses the keyboard. Read only, live; nothing the game
  does changes. The diagnosis and the fixes it would decide between: docs/notes/flat_targeting.md. Unit test: cleanroom/tests/flat_pick_test.cpp
  (vr/flat_pick.h).
- **New: how you play, standing or seated** (the author, after the fix below: "if I play seated, the recenter button must set
  the hero's height. Say I calibrated the body standing, then sat down: now I look from the middle of the hero, not from his
  head, and recenter does not lift the view either - it stays crouched"; native OpenXR, VR first person F4). D2R VR Authentic
  Settings > Body > *How you play* (next to the measurement) and the headset settings' Body page (its new top row,
  *Standing* / *Seated*), `[calibration] seated`, *Standing* by default, live. *Standing*: exactly as below - the hero's eyes
  at the floor plus your measured eye height; a recenter sets straight ahead and where you stand, not the height (a recenter
  taken crouched leaves the standing height where it is). *Seated*: the hero's eyes are where your head is when you recenter
  (F11, the headset's own recenter, the panel's *Recentre view*) - sit, recenter, and you look from his eyes again; the crouch
  ([body] crouch) counts its drop from there, so leaning forward in the chair is no crouch until it passes *Crouch: head lower
  than this* (20 cm). *Calibrate body* / *Measure...* seated reads only the span between your hands: no floor needed, the
  head's height not read (sit upright, arms out, the same triggers); it writes `arm_span_m` alone and keeps the `eye_height_m`
  a standing measurement stored, which stays the world's scale (none stored: *My height* / BodyWalk's Body Height, as
  unmeasured); the arms are fitted to the new span as before. The prompt in the headset says so ("Seated: measure my arm
  span"). Played standing again with only a span stored: nothing measured in use until you measure standing. The log says
  which ("calibration - played seated", "standing height: played seated"). Unit tests: cleanroom/tests/body_calibration_test.cpp
  (*seated*), crouch_test.cpp (*play seated*, *recentre crouched*), calibration_test.cpp (Windows), tools/hset_test.cpp (the
  Body page's row). Not yet seen in the headset.
- **Fixed: after measuring your height standing, the hero was small, under you** (the author: "when I stand up and press
  calibrate, the character ends up small under me"; native OpenXR, VR first person F4). The camera puts the hero's bind-pose
  eyes at the height your eyes have standing, and moves from there with your head. That standing height was always your
  head's height at the last recenter - and a recenter is taken wherever the head is at that moment: the session's first frame,
  F11, the runtime's own. Recentred seated (1.18 m) and measured standing (1.69 m), the camera stood 51 cm over the hero's
  eyes at his scale: the hero looked about three quarters of your size, under you. The measurement itself was right (the eye
  height over the STAGE floor, the world's scale from it); only the camera's zero did not use it. Now, with *Use the measured
  height and arm span* (`[calibration] use=1`) and the headset's floor, the standing height is the floor plus your measured
  eye height - the same floor and the same head the T-pose read - so standing upright you look from the hero's eyes whatever
  height a recenter was taken at, and a recenter only sets straight ahead and where you stand. Without a measurement, or with
  no STAGE floor, or for a frame with no place for the head: the recenter's height, as before. The log says which
  ("openxr - standing height: ..."). bearstonem/d2r-pcvr re-zeroes its camera at the head where the T-pose was taken (and has
  a "reset eye height" action); here it is the measured height over the floor, which a later recenter does not undo. Nothing
  tuned by hand: the hero's eyes, your measured eyes and the floor. Unit tests: cleanroom/tests/body_calibration_test.cpp
  (*stand*, with the author's numbers), calibration_test.cpp. To play seated: *How you play: Seated*, below.
- **The hero crouches when you crouch, back, from d2r-pcvr** (bearstonem, MIT; VR first person F4 only; its own tick -
  Settings > Body > "Hero crouches when you crouch (F4)", `[body] crouch`, off by default). Taken out of the release after
  the author's test, where crouching did nothing: the crouch was counted from the head's height at the last recenter, the
  same zero as above - recentred seated or at the session's start, a crouch from standing hardly got under it. It now counts
  from the standing height above: your measured eye height over the floor (else the recenter's, so recenter standing).
  Crouch in your room and the camera already went down with your head; now the hero's body goes down as far, his knees bent
  again - thigh and shin at their own lengths - and his feet kept where the game's animation puts them, so looking down you
  see your crouched legs, not your chest in your face. How deep is the camera's own drop at the camera's own scale (the
  hero's bind-pose eyes over your measured or set eye height), no deeper than his legs fold: nothing tuned by hand. A dip
  of less than *Crouch: head lower than this, cm* (`crouch_cm`, 20) below your standing height leaves him standing; he
  stands up again once the head is back within half of it, eased in and out, and stands if the head's height is lost for a
  quarter second. Only the drawn bones: where he stands, his hit box, his walking and every game state stay the game's. The
  legs are found in each rig by its own bones (a foot or ankle under a knee and a hip), d2r-pcvr's way. Unticked, and in
  F1, F2, F3 and the flat views: exactly as before. Unit test: cleanroom/tests/crouch_test.cpp (*standing*: the headset's
  case); not yet seen in the headset again.
- **Fixed: the bow's energy orbs shone through what is in front of them** (the author, in the headset: the quiver orbs, VR first person F4, were drawn over things nearer
  than them). They now take the game's depth the way the wrist medallions do: through the ball, the ray's own way into it is set against where the game's picture has its
  surface - nothing of the ball where the scene is nearer than its front, and a surface sunk into it hides the part behind it - and the halo beside it by the ball's middle,
  with the medallions' soft edge. The orb the free hand is at is no longer drawn over the hand (it was, at three quarters, and the orbs the hand neared at a part of that):
  fingers in front of it hide it, the ball round them shows. The effect only (reshade/D2R_DepthFog.fx, the QUIVER-ORBS block); no new key. Copy the new effect in.
- **New, on by default: the arrow's fire goes out at the release** (the author: "the flame on the nocked arrow does not disappear at once when the arrow is released"). With an
  elemental arrow skill the fire, the frost or the arcane light that the effect draws on the bow's own arrow ([bow] arrow_fx) stayed on the arrow in the hand until the game hid
  that arrow, a moment after the shot. It now goes out at the release that shoots, or a delay after it the player sets, and lights again on the next arrow: a new draw, the game
  showing an arrow again after the shot, another skill chosen. D2R VR Authentic Settings > Bow > *Arrow effects: out at the release* (`[bow] arrow_fire_cut=1`) and *... after, ms*
  (`[bow] arrow_fire_cut_ms`, 0 = at the release, up to 2000; a new draw or the next arrow before then drops it). Only our own light on the arrow: the game's missile, its
  effects and its damage are the game's, untouched. Off: as before. F4 only, live. Unit test: cleanroom/tests/arrow_fire_cut_test.cpp (vr/arrow_fire_cut.h).
- **Fixed: the map no longer came into the hand after a change of the window or render size** (a player, native OpenXR, `[hud] map=1`: "once I
  change Stereo > Square window size, px, the map stops working", with Home > Resolution (VR render size) on the window's size, "until you
  reinstall the mod using default settings"). The map is taken out of the picture by the game's scissor box for it, looked for in a top corner of
  the interface area. That area was worked out from the Safe Screen Percent in Settings.json, read once when the plugin starts, against the size of
  the interface's texture. The box had to be within 7 % of the picture's height of that corner, with its side the safe margin in from the
  picture's edge. With the picture the window's size, at any size other than the shipped 1536 square, the area could be somewhere else: a file
  number that is not the game's (no key or none yet read as the whole picture, a size changed while the game runs), a texture bigger than the
  viewport the game draws through, or a box at the picture's very edge (a wide window). Then nothing was cut and the orb stayed empty. A reinstall
  brought it back only because the default ini has the render size at a 1536 square again. Now the area comes from the game's OWN interface
  scale (asked every quarter second; the area is 2160 of its units tall), in the viewport the interface is drawn through. The file's number is
  only a second guess, and the box's side may be anywhere from the picture's edge to the area's side. Nothing in the test is measured in pixels,
  so a window, render size or interface size of any kind works, and a size put back works at once. The box's size and top are tested as before
  (fractions of the picture). No new key. The log says where the box was found ("hud: the map's box ... canvas ...; interface area ... by the
  game's scale, ... by Settings.json"). After ten seconds with no box, it says which boxes the game drew instead ("hud: no map box ...": the map
  off in the game with Tab, or not where it is looked for). Not tried in the game yet. Unit test: cleanroom/tests/minimap_box_test.cpp
  (vr/minimap_box.h; 1920x1920, 2400x2400, 1353x831 and 2560x1440 at interface sizes 80 and 100, both corners).
- **New, on by default: the game's sounds turn with your view** ([audio] head_follow, listener_turn; D2R VR Authentic Settings > Camera > Sound). Monsters, NPCs
  and streams are heard from where they are around you as you turn the head, the stick or the mouse - stock D2R pans by the flat camera's screen only, so in VR
  a monster you turned away from still sounded where the flat camera had it. Music, ambience, the interface and the hero's own sounds stay in the middle. Two
  FMOD hooks from bearstonem/d2r-pcvr v0.3.21 (MIT, contributed there by ChuckRibbits): a voice's screen offset becomes a direction on the ground at its own
  distance (the game's loudness and falloff), and the listener is turned along the view every 10 ms. Stereo only; with Windows' Mono audio on the log says so.
  The listener hook has to be in when the game sets its sound up, at the start: ticked while the game runs, the turn needs the game started again. On another
  game build nothing is hooked and the log says so. Not tried in the game yet. Unit test: cleanroom/tests/spatial_audio_test.cpp (vr/spatial_audio.h).
- **New, off by default, a test: item labels down to the bottom of the picture** ([hud] labels_whole_eye; Settings > UI: F4 body > Item labels on the ground; every
  view). The game drops a label whose bottom passes its interface's height; bearstonem/d2r-pcvr v0.3.23 found that where the interface is smaller than the
  picture (his native VR's widescreen band of a square eye) items low in the view - the one you look down at - had no name, with Show Items held too, and gives
  that one check the whole picture's height. Ours lays the interface over the whole picture ([render] vr_resolution_ui), so we may not have the problem: to be
  tried with and without the tick (Safe Screen, a square render size). The log says "item labels kept down to the picture's bottom" the first time it changes
  anything. Unit test: cleanroom/tests/label_area_test.cpp (vr/label_area.h).
- **On in the settings that come with the mod** (the author's own, as he plays): the hero crouches when you crouch (from 5 cm down),
  Whirlwind armed by its button, the javelin's hit-by-touch tuning, and the sound turned with your view without the listener's own turn
  ([audio] listener_turn 0). Each is still its own tick in D2R VR Authentic Settings; an update that keeps your settings file changes none of them.
- **D2R VR Launcher 0.3.5** comes with this Setup (bearstonem's review): "System default" really is the system's OpenXR runtime (an inherited
  XR_RUNTIME_JSON is cleared), Unleashed's own install text, and a Discord link on each mod's card.

## 0.173

- **The map in the hand with the game's map in the middle** (a player: native OpenXR drew the map across the view, no map in the hand). The map comes into the
  hand only from a top corner; the plugin put the game's map on the right in Settings.json at its start, which holds only when it runs before the game reads the
  file, and the game's options could put it back in the middle for the session. Now, in VR with `[hud] map=1`, the game's live map mode is read and a map in the
  middle (the whole-screen map) is put in its corner by the game's own setter - the right one, or the left with `[hud] map_corner=1` - in native OpenXR and FlatVR
  alike, for the running game only. A map already on the right or the left is never touched: nothing changes for those players; no new key. On another game build
  (the setter's bytes not as 3.3.93787 has them) nothing is done and the log says so. The setter and its globals are bearstonem/d2r-pcvr's finding (MIT).

- **New, off by default: the right stick's up / down tilts the view in VR F2** (GitHub issue 11, a player's wish: "there is no up/down camera with a
  controller in VRmode" - with an Xbox pad on "Mono - a flat screen" only the mouse and the headset tilted it). D2R VR Authentic Settings > Camera > F2 Third
  person > *Right stick up / down tilts the view*, `[camera] stick_pitch_f2=1`: pushing the right stick up looks higher, down looks lower, on top of the horizon
  (`[camera] pitch`); left / right still turns. The turn's dead zone (a quarter of a push) applies and a push more sideways than up / down only turns, so a turn
  does not tilt by accident. `stick_pitch_speed` (degrees per second at a full push, 60), `stick_pitch_min` / `stick_pitch_max` (the highest and lowest look, in
  `[camera] pitch`'s degrees: -90 and 0; the camera is clamped there, a `[camera] pitch` already outside is left where it is) and `stick_pitch_sign` (-1 = up
  looks down). F11 puts the tilt back. Needs `[turn] right_stick` (the right stick is the plugin's, the game never sees it). F2 only: F1, F3, F4 and flat mode
  as before; not with native OpenXR's headset view, where the headset has the pitch. Off, nothing changes, bit for bit. Unit test:
  cleanroom/tests/stick_pitch_test.cpp (vr/stick_pitch.h).
- **Fixed: "Show items on the ground (Alt)" on a long press or a touch only flashed** (a player: "it only shows them for a split second then goes away, even
  if you hold the button down"). A game key bound to a button's long-press line (D2R VR Settings > Controls > "a long press") or to a touch line was sent as a
  0.1 s pulse; it is now held from the long press (or the finger's touch) until the button is let go (or the finger lifted), as on a button's plain line -
  so the game's "Show items: hold" keeps the labels up, and "toggle" still toggles once per press. A menu, the focus going or a view change lets it go.
  Actions and the rest on those lines stay one pulse. Unit test: cleanroom/tests/hold_line_test.cpp.
- **New, off by default: melee skills armed by their button** ([gameplay] melee_skill_armed; D2R VR Authentic Settings > Strikes > "A melee skill's button arms it: every hit is that skill";
  VR first person F4, with hit by touch). The author's wish: "for the Barbarian's and the Paladin's attack skills (and whoever else has them), when we bind a skill to a button, pressing it does
  not activate it but remembers it as the chosen one, like the Amazon's, and on every blow it fires and spends mana; if there is not enough mana, a normal blow." With a melee weapon (or fists)
  swung by touch, a press of a pad skill button (B, X, Y, RB, with or without LT) whose slot holds a melee attack skill is not passed to the game: it arms that skill (a short buzz in both
  hands). Every touch the game's attack is pressed for is then read by the game as the armed skill - through the same slot-skill hook the bow's "no mana: the plain arrow" uses - so the cast is
  the game's own: its mana, cooldown, weapon check, animation, and the touched monster as its target; one touch, one cast. A touch the hero cannot pay (the skill's cost in the game's Skills.txt
  at his level, against his mana) is the plain attack, and the skill stays armed. The same button again, or a button with the plain attack on it, disarms; another melee skill's button arms
  that one. The skills are derived from the game's Skills.txt (a class skill for the attack button whose range is melee, h2h): Jab, Power Strike, Impale, Charged Strike, Fend, Lightning
  Strike, Poison Dagger, Sacrifice, Smite, Zeal, Vengeance, Conversion, Bash, Double Swing, Stun, Concentrate, Frenzy, Berserk, Feral Rage, Maul, Rabies, Fire Claws, Hunger, Fury, Tiger
  Strike, Dragon Talon, Fists of Fire, Dragon Claw, Cobra Strike, Claws of Thunder, Dragon Tail, Blades of Ice, Royal Strike and Cleave. Whirlwind, Charge, Leap Attack and Dragon Flight (they
  move the hero), spells, buffs and warcries cast from their buttons as ever; a bow, a crossbow and a thrown weapon are not touched, nor F1 - F3 and the flat modes. It needs the plain attack on
  the attack button (A); a skill bound as a key command (cmd:) is not taken. The log says what was armed and what each touch was ("melee armed: ..."). Unticked: the skill buttons cast as
  before. Not tried in the game yet. Unit test: cleanroom/tests/melee_arm_test.cpp (vr/melee_arm.h).
- **New: the armed melee skill in the mana medallion** ([gameplay] melee_skill_armed_icon, on by default but drawn only while melee_skill_armed is on; Settings > Strikes, under the
  melee_skill_armed tick: "Show the armed skill in the mana medallion"; VR first person F4). The author's design: the armed skill's icon - the game's own, the one the skill panel's tiles and
  the skill wheel draw (the player's own icon cache) - lies on the bottom of the MANA medallion on the wrist, under the blue liquid and the glass (the skill spends mana, so it belongs there),
  cut to a circle with a soft edge inside the dome, never out of it. Full strength while the hero can pay the next hit (the very check the hit makes: the skill's Skills.txt cost at his level
  against his mana); half see-through when he cannot - the next hit will be the plain attack - and bright again, eased, when the mana is back. Nothing armed: no icon. The medallion shape of
  the glass orbs only (the full ball, the game's own globes and the palm panel have no bottom to lie on). Unticked: the medallion exactly as before. Needs the new D2R_DepthFog.fx with the
  DLL (OrbIcon, OrbIconTex; an older effect: no icon, and the log says so). Not tried in the game yet. Unit test: cleanroom/tests/armed_icon_test.cpp (vr/armed_icon.h).
- **The skill arming on the headset panel** (the author: "put its toggle into the overlay's options"): the in-headset settings panel's Melee page has two more rows, "Skill button arms the
  skill" ([gameplay] melee_skill_armed) and "Armed skill in the mana" ([gameplay] melee_skill_armed_icon) - the same keys and defaults as D2R VR Authentic Settings, applied by themselves. The
  list of skills, the icon's sliders and the bow's icon stay in D2R VR Authentic Settings (the page is full: six rows). vr/hset_defs.h.
- **Double Swing as a combo** ([hands] double_swing_combo, off; Settings > Strikes, under "Double Swing by the two hands"; live). The author: "it fires on the next blow at the target, but it
  must be every next blow, like a combo - I strike, first it is an ordinary blow, and every next one must be like a double swing". Ticked: the first touch on a monster is the normal attack
  and every following touch on the same monster - either hand - within the window of the last one is the game's Double Swing (or Frenzy): normal, double, double, double...; another monster
  or a longer pause starts a new combo; a touch the hero cannot pay is a normal attack and the combo goes on. Unticked: the pairs, as before. Not tried in the game yet. vr/double_swing.h.
- **The skill icon in the mana medallion: size, depth and two opacities** ([gameplay] melee_skill_armed_icon_size_pct / _depth_pct / _opacity_pct / _faint_pct; Settings > Strikes, under
  "Show the armed skill in the mana medallion"; live). The author's wish after trying it: its size (30 - 100 % of the medallion's inner circle, 87.5 as it was) and how deep it lies (100 = on
  the bottom, as it was; 0 = as high as the dome lets it - its rim just under the glass, over a low mana's liquid). The height is the dome's own shape, and the effect finds the icon by the
  same ray step it finds the floor with, so it keeps the medallion's parallax; the liquid under a lifted icon is hidden by it. Two opacities: with the mana for the next hit (100) and
  without (35, the old "half see-through"; 0 = out until the mana is back). Needs the new D2R_DepthFog.fx (OrbIconLift; an older one: the icon stays on the bottom).
- **The Amazon's chosen bow skill in the mana medallion** ([bow] quiver_icon, on; Settings > Bow, under the quiver's ticks). The author: "the same for the Amazon - when I choose a skill the
  icon must show inside the mana". The bow skill chosen from the quiver (an orb, a quiver button, the skill panel) shows its icon there while a bow is in hand: full while the hero can pay a
  shot with it (the release's own check), faint when not; the plain arrow: none. The same sliders as the melee icon. Needs [bow] quiver on. Not tried in the game yet.
- **Which melee skills are armed: a tick each** ([melee_armed] s<skill>; Settings > Strikes, "Armed by its button:" in the group "Melee skills armed by their button", a row per class; live). The 34 melee attacks, ticked by default, and the four
  attacks that move the hero - Whirlwind, Leap Attack, Charge, Dragon Flight - unticked by default. An unticked skill's button casts it as before; unticking the armed skill disarms it.
- **Whirlwind armed** (the author ticked it and it "did not work"): it was not in the list at all - Whirlwind, Charge, Leap Attack and Dragon Flight were left out as movement skills, so
  their button passed to the game and cast them as ever, and a hit by touch stayed the plain attack. Ticked now, Whirlwind's button arms it and a hit by touch is the game's own cast of it
  through the same path as the others (the attack button read as Whirlwind, the touched monster its target; its mana by the game's numbers, 12.5 at level 1, else the plain attack): the
  game whirls the hero to the monster's place. One more fix it needed: hit by touch's attack is held with the game's Stand Still ([gameplay] attack_in_place), which would pin a mover in
  place - a touch the game reads as Whirlwind, Leap Attack, Charge or Dragon Flight is no longer held (vr/stand_still_wire.cpp). Not tried in the game yet: whether the game's Whirlwind from
  a short press on a monster whirls far enough to feel right is for the headset to say. Unit tests: cleanroom/tests/melee_arm_test.cpp, armed_icon_test.cpp.
- **On in the settings that come with the mod** (the author's own, as he plays): melee skills armed by their button with the icon in the mana
  medallion (100 % of the inner circle, 10 mm over its floor, 91 % opacity), Double Swing by the two hands as a combo, and the toolbar cut at 0 %.
  Each is still its own tick in D2R VR Authentic Settings; an update that keeps your own settings file changes none of your values.
- **The skill icon over the medallion's floor, mm** ([gameplay] melee_skill_armed_icon_lift_mm, 0 - 10; Settings > Strikes): the icon lies this
  far over where its depth puts it, so the medallion's floor texture does not show through it (the author: they overlapped).
- **Fixed: "Collect logs for support" and antivirus software** (a player: his game's programs were gone after pressing it). The packing
  script now runs from a file in TEMP (no hidden PowerShell with an encoded command, which antivirus programs take for malware), and it
  removes nothing but its own folder there (TEMP\D2R_VR_logs_*).
- **Setup always asks for the game's folder**, an update too (GitHub issue 10): a game moved to another drive, or several copies of it (online and
  offline), and Setup puts the mod where you choose. A last folder without the game in it is replaced by the one Steam or Battle.net names now.
- **D2R VR Launcher 0.3.4** comes with this Setup: before Play it checks that the game was started once without mods (its own settings file in
  Saved Games) - a brand-new install of the game crashed at the logo with any mod until it had been started plainly once. And the launcher's
  publisher, like the mod's author, is natpoh now (the plugin's "by" in D2RLoader's log, the programs' copyright, Setup).

## 0.172

- **New: a first-start guide in the headset**, on by default for a new player. The first time you stand in the world in VR first person (F4) with the
  plugin's own controls, the skill panel's Settings window comes up on its new Guide page: the T-pose measurement offered first (when none is stored), then
  five short pages of what to press for the inventory, the map, the skill panel, the belt, the settings in the headset, the pause menu and the F views. It is
  drawn and paged like the other pages of that window (the laser: Back, Next, Done) and hangs where the window hangs, not before the face. Done or closed, it
  does not come up by itself again ([onboarding] done=1); the Guide page, or Settings > Body > "Show the first-start guide", shows it any time. The idea of
  offering the measurement at the first start is bearstonem/d2r-pcvr's (MIT).
- **D2R VR Launcher 0.3.2** comes with this Setup: each mod's card has its own icon - D2R VR Unleashed's own (bearstonem's) and Authentic's crystal mask.
  A launcher 0.3.1 is updated by this Setup; nothing else in it changed.
- **Thumbstick dead zones against stick drift** (a player's wish: "are you able to add stick dead zone %'s like to counter stick drifts people might have on their
  controllers"). Each VR controller's thumbstick has a dead zone and an outer edge of its own, in % of a full push: D2R VR Settings > Controls > *Thumbsticks: dead
  zone (stick drift)*, `[direct] stick_deadzone_left_pct`, `stick_deadzone_right_pct` (0 - 50) and `stick_outer_left_pct`, `stick_outer_right_pct` (60 - 100). The dead
  zone is radial (the stick's length decides, so a diagonal is not cut sooner than a straight push, and the direction is kept) and rescaled: past it the push starts
  again from zero - no jump - and it reaches a full push at the outer edge, which saturates. It is applied once, where the stick comes in (native OpenXR, and
  FlatVR's raw controllers through Game Link), so everything that reads the stick agrees: the walk in every view, the right stick's turn and snap, the belt flick and
  the skill panel's flick, the room walk's "the stick wins", the plugin's own gamepad and BodyWalk through Game Link. A real gamepad and BodyWalk's own pad are left as they
  are. The defaults (0 and 100) change nothing, bit for bit. Only the controllers' values are shaped - nothing of the game. Live; the log says "the left thumbstick: ..."
  when a value changes. Unit test: cleanroom/tests/stick_shape_test.cpp (vr/stick_shape.h).
- **Fixed: the Kurast sewer stairs** (Act 3: Kurast Bazaar and Upper Kurast, down to the Sewers; VR F4). A takes the stairs standing:
  when the game finds nothing to interact with there, the sewer stairs the head faces nearby are offered to A - only if the game's own
  reach and line-of-sight test accepts them, so never through a wall or farther than the game reaches; travel and quests are the game's.
  Before, the player had to crouch and look at the stairs. And with "Roofs: their underside, not the sky" on, the entrance's walls and
  stairs are drawn from behind too, instead of the sky through them. From bearstonem/d2r-pcvr (MIT). The black end of the stairwell
  (the model has no wall there) is not in this version.
- **New, off by default: a shield put away does not block** ([hands] shield_stow_no_block; D2R VR Authentic Settings, under "... and the shield can be put away and
  drawn from behind the left shoulder"). With the shield away behind the left shoulder the game rolls no block for the hero - as for a hero with no shield - and goes
  on to its own dodge check. Drawn, the shield blocks by the game's own chance and roll as ever; its Defense stays. The game's block check is the one bearstonem's
  d2r-pcvr found (MIT); a build where it is not found leaves blocking as it is and says so in the log.

## 0.171

- **Fixed: a staff held in both hands aimed off to the side** (spell aim by the drawn hand or staff, the weapon's weight on, VR F4). The spell and the spell's ray now go along the staff as it
  is drawn between the two grips, butt to tip, from its tip. In 0.170 they went along the line between the two controllers, and each grip sits a fist's width from its controller - with the
  hands near each other that line ran far off the staff. (Known issue 26 of 0.170, docs/KNOWN_ISSUES.md.)
- **On in the settings that come with the mod** (the author's own settings, as he has them): skills on the right trigger and on A ([openxr] skill_rt_a), the RUN / WALK button on the skill
  panel ([openxr] skill_run_walk) and a spell's aim distance that follows how far the arm is stretched ([bow] aim_distance_by_arm). Each is still its own tick in D2R VR Authentic Settings
  (the two skill panel ticks: Stereo > Native OpenXR, in the skill panel rows under "Skill panel style"; the aim distance: Weapon Adjust > Aim distance by the arm), and an update that keeps your own settings file changes none
  of your values.
- **A first-start guide in the headset** ([onboarding], D2R VR Authentic Settings > Body > First-start guide). The first time you stand in the world
  in first person (F4) on native OpenXR with Controls without BodyWalk, a panel in front of you offers the T-pose measurement (when none is stored:
  the right stick up measures, down goes on), then five short pages of what to press to open what: walking and fighting, the inventory, the map,
  the pause menu, the skill panel, the settings in the headset, the belt, weapons, the bow and the F views. The right stick up turns the page,
  down goes back, held up a second closes it. It presses nothing and takes no button (the game never gets the right stick, and the turn is its
  left / right only), the game runs on behind it, and it hides while a menu, the skill panel, the settings panel or the measurement is up.
  Finished or closed it is done ([onboarding] done=1); the headset settings' Body page has a *First-start guide* button to see it again, and
  *Show the first-start guide* unticked keeps it away. An ini from before 0.171 counts as done when a measurement is stored. The idea of
  offering the measurement at the first start is bearstonem/d2r-pcvr's (MIT); the flow is vr/first_start.h (first_start_test).
- **The texts checked against the code** (docs/text_audit_0.171.md): the tooltips of D2R VR Authentic Settings, the comments of d2r_vr.ini,
  README, SETTINGS.md and docs/. Among them: the hero's own effects (Bone Armor) say they do not work yet; the game's Ambient Occlusion can
  be Max (since 0.164 it reads its depth from our camera, and nothing depends on Medium); the controls in README are the plugin's own layout
  as it is now; a Quality preset says that it switches Native OpenXR off; tabs that no longer exist (Bow, Javelin, Hands) are named as they
  are now. Corrections to the notes below: in 0.170 "Bow tab > A spell's aim distance" is Weapon Adjust > Aim distance by the arm, and the
  hero's own effects are off in the settings that come with the mod; in 0.166 Quick strike is on the Strikes tab.

## 0.170

The main points (details below and in docs/d2rloader_mod.md):

- **D2R VR is a D2RLoader mod now.** Everything of it lives in one folder, `mods\D2R-VR` in the game's folder: the plugin, `d2r_vr.ini`, its logs, ReShade with its
  ini and presets, the effect, FlatVR's add-on, the weight and grip libraries, BodyWalk Portable, the licence. Nothing of it is in `d2rloader\plugins` or beside
  `D2R.exe` any more (but `D2R_VR_Settings.exe`, which stays where you look for it), so it no longer shares file names with bearstonem's d2r-pcvr
  (the mod `D2R-PCVR`) and the two cannot replace each other - D2RLoader's `-mod` picks which one runs. Setup writes the mod's `modinfo.json` and sets
  `default_mod = "D2R-VR"` when no mod was the default, so a plain start of D2RLoader still runs D2R VR; the shortcut "D2R VR Authentic" and START run
  `D2RLoader.exe -mod D2R-VR`. An update from 0.169 and older moves what was yours once - your `d2r_vr.ini` byte for byte, its backups, your ReShade if D2R VR
  installed it, the sky and ceiling pictures, the libraries, BodyWalk Portable - and removes the old global plugin (D2RLoader would load it under every mod);
  a ReShade of your own beside the game stays. The uninstaller removes `mods\D2R-VR` and the `default_mod` it set, nothing else. docs/d2rloader_mod.md has the table
  of where every file is and who reads it.
- **The shared D2R VR Launcher.** Setup carries the D2R VR Launcher (the VR mods of a game folder in one window, with their updates) and installs it quietly, for
  you only, when it is missing or older; it never blocks Setup. The mod answers it with `vrmod-manifest.json` and an icon in its folder, and Play runs
  `D2R_VR_Settings.exe --start` (the START button without the window; exit code and a log line say what happened). The uninstaller leaves the launcher installed.
- **Another mod's ReShade settings no longer break ours.** d2r-pcvr keeps its ReShade in its own folder and points ReShade there with the
  `RESHADE_BASE_PATH_OVERRIDE` environment variable; left set, our ReShade looked there too, found no ReShade.ini and refused to start (error 1114), so no headset
  session came up. Every `RESHADE_*` variable the game inherited is now cleared in the game's process before ReShade loads (only there), then D2R VR's own
  is set to its folder.
- **A ReShade you already have is used as it is.** A `dxgi.dll` next to `D2R.exe` is your own ReShade: Setup, the uninstaller and the Home buttons no longer rename, move or
  delete it (the old Setup turned it into ReShade64.dll or removed it). D2R VR's effect and pictures are in the mod's folder (`mods\D2R-VR\reshade`) and your ReShade looks in the
  game's, so Setup adds two items to the end of `EffectSearchPaths` and `TextureSearchPaths` in your `ReShade.ini` (a copy is kept as `ReShade.ini.before_d2rvr`; the uninstaller
  takes them out again, your file comes back byte for byte), and the plugin loads FlatVR's add-on from the mod's folder. Tested with a real ReShade 6.8.0 (a small D3D11 probe
  program lets it initialise and reads its log). Without a dxgi.dll D2R VR has a ReShade of its own in the mod, as before. The two old Home buttons are replaced by *Add D2R VR's
  folders to my ReShade* and, as the one explicit choice, *Put my dxgi.dll aside* (`dxgi.dll.off`, asks first, deletes nothing).
- **Thumbsticks: tell when SteamVR bound nothing to them** (native OpenXR, our own controls). A binding saved by SteamVR for another mod, or an older version, can leave
  the thumbstick action without a source while the buttons work: no walking, no turning, and nothing said why. D2R VR now counts the bound sources, reads a spare
  stick action when the first has none, and Home > Status > *Thumbsticks* says green, orange or red - with SteamVR's way out (Settings > Controllers > Manage Controller
  Bindings > D2RLoader > default). And a real gamepad next to the plugin's own pad is no longer overwritten in direct mode: buttons are ORed, the larger trigger and the
  pushed sticks win, and it walks. docs/direct_input.md.
- **Native OpenXR never hangs the game on a busy headset.** A player went from FlatVR's 3D depth through BodyWalk Lite back to Native OpenXR: the headset stayed empty and the game
  stood until Task Manager - the runtime's frame wait never returned while FlatVR held the headset. The two blocking runtime calls, the frame wait and the session's start, now run on
  a thread of their own and the game waits for them a fraction of a second ([openxr] wait_frame_ms, 150); if they have not come, that frame gets no picture in the headset and the
  game goes on flat (the log says so, and when the headset answers). And no headset session is started over a FlatVR that holds the headset and cannot be stopped (BodyWalk Lite has
  no Game Link): Home > Status says "close BodyWalk and restart SteamVR", and it is tried again every 10 s while FlatVR runs. On by default ("Never hang on a busy headset",
  [openxr] busy_guard); needs a game restart.
- **Native OpenXR: the eye distance as a setting, and canted displays.** The distance between the two eyes of the stereo pair is the headset's own, as the runtime reports it
  (default: Stereo > Native OpenXR > "Eye distance: from the headset", [openxr] ipd_auto 1). Untick it to set your own (50 - 80 mm, [openxr] ipd_mm, live) for a headset whose IPD
  was not picked up or was picked up wrong: the two cameras keep their middle and their turns and stand that far apart. The settings show what the headset reports. And on headsets
  whose displays are canted (Pimax, Varjo, Index and others) both eyes are now drawn and submitted in the head's frame ([openxr] canted_eyes, on): the right eye used to be drawn with
  one turn and submitted with another, and the eyes came out crossed with the centre stretched (a SteamVR player's report). Parallel headsets are unchanged, bit for bit. The log says
  once which eye distance is used and how the eyes are turned.
- **A spell's aim distance can follow how far your arm is stretched** (own tick, off: Bow tab > "A spell's aim distance follows how far the arm is stretched", [bow]
  aim_distance_by_arm; VR first person). As in flat Diablo, where a click near the hero spreads Teeth wide and a far one keeps it in a tight cone: an arm held at the body aims
  the spell near ([bow] aim_near_cells, 3), an arm stretched out along the aim aims it at the weapon's own distance. Spells only - a wand, an orb, bare hands, a staff with one
  hand on it; a staff held in both hands always aims at its far distance (the tightest cone), and a bow, a crossbow and throws keep their fixed distance. The stretch is measured
  on your own arm (the drawn shoulder and the length to the grip, fitted to your T-pose span or your height) and smoothed a little. It only chooses where the game's own spell is
  aimed, as a click would. Live.
- **Hit by touch in the headset's panel, and the least blade speed up to 20 m/s.** The headset settings have a Melee page (after Belt): the Hit by touch tick, the least blade
  speed that hits and how far the blade moves to start a stroke - the same keys, ranges and defaults as D2R VR Authentic Settings > Strikes, written live. The least blade speed (the general
  slider and each weapon kind's own) goes up to 20 m/s (it stopped at 8); the step (0.1) and the shipped value stay. The shield bash keeps its own 0.3 - 8 m/s.
- **The crossbow: a new hold and a hand-over are in the build, but OFF, and not working yet** ([hands] xbow_barrel_forward, xbow_handover, xbow_support_game, xbow_support_close - all 0 in the settings that
  come with the mod; VR first person F4). The crossbow can be laid by its own drawn barrel along the controller, its hands can be on the game's own roles (the holding palm on the trigger grip at the back, the
  support hand on the fore-grip under the prod), and it can be passed from hand to hand - take it with the free hand at the fore-grip and let go with the other, or squeeze the free hand's grip near the
  holding hand's - with every try logged ("crossbow hand-over: ...", "ranged holster - the right hand's grip at ..."). In the author's test of 2026-10-18 the crossbow's grip was still off and the hand-over did
  not work: it is to be fixed later, and 0.170 ships the old crossbow settings (docs/KNOWN_ISSUES.md). The ticks stay in D2R VR Authentic Settings (Weapon Adjust > Crossbow), marked as not finished.
- **The hero's own effects, faded: in the build, but OFF and not working yet** ([render] dim_own_effects, dim_own_effects_pct, dim_own_auras_pct; off in the settings that come with the mod;
  Body tab > "Draw the hero's own effects fainter"; VR first person F4). A player's request: Bone Armor wraps round the eyes of the first-person view and blinds. The overlays of the hero's
  own states - Bone Armor (and its casting flash), Frozen / Shiver / Chilling Armor, Energy Shield, Cyclone Armor, Blade Shield, Fade - and the aura rings at his feet are to be faded with the
  effect's own "Fade" parameter, the one the game fades an overlay with, to a percentage you set (100 = as the game draws them, 0 = hidden). Two tests in the game: the first did nothing (the
  hooks that watch the creation of overlays went in after the buffs of the session were cast; they now go in at the start, ticked or not, and the log says what they see); the second proved the
  whole chain - the hero's key, the overlay names, the entities, the effect lookup - but the lever was the effect's playback speed, not its fade. Now the log shows the "Fade" parameter being
  set on the effect (the game's name object reads "Fade"; "bonearmorfront ... \"Fade\" set to 30 % on effect ..."), and still nothing changes on screen. So it works up to setting Fade, and no
  further: to be looked into later (docs/KNOWN_ISSUES.md, docs/own_effects_recon.md). Visual only: the buff and its numbers are the game's.
- **Known issue: a staff in both hands - the spell's ray starts at the wrist and goes to the side.** A regression from the 0.170 grip changes. To be fixed in 0.171 (docs/KNOWN_ISSUES.md, entry 26). The other known issues of this build are the crossbow's new hold and hand-over (entry 24) and the hero's own
  effects (entry 25), both in the build but off.
- **Double Swing by the two hands** ([hands] double_swing_by_hands, off; Strikes tab > "Two weapons: Double Swing by the hands"; VR first person F4, hit by touch on, a melee weapon in
  each hand - a Barbarian). The first hand's touch on a monster is the game's normal attack, as ever; when the OTHER hand then touches the SAME monster within 700 ms
  ([hands] double_swing_window_ms) the second strike is the game's own Double Swing instead (or Frenzy; [hands] double_swing_skill says which first when both are on a slot): its pad
  button is pressed once and the monster is handed to the cast as its target, the way the shield bash casts Smite. Only when the hero has a level in the skill, it sits on one of
  the pad's ten skill slots and the mana is there; otherwise the second touch is a normal attack too. The swings, the damage, the animation and the mana are the game's. The log says
  every decision once ("double swing: ..."). Not yet tried in the game (docs/double_swing.md).
- **For developers: how the game draws its sky and fog** (docs/game_render_sky_fog.md, no code): what bearstonem's Arcane sky is (a data change plus ReShade, not drawn in the
  scene), the game's own fog fields and fog pass, its pass order (the fog comes after the transparent resolve, so the game has the same fire problem ours had), its skyboxes, and a
  staged plan to draw the fog and sky before the transparent resolve.
- **The settings program is called "D2R VR Authentic Settings".** It is the mod's own settings (the shared D2R VR Launcher and Setup's last page have other things with "D2R VR" in
  the name, and the page offered "Open D2R VR Settings" next to "Open D2R VR Launcher"). The Start menu entry, the desktop shortcut, the last Setup page's "Open ...", the
  window's title and its message boxes carry the new name; the shortcuts of the old names ("D2R VR Settings" in the Start menu; "D2R VR", "D2R VR Authentic" and "D2R VR Settings" on
  the desktop - for all users and for you) are removed when you update, and the uninstaller takes whatever is left. A display name only: the file is still D2R_VR_Settings.exe, and the
  ini, the ids and the launcher's entry are as they were. (The desktop no longer has a "D2R VR Authentic" shortcut: that name opened the settings program; the Start menu's
  "D2R VR Authentic" starts the game with the mod.)
- **The fog spares fire, spells and smoke** (test, off: [fog] spare_effects, World > Fog). Our fog is laid over the
  finished picture by the scene's depth, and the game's flames, spells and smoke have no depth of their own: a torch
  or a Fire Bolt in front of a far wall was fogged like the wall. With it on, the game's own transparent layers of
  each frame are copied at its transparent resolve, and the fog covers only the share of each pixel that is the
  scene behind them. VR F4; read at the game's start. See docs/game_render_sky_fog.md.

- **The Arcane Sanctuary's own sky all round.** In the Arcane Sanctuary the void round the floating platforms shows
  the game's own star layers and cloud noise in every direction, below the platforms too, through our ReShade effect
  ([sky] arcane_sky, on; the Sky tab). The pictures are made on your machine from your own game's files - D2R VR
  Settings reads them (read only) when it opens, after it installs ReShade and with "Make the Arcane Sanctuary sky
  now" on Home, and Setup runs it once; nothing of Blizzard's is shipped. Not in the
  views from above; works with the outdoor sky off too. No fog there while it shows, unless you tick "Arcane Sanctuary: fog
  fades into its sky" ([sky] arcane_fog, on in the settings that come with the mod): then the far platforms dissolve into the sky behind them - its dark and its
  nebula, not its stars. The pictures go into the mod's
  own ReShade folder (mods\D2R-VR\reshade), and the level's flat star cards are taken out in the mod's own data folder
  (mods\D2R-VR\D2R-VR.mpq\data; the game's files are not touched, and a file you changed stays). The idea, the
  effect and the way the pictures are made are taken from bearstonem/d2r-pcvr (MIT), v0.3.18 - thanks.
- **No more hang at a portal into a new area** (native OpenXR; nothing to set). A player's 0.170 test hung twice going through a portal in an act (Arcane Sanctuary into the Palace
  Cells): the new area loaded inside one 421 ms frame of the game without the menu or loading-screen flags ever showing, so the right eye's replay - which waits 240 pairs
  after every area change, because the game frees and makes pictures while one loads - did not wait, ran on through the first frames of the new area and the GPU stopped. The
  replay now also stops when the game sets the area (the last replay is first let finish on the GPU, 200 ms at most) and after any game frame of over 150 ms, whatever the world
  flags say, and starts again after 240 settled pairs. The log says why: "replay - paused (the game set a biome: an area loads)" or "(a game frame of over 150 ms: a load)",
  and "replay - on (the area settled)".
- **The wrist medallions' depth and turn for each hero under a shield** (VR first person F4 only; live; 0 = as before). With a shield on the forearm the ball
  rolls to the inner side of the wrist, and how deep that side lies differs with the hero's model - so for each class (Amazon ... Warlock) there are now two
  sliders under *Under a shield*: **depth (Z)** and **along the arm (X)**, -5 to 5 cm, added to the shield's own Z and X and to the wrist's, in the rolled frame
  and with the roll (`hand_vitals_orb_shield_up_cm_<class>`, `hand_vitals_orb_shield_along_cm_<class>`; the line above the rows says which hero the game has
  now). They act on the wrist that wears the shield only, and only while it does. The place itself still comes from the drawn arm (the hand's frame as the
  skeleton poses it, rolled half a turn about the arm); the model gives no thickness of the forearm, so the depth per hero is what you set.
  - *The medallion's turn per hero under a shield*: three more sliders for each class, **rotation X, Y, Z**, -180 to 180 degrees
    (`hand_vitals_orb_shield_rot_x_deg_<class>`, `_rot_y_deg_<class>`, `_rot_z_deg_<class>`), added to the shield's own rotation sliders and to the wrist's own
    turn - anatomical like them (the same numbers on either wrist are the mirror image), easing in and out with the roll, for the wrist that wears the shield
    only. 0 = as before.

- **With Hit by touch the trigger no longer attacks** ([gameplay] melee_touch_only, on; Strikes > Hit by touch > "The trigger does not attack", and the headset panel's Melee page; VR first
  person F4 with Controls without BodyWalk). A player's report: with Hit by touch on and a melee weapon in hand the right trigger still attacked, so the swing could be skipped - a
  bug of Hit by touch, not a choice. While the blade's touch is live the trigger's attack is not passed to the game for any weapon the touch swings - melee weapons, bare hands and
  throwing weapons (the touch or the throw gesture throws those) - and only a bow and a crossbow keep the trigger, because they are shot and not swung. Spells and skills on the other
  buttons are as they were, and the trigger works as ever where Hit by touch does not run (no tracking, a menu or panel up, another view). Only the plugin's own controller press is
  withheld; nothing of the game is blocked or hooked. Untick it for the old behaviour. The log says what it decided ("melee touch only: ..."). docs/melee_touch_only.md.

- **The skill panel's category strip: shown as soon as the categories are known, a dim note when it cannot be shown, and a log line that says why** (Stereo > Native OpenXR, the skill
  editor's categories tick, on). A player has never seen the strip of the three trees over the skill grid in the editor, and nothing in the code explains it - so this is not a fix, it is
  a way to see the cause. If the editor opens before the game side knows the class's trees, the strip now comes up as soon as they are known, on the page that is showing, not at the
  next editor; if it cannot be shown at all, a dim line where it would be says that the skill categories need the icon cache's skill trees (run prepare_skill_icons.py again); and each
  time the grid comes up one log line says whether the strip is shown and, if not, which of its conditions failed ("skill panel - the editor on slot N: category strip NOT shown (...)").
  docs/skill_wheel.md.

- **Skills on the right trigger and on A** ([openxr] skill_rt_a, off; Stereo > Native OpenXR > "Skill panel: skills on the right trigger and on A"; VR first person F4 with the mod's own controls).
  The right trigger casts the skill on the game's own RT slot and the right A the one on its A slot - and with the left trigger held, LT + RT and LT + A - through the game's own
  buttons, so its mana, cooldown and animation. Put a skill on them in the skill panel's EDIT SKILLS (the Tiles look; the plates are named "Right A" and "R trigger"): point at the RT or
  A plate and pick a skill; "Attack" in the list puts the slot back. A slot with nothing, Attack or Interact on it leaves its control as it is, so the trigger attacks and A interacts as
  ever; a slot picked on the panel's first page, a bottle in the hand, a javelin, the bow's string and a held grip keep the control for themselves. With Hit by touch's "the trigger no
  longer attacks" only the plain attack on the trigger is withheld - a skill on RT casts. docs/skill_wheel.md.
- **The settings that come with the mod are the author's again** (d2r_vr.ini as tuned on the author's rig: for example the VR render size 1536, the monster's plate nearer and smaller far away, the
  Arcane Sanctuary's fog and the hero's own effects dimmed switched on). His calibration, platform, debug switches and update channel are not part of it, and the crossbow ships with its old settings
  (see above).

## 0.169

- **The weight and grip libraries out of the plugins folder.** 0.168.0 put d2r_vr_weight.dll and d2r_vr_grip.dll
  in d2rloader\plugins, where D2RLoader took them for plugins and logged "2 plugins rejected" on every start
  (harmless, but it looked like an error). They are in d2rloader\d2r_vr_libs now; Setup removes the old copies,
  and the plugin looks there first (beside the plugin still works).

## 0.168

The main points (details below and in SETTINGS.md):

- **No more frozen game in native OpenXR.** test builds after 0.167 could freeze for good a
  minute into play: the right eye's replayed lists went to the GPU in one batch. They go
  in the game's own calls now ([debug] replay_marks 3; 1 = each list with a mark, the safe
  fallback), about 1 ms less of the game's thread a stereo pair than one by one. A stall
  guard ([debug] stall_guard) keeps our work from waiting on a GPU queue that stands still,
  and a lost device writes its report before the game ends.
- **Weapons with weight and a real grip.** The hero's Strength against the weapon's
  requirement decides how heavy it feels; an optional weight-physics library and an
  optional grip library ([weight] hands_model, grip_model) move the held weapon and close
  the hand on its shaft; the second hand takes a sword only on its hilt.
- **A flail's ball on its chain** swings free, is what strikes with hit by touch, with
  its own weight ([flail] chain, ball_weight_pct).
- **Behind the shoulders**: weapons, the bow and the crossbow are put away and drawn back
  there; a dropped weapon lies in the world and comes back to a grip. The shield is always
  on the arm opposite the weapon and is put away behind the left shoulder - carry it there
  with the grip held and let go.
- **Bow**: energy orbs along the stave pick the arrow skill (always shown, the white one is
  the plain shot); the arrow burns, freezes or crackles by the chosen skill (a slider moves
  the effect along the arrow); with too little mana the release is the plain shot and the
  arrow's effect goes out; the string sits in the crook of the fingers.
- **Crossbow**: held by its own grip in the right palm, the left hand closes on the stock's
  fore-grip, the shot follows the drawn barrel.
- **Spells** aimed by the drawn hand or staff; both hands on a staff aim along it.
- **Wrists**: summons, mercenary, buffs, auras and curses beside the medallions; life and
  mana on the palm.
- **Skill panel**: the skills by the class's three trees; Run / Walk is a command you bind to
  any button, and a button can act on a touch.
- **The monster's own plate over its head**, the item condition figure movable, roofs kept
  with their underside, a wounded mercenary tinted red, room-scale walking.
- **PS VR2**: the pause on the right Options button; vibration in the hand that hits.
- **FlatVR**: BodyWalk through Game Link drives the plugin's own controller layout.
- The settings that come with the mod are the author's again.


- **Wrist status: summons, mercenary, buffs, auras, curses, from d2r-pcvr** (bearstonem,
  MIT; VR first person F4 only, native OpenXR; its own tick - Settings > UI: F4 body >
  Toolbar in the headset > Where = Wrists > "Wrist status", off by default; the game is
  only read, nothing is changed). Small icons and counters come up beside the medallions
  when the back of a wrist is turned to your eyes: the left wrist shows what is ON you -
  the auras and buffs you have (a chip each under a green rim: Might, Holy Shield, Frozen
  Armor, Battle Orders, a potion, a shrine ...), the curses and ailments that hurt you
  first under a red one (Amplify Damage, Decrepify, Chilled, Poisoned ...) - and the right
  wrist what you COMMAND: your mercenary (a red 0 while it is dead) and how many skeletons,
  mages, golems, revived monsters, valkyrie, decoy, ravens, wolves, grizzly, oak sage,
  vines, shadows, sentries, traps, hydras and demons you have. Which wrist shows what, the
  panel's size, height over the wrist, place along the arm, how squarely the wrist must face
  you and its opacity are settings, live; the skill icons are the skill wheel's local icon
  cache (without it, initials). Read from the game's own pet lists and its own state check on
  the local server's hero, each layout behind its byte guard (another build: nothing shown).
  The idea, the pet lists' layout, the state check and the table of states are
  bearstonem/d2r-pcvr's (MIT, v0.3.18), audited again against this build (docs/wrist_status.md).
- **Buttons on a touch** (Controls > Buttons of the controllers > *the finger on a
  control*, VR first person F4 only; nothing bound by default): the two triggers, X
  Y A B and the two thumbsticks can do something when your finger ARRIVES on them
  (the capacitive touch a Touch controller, PS VR2 on SteamVR and an Index
  controller report), instead of or besides their press - once per touch, nothing
  repeated while the finger stays; not in a menu or with a panel of the mod's own
  up. Made for the game's run / walk switch (*Run / walk switch* in the list:
  a press or a touch of the button you like). `[direct] <control>_touch`.
- **Skill panel: the skills by the class's trees, and a RUN / WALK button** (Stereo >
  Native OpenXR, under the skill wheel; each its own tick, the categories on, the
  button off; VR first person F4 only; unticked = the panel as it was; idea after
  bearstonem/d2r-pcvr (MIT)):
  - *Skill editor: categories* - EDIT SKILLS -> a slot shows a strip over the
    skills: All, your class's three skill trees (named as the game names them, in
    your language) and General / Items, each with its count. Which skill is on
    which tree comes from the game's own tables, read once into the icon cache by
    `tools/skill_icons/prepare_skill_icons.py` - run it again after this update,
    an older cache has no trees and the editor keeps its one list.
  - *Skill panel: a RUN / WALK button* (off: put the command on a button of your
    own instead - *Run / walk switch* in Controls > Buttons of the controllers and
    in the headset's Left / Right pages, *D2R key: Run / walk switch* in BodyWalk's
    bindings) - a tile beside the first row of the tiles,
    a button in the footer of the wheel; the trigger on it presses the game's own
    run / walk command and the panel stays open; it shows which one the game is
    on. Nothing of the game's moving or casting is done by the plugin.
- **The crossbow's left hand, from d2r-pcvr** (bearstonem, MIT; VR first person
  F4 only, each its own tick under Weapon Adjust > Crossbow, unticked = as before;
  the shot and the bolt are not changed):
  - *Left grip lets go cleanly*: held while the left grip is (squeeze first and
    bring the hand, or the other way round); let go at once on a weapon swap, a
    model change or lost tracking - no jump of the hand onto the stock; a squeeze
    still held from the shoulder swap takes nothing; and the crossbow is never put
    into both hands while it waits for its grip.
  - *Left wrist follows the controller*: the hand keeps your controller's turn on
    the stock (from below, palm up, or from above), its palm's middle on the place.
  - *Take the grip right after a swap*: no shot needed after a swap from a shield set.
  - *Left palm under the drawn barrel*: under each crossbow's own barrel as the
    model has it (its bolt helper bone), at the front of the stock - 6 cm behind
    where the limbs meet it, 7 cm below the barrel (two fine adjustments around 0)
    - not on the shot line set by eye.
  - *The right hand holds it by its grip*: the crossbow sits in the right palm by
    its own grip (where the game's own hand holds it), not by the bone it hangs
    on - it looked held by the wrist.
  - *Left hand on the game's own fore-grip*: where the game's other hand holds the
    crossbow in its own two-handed hold, learned from the game's pose.
  - *The left hand closes on the stock*: there it takes the game's own support
    hand's turn and fingers (mirrored), the palm toward the stock.
  - *Shoot along the drawn barrel* (from bearstonem/d2r-pcvr): the bolt goes where
    the crossbow you see points, raised or rolled - no hand correction. Only the
    direction of the game's own attack is steered; its bolt, damage, ammunition and
    rate of fire are the game's. While the barrel is not read, the shot line sliders.
  - Debug: *Crossbow: draw the barrel as modelled* (a cyan line and a magenta ball).
- **Spells aimed by the drawn hand or staff** (Auto-target > "Aim spells by the
  drawn hand or staff", on; VR first person F4 only; unticked = as before). The
  line a spell goes along now comes from the hero as he is drawn, not from the
  controller turned by hand-set corrections: a staff, wand or orb aims along its
  own axis toward its head (with the weapon's weight on, along the shaft as the
  grip holds it); an empty hand along its fingers, or out of the palm when the
  fingers point up or down. A staff held upright aims with its hand. The game's
  own cast - the skill, mana, the missile - is unchanged; only its target point
  is steered, as before. With no fresh pose the old aim and its *aim correction*
  sliders stay. Debug > Aim rays > "Spell's ray" (`[debug] spell_aim_ray`):
  where the spell goes, live - green from the model, orange the old line - to
  the floor (a full ball) or 8 m out (a ring), as the crossbow's ray. The idea of aiming along the
  posed weapon's shaft is from d2r-pcvr (bearstonem, MIT).
  - *Both hands on a staff* (weight on): the spell goes along the staff between
    the hands, from the rear hand to the front one - no longer along the hand
    that took it first.
  - *How far out the spell's point goes* by what is held (Bow tab): a wand or
    orb 20 cells (`[bow] caster_range`), bare hands 5 (`[bow] hand_range`), a
    staff its own *Staff aim point* sliders, one hand / two. The game's own point
    was about 3 cells - steps of 20 degrees.


- **From d2r-pcvr 0.3.17** (bearstonem, MIT), written again here on our signatures,
  each with its own tick and in VR F4 only:
  - **The roof stays when you walk into a building** (World > "Keep the roof when you
    walk into a building", on). For its camera high above, the game fades out the roof
    of a building the hero walks into (Kurast Docks, the huts in town): from the eyes
    that showed the sky through the ceiling. Now its request to fade the roof out is
    answered with the roof kept. F1, F2 and F3 keep the game's cut-aways; nothing else
    that fades is touched.
  - **The underside of a kept roof, not the sky** (World > "Roofs: their underside, not the
    sky", on; from d2r-pcvr 0.3.18). The game draws a roof only from above. Now the
    materials of the roofs - a roof model's materials named "roof", no list by hand -
    are built double-sided when an area loads, so from under the roof its underside
    is drawn. It shows from the next area you go to.
  - **Hanging lanterns move once a stereo pair** (Stereo > "Hanging lanterns move once a
    pair", on). Where the right eye is drawn by a second pass of the game's draw (no
    replay, or the first seconds in a new area before the replay starts) the props the
    game swings - lanterns, chains, signs - were stepped again with the right eye's tiny
    time and twitched. They are stepped in the left eye's pass only now, and both eyes
    get that pose. With the replay drawing the right eye (the default) the game's draw
    runs once and nothing changes.
  - **A wounded mercenary is tinted red** (UI: F4 body > "Wounded mercenary tinted red", off
    until you tick it; under 50 % of its life, both adjustable). Drawn by our effect
    over the finished picture: what the game drew inside the merc's body takes the
    red, the ground and what stands in front of it do not. The merc and its life are
    only read; unlike d2r-pcvr's version nothing is set on the merc in the game. Needs
    the new D2R_DepthFog.fx.

## 0.167

- **A faster picture in the headset.** In native OpenXR the fog, the sky and the
  cave ceilings need the scene's depth. 0.166 bound it at every effect pass, and
  each binding made ReShade wait for the graphics card, twice a frame. Now the
  game's depth buffer is picked once (and again only when it goes away or the
  window or render size changes), copied for each eye and bound once. In the author's headset
  (native OpenXR, a 1920 window) the rate went from about 62 pairs a second to
  about 106. World > Fog > "Depth for the effects" (our own, picked once - the
  default; ReShade's own to compare). The `[render] own_depth=0` workaround that
  one player with an RTX 5090 used is no longer needed.
- **The full controller in VR F1, F2 and F3 without BodyWalk** (Stereo > Native
  OpenXR > "Play with the controllers in F1, F2, F3 without BodyWalk", on). In
  0.166 only the left stick's walk and the belt potions worked there. Now the
  plugin's own controller is the game's gamepad in those views too - the layout
  of first person without its panels (the skill panel, the settings panel): the
  triggers, the buttons, both sticks, the game's actions and key commands, through
  the game's own gamepad code, no driver. With BodyWalk running nothing changes:
  its gamepad is the gamepad whole. A real pad that answers keeps its buttons; the
  controllers are only added to it.
- **Settings from the fork d2r-pcvr** (bearstonem, MIT):
  - **Turning** (Controls, and the headset panel's Options page): smooth turning
    on or off, its speed (10 - 720 degrees a second) and the snap angle (15 - 180).
    The mode is a switch of its own, so changing it keeps both numbers.
  - **Depth for the effects, how it is set** (World > Fog): "in each pass as it is
    bound" (as before) or "once a frame where the game makes it", which every
    pass - fog, ambient occlusion, particles, grass - reads alike; or off. The
    default is as before.
  - **Controllers:** the Steam Frame's own layout (on) and, for the Valve Index,
    the grip as how hard you squeeze (off).
  - **Shield Smite** is pressed 180 ms on a slot of the left trigger's row (65
    before): the game's pad code needs about 50 ms to prepare that row, and a short
    press could end between two of its polls and the bash cast nothing.
  - **Pick up / interact: one thing per press** (Input, first person, on): a press
    acts on what is under it then and nothing else, so a chest opened by a press
    does not also give you its loot.
  - **Vibration:** a channel of its own for each hand and each pulse stopped on time
    (both on, under "Controller vibration").
  - **Menus placed only when the headset is tracked** (on): until it says so they
    follow your gaze; and **menus and the mouse follow the VR render size** (on):
    the game's interface is laid out over the whole picture and its mouse reaches
    all of it - no menu edge lost, the main menu's bottom row can be clicked when
    the window and the picture differ in size.
- **The shield on the forearm** (Weapon Adjust > Shield (first person), on). The
  shield no longer hangs on the hand; it is strapped to the left forearm as the
  rig has it, with six sliders (move along X, Y, Z; turn about X, Y, Z). And **the
  forearm turns with the wrist** (Weapon Adjust, on): when you roll a controller the
  forearm turns with it - a little at the elbow, all of it at the wrist - instead
  of the hand screwing round on a still arm; a slider sets the forearm's share.
- **Shield bash: its own speed and start distance** (Weapon Adjust > Shield bash
  casts Smite): the least speed (0.8 m/s) and how far the shield moves to start a
  bash are numbers of the bash's own, apart from hit by touch's. And
  **Draw what casts skills** (Debug, off) draws the shield's capsule, the speed
  marks and the trail in the headset and logs why a bash did or did not count.
- **The frame cap belongs to VR.** Flat mode keeps your own "Framerate Cap" of the
  game; the headset's rate no longer locks it. VR's cap is worked out from the 3D
  choice (native OpenXR 0 - the headset paces the game; eyes by turns twice the
  headset's rate; the other choices the rate itself), and Home shows what is
  written and why. Your own cap is kept before VR writes its own and put back when
  you switch to Flat.
- **The settings that come with the mod are the author's again**: turning is smooth
  at 180 degrees a second, the fog starts nearer (at 30, full at 200), the VR render
  size is 1920, and the bow's release-to-arrow log is off.
  - **Updating:** a player who has not changed d2r_vr.ini gets all of this at once.
    If you have, Setup asks whether to keep your settings: "Yes" keeps your file and
    adds only the settings that are new in this version; "No - take this version's
    default settings" replaces your changes and keeps the old file as
    d2r_vr.ini.before-0.167.0.
- **The string at the fingers with the relaxed hand off, too.** With "The hand that takes the string: relaxed" unticked, the string ran to the controller's grip point, the middle
  of the palm, so it met the base of the hand. Now it runs to the fingers' first joints there as well - those of the fingers as the game animates them; with the tick on nothing
  changes. The log says which bones it is made of.
- **The quiver: pick the arrow of the next shot** (Weapon Adjust > Bow, "Quiver", on; a bow). With the string drawn a button of the string hand picks it (B and A;
  `quiver_next` and `quiver_prev` in `[direct]` change them), or an energy orb along the bow (below): Magic, Fire, Cold, Exploding, Ice, Guided, Freezing or Immolation
  Arrow, Multiple Shot, Strafe - **only the ones the hero has learned** (points or items) - and, first, what the slot held before (the plain attack). None learned: a short
  buzz and nothing else. It is the game's own skill setter on the slot the shot presses, the same as picking the skill in the panel; the shot is the game's own, as ever. The
  arrow glows in the skill's colour and the controller ticks at each change. **The quiver behind the shoulder is gone** (a free hand reached behind a shoulder, trigger for the
  next, A / X for the previous; the "Quiver: reach behind the shoulder" box and `quiver_reach`): not wanted, and the shoulders are the weapon swap's. The crossbow has no quiver now. The idea is the fork d2r-pcvr's quiver button (bearstonem, MIT, from aSuperGod);
  there the shot calls the skill directly, here the game's own attack does.
- **Fire, frost and arcane light on the arrow** (Weapon Adjust > Bow, "Arrow effects", on; a bow with the quiver). From the moment an arrow skill is chosen - by an orb, a button or the
  game's own slot - the arrow in the bow, nocked at rest and through the draw, burns, freezes or crackles by it (the plain shot has none): flames and rising embers for Fire, Exploding and Immolation Arrow; frost, ice crystals, glints and a falling mist for Cold, Ice and Freezing Arrow; a
  violet glow with sparks circling the shaft for Magic Arrow. Light the effect draws in the picture (real 3D with parallax, hidden by a hand or a sleeve in front of it; nothing of the
  game's is created), a few hundredths of a millisecond per eye on a fast card (measured). Guided Arrow, Multiple Shot and Strafe keep the small coloured glow;
  "Arrow effects" unticked, every skill does. Size and brightness sliders. Needs the new D2R_DepthFog.fx. The idea is the fork d2r-pcvr's quiver preview (bearstonem, MIT, v0.3.17),
  which makes the game's own missile on the string; this draws our own light instead.
- **No mana for the chosen arrow: the plain arrow** (Weapon Adjust > Bow, "Quiver: no mana for the chosen arrow - shoot the plain arrow", on). With an arrow skill chosen and too little mana
  to pay it the game refuses the release ("not enough mana", nothing shot); now the release is the game's plain attack, and the skill stays chosen, so with the mana back the next release is
  the skill again. Whether the hero can pay is the game's own arithmetic on the game's own numbers (the skill's row in Skills.txt and its level against the hero's mana), not a guess. Unticked:
  the game's refusal, as it was.
- **The arrow's effect goes out when the mana is short** (Weapon Adjust > Bow, "Arrow effects: out while the mana is short for the chosen arrow", on). With an arrow skill chosen that the hero
  cannot pay - by the game's own numbers, the same as the plain-arrow release's - the arrow looks plain (the fire, frost or arcane light and the small glow go out, eased) and lights again when
  the mana is back; at rest as well as while you pull. Unticked: the effect stays whatever the mana.
- **The white orb is always there.** The orb of the plain attack was missing when the game came up with an arrow already chosen on the attack button (the quiver had never seen the plain
  attack there); it is the first orb whenever the game offers the plain attack to bind. And the log now says, at every release with an arrow skill chosen, which skill is on the A slot
  and how much mana the hero has, and when the A slot's skill changes without the quiver asking - so a plain shot where a skill was chosen can be told from the plugin's doing: the plugin
  only presses the game's own attack and never picks the plain attack by itself.
- **Energy orbs along the bow** (Weapon Adjust > Bow, "Quiver: energy orbs", on). A row of glowing orbs hangs along the bow's stave - one for the plain shot, one for every bow
  skill you have learned, each in its skill's colour; the skill of the next shot is the bigger, brighter one. Pull the trigger of your free hand at an orb and that skill is the next
  shot's - the game's own skill setter, the same as picking it in the skill panel. At an orb the trigger is the orb's (no shot, no draw; the grip does nothing there) and the string
  is not taken there; anywhere else the trigger and the draw are as ever. A trigger that took an orb stays the orb's until you let go. "Orbs: pick with the trigger" unticked: the
  grip picks, as the orbs first did.
  The orbs sit on the bow's own bones (along the limb that points up) and follow it; they never go: at rest, while the string is drawn, at the release, in a menu (the skill that is loaded marked; no orb is taken then, only with the string at rest). The orb your hand is at grows, brightens and is not
  hidden by the hand; the grab radius is bigger than the orb. Ten sliders under "Quiver: energy orbs" set the size, the grab radius, the start on the stave, the spacing, the
  shift of the row and its turn about three axes (degrees, pivoting on its first orb, so the row can be tilted off the stave; 0 = along the stave). Needs the new D2R_DepthFog.fx.
- **Thanks to bearstonem** again: the turn settings, the depth at its source, the
  Steam Frame's layout and the Index grip, the Smite press, the one-press interact,
  the vibration channels, the menus waiting for the headset, the menus and the mouse
  on the VR render size, the shield on the forearm, our own depth bound once and the
  quiver follow what the fork [d2r-pcvr](https://github.com/bearstonem/d2r-pcvr) (MIT)
  found in the game.

## 0.166

- **The new features are on by default.** 0.165 shipped with the old default
  settings, so everything it brought - weapon weight, hit by touch, the bow
  drawn with both hands, the life and mana on your wrists, the belt potions,
  native OpenXR, controls without BodyWalk - stayed off until you ticked it in
  D2R VR Settings. The d2r_vr.ini that 0.166 comes with is the author's own
  tuned configuration, and these are on in it:
  - **In the headset:** native OpenXR (F1 and F3 in real 3D), controls without
    BodyWalk, the laser on menus with the item tooltips at once, the skill panel
    in the tiles' look and the settings panel in the headset, controller
    vibration, the VR render size 2048, brighter caves ("Light in caves") and
    "Any fire lights the ceiling"; the lighting fix, now in flat mode too.
  - **Weapons:** weapon weight; hit by touch (the blade must move at least 6 m/s
    and 15 cm to start a stroke), the shield bash that casts Smite; the bow drawn
    with both hands (the string is taken with the trigger or the grip), arrows
    along the hand, the game's own bow following your hand and lying along your
    hands, the hand on the string; the weapon swap over the shoulder.
  - **Life and mana, potions:** the wrist medallions, the red heartbeat at low
    life (with vibration), the plates of ordinary monsters hidden, the belt
    potions as the game's own models that you take in the hand and drink.
  - **Experimental, and on as well:** Quick strike, Quick release (the bow's
    arrow made earlier in the attack), Quick throw (all three below) and hiding
    the game's own flying potion. Untick them in D2R VR Settings (Weapon Adjust >
    Melee / Bow / Throws) if you want the game's own timing back. Direct hit
    stays off.
  - Some logging for support is on too (the bow's release-to-arrow times, the
    list of the game's compute shaders).
  - **Updating from 0.165:** a player who has not changed d2r_vr.ini gets all of
    this at once. If you have changed it, Setup asks whether to keep your
    settings: "No - take this version's default settings" gives you the above and
    replaces your changes; "Yes" keeps your file and adds only the settings that
    are new in this version.
- **Quick strike** (Weapon Adjust > Melee: hit by touch, experimental, on). The
  game lands a melee blow part-way through the attack animation (a Paladin's
  sword: frame 7, 0.28 s in; the author measured 0.32 - 0.34 s from the touch to
  the hit). This moves that moment to the start (frame 1, a slider) inside the
  same attack: the blow lands about a quarter of a second sooner. The animation's length and the attack rate stay,
  so the next attack cannot start earlier. The melee rows of the hero's class
  (and the Paladin's Smite); never the bow, throws, spells or kicks. Offline
  only, first person only; put back exactly when switched off.
- **Quick throw** (Weapon Adjust > Throws, experimental, on). The game makes the
  missile of a throw part-way through the throw animation (an Amazon's: frame 9
  of 16, about 0.36 s in). This moves that moment to the start (frame 1, a
  slider) inside the same throw: the missile is made about 0.3 s sooner; the
  animation and the attack rate stay. Javelins, throwing knives and axes;
  throwing potions keep the game's timing ("Quick throw: throwing potions too",
  off), because a potion's explosion has to come about when your potion lands.
  Offline only; put back exactly when switched off.
- **Auto-target is turned off in first person (F4) only.** The options that
  take the game's auto-target away from spells, arrows and throws (Auto-target
  tab, Weapon Adjust > Attack along the hand) act in F4; in F1, F2, F3 and flat
  mode the game's own auto-target works as usual.
- **Attack direction in the views from above and on the floor.** In VR F1 and F3
  the hero hits and casts where he faces: your hand no longer turns an attack
  there. In F2 and F4 it goes where you look, as before; flat play is as it was.
- **F1, F2 and F3 without BodyWalk: walk and drink belt potions** (Stereo >
  Native OpenXR, with Controls without BodyWalk, on). In the headset's views from
  above, from behind and on your floor, the controller's left stick walks the hero
  ("Walk with the left stick in F1, F2, F3 too"), and with the left trigger held a
  flick of a thumbstick drinks a belt potion - left = 1, up = 2, right = 3, down =
  4 ("Belt potions in F1, F2, F3"). One flick, one potion; the stick must come back
  to the middle first. Nothing else of the new controls works there (attacks,
  buttons, gestures and panels are first person only). With BodyWalk running
  nothing changes.
- **Flat mode keeps your own window.** The settings that come with the mod are
  made for the headset (native OpenXR, a square window, the render size 2048); a
  player on the monitor ("How you play": Flat) no longer gets them. Flat mode
  starts no headset session, makes no square window and sizes nothing: the game
  uses the window and resolution you set in its own Options. Your own window (size
  and window mode) is remembered before a square one is first made, and put back
  when you switch to Flat, at START and at the game's start. In VR, native OpenXR
  and FlatVR alike, the square window stays as it was.
- **The game's lighting fix works in flat mode too** (Render > "... in flat mode
  too", on). The game finds each light tile's depth range with a shader that is
  right at 16:10 only: in the headset 0.165 already replaced it, now the flat
  window gets the same - on a 16:9 monitor a little, in a square window in full.
  The log tells the size the game draws at and whether ours is bound.
- **The game's own flying potion** (hidden while yours flies, experimental, on)
  is now taken out with the game's own switch for its effect, not only moved
  under the ground. Only what is drawn changes: the missile, its damage, its
  stack and its explosion are the game's.
- **The shipped BodyWalk profile carries no tracker serial.** In 0.165.0 the
  profile's presets held the author's tracker serial number; it is blanked, and
  the release scripts now check the profile for serial numbers, MAC addresses and
  user folders at any depth.
- **Known:** without BodyWalk, VR F1, F2 and F3 get only the left stick's walk and
  the belt potions from the controllers; for the full controller there BodyWalk
  has to be running. A fix is in progress for the next update.

## 0.165

The big step of this version is first person (F4) on **native OpenXR**: the
plugin can read your controllers itself, your blade can pick the target,
weapons have weight, you throw with your hand and draw a bow with both. Every
new option for your hands and for the game's rules works **in VR first person
(F4) only** - F1, F2, F3 and the flat modes play as before - and is **off until
you tick it** in D2R VR Settings, unless it says "on". Options marked
*prototype* are early; *experimental* ones change how the game works inside.

- **Controls without BodyWalk** (Stereo > Native OpenXR, off, prototype). The
  plugin reads both controllers itself and gives the game its gamepad, its
  actions and its key commands, so native OpenXR needs no BodyWalk for the
  controls. BodyWalk can run on for FlatVR, and its walking by the body (steps
  in place, an arm swing, a lean) still walks the hero, mixed into the
  controller's left stick ("BodyWalk's walking still walks the hero", on). The
  layout - the right trigger attacks, the right A picks up and opens, the face
  buttons and the left trigger are the game's skill buttons, the grips belong
  to the belt and the weapon - is a table in `[direct]` of d2r_vr.ini, and every
  control can be rebound. In menus the controllers are the game's own gamepad
  layout. A long press of a stick click (half a second): the right one asks for
  the skill panel, the left one opens the inventory; a quick click of the left
  stick shows and hides the map. The game no longer shows "PLEASE RECONNECT
  CONTROLLER" after a long frame, and a trigger or button still held when the
  headset's focus returns is not a press. In this mode START on Home launches
  the game without starting BodyWalk. F4 only: in F1 - F3 BodyWalk's controls
  are the controls.
- **Hit by touch** (Weapon Adjust > Melee: hit by touch, off). In first person
  the blade you swing through a monster's body picks the target and the moment
  of the game's own attack: the attack button is pressed on the contact and the
  monster you touched is its target. The game's animation, reach, hit rolls and
  attack speed stay the game's. A blade held still or waved slowly does
  nothing (it must move at least 0.8 m/s - a slider), a monster is hit once per
  swing, allies and monsters behind a wall are left alone. The blade is built
  from the weapon as it is drawn - a staff's from butt to head, a dagger's the
  right way, a blade in each hand for two weapons - and its length, thickness
  and direction can be tuned live (Debug tab); "Draw the capsules of Hit by
  touch" shows what is tested, and the blade turns green while a contact would
  count.
  - **Thrown weapons too** (on with it): a javelin, a throwing knife or a
    throwing axe swung through a monster at point blank is the game's own Throw
    at it. The throw by gesture keeps working and never fires together with the
    touch; potions are never swung. A javelin's blade runs from the hand to the
    spearhead (measured on screenshots); the knife's, the axe's and the spear's
    blades are estimates - tune them on the Debug tab.
  - **Shield bash casts Smite** (off): with Smite on one of the pad's skill
    slots and a shield on your left arm, a hard bash through a monster casts one
    Smite at it - the game's own cast.
  - **Direct hit** (experimental, off, offline only): the touch calls the game's
    basic attack itself instead of pressing its button - no animation, no
    walk-up, no attack-rate wait. For comparing with the normal way.
- **Weapons have weight** (Weapon Weight tab, off). A weapon you hold alone is
  heavy - half its real mass to begin with, a slider from none to three times
  that: it lags behind a hard swing and, with "Gravity feel" (on), sags a few
  degrees when held out - a two-hander in one hand sinks a lot, two hands hold
  it much better. A hand holds the weapon while its grip is squeezed, where the
  grip closed on the shaft; let one grip go and the other keeps it; pass the
  weapon from hand to hand; on a long shaft the second hand slides along it.
  Let go of both and the weapon falls to the ground and tips over - and, by
  default, goes to your back after 0.6 s, where you take it again with a hand
  behind either shoulder (with Controller vibration on, a pulse tells you are
  on it) or call it back with a grip. A hand that takes a weapon sits on the
  shaft like the game's own grip, with live correction sliders for each hand,
  and its fingers and thumb take the game's own pose for a hand holding that
  weapon (on). Hit by touch follows the weapon: a weapon no hand holds strikes
  nothing.
- **Throwing** (Weapon Adjust > Throws). A javelin or a throwing weapon in your
  right hand is thrown by a throwing motion: **by the right grip** - with
  Controls without BodyWalk, on: the grip is the weapon in your hand, swing and
  let go at the end of the swing - or **by the right trigger** (off). The throw
  is the game's own attack, aimed along the way your hand was going, and the hero
  throws with the game's Throw skill by himself, no skill slot needed. A gentle
  auto-aim (a cone of 15 degrees round the line of your hand, a slider) takes a
  hostile monster in it; nothing in it, and the throw goes exactly where you
  threw. Throwing potions use the same gestures and land where a real throw of
  your hand's speed would, over the real ground ("how far", 800 % of a real
  throw, never farther than the 48 cells the game accepts). With Weapon Weight on,
  the model of a javelin or a knife you threw is gone the moment the game takes
  the throw and the hand stays empty while the game's throw plays; a thrown
  potion's model flies all the way to where it lands and breaks there (on).
  - **Throwing potions: our potion only** (experimental, off): one tick makes
    the potion of the weight model the only one you see. The game's own flying
    potion is not drawn (only what is drawn: its missile, damage, stack and
    explosion are the game's), and the game's explosion happens where and when
    your potion lands. The hide of the game's potion is not proven in the game
    yet; each of the three things also has a tick of its own.
- **The bow** (Weapon Adjust > Attack along the hand > Bow, off). Draw the
  string with both hands: the free hand's trigger (or its grip - a setting) at
  the bow's string, pull back along the line to the bow and let go. The game's
  own attack - or the pad button of a skill you pick (Magic, Fire, Cold, Ice,
  Exploding, Immolation, Freezing, Guided Arrow, Multiple Shot, Strafe) - is
  pressed once and the arrow goes along your draw. The game's animation, attack
  rate, mana and arrows are unchanged; the length of the pull adds nothing. A
  trigger pressed away from the string shoots nothing ("a press away from the
  string shoots nothing", on). The grab zone round the string and the least pull
  that shoots are sliders.
  - **The game's own bow follows your hand** (off): the bow's string, limbs and
    arrow - of any of the 15 bow models - follow the drawing hand and snap back
    at the release; a pull too short to shoot lets the string go back softly.
    The arrow passes the bow hand at a point you set and is kept clear of the
    hand and the forearm; the bow can lie along the line between your hands, as
    a real bow does. Another option makes the hand on the string hook its
    fingers on it, in the game's own draw pose (the Amazon's right hand), with
    the string - and the arrow's line - held at the first joints of the hooked
    fingers where the game's draw animation has them; sliders set the curl, the
    thumb and the string's place.
  - **Quick release** (experimental, off): the game makes a bow's arrow part-way
    through its attack animation, about 0.3 s after the release. This option
    moves that moment earlier (to frame 2) inside the same attack: the animation
    and the attack rate stay, the arrow, its damage and its mana come about 0.2 s
    sooner, and a hit on you in those first 0.2 s no longer cancels the shot.
    Offline only, with a bow in hand; put back exactly when switched off.
- **A skill panel in the headset** (Stereo > Native OpenXR, off, prototype). The
  controller's skill slots in front of you, as a wheel or as tiles made like the
  game's own controller skill dialog (two rows of six plates). Point with the
  laser and pull the trigger, and from then on that trigger casts the slot - the
  game's own pad button. "EDIT SKILLS" puts any skill you have into a slot
  through the game's own setter. **Hold the right stick click for half a second**
  to open it (a short click is still the weapon swap), hold it again to close it;
  without Controls without BodyWalk, hold the left grip and click the right
  stick. The icons come from a cache made out of your own installed game;
  without it you see names.
- **Settings in the headset** (UI: F4 body > Menus, off, prototype): both
  grips squeezed for a moment and a stick click open a panel with the picture's
  distance and width, brightness, your height, the laser, the game's view F1 -
  F4, the VR render size and the body measurement. With the skill panel's tiles
  these pages are the window's second tab, **Settings**, beside **Skills**: the
  same hold of the right stick click opens it on the tab you used last, and the
  window flips along Skills, then the Settings pages View, Options, Video and
  Body by a flick of a thumbstick, a tap of a grip (left back, right on) or the
  arrow at its edge. Apply writes only what you changed into d2r_vr.ini; nothing
  in the panel sends a click to the game.
- **Life and mana on your wrists** (UI: F4 body > Toolbar in the headset > Where
  > Wrists, off). Two balls like medallions on the back of the wrists - a glass
  dome on a metal base (gold, bronze or silver, with a patina you set), with
  liquid that rises and falls with your numbers, drawn with the game's depth so
  that a hand or a monster in front hides them - or the game's own life and mana
  globes cut out of its toolbar. Position, rotation and size per wrist, a depth
  offset for each hero class, which way the liquid fills, a soft heartbeat at low
  life. It only reads your life and mana. Needs the D2R_DepthFog.fx that comes
  with this version (Setup puts it in).
- **Low life: a red heartbeat vignette** (UI: F4 body, off). While your life is
  under a threshold (30 %) the edges of the view tint red and beat like a heart,
  faster and redder as your life falls; a gentle mode; the beat can be felt in
  the controllers too. A colour over the edges only: nothing is scaled, moved or
  shaken. In the headset's settings panel (Life and Look pages) nothing shows or
  buzzes until you press **Preview 5 s**, which plays the vignette and the beat
  for five seconds with the rows as they stand. On a PlayStation VR2 (SteamVR) the
  gentle beat is lifted to what its controllers show (`[openxr] haptics_motor`).
- **Hide the plates of ordinary monsters** (UI: F4 body, off, prototype):
  champions, uniques, bosses, their minions and NPCs keep theirs. Presentation
  only. And **Toolbar cut** sliders (UI: F4 body > In the game's picture) make
  the emptied strip taller, for bits of the toolbar's statues that stay in the
  picture.
- **F1 and F3 in real 3D on native OpenXR** (Stereo > Native OpenXR, on). The
  game on your floor is a diorama in your room that you walk round and lean over,
  on the headset's own floor; the view from above is a window onto the game that
  you lean to see round things. The game's own mouse pointer is hung on the
  ground where its mouse points (on). Menus and loading stay the flat picture.
  Untick it for the flat picture as before.
- **VR render size is a slider** (Home > In the headset): any size from 1024 to
  4096 pixels a side while the game's window keeps its own size - no more dragging
  the window square. Its left end is the window's size; a tick box takes your
  headset's recommended size instead. The game is asked once, after you stop
  dragging. Older settings keep working.
- **Eyes in one place get a gap** (`[openxr] ipd_fallback_mm`, 64). A runtime that
  reports both eyes at the same point (no stereo, a double image) now gets a 64 mm
  gap between them. 0 = off.
- **Lights no longer vanish in rectangles at a square or wide window** (Render
  > "Fix the game's lighting at a square or wide window", on). The game finds each
  light tile's depth range with a shader that is right at 16:10 only; ours does
  it right at any size (at 16:10 it gives the game's own numbers).
- **Attacks never walk the hero in F4** (Weapon Adjust, on). Every attack the
  plugin presses - the trigger, a swing by touch, a throw, a bow's shot - is
  pressed with the game's own Stand Still, so with nothing under the point it is
  a swing or a shot in place; a click on bare ground gets it too. Skills that
  move the hero (Teleport, Charge, Leap, Whirlwind...) and pick-ups are left
  alone.
  - **No stray mouse clicks, no stray walks** (on): in the world, with no panel
    open, the game gets no mouse button the plugin did not send, and a walk or
    run to a point out of a click on bare ground is dropped - the hero no longer
    runs forward in the middle of a bow shot. The log names the origin of each.
  - **A shot along the hand never sends the hero after a monster** (on). Before
    every pad cast the game looks for a target near the aim and, finding one,
    takes Stand Still off the cast - so the hero ran up to the monster instead of
    shooting. That first look now finds no target for a shot along the hand.
  - "An attack never runs the hero off" (off) is a safety net for the rest.
- **Smaller things for your hands** (all off, F4):
  - **Weapon sets over the shoulder**: reach a hand behind either shoulder and
    press its grip, and the game's own "Swap weapons" is pressed.
  - **Height calibration: Measure my height and arm span** (Body, or
    "Calibrate body" on the headset panel's Body page; prototype): stand upright
    with your arms out level at shoulder height (a T-pose), let go of both
    triggers, then squeeze both and hold still for 1.2 seconds - a panel in the
    headset leads you, and the right B cancels. It saves your eye height and
    your arm span: the eye height sets the world's scale, the span fits the
    hero's arms (the hands stay 1:1 with the controllers). "Use the measured
    height and arm span" switches it on and off, "Forget the measurement" clears
    it. Needs the headset's floor (a room set up) and first person (F4).
  - **Controller vibration**: a pulse when a blow of yours lands, a belt potion
    is taken, the laser clicks on a menu or you pick something up.
  - **Item tooltips at once under the laser**: the game's own tooltip and its
    comparison with what you wear, with no wait and no Shift.
  - **Gesture runes in the headset** (prototype): draws the runes BodyWalk scores
    when you draw a shape in the air, with your hand's trail. It needs a BodyWalk
    that sends them - not out yet - so for now nothing shows.
  - The hand with a shield on it closes with the grip (on).
- **Fixes.** With a bow or a javelin the aim along the hand put the point 60
  cells out, farther than the game accepts: a shot along north, south, east or
  west - and anywhere within 34 degrees of them - was thrown away. It is 40
  cells now. A hand with a belt potion in it no longer takes the staff, spear or
  two-handed sword. A native OpenXR start that found no headset is tried again
  every 10 seconds. Settings always opens on Home, which has a large START
  button now. A bad overlay can no longer black out a frame: it is left out and
  the log names it. Setup accepts both ZIPs d2rloader.net lists for D2RLoader
  1.3.1.
- **Thanks to bearstonem** again: reading the controllers directly, the skill
  wheel, the weapon swap over the shoulder, the shield's Smite, the item tooltips
  at once, the controller vibration, hiding the plates of ordinary monsters, the
  T-pose measurement, the bow's arrow skills and its bones follow what the fork
  [d2r-pcvr](https://github.com/bearstonem/d2r-pcvr) (MIT) found in the game.
- **For support** (Debug tab, all off): logs of what the game does with each
  attack of ours (`shot_trace`), of the long frames and what the plugin spent in
  them (`frame_trace`), and of where the time goes between a bow's release and
  its arrow; and the plugin's log written from a thread of its own (`log_async`).
  Send them in with a stutter or a shot that ran the hero off.
- **For developers: the source does not build as it is yet.** The rune drawing
  uses the VrRunes section of BodyWalk's Game Link header, which is not in the
  released BodyWalk SDK yet. Until it is, build against a header that has it
  (`-DBODYWALK_SDK_INCLUDE=<folder with bodywalk/game_link.h>`); the README and
  docs/KNOWN_ISSUES.md (issue 23) say how. It will be fixed in a following
  release.

## 0.164

- **Spells fly along your hand, no auto-target.** In first person a spell
  that flies - Fire Bolt, Charged Bolt, Bone Spear, Frozen Orb... - goes
  where your hand points, as every attack does; the game's auto-target and
  aim assist are off for that cast, so it no longer turns to a monster.
  Spells placed on the ground (Blizzard, Fissure, Meteor), summons, curses
  and auras keep the game's targeting. A new tab, **Auto-target**: "Disable
  auto-target for spells" (on), and a tick per spell that keeps the game's
  auto-target for that one - for those that seek their target themselves
  (Guided Arrow is ticked).
- **Arrows and throws along the hand** (Weapon Adjust > Attack along the
  hand, both off): "Arrows along the hand, no auto-target" for bows and
  crossbows, "Throws along the hand, no auto-target" for javelins and
  throwing weapons.
- **Menus as a panel, the world stays in 3D** (UI: F4 body > Menus, on). In
  first person with native OpenXR, the inventory, the stash, a trader, the
  skills or the game menu hang in front of you as a panel of their own, and
  the world stays in stereo around you. On FlatVR it needs BodyWalk 1.80
  (Setup brings it; with an older one the menus stay flat, as before).
- **Laser pointer on menus and panels** (native OpenXR, prototype, off):
  point a controller at the menu panel and it is the mouse.
- **Belt potions as the game's own models** (UI: F4 body > Belt potions,
  off): the belt's bottom row hangs on the hero's hips, lit and shaded by the
  game; "take them in the hand and drink" brings one to your mouth to drink it.
- **Allies' portraits** can be moved (UI: F4 body > Allies' portraits), and a
  monster's name plate is read over the monster itself.
- **No more grey haze in the picture's corners** outdoors: the game's own
  depth fog, made for its camera from above, is set to 0
  (World > "The game's own depth fog" brings it back).
- Fixes: the weapon set (I / II) is told right after a swap to an empty set;
  our arms and hidden head only on our own standing body, never on other
  players or our corpse; DLSS gets the stereo-fixed motion vectors at 4K's
  DLSS Balanced and up as well.
- **Thanks to bearstonem**: the spells along the hand, the menus as a panel and
  the laser pointer on menus follow what the fork
  [d2r-pcvr](https://github.com/bearstonem/d2r-pcvr) (MIT) found in the game.
- **No more crash in the Act 3 jungle with a far render distance.** With
  many room rings and models drawn far out, the game's renderer ran out of
  its 12 MB of memory in Spider Forest and the jungle round it, and the game
  closed. D2R VR now gives the game's renderer and entity memory the room
  that `[render] render_pool_mb` (64) and `entity_pool_mb` (280) in
  d2r_vr.ini ask for - those two settings did nothing before. It is done
  once at start, through the game's own allocator; the log says how big the
  pools are ("memory pools: renderer 12 -> 64 MB, entity 70 -> 280 MB").
- **No more pale veil with a straight edge in Lut Gholein.** The game keeps a
  copy of its depth for its fog, height fog, ambient occlusion, reflections and
  water, measured for its camera from above: 550 to 1060 units away. From our
  camera everything is nearer, so all of it read as one flat wall 550 units
  off, and the game's height fog drew a veil over the town that ended in a
  straight line across the picture. D2R VR now gives those effects the depth
  from our camera (D2R VR Settings > World > Fog: "The game's effects measure depth
  from our camera", on; `[render] depth_fix`, `depth_range`). Untick it for
  the old picture.
- **Brighter caves and dungeons** - D2R VR Settings > Ceiling > Light in
  caves (`[cave_light]`, off by default). Underground the game has no sun:
  only the hero's circle of light, the torches, and a light that fills the
  place inside that circle. On a headset whose screen loses the darkest
  shades (an LCD, such as Meta Quest) caves were black where an OLED (PS VR2)
  showed them. The game's own light is turned up, before it makes its colours,
  so they keep their strength - not a lifted black level:
  - "Hero's light circle": how far his light reaches, up to 10 times the game's;
  - "Hero's light strength": how bright it is;
  - "Fill light": the light inside the circle, times the dungeon's own, and
    "at least" for the darkest ones (the Act 2 tombs).
  Drawing only: the light radius and everything the game plays by stay.
- **"Any fire lights the ceiling": the light over the fire, not behind it.**
  A flame has no depth, so its light was put on the wall behind it. The new
  slider "Fire light nearer to you" (Ceiling > Cave ceiling,
  `[ceiling] fire_nearer`) brings it nearer, along your line of sight.

## 0.163

- **No more attacks on head turns and while walking.** BodyWalk's strike
  gestures measured the hand's speed against the head, so a quick head turn
  or a step swung still hands. BodyWalk 1.79 measures the hands themselves.
  D2R VR now needs BodyWalk 1.79: Setup puts BodyWalk Portable 1.79 beside an
  older one.
- **Item labels on their items** in first person (F4, native too) - D2R VR
  Settings > UI: F4 body > Item labels on the ground:
  - "at their items, %": each label at its own item's depth in stereo
    (100 = exactly; the game draws them on the same spot in both eyes, far off);
  - "size far away": nearer labels larger, farther ones smaller;
  - "stereo depth on top": all of them nearer or farther.
- **Names over monsters**: their opacity works now, and they have a size
  (UI: F4 body > Names over monsters).
- **The game interface size** slider is on UI: F4 body now: with the square
  game window, lower it until the inventory and the stash fit.
- **Native OpenXR**: an open panel (inventory, trade, stash) moves the flat
  picture out to the panel distance, and back when it closes.
- **The map in your hand**: with the game's map in the middle (full screen)
  the orb stayed empty - D2R VR now puts it on the right at start.
- **Sky and fog in flat mode** are back with the game's Resolution Scale above
  100.
- **The square game window** no longer unticks itself when the game's
  Settings.json has no window size in it, and the log says why a window is not
  made square.
- For support: the log names every press that makes the hero act and where it
  came from ([debug] input_log).

## 0.162

- **A square window bigger than your monitor, up to 4000x4000** (D2R VR
  Settings > Stereo > Native OpenXR > Square window). Make a virtual monitor
  that big in BodyWalk (Virtual Monitor) - no need to make it your main
  monitor, so your desktop icons stay put. When you enter a game, D2R VR
  moves the game's window onto the biggest monitor and grows it to your size
  a few seconds after the hero appears. The menus stay at your monitor's size:
  D2R itself closes when its window is very tall while it builds a screen
  (its title screen, the character screen).
- **The game always starts at a size it can take**: as tall as your main
  monitor holds, 2400 at most.
- **The Low preset has cave ceilings too** - players on Low had none.
- When ReShade64.dll does not load (Windows error 1114, a broken or second
  copy of ReShade), the mod's log names every ReShade copy it finds in the
  game - for the logs you send us.

## 0.161

- **No more fog flicker in the left eye.** With ReShade's Generic Depth on
  and a big square window, ReShade sometimes swapped the scene's depth for
  the game's shadow map when you turned your head, and the fog and the sky in
  the left eye flashed as if seen from above. D2R VR's fog, sky and ceiling
  now read the scene's depth through a channel of their own, which nothing
  else can take over. ReShade's own depth stays for FlatVR's "3D from the
  depth". Update the mod and its shader together - the installer does.
- **The game's window no longer goes past your monitor.** A window taller
  than about 2400 makes D2R itself close at the title screen, so the square
  window (Stereo > Native OpenXR) is now fitted to your monitor: on a 4K
  screen about 2069x2069. For a bigger square, use a bigger or a virtual
  monitor (for example BodyWalk's Virtual Monitor).
- **No more crash at the title screen from a too tall window.** A windowed
  size taller than 2400 in the game's Settings.json (say 3000x3000, even
  set read-only) is cut to 2400 when the game starts, and the read-only flag
  is taken off - D2R itself closes at its title screen above about 2400.
  After such crashes native OpenXR may have been switched off by the start
  guard: switch it on again in D2R VR Settings (3D in the headset).

## 0.160

- **The fog, the sky and the ceiling always get the scene's own depth.**
  D2R VR now hands ReShade the game's depth itself, so ReShade's guess no
  longer matters. With a square window ReShade had taken the game's shadow
  map instead: the fog and the sky were laid as if seen from above.
- **Square game window up to 2400x2400** (D2R VR Settings > Stereo > Native
  OpenXR > Square window: size). The 2047 limit of 0.159 is gone. Taller
  windows are not possible: D2R itself closes at the title screen with a
  window taller than about 2400 - so for "3000x3000" take 2400x2400.
- **The square window stays square from the second start on.** The game
  wrote the size Windows had cut its window to back into its own settings
  (2400x2137 for 2400x2400), and the next start had black bars again. D2R VR
  now keeps the window at the size you set while native OpenXR runs.

## 0.159

- **Native OpenXR: the game's window can be bigger than your monitor.**
  Windows cut a window to the screen's height - a 2200x2200 window came out
  2200x2136, and the picture in the headset got black bars. The window now
  takes the size the game's Settings.json asks for.
- **Native OpenXR without black bars**, two ways:
  - Tick **"Square game window (no black bars)"** in D2R VR Settings >
    Stereo > Native OpenXR and set its size (2000 by default, up to 2040).
    Settings makes the game's window square for you and puts ReShade's depth
    setting back to the one that works. Close the game first: it writes its
    own window size back when it exits. A square window must stay under
    2048: from 2048 on, ReShade takes the game's 4096x4096 shadow map for the
    scene's depth, and the sky and the fog come out wrong.
  - Or untick Stereo > **"Sharper: the picture keeps the window's shape"**:
    no bars with any window, a little softer up and down.

## 0.158

- **FlatVR: the horizon no longer leans** after switching from native OpenXR
  (or after a recentre): the camera took the head's sideways tilt at that
  moment as "upright", and the picture stood a degree or three askew - seen
  most when turning the head right. In the headset the camera's roll is now
  always the head's own.
- **Caves: any fire can light the ceiling** - a new box "Any fire lights the
  ceiling (ReShade, from the picture)" (Ceiling): torches, braziers, fire
  spells and burning monsters light the vault over them, found in the picture
  itself. No reading of the game's memory, so it is light on the CPU (unlike
  "Torches light the ceiling"). A spell's light goes out with the spell; a
  fire out of sight keeps lighting for a few seconds.
- **D2R VR talks to BodyWalk through Game Link**, BodyWalk's open link for
  game mods, instead of a plugin of its own (the D2R Bridge). Setup puts the
  Game Link plugin in and takes the old bridge out; the mod's actions are in
  BodyWalk's Mapping as before.
- **Native OpenXR**:
  - "Eyes by turns" as an option (Performance > 3D in the headset): one eye a
    game frame, half the work; the other eye shown as it was last drawn,
    turned to the head by the headset. The sky in the headset was missing
    there and is back.
  - A brightness slider (Stereo > Native OpenXR), on headsets whose runtime
    supports it - SteamVR does not yet.
  - PS VR2 on SteamVR: the camera no longer falls to the ground when SteamVR
    moves its "straight ahead" without saying so; a controller that loses its
    place for a moment is held by the head instead of the arms jumping and
    resetting; when SteamVR takes the input away (its dashboard) the hands
    stay put and the buttons let go.
  - The game no longer dies at every native start on some PCs (ReShade's DXGI
    wrapper on the headset runtime's adapter lookup).
- **Stereo "one pass, two pictures"**: the right eye's fog, sky and ceiling
  use the right eye's own depth - they came out doubled.
- **Your own ReShade setup**: "Keep ReShade as dxgi.dll" (Home) keeps a
  ReShade of your own (2D presets, another mod that wants dxgi.dll) working -
  with it on, Setup leaves your dxgi.dll alone; a dxgi.dll beside ours is put
  aside, never deleted. ReShade's rows show in flat too.
- In a shop the left stick click is the pad's own again: "hold the left
  stick to repair all" at a vendor works.

## 0.157

- **The camera no longer spins without controllers.** With the controllers
  put down (not seen by the headset) the view could swing between the ground
  and the sky every frame: BodyWalk was handing the game the head from two
  places at once, each with its own idea of straight ahead. The game now
  takes the head from one place only - FlatVR's when it runs - drops a head
  that jumps farther than a head can turn, and holds the last good one
  through a moment without tracking. A recentre still comes through (after
  0.3 s). Without hands the camera just stays put.
- **Native OpenXR**: a frame the headset gives no eyes for keeps the last good
  ones (up to 2 s) instead of turning the camera to BodyWalk's head and back;
  a hand counts only when the headset knows where it is, not just how it is
  turned.
- **Native OpenXR no longer kills the game when the headset sleeps**: the
  game waited without end for pictures SteamVR does not hand out while the
  headset is asleep, the graphics card gave up and the game died. A picture
  that does not come is now left out of that frame.
- **Native OpenXR start guard**: on some PCs the game died at every start in
  native mode (the headset's runtime and ReShade do not get along there).
  If the game dies while native OpenXR is starting, the next start switches
  native off ([openxr] on=0) and runs on FlatVR - the log says so; switch it
  on again in D2R VR Settings > Performance.
- **Native OpenXR: the head turns about the neck** ("Head turns about the
  neck", Camera tab) - looking down the eyes go forward over the chest, as on
  FlatVR. It did nothing in native before.
- D2R VR Settings: the Camera tab's first-person group is F4.
- **If the game crashes, we can now see where**: D2R VR writes each step of
  its start as it goes (d2rloader\logs\d2r_vr_start.txt), and a crash's
  module, code and stack to d2r_vr_crash.txt with a minidump beside it.
  Collect logs (D2R VR Settings > Home) takes them, D2RLoader's own crash
  reports and Windows' record of the crash - post the zip in our Discord.

## 0.156

- **Native OpenXR (prototype)**: the game shows itself in the headset - no
  FlatVR screen in between (D2R VR Settings > Performance > 3D in the headset
  > Native OpenXR; needs BodyWalk 1.78). Stereo from the very first moment in
  first person, the hero's body facing where you look from the start, your
  controllers through BodyWalk even when BodyWalk was started after the game,
  and BodyWalk's belt zones (gesture zones ticked "VR") as see-through balls
  in the headset. Bare hands (no controller) press nothing. A sharper picture
  by default: each eye keeps the game window's shape, black above and below
  (Stereo > Native OpenXR, "Sharper: the picture keeps the window's shape").
  Tip: switch Virtual Desktop's Synchronous Spacewarp off, or it may hold the
  game at 45 fps.
- **Launch Diablo II starts BodyWalk too**, and FlatVR once BodyWalk is up -
  no need to know to start them first.
- **BodyWalk 1.78 required**: FlatVR can leave out a late picture instead of
  flashing an old frame on head turns ("Leave out late pictures"), and
  BodyWalk follows the game's wishes while minimised. Setup puts BodyWalk
  Portable 1.78 beside the game when yours is older.
- **Medium** quality now draws models out to 200 (was 300): about 2 ms less
  CPU a frame for little that you would miss.

## 0.155

- **Collect logs for support** (D2R VR Settings > Home): one zip on your
  desktop with the logs, the settings, the versions and your graphics card -
  while the game runs it first records 10 seconds of how the frames are
  drawn. Post the zip in our Discord (#diablo-2-vr) when something is wrong.
- **D2R VR needs BodyWalk 1.77** (FlatVR's frame conveyor). Setup checks the
  BodyWalk you have: if it is older - Steam's is 1.74 for now - it stays as
  it is and Setup puts BodyWalk Portable 1.77 in the game's folder.
- **Start BodyWalk starts the newest BodyWalk** it finds, never one older than
  D2R VR needs; Home > Status says when the running BodyWalk is too old.
- FlatVR's add-on 2.19 with the picture's present moment is back (it goes
  with BodyWalk 1.77).
- **Cave ceilings for everyone**: their stone pictures now come with the
  installer - without them ReShade said "There were errors loading some
  effects" and the caves had no ceiling.
- Setup switches FlatVR's background and glow round the screen off (BodyWalk
  > FlatVR > Background & Glow).

## 0.154

- **The eyes stay in line in Stereo - one pass, two pictures** with the
  BodyWalk that comes with the installer (1.76): 0.153 brought a newer FlatVR
  add-on, and with BodyWalk 1.76 the eyes went out of line after about 10
  seconds. The add-on is the one from 0.152 again; the newer one comes back
  with BodyWalk 1.77.
- **Pipeline depth is 0 by default again** (D2R VR Settings > Stereo >
  Timing): with 1 (0.153) head turns lagged and the sky floated in mono and
  in eyes by turns.

## 0.153

- **A new 3D mode: Stereo - one pass, two pictures** (the default). The game
  draws each frame once and the right eye is drawn again on the graphics card
  from the same work, with its own camera: true 3D with the sky and the
  shadows right in both eyes, at nearly the mono frame rate (on the author's
  PC 127-159 stereo pairs a second, against 108 with two passes). D2R VR
  Settings > Performance > **3D in the headset** has four modes now:
  Stereo - one pass, two pictures; Stereo - eyes by turns; 3D from the depth
  (ReShade); Mono. If the new one is unstable on your PC, take Eyes by turns.
- **Frames paced to the headset**: the game's frames are held to the
  headset's refresh rate - a game faster or slower than the headset juddered
  in head turns. D2R VR Settings sets the game's frame cap to the headset
  rate (twice it for Eyes by turns).
- **Smoother head turns** in mono, 3D from the depth and Eyes by turns when
  the game and the headset run at different rates: Pipeline depth is 1 by
  default, and the sky follows the frame that is shown.
- The game's temporal anti-aliasing is set to FXAA when the game starts in
  real stereo - it mixed the two eyes into a ghost behind everything that
  moves ([stereo] keep_taa=1 keeps it).
- **No hitch every 10 seconds**: the log's thread count (0.152) stopped the
  game for 30-50 ms every 10 s; it no longer runs on the game's thread.
- Known limits: with DLSS, Stereo - one pass shares one DLSS between the two
  eyes (a DLSS for each eye still hangs the graphics card there - off). When
  the graphics card cannot keep the headset's rate, the picture judders -
  lower the headset rate or the resolution.
- **For developers - help is welcome**: the roadmap, the known problems (what
  we know about each one and where in the code to start) and notes for
  developers are in [docs/](docs/README.md) - fork the project, fix one and
  send a pull request. The weapon weight work for first person is published
  too: a model with tests and a Unity bench, not in the game yet
  ([docs/WEAPON_WEIGHT.md](docs/WEAPON_WEIGHT.md)).
- **Next version**: D2R VR moves to BodyWalk's Game Link standard (the open
  BodyWalk plugin SDK) instead of its own bridge to BodyWalk.

## 0.152

- **Smooth head turns again in tabletop and first person**: FlatVR's frame
  ring (new in 0.150) is now off by default - with it the picture juddered
  and doubled when you turned your head. Players who tried it: "a night and
  day difference". The switch stays in D2R VR Settings > Stereo > Timing
  (Newest finished picture); keep **Frame stamps** on - without them it is
  as bad as before.
- The log tells where a frame's time goes (presents a second, the game's
  busiest threads, and in real stereo each eye's pass).

## 0.151

- **Creating a character works in first person (F4)**: the Create New screen
  was taken for a game area - the heroes stood there without heads and the
  name field and buttons slid off. It is a menu again, with the game's own
  camera.
- **Two switches for judder in head turns** (tabletop and first person),
  D2R VR Settings > Stereo > Timing, both on by default and live:
  **Frame stamps** - FlatVR places each frame at the head pose it was drawn
  for; untick and the screen stays at the head as it is, like in the other
  views. **Newest finished picture (FlatVR frame ring)** - untick and the
  picture goes over to FlatVR the old way. If tabletop or
  first person doubles or judders when you turn your head, untick them one
  at a time and tell us which one helped.
- FlatVR's ReShade add-on 2.19 (the frame ring can be switched off from the
  game).

## 0.150

- **Much faster with DLSS in real stereo**: the motion fix for DLSS (0.145)
  also ran a heavy self-check every frame. It no longer does - 50 stereo
  pairs a second became 79 on the author's PC (100 -> 158 fps).

## 0.149

- **A desktop shortcut**: Setup can put "D2R VR" on the desktop (ticked by
  default) - D2R VR Settings starts BodyWalk, installs ReShade and D2RLoader
  and launches the game, so there is no need to open the game's folder.

## 0.148

- **Setup's "Open D2R VR Settings" works**: on the finish screen it failed with
  "CreateProcess failed; code 740 - the requested operation requires
  elevation" when Setup ran as administrator.

## 0.147

- **Launch the game from D2R VR Settings**: Home > **Launch Diablo II
  (D2RLoader)**, under Start BodyWalk - shown while D2RLoader is installed and
  the game is not running.

## 0.146

- **ReShade installs from D2R VR Settings now**: Home > Status > **Install
  ReShade** downloads it from reshade.me and sets it up for the game by
  itself - no renaming files. Setup no longer installs it: its ReShade step
  failed ("did not finish, code 1") when D2RLoader was not in the game's
  folder yet.

## 0.145

- **DLSS works with real stereo**: each eye gets a DLSS of its own, fed the
  motion against that same eye's last picture - no shaking standing still, no
  smear in head turns. The Status tab no longer asks to switch DLSS off.
- **Character screen**: the heroes keep their heads (the mod's VR camera stays
  out of the menus).
- **Performance tab**: Potato is Low with the 3D made from ReShade's depth
  (one picture a frame) instead of real stereo. New "Headset refresh rate"
  (72-120 Hz) sets the game's frame cap - the rate for one picture a frame,
  twice it for real stereo. Render distance, rate and cap need the game
  restarted (said under the 3D choice).
- **3D in the headset follows the mod**: FlatVR's 3D source (depth, stereo
  pair or none) is set by the mod whenever BodyWalk starts and whenever it
  changes; FlatVR's Head Lock comes back after such a switch. The mod's
  BodyWalk profile no longer forces the stereo pair on, and keeps the depth
  unflipped.
- **The toolbar is on your left forearm now** (first person, F4): orbs,
  belt and skills lie along the inside of the arm, cut in two - turn the palm
  up to read it. To have it on the body again: D2R VR Settings > **UI: F4
  body** > Toolbar in the headset > **Where: Low in front** (or Chest, low).
  The forearm and the body each keep sliders of their own.
- **New defaults from the author's own play**: cave ceilings on with the tuned cathedral and sewer
  vaults, near fog (30-200), the crosshair and the left hand's hold retuned,
  the inventory screen nearer (2.5 m).
- **The staff in the right hand works again**: the left hand takes it with
  the grip and slides along it, closes and opens with the grip while it is off
  it, and spells go along the staff - all of it had been off whenever "aim
  from the left hand to the right" was off (the default since 0.140).

## 0.144

- **The belt works again**: the potion zones at your waist (first person, F4)
  show in the headset and fire, and so do the sword, axe and throwing swings.
  The mod's BodyWalk profile had shipped without their recorded positions.
- **Every game control from BodyWalk**: 68 new actions "D2R key: ..." in
  BodyWalk's Mapping tab - skills F1-F16, potions 1-4, Alt, Run, Shift,
  character, inventory, map, mercenary, cube and the rest - pressed straight
  in the game, no gamepad and no key needed.
- **Third person (F2) like flat mode**: the mouse turns the camera, the pointer
  is a crosshair in the middle (on/off, size, and its depth looking ahead and
  looking down on the UI: F2 tab), a click goes
  there, W A S D walk where the camera looks; F9 frees the pointer for menus.
- **The game on the floor (F3)**: the corner map gets size, depth and place
  like the toolbar (UI: F3 tab), and the mouse pointer lies on the game's
  ground by itself.
- **Turning with the right stick in first person (F4) fixed**: the body
  turned not at all when some of the game's controller polls came without
  BodyWalk's pad and reset the stick to the middle.

## 0.143

- **Smoother in dungeons on weaker PCs.** The torch light on the ceiling is off
  for now: finding the torches read all of the game's memory every 1.5 seconds
  and cost about a third of a CPU core while a ceiling was drawn.
- **Ceilings in act 2**: the Lut Gholein sewers (grey-beige brick) and the
  palace harem (Moorish coffers). Backgrounds done: 25 - sky over 18 open-air
  area types, a ceiling in 7 dungeon types.
- **The cathedral** keeps its flat ceiling with one dome over the altar.
- **Third person in VR**: W A S D move the hero relative to the camera.

## 0.142

- **Ceilings in the cathedral and the catacombs**, anything the game draws above
  the ceiling cut cleanly, ceiling heights from the area's floor. Settings >
  Ceiling.

## 0.141

- **Sky fixed in several areas.** Tristram, Kurast (Lower Kurast, the Bazaar,
  Upper Kurast, the Causeway), Travincal, the River of Flame, the Chaos
  Sanctuary and Nihlathak's Temple now have their sky, like the other
  open-air areas.
- **The barbarian's left hand fixed.** In first person his second weapon sits
  properly in the left hand, and he can strike with it.
- **Daggers can be thrown** - a Throw action in the mod's BodyWalk profile, plus the left-hand attack and D-pad actions.
- **"D2R Left Hand" tab** in BodyWalk's Mapping (D2R Bridge 0.23): actions for
  the left hand while it holds a weapon.
- **Ready for game updates.** Every time the game starts the mod looks for the
  game code it needs (54 places) by signature, so a game or D2RLoader update
  that moves the code does not switch the mod off. A new **Status** tab in
  D2R VR Settings shows each one - found, not found and what goes off without
  it - and a **Scan** button looks again for whatever is missing.

## 0.140

- **The author's tuned settings are the defaults**: depth from above, the body
  in F4, fog, sky, the toolbar and map in the headset, the weapons.
- **ReShade works right after installing.** The installer fixes ReShade's
  effect search path, which ReShade's own setup writes in a form it cannot read
  (no fog and no sky before).

## 0.139

- **D2R VR Settings checks the game's video settings** in its Status column:
  DLSS, Vertical Sync and TAA anti-aliasing must be off (they mix or halve the
  two eyes' frames), and a frame cap under 180 is flagged.

## 0.138 - first public release

- Four views, F1 - F4: from above in real 3D, third person, the game on your
  floor, and first person (preview - the weapons are not calibrated yet).
- Flat mode: a monitor, mouse and W A S D, no headset and no BodyWalk.
- The installer sets up ReShade, the mod, FlatVR's add-on and, if needed, a
  free BodyWalk Lite inside the game folder; an update keeps your tuned
  settings, and uninstalling removes everything the mod put in.
- D2R VR Settings: Start BodyWalk / Start FlatVR buttons, a status of the whole
  VR chain, an update check.
