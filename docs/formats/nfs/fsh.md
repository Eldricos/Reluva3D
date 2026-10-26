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
| `0x7C` | a u32 count, then values: hot spots on cockpit pictures (`dash.qfs`); not decoded yet | 96 |

- **Palettes.** Every 8-bit picture has its own palette; no file has a
  shared `!pal` entry. The alpha bit in 16-bit palettes varies from
  colour to colour, so it carries transparency, as in 16-bit pictures.
- **Text.** `<additive>` (sometimes `additive` or with a trailing CRLF)
  marks textures drawn additively. Strings such as
  `A0 B45 R24 ROTATE` or `A0 B45 R30 INFRONT NOADD` sit on what look like
  light flares; their meaning is to be worked out.

Of the install's 16-bit 1:5:5:5 pictures, 2,897 are fully opaque, 636 mix opaque and
clear pixels, and 24 are entirely clear.

## Tools

- `reluva-tool fsh-info <file> [name in .viv]` lists the shapes.
- `reluva-tool fsh-dump ... <dir>` writes them as TGA files.
