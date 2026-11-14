# And Then There Were None: dialogue

> **Status:** partial · **Code:** `src/engines/awe/game/dialogue.cpp` (shared), `src/engines/awe/games.h` (`DialogueTables`, `DialogueRules`), `src/engines/awe/attwn/attwn.cpp` (this game's) · **Checked against:** the code (`ac.exe`), the English install's dialogue tables and voices · **Confidence:** from the code unless marked

## Overview

Conversations are Orient Express's
([../../motoe/systems/dialogue.md](../../motoe/systems/dialogue.md)): parts,
threads and nests of lines, read from tab-separated tables, each line with
its voice, conditions and commands. This page has what differs: where
the tables are and how they're read, and how `ac.exe`'s dialogue manager
plays them. Its box isn't drawn as this game's yet (Reluva draws Orient
Express's): that's the interface's, compared with it.

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

### The codes

The loader codes a part as Orient Express's (a letter from 0, a number n
as n + 99). A thread's letter is its first character alone and a number n
is n + 0x19 (`0x431696`), so `1`, `2` and `3` are 0x1a to 0x1c; a nest is
its first letter alone, whatever it is (`0x43177a`). Orient Express adds a
second character's place, and numbers threads and nests from 0x60. The
English tables' codes come out in the same order either way: their
threads are single letters, `0`, `1` and `3`, their nests single letters
but in `deadonarrival` (6B), whose seven lines are all nest `0`, so
alternatives in both. Nothing talks to `deadonarrival`: only a movie has
that name.

### The dialogue manager

It's Orient Express's
([../../motoe/systems/dialogue.md](../../motoe/systems/dialogue.md),
whose sections this follows) but for what's below. "DM" is the manager
(`[0x485f38]+0xf4`).

**The talk command** (`0x40d5f0`). `talk <name> <code> [option]`: the
code's letters as Orient Express's, a number n part n + 99; one option,
`nogen`, `forcegen` or an alternative's number. No `nogreet`, no second
option, and a `+` isn't special. A talk that finds nothing then gives
Narracott's "nothing to say" whatever started it (`0x40d74f`), where
Orient Express gives it only after a click on a character. (`talka` reads
a number as a thread's, n + 0x19, a slip no script meets: nothing uses
`talka`.)

**Starting** (`0x4325e0`, Start with six arguments: no `plus`, no
`greet`).

- No change to the inventory.
- The character's name is the first speaker who isn't `narracott`
  (`0x4323d0`), and their generic conversations are named `<speaker>gen`
  (`armstronggen`), matched exactly (`stricmp`, `0x4324f4`) where Orient
  Express's `gen_<speaker>_` is any name that contains it.
- No regreet and no visit tags: the opening (thread `1`) until it's been
  heard to its end, then thread `2` if the part has one (none in the
  English release), else the choices at once (`0x4328fa`).
- The goodbye: `<speaker>exit`, one of parts 0 to 3 at random, 4 ("Good
  evening, Doctor.") for the fourth after dark (`word[0x485f10]` above 0,
  as Orient Express's time of day). No finale goodbye and no `gen_exit`
  fallback (`0x432ad0`).
- **The box** takes its height from the rows of the choices' text (each
  line's +0x650): up to 3 rows 90 pixels and the first window, 4 rows 115
  and the second, more 141 and the third (`0x4328b6`; the windows at
  `[0x485f38]+0x88`, `+0x8c`, `+0x90`, each made from
  `interface\dialoguewin%02d.tga` at y 600, `0x415b2f`).
  The rows are the part's choices', its character's extras', less one for
  the part's own goodbye and plus one for the new, though neither was
  counted. It slides in from the foot of the 600-line frame (`0x258`).

**"Nothing to say"** (`0x4325a0`): `narracottgen` part 5, "I have nothing
more to say to her", when the list `brentgen claythornegen erogersgen
mothergen` contains the character's generic name, else part 6, "to him".
The test is a "contains" (`0x402a20`), so with no one talked to yet it's
"her". The name is the last talk's that found its part, as in Orient
Express.

**Choosing a line** (`0x431e70`): with several that may be said, the one
the talk's number asks for, else one at random other than the last; it
isn't kept for the rest of the talk, as Orient Express keeps it, and its
place counts among all the nest's lines, said or not. The voice's `_n` is
that place. Whether a thread is offered (`0x433040`) looks at its first
nest alone; a finished one only in the first menu and if it has a line to
come back to, its condition not tested again; the alternative the asking
picks is put back.

**The choices** (`0x432e10`): a repeatable thread offered by the first
menu isn't made unfinished again, so later menus of the talk don't offer
it. One choice left: the goodbye is said without asking, as in Orient
Express; any other is offered, not played (`0x432ec1`). No `+` to go on
to a next part. The box is sized again by the choices' rows, at once
(`0x43301a`).

**A line** (`0x433440`, `0x4333b0`, `0x433640`).

- Its colour (`0x433220`) is a colour, not `fonts.txt`'s: `0xffd2aa6e`
  for Narracott and for the choices, `0xffc08a6f` and `0xff8f9cb0` for
  the others, alternating when the speaker changes. Each of Narracott's
  lines starts them again: the next other speaker has the first.
- Its speaker's character is the scene's object of that name; for `brent`,
  failing that, `brentknit`, then `brentwig` (`0x433340`).
- Without a voice it lasts its length over 10 in whole seconds, at least
  2 (`0x4333f7`).
- Its event file is looked for in `Acts`, `location`, then `general`
  (`0x43357a`).
- The voice: `soundo\`, the name, act and codes as Orient Express's,
  `_n` and `.wav`; a borrowed thread's by the character's generic name
  (a goodbye's by `<speaker>exit`).

**A line's end** (`0x433920`). The Povent's `juncture` and `terminate` as
Orient Express's; no `advance`. The box stays up if the Povent has
`talk ` (with its space) and not `fade `, or has `leavebox`; Orient
Express: `talk` or `leavebox`, and not `nobox`. That differs for three
Povents in the English tables (`fade out, cutscene ..., fade in, talk
fridaynightdinner c` and two like it), where this game's box goes. The
Povent runs in the main script with `mainloop` or `terminate`, or with no
next line and no other thread offered, the goodbye counting
(`0x432dc0`); else beside the dialogue.

## In Reluva

`DialogueTables` (`games.h`) is how a game's tables are read: whether the
7th column is a Target, whether Duration and Optional are read, whether a
translation takes the line's place, the box's width, and the events'
folders. `DialogueRules` is how its manager differs, each of the above.
Orient Express's are the defaults; this game's are in `attwn/attwn.cpp`
with the addresses. The shared reader (`DialogueBook::add`), the manager
(`DialogueManager`) and the box take them from there.

Not yet: the box drawn as this game's (its three windows, where the
choices go, the colours above), with the interface; and the codes as this
loader makes them, which put the same tables in the same order.

## Sources and checks

- **Where the facts come from:** `ac.exe`'s loader and the paths above.
- **How it's checked:** `awe.unit.attwn` reads a table of this game's
  layout and shows Orient Express's reading of it turning Tone into a
  Target; it plays conversations by this game's rules (the box's heights,
  the colours, no regreet, a lone choice offered, the talk command's
  options, Narracott's "her" and "him"). The voices were counted over the
  English install's tables; the box's rule over their Povents.
