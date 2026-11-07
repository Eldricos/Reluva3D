# Mouse cursors (art\mouse.ani)

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).

## Mouse cursors (`art\mouse.ani`)

In the installed `ART` folder (on disc 1 inside `ART/ART.ALL`). `u16 count` (26),
then for each cursor a 12-byte header and `width * height` raw 8-bit pixels (top
row first):

| Offset | Contents |
|---|---|
| 0 | unused |
| 1 | transparent colour index |
| 2 | u16 height |
| 4 | u16 width |
| 6 | u16 hotspot y |
| 8 | u16 hotspot x |
| 10 | u16 unused |

The cursor's top-left corner is at the mouse position. Scene hotspots are tested
at the cursor's centre, the conversation menu at its hotspot. The cursor id is
the hotspot's cursor byte (0 over nothing, 15 in the reply menu and while lines
are spoken). Palette entries 1..5 rotate every 300 ms in scenes.
