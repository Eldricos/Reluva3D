# Archives (.CAT, .LST, .ALL)

> **Status:** decoded · **Code:** `src/engines/stharbinger/formats/archive.cpp`

## Overview

Most of the game's files are packed in archives of up to three files: an
`.LST` of offsets, the `.ALL` itself and an optional `.CAT` of names.

## Layout

- `.LST`: `u32` offsets into `.ALL`, normally one more than the number of entries (the last is the file size).
- `.ALL`: the entries back to back, with no headers.
- `.CAT` (optional): text, CRLF lines: the entry count, then one name per entry.

Entry *i* is `ALL[offset[i] .. offset[i+1]]`. Without a `.CAT`, entries are used by
index (dialogue audio, script archives indexed by scene slot).

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).

- **How it was checked:** the unit tests (`stharbinger.unit.formats`, [TESTING.md](../TESTING.md)) cover archives.
