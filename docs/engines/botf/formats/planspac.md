# Star Trek: Birth of the Federation: planets' populations (`planspac.bin`)

> **Status:** decoded · **Code:** `src/engines/botf/formats/planet_populations.cpp` · **Checked on:** the English retail `stbof.res`'s `planspac.bin`, against ten planets on screenshots (November 2026) · **Confidence:** confirmed unless marked

## Overview

`planspac.bin` in `stbof.res` holds a planet's maximum population by its
type, size and bonuses. It's 840 bytes: 21 rows, one for each planet type
and size a population can live on (no gas giants). The game shows the
figure as a planet's "Max Pop" on the galaxy screen
([systems/galaxy-screen.md](../systems/galaxy-screen.md#the-strip)), and a
system's maximum population is its terraformed planets' added up.

## Layout

Each row, 40 bytes:

| Offset | Type | Field |
|---|---|---|
| 0 | `u8` | the planet's type (`systInfo`'s, [sav.md](sav.md)): 0 arctic to 7 volcanic |
| 1 | `u8` | its size: 0 small, 1 medium, 2 large |
| 2 | `u16` | 0 |
| 4 | `i32[9]` | the maximum populations, by the growth bonus's level times 3 and the energy bonus's level |

A bonus's level is 0 for none, 1 for some (310 in the save) and 2 for much
(620) *(published)*. Earth, large and terran with much growth and much
energy, takes the row's last figure, 140; a large terran planet with
neither takes its first, 120.

## In Reluva

`PlanetPopulations` (`src/engines/botf/formats/planet_populations.cpp`)
reads the file; the galaxy screen shows a planet's figure with the
pointer over it. The unit tests (`botf.unit.tables`: "planet populations")
cover the rows, the bonus levels and a missing row.

## Sources and checks

- **Where the facts come from:** the Ultimate Editor's source (GPL-2.0),
  for the row's key and the index from the two bonus levels; the file
  itself in the English `stbof.res`.
- **How it was checked:** the four inhabited planets of Sol and the six of
  Xerxes, on the user's screenshots of the pointer over each (November
  2026): every "Max Pop" is the file's figure for the planet's type, size
  and bonus levels in the save.

## Open questions

- Why the food bonus plays no part, where the growth and energy bonuses
  do.
- The planet's growth rate, which the game shows with its maximum
  population and which no table read gives yet.
