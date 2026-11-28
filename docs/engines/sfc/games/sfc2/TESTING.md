# Star Trek: Starfleet Command II: testing

## Automated checks

### Unit tests

`tests/engines/sfc/unit/`, a file per topic, each a suite
`sfc.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `sfc.unit.detection` | The engine is registered; an empty folder isn't a game; the game's files with other contents are an unknown release, whatever their names' case |
| `sfc.unit.battle` | A battle laid out in memory as the game keeps it: the player, a neutral and an enemy read with their sides, targets and range; the pilot's first inputs (red alert, speed, the nearest enemy, a turn toward it, the shield facing it, no shot off its bow); no battle, no input |
| `sfc.unit.formats` | sprites.q3's images (16- and 8-bit, each kind of run, a name from the objects) and a run past a row's end; a .bft font, measured and drawn; a .mod model, and a triangle with a material that isn't there; a tab table with quotes and comments; a string table with continuation lines and a heading; the objects' records found past a body that isn't read; a settings file (comments, quotes, sections by path, a quote not closed, errors); a campaign map built in the test, and one cut short |

### The basic engine test

`sfc.engine` (`tests/engines/sfc/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`data/engine-record.txt`): it isn't taken for
the game, starting the game without `StarFleet2.exe` says so, and a
registry value the program writes is kept in its save folder's
`registry.txt` and read back, and two CDs laid out as the game's
(InstallShield 5, the setup's groups) are imported: nothing planned from
the first alone, all of it once the second is added, the result
recognised. `common.unit.installshield 5` checks version 5's cabinets
themselves.

### The game-files test

With your own copy: `RELUVA_SFC2=<the game's folder> ctest -L gamedata
-R sfc`, and `RELUVA_SFC2_CDS=<a folder holding the two CDs, each in a
folder of its own>` for the import step.
`tests/engines/sfc/gamedata.cmake` runs `reluva-tool` on the game, a
step each:

| Step | What it checks |
|---|---|
| `sfc.gamedata.release` | What the game is (`detect`) |
| `sfc.gamedata.files` | Every file by name, size and MD5, as one checksum |
| `sfc.gamedata.formats` | Every picture in `sprites.q3` decoded and its objects found, and every font, model, sound, settings file and campaign map read (`sfc sprites`, `fonts`, `models`, `sounds`, `settings`, `maps`) |
| `sfc.gamedata.intro` | The opening movie decoded out of `SFC2Intro.exe`, and its 1000th frame |
| `sfc.gamedata.host` | `StarFleet2.exe` run on a fixed clock to its CD key dialog, a made-up key typed and refused: the frames shown, where it stopped, and the dialog before and after (no key is needed or kept) |
| `sfc.gamedata.import` | The game imported from its two CDs, each file compared with the install's |

The records, in `tests/engines/sfc/data/gamedata/<release>/`, hold only
counts and checksums; a step passes when its record is the same.
Records so far: `english-1.0`, an install by the CDs' own setup (3,357
files), its import the same file for file (3,356, all but setup's
`sfc2.isu`), made on Linux. The whole test takes about two minutes.

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

Each item says what to do and what should happen. How each was played
headless, and what was seen, is in
[systems/hosting.md](systems/hosting.md) (and the patch's in
[systems/patching.md](systems/patching.md)); when, in [Last
checked](#last-checked). When something differs from the original,
note the release, the screen or mission, and what each does.

### Getting the game in

1. *Import Game...* with the first CD, *Add other disc...* with the
   second: English retail 1.0, 3,356 files.
2. The same with *Add patch...* and `sfc2patch20022036.exe`: 2.0.3.6.
3. *Add Game...* with an install made on Windows: recognised as its
   release.

### Starting

1. Start it: the opening movie, which a key or click skips, then
   "Loading..." and the title screen with its CD key dialog. A wrong key
   gets "ERROR! ... incorrect"; the copy's own key (typed with Shift for
   capitals, and `-`) closes the dialog, the music starts and the main
   menu shows. Started again, it asks for no key.
