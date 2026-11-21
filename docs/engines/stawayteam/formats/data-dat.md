# Star Trek: Away Team: the data archive (`DATA.dat`)

> **Status:** decoded · **Code:** `src/common/zip.cpp` · **Checked on:** the US English retail `DATA.dat` (6,949 files), every file read and its CRC-32 checked (November 2026) · **Confidence:** confirmed unless marked

## Overview

`DATA.dat`, in the install's folder, holds nearly all of the game's
files: its rules, maps, missions, dialogue, pictures, models, fonts,
sounds and music. Only the movies (`DATA/Movies/`) and the program are
outside it. It is a ZIP archive, standard but for one signature, read
with the common ZIP reader. What its files hold is surveyed in
[research/file-survey.md](../research/file-survey.md#datadat); each
format gets its own page as it's worked out.

## Layout

A ZIP archive, as PKWARE's APPNOTE.TXT describes it: a local file header
and the data for each file, then the central directory, then the end of
central directory record. All numbers little-endian.

### Files

- 6,949 files, every one stored (method 0, no compression) and none
  encrypted (no flags), each with its CRC-32.
- Names with `/` between folders, in their own case
  (`Cache/Models/!Unknown Model.mdl16`). No folder entries.

### The end of central directory record

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Signature: `52 45 05 06` (`RE\5\6`) where ZIP has `50 4b 05 06` (`PK\5\6`) |
| 4 | 18 | As ZIP's: disk numbers 0, 6,949 entries, the directory's size and offset, no comment |

## Behaviour

In `Star Trek.exe`: `0x600045`. The game's reader takes an end record
signed either way, `PK\5\6` or `RE\5\6`, so a standard ZIP serves it too
*(inferred)*. Two more readers, at `0x600146` and `0x60025a`, stop at
either signature when walking the records.

The game writes ZIP archives of its own (a local header at `0x6014e1`),
and signs their end record `RE\5\6` (`0x6015e9`): its saves, it seems
*(guessed)*.

## In Reluva

- **Reading it:** `ZipArchive` (`src/common/zip.cpp`), which takes an end
  record signed `RE\5\6` as well as `PK\5\6`, as the game does. The
  engine opens it as the game starts, and says so if it's missing or
  can't be read.
- **Commands:** `reluva-tool stawayteam data-list` lists its files and
  their sizes (all, or those whose names hold a given part);
  `reluva-tool stawayteam data-extract` writes them out, checking each
  one's CRC-32.

## Sources and checks

- **Where the facts come from:** the retail `DATA.dat`, its local headers
  walked and its end record read in hex; the signature checks in
  `Star Trek.exe`, disassembled with `objdump` at the addresses given;
  PKWARE's APPNOTE.TXT for the rest of the layout.
- **How it was checked:** `reluva-tool stawayteam data-extract` on the
  retail archive reads all 6,949 files, every CRC-32 matching, 324,456,767
  bytes in all; a unit test (`common.unit.archives`) reads an archive
  whose end record is signed `RE\5\6`.
