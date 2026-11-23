# Star Trek: Away Team: text scripts (`.RDS`)

> **Status:** decoded · **Code:** `src/engines/stawayteam/formats/rds_script.cpp` · **Checked on:** the US English retail `DATA.dat`: all 48 scripts, 1,378 entries (November 2026) · **Confidence:** layout confirmed; how the game reads it inferred from the files, not traced in its code

## Overview

The game's words: what the crew say, the briefings, the descriptions of
characters, items and objects, the messages at a mission's end. Each
`.RDS` file in `DATA.dat`'s `Levels/` is a script of labelled entries,
and the objects name an entry by its script and label:

```text
Script=Mission 01/Mission 01
Text=01 Mission Briefing
```

There are 48: one per mission (`Levels/Mission NN/Mission NN.RDS`), one
per crew member (`Levels/Marcus Refelian.RDS`...), and shared ones
(`Dialog Script`, `Banter Dialog`, `Character Descriptions`, `Inventory
Item Descriptions`, `Mission Objectives`, `Mission Ending Messages`...).

## Layout

Text in Windows-1252 (`’` is `0x92`), lines ending CRLF.

### Lines

| Line | What it is |
|---|---|
| `//...` | A comment: `//***Mission 02` heads each mission's part of a crew member's script |
| `:Label` | Starts an entry: the label is the rest of the line, as written |
| Anything else | The entry's text, up to the next label |

- **Labels** keep their spaces: `Both Scientists ` (with one at its end)
  and ` 12 Something is wrong` (with one at its start) are labels, and
  the objects name them so (`Mission Over Text=Both Scientists `). Some
  are cut short as the objects name them too (`A Klingon Scient`).
- **Text:** most entries are a line; 48 have paragraphs, with blank lines
  between them. Blank lines after an entry, before the next label or
  comment, aren't part of it. Two entries are empty.
- **Before the first label** there are only comments and blank lines.

The game's reader (`0x413f40`, the scripts found as `Levels/*.RDS`,
`0x4f9ea0`, the extension at `0x63eeec`): lines of up to 1024 bytes
(`0x4adfa0`); a `//` line skipped, wherever it is; a `:` line starts an
entry, its label the rest of the line as written; any other line, blank
ones too, is added to the entry with a line break after it, and an
entry's text loses its trailing tabs, spaces and line breaks as the
next starts or the file ends (`0x4e3450`, `0x5dfce0`, the set `\t \r\n`
at `0x64ab20`). A line before the first label is dropped (`0x5dfc00`).
The list starts with an entry `!None` of no text. A label is found by
the first entry with it, compared exactly, case and all (`0x414320`,
`0x4e38b0`). On the game's files this is what the table above says: a
blank line after a label would be kept at the text's start, and spaces
at the end of the last line dropped, but no file has either.
  Three crew members' scripts (Losak Wotril, Lyrit Bronail, Olanda
  Rewpolok) have no entries at all, only their comments.

### Markup in the text

Filled in or chosen by the game as it shows the text *(inferred)*:

| Markup | Uses | Seen in |
|---|---|---|
| `$trigger` | 2 | What a dead body or item is: "This is the dead body of $trigger." |
| `$inventory` | 1 | What it holds |
| `$KEY <command>`, `$key <command>` | 9 | The key for a command, in Missions 01 and 02's instructions: `($KEY PAUSE)`, `[$key action]` |
| `{Inventory ...}` | 1 | Text shown only if there's an inventory |

## In Reluva

- **Reading:** `readRdsScript` into an `RdsScript`: its entries, each a
  label and its text (lines joined with `\n`), found by label with
  `find`. The markup is kept as it is, for the game to fill in.
- **Commands:** `reluva-tool stawayteam data-check` reads every script
  in `DATA.dat`; `reluva-tool stawayteam text-show` prints one's entries.

## Sources and checks

- **Where the facts come from:** all 48 `.RDS` files in the retail
  `DATA.dat`, surveyed with a script: the kinds of line, the entries,
  blank lines in and after them, labels with spaces, the markup; and the
  objects that name their labels.
- **How it was checked:** `reluva-tool stawayteam data-check` reads all
  48, 1,378 entries, as the survey counted them; unit tests
  (`stawayteam.unit.formats`) on a script made in the test: comments,
  labels with spaces, paragraphs, empty entries, a script with only
  comments.

## Open questions

- **A line over 1024 bytes:** whether the game's line reader cuts it or
  carries the rest to the next line; no file has one.
