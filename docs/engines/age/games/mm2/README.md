# Midtown Madness 2

Angel Studios' street racer, published by Microsoft in 2000: races,
driving lessons and cruising in San Francisco and London, and
multiplayer. Game id `mm2`, on the [Angel Game Engine](../../../../../README.md);
its code is in `src/engines/age/`.

## Releases

| Release | Recognised by | State |
|---|---|---|
| English, Windows (`Midtown2.exe` 1.00.3200) | `mm2core.ar` (MD5 `8ba3c06f784f6f7abff1d4b902ef3440`, 21,430,272 bytes) with `Midtown2.exe` | Playable; the game-files test's records are made from it |
| Any other (other languages, the demo) | `mm2core.ar` and `Midtown2.exe` by name | Shown as "unknown release"; not checked |

The install folder and the CD's `GAME` folder (the same files with
upper-case names) are both found.

## Where it stands

| Part | State |
|---|---|
| Detection | Midtown Madness 2: the English Windows release by `mm2core.ar`'s hash (install folder or the CD's `GAME` folder); other releases by file names, shown as "unknown release" |
| Archives | done: `.ar` ([ar.md](formats/ar.md)); all 13,288 retail files read. Every `.ar` in the game folder is mounted, loose files over them (`filesystem.cpp`); `reluva-tool age ar-list` and `ar-extract` |
| Textures | done: `.tex` in its four formats ([tex.md](formats/tex.md)); all 3,665 retail textures decode; the flags' meanings still to work out; `reluva-tool age tex-info` |
| Models | done: `.pkg` meshes, shaders with their paint jobs, placed models, and `.mtx` part placements ([pkg.md](formats/pkg.md)); 1,034 of 1,040 retail models load (the 6 that don't are unused); `reluva-tool age pkg-info` |
| Cities | done: `.psdl` streets and buildings, generated from every attribute, with tunnels, walls and railings, and the roads after the bounds read ([psdl.md](formats/psdl.md)); `.cpvs` visibility ([pvs.md](formats/pvs.md)); the water under the bridges, the road markings of `decals.pathset`, animated water textures, the texture flag that clamps ([tex.md](formats/tex.md)); the city lit by its three lights, and its fog; `.inst` placed models ([inst.md](formats/inst.md)); the sky dome and the 16 lightings ([tune.md](formats/tune.md)); both retail cities read whole, every texture and model they name found; `reluva-tool age psdl-info` |
| Driving | a first cruise mode, Reluva's model of the game's tuning ([car-simulation.md](systems/car-simulation.md)): every car the menus offer, in either city; its simulation (suspension, tyres, engine, automatic gearbox, drag) from its `.vehCarSim`; collisions with the city's generated geometry, facade bounds and placed models' `.bnd` bounds ([bnd.md](formats/bnd.md)); the near, far and in-car cameras from its camera files; its engine sound from its sound table; a HUD (speed, gear, revs, damage) laid out as the game's own screens show it ([research/footage.md](research/footage.md)). Its handling not compared with the game yet |
| Races | a first version ([races.md](systems/races.md), [race.md](formats/race.md)): every Blitz, Circuit and Checkpoint race of both cities from the game's race files, at Amateur or Professional, against opponents in the cars and at the skills the race's aimap gives them, driving their recorded paths along a road graph Reluva builds from the city (every opponent of every circuit and checkpoint race finishes); checkpoints, laps, a Blitz's time limit, positions, results; the game's checkpoint stands and HUD arrow; the race's traffic, pedestrians and police; the game's music; the race menu's Cruise, and San Francisco's "Stunt Mode: Acceleration" from its unused `accel0` points and messages |
| City life | a first version ([traffic.md](systems/traffic.md)): the road maps ([bai.md](formats/bai.md)) and AI settings ([aimap.md](formats/aimap.md)) of both cities read whole; traffic in the game's ambient models along the lanes, through the junctions, knocked aside by the player; pedestrians, the game's skinned models and animations ([ped.md](formats/ped.md)), walking the sidewalks and diving out of the way; police that chase a speeding player along the roads and bust a stopped one. traffic lights at the road ends the road maps mark, which the traffic stops for; lane changes; the cars' lamps (brake lights, indicators, headlights with flares and pools of light at night, sirens). police patrolling the lanes when not chasing, with the game's siren tables and arrest sound, and the player's own siren in the police car. Ferries, boats and ducks on their paths, drawbridges that rise and fall, parked cars, and London's Underground trains ([pathset.md](formats/pathset.md)); props, signs and traffic lights knocked down and broken into pieces; each race's own barricades; the ambient sounds of `aud/ambient`; the street furniture of `proprules.csv` along the sidewalks (the game has no police radio to play) |
| Multiplayer | a first version ([multiplayer.md](systems/multiplayer.md)): the game's multiplayer menus; sessions over UDP between copies of Reluva, found on the local network or by address; Cruise, Blitz, Checkpoint, Circuit and Cops & Robbers, with chat, teams and the game's gold, words and announcer; computer players to fill a session, or to play alone; joining a game already playing, the session handed on when the host goes, passwords proved rather than sent, the host's checks on cars and progress, others' engine sounds and damage, and the router asked to pass the port on (UPnP, NAT-PMP). Its protocol is Reluva's own (the original's is DirectPlay) and its Cops & Robbers rules Reluva's reading of the files |
| Menus | the game's menus from `widget.csv` ([menus.md](systems/menus.md)): main menu, races, Crash Course (with its intro), garage and Vehicle Showcase; the Options (audio, graphics, controls, keys, about), kept in Reluva's `options.txt` and used; drivers with their own records, Driver's Stats and Race Records; Quick Race; changing a race's conditions (Change Race Options) and Race Locales; replays (the race run again from what was recorded, [replays.md](systems/replays.md#how-it-works)), the in-race menu and the results screens; the menus' music and voices; the mouse throughout; the navigation bar's Help (the game's Windows help file, [winhelp.md](../../../../common/winhelp.md)) and Minimise; multiplayer's |
| Viewer | Race page (3D); Drive page (3D); World page (3D): London and San Francisco flown through, from the road nearest the middle, at each time and weather; Models page (3D, on the GPU renderer): every model with its parts placed, by level of detail and paint job, the lit parts (lamps, glows) on request; Textures page; Files page: every file with its size and archive, by folder; text files shown as text, the rest as hex dumps |

Not done yet: the game against the original over DirectPlay (Reluva
plays only Reluva), and a comparison of the handling with the game's own.
The opening movie, `logos.avi` (Indeo 5), is played by Reluva's own Indeo
5 decoder ([indeo5.md](../../../../common/video/indeo5.md)).

## Using it

- Add the game's install folder (the one holding `Midtown2.exe` and
  `mm2core.ar`) in the launcher, or import it from the CD.
- The game needs Reluva's 3D renderer: without a GPU the race screen
  says so.
- The keys are the game's, changeable in its Options (Control): the
  arrows drive, **Space** the handbrake, **Enter** the horn, **A** and
  **Z** the gears, **C** the camera; a game pad works too
  ([options.md](systems/options.md)).
- The options are kept in Reluva's own `options.txt` and the drivers'
  records in the game's save folder, not the Windows registry or the
  game's `.plr` files.
- `reluva-viewer` adds the data viewer: the Race, Drive, World, Models,
  Textures and Files pages.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [formats/](formats) | The game's own files, a page each: [archives](formats/ar.md), [textures](formats/tex.md), [models](formats/pkg.md), [cities](formats/psdl.md), [placed models](formats/inst.md), [visibility](formats/pvs.md), [road maps](formats/bai.md), [AI settings](formats/aimap.md), [path sets](formats/pathset.md), [pedestrians](formats/ped.md), [bounds](formats/bnd.md), [tuning text and lighting](formats/tune.md), [races](formats/race.md), [the game's text](formats/mmlang.md) |
