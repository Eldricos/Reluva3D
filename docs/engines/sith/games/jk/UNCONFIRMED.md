# Jedi Knight: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

- **How a level's surfaces are lit:** the viewer lights a corner by its
  light and its surface's extra light, or by its sector's extra light
  where that's more, each up to 1; skies and light mode 0 unlit, light
  mode 1 by the surface's extra light alone, through the sector's
  colormap, as `JK.EXE`'s colormap renderer does
  ([formats/jkl.md](formats/jkl.md#light)). The game drawing a level in
  software, compared frame by frame, would settle it.
- **Fonts' spacing:** a gap after each glyph, white space moving on by
  the font's space width, and a missing character drawn as the font's
  fallback ([formats/sft.md](formats/sft.md)).
- **Which pictures are transparent:** a picture's transparent colour only
  where its flags' bit 0 says so, and a texture's from its own flag and
  colour ([formats/bm.md](formats/bm.md), [formats/mat.md](formats/mat.md)).

## Seen running, not traced in the code

None yet.

## Inferred

- **Which palette an 8-bit picture without its own takes, on the HUD:**
  the menus' screens are known from the code
  ([formats/bm.md](formats/bm.md#behaviour)); which palette the game's
  HUD pictures are drawn with in play isn't. The viewer starts with the
  interface's.
- **`\n` in strings** is taken for a new line
  ([formats/uni.md](formats/uni.md#layout)).

## Guessed

- **Which archive a name comes from:** Reluva looks in the install's
  `Resource` archives, then its `Episode` ones, each folder's in name
  order ([formats/gob.md](formats/gob.md#in-reluva)). The game was seen to
  try loose files first, then its archives
  ([formats/gob.md](formats/gob.md#behaviour)); which archive wins when
  two have a name would be settled by tracing how `JK.EXE` searches them.

## Settled

- **November 2026: a sound class's flag `0x1` loops the sound.**
  `JK.EXE`'s sound code keeps it playing, and stops it when its thing
  goes ([formats/snd.md](formats/snd.md#flags)).
- **November 2026: a font's space isn't its glyph's width.** `JK.EXE`
  moves white space on by the header's space width, and never draws the
  space's glyph ([formats/sft.md](formats/sft.md)).
- **November 2026: which CD the game wants.** `JK.EXE` checks
  `JK_.CD`'s value, one bit for each disc, as Reluva does
  ([formats/jk-cd.md](formats/jk-cd.md)).
- **November 2026: transparent colours.** A texture's is its own flag and
  colour (colour 0 in every file), a picture's is its header's colour
  when its flags' bit 0 is set, from `JK.EXE`'s loaders
  ([formats/mat.md](formats/mat.md), [formats/bm.md](formats/bm.md)).
- **November 2026: a string table's count is a limit.** `JK.EXE`, run in
  the emulator, warns that `jkstrings.uni` ends before its count and
  reads on ([formats/uni.md](formats/uni.md#layout)).
