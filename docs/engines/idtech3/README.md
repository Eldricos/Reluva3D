# The id Tech 3 engine

Engine id `idtech3`, in `src/engines/idtech3/`. First game: Star Trek:
Voyager - Elite Force (`stvef`).

## Games

| Game | Id | Docs |
|---|---|---|
| Star Trek: Voyager - Elite Force | `stvef` | [games/stvef/](games/stvef/README.md): using it, its map entities, its game code's interfaces, its network protocol, and its known differences, unconfirmed items, testing and how it was worked out |

## Where it stands

| Part | State |
|---|---|
| Detection | Elite Force's CD, its 1.1 and 1.2 patch levels and the Expansion Pack, by their files' hashes; other releases as "unknown release" |
| Import | From the CD, an install or a copy; the 1.2 patch from its installer, and the Expansion Pack from its CD or an install |
| Mods | A folder beside `baseEF`, picked in the launcher or in the game's own Mods screens |
| File system | done: `baseEF` loose files over its paks, later paks first (`filesystem.cpp`, [pk3.md](formats/pk3.md)) |
| Text files | done: tokenizer ([text.md](formats/text.md)) and entity lump |
| Shader scripts | every Quake III directive parsed into typed fields, unknown ones listed (`reluva-tool idtech3 shader-check`) ([shader.md](formats/shader.md)) |
| Maps | every BSP lump read and validated; all 67 retail maps load ([bsp.md](formats/bsp.md)) |
| Images | TGA, JPEG, PNG (`image/image.cpp`) |
| Models | MD3 and Elite Force's MDR, players put together from their parts, lit by the light grid ([md3.md](formats/md3.md), [mdr.md](formats/mdr.md)) |
| Game code | QVM interpreter done ([qvm.md](formats/qvm.md)); Elite Force's menus, game and client game run on it |
| Viewer | Menu, Play, World, Models, Maps, Textures and Files pages |
| Renderer | Every shader stage, sky boxes and cloud layers, curved surfaces, fog, dynamic lights, models' levels of detail, and visibility as the original's renderer has it |
| Sound | sounds by name, decoded once (`sounds.cpp`), mixed by `src/audio`'s mixer; the menus' sounds; in the Play page sounds placed in the world, looping sounds and music (`sound_system.cpp`) |

How each part works with Elite Force, in full: [games/stvef/](games/stvef/README.md#where-it-stands).

## This folder

| Doc | What's in it |
|---|---|
| This page | The engine: why one engine for a family of games, its code, where it stands; what's next is in [PLAN.md](PLAN.md) |
| [formats/](formats/) | id Tech 3's file formats, the same for every game on it, each fact with its source |
| [games/](games/) | A folder per game, for what's its own (above) |
| [PLAN.md](PLAN.md) | The plan: next steps, and the open questions about the game code |

A page goes in a game's folder when it was worked out from that game, and
moves up to this folder once another game is found to share it.

## Code

Shared code it uses (the x86 interpreter, Bink videos, Wise installers) is
described in [docs/common/](../../common/).

### Why one engine for a family of games

Like ScummVM's SCUMM engine, which runs many games, this engine is meant
for every game built on the Quake III Arena engine (Elite Force, Quake III
Arena, Return to Castle Wolfenstein, Jedi Outcast and Jedi Academy,
Soldier of Fortune II...). What they share (pk3 search paths, shader
scripts, BSP maps, MD3 models) is written once here. What differs is keyed
off a game's `GameInfo` (`games.h`: whose game code it has, its refEntity
layout, buttons, default player, single-player DLLs, cinematics), and
game-specific code goes in a folder of its own.

Where things are:

| Folder | What |
|---|---|
| `idtech3/` | The base: formats (pk3 search paths, BSP, MD3/MDR, shaders), collision, light grid, the QVM, cvars, bindings, sound, the GPU world renderer, the bot library |
| `idtech3/client/` | What a client game hands the engine (`scene.h`: Scene, View, Picture), drawing it (`scene_drawer`, through a per-game `RefEntityLayout`), and the player's input (`player_input`) |
| `idtech3/server/` | The server's world (`server_world`): linked entities in a tree, traces, point contents, boxes, areas, as Quake III's server keeps them; each game's host only says how to read an entity's shape |
| `idtech3/pages/` | The pages, a header each, with the shared context and settings in `page.h` |
| `idtech3/raven/` | What Raven's games share and Quake III doesn't: single-player save chunks, ambient sound sets |
| `idtech3/eliteforce/` | Elite Force's own: its `GameInfo` (`game_info.cpp`), refEntity layout, entity and network field layouts, and the hosts that run its QVMs and DLLs with their call numbers |
| `idtech3/wire/` | Network messages and channels (Elite Force's protocol 24 for now) |

A later game in the family (Jedi Outcast in `jk2/`, say) adds its own folder
with its hosts and a `GameInfo`, using the base, `client/`, `server/` and,
for Raven's, `raven/`. Elite Force II runs on Ritual's engine, not this one,
and would be an engine of its own.

Code that isn't specific to id Tech 3 goes in `src/common/` so any engine
can use it: zip archives, the `Archive`/`SearchSet` file system, image
decoding, audio. The engine never touches SDL; it uses `System`, like
every engine.

## Terms

Quake III engine words these pages use, as they apply to every game on
the engine:

- **QVM:** a program compiled for Quake III's virtual machine
  (`vm/*.qvm`), which the engine interprets: a game's code, shipped in its
  paks ([qvm.md](formats/qvm.md)).
- **qagame, cgame and ui:** the three parts of a game's code. qagame is
  the server's game (rules, entities, players), cgame the client game (the
  view, HUD and effects, built from what the server sends), ui the menus.
  Each is a QVM, or a DLL where a game ships native code.
- **System call:** a numbered call from a game's code into the engine
  (to read a file, play a sound, trace a line); each game numbers its own.
- **cvar:** a console variable, a named setting (`r_mode`, `sv_pure`) set
  from the console, a config file or the menus.
- **configstring:** one of the numbered strings the server keeps and sends
  to every client: the map, the models and sounds in use, each player's
  info.
- **refEntity:** one thing the client game hands the renderer to draw in a
  frame: a model, a sprite, a beam (Quake III's `refEntity_t`). Its layout
  differs between games.
- **netchan:** the network channel, which numbers each connection's
  datagrams, splits large messages and puts them together again.
- **pure server:** a server (`sv_pure 1`) that tells clients its paks'
  checksums, so they use only those paks ([pk3.md](formats/pk3.md),
  "Checksums").
- **botlib:** the bot library, the engine's code for bots: their movement,
  routes and goals, and their chat. Game code calls it through system
  calls.
- **AAS:** the Area Awareness System, the bots' map of a level
  (`maps/<name>.aas`), made from the BSP: the areas a player can be in and
  the ways between them ([aas.md](formats/aas.md)).
