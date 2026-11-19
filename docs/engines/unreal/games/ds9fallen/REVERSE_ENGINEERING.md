# The Fallen: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and may not
  be used.

**Clean room.** The formats were written up from public community
documentation of the UE1 package format (the notes written alongside
community package tools, and modding wikis), and checked against The
Fallen's own files. There is an open-source clean-room reimplementation
of UE1; it may be consulted as a pointer to where to look, but none of
its code is copied or translated, and findings are confirmed against the
game's data with `reluva-tool` before they go in. Never use Epic's
licensee source code (leaked or otherwise).

As ScummVM does, the original game's own binaries (`Engine.dll`,
`Render.dll`, `Fire.dll`, `DS9.exe` and the rest) may be disassembled to
find out what the engine does: which light a shadow is cast from, how a
curve is shaped. What's learned is written up in these docs as
behaviour, in our own words, and implemented as new code of Reluva's
own: nothing from the binaries is copied, translated instruction by
instruction, or kept in the repository.

## Sources

- **The game's own data:** a retail Windows install (packages of
  versions 71-73: its `System`, `Maps`, `Textures`, `Sounds`, `int`,
  `mp3` and `movies` folders), and the full CD install for the files
  some copies lack (mission 11's and the training's dialogue and `.int`
  files). Saved games written by the original. The files were used
  locally and never committed.
- **The game's DLLs,** read for what they do:
  - **`Engine.dll`:** loading a map (`UGameEngine::LoadMap`), the tick
    (`ULevel::Tick`, `AActor::Tick`, `AActor::ProcessEvent`), logging
    the player in and travel (`ULevel::SpawnPlayActor`), moves and the
    physics modes (`ULevel::MoveActor`, the `phys*` functions, The
    Fallen's `ACTVE_PlayerPawn::physClimbing`, `physHanging`,
    `physClambering`, `TestForLedge`, `CheckClimbing`), collision
    (`UModel::PointCheck`, `LineCheck`, `PointRegion`), the AI's paths
    (`APawn::findPathToward` and the rest), the dialogue engine
    (`FDialogEngine`), the Canvas natives, sound (`execPlaySound`,
    `CheckHearSound`), animation (`execPlayAnim`, `execLoopAnim`,
    `execTweenAnim`, `execFinishAnim`, `AActor::UpdateAnimation`, the
    `C3D` natives), attachment points (`PointMatrix`, `TriFrame`),
    splines (`ACTVE_Spline::GetData`, `ComputeRotation`), saving
    (`SaveGame`), the textures' pacing (`UTexture::Tick`,
    `UTexture::Update`).
  - **`Render.dll`:** the light manager and lightmaps (`SetupForActor`,
    `GlobalLighting`, `AddLight`), characters' shadows
    (`PrepareProjectedShadow`, `DrawProjectedShadow`), sprites and
    meshes (`DrawActorSprite`, `DrawActorSpriteMM`, `DrawMesh`), vertex
    normals, coronas, distance fog (`SetDistanceClipping`).
  - **`Fire.dll`:** the fire, water, wave, wet and ice textures; its
    routines also run in an x86 emulator against Reluva's.
  - **`Miles.dll`:** The Fallen's audio driver (`MilesAudioSubsystem`:
    its `PlaySound` and per-frame `Update`), the lip-sync, the choice of
    3D provider; with Miles' own `Mssfast.m3d` and `Reverb3.flt` from
    the install's `redist`.
  - **The render drivers** `D3DDrv` and `OpenGLDrv`, for how a blended
    polygon is drawn, the detail textures and fog.
- **The game's scripts:** its compiled UnrealScript, written out as text
  (`reluva-tool unreal script`), for what the natives' callers expect.
- **Public write-ups:** community documentation of the UE1 package
  format, of UnrealScript's bytecode and of UnrealEd's properties; the
  public UnrealScript documentation of actors' lifetimes; the tracker
  formats' public descriptions.
- **Recordings of the original:** a play-through (5 h 40 m, 960x720),
  a full playthrough of all three characters (9 h 27 min, 1280x720, of a
  later build than these files: [research/playthrough.md](research/playthrough.md)),
  and a screenshot of the original in TrainingRoom.

## Methods

- **Reading the DLLs.** Functions are named in the docs by their exports'
  names, or by their address where they have none (Render.dll's per-light
  setup at `0x10b0772e`). What each does is written up on its page, and
  Reluva's code for it written in Reluva's own terms.
- **Running the original's code to compare.** Fire.dll's routines were
  run in an x86 emulator
  against Reluva's own procedural textures, byte for byte.
- **Sweeping the install.** `reluva-tool unreal check` reads every
  package to its end, and reports what doesn't read; `unreal run -all`
  runs every map; `unreal probe` maps an export's bytes where a layout
  wasn't known (The Fallen's meshes).
- **Checking against the recordings.** Each level load in a recording
  is found where the frame stops changing (the LOADING panel), and
  Reluva's frames taken at the same times after its start; colours,
  timings, cameras and dialogue compared
  ([research/playthrough.md](research/playthrough.md)).
- **When the sources disagree.** The DLLs decide how the game behaves; a
  recording decides what was seen, but the recordings are of a later
  build. Where Reluva follows a recording against the code, it says so
  in [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md); what neither settles
  is in [UNCONFIRMED.md](UNCONFIRMED.md).

## The tools

`reluva-tool help unreal` lists the engine's commands
(`src/engines/unreal/tools/`):

| Command | What it does |
|---|---|
| `unreal check <game folder> [-v]` | Reads every package of a game: textures, sounds, meshes, models, scripts; and the levels' attachment points |
| `unreal info <package>` | A package's header, names, imports and exports by class |
| `unreal save <saved game .dsa>` | What a saved game holds |
| `unreal exports <package> [filter]` | A package's exports, those whose class or name has the filter |
| `unreal props <package> <export>` | An export's properties, and the bytes of its class's own data |
| `unreal extract <game folder> <package name> <export> [steps]` | Writes an export as a file: a texture as `.tga` (a procedural one after some steps), a sound as `.wav` |
| `unreal points <game folder>` | Every mesh's attachment points checked against its bones' precomputed places |
| `unreal probe <package \| game folder> <export> [-all \| -at <offset> <bytes>]` | An export's data read as each layout it might have |
| `unreal script <game folder> <class \| Class.Function \| -all <dir>>` | Compiled UnrealScript as text |
| `unreal run <game folder> <map \| -all> [seconds]` | Runs a level's scripts (or every level's) for some seconds, with what they did and what failed |
| `unreal play <game folder> <map> [step...]` | Plays a level without a window, driven by steps ([playing.md](systems/playing.md)) |
| `unreal trace <game folder> <map> x y z x2 y2 z2 [ex ey ez]` | A box moved through a level's BSP: where it stops, and the surface it hit |
| `unreal ledges <game folder> <map> [max \| ladders \| lights <effect>]` | Places in a level where the player could climb or hang, to try with `play`'s `teleport` |

`reluva-tool avi-frames` decodes the intro movie's frames, and
`reluva --hash` gives the files' signatures for the detection table.

## Working files not included

- **Disassemblies of the DLLs.** Not kept in the repository; made again
  from your own copy of the game with any disassembler (Reluva's own:
  [disasm.md](../../../../common/x86/disasm.md)).
- **The recordings** and the frames cut from them: someone else's
  videos, not made here.
- **Scripts written out as text** (`reluva-tool unreal script <game
  folder> -all <dir>`) and exports extracted (`unreal extract`): made
  again from your own copy.
