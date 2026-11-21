# LEGO Island: testing

## Automated checks

### Unit tests

`tests/engines/legoisland/unit/`, a file per topic, each a suite
`legoisland.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `legoisland.unit.detection` | Recognising the game's releases: an unknown release by its files' names, in any case; `LEGO1.DLL` without the world database isn't the game |
| `legoisland.unit.si` | SI files, from one built in the test: the objects, a select, pieces split across buffers with a pad chunk and filler, and rebuilding a `.bmp`, `.wav` and `.flc` |
| `legoisland.unit.import` | Importing: from the CD, from an install with the CD added after, what each note says is missing, and a folder with neither |

### The basic engine test

`legoisland.engine` (`tests/engines/legoisland/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`data/engine-record.txt`).

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

<!-- What only a person playing can check. -->
- The data viewer's page: that its stills look right and its sounds and
  animations play at the right speed; checked by eye on the English 1.0
  release.

## A playthrough by hand

<!-- A checklist of what to try, in the order a player meets it. -->

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | English 1.0 | Linux | Importing the CD; the data viewer on every SI file: the jukebox's stills, the intro's Smacker movie, the history book's letters, by eye |
