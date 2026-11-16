# And Then There Were None: testing

## Automated checks

### Unit tests

`awe.unit.attwn` (`tests/engines/awe/unit/attwn.cpp`), on files made in
the test: recognising the game, its file names, unscrambling `.act`
([formats/files.md](formats/files.md)), reading a scene
([formats/ac1.md](formats/ac1.md)), its own script words
([systems/script.md](systems/script.md)), its dialogue tables,
conversations played by its dialogue manager's rules
([systems/dialogue.md](systems/dialogue.md)), its lights at night
([systems/lighting.md](systems/lighting.md)), and its saves' layout
([systems/saves.md](systems/saves.md)).

### The basic engine test

`awe.engine` (`tests/engines/awe/engine.cpp`), after Orient Express's,
makes a small install of this game's kind, without any of its files:
stand-ins for `ac.exe` and `granny2.dll`, its text scrambled as `.act`,
`Chapter01` of one act, the main menu with its `reset` and New Game, and
two `.ac1` scenes of a place's `Base` folder. It finds the game as the
launcher does, then plays it, in the engine and in the data viewer: New
Game, act 1A's first scene, a click that sets a tag, says `savelast`
and goes on, a quick save (`last.ac1` and `quick.ac1`, read back in this
game's layout). Its lines in
`tests/engines/awe/data/engine-record.txt` start `attwn.`.

### The game-files test: with your own copy of the game

As Orient Express's ([../motoe/TESTING.md](../motoe/TESTING.md)), from
the same script (`tests/engines/awe/gamedata.cmake`), with
`RELUVA_ATTWN` naming the folder that holds `ac.exe`:

    RELUVA_ATTWN=/path/to/attwn ctest --test-dir build -R awe.attwn.gamedata

| Step | What it runs |
|---|---|
| `awe.attwn.gamedata.release` | What the game is (`detect`) |
| `awe.attwn.gamedata.files` | Every file read (`awe files-check`) |
| `awe.attwn.gamedata.scripts` | Every script's commands counted, and the verbs Reluva doesn't know (`awe script-check`) |
| `awe.attwn.gamedata.route-search` | A way from a new game to the credits, searched for (`awe game-run -solve`), kept in the build (`gamedata/awe.attwn/route.txt`) |
| `awe.attwn.gamedata.route` | The whole game by it, four ways (`awe game-check -quick`) |
| `awe.attwn.gamedata.endings` | The four codas: the route played back, and changed to let Lombard die in 9A (his five minutes waited out before the rescue), Vera in 10A (another of her room's hotspots clicked for the marble chunk), and both, each to the credits with the coda its movie names (`awe game-run -play -v`) |

The records are in `tests/engines/awe/data/gamedata/attwn-english/`.

### The whole game: `reluva-tool awe game-check`

`awe game-check <game> <route.txt> -quick` plays a route four ways: by
the mouse with sound, by the hand alone (every click, the inventory's
panel and the conversations' choices made as a player's mouse does), saved
and loaded after every action, and wandering off it. The search's route,
2,162 actions from New Game to the projector's last film and the
credits, passes all four.

The search (`awe game-run <game> -solve`) takes some hours here, over
several runs of the step, each going on from the last. On the way it
needed what this game has of its own: the machines and the puzzle
screens, its one timer, items lying about (`ITEM`), its inventory's
Combine and Examine, the tools' clicks kept off its sack and journal.
The tools stop the timer and let time pass by a wait, tried before an
action whose conditions tested the timer (Brent's "It's done" thirty
seconds after the fishing line), so a route plays the same however fast
its lines go by. The search gives up a scene with nothing to click but
the inventory, and a state with the player hidden once all is over:
Wargrave's thread `c` in 4A leaves Narracott in a close-up, or hidden,
as the table has it ([UNCONFIRMED.md](UNCONFIRMED.md)); and it counts
something newly clickable as new (the projector's bulb shows once the
drawers above it are shut).

### What the automated checks don't cover

Anything off the route but what wandering finds, and the codas' other
ways (other hotspots in Vera's room); the original game played beside it,
for want of recordings of it.

## A playthrough by hand

Not yet.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 16 November 2026 | English (retail, two CDs) | Linux | The four endings by the `endings` step: each plays to the credits with its own coda |
| 15 November 2026 | English (retail, two CDs) | Linux | `awe.engine` with this game's half added; Orient Express's game-files steps again after the night pictures (all five passed) |
| 15 November 2026 | English (retail, two CDs) | Linux | Scene by scene against gameplay footage: the footage's frames every 2 seconds matched to the scenes by their backgrounds (2,103 visits, 170 scenes), each scene shown by `awe page-shot` at the start of each of the chapter's acts, and 1,820 frames compared by the colour of a grid of cells: within 3% overall in every chapter. Fixed: the night's own pictures in 2A and 4C. The other differences were the story's state (the flashlight, the generator off, a movie on the screen) or a frame mid-fade |
| 15 November 2026 | English (retail, two CDs) | Linux | Against gameplay footage (two playthroughs, by chapter): the logos, main menu, options, in-game menu and save screen, the title card, the dialogue box and its colours, the journal and the inventory, the flashlight, the safe and the deposit boxes, the front entrance at night (its pictures' night colour, fixed), the journal's back arrow (fixed), an ending |
| 15 November 2026 | English (retail, two CDs) | Linux | Phase 5: the game-files steps, all five, the route the search's own (2,162 actions to the credits), `awe game-check -quick` on it all four ways passed |
| 14 November 2026 | English (retail, two CDs) | Linux | Phase 4 so far: `awe game-run <game> -v`, every act started with nothing undone; in the Scenes page a title card, the journal (contents and entries), the main and in-game menus, New Game, a save made, listed and loaded |
| 14 November 2026 | English (retail, two CDs) | Linux | Phase 3, the rest: the lights counted over the 537 scenes; the dialogue box in the Scenes page, a line and a menu (`awe page-shot ... "run:act 01a, talk marston a forcegen"`); the fonts' face names from their files |
| 14 November 2026 | English (retail, two CDs) | Linux | Phase 3, the dialogue manager: the box's rule over the tables' Povents (three differ from Orient Express's), the codes over the tables, the talk command's words over the scripts (`talka` unused, no `+`) |
| 14 November 2026 | English (retail, two CDs) | Linux | Phase 3 so far: `awe script-check`, 5,614 commands; `awe path-check -all`, `ac.exe`'s path-finder against Reluva's, 88,000 searches, 0 mismatches |
| 14 November 2026 | English (retail, two CDs) | Linux | Phase 2: `awe files-check -ac2`, all 537 scenes and their cameras against `granny2.dll`; the Scenes page on the main menu, the entry hall and the kitchen in act 1B, with its characters (`awe page-shot -3d`) |
| 14 November 2026 | English (retail, two CDs) | Linux | Phase 1: recognised by `reluva --detect`; `awe files-check -text -sound -gr2 -bik` read every file; the data viewer's pages |
| 14 November 2026 | English (retail, two CDs) | Linux | The survey in [PLAN.md](PLAN.md): every Granny file read and its Oodle1 sections against the game's `granny2.dll`; every Bink file decoded, three against `binkw32.dll` |
