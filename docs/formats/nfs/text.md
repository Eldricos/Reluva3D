# Need for Speed III: front-end text (`FeData/text/text.*`)

Status: **implemented** (`src/engines/nfs/text_table.cpp`), and checked
on all 9 retail tables.

Worked out from hex dumps of the retail tables, October 2026, and
checked with a survey script: every table's offsets are in order and
inside the file, and every string but the last ends with a 0.

## Layout

All values are little-endian.

- A table of u32 offsets, one per string. The first offset is where the
  table ends, so the count is that offset ÷ 4.
- The strings, each ended by a 0. The last may run to the end of the
  file.

Every retail table has **1,929** strings, so a string's number is the
same in every language. String 0 is `BAD TEXT INDEX 0`, or empty.

The text is **Windows-1252**. `0x85` (…) and `0xA9` (©) are used, as
are the accented letters of each language. The game's fonts
([ffn.md](ffn.md)) have glyphs for the same code page.

## The files

| File | Language |
|---|---|
| `text.eng` | English |
| `text.fre` | French |
| `text.ger` | German |
| `text.ita` | Italian |
| `text.spa` | Spanish |
| `text.swe` | Swedish |
| `text.met` | English, but only 1,044 strings filled. 29 differ from `text.eng`, e.g. "Exit Game?" for "Exit to System?" and lower-case "save failed". It looks like an older copy; its use is unknown. |
| `text.cmt` | almost empty: 73 strings of one space; perhaps translators' comments |

Cars carry their own front-end text in `car.viv` (`fedata.eng`...), not
studied yet.

## Tools

`reluva-tool text-dump <text.xxx>` prints each string with its number,
as UTF-8.
