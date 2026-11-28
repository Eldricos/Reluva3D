# Jedi Knight: unconfirmed items

What Reluva does that hasn't been checked against the running original.

## Read from the code, not seen running

None.

## Seen running, not traced in the code

None.

## Inferred

None.

## Guessed

None.

## Settled

- **November 2026: which textures are transparent.** Those whose flag
  bit 0 is set, clear where their colour is 0: through Direct3D they're
  loaded with a bit of alpha, clear just there, and drawn with the alpha
  test, every one seen through the light side matching a flagged
  texture ([formats/mat.md](formats/mat.md#behaviour)).
- **November 2026: a missing character.** It's drawn as the font's
  fallback's glyph, which for 32 is the marker block, not a space: a
  player named with a byte no font has, in the *Players* list
  ([formats/sft.md](formats/sft.md#header)).
- **November 2026: how a long subtitle is laid out.** Traced in the
  code (`0x4349d0`, `0x4352c0`): white space, a line break among it,
  goes with the word before it; a line ends before a word that would
  reach the width; lines are the strip's height and gap apart. Reluva's
  viewer now breaks lines by the same rules, which no subtitle of the
  game's tells from the ones seen before
  ([formats/smk.md](formats/smk.md#behaviour)).
- **November 2026: `\n` in strings.** `JK.EXE` keeps it as written, and
  never shows the one string that has it; Reluva now keeps it too
  ([formats/uni.md](formats/uni.md#layout)).
- **November 2026: which archive a name comes from.** `JK.EXE` searches
  five places, the episode's archives before the resources', one
  episode open at a time; Reluva now searches in that order
  ([formats/gob.md](formats/gob.md#behaviour)).
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
