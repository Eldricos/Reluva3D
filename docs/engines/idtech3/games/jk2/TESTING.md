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
  the paks), `jk2-videos` (`roq-decode`: the opening logos and the first
  cinematic, 60 frames each, their pictures and sound), `jk2-menus`
  (`ui-run`: the multiplayer menus' main menu, Setup, and Create Server
  by way of Play). Records: `tests/engines/idtech3/data/gamedata/jk2-1.02a/`. See
  [DEVELOPMENT.md](../../../../DEVELOPMENT.md#engine-tests).

### What the automated checks don't cover

The launcher's *Add Game...* and *Import Game...* with the real CD; how
the maps look (`reluva-viewer`'s World page), against the original; the
videos' sound heard (the Videos page), and a video texture seen in
kejim_post (Mon Mothma's screens); the multiplayer menus' music heard,
and the other menus clicked through on the Menu page.

## A playthrough by hand

Not playable yet.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 2026-11-14 | English 1.02a | Linux | The files' layout and headers, for the plan; detection, `reluva-tool import` from the CD's files and an install, the game-files steps; every map in the World page; Ghoul2 people and droids in the Models page |
