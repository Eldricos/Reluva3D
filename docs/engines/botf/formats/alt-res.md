# Star Trek: Birth of the Federation: full-screen pictures (`alt.res`)

> **Status:** decoded · **Code:** `src/engines/botf/formats/alt_res.cpp` · **Checked on:** the English retail `alt.res`: all 31 files decoded and shown (October 2026); its checks and padding, with 18 of the user's saved games' (November 2026) · **Confidence:** confirmed unless marked

## Overview

`alt.res` is an archive of 31 TGA images, each LZSS-compressed. Not a zip,
unlike [`stbof.res`](stbof-res.md). The saved games are the same archive
([sav.md](sav.md)). Every image is a full-screen 800x600
picture. They are presumably backgrounds in each race's style, an
"alternate" set to `stbof.res`'s; which screens use them is for the running
game to show *(guessed)*.

## Layout

All values little-endian.

| Offset | Type | Field |
|---|---|---|
| 0 | `char[16]` | `"alt.res"`, zero-padded |
| 16 | `u32` | directory offset (12,982,522) |
| 20 | `u32` | number of files (31) |
| 24 | | the files' compressed data, one after the other |
| | | directory: 40 bytes per file |
| | | zeros, to make the file the header and a whole number of 16 KB blocks (8,774 bytes) |

A directory entry:

