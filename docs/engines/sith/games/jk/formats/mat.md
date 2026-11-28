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
| 8 | 4 | Type: 0 colours only, 2 with textures. Bit 0, set in no file, would put a palette of 768 bytes after the textures (`0x44a5de`) *(from the code)* |
| 12 | 4 | How many cels |
| 16 | 4 | How many textures: 0 for type 0, else as many as the cels |
| 20 | 4 | The colour format's kind: 0, palette indexes, in every file. The game copies bytes 20 to 75 into the material as they are (`0x44a332`) and gives the textures' pictures the kind and the bits a pixel (`0x44a4a6`) *(from the code)* |
| 24 | 4 | Bits a pixel: 8 in every file |
| 28 | 48 | The rest of the colour format, each colour's bits and shifts as in a 16-bit picture's ([bm.md](bm.md#header)), unused for kind 0: two formats of kind 0 are taken as the same whatever the rest holds (`0x4238ab`) *(from the code)*. Differs between files and makes no pattern: left as it was in memory *(inferred)* |

### Cels

For each cel, 24 bytes for a colour, 40 for a texture:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Kind: 0 a colour, 8 a texture |
| 4 | 4 | The colour, a palette index |
| 8 | 16 | Four floats, 1 in every file. The game keeps them with the cel (`0x44a383`), and only the format's writer reads them (`0x44a8e7`), which nothing in `JK.EXE` calls: everything else that takes a cel reads only its kind, its colour and its texture *(from the code)*: see [Behaviour](#behaviour). What it meant to the tool that wrote the files can't be told from the game, and changes nothing in it |
| 24 | 12 | Texture cels only: read and skipped (`0x44a3a3`) *(from the code)*. In 2,133 cels 0, then 0 and `0xbff78482` (a value the header's 48 bytes hold too) one way round or the other; in 22, 3, `0x302` and `0x81`. Left as they were in memory: the game's own writer, never called, fills only the texture's number of these 16 bytes (`0x44a946`) *(from the code)* |
| 36 | 4 | Texture cels only: which texture (`0x44a3bf`); in every file, the cel's own number |

### Textures

For each texture, 24 bytes, then its mip levels:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Width |
| 4 | 4 | Height |
| 8 | 4 | Flags: bit 0, the texture has a transparent colour, the one at 16 (`0x44a4fe`) *(from the code)*. 1 or 0 |
| 12 | 4 | 0 in every file |
| 16 | 4 | The transparent colour, a palette index: 0 in every file. The game makes it each mip level's colour key (`0x44a508`, `0x423830`) *(from the code)* |
| 20 | 4 | How many mip levels: 1, 3 or 4 |

Each mip level is half the width and height of the one before, its
pixels a byte each, row by row.

## Behaviour

A texture with bit 0 of its flags set has its colour at 16, colour 0
in every file, left out where it's drawn: the game sets it as the colour
key of each mip level's picture (`0x44a4fe` to `0x44a508`) *(from the
code)*. The files agree: colour 0 is in 145 of the 157 transparent
textures but only 22 of the 1,998 others.

Drawn through Direct3D, a transparent texture is loaded in a 16-bit
format with a bit of alpha (5-5-5-1), clear where its colour is 0, and
the rest in 5-6-5 without; the game draws with the alpha test on, so
the clear texels are left out *(confirmed)*. Seen running, every
texture the game loaded logged as it was drawn: the 71 loaded at Nar
Shaddaa's start (none of its 8 transparent ones in view) all 5-6-5;
through all 21 of the light side's levels, 37 loaded with alpha, each
one's clear texels as many as the colour-0 texels of a transparent
texture of its size, and in 36 of them of no other texture's (the 37th,
128x128, has none clear, like some textures of either kind).

A cel is kept as its kind, colour, four floats, texture number, and a
pointer to its texture (`0x44a376` to `0x44a3bf`, `0x44a5c9`). Every
place that picks a cel out of a material, in the Direct3D (`0x43b4a6`)
and software renderers (`0x43c43c`, `0x43cf4c`, `0x44c8a3`), the level's
code (`0x4c6907`, `0x4c6e1a`, `0x4e5f0d`, `0x4ec0b9`) and the sprites
(`0x46cb4c`), reads only the kind, the colour or the texture, and so
does each drawing routine the renderers hand a cel to (`0x44e150` to
`0x45b6d0`, `0x45cb80` to `0x46b4e0`, `0x4765d0` to `0x47d370`) *(from
the code)*.

## In Reluva

`parseMaterial` (`src/engines/sith/formats/material.h`) reads one into a
`Material`, each texture's transparent colour from its flag and field
16; `materialToImage` makes a texture's mip level RGBA through a
colormap's palette. The data viewer's Materials page shows every cel,
through a chosen colormap and light level. `reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 1,983 materials compared: every
  file's size is its header, cels, textures and mip levels to the byte;
  the editing community's published write-ups of the format, which
  agree; the textures drawn in the data viewer; `JK.EXE`'s reader
  (`0x44a260`), which checks for `MAT ` and version 50, and writer
  (`0x44a830`, which nothing calls) of the format.
- **How it was checked:** every material read by `reluva-tool sith
  formats`; `sith.unit.formats` on a material made by the test. Readers
  of the cels' floats were searched for in the whole program: every
  load of a cel from a material's array of them, the loops over it, and
  the fields each drawing routine reads of the cel it's given (traced
  from the routine's arguments); no call to or pointer at the writer
  was found.

## Open questions

None.
