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
again from copies of the game with one value changed: in `3tr00.hrz`
the visual height, the cloud dome offset and the horizon radius, and in
`sky.fsh` the sun's `A`. Then the code those pointed to in `nfs3.exe`:
the `.hrz` reader (`0x478344`), the sky's set-up (`0x478a34`,
`0x47ab40`, `0x47ae10`) and its drawing (`0x479680` to `0x47b850`).

### Round the horizon: 16 slots

The game divides the circle into 16 slots of 22.5°. For the camera's
heading h, in turns (atan2(x, z) / 2π of its forward direction, in the
track's coordinates; `0x479160`), the first slot drawn is
16 × frac(h + 0.5 + rotation), and it draws the 8 from there: the
slot at the centre of the view is 16 × frac(h + 0.75 + rotation). The
rotation is the `.hrz` value in degrees, kept as turns (× 65536 / 360,
in 16.16) from 0 to 1.

Checked against the road: at Hometown's start the road's direction
(from the `.col` virtual road) is at 0.882 turns, and the recorded
frame's centre was 0.127 into slot 2, so 0.133 turns: a quarter turn
on, as above. The track's coordinates are left-handed (the road's
right is (z, 0, −x) of its forward), so turning right raises h, as the
tiles show, left to right.

### In the order the game draws them

- **The sky's shading**, unfogged: a band at the horizon radius from the
  midpoint to the top of the shaded area, and the clear colour (THRASH
  state 3) above it, the sky's top colour.
  - **Its colours by slot.** At the horizon the sky has a colour at each
    slot edge (`0x478a34`): edge k and edge 15 − k have
    sun side + k × (far side − sun side) / 8 (in 16.16, truncated), for
    k from 0 to 7. So slot 15 is all the sun side's colour, and slot 7
    is seven eighths of the way to the far side's. It doesn't follow
    the sun picture's `A`. The recorded frame has (223, 210, 151) and
    (210, 205, 157) at the first two edges, as computed.
- **The sun**, unfogged, added (unless `NOADD`). With `INFRONT` it comes
  after the clouds instead (`0x47a190`), still before the earth and
  horizon. Its picture's text (`A200 B45 R25 ROTATE`, [fsh.md](fsh.md))
  places it (`0x47ab40`):
  - **Where.** At (cos A, cos B, sin A) × 3000 in the track's
    coordinates, A and B in degrees. So A is a heading of
    90° − A, and B = 45 puts it at atan(cos 45°), 35°, above the horizon.
  - **How big.** A square whose corners are R × 10 from its centre: a
    side of R × 14.1 units at about 3000 to 3700 away. Hometown's R 25
    is about 7°.
  - **ROTATE** turns it as it crosses the screen (by its x / z in the
    view); without, its up follows the sky's.
  - Checked with `A313` in a copy's `sky.fsh`, which puts the sun down
    Hometown's start: the game drew it at the top centre of the screen,
    its centre about 12 pixels down at w 2981, 38 pixels across, as
    computed.
- **The clouds**, fogged with the sky's fog value (below) when "fog
  clouds" is set: the variant's tile from `sky.fsh` (`CLDD`...).
  - **The grid** (`0x47ae10`). 13 × 13 points 450 units apart, from
    −3000 to +2400 on x and on z, round the camera. Each point's texture
    coordinates are its place in the grid, u along x and v along z
    (the recorded frame: 450.6 units a tile, measured on the screen).
  - **The dome.** At a distance r along the ground a point is
    off1 + 3 × off2 × cos(r / 3000 quarter turns) above the camera, for
    the cloud dome offset (off1, off2): the reader keeps 3 × off2
    (`0x4786a4`). Measured: (−100, 250) gave 750 cos(r / 1900) − 88, and
    (−300, 600) 1818 cos(r / 1900) − 284. The visual height and the
    radius don't change it.
  - **The nimbus.** A point is lowered by its brightness / 255 ×
    3 off2 / 2 × (nimbus / 100, rounded down): only a nimbus of 100
    counts. Hometown's is 80.
  - **The brightness** of each point, set once as the track loads:
    the brightness less a random amount below the variance; then pushed
    from the middle, by a signed square root of (b − 128) / 128; then 20
    passes that move each towards its neighbours within two points, by
    half the mean of (neighbour − it) × |neighbour − 128| / 128, kept
    within 0 to the brightness. It is the colour and the alpha (on a
    Voodoo; the Direct3D, Rage and PowerVR paths take bright −
    variance / 2 as the alpha). Hometown's points were 88 to 199 for
    235 and 208. Type 1 is added (source alpha, one).
  - **The drift.** Each game tick the texture coordinates move by
    (cos dir, sin dir) × speed × 0.00003, in tiles, kept within a tile
    (`0x47b76a`). The game ticks about 64 times a second: its tick
    count (`0x7d3684`) went up 641 in 10 seconds of a race.
