# Need for Speed II SE: animation scripts (`.can`)

> **Status:** decoded · **Code:** `src/engines/nfs/nfs2se/formats/can.cpp` · **Checked on:** all 128 files on the US CD and in the install · **Confidence:** confirmed unless marked

## Overview

The camera's moves as a race starts and ends, and an object's as a car
knocks it over: `GameData/Tracks/Data/pc/trNNSS.can`, for track NN,
script SS from 00 to 06, and `trNN00a.can`; the program logs them as
"animation scripts". 64 names, each on the CD and in the install, the
same bytes.

| Script | Keys | What it moves |
|---|---|---|
| 00 | 51 | The camera, flying along the road to the car as a race starts; one for each track (`TR04`'s and `TR05`'s the same) |
| 00a | 51 | The same for the 3dfx program, which loads it in 00's place; it ends further back and higher, perhaps for its view behind the car *(guessed)* |
| 01 | 41 | The camera, round the car as the race ends |
| 02 | 6 | A knocked object, tipped over (a slow hit) |
| 03 | 17 | A knocked object, thrown up and turned over (a fast hit) |
| 04 | 51 | The camera, round the car as a race starts, when 00 isn't played |
| 05 | 51 | 04 for a race run backward |
| 06 | 41 | 01 for a race run backward: 01 turned half round |

Scripts 01 to 06 are the same on every track.

## Layout

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | the file's size: 8 + 20 × the keys, in every file |
| 2 | u16 | 11 values among the files, shared by unrelated ones (scripts 01 and 06 all 0x0269, 02 0x828E, 03 0x8357, 04 and 05 0x8320, 00 by track): not a checksum; neither program reads it, only 4, 6 and the keys (0x42db30, 0x42dbf0). Bit 15 set on 02 to 05 and most 00s, clear on 01, 06 and `TR00`'s, `TR06`'s and `TR08`'s 00; the low 15 bits all from 0x164 to 0x357. Read as a DOS time of day (hour × 2048 + minute × 32 + second ÷ 2), every value is a valid time: 00:11:08 to 00:19:20 with bit 15 clear, and with it set (the hour's top bit, 16) a window of 6 minutes, 16:20:28 to 16:26:46: eight values within 201 of the 32,768 the 15 bits could hold. Files with the same keys have the same value; others share one too, many at 00:19:18 as from one run of a tool. So the time each file was written *(inferred)* |
| 4 | u16 | number of keys |
| 6 | u16 | the race clock's ticks to a key: 6 in every file (the program takes 6 for a value outside 1 to 400) |
| 8 | 20 each | the keys |

A key:

| Offset | Size | Field |
|---|---|---|
| 0 | 3 × 4 | an offset from the car or the object, 16.16: x, height, y, as the tracks' points |
| 12 | 4 × s16 | a rotation, a quaternion x, y, z, w, 0x4000 being 1 |

Every key's rotation is 16,382 to 16,384 long, and neighbouring keys'
point the same way (a positive dot product).

## Behaviour

Addresses are `nfs2sen.exe`'s, from its code and the hosted game
(November 2026).

- **Loading** (0x42de70, as a race is set up): `%sTr%02d` and then
  `%s%02d.can` for 0, 1, 2... until one is missing. The 3dfx program
  (`NFS2SEA.EXE` 0x43f240) asks for `%s%02da.can` first, so script 0 is
  `trNN00a.can` there; the software program never reads the `a` files.
- **Playing** (0x42dbf0): the key is the ticks since the start divided
  by the ticks to a key; a script plays once, to its last key. Between
  two keys the offset and each of the rotation's four numbers are
  interpolated in a straight line, not renormalised (0x4581c0); the
  rotation becomes a matrix (0x4580d0). A camera's matrix is turned a
  quarter round x (0x42da42): the files keep cameras' keys pitched by
  90°.
- **The clock:** the race's ticks, about 64 a second on the host: a
  51-key script lasts about 4.7 s (300 ticks), as script 00 did as the
  hosted race started.
- **Race start** (0x43943a): 00, unless the race is run backward (05)
  or the race is a tournament or a knockout, or played over a network
  (04): the race type (0x4dd2d8) isn't a single race, or the game type
  (0x4dd2e8) is 4, the network; both set from the front end's choices.
  So one player, split screen, the modem and the serial link see 00 in
  a single race. The camera is the car's place plus the offset; for 05,
  turned by the car's heading as well *(inferred)*.
- **Race end** (0x4395e3): 01, or 06 backward, for a human's car.
- **Knocked objects** (0x433650): a car hitting an object that can be
  knocked over ([trk.md](trk.md), Extrablocks, 11) plays 02 below a speed
  of 25 and 03 above, turned to the car's heading. These two only move in
  height and depth, and turn round x.
- Neither replays nor the attract mode play them.

## In Reluva

Read as `AnimationScript` (`src/engines/nfs/nfs2se/formats/can.cpp`);
`reluva-tool nfs survey` reads all of them. Not played by the viewer.

## Sources and checks

- **Where the facts come from:** the files, and `nfs2sen.exe`'s and
  `NFS2SEA.EXE`'s loading and playing code, disassembled and read; the
  hosted game's race start, watched; no published description is used.
- **How it was checked:** survey scripts over the 128 files (November
  2026): every size and count, the rotations' lengths, the order of the
  quaternion's numbers against the code that makes the matrix, script 00
  against the track's road (its last key behind the car, its first along
  the road from it), 06 against 01; the reader on all of them; the unit
  tests (`nfs.unit.nfs2se`).

## Open questions

- Whether the word at 2 is the time each file was written: the CD's
  files' own dates would settle it (the copy here hasn't kept them).
