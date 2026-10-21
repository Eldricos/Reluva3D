# The id Tech 3 engine

Engine id `idtech3`, in `src/engines/idtech3/`. First game: Star Trek:
Voyager - Elite Force (`stvef`).

## Why one engine for a family of games

Like ScummVM's SCUMM engine, which runs many games, this engine is meant
for every game built on the Quake III Arena engine (Elite Force, Quake III
Arena, Return to Castle Wolfenstein, Jedi Outcast and Jedi Academy,
Soldier of Fortune II...). What they share (pk3 search paths, shader
scripts, BSP maps, MD3 models) is written once here. What differs is keyed
off a `GameInfo` entry in `games.cpp`. Game-specific code will go in
subfolders (`src/engines/idtech3/eliteforce/`) when there is some.

Code that isn't specific to id Tech 3 goes in `src/common/` so any engine
can use it: zip archives, the `Archive`/`SearchSet` file system, image
decoding, audio. The engine never touches SDL; it uses `System`, like
every engine.

## Where it stands

| Part | State |
|---|---|
| Detection | Elite Force: one Windows release by pak0.pk3's hash; other releases by file names, shown as "unknown release" |
| File system | done: `baseEF` loose files over its paks, later paks first (`filesystem.cpp`, [pk3.md](../formats/idtech3/pk3.md)) |
| Text files | done: tokenizer ([text.md](../formats/idtech3/text.md)) and entity lump |
| Shader scripts | every Quake III directive parsed into typed fields, unknown ones listed (`reluva-tool shader-check`) ([shader.md](../formats/idtech3/shader.md)) |
| Maps | every BSP lump read and validated; all 67 retail maps load ([bsp.md](../formats/idtech3/bsp.md)) |
| Images | TGA, JPEG, PNG (`common/image.cpp`) |
| Viewer | World (3D), Maps (wireframe), Textures and Files pages |
| Renderer | every shader stage with its blend, depth, alpha test, rgbGen/alphaGen, tcGen/tcMod and animMap; deformVertexes wave/bulge/move; sort order; sky boxes and cloud layers; fog volumes (by camera distance); the original's implicit shaders and light scaling ([shader.md](../formats/idtech3/shader.md)). Not yet: autosprites, portals, dynamic lights, visibility culling |
| Models, sound, game code | not started |

## Next steps

1. **Check against the retail data.** Done for maps (all 67 load) and
   detection (one release). Still to do: write down any Elite
   Force-specific shader directives or entity keys.
2. **3D rendering in `System`.** Done: `gfx::Renderer3D` in
   `src/common/gfx3d.h` (textures, static meshes, draw calls with blend,
   depth, cull and alpha-test state, a 2D overlay), reached through
   `System::init3D`. The SDL3 backend implements it with SDL's GPU API
   (`src/backends/sdl3/sdl_gpu.cpp`) on the same GPU device as SDL's
   renderer, which the launcher keeps using. Shaders: see
   `src/backends/sdl3/shaders/build.sh`. Vulkan is tested: Mesa's software driver on Linux, and an
   NVIDIA RTX 2080 on Windows (Visual Studio 2022 build), where retail maps
   run at the monitor's 144 Hz with about 1.2 ms of CPU time per frame.
   The Metal path
   (macOS) is generated but untested.
3. **World renderer**, continued: autosprite deforms, portals and
   mirrors, sorting translucent surfaces by distance, fog by depth inside
   the volume, then visibility culling with the BSP tree and vis data.
   Check the look against the original game, and run `reluva-tool
   shader-check` on it for directives Elite Force adds.
4. **Models**: MD3 (to confirm which model formats Elite Force uses for
   its characters and weapons).
5. **Sound**: WAV effects and the music through `common/audio`.
6. **Game code**: see below.

## Game code: open questions

The renderer and file formats are the shared, well-documented part. The
game logic is the hard part, and there are several possible routes. Each
one has to be checked before anything is written:

- **Licences.** Reluva's own code is all new. The GPL Quake III Arena
  source (id's 2005 release, ioquake3) may be read as documentation of the
  formats the games share, never copied.
- **Raven's Elite Force game source is off-limits.** The single-player
  game source Raven published in 2002 (the "STEF Game Source", expansion
  version) comes under Raven's *Limited Use Software License Agreement*,
  not the GPL. It allows only modifications that run with the retail game,
  and forbids derivative works, reverse engineering and translation. Don't
  copy it, and don't read it to write Reluva code; treat it like the leaked
  source CONTRIBUTING.md warns about. The same goes for the multiplayer SDK
  unless it turns out to carry a GPL-compatible licence. It also wouldn't
  help with the shared formats: by its own ReadMe it holds only the game
  DLLs' code, not the executable's renderer or map loading.
- **Holomatch (multiplayer)**: `pak0.pk3` holds `vm/qagame.qvm` (server
  game logic), `vm/cgame.qvm` (client: HUD, effects, prediction) and
  `vm/ui.qvm` (menus): game logic as Quake III VM bytecode. So the original
  logic can run in a QVM interpreter, with the engine providing the
  system calls it makes, the way ScummVM runs games' own scripts. The
  bytecode stays in the user's copy; Reluva only runs it.
  - The QVM file format and instruction set are the same as Quake III's,
    documented in the GPL source and community write-ups, so the
    interpreter is new code written from that documentation.
  - The system calls (numbers, arguments and meanings) are Elite Force's
    own. They have to be worked out from the bytecode in an owned copy
    (reverse engineering for interoperability, allowed by CONTRIBUTING.md
    where local law permits) and from Quake III's documented ones where
    they match, never from Raven's SDK.
  - Probably only Holomatch uses the QVMs; single player has its DLLs. To
    confirm.
- **Single player**: its logic ships as native x86 code
  (`efgamex86.dll`, with the menus in `efuix86.dll`), which a portable
  engine can't load. It would have to be reimplemented from
  observed behaviour and the map data (entities, scripts), following
  CONTRIBUTING.md.
