# Star Trek: Armada: ZFS archives (`.zfs`)

> **Status:** decoded · **Code:** `src/engines/storm3d/formats/zfs_archive.cpp` · **Checked on:** the English retail 1.0 install's two archives, all 596 files · **Confidence:** confirmed unless marked

## Overview

Two archives hold the game's object definitions and mission texts:

| Archive | Files | What's in them |
|---|---|---|
| `assets.zfs` | 515 | 488 object definitions ([odf.md](odf.md)) and 27 tech trees (`.tt`) |
| `objtxt.zfs` | 81 | The missions' objectives, as text (`b1ob1.txt`...) |

Every file in them is text once decoded. The format has the magic
`ZFSF`, as Interstate '76's archives are said to *(published)*; whether
theirs are laid out the same isn't checked.

## Layout

Little-endian throughout.

### Header

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `ZFSF` |
| 4 | 4 | Version: 1 |
| 8 | 4 | The length of a name in a table entry: 16 |
| 12 | 4 | Entries in each table block: 100 |
| 16 | 4 | The number of files |
| 20 | 4 | The key the files are XORed with ([Encoding](#encoding)): `2e8ed45b` in `assets.zfs`, `2567512e` in `objtxt.zfs` |
| 24 | 4 | Where the first table block starts: 28, just after the header |

That the name length and the block size are read from the header, rather
than fixed, is *(inferred)*: both archives have the same values.

### Table blocks

A block is the offset of the next block (0 for the last), then the
header's number of entries, used or not. Each block is followed by the
data of its files, in order, and then the next block: `assets.zfs` has
six blocks, the last 15 entries used, and `objtxt.zfs` one.

| Offset | Size | Field |
|---|---|---|
| 0 | 16 | The name, zero-padded; one, `testobjectives.t`, fills all 16 bytes with no zero |
| 16 | 4 | The file's offset from the start of the archive |
| 20 | 4 | The entry's index, from 0, counting across blocks |
| 24 | 4 | The file's size |
| 28 | 4 | The file's time: Unix time, 10 March 2000 for every file *(inferred)*: it's the day `Armada.exe` was built |
| 32 | 4 | 0 in every entry. Unused entries are all zero |

The header, the blocks and the files' data add up to each archive's size,
with no gaps.

### Encoding

Each file is XORed with the header's key, 4 bytes at a time from the
file's start, the key's low byte first. The last `size % 4` bytes aren't
encoded: they're the plain text's last bytes, typically a line's `\r\n`.

## In Reluva

`ZfsArchive` (`src/engines/storm3d/formats/zfs_archive.h`) reads an
archive's tables at `open()`, and decodes a file at each `read()`; it's an
`Archive`, so it can be searched with the install's folders. It refuses
an entry whose last field isn't 0, as no archive has one.

- `reluva-tool storm3d zfs-list <file.zfs>`: each file's index, offset,
  size, time, last field and name, and the key.
- `reluva-tool storm3d zfs-extract <file.zfs> <output dir> [name filter]`:
  the files, decoded.

## Sources and checks

- **Where the facts come from:** the two archives, read byte by byte: the
  header's fields from their values against the tables' sizes; the key
  from runs of spaces in `objtxt.zfs`'s texts, which come out as the
  header's own bytes repeated.
- **How it was checked:** every file of both archives decodes to text
  with no control characters (the only bytes over 127 are the Windows
  quotation marks `0x91` and `0x92`); `zfs-extract`'s output was compared
  with a separate script's. The unit tests (`storm3d.unit.zfs`) build
  archives over two blocks, with a 16-character name and a size that
  isn't a multiple of 4. No published description of the format was
  used.

## Open questions

- What a non-zero last field would mean: compression, perhaps
  *(guessed)*. `Armada.exe` holds the LZO compression library (its
  copyright notice is in it), but no file here is compressed.
