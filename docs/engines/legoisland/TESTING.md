# LEGO Island: testing

## Automated checks

### Unit tests

`tests/engines/legoisland/unit/`, a file per topic, each a suite
`legoisland.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `legoisland.unit.detection` | Recognising the game's releases: an unknown release by its files' names, in any case; `LEGO1.DLL` without the world database isn't the game |
| `legoisland.unit.obj` | The SI files' 3D objects, built in the test: an animation with and without the camera, textures with old bytes after a name, a model taking a part's LODs, a path of two faces with a trigger; an object's data without its last 0 |
| `legoisland.unit.si` | SI files, from one built in the test: the objects, a select, pieces split across buffers with a pad chunk and filler, and rebuilding a `.bmp`, `.wav` and `.flc` |
| `legoisland.unit.wdb` | `WORLD.WDB`, from one built in the test: a world, the shared textures (one with a second picture) and part, a part's LODs and meshes (corners given again, texture coordinates beside them), a model's animation and ROIs (one with an empty LOD, one taking a part's), and files cut short or with wrong offsets |
| `legoisland.unit.dta` | A world's `*INF.DTA`, from one built in the test: its animations and their models; files cut short, too long, or of another version |
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
- The data viewer's SI page: that its stills look right and its sounds
  and animations play at the right speed; checked by eye on the English
  1.0 release.
- The data viewer's `WORLD.WDB` pages: that the textures look right, and
  the models come out whole, their pieces in place, their textures the
  right way round; checked by eye on the English 1.0 release.

## A playthrough by hand

<!-- A checklist of what to try, in the order a player meets it. -->

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | English 1.0 | Linux | Importing the CD; the data viewer on every SI file: the jukebox's stills, the intro's Smacker movie, the history book's letters, by eye |
| November 2026 | English 1.0 | Linux | `reluva-tool legoisland si-objects` on all 26 SI files: every 3D object read; the viewer's SI page drawing the build screens' textures, by eye |
| November 2026 | English 1.0 | Linux | `reluva-tool legoisland dta-check`: all 19 `*INF.DTA` files read, and each animation found in its SI file |
| November 2026 | English 1.0 | Linux (software Vulkan) | `reluva-tool legoisland wdb-info` reading every part and model of `WORLD.WDB`; its textures written out and looked at; the viewer's models by eye: the bricks, the ambulance, `carcomp` with its steering wheel from a part, the dune buggy's room, the pizzeria, the island |
