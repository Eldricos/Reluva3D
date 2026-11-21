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
  state, and so do the actors it holds (attached once, as a phaser put
  away at the belt, so the save is all there is of them); what it was
  touching from its `Touching`, so it isn't touched again;
- the events each actor had disabled (`Disable('Tick')`) are those its
  state and class listen for that its saved probe mask leaves out;
- the dialogue's counts come from the saved table, by key, and its lines
  under way go on where they were, each on its clock, those waiting for
  their speaker still waiting, as the engine's `InitDialog` keeps them
  (0x10337890; their voices aren't started again by it). It numbers new
  threads from 1 again, as Reluva does;
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
Its security officer's phaser goes back to her belt clip. Not restored:
the IK requests and targets in the mesh states (`CTV_Player` sets them
again every tick). Saved 1.5 s into Worf's line after loading it, a game
of Reluva's keeps the line under way (WRF148, done at 105.02 s), and
loaded, it goes on to its end there.

**Saving** writes the original's format ([save.md](../formats/save.md),
`game_save_write.cpp`): QuickSave.dsa, else the level's title and the
seconds played, as the game names its saves, in the game's save folder
(`Saves` in Reluva's folder for it). The original loads them, and Reluva
loads them as it loads the original's (above). Every map saved four
seconds in and loaded back comes back the same: the player where it
was, in its state, with its weapon.

Reluva's own format (its older saves; `reluvaFormat` writes it) still
loads (version 4 keeps, for a line under way, the line whose
RETURNLINE it carries, which a save of the original's names; version 3
keeps a layer's whole state, its clears and what it's fading in over;
2 and 3 read, a line under way in them carrying none). A file of it is
the magic `RLVSAVE4` (or `RLVSAVE3`, `RLVSAVE2`), then, little-endian,
with strings as a length and their bytes:
- the map's name, the save's description and the time saved (Unix
  seconds, 64 bits);
- the objects: each either one of the map's actors by its export, or
  made anew by its class and name. Then, for each, its properties (each
  a type byte and its value; an object as one of the saved objects, a
  package's export, a class, one of the map's exports, or the player's
  viewport, console or canvas), its state, the state its code runs in
  and the statement it's at, what it's waiting on, the events its state
  ignores and its timer;
- the level's lists by the saved objects' numbers: the actors, the
  ghosts, the player and the game type, the level's time and ticks;
- what the level keeps for each actor: what it's touching, its moves
  under way, falling, waiting on an animation, its body layers and face
  channels, an attachment, and its bones' bends and IK;
- the dialogue: whether its table was loaded, each line's plays and the
  lines under way;
- the surfaces' textures the scripts changed, the class name counts (so
  new actors' names go on from them), and the textures' own properties
  the scripts set (the HUD's places, the FLC textures' playing).

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
