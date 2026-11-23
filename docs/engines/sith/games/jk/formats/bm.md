# Jedi Knight: pictures (`.bm`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/bitmap.cpp` · **Checked on:** US retail, all 146 pictures and the 27 fonts' · **Confidence:** confirmed unless marked

## Overview

The interface's pictures, in `Res1hi.gob`'s `ui\bm\`: the menus'
backgrounds (640x480, with a palette of their own), buttons and icons,
the HUD's pieces. 33 come in two copies, 8-bit and 16-bit (`icf_pro8.bm`
and `icf_pro16.bm`). A file holds one or more cels of the same size, a
button's states or an animation's frames. The fonts' glyphs are `.bm`
pictures too ([sft.md](sft.md)).

| Kind | Files |
|---|---|
| 8-bit, a palette of its own (flags 2) | 11: the menus' backgrounds |
| 8-bit, a transparent colour (flags 1) | 72: the menus' buttons and icons, the HUD's pieces |
| 8-bit, neither (flags 0) | 22: the Force powers' icons, the sabers' and the Force meter's pieces |
| 16-bit, a transparent colour (flags 1) | 41 |

## Layout

All numbers little-endian.

### Header

128 bytes, 32 numbers of 4 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `BM  ` |
| 4 | 4 | Version: 70 (`0x46`) in every file |
| 8 | 4 | 0 in every file |
| 12 | 4 | Flags: bit 0, the transparent colour at 28 is used (`0x42cf89`); bit 1, a palette ends the file (`0x42cfb7`) *(from the code)*. 0, 1 or 2 in the files. They don't say whose palette a picture without its own takes: that's where it's drawn ([Behaviour](#behaviour)) |
| 16 | 4 | How many cels |
| 20 | 8 | Where the picture goes, x and y: on a menu screen, from the screen's top left; in the HUD, from the top left of the status picture it's drawn into (`sthealth.bm`: 21, 21) ([Behaviour](#behaviour)) *(from the code)*. Set on 41; 0, 0 on the rest |
| 28 | 4 | The transparent colour, used when bit 0 of the flags is set: 0 in the 8-bit pictures, `0xf81f` (magenta) in the 16-bit ones |
| 32 | 4 | The colour format's kind: 1 in the 16-bit pictures, 0 in the 8-bit ones, as in a material's header ([mat.md](mat.md#header)): 0 palette indexes, 1 red, green and blue *(inferred)*. Bytes 32 to 87 are the format, read as one (`0x42ce6e`) *(from the code)*. Not about transparency: the 8-bit pictures with a transparent colour have 0 |
| 36 | 4 | Bits a pixel: 8 or 16 |
| 40 | 12 | 16-bit: red, green and blue's bits: 5, 6, 5 |
| 52 | 12 | 16-bit: where each starts: 11, 5, 0 |
| 64 | 12 | 16-bit: the bits each lacks of 8: 3, 2, 2 |
| 76 | 52 | 0 in every file |

### Cels

For each cel: its width and height (4 bytes each), then its pixels, row
by row from the top, one byte each (8-bit) or two (16-bit).

### Palette

When bit 1 of the flags is set: 256 colours of three bytes, red, green and
blue, at the end of the file.

## Behaviour

`JK.EXE` reads a picture at `0x42cd30`; the fonts' strips go through the
same code (`0x42cbcc`). A picture with bit 0 of its flags set has its transparent
colour made each cel's colour key (`0x42cf89` to `0x42cf94`, `0x423830`)
*(from the code)*. The 22 without it are drawn whole: the Force powers'
icons and the meter's back have no colour 0 at all, and the sabers'
colour 0 is drawn as the palette's colour *(inferred)*.

### The menus

The menus load 35 pictures, their backgrounds and the buttons they
share, once (names at `0x52b238`, read at `0x412acd`), and start with
`bkmain.bm`'s palette (`0x412b4d`). Each screen is opened with its
background (`0x4129a0`), and opening it makes the background's palette
the screen's (`0x50fad7` to `0x50fb6b`): every 8-bit picture on a
screen is drawn with its background's palette. A background without a
palette of its own (`bkdialog.bm`) leaves the one already there *(from
the code)*.

- **The Force powers' screen** loads its own 17 pictures, the powers'
  icons, the stars and the meter's pieces (`0x4167cc`), and opens with
  `bkforce.bm` (`0x4167c2`) *(from the code)*: so its icons look right
  only with that palette, and at their x and y each sits in one of the
  screen's sockets.
- **The buttons, arrows and lists** are drawn on many screens, with each
  one's palette ([The shared pictures](#the-shared-pictures)). Every background but `bkfieldlog.bm` has colours 224 to
  255 of the interface's palette (`uicolormap.cmp`'s), and colour 0
  black or nearly, so a picture keeping to those looks the same on any
  of them; 16 of the 72 with a transparent colour do.
- **A button's place**: where the screen doesn't give one (a negative x
  or y), the picture's x and y, and its first cel's size, are used
  (`0x512250`) *(from the code)*. These are within the 640x480 screen
  (`flok.bm`: 568, 344).
- **The character screens** (`bkbuildmulti.bm`, `bkbuildload.bm`) have
  `uicolormap.cmp`'s palette as their own, and load that colormap to draw
  the character (`0x419af9`) *(from the code)*. The sabers' pictures,
  named in `misc\sabers.dat`, are loaded by the multiplayer character
  screen's code (`0x419378`, `0x4194e8`), which uses
  `bkbuildmulti.bm` (`0x41924d`) *(from the code)*, so they take its
  palette *(inferred)*.

### The shared pictures

Which screens draw each of the 22 shared pictures, read from the
screens' tables in `JK.EXE`'s data and the background each is opened
with (`0x4129a0`'s 43 callers) *(from the code)*. A list draws `up15.bm`
and `down15.bm` (the pair at `0x52b170`), a check box `check.bm` (each
screen's picture 15, `0x511284`), a slider the pair its table names, a
picture button its own:

| Pictures | Drawn on (by background) |
|---|---|
| `up15.bm`, `down15.bm` | `bkMain.bm` (the players' list), `bkSingle.bm` (2 screens), `bkMulti.bm` (3: the game's settings, the providers, the games), `bkSetup.bm` (8: Setup's pages, the cut scenes' list, the saved games' and the saved controls' lists), `bkBuildLoad.bm` (2) |
| `check.bm` | `bkMain.bm` (a new player's difficulty), `bkMulti.bm` (the game's settings), `bkSetup.bm` (8 of Setup's pages) |
| `sliderBack.bm`, `sliderThumb.bm` | `bkSetup.bm` (2 of Setup's pages) |
| `sliderBack200.bm`, `sliderThumb.bm` | `bkSetup.bm` (2 of Setup's pages) |
| `arrowLeft.bm`, `arrowRight.bm` | `bkMulti.bm` (the game's settings), `bkBuildMulti.bm` (a multiplayer character), `bkBuildLoad.bm` (a new character) |
| `flOk.bm` | `bkFieldLog.bm`: the map (by its buttons) and the objectives |
| `flRotLef.bm` to `flReset.bm` (12) | `bkFieldLog.bm`: the map only |
| `objectivescheck.bm` | `bkFieldLog.bm`: the objectives, drawn by their screen's own code (`0x417c1c`) |

Two dialogs, the CD prompt and a message with only *Ok* (`0x416970`,
`0x416a90`), are opened with no background (`0x4168d7`, `0x4168e6`), so
they keep the palette of the screen under them *(from the code)*.

### The HUD

`statusleft.bm` and `statusright.bm` sit in the screen's bottom left
and bottom right corners (`0x4076ca` to `0x40770e`). The other pieces
are drawn into them, each at its own x and y from their top left:
`sthealth.bm` and `stshield.bm` into the left (`0x408112`,
`0x408013`), `stbat.bm`, `stfieldlite.bm`, `stfrc.bm` and
`stfrcsuper.bm` into the right (`0x407dfa`, `0x407e52`, `0x407ea7`,
`0x407f0f`) *(from the code)*.

Each piece has an 8-bit and a 16-bit file (`statusLeft.bm`,
`statusLeft16.bm`; the names at `0x5259ec`), and the HUD loads the
8-bit ones only when the display is 8-bit (`0x407597`), keeping their
pixels as they are (`0x42d4b0`) *(from the code)*. So the 8-bit pieces
are drawn with the display's palette in play, the level's: in
`01narshadda.jkl` the 132 pixels of `statusleft.bm` and
`statusright.bm` where `01narsh.cmp`'s palette and `uicolormap.cmp`'s
differ (colour 95) show `01narsh.cmp`'s colour, in a frame of the
running game. With Direct3D the display is 16-bit and the 16-bit
pieces are used, with no palette *(from the code)*.

## In Reluva

`parseBitmap` (`src/engines/sith/formats/bitmap.h`) reads one into a
`Bitmap`; `bitmapToImage` makes a cel RGBA, through its own palette or
one it's given, its transparent colour clear where its flags say it's
used and the caller wants it. The data viewer's
Pictures page shows every cel, with the interface's palette or any
colormap's or background's. `reluva-tool sith formats <install>` reads them
all.

## Sources and checks

- **Where the facts come from:** the 146 pictures and the 27 fonts'
  compared: every file's size is the header, its cels and its palette to
  the byte; the editing community's published write-ups of the format,
  which agree; the pictures drawn in the data viewer with each palette,
  and the Force powers' icons placed on their screen; `JK.EXE`'s reader
  (`0x42cd30`), and the menus' and the HUD's code that loads and draws
  the pictures.
- **How it was checked:** every picture read by `reluva-tool sith
  formats`; `sith.unit.formats` on 8-bit and 16-bit pictures made by the
  test; a frame of the first level from `reluva-tool sith host`, its HUD
  compared pixel by pixel with the 8-bit pieces through each palette.

## Open questions

- Whether the HUD's 8-bit pieces take the palette of the level's first
  colormap in every level, or of whichever colormap the game makes the
  display's: seen in the first level only, where they're the same
  colormap.
