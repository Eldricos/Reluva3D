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
list reads alone; the rest is the game's state, not looked into.

## Layout

All numbers little-endian.

### Header

0x62c bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Version: 6. *Load Game* lists only saves of 6 (`0x41dec8`) |
| 4 | 256 | The episode, its archive's name, zero-ended: `JK1` (the size, to the next field, *inferred*). *Load Game* shows the episode's name where it's one the game knows (`0x41daef` to `0x41db2f`), else this as it is |
| 0x104 | 4 | Health, a float |
| 0x108 | 4 | The most health, a float |
| 0x1fc | 4 | Shields, a float |
| 0x42c | 512 | The title, UTF-16, zero-ended (the size, to the header's end, *inferred*): the level's name, `~`, and the save's own name, `1 - Double-Cross on Nar Shaddaa~abc` *(confirmed)*. An autosave's is its file name, without a `~` *(confirmed)* |

The rest of the header holds other floats (1, 2, 3, 4, 6, 8, 17, 30,
63, 230, 400, 21000 among them in a level 21 autosave) whose meaning
isn't known.

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
  the saves the game made running in the emulator, read field by field.
- **How it was checked:** the load screen shown with saves changed as
  above, and a save made with *Save* (title, version, episode, health
  and shields as the load screen showed them).

## Open questions

- The header's other fields, and the state after it.
