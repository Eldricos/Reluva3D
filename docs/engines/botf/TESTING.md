# Star Trek: Birth of the Federation: testing

## Automated checks

### Unit tests

`tests/engines/botf/unit/`, a file per topic, each a suite
`botf.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `botf.unit.detection` | Recognising the game's releases |

### The basic engine test

`botf.engine` (`tests/engines/botf/engine.cpp`), without the
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
