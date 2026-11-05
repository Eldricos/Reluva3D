# Testing status

## Tests without the game's files

Run by `reluva-tests stgenerations` (and ctest, and CI on every system):

| Suite | What it covers |
|---|---|
| `stgenerations.unit.script` | Script files and the script VM: the program's layout, arithmetic, conditions, states and events, locals, natives, parents, the opcode table |
| `stgenerations.unit.level` | Levels: tables, textures, the map, surfaces and skies, the 3D renderer and its lighting, walking (walls, steps, falls, jumps, gaps), missions (scripts, items, punches, flow, settings), actors, projectiles, doors and lifts, line of sight |
| `stgenerations.unit.hud` | The HUD: inventory, panels, effects, dragging items, the item held up, unprojecting, the dissolves |
| `stgenerations.unit.combat` | Space combat's data (textures, models, ship classes, missions), its projection, the story driving the bridge |
| `stgenerations.unit.campaign` | The story's script; the captain's menu |
| `stgenerations.unit.sound` | The missions' sounds: file names, subtitles, cues, positions, events |
| `stgenerations.unit.formats` | Fonts, subtitles, overlays, sprite sets, script files |
| `stgenerations.unit.canvas` | The game's drawing surface, BMPs, palettes |
| `stgenerations.unit.audio` | The game's sound calls on Reluva's mixer |
| `stgenerations.unit.detection` | The installed game and its CDs found, importing them (all at once, a part at a time, the whole copy of a damaged one), the files then searched |
| `stgenerations.engine` | The whole engine, on a game built in code: imported as the launcher does, then played: the title, the menu, a new game's movie, an automatic save, the bridge, a saved game named and loaded, the settings, quitting through the credits; recorded in `tests/engines/stgenerations/data/engine-record.txt` |

STG-Engine's tests are all here, as these suites ([PORTING.md](PORTING.md#tests)).

## With the game's files

`RELUVA_STGENERATIONS=<the game's folder> ctest -L gamedata` (Reluva's
import of the game, or the install and both CDs side by side), steps
`stgenerations.gamedata.<step>` (`tests/engines/stgenerations/gamedata.cmake`):

| Step | What it runs |
|---|---|
| `release` | What the game is, its parts, its files' signatures |
| `archives` | Every archive's every file read |
| `pictures` | Every bitmap, overlay, sprite set and font |
| `levels` | Every away mission's level, textures, sky and script |
| `sounds` | Every sound |
| `movies` | Every movie's frames and sound |
| `front_end` | The title, the menu, a new game, its movies skipped, the bridge, a save named and loaded |
| `mission` | The first away mission: its briefing, PROCEED, a few steps in |
| `combat` | The first space combat: the briefing, PROCEED, the helm's orders, phasers, a torpedo |

The records (`tests/engines/stgenerations/data/gamedata/english/`) were
made on Linux from an import of the 2 CD English release. They hold only
checksums, counts and summary lines. The play steps run the game's code,
3D included: their records may differ in the last bits on other systems
(see [DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests)); keep a baseline
of your own there.

## Checked on the game's own files

With a copy of the 2 CD English release (installed, and both CDs):

- **Recognised:** `sgens.exe` and `DATA/COMMON.DAT` match STG-Engine's
  signatures exactly.
- **Imported:** the install and, from the CDs, the story, space combat,
  the music and the movies (1.2 GB).
- **Read:** all 18 archives (8175 files), 1218 bitmaps, 33 overlays, 97
  sprite sets, 5 fonts, all 12 levels and their scripts, 4129 sounds, all
  14 movies (every frame).
- **Movies:** every frame of `MOV_07.AVI` matches FFmpeg's decoding to the
  pixel.
- **Played** (`reluva-tool stgenerations play`, its pictures looked at):
  the title and the captain's menu; New, the prologue's movie; the bridge,
  with the crew's subtitled speech; a game saved under a typed name and
  loaded back; the first away mission's transition, 3D view, HUD and
  walking; space combat's briefing and console.
- **In a window** (`reluva` under X, SDL's renderer): the picture scaled,
  clicks where they should be, the game's pointer as the system's.

That copy has a few damaged files, which the game reads as the original
would:

| File | What's wrong | What happens |
|---|---|---|
| `MUSIC.1`: `MUSI9038`, `MUSI2507`; `MUSIC.2`: `MUSI9020`, `MUSI9055`, `LOSE1`, `MUSI1502` | Stored with a few bits wrong (their CRCs don't match) | Played as they are, a warning in the console |
| Disc 1's `STELLAR.DAT`: `SOUND/VOICES/COMP0213`, `COMP0152` | Damaged; disc 2's copy is whole | Import Game copies disc 2's |
| `GALORN1.DAT`: `galorn1/sounds/troi0083.wav` | Damaged, in the install and on disc 2 alike | That line of Troi's is silent |

The install also holds folders of files someone took out of the archives
(`DATA/amargos1/`, `COMMON/` and others, 194 pictures); the game doesn't
read loose files below `DATA/` (nor did the original), so they make no
difference.

## Still to check

- **A full playthrough** in Reluva, every mission and combat, through each
  ending.
- **The unconfirmed details** ([UNCONFIRMED.md](UNCONFIRMED.md)), against
  the original.
- **Other systems:** Windows and macOS, a real sound card, a high-density screen (the
  pointer's second picture).
- **Other releases:** no other language or build has been seen.
