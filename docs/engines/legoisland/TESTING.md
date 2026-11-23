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
| `legoisland.unit.scene` | The data viewer placing an animation's nodes: keys between and outside their times, a turn the short way round, a child moved and turned with its parent, a showing key hiding a node |
| `legoisland.unit.si` | SI files, from one built in the test: the objects, a select, pieces split across buffers with a pad chunk and filler, rebuilding a `.bmp`, `.wav` and `.flc`, and an event |
| `legoisland.unit.turning` | Turning by time, on the code it patches built in the test and run by the interpreter: unpatched, the same turn whatever the frame's time; patched, as unpatched at 20 frames a second, a third of it at 60, a quarter second's for a longer frame; code not as expected left alone |
| `legoisland.unit.wdb` | `WORLD.WDB`, from one built in the test: a world, the shared textures (one with a second picture) and part, a part's LODs and meshes (corners given again, texture coordinates beside them), a model's animation and ROIs (one with an empty LOD, one taking a part's), and files cut short or with wrong offsets |
| `legoisland.unit.dta` | A world's `*INF.DTA`, from one built in the test: its animations and their models; files cut short, too long, or of another version |
| `legoisland.unit.import` | Importing: from the CD, from an install with the CD added after, what each note says is missing, and a folder with neither |

### The basic engine test

`legoisland.engine` (`tests/engines/legoisland/engine.cpp`), without the
game's files: a small install of its own (the files' names, made-up
contents), taken through what playing does short of the game's own
program, against its record (`data/engine-record.txt`):

- the install found, and planned for import with a copy of the CD's
  scripts beside it;
- the engine's options, their defaults the same as nothing set, and the
  settings each makes (frame rate, turning, the setup's registry values,
  the joystick);
- frames held to 60 and 20 a second, and unheld, on a made-up clock: a
  late frame shown when it's ready, the next ones not hurried;
- the process's registry as the installer leaves it, then with settings,
  and the joystick reported;
- the program loaded, played on its thread and run by the engine as the
  launcher runs it, each stopping with the error it should.

The game-files test (below) runs the program itself.

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
With your own copy, imported (`tests/engines/legoisland/gamedata.cmake`,
ctest `legoisland.gamedata.<step>`, label `gamedata`): the game's folder
from `RELUVA_LEGOISLAND`, or the launcher's entry for it.

```sh
RELUVA_LEGOISLAND=/path/to/LEGO-Island ctest --test-dir build -L gamedata
```

| Step | What it runs |
|---|---|
| `release` | What the game is (`detect`) |
| `scripts` | Every SI file's header and events (`si-info`, `si-events`) |
| `world` | `WORLD.WDB` read (`wdb-info`) |
| `animations` | Every world's animation list, each animation found (`dta-check`) |
| `turning` | The same route, then the left arrow held 40 frames once Pepper's arrival is over (frame 9000): that `LEGO1.DLL` was patched to turn by time, and how far she turned, in the state and the frames at 9001 and 9501 |
| `island` | The game run on a fixed clock from a new save folder: the intro skipped, a player registered, Pepper dragged to the pizzeria, out on the island to frame 7000; its state, a frame every 1000 and its save files, as checksums (`run -fixed-clock 20000 -until 7000`) |
| `build` | A player registered, Pepper dropped on the boat: the lifeguard hut, its baseplate clicked, the jet-ski's build screen, a piece clicked (its colours and decals offered) and one dragged, to frame 14500 |
| `elevator` | The elevator ridden from the lobby to the observation deck, to frame 6000 |
| `players` | A player registered and the game quit through its door (the green brick): its saves written, the credits, its end; then on the same save folder a second player registered, the first's save moved along (`G0.GS` to `G1.GS`, checked) |

The records, in `tests/engines/legoisland/data/gamedata/english-1.0/`,
hold only checksums, counts and summary lines. The `island` run's clock
follows the instructions run, and its threads take turns by them, so it
repeats exactly on one machine (two runs: the same instructions, state,
frames and save files); on another system, the maths library's last bit
can make it differ with nothing broken, so keep a baseline of your own
(`RELUVA_GAMEDATA_RECORDS`, [DEVELOPMENT.md](../../DEVELOPMENT.md)).
If a change is meant, run again with `RELUVA_GAMEDATA_UPDATE=1` and
review the records' diff.

### What the automated checks don't cover

<!-- What only a person playing can check. -->
- The data viewer's SI page: that its stills look right and its sounds
  and animations play at the right speed; checked by eye on the English
  1.0 release.
- The data viewer's `WORLD.WDB` pages: that the textures look right, and
  the models come out whole, their pieces in place, their textures the
  right way round; checked by eye on the English 1.0 release.
- The data viewer's animation-list page: that each world's circles and
  models fall where its scenes play; checked by eye on the English 1.0
  release.
- The data viewer's SI 3D page: that the SI files' models come out
  whole, and their animations play smoothly with every piece in place,
  through their own cameras too; checked by eye on the English 1.0
  release.

## A playthrough by hand

<!-- A checklist of what to try, in the order a player meets it. -->
In the engine, on a new save folder, against the original (played, or
recordings of it). Each item says how far it's been checked: *scripted*
(`reluva-tool legoisland run` and its frames, by eye), *played* (in the
engine, by a tester), or *to try*.

