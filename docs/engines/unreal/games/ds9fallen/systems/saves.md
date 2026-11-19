# The Fallen: saved games

> **Status:** partial · **Code:** `src/engines/unreal/vm/vm_save.cpp`, `formats/game_save_write.cpp` · **Checked against:** the original's saves, and every map saved and loaded · **Confidence:** from the code unless marked

## Overview

Games are saved and loaded in the original's own format
([save.md](../formats/save.md)), from the game's menus, F6 and F5.

## How it works

`vm_save.cpp` saves a level being played and starts one from a save.

**The original's saved games** ([save.md](../formats/save.md))
load as the engine's `LoadMap` loads them: the save is the level itself,
so its package is opened as the map (`?LOAD=<file>.dsa` adds it to the
packages). With `bBegunPlay` set, nothing begins again; instead
(`resumeSavedGame`):
- the ghosts are made too, set aside;
- the level's time is the saved one;
- each actor goes back into its saved state, its state code at the
  statement it was at. One waiting in `Sleep` waits out what was left
  (`LatentFloat`); one waiting in any other latent function makes the
  call again (a move, a `FinishAnim`: its properties have what it needs);
- each actor's body layers and face channels come from its C3D mesh
  state; what it was touching from its `Touching`, so it isn't touched
  again;
- the dialogue's counts come from the saved table, by key;
- the sounds that were playing start again where they'd got to; the
  music becomes the player's `CurrentMusic` again, from where it was;
- the game's unpaused, and the player logs in with `?load`: the game
  type's `Login` takes up the saved pawn (a PlayerPawn without a player),
  and `CTVE_GameInfo` marks the game loaded so `PostLogin` doesn't start
  the level's opening again. The player gets `bJustLoaded`.

Loaded so, `Bajoran_Ship_-_Ke'el_Tul-97.dsa` (M01_SiskoL1A) has Sisko in
his EVA suit on the Ke'el Tul with 91 health and his phaser drawn. The
speech trigger he'd just stepped on was waiting in its state code, so the
scene goes on (Worf: "Sir, Chief O'Brien was injured during the initial
assault..."), and then he walks on. No script accesses None in 20 seconds.
Not restored: the IK targets, bones' adjustments and attachments in the
mesh states (the scripts set them again each tick), the probe masks (what
events each state ignores, which the states themselves give), and the
dialogue's lines under way (none in the example).

**Saving** writes the original's format ([save.md](../formats/save.md),
`game_save_write.cpp`): QuickSave.dsa, else the level's title and the
seconds played, as the game names its saves, in the game's save folder
(`Saves` in Reluva's folder for it). The original loads them, and Reluva
loads them as it loads the original's (above). Every map saved four
seconds in and loaded back comes back the same: the player where it
was, in its state, with its weapon.

Reluva's own format (its older saves; `reluvaFormat` writes it) still
loads (version 3 keeps a layer's whole state, its clears and what it's
fading in over; version 2 reads). A file of it holds:
The game's side is all script:
- F6 (`QuickSave`) and the main menu's Save Game send `SaveGame`.
- The Load Game list asks `DrawLoadGameList`, which hands the HUD each
  game, newest first (`DrawLoadGameFile(Id, name, time)`).
- `LoadByFileID(Id)` gives the HUD its file, and the HUD travels to
  `?LOAD=<file>`. The page loads that game's map (or, for the
  original's, the save itself) and carries on from it.
- F5 (`QuickLoad`) asks to confirm, then travels to
  `?load=QuickSave.dsa`. With none there, the HUD is told (`HudEvent`
  1010) and says so.
- `DeleteByFileID` deletes one.
- `?RESTART` (`RestartLevel`) plays the map again from where it began.

Checked on M04_KiraL1 (quick save, walk off, quick load: the same
place, view and HUD) and with the menus' Save and Load. All 74 maps
save after four seconds of play, load back and play on (`unreal play ...
savegame QuickSave travel "?load=QuickSave.dsa"`).

Not kept: what the engine works out again each tick (posed meshes,
the colliders' list, the limbs' effectors) and the dialogue table's
voice lengths.

## In Reluva

`vm/vm_save.cpp` saves a level being played and starts one from a save;
`formats/game_save_write.cpp` writes the file. Saves go in `Saves` in
Reluva's folder for the game.

## Sources and checks

- **Where the facts come from:** Engine.dll's `LoadMap` and `SaveGame`,
  read for what they do, and the original's saves.
- **How it was checked:** on M04_KiraL1 and every map (above).
