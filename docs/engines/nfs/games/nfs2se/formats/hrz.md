# Need for Speed II SE: skies and lights (`.hrz`, `.lgt`)

> **Status:** decoded · **Code:** `src/engines/nfs/nfs2se/formats/hrz.cpp` · **Checked on:** all 34 sky files and 8 light files on the US CD · **Confidence:** confirmed unless marked

## Overview

Commented text, each value named by the comment before it; the names
here are those comments'. NFS III's sky files are the same kind of text
with other values ([hrz.md](../../nfs3/formats/hrz.md)).

- **The software program's skies:** `GameData/Tracks/pc/trNN.hrz`, the
  copies in `GameData/DashHud/pc/huddata.viv`, and `Tracks/se/tr02.hrz`,
  `tr04.hrz`: 26 values.
- **The 3dfx program's skies:** `GameData/Tracks/se/3trNN.hrz`: 27 values.
- **Lights:** `GameData/Tracks/se/trNN.lgt`: 65 values.

The software program reads its sky as `%sTr%02d.hrz` (0x4719a0), from
`GameData/Tracks/pc` with the track's pictures `Tr000.qfs` beside it
(seen in the hosted game's files opened for a race at `TR00`), the
3dfx program `%s3Tr%02d.hrz` (`NFS2SEA.EXE` 0x42ce30). **Neither reads
the `.lgt` files**: neither program has the name or a pattern making it,
the hosted game opens only the `.hrz` for a race, and the 8 files hold
the same numbers (475 bytes each). What they were for isn't known: their
lights run the other way from the software program's (15 full light,
not dark), and their palettes are on a scale of 0 to 127, as a renderer
taking 128 for a colour unchanged would want: the PlayStation's
graphics chip lights a texture by its colour × the light ÷ 128 (the
psx-spx description of its GPU). Their wording is in neither program
nor the other text files, and they're only beside the tracks rebuilt for
the SE. So another renderer's template *(inferred)*, the PlayStation's
*(guessed)*: a `.lgt` beside a PlayStation release's tracks would settle
it. The only track names either program makes with a varying extension
are the computer cars' speed files' (`%sTr%02d%s.%s`, 0x4b8e84, the
extensions kept a byte less, `por` for `qps`, and put right by
0x44f480), and no form of the name shifted by up to 3 is in either.
No public listing of another release's files showing a `.lgt` was found
(late November 2026; D. Auroux's NFS II track notes name only the
`.trk`, `.qfs`, `.col` and `.hrz`).

## Layout

Integers, separated by commas and spaces, with C comments between them.
A comment may hold values of its own (`/*37,31,102, at position of sun
*/`), which aren't the file's; `tr08.hrz` (software) has one value before
its first comment.

### Software skies (26)

| Values | The comment's name |
|---|---|
| 1 | Black horizon (1 yes, 0 no) |
| 1 | Mirror flag |
| 1 | The radius of the horizon |
| 1 | Its angle of rotation, in degrees |
| 1 | The distance to the horizon for flat projection (no more than the radius) |
| 1 | How high in the air the flat shaded area's base is (world offset) |
| 1 | The horizon's height (from the base) |
| 1 | Where the pixmap's base is in the horizon, percent of the height |
| 3 | RGB of the flat shaded sky |
| 3 | RGB of the flat shaded horizon base |
| 4 | Depth cueing distances (0-1100) |
| 4 | Their light values (0-15) |
| 1 | Colour system: 0 RGB, 1 HSV |
| 3 | The RGB or HSV value |

### 3dfx skies (27)

| Values | The comment's name |
|---|---|
| 2 | Cloud dome offset and height |
| 5 | Black horizon, mirror, radius, angle, flat distance, as the software's |
| 1 | How high the Gouraud shaded area's base is |
| 1 | Its height (from the base) |
| 1 | Its midpoint (from the base) |
| 2 | The horizon pixmap's top and bottom (from the base) |
| 3 × 3 | RGB at the top and base of the shaded earth, and the top of the shaded sky |
| 2 × 3 | RGB at the shaded sky's base: toward the sun, then away from it |

