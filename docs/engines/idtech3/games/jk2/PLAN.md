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
| Scripts | 1,356 compiled ICARUS scripts (`.ibi`), 68 ROFF camera/object paths (`.rof`) | In single player the DLL runs them; in multiplayer the engine plays ROFFs for the game's code ([roff.md](../../formats/roff.md)) |
| Effects | 229 `.efx` | In multiplayer, the engine runs them for the client game ([efx.md](../../formats/efx.md)); in single player, the DLL reads them |
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
   (phase 6, done): the light styles' own passes, animated, and the wind
   and weather that move surface sprites and show weather ones.
3. **Ghoul2 models** (done): GLM meshes and GLA skeletons read and
   checked (`ghoul2-check`: all 71 and 21), posed on the CPU for each
   draw by the model cache, each level of detail a model; only surfaces
   without flags drawn; `model_default.skin`; the skeleton's
   `animation.cfg` in Raven's layout (looping, backwards). The Models page
   shows and animates every one ([ghoul2.md](../../formats/ghoul2.md)).
   With the game's code (phase 6, done): its Ghoul2 calls (each part's
   animation, bones it turns, bolts, surfaces on and off); 1.02a's
   multiplayer has no Ghoul2 collision ([UNCONFIRMED.md](UNCONFIRMED.md),
   Settled), so that waits for single player's.
4. **Text, fonts and RoQ** (done): string packages (`raven/strings`,
   [strings.md](../../formats/strings.md)), Raven's fonts (`raven/fonts`,
   [fonts.md](../../formats/fonts.md)), shown in the data viewer's Text
   page; a RoQ decoder in `src/video/`
   ([roq.md](../../../../common/video/roq.md)), whose videos play in the
   Videos page (the opening logos with their sound) and as `videoMap`
   textures. Cinematics open through `CinematicMovie` (Bink or RoQ by
   file type), ready for single player.
5. **Multiplayer menus** (done): a host for `ui.qvm` (`jk2/ui_host`,
   behind `client/menu_host.h`, which Elite Force's host shares), its
   system calls checked against the bytecode
   ([systems/syscalls.md](systems/syscalls.md)); the menu scripts through
   the engine's script reader, Raven's fonts and string packages, the
   turning emblem of the main menu, the menus' music. The main menu, Play,
   Setup, Controls and Create Server open and draw. The
   multiplayer menus show players as icons, so there's no model in Player
   settings to draw; Ghoul2 in a menu waits for single player's. The
   server lists find servers (the local network, the masters,
   favourites); joining one and Begin wait for phase 6.
6. **Multiplayer game** (done): `jk2mpgame.qvm` and `cgame.qvm` hosts, their
   system calls, and the engine's Ghoul2 calls they make (animation, bolts,
   collision against Ghoul2 models for hits and the saber); the Play page
   for the game. Then the network protocol (1.02's: 15, as `jk2mp.exe`
   registers it) with its entity and player state layouts, and the bots.
   Ends with: a free-for-all against bots, and a match against retail 1.02.

   What's known so far, from 1.02a's QVMs (`qvm-info`, `qvm-dis
   -blockcopies`) and the GPL source's multiplayer `g_public.h` and
   `cg_public.h` as a guide:
   - The game module (`jk2mpgame.qvm`: 249,138 instructions, 1,112
     functions) uses calls 0–50 in Quake III's order with Raven's after
     it (ROFF mover paths 47–50, string packages), the maths calls 100–111,
     a few of the bot library's (setup, snapshot entities, console
     messages, user commands; the elementary actions 400–425; goal, move
     and weapon states) and Ghoul2's (584–599: bolts, models, animation).
     Its bots run on its own waypoints: it makes no area (AAS) call but
     303. Calls 0–38 are numbered as Elite Force's, so the host is to be
     Elite Force's, generalised: each game's call numbers and layouts in
     a table.
   - The client game (`cgame.qvm`: 216,795 instructions) uses about 140
     calls, Ghoul2's among them (200–300).
   - An entity state is 296 bytes and a player state 1,368, as both
     modules copy them whole; the GPL `entityState_t` adds up to 296. The
     server's part after it (`entityShared_t`) is Quake III's with
     `mIsRoffing` and two `broadcastClients` words: 112 bytes, to 408.
     To check against the game's own code as it runs.

   Done so far: the game module runs on the shared host
   ([systems/game-calls.md](systems/game-calls.md)): a level starts, a
   player joins and moves, bots play. The client game runs on the shared
   client game host ([systems/cgame-calls.md](systems/cgame-calls.md)):
   it draws the HUD and the view. Players' Ghoul2 instances are posed
   (animations, angle overrides, bolts) and the renderer draws them.
   The Play page runs a local match from Create Server's Begin: the force
   menu opens on joining, and the player plays in first or third person.
   Raven's effects run for the client game: blaster bolts, sparks, smoke,
   marks, lights and the saber's trail ([efx.md](../../formats/efx.md)).
   The network protocol (15) runs: Join Server joins a server, and others
   can join a hosted match ([systems/network.md](systems/network.md)).
   Light styles change as the client game sets them
   ([rbsp.md](../../formats/rbsp.md)); recorded paths (ROFF) play on
   movers ([roff.md](../../formats/roff.md)). Capsule traces work, for
   mods: 1.02a's own code never uses them
   ([bsp.md](../../formats/bsp.md), Collision). Ghoul2's animations blend
   into each other ([ghoul2.md](../../formats/ghoul2.md), Animation).
   The client game's last calls are answered (the light at a point,
   shader remaps, a pane's corners, forced view angles and turn limits,
   muting sounds), and ROFF notes reach both modules.
   The deltas, user commands and XOR agree with jk2mp.exe's own
   routines, run in the x86 interpreter (`delta-check`), and matches were
   played against the retail game both ways, on Windows (November 2026,
   [TESTING.md](TESTING.md)). What's left differs only in details
   ([KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md)).
