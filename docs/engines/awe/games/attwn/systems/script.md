# And Then There Were None: the script runner

> **Status:** partial · **Code:** `src/engines/awe/game/script.cpp`, `conditions.cpp` (shared); `src/engines/awe/attwn/script.cpp` (this game's own words) · **Checked against:** the code (`ac.exe`); `awe script-check` over the English install's 5,622 commands · **Confidence:** from the code unless marked

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
- **`suspicion <who> [+|-]n`**, a verb (`0x40d3df`): the tag
  `suspicion_<who>` set to n, or with `+` or `-` moved by n from what it
  was (unset as 0), then kept to 0 or 1.
- **`cutscene <name>`** (handled with `scene` and `gotoscene` at
  `0x40ddd9`): `scene` with `[0x485f04]` set, so its place is its name up
  to its `_` and it's looked for in `Cutscenes`; 20 uses
  ([acts.md](acts.md#cutscenes-and-title-cards)).
- **`journal <n>`** (`0x40bf21`): the journal turned to tab n, its book
  blinking ([journal.md](journal.md)).

- **`test <machine> [<part>]`** (`0x40d93d`, `0x40e2d0`): the island's
  machines tried, by what's been done to them: the commands below run in
  its place. 23 uses; see [The machines](#the-machines).
- **`test <machine>`**, a condition (`0x40a8b2`): whether the machine is
  put together, its tags all set (`0x40e150`); only three can be.
- **`movie coda`** (`0x40c302`): the ending's movie by who was saved,
  `coda#2bothalive` with the tags `9a_lombardsaved` and `10a_verasaved`,
  `coda#3lombardalive` or `coda#4veraalive` with one, else
  `coda#1neitheralive`; played as any other movie. It's played before
  act 10B, not at the game's end.

- **`timer <word>`** (`0x40d9dd`): one clock, `[0x485f2c]`, set to 0,
  whatever the word (`start`, `reset`); the main runner adds each frame's
  seconds to it (`0x409f9c`). As a condition (`0x40a965`), `timer <n`,
  `timer >n` or `timer n` compare its seconds; the save's `TIMR` keeps it
  ([saves.md](saves.md#the-records)). Orient Express's `timer <name>`
  makes a named timer, moved on only by `time <n>`. Brent's sewing waits
  on it: done 30 seconds after the fishing line.

None of these is in `motoe.exe` (its `test` does nothing).

### The machines

A tag "set" is one the store has (not -99). Each machine is a scene
object whose click or item uses set its tags and say `test`.

| Machine | Put together when set (`0x40e150`) | Tried |
|---|---|---|
| `applepress` (the kitchen) | `presspropellor`, `presscheesecloth`, `pressapples`, `pressglass` | Not together: `talk kitchen 5` without the apples, `7` without the propeller, `8` without the glass, nothing without the cheesecloth alone. Together (`0x40e36f`): the press's sound, the handle down, the glass filled, then `take ciderglass` and `talk kitchen 10` if there's no cider glass yet and the basket of apples was used up, else `take juiceglass` and `talk kitchen 9`; `pressapples` and `pressglass` taken away. |
| `gramophone` (the entry hall) | never | Without `gramophone_needle` and `gramophone_replacerecord`: `talk entryway 5` with the needle and no record, else `talk narracottgen 2`. With them, the record turns and `talk entryway 4` with both tubes (`gramophone_tube1`, `_tube2`), else `talk narracottgen 140`. |
| `projector` (the screening room) | never | The reel spins, full if a film's in it (`projector_moviebbc`, `_moviehome` or `_moviehello` set, or the BBC film not taken out). Without `projector_bulb`: `talk narracottgen 139`. With it, after a fade, the first that holds: `screenup` set, `clip-ninemore` and `talk screeningroom 10`; no film, `clip-blank` and `talk narracottgen 38 2`; `projector_moviehome`, `clip-home` and `137`; `projector_moviehello`, the end: `movie clip-hello, movie credits, reset, scene mainmenu01` (`0x40e863`); else `clip-bbc` and `136`. |
| `parlorradio` (the parlour) | `parlorradio_tube1`, `_tube2`, `_tube3`, `_power`, `_wire` | The scene's music again first (`0x40e935`; its music plays while `test parlorradio` holds); together, `talk parlor 8`; all but the wire, `talk parlor 6`; else, its part `power` with the power on, `talk parlor 7`. |
| `saferadio` (the safe room) | `saferadio_tube1`, `_tube2`, `_lamp` | The music again; together, `talk saferoom 14`, or `talk narracottgen 91` with `saferadio_mic`. |

The game ends only by the projector, in act 10B: Owen's last
conversation sends Narracott to the dining room for the sailor boy, which
opens the screening room's drawer with the last film in it.

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
  runner and tests `suspicion` through the shared evaluator, tries the
  machines and picks the coda, and shows Orient Express without them. `awe script-check` over the install:
  5,622 commands of 56 verbs, `cutscene` and `journal` the only verbs it
  doesn't know. The machines were played in the route search
  ([../TESTING.md](../TESTING.md)).
