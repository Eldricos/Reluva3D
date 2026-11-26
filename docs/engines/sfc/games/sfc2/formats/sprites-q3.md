# Star Trek: Starfleet Command II: the interface's pictures (`sprites.q3`)

> **Status:** partial · **Code:** `src/engines/sfc/formats/sprite_archive.cpp` · **Checked on:** English retail 1.0, its one file (19,353 images) · **Confidence:** confirmed unless marked

## Overview

`Assets/Sprites/sprites.q3` (78,253,026 bytes in 1.0) holds every
picture of the interface: screens' backgrounds at 640x480, 800x600 and
1600x1200, buttons in each of their states, icons, the map's symbols,
ships' outlines for the systems display. The images lie back to back
from the start of the file; after them, a section of the interface's
objects (buttons, groups, animations) refers to each image by its size
and its offset. The images are read; the objects only as far as the
names some of them give their images.

## Layout

All numbers are little-endian.

### The file's header

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `MDM!` |
| 4 | 2 | 0x99 (153), the same number that tags each object record, so perhaps a version *(inferred)* |
| 6 | 2 | 0x100 |
| 8 | 2 | 0x10 |
| 10 | 4 | where the objects' section starts (75,464,798 in 1.0) |

The first image's header follows at 14.

### An image

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | width |
| 2 | 2 | height |
| 4 | 2 | x of a point in it, from its left: half the width in 18,580 of the 19,353; perhaps the point the image is drawn at *(guessed)* |
| 6 | 2 | y of that point, from its top |
| 8 | 2 | 0 in every image |
| 10 | 2 | bits a pixel: 16 (18,935 images) or 8 (418) |
| 12 | 2 | 255 in every image |
| 14 | 2 | 2 in every image |
| 16 | 4 | the size of the rest, after these 20 bytes |
| 20 | 512 | 8-bit images only: their palette, 256 colours in RGB555 |
| then | 4 x height | where each row's codes start, from the end of this table, in the codes' units (16-bit words, or bytes for an 8-bit image) |
| then | | the rows' codes, to the image's end |

The next image starts straight after: header, size field and the 20
bytes add up to the end of the last image, which is where the objects'
section starts.

### A row's codes

Each row is a run of codes, which cover its width exactly. In a 16-bit
image a code is 16 bits, its top two bits the kind and the other 14 a
count; in an 8-bit image a code is a byte, two bits and a 6-bit count.

| Kind (top bits) | What follows | Meaning |
|---|---|---|
| 0 | count pixels | the pixels, as they are |
| 1 | one pixel | the pixel, count times |
| 3 | nothing | count pixels left transparent |

Kind 2 isn't used. A pixel is RGB555 (bit 15 unused, then 5 bits each
of red, green and blue), or, in an 8-bit image, a byte indexing its
palette.

### The objects

From the header's offset to the end: the section's size (to the file's
end: 2,788,228 bytes in 1.0) and its count of records (32,789), then the
records. Each starts the same way:

| Offset | Size | What |
|---|---|---|
| 0 | 2 | its kind |
| 2 | 4 | its number: 1, 2, 3... in the file's order |
| 6 | 2 | 0x99 |
| 8 | 4 | its name's length (0 for none) |
| 12 | n | the name |

What follows depends on the kind, and isn't worked out but for an
image's own record (kind 1): four zero bytes, then the image's size (as
counted from its header's start) and offset, then 0xFF. A record runs
to the next one's start, found by its number. The kinds in 1.0, with
what their names suggest:

| Kind | Records | Named | Names (for example) | What, by the names *(guessed)* |
|---|---|---|---|---|
| 1 | 19,353 | 6,205 | `MD_BlackHole_bmp`, `NonSelectBtn` | an image, one each |
| 3 | 1,693 | all | `campaign640x480`, `MapIcons` | a screen or a set of icons |
| 8 | 2,143 | 1,378 | `CompassMapUp` | a button |
| 9 | 53 | 45 | `MetaChat` | a chat or text area |
| 10 | 4,649 | 3,093 | `MD_BlackHole`, `MD_Nebula` | an animation or picture of the map |
| 11 | 176 | 122 | `MetaSideBarListScrollBar` | a scroll bar |
| 15 | 87 | 70 | `C_metalistbox`, `MetaStatsText` | a text or list box |
| 17 | 1,989 | all | `MetaSideBarRadio` | a set of radio buttons |
| 21 | 2,102 | 497 | `MetaSideBarList`, `cTurnNumber` | a list or scroll box |
| 25 | 544 | all | `t%Banner_AREA_0` | a template |

2.0.3.6's file has 33,056 records of the same ten kinds.

## In Reluva

- **Reading it:** `SpriteArchive` (`src/engines/sfc/formats/sprite_archive.h`)
  lists the images, finds the objects' records (kind, number, name,
  where and how long), names the images from their own, and decodes one
  as RGBA32, its skipped pixels transparent.
- **The data viewer:** the Images page shows every image, with its header
  and name.
- **Commands:** `reluva-tool sfc sprites <game folder>` lists the images
  and decodes each, and counts the objects by kind; `reluva-tool sfc
  sprite-png <game folder> <index>
  <out.png>` writes one as a PNG.

## Sources and checks

- **Where the facts come from:** the 1.0 file itself. The header's last
  field matched the end of the last image; the row codes were worked out
  on a 116x29 button whose border is a grey ramp (0x2108, 0x294A, 0x318B:
  equal components only as RGB555), and the 8-bit images' palettes from
  their first 512 bytes, all greys and colours in the same encoding.
- **How it was checked:** every one of the 19,353 images decoded, each
  row ending exactly where the next begins and covering the image's
  width; a 640x480 screen and an 8-bit ship's outline written as PNGs
  and seen to be right (brushed metal and copper; a white outline on
  black).
- **Unit test:** `sfc.unit.formats` (sprites, sprite overrun), on images
  built in the test.

## Open questions

- The header's 0x100 and 0x10, and the image header's 0, 255 and 2.
- What the point at 4 and 6 is for, where it isn't the centre.
- The object records' fields past their names, for each kind but an
  image's: which images make each button's states and each animation's
  frames, where each goes on its screen, and what the 2.0.3.6 patch's
  copy changes in them.
