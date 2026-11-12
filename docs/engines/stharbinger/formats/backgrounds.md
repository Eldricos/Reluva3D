# Backgrounds (.CMP) and palettes (.PAL)

> **Status:** decoded · **Code:** `src/engines/stharbinger/formats/graphics.cpp` · **Checked on:** both CDs' `.CMP` backgrounds, converted and looked at (STDS9H's extractor, October 2026; the number of files was not recorded) · **Confidence:** confirmed unless marked

## Overview

Backgrounds (`DS9/ART/X/Y/NAME.CMP`) are compressed 640×480 8-bit pictures;
their palettes are separate files (`DS9/PALETTES/NAME.PAL`).

## Layout

### Backgrounds (`.CMP`)

`u32` length of the rest of the file, then a PKWARE Data Compression Library
("implode") stream: byte 0 is `0` (binary literals) and byte 1 is `6` (4 KB
dictionary). It decompresses to 640×480 8-bit pixels. The palette is separate.

### Palettes (`.PAL`)

768 bytes: 256 × RGB, 6-bit VGA values (0–63). When a background loads, the game
loads `PALETTES/<background>.PAL` if it exists and otherwise keeps the current palette.

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).
