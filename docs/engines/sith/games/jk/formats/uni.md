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
`"\"` is a backslash. `\n` in a string is a new line *(inferred: the
error messages use it between paragraphs)*. One credits line,
`CREDITS_big_446`, has its string without quotes.

## In Reluva

`parseStrings` (`src/engines/sith/formats/strings.h`) reads one into a
`StringTable`: the string after the number, or the rest of the line when
it isn't quoted. The data viewer's Strings page lists them. `reluva-tool
sith strings <install> <file.uni>` prints one table, and `reluva-tool
sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the seven tables, every line of them;
  the comments at their tops (`"<key>" <unused number> "<string>"`).
- **How it was checked:** every table read by `reluva-tool sith
  formats`, the count of strings compared with a count by hand;
  `sith.unit.formats` on a table with each of the cases above.

## Open questions

- How the game reads the unquoted credits line, and whether it looks up
  the keys with arguments by their whole text.
