# The captain's menu (OPTIONS.DAT)

> **Status:** decoded · **Code:** `src/engines/stgenerations/game/optionsmenu.h`, `src/engines/stgenerations/game/keymap.h` · **Checked on:** the 2 CD Windows release: `OPTIONS.DAT` read whole (68 files, 45 of them bitmaps); the captain's menu played · **Confidence:** layout confirmed; behaviour from the code unless marked

## Overview

The captain's menu, from `OPTIONS.DAT`.

## Layout

`CAPTAINS.BMP` with buttons down the left (`LOAD`, `SAVE`, `NEW`, `QUIT`,
`SETTINGS`, `CONTROLS.BMP`, 120 x 27, at x 107 from y 32, 30 apart) and the
panels (`OPTSAVE`, `OPTSET`, `OPTNEW`, `OPTQUIT`, `AWAY`/`STARSHIP.BMP`, 320 x 360)
at (227, 29). Button strips have three frames counted from the bottom: dim,
normal, lit. The Load and Save rows are 30 pixels apart from y 33 of the panel,
the automatic save's at 303. Every picture's colours are matched to
`CAPTAINS.BMP`'s palette. Sounds: `X_1000`..`X_1003` (opened, pressed, empty
slot, accept/cancel) and `M_2000`..`M_2002` (cancelled, confirmed, return).
The original keeps its settings in `SGENS.CFG` (its settings file, in the
installed game's folder beside `sgens.exe`: two 256-byte key maps, then
music and effects volume 0..29, difficulty, window size, subtitles,
lookspring).

## Behaviour

### Keys

Each key map (away at cfg+0x138, starship at cfg+0x238) is indexed by a
Windows virtual key; each byte is an action, 0xFF for none. Virtual keys 1, 2
and 4 are the left, right and middle mouse buttons. The maps start all 0xFF,
and if `SGENS.CFG` is missing 0x488977 and 0x488A23 write the defaults. At
start-up, and when the options change, 0x488913 copies the away map over the
game's whole table at 0x545500, so the keys in that built-in table (B, H, A,
Space, `[`, `]` and the rest) do nothing in the original once a map is
loaded. This engine keeps those as extras for keys the map leaves free
([KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)).

| Away action | code | default | effect (0x43E8FB, flags on the game object) |
|---|---|---|---|
| Attack | 0x13 | Insert, left button | +0x254; one attack a press |
| Forward / Backward | 1 / 2 | Up / Down | +0x228 / +0x22C |
| Left / Right | 6 / 7 | Left / Right | +0x230 / +0x234 (turn) |
| Slide Left / Right | 3 / 4 | Num / / Num * | +0x238 / +0x23C |
| Jump | 0xA | Enter | +0x244 |
| Crouch | 0xB | Ctrl | +0x240 |
| Run | 0xC | Shift | +0x210 and +0x218: walking and turning twice as fast (0x43B904, 0x43C934) |
| Look Up / Down | 8 / 9 | PgUp / PgDn | +0x220 / +0x224 |
| Look Center | 0x12 | Home | +0x250 |
| Mouse Look | 0x34 | right button | game.vt120 0x440BB4: +0xE8 while held |

Codes 5 (turn keys slide), 0xD (walk at half speed) and 0x14 (attack, the
built-in table's left button) have no place on the panel. With Mouse Look
held, the view turns towards the mouse (0x43AFA3): outside a 64-pixel band
across the middle of the view, the target heading is atan(offset / distance
from the far side to the band) degrees away, the band starting 32 pixels
left of the middle (16 above it for the pitch), and each frame the view
moves a fifth of the way. The keys and the mouse feed the same turn (`0x43C7D4`):
Left adds the turn rate, and the mouse left of the middle a positive angle, so it
turns left; above the middle it looks up, as Look Up does. On release, with
Lookspring on, the view levels.

The Controls panel (0x483843) has two cells an action, 32 x 26, at x 84 (the
first ten actions) or 252 (the rest) of the panel, the second 34 to the
right, rows 30 apart from y 37 (0x48074F). The panel order comes from the
tables at 0x548B40 (away) and 0x548B50 (starship). A cell shows the first two
keys found for its action, the higher first (0x485CF0), named by 0x48639D
(letters and digits as they are, `num 0`..`num 9`, `F1`..`F12`, `mb 1`..`mb
3`, `shift`, `pgup`, `[` and so on, `?!?` for anything else). Clicking a cell
puts `KEYHIGH.BMP` round it (3 pixels left of and 6 above the cell) and holds
the mouse; the next key or button (0x485AF3) is taken unless it's a function
key, or, for Mouse Look, anything but the left or right button. The cell's old
key is unbound, the new key gets the cell's action, and any other cell
showing the new key is emptied. Accept keeps the maps, Cancel drops them.

## Sources and checks

From STG-Engine's notes on the 2 CD Windows release ([README](README.md) says how they were made). All values are little-endian.