| Offset | Type | Field |
|---|---|---|
| 0 | `char[16]` | name, zero-padded, e.g. `"cicon800.tga"` |
| 16 | `u32` | offset of the compressed data, from the start of the file |
| 20 | `u32` | index: 0 to 30, the entry's own number |
| 24 | `u32` | decoded size: 960,018 or 960,044 |
| 28 | `u32` | compressed size |
| 32 | `u32` | flag: 1 in every entry (compressed?) *(guessed)* |
| 36 | `u32` | check: a CRC-16 of the compressed data ([below](#the-check)) |

The data isn't stored in directory order: the last entry, `rw.tga`, comes
first at offset 24, then entries 0 to 29 in order. Each file's compressed
data ends exactly where the next one's begins, and the last ends at the
directory.

### Compression

A variant of the common 1989 LZSS (Haruhiko Okumura's), with the reference
bytes in a different order:

- Each file is compressed on its own: a 4,096-byte window, all zeros at
  the start, writing from position **1**, and a new flag byte.
- A flag byte describes the next eight items, low bit first. Bit 1: one
  literal byte. Bit 0: a two-byte reference `a b`:
  - length = `(a >> 4) + 2` (2 to 17 bytes)
  - window position = `((a & 0x0F) << 8) | b`
- Every byte output, literal or copied, is also written to the window at
  the current position, which then advances (wrapping at 4,096).
- A file ends when its decoded size (from the directory) is reached; any
  flag bits left in the last flag byte are unused, and the next file's
  data starts at the next byte.

### The check

A CRC with the polynomial 0x1021 (CRC-CCITT's), starting from 0, of the
file's bytes as stored, compressed: each byte is shifted into the
register's low end, and the byte shifted out of its top is looked up in
the CRC's table and XORed in:

```text
check = 0
for each byte b: check = ((check << 8) | b) ^ table[check >> 8]   (16 bits)
```

That's the CRC without the two zero bytes a CRC normally runs on after
the data, which is why the common CRC-16s didn't match it. Each of the 31
files' checks is so, and so is every file's in 18 of the user's saved
games (7,734 files), but for a save's `result.lst`, stored in chunks,
whose check is its chunks' added up ([sav.md](sav.md#results)).

### Contents

Every file is an uncompressed TGA: type 2, no colour map, 800x600, 16-bit
(`A1R5G5B5`), descriptor `0x01` (one attribute bit, rows from the bottom
up). 960,018 bytes, or 960,044 for the 21 that end with a 26-byte TGA 2.0
footer (`TRUEVISION-XFILE.`) with no extension or developer area (both
offsets 0). The common image reader (`src/image/image.cpp`) ignores the
footer.

The names follow the race letters (`c`, `f`, `h`, `k`, `r`, see
[stbof-res.md](stbof-res.md#races)):

| Per race | Names |
|---|---|
| all five | `?icon800.tga`, `?l.tga`, `?w.tga`, `?_ss.tga`, `?_sbg01.tga` |
| one race only | `c_oohq.tga`, `f_toc.tga`, `h_fchq.tga`, `h_fo.tga`, `k_gh.tga`, `r_tshq.tga` |

Entry 0 is `c_sbg01.tga`.

## In Reluva

`AltResArchive` (`src/engines/botf/formats/alt_res.cpp`) reads the
directory and decompresses each file (`decompressLzss`). Writing,
`compressLzss` compresses a file, `altResCheck` works out its check and
`writeAltRes` writes the archive, padded as the game's. The compression
is Reluva's own: it never refers to the window where nothing has been
written yet, so its files read the same whatever the window starts
filled with (zeros here, spaces in most of a save's), and they come to 2%
more than the game's own for the same data (the 7,734 files of the
saves). reluva-viewer's
Birth of the Federation Images page shows the 31 pictures at their own
size, and `reluva-tool botf alt-list <alt.res> [output folder]` lists the
archive and extracts its files. The unit tests (`botf.unit.archives`:
"lzss", with bytes from the retail file's start, and "alt.res") cover
reading, and `botf.unit.saving` writing ("lzss compression", "checks", with
stored files and checks from the user's saves, and "alt.res written").

## Sources and checks

- **Where the facts come from:** the first 8 KB and last 4 KB of `alt.res`
  (`tools/botf-re/botf_sample.py`), then four runs of
  `tools/botf-re/botf_probe.py` on the whole file, October 2026.
- **How the compression was worked out:**
  - The bytes from offset 24 show a flag byte (`e7`) followed by eight
    items, then another flag byte (`07`) after exactly eight, and the
    literals include `20 03 58 02 10 01`: 800, 600, 16 and 1, the end of a
    TGA header. With references read as length `(a >> 4) + 2`, the first
    two references fill exactly the 9 header bytes between `00 00 02` and
    the width; the usual Okumura order (length in `b`'s low nibble) gives
    12.
  - Long runs of references `f0 1b`, `f0 2c`, `f0 3d`, ... each copy 17
    bytes from a position 17 further on: runs of one byte value (black),
    each copied from the byte just before the write position. That pins
    the write position to 1 more than each reference's position, so
    writing starts at 1. (Not at `0xFEE`, as Okumura's encoder has it,
    which makes these references read 18 bytes *ahead*, into the
    zero-filled window. That decodes the header and the black runs the
    same way, but every other copy then reads from the wrong place: the
    images come out garbled after their first rows.)
  - Checked on the first 37 rows of `rw.tga` (the first 8 KB): with
    writing from 1, the copies that aren't runs are at even distances
    (2-byte pixels) in 96% of cases, against 4% with `0xFEE`; the pixels
    use 96 colours, the most common dim blue, green, red and grey, none
    with the top bit set: a starfield of single stars on black. Other
    start positions give streaks of saturated colour and 180-310 colours.
  - Decoding the whole file as one stream broke every image after the
    first: headers with garbage colour-map fields, and copies of old data
    where zeros belong. That is what a decoder does if it keeps the window
    and the flag bits across a file boundary, which led to decoding file
    by file: the second file's header then decodes cleanly, and the
    compressed data ends where the directory says the next file starts.
- **How the check was worked out:** on the user's saved games' smallest
  files, November 2026. A file stored as `01 08` (one literal, 8) has
  the check 0x0108, its stored bytes as they are; three stored as
  `03 xx 00` have `xx` XOR 0x30, then 0x63: the bytes shifted into the
  register, and the first, 3, shifted out and replaced by 0x3063, the
  0x1021 CRC's table entry for 3. Worked out over every file of the 18
  saves and `alt.res` so, it gives every check.
- **How it was checked:** the last probe run decoded all 31 files by their
  directory entries: each gives exactly its decoded size, from exactly its
  compressed size, and starts with a valid TGA header. The first three runs
  used a wrong window start (corrected above, after the images showed
  garbled in Reluva); sizes and headers don't depend on it, but the pixels,
  footers and check-value tests did. With the right start, all 31 images
  show correctly in Reluva's viewer, and a fifth probe run reads clean
  footers.

## Open questions

- Whether the game checks the checks when it reads an archive.
- What the flag (1 in every entry) means.
- Which screens use the pictures.
