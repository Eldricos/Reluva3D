# Murder on the Orient Express: plan

This page began as a plan written from a directory listing alone. The
sections from "What the install holds" onwards keep that plan. Where
sample files have since confirmed or corrected it, "Where it stands"
below says so, and the formats and the game's behaviour are written up
in [../../formats/](../../formats/) and [systems/](systems/), with how each was
worked out (CONTRIBUTING.md).

"Where it stands" is now on the [README](README.md#where-it-stands),
and the corrections to the plan are listed here, before it.

Corrections to the plan below:

- **`.01` and `.02` backgrounds are two camera views of a room**, not a
  picture and its mask: `Athens.1-4_1-2.01.jpg` looks at the door,
  `.02.png` at the window. The PNG views have alpha where the windows
  are, so the moving scenery outside (`Elements/Skies`, `Snowscapes`,
  the blizzard videos) shows through.
- **`.ac2` is not text.** It's the game's objects written field by field.
  Saves use the same extension with different records.
- **Most Granny animations need no curve decompression.** The 2.5
  exporter wrote plain B-spline curves, except in 36 of the 1,701
  animations, which use eight of Granny's compressed formats.
- **The model textures are inside the `.gr2` files**, Bink-compressed. The
  folders' few JPEGs are extras.

## Goal

Murder on the Orient Express playable from the intro to the end (phase
6 below), then the other two games (phase 7).

### What the install holds

Taken from a directory listing of the retail English Windows release
(7,876 files, 1.36 GB). **Everything is a loose file, so there is no container
format to crack.** `DirectoryArchive` already reads the folder, with names
matched ignoring case.

| Where | What | Format | Reluva today |
|-------|------|--------|--------------|
| `motoe.exe` (684 KB) | the game: engine and game logic | PE, Visual C++ | `common/pe` reads it |
| `granny2.dll`, `binkw32.dll`, `mss32.dll` + `mssmp3.asi`, `unicows.dll` | RAD's Granny (3D), Bink (video), Miles (sound); Unicode on Win9x | PE DLLs | x86 interpreter can load them |
| `Locations/<Area>/*.ac2` (≈380) | one per scene/camera: `Salon_Table.02_06b.ac2` = Salon, table 2, act 6b | **unknown, AWE's own** | — |
| `Locations/<Area>/Backgrounds` | prerendered views, mostly in pairs: `X.01.jpg` (~300 KB) and `X.02.png` (~1.6 MB) | JPEG/PNG | `common/image` decodes |
| `Locations/<Area>/Cutouts` | foreground pieces drawn over characters (probably), a few `.bik` | PNG/JPG/TGA/Bink | images yes |
| `Locations/Menu.*` | the menus are scenes like any other | `.ac2` + images | — |
| `Text/Scripts/slot0..14.ac2` (25 KB → 365 KB) | per-chapter game state? Starting saves? | `.ac2` | — |
| `Models/<Character>/*.GR2` (1,803) | `<name>_model.GR2` mesh + skeleton, one GR2 per animation (`poirot_walk.GR2`), face animations (`_f*`), `_model_1024` more detailed meshes the game never loads | Granny 2 | — |
| `Models/<Character>/*.wav` | footstep/move sounds tied to animations | WAV | `common/audio` |
| `Movies/*.bik` (46, 570 MB) | cutscenes | Bink 1 (2006 predates Bink 2) | — |
| `Elements/Binks`, `Elements/Water` | looping video textures: flames, curtains, water, blizzard | Bink | — |
| `Elements/Fog`, `Skies`, `Particles`, `Snowscapes` | effects layers. The fog TGAs are 3,145,772 bytes = 1024×768×4 + header, so **the game's frame is 1024×768** | TGA/JPG | yes |
| `Interface/` | cursors (one TGA per verb: look, take, talk, operate, walk...), dialogue windows, menu bar, placard | TGA/PNG | yes |
| `Interface/Fonts` | Arial, Arial Narrow, Calisto | TrueType | **no TTF rasteriser** |
| `Inventory/Items/<item>/` (≈150 items) | `inv*` icon, `cur*` cursor, `exa*`/`ex2*` close-up, `sce*`/`sc2*`/`sc3*` (probably how the item looks in the scene) | PNG/JPG | yes |
| `Inventory/Scrapbook`, `Text/Scrapbook` | Poirot's journal/scrapbook pages and their text | PNG/JPG + text | yes |
| `Text/*.txt` | `acts`, `itemlist`, `combinelist`, `uselist`, `navigation`, `interface`, `fonts` | text, probably | — |
| `Text/Dialogue/Act*.txt`, `gen_*.txt`; `Text/Dialogue/Events/*.txt` (561) | dialogue trees and lines; one small file per dialogue event, named like the VO | text | — |
| `Text/Movies/*.txt` | subtitles, one per cutscene | text | — |
| `Sound/VO` (3,084 WAVs, 156 MB) | voice, named `dia_<who><act><node>.wav`, `gen_*`, `com_*` | WAV (PCM or IMA ADPCM?) | yes |
| `Sound`, `Sound/Items`, `Sound/Footsteps` | effects | WAV | yes |
| `Sound/Music` | 3 MP3s + 1 WAV | MP3/WAV | yes |

Odd ones to check: seven files with no proper extension
(`Calais.5-8_5-6.Night.01`, `Clock.730`, `Calais.9_Cutshot.02`,
`calais.9_table.01`, `Restaurant_Kitchen.01.Cutout.01`,
`Restaurant_Counter.01.Food`, `Restaurant_Kitchen.Fridgedoor`). They're
probably images whose extension the engine adds, or leftover files nothing
loads. The `.ac2` files will tell.

### How the game probably works

From the data (to be confirmed): each **scene** is a prerendered 2D
background from a fixed camera. **3D characters** (Granny models, skinned
and animated) walk on it. **Cutouts** go over them where scenery is in
front, and the `.02.png` may be a depth or mask layer for the same job.
Video loops play in parts of the frame. The player clicks hotspots with verb
cursors, talks through dialogue trees, collects and combines items, and
the story moves through acts (`01a`…`08a`, matching the file suffixes
`_06a`, `_06b`, `_07a`, which pick a scene's variant for an act).

The `.ac2` files carry the scene: camera, hotspots, walk areas, where
characters stand, and the scene's scripting. A file-type database lists
`.ac2` as the game's save format too. That fits one serialisation of
AWE's objects used for both scene data and saves, and would explain
`Text/Scripts/slotN.ac2` as built-in states per chapter.

### Questions once open, now answered

- **The release:** the English retail release, 1.0, its files dated
  2006-11-05. Other languages and patched releases haven't been tried.
- **The resolution:** 1024×768 only. The device's back buffer is set to
  1024 by 768 (`0x401d99`, `0x401db2`) and the display modes are
  searched for 1024×768 alone (`0x401e0d`); the options offer no other.
- **The `_model_1024` files** (31 in the install) are never loaded:
  nothing in `motoe.exe` names them (it holds no "1024" at all) and no
  scene or table does. The scenes name the `_model` files, so these are
  left over from making the game, not a detail setting.
- **Recordings of the original:** a walkthrough's videos, calibrated
  against the install's backgrounds, were used to check how scenes,
  text and fades look ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)).

