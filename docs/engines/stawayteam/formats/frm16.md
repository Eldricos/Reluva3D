# Star Trek: Away Team: pictures (`.frm16`)

> **Status:** decoded · **Code:** `src/engines/stawayteam/formats/frame.cpp` · **Checked on:** the US English retail `DATA.dat`: all 671 frames, every row decoding to the frame's width (November 2026) · **Confidence:** header from the code; the blocks' coding confirmed unless marked

## Overview

The game's pictures, 16 bits a pixel: the interface's screens and
buttons, portraits and icons, the maps' textures, the minimaps. In
`DATA.dat`, 672 files under `Cache/` end `.frm16`; 671 are frames, the
other ([below](#a-file-that-isnt-one)) isn't. The same frame format is
inside the sequences (`.seq16`) and models (`.mdl16`), a frame each.

| Kind | Flags | Files |
|---|---|---|
| Raw pixels: the textures (`Cache/Textures/`), 128x128 | `0x20` | 171 |
| Run-length coded, every pixel opaque or not drawn | `0x2024` | 203 |
| Run-length coded, with blended pixels | `0x2024` | 297 |

## Layout

A header, then up to three blocks, each followed by a table of where
its rows start.

### The header

In `Star Trek.exe`: `0x4b3a30`, reading 28 bytes.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `0x2e`; the game refuses a file without it |
| 1 | 1 | Bits a pixel: 16 |
| 2 | 2 | Origin x, signed: the point in the frame that goes where it's drawn (the game keeps it negated, `0x4b3a77`) *(inferred)* |
| 4 | 2 | Origin y, signed |
| 6 | 2 | Width |
| 8 | 2 | Height |
| 10 | 2 | Not read by the loader: the same within a folder (2,257 for every `War Room` texture), not the number of colours |
| 12 | 4 | Flags: `0x20` raw, `0x2024` run-length coded; their bits' meanings aren't known |
| 16 | 4 | Block 0's size: the pixels drawn whole |
| 20 | 4 | Block 1's size: the blended pixels' alpha, or 0 |
| 24 | 4 | Block 2's size: the blended pixels, or 0 |

The game reads no block over 4 MB (`0x4b3ae3`).

### The blocks

After the header, each block that isn't empty, in order: its bytes,
then `height` 32-bit numbers, where each row starts in it. A row ends
where the next starts; the last at the block's end.

- **Raw** (block 0 of the textures): the first row starts at 0; the
  block is `width` x `height` pixels, 2 bytes each.
- **Run-length coded:** the block's first 4 bytes are its own size, so
  the first row starts at 4.

Each pixel is RGB 5:6:5, little-endian: red in bits 11-15, green in 5-10,
blue in 0-4. Bit 15 is set in 3.5% of the pixels, which RGB 5:5:5 never
sets, and the pictures look right so.

### Run-length coding

Blocks 0 and 2, a row at a time, a byte saying what follows:

| Byte | Meaning |
|---|---|
| `1nnnnnnn` | Skip `n` pixels: not drawn by this block |
| `01nnnnnn` | `n` pixels follow, 2 bytes each |
| `00nnnnnn` | One pixel follows, 2 bytes, for `n` pixels |

Every row of the 671 frames comes to its width exactly. A count of 0
happens: 14 literal and 4 skip codes with nothing in them.

### Blended pixels

Block 0 has the pixels drawn whole; block 2, those blended with what's
behind them (anti-aliased edges, shadows, see-through panels). They
never overlap: 898,430 pixels are block 2's, none of them also block
0's. Block 1 has a byte for each colour of block 2, in the same order,
row by row: one for each pixel that follows a `01` code, one for the
pixels of a `00` code together. Its rows' starts are in its own table.
The byte is the pixel's alpha, 255 opaque *(inferred: the pictures look
right so)*. All 297 frames with block 2 have block 1, and the other way
round.

### A file that isn't one

`Levels/Mission 16/16 Borg Rescue.frm16` starts `FRAME` (`46 52 41 4d
45 00`), which the game's loader refuses. Nothing names it: the mission's
map uses `Cache/Levels/Mission 16/16 Borg Rescue map.frm16`.

## Behaviour

The game picks a frame's file by the screen's depth (`0x4b3ef7`):
`.frm` for 8 bits, `.frm16`, `.frm24` or `.frm32`. Only `.frm16` files
are in `DATA.dat`.

## In Reluva

- **Reading:** `readFrame` into a `Frame`: its size, origin and flags,
  and every pixel's colour and alpha, the blocks merged; `frameImage`
  makes it an RGBA picture.
- **The data viewer:** its Pictures page shows every frame
  ([README.md](../README.md#using-it) has the keys).
- **Commands:** `reluva-tool stawayteam frame-check` decodes every
  frame in `DATA.dat`; `reluva-tool stawayteam frame-png` writes them
  out as PNG files.

## Sources and checks

- **Where the facts come from:** the loader in `Star Trek.exe`, found
  through its `.frm16` string and disassembled with `objdump`, for the
  header; the 671 frames in the retail `DATA.dat`, for the blocks' coding:
  their sizes added up to each file's, the row tables' starts, and the
  codes worked out from rows by hand, then tried on every row.
- **How it was checked:** `reluva-tool stawayteam frame-check` decodes
  all 671, each to its file's end; pictures written out with `frame-png`
  and looked at (portraits, minimaps, textures, buttons), matching a
  first decoder written apart from Reluva's pixel for pixel; unit tests
  (`stawayteam.unit.formats`) on frames made in the test: raw, each kind
  of code, blended pixels with their alpha, and damaged ones.

## Open questions

- **The header's bytes 10-11:** the loader doesn't read them.
- **The flags' bits:** `0x20` and `0x2024` are the only values seen, and
  the blocks themselves say how they're coded.
