# Star Trek: Starfleet Command II: testing

## Automated checks

### Unit tests

`tests/engines/sfc/unit/`, a file per topic, each a suite
`sfc.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `sfc.unit.detection` | The engine is registered; an empty folder isn't a game; the game's files with other contents are an unknown release, whatever their names' case |
| `sfc.unit.formats` | sprites.q3's images (16- and 8-bit, each kind of run, a name from the objects) and a run past a row's end; a .bft font, measured and drawn; a .mod model, and a triangle with a material that isn't there; a tab table with quotes and comments; a string table with continuation lines and a heading |

### The basic engine test

`sfc.engine` (`tests/engines/sfc/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`data/engine-record.txt`).

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### Reading every retail file

With your own copy, these read every file of a kind and say what fails:

- `reluva-tool sfc sprites <game folder> -q`
- `reluva-tool sfc fonts <game folder>`
- `reluva-tool sfc models <game folder>`
- `reluva-tool sfc sounds <game folder>`
- `reluva-tool sfc table <file>`, for each table

### What the automated checks don't cover

Whether the data viewer shows the pictures, fonts and models as the game
does: by putting it beside the running game.

## A playthrough by hand

Nothing of the game runs yet.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 22 November 2026 | English retail 1.0 | Linux | `reluva --detect` on the install's `StarFleet2.exe` and `sprites.q3`: recognised as 1.0 |
| 22 November 2026 | English retail 1.0 | Linux | `reluva-tool sfc host`: start-up to the 3D test, 44,063,247 instructions, stopping for want of Direct3D 3 |
| 22 November 2026 | English retail 1.0 | Linux | Every image, font, model, table and sound read by the commands above, none failing; the data viewer's pages seen in screenshots (Xvfb, Mesa's software Vulkan), a model with its textures |
