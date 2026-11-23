# Jedi Knight: sprites (`.spr`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/sprite.cpp` · **Checked on:** US retail, all 20 sprites · **Confidence:** confirmed unless marked

## Overview

Things drawn as a flat picture facing the camera: blaster bolts and their
sparks, explosions, splashes, bubbles. They are in `Res2.gob`'s
`misc\spr\`. The picture is a material ([mat.md](mat.md)), whose cels are
an explosion's frames.

## Layout

Plain text: a comment naming the columns, then one line:

| Column | What |
|---|---|
| material | The `.mat` |
| type | 0 in every file |
| width, height | Its size, in the game's units: 0.0015 (`twinkle.spr`) to 0.6 |
| geo, light, tex | Geometry, lighting and texture modes, as a model face's ([3do.md](3do.md)): 4, 0, 0 in all but `twinkle.spr` (geometry 3, a plain colour) |
| extralight | 0.0 in every file |
| xoff, yoff, zoff | An offset from the thing, as their names say *(inferred)* |

## In Reluva

`parseSprite` (`src/engines/sith/formats/sprite.h`) reads one into a
`Sprite`. The data viewer's Sprites page draws each, and steps through
its material's cels. `reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 20 files and their column comment;
  the sprites drawn in the data viewer.
- **How it was checked:** every sprite read by `reluva-tool sith
  formats`; `sith.unit.formats` on a sprite made by the test.

## Open questions

- How the game uses the offset, and how fast it steps through the cels.