The last two have no comments of their own; the comments beside them
("at position of sun", "at position opposite to sun") say which.

### Lights (65)

| Values | The comment's name |
|---|---|
| 4 × 2 | Depth cueing definition |
| 16 × 3 | Light palettes (0-127) |
| 2 | Maximum and minimum intensity |
| 7 | Effects' light colour: type, r0, g0, b0, r1, g1, b1 |

## Behaviour

Addresses are `nfs2sen.exe`'s, or `NFS2SEA.EXE`'s marked [A]. From the
code, and the software program's tables matched in memory on the hosted
game at `TR00` (November 2026).

### The software sky

Drawn by 0x470eb0.

- **The ring:** 16 points at the file's angle plus a sixteenth of a turn
  each, at the radius (0x4717f0), a lower ring at the base and an upper
  one the horizon's height above it. Each point is projected as if at
  the flat distance; the true depth only drops segments behind the eye.
- **Each of the 16 segments:** a flat quad of the sky's colour from the
  upper ring up to a point ten heights above; one of the horizon base's
  colour from the lower ring down to ten heights below; and between them
  a slice of the horizon pixmap. There are 8 slices: segment k takes
  slice k mod 8, or with the mirror flag, segments 8 to 15 take slice
  (15 − k) flipped left to right. The pixmap's base percentage sets a
  row between the rings (0x448250) *(inferred)*.
- **The colours** become the palette's nearest entries (0x4718f0).
- **The slices** are the track's first 8 pictures of `Tracks/pc/trNN0.qfs`
  (0x471980), painted sky and all, with no see-through texels: 128 × 256
  on `TR00` and `TR02`, 85 × 150 on `TR03`, 80 × 150 on `TR07`.
- **Black horizon** (or the horizon turned off, 0x4716d0) fills the view
  with one polygon instead: black, or the sky's colour.
- **The sky isn't depth cued**: its polygons go on the list drawn
  directly, not into the depth buckets (0x45acd0).

### The software depth cueing

- **The curve** (0x470d10, 0x45be20): the four (distance, light) pairs
  are the control points of a uniform cubic B-spline, its ends repeated
  three times, sampled 20 times a segment and joined by lines: a light
  of 0 to 15 for every distance. It passes near the middle two points,
  not through them.
- **Where it changes:** walked from the far end, each distance where
  the light changes is kept (at most 16), the farthest set to 1,239.
  `TR00`'s (0, 250, 500, 900; 0, 0, 10, 15) give 1,239 at 15, 899 at 14
  ... 222 at 0, as in memory.
- **The distances** are the program's depth buckets for sorting
  polygons, 1,240 of them: a polygon's is its four corners' mean depth
  plus 10, in halves of the unit (0x45c5f0); the polygons outside 0 to
  1,239 aren't drawn. A distance d is about d / 2 − 10 units away:
  `TR00` starts darkening about 101 units out, and is dark from 440. The
  unit is the track's *(inferred)*: the depths are clipped against the
  views' far planes of 500, 400 and 150 (0x4339a0), and the 1,240
  buckets reach 609.5.
- **Each frame** (0x46f330) a marker goes in at each change's bucket;
  drawing far to near, it sets the light for the polygons after it.
- **The lists** sort by the depth less a bias of their own (0x434f40),
  so the bias moves a polygon's light too: the track's ground 0, its
  second group 6 units, 10 and a fourth that's 0 but −12 on track 2 and
  −16 on track 3 while the player is at road points 52 to 98 (0x434fe0),
  and the cars 4 (0x4489f6).
- **A light L** is one of 16 colour tables (0x4693c0): each palette
  colour moved L/16 of the way to the file's last colour, then the
  nearest entry (entry 255, see-through, kept). 0 changes nothing.
  Colour system 1 does it in HSV *(inferred)*; no file uses it. The
  colour is a grey (40, 40, 40) on every track but `TR07`'s lighter and
  `TR08`'s black: so the distance darkens, not fogs, on most.
