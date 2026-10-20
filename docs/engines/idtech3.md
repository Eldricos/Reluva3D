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
| Detection | Elite Force, by file names only: needs sizes and MD5s from owned copies |
| File system | done: `baseEF` loose files over its paks, later paks first (`filesystem.cpp`, [pk3.md](../formats/idtech3/pk3.md)) |
| Text files | done: tokenizer ([text.md](../formats/idtech3/text.md)) and entity lump |
| Shader scripts | parsed; maps, animMap, blendFunc, surfaceparm and skyParms interpreted ([shader.md](../formats/idtech3/shader.md)) |
| Maps | every BSP lump read and validated ([bsp.md](../formats/idtech3/bsp.md)) |
| Images | TGA, JPEG, PNG (`common/image.cpp`) |
| Viewer | Maps (wireframe), Textures and Files pages |
| Renderer | not started, see below |
| Models, sound, game code | not started |

## Next steps

1. **Check against the retail data.** Run the viewer on an owned copy:
   confirm every map loads (version 46, record sizes), add the paks'
   hashes to `games.cpp`, and write down any Elite Force-specific shader
   directives or entity keys.
2. **3D rendering in `System`.** The `System` interface only presents a
   software framebuffer today. Real-time 3D needs the GPU, so the plan is
   an optional 3D interface in `src/common/` (textures, vertex and index
   buffers, pipeline states for the blend, depth and cull modes shader
   stages ask for, and draw calls), implemented by the SDL3 backend with
   SDL's GPU API (Vulkan, Metal, Direct3D 12). Engines would still never
   include SDL, and later 3D engines get the same interface for free. The
   2D framebuffer path stays for 2D games and the viewers.
3. **World renderer** on that interface: patch tessellation, lightmaps,
   shader stages (`rgbGen`, `tcMod`...), sky, fog, then visibility culling
   with the BSP tree and vis data.
4. **Models**: MD3 (to confirm which model formats Elite Force uses for
   its characters and weapons).
5. **Sound**: WAV effects and the music through `common/audio`.
6. **Game code**: see below.

## Game code: open questions

The renderer and file formats are the shared, well-documented part. The
game logic is the hard part, and there are several possible routes. Each
one has to be checked before anything is written:

- **Licences.** Only code under a GPLv3-compatible licence can be used.
  id's 2005 GPL release of Quake III Arena (GPL version 2 or later)
  qualifies. **Check exactly what Raven released for Elite Force, and
  under what licence**, before using any of it. Game SDKs from the era
  often came under their own licences, which aren't compatible with the
  GPL. If it isn't clearly GPL-compatible, use it only as observed
  behaviour, the same as the running game.
- **Holomatch (multiplayer)**: if the retail paks hold the game logic as
  Quake III VM bytecode (`vm/*.qvm`), the original logic could run in a
  QVM interpreter, with the engine providing the system calls it makes.
  The original assets would then work unchanged, as ScummVM runs games'
  own scripts. To check: list the paks' `vm/` folder in the Files page.
- **Single player**: its logic ships as native x86 code (a DLL), which a
  portable engine can't load. It would have to be reimplemented from
  observed behaviour and the map data (entities, scripts), following
  CONTRIBUTING.md.
