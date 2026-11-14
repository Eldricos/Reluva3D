# And Then There Were None: character lighting

> **Status:** done · **Code:** `src/engines/awe/render/lighting.cpp` (shared), `src/engines/awe/games.h` (`LightingRules`), `src/engines/awe/attwn/attwn.cpp` (this game's) · **Checked against:** the code (`ac.exe`) and the install's 328 `LITE` records · **Confidence:** from the code

## Overview

The characters are lit as Orient Express's
([../../motoe/systems/lighting.md](../../motoe/systems/lighting.md)):
Direct3D 8's fixed-function lights from the scene's `LITE` records, the
camera's ambient colour, each character's material. This page has what
differs: the light's fields and defaults, the camera's default ambient,
and which lights go off at night.

## How it works

### The same code

Compared with `motoe.exe`'s with the objects' field offsets set aside
(their layouts differ): the light's switches (`0x41d700`, `0x41d730`), the
re-apply after a change of time (`0x41d8a0`) and the flicker (`0x41dc30`)
are the same code; so is the night test (`0x402b30`: `NVNT`, time 2, time
1 outdoors), and the night ambient colours (`0x401150`, `0x4011b0`). The
character draw's setup (`0x420f80`) sets the material, the ambient (by
day the camera's, at night the blue, white with `spotlight.tga`),
lighting, specular and normalising as Orient Express's does.

### The light's record

28 fields (the reader, `0x41e320`), Orient Express's 29 less the last:
there's no "shadow only". The 25th, unnamed in Orient Express (`b6c`), is
at `+0x658` here, and the scene loader gives it a meaning: a light with
it set, in an act's version, changes the base's light whose index is its
26th field instead of being added (`0x42c9a4`, the light found by
`0x42d2e0`, copied by vtable +0x38). No light in the install has it set.

Its defaults (the constructor, `0x41d5a0`), for fields a record doesn't
give: diffuse 1 (Orient Express 0.3125), attenuation 0, 0.0001, 0 (0.1,
0.001, 0). The install's records give them all.

### The camera's ambient

Its constructor's is 0.5 grey (`0x408510`), Orient Express's 0.3125: for a
scene without a camera.

### Night

Orient Express switches every light off at night. Here a light goes off
(`0x41d760`) only if:

- it's the base scene's, or a scene's without a base (its `+0x654`,
  set by the loader for those and cleared for an act's version's own
  objects: `0x42c996`, `0x42cc67`),
- its 25th field isn't set,
- the scene isn't outdoors (`OUTD`),
- and no `spotlight.tga` is shown (as Orient Express).

So an act's own lights stay on at night, and outdoors every light does
(the night's blue ambient still applies there). Five of the install's
lights are an act's own.

### The install's lights

328: 198 directional, 129 point, 1 spot; one with a condition; none off,
none with the 25th field set.

## In Reluva

`LightingRules` (`games.h`): the camera's and the light's defaults, and
whether night spares the lights above; Orient Express's are the
defaults, this game's in `attwn/attwn.cpp`. The scene's merge marks
where an act's version's own records start (`Ac2Scene::actFrom`), which
the night rule asks.

Not followed: the 25th field's change of a base light, which no light
uses.

## Sources and checks

- **Where the facts come from:** `ac.exe`'s light class (its vtable at
  `0x47481c`), found by its Direct3D calls (`SetLight` +0xb0,
  `LightEnable` +0xb8), and compared with `motoe.exe`'s; the scene loader
  (`0x42c680`) and its base loader (`0x42ca40`).
- **How it's checked:** `awe.unit.attwn` lights a base scene and an act's
  version at night, indoors and out, and a scene without a camera. The
  lights were counted with `awe ac2-dump` over the 537 scenes.
