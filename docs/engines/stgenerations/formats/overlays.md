# Overlays (.OVR)

> **Status:** decoded · **Code:** `src/engines/stgenerations/formats/overlay.h` · **Checked on:** the 2 CD Windows release, two samples

## Overview

These are first-person animations, such as the player's fist (`KIRKHIT.OVR`) or a hand holding an
item (`NUPLANT.OVR`).

## Layout

```
char[8]  "OVERLAY\0"
uint32   width
uint32   row count (all frames stacked vertically)
uint32   row offsets[row count], relative to the end of this table
rows: int16 runs until the row reaches `width`
  n < 0: skip -n transparent pixels
  n > 0: n palette indices follow, padded to an even byte count
```

Every row of both samples decodes to exactly the width. The **frame height
is not stored**: whoever loads an overlay sets it (`0x4029A5`), and without that
it's one frame of every row (`0x402740`). The held items (nu*.ovr) are cut into
88-row frames (`0x430E06`), and the punches of SET_PUNCH into frames as tall as
the 3D view, 252 rows (`0x431D25`, view word +0x7A), so KIRKHIT's 1260 rows are 5
frames and the 1008-row ones 4. The view overlays of SHOW_VIEW_OVERLAY are 640 x
252, one frame. There's no
palette either; the overlay uses the palette of the current scene. The frames
are stacked top down, but each frame's rows are stored bottom up: only that
way up do NUPLANT's nozzle tips sit on the hotspots sgens.exe gives its poses,
and Kirk's fists in KIRKHIT swing up from the bottom of the view.

## Sources and checks

From STG-Engine's notes on the 2 CD Windows release ([README](README.md) says how they were made). All values are little-endian.
