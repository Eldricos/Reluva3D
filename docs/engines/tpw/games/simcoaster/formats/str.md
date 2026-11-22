# SimCoaster: string tables (`.str`) and the character set

> **Status:** decoded · **Code:** `src/engines/tpw/formats/str.cpp` · **Checked on:** the US release's 37 string tables and its character set · **Confidence:** confirmed unless marked

## Overview

The game's text is in string tables in its language's folder,
`Data/Language/American/`: 37 of them, from `MONTHS.str` to `UITEXT.str`
(994 strings: the interface) and `TAG_SYSTEM.str`, 3,775 strings in all.
Their text is in a character set of the game's own, which
`MBToUni.dat` ("multibyte to Unicode") maps to Unicode, so it reads the
same whatever the Windows code page; `UniToMB.dat` is the other way
round, for what the player types *(inferred)*.

## Layout

All numbers little-endian.

### The character set (`MBToUni.dat`)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `BFMU` |
| 4 | 2 | 0 |
| 6 | 2 | How many codes: 250 |
| 8 | 2 each | Each code's Unicode character, codes counted from 1 |

Code 0 isn't a character. Code 1 is `*`, 2 to 11 the digits `1` to `9`
and `0`, 12 a space, 13 a tab, 14 a new line, 15 to 19 `.`, `,`, `$`,
`£` and `-`, 20 U+0007 (a control character: what the game does with it
isn't known), 21 `™` (U+2122) and 22 `’` (U+2019). From 23 on come the
rest of Latin-1's characters, from `ÿ` down to `!`. The strings use
codes 1 to 229.

### A string table

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `BFST` |
| 4 | 4 | The table's number: 1000 for `MONTHS.str` to 1036 for `TOTD.str`, the tips of the day |
| 8 | 4 | How many strings |
| 12 | 4 each | Where each string is, counted from offset 12 |

Three tables have no strings (`CHAT_COMMANDS.str`, `FEMALE_NAMES.str`,
`OBSTACLES.str`).

### A string

Parts, each starting on a multiple of 4 bytes from the string's start:

| Part | Bytes | What |
|---|---|---|
| Text | 1, then the text's length (2 bytes), then 0; then the text, in the character set's codes; then zeros to the next multiple of 4 | Text |
| Parameter | 2, its number (from 0), 0, 0 | The place of something the game fills in: "If you succeed you'll receive {0} Golden Tickets." |
| End | 0, 0, 0, 0 | The string's end |

A string may have several of each (4,154 texts and 379 parameters in
all); every string ends with its end part, and the next starts straight
after it.

## In Reluva

`CharacterSet` and `StringTable` (`src/engines/tpw/formats/str.cpp`)
read the character set and the tables, each string as its parts, its
text in UTF-8. `reluva-tool tpw str-check <game folder>` reads every
table in the language's character set, with a table's name
(`UITEXT`) lists its strings, each parameter's place as `{n}`; the
game-files test runs it (`tpw.gamedata.text`).

## Sources and checks

- **Where the facts come from:** the US release's tables and
  `MBToUni.dat` (November 2026): the character set from its codes for the
  digits and the space, then every table read with it, with nothing left
  over; the parts from the strings with parameters, whose texts read
  round them ("You will also receive {1} shares.").
- **How it was checked:** all 37 tables read with `reluva-tool tpw
  str-check`, every string to its end part and every code in the
  character set; the text reads as English throughout, `’`, `©` and `™`
  included. The unit tests (`tpw.unit.formats`) read a character set and
  a table built in the test, and turn away damaged ones.

## Open questions

- `residx.dat` ("BFRI"), beside the tables: an index of the folder's
  files by number and kind (75 entries: the tables, the fonts and the
  `.dat` files), not read yet.
- `UniToMB.dat` ("BFUM"), not read yet.
- Whether a parameter part's other two bytes mean anything (always 0
  here).
