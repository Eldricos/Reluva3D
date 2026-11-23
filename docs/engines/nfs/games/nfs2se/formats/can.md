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
| 2 | u16 | 11 values among the files, shared by unrelated ones; not read |
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
  or two of the race's settings say otherwise (04; which settings isn't
  worked out). The camera is the car's place plus the offset; for 05,
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

- The word at 2.
- Which race settings choose script 04 over 00.
