# Jedi Knight: particle clouds (`.par`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/sprite.cpp` · **Checked on:** US retail, all 3 files · **Confidence:** confirmed unless marked

## Overview

A cloud of points, each drawn with the same material, all one size:
`sphere.par` and `sphere2.par` (100 points about a sphere, with
`00teleport.mat`) and `test.par`. They are in `Res2.gob`'s `misc\par\`,
written in the models' style ([3do.md](3do.md)).

## Layout

Line by line; a `#` starts a comment to the line's end.

| Section | Lines |
|---|---|
| `HEADER` | `PAR 1.0`; `SIZE <s>`, each point's size (0.002 to 0.01); `MATERIAL <file.mat>`; `LIGHTINGMODE <n>`, 0 in all three |
| `GEOMETRYDEF` | `RADIUS <r>`; `INSERT OFFSET <x> <y> <z>`; `VERTICES <n>` then `<i>: <x> <y> <z> <intensity>` |

## In Reluva

`parseParticles` (`src/engines/sith/formats/sprite.h`) reads one into
`Particles`. The data viewer's Sprites page draws each point as a square
of its size facing the camera. `reluva-tool sith formats <install>`
reads them all.

## Sources and checks

- **Where the facts come from:** the 3 files and their comments; the
  clouds drawn in the data viewer.
- **How it was checked:** every file read by `reluva-tool sith formats`;
  `sith.unit.formats` on a cloud made by the test.

## Open questions

- How the game moves the points (a teleport's sphere growing or
  turning), which the file doesn't say.
