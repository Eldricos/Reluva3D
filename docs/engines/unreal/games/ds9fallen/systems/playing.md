# The Fallen: playing a level

> **Status:** done · **Code:** `src/engines/unreal/game/level_play.cpp` · **Checked against:** the game running in Reluva, and a recording · **Confidence:** from the code unless marked

## Overview

The level's scripts run live with the player logged in, moving and
colliding against the level, steered from the keyboard and mouse and
seen from the game's own camera, cut scenes included.

## How it works

`LevelPlay` (`level_play.cpp`) is a level being played. It starts the
level as above and logs the player in. Then, as real time passes, it
ticks the level in steps of a thirtieth of a second, at most four a
frame: when the scripts can't keep up, the game slows down rather than
falling further behind. The Maps page plays one with Enter:

- **Controls** are the player's input axes and buttons, set each tick
  as the engine sets them from the keys (below). W/S move, A/D step
  aside, Left/Right turn, Up/Down look, and dragging the mouse turns
  and looks (`aMouseX`/`aMouseY`, used up by one tick). Space jumps
  (`bExtra1` and `aUp` held, and the `Jump` exec function called once).
  C crouches, as the game binds it to `Duck` (`bDuck` held and `aUp`
  down). Ctrl or F fires (`bFire` and `Fire`). The number keys call the exec
  functions the game binds them to (`[Engine.Input]` in User.ini, else
  DefUser.ini: 2 is `Rifle`, 0 `HolsterWeapon`).
- **The camera** is where the game puts it: the page calls the player's
  `PlayerCalcView` each frame, as the engine does before drawing. The
  Fallen's is third person, and all script: `CTVE_Camera.CameraAI`
  follows the player, swings aside as they run and pulls in out of
  walls, and follows the cut scenes' directors. With it come the field
  of view (`FovAngle`) and the HUD's letterbox. When the camera is
  ahead of the player, `CameraAI` writes its direction with the yaw
  turned round and the pitch past straight up (`Pitch = 98304 −
  Pitch`). So the page takes the view's facing from the rotation's
  direction, not its yaw and pitch alone, which would look straight
  down.
