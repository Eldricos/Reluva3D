# Midtown Madness 2: testing

How Reluva's Midtown Madness 2 is checked: the automated checks, what they
cover and don't, and what to try in a playthrough by hand.

## Automated checks

### Unit tests

`ctest --test-dir build` runs the engine's suites
(`tests/engines/age/unit/`, `reluva-tests age` alone) among the others, on
files built in the tests and needing no game files, so they run in CI:

| Suite | What it checks |
|---|---|
| `age.unit.detection` | Finding the game: the English release by `mm2core.ar`, any other as an unknown release |
| `age.unit.formats` | The game's files read: archives and mounting them, textures, models, cities, placed models, tuning text, bounds, races, road maps, AI settings, pedestrians, path sets, tunnels, the dashboard's files |
| `age.unit.driving` | The cars: collision, the simulation, engine sounds, their files, a game controller, texture quality |
| `age.unit.city` | The city's life: opponents along their paths, the road graph, traffic, its lights and rules, police sirens, speed limits, boats and bridges, parked cars, trains, props, ambient sounds |
| `age.unit.game` | The game: lessons, the player's record, the menus' layout, music, options, replays, drivers, cruising |
| `age.unit.network` | Multiplayer: the session (finding, joining, passwords, joining late, a new host and the computer players kept, the host's checks, everything sealed, through a meeting server) and Cops & Robbers |

The shared code the game uses has its own suites:
`common.unit.directmusic` (DLS and DirectMusic),
`common.unit.windows_resources` (`mmlang.dll`'s string tables, the help
file), `common.unit.portmap` (the router's port), `common.unit.indeo`
(the opening movie's decoder).

### The basic engine test

`age.engine` (`tests/engines/age/engine.cpp`) makes a small install of
its own and runs it through the engine as the launcher starts it, so a
change anywhere (in `src/common/`, say) that alters what the engine does
is caught. CI runs it on Linux and Windows with the rest.

- **The install:** a stand-in `Midtown2.exe` beside an `mm2core.ar` built
  in the test, holding a city's `.cinfo` and `.psdl`, a car's `.info`,
  `.vehCarSim` and `.pkg`, a texture and a text file. It's found by the
  fallback detection, as an unknown release.
- **The game:** with no 3D renderer in the test, the race screen says it
  needs one; **Esc** goes back to the launcher.
- **The viewer:** the same install in the data viewer, as `reluva-viewer`
  runs it: **Tab** past the 3D pages (which say they need a renderer) to
  the textures, the next one, then the files: one opened and closed;
  **Tab** round, and **Esc**, which must end it.
- **The record** (`tests/engines/age/data/engine-record.txt`): what's
  detected, how each run ended, every screen shown (a checksum), the
  window's titles.
- **When it fails:** it prints each line, with the old value beside any
  that changed. If the change is meant,
  `reluva-tests age.engine --update`, review the record's diff, and commit
  it with the change.

### The game-files test

Point `RELUVA_MM2` at the game's folder (the one holding `Midtown2.exe`
and `mm2core.ar`) and run the steps labelled `gamedata`
(`tests/engines/age/gamedata.cmake`):

    RELUVA_MM2=/path/to/Midtown\ Madness\ 2 ctest --test-dir build -L gamedata -R age

On Windows: `set RELUVA_MM2=C:\Games\Midtown Madness 2`, then
`ctest --test-dir build -C Release -L gamedata -R age`. Without the
variable the steps show as skipped.

| Step | What it runs |
|---|---|
| `age.gamedata.release` | What the game is (`detect`) |
| `age.gamedata.archives` | Every archive's files listed (`age ar-list`, each `.ar` in the folder) |
| `age.gamedata.files` | Every texture, model, city, road map, placement list, path set and visibility file read; those it can't, named (`age check`) |
| `age.gamedata.cars` | Every car driven on a flat plane by Reluva's simulation (0-60, 0-100, the quarter mile, top speed, 60-0), ranked against the game's figures for it (`age cars`; [car-simulation.md](systems/car-simulation.md#against-the-games-figures)) |
| `age.gamedata.play` | The game run headless: the opening skipped, the race screen (which says it needs 3D) left (`age game-run`) |
| `age.gamedata.viewer` | The data viewer run headless: its pages in turn, a few keys on the textures and the files, then **Esc** (`age viewer-run`) |

`game-run` and `viewer-run` run on a System with no window whose clock
moves only as the game waits, so every run is the same. What each command
prints (the game's folder and timings taken out) is checksummed, with a
summary line, into a record: `tests/engines/age/data/gamedata/<release>/<step>.txt`.
A step passes when its record is the same as the committed one. The
records hold only checksums and counts, nothing of the game.

Records so far, made on Linux: `en` (the English release). `check` reads
4,856 files there and names the 9 it can't, all unused by the game; the
record shows any change to that list:

| File | Why |
|---|---|
| `city/london_sup.bai`, `city/sf_sup.bai` | Not road maps in the form the cities' are |
| `geometry/sky_dawn.pkg`, `geometry/vpdb731.pkg` | An older model version, `PKG2` |
| `geometry/thing.pkg` | A test file, its mesh's size 0 |
| `geometry/vpvw_dune.pkg` | Two wheel meshes overwritten with text |
| `geometry/wf_hse_baywin03_stone_wht_5s_12_l.pkg`, `geometry/ww_bldg1_stone_tan_t6_20_l.pkg` | Empty |
| `race/london/blitz10.pathset` | No whole paths |

For another release, make its records with `RELUVA_GAMEDATA_UPDATE=1`,
look at each command's output (kept in the build's output folder, as the
step's message says), and commit them. See
[docs/DEVELOPMENT.md](../../../../DEVELOPMENT.md#engine-tests).

### What the automated checks don't cover

- Anything drawn in 3D: the tests have no GPU, so the race screen, the
  Drive, World and Models pages are only checked to say so.
- How the cars handle, and whether the traffic, police and opponents
  behave as the game's do: only playing, against recordings, can say.
- Sound and music, by ear.
- Multiplayer between two machines on a real network (the unit tests use
  an in-memory one).

## A playthrough by hand

1. The opening movie plays, and a key skips it; the main menu follows.
2. Create a driver, Amateur; the main menu shows them.
3. Crash Course: the first lesson in San Francisco, passed and failed.
4. A Blitz, a Circuit and a Checkpoint race in each city: the countdown,
   checkpoints, laps, the opponents, the results screen, the race record.
5. The race menu's Cruise: traffic, pedestrians, police chasing a
   speeding car and busting a stopped one, traffic lights, the
   drawbridges and ferries, London's trains.
6. Knock down props and traffic lights; damage the car to wrecked.
7. Each camera, the in-car dashboard, the map's zoom and full screen,
   the HUD on and off, the rear-view mirror.
8. The Options: audio, graphics (texture quality), controls (a key
   rebound, a game pad), applied and kept after a restart.
9. A replay: the instant replay after a race, saved and played again.
10. The in-race menu: resume, restart, quit.
11. Multiplayer: two copies of Reluva on the local network, a race and
    Cops & Robbers, chat; a password; a computer player.
12. The Help button opens the game's help file.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 2026-11-19 | English | Linux | The automated checks: the unit tests, `age.engine`, and the game-files test's six steps against the `en` records |
