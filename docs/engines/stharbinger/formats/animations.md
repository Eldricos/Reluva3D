# Star Trek: Deep Space Nine - Harbinger: animations and sprites (`.RLE`)

> **Status:** decoded · **Code:** `src/engines/stharbinger/formats/graphics.cpp` · **Checked on:** both CDs' 1,458 `.RLE` files (with those inside archives), 28,650 frames: every frame's rows fit its header (STDS9H's extractor, October 2026) · **Confidence:** confirmed unless marked

## Overview

Animations (`DS9/ANIMS/XXXX/NAME.RLE`) and the action sequences' sprites:
frames of run-length coded 8-bit pixels.

## Layout

```
file:   8 bytes  00 00 01 01 0A FF 00 00
        u16      frame count
frame:  u8       key / flags (purpose not confirmed)
        u16      width, height, x, y
        12 bytes unknown
        u32      length of the row data
        row data
```

The row data is a byte stream. The top two bits of each byte select the operation
and the low six bits are a count *n*:

| Bits | Operation |
|---|---|
| `00` | copy the next *n* bytes as pixels |
| `01` | start a new row, then skip *n* transparent pixels |
| `10` | repeat the next byte *n* times |
| `11` | skip *n* transparent pixels |

The last row may be followed by one more "new row" byte.

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).

- **How it was checked:** the unit tests (`stharbinger.unit.formats`, [TESTING.md](../TESTING.md)) cover RLE frames.
