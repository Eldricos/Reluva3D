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
  `jk2-models` (`model-check`, the MD3 models), `jk2-images`
  (`image-check`, the images the maps' shaders name: 15 missing, as in
  the paks). Records: `tests/engines/idtech3/data/gamedata/jk2-1.02a/`. See
  [DEVELOPMENT.md](../../../../DEVELOPMENT.md#engine-tests).

### What the automated checks don't cover

The launcher's *Add Game...* and *Import Game...* with the real CD; how
the maps look (`reluva-viewer`'s World page), against the original.

## A playthrough by hand

Not playable yet.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 2026-11-14 | English 1.02a | Linux | The files' layout and headers, for the plan; detection, `reluva-tool import` from the CD's files and an install, the game-files steps; every map in the World page |
