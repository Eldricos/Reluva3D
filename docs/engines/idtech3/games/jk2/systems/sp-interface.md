# Jedi Outcast: how single player's DLL and the engine talk

> **Status:** partial · **Code:** `src/engines/idtech3/eliteforce/sp_game_host.cpp` (shared with Elite Force), `src/engines/idtech3/jk2/sp_game_calls.cpp` · **Checked against:** `jk2sp.exe` 1.02a's code; `jk2gamex86.dll` running the first level in Reluva · **Confidence:** from the code unless marked

## Overview

Single player's game logic is `jk2gamex86.dll` (exports `GetGameAPI`,
`dllEntry` and `vmMain`; imports only KERNEL32's and WINMM's, all of
which Reluva's Windows stand-ins provide). Like Elite Force's
`efgamex86.dll` it holds both the server game and the client game, and
Reluva runs it the same way, in its x86 interpreter, with the engine's
side answered by Elite Force's single-player host in a Jedi Outcast mode.
ICARUS, the effects system and NPC navigation are inside the DLL, not
the engine.

The GPL Jedi Outcast source's single-player `g_public.h` and
`cg_public.h` were read as a guide to the tables' shapes; every count,
place and layout below was checked in `jk2sp.exe` 1.02a. Calls are
cdecl; pointers are addresses in the emulated process.

## How it works

### Loading (0x4368e0 on)

1. The engine fills its table on the stack (0x4368e0–0x436cf5) and
   calls `GetGameAPI` (looked up at 0x440703) with it.
2. The game's table comes back; its first word, the API version, must be
   **6** (0x436d58), as Elite Force's.
3. The same DLL's `dllEntry` is handed the client game's call handler
   (0x43eea0, which hands its arguments to 0x401570), and its `vmMain`
   kept (0x440740). `dllEntry` itself already calls the handler: it
   registers settings (call 3).

### Starting a level (0x437250 on)

The level's clock starts at 1000 ms; the game's `Init`, then three
frames 100 ms apart, a player still connected from the last level
connected again, then one more frame. The player joins
(`ClientBegin`) only once their client game has loaded, and the server
frame (0x437d60) doesn't wait for that: it runs the time gone by, up to
a second a call (0x437dbc), in 50 ms frames. So about a second of
frames runs before the player begins, in which the level's NPCs spawn
(at 1550 ms, after the frames before the player connects); the scripts
the player's arrival starts look for them as they load.

### The engine's functions (import table)

103 slots, the same count as the GPL source's `game_import_t`:

- **0–41:** as the Elite Force Expansion Pack engine's 42
  ([../../stvef/systems/sp-interface.md](../../stvef/systems/sp-interface.md)):
  `Printf` 0 … `FS_FreeFile` 15, `FS_GetFileList` 16,
  `AppendToSaveGame` 17, `ReadFromSaveGame` 18 (with a fourth argument
  and the length returned), its optional form 19, `SendConsoleCommand`
  20 … `EntityContact` 38, `VoiceVolume` 39 (an array, not a function).
  Two differ in their arguments: `trace` 29 takes two more after the
  content mask (a Ghoul2 trace type and level of detail), and `Malloc`
  40 takes a tag and whether to zero what it gives, before `Free` 41.
  One differs in what it does: `SetBrushModel` 26 (0x4364f0) sets the
  entity's contents to what its model's brushes and curves hold, ORed
  (0x40d290), where Quake III's and Elite Force's set every bit (-1),
  then links it.
- **42–102:** Ghoul2's, in the GPL source's order: from
  `G2API_PrecacheGhoul2Model` 42 and `G2API_InitGhoul2Model` 43 to
  `RE_RegisterSkin` 101 and `RE_GetAnimationCFG` 102 (the names in
  `jk2/sp_game_calls.cpp`). *(Their order follows the GPL source; only
  the count was checked.)* Many take the instance as the game's
  `CGhoul2Info_v`, a handle into an array the engine owns and hands out
  through `TheGhoul2InfoArray` 83 as a C++ object with virtual
  functions, whose elements the game also reads directly.

The fill is one long run of stores into the table on the stack, with a
`push` halfway (0x436c02) that moves the later offsets by 4: slots 77
and 82 onward are written 4 bytes further up than their places.

### The game's functions (export table)

