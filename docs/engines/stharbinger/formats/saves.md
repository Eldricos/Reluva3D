# Saved games (SAVEGAME\*.DS9)

> **Status:** decoded · **Code:** `src/engines/stharbinger/game/game_save.cpp`

## Overview

The saved games, and the settings file `DLAS.DS9`.

## Layout

DEEP.EXE's format (Save_Game 0x1EABB), little-endian, no header. Manual saves are
`NNNNNNNN.DS9`, autosaves `<NAME>SAVE.DS9`. Slots follow the linker files
(`DS9.LCC`, `DS9.LVM`, `DS9.LDM`) and the scene's lists; script positions are
directive indexes in the scene's script.

| Size | Contents |
|---|---|
| u16 | disc (CDCHANGE) |
| u32 ×4 | tricorder icon; deck-map location (FF none); countdown period (FFFF off) and phase |
| cstr ×2 | game name (`ds9`); description |
| 307200 | the screen (640×480) |
| 768 | palette (6-bit) |
| u32 | background whose palette is set (299: none) |
| u16 | scene (DS9.LCC index) |
| u32 + n × u32 | GOSUB stack: depth, then return points |
| 300 × var | dialogues by DS9.LDM slot: FF, or 00, name, u16 group, mode, RUDE, rude, BACK, EXIT, DONE[50], EMPTY goto[50], EMPTY reply[50], NPC and choice groups (FF, or n × (on, said)), DONEX / EMPTYX goto / EMPTYX reply (FF, or target, group, item), current NPC item per group (group or FF, index), indexes[50], hooks (n × (state, group)) |
| 3000 × u16 | global variables (DS9.LVM order) |
| u16 | current background |
| 300 × var | animations: FF, or 1 (loaded on first use) / 0, the 0x34-byte record (x, y, rate, countdown, flags, priority …), u32 frame, u32 frame drawn (FFFF none), u32 ANIMEND and ANIMARRV directives, u8 n and n × 8 ANIMFRAM bytes |
| 300 × var | hotspots: FF, or 00, u16 x, y, w, h, u8 on, u16 label, u8 cursor |
| u32 | where the script continues (the RETURN it idles on) |
| 2 × u32 | ambient sounds (FFFF none) |
| 300 × var | timers: FF, or 00, u16 reload, count, label, u8 on, u8 kind |

`DLAS.DS9` holds the settings: u32 difficulty (0..2), u32 autosave, u32 the save
the menu showed last.

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).
