# id Tech 3: Raven's string packages (`strip/*.sp`)

> **Status:** decoded · **Code:** `src/engines/idtech3/raven/strings.cpp` · **Checked on:** Jedi Outcast 1.02a's 30 packages, 2,039 entries (`reluva-viewer`'s Text page, November 2026) · **Confidence:** confirmed unless marked

## Overview

The game's text, from the menus' labels to the cinematics' subtitles,
kept apart from its code by Raven's StripEd tool: a package per level and
per part of the game (`MENUS0` to `MENUS3`, `INGAME`, `SVINGAME`...), each
entry by a reference name, in English, French and German. Jedi Outcast's
are in `base/assets1.pk3`. An entry is named `PACKAGE_ENTRY`: the menus'
scripts write `@MENUS1_NEW` for the text of entry `NEW` of `MENUS1`.

## Layout

Text, Latin-1, a key and its value on each line (CR LF):

```
VERSION 1
CONFIG W:\bin\striped.cfg
ID 7
REFERENCE INGAME
DESCRIPTION "in game text for single player and MP items"
COUNT 200
INDEX 0
{
   REFERENCE OBJECTIVES
   TEXT_LANGUAGE1 "OBJECTIVES:"
   TEXT_LANGUAGE2 "OBJECTIFS:"
   TEXT_LANGUAGE3 "ZIELE:"
}
```

| Key | Where | Value |
|---|---|---|
| `VERSION`, `CONFIG` | the package | 1; the tool's settings file |
| `ID` | the package | its number (`INGAME` 7, `SVINGAME` 32) |
| `REFERENCE` | the package, an entry | its name |
| `DESCRIPTION` | the package | quoted |
| `COUNT` | the package | how many entries (always the number of `INDEX` lines) |
| `INDEX` | before an entry's `{` | its number |
| `TEXT_LANGUAGE1` to `3` | an entry | English, French, German, quoted; some entries lack 2 or 3 |
| `NOTES` | an entry | quoted, for the translators |
| `FLAGS` | an entry | `SP_FLAG9` (51 entries), `SP_FLAG2` (1) |

A quoted value runs from the line's first quote to its last, so quotes
inside it stay (`"I hate it when you say "troubling.""`); `\n` in it is a
line break, the only escape the packages use.

## In Reluva

`parseStringPackage` reads one package, refusing a key it doesn't know or
an entry left open; `StringTable` reads every `strip/*.sp` and finds an
entry by `PACKAGE_ENTRY`, whatever its case, in a language, an entry with
no text in it giving its English *(guessed)*. The data viewer's Text page shows every package in the game's
fonts ([fonts.md](fonts.md)), in each language.

## Sources and checks

- **Where the facts come from:** the packages themselves, every key and
  value counted across all 30; the names from the menus' scripts
  (`ui/*.menu`, `ui/jk2mp/*.menu`).
- **How it was checked:** every package reads, its `COUNT` equal to its
  entries; a unit test (`idtech3.unit.formats`, "raven text") reads a
  made-up package with a quote inside a value and a line break, and looks
  entries up.
