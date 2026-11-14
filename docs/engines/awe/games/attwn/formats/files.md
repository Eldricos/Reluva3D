# And Then There Were None: file names and scrambled text

> **Status:** decoded · **Code:** `src/engines/awe/attwn/files.cpp` · **Checked on:** the English release: all 479 `.act` files, 1,351 `.aca` and `.acg` pictures, 2,124 `.acv` sounds · **Confidence:** from the code unless marked

## Overview

The game asks for its files by ordinary names (`text/itemlist.txt`,
`inventory\%s\inv%s.tga`) but the install has most of them under other
extensions: `.act`, `.aca`, `.acg` and `.acv`. Only the text is changed
inside; the rest are the ordinary files renamed.

## Layout

### Names

Before opening a file, `ac.exe` makes its extension's first two letters
"ac" (`0x403bd0`: the three letters after the last dot, if there's a dot
four from the end) and asks whether that file is there (`0x403c20`,
`CreateFileA`). If it is, it opens it; if not, the name as asked.

| Asked for | Opened | What it is |
|---|---|---|
| `.txt` | `.act` | text, scrambled (below) |
| `.tga` | `.aca` | a TGA (in the install, 32-bit uncompressed) |
| `.jpg` | `.acg` | a JPEG |
| `.wav` | `.acv` | a WAV (IMA ADPCM or PCM) |

These four are the kinds the executable opens so; `.ac1` scenes and
saves, `.gr2`, `.bik` and `.mp3` are opened by their own names.

### Scrambled text

The text opener (`0x4028f0`) reads an `.act` file's first u16 as a count of
lines and sets the file as scrambled (`[0x485f20]`, the count kept at
`[0x485f1c]`); the line reader (`0x402980`) then reads each line as:

| Field | Type | |
|---|---|---|
| length | u16 | the line's bytes |
| bytes | u8 × length | each XORed with 0x30, its line ending (`\n`, or `\r\n` in the tables) included |

until the count is spent. Without an `.act`, the same reader takes the
`.txt` a line at a time (`fgets`). The text itself is 8-bit (Windows-1252),
not UTF-16 as Orient Express's: tab-separated tables, scripts and event
files in the same family as
[Orient Express's](../../motoe/formats/text.md).

## In Reluva

`attwn::GameFiles` is the install as the game sees it: it lists each
`.act`, `.aca`, `.acg` and `.acv` file by its ordinary name, reads a name
from its "ac" file when there is one, and unscrambles text
(`attwn::unscrambleText`). The rest of the engine reads the game's files
through it (`openGameFiles`, `games.h`), so the readers don't know.
`reluva-tool awe table-show` reads an `.act` file directly.

## Sources and checks

- **Where the facts come from:** `ac.exe` (593,920 bytes), the functions
  above; the `.act` format was first found on an event file (`wait .7`
  with 0x30), then read in the code.
- **How it's checked:** `reluva-tool awe files-check <game folder> -text
  -sound` over the English install: all 479 text files and 2,138 sounds
  read; every `.act` ends where its count of lines says. The data viewer's
  Pictures, Text and Sounds pages show them. `awe.unit.attwn` checks the
  names and the unscrambling on files made in the test.
