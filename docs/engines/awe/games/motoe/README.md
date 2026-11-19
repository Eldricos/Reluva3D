# Murder on the Orient Express

Game id `motoe`, the first game on the [AWE engine](../../README.md).

## Releases

| Release | Recognised by | State |
|---|---|---|
| English 1.0 (retail, Windows) | `motoe.exe` and `Text/itemlist.txt` | Plays from a new game to the end credits |
| Other releases | The same files, whatever their contents (a fallback) | Not tried |

## Where it stands

Worked out from a set of the game's files (the executable and DLLs, three
scenes, two Granny files, the text tables, two backgrounds):

- **Detection:** `motoe.exe` and `Text/itemlist.txt` (English 1.0), with
  a fallback for other releases.
- **Scenes (`.ac2`): read.** The layout was read from `motoe.exe`'s
  scene loader and each object class's load function, and checked byte
  for byte against the samples. See
  [formats/ac2.md](formats/ac2.md). Scenes list their
  pictures (with screen positions and depth), hotspots (rectangles,
  scripts, sounds), characters (model, position, animations, walk
  points), particles, lights, videos, the camera, music and events. Many
  fields are named only by their offset in the original's objects so far.
- **Granny models and animations (`.gr2`): read and drawn.** Sections are
  compressed with Oodle1 (RAD's own compression, built into Granny),
  textures with Bink's, and some animation curves
  in Granny's compressed formats. All three are decoded natively, worked
  out from the game's `granny2.dll` and checked against it byte for byte
  (bit for bit for the curves) on every file of the install. The engine
  no longer needs the DLL; the tools still run it in the x86 interpreter
  to compare. Skeletons, skinned meshes, textures and the animation
  curves all work. Poirot renders textured and walking. See
  [../../formats/gr2.md](../../formats/gr2.md).
- **Text tables and script commands: read.** UTF-16 tab-separated
  tables; commands split into verbs and arguments. See
  [formats/text.md](formats/text.md).
- **Cameras: worked out.** Scenes' cameras are Granny cameras, and
  Reluva's matrices match granny2.dll's. Characters are drawn into the
  Scenes page through them, posed by their idle (or sitting) animation,
  between the background and the cutouts in front.
- **Scenes' video loops and conditions.** BINK objects play in the
  Scenes page, placed as the game's Direct3D sprites are (scale, then a
  turn about the top-left corner). Objects come and go with their
  conditions (`moving >0`). M toggles the train moving (the countryside
  streaking past the windows) or stopped (the snowscape), and W toggles
  the weather. Salon_01 matches a walkthrough video of the game.
- **Walk grids: read.** Each camera's 64 × 64 grid of where characters
  can walk is understood: how cells map to the floor, what the values
  mean, how clicks and `walk a b` pick cells, and the game's three tries
  at a path. G shows a scene's grid over its background, and clicking
  the floor walks the player's character there along a path planned as
  the game's own path-finder plans it: a native port, checked point for
  point against `motoe.exe`'s code (`awe path-check -all`).
- **Scripts: the runner works.** Commands run as the game's runner runs
  them: queued as events, each finishing at once or when what it started
  is over, the `a` forms not waiting, the door macros and script files
  expanded (see [systems/script.md](systems/script.md)).
  Clicking a hotspot in the Scenes page runs its script: walks, scene
  changes, sounds, items, tags, dialogue and the characters' verbs work.
  `reluva-tool awe script-check` lists every command an install's scripts
  use, and every word of its conditions; every one is handled. Items
  are picked up in the inventory and used on hotspots and characters.
