# Need for Speed: RefPack compression (`.qfs`)

Status: **implemented** (`src/engines/nfs/refpack.cpp`). It was checked
on all 588 compressed files in the NFS III install and on its CD. Each
decompresses to exactly the size its header gives, uses up all its input,
and gives an `.fsh` whose own size field matches.

Sources: the public descriptions of EA's RefPack (MultimediaWiki and the
NFS modding community's QFS notes), checked against the retail files as
above.

## Header

| Offset | Size | Meaning |
|---|---|---|
| 0 | 1 | flags: `0x10`, plus `0x80` for 4-byte sizes, `0x01` if a compressed size comes first |
| 1 | 1 | `0xFB` |
| 2 | 3 or 4 | (with flag `0x01`: the compressed size, then) the uncompressed size, big-endian |

All 588 retail RefPack files start `10 FB`: 3-byte size, no compressed
size.

## EA Huffman (`30 FB`, `32 FB`, `34 FB`)

Two files, in the install and again on the CD, start `32 FB` instead:
`trk007/tr070_8.qfs` and `tr071_8.qfs`, that track's 8-bit texture sets.
They're read since October 2026 (`decompressEaHuffman`, `refpack.cpp`).

**How it was worked out.** No public description of the scheme turned
up, so it comes from the game's own routine (`nfs3.exe` at `0x50ed50`,
called with the data in EAX, the output in EDX and 1 in EBX, Watcom's
register convention; it returns the size). `reluva-tool nfs-huff-check`
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

Other second bytes `FB` (not RefPack, not these) are reported as
"compressed with an EA scheme Reluva doesn't read yet"; none are in NFS III.

## Commands

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

## Tests

`tests/test_nfs.cpp` decodes streams written by hand:

- every command form;
- a copy from 17,000 bytes back;
- the header flag `0x01`;
- damaged streams (no end code, wrong size, a copy from before the
  start).
