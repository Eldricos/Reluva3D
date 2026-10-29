# Clue: Murder at Boddy Mansion: resource files

Source: the files of the retail CD (version 2.0, US and UK data), a clean
install of it and the 2.2 patch, studied byte by byte (October 2026). All
153 `PACK` files were unpacked, and the images decoded and compared with
the game's look: Miss Scarlet's walk, the ballroom's couch and the team
photo of the credits came out right. Nothing here comes from the
executables yet.

All numbers are little-endian.

## `PACK`: a compressed file

`.res` (images) and `.a` (animations) are stored compressed:

| Offset | Size | |
|---|---|---|
| 0 | 4 | `PACK` |
| 4 | 4 | size of the compressed data |
| 8 | 4 | size once uncompressed |
| 12 | | zlib stream (with its 2-byte header, `78 DA`) |

Each file is exactly 12 bytes plus the compressed size, and each unpacks
to the size given. `.o` (objects) and `.lvl` (the mansion) aren't packed:
they start with `DDRV` straight away.

## `DDRV`: a list of records

| Offset | Size | |
|---|---|---|
| 0 | 4 | `DDRV` |
| 4 | 4 | size of what follows the 12-byte header (not always: see below) |
| 8 | 4 | number of records |
| 12 | | the records |

In `.res` and `.a` files each record starts with a 4-character tag, a
version (`0x200`) and its size, not counting these 12 bytes:

| Tag | In | |
|---|---|---|
| `EIMG` | `.res` | an image |
| `ANIM` | `.a` | an animation: a name, the names of its frames, their order and timing |

The `DDRV` size field of `.res` files is smaller than the data; the
records are walked by their own sizes instead. `clue.lvl` is a `DDRV` of
named files: `/Info.dat`, `/Objects.dat`, `/Workspace.dat`, `/World.dat`.

## `EIMG`: an image

There are two layouts. The run-length one below holds 6,852 of the
images: all the mansion, its objects and the characters. The other, in
the fonts and part of the interface (`18ptup`, `largeu`, `ticker`,
`main_anim`, ...), starts with a 16-bit width and height and 16 (bits per
pixel), and seems to hold its pixels unpacked; it's still to be worked
out.

Offsets from the start of the record (its tag):

| Offset | Size | |
|---|---|---|
| 12 | 4 | 2 (the image type, the only one seen) |
| 16 | 4 | width |
| 20 | 4 | height |
| 24 | 1 | 3 |
| 25 | 4 | bytes per row of the original surface: (width rounded up to even) × 2 |
| 29 | 24 | not understood yet (zeros, the size again, `00 FF 00 00`) |
| 53 | 4 | size of the pixel data |
| 57 | | pixel data |

The pixels are 16-bit RGB565, run-length coded row by row. For each row,
a series of codes:

- `0x80`: end of the row.
- `0x81`-`0xFF`: skip (code & 0x7F) transparent pixels.
- `0x01`-`0x7F`: that many pixels follow, 2 bytes each.

This reads each of the 6,852 images to the last byte of its data. The
executable's `ImageRLE16.dll` draws these images (its exports:
`DecodeByteBlit16` with clipped, flipped, scaled and sheared variants).

## Names

Resource names are `file:Xname`: the file without extension and a letter
for the kind, then the name. `I` is an image, `A` an animation, `O` an
object. For example `c_scarlet:Imsidlee_01` is the first frame of Miss
Scarlet's idle animation; `ballroom:Oblcouch_ad` an object of the
ballroom.

## `ANIM`

Seen so far: a name (`c_scarlet:...`), the frame images by name, the
frame order, and an `ISOA` block (isometric animation) with, per frame, an
offset from the anchor point (for Miss Scarlet −44, −106) and a time (100,
in milliseconds presumably). To be worked out in full.
