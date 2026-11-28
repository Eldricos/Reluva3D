# Jedi Knight: string tables (`.uni`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/strings.cpp` · **Checked on:** US retail, all 7 string tables · **Confidence:** confirmed unless marked

## Overview

The text the game shows, by key, in plain text with Windows line ends:

| File | Strings | What |
|---|---|---|
| `Res1hi.gob` `ui\jkstrings.uni` | 787 | The menus, the HUD, key names, errors |
| `Res1hi.gob` `ui\credits.uni` | 451 | The credits |
| `Res1hi.gob` `ui\cutstrings.uni` | 187 | The cut scenes' subtitles |
| `Res2.gob` `misc\sithstrings.uni` | 25 | The engine's own messages |
| Each episode's `misc\cogstrings.uni` | 140, 6, 49 | The levels' names, objectives and messages |

## Layout

Line by line:

- **Blank lines and lines starting `#`** are left out.
- **`MSGS <count>`** first. The count is at least the number of strings,
  but not always equal (900 for 787 strings): a limit. `JK.EXE` says as
  much when it reads `jkstrings.uni`, for a debugger: `Premature 'END'
  found after only 787 lines in 'ui\jkstrings.uni'. Check number in 'MSG
  xxx' header.` ([systems/hosting.md](../systems/hosting.md)).
- **An entry:** `"<key>"  <number>  "<string>"`, spaced with tabs or
  spaces, and perhaps a `#` comment after it, which may hold quotes of its
  own. The number is 0 in every entry.
- **`END`** ends the table; anything after it is left out.

A key is everything between its quotes: 7 keys in `jkstrings.uni` go
on after the name with their arguments' kinds (`"ERR_CANNOT_FIND_FILE %s
%s"`), and 4 in `credits.uni` have a space in them (`"CREDITS_ small_7"`).

A string runs to the next quote: there's no escape for a quote, and
`"\"` is a backslash, and `\n` is kept as written, a backslash and an
`n`: the reader has no escapes (`0x42f400` copies the string byte by
byte) *(from the code)*. The one string with `\n` in the files,
`ERR_MAY_BE_RELATED_TO` in `jkstrings.uni`, is never shown: `JK.EXE`
doesn't name its key, nor builds it (its built keys are `COG_%05d`,
`GOAL_%05d` and the ranks'), and no other file names it *(from the
code)*. One credits line,
`CREDITS_big_446`, has its string without quotes (`Paul Zabierek`),
which the game reads as empty ([Reading a table](#reading-a-table)).

## Behaviour

### Reading a table

In `JK.EXE`: `0x4359b0`, its words found by `0x42f210` (between two
quotes) and `0x42f1a0` (up to a space or tab).

- **The key** is the text between the line's first two quotes, all of
  it. A line without them can't be read: it's left out, though counted,
  with "Cannot understand this line in string table" for a debugger
  *(from the code)*.
- **The number** is the word after the key, kept as a number; a line
  that ends with it can't be read either *(from the code)*.
- **The string** is the text between the next two quotes after the
  number. Without them it's empty *(from the code)*: `CREDITS_big_446`
  is read as an empty string, and the credits show no name there
  ([The credits](#the-credits)).
- **Each string** is widened to 16 bits a character, byte by byte
  (`0x42f400`), and kept in the file's order, with its key and number
  *(from the code)*.
- **A key twice** gives "The key '%s' is in the string table '%s' more
  than once." for a debugger *(from the code)*.

### The credits

In `JK.EXE`: `0x421c40` to `0x421dae`.

- **Each string is a line**, in the table's order, drawn with
  `creditlarge.sft` when its key holds `big` and `creditsmall.sft`
  otherwise (`0x421d0e`) *(from the code)*.
- **An empty string** is still a line: its height is the lines it takes
  times the font's line height, and an empty string takes one
  (`0x4357c0`, `0x4352c0`), so it leaves a blank line of its font. A
  missing string would be drawn as a space (`0x421d4d`), but the reader
  never leaves one missing *(from the code)*. The file's own blank lines
  between sections are empty `big` strings (`CREDITS_big_3 ""`).
- **`CREDITS_big_446`** (Paul Zabierek) comes right after the last
  tester, Jason Yunker, and before the blank `CREDITS_big_316`: the
  running game shows no name there, but two blank lines, one more than
  between the other sections.

### Looking strings up

- **By key**, in a hash table, whole and exact, case and all (`0x437d80`).
  The 4 keys with arguments that `JK.EXE` names, it names by their whole
  text (`"ERR_CANNOT_LOAD_FILE %s"`, `0x5255d4`; `"ERR_BAD_EPISODE_FILE
  %d %d"`, `0x5264f0`; `"ERR_CANNOT_SET_PLAYER %s"`;
  `"ERR_INVALID_EPISODE %s"`), and puts its arguments in the string it
  finds (`0x41f090`). It doesn't name the other 3
  (`ERR_CANNOT_FIND_FILE`, `ERR_CANNOT_FIND_CUTSCENE`,
  `ERR_UNABLE_TO_START_NETWORK`) *(from the code)*.
- **A key not found** gives its own text back, up to 63 characters
  (`0x435fe0`) *(from the code)*.
- **By place:** the cut scenes' subtitles are looked up by their place in
  `cutstrings.uni`, not their keys ([smk.md](smk.md#subtitles))
  *(from the code)*.

## In Reluva

`parseStrings` (`src/engines/sith/formats/strings.h`) reads one into a
`StringTable`: the string after the number, empty when it isn't quoted,
`\n` kept as written, as the game reads it. The data viewer's
Strings page lists them. `reluva-tool sith strings <install>
<file.uni>` prints one table, and `reluva-tool sith formats <install>`
reads them all.

## Sources and checks

- **Where the facts come from:** the seven tables, every line of them;
  the comments at their tops (`"<key>" <unused number> "<string>"`);
  `JK.EXE`'s machine code ([Behaviour](#behaviour)) and the keys in its
  strings; the credits screen run by `reluva-tool sith host` (main
  menu, *Credits*), a frame every 2 seconds.
- **How it was checked:** every table read by `reluva-tool sith
  formats`, the count of strings compared with a count by hand;
  `sith.unit.formats` on a table with each of the cases above.
