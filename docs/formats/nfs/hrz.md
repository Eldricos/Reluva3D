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

## What the game does with it

Worked out from the game's own drawing: a race frame on Hometown
recorded in full (`nfs-host -gpudump`, every vertex with its texture
and states), with the game told it has a Voodoo 2 so that it draws its
sky ([nfs.md](../../engines/nfs.md), Sky and fog). Then the same frame
again from copies of the game with one value of `3tr00.hrz` changed:
the visual height, the cloud dome offset, the horizon radius. The code
the frame pointed to in `nfs3.exe` confirmed the horizon's layout.

In the order the game draws them:

- **The sky's shading**, unfogged: a band at the horizon radius from the
  midpoint to the top of the shaded area, in the sky's colours, and the
  clear colour (THRASH state 3) above it, the sky's top colour.
- **The clouds**, fogged with the sky's fog value (below) when "fog
  clouds" is set: the variant's tile from `sky.fsh` (`CLDD`...) on a
  dome around the camera.
  - **The dome.** A grid of tiles about 450 units across (450.6
    measured). At a distance r along the ground it is
    off1 + 3 × off2 × cos(r / 1900) above the camera, for the cloud dome
    offset (off1, off2): (−100, 250) gave 750 cos(r / 1900) − 88, and
    (−300, 600) gave 1818 cos(r / 1900) − 284. The visual height and
    the horizon radius don't change it.
  - **The colour.** Each corner's brightness is between the cloud
    brightness less its variance and the brightness: Hometown's corners
    were 88 to 199 for 235 and 208. Alpha is the same. Type 1 is added
    (source alpha, one).
- **The earth's shading**, fogged as the sky: the band below the
  horizon, in the earth's colours.
- **The horizon picture**, fogged as the sky. It is the first eight
  pictures of the track's texture sheet (`0000` to `0007` of
  `trNN0.qfs`, 64 × 64 each), not anything in `sky.fsh`. `sky.fsh`'s
  `CHRD`, `CHRN`, `CHRW` and `CHNW` are sphere maps, the cars'
  reflections.
  - **Layout.** 16 slots round the horizon, 22.5° each, a tile each
    (`0x478bc0`). Slot i has tile i mod 8. With the mirror flag, slots 8
    to 15 have tiles 7 to 0 instead, each drawn flipped. So the picture
    goes round twice, the second time mirrored.
  - **On the screen.** The game draws each tile as a flat quad 126
    pixels wide at 640 × 480, 22.5° of a 90° view, at w equal to the
    radius. Its top and bottom are the picture's heights over the base.
- **The scenery**, with the scenery's fog value.

**Fog** ([thrash.md](thrash.md), Fog) is the 3D card's table fog: each
pixel is mixed towards the fog colour by
f(w) = (1 − e^(−w / v)) / (1 − e^(−52428.8 / v)), w its depth. The
value v is:

- for the scenery: 5 × (100 − density), clamped to 5..500, or no fog for
  density 0 (`0x470ad0`). Hometown's day, density 5: 475.
- for the sky: that, unclamped below by density 0, times
  round((1 − pixmap effect / 100) × radius / 100) (`0x470b20`; the
  multiplier is made as the file is read, `0x47a88e`). Hometown's 70
  and 1500: 5, so 2375. With the radius 2000 the game sent 2850. So the
  horizon picture, 1500 away, is fogged about half.

The density and colour are the dynamic fog region's where the camera is,
as below; the same density goes into both values.

With `softtria.dll` the game draws none of this: the sky is the clear
colour, fog stays off, and it uses the 8-bit texture set
(`TrNNN_8.qfs`). It decides by its renderer's number (`1 << number` at
`0x7a3a58`; bit 1, softtria, picks the 8-bit set at `0x41dbe8`), and
sends fog only to the 3dfx renderers.

## How Reluva draws it

The Tracks page draws as the game does, above:

- **The sky's and earth's shading.** A dome: the earth's colours from
  the base up to the midpoint (the horizon), then the sky's from the
  midpoint to the top. The sky's base colour turns from the sun's side
  to the far side, by the sun's `A`.
- **The horizon picture.** The track sheet's pictures `0000` to `0007`
  side by side, twice round a band at the horizon radius, the second
  copy flipped with the mirror flag. The band is a cylinder in 3D, so
  towards the screen's sides it is spaced as a projection, not as the
  game's flat quads.
- **The clouds.** The game's dome and brightness, over a grid of 16 × 16
  tiles around the camera. They drift with the wind: its speed in
  hundredths of a tile a second, towards its direction.
- **Fog.** The game's curve and values, per pixel over the depth
  (`DrawCall::tableFog`). Within a dynamic fog region (by the block the
  camera is in), the colour and density go from the start's to the
  apex's between the before and centre blocks, then to the end's by the
  after block. The clear colour is the fog colour. `Y` turns fog off
  and on.
- **Ambient light.** The percentages scale the track's baked vertex
  lighting.
- **The sun or moon.** The variant's picture (`SUND`, `SUNN`, `SUNW`, and
  `SNNW` for night with weather where a track has one, else `SUNW`),
  facing the camera inside the dome. Its text gives `A` (degrees round,
  as for the dome's sunlit side), `B` (degrees above the horizon) and
  `R`, taken as its width in degrees. It's added to the sky, unless the
  text says `NOADD`; with `INFRONT` it's drawn over the horizon picture,
  else behind it.
- **Lightning.** With lightning on, each second has a chance (out of
  256) of a flash a fifth of a second long, which brightens everything.

Not yet checked against the game:

- **Where slot 0 points**, and so where the horizon picture and the sun
  sit round the circle: the frame recorded doesn't say which way the
  camera faced. The page starts both at its own angle 0.
- **The sun's `R` and `B`**: the sun wasn't in the frame recorded.
- **The clouds' drift**, the nimbus, the rotation and the lightning.
- **Which texture set** the night and weather variants take their
  horizon pictures from: the page uses the set shown.
- **Units.** The track's coordinates are taken to be the units of the
  game's depth w, as the horizon radius and the cloud tiles are.
