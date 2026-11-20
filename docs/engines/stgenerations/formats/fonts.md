# Star Trek: Generations: fonts (`.FNT`)

> **Status:** decoded · **Code:** `src/engines/stgenerations/formats/font.h` · **Checked on:** the 2 CD Windows release, all 5 fonts · **Confidence:** confirmed unless marked

## Overview

The game's fonts, `.FNT` files.

## Layout

```
char[4]  "FONT"
uint32   size of the rest of the file
uint16   glyph count
uint8    transparent colour index
uint8    line height
uint32   0
glyph[]:
  uint16 character code
  uint16 width * height + 2
  uint8  width
  uint8  height              (top aligned, may be < line height)
  uint8  pixels[height][width]
```

## In Reluva

`Font` (`src/engines/stgenerations/formats/font.h`) reads them, for the
game's text and the bridge's. `reluva-tool stgenerations pictures <game
folder>` decodes every font with the pictures.

## Sources and checks

From reading the game's files, the 2 CD Windows release, while the engine was STG-Engine ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md) says how). All values are little-endian.
