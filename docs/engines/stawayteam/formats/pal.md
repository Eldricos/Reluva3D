# Star Trek: Away Team: palettes and colour tables (`.pal`)

> **Status:** partial · **Code:** none yet · **Checked on:** the US English retail `DATA.dat`'s 6 `.pal` files (November 2026) · **Confidence:** inferred from their sizes and contents unless marked

## Overview

`DATA.dat`'s `Palettes/` holds a 256-colour palette and five tables for
drawing with it. The game's pictures are 16-bit
([frm16.md](frm16.md)), so these serve an 8-bit way of drawing: the
frame loader takes `.frm` files for an 8-bit screen
([frm16.md](frm16.md#behaviour)), though there are none in `DATA.dat`.
The game names the tables in its code (`Alpha20_80.pal` at `0x44ba3a`,
`FadeTable.pal` at `0x44ba91`).

## Layout

| File | Size | What it holds |
|---|---|---|
| `GamePalette.pal` | 768 | 256 colours, 3 bytes each (red, green, blue, 0-255): its first 16 run from black to white; `GamePalette.bmp` beside it shows it |
| `Alpha20_80.pal`, `Alpha40_60.pal` | 65,536 | For two colours of the palette, the one nearest their blend, 20:80 and 40:60: 256 x 256 indices |
| `BlackBias.pal` | 65,536 | 256 x 256 indices |
| `FadeTable.pal` | 8,192 | Each colour faded: 32 levels x 256 indices |
| `InvColorTable.pal` | 32,768 | For each 15-bit colour, the palette's nearest: 2^15 indices |

## In Reluva

Not read yet.

## Sources and checks

- **Where the facts come from:** the six files' sizes and first bytes,
  and their names' strings in `Star Trek.exe`.
- **How it was checked:** not yet against the game's use of them.

## Open questions

- **Whether the game ever draws in 8 bits**, and what `BlackBias.pal`
  does.
