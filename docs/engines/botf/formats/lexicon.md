# Star Trek: Birth of the Federation: the interface's words (`lexicon.dic`)

> **Status:** decoded · **Code:** `src/engines/botf/formats/lexicon.cpp` · **Checked on:** the English retail `lexicon.dic`: 1,460 entries (October 2026) · **Confidence:** confirmed unless marked

## Overview

The words and short phrases the interface shows: option values ("Early",
"Many", "Normal"), race, star and planet type names, labels. One file,
81,577 bytes in the English `stbof.res`; it differs in the German one.

## Layout

| Offset | Type | Field |
|---|---|---|
| 0 | `u32` | where the table is |
| 4 | | the strings, zero-terminated, Windows-1252 |
| table | `u32` | 952 in the English file; not known |
| table + 4 | `u16` | how many entries |
| table + 6 | | per entry: `u32` the string's offset, `u32` its length (with the zero) |

The entries are in roughly alphabetical order, phrases with `%1`/`%2`
placeholders among them ("%s1 accepted an affiliation proposal from %2.").

## Behaviour

Which entry a screen shows isn't in the file: the executable picks it. The
numbers the game setup screen uses are from the Ultimate Editor
(`ue/edit/res/stbof/files/dic/idx/LexDataIdx.java`, GPL-2.0, read for the
numbers only; see [tables.md](tables.md#other-sources)), where each is
marked "confirmed by value change": its authors changed the entry and saw
the screen change. In `src/engines/botf/formats/lexicon.h` (`lex::`):

| Setting | Entries |
|---|---|
| Civilization level | 88 Beginning, 252 Early, 234 Developed, 312 Expanded, 4 Advanced |
| Minor races | 872 None, 358 Few, 1161 Some, 622 Many |
| Difficulty | 1150 Simple, 256 Easy, 873 Normal, 433 Hard, 482 Impossible |
| Timers | 872 None |
| Random events | 880 Off, 882 On |
| Tactical combat | 621 Manual, 52 Automatic |
| Victory conditions | 248 Domination, 1407 Vendetta, 1253 Team Play |
| Galaxy shape | 555 Irregular, 264 Elliptical, 1056 Ring, 1172 Spiral |
| Galaxy size | 1158 Small, 627 Medium, 596 Large |
| Empire descriptions (empire selection) | 270 Cardassians, 271 Ferengi, 272 Federation, 273 Klingons, 274 Romulans (found by the recording's text, not from UE) |

Every one of these entries is the word the screen shows. The preferences
are in capitals ("MANY", "HARD", "DOMINATION", "TEAM PLAY"), the galaxy
values not ("Irregular", "Large"), as in the recording. Two things the
game adds: the civilization levels are only the word ("EARLY"), and the
screen numbers them ("2 EARLY", the level counting from 1); and entry 872
is "None", which the timers show as "NONE" (there's no capitalised entry).
How the screens use them is on [screens.md](../systems/screens.md).

## In Reluva

`Lexicon` (`src/engines/botf/formats/lexicon.cpp`) reads the entries; the
game setup and empire selection screens take their words from it. The unit
tests (`botf.unit.tables`, "lexicon") cover it.

## Sources and checks

- **Where the facts come from:** the English file
  (`tools/botf-re/botf_res_sample.py --ext dic --all-whole`), October 2026;
  the entry numbers from the Ultimate Editor and a recording of the game.
- **How it was checked:** the table ends exactly at the end of the file,
  and every string ends at its length. Every entry in the table above is
  the word the screen shows in the recording.

## Open questions

- The `u32` before the entry count (952 in the English file).
