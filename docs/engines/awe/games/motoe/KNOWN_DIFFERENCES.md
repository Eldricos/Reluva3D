# Known differences from the original (Murder on the Orient Express)

Where Reluva knowingly does something other than `motoe.exe` (English
1.0) does. Each was chosen on purpose, or follows from how Reluva is
built, and none changes what happens in the story. Bugs, once found, are
fixed, not listed here.

The format pages give the detail; this page gathers them.

## Drawing

- **Shadows on a device without a stencil buffer** are filled on the
  CPU: their edges may differ from a graphics card's by a pixel, and a
  shadow triangle with a corner behind the camera is left out. With a
  stencil buffer (any current graphics card), they're drawn as the
  game's are. See [systems/shadows.md](systems/shadows.md).
- **The specular light is a second pass.** Direct3D adds the specular
  colour after the texture in one draw. Reluva draws the textured model,
  then adds the specular in a pass over it (depth equal, no depth
  write). The result is the same sum. See
  [systems/lighting.md](systems/lighting.md).
- **Anti-aliasing changes at once.** The game resets the device; Reluva
  makes the frame's targets again. Shadow edges stay hard (their mask is
  made on the CPU). See [systems/options.md](systems/options.md).

- **Characters are skinned and lit on the CPU**, each frame, spread over
  the machine's cores, as Direct3D's fixed-function pipeline would light
  them: the same colours, by the same per-vertex formula. Their meshes
  stay on the GPU and only the vertices are sent each frame. See
  [systems/lighting.md](systems/lighting.md) and
  [../../README.md](README.md#frame-time).
- **Text is drawn by Reluva's own TrueType renderer**, not Windows'.
  The game's fonts are used, sized as Windows sizes them, but letters can
  differ a little in their edges: in the walkthrough the dialogue's lines
  come out about 2 % wider than Reluva's. Where the lines break is the
  same everywhere text is wrapped: the spoken line, the choices and a
  movie's subtitles (`0x43db80`) and the interface's text (`0x414720`)
  all use the game's rule, two spaces kept after a sentence's end
  ([systems/dialogue.md](systems/dialogue.md),
  [systems/interface.md](systems/interface.md)).
- **A choice low in the box is always drawn.** The game leaves out a
  choice whose rows would reach y 745 (`0x43d1cb`: its top plus its
  height); the choices never get that far down, so Reluva doesn't test
  it.

## Characters and animation

- **A walk's root motion.** The game takes a base animation's root
  motion out of the pose through Granny's track groups and moves the
  model. Reluva takes out the distance the root moves in one loop
  instead (`Actor::motion`). The character goes the same way at the same
  pace. See [../../formats/gr2.md](../../formats/gr2.md).
- **An `anim` whose file is missing ends at once.** In the game it waits
  for ever, which would hang the script. No `anim` in the install names a
  missing file. See [systems/characters.md](systems/characters.md).
- **An `anim` finishes whether it played under the asked name or a
  renamed one.** See [systems/characters.md](systems/characters.md).
- **A speaker in another scene of the place isn't animated.** For a
  line's Event and Prevent the game finds the speaker in the location's
  other scenes too (`0x43bb60`) and animates them there, off screen.
  Reluva holds only the scene shown and skips those gestures. Nothing is
  seen either way. See [systems/script.md](systems/script.md).

## Time

- **Fades go by the clock.** The game steps a fade by 8/255 every 0.01 s
  but at most one step a frame, so it fades more slowly when frames are
  slow. Reluva steps by the time that has passed: 32 steps, 0.32 s. The
  recordings' fades measured 0.27 to 0.47 s: the longer ones are the
  game's frame-bound steps; the shortest is under the 0.32 s the game
  can't go below, within what a 30 fps video can tell of a ramp's ends. See [systems/acts.md](systems/acts.md).

## Saves and scenes

- **Scenes aren't kept in memory.** The game keeps every scene of the
  place loaded; Reluva reads a scene again when it's shown. What the
  game would have kept is kept instead: characters' positions, and what
  scripts left them doing (paused, their shadow, what they hold, an
  endless animation), by scene, until a new place is loaded (an act's
  location, or a `scene` in another place: the main menu counts as one,
  so a game left for it starts afresh); the player's position per
  scene. Objects' `hide` and `show` don't need
  keeping: entering a scene tests every object's condition afresh in the
  game too. A character's routine starts again from its beginning in
  both (`0x440690`).
  Saves are in the game's own format either way. See
  [systems/characters.md](systems/characters.md#leaving-a-scene-and-coming-back)
  and [systems/saves.md](systems/saves.md).

## Not done, as nothing in the install uses them

Kept for the other AWE games, where they may be needed:

- the lights' flicker and the `light` command;
- objects' own sounds (`sound`, `soundLoops`, `hoverSoundLoops`: empty or
  0 in every scene);
- animated pictures (`2DAN`, `NING`: in no scene);
- about 25 script verbs no script calls (most are only logged; see
  [systems/script.md](systems/script.md)).

## Only one release checked

Everything was worked out and checked on the English 1.0 install. Other
languages and patched releases haven't been tried.
