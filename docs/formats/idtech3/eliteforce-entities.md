# Elite Force: map entities

What the retail maps' entity lumps hold ([bsp.md](bsp.md) for the
format), from `reluva-tool entity-survey` run on the retail `baseEF`
(patched to 1.2) in October 2026: **67 maps, 44,849 entities, 210
classes, 106 keys**, all maps read without error. The survey's own output
lists every class with its keys, counts and common values; this page
sorts them into what's Quake III's and what Elite Force adds.

Where a meaning is given, its source is marked:

- **Q3**: as Quake III's (its GPL source, read for facts);
- **JK2**: as the same name in Jedi Outcast's GPL source, Raven's next
  game on this engine, which Elite Force's single player prefigures;
- **seen**: inferred from the name and the values the maps give it;
- **?**: not known.

Raven's own Elite Force game source isn't used (see
[../../engines/idtech3.md](../../engines/idtech3.md)).

## Who reads them

Nearly every key is read by the game code: `vm/qagame.qvm` in Holomatch,
`efgamex86.dll` in single player. Reluva runs that code unchanged, so
these keys work without Reluva knowing them. Reluva's engine reads only:

- worldspawn's `gridsize`, for the light grid. **No retail map sets it**,
  so every map uses the default 64 × 64 × 128;
- `model` (`*N`) and `origin`, `angle`/`angles` where its own pages place
  things (the World page);
- nothing else. None of Quake III's compile-time remapping keys
  (`_remap`, `_celshader`, `_lightmapscale`) occur.

