# Star Trek: Birth of the Federation: text tables (`.tdb`)

> **Status:** decoded · **Code:** `src/engines/botf/formats/text_table.cpp` · **Checked on:** all 22 tables of the English retail `stbof.res`, three of them read whole (October 2026) · **Confidence:** confirmed unless marked

## Overview

22 tables in `stbof.res` hold the game's longer text: every string and its
key are readable; what the keys mean is open.

| Tables | Strings | Text |
|---|---|---|
| `cdiplomt`, `fdiplomt`, `hdiplomt`, `kdiplomt`, `rdiplomt` | 221 each | what each major race says in diplomacy |
| `minor1` to `minor9` | 71 each | what the minor races say |
| `treaty` | 10 | treaty descriptions ("%s1 and %2 have formed a strategic alliance...") |
| `inttext0` to `inttext4`, `intqueue` | 60 to 301 | intelligence reports |
| `teachtip` | 394 | tutorial tips |

## Layout

All values little-endian.

| Offset | Type | Field |
|---|---|---|
| 0 | `u32` | count |
| 4 | `u32` | ?: a value like `0x005A0628`, different in every table: left over from the tool, like `sfx.snd`'s ([snd.md](snd.md)) *(guessed)* |
| 8 | `u32` | longest: the longest string's length, zero byte included |
| 12 | `u32` | 0 |
| 16 | | count records of 16 bytes (below) |
| 16 + 16 x count | | the strings, each ending in a zero byte, packed with no gaps |

A record:

| Offset | Type | Field |
|---|---|---|
| 0 | `u32` | key |
| 4 | `u32` | length of the string, zero byte included |
| 8 | `u32` | ?: like the header's second value, different in every record |
| 12 | `u32` | offset of the string, from the end of the records |

The strings aren't stored in record order: `treaty.tdb`'s record 4 points
at offset 0.

### The strings

All plain ASCII in the English tables. Each string starts with a space and
ends with a newline before its zero byte. Placeholders, filled in by the
game: `%1`, `%2`, `%3` and `%s1` (in the three whole tables).

### Keys

Records are sorted by key in most tables; in `intqueue`, `inttext2`,
`inttext3` and `inttext4` they aren't, and keys may repeat there. The keys
look structured but aren't understood yet:

- diplomacy, minor-race and treaty tables: the third byte is `0x41`,
  `0x42`, `0x44` or `0x48` (`A`, `B`, `D`, `H`), the low byte varies
  (`07`, `08`, `37` in `treaty.tdb`; `92`, `93`, `94` together for a
  proposal and its two answers in the minor tables). The five diplomacy
  tables share their key layout (61, 63 and 3 keys of the first three kinds
  in the first 2 KB).
- `teachtip.tdb`: the ids of the widgets the tips are for (the window
  layouts', [wdf.md](wdf.md)): 8001 to 8006 the game menu's buttons, as
  the game shows them, and 1002 the galaxy screen's Map button ("Click
  here to toggle the zoom level of the galactic map.").
- the intelligence tables: high words from 648 to 1,340.

## In Reluva

`TextTable` (`src/engines/botf/formats/text_table.cpp`) reads every string
and its key, decoding Windows-1252 to UTF-8 (Windows-1252 has every
accented letter the fonts have, for other languages). reluva-viewer's Birth
of the Federation Text page shows each table's strings, wrapped, in a game
font, leaving out the leading space and the closing newline. The unit tests
(`botf.unit.tables`, "text tables") cover it.

## Sources and checks

- **Where the facts come from:** the first 2 KB of all 22 tables in the
  English `stbof.res`, and three whole tables, `treaty.tdb`, `minor6.tdb`
  and `cdiplomt.tdb` (`tools/botf-re/botf_res_sample.py --ext tdb`),
  October 2026.
- **How it was checked:** in the three whole tables (10, 71 and 221
  strings) every string ends in a zero byte, the strings tile the text with
  no gaps or overlaps, and the last ends at the end of the file. In the
  2 KB samples, `longest` equals the longest length in every table whose
  records all fit, and the header's last value is 0 in all 22. All 22
  tables then loaded in Reluva's viewer.

## Open questions

- What the other tables' keys mean, and which key the game asks for in
  which situation: for the running game to show.
- The header's second value and each record's third.
