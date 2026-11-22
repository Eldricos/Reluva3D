# EA's picture banks (`.fsh`, "SHPI")

## What it is

Electronic Arts' banks of pictures ("shapes"), as its games of the late
1990s and after keep their textures and front-end art: a directory of
named shapes, each a chain of blocks, the picture first, then what's
attached to it, such as a palette or text. A bank may be compressed whole
with RefPack ([refpack.md](../refpack.md)), as Need for Speed III's
`.qfs` files are. The code is `src/image/fsh.cpp` (`parseShapeBank`,
`decodeShape`).

## What Reluva supports

- Pictures of 8 bits (palette indices), 16 bits (5:6:5 and 1:5:5:5), 24
  bits and 32 bits, decoded to RGBA.
- Palettes of 16 (1:5:5:5), 24 and 32 bits attached to a picture.
- Text blocks, kept as text; other attached blocks kept as they are, for
  a game's own code to read.
- A whole bank compressed with RefPack or EA Huffman.
- An 8-bit picture without a palette is read, but not decoded.

## The format

All values are little-endian.

### Header and directory

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | `SHPI` |
| 4 | u32 | The file's size |
| 8 | u32 | How many shapes |
| 12 | 4 | The directory's id: `GIMX` in Need for Speed III's |
| 16 | 8 × n | Per shape: a 4-character name, then a u32 offset |

### Blocks

A shape is a chain of blocks. Each block starts with:

- a type byte;
- a 3-byte offset to the next block, 0 for the last. The last block runs
  to the next shape, or to the end of the file.

The first block is the picture. Its header is 16 bytes:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 1 | Type (below) |
| 1 | 3 | Offset to the next block |
| 4 | u16 | Width |
| 6 | u16 | Height |
| 8 | s16 | Centre x |
| 10 | s16 | Centre y |
| 12 | s16 | Screen x |
| 14 | s16 | Screen y |

The centre (a hot spot) and the screen position are for front-end art
and sprites. Both may be negative: a 640×480 picture cut into 256×256
tiles gives its tiles centres of −256 and −512.

The pixels follow: width × height of them, rows top to bottom, with no
padding between rows. The block may have 0 to 7 bytes of padding at its
end.

| Type | Pixels |
|---|---|
| `0x7B` | 8-bit palette indices |
| `0x7E` | 16-bit, bit 15 alpha, then 5:5:5 R, G, B |
| `0x78` | 16-bit 5:6:5 R, G, B |
| `0x7D` | 32-bit, bytes B, G, R, A |
| `0x7F` | 24-bit, bytes B, G, R |

Attached blocks after the picture:

| Type | Meaning |
|---|---|
| `0x2D` | Palette, 16-bit 1:5:5:5; a 16-byte header like a picture's, its width the number of colours (up to 256) |
| `0x24` | Palette, 24-bit B, G, R |
| `0x2A` | Palette, 32-bit B, G, R, A |
| `0x6F` | Text: a u32 length at offset 4, the text at 8 |

The alpha bit of a 16-bit palette's colours varies from colour to
colour, so it carries transparency, as in 16-bit pictures. Other block
types are a game's own: Need for Speed III's cockpit gauges (`0x7C`), for
one.

## Sources and checks

- **Where the facts come from:** the Need for Speed modding community's
  FSH notes, for the overall layout; the record types, block sizes,
  palettes and attached blocks counted on Need for Speed III's files with
  survey scripts (October 2026).
- **How it was checked:** every picture bank of Need for Speed III's
  install and CD (694 banks, 15,990 pictures) read to its end; the
  colours checked by eye on decoded pictures. The unit tests
  (`common.unit.fsh`, `tests/common/fsh.cpp`) read banks built in the
  test: every picture type, palettes, text, a compressed bank and damage.
- **Confidence:** confirmed unless marked.

## Used by

- **`nfs`**: Need for Speed III's textures, front-end art and cockpits
  ([fsh.md](../../engines/nfs/games/nfs3/formats/fsh.md)).
