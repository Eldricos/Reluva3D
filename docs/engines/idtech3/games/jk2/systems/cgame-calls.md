# Jedi Outcast: the multiplayer client game's system calls

> **Status:** partial · **Code:** `src/engines/idtech3/eliteforce/cgame_host.cpp` (shared with Elite Force), `src/engines/idtech3/jk2/cgame_calls.cpp` · **Checked against:** the code only (1.02a's `cgame.qvm`, run in Reluva) · **Confidence:** from the code unless marked

## Overview

The player's side of multiplayer (the view, the HUD, effects, prediction)
is `vm/cgame.qvm` in `assets0.pk3`: 216,795 instructions in 1,097
functions, 235 calls through function pointers. It reaches the engine
through numbered calls in the order of the GPL Jedi Outcast source's
multiplayer `cg_public.h`, read as documentation and checked against
what 1.02a's module passes.

Most of its calls are Quake III's, numbered differently from Elite
Force's from 16 on. Reluva runs both games' client games with one host
(`eliteforce::CgameHost`): Jedi Outcast's numbers for the shared calls
are translated to Elite Force's (`jk2SharedCall`), its own answered by
`jk2Call`. Its commands are Quake III's (0 init, 1 shutdown, 2 console
command, 3 draw the frame), then Raven's calls into it for the effects
system and Ghoul2 (4 to 24).

## How it works

### Its structures

| Structure | Bytes | Layout |
|---|---|---|
| `gameState_t` | 21,604 | an offset each for 1,400 configstrings (the module refuses an index of 1,400 or more), 16,000 bytes of text, the bytes used |
| `snapshot_t` | 77,200 | Quake III's: flags, ping, server time, the area mask, the player state (1,368), the entity count, 256 entity states (296 each), the command count and sequence |
| `glconfig_t` | 11,332 | the menus': three 1,024-byte strings and 8,192 of extensions, then Quake III's 17 words |
| `refEntity_t` | 212 | Raven's (`jk2/ref_entity.h`), its Ghoul2 instance in the last word (208); its effects' sizes in the common part ([efx.md](../../../formats/efx.md)); the saber's blade length at 180 |
| `trace_t`, `usercmd_t` | 1,080, 28 | as the game module's ([game-calls.md](game-calls.md)) |

### Calls shared with Elite Force

