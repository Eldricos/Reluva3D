# Star Trek: Deep Space Nine - The Fallen

Game id `ds9fallen` (The Collective, 2000), the first game on the
[Unreal engine](../../README.md); its code is in `src/engines/unreal/`
with the engine's.

## Releases

| Release | Recognised by | State |
|---|---|---|
| Windows, English (packages of versions 71-73) | The hashes of `System/DS9.exe` and `System/Engine.u` | Playable: every level runs; being checked against a recording of the original |
| Other releases (other patch levels, languages, Mac) | Their file names: `System/DS9.ini` or `System/DS9.exe`, with `System/Core.u` and `System/Engine.u`; shown as "unknown release" | Not tried |

## Where it stands

| Part | State |
|---|---|
| Detection | The Fallen: one Windows release by `DS9.exe` and `Engine.u`'s hashes; others by file names (`System/DS9.ini` or `System/DS9.exe`, with `Core.u` and `Engine.u`), shown as "unknown release"; any other UE1 game as unidentified |
| Packages | header, name, import and export tables, the older layouts (before versions 64 and 68) as well as later ones; only The Fallen's, versions 71-73, checked on real files ([package.md](formats/package.md)); found by name through the `.ini`'s `[Core.System] Paths` (`package_set.cpp`); imports resolved to the package that defines them |
| Properties | tagged properties of every type, state frames ([package.md](formats/package.md)) |
| Textures | P8 with palettes (masked colour 0), DXT1, RGB8, RGBA8; all The Fallen's stored textures decode. Procedural ones play: fire, water, wave (lit as bumps), wet, ice, and The Fallen's FLC ones ([texture.md](formats/texture.md)); the fire, wave and wet ones are Fire.dll's (byte for byte, run against it in an emulator), and so is every texture's pacing |
| Sounds | the embedded WAV, played through `src/audio`; The Fallen's music streamed from `mp3/` in place of its placeholder Sounds ([sound.md](formats/sound.md)) |
| Music | The Fallen has no music modules: its music is the MP3s (above), which play. Other UE1 games' modules (`.umx`: IT, S3M, XM, MOD) play on the Sounds page through Reluva's tracker player ([tracker.md](../../../../common/audio/tracker.md)), rendered from the first order to where the song loops back; `unreal check` plays each through. A level's `Song` isn't played: The Fallen's audio driver (Miles.dll) never reads it (no module player, `RegisterMusic` empty), and the drivers that do (Galaxy's) aren't to hand to read their transitions from |
| Levels | all 74 of The Fallen's maps read and draw. The level's BSP model: points, vectors, nodes, surfaces, vertices ([model.md](formats/model.md)); polygons drawn textured, with masked, translucent and modulated surfaces, and only the zones seen through the zone portals. Lit by lightmaps worked out from the shadow bits and light actors, with zone ambient ([lighting.md](systems/lighting.md)): lights' types change their brightness (flicker, pulse) and their effects draw their patterns across the surfaces (a torch's waver, water's shimmer, waves rolling out), scripted lights follow their scripts, lights' coronas glow where they're seen ([rendering.md](systems/rendering.md)), lit actors light what's near them as they move, and movers are lit where they are. Movers (doors, lifts) from their own brushes, moved by their scripts, and the sky from the level's sky zone ([actors.md](systems/actors.md)). Lightmaps, movers, the sky and the light effects' patterns not yet compared with the original running |
| Meshes | all The Fallen's Mesh and LodMesh exports read (455 and 6), with their animations ([mesh.md](formats/mesh.md)): vertex animation, and characters posed from their skeletons with their own clips and the shared animation sets; layers on parts of the body, faces (blink, brows, mouth), tweens from the pose showing, and the scripts' bends and IK ([animation.md](systems/animation.md)); attachment points, where held things go, checked against the meshes' own precomputed places (`reluva-tool unreal points`, [bones.md](systems/bones.md)). Lit where they stand ([actors.md](systems/actors.md)), casting shadows of their own shape onto the floor ([rendering.md](systems/rendering.md)); a Meshes page |
| Viewer | Maps (3D, fly camera from the PlayerStart; Enter plays the level), Meshes, Textures, Sounds, Movies, Packages (tables and properties) |
| Movies | the intro (`movies/intro.avi`) plays first when the game starts, as the original's does, its pictures kept with its sound; Esc, Space or Enter skips it. AVI ([avi.md](../../../../common/video/avi.md)) with Cinepak video ([cinepak.md](../../../../common/video/cinepak.md)) and PCM sound; a Movies page plays it again |
| UnrealScript | classes, states, functions, properties, enums, constants and their bytecode read (every one of The Fallen's), and written out as text (`unreal script`) ([script.md](formats/script.md)). A VM runs them: classes loaded, objects, the interpreter with states, latent code and timers, and every native the game's scripts call, Core's, Engine's and The Fallen's own; every map runs (`unreal run`) ([script-vm.md](systems/script-vm.md), [level.md](systems/level.md)) |
| Game | started: the level's scripts run live with the player logged in, moving and colliding against the level ([movement.md](systems/movement.md), [collision.md](systems/collision.md)), steered from the keyboard and mouse and seen from the game's own camera, cut scenes included ([playing.md](systems/playing.md), [splines.md](systems/splines.md)). Actors' animations play on the scripts' timing, with their notifications. The AI finds its way along the level's paths ([ai.md](systems/ai.md)); the characters' lines play from the dialogue tables ([dialogue-tables.md](formats/dialogue-tables.md), [dialogue.md](systems/dialogue.md)), shown as they're said: cut scenes, hails over the communicator (requests for supplies, beamed down in the beam-down zones; reports, signing off) and the AI's own lines (spotting the player, losing them, chatter); the documents, the level's objectives and title and its actors' strings (messages, tricorder readings) read from `int/` ([int.md](formats/int.md), [documents.md](systems/documents.md)). The game's own HUD is drawn (its panels, numbers, communicator; [font.md](formats/font.md), [hud.md](systems/hud.md)), and its menus work: the title, credits, main menu and info pad, New Game travelling to the opening, the UWindow options window with the mouse ([menus.md](systems/menus.md)); games saved and loaded, in the original's own format ([saves.md](systems/saves.md), [save.md](formats/save.md)); sound: effects heard from where they are (muffled through walls), ambient sounds, the music (the MP3s) and the dialogue's voices ([sound.md](systems/sound.md)); the effects: particles, sparks, electric arcs, beams, rain and snow, decals ([effects.md](systems/effects.md)). No native a map calls is left unwritten; The Fallen's own physics modes work: the player climbs ladders, hangs from ledges and pulls up onto them ([climbing.md](systems/climbing.md)), and each kind of effect moves its particles |

**First check against the retail data** (October 2026): all 175 packages
open (versions 71-73), all 3801 palettes and 2868 of 2930 sounds decode.
A second run, after fixes, read all 74 maps (`.dsm`; the largest,
M03_KiraL2, 25619 BSP nodes) and they draw in the Maps page; 182
textures are procedural with no stored pixels (now shown as stand-ins,
[texture.md](formats/texture.md)) and the 62 sounds that fail
are empty placeholders for the music in the install's `mp3` folder
([sound.md](formats/sound.md)). How to check again is in
[TESTING.md](TESTING.md).

What's left is mostly checking against the original, and speed. Nothing
has been compared side by side with The Fallen running, though a full
playthrough's recording has been
([research/playthrough.md](research/playthrough.md)); what's still
open is in [UNCONFIRMED.md](UNCONFIRMED.md). The game's own gap, which
nothing here can fill: no dialogue table for mission 12.

### Speed

M10_WorfL1A, the slowest map, runs 20 seconds of play in about 5.8 s of
CPU in `unreal play`, which prepares every frame for drawing, after 0.8 s
starting; M03_KiraL2 and M04_KiraL1 take about 2.5 (November 2026, on
this container's CPU). Of a short run of M10, a quarter is the level's
ticks (mostly scripts), a sixth preparing the draws, a tenth the
procedural textures and 7% the sound (the music's MP3 decoding most);
allocating and freeing the VM's values is about a tenth throughout. The scripts alone
(`unreal run -all`) take at most 2.9 s for a map's 20 s, and 128 s for
all 74 maps. What's left most is posing and lighting the characters for
drawing, the HUD's scripts and the viewer's own text. The work done so
far is in [systems/playing.md](systems/playing.md#in-reluva).

- **The GPU path** (November 2026): `RELUVA_BENCH="<map> <seconds>"`
  starts `reluva <target>` straight into that map, plays it, and
  prints each frame's times: all of the page's update, the level's
  ticks within it, and the renderer's `endFrame` (submitting and
  waiting). Run in this container on Mesa's software Vulkan
  (lavapipe, `SDL_VIDEO_DRIVER=offscreen`), so `endFrame` there is a
  CPU doing the GPU's work; what's Reluva's own is the rest. Over 20 s:
  M04_KiraL1 46 ms a frame, of it 5.5 ms the ticks, 35.5 ms
  `endFrame`, 5.3 ms preparing the draws; M03_KiraL2 89 (11, 71, 7);
  M10_WorfL1A 219 (34, 161, 24). The ticks per frame grow with slow
  frames (several ticks each); per second of play they're about
  2.6 ms of every 60th. So on a real GPU Reluva's own work is about
  11 ms a frame on M04, about 30 on M10.
- Water and wet textures (`Water::render`, a quarter of M10's frame
  work in `unreal play`) now wrap with a step rather than a remainder per
  pixel: the same frames (74 textures hashed over 40 steps each),
  about 6% off M10's 20 s in `unreal play`.
- Since then (M04_KiraL1, the rifle drawn, 9 s): a character's pose,
  IK and all, is kept in its mesh's poser (the last 8, by everything
  it's made from), and the drawing poses with the level's poser, so
  the pose its scripts asked after this tick is drawn as it is. The
  scripts' own queries still pose it about three times a tick for the
  player: they change its bone turns and IK targets between them. The
  viewer's info box keeps its text and draws only the lines that
  changed (5% of the time before). Together about a tenth less time,
  the frames the same to the pixel.
- The HUD is the game's own `PostRender` script once a frame (4%);
  only a faster VM makes it cheaper.
- The VM (November 2026, measured in instructions under callgrind,
  M10_WorfL1A's 20 s in `unreal run`: 14.66 billion before, 12.20
  after, 17% fewer; `run -all` 82 s from 96, every map's record the
  same to the call): a property the engine's code reads by name finds
  its slot through a small cache in front of the class's table (it was
  15% of the run, the effects' copies reading theirs for every copy on
  every tick); a value's struct fields are nothing until it has some,
  so the many values that are numbers or objects are made, copied and
  dropped without a vector; a call's arguments and a function's locals
  reuse the room of the call before at the same depth; a float is set
  in place. Pooling vector values themselves, tried before, was slower.
  Then each call by name keeps the name it interned and the function it
  found for the class and state it was last made on, a native call the
  function its number names, and an `IsA` by a class's name the name it
  interned: 11.76 billion, 20% fewer than at first. What's left is
  mostly the values themselves. So a value's string, like its struct
  fields, is nothing until it has one (a value is 48 bytes, from 72),
  and a struct value's field list, let go, is kept for the next one,
  room and all, so vectors and rotators are made without allocating:
  11.05 billion, 25% fewer than at first.

## Using it

- **The game.** `reluva` starts it with the intro movie (Esc, Space or
  Enter skips it), then plays the level the game's `.ini` names
  (`[URL] LocalMap`, `title.dsm`), as the original does. The keys are
  the game's: W/S move, A/D step aside, Left/Right turn, Up/Down look,
  the mouse turns and looks, Space jumps, C crouches, Ctrl or F fires,
  the number keys call what the game binds them to, F6 saves and F5
  loads ([playing.md](systems/playing.md),
  [saves.md](systems/saves.md)). Developer builds keep the data
  viewer's keys in the game too.
- **The data viewer** is `reluva-viewer` (the game's entry from its list,
  `reluva-viewer <entry>`, or `reluva-viewer <game folder>`), with the
  pages Maps (a fly camera from the PlayerStart; Enter plays the level,
  V lets the camera loose and gives it back, P turns the scripts off to
  show the level as saved), Meshes, Textures, Sounds, Movies and
  Packages.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [formats/](formats/) | The game's files: [packages](formats/package.md), [textures and palettes](formats/texture.md), [sounds and music](formats/sound.md), [BSP models](formats/model.md), [meshes](formats/mesh.md), [fonts](formats/font.md), [documents, objectives and map strings](formats/int.md), [dialogue tables](formats/dialogue-tables.md), [compiled UnrealScript](formats/script.md), [saved games](formats/save.md) |
| [systems/](systems/) | How the game behaves: [the UnrealScript VM](systems/script-vm.md), [a level's actors and ticks](systems/level.md), [movement](systems/movement.md), [climbing](systems/climbing.md), [collision](systems/collision.md), [the AI](systems/ai.md), [dialogue](systems/dialogue.md), [the HUD](systems/hud.md), [the menus](systems/menus.md), [sound and music](systems/sound.md), [effects](systems/effects.md), [saved games](systems/saves.md), [documents and objectives](systems/documents.md), [splines](systems/splines.md), [animation](systems/animation.md), [bones and attachment points](systems/bones.md), [playing a level](systems/playing.md), [lighting](systems/lighting.md), [shadows, coronas, fog and the rest of a frame](systems/rendering.md), [movers, the sky zone and sprites](systems/actors.md) |
| [research/](research/README.md) | Working notes: the comparison with a full playthrough's recording |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Terms

The engine's own words (package, export, native, zone, mover...) are in
its [README](../../README.md#terms).

- **CTVE, CTV:** the prefixes of The Fallen's own classes
  (`CTVE_PlayerPawn`, `CTV_Human`).
- **C3D:** The Fallen's own natives and mesh states for its skeletal
  meshes (`C3DLoopAnim`, `C3DSpeechEvent`)
  ([animation.md](systems/animation.md)).
- **Ghost:** an actor set aside out of the level until an event brings it
  back (`bIsGhost`, `GhostActors`) ([level.md](systems/level.md)).
- **MultiMesh:** copies of one actor that an effect draws
  (`CTVE_MultiMesh`) ([effects.md](systems/effects.md)).
- **Hail:** a call over the communicator ([dialogue.md](systems/dialogue.md)).
- **`.dsm`, `.dsa`:** The Fallen's maps and saved games.
