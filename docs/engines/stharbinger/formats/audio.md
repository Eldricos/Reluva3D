# Sound (.RAW and dialogue clips)

> **Status:** decoded · **Code:** `src/engines/stharbinger/formats/audio.cpp`

## Overview

Sound effects and music segments (`DS9/SOUND/NAME.RAW`) and the speech
clips (`DS9/DIALOGUE/NAME/NAME.ALL`).

## Layout

Headerless PCM, mono, 22050 Hz. Two formats are used:

- **16-bit signed** (most sound effects). Some files are 8-bit sound stored in the high byte (every low byte is 0).
- **8-bit unsigned** (speech and music).

Nothing in the file says which. Detection: if every even byte is 0, or the odd bytes
are much more predictable than the even bytes (byte entropy differs by more than 1
bit), the file is 16-bit; otherwise it is 8-bit unsigned.

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).

- **How it was checked:** the unit tests (`stharbinger.unit.formats`, [TESTING.md](../TESTING.md)) cover raw sound detection.
