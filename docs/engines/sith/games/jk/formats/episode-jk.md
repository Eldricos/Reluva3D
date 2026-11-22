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
| gotoB | The other way on: `DECIDE`'s dark side (600 on, against gotoA's 150); a level's when it's lost, as `16aEscapeShip.jkl`'s goes to the cut scene `41da.smk`, which returns to the step before *(inferred)*; else -1 |

### The single-player sequence

Levels 1 to 8 on CD 1, 9 to 21 on CD 2, as the readme says. After level
14 the `DECIDE` step branches: steps 150 to 212 for the light side, 600
to 661 for the dark, each with its own levels 15 to 21 (`16a`/`16b`,
`17a`/`17b`...) and its own ending (`52-53a.smk`, `57a.smk`). The opening
cut scene, `01-02a.smk`, isn't a step: `JK.EXE` names it itself.

## In Reluva

`parseEpisode` (`src/engines/sith/formats/episode.h`) reads one into an
`Episode`. `reluva-tool sith episode <install>` prints each episode's
steps, and the CD each cut scene was found on; `reluva-tool sith formats
<install>` reads them all.

## Sources and checks

- **Where the facts come from:** the three files and their comments; the
  readme (which CD the levels need); `JK.EXE`'s strings (its messages
  about `episode.jkl`'s SEQ, TYPE and END lines, and `01-02a.smk`).
- **How it was checked:** every episode read by `reluva-tool sith
  formats`, its steps counted against SEQ; every cut scene a step names
  found on the CD the step gives, by `reluva-tool sith episode`;
  `sith.unit.formats` on an episode made by the test.

## Open questions

- What a lost level's gotoB does exactly, and how the game asks for a CD
  when a step's isn't in the drive.