- **The mouse: as the game's.** The object under it is found by the
  game's pick (`pickFirst` objects, then the characters by their bones'
  boxes, then everything by outline or rectangle, front to back), and
  clicks use it, pictures with scripts among them. The game's cursor is
  drawn in place of the system's: the arrow and the verb the object
  under it asks for (its `cursor`, or the first of its
  `conditionalCursors` that holds), or the item in hand. Hotspots light
  up and sound under it, and Escape and Enter press the hotspots whose
  key they are (see [formats/ac2.md](formats/ac2.md#the-mouse-picking-and-cursors)).
- **Dialogue: plays.** Conversations run as the game's dialogue manager
  runs them (see [systems/dialogue.md](systems/dialogue.md)):
  the opening, the choices, each line's commands, recording and text in
  the game's dialogue box, progress kept for the conditions, the
  characters' generic lines and goodbyes.
- **Acts: a new game plays.** N in the Scenes page starts one: the main
  menu, Play, act 01A's scripts and location, and on through the scenes'
  own events, as `acts.txt` and the game's act code say (see
  [systems/acts.md](systems/acts.md)). Scene music, timers,
  completion conditions, `savescene`/`gotoscene`, fades and movies (with
  their subtitles; Escape skips them), loading screens, and saved games
  in the game's own format (the save and load screens, F2/F3, the
  developers' saves: see [systems/saves.md](systems/saves.md)),
  the options screen and all its settings, anti-aliasing included
  ([systems/options.md](systems/options.md)),
  walking into scenes, with followers, and the characters' shadows
  ([systems/shadows.md](systems/shadows.md)), the
  puzzle screens: the torn letter, the safe and the puzzle box
  ([systems/puzzles.md](systems/puzzles.md)), and the
  interface ([systems/interface.md](systems/interface.md)):
  the bar along the top (travelling between the cars), the inventory
  window with its items, examining, combining, the scrapbook, the
  passports and the fingerprint cards, and the cards held up work.
- **Characters: lit as the game lights them.** Direct3D 8's
  fixed-function lighting, per vertex: the scene's `LITE`s (directional,
  point and spot lights), the camera's ambient colour (a blue one at
  night outdoors, with the lights off), and each character's material
  (see [systems/lighting.md](systems/lighting.md)). T in
  the Scenes page toggles night.
- **Characters: animated as the game animates them.** Up to ten
  animations each on body, head and eye layers, blended by the files'
  track masks (a weight per bone, saying how much an animation moves
  it); blinking, idle gestures and their routines; `anim`,
  `turn`, `look`, `rotate`, `pause`/`resume`, `attach`; talking in
  conversations, heads turned to their Target and mouths moving with the
  voice (see [systems/characters.md](systems/characters.md)).
