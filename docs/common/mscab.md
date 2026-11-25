# Microsoft cabinets

## What it is

Microsoft's cabinet format (`.cab`, `MSCF`): files kept together, their
data stored or compressed in blocks. As far as Reluva reads them: the
cabinets a self-extracting patch carries inside its program, taken out
without running it.

## What Reluva supports

`common/mscab.cpp` (`readMicrosoftCabinets`) finds every cabinet in a
file (a `.cab`, or a program holding one) by its signature and takes its
files out, their data stored or compressed with MSZIP. Quantum and LZX
aren't read; a cabinet using them is an error, as is one that's damaged.
Files continued from or into another cabinet aren't taken. MSZIP's
blocks are inflated by `common/inflate.cpp`, each with the last 32 KB
before it as its history. Starfleet Command II's import uses it on the
2.0.3.6 patch's download ([systems/patching.md](../engines/sfc/games/sfc2/systems/patching.md)).

## The format

As Microsoft's published Cabinet File Format describes it *(published)*.
All numbers little endian; offsets in bytes from the cabinet's start.

**The header** (36 bytes, then more with flag 4):

| Offset | Size | What |
|---|---|---|
| 0x00 | 8 | `MSCF` and four zero bytes |
| 0x08 | 4 | the cabinet's size |
| 0x10 | 4 | where the file entries start |
| 0x18 | 2 | the version: 0x0103 |
| 0x1a | 2 | how many folders |
| 0x1c | 2 | how many files |
| 0x1e | 2 | flags: 1 and 2, a cabinet before and after this one (their names follow; not read); 4, reserved space |
| 0x24 | 4 | with flag 4: the header's reserved size (2 bytes; skipped), each folder's (1) and each block's (1) |

**The folders** after it, 8 bytes each (and their reserve): where the
folder's first data block is (4), how many blocks (2), and its
compression (2; its low 4 bits: 0 stored, 1 MSZIP, 2 Quantum, 3 LZX).
A folder's files are one run of data, cut into blocks.

**The files** where the header says, 16 bytes and a name each: its size
(4), its offset in its folder's data (4), its folder (2; 0xfffd and up,
continued from or into another cabinet), a DOS date and time and
attributes (6; not read), then its name, NUL-terminated, folders split by
`\`.

**A data block:** a checksum (4; not checked), its size (2), its size
unpacked (2; at most 32 KB), then its data (after its reserve). MSZIP's:
`CK`, then a raw deflate stream (RFC 1951) that may refer back into the
data of the folder's blocks before it.

## Sources and checks

- **Read from** Microsoft's published description of the format, the
  Cabinet File Format (part of its Cabinet SDK documentation)
  *(published)*.
- **Checked on** Starfleet Command II's 2.0.3.6 patch,
  `sfc2patch20022036.exe` (23,559,287 bytes): one cabinet at 284,016, 17
  files in one MSZIP folder, their names starting with `\`. Each is byte
  for byte what a short script, written earlier from the same
  description, took out, and the patch they hold applies
  ([systems/patching.md](../engines/sfc/games/sfc2/systems/patching.md)).
  Earlier in the program, eight bytes like a cabinet's signature that
  aren't one: these are passed over, as a cabinet must be version 0x0103
  and fit in the file.
- **Tested** by `common.unit.archives` (a cabinet made by the test in a
  program, one stored folder and one MSZIP folder whose second block
  copies from its first).
- **Confidence:** confirmed for what the patch has: one cabinet, MSZIP.
  Reserves, stored folders and several cabinets in one file are read as
  the description says, but only tested on cabinets the test makes.

## Used by

- **`sfc`:** the 2.0.3.6 patch, as it's imported
  ([systems/patching.md](../engines/sfc/games/sfc2/systems/patching.md)).