## Phases

Each phase ends with something that runs and can be checked by eye, in the
data viewer style New Worlds and Elite Force used. Viewers stay in the build
as debugging tools after the game itself runs.

**Phase 0: detection and skeleton.**
- `src/engines/awe/` with a `MetaEngine` and the CMake option.
- Detection by `motoe.exe` plus one or two data files (md5 of the first
  5000 bytes and size, from `reluva --hash`), with a fallback entry for
  unknown releases.
- An engine that opens a 1024×768 window with a list of the game's files.
- So the launcher's *Add Game...* finds the game and starts it.

**Phase 1: probing tools and format notes.**
- `reluva-tool` commands: `awe gr2-info` (header, sections, compression,
  type tree), `awe ac2-dump` (as far as it's understood) and `awe bink-info`
  (header, frame count, size, audio tracks).
- Format notes, each saying how its contents were worked out:
  [ac2.md](formats/ac2.md), [gr2.md](../../formats/gr2.md),
  [bink.md](../../../../common/video/bink.md), [text.md](formats/text.md).

**Phase 2: 2D viewers.**
- A scene viewer: backgrounds with their cutouts layered. Test the
  `.01`/`.02` theory by toggling the second layer.
- Inventory items in all their forms, cursors, interface pieces.
- A text viewer for dialogue, item lists and subtitles, with the VO played
  for a line.
- Needs TrueType (part 4).

**Phase 3: video.**
- Bink decoder (part 3).
- A movie viewer with sound and subtitles from `Text/Movies`.
- Scene video loops (water, curtains, flames) playing in the scene viewer.

**Phase 4: 3D characters.**
- GR2 reader (part 2).
- A model viewer: turntable, every animation of a character, face
  animations layered on body ones, `_model` against `_model_1024`.
- Then characters placed in the scene viewer once `.ac2` gives the camera.
  This is the first check that the camera has been read right, since the
  model must line up with the prerendered room.

**Phase 5: scenes from `.ac2`.**
- Hotspots, exits between scenes (`navigation.txt`) and the walk area
  shown over the background.
- Clicking through the train scene to scene. Poirot walking with
  pathfinding over the walk area.

**Phase 6: the game.**
- Verbs and cursors, inventory (`itemlist`, `combinelist`, `uselist`) and
  dialogue trees with VO.
- Acts and their scene variants, the scrapbook and journal, and the
  in-game, save and load menus (themselves scenes).
- Saves: our own format first. Reading the original's `.ac2` saves is a
  bonus once the format is known.
- Playable from the intro to the end, chapter by chapter, using
  `slotN.ac2` as start points if that theory holds.

**Phase 7: the other two games.**
- Compare the And Then There Were None and Evil Under the Sun installs
  against this.
- Generalise per game: file names, `.ac1` against `.ac2`, interface
  layout.

Next:

1. The game plays from a new game to the end credits without a window:
   `reluva-tool awe game-run <install> -solve` finds a way of 1,075 actions
   in about a quarter of an hour, and `-play` plays it back (see
   [systems/acts.md](systems/acts.md#a-playthrough-from-a-new-game--solve-and--play)).
   With `-mouse -characters` the player clicks points the game's pick
   finds, and the characters' routines run; `-3d` draws in a window.
   Played that way, the first playthrough went wrong where Reluva
   differed from the game (fixed), and where it relied on what a player
   can't do: a door hidden by another, a routine that hadn't run. Run
   again as a mouse player with the characters, the search finds a way of
   1,105 actions whose every click the game's pick makes (see
   [systems/acts.md](systems/acts.md#as-a-player-plays-it--mouse--characters--3d)).

## Decisions

How each hard part was to be approached, and how the engine was to be
tested, as the plan set out.

### The hard parts, and how to approach each

#### 1. `.ac2`: the core unknown

Everything game-specific depends on it, so it comes first. Approach:
- Hexdump a spread (smallest: the 4.6 KB `Salon_01_06a.ac2`; a menu one; a
  big one; a `slot` one) with `reluva-tool hexdump`. First question: is it
  text, tagged binary, or a raw object dump?
- Compare files that should differ only slightly (`Salon_01_06a` and
  `Salon_01_06b` are both 4,640 bytes) to find what's a name, a count or a
  coordinate.
- Find the file names it mentions (backgrounds, cutouts, models, VO) to link
  scenes to their assets.
- Then read how `motoe.exe` loads it (allowed by CONTRIBUTING for
  interoperability) where comparing files stops working. That's the same
  route taken for Elite Force's single player interface.

#### 2. Granny 2 (`.GR2`)

RAD's Granny files are self-describing: a header, sections (each may be
compressed), relocation tables, and type trees that name every field. So once
a file is decompressed and its pointers fixed up, a generic reader can walk
any of them (models, skeletons, animations) without knowing their layout up
front. Public descriptions of the container exist: the opengr2 wiki,
RagnarokFileFormats' GR2 notes, and the Knit reader. Plan:
1. Read the header and section table, and report the Granny version and
   each section's compression. Many games' GR2 files are uncompressed, and
   these files' sizes (lots of near-identical 30 KB animations) don't
   decide it either way.
2. If sections are Oodle-compressed (Granny 2.x of that era uses "Oodle0"
   or "Oodle1"), **first run `granny2.dll`'s own decompressor in the x86
   interpreter**. It's load-time work, so speed doesn't matter, and the
   data never leaves the user's copy. That gets models on screen early. A
   native decompressor, written from public descriptions, can follow,
   checked byte for byte against the DLL's output on every file in the game.
3. Generic type-tree walker → mesh (vertices, indices, bone weights),
   skeleton, material/texture names, animation curves. Granny stores curves
   in several compressed forms (DaK32fC32f, D4nK16uC15u…). Implement the
   ones the game actually uses: the reader can count them over all 1,803
   files.
4. Skinning on the CPU into `gfx::Vertex` each frame at first (characters
   are few). Move it to the GPU later if needed.

#### 3. Bink video (`.bik`)

Needed early: cutscenes, the intro and logos, and video loops in the scenes.
Bink 1 is documented on the MultimediaWiki. Write a decoder in
`src/common/video/` (shared, since many games of the era use Bink):
container, video (bundles, Huffman trees, DCT blocks, motion), and Bink
audio (the DCT/RDFT variants). Check its frames against reference
decodes. The x86 route (`binkw32.dll`) is a poor fit here: real-time video
needs speed, and the DLL likely uses MMX, which the interpreter lacks.

#### 4. TrueType text

The game draws all its text in Arial, Arial Narrow and Calisto from
`Interface/Fonts`, mapped by `Text/fonts.txt`. Add `stb_truetype` beside
`stb_image` in `third_party/stb` (public domain/MIT, same as what's there)
and a `common/` text renderer with UTF-8, wrapping and caching glyphs.
Shared with later engines.

#### 5. Game logic: native, not emulated

Unlike Elite Force, the game logic is in `motoe.exe` itself, not in DLLs
behind a clean interface. Running the whole exe (DirectX, windows,
threads) isn't practical. The logic gets reimplemented natively from the
data (`.ac2`, `Text/*.txt`) and from studying the exe, as with New Worlds.
The x86 interpreter stays a tool for the RAD libraries only.

### Testing

- Unit tests (`tests/engines/awe/unit/`) on small files built in the tests
  themselves, the way `zip_writer.h` builds archives. **No game data is ever committed.**
- The Bink decoder gets tests against tiny hand-made streams and is checked
  locally against the game's movies.
- `reluva-tool` sweeps over the whole install (every GR2 parses, every
  `.ac2` reads to its end, every name an `.ac2` mentions exists). These are
  the regression tests for anyone with the game.
