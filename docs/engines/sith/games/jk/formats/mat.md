# Jedi Knight: materials (`.mat`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/material.cpp` · **Checked on:** US retail, all 1,983 materials · **Confidence:** confirmed unless marked

## Overview

The surfaces of the levels and models: `Res2.gob`'s `mat\` (748, the
levels') and `3do\mat\` (1,235, the models'). Each has one or more cels,
a plain colour or a texture, as an animated surface steps through. Every
texture is 8-bit, drawn through the level's colormap ([cmp.md](cmp.md)).
49 hold only colours; 1,934 have textures, 64x64 and 128x128 the most
common sizes; 582 of their 2,155 textures have four sizes each (mip
levels).

## Layout

All numbers little-endian.

### Header

76 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `MAT ` |
| 4 | 4 | Version: 50 (`0x32`) in every file |
| 8 | 4 | Type: 0 colours only, 2 with textures |
| 12 | 4 | How many cels |
| 16 | 4 | How many textures: 0 for type 0, else as many as the cels |
| 20 | 4 | 0 in every file |
| 24 | 4 | Bits a pixel: 8 in every file |
| 28 | 48 | Differs between files and makes no pattern: left as it was in memory *(inferred)* |

### Cels

For each cel, 24 bytes for a colour, 40 for a texture:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Kind: 0 a colour, 8 a texture |
| 4 | 4 | The colour, a palette index |
| 8 | 16 | Four floats, 1 in every file |
| 24 | 4 | Texture cels only: 0, or 3 in 22 cels |
| 28 | 8 | Texture cels only: in 2,133 cels one of these two is 0 and the other `0xbff78482`, a value the header's 48 bytes hold too, left as it was in memory *(inferred)*; in 22, `0x302` and `0x81` |
| 36 | 4 | Texture cels only: which texture; in every file, the cel's own number |

### Textures

For each texture, 24 bytes, then its mip levels:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Width |
| 4 | 4 | Height |
| 8 | 4 | 1 when colour 0 is transparent *(inferred)*, else 0 |
| 12 | 8 | 0 in every file |
| 20 | 4 | How many mip levels: 1, 3 or 4 |

Each mip level is half the width and height of the one before, its
pixels a byte each, row by row.

## Behaviour

Colour 0 is in 145 of the 157 transparent textures but only 22 of the
1,998 others, so it's taken for the transparent colour.

## In Reluva

`parseMaterial` (`src/engines/sith/formats/material.h`) reads one into a
`Material`; `materialToImage` makes a texture's mip level RGBA through a
colormap's palette. `reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 1,983 materials compared: every
  file's size is its header, cels, textures and mip levels to the byte;
  the editing community's published write-ups of the format, which
  agree.
- **How it was checked:** every material read by `reluva-tool sith
  formats`; `sith.unit.formats` on a material made by the test.

## Open questions

- The four floats, and the 22 texture cels whose fields at 24 to 35 are
  set.