7. **Single player's game**: the DLL's `GetGameAPI` tables (the engine
   functions it's handed, and the game's functions it hands back) and the
   client game's system calls through `dllEntry`, worked out from
   `jk2sp.exe` as Elite Force's were (see Decisions); the Windows calls it
   imports (KERNEL32's and WINMM's only). `reluva-tool idtech3 sp-run` on
   `kejim_post`, then the Play page. Ends with: the first level played to
   its end, cinematics included.

   Started ([systems/sp-interface.md](systems/sp-interface.md)): the
   engine's table (103 slots, the first 42 as the Elite Force Expansion
   Pack engine's), the game's, the entity layout and the version are
   read from `jk2sp.exe`; Elite Force's single-player host loads the DLL
   in a Jedi Outcast mode. The client game's 112 calls are mapped from
   `jk2sp.exe`'s jump table and most answered, sharing Elite Force's
   handling where they match. Ghoul2's instances are built in the game's
   memory with 1.02a's layouts, the engine's array object and most of its
   Ghoul2 functions answered ([systems/sp-ghoul2.md](systems/sp-ghoul2.md)):
   `sp-run` runs `kejim_post`'s `Init`, the player joins and the level
   runs, its scripts starting the opening cinematic; the client game
   runs too, its HUD's menus read by the engine's menu reader
   ([systems/sp-menus.md](systems/sp-menus.md)) and bolts posed from the
   records. The Play page runs it: the start page's Single Player loads
   `kejim_post`, the level drawn with its light styles, the client
   game's scene entities and Ghoul2 models posed from the records, with
   single player's own settings and keys (`default.cfg`, kept apart from
   multiplayer's as the original's two programs keep theirs). The
   opening plays through: the text crawl, the movie, the cutscene, then
   the player in the level ([systems/sp-cinematics.md](systems/sp-cinematics.md)).
   `kejim_post` plays through to its level change with cheats
   (`sp-run -tour`: every trigger, fighting with every direct-fire
   weapon); the brush models' contents and call 88 came of it. Games
   save and load: the automatic saves, `save` and `load` (`F12`, `F9`)
   and `load *respawn` after a death, compressed chunks and JPEG
   pictures, the game's Ghoul2 instances with them
   ([systems/sp-saves.md](systems/sp-saves.md)). `kejim_base` plays
   through the same way, the player carried over from `kejim_post` and
   on into `artus_mine` (`sp-run -next`). `artus_mine` plays to
   `artus_detention` by a written route (its way out waits on a squad
   killed and two triggers used); the tour learned to hold use, face a
   trigger's angle and leave corpses and pits alone, and the page runs
   the game's `wait` (its checkpoint saves, `wait 2;save auto*`).
   `artus_detention` plays through by the tour to `artus_topside`, with
   nothing new needed. `artus_topside`'s AT-STs brought the Ghoul2
   collision in traces ([systems/sp-ghoul2.md](systems/sp-ghoul2.md),
   Collision) and the game's own console commands (`npc`, `use`); its
   way out needs the shields lowered, both ion cannons and three AT-STs
   destroyed with the emplaced guns (`sp-run`'s `@aim` mans and aims
   them), then Desann's fight lost as the script means; a written route
   plays it to `valley`. `valley`, a cutscene, plays itself to the movie
   `jk02` and `yavin_temple`, Kyle given back his health and his first
   Force powers. `yavin_temple` plays to `yavin_trial` by a short route:
   the meeting with Luke (a scene of two and a half minutes), then the
   level change; `yavin_trial` by the tour to `ns_streets`. `ns_streets`
   plays to `ns_hideout` by the tour then a route: the scrap car, its two
   switches force-pushed, through the tunnel (the server world now gives
   the game the entities in a box in the original's order). `ns_hideout`
   plays to `ns_starpad` by the tour, which keeps out of a kill
   trigger's box as out of a `trigger_hurt`'s (the level starts with
   the player standing in one). `ns_starpad` plays to
   `bespin_undercity` by the tour then a route: the hangar squads, the
   roof doors, the fuel code, the cockpit and the Lady Luck's gun.
   `bespin_undercity` plays to `bespin_streets` by the tour (sparing
   the R5 droid, whose death fails the mission), then a short route:
   the droid's drive to the exit lift, then the lift. `bespin_streets`
   plays to `bespin_platform` by the tour, nothing new needed.
   `bespin_platform` cuts limbs off, which brought two more Ghoul2 slots
   (copying an instance, the new origin), and plays to `cairn_bay` by
   the tour then a route: the doors' console, cinematic 22 and Tavion's
   fight. Next: the next levels the same way, and a console on the page.

8. **Single player's menus**: started with the menu files read, which
   the client game's HUD needs ([systems/sp-menus.md](systems/sp-menus.md)).
   The Team Arena style menu system run natively
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
