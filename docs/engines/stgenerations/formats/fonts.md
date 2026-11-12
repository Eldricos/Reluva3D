# Fonts (.FNT)

> **Status:** decoded · **Code:** `src/engines/stgenerations/formats/font.h` · **Confidence:** confirmed unless marked

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

## Sources and checks

From STG-Engine's notes on the 2 CD Windows release ([README](README.md) says how they were made). All values are little-endian.
