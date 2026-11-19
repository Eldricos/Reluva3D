# The Fallen: testing

How Reluva's Unreal engine is checked on The Fallen: the automated
checks, the checks run by hand on the game's files, and what to try in a
playthrough.

## Automated checks

### Unit tests

`tests/engines/unreal/unit/`, a file per topic, each a suite
`unreal.unit.<topic>`, on data built in the tests (never the game's
files). `reluva-tests unreal` runs them, and `ctest` with the rest.

| Suite | What it checks |
|---|---|
| `unreal.unit.packages` | Compact indices, the package tables, properties, search paths and detection, the map extension, a dialogue table |
| `unreal.unit.textures` | Textures (stored and compressed), procedural and animated textures |
| `unreal.unit.sounds` | Sounds, their errors, and the streamed MP3s that stand in for the placeholders |
| `unreal.unit.levels` | The BSP model, actors, zone visibility, collision |
| `unreal.unit.meshes` | Meshes, the imported-model block, animation sets and poses, class defaults and mesh actors |
| `unreal.unit.script` | Compiled script read and written as text, and the VM |

### The basic engine test

`unreal.engine` (`tests/engines/unreal/engine.cpp`), without the game's
files: a small install of its own (a System folder with its `.ini` and
packages built in the test, a Textures folder), found as The Fallen, its
packages opened through the `.ini`'s search path, the level it would
start in read and a texture decoded, against its record
(`tests/engines/unreal/data/engine-record.txt`).

### The game-files test

Not yet. Until there is one, the game's files are checked by hand with
`reluva-tool`, from your own copy of the game:

```sh
./build/reluva-tool unreal check "/path/to/DS9TheFallen"      # every package, texture, sound, level
./build/reluva-tool unreal info  "/path/to/DS9TheFallen/System/Engine.u"
./build/reluva-tool unreal props "/path/to/DS9TheFallen/Maps/<map>.unr" <export>
./build/reluva-tool unreal script "/path/to/DS9TheFallen" <class | Class.Function | -all <dir>>
./build/reluva-tool unreal run    "/path/to/DS9TheFallen" <map | -all> [seconds]
./build/reluva-tool unreal play   "/path/to/DS9TheFallen" <map> play wait 20 hold w 2 shot frame.ppm
./build/reluva-tool unreal points "/path/to/DS9TheFallen"     # attachment points against the meshes' precomputed places
./build/reluva-tool unreal ledges "/path/to/DS9TheFallen" M01_KiraL2   # places to pull up onto a ledge or catch one, duck under, swim into
./build/reluva-tool unreal ledges "/path/to/DS9TheFallen" M01_KiraL2 ladders   # the ladders, their width, a foot in water (also: steps, lights <effect>)
./build/reluva-tool unreal trace "/path/to/DS9TheFallen" M01_KiraL2 x y z x2 y2 z2 [ex ey ez]   # a box swept through the BSP
./build/reluva-tool unreal extract "/path/to/DS9TheFallen" <package> <export>   # a texture (.tga), sound or music
./build/reluva-tool avi-frames "/path/to/DS9TheFallen/movies/intro.avi" frames <frame>
./build/reluva --hash "/path/to/DS9TheFallen/System/DS9.exe" ".../System/Engine.u"
```

and fixing what `unreal check` reports: the version/licensee pair The
Fallen uses, and any place its layouts differ from stock UE1 (licensees
often added fields).

- **`unreal check <game folder>`** reads every package: textures (each
  animated one run for ten seconds), sounds, meshes, models, scripts, and
  the levels' attachment points. Its counts are on each format's page.
- **`unreal run <game folder> -all`** starts every map, logs the player
  in and runs 20 seconds of play, reporting the scripts' errors, natives
  not written, None accessed and how things moved
  ([level.md](systems/level.md)).
- **`unreal play <game folder> <map> <steps...>`** plays a level without
  a window, driven by steps, and takes shots (`shot <file.ppm>`) to look
  at ([playing.md](systems/playing.md)).

### What the automated checks don't cover

Everything that needs The Fallen's own files: the unit and engine tests
use data built in the tests. And how the game looks and sounds against
the original: that's checked by eye and ear, against recordings.

## A playthrough by hand

In the order a player meets it:

1. **The start.** `reluva` plays the intro movie with its sound; Esc,
   Space or Enter skips it. Then the title, the credits and the main
   menu.
2. **New Game** travels to the opening (`CM01a_SW`, the space flyby and
   bridge scene, then M01_SiskoL1A): the cut scenes' cameras, subtitles
   and letterbox as in the original.
3. **Playing a level:** moving, jumping, crouching, firing; the camera
   following and pulling in out of walls; climbing ladders, hanging from
   ledges and pulling up onto them; swimming.
4. **The AI:** guards spotting, hearing and searching for the player,
   their lines; friendly characters following their paths.
5. **Hails** over the communicator: requests for supplies, beamed down in
   the beam-down zones; reports; signing off.
6. **The HUD and the info pad:** the objectives, documents and the
   tricorder's readings.
7. **The options window** with the mouse, its settings changing the game
   as it goes (the volumes among them).
8. **Saving and loading:** F6 and F5, and the menus' Save and Load; a
   save of the original's loads in Reluva, and Reluva's in the original.
9. **Travelling** from level to level, with the player's things where a
   level's parts link.
10. **The end credits** (`CM_SpaceStation/Endcredits`).

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| October 2026 | Windows, English | — | `unreal check` over the retail data: all 175 packages, every palette, texture, sound, mesh and script, all 74 maps; `unreal run -all` |
| October 2026 | Windows, English | — | Against a play-through recorded from the original (5 h 40 m, 960x720), shot for shot in cut scenes ([lighting.md](systems/lighting.md)) |
| November 2026 | Windows, English | — | Against a full playthrough's recording (all three characters, 9 h 27 min) ([research/playthrough.md](research/playthrough.md)) |
