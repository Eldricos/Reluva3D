# LEGO Loco: the date-based surprises (`EE.INI`)

> **Status:** decoded · **Code:** `src/engines/legoloco/formats/easter_eggs.cpp` · **Checked on:** the CD's `art-res\EE.INI` and the resource file's `ee.ini`, version 1.0.18.2: every line read (November 2026) · **Confidence:** from the code unless marked

## Overview

`art-res\EE.INI` lists the surprises the game shows on certain dates and
at certain times: Easter in April, Halloween at the end of October,
Christmas in December. It's a Windows INI file with two sections,
`[TimeEvents]` and `[LoadEvents]`, whose keys are numbered `001`,
`002`... The game reads it from the disk, `art-res\EE.INI`, through
Windows' `GetPrivateProfileStringA` (`0x41f680`, and seen running), so
the copy in the [resource file](resource.md), `ee.ini`, isn't the one
read; the two hold the same events, the resource file's with a comment
naming the fields at its top.

## Layout

For each section, the game reads key `001`, then `002` and so on,
stopping at the first key that's missing or empty (`0x41f690`). The CD's
file has 18 time events and 129 load events.

### `[TimeEvents]`

In `loco.exe`: `0x41fbe0`, by `sscanf` with
`%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%hd,%ld,%c,%ld,%ld`. The names
are the resource file's copy's comment's:

| # | Field | Notes |
|---|---|---|
| 1 | start day | |
| 2 | start month | 0 for any |
| 3 | end day | |
| 4 | end month | 0 for any |
| 5 | start hour | |
| 6 | start minute | |
| 7 | end hour | |
| 8 | end minute | |
| 9 | resource | what to show: a resource number |
| 10 | frame set | 16-bit: the set it starts with |
| 11 | period | seconds: shown again after a random 1 to this many (below) |
| 12 | type | one letter, where the object starts ([below](#what-the-type-letters-do)); the CD has `S` and `P` |
| 13 | x | |
| 14 | y | |

How the game uses them, from its code: as it reads an event
(`0x41fbe0`) it makes the months 0-based (so 0, "any", becomes -1), and
sets the event's first showing a random 0 to `period` seconds from the
C runtime's `time()`. Each time it checks them (`0x41ff20`), an event
whose dates and hours hold and whose time has come is shown: its
resource, frame set, type and position are given to a new object of
the kind that crosses the screen (`0x423ab0`, `0x422ec0`), made only
while the game's measured frame rate is above a setting of its own, but
for resource 14433 (`0x3861`). Its next showing is then a random 1 to
`period` seconds on (0: a second; below 0, `rand() % (2 − p) + p`)
*(from the code)*. The CD's
are clouds, aeroplanes and a UFO, every month from 1:00 to 23:00, every
5 to 95 minutes.

#### What the type letters do

The object takes the letter in capitals and starts by it (`0x422f84`,
a switch over `C` to `W`), against the board's width and height
(`0x4aad0c`, `0x4aad10`) and the picture's own point (its hotspot)
*(from the code)*:

| Letter | The comment's word | Where it starts |
|---|---|---|
| `W` | World | at x, y |
| `R` | Random | anywhere on the board, by chance |
| `C` | Centred | in the middle |
| `P` | Port | at the right edge, at height y, or by chance for -1 |
| `S` | Starboard | off the left edge, its width outside, at height y, or by chance for -1 |
| `U` | | at the bottom, at x, or by chance for -1 |
| `D` | | at the top, at x, or by chance for -1 |

Other letters leave it where it's made. As it moves (`0x423560`), `P`,
`S`, `U` and `D` each have their own way, likely across the board from
where they start *(inferred)*; the CD's aeroplanes and clouds go both
ways (`P` and `S`).

`%c` takes the character straight after the comma, so the letter must
follow it with no space; the numbers may have spaces before them.

```text
[TimeEvents]
001=001,000,031,000,001,000,023,00, 14451,0,  310,S, 0,-1
```

### `[LoadEvents]`

In `loco.exe`: `0x41fb20`, six numbers by `%ld,%ld,%ld,%ld,%ld,%ld`:
the start day and month, the end day and month, a resource number and
another to use in its place between those dates. The CD's date ranges
are 1 to 14 April, 11 June to 11 July, 30 October to 1 November, and
in December: Easter's backdrop, pond and Toybox handle, the desert's,
Halloween's and winter's and Christmas's in place of the usual ones.

The game applies them as it starts (`0x41f970`): for each whose dates
hold, by `localtime(time())`, it puts the second resource's name in the
first's place (`0x447290`). On 31 October it also marks every resource
from 6144 to 6542 (`0x1800` to `0x198e`: the trains and wagons among
them) with `0x400` at their `+0x164`: the mark a definition's
`semi-transparent` keyword sets (`0x425084`), so the trains are drawn
see-through, as ghosts, on Halloween *(from the code)*; not yet seen. Seen in Reluva with the game's command line (below): at
Easter the board's backdrop green, the pond with eggs and the Toybox's
handle striped; at Halloween the backdrop dark, the handle a bone,
skulls in the pond.

### The season on the command line

The game's command line (`0x406790`, from `WinMain` at `0x46308c`) may
name a season, which it then takes whatever the date: `Easter` (taken
as 1 April), `Desert` (11 June), `Halloween` (31 October), `Winter`
(2 December) or `XMas` (25 December). `/s`, `-s` or `s`, Windows'
switch for starting a screen saver, starts it as one (`0x4068b5`): it
takes the resources from the folder it's in rather than `LEGO.INI`'s
`RemoteRes` (`0x406ab8`), and plays a board from `art-res\ScrSaver` as
`LEGO.INI`'s `[ScreenSaver]` section says (`0x408876`, `0x4481e1`)
*(from the code)*.

```text
[LoadEvents]
001=001,004,014,004, 1024, 1080
```

## In Reluva

- `src/engines/legoloco/formats/easter_eggs.cpp`: `parseEasterEggs`
  reads the two sections as the game does, key by key until one is
  missing, into `TimeEvent` and `LoadEvent`. A line it can't read whole
  fails, where the game would take the fields before the fault *(inferred)*;
  the CD has none.
- `reluva-tool legoloco ee <game folder>` prints both sections.
- The engine's "Season" setting (`season`), and `reluva-tool legoloco run
  -season <word>`, give the game one of the command line's seasons
  (`GameProcess::setSeason`).

## Sources and checks

- **Where the facts come from:** the fields and their order from
  `loco.exe`'s readers at the addresses given, and their names from the
  comment in the resource file's copy; what they do from the code that
  applies the load events, checks the time events and reads the command
  line, at the addresses given; the type letters' names are the
  comment's, what they do the code's.
- **How it was checked:** `reluva-tool legoloco ee` reads every line of
  both copies, and prints the same events from each. The unit tests
  (`legoloco.unit.easter_eggs`) read lines in the files' shape, stop at
  a missing key, and refuse a short line. The game run with `-season
  Easter` and `-season Halloween`, beside a run with neither, showed the
  replacements on the board and the Toybox.

## Open questions

- How each type letter's object moves, and whether the Halloween trains
  look as their mark says.
