# Star Trek: Starfleet Command II: text tables (`.txt`)

> **Status:** decoded · **Code:** `src/engines/sfc/formats/text_tables.cpp` · **Checked on:** English retail 1.0, the four tab tables and four string tables · **Confidence:** confirmed unless marked

## Overview

The game keeps much of its data in text, of two kinds:

- **Tab tables,** saved from a spreadsheet: `Assets/specs/shiplist.txt`
  (every ship: race, hull, class, BPV, the years it's available, its
  weapons and systems; a copy in `MetaAssets/`), `Assets/specs/ftrlist.txt`
  (fighters and shuttles), `Assets/Sound/sounds.txt` (each sound's file
  and how it plays) and `Assets/Sound/SFCMusic.txt` (the music's pieces).
- **String tables,** the interface's text: `Assets/Strings/strings.txt`,
  `aaStrings.txt`, `quicktips.txt` and `shipnames.txt`.

## Layout

### Tab tables

8-bit text, lines ending CR LF, cells separated by tabs. A row whose
first cell is empty is a comment, as `shiplist.txt`'s first line says
("Keep the first column blank if you want to write a comment"); the
first row that isn't one names the columns. A cell can be in double
quotes, a quote in it doubled (`sounds.txt`'s comments are). Every row
of `shiplist.txt` has 190 cells; 1,337 of its 2,292 rows are ships.

`sounds.txt` explains its own columns in its comment rows: a sound's
identifier, its file in the zips, Dolby or 3D, compression ("Interplay's
2:1" or "4:1"), random groups,
sequences, looping, streaming and load groups. That the voices stored
as IMA ADPCM ([sound-zips.md](sound-zips.md)) are its "4:1" is *(guessed)*.

### String tables

UTF-16 little-endian without a byte-order mark, lines ending CR LF:

| Line | Meaning |
|---|---|
| `\|key\|text` | the text of a key (`\|tSetupTitle\|Star Trek: Starfleet Command`) |
| `\|\|text` | another line of the key before it (a ship class's names, in `shipnames.txt`) |
| no `\|` first | a heading (`WEAPON ICONS`, in `quicktips.txt`) |

Text can hold codes in tildes: `~F:federation8~` picks a font, `~X:6~`
a position, as their names suggest *(inferred)*. `aaStrings.txt` has one line
that is a lone `|`.

## In Reluva

- **Reading them:** `TabTable` and `StringTable`
  (`src/engines/sfc/formats/text_tables.h`). A string-table line with a
  single `|` is read as a key with no text.
- **The data viewer:** the Tables page shows each table, a row to a line,
  with or without the comment rows.
- **Commands:** `reluva-tool sfc table <file>` shows one.

## Sources and checks

- **Where the facts come from:** the files themselves, and their own
  comments.
- **How it was checked:** every table read, `shiplist.txt`'s rows all of
  190 cells; the string tables' entries
  counted (609 in `strings.txt`, 982 in `shipnames.txt`, 1,454 in
  `quicktips.txt`, 3,526 in `aaStrings.txt`).
- **Unit test:** `sfc.unit.formats` (tab table, string table), on tables
  built in the test.

## Open questions

- What each of `shiplist.txt`'s 190 columns means beyond its heading.
- The codes in the string tables' text.
- How the game takes the lone `|`.
