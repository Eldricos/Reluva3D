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
  - **ROTATE** turns it as it crosses the screen: by its x / z in the
    view, in turns (`0x479fd9`), so a full turn 45° off the centre of
    the view. Without, its up follows the sky's (the projected direction
    from (cos A, cos B, sin A) to (cos A, sin B, sin A)).
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
    half the mean of (neighbour − it) × ((neighbour − 128) / 128)², kept
    within 0 to the brightness when it moves, and truncated. The weight
    is squared (`0x47b0a4`-`0x47b0e1` multiply by |n − 128| / 128 twice;
    corrected November 2026, it was taken once), which leaves more
    points near the ends: run on Hometown's values the points span
    about 70 to 197, as the recorded frame's 88 to 199, where the single
    weight gave at most about 178. It is the colour and the alpha (on a
    Voodoo; the Direct3D, Rage and PowerVR paths take bright −
    variance / 2 as the alpha). Hometown's points were 88 to 199 for
    235 and 208. Type 1 is added (source alpha, one).
  - **The drift.** Each game tick the texture coordinates move by
    (cos dir, sin dir) × speed × 0.00003, in tiles, kept within a tile
    (`0x47b76a`).
  - **The camera's place** (November 2026). To that the drawing adds
    the camera's x and z over the visual height (`0x47b88a`; the reader
    keeps 1 / visual height at `0x55d208`, `0x47866b`), so as the camera
    moves the grid, which follows it, shows the clouds passing over:
    a tile for each visual height moved. That is all the visual height
    does.
  - **Heights.** The grid's heights are the world's: the drawing
    subtracts the camera's y (`0x47b4f0`), and so does the sky's shaded
    area (`0x479500`, its base and heights less the camera's y). The game ticks about 64 times a second: its tick
    count (`0x7d3684`) went up 641 in 10 seconds of a race.
- **Lightning's bolts**, over the clouds (`0x47a1c4`, `0x4946e0`), and
  their flash on the clouds. From the code (`0x47b5c0`, `0x4945a0`,
  `0x494420`, `0x494630`):
  - **When.** On game ticks that are a multiple of the second lightning
    value, with a chance in percent of the first (rand() % 100 below
    it). Hometown's night with weather (75, 64) has one about three
    times in four seconds. With the first 0 there's none.
  - **Where.** A bolt starts at a random x and z within 750 of the
    camera (rand() % 1500 − 750), on the cloud dome, heading away from
    the camera; a thunder sound plays there.
  - **Its shape.** Up to 40 points, each 40 units on from the last
    along the ground and on the dome, turning by −2 to 2 times 9° each
    step. Where the points left reach the fork value (30 at first), it
    forks if rand() % n isn't 0: the main line goes on with the fork
    value 0.75 n, and a branch of n / 2 points turns by 18° steps with
    0.3 n. The game holds 240 points for all bolts.
  - **Its life.** 24 ticks. Each tick every point moves −1, 0 or 1 times
    its jitter on x and on z: (1 − n / 40) × 3, n the points left when
    it was made.
  - **Drawn** as lines in 0xFFE2E2FF, pale blue-white.
  - **The flash.** For the latest bolt, life × 510 / 24, at most 255:
    full for 12 ticks, then fading. It lights the cloud grid's points
    around where the bolt starts (`0x47b3a0`): the nearest point and
    those next to it, the columns either side at half, raising each
    point's brightness to that if higher. Nothing else gets brighter.
- **The earth's shading**, fogged as the sky: the band below the
  horizon, in the earth's colours.
- **The horizon picture**, fogged as the sky, drawn white whatever the
  variant (`0x478310`). It is the first eight pictures of the track's
  texture sheet, by their place in it (64 × 64 each; named `0000` to
  `0007` on most sheets, `HOR1` to `HOR8` on Country Woods' and
  Aquatica's, see [frd.md](frd.md), Textures), not anything in
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
- **The horizon picture.** The track sheet's first eight pictures
  side by side, a tile a slot, twice round a band at the horizon radius,
  the second copy flipped with the mirror flag. The band is a cylinder
  in 3D, so towards the screen's sides it is spaced as a projection, not
  as the game's flat quads. Its v runs a texel in from the picture's top
  and bottom: the strip's texture repeats, and at v = 0 the filter mixed
  the bottom row into the top, a dashed line along the hills' tops that
  the game doesn't have (it looked like far power lines; below).
- **The sun or moon.** The variant's picture (`SUND`, `SUNN`, `SUNW`, and
  `SNNW` for night with weather where a track has one, else `SUNW`) at
  the game's place and size, facing the camera, upright or, with
  `ROTATE`, turned as the game turns it. Drawn before the clouds, or
  after them with `INFRONT`.
- **The clouds.** The game's grid, dome, nimbus and brightness (with
  Reluva's own random numbers), drifting by the game's amount at 64
  ticks a second and moving by the camera's place over the visual
  height (`cloudOffset`). The sky's heights, clouds, shaded area and
  horizon picture, are the world's less the camera's height. The `sky.fsh` pictures have no mipmaps, as THRASH's
  textures have none: the night's `CLDN` is black with one-texel stars
  (at most 100), which mipmaps averaged nearly away.
- **Fog.** The game's curve and values, per pixel over the depth
  (`DrawCall::tableFog`). Within a dynamic fog region (by the block the
  camera is in), the colour and density go from the start's to the
  apex's between the before and centre blocks, then to the end's by the
  after block. The clear colour is the fog colour. `Y` turns fog off
  and on.
- **Ambient light.** The percentages scale the track's baked vertex
  lighting.
- **Lightning.** The game's bolts, chances, jitter, life and flash,
  ticked at 64 a second (`lightningStarts`, `makeBolt`, `tickBolt`,
  `boltFlash`), with Reluva's own random numbers. The bolts are thin
  quads, about a pixel and a half wide.

Until November 2026 the page looked the pictures up by name, so Country
Woods and Aquatica, whose sheets name them `HOR1` to `HOR8`, had no
horizon picture: a flat band of the earth's colour where
`Country_Woods_Day` shows hills. It takes them by place now.

## Against footage of the game (November 2026)

Eighteen recompressed YouTube clips of the retail game, one per track by
day and by night, from a 3D card at high detail (`<Track>_<Day|Night>`,
times as m:ss), against the Tracks page and frames from `nfs-host`:

- **The aurora** in `Summit_Night` (2:17, 2:40, 7:20) is the night
  "moon": `SUNN` in Summit's `sky.fsh` is a 128 × 128 green and purple
  aurora, text `A330 B62 R185`, so about 47° across, 25° up, added,
  without `ROTATE`. Nothing in the `.hrz` makes it. The page draws it.
  Checked by a copy of the game whose Hometown `sky.fsh` was Summit's
  with the text `A313 B62 R185` (down Hometown's start): `nfs-host`'s
  night race and the page both show it upright, its bright lobes low
  left and high right, at the same place over the road.
- In `Summit_Night` it runs the other way, high left to low right.
  That clip looks like a mirrored race: at its start (0:16-0:22) the
  80 sign is on the left of the road, where `Summit_Day` (0:16) has it
  on the right. So the game mirrors the sun's picture with the track:
  the mirror flag negates the projection's x scale, which flips the
  whole 3D view, sky and all ([frd.md](frd.md), the mirrored track). The
  page's mirrored mode (`U`) flips it the same way.
- **Stars and moon.** `Hometown_Night` (0:40) has many one- or
  two-pixel stars and the moon high right; the page showed the stars
  fainter. They are `CLDN`, added: the page's mipmaps averaged them
  away (above). Against an `nfs-host` night race at Hometown's start
  (November 2026, fresh `config.dat`, so the highest detail) the page
  now shows as many stars, as small. The host's sky above the shaded
  band is (8, 12, 24), near the `.hrz`'s top colour (12, 12, 27). Not
  compared at the same height above the horizon: the page's view is
  narrower than the game's 90°.
- **Clouds.** `Hometown_Day` (0:20) has thin white streaks on a deep
  blue; the page's clouds are broader and paler. Against `nfs-host`'s
  day race at the start the sky's red (the clouds' white over the blue)
  runs over the same range, 172-213 in the host and 186-231 on the page
  (10th to 90th percentile, blue at 255 in both), so the brightness and
  fog are about right; the host's clouds are smaller puffs with blue
  between, the page's blotchier. The puffs' layout comes from the
  random brightness of the grid's points (Reluva's own random numbers),
  and the host point-samples where the card filters, so this is left.