2. Ctrl+F5 or closing the window asks before stopping it; Return to
   launcher.

### The main menu's screens

1. Film Room: after a mission, its recordings (`Auto1.rec`...); PLAY
   replays one to its end.
2. Credits roll and return; Hot Keys lists the keys by group; the Vessel
   Library shows each empire's ships.

### Single player

1. **Tutorials:** the six courses, each passed by following its orders
   (Command 190 and 290, Science 310, Weapons 380, Weapons Advanced
   480, Command 590): "You have passed course..." on the debriefing.
2. **Skirmish:** PLAY GAME's "Hostile Skirmish", or an Advanced Custom
   Skirmish against one frigate; S to speed up, R red alert, the
   backquote or Y to target an enemy, the keypad's * to follow it, Z to
   fire: "You are the winner!"
3. **Campaign:** NEW GAME, Federation vs ISC at CAPTAIN; the
   introduction, then the Dynaverse map. A hex clicked moves the ship;
   MISSIONS lists what's offered; ACCEPT, READY, the mission, its
   debriefing and DONE. The campaign played so far:

   | # | Mission | Outcome | Prestige after |
   |---|---|---|---|
   | 1 | Convoy Escort | Astounding Victory | 350 |
   | 2 | Convoy Escort | Astounding Victory | 650 |
   | 3 | Convoy Escort | Astounding Victory | 950 |
   | 4 | Peace In Our Times (`Uni_1Peace`, the first story mission, offered from 800) | Defeat: the diplomat beamed down, the ship lost after | 1,025 |
   | 5 | Convoy Escort | Astounding Victory | 1,325 |
   | 6 | Promises to Keep (`Uni_2Promises`) | Devastating Defeat: the ship lost in the minefield | 1,250 |
   | 7 | Nor Iron Bars A Cage (`Uni_3IronCage`) | Devastating Defeat | 1,175 |
   | 8 | Convoy Escort | Draw | 1,325 |
   | 9 | Convoy Escort | Victory | 1,550 |

   A ship lost, the next mission starts in a new frigate. The story
   missions came in their order whatever the outcome; winning them needs
   their own orders followed (Peace In Our Times: target the planet
   with T, approach it slowly, beam the diplomat down from the
   transporters (N); then resist the three ships that come).
4. **Saving and loading:** the auto-save after each mission, listed by
   the Campaign Selector and loaded with LOAD GAME, the campaign going on
   from it; SAVED GAMES on the map, a save by name made and deleted.
5. **The map's other screens:** SHIPYARD at a base (a ship bought),
   SUPPLY and MEDALS.

### Multiplayer

1. Two copies on a network (or on one machine): Multiplayer, TCP/IP;
   one HOSTs and ANNOUNCEs, the other CONNECTs and JOINs from Select
   Game; chat in the lobby; ships chosen, READY, START: the same mission
   in both, played to "You are the winner!" and "Defeat!".
2. The host leaving mid-game: the other copy takes the session over.
3. IPX: the search runs. Dynaverse II's LOG IN: "Failed to locate the
   directory servers" ([systems/online.md](systems/online.md));
   MPlayer's starts its site and ends the game.

### The 2.0.3.6 patch

1. The patched game to its main menu; its Sulu skirmishes, a skirmish
   at ADMIRAL, a campaign's Scan mission, multiplayer between two
   patched copies; a 1.0 campaign's save loaded.

### Headless

