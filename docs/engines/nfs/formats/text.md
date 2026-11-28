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

Every one of NFS III's retail tables has **1,929** strings, so a string's number is the
same in every language. String 0 is `BAD TEXT INDEX 0`, or empty.

The text is **Windows-1252**. `0x85` (…) and `0xA9` (©) are used, as
are the accented letters of each language. The game's fonts
([ffn.md](ffn.md)) have glyphs for the same code page.

### NFS III's files

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
| 0 | 1 | the font's size (below): 16, 18, 24 or 36 in the files |
| 1 | 1 | the string's colour, 0 to 5 (below) |
| 2 | 1 | its colour when it's picked, 0 to 5 *(inferred)* |
| 3 | 1 | how it's placed: 0 left, 1 right, 2 centred on x; 3, 4 and 5 the same over a shadow |
| 4 | u16 | x, 0 to 620, on the 640×480 screen |
| 6 | u16 | y, 0 to 447: the top of the string's line |
| 8 | u32 | the string's offset |

`nfs2sen.exe` reads each field with a function of its own (0x41b9a0 to
0x41ba60), and draws a string by them (0x405540):

- **The font** by the size (0x4056e0): 13 `swiss13.ffn`, 14
  `fixed14.ffn`, 16, 18 and 36 the `swiss` fonts of those sizes. 24 has
  none, and the font drawn before stays: 5 records have it. Neither
  program names `swiss24.ffn` or `swiss36a.ffn`.
- **The colours** are a table in the program (0x4bfd9c): 0 near black
  (0, 0, 8), 1 grey (0x90, 0x80, 0x80), 2 white, 3 brown (0x81, 0x54,
  0x12), 4 gold (0xF4, 0xCC, 0x28), 5 cyan (0x08, 0xF0, 0xF0). The caller
  says which of the two bytes is drawn, or a colour of its own. Seen on
  the hosted game: the main menu's items (0, 2) black, white as the
  cursor is on them; the settings' names (1, 2) grey, white; their values
  (3, 4) brown, gold.
- **The shadow:** with 3 to 5, the string is drawn first in near black
  one pixel right and down.
- **Seen on the hosted game:** about 25 strings on three screens (the
  main menu, the game's settings, the track's), each where its record
  puts it, in its font: the main menu's, right-aligned at x = 168, their
  tops at y plus the font's own offset; `CANCEL` (3) starting at x = 371 and
  `DONE` (4) ending at x = 351. Strings the code draws itself (the
  settings' values, the records' names and times) have x and y of 0, or
  a column's.

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
`TextTable::places`, with each one's colours and style. The data
viewer's Strings page shows them, NFS II SE's with their places and
sizes in the built-in font (key F).

`reluva-tool nfs text-dump <text.xxx>` prints each string with its number,
as UTF-8.

## Sources and checks

- **Where the facts come from:** the games' own files, and NFS II SE's `nfs2sen.exe` (its front end's records), read as each section above says, with the addresses it gives.
- **How it was checked:** the reader run on every language's table of both games to its end; the unit tests (`nfs.unit.formats`) and the game-files tests (`nfs.gamedata.formats`, `nfs.nfs2se.gamedata.formats`).

Worked out from hex dumps of the retail tables, October 2026, and
checked with a survey script: every table's offsets are in order and
inside the file, and every string but the last ends with a 0. NFS II
SE's front-end records from its hex dumps and a survey script over its
seven tables (November 2026), the sizes matched to its fonts' names;
then each field from `nfs2sen.exe`'s code that reads and draws them, and
the strings' places, sizes and colours measured on the hosted game's
front end.