- **What's drawn** is the level's actors as their properties are now,
  read each frame (`LevelPlay::look`, through each class's property
  slots, found once):
  - Meshes where they stand, turned, scaled, in their render style
    (modulated: the player's floor shadow, a `CTV_Shadow` quad), posed
    where their animation is (`AnimFrame`, below). A character's
    animation from the shared sets is borrowed the first time it plays
    one its mesh lacks (the run, `runf`). While tweening into an
    animation, it's drawn blended from the frame the tween began at to
    the animation's first.
  - Sprites.
  - Movers where their scripts have moved them.

  Lighting is the level's lightmaps, as worked out when it loaded.

While playing, V lets the camera loose: the keys fly it round the level
as it plays on, the player standing, and V again gives it back to the
game.

### The player's input

`ScriptLevel::controls` (forward, strafe, turn, look up, each −1 to 1)
is what the keys would say. Each tick it sets the input axes the way the
engine does from DefUser.ini's bindings, 20 times their speeds as UE1's
input scales a held key: `aBaseY` ±6000 for `MoveForward` (Speed 300),
`aStrafe` ±6000, `aBaseX` ±3000 for `TurnLeft`/`TurnRight` (150),
`aLookUp`, and the rest 0. Walking caps its acceleration, so only
turning and the scripts' own factors (climbing) feel the scale; at 1x
the keys turned 20 times too slowly and ladders climbed at 6 a second. Then come `PlayerInput` and `PlayerTick`. The
Fallen's scripts work out the rest:
- `CTVE_PlayerPawn.PlayerInput` turns `aBaseY` into `aForward` and
  `aBaseX` into `aTurn`. It zeroes them all while the HUD's letterbox is
  up, in a cut scene.
- `PlayerWalking.PlayerMove` takes 0.4 of them for the acceleration (a
  walk: 96 units a second).
- `UpdateRotation` turns her.

`reluva-tool unreal run` reports what moved on each map. A test harness
walking a player forward, turning, forward again, then strafing, on
TrainingRoom, M01_KiraL2, M03_WorfL1, M04_SiskoL1 and M01_KiraL1A after
its opening: she walks, turns, stops at walls and in corners, slides
along them, steps down off slopes, and doesn't leave the level.

## In Reluva

`reluva-tool unreal play` plays the page without a window, through a
software renderer (`engines/unreal/tools/soft_renderer.cpp`: textured, depth tested,
blended triangles, enough to see a frame), driven by steps:
`play`, `wait <s>`, `hold <key> <s>`, `down`/`up <key>`, `press <key>`
(letters, `left` `right` `up` `down`, `space`, `enter`, `escape`, `f1`...),
`travel <url>` (as the game's `ClientTravel`: "CM_SpaceStation/intro",
"?load=QuickSave.dsa"), `saves <dir>` (where games are saved),
`savegame <name>` (saved now: `QuickSave`, else the next `Save<n>`),
`wav <file>` (the sound heard since the last, mixed offline), `sounds`,
`watch <actor>` (the camera let loose in front of a live actor),
`teleport <actor> <dx> <dy> <dz> <yaw>` (the player put by an actor,
facing yaw degrees, falling), `where` (the player's place, velocity,
state, physics and animation), `actor <name>` (a live actor's place,
state, collision and look), `lines` (the dialogue lines said so far),
`view` (the game camera's place, turn and field of view, and whether
it's in solid), `prop <actor> <property>` (a live property as text, a
struct's fields listed), `console <command>` (as the player types it: `set
DS9_FederationPhaserRifleAmmo AmmoAmount 20`),
`drag <dx> <dy>`, `click <x> <y>`, `shot <file.ppm>`. Checked that way:
- **M01_KiraL2:** Kira runs down the stone corridor from behind, the
  camera swinging to her side, and turns into the wall with the camera
  pulled in beside her.
- **M04_KiraL1:** she rides the lift down from her start and stands in
  the turbolift.
- **M01_KiraL1A:** the opening's directors fly over the monastery
  bridge, then follow her and a monk, letterboxed.

**Speed** (`unreal play`, 20 s of play, 600 frames; November 2026). `unreal play`
prepares each frame for drawing as the Maps page does: it poses, lights
and uploads the characters and runs the HUD. Only the drawing itself is
left out.
- M10_WorfL1A, the slowest map, now takes about 12.5 to 15 s; it took
  21.5 s before the work below. M03_KiraL2 takes about 5 s (12 s before).
- The scripts alone (`unreal run -all`, no drawing at all) take 128 s for
  all 74 maps. The slowest is M11_WorfL1, at 2.9 s for its 20 s.
- The work:
  - **Fire, water and the other procedural textures** tick only when
    they're drawn, as UE1's do. Engine.dll's `UTexture::Update(Time)`
    ticks a texture by the time since it last updated, and the renderer
    gets each texture it draws through `UTexture::Get`, which calls it.
    The Maps page now notes which textures each frame draws
    (`TextureUseRecorder`) and ticks only those. Before, every one in the
    level stepped every frame, seen or not.
  - **The water texture's step and slopes** read their rows directly,
    without a modulo for every cell. The picture is the same.
  - **Packages are read whole at once.** They had been read a byte at a
    time through stream iterators, a fifth of a level's start.
  - **Answers kept:**
    - `VmObject::find` with a string literal remembers its slot for each
      class.
    - The level's walls between a light and an actor (`wallBetween`) are
      remembered for the same two places, since the BSP doesn't move. A
      character standing still asked it of every light each frame.
  - **The overlay:** only the rows drawn on the frame before are
    cleared, not all 2.7 MB of it.
- What's left most, from callgrind on M10_WorfL1A:
  - Posing, lighting and uploading the characters for drawing: about
    half, the IK's reaches (worked out twice a tick for each character
    holding a gun) among it.
  - The HUD's scripts: a tenth.
  - The viewer's own text box: a tenth.
  - Sounds decoded from MP3 the first time each is heard, and meshes
    read as they first come into view: once each.

The biggest maps' ticks took twice as long. Profiled on M03_KiraL2
(callgrind, then stack samples later in the run), these made it about
twice as fast (20 seconds of play: 21.6 s, now about 10.7 s):
- The moves' loops (blocking, touching, traces) go through only the
  actors that collide with actors: a list made each tick (again when
  `SetCollision` changes one), not all 4,000. Traces read the classes'
  collision slots rather than asking `isA` by name.
- Movers far from a move are passed over by their brush's bounding
  sphere before the costly test in their own space. That also ended a
  bug: a flat mover brush (a force field's sheet) makes a BSP of one
  plane, solid all the way behind it; CM04_Lab refused 301 placings 2000
  units from one. Overlaps are also kept inside the brush's box.
- Names are lower-cased on the stack; a struct's properties are found by
  an index; a native's code, whether a function is `&&` or `||` and
  whether it has out parameters are found once; frames make their
  temporaries' list only when needed; vectors are written in place;
  values copy their string and fields only when they have them.

The effects' and Core's random numbers start again with each level, so
a map runs the same alone or in `unreal run -all`.

## Sources and checks

- **Where the facts come from:** the engine's input as set up in
  DefUser.ini and User.ini, and the game's own scripts (its camera).
- **How it was checked:** with `reluva-tool unreal play` (above), and
  against a full playthrough's recording
  ([research/playthrough.md](../research/playthrough.md)).