The API version, then 15 functions, then three words:

| Place | What |
|---|---|
| 0 | API version (6) |
| 1–13 | as Elite Force's Expansion Pack's: `Init(mapname, spawntarget, checksum, entities, levelTime, randomSeed, globalTime, savegameLoaded, loadTransition)` … `ConsoleCommand` |
| 14, 15 | `PrintEntClassname(clientNum)`, `ValidateAnimRange(start, end, speed)` |
| 16, 17, 18 | the entities, their size, how many are in use |

### Entities

The parts the engine shares, as `jk2sp.exe`'s `linkentity` (0x43a620)
reads them: the entity state is 252 bytes (Elite Force's 232, then a
model scale, a radius and a bolt word after the vehicle model); then the
player state pointer 0xfc, in use 0x100, linked 0x104, server flags
0x108, brush model 0x10c, mins 0x110, maxs 0x11c, contents 0x128,
absolute mins 0x12c and maxs 0x138, current origin 0x144 and angles
0x150, owner 0x15c, and the Ghoul2 instance 0x160 *(inferred: after the
owner, as the GPL source has it)*. The model index (0xa0)
and solid (0xb4) are where Elite Force's are.

### The client game's calls

Numbered 0 to 111 (0x401577 rejects any higher), through `dllEntry`'s
handler; read from its jump table (0x401fd0), each case's code and what
it calls (the renderer's through its table at 0x7ff770, in the GPL
source's `refexport_t` order). The GPL source's `cgameImport_t` has 118:
1.02a has four force feedback calls where it has ten, no
`Language_UsesSpaces`, and one call it lacks (81).

| Number | Calls | As Elite Force's |
|---|---|---|
| 0–25 | print, error, time, settings, arguments, files, commands, screen update, collision (load, models, contents, traces, marks) | the same numbers |
| 26 | `CM_SnapPVS(origin, mask)` (0x412360): the snapshot's area mask as seen from there, 32 bytes: a bit for each area not joined to the one the point is in (0x4122d0 sets the joined ones, then every word is inverted); none set if the point is in no area | — |
| 27–34 | sound: start, start local, clear and add looping, entity position, respatialize, register, background track | 26–33 |
| 35–38 | force feedback: three of one argument, one of none | 34–37 |
| 39–43 | `R_LoadWorldMap`, `R_RegisterModel`, `R_RegisterSkin`, `R_RegisterShader`, `R_RegisterShaderNoMip` | 38–42 |
| 44–48 | fonts: register, width in pixels, letters, height, draw (Raven's, as multiplayer's) | — |
| 49 | `Language_IsAsian()` (0x462900): Korean, Chinese or Japanese | — |
| 50 | `AnyLanguage_ReadCharFromString(&text, &isTrailingPunctuation)` (0x462700): the next letter, the text moved past it (a byte, but two in those three languages); whether it's one of `!?,.;:`, which a line may not start with | — |
| 51–62 | clear scene, add entity, lighting at a point, polygon, light, render, colour, picture, screenshot picture, model bounds, tag, turned picture | 43–54 |
| 63 | `R_DrawRotatePic2`: a picture turned about its centre | — |
| 64, 65 | `R_LAGoggles`, `R_Scissor` | — (55) |
| 66 | `GetGlconfig`: 84 bytes (0x401020), four text pointers then the numbers, the screen size at 56 and 60 | 56, its own layout |
| 67–73 | game state (20,100 bytes, 0x401000), snapshot number, snapshot, server command, command number, user command (28 bytes), `SetUserCmdValue` | 57–63 |
| 74 | `SetUserCmdAngles(pitch, yaw, roll)` | — |
| 75–80 | ambient sets: update, local set, parse, add entry, brush model sound; `S_GetSampleLength` | 65–70 |
| 81 | two points (0x415ba0) the engine writes out as six numbers, to record a path | — |
| 82–85 | Ghoul2: list bones, list surfaces, `HaveWeGhoul2Models`, `SetModels` | — |
| 86, 87 | light styles get and set | — |
| 88 | `R_GetBModelVerts(model, points out, normal)` (0x48ae20): of the brush model's two largest faces (each's area its first four points' two triangles, a quarter of each cross product, squared lengths summed, cut to whole numbers), the second if it faces the last scene's view more (its normal against the view's forward axis, smaller) and faces it at all (below 0), else the largest; its first four points written; the normal isn't | — |
| 89 | `R_WorldEffectCommand(text)` | — |
| 90–94 | cinematics: play, stop, run, draw, set extents | — |
| 95, 96 | `Z_Malloc(size, tag)` (0x414960, zeroed), `Z_Free` | — |
| 97–107 | the menus' parser: reset, new menu, parse int, string and float, start and end a session, `PC_ParseExt`, paint all, string init, menu info | — |
| 108 | `SP_Register(package, flags)` (0x431e40; the second argument made 1 or 12) | — |
| 109, 110 | the same case (0x401f63): text by reference, or (110) by number (0x432290: its high byte the package's number, its low byte the entry's), copied into a buffer; the length returned | — |
| 111 | `UI_GetItemText(menu, item, buffer)` | — |

The structures handed over: a snapshot (0x401120) has its flags, ping,
server time and area mask as Elite Force's, the command number at 44,
the player state at 48 (948 bytes), the entity count at 0x3e4, up to 256
entity states of 252 bytes from 0x3e8 and the server command sequence
at 0xfff4; the player state's view height is at 176 (0x2000e11c adds it
to the origin's height), Elite Force's at 168. A scene entity
(`refEntity_t`) is 168 bytes, laid out as the GPL source has it: Quake
III's 140 (a sprite's radius at 132, its rotation or a saber glow's
length at 136), then the Ghoul2 angles, the model's scale (152) and a
pointer to its `CGhoul2Info_v` (164); its kinds are model 0, sprite 2,
oriented quad 3, line 4, electricity 5, cylinder 6, beam 8, saber glow
9, portal 10 *(published)*. The view (`refdef_t`) is Quake III's, without Elite
Force's text.

## In Reluva

`SpGameHost::load(dll, jedi)` loads the DLL in Jedi Outcast's mode:
slots 0–41 answered as for the Expansion Pack, `Malloc` zeroing when
asked, the entity layout above, the export table's longer list.
`reluva-tool idtech3 sp-run <base folder> <map>` finds `jk2gamex86.dll`
beside the game's folder and runs it.

The client game's calls are answered as the table above says, Elite
Force's handling shared where the call and what it passes are the same
(`SpGameHost::cgameCall` with Elite Force's number), the snapshot and
scene entity sizes per game (`SpGameHost::CgameLayout`). Not yet
answered: the cinematics (90–94), which stop the game with their
number; the weather's `R_WorldEffectCommand` (89) is taken and does
nothing. The Ghoul2 trace type and level of detail `trace` is given
decide whether a trace meets a Ghoul2 body's triangles or only its box
([sp-ghoul2.md](sp-ghoul2.md), Collision): with the box alone, a
rocket stopped at an AT-ST's box and stayed there, neither bursting nor
flying on. A line typed (or a key's command) goes,
after the client game's turn, to the game's own console commands (`npc`,
`use`, `runscript`...; 0x436e00 from the command executor at 0x413f48)
before it's sent to the game as the player's. Ghoul2's engine slots and calls
84 and 85 are answered as [sp-ghoul2.md](sp-ghoul2.md) says, the menus'
(97–107, 111) as [sp-menus.md](sp-menus.md) says. With them, `sp-run`
on `kejim_post` runs the game's `Init`, the player joins, and the level
and (with `-cgame`) the client game run, the client game drawing a
scene each frame.

**The Play page:** the start page's Single Player runs the DLL as
Elite Force's does, from the first level: the level drawn with the light
styles the client game sets (its calls 86 and 87), brightened only in
fullscreen as multiplayer's, and the scene entities in the layout above,
Ghoul2 ones posed from the instance's records
([sp-ghoul2.md](sp-ghoul2.md)). The original's two programs keep their
own settings (`jk2config.cfg`, `jk2mpconfig.cfg`) and the same names
mean different things in them (`cg_hudFiles`: multiplayer's 0 or 1,
single player's a file), so single player's are kept apart: read from
`default.cfg` and Reluva's `reluva-sp.cfg`, swapped in while its page
runs. The player's name starts as `Kyle` (0x409965). For Jedi
Outcast the host runs the second of frames above between the player's
`ClientConnect` and `ClientBegin`. The movies, the opening crawl and the
game waiting under them are as [sp-cinematics.md](sp-cinematics.md)
says. With these, `kejim_post`'s opening plays through: the crawl, the
approach, the cutscene in the cockpit and Mon Mothma's message, then the
player in the level, which plays through to its end (`sp-run -tour`,
below). Games save and load as [sp-saves.md](sp-saves.md) says. Not
yet: a console on the page.

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a: the table's fill
  (0x4368e0–0x436cf5), the version check (0x436d58), the client game's
  set-up (0x440740) and handler (0x43eea0, 0x401570), `linkentity`
  (0x43a620). The GPL Jedi Outcast single-player source (`g_public.h`,
  `cg_public.h`, `q_shared.h`) as a guide to the tables' order and the
  structures, *(published)* for the Ghoul2 slots' names.
- **How it was checked:** `sp-run` on `kejim_post` loads the DLL, its
  version is 6, the game's `Init` runs, the player joins and 100 server
  frames run, the client game drawing each (a game-data step records
  it). The Play page run on the first level, on screen: its opening to
  the player's first steps. A wrong `CM_SnapPVS` (a bit per
  cluster, overrunning the 32 bytes into the snapshot's player state)
  showed as the client game reading client -1. Call 50 taken for
  `Language_UsesSpaces` left the subtitles' letters unread, the client
  game breaking lines past its buffer into its copy of the engine's
  table. Without the second of frames, the cutscene's script found its
  actors before they'd spawned and waited for ever.
- **Playing a level through:** `sp-run <base> kejim_post -tour 100000
  -spare jan -spare cinematic1_jan -spare cinematic1_kyle -set
  helpUsObi 1` plays the level with cheats from 100 s (the opening
  over): every trigger touched in turn, fighting the nearest NPCs
  between them with every direct-fire weapon, the saber included, to
  the level change (`maptransition kejim_base`). It found the brush
  models' contents (a shot at a breakable took it for a lightsaber,
  `CONTENTS_LIGHTSABER`, and the game crashed on its missing wielder)
  and call 88. `kejim_base -tour 5000 -spare cinematic3` plays the
  second level to `maptransition artus_mine`, its ending cinematic
  (Mon Mothma's) included. `-next` then starts the level changed to as
  the Play page does, with the player stored as `maptransition` stores
  them: from `kejim_post`, `kejim_base` receives the same health,
  armour, weapons and ammunition, the inventory's two keys emptied.
  `artus_mine`'s way out waits on its generator room's squad (a
  `target_counter` of seven deaths) switching on a trigger that's set
  off only facing its angle with use held (spawn flags 2 and 4), as the
  tram's is with use; the tour holds use (button 5) facing each
  trigger's angle, and `sp-run -replay tests/engines/idtech3/data/routes/artus_mine.txt`
  plays that way out to `maptransition artus_detention`. An entity's
  health is the game's field at 0x218 (the most it can have at 0x21c),
  which the tour reads to leave corpses alone. `artus_detention -tour
  16000` plays to `maptransition artus_topside` with nothing more.
  `artus_topside`'s AT-STs found the Ghoul2 collision: a rocket stopped
  at an AT-ST's box and stayed there. With it, the rockets meet the
  AT-ST and the game knows which part they hit. The
  AT-STs take damage only from heavy weapons (`SET_DMG_BY_HEAVY_WEAP_ONLY`
  in their scripts), which by the GPL source *(published)* are only the
  emplaced guns' and AT-STs' own shots, not rockets. The ion cannons
  are the same, and shielded and invincible until the shield trigger
  (use, facing south) runs `shields_down`. On an emplaced gun the
  player's view aims it (`sp-run`'s `@aim name` turns the commands'
  angles to an entity, from the player state's `delta_angles` at 0x48);
  a held use takes the player off and on again at once (1.02a's
  `RunEmplacedWeapon`, 0x2007dc00 in `jk2gamex86.dll`, which also takes
  a step back or a jump), so one press leaves. Which gun reaches a
  target, `reluva-tool idtech3 bsp-trace` shows (the map's solid parts
  along a line): gun C's line to the AT-ST guarding the bay meets the
  wall it stands on, gun B's is clear; and an AT-ST's middle is between
  its legs, so `@aim` aims three quarters up a box. The way out ends in
  cinematic 9 (its actors brought by a trigger on the way) and Desann's
  fight, which Kyle is meant to lose: undying, his death script
  (`cinematic9_run_end`) is the level's end. `sp-run artus_topside
  -replay tests/engines/idtech3/data/routes/artus_topside.txt -set
  helpUsObi 1 -next` plays it to `maptransition valley`.
- **ns_streets:** the tour leaves out triggers that fire a
  `target_kill` or a `misc_camera`, and holds use only at triggers
  that wait for it (in the original's order, holding it at kejim_post's
  squad trigger also set off the security camera's over it, whose view
  holds the player). The level locks weapons and the Force until the bar's
  conversation (`bartender_talk`); before it, a push only has Kyle say
  he doesn't want trouble yet. Its way out is the scrap car: standing
  on it starts it, and two switches (`func_static`s that a push uses)
  must be pushed while it waits, at c2 and then c5, each for three
  seconds; its last branch carries the player through `last_doors` to
  the level change. The tunnel there is inside an electrical
  `target_kill`'s trigger, but a rider who doesn't move doesn't touch
  it: the game sweeps a player's own movement for triggers, not a
  mover's push (the GPL source's `G_TouchTriggersLerped`, read as a
  guide). `sp-run`'s `@aim x y z` aims at the switches, `@until +ms`
  times the route from the tour's end, and `-tour` with `-replay` plays
  the route in place of the tour's level change: `sp-run ns_streets
  -tour 45000 -spare kyle -replay
  tests/engines/idtech3/data/routes/ns_streets.txt -set helpUsObi 1
  -next` plays it to `maptransition ns_hideout`.
- **ns_hideout:** the player starts inside an electrical
  `target_kill`'s trigger and comes to no harm standing still, as
  above; the tour keeps out of such a trigger's box, as it does a
  `trigger_hurt`'s. `sp-run ns_hideout -tour 5000 -set helpUsObi 1
  -next` plays it to `maptransition ns_starpad`.
- **ns_starpad:** a chain of scripted jobs. Lando rides the elevator up
  with the player and must be done talking before the hangar attack
  starts, or his talk cuts the attack's script short; the squads that
  attack then send him on to the Lady Luck as they die (their death
  targets count up `lando_path`). The roof doors want four use triggers
  a side, then the door's; the fuel lines two panels set to a code (the
  route tries every pair, and the right one locks them); then the
  cockpit, Reelo's arrival, and the ship's gun, a `misc_panel_turret`.
  It aims at its own facing plus the command's angles since it was
  taken, not the player's view (the GPL source's
  `panel_turret_think`, read as a guide); Reelo and his eight thugs must
  die within a minute, and the closing cinematics change the level.
  Three things in `sp-run` came of it: `@nearest` takes a side the
  player fits in with a line to the target (given the map: one side of
  thug13 is inside a wall), and a spot over a `trigger_hurt` counts as
  over a pit only with nothing solid between (the whole landing pad is
  over a drop); `@aimfrom x y z yaw pitch` measures `@aim` from a turret
  and turns from its facing; `@where name` lists any entity of that
  name with its server flags and frame (how the panels were seen to
  take presses: not the first after a `setviewpos`). Two hidden
  cinematic actors that can't be hurt are spared by name, or the tour
  fights them. `sp-run ns_starpad -tour 5000 -spare lando -spare
  cinematic20_kyle -spare cinematic20_lando -replay
  tests/engines/idtech3/data/routes/ns_starpad.txt -set helpUsObi 1
  -next` plays it to `maptransition bespin_undercity`.
- **bespin_undercity:** opening the droid's door wakes an R5, whose
  death fails the mission (it's spared). It drives into the exit lift,
  presses the panel, backs up and switches on the lift's trigger; the
  player stepping in ends the level. A teleport onto the droid kills
  the player, so the route stands beside it. `sp-run bespin_undercity
  -tour 5000 -spare droid -replay
  tests/engines/idtech3/data/routes/bespin_undercity.txt -set helpUsObi
  1 -next` plays it to `maptransition bespin_streets`.
- **bespin_streets:** `sp-run bespin_streets -tour 5000 -set helpUsObi 1
  -next` plays it to `maptransition bespin_platform`, nothing new
  needed.
- **bespin_platform:** the console that unlocks the platform doors also
  shows them through a `misc_camera` (its target names both), so the
  tour leaves it out; in the camera's view the player does nothing until
  pressing use again. The unlocking switches on the trigger for
  cinematic 22, at whose end Tavion comes. She blocks the saber and
  blaster bolts but not rockets or the disruptor, and her death script
  is cinematic 23, the level's end. The game's dismemberment called two
  new Ghoul2 slots ([sp-ghoul2.md](sp-ghoul2.md), Cutting off a limb).
  `sp-run bespin_platform -tour 5000 -replay
  tests/engines/idtech3/data/routes/bespin_platform.txt -set helpUsObi 1
  -next` plays it to `maptransition cairn_bay`.
- **cairn_bay:** the opening runs to 55 s, and the tour starts after it;
  Luke fights alongside and his death fails the mission, so he's
  spared. One trigger out in the bay (`run_remove`'s) kills the player
  at once, through god mode, a fifth of a second after a teleport there
  (what by isn't worked out); the tour leaves it out. The exit trigger is
  in a lift shaft whose bottom kills: the lift goes up and down in turn,
  and the exit is switched on only for the 4.3 s it waits below.
  `sp-run`'s `@onto name [+height]` stands the player on an entity, or
  that high above its bottom (inside the lift's car), and the route does
  that every half second for a round. `sp-run cairn_bay -tour 56000
  -spare luke -spare cinematic24_luke -spare cinematic25_luke -spare
  run_remove -replay tests/engines/idtech3/data/routes/cairn_bay.txt
  -set helpUsObi 1 -next` plays it to `maptransition cairn_assembly`.
- **cairn_assembly:** `sp-run cairn_assembly -tour 5000 -set helpUsObi 1
  -next` plays it to `maptransition cairn_reactor`, nothing new needed.
- **cairn_reactor:** `sp-run cairn_reactor -tour 5000 -set helpUsObi 1
  -next` plays it to `maptransition cairn_dock1`, nothing new needed.
- **cairn_dock1:** a stealth level. A guard who sees Kyle sends an
  officer running to an alarm button; the alarm brings the capture
  scene (`imprisoned`), which kills Kyle and fails the mission, so the
  tour's open fighting doesn't get through. With the game's `notarget`
  cheat (`-cmd notarget`) no guard sees him, and `sp-run cairn_dock1
  -tour 5000 -cmd notarget -spare cairn_vertical -set helpUsObi 1 -next`
  plays it to `maptransition doom_comm` (cinematic 26 and the movie
  `jk07` on the way); `-spare cairn_vertical` leaves out a lift's
  trigger where the player was crushed.
- **doom_comm:** `sp-run doom_comm -tour 5000 -set helpUsObi 1 -next`
  plays it to `maptransition doom_detention`, nothing new needed.
- **doom_detention:** `sp-run doom_detention -tour 5000 -set helpUsObi 1
  -next` plays it to `maptransition doom_shields`, nothing new needed.
- **doom_shields:** `sp-run doom_shields -tour 5000 -set helpUsObi 1
  -next` plays it to `maptransition yavin_swamp` (the movie `jk09` on
  the way), nothing new needed.
- **yavin_swamp:** `sp-run yavin_swamp -tour 5000 -set helpUsObi 1
  -next` plays it to `maptransition yavin_canyon`, nothing new needed. Things found on the
  way: a spot over an electrified floor (a `trigger_hurt` whose damage
  isn't stopped by god mode) kills a player dropped there, so the tour
  now counts a spot over a hurt with nothing solid between as in it, and
  keeps a visit's spot out of a door's or platform's box; the game's
  `npc kill <name>` also removes every unused NPC spawner (48 here,
  among them the ending's actors), so it's no use. `sp-run`'s
  `-dumproute file` writes the tour's route out, to replay part of it;
  `-cmd text` (a console command, or a route's `@` line) follows the
  tour's cheats, and `-before ms` runs those before the tour starts.
- **The entities in a box** (`EntitiesInBox`, 0x43ab30, walking the
  sectors at 0x43aa20): each sector's list newest first (`LinkEntity`
  puts an entity at its head, 0x43a9fa), then the side below the split
  if the box reaches it, then the side above. The game handles
  overlapping triggers, and what a move meets at once, in that order;
  Reluva's server world gives them the same.