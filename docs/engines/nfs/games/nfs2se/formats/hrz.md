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

The software program reads its sky as `%sTr%02d.hrz` (0x4719a0), the
3dfx program `%s3Tr%02d.hrz` (`NFS2SEA.EXE` 0x42ce30). **Neither reads
the `.lgt` files**: neither program has the name or a pattern making it,
the hosted game opens only the `.hrz` for a race, and the 8 files hold
the same numbers. What they were for isn't known.

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

## How the game uses them

Addresses are `nfs2sen.exe`'s, or `NFS2SEA.EXE`'s marked [A]. From the
code, and the software program's tables matched in memory on the hosted
game at `TR00` (November 2026).

### The software sky (0x470eb0)

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
- **Black horizon** (or the horizon turned off, 0x4716d0) fills the view
  with one polygon instead: black, or the sky's colour.

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
  plus 10, in halves of the unit (0x45c5f0). A distance d is about
  d / 2 − 10 units away: `TR00` starts darkening about 115 units out
  *(the unit inferred)*.
- **Each frame** (0x46f330) a marker goes in at each change's bucket;
  drawing far to near, it sets the light for the polygons after it.
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

### The 3dfx sky ([A] 0x42c3c7)

- **The colours at the sun and away from it** are laid round the ring,
  not by the player's heading: the 16 ring points' colours go from the
  sun's at the file's angle toward the other, `sun + (opposite − sun) ×
  k / 8` for k to 7, and back the same way (0x42d0a3), so the far side
  reaches 7/8 of the way. They're the shaded sky's colours at its
  midpoint row, the top's above it; the earth's two below, drawn in one
  of the program's views only (0x42c470).
- **The pixmap:** 8 slices as the software's, each as tall as its
  picture against the tallest.
- **The cloud dome** (0x42be50): a 13 × 13 grid, x and z from −1,000 to
  800 by 150, at `offset + height × cos(2π r / 4000)` for r its distance
  from the middle, with `sky.fsh`'s `CLDn` pictures; on tracks 2 to 7
  only (a table, 0x4eda10).
- **The clear colour** is the sky's top, or black with black horizon.

### The 3dfx fog: the program's own

Nothing of the `.hrz` fogs the 3dfx game. For each track the program
sets Glide's table fog (`grFogMode`), a colour, and 20 tables
(`guFogGenerateExp`) of densities from d0 to d1 (0x42b284):

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

Each frame it picks a table, and on some tracks a colour, by the
camera's place along the road (0x427690): `TR00`'s colour rising from
(6, 6, 6) to (62, 62, 62) over a stretch, `TR04`, `TR05` and `TR07`
denser in places, by lists in the code.

## In Reluva

Read as `Sky` and `Lights` (`src/engines/nfs/nfs2se/formats/hrz.cpp`),
the comments left out as for NFS III's (`commentedNumbers`,
`src/engines/nfs/formats/commented_text.cpp`). `reluva-tool nfs survey`
reads a sky as NFS III's, else as NFS II SE's. Not drawn yet.

## Sources and checks

- **Where the facts come from:** the files' own comments, which name
  every value; the layout counted on all of them (November 2026). How
  the game uses them from both programs, disassembled and read, and the
  software program's tables in the hosted game's memory.
- **How it was checked:** every file split by its comments into named
  groups, each kind's counts the same on all; the readers on all 42; the
  unit tests (`nfs.unit.nfs2se`).

## Open questions

- What the `.lgt` files were for.
- The units of the depth buckets for certain, and which of the 3dfx
  program's views draws the earth and takes the denser fog.
