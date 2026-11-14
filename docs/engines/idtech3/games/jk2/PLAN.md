# Jedi Outcast: plan

## Goal

Star Wars Jedi Knight II: Jedi Outcast, the English Windows release at
1.02a (the copy this plan was made from), played through as the original:

- **Single player:** all 26 levels, from `kejim_post` to `yavin_final`,
  with the game's menus, datapad, cinematics, saving and loading. The game's
  own code (`jk2gamex86.dll`) runs in Reluva's x86 interpreter, as Elite
  Force's single player does.
- **Multiplayer:** the 15 levels (free for all, duel, capture the flag),
  with bots, run by the game's own QVMs, hosting and joining Reluva and
  retail 1.02 servers.

Later: the 1.03 single-player and 1.04 multiplayer patches, other
languages, the Mac release.

### What the game holds

Read from the copy (see [README.md](README.md#releases) for the files and
their hashes):

| What | Files | Reluva has it? |
|---|---|---|
| Paks | `base/assets0.pk3` (6,674 files), `assets1.pk3` (8,011, nearly all speech) | Yes: pk3 search paths, the base folder named `base` |
| Shaders | 29 `shaders/*.shader` | The Quake III directives; Jedi Outcast's own still to list |
| Maps | 41 `maps/*.bsp`, **RBSP version 1**, 18 lumps (Quake III's 17 and a light array) | No: only IBSP 46 |
| Models | 71 Ghoul2 meshes (`.glm`) and 21 skeletons (`.gla`), both version 6; 276 MD3 | MD3 only |
| Game code, single player | `jk2gamex86.dll`: the game and client game in one DLL (exports `GetGameAPI`, `dllEntry`, `vmMain`), with ICARUS, the effects system and navigation inside it | The x86 interpreter, PE loader and Elite Force's DLL host |
| Menus, single player | `ui/*.menu`, `ui/menus.txt`: Team Arena style menu scripts, run by `jk2sp.exe` itself (no menus DLL) | No |
| Game code, multiplayer | `vm/jk2mpgame.qvm`, `cgame.qvm`, `ui.qvm` (Quake III's QVM format) | The QVM interpreter |
| Scripts | 1,356 compiled ICARUS scripts (`.ibi`), 68 ROFF camera/object paths (`.rof`) | Not needed by the engine: the DLL runs them |
| Effects | 229 `.efx` | Not needed by the engine: the DLL reads them |
| Text | 30 StripEd string packages (`strip/*.sp`), each line in English, French and German | No |
| Fonts | 3 `fonts/*.fontdat` with their `.tga` | No |
| Sound | 8,126 MP3 (speech, music), 309 WAV; EAX settings per map (`eagle/*.eal`) | MP3 and WAV decoding |
| Cinematics | 15 RoQ videos (`video/*.roq`) | No: Bink, Smacker and others, not RoQ |
| Bots | `botfiles/*.jkb`, waypoints `botroutes/*.wnt`; no `.aas` | Quake III's botlib, which these bots may not use |
| Navigation | 26 `maps/*.nav` (single player's) | Not needed by the engine: the DLL reads them |

## Phases

Each phase ends with something that runs, tested against the copy, and
committed. The order follows Elite Force's: what can be seen without the
game's code first, then multiplayer, whose engine calls can be read from
the QVMs' bytecode, then single player, which reuses those engine services
behind the DLL's own tables.

1. **Recognise and import** (done): `jk2/game_info.cpp` (`jk2`, base
   folder `base`, shaders in `shaders/`, `GameCode::kNone`); detection by
   `assets0.pk3`'s hash, others by file names as an unknown release;
   *Import Game...* from the CD (its `GAMEDATA/GameData/`), an install
   (`GameData/`) or the folder itself, taking the paks, saved games,
   recordings (`.dm_15`) and `jk2gamex86.dll`. `reluva` says the game
   can't be played yet; `reluva-viewer` shows its files. Its shaders read
   (1187), the 18 directives Quake III doesn't have listed in
   [shader.md](../../formats/shader.md); every MD3 model reads. The
   game-files test: `RELUVA_JK2` ([TESTING.md](TESTING.md)).
2. **RBSP maps** (done): Raven's BSP read by the IBSP reader
   into the same `Bsp` (four light styles per surface and vertex, the
   light grid's shared points, the level's fog), all 41 maps read and
   drawn in the World page, every style full and added into the first
   style's lightmap as it loads ([rbsp.md](../../formats/rbsp.md));
   `oneshotAnimMap` and `clampAnimMap` drawn; surface sprites
   (`surfaceSprites` and its `ss*` options, 41 shaders: grass, ferns,
   vines, bubbles) placed as the level loads and drawn turned, swaying and
   fading as the original's ([shader.md](../../formats/shader.md));
   `sun`, `lightColor` and `notc` found to have nothing to draw; game-files
   steps over every map and the images they name. With the game's code
   (phase 6): the light styles' own passes, animated, and the wind and
   weather that move surface sprites and show weather ones.
3. **Ghoul2 models** (done): GLM meshes and GLA skeletons read and
   checked (`ghoul2-check`: all 71 and 21), posed on the CPU for each
   draw by the model cache, each level of detail a model; only surfaces
   without flags drawn; `model_default.skin`; the skeleton's
   `animation.cfg` in Raven's layout (looping, backwards). The Models page
   shows and animates every one ([ghoul2.md](../../formats/ghoul2.md)).
   With the game's code (phase 6): its Ghoul2 calls (each part's
   animation, bones it turns, bolts, surfaces on and off, collision).
4. **Text, fonts and RoQ** (done): string packages (`raven/strings`,
   [strings.md](../../formats/strings.md)), Raven's fonts (`raven/fonts`,
   [fonts.md](../../formats/fonts.md)), shown in the data viewer's Text
   page; a RoQ decoder in `src/video/`
   ([roq.md](../../../../common/video/roq.md)), whose videos play in the
   Videos page (the opening logos with their sound) and as `videoMap`
   textures. Cinematics open through `CinematicMovie` (Bink or RoQ by
   file type), ready for single player.
5. **Multiplayer menus**: a host for `ui.qvm` (`jk2/ui_host`), its system
   calls worked out from the bytecode (`qvm-info`, `qvm-refs`); fonts,
   strings and the player model (Ghoul2) in the menus. Ends with:
   multiplayer's menus run, Setup works, the model in Player settings.
6. **Multiplayer game**: `jk2mpgame.qvm` and `cgame.qvm` hosts, their
   system calls, and the engine's Ghoul2 calls they make (animation, bolts,
   collision against Ghoul2 models for hits and the saber); the Play page
   for the game. Then the network protocol (1.02's, to be confirmed as 15)
   with its entity and player state layouts, and the bots (the QVM's own
   waypoint code; which botlib calls it still makes is to be found). Ends
   with: a free-for-all against bots, and a match against retail 1.02.
7. **Single player's game**: the DLL's `GetGameAPI` tables (the engine
   functions it's handed, and the game's functions it hands back) and the
   client game's system calls through `dllEntry`, worked out from
   `jk2sp.exe` as Elite Force's were (see Decisions); the Windows calls it
   imports (KERNEL32's and WINMM's only). `reluva-tool idtech3 sp-run` on
   `kejim_post`, then the Play page. Ends with: the first level played to
   its end, cinematics included.
8. **Single player's menus**: the Team Arena style menu system run natively
   (`ui/menus.txt`, every item type, the owner draws and `uiScript`
   commands that `jk2sp.exe` provides: new game, load and save lists,
   controls, the datapad's force powers, weapons, inventory and mission).
   Ends with: a new game started from the main menu, the datapad in play.
9. **Single player complete**: saved games (Raven's chunks, shared with
   Elite Force in `raven/savegame`), dynamic music (`ext_data/dms.dat`),
   speech with lip sync, all 26 levels played through; a game-files step
   per level, saving and loading.
10. **Other releases**: the 1.03 and 1.04 patches, other languages
    (string packages already hold French and German), the Mac release.
11. **Extras, once everything above is done and tested**: EAX
    environmental sound (`eagle/*.eal`) and force feedback
    (`forcecfg/*.fcf`, which the original plays through `FFC10.dll`).

## Decisions

| Date | Decision | Why |
|---|---|---|
| 2026-11-14 | Game id `jk2`, code in `src/engines/idtech3/jk2/`, docs in `games/jk2/` | The name the game's own files use (`jk2sp.exe`, `jk2mpgame.qvm`); the engine's README already names `jk2/` as this game's folder |
| 2026-11-14 | Same engine (`idtech3`), not a new one | It's Quake III's engine as Raven changed it, the same generation as Elite Force's: pk3 files, shaders, the QVM, Raven's save chunks are shared, and what's new (RBSP, Ghoul2, fonts, string packages) is shared again by Jedi Academy and Soldier of Fortune II |
| 2026-11-14 | Run the game's own code (the DLL in the x86 interpreter, the QVMs in the QVM interpreter), as with Elite Force | ICARUS, the effects system, navigation and the client game are all inside the DLL; the engine provides only what the executable did. Rewriting the game logic would be far larger and less faithful |
| 2026-11-14 | Jedi Outcast's GPL source (Raven's 2013 release) as a guide only, never copied | It may be read to solve unknowns (as Jedi Academy's may), but Reluva's code stays all new (CONTRIBUTING.md). It's a guide to shapes and names: each table, call number and layout is checked against the retail 1.02a files (addresses in `jk2sp.exe`, `jk2mp.exe`, the QVMs' bytecode), since the release isn't 1.02a's code |
| 2026-11-14 | Multiplayer before single player (phases 5-6 before 7-9) | Multiplayer has no scripts, so the basic gameplay (moving, weapons, the saber, force powers, Ghoul2's hits) can be tested in it before single player builds on it. Its QVMs' engine calls can be read from their bytecode, and the engine services they need (Ghoul2's calls, fonts, strings) are then there for the DLL, whose tables are harder to work out. Elite Force went the same way. Single player's menus come last because `jk2sp.exe` holds them, so they're Reluva's to write in full |
| 2026-11-14 | EAX (`eagle/*.eal`) and force feedback (`forcecfg/*.fcf`) only after everything else is done and tested (phase 11) | The game plays the same without them, and neither is available through Reluva's `System` yet. Until then they're listed in KNOWN_DIFFERENCES.md |
| 2026-11-14 | Shader scripts' folder per game (`GameInfo::shaderDir`), not both folders for every game | Each original reads only its own (Elite Force `scripts/`, Jedi Outcast `shaders/`), so a mod's file in the other folder is ignored as the original ignores it |
