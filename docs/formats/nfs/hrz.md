# Need for Speed III: `.hrz` sky files

Status: **implemented** (`src/engines/nfs/hrz.cpp`), and checked on all
36 retail files: four per track, `3trNN.hrz` (day), `n` (night), `w`
(weather) and `nw` (night with weather).

The files are plain text: comma-separated numbers, with `/* comments */`
naming each value. Some comments sit on the same line as numbers
(`223,210,151, /* 255, 176, 61, at position of sun */`). Reluva reads
the numbers in order and names them after the comments.

Every retail file has, in order:

- 5 numbers: the fog colour, the fog density, and the fog's effect on
  the horizon picture;
- the number of dynamic fog regions (0 to 15 in the retail files);
- 15 numbers per region:
  - start, apex and end, each r, g, b and density;
  - the slices (track blocks) before, at the centre and after.
- 48 numbers, as their comments name them:
  - **Clouds:** cloud type (1 additive, 2 blended); fog clouds;
    lightning and its chance; visual height; cloud dome offset (2); cloud
    brightness, variance and nimbus; wind speed and direction.
  - **Horizon:** black horizon; mirror flag; horizon radius; rotation;
    has a picture.
  - **Shaded area:** the Gouraud-shaded area's base (world height), its
    height, its midpoint; the horizon picture's top and bottom (offsets
    from the base).
  - **Colours:** the earth's colours at the top and base; the sky's at
    the top, at the base towards the sun, and at the base away from it.
  - **Weather:** type, start and end slices, fade, then the stay-on,
    fade-off, stay-off and fade-on times in ticks.
  - **Ambient light:** r, g, b, in percent.

The check is that every file has 6 + 15 × regions + 48 numbers.

## How Reluva draws it

The Tracks page uses the colours and heights:

- **The dome.** The earth's colours from the base up to the midpoint
  (the horizon), then the sky's from the midpoint to the top. The sky's
  base colour turns from the sun's side to the far side.
- **The sun's direction.** It comes from the `A` value of the sun
  picture's text in the track's `sky.fsh` ([fsh.md](fsh.md)).
- **The horizon picture.** `CHRD`, `CHRN`, `CHRW` or `CHNW` from
  `sky.fsh`, by variant, in a band at the horizon radius, between its
  bottom and top heights. With the mirror flag, every other copy is
  flipped.
- **Ambient light.** The percentages scale the track's baked vertex
  lighting.

Not yet:

- **Fog and clouds.** The fog regions, the clouds (`CL??` pictures) and
  lightning aren't drawn.
- **Picture repeats.** The number of times the horizon picture repeats
  round the circle (Reluva uses four) is a guess, to check against the
  game.
