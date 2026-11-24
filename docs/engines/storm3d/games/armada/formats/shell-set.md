# Star Trek: Armada: the player's progress (`shell.set`)

> **Status:** decoded · **Code:** `src/engines/storm3d/formats/shell_progress.cpp` · **Checked on:** the English retail 1.0 install's `shell.set`, the original reading copies changed by hand · **Confidence:** layout from the code; meanings partly confirmed

## Overview

`shell.set`, in the install's folder, keeps how far the player has come
through the campaigns: which missions are open in each and which
campaigns are finished, and the player's name. The program reads it as
it starts and writes it back over the install's copy. Not to be mixed
up with `mshell.set`, the list of the campaigns' maps
([systems/front-end.md](../systems/front-end.md#the-single-player-screen)).

## Layout

One record, written with `fwrite` to a file opened with `fopen(...,
"w")` and read with `fread` from `"r"` (0x545b90 and 0x545c60) *(from
the code)*. Both are text mode, so a byte 0x0a in it goes out as CR LF
and comes back as one. The record's parts, in the order written:

| Bytes | What |
|---|---|
| 5 | For each campaign, the last mission open, from 0 |
| 5 | For each campaign, 1 if it's finished |
| 3 | The program's own flags; the install's has 1 in the third |
| 1 | The campaign the player was on |
| 1 | A campaign: the code sets it to 4 when it opens every mission |
| 1 | 1 if two login names follow |
| 64 + 64 | Two names, only if the byte before is 1 |
| 64 | The player's name, ended by a NUL |

The campaigns are in `mshell.set`'s order: the Federation, the
Klingons, the Romulans, the Borg, the finale. The install's file is 80
bytes, all 0 but the third flag and the name, "Player".

What the bytes mean was checked with the original:

- A campaign opens when the one before it is finished. The tooltip
  over a campaign not open names the last one open (0x540c39 tests the
  Federation's byte for the Klingons') *(from the code)*.
- With the Federation's finished byte set to 1 and the Klingons' last
  mission set to 2, the original opened the Klingons' campaign and
  listed three of its missions.
- The two names after the flag sit where the online service's login
  would *(guessed)*; the third, "Player", is the name the multiplayer
  screens offer *(guessed)*.

## In Reluva

`ShellProgress` (`src/engines/storm3d/formats/shell_progress.h`) reads
and writes the record, its newlines as text mode has them. The game
reads the player's copy from its save folder, else the install's, and
the single-player screen opens the campaigns and missions it says.
Reluva doesn't write it yet: nothing in the front end changes it.

## Sources and checks

- **Where the facts come from:** the code that reads and writes it, and
  the original run under the emulator with changed copies.
- **How it was checked:** the unit tests (`storm3d.unit.player`) read
  the install's record and write it back byte for byte, and a newline
  byte both ways. Reluva's single-player screen, given the copy with the
  Klingons open, matches the original's frame in its mission panel to
  the pixel.

## Open questions

- What the three flags and the two campaign bytes are for.
- When the program writes the file: at the end of a mission won, or as
  it quits.
