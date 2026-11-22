# LEGO Loco: the date-based surprises (`EE.INI`)

> **Status:** decoded · **Code:** `src/engines/legoloco/formats/easter_eggs.cpp` · **Checked on:** the CD's `art-res\EE.INI` and the resource file's `ee.ini`, version 1.0.18.2: every line read (November 2026) · **Confidence:** from the code unless marked

## Overview

`art-res\EE.INI` lists the surprises the game shows on certain dates and
at certain times: Easter in April, Halloween at the end of October,
Christmas in December. It's a Windows INI file with two sections,
`[TimeEvents]` and `[LoadEvents]`, whose keys are numbered `001`,
`002`... The game reads it from the disk through Windows'
`GetPrivateProfileString` (`0x41f680`), so the copy in the
[resource file](resource.md), `ee.ini`, isn't the one read *(inferred)*;
the two hold the same events, the resource file's with a comment naming
the fields at its top.

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
| 10 | frame set | 16-bit |
| 11 | period | |
| 12 | type | one letter: World, Random, Centred, Port or Starboard, by the comment; the CD has `S` and `P` |
| 13 | x | |
| 14 | y | |

`%c` takes the character straight after the comma, so the letter must
follow it with no space; the numbers may have spaces before them.

```text
[TimeEvents]
001=001,000,031,000,001,000,023,00, 14451,0,  310,S, 0,-1
```

### `[LoadEvents]`

In `loco.exe`: `0x41fb20`, six numbers by `%ld,%ld,%ld,%ld,%ld,%ld`:
the start day and month, the end day and month, a resource number and
another to use in its place *(guessed)* between those dates. The CD's
date ranges are 1 to 14 April, 11 June to 11 July, 30 October to 1
November, and in December.

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

## Sources and checks

- **Where the facts come from:** the fields and their order from
  `loco.exe`'s readers at the addresses given, and their names from the
  comment in the resource file's copy. The type letters' meanings are
  only the comment's.
- **How it was checked:** `reluva-tool legoloco ee` reads every line of
  both copies, and prints the same events from each. The unit tests
  (`legoloco.unit.easter_eggs`) read lines in the files' shape, stop at
  a missing key, and refuse a short line.

## Open questions

- What the time events' period, type and position do, and what the load
  events' second number does: seen running on those dates, or in the
  code that uses them.
