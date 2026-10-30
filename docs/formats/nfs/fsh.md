# Need for Speed: `.fsh` pictures ("SHPI")

Status: **implemented** (`src/engines/nfs/fsh.cpp`). It reads all 694
banks (15,990 pictures) in the NFS III install and on its CD, including
those inside `.viv` archives and compressed as `.qfs`
([refpack.md](refpack.md)).

Sources:

- the NFS modding community's FSH notes, for the overall layout;
- the record types, block sizes, palettes and attached blocks, counted on
  the retail files with survey scripts (October 2026);
- the colours, checked by eye on decoded pictures: the title screen, car
  art, cockpits and track textures.

All values are little-endian.

## Header and directory

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | `SHPI` |
| 4 | u32 | file size (equal to the file's in all 694) |
| 8 | u32 | number of shapes |
| 12 | 4 | directory id: `GIMX` in every NFS III file |
| 16 | 8 × n | per shape: 4-character name, u32 offset |

## Blocks

A shape is a chain of blocks. Each block starts with:

- a type byte;
- a 3-byte offset to the next block, 0 for the last block. The last
  block runs to the next shape, or to the end of the file.

The first block is the picture. Its header is 16 bytes:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 1 | type (below) |
| 1 | 3 | offset to the next block |
| 4 | u16 | width |
| 6 | u16 | height |
| 8 | s16 | centre x |
| 10 | s16 | centre y |
| 12 | s16 | screen x |
| 14 | s16 | screen y |

The centre (hot spot) and the screen position are used by front-end art
and sprites. Both may be negative: a 640×480 picture cut into 256×256
tiles gives its tiles centres of −256 and −512.

The pixels follow: width × height of them, rows top to bottom, with no
padding between rows. The block may have 0 to 7 bytes of padding at its
end.

Counts are over the install and the CD together.

| Type | Pixels | Retail count |
|---|---|---|
| `0x7B` | 8-bit palette indices | 6,376 |
| `0x7E` | 16-bit, bit 15 alpha, then 5:5:5 R, G, B | 7,114 |
| `0x78` | 16-bit 5:6:5 R, G, B | 1,358 |
| `0x7D` | 32-bit, bytes B, G, R, A | 1,140 |
| `0x7F` | 24-bit, bytes B, G, R | 2 |

Attached blocks after the picture:

| Type | Meaning | Retail count |
|---|---|---|
| `0x2D` | palette, 16-bit 1:5:5:5; 16-byte header, width = number of colours (up to 256) | 6,306 |
| `0x2A` | palette, 32-bit B, G, R, A | 70 |
| `0x6F` | text: u32 length at offset 4, the text at 8 | 266 |
| `0x7C` | a u32 count, then count (x, y) pairs of u32: the cockpit's gauges (below) | 96 |

- **Palettes.** Every 8-bit picture has its own palette; no file has a
  shared `!pal` entry. The alpha bit in 16-bit palettes varies from
  colour to colour, so it carries transparency, as in 16-bit pictures.
- **Text.** `<additive>` (sometimes `additive` or with a trailing CRLF)
  marks textures drawn additively. Strings such as `A200 B45 R25 ROTATE`
  or `A0 B45 R30 INFRONT NOADD` sit on the sun pictures of each track's
  `sky.fsh` (`SUND`, `SUNN`, `SUNW`: day, night, weather) and on similar
  sprites. On the sun, from the game's code ([hrz.md](hrz.md), What the
  game does with it):
  - `A` and `B`, in degrees, place it at (cos A, cos B, sin A) × 3000;
  - `R` × 10 is how far its corners are from its centre;
  - `ROTATE` turns it as it crosses the screen, `NOADD` blends it
    rather than adding it, and `INFRONT` draws it after the clouds.
- **Gauges** (`0x7C`). On picture `0000` of each car's `dash.qfs`: 12
  pairs, read by nfs3.exe (finder `0x4f2460`; the cockpit loader
  `0x488ad0` copies 12 pairs to `0x792030` whatever the count; needles
  drawn at `0x4880b0`). As 24 u32, in a 640×480 frame (scaled to the
  screen):

  | Values | Meaning |
  |---|---|
  | 0, 1 | a texel of the dash picture giving the needles' colour |
  | 2 | the needle's width at its base (the tip is half) |
  | 3 | not read |
  | 4-13 | the speedometer: centre x, y; the hub's radius (the needle starts there); units (1 km/h, 0 mph); the lowest and highest reading; then p0 x, y and p1 x, y |
  | 14-23 | the tachometer, the same; its units value isn't read, and its readings are thousands of rpm |

  At the lowest reading the needle points from the centre toward p0, at
  the highest toward p1, sweeping clockwise between them; its length is
  the mean of the two distances. A gauge whose p0 equals p1 isn't drawn
  (`merc` has all zeros: no gauges). Checked on all 17 cars: `amd7`'s
  speedometer reads 0-170 mph about (384, 407), its tachometer 0-7
  about (319, 407), and the colour texels are needle-coloured (white,
  orange on `corv`, amber on `diab`). The F355's gear pictures (`geaR`
  to `gea6`) carry 7 pairs too, the same on each; nothing reads them.
  The Cockpits page draws the needles so. These parts are Reluva's own
  choices, not read from the game: a reading past the range stops at its
  end; a needle is a flat quad in the texel's colour; and the steering
  wheel (picture `0001`) goes with its centre at its screen position.
- **Additive.** The picture loader (`0x4d40c3`, `0x4d42e8`) looks for
  `<additive>`. The bare `additive` on picture `0064` of `tr000` and
  `tr001` doesn't match it, so those aren't drawn additively. The sun's
  text is read only by the sky loader (`0x47a970`); it knows no
  keywords but `A`, `B`, `R`, `ROTATE`, `NOADD` and `INFRONT`.
- **`sky.fsh`'s other pictures.** `CLDD`, `CLDN`, `CLWD` and `CLWN` are
  the cloud tiles. `CHRD`, `CHRN`, `CHRW` and `CHNW` are sphere maps, a
  ball's worth of sky, ground and horizon: the cars' reflections. The
  horizon picture is in the track's texture sheet ([hrz.md](hrz.md)).

Of the install's 16-bit 1:5:5:5 pictures, 2,897 are fully opaque, 636 mix opaque and
clear pixels, and 24 are entirely clear.

## Track texture sets

Each track has two sets of each texture file:

- `trNNN.qfs` holds 16-bit 1:5:5:5 textures (plus one 32-bit one).
- `trNNN_8.qfs` holds the same textures, at the same sizes and in the
  same order (checked on `tr000`), as 8-bit pictures, each with its own
  palette.

The 8-bit sets are the software renderer's: with `softtria.dll` the game
loads `trNNN_8.qfs` (the renderer's number decides, `0x7a3a58`; see
[hrz.md](hrz.md)). The Summit's two 8-bit sets are compressed with EA's
Huffman scheme ([refpack.md](refpack.md)). The Tracks page shows them
with `X`, after the two 16-bit sets.

## Tools

- `reluva-tool fsh-info <file> [name in .viv]` lists the shapes.
- `reluva-tool fsh-dump ... <dir>` writes them as TGA files.
