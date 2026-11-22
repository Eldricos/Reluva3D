# LEGO Loco: testing

## Automated checks

### Unit tests

`tests/engines/legoloco/unit/`, a file per topic, each a suite
`legoloco.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `legoloco.unit.boards` | Boards: the header, squares, pieces with their minifigures, and trains; a board holding fewer pieces than it counts; refusing a short one |
| `legoloco.unit.definitions` | Object definitions: their parts, occupancy grids and frame-set table, the game's corrections, and refusing damaged ones |
| `legoloco.unit.detection` | Recognising the game: an empty folder, and the right names with other contents, aren't it |
| `legoloco.unit.easter_eggs` | `EE.INI`'s two sections, read key by key until one is missing; the area arrangements and their index |
| `legoloco.unit.game_files` | A copy's files: the resources' names from an executable's string table, finding a resource by number, and the frames' width |
| `legoloco.unit.resources` | The resource file: reading its index and entries by name, unpacking a compressed entry, and refusing damaged ones |

### The basic engine test

`legoloco.engine` (`tests/engines/legoloco/engine.cpp`), without the
game's files: a small install of its own (`tests/engines/legoloco/install.h`:
an executable with a string table, and a resource file with a picture,
its definition, a sound and boards), checked not to be taken for the
game, and taken through the data viewer's pages with keys pressed on
each; its record (`data/engine-record.txt`) holds what was drawn and the
pages' titles.

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

Reading the real game's files: until there's a game-files test,
`reluva-tool legoloco rf-list <game folder>` reads and unpacks every
entry of a copy's resource file, and should end with none failed;
`reluva-tool legoloco dat <game folder>` reads every object definition,
`reluva-tool legoloco sav <game folder>` every board,
`reluva-tool legoloco ee <game folder>` `EE.INI` and
`reluva-tool legoloco lay <game folder>` the area arrangements,
likewise.

## A playthrough by hand

No game to play yet. In the data viewer (`reluva-viewer <folder>`):

- Boards: every layout drawn whole on its backdrop, no magenta squares
  (pieces without a picture); S shows the squares' values.
- Pictures: a minifigure's walks and moods play (Up/Down, Space); a
  building's frames line up with the marks on its strip.
- Definitions and Sounds: each opens; the sounds play.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | CD, 1.0.18.2 | Linux | Every reader over the CD's files; the viewer's pages drawn with `reluva-tool legoloco view` |
