# Star Trek: Armada: saved games (`save\s<n>.sav`)

> **Status:** partial · **Code:** `src/engines/storm3d/formats/saved_game.cpp` · **Checked on:** none of the program's own: the install has no saved game · **Confidence:** from the code; Reluva's own form after the head

## Overview

A game saved in play is a file in the install's `save` folder, named
`s<n>.sav` (`s0.sav` first). The program writes it with the same writer
as a mission's map ([bzn.md](bzn.md)), with more in it: everything the
game is doing, and the mission's scripts' variables. Reluva writes the
same head, so its lists read both, but its own form after it.

## Layout

The program's, *(from the code)* with no save there to check it on:

- **The head,** in text whatever the rest is: `version [1] =` and 2051
  (0x803) on the next line; `saveGameDesc = ` and the description's
  128 bytes in hex, 256 digits; `binarySave [1] =` and `true`. The lines
  end in CR LF (0x543be0).
- **The description** is what the player typed in the Save Game
  screen's box, or the mission's name without, with the time saved
  after it: `" %b %d %I:%M:%S %p"`, as "Premonitions Nov 28 03:15:42
  PM". A list reads only this of a file: its first 512 bytes, the
  version, and from version 0x7fa on the description (0x543ab0).
- **The rest,** binary fields as a map's: the settings, with the
  campaign's and mission's numbers, the run's kind and the game's clock
  among them; every object with its game state (its orders, systems,
  damage, build queues...); the players' records with their relations
  and attack counters; the mission's part, with whether it's done or
  failed; the areas of interest and paths; then the shots in flight, the
  explosions and effects, the computer players' states, the shroud, the
  special flags' next id, the scripts' variables (`[CCA_Scripts]`), and
  the difficulty and objectives file. Each field must be the type and
  size the reader asks for, and nothing gives an object's length, so
  reading one wants every kind of object's layout (0x5431d0).
- **The scripts' variables** are what each of the mission's DLLs'
  save functions writes (the program's `save_scripts` list), through
  `out()`: a `hot_int` its value, a `timer_int` its value and seconds
  left, a list of integers, floats or points their values; binary, no
  names. Loading starts the DLLs afresh and gives the values back
  through their load functions and `in()`, each `hot_int` marked
  changed; the rules and the scripts running aren't saved.

Reluva's, a form of its own: the same head, then `reluvaSave [1] =` and
`1` where the program's says `binarySave`, so the program stops there.
After that: the campaign's and the mission's numbers and the map's
name, then the mission's state as Reluva keeps it
([systems/saved-games.md](../systems/saved-games.md)).

## In Reluva

`SavedGame` writes and reads Reluva's files, reads any file's
description for the lists, and names a new one one past the highest
`s<n>.sav` there, a name not of that form counting as 0 (0x53ea20). The
program's own saves list but can't be loaded.

## Sources and checks

- The program's writer and reader (0x543be0, 0x5431d0), its list
  (0x53e750, 0x543ab0) and its `in()` and `out()` (0x4b0010 on), read in
  the disassembly.
- The unit tests (`storm3d.unit.player`) write a save and read it back,
  read a description from a head of the program's form, and name new
  saves.

## Open questions

- The program's own saves: each kind of object's fields, the computer
  players' states, and the shots', explosions' and effects' records,
  which reading or writing one wants.
