# And Then There Were None: testing

## Automated checks

### Unit tests

`awe.unit.attwn` (`tests/engines/awe/unit/attwn.cpp`), on files made in
the test: recognising the game, its file names, unscrambling `.act`
([formats/files.md](formats/files.md)), reading a scene
([formats/ac1.md](formats/ac1.md)), and its own script words
([systems/script.md](systems/script.md)).

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
| 14 November 2026 | English (retail, two CDs) | Linux | Phase 3 so far: `awe script-check`, 5,614 commands; `awe path-check -all`, `ac.exe`'s path-finder against Reluva's, 88,000 searches, 0 mismatches |
| 14 November 2026 | English (retail, two CDs) | Linux | Phase 2: `awe files-check -ac2`, all 537 scenes and their cameras against `granny2.dll`; the Scenes page on the main menu, the entry hall and the kitchen in act 1B, with its characters (`awe page-shot -3d`) |
| 14 November 2026 | English (retail, two CDs) | Linux | Phase 1: recognised by `reluva --detect`; `awe files-check -text -sound -gr2 -bik` read every file; the data viewer's pages |
| 14 November 2026 | English (retail, two CDs) | Linux | The survey in [PLAN.md](PLAN.md): every Granny file read and its Oodle1 sections against the game's `granny2.dll`; every Bink file decoded, three against `binkw32.dll` |