| [systems/](systems) | How the game behaves: [the car](systems/car-simulation.md), [collisions](systems/collisions.md), [cameras](systems/cameras.md), [damage](systems/damage.md), [the HUD](systems/hud.md), [the car's sounds](systems/car-sounds.md), [races](systems/races.md), [Cruise and the stunt](systems/cruise-and-stunt.md), [Crash Course](systems/crash-course.md), [music](systems/music.md), [replays](systems/replays.md), [the record and unlocking](systems/record-and-unlocking.md), [traffic](systems/traffic.md), [props](systems/props.md), [pedestrians](systems/pedestrians.md), [police](systems/police.md), [boats and bridges](systems/boats-and-bridges.md), [ambient sounds](systems/ambient-sounds.md), [the menus](systems/menus.md), [options](systems/options.md), [drivers and records](systems/drivers-and-records.md), [multiplayer](systems/multiplayer.md), [Cops & Robbers](systems/cops-and-robbers.md), [the network protocol](systems/network-protocol.md) |
| [research/](research/README.md) | Working notes: what the game's own screens show, from public videos |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |
| [PLAN.md](PLAN.md) | The plan and its phases |

Shared code it uses: [DirectMusic](../../../../common/audio/directmusic.md)
and [DLS](../../../../common/audio/dls.md) for the music,
[Windows Help files](../../../../common/winhelp.md) for the Help button,
[Indeo 5](../../../../common/video/indeo5.md) for the opening movie.

## Terms

- **Blitz, Circuit, Checkpoint:** the game's three kinds of race: against
  the clock, laps, and a dash through checkpoints
  ([races.md](systems/races.md)).
- **Crash Course:** the driving lessons ([crash-course.md](systems/crash-course.md)).
- **Cruise:** the city with no race ([cruise-and-stunt.md](systems/cruise-and-stunt.md)).
- **Amateur, Professional:** a driver's level, which picks each race's
  files.
- **aimap:** a race's or city's settings for the computer-driven cars,
  pedestrians and police ([aimap.md](formats/aimap.md)).
- **Road map (`.bai`):** the roads as the computer-driven cars see them
  ([bai.md](formats/bai.md)).
