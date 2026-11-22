# Jedi Knight: testing

## Automated checks

### Unit tests

`tests/engines/sith/unit/`, a file per topic, each a suite
`sith.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `sith.unit.detection` | Recognising the game's releases: the fallback table, names in any case, a partial install or a CD not taken for one |
| `sith.unit.formats` | Reading colormaps, 8- and 16-bit pictures, materials, fonts and string tables, and refusing broken ones |
| `sith.unit.gob` | Reading `.gob` archives (names with either separator and in any case), refusing broken ones, and the install's set of them |
| `sith.unit.import` | Importing from CD 1 and adding CD 2, what's copied and where, what isn't imported from, and that the result is recognised and finds its CDs |
| `sith.unit.folders` | Telling the CDs apart by `JK_.CD`, and finding them in and beside an install and by the settings |

### The basic engine test

`sith.engine` (`tests/engines/sith/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`tests/engines/sith/data/engine-record.txt`).
So far: an empty folder isn't the game, the install is recognised (as an
unknown release, its files being empty), a CD is found beside it and
another in it, and each of the data viewer's pages is drawn from archives
the test makes, before and after a key (the Colormaps page has one
colormap, so its key changes nothing).

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

That the US release's signatures match a real install: checked by hand
([Last checked](#last-checked)).

## A playthrough by hand

- *Add Game...* on the install finds "Jedi Knight: Dark Forces II (US,
  1.0)".
- `reluva-tool sith folders <install>` finds both CDs where they are.
- *Import Game...* on CD 1, with CD 2 added, makes an install that's
  recognised, with both CDs' cut scenes in it.
- In `reluva-viewer`: the menus' backgrounds look as in the game; the
  Force powers' icons look right with `bkforce.bm`'s palette; textures
  look right through `dflt.cmp`; the fonts' sample text is evenly spaced.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US retail | Linux | The data viewer's pages drawn with `reluva-tool sith page` on the install, and looked over: pictures, materials, colormaps, fonts and strings |
| November 2026 | US retail | Linux | `reluva-tool sith formats` on the install: all 146 pictures, 21 colormaps, 1,983 materials, 27 fonts and 7 string tables read |
| November 2026 | US retail | Linux | `reluva-tool sith folders` on the install, with copies of both CDs beside it: recognised as the US release, both CDs found. `reluva-tool import` from the copy of CD 1 with CD 2 added: recognised as the US release, both CDs found in it |
