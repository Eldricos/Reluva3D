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
| type | 0 in every file; changes nothing: see [The type](#the-type) |
| width, height | Its size, in the game's units: 0.0015 (`twinkle.spr`) to 0.6 |
| geo, light, tex | Geometry, lighting and texture modes, as a model face's ([3do.md](3do.md)): 4, 0, 0 in all but `twinkle.spr` (geometry 3, a plain colour) |
| extralight | 0.0 in every file |
| xoff, yoff, zoff | An offset from the thing, along the view's axes: [Behaviour](#behaviour) |

A line needs all eleven columns, a type of 0 to 2 and a width and
height above 0, or the sprite isn't made (`0x4f2465`, `0x4f254a`)
*(from the code)*.

## Behaviour

In `JK.EXE`: drawing a sprite, `0x46ccf0`; its cels, `0x4fb790` and
`0x4f0780`.

**The offset** is added after the thing's position is turned into the
view's frame, so it moves the picture along the view's axes, whichever
way the thing faces: xoff to the right, yoff away from the viewer, zoff
up; the width runs along x and the height along z (`0x46cd69`) *(from
the code)*. The files' offsets fit it: 10 sprites, the blasts and hits,
have yoff from -0.03 to -0.1, which brings the picture toward the
viewer, out of the wall it hit *(inferred)*, and four raise it with zoff
(0.024 to 0.22).

**The cels:** a thing of `type=explosion` whose `typeflags` has `0x1`,
drawn with a sprite whose material has more than one cel, steps through
them all once in its lifetime (`timer`), the first cel at its start:
each cel is shown for the lifetime divided by the number of cels, in
whole milliseconds, and the last stays until the thing goes
(`0x4fb7ab`, `0x4f0100`, `0x4f07d8`) *(from the code)*. In every
retail level, each template that draws a sprite with more than one cel
is such an explosion: `+laserhit` (`bryx.spr`, 8 cels in 0.5 seconds, 62 ms each),
`+grenade_exp` (`detx.spr`, 13 in 0.8, 61 ms each). A sprite on any other
thing shows its material's own current cel (`0x46d109`) *(from the
code)*: in retail, those (bubbles, the force destruction bolt, the TIE
bomb) have a single cel.

### The type

The type is kept at byte `0x20` of the sprite (`0x46ca54`), and the
game's sprite code doesn't read it again: not the drawing (`0x46ccf0`),
the explosions' cels (`0x4fb790`), nor the sizing of a thing by what it
draws, which takes a sprite's radius (`0x4c7d19`) *(from the code)*. The
only code that reads byte `0x20` of a sprite is the cog verb
`SetThingModel`, which returns that byte of whatever a thing draws as
the number of its old model, without checking that it is a model
(`0x503950`); a model keeps its number there (`0x4e9918`). On a thing
drawn with a sprite it so returns the sprite's type, 0 in retail
*(from the code)*. What the type was meant to change can't be told from
the game, and changes nothing in it.

## In Reluva

`parseSprite` (`src/engines/sith/formats/sprite.h`) reads one into a
`Sprite`. The data viewer's Sprites page draws each, and steps through
its material's cels. `reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 20 files and their column comment;
  the sprites drawn in the data viewer; `JK.EXE`'s sprite reader
  (`0x4f2458`), which makes the sprite (`0x46ca20`), its sprite drawing
  (`0x46ccf0`) and its explosions' cel stepping (`0x4fb790`,
  `0x4f00a0`, `0x4f0780`), read from the machine code.
- **How it was checked:** every sprite read by `reluva-tool sith
  formats`; `sith.unit.formats` on a sprite made by the test; every
  level's templates that name a sprite listed by a script with their
  type, `typeflags` and `timer`, and each sprite's material's cels
  counted. Readers of the type were searched for in the sprite code
  (`0x46ca20` to `0x46d200`, `0x4f23b0`) and at every load of the
  object a thing draws (a thing's byte `0xc8`) followed by a read of
  its byte `0x20`: two, `SetThingModel`'s and a polyline's radius
  (`0x4c7d42`).

## Open questions

None.
