# The executable (DEEP.EXE)

> **Code:** `src/engines/stharbinger/tools/exe.cpp` · **Confidence:** from the code unless marked

## Overview

`DEEP.EXE`, the game's program, installed from disc 1's `SSE/SSE.ALL`.

## Layout

The DOS/4GW extender followed by the game: a Watcom LE executable whose `MZ` stub
starts at file offset `0x352A4` (LE header at `0x37CF4`). Useful addresses:

| Address | What |
|---|---|
| `0x11000` | jump table of the scene interpreter (commands `0x64`..`0xAA`) |
| `0x149B0` | jump table of the directive-list (dialogue) interpreter (commands `0x32`..`0xAA`) |
| `0x1DD32` | `Load_Directive_List` |
| `0x1DC25` | dialogue record loader |
| `0x1B0DD` | name-list loader |
| `0x55140` | argument count per command |
| `0x55294` | scene name table |
| `0x5BE40` `0x5CC50` `0x5C7A0` | animation, background and sound name tables |

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).