- **Particles** (`SNOW`, `RAIN`, `PART`) are simulated by the rules of
  the game's own update method and drawn as its point sprites are, among
  the scene's 2D layers by depth (snow outside the Salon's windows when
  the weather is on and the train stopped). In the snowy exteriors the
  player breathes, and leaves footprints that fade (see
  [systems/characters.md](systems/characters.md#outdoors-footprints-and-breath)).
- **The game's fonts** (`Text/fonts.txt`, `Interface/Fonts`) are drawn
  with stb_truetype, and the movies' subtitles show under them.
- **Bink movies: decoded** by the decoder Elite Force uses too, the same
  as the game's own `binkw32.dll` on every frame of all 87 files (see
  [bink.md](../../../../common/video/bink.md)), and play in the Movies page with
  their sound.
- **reluva-tool:**
  - `awe ac2-dump` (one scene in full, or many checked to their ends);
  - `awe gr2-info` (sections, and with `-t` the whole type tree);
  - `awe model-dump` (a summary, posed by an animation, written as OBJ with
    its textures);
  - `awe table-show` (a text table with its commands split);
  - `awe page-shot` (a viewer page drawn without a window, saved as TGA);
  - `awe camera-check` (a scene's camera as granny2.dll and Reluva build it);
  - `awe files-check` (every Granny file, scene, text file and movie of an
    install read, with cameras and compressed curves compared against the
    DLL, and with `-textures` and `-oodle` the textures and sections too:
    the regression test);
  - `awe pose-check` (a model posed by an animation, by Reluva and by
    granny2.dll, compared bone by bone);
  - `awe bink-check` (movies decoded by Reluva and by the game's binkw32.dll,
    compared pixel by pixel; `-yuv` before the colour conversion);
  - `awe path-check` (paths planned by Reluva's port and by `motoe.exe`'s code,
    compared; `-all` over every scene);
  - `awe game-run` (every act, or developers' save, played without a window
    or a player, as far as it goes: what the scripts ask that Reluva
    doesn't do; `-hotspots`, `-explore n` to click on; `-solve` to
    search for a way from a new game to the credits, `-play` to play one
    back; `-3d -draw` plays it in a window, every frame drawn, for timing
    with `RELUVA_PERF`);
  - `awe bink-info` (a movie's header and frames; `-yuv`, `-wav`, `-tga` to
    decode it);
  - `awe save-check` (a saved game's records, read and written back).
- **Tests:** the unit tests (`tests/engines/awe/unit/`, suites
  `awe.unit.formats`, `.scene`, `.script`, `.dialogue`, `.characters`,
  `.saves`) and the basic engine test (`awe.engine`), on files built in the
  tests; and the game-files test (`awe.gamedata.*`, with `RELUVA_MOTOE`).
  See [TESTING.md](TESTING.md).

Sweeps over the full English install (`awe ac2-dump` over all 394 scenes,
`awe files-check -gr2` over all 1,803 Granny files) found no records, fields
or member types beyond those Reluva reads.

### Frame time

The game itself asks little of a machine; what costs is how Reluva does
on the CPU what the game's Direct3D device did. `RELUVA_PERF=1` writes
where a frame's time goes every three seconds (to the console, with the
scene): the 2D (the pictures put together into layers, the interface,
the text, the cursor), the characters (posed, skinned and lit, onto the
GPU, drawn) and the present (the GPU's frame and the overlay shown).
`reluva-tool awe game-run <install> -play <route> -mouse -3d -draw` plays a
route in a window drawing every frame, so builds can be compared on the
same scenes.

What was done about it, checked by screenshots of the route identical
byte for byte before and after:

- **Drawn once a frame.** The 2D was put together again by each thing
  that changed it in a frame (twice in a conversation: the box and the
  characters); now once, at the end.
- **No clearing for nothing.** With 3D, the pictures go into layers kept
  on the GPU and the screen buffer is only the overlay; clearing it to
  black first, a byte at a time, cost 3 ms a frame.
- **Characters skinned on every core.** Each character's and prop's
  skinning and per-vertex lighting (the game's fixed-function lighting,
  done as Direct3D does it) is work of its own, so they're done at once
  (`common/parallel.h`), then drawn in their order.
- **Meshes kept on the GPU.** A character's meshes were made anew each
  frame, each with two uploads in command buffers of their own; now
  they're made once and only their vertices replaced, the uploads of a
  frame sent together in one command buffer before its draws
  (`setMeshVertices`, `SdlGpuRenderer::flush`).

In the market at the start of the game (Sirkeci.Market_03, measured
under a software GPU, so the present isn't a real card's), the 2D went
from 5 to 2.3 ms a frame and the characters from 9 to 3.4 ms.

## Using it

- **Pages.** A game is played from the Scenes page (N starts one). The
  other pages are data viewers: Models (textured, animated, with the
  mouse to turn and zoom), Movies, Pictures, Text and Sounds.
  Tab moves between them. The window's title names the page and what it
  shows, then the frames shown in the last second.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [formats/](formats/) | The game's files: [scenes (`.ac2`)](formats/ac2.md), [text files and script commands](formats/text.md); Granny 2 is the engine's ([gr2.md](../../formats/gr2.md)) |
| [systems/](systems/) | How the game behaves: [acts and scenes](systems/acts.md), [the script runner](systems/script.md), [dialogue](systems/dialogue.md), [characters](systems/characters.md), [lighting](systems/lighting.md), [shadows](systems/shadows.md), [the interface](systems/interface.md), [puzzles](systems/puzzles.md), [options](systems/options.md), [saved games](systems/saves.md), [movies and video loops](systems/movies.md) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What's read from the code or inferred but not seen running |
| [TESTING.md](TESTING.md) | The automated checks, and a playthrough by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out |
| [PLAN.md](PLAN.md) | The original plan: what the install holds, the hard parts, the phases |
