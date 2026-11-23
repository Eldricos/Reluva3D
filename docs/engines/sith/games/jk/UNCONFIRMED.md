# Jedi Knight: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

- **A missing character drawn as a font's fallback**
  ([formats/sft.md](formats/sft.md)): the fonts' spacing was seen
  running, but no text met had a character its font lacks.
- **Which textures are transparent:** a texture's transparent colour from
  its own flag and colour ([formats/mat.md](formats/mat.md)); the
  pictures' was seen running.

## Seen running, not traced in the code

- **How a long subtitle is laid out:** its lines a font's height apart,
  a line broken at a space centred with that space in its width; seen in
  one subtitle of `03-04A` ([formats/smk.md](formats/smk.md#behaviour)).

## Inferred

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

- **November 2026: how a level's surfaces are lit.** The game's first
  frame of Nar Shaddaa and the viewer's, from the same start with the
  game's 90-degree view, agree within 2 of 255
  ([formats/jkl.md](formats/jkl.md#light)).
- **November 2026: a sector's tint.** Under the water in Baron's Hed the
  game's palette is the tint's multipliers, rounded to the nearest, to
  the pixel; the viewer now rounds as it does
  ([formats/jkl.md](formats/jkl.md#tint)).
- **November 2026: fonts' spacing.** A full subtitle, the game's and the
  viewer's, is the same to the pixel, once the viewer spaces its lines a
  font's height apart and centres a line with the space it was broken at
  ([formats/sft.md](formats/sft.md), [formats/smk.md](formats/smk.md)).
- **November 2026: the HUD's palette and transparency.** The HUD shows
  the level's colours in Baron's Hed as in Nar Shaddaa, each piece clear
  at colour 0; and which of a level's colormaps it takes changes nothing
  in any retail level ([formats/bm.md](formats/bm.md#the-hud)).
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
