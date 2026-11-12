# Deck maps (MAPS/*.DAT)

> **Status:** decoded · **Code:** `src/engines/stharbinger/game/game_extras.cpp` · **Confidence:** layout confirmed; behaviour from the code unless marked

## Overview

The deck maps for the map screen (`DS9/MAPS/*.DAT`).

## Layout

Raw 8-bit images with no header: `OPS` 301×309, `UPPER` 469×930, `LOWER` 393×767,
`HABITAT` 1101×883. They use colour slots shared by a family of palettes (`OPS01` for
Ops, `CCRUN05` and similar for the others).

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).
