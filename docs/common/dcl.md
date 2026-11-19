# PKWARE DCL ("implode") compression

## What it is

The PKWARE Data Compression Library's format, used by many DOS games for
their archives. `src/common/dcl.cpp` (`reluva::decompressDCL`) unpacks it.

## What Reluva supports

Unpacking only: coded and stored literals, and each dictionary size
(1, 2 or 4 KB).

## The format

- **A two-byte header:** whether literals are coded (1) or stored as bytes
  (0), then the dictionary size: 4, 5 or 6 (1, 2 or 4 KB).
- **Then a bit stream,** least significant bit first: a 0 bit and a literal,
  or a 1 bit and a length and distance copied from what was decoded. The
  lengths, distances and coded literals use fixed Huffman codes, given as
  run-length coded code lengths.
- **The end** is a length code of 519.

## Sources and checks

- **Where the facts come from:** Mark Adler's `blast.c` (in zlib's
  `contrib/blast`, under the zlib licence), which documents the format and
  its fixed code tables. The decoder follows its description; the code is
  Reluva's own.
- **How it was checked:** `common.unit.archives` (`dcl`) decodes the
  example given with `blast.c` and rejects broken input.
- **Confidence:** published unless marked.

## Used by

- **`stharbinger`**: the members of Harbinger's `.ALL` archives
  ([archives.md](../engines/stharbinger/formats/archives.md)).
