# The discs

> **Status:** decoded · **Code:** `src/engines/stharbinger/detection.cpp`, `src/engines/stharbinger/formats/resources.cpp` · **Confidence:** confirmed unless marked

## Overview

Where the game's files are on its two CDs.

## Layout

| Path | Contents |
|---|---|
| `SSE/SSE.ALL` (disc 1) | installed program files: `DEEP.EXE`, drivers, and the script/name-list archives (`CC`, `CA`…`CV`, `CCB`, `CCQ`…) |
| `DS9/ART/X/Y/NAME.CMP` | backgrounds, stored by the first two letters of the name |
| `DS9/ANIMS/XXXX/NAME.RLE` | animations, stored by the first four letters |
| `DS9/SPRITES/…`, `DS9/PANEL/`, `DS9/EXPLODE/`, `DS9/TORPEDOS/` | action-sequence graphics (`.RLE`) |
| `DS9/PALETTES/NAME.PAL` | palettes |
| `DS9/SOUND/NAME.RAW` | sound effects and music segments; `.MUS` playlists, `.VCE` voice cues |
| `DS9/DIALOGUE/NAME/NAME.ALL` + `.LST` | speech clips |
| `DS9/PHONEMES/NAME/NAME.ALL` + `.LST` | lip-sync data plus the words of each clip |
| `DS9/MOVIES`, `DS9/TRANS`, `DS9/INTRO` | Smacker videos (`.SMK`) |
| `DS9/MAPS/*.DAT` | deck maps for the map screen |
| `DS9/TORPEDOS/CD` | disc number as text (`0`, `1`) |

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).

- **How it was checked:** the unit tests (`stharbinger.unit.formats`, [TESTING.md](../TESTING.md)) cover a disc's folders.
