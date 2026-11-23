# Jedi Knight: the CD marker (`JK_.CD`)

> **Status:** decoded · **Code:** `src/engines/sith/folders.cpp` · **Checked on:** US retail, both CDs · **Confidence:** confirmed unless marked

## Overview

Each CD has a file `GAMEDATA/RESOURCE/JK_.CD`, four bytes long, which is
different on each. The install has none. `JK.EXE` reads it from the
CD's `gamedata\resource` folder to tell which CD is in the drive
([Behaviour](#behaviour)), and checks its value.

## Layout

| CD | Bytes |
|---|---|
| 1 | `c4 32 92 69` |
| 2 | `84 33 92 69` |

As a little-endian number they are `0x699232c4` and `0x69923384`:
`0x69923284` with one bit set for the CD, `n << (n + 5)` for CD n (bit 6
for CD 1, bit 8 for CD 2), as `JK.EXE` works it out (`0x40ec19`)
*(from the code)*. `JK.EXE` also takes `0x69973284` as any CD, which
neither retail CD has. Nothing else in them is known to mean anything:
they aren't a count, and the rest of the number is the same on both.

## Behaviour

In `JK.EXE`: `0x40ebb0`, `0x40f110`.

- **The game checks the value**, not only that the file is there: given
  a CD's number (0 for either), `0x40ebb0` opens `jk_.cd`, reads 4 bytes,
  and accepts `0x69973284`, or for 0 either CD's marker, or for CD n
  its own. A missing file or any other value fails *(from the code)*.
- **It looks for the file as for any other:** at the install's root, in
  its `resource` folder (and in an episode's folder), then in the
  CD's `gamedata\resource`, as `reluva-tool sith host -files` shows. So
  a copy in the install would pass *(inferred)*.
- **When it fails** the game closes its archives and asks "Insert Jedi
  Knight CD #n and press Enter" (`GUI_INSERTCD`; for 0 "Insert a Jedi
  Knight CD and press Enter", `GUI_INSERTANYCD`; titled "Insert CD").
  Answered, it checks again; given up, it opens its archives again and
  fails *(from the code)*.
- **When it checks:** before each cut scene and level of the episode
  ([episode-jk.md](episode-jk.md#asking-for-a-cd)), once for either CD
  before the opening (`0x401339`), and at `0x403573` (CD 1), `0x40dd86`
  (either, when the registry's `InstallType` is under 1) and `0x4103bb`,
  which aren't traced further *(from the code)*.
- **Which CD is in:** `0x40f110` gives 1, 2, or 0 (for the any-CD
  marker, another value or no file), and `0x421a45` plays that CD's audio
  tracks with it: 4 and 5 for CD 1, 6 and 7 for CD 2, else 2 and 3
  *(from the code)*. It sets the credits up *(inferred)*.

The readme says CD 1 is needed for levels 1 to 8 and CD 2 for levels 9
to 21, and that either does for multiplayer.

## In Reluva

`cdNumber` (`src/engines/sith/folders.h`) takes a folder for CD 1 or CD 2
when its `JK_.CD` holds those bytes exactly, whatever the names' case
(not the any-CD marker, which no retail CD has);
`findFolders` uses it to find each CD for an install
([README.md](../README.md#using-it)). `reluva-tool sith folders
<install>` prints what it finds.

## Sources and checks

- **Where the facts come from:** the two CDs' files, compared; the file's
  name and the CD's folder in `JK.EXE`'s strings; `JK.EXE`'s machine
  code ([Behaviour](#behaviour)); the prompts' strings in
  `ui\jkstrings.uni`; the readme (`README.TXT`, 1b, "Disc
  Information").
- **How it was checked:** `sith.unit.folders` on folders made by the
  test; `reluva-tool sith folders` on an install with copies of both CDs
  beside it; the files `JK.EXE` opens for the marker, listed by
  `reluva-tool sith host -files` up to the first cut scene of a new
  game.

## Open questions

- Whether any release's CD holds the any-CD marker, `0x69973284`, and
  why the markers are the numbers they are.
