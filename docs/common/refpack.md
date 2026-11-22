# EA's compression: RefPack and EA Huffman

## What it is

Electronic Arts' LZ77 compression, RefPack, which its games of the late
1990s and after use for their files (Need for Speed III's `.qfs`
pictures, the Theme Park World engine's archives), and EA's Huffman
scheme, which shares its kind of header. The code is
`src/common/refpack.cpp` (`decompressRefPack`, `decompressEaHuffman`).

## What Reluva supports

- RefPack with either size width, with or without the compressed size.
- EA Huffman's `30 FB`, `32 FB` and `34 FB` (with `31`, `33`, `35`, their
  extra field skipped).
- `unpackIfRefPack` passes other data on unchanged, and fails for EA's
  other schemes (another first byte before `FB`), which aren't read.
- Damage is caught: a stream without its end, a copy from before the
  start, or a result not the size the header gives.

## The format

RefPack: a short header, then commands that copy literal bytes and runs
from earlier in the output.

### RefPack's header

| Offset | Size | Meaning |
|---|---|---|
| 0 | 1 | flags: `0x10`, plus `0x80` for 4-byte sizes, `0x01` if a compressed size comes first |
| 1 | 1 | `0xFB` |
| 2 | 3 or 4 | (with flag `0x01`: the compressed size, then) the uncompressed size, big-endian |

The files seen so far all start `10 FB`: a 3-byte size, no compressed
size.

### RefPack's commands

Each command copies some literal bytes from the input, then copies a run
of bytes from earlier in the output. The copy may overlap what it's
writing, which repeats a pattern.

| First byte | Bytes | Literals | Copy length | Copy distance |
|---|---|---|---|---|
| `0x00`-`0x7F` | 2 | `b0 & 3` | `((b0 >> 2) & 7) + 3` | `((b0 & 0x60) << 3) + b1 + 1` |
| `0x80`-`0xBF` | 3 | `b1 >> 6` | `(b0 & 0x3F) + 4` | `((b1 & 0x3F) << 8) + b2 + 1` |
| `0xC0`-`0xDF` | 4 | `b0 & 3` | `((b0 & 0x0C) << 6) + b3 + 5` | `((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1` |
| `0xE0`-`0xFB` | 1 | `((b0 & 0x1F) << 2) + 4` | none | |
| `0xFC`-`0xFF` | 1 | `b0 & 3` | none: the end | |

The literals follow the command's bytes.

### EA Huffman (`30 FB`, `32 FB`, `34 FB`)

Another of EA's schemes, with the same kind of header; Need for Speed
III has two files in it.

**How it was worked out.** No public description of the scheme turned
up, so it comes from the game's own routine (`nfs3.exe` at `0x50ed50`,
called with the data in EAX, the output in EDX and 1 in EBX, Watcom's
register convention; it returns the size). `reluva-tool nfs huff-check`
runs that routine in the interpreter on every such file and compares
Reluva's decoder with it: both files come out the same, byte for byte
(509,000 and 558,640 bytes, each a `SHPI` sheet of 191 8-bit pictures).

**The stream** is read a bit at a time, most significant first, from the
start of the file:

1. The type, 16 bits (`0x32FB`), and the size, 24 bits. With bit 8 of
   the type set (`31`, `33`, `35`), another 24 bits follow, which the
   game skips (no file here has one).
2. The escape symbol, 8 bits.
3. For code lengths 1, 2, 3..., how many codes have that length. The list
   ends at a length that has some, once the codes fill 16 bits (the
   next code, shifted to 16 bits, is 0x10000).
4. The symbols, in code order. Each is a number n: the symbol is the
   (n + 1)th byte value not used yet after the one before, counting on
   from 255 to 0 and round.
5. The data: canonical Huffman codes (shorter codes first, each length's
   codes counting up from twice the last length's end). A symbol is a
   byte, unless it's the escape symbol, which is followed by a number n:
   - n > 0: the last byte again, n times;
   - n = 0, then a bit 1: the end;
   - n = 0, then a bit 0: the next 8 bits as a byte (the escape value
     itself, say).
6. For `32 FB`, each byte then becomes the running sum of the bytes so
   far (the data was stored as differences); for `34 FB`, that twice.
   `30 FB` is as decoded.

**Numbers** (counts, symbols, repeats) are written as z zero bits, a 1,
then z + 2 bits v; the number is v + 2^(z+2) − 4. So `1xx` is 0 to 3,
`01xxx` is 4 to 11, and so on.

Other second bytes `FB` (not RefPack, not these) are EA's other
schemes, which Reluva doesn't read.

## Sources and checks

- **Where the facts come from:** RefPack's layout is from the public
  descriptions of it (MultimediaWiki and the Need for Speed modding
  community's QFS notes). No public description of EA Huffman turned up:
  it was worked out from Need for Speed III's own routine, as
  [EA Huffman](#ea-huffman-30-fb-32-fb-34-fb) says.
- **How it was checked:** on all 588 compressed files of Need for Speed
  III's install and CD: each decompresses to exactly the size its header
  gives, uses up all its input, and gives an `.fsh` whose own size field
  matches. The unit tests (`common.unit.refpack`,
  `tests/common/refpack.cpp`) decode streams written by hand: every
  command form, a copy from 17,000 bytes back, the header flag `0x01`,
  EA Huffman as it is and as differences, and damaged streams.
- **Confidence:** confirmed unless marked.

## Used by

- **`tpw`**: SimCoaster's archives
  ([wad.md](../engines/tpw/games/simcoaster/formats/wad.md)).
- **`nfs`**: Need for Speed III's `.qfs` pictures and its two EA Huffman
  texture sets ([fsh.md](../engines/nfs/games/nfs3/formats/fsh.md)).
