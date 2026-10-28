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
- **The sun or moon.** The variant's picture (`SUND`, `SUNN`, `SUNW`, and
  `SNNW` for night with weather where a track has one, else `SUNW`),
  facing the camera inside the dome. Its text gives `A` (degrees round,
  as for the dome's sunlit side), `B` (degrees above the horizon) and
  `R`, taken as its width in degrees. It's added to the sky, unless the
  text says `NOADD`; with `INFRONT` it's drawn over the horizon picture,
  else behind it.
- **Clouds.** With a cloud type other than 0, the variant's tile (`CLDD`,
  `CLDN`, `CLWD`, `CLWN`) over a layer at the visual height, a tile every
  300 units, fading out towards the horizon. Type 1 is added, type 2
  blended. The colour is the cloud brightness (0-255) times the ambient
  light, but at least a third of it. The clouds drift with the wind: its
  speed in hundredths of a tile a second, towards its direction.
- **Fog.** After the scene, its solid geometry is drawn again in the fog
  colour, over what was drawn, more of it the further away: the density
  is taken as how much fog there is at the horizon's radius, growing
  linearly from the camera. Within a dynamic fog region (by the block the
  camera is in), the colour and density go from the start's to the
  apex's between the before and centre blocks, then to the end's by the
  after block. The horizon picture is blended towards the fog colour by
  its effect on the picture times the density. The clear colour is the
  fog colour. `Y` turns fog off and on.
- **Lightning.** With lightning on, each second has a chance (out of
  256) of a flash a fifth of a second long, which brightens everything.

## What the game does with it

With `softtria.dll`, the only renderer Reluva runs, the game draws none
of this: the sky is the clear colour (THRASH state 3), fog stays off
(state 0x15 is always 0), and the 8-bit texture set (`TrNNN_8.qfs`) is
used. Changing `3tr00.hrz` to dense red fog changed nothing in a race.
The game decides by its renderer's number (`1 << number` at
`0x7a3a58`, bit 1 being softtria, tested at `0x41dbe8` to pick the
8-bit set). Its Direct3D and 3dfx paths would draw the sky and send fog
to the renderer.

Not yet:

- **Checked against the game.** How the density becomes a distance, the
  sun's `R` and where `A` starts, the cloud layer's size and speed, and
  the lightning are Reluva's readings: the game's hardware paths would
  show them, and the software path draws none.
- **The cloud dome offset and nimbus**, and the clouds' variance, aren't
  used.
- **Picture repeats.** The number of times the horizon picture repeats
  round the circle (Reluva uses four) is a guess, to check against the
  game.