1. **The intro:** the LEGO Media, LEGO and Mindscape logos, then the
   flight over the island, with sound; a click skips it. *Scripted,
   played.*
2. **The Information Center:** the Infomaniac's welcome; the portraits'
   films; the radio; the left arrow to the elevator's lobby, the right to
   the score cube; the elevator to the observation deck and its
   controls (sun, moon, sky colour, the globe). *Scripted, but the
   observation deck's controls: to try.*
3. **Registering:** a name spelled in the book and its tick; a second
   player; the history book. *A first and a second player: scripted,
   played; the history book to try.*
4. **Choosing a character:** each of the five dragged to each place on
   the map: the Information Center, the pizzeria, the boat, the race
   track, the gas station, the hospital, the police. *Pepper to every
   place but the Information Center: scripted; the other four to try.*
5. **The island on foot:** walking, turning (at the frame rate set),
   the islanders, the jukebox, the camera's height; its look against the
   original's. *Walking and its look at the pizzeria: scripted, played.*
6. **Vehicles:** the skateboard, the bicycle, the motorcycle, the
   ambulance, the tow truck, the police car, the pizza deliveries.
   *Skateboard: played; the rest to try.*
7. **Building:** the jet-ski, the helicopter, the dune buggy, the race
   car: built through, painted, ridden. *Jet-ski: played; the rest to
   try.*
8. **Races:** the jet-ski race and the car race, each to its end and its
   score on the cube. *To try.*
9. **Act 2:** the Brickster's escape, the bricks collected. *To try.*
10. **Act 3:** the chase in the helicopter, and its end. *To try.*
11. **Saving and loading:** quit through the door, start again, the
    player taken from the book; a game in Act 2 or 3 picked up again.
    *Scripted (Act 1); Acts 2 and 3 to try.*
12. **The credits**, after quitting through the door. *Scripted.*
13. **Settings:** each of the engine's (frame rate, the island's
    quality, music, 3D sound, the game's cursor, a joystick) seen to
    take effect. *To try.*

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | English 1.0 | Linux | Importing the CD; the data viewer on every SI file: the jukebox's stills, the intro's Smacker movie, the history book's letters, by eye |
| November 2026 | English 1.0 | Linux (virtual display) | Played in the engine: the intro (its logos and movie), skipped with a click; the Information Center, animating |
| November 2026 | English 1.0 | Linux | `reluva-tool legoisland run` with `-click` and `-shots`, by eye: the Infomaniac drawn and walking; a portrait's film; a name registered; the door's question, its bricks, the green one picked and the credits; the elevator and the island through its window; the score cube |
| November 2026 | English 1.0 | Windows (a tester) | Played in the engine: a second player registered; the Octan station's pillars beside the camera, one colour across each face, no seam where the screen's edge cuts them |
| November 2026 | English 1.0 | Linux | Turning by time: the left arrow held 40 frames, at about 85 frames a second (0.47 s), turned the player about 85 degrees, where by the frame it turned a full circle |
| November 2026 | English 1.0 | Linux | `reluva-tool legoisland run` on one save folder twice: a player registered and the game quit through its door, its three save files written; then the player taken again from the register book and out to the island |
| November 2026 | English 1.0 | Linux | The island at the pizzeria, by eye, against a screenshot of the original: its roads, markings and edges, its brightness. The jet-ski's build screen, its pieces clicked and dragged. In the engine (by a tester): the skateboard ridden; the jet-ski built and ridden |
| November 2026 | English 1.0 | Linux | `reluva-tool legoisland run` with `-click`, `-drag`, `-key` and `-shots`, by eye: a name registered and `Players.gsi` written; Pepper dragged to the pizzeria and out on the island; walked to the pizzeria and on with the arrow keys |
| November 2026 | English 1.0 | Linux | The viewer's animation-list page by eye: `ACT1INF.DTA` and `OBSTINF.DTA`, their circles and models on the map |
| November 2026 | English 1.0 | Linux (software Vulkan) | The viewer's SI 3D page by eye: `Chptr.mod`; the copter's build animation; Infomaniac's `iic001in`; the Brickster on his motorbike; `ACT2MAIN.SI`'s ambulance scene; Act 3's `tlp053in` from above and through its camera at 9, 25 and 55 s. The `WORLD.WDB` model page again (`carcomp`), its drawing now shared with this page |
| November 2026 | English 1.0 | Linux | `reluva-tool legoisland si-events` on all 26 SI files: all 33 events read |
| November 2026 | English 1.0 | Linux | `reluva-tool legoisland si-objects` on all 26 SI files: every 3D object read; the viewer's SI page drawing the build screens' textures, by eye |
| November 2026 | English 1.0 | Linux | `reluva-tool legoisland dta-check`: all 19 `*INF.DTA` files read, and each animation found in its SI file |
| November 2026 | English 1.0 | Linux (software Vulkan) | `reluva-tool legoisland wdb-info` reading every part and model of `WORLD.WDB`; its textures written out and looked at; the viewer's models by eye: the bricks, the ambulance, `carcomp` with its steering wheel from a part, the dune buggy's room, the pizzeria, the island |
