# The captain's menu (OPTIONS.DAT)

> **Code:** `src/engines/stgenerations/game/optionsmenu.h` · **Confidence:** layout confirmed; behaviour from the code unless marked

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
The original keeps its settings in `SGENS.CFG` (two 256-byte key maps, then
music and effects volume 0..29, difficulty, window size, subtitles).

## Sources and checks

From STG-Engine's notes on the 2 CD Windows release ([README](README.md) says how they were made). All values are little-endian.
