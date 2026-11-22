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

How the game draws them isn't worked out.

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

## In Reluva

Read as `Sky` and `Lights` (`src/engines/nfs/nfs2se/formats/hrz.cpp`),
the comments left out as for NFS III's (`commentedNumbers`,
`src/engines/nfs/formats/commented_text.cpp`). `reluva-tool nfs survey`
reads a sky as NFS III's, else as NFS II SE's. Not drawn yet.

## Sources and checks

- **Where the facts come from:** the files' own comments, which name
  every value; the layout counted on all of them (November 2026).
- **How it was checked:** every file split by its comments into named
  groups, each kind's counts the same on all; the readers on all 42; the
  unit tests (`nfs.unit.nfs2se`).

## Open questions

- How the game draws the sky and uses the depth cueing and light palettes.
