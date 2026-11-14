# And Then There Were None: the script runner

> **Status:** partial · **Code:** `src/engines/awe/game/script.cpp`, `conditions.cpp` (shared); `src/engines/awe/attwn/script.cpp` (this game's own words) · **Checked against:** the code (`ac.exe`); `awe script-check` over the English install's 5,614 commands · **Confidence:** from the code unless marked

## Overview

The game's scripts are Orient Express's language
([../../motoe/systems/script.md](../../motoe/systems/script.md)): the
same runner and condition evaluator, most of the same verbs and words.
This page has what differs, and where this game keeps its scripts.

## How it works

### Where the scripts are

| What | Orient Express | And Then There Were None |
|---|---|---|
| Scene scripts | `Locations/**/*.ac2` | `Locations/**/*.ac1`, `Cutscenes/`, `Interface/Scenes/` |
| Script files (`script <name>`) | `Text/Scripts/<name>.txt` (one; the rest of the folder is saves) | `Text/Scripts/<name>.txt`: 38 |
| A dialogue line's events | `Text/Dialogue/Events/` | `Dialogue/Acts/Events/`, `Dialogue/General/Events/`, `Dialogue/Locations/Events/` (`dialogue\acts\Events\%s_%s_%s.txt` and the like in `ac.exe`) |
| Dialogue tables | `Text/Dialogue/` | `Dialogue/Acts/`, `Dialogue/General/`, `Dialogue/Locations/` |
| The acts' table | `Text/acts.txt` | `Text/Chapter01.txt` to `Chapter10.txt` |

### Words only this game has

The runner tells a verb by its first word, compared ignoring case
(`0x4716a0`); a command's words are 0x40 bytes apart.

- **`inc needed|possible <act>...`** (`0x40bdad`): if the current act,
  written `%02d%c` (`0x4028c0`: `01b`), is one of the third to fifth
  words, ignoring case (`0x402a20`), the tag `<act>_neededcounter` (the
  second word `needed`) or `<act>_possiblecounter` (anything else) goes
  up by 1, an unset tag (-99 from the store, `0x412700`) counting as 0.
  The chapters' tables end an act on these counters (`tag
  01a_neededcounter >0`). 79 uses, most in the tables.
- **`suspicion <who> [<|>]n`**, a condition (`0x40a6f1`): the tag
  `suspicion_<who>`, unset as 0, against n: less, more, or without either,
  equal. 37 uses.
- **`cutscene <name>`** (handled with `scene` and `gotoscene` at
  `0x40b422`): a scene change to a cutscene's scene; 20 uses. Not done
  yet: phase 4 of [PLAN.md](../PLAN.md).
- **`journal <n>`** (`0x40bf21`): the number to the journal
  (`[0x485f40]`, `0x41c290`); 1 use. Not done yet: phase 4.

None of these is in `motoe.exe`.

`chapter <n>` (the act's number against n) is Orient Express's too, and
already Reluva's.

### Words in the data that aren't the game's

- `zero` (8 uses), `jetty_scuttle`, `seabird_counter >1` and
  `!4b_trogerslock` (one each) are no word the evaluator has; the last
  three look like tags missing their `tag`. Reluva has them not hold, as
  Orient Express's evaluator does with words it hasn't *(inferred)*.

## In Reluva

The shared evaluator and runner ask the game's own functions
(`GameInfo::condition`, `GameInfo::verb`, in `attwn/script.cpp`) for a
word they don't know; Orient Express has none. `awe script-check` reads
both games' scripts where each keeps them, and counts this game's own
words as known.

## Sources and checks

- **Where the facts come from:** `ac.exe`'s runner and evaluator, at the
  addresses above; the uses, from `awe script-check` over the English
  install.
- **How it's checked:** `awe.unit.attwn` runs `inc` through the shared
  runner and tests `suspicion` through the shared evaluator, and shows
  Orient Express without either. `awe script-check` over the install:
  5,614 commands of 55 verbs, `cutscene` and `journal` the only verbs it
  doesn't know.