- **The earth's shading**, fogged as the sky: the band below the
  horizon, in the earth's colours.
- **The horizon picture**, fogged as the sky, drawn white whatever the
  variant (`0x478310`). It is the first eight pictures of the track's
  texture sheet (`0000` to `0007`, 64 × 64 each), not anything in
  `sky.fsh`. `sky.fsh`'s `CHRD`, `CHRN`, `CHRW` and `CHNW` are sphere
  maps, the cars' reflections.
  - **Which sheet.** `trNN0.qfs`, or `trNN1.qfs` for a mirrored track
    (`0x41dbc0`, by the flag at `0x6fd4c4`): set 1 is set 0 with the
    signs' text reversed. Night and weather use the same sheet and
    pictures; only the `.hrz` and `sky.fsh` pictures change with them.
  - **Layout.** Slot i has tile i mod 8 (`0x478bc0`). With the mirror
    flag, slots 8 to 15 have tiles 7 to 0 instead, each drawn flipped.
    So the picture goes round twice, the second time mirrored.
  - **On the screen.** Each tile is a flat quad 126 pixels wide at
    640 × 480 (22.5° of a 90° view) at w equal to the radius, between
    the picture's heights over the base.
- **The scenery**, with the scenery's fog value.

### Fog

The 3D card's table fog ([thrash.md](thrash.md), Fog): each pixel is
mixed towards the fog colour by
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

The Tracks page draws as the game does, above, with the functions in
`hrz.h` (`horizonTurn`, `skyBaseColours`, `cloudHeight`,
`cloudBrightness`, `cloudDrift`, `sceneryFogValue`, `skyFogValue`):

- **The dome.** The earth's colours from the base up to the midpoint
  (the horizon), then the sky's from the midpoint to the top, its
  colours at the horizon by slot. Its 32 segments start at slot 0, two a
  slot, placed by the rotation as the game's.
- **The horizon picture.** The track sheet's pictures `0000` to `0007`
  side by side, a tile a slot, twice round a band at the horizon radius,
  the second copy flipped with the mirror flag. The band is a cylinder
  in 3D, so towards the screen's sides it is spaced as a projection, not
  as the game's flat quads.
- **The sun or moon.** The variant's picture (`SUND`, `SUNN`, `SUNW`, and
  `SNNW` for night with weather where a track has one, else `SUNW`) at
  the game's place and size, facing the camera, upright (`ROTATE` isn't
  followed). Drawn before the clouds, or after them with `INFRONT`.
- **The clouds.** The game's grid, dome, nimbus and brightness (with
  Reluva's own random numbers), drifting by the game's amount at 64
  ticks a second.
- **Fog.** The game's curve and values, per pixel over the depth
  (`DrawCall::tableFog`). Within a dynamic fog region (by the block the
  camera is in), the colour and density go from the start's to the
  apex's between the before and centre blocks, then to the end's by the
  after block. The clear colour is the fog colour. `Y` turns fog off
  and on.
- **Ambient light.** The percentages scale the track's baked vertex
  lighting.
- **Lightning.** With lightning on, each second has a chance (out of
  256) of a flash a fifth of a second long, which brightens everything.

Not checked against the game:

- **Lightning**: how often, how long and how bright.
- **Units.** The track's coordinates are taken to be the units of the
  game's depth w, as the horizon radius and the cloud tiles are; the
  sun's and the road's directions agreeing supports it.
