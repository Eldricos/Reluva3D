# Known differences from the original (Murder on the Orient Express)

Where Reluva knowingly does something other than `motoe.exe` (English
1.0) does. Each was chosen on purpose, or follows from how Reluva is
built, and none changes what happens in the story. Bugs, once found, are
fixed, not listed here.

The format pages give the detail; this page gathers them.

## Drawing

- **Shadows are filled on the CPU.** The game uses the graphics card's
  stencil buffer. Reluva's renderer has none, so `shadows.cpp` keeps the
  stencil on the CPU. Triangles are filled at pixel centres, which may
  differ from a graphics card's edges by a pixel. A shadow triangle with
  a corner behind the camera is left out. See
  [shadows.md](../../formats/awe/shadows.md).
- **The specular light is a second pass.** Direct3D adds the specular
  colour after the texture in one draw. Reluva draws the textured model,
  then adds the specular in a pass over it (depth equal, no depth
  write). The result is the same sum. See
  [lighting.md](../../formats/awe/lighting.md).
- **Anti-aliasing changes at once.** The game resets the device; Reluva
  makes the frame's targets again. Shadow edges stay hard (their mask is
  made on the CPU). See [options.md](../../formats/awe/options.md).

## Characters and animation

- **A walk's root motion.** The game takes a base animation's root
  motion out of the pose through Granny's track groups and moves the
  model. Reluva takes out the distance the root moves in one loop
  instead (`Actor::motion`). The character goes the same way at the same
  pace. See [gr2.md](../../formats/awe/gr2.md).
- **An `anim` whose file is missing ends at once.** In the game it waits
  for ever, which would hang the script. No `anim` in the install names a
  missing file. See [characters.md](../../formats/awe/characters.md).
- **An `anim` finishes whether it played under the asked name or a
  renamed one.** See [characters.md](../../formats/awe/characters.md).
- **A speaker in another scene of the place isn't animated.** For a
  line's Event and Prevent the game finds the speaker in the location's
  other scenes too (`0x43bb60`) and animates them there, off screen.
  Reluva holds only the scene shown and skips those gestures. Nothing is
  seen either way. See [script.md](../../formats/awe/script.md).

## Time

- **Fades go by the clock.** The game steps a fade by 8/255 every 0.01 s
  but at most one step a frame, so it fades more slowly when frames are
  slow. Reluva steps by the time that has passed: 32 steps, 0.32 s. The
  recordings' fades measured 0.27 to 0.47 s: the longer ones are the
  game's frame-bound steps; the shortest is under the 0.32 s the game
  can't go below, within what a 30 fps video can tell of a ramp's ends. See [acts.md](../../formats/awe/acts.md).

## Saves and scenes

- **Scenes aren't kept in memory.** The game keeps every scene of the
  place loaded; Reluva reads a scene again when it's shown. Characters'
  positions are recorded when a scene is left and put back when it's
  shown again, and the player's position is kept per scene. Saves are in
  the game's own format either way. See
  [saves.md](../../formats/awe/saves.md).

## Not done, as nothing in the install uses them

Kept for the other AWE games, where they may be needed:

- the lights' flicker and the `light` command;
- objects' own sounds (`sound`, `soundLoops`, `hoverSoundLoops`: empty or
  0 in every scene);
- animated pictures (`2DAN`, `NING`: in no scene);
- about 25 script verbs no script calls (most are only logged; see
  [script.md](../../formats/awe/script.md)).

## Only one release checked

Everything was worked out and checked on the English 1.0 install. Other
languages and patched releases haven't been tried.