`reluva-tool sfc host <game folder> -save <folder> -fixed-clock` with
`-click`, `-type`, `-key` and `-shot` at frames, as the runs in
[systems/hosting.md](systems/hosting.md) give them: the same input plays
the same way each time. The CD key dialog: `-click 600 395 370 -type
620 <key> -click 700 395 405 -shot 900 out.png`. The campaign above was
played a mission a run, each from the last's auto-save: Single Player
(240,232), Campaign (620,190), the save (150,177), LOAD GAME (262,445),
a hex beside the ship, MISSIONS (53,277), ACCEPT (544,203), READY
(598,501); in the battle the game's speed raised (`]`), red alert, full
speed, and every 60 frames the backquote, the keypad's * and Z; DONE
(653,490) once the debriefing shows.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 28 November 2026 | English retail 1.0 | Linux | A campaign played on through eight more missions, headless with `-fixed-clock`, each run from the last's auto-save, from the first win's: Convoy Escort won twice (950 prestige), then the first story mission, Peace In Our Times (the diplomat beamed down to Organia, then "Defeat"), Convoy Escort won, Promises to Keep and Nor Iron Bars A Cage lost, Convoy Escort drawn and won (1,550). The game-files test, all six steps the same |
| 27 November 2026 | English retail 1.0 and 2.0.3.6 | Linux | Every import stood in for: 1.0's last 9 and the 60 more 2.0.3.6 imports, the program loaded with none left; the game-files test the same |
| 27 November 2026 | English retail 1.0 | Linux | The maps' values in a campaign: the hexes of new and played campaigns' databases compared with the Middle map's cells (economic, strength and impedence copied as base and current economic, victory and speed points); each empire's economic sum the news's total; strength dropped in 28 and 47 hexes after missions |
| 27 November 2026 | English retail 1.0 and 2.0.3.6 | Linux | `sprites.q3`'s objects: every group's children (3,681 groups, 35,260 children in 1.0) and every button and picture's three image lists (6,792) read; the Film Room drawn from them and compared with the game's own screen, 96.3% of its pixels the same; the data viewer's Screens, Settings and Maps pages shown on a virtual X display; the game-files test's formats step re-recorded |
| 27 November 2026 | English retail 1.0 | Linux | A ship bought, headless with `-fixed-clock`: the first win's save loaded, the frigate moved to a Federation planet, SHIPYARD's F-POL+ (228 prestige) bought; the fleet two ships, prestige 122, both in SUPPLY with their trade-in values |
| 27 November 2026 | English retail 1.0 | Linux | Speed in the launcher's window, on a virtual X display driven by `xdotool`: a skirmish's battle at 54 to 62 frames a second, 50 to 52 with the game's speed raised to 11; headless, untraced, the same battle at 91 frames and 148.5 million instructions a second. The host tool had traced every run (48.1 million); the game-files host step the same untraced, in a tenth of the time |
| 27 November 2026 | 2.0.3.6 | Linux | The patched game, headless with `-fixed-clock`: a 1.0 campaign's save loaded (two access violations raised and caught on the way); the four Sulu skirmishes played to their debriefings, none won (Sulu 1 left, "Defeat"; 2 and 4 lost; 3 still fighting at 42 minutes) |
| 27 November 2026 | English retail 1.0 | Linux | The Film Room, headless with `-fixed-clock`: a campaign's two recordings listed from the save folder (Auto1, `Met_13Monster`, 34,474 bytes) and Auto1 played to its end, back to the Film Room |
| 27 November 2026 | English retail 1.0 | Linux | Science 310 passed, headless with `-fixed-clock`: probe, deep scan, the spare part beamed aboard, a transporter mine, and a cargo box pushed into an asteroid with the tractor beam (18:01). All six tutorials now passed |
| 27 November 2026 | English retail 1.0 | Linux | Four more tutorials passed, headless with `-fixed-clock`: Command 290 (20 asteroids destroyed), Weapons 380 (the freighter destroyed with overloaded photons, a mine laid, 13:10), Weapons Advanced 480 (the mock battle with the Daystrom to its ten minutes) and Command 590 (the captured Klingons fought with the fleet, 6:59); Science 310 played to its transporter, three of its runs ending against asteroids |
| 26 November 2026 | English retail 1.0 | Linux | A loaded campaign starting missions again, headless with `-fixed-clock`: a new campaign, Convoy Escort won and Monster played, the auto-save loaded in a second run (the clock going on from the first) and Patrol accepted, READY: the mission started. Before, the same save stuck at READY; with the tick count moved on by 0.5 to 36 million ms it started, the date left as it was |
| 26 November 2026 | English retail 1.0 | Linux | Online play: Dynaverse II's login on the shared Winsock, its directory servers not found and said so; MPlayer's button starting its site and ending the game; IPX searching; two copies joining over TCP/IP as before |
| 26 November 2026 | English retail 1.0 | Linux | Multiplayer between two copies, headless with `-fixed-clock -net`: chat typed in the joining copy's lobby shown in both; the host forfeiting, the joining copy taking the session over and winning (`DPSESSION_MIGRATEHOST`) |
| 26 November 2026 | English retail 1.0 and 2.0.3.6 | Linux | The settings files (40 and 48, none failing), the campaign maps (4 and 9) and `sprites.q3`'s objects (32,789 and 33,056 records, ten kinds) read; the game-files test's formats step re-recorded |
| 26 November 2026 | English retail 1.0 | Linux | A mission's speed: a skirmish's battle on the real clock, 48.1 million instructions and 38.6 frames a second (`-rate`); its work and Convoy Escort's, 31 and 36 million a second, by the share of their instructions not waiting for the next frame (`-profile` on the fixed clock) |
| 26 November 2026 | English retail 1.0 | Linux | The main menu's other screens, headless with `-fixed-clock`: Film Room (its list empty), Credits, Hot Keys, Vessel Library and its Choose Vessel; the Windows stand-ins moved from the engine to the shared layer, the game-files test and the skirmish's frames the same after |
| 26 November 2026 | English retail 1.0 | Linux | A multiplayer game won in battle between two copies, headless with `-fixed-clock -net`: the host following and firing on the joining player's ship, "You are the winner!" and the other "Defeat!"; the Klingons' campaign begun, Monster won (300 prestige), Shipyard Defense started and lost |
| 26 November 2026 | 2.0.3.6 | Linux | The patched game played, headless with `-fixed-clock`: a skirmish won at ADMIRAL; a new campaign's Scan mission won ("Victory", 200 prestige) with red alert, the deep scanners and fire; a multiplayer battle won between two copies |
| 25 November 2026 | English retail 1.0 | Linux | The Command 190 tutorial passed, headless with `-fixed-clock`: red alert, a click on the 3D view, the containers targeted (T) and followed (keypad *), fired on (Z); "You have passed course: Command 190 Basic Controls" at 2:48 |
| 25 November 2026 | English retail 1.0 | Linux | The game-files test (`ctest -L gamedata -R sfc`), its six steps recorded from an install by setup and run again the same: 3,357 files; 19,353 pictures, 126 fonts, 317 models and the sounds read, none failing; 2,721 frames of the opening movie; the CD key dialog and a made-up key refused; the import from the CDs the same as the install, 3,356 of 3,356 files |
| 25 November 2026 | English retail 1.0 | Linux | The skirmish won again, headless with `-fixed-clock`, firing every 120 frames (at 4:22) and every 60 (8:02): "You are the winner!" |
| 25 November 2026 | English retail 1.0 | Linux | A loaded campaign: from the second win's auto-save, Base Defense accepted hangs as Patrol does, the same without the JIT; from the first win's, Convoy Escort won, then no missions offered in the same run |
| 25 November 2026 | English retail 1.0 | Linux | Further into a campaign, headless with `-fixed-clock`: Convoy Escort won firing every 60 frames ("Astounding Victory", 350 prestige; every 120 now loses the frigate); Monster lost, then forfeited (prestige 350 to 250, a new frigate); Patrol loaded and played, lost outnumbered; the auto-save loaded and Convoy Escort won again (650); MEDALS, SUPPLY and SHIPYARD shown. Each input repeats exactly, also with other runs loading the machine. The second win's auto-save loaded, a hex moved, Patrol alone offered and accepted: it never starts (`-profile` finding the program busy each frame from the click) |
| 25 November 2026 | English retail 1.0 | Linux | Multiplayer as between two machines: host and joiner, headless with `-fixed-clock -net`, each in a network namespace of its own (10.9.0.1 and 10.9.0.2, a veth link between them); the session found by broadcast and joined, a ping of 31, Free For All started in both, the joining player forfeiting and the host winning |
| 25 November 2026 | 2.0.3.6 | Linux | The 2.0.3.6 patch applied as it's imported (`reluva-tool import` with both CDs and `sfc2patch20022036.exe`): `Patch.exe` run in the emulator, 309 patches applied, every changed file verified by it; recognised as 2.0.3.6, 76 seconds in all; the same files as `reluva-tool sfc patch` gives but for `API\`. The patched game to its main menu, 5 billion instructions without a stop |
| 24 November 2026 | English retail 1.0 | Linux | A campaign mission won, headless with `-fixed-clock`: a new Federation campaign, Convoy Escort, game speed raised, the nearest enemy targeted and fired on; "Astounding Victory", 300 prestige, the Dynaverse at 350 after; then Monster, a "Devastating Defeat". Before DLLs were unloaded the same input ended in a draw, and Patrol stopped as it loaded |
| 24 November 2026 | English retail 1.0 | Linux | A skirmish won, headless with `-fixed-clock`: Advanced Custom Skirmish set to one Klingon frigate, LOW, CAPTAIN; red alert, Y and Z every 120 frames; the Klingon destroyed at 4:21 and "You are the winner!", its battle report; 60 billion instructions |
| 24 November 2026 | English retail 1.0 | Linux | A multiplayer game played to its end between two copies on one machine, headless with `-fixed-clock -net`: Free For All hosted and joined, started, the joining player forfeiting a minute in (its debriefing: disconnected early), the host told the player left and winning ("Congratulations! You are the winner!"), both sessions closed; Base Assault the same way, the host playing on against the starbase |
| 23 November 2026 | English retail 1.0 | Linux | Multiplayer between two copies on one machine, headless with `-fixed-clock -net`: one hosting Base Assault, the other finding it on Select Game and joining; both listing the two players with their pings, ships chosen, READY, START: the mission in both, their messages passing for five minutes |
| 23 November 2026 | English retail 1.0 | Linux | A multiplayer game started, headless with `-fixed-clock`: hosted, an AI player given a ship from its fleet and readied, the player's ship chosen and readied, START: the DirectPlay session closed and Base Assault started, both ships in space; 30 billion instructions |
| 23 November 2026 | English retail 1.0 | Linux | Multiplayer, headless with `-fixed-clock`: the Game Connections screen, HOST, the eleven multiplayer scenarios listed, ANNOUNCE GAME opening a DirectPlay session, Multiplayer Startup with the player, an AI player added, a ship chosen, READY; START not yet starting it |
| 23 November 2026 | English retail 1.0 | Linux | The opening movie: `reluva-tool sfc intro` decoding all 2,721 frames and its 181.4 s of sound; in the window, played with its sound before the game, a click skipping it to the title screen |
| 23 November 2026 | English retail 1.0 | Linux | Saving by hand and deleting: on the map, SAVED GAMES, "Reluva Test" typed and saved (its folder and files made), then deleted (its files and folder removed, `-calls` showing the paths); Auto Save deleted too, and made again by the game at once |
| 23 November 2026 | English retail 1.0 | Linux | A campaign saved and loaded: its auto-save on finishing Convoy Escort (`Sfc2Spd.sds` and `db.info` under `saves/Auto Save`), listed by the Campaign Selector, loaded with LOAD GAME back to the Dynaverse as saved (commander, ship, prestige, the stardate going on) |
| 23 November 2026 | English retail 1.0 | Linux | A campaign's mission finished: Convoy Escort forfeited from Esc's menu, its debriefing, DONE, back on the Dynaverse map, the ship moved again and missions offered again; 38 billion instructions |
| 23 November 2026 | English retail 1.0 | Linux | A campaign, headless with `-fixed-clock` (the same frames each run, the date fixed too): Federation vs ISC begun, its introduction clicked through, the Dynaverse map shown, the ship moved a hex, MISSIONS opened, Convoy Escort accepted, its briefing and READY, the mission loaded and fought, a freighter lost; 36 billion instructions |
| 23 November 2026 | English retail 1.0 | Linux | The tutorials, headless with `-fixed-clock`: all six started from their screen and run; Command 190 followed (T targets a container, the next order comes) and, its containers not destroyed, ended "incomplete" with its debriefing; Command 290 to its debriefing; the voice-over captured with `-sound` |
| 23 November 2026 | English retail 1.0 | Linux | A skirmish played: headless with `-fixed-clock` (the same frames each run) and in the window with `xdotool`, the game imported from the CDs: S for speed, Y to target, R red alert, the helm's turn arrow, Z and the FIRE button hitting the Klingon, keypad 5's high-energy turn |
| 23 November 2026 | English retail 1.0 | Linux | Importing from the two CDs (`reluva-tool import`): 3,356 files in 20 seconds, every MD5 the header's, byte for byte the owner's install by setup but for `sfc2.isu`; recognised as 1.0. Every reader over it: 126 fonts, 317 models (6 with 4-cornered polygons), the sounds and `sprites.q3`, none failing |
| 23 November 2026 | English retail 1.0 | Linux | A skirmish: with a copy of the install short of files, "No missions available"; with the CD's program files added (taken with `unshield`), "Hostile Skirmish" listed and started, the Klingon coming in, 35 billion instructions without failing |
| 23 November 2026 | English retail 1.0 | Linux | DirectPlay's lobby: `reluva-tool sfc host` past the title screen to the main menu ("Select Game"), its lobby asked each frame; Single Player clicked, its Tutorials, Skirmish and Campaign screen shown; 6 billion instructions without failing |
| 23 November 2026 | English retail 1.0 | Linux | Miles: `reluva-tool sfc host -sound` past the title screen, the music captured byte for byte `MNU_A1.WAV` then `MNU_A2.WAV`; in the launcher's window (SDL's dummy audio driver) to the same point, stopping for want of DirectPlay's lobby |
| 23 November 2026 | English retail 1.0 | Linux | `reluva-tool sfc host`, headless: the owner's CD key typed and taken, the full title screen, `registry.txt` written with the `WON\CDKeys` value; a second run with the same save folder went past the dialog with no input, both stopping at `AIL_set_redist_directory` |
| 22 November 2026 | English retail 1.0 | Linux | In the launcher's window (a virtual X display, the mouse and keys sent by `xdotool`): the title screen and CD key dialog, text typed with capitals and `-`, ENTER answered "incorrect" for a made-up key, the pointer drawn without its background, Ctrl+F5 stopping it. The same headless with `-click`, `-type` and `-shot` |
| 22 November 2026 | English retail 1.0 | Linux | `reluva --detect` on the install's `StarFleet2.exe` and `sprites.q3`: recognised as 1.0 |
| 22 November 2026 | English retail 1.0 | Linux | `reluva-tool sfc host` with exceptions dispatched: four raised and caught, the title screen and its CD key dialog between 0.6 and 1 billion instructions; 2 billion run without failing (1,086 frames) |
| 22 November 2026 | English retail 1.0 | Linux | `reluva-tool sfc host` with Direct3D 6: the 3D test passed, an 800x600 mode, the first frame ("Loading...") after 45.08 million instructions; stops at a C++ exception it expects to catch, after 48.12 million |
| 22 November 2026 | English retail 1.0 | Linux | `reluva-tool sfc host`: start-up to the 3D test, 44,063,247 instructions, stopping for want of Direct3D 3 |
| 22 November 2026 | English retail 1.0 | Linux | Every image, font, model, table and sound read by the commands above, none failing; the data viewer's pages seen in screenshots (Xvfb, Mesa's software Vulkan), a model with its textures |
