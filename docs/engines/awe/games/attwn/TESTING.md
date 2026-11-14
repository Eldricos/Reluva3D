# And Then There Were None: testing

## Automated checks

### Unit tests

`awe.unit.attwn` (`tests/engines/awe/unit/attwn.cpp`), on files made in
the test: recognising the game, its file names, and unscrambling `.act`
([formats/files.md](formats/files.md)).

### The basic engine test

`awe.engine` (`tests/engines/awe/engine.cpp`) plays a small install of
its own, an Orient Express one; this game gets one alongside it once it
plays (phase 5 of [PLAN.md](PLAN.md)).

### The game-files test

Not yet: phase 5 adds `awe.gamedata` steps for `attwn`, as Orient
Express has ([../motoe/TESTING.md](../motoe/TESTING.md)).

### What the automated checks don't cover

Nothing of this game is played yet.

## A playthrough by hand

Not yet.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 14 November 2026 | English (retail, two CDs) | Linux | Phase 1: recognised by `reluva --detect`; `awe files-check -text -sound -gr2 -bik` read every file; the data viewer's pages |
| 14 November 2026 | English (retail, two CDs) | Linux | The survey in [PLAN.md](PLAN.md): every Granny file read and its Oodle1 sections against the game's `granny2.dll`; every Bink file decoded, three against `binkw32.dll` |