The single-player DLL names every non-item class the single-player maps
use (its strings). Item and weapon classes aren't among its strings:
single player reads its items from data, apparently, so which item
names it knows can't be checked that way. The Holomatch game code wasn't
checked (it isn't in the folder surveyed).

## Compiler-only entities

Used by the map compiler and dropped by the game, as in Quake III:

- **`light`** (19,421, every map). `light` (intensity, 19,329), `_color`
  (15,100), `scale` (4,198; Elite Force's q3map scales the light by it,
  **seen**: values 0.2 to 4), `radius` (236; **?**), `_cone` (34; spotlight
  cone, **Q3**-style compiler key), `target` (spotlights, **Q3**), `angle`
  (607; values `-1`/`-2` are the editor's up/down), `color` (168; a second
  spelling of the colour, **seen**), `groupname` (below), `spawnflags` 32
  (once, **?**).
- **`func_group`**: none survive compilation (it's merged into the world).

## Quake III's, used as in Quake III

Classes: `worldspawn`, `info_player_start`, `info_player_deathmatch`,
`info_player_intermission`, `info_null`, `info_notnull`, `func_door`,
`func_plat`, `func_static`, `func_wall`, `func_train`, `func_bobbing`,
`path_corner`, `trigger_multiple`, `trigger_hurt`, `trigger_teleport`,
`trigger_push`, `target_speaker`, `target_relay`, `target_delay`,
`target_give`, `target_print`, `target_position`, `target_location`,
`target_push`, `target_teleporter`, `misc_model`, `misc_teleporter_dest`,
`misc_portal_surface`, `misc_portal_camera`, `team_ctf_redplayer`,
`team_ctf_blueplayer`, `team_ctf_redspawn`, `team_ctf_bluespawn`,
`team_ctf_redflag`, `team_ctf_blueflag`.

Keys: `classname`, `origin`, `angle`, `angles`, `model`, `model2`,
`spawnflags`, `target`, `targetname`, `team`, `message`, `music`, `noise`,
`speed`, `wait`, `random`, `delay`, `lip`, `height`, `phase`, `dmg`,
`health`, `count`, `radius`, `roll`, `fov` (on portal cameras), `light`,
`_color`, `color`, `gravity`, `sounds`.

Differences seen in how Elite Force's maps use them:

- **`misc_model`** carries `autobound` (always `1`), `mins`, `maxs` and
  spawnflags 1 and 4 in 26 single-player maps, and the single-player
  DLL names the class: there it's a real entity, not only the compiler's
  (**seen**). `autobound 1` with explicit `mins`/`maxs`: the bounds the
  editor worked out from the model (**seen**).
- **`func_door`**: `soundset` (586 of 672) picks the door's sounds by
  name (`voyagerdoors`, `DNlargedoors`, `klingondoor`, `classictrekdoors`,
  `forgedoor`...), where Quake III's `sounds` is a number (still on 128,
  mostly `2`). `opentarget` / `closetarget`: fired when the door finishes
  opening / closing (**JK2**). `team` names doors that move together
  (**Q3**), here often with words (`hazard8`, `zygote`). `dmg -1` (211):
  **?** (no damage when blocked, probably).
- **`target_speaker`**: `soundset` (253) instead of a `noise` file on
  some; `soundgroup` (3) **?**.
- **`trigger_multiple`**: `soundset` (560): entering it switches the
  ambient sound set, as Jedi Outcast's `trigger_multiple`/ambient sets
  (**JK2**); see the ambient sound calls in
  [eliteforce-sp-interface.md](eliteforce-sp-interface.md).
- **`worldspawn`**: `music` (47, Quake III's), `soundset` (41: the map's
  starting ambient set, **JK2**), `spawnscript` (26: a script run as the
  level starts, below), `infostring` (46: `II_<MAP>`, **?** — probably the
  map's text for the objectives or loading screen), `gravity` (3: 250,
  500, 800; the level's gravity, **seen**), `message` (3: the level's
  title, **Q3**), `sky` (1: `junk`, ignorable), `angle`, `spawnflags` 1
  (3), `target2`, `targetname`, `wait`, `_color` (1 each; editor
  leftovers, **?**).

## Elite Force's additions

### Scripts (single player)

Elite Force's single player drives its story with scripts, as Jedi
Outcast's ICARUS does (**JK2** for the naming throughout):

- **Script hooks** (a script's path relative to `scripts/`, e.g.
  `borg1/rotate_splattamatron`): `spawnscript` (1,898), `usescript`
  (1,410), `painscript` (350), `deathscript` (59), `ffdeathscript` (90,
  killed by a friend: the crew), `awakescript` (45), `angerscript` (52),
  `attackscript` (7), `blockedscript` (6).
- **Names scripts use**: `script_targetname` (412), `npc_targetname`
  (1,278, an NPC's name).
- **Values scripts read**: `parm1` to `parm9`, `parm13`, `parm14` (up to
  209 uses each; JK2 has 16).
- **`target_scriptrunner`** (794 in 50 maps): runs its `usescript` when
  fired; `count` (times it may run), `wait`, `delay`, `parm1`–`parm4`;
  spawnflags 1, 128, 256, 512, 1024 (**?**: JK2's are 1 = run on the
  activator, 128 = inactive...).
- **`target_change_parm`** (72): sets parms on its target (`parm4`,
  `parm7`, `parm8`, `parm13`, `parm14`) (**seen**).
- **`ref_tag`** (1,271 in 45 maps): a named place and direction scripts
  refer to: `targetname`, `angles`, `fov` (672; camera shots), `groupname`
  (editor), `ownername` (3), `target` (119), `script_targetname` (17).
- **Cameras**: `misc_camera_focus`, `misc_camera_track` (1 each),
  `cameragroup` (8, on `info_notnull` and NPCs; **?**).

### Characters (NPCs, single player)

43 `npc_*` classes, one per character or species: the crew
(`npc_munro`, `npc_janeway`, `npc_tuvok`...), `npc_starfleet` /
`npc_starfleet_random` (with `npc_type`), and the enemies (`npc_borg`
445, `npc_species8472` 160, `npc_reaver`, `npc_stasis`, `npc_klingon`,
`npc_hirogen`, `npc_malon`, `npc_harvester`, `npc_scoutbot`, ...), plus
`npc_spawner` (1).

Their keys, besides the scripts above:

| Key | Uses | Meaning |
|---|---|---|
| `npc_type` | 268 | which character file to use (`npc_starfleet` and its random version) **JK2** |
| `npc_target`, `npc_target2` | 834, 14 | fired on death / a second target **JK2** |
| `paintarget`, `npc_paintarget` | 133, 2 | fired when hurt **JK2** |
| `delay` | 378 | spawn delay after being triggered **JK2** |
| `count` | 528 | how many times it can be spawned **JK2** |
| `health` | 639 (all classes) | starting health **Q3** |
| `nobasicsounds`, `nocombatsounds`, `noextrasounds` | 72, 175, 72 | silences sets of its sounds **JK2** |
| `noscavsounds` | 15 | the same for a Scavenger set **seen** |
| `playerteam` | 8 (`npc_scoutbot`) | which side it fights for **JK2** |
| `ownername` | 113 (`npc_stasis`) | **?** |
| `closetarget`, `target2` | 40 each (`npc_stasis`) | **?** |

Spawnflags use bits 1 to 1024 (`npc_borg` all eleven); their meanings
per class aren't worked out (**?**).

### Navigation (single player)

How NPCs find their way: `waypoint` (3,200), `waypoint_squadpath`
(2,958, with `leaddist` on almost all: how far ahead the squad leader
walks, **seen**), `waypoint_navgoal`, `waypoint_navgoal_1`, `_2`, `_4`,
`_8` (the number a size, **JK2**-like), `waypoint_small`, `point_combat`
(433: places to fight from, **JK2**), linked by `target` to `target4`.
`hiddenwaypoint` (157) **?**; `ownername` (57) **?**.

### Effects

`fx_*` classes (single player): `fx_spark`, `fx_steam`, `fx_smoke`,
`fx_drip`, `fx_bolt`, `fx_plasma`, `fx_elecfire`, `fx_teleporter`,
`fx_surface_explosion`, `fx_electrical_explosion`, `fx_explosion_trail`,
`fx_blow_chunks`, `fx_borg_bolt`, `fx_borg_energy_beam`, `fx_dn_beam_glow`,
`fx_crew_beam_in`, `fx_transporter_stream`, `fx_cooking_steam`,
`fx_psycho_jism`. Keys: `radius`, `damage`, `speed`, `delay`, `random`,
`duration`, `startrgba` / `finalrgba` (colour at the start / end, **seen**),
`type` (`fx_crew_beam_in`, **?**), `target` (where it points, **seen**).

### Things to use and break

- **`func_usable`** (648): a brush that toggles or runs a script when
  used (**JK2**); `model2`, `health`, `infostring`, `parm1`/`parm2`.
- **`func_breakable`** (235), **`misc_model_breakable`** (1,377): breakable
  brushes and models; `material` (debris kind, **seen**), `splashdamage` /
  `splashradius` (explosion when broken, **seen**), `health`, `team`;
  `fullname` (3) **?**.
- **`func_stasis_door`** (31): the stasis aliens' doors (**seen**).
- **Stations and pickups as models**: `misc_model_borg_ammo`,
  `misc_model_borg_health`, `misc_model_borg_crystal_station`,
  `misc_model_borg_disnode`, `misc_model_dreadnought_*`,
  `misc_model_forge_*`, `misc_model_scavenger_*`,
  `misc_model_stasis_*`, `misc_model_dn_crystal_station`,
  `misc_model_scav_crystal_station`, `misc_model_plasma_filter`,
  `object_cargo_barrel1`, `isodesium_pickup`,
  `starfleet_helmet_pickup`.
- **`misc_turret`**, **`misc_laser_arm`**, **`misc_stasis_shooter`**.
- **`infostring`** (214, `II_...`, e.g. `II_CONTROL_PANEL`): the text
  shown for the thing pointed at (**seen**; text in the string files).

### Targets and triggers

`target_activate` / `target_deactivate`, `target_counter` (`count`,
`bouncecount`), `target_random`, `target_interest` (`interest`),
`target_gravity_change` (`gravity`), `target_play_music` (`music`),
`target_level_change` (`mapname`: the next map), `target_autosave`;
`trigger_once`, `trigger_location` (`message`: the location's name),
`trigger_formation`, `trigger_visible` (`fov`), `trigger_entdist`
(`distance`, `ownername`). Meanings follow their names (**seen**);
spawnflags **?**.

### Ambient creatures (single player)

`ambient_etherian_fixits`, `ambient_etherian_fliers`,
`ambient_etherian_mine` (**seen**).

### Items and weapons

**Holomatch** maps (`hm_*`, `ctf_*`) use joined names:
`weapon_compressionrifle`, `weapon_imod`, `weapon_scavenger`,
`weapon_stasisweapon`, `weapon_grenadelauncher`,
`weapon_tetriondisruptor`, `weapon_quantumburst`, `weapon_dreadnought`;
their `ammo_*`; `item_hypo`, `item_hypo_small`, `item_armor_shard`,
`item_armor_combat`, `item_armor_body`, `item_quad`, `item_haste`,
`item_regen`, `item_enviro`, `item_flight`, `item_invis`, `item_seeker`;
`holdable_medkit`, `holdable_shield`, `holdable_transporter`,
`holdable_detpack`.

**Single-player** maps mostly use underscored names for the same weapons:
`weapon_compression_rifle`, `weapon_grenade_launcher`,
`weapon_tetrion_disruptor`, `weapon_quantum_burst`,
`weapon_scavenger_rifle`, plus `weapon_phaser`, `weapon_stasis`,
`weapon_tricorder`, `weapon_voyager_hypo`, `weapon_red_hypo`,
`weapon_blue_hypo`, `weapon_melee`, `weapon_imperial_blade`,
`weapon_klingon_blade`, `ammo_crystal_forge`. A few single-player maps
use Holomatch's names too (`weapon_imod` on borg1, `weapon_dreadnought`
on dn5).

## Editor leftovers

Keys the game ignores, by every sign: `groupname` (2,646: the editor's
grouping), `*rem1`–`*rem3`, `rem1`, `rem2` (notes), `targetname_old`,
`parm2_hunh`, `scal3` (a typo of `scale`), `chell` (33, on
`waypoint_squadpath`), `hiderange` (1), `infostrings` (1), an empty key
(7, on `light` and `func_door`), `light` on a `func_static` and a
`target_scriptrunner`, `scale` on a `target_scriptrunner`.

## Still unknown

The meanings marked **?** above, and each class's spawnflags in single
player. Each would need the code that reads it in `efgamex86.dll` (or
`qagame.qvm` for Holomatch); none matters to the engine.
