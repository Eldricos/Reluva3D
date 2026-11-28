# Jedi Knight: episodes (`episode.jk`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/episode.cpp` · **Checked on:** US retail, all 3 episodes · **Confidence:** confirmed unless marked

## Overview

Each episode archive (`Episode/*.gob`) has an `episode.jk` at its top:
the episode's title, its kind, and its steps, the levels and cut scenes
in the order they're played. Its comments name the columns. Plain text
with Windows line ends.

| Archive | Title | Type | Steps |
|---|---|---|---|
| `JK1.GOB` | The Force Within | 1, single player | 55 |
| `JK1MP.GOB` | Jedi Training | 2, deathmatch | 6 |
| `JK1CTF.GOB` | Jedi Capture the Flag | 8, Capture the Flag | 3 |

## Layout

Line by line; a `#` starts a comment to the line's end.

- **The title**, in quotes, on the first line ("Title must be first
  line", its comment says; the game looks a translation up in
  `jkstrings.uni`).
- **`TYPE <n>`**: the episode's kind, as the table above.
- **`SEQ <n>`**: how many steps; equal to the number of steps in every
  retail episode.
- **A step:** `<line>: <cd> <level> <type> <file> <lightpow> <darkpow>
  <gotoA> <gotoB>`, spaced with tabs or spaces.
- **`end`**: the last line read.

### A step

| Field | What |
|---|---|
| line | The step's number, which the gotos name: 10, 11, 12, 20... |
| cd | The CD it needs: 1 or 2; 0 (either) in the multiplayer episodes |
| level | The level number the player is shown |
| type | `CUT` a cut scene, `LEVEL` a level, `DECIDE` the choice of the light or dark side |
| file | The cut scene's `.smk` or the level's `.jkl` (`DECIDE`'s is a cut scene, `38a.smk`) |
| lightpow, darkpow | The inventory bins of the Force powers the step gives, light and dark; 0 for none (the comments say so) |
| gotoA | The step after it; -1 at the end of the game |
| gotoB | The other way on: `DECIDE`'s dark side (600 on, against gotoA's 150); a level's when its script ends it lost ([Losing a level](#losing-a-level)); else -1 |

### The single-player sequence

Levels 1 to 8 on CD 1, 9 to 21 on CD 2, as the readme says. After level
14 the `DECIDE` step branches: steps 150 to 212 for the light side, 600
to 661 for the dark, each with its own levels 15 to 21 (`16a`/`16b`,
`17a`/`17b`...) and its own ending (`52-53a.smk`, `57a.smk`). The opening
cut scene, `01-02a.smk`, isn't a step: `JK.EXE` names it itself.

## Behaviour

### Losing a level

In `JK.EXE`: `0x40a580`, `0x402eb0`, `0x402f90`, `0x40dc60`.

- A level ends when its script calls `jkEndLevel`: with a number other
  than 0 the episode goes on to the step's gotoA, with 0 to its gotoB
  *(from the code)*.
- Either way the step's Force powers are given first: lightpow unless the
  player has chosen the dark side, darkpow unless the light, and only
  bins 20 to 34 (`0x402eb0`) *(from the code)*.
- Two scripts end a level lost, with `jkEndLevel(0)`: `16a_timer.cog`
  when the falling ship's countdown runs out (`16aEscapeShip.jkl` and
  `16bEscapeShip.jkl`), and `21_endlevel.cog` when the core closes
  (`21ajerec.jkl` and `21bjerec.jkl`). So a lost `16a` plays `41da.smk`
  (152), then the level's opening, `39a.smk` (150), then the level again;
  `16b` the same through 602 and 600 (`54a.smk`); `21a` plays `50a.smk`
  (210) and the level again; `21b`'s gotoB is its own step, the level
  again at once *(from the code)*.
- A step whose goto is -1 has no next step: the episode ends as after
  its last cut scene if the step's gotoA is -1 too, and otherwise the
  game leaves it as a given-up CD request does (`0x403031`)
  *(from the code)*. No level that can be lost has a gotoB of -1. A
  goto that names no step stops the game with `ERR_BAD_EPISODE_FILE`
  ("%d %d", the step and its goto; `0x40dca6`) *(from the code)*.

### Asking for a CD

- **A `CUT` step:** the game checks for the step's CD (`0x40318f`),
  unless the player has chosen *Disable cutscenes*. If the player gives
  the request up, the cut scene is left out and the episode goes on to
  gotoA (`0x4031c6`) *(from the code)*.
- **A `LEVEL` step:** it checks too (`0x4032da`), unless the registry's
  `InstallType` is 9 or more (9 when there's none, `0x41ec53`). If the
  player gives the request up, it asks "Abort CD Request" ("Warning: This
  will cause you to return to the Main Menu without saving. Are you sure
  you wish to abort?", `0x4032e8`): yes leaves the episode, no checks
  again *(from the code)*.
- **A `DECIDE` step** isn't checked *(from the code)*.
- **The check** is the CD marker's ([jk-cd.md](jk-cd.md#behaviour)): it
  asks "Insert Jedi Knight CD #n and press Enter" until the CD is there
  or the request is given up *(from the code)*.
- **The prompt** (`0x416970`) is a menu dialog with two buttons, *Ok*
  (answers 1: check again) and *Cancel* (answers -1: give up). It makes
  *Ok* the screen's Enter button and *Cancel* its Esc button
  (`0x416a3a`, `0x416a4c`), and the menus press those for Enter and Esc
  (`0x5114f9`, `0x511513`); neither button has a key of its own, so
  Enter answers "again" and Esc "give up", as does a click on either
  *(from the code)*.

## In Reluva

`parseEpisode` (`src/engines/sith/formats/episode.h`) reads one into an
`Episode`. The data viewer's Episodes page lists the steps, and its Cut
scenes page the steps that play each cut scene. `reluva-tool sith
episode <install>` prints each episode's
steps, and the CD each cut scene was found on; `reluva-tool sith formats
<install>` reads them all. When `JK.EXE` runs in Reluva, its checks find
the step's CD in drive D, and Reluva's registry has no `InstallType`
([systems/hosting.md](../systems/hosting.md)), so only cut scenes are
checked.

## Sources and checks

- **Where the facts come from:** the three files and their comments; the
  readme (which CD the levels need); `JK.EXE`'s strings (its messages
  about `episode.jkl`'s SEQ, TYPE and END lines, and `01-02a.smk`);
  `JK.EXE`'s machine code for [Behaviour](#behaviour): the episode's
  reader (`0x40d560`, a step 64 bytes, its type from its first letter),
  the step after (`0x40dc60`), the episode's moves (`0x402eb0` to
  `0x403360`), the CD prompt (`0x416970`) and the menus' keys
  (`0x5114d0`), and the `jkEndLevel` verb (`0x40a580`); the levels' scripts
  in `JK1.GOB` that call `jkEndLevel(0)`, and the levels that use them;
  the prompts' strings in `ui\jkstrings.uni`.
- **How it was checked:** every episode read by `reluva-tool sith
  formats`, its steps counted against SEQ; every cut scene a step names
  found on the CD the step gives, by `reluva-tool sith episode`;
  `sith.unit.formats` on an episode made by the test.