- **Night:** if a key is held as the track loads (byte 0x4c85b5 of the
  keys held, scan code 0x31: N *(inferred)*), the file's values give way
  to a black sky, black horizon, distances 0, 80, 85, 300 with lights
  0, 0, 12, 15, and the colour (10, 10, 10).

### The 3dfx sky

Drawn by [A] 0x42c010 before everything else, never testing or writing
depth (0x42c9e0), in the main view as in the rear-view mirror.

- **The ring:** 17 points (the last the first again) at the file's angle
  plus a sixteenth of a turn each, `(r sin θ, y, r cos θ)` for the
  radius r, centred on the eye and turned with the view but not moved
  (0x42cc70). Each is drawn as if the flat distance ahead, its depth in
  the view replaced by it (0x42cb80); a segment is drawn only if its
  lower points reach a quarter of the radius ahead (0x42c38f). The
  heights are the eye's plus the file's: the base, the base plus the
  midpoint, the pixmap's top and bottom, kept as fractions of the height
  rounded down to 65,536ths (0x42d07b), and the base plus the height.
- **The colours at the sun and away from it** are laid round the ring,
  not by the player's heading: the 16 ring points' colours go from the
  sun's at the file's angle toward the other, `sun + (opposite − sun) ×
  k / 8` for k to 7, and back the same way (0x42d0a3), so the far side
  reaches 7/8 of the way.
- **Bottom to top** (0x42c3c7, 0x42c479): the shaded sky from the
  midpoint row in the ring's colours to the top in the sky's top colour;
  the cloud dome; the earth from the base in its base colour to the
  midpoint in its top colour; the horizon's pictures between the
  pixmap's two heights. Above the top is the clear colour: the sky's
  top, or black with black horizon (0x427440).
- **The pictures** are the first 8 of the track's `trNN0.qfs`
  (`trNN0m.qfs` mirrored), `0000` to `0007` (0x445550): segment k takes
  picture k mod 8, or with the mirror flag (on in all 8 files) segments
  8 to 15 take 15 − k turned left to right (0x42cd2e). Each covers its
  segment, its top right corner 3% past the next segment's (0x42c024);
  their green is see-through as the track's.
- **The cloud dome** (0x42be50, 0x42b4d0): a 13 × 13 grid, x and z
  from −1,000 to 800 by 150 round the eye, at `offset + height ×
  cos(2π r / 4000)` for r its distance from the middle, in true
  perspective, with `sky.fsh`'s `CLDn` for track n once to each cell;
  added to what's behind on tracks 2 to 6, half over it on track 7, and
  none on the others (a table, 0x4eda10).
- **Black horizon** (`TR08`), and `TR04`'s tunnels (0x42c7a0), draw one
  black quad instead: on track 4, at road points 0x448 to 0x555, either
  way; from 0x3F6 to 0x445 looking on along the road (the view's way
  against the point's forward vector, 0x42c8d0); from 0x556 to 0x599
  looking back (0x42c820). The view's way is its forward row (its
  matrix at 0x5200c0, the main view's the camera's own, 0x4d3ab8 for
  player 0), not the car's: they agree only from the bumper or the
  cockpit. The mirror skips it (0x42c056). There's no sun: `sky.fsh` has only clouds, rain
  and splashes.

### The 3dfx fog: the program's own

Nothing of the `.hrz` fogs the 3dfx game. For each track the program
sets Glide's table fog (`grFogMode`), a colour, and 20 tables
(`guFogGenerateExp`), table i's density i twentieths of the way from d0
to d1 (0x42b284):

