# Jedi Knight: saved games (`.jks`)

> **Status:** partial · **Code:** none (the game reads and writes them itself) · **Checked on:** US retail, saves made by the game in Reluva's emulator (an autosave of each of the light side's 21 levels and one made with *Save*) and two level 21 autosaves from elsewhere · **Confidence:** from the code unless marked

## Overview

A game in progress, as the game saves it: in the player's folder
(`player\<name>\`, in Reluva the target's save folder), one file each:

- `save%04d.jks` (`save0000.jks` on), from *Save* in the game's menu
  (`0x41e31c`, `0x41e363`);
- `quicksave.jks`, the quick save (`0x402993`);
- `_JKAUTO_<level>.jks`, made as each level starts
  (`_JKAUTO_01narshadda.jks`, the name from the code at `0x525648`).

A save made at Nar Shaddaa's start was 360,236 bytes, its autosave
360,198 *(confirmed)*. Its first 0x62c bytes are a header the *Load Game*
list reads alone; after it come where the game was in its episode and a
stream of records, one for each thing, surface, sector, script and
so on, in the form the game's network messages take.

## Layout

All numbers little-endian.

### Header

0x62c bytes, filled in by `0x4da5b4` before the file is written:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Version: 6. *Load Game* lists only saves of 6 (`0x41dec8`) |
| 4 | 128 | The episode, its archive's name, zero-ended: `JK1` *(confirmed)*. *Load Game* shows the episode's name where it's one the game knows (`0x41daef` to `0x41db2f`), else this as it is |
| 0x84 | 128 | The level's file name, zero-ended: `21ajerec.jkl` *(confirmed)* |
| 0x104 | 4 | Health, a float |
| 0x108 | 4 | The most health, a float |
| 0x10c | 800 | The player's inventory: 200 floats, one for each bin of `items.dat` in order of its number (the amounts, from the player's bins, 0x18 bytes each). Shields, bin 60, is at 0x1fc |
| 0x42c | 512 | The title, UTF-16, zero-ended (the size, to the header's end, *inferred*): the level's name, `~`, and the save's own name, `1 - Double-Cross on Nar Shaddaa~abc` *(confirmed)*. An autosave's is its file name, without a `~` *(confirmed)* |

Read with `items.dat`'s names, a level 21 autosave's inventory is as the
game was there *(confirmed)*: the weapons (bins 1 to 10) at 1, the
detonators at 30, energy 230, power 63, the force at 400, `jedi_rank` 8,
the powers' ranks, shields 100, `alignment` 14.26.

### After the header

Written by `0x4daa0d` and read back by `0x4dab70`:

| Size | Field |
|---|---|
| 32 | The episode again, zero-ended (the JK layer writes these 36 bytes, through the hook at `0x835f44`) |
| 4 | The level's place in the episode's list in `episode.jk`, counted from 0: 1 for `01narshadda.jkl`, 41 for `21ajerec.jkl`, 53 for `21bjerec.jkl` *(confirmed)* |
| 32 | The level's file name again, zero-ended. Loading a save loads this level, if it isn't the one running |
| 4 | The level's time, in ms (`0x836c14`) |
| 24 | Game state kept at `0x8ee660`; `01 00 00 00`, twelve zeros, `01 00 00 00`, four zeros in every save seen; what it means isn't known |

Then records, to the end of the file: a type (4 bytes), a size (4) and
that many bytes. They're the game's network messages: loading reads each
into its message buffer (`0x8b4c00`, the type at +0x24, the size at
+0x20) and handles it as one received (`0x4e1dd0`). The world's come
first (`0x4da770`), then the JK layer's (the hook at `0x835910`).

| Type | Written by | What |
|---|---|---|
| 10 | `0x4f37b0` | *Unknown* |
| 12 | `0x4f4210` | A thing, one for each in the world |
| 13 | `0x4fc770` | A COG script: the level's, then the 40 kept for the whole game |
| 14 | `0x4f8f80` | A surface, one for each in the level |
| 15 | `0x4f93b0` | One of 256 entries of 0x29c bytes at `0x88afe0`, likely an AI's state *(inferred)* |
| 16 | `0x4f9900` | An item of `items.dat`, one for each, 82 |
| 17 | `0x4f9a70` | *Unknown* |
| 18 | `0x4f9120` | A sector, one for each in the level |
| 21 | `0x4f9d20` | A thing's puppet state (its [`.pup`](pup.md)) |
| 23 | `0x4f9f20` | One of a list at `0x855000`; *unknown* |
| 24, 30, 31 | `0x4f9ff0`, `0x4fa240`, `0x4fa420` | One each; *unknown* |
| 49, 50, 53, 54, 58 | `0x40c920`, `0x40ceb0`, `0x40c140` and `0x40ccb0`, `0x40c140` and `0x40ceb0`, `0x40c500` | The JK layer's; *unknown* |

The counts that place them *(confirmed)*: a save at Nar Shaddaa's start
has 321 things, 131 scripts, 4,595 surfaces and 462 sectors, a level 21
autosave 42, 66, 288 and 19; the surfaces and sectors are the counts in
each level's [`.jkl`](jkl.md). These, the Nar Shaddaa autosave and the
dark side's level 21 autosave all read to their exact ends this way.

## Behaviour

*Load Game* (`0x41ddd0`) lists the `.jks` files of the player's folder
but those whose names start `_JKAUTO_` (`0x41de47` to `0x41de60`,
before opening them), and of the rest, those of version 6 whose title
has a `~` (`0x41ded6`, the C library's `wcschr`): the list shows the
title after the `~`, and, for the one chosen, the episode, *Level:* the
title before it, *Health: %d/%d* from 0x104 and 0x108, and *Shields:
%d/200* from 0x1fc (`0x41da70` to `0x41dbcb`; the strings
`GUI_SLHEALTH` and `GUI_SLSHIELDS`). Seen running *(confirmed)*: a
level 21 autosave copied in as `save0001.jks` is opened and not listed;
with `x~Level 21` written as its title, it's listed as "Level 21",
*Level: x*, *Health: 100/100*, *Shields: 100/200*, and loads.

## In Reluva

Reluva doesn't read saves: the game reads and writes them in the
target's save folder ([systems/hosting.md](../systems/hosting.md),
step 23). An autosave can be loaded by giving it a title with a `~` and
a name of its own, as above.

## Sources and checks

- **Where the facts come from:** `JK.EXE`'s *Load Game* list
  (`0x41ddd0`) and its detail (`0x41da70`), and the names in its data;
  the save writer (`0x4daa0d`), its header (`0x4da5b4`) and loader
  (`0x4dab70`); the saves the game made running in the emulator, read
  field by field.
- **How it was checked:** the load screen shown with saves changed as
  above, and a save made with *Save* (title, version, episode, health
  and shields as the load screen showed them); four saves read to their
  ends record by record, their counts against the levels' own.

## Open questions

- The 24 bytes of game state after the level's time.
- What the records of types 10, 15, 17, 23, 24, 30, 31 and the JK
  layer's hold, and the fields inside every record.
