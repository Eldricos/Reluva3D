# LEGO Loco: testing

## Automated checks

### Unit tests

`tests/engines/legoloco/unit/`, a file per topic, each a suite
`legoloco.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `legoloco.unit.definitions` | Object definitions: their parts, occupancy grids and frame-set table, the game's corrections, and refusing damaged ones |
| `legoloco.unit.detection` | Recognising the game: an empty folder, and the right names with other contents, aren't it |
| `legoloco.unit.resources` | The resource file: reading its index and entries by name, unpacking a compressed entry, and refusing damaged ones |

### The basic engine test

`legoloco.engine` (`tests/engines/legoloco/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`data/engine-record.txt`).

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

Reading the real game's files: until there's a game-files test,
`reluva-tool legoloco rf-list <game folder>` reads and unpacks every
entry of a copy's resource file, and should end with none failed;
`reluva-tool legoloco dat <game folder>` reads every object definition,
likewise.

## A playthrough by hand

<!-- A checklist of what to try, in the order a player meets it. -->

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| | | | |
