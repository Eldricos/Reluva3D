# Jedi Outcast: testing

## Automated checks

- **Without the game:** the unit tests (`idtech3.unit.files`) import a
  made-up copy laid out as the CD is and check what's taken and that the
  result is found as Jedi Outcast.
- **With your copy of the game:** the game-files test, with the folder
  holding `base` (an install's `GameData`, the CD's `GAMEDATA/GameData`, or
  an import):

  ```sh
  RELUVA_JK2=/path/to/GameData ctest --test-dir build -R idtech3.gamedata.jk2
  ```

  Its steps, beside Elite Force's: `jk2-release` (what's detected),
  `jk2-maps` (`bsp-check`, every map), `jk2-shaders` (`shader-check`),
  `jk2-models` (`model-check`, the MD3 models), `jk2-ghoul2`
  (`ghoul2-check`, every Ghoul2 mesh and skeleton), `jk2-images`
  (`image-check`, the images the maps' shaders name: 15 missing, as in
  the paks), `jk2-effects` (`efx-check`, every effect file and the
  shaders, models and sounds it names), `jk2-roffs` (`roff-check`, every
  recorded path), `jk2-videos` (`roq-decode`: the opening logos and the first
  cinematic, 60 frames each, their pictures and sound), `jk2-game` (`game-run`: the
  game module on ffa_bespin, the player joining and running, then two
  bots, then the client game drawing the view and its effects as the
  player fires, then a player joining over the network in memory:
  `net-run -loopback`), `jk2-menus`
  (`ui-run`: the multiplayer menus' main menu, Setup, and Create Server
  and Join Server by way of Play), `jk2-single-player` (`sp-run`: the
  single-player game DLL on kejim_post, its `Init`, the player joining
  and 100 server frames, the client game drawing each, on a fixed
  clock; then the level saved and loaded into a new game, which runs on),
  `jk2-sp-tour` (`sp-run -tour`: kejim_post, then kejim_base, played
  through with cheats from the end of the opening, every trigger
  touched and the nearest stormtroopers fought between them with every
  direct-fire weapon, to the level change; after each, `-next`: the
  next level started with the player carried over, its weapons,
  ammunition and health recorded; about seven minutes), `jk2-sp-artus`
  (the same for artus_mine and artus_topside, by the routes in
  `tests/engines/idtech3/data/routes/`: artus_mine's generator room's
  squad, the generator's cinematic and the tram; artus_topside's
  shields, ion cannons and AT-STs from the emplaced guns, cinematic 9
  and Desann's fight; artus_detention by the tour; then valley, a
  cutscene, left to play to its level change; about eight minutes),
  `jk2-sp-yavin` (the Yavin levels: yavin_temple by its route, Luke's
  scene then the level change; yavin_trial by the tour),
  `jk2-sp-nar-shaddaa` (ns_streets: the tour, then its route, the scrap
  car and its two switches; ns_hideout by the tour; ns_starpad by the
  tour then its route, about forty minutes), `jk2-sp-bespin`
  (bespin_undercity: the tour, then the R5 droid's drive to the exit
  lift; bespin_streets by the tour; bespin_platform by the tour then
  its route, the doors' console and Tavion's fight), `jk2-sp-cairn`
  (cairn_bay: the tour, then the lift to the exit). Records: `tests/engines/idtech3/data/gamedata/jk2-1.02a/`. See
  [DEVELOPMENT.md](../../../../DEVELOPMENT.md#engine-tests).

### What the automated checks don't cover

The launcher's *Add Game...* and *Import Game...* with the real CD; how
the maps look (`reluva-viewer`'s World page), against the original; the
videos' sound heard (the Videos page), and a video texture seen in
kejim_post (Mon Mothma's screens); the multiplayer menus' music heard,
and the other menus clicked through on the Menu page; a match from
Create Server's Begin: the force menu on joining, Apply and join,
walking, the saber and third person (`p`), bots added from the console
(`addbot tavion`); network play: a match the Play page hosts joined from
another machine (or `reluva-tool idtech3 net-run <base> -join
<address>`), and a server (`net-run <base> ffa_bespin 300 -set sv_pure
0`) found and joined from Join Server; the message code against
jk2mp.exe's own (`reluva-tool idtech3 huffman-check <jk2mp.exe>`, which
needs an install: an import has no jk2mp.exe), and the deltas, commands
and XOR too (`reluva-tool idtech3 delta-check <jk2mp.exe>`); Join
Server's lists
against real servers: the local network, the Internet (Get New List with
Source: Internet), a favourite added and kept after restarting, and
Server Info on one.

## A playthrough by hand

Not playable yet.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 2026-11-14 | English 1.02a | Linux | The files' layout and headers, for the plan; detection, `reluva-tool import` from the CD's files and an install, the game-files steps; every map in the World page; Ghoul2 people and droids in the Models page |
| 2026-11-15 | English 1.02a | Linux | `huffman-check` and `delta-check` against the install's jk2mp.exe: all agree |
