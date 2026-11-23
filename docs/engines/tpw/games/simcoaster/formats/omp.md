# SimCoaster: placed objects (`scape.omp`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/omp.cpp` · **Checked on:** the US release's four `scape.omp` files · **Confidence:** confirmed unless marked

## Overview

Each world's folder has a `scape.omp` (`Data/levels/arabian/scape.omp`):
a short list of points in the world, each with a kind, a number and a
radius, none of them drawn. They seem to be where the world's
surroundings make sounds and particles *(inferred)*. Their kind-1
points run along each world's river (the Arabian Nights' canyon, the
Land of Invention's stream, the Polar Zone's river). Kind 2 is only in
the Polar Zone, where its frozen and open rivers meet. Kind 3 sits at
the entrance, by the road and the ponds. The file ends with the names
of the files the numbers were defined in: a particle library's and the
sound events' (`par_lib.h`, `SfxEvent.h`, `ArabianEvent.h`).

The Arabian Nights has 13 objects, the Land of Invention 10, the Polar
Zone 11, the Intro World none.

## Layout

All numbers little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `OBJ_` |
| 4 | 4 | How many objects |
| 8 | 4 | An object's size: 60 |
| 12 | | The objects |

Then an `INCL` chunk: `INCL`, how many names (3 in all four), and for
each its length with its ending zero (4 bytes) and the name. The files
end there.

### An object

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Its kind: 1, 2 or 3 |
| 4 | 4 | 0 or 1 for kinds 1 and 3 (1 for two of the Arabian Nights' kind-1 objects, away from its river); 20 for kind 2. Not worked out |
| 8 | 4 | A number: 3, 7 or 8 for most, 139, 162 and 310 for one each; 0 for kind 2. A sound event's, perhaps *(guessed)* |
| 12 | 12 | Where it is: x, y, z, signed, in thousandths of a unit, in the space of the world's models and ground ([lnd.md](lnd.md#the-cells)) |
| 24 | 4 | Its radius, in thousandths: 35, 50 or 80 units; 0 for kind 2 and for the Polar Zone's kind-3 object on its river |
| 28 | 8 | Two more numbers in thousandths: for six of the eight kind-3 objects 100 and −100 or 60 and −50 (220 and −300, 100 and −66 for the others); 0 for kind 2 and for all of kind 1 but one (100 and −100). Not worked out |
| 36 | 24 | 0 in all |

The positions are confirmed by where they fall: drawn over the world's
terrain, the kind-1 objects follow its river within a few units.

## In Reluva

`ObjectMap` (`src/engines/tpw/formats/omp.cpp`) reads the objects and
the names. The viewer's Worlds page draws each object as a dot over the
terrain and its radius as a ring (O shows or hides them): kind 1 cyan,
2 magenta, 3 yellow. `reluva-tool tpw terrain-check` reads every
world's objects and lists them; the game-files test runs it
(`tpw.gamedata.terrain`).

## Sources and checks

- **Where the facts come from:** the US release's four files (November
  2026): the records walked to the `INCL` chunk and it to each file's
  end; the positions drawn over the terrain.
- **How it was checked:** all four read with `reluva-tool tpw
  terrain-check`; drawn with `world-shot`. The unit tests
  (`tpw.unit.formats`) read objects built in the test, and fail cut
  ones.

## Open questions

- What the kinds and numbers are: which sounds and particles, if they
  are. The sound banks' `.map` files ([sdt.md](sdt.md)) and the particle
  library (`Data/Particle/Tp2.plb`) may name them.
- The second word and the two numbers after the radius.
