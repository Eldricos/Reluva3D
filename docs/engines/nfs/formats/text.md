# Need for Speed: text tables (`text.*`, `textsim.*`)

> **Status:** decoded · **Code:** `src/engines/nfs/formats/text_table.cpp` · **Checked on:** every language's table · **Confidence:** confirmed unless marked

## Overview

Strings, one table per language. NFS III's front end has them in
`FeData/text/`, each a table of offsets and then the strings; NFS II SE's
in `FeData/pc/text/` add each string's place on the screen
([NFS II SE's front end](#nfs-ii-ses-front-end)), and its race messages
(`GameData/DashHud/pc/textsim.*`) are as NFS III's.

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

### The files

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

### NFS II SE's front end

`FeData/pc/text/text.*`: English, French, German, Italian, Spanish,
Swedish and `text.met`. Records of 12 bytes, one per string, then the
strings, each ended by a 0:

| Offset | Size | Meaning |
|---|---|---|
| 0 | 1 | the font's size: 16, 18, 24 or 36, the sizes of its `swiss*.ffn` fonts *(inferred)* |
| 1 | 3 | each 0 to 5; not worked out |
| 4 | u16 | x, 0 to 620 |
| 6 | u16 | y, 0 to 447: on the 640×480 screen *(inferred)* |
| 8 | u32 | the string's offset |

The first string starts where the records end, so the count is the first
offset ÷ 12. Every table has **1,498** strings, their offsets in order
and inside the file; 45 of English's are empty. The text is
Windows-1252, as NFS III's.

### NFS II SE's race messages

`GameData/DashHud/pc/textsim.*`, the six languages: NFS III's layout,
15 strings ("INSERT CONTROLLER TO CONTINUE", "QUIT CURRENT RACE? (Y/N)"
...). The programs also name `gf8text.eng`, which isn't on the CD.

## In Reluva

Read as `TextTable` (`src/engines/nfs/formats/text_table.cpp`), and checked
on all 9 of NFS III's tables and all 13 of NFS II SE's. A table that
isn't NFS III's layout is read as NFS II SE's front end's, its places in
`TextTable::places`. The data viewer's Strings page shows them, NFS II
SE's with their places and sizes in the built-in font (key F).

`reluva-tool nfs text-dump <text.xxx>` prints each string with its number,
as UTF-8.

## Sources and checks

- **Where the facts come from:** the game's own files and `nfs3.exe`, read as each section above says, with the addresses it gives.
- **How it was checked:** the reader run on every language's table to its end; the unit tests (`nfs.unit.formats`) and the game-files test (`nfs.gamedata.formats`).

Worked out from hex dumps of the retail tables, October 2026, and
checked with a survey script: every table's offsets are in order and
inside the file, and every string but the last ends with a 0. NFS II
SE's front-end records from its hex dumps and a survey script over its
seven tables (November 2026), the sizes matched to its fonts' names.