| Track | Colour | Density |
|---|---|---|
| 0 | 0, 2, 4 | 0.008 to 0.028 |
| 2 | 204, 97, 52 | 0.002 to 0.022 |
| 3, others | 132, 130, 132 | 0.003 to 0.023 |
| 4 | 132, 130, 132 | 0.002 to 0.022 |
| 5 | 125, 128, 132 | 0.003 to 0.023 |
| 6 | 189, 189, 134 | 0.002 to 0.022 |
| 7 | 132, 130, 132 | 0.005 to 0.025 |
| 8 | 0, 2, 4 | 0.003 to 0.023 |

A vertex is fogged by its depth in the view in track units: Glide's w,
the reciprocal of the 65,536 ÷ the 16.16 depth the program gives it
(0x4932f0). The sky's shaded part is given a depth of 1, so it's barely
fogged; its earth and pictures 100, but 1 on track 0 (0x42c043).

Each frame it picks a table, and on some tracks a colour, by the
camera's road point (0x427690): the point of the view's eye, a road
locator moved to it each frame and copied to 0x50f594 (0x446ffc), so
trailing the car by the camera's distance in the chase views, and in
replays and the TV cameras wherever the camera is; the mirror takes the
main view's. Its lists, run
over every point: `TR00` table 0, its colour rising to (62, 62, 62)
from point 481 and back by 655; `TR04` up to table 14 and a bluer grey
from 576 to 764, then (3, 3, 3) and table 11 from 1,053 to 1,376, red
(168, 41, 6) from 1,281 to 1,349; `TR05` darker, to (37, 40, 44), from
1,524 to 1,716; `TR07` up to table 9 from 1,206 to 1,749; the others
table 0 throughout. The rear-view mirror (the program's second view,
drawn when the rear view is on: 0x46dcd2) takes the densest table,
19, with the same colours, on every track but 4, whose code overwrites
the choice with the road's place (0x427a72).

## In Reluva

Read as `Sky` and `Lights` (`src/engines/nfs/nfs2se/formats/hrz.cpp`),
the comments left out as for NFS III's (`commentedNumbers`,
`src/engines/nfs/formats/commented_text.cpp`). `reluva-tool nfs survey`
reads a sky as NFS III's, else as NFS II SE's.

The data viewer's Tracks page (`src/engines/nfs/nfs2se/pages/track_page.cpp`)
draws the 3dfx sky and fog of its main view as above (H and F turn them
off): the fog's colours and tables from the program's lists
(`src/engines/nfs/nfs2se/glide_sky.cpp`) by the camera's road point,
and Glide's table fog by the renderer's own (`DrawCall::tableFog`).
`TR04`'s tunnels black as above (`skyInTunnel`).

G shows the software program's look instead
(`src/engines/nfs/nfs2se/software_sky.cpp`): its pictures, its sky as
above, from `Tracks/pc/trNN.hrz` and the first 8 pictures, and its depth
cueing. A quad's light comes from its depth bucket: its corners' mean
depth in the view, less its list's bias (`depthBucket`, `depthCueLights`).
The quad's picture is drawn times 1 − L/16, then the file's colour times
L/16 added over it; a quad outside the buckets isn't drawn. The colours
aren't snapped to the screen's palette, and the painted lines aren't
cued. The lists' biases (0x434f40):
- 0 for the ground and extrablock 18's objects;
- 6 for the second group and 7's objects;
- 10 for the `.col`'s objects;
- 19's objects: 0, but −12 on track 2 and −16 on track 3 near its start.

Which list holds which objects is *(inferred)*. In `TR00`, compared
with the hosted software game's frames at the same road points, the
pictures, the sky's slices and the darkening agree.

## Sources and checks

- **Where the facts come from:** the files' own comments, which name
  every value; the layout counted on all of them (November 2026). How
  the game uses them from both programs, disassembled and read, and the
  software program's tables in the hosted game's memory. For the `.lgt`
  files' scale, the PlayStation's texture lighting from psx-spx's
  description of its GPU (prose, read only).
- **How it was checked:** every file split by its comments into named
  groups, each kind's counts the same on all; the readers on all 42; the
  unit tests (`nfs.unit.nfs2se`).

## Open questions

- What the `.lgt` files were for.
