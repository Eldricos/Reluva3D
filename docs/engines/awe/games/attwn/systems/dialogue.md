# And Then There Were None: dialogue

> **Status:** partial · **Code:** `src/engines/awe/game/dialogue.cpp` (shared), `src/engines/awe/games.h` (`DialogueTables`), `src/engines/awe/attwn/attwn.cpp` (this game's) · **Checked against:** the code (`ac.exe`), the English install's dialogue tables and voices · **Confidence:** from the code unless marked

## Overview

Conversations are Orient Express's
([../../motoe/systems/dialogue.md](../../motoe/systems/dialogue.md)): parts,
threads and nests of lines, read from tab-separated tables, each line with
its voice, conditions and commands. This page has what's known to differ:
where the tables are, and how they're read. The dialogue manager itself
(starting a talk, the choices, the box) isn't compared yet.

## How it works

### The tables

| What | File (`ac.exe`) |
|---|---|
| The act's | `dialogue\acts\act%02d%c.txt` (`0x402c78`), loaded with the act |
| Narracott's own, loaded with every act's | `dialogue\general\narracott.txt` (`0x4174e5`) |
| A character's general lines | `dialogue\general\%s.txt` (`0x403154`) |
| A place's | `dialogue\locations\%s.txt` (`0x4033f4`) |
| A line's events | `dialogue\acts\Events\%s_%s_%s.txt`, and the like for `general` and `location` (that one's folder `location`, not `locations`) |

The install has events for the acts' lines only (`Dialogue/Acts/Events`).

### The columns

The loader (`0x431160`) takes each line's columns in order with `strtok`,
into a line of 0x654 bytes (Orient Express's is 0x1324):

| Column | Heading | Read into |
|---|---|---|
| 1 to 5 | Name, Act, Conver., Thread, Nest | the part, the thread and the nest, coded as Orient Express's (`+0`) |
| 6 | Speaker | `+0x08` |
| 7 | Tone | read and dropped (`0x4317c0`); it holds notes for the voices ("sleeping noises", "PRESS"), where Orient Express has Target |
| 8 | Line | `+0x48`, 256 bytes, its quotes taken off; longer is cut |
| 9 | Line Translation | `+0x148`, apart: it isn't said in the line's place there (none in the English release) |
| 10 | Condition | `+0x248`, empty for `0` |
| 11 | Repeat | `+0x04`: a letter less `A`, else -1 |
| 12 to 14 | Prevent, Event, Povent | `+0x348`, `+0x448`, `+0x548` |

Duration and Location aren't read. A line's width is measured
(`0x415ed0`, into `+0x648`); one wider than 750 pixels is wrapped
(`0x434960`), its rows at `+0x650` (Orient Express: 944).

### Voices

Named as Orient Express's: `Sound\VO\` and the Name, Act, Conver., Thread
and Nest run together (`armstronggen02aa`), `_n` after for a repeated
line. 1,822 of the 1,920 lines in the tables have a voice so named; most
of the rest are repeats.

## In Reluva

`DialogueTables` (`games.h`) is how a game's tables are read: whether the
7th column is a Target, whether Duration and Optional are read, whether a
translation takes the line's place, the box's width, and the events'
folder. Orient Express's are the defaults; this game's are in
`attwn/attwn.cpp`. The shared reader (`DialogueBook::add`) and the box
take them from there.

## Sources and checks

- **Where the facts come from:** `ac.exe`'s loader and the paths above.
- **How it's checked:** `awe.unit.attwn` reads a table of this game's
  layout and shows Orient Express's reading of it turning Tone into a
  Target. The voices were counted over the English install's tables.
