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

## EA's other schemes: not read yet

Two files, in the install and again on the CD, start `32 FB` instead:
`trk007/tr070_8.qfs` and `tr071_8.qfs`, the Summit's 8-bit texture sets.
That header belongs to another of EA's compression schemes (the
`30 FB`/`32 FB`/`34 FB` family, Huffman-based by the public descriptions),
not RefPack.

Reluva reports such data as "compressed with an EA scheme Reluva doesn't
read yet", and `reluva-tool nfs-survey` lists them apart from failures.
Summit's 16-bit sets (`tr070.qfs`, `tr071.qfs`) are RefPack and read
fine. The 8-bit sets would matter only if the 8-bit textures are needed.
That's to be decided once it's known what uses them (see
[fsh.md](fsh.md)).

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
