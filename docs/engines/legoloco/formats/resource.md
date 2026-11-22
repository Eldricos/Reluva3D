# LEGO Loco: the resource file (`resource.RFH`, `resource.RFD`)

> **Status:** decoded · **Code:** `src/engines/legoloco/formats/resource_file.cpp` · **Checked on:** the CD's resource file, version 1.0.18.2: all 2,522 entries read and the 1,372 compressed ones unpacked (November 2026) · **Confidence:** confirmed unless marked

## Overview

Most of the game's data is in one archive in `art-res`: the pictures
(`.bmp`, and `.but`, which are BMPs too), sounds (`.wav`), object
definitions (`.dat`), layouts (`.lay`), ready-made boards (`.sav`),
animated cursors (`.ani`) and `ee.ini`. `resource.RFH` is its index and
`resource.RFD` its data. The game finds the index by `LEGO.INI`'s
`[DIRECTORIES] ResFile`, and opens the data beside it.

Its 2,522 names are paths with `\` between folders, up to four folders
deep, some with spaces (`sounds\minifig\super hero\030.wav`), none twice
even ignoring case. One, `clipart\70008bmp`, has no dot before its
extension.

## Layout

All values little-endian.

### The index: `resource.RFH`

Entries one after another to the end of the file, with no header:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | the name's length, counting its terminating zero |
| 4 | length | the name, ending in a zero |
| 4 + length | 4 | the entry's size in `resource.RFD`, as stored |
| 8 + length | 4 | 1 if the entry is compressed, else 0 |

Only 0 and 1 are seen in the flag.

### The data: `resource.RFD`

The entries as stored, in the index's order, with nothing between them:
an entry's offset is the sum of the sizes before it. The sizes add up to
the file's size exactly.

### Compressed entries

In `loco.exe`: `0x45c830`.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | the unpacked size |
| 4 | 4 | the root of the tree: a node number |
| 8 | 2,048 | the tree: 512 nodes, each two 16-bit node numbers, its left child then its right |
| 2,056 (`0x808`) | to the end | the bits |

A Huffman code. Nodes 0 to 255 are the byte values themselves, so their
slots in the table are left zero; inner nodes are numbered from 256 up,
each after its children, and the root is the last. Every root on the CD
is an inner node.

To unpack a byte, start at the root and take one bit at a time, 0 for
the left child and 1 for the right, until a node below 256 is reached:
that is the byte. Repeat until the unpacked size is reached. The game
reads the bits in 32-bit words, from each word's lowest bit, which is
the same as taking each byte's lowest bit first; the bits are padded to
a whole word, so up to 3 bytes follow the last one used.

## In Reluva

- `src/engines/legoloco/formats/resource_file.cpp`: `ResourceFile`, a
  `reluva::Archive`, reads the index and reads an entry from the data
  file when asked, unpacking it with `unpackResource`. It finds a name
  with `/` or `\`, in any case. It refuses a flag other than 0 or 1, a
  root below 256 and an unpacked size over 64 MB, none of which the CD
  has.
- `reluva-tool legoloco rf-list <folder>` lists every entry with its
  stored and unpacked sizes, unpacking each to check it.
  `reluva-tool legoloco rf-extract <folder> <output folder> [name...]`
  writes the entries out, unpacked.

## Sources and checks

- **Where the facts come from:** the index's and the compressed entries'
  layouts were found by comparing the CD's entries: the sizes adding up
  to the data file's, flagged entries starting with a size larger than
  themselves, a node number and a table that's zero for its first 256
  slots. The bit order was found by unpacking `ee.ini` both ways, one of
  which gave text. The game's own unpacking, at `0x45c830` in
  `loco.exe`, was then read and agrees: the table's layout, the bit order
  and the words. `LEGO.INI`'s `ResFile` is read at `0x446070`, and the
  resource file opened with it by `0x45caa0`.
- **How it was checked:** `reluva-tool legoloco rf-list` reads every
  entry of the CD's resource file and unpacks every compressed one to its
  stated size, with none failing; every unpacked entry starts as its kind
  should (`BM` for the pictures, `RIFF` for the sounds, the boards'
  header for the `.sav` files). The unpacked entries were compared byte
  for byte with a separate unpacker written for the study, and are the
  same. The unit tests (`legoloco.unit.resources`) build a resource file
  and a compressed entry, and check reading them and refusing damaged
  ones.
