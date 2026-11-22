# LEGO Loco: testing

## Automated checks

### Unit tests

`tests/engines/legoloco/unit/`, a file per topic, each a suite
`legoloco.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `legoloco.unit.detection` | Recognising the game: an empty folder, and the right names with other contents, aren't it |

### The basic engine test

`legoloco.engine` (`tests/engines/legoloco/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`data/engine-record.txt`).

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

<!-- What only a person playing can check. -->

## A playthrough by hand

<!-- A checklist of what to try, in the order a player meets it. -->

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| | | | |
