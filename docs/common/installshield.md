# InstallShield cabinets

## What it is

How InstallShield's setup programs kept a game's files on its CDs: a
header, `data1.hdr`, naming every file and the groups setup installs them
in, and the files' data in volumes, `data1.cab`, `data2.cab`..., which can
be on different CDs, a file's data running on from the end of one into
the next. As far as Reluva reads them: enough to install a game's files
from its CDs on any system, without running setup.

## What Reluva supports

`common/installshield.cpp` (`readInstallShieldCabinet`,
`findInstallShieldVolumes`, `extractInstallShieldFile`) reads the
header, finds the volumes beside it or in another folder (the other CD),
and takes a file out, its MD5 checked. Only version 7's header is read
(InstallShield 7's; the only one seen yet). An engine's import says which
groups are the game's, and takes them as setup would install them:
Murder on the Orient Express's and And Then There Were None's (`awe`).
`reluva-tool import <first CD> <dest> -patch <other CD>` shows and does it.

## The format

All numbers little endian; offsets in bytes.

**The common header** (0x14 bytes), at the start of the header and of
every volume:

| Offset | Size | What |
|---|---|---|
| 0x00 | 4 | `ISc(` (0x28635349) |
| 0x04 | 4 | the version: 0x01007000, InstallShield 7 in bits 12–15 |
| 0x08 | 4 | not read |
| 0x0c | 4 | the header's: where the cab descriptor is (0x200) |
| 0x10 | 4 | the header's: its size |

**The cab descriptor** (offsets from its start):

| Offset | Size | What |
|---|---|---|
| 0x0c | 4 | the file table's offset, from the descriptor |
| 0x1c | 4 | how many folders |
| 0x28 | 4 | how many files (entries) |
| 0x2c | 4 | the file entries' offset, from the file table |
| 0x3e | 4 × 71 | the file groups: each a list (0 for none), offset from the descriptor |

**The file table** starts with an offset for each folder's name (from the
table), then the names: NUL-terminated, `\` between folders, the top one
empty. The file entries follow, 0x57 bytes each, a file's index its place:

| Offset | Size | What |
|---|---|---|
| 0 | 2 | flags: 1 its data runs on into the next volume, 4 compressed, 8 no file (no name either) |
| 2 | 8 | its size |
| 10 | 8 | its data's size, all volumes together |
| 18 | 8 | where its data starts, in its volume |
| 26 | 16 | the MD5 of the file |
| 58 | 4 | its name's offset, from the file table |
| 62 | 2 | its folder |
| 76 | 4 | with link flag 1: the file whose data it has |
| 84 | 1 | link flags: 1 it has another file's data (and its volume isn't then that data's: it's the file's own CD's) |
| 85 | 2 | its volume, from 1 |

**File groups:** each list entry is three offsets from the descriptor:
the group's name, its descriptor, the next entry (0 for none). A group
descriptor has its name's offset at 0 and the indexes of its first and
last files at 0x16 and 0x1a: a group is the files between them.

**A volume** (`data<n>.cab`): after its common header, at 0x14:

| Offset | Size | What |
|---|---|---|
| 0x14 | 4 | where its data starts (0x200) |
| 0x18 | 4 | not read |
| 0x1c | 4 | the first file whose data is in it |
| 0x20 | 4 | the last |
| 0x24 | 8 | when the first file's data began in the volume before: where the rest is here |
| 0x2c | 8 | the first file's size |
| 0x34 | 8 | how much of the first file's data is here |
| 0x3c | 8 | where the last file's data starts |
| 0x44 | 8 | the last file's size |
| 0x4c | 8 | when its data runs on: how much of it is here |

**A compressed file's data** is in chunks of 32 KiB of the file (the last
the rest), each a 16-bit length, then that many bytes of raw deflate (RFC
1951), a stream of its own ending in a final block. A file that runs on
from one volume into the next has the first part at its offset and the
rest at the next volume's 0x24, the sizes from the two volumes' headers;
the chunks don't start again where it's cut.

Not read: the rest of a group's descriptor and the components, which say
which groups setup offers; the link entries' other fields (+80, the next
file sharing the data); flag 2 (never set here).

## Sources and checks

- **Worked out on** And Then There Were None's two English CDs: the first
  holds `data1.hdr` (650,025 bytes), `data1.cab` (8,865,586) and
  `data2.cab` (638,181,376), the second `data3.cab` (526,667,432). The
  header's 5,918 entries were found by their names (`ac.exe`'s and the
  install's), their sizes and MD5s matched to the installed game's files;
  the folders by where those files are installed; the volumes' fields by
  the file split between `data2.cab` and `data3.cab`
  (`FMV/lombardsaved.bik`: 62,017,046 bytes of data in the first, 2,310,329
  in the second); the chunks by inflating from a file's offset; the
  groups' first and last fields by finding known files' indexes in their
  descriptors; the linked files by one that didn't inflate from its own
  volume, its data at the same offset in the volume of the file +76
  names (196 entries have link flag 1).
- **Checked:** every file of the game's two groups, 5,865, taken out of
  the CDs is byte for byte the installed game's
  (`awe.attwn.gamedata.import`), and the same as `unshield x` (a
  free tool that takes cabinets apart) gives; `unshield`'s listing of the
  groups (17, the same names and files) was the check on the groups' fields.
  Its source wasn't read.
- **Checked on** Murder on the Orient Express's two English CDs too, laid
  out the same way (`data1.hdr`, 889,260 bytes, version 7; `data1.cab`
  and `data2.cab` on the first, `data3.cab` on the second, the same 17
  groups): its 7,875 files are the installed game's
  (`awe.gamedata.import`) and `unshield x`'s, but for one name, which
  `unshield` writes with `_` for the `Ä` (0xc4 in Windows-1252) the
  header has, as the install has it
  (`Text/Dialogue/Events/gen_solutionexit_0_Ä3b.txt`).
- **Confidence:** confirmed (worked out on these CDs, version 7's
  header) unless marked.

## Used by

- **`awe`**: Murder on the Orient Express's and And Then There Were
  None's CDs ([README](../engines/awe/games/motoe/README.md),
  [README](../engines/awe/games/attwn/README.md)).