| Jedi Outcast's | Elite Force's | |
|---|---|---|
| 0–15 | 0–15 | print ... add command |
| 17–30 | 16–27 | client command, update screen, the collision calls (each capsule call as the box's, its mover or temporary model a capsule; [bsp.md](../../../formats/bsp.md), Collision), mark fragments |
| 32–41 | 28–35 | sounds (the "real" looping sound as the plain one) |
| 42–46 | 36–39, 57 | world map, models, skins, shaders |
| 110 | 107 | floor |
| 200–211 | 40–48 | the scene, colour, pictures, model bounds, tags (the additive light as a light) |
| 219–226, 232 | 49–56, 58 | glconfig, game state, snapshots, server commands, user commands, memory; `SetUserCmdValue`'s third and fourth arguments are the force power and item selected, which the player's commands carry |

### Its own

| # | Call | What Reluva does |
|---|---|---|
| 16 | `RemoveCommand` | as it says |
| 31, 38 | `S_MuteSound`, `S_StopLoopingSound` | (entity, channel): the first sound the entity plays on that channel (1 and up) silenced; (entity): its looping sound stopped at once |
| 47–52 | the text calls | as the menus' ([syscalls.md](syscalls.md)): the HUD's numbers and names |
| 111 | ceil | |
| 203 | `R_AddPolysToScene` | each polygon added |
| 204 | `R_LightForPoint` | the level's light grid at the point (ambient, directed and its direction, with the light styles as set), without the minimum models get; 0 without a grid |
| 212, 213 | `R_DrawRotatePic`, `R_DrawRotatePic2` | a turned picture |
| 214 | `R_RemapShader` | (old, new, time offset as text): from then on, what's drawn with the old shader is drawn with the new, its time that many seconds behind; new the same as old undoes it, a new level forgets them (`ShaderCache::remap`) |
| 215, 216 | `R_GetLightStyle`, `R_SetLightStyle` | a style's colour (RGBA, red the word's lowest byte) read and set; the level's lightmaps, vertices and light grid follow ([rbsp.md](../../../formats/rbsp.md)) |
| 217 | `R_GetBModelVerts` | (inline model, four corners out, normal): as the GPL renderer's: of the model's two largest faces, the one facing away from the last view's forward axis, else the largest; its first four corners; the normal isn't written. jk2mp.exe calls its renderer for it through a table filled as it runs, so 1.02a's own version isn't read |
| 218 | `FX_AddLine` | a line added to the effects system |
| 227 | `SetClientForceAngle` | (time, angles): jk2mp.exe keeps the time and angles (0x401200); the next command made before that time takes the yaw, less the player state's turn (`delta_angles[1]`, at 72), once (0x407700) |
| 228 | `SetClientTurnExtent` | (add, sub, time) (0x401230): unless a yaw was forced, the next command made before that time can't turn across either extent (each less the player state's turn): the yaw goes back to the last command's |
| 229 | `OpenUIMenu` | the page opens that menu over the match (6, the force powers, when the game sends `nfr` to a new player) |
| 233–236 | the key calls | the catcher kept |
| 237–243 | the script calls | as the menus': the HUD is a menu script |
| 244–246 | stop music, real time, snap vector | |
| 247–251 | cinematics | none plays |
| 252 | `GetEntityToken` | the level's entity text, word by word |
| 253 | `R_InPVS` | from the level's visibility |
| 254 | `FX_RegisterEffect` | an effect file read and numbered ([efx.md](../../../formats/efx.md)) |
| 255–260 | `FX_PlaySimpleEffect`, `FX_PlayEffect`, `FX_PlayEntityEffect`, and the three by number | played at the place, facing up, a way, or with three axes; an entity's effect made later at where the client game says the entity is (its `CG_GET_LERP_ORIGIN`) |
| 261 | `FX_PlayBoltedEffectID` | not yet (1.02a doesn't call it) |
| 262 | `FX_AddScheduledEffects` | the effects run for the frame and added to the scene as Raven's renderer takes them; a decal's mark made by the client game (its `CG_IMPACT_MARK`, given through the shared buffer) |
| 263, 264 | `FX_InitSystem`, `FX_FreeSystem` | everything dropped |
| 265 | `FX_AdjustTime` | the effects' clock and the eye |
| 266–269 | `FX_AddPoly`, `FX_AddBezier`, `FX_AddPrimitive` (a saber's trail), `FX_AddSprite` | added to the effects system, from the structures they're given (180, 136, 348 and 72 bytes) |
| 270–272 | the string packages | as the menus' |
| 273–277 | ROFF: clean, update entities, cache, play, purge | as the game module's, on the client game's own entities, through its exports for their origins, angles and trajectories; on the server's clock (the latest snapshot's time), as the original's; one system with the game module's on the Play page's own server ([roff.md](../../../formats/roff.md)) |
| 278–299 | Ghoul2 | instances posed by their animations and angle overrides, bolts from bones and tag surfaces, models hung from bolts, surfaces on and off ([ghoul2.md](../../../formats/ghoul2.md), Animation) |
| 300 | `SetSharedBuffer` | kept: where the engine's effects system and the module trade data |

### Patches

1.03's and 1.04's `cgame.qvm` (the same calls in both; `jk2mp.exe` 1.03's
jump table, from 0x4017a0):

- **The text calls**: 52 `Language_IsAsian` and 53
  `Language_UsesSpaces` are new, and `AnyLanguage_ReadCharFromString` moves
  to 54 with the menus' three arguments ([syscalls.md](syscalls.md),
  Patches).
- **Ghoul2**: 285 is new, `GetBoltMatrix_NoRecNoRot` (the name *(inferred)*):
  285's bolt with two of the engine's flags set (0x1b1d23c and
  0x1b1d26c), the first of which 284 sets too; 1.02a's 285 and 286 to 300
  move to 286 and 288 to 302; 287 is new, `G2API_CollisionDetect`
  (below).
- **`G2API_CollisionDetect`** (records, instance, angles, origin, time,
  entity, start, end, scale, trace flags, detail, radius) traces a line
  through an instance's posed triangles (the engine's 0x4abf60) and fills
  up to 16 records of 64 bytes, the entity at 4 (-1 an empty one), sorted
  by distance, the float at 0.
- **The snapshot** is 77,212 bytes, the player state 1,380
  ([network.md](network.md), Patches).

Reluva answers 285 as 284, the bolt's turn kept
([../KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md)), 287 with the triangles Reluva traces saber hits with,
and the rest by their 1.02a numbers.

## Sources and checks

- **Where the facts come from:** 1.02a's `cgame.qvm` (its calls with
  `qvm-info`, its whole-struct copies with `qvm-dis -blockcopies`, its
  configstring bound at instruction 4625), and the GPL Jedi Outcast
  source's multiplayer `cg_public.h`, `q_shared.h` and `tr_types.h`,
  read as documentation; the code is new.
- **How it was checked:** `game-run -cgame` on `ffa_bespin`: the client
  game starts (its effects system set up, the level loaded, 63 models,
  289 shaders, 179 sounds, the music), and each frame draws its HUD
  (health, armour, ammunition and force meters, weapon icons, the
  crosshair, text in its fonts) and a scene from the player's eye: the
  level's items, the player's weapon, and as it fires the blaster's
  effects (its bolt, muzzle flash, sparks, smoke and burn marks). A
  game-files step (`jk2-game`) records it.

### Open questions

- Players are posed (a player's entity carries its instance at 208) and
  drawn; seen in third person on the Play page.
- Starting takes about 340 million instructions (the effects system and
  the models it loads): to look at for speed.