- **Distance.** The page drew a broken line along the horizon at
  Hometown's start where neither the clips nor `nfs-host` have one.
  It wasn't far power lines but the horizon band's top edge (above),
  fixed. The page also drew every block of the track; it now draws the
  blocks the game draws ([frd.md](frd.md), Which blocks are drawn):
  those in the camera block's neighbour list within 500 units by day,
  375 by night, at the road detail the game picks by distance. Those
  distances depend on the **view distance** option (`viewlevel`,
  `0x6fbc28`): levels 0 and 1 (the highest, what `nfs-host`'s fresh
  `config.dat` has, and the clips' "high detail") 500, level 2 275 and
  level 3 200 by day; by night 375, 205 and 150. The page uses level 1;
  with a lower one the game draws fewer blocks ahead than the page.
  The game also drops a polygon whose first corner is more than 500
  ahead along the view (`0x41b3bf`, the camera's `+0x30`, 500 for the
  main view whatever the level and by night too); the page does the
  same now.
- **Rocky Pass by day** (its rotation 160°), page and `nfs-host` at the
  start: the same scene, but the sky about 25 bluer in the host
  ((205, 242, 222) against (213, 234, 191), 20-26° up, the host's
  frames in 16-bit steps). The view's centre is at slot 14.9 or 0.7
  with the rotation added or taken away, both on the sun side, so this
  doesn't tell the rotation's sign.

  **It's the clouds** (November 2026). The same race was run from two
  copies of the game whose `3tr03.hrz` changed only the clouds, and the
  page given the same files, compared at 20° up (the host's view taken
  as 90° across, which fits its gradient):

  | | `nfs-host` | Tracks page |
  |---|---|---|
  | No clouds (brightness 0) | (193, 213, 173) | (184, 210, 178) |
  | Even clouds (variance 0) | (250, 255, 225) | (248, 255, 239) |
  | The file's clouds | (228, 246, 203) | (206, 232, 198) |

  Without clouds the sky's shading agrees within the elevation's
  uncertainty: the horizon colours, the slot, the fog and the sun are
  right. The rotation's sign can't be what's off: at Rocky Pass's
  start the view's slot without the rotation is 7.8, so either sign
  lands on slots mirrored about 7.5, where the colours are the same.
  With every cloud point alike the clouds add as much in both. Only
  their random layout differs: the game takes the points from its own
  generator (`0x4e0d88`, a lagged sum shared with the rest of the game),
  so which part of the sky is thick changes from race to race; at that
  spot the host had more cloud, which, added and fogged towards the
  yellow-grey fog colour, lifted its sky by about 35 against the page's
  20.
- **Rocky Pass by night**, the same: stars in both; the page's sky
  (50, 45, 66) and horizon hills (32, 31, 48) a step brighter than the
  host's (41, 40, 57) and (24, 20, 41); the trees by the road much
  darker on the page ((1, 2, 5) against (8, 16, 8)), which fits the
  car's headlights, added to the vertex colours of what is near
  ([frd.md](frd.md), the headlights). The page has them now: a car at
  the camera, `E` for off, low and high beams.
- **By night or weather** the game leaves out each block's object
  layer 3 and its fourth group of placed objects (`0x41cdc0`); the page
  does too. At Hometown's start they are empty: what goes is the road
  beyond 375.
- **Other settings** that change these: the Horizon option, which
  turns the sky on ([nfs.md](../../engines/nfs.md), the detail levels;
  a saved `config.dat` can have it off), and `nfs-host`'s own drawing
  (point-sampled), not the card's.
- **Lightning** is in none of the clips (no weather), so it stays
  unchecked.

Not checked against the game:

- **Units.** The track's coordinates are taken to be the units of the
  game's depth w, as the horizon radius and the cloud tiles are; the
  sun's and the road's directions agreeing supports it.
