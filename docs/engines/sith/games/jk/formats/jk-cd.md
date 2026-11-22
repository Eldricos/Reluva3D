# Jedi Knight: the CD marker (`JK_.CD`)

> **Status:** partial · **Code:** `src/engines/sith/folders.cpp` · **Checked on:** US retail, both CDs · **Confidence:** confirmed unless marked

## Overview

Each CD has a file `GAMEDATA/RESOURCE/JK_.CD`, four bytes long, which is
different on each. The install has none. `JK.EXE` names it, beside the
paths of the CD's `gamedata\resource` folder, so the game reads it from
the CD *(from the code)*.

## Layout

| CD | Bytes |
|---|---|
| 1 | `c4 32 92 69` |
| 2 | `84 33 92 69` |

As a little-endian number they are `0x699232c4` and `0x69923384`. What
they mean isn't known: they don't look like a disc number.

## Behaviour

What the game does with the bytes (which CD it wants for a level, and
how it asks for the other) isn't traced yet. The readme says CD 1 is
needed for levels 1 to 8 and CD 2 for levels 9 to 21, and that either
does for multiplayer.

## In Reluva

`cdNumber` (`src/engines/sith/folders.h`) takes a folder for CD 1 or CD 2
when its `JK_.CD` holds those bytes exactly, whatever the names' case;
`findFolders` uses it to find each CD for an install
([README.md](../README.md#using-it)). `reluva-tool sith folders
<install>` prints what it finds.

## Sources and checks

- **Where the facts come from:** the two CDs' files, compared; the file's
  name and the CD's folder in `JK.EXE`'s strings; the readme (`README.TXT`,
  1b, "Disc Information").
- **How it was checked:** `sith.unit.folders` on folders made by the
  test; `reluva-tool sith folders` on an install with copies of both CDs
  beside it.

## Open questions

- What the four bytes stand for, and whether the game checks them or
  only that the file is there.
