# And Then There Were None: plan

Written from a survey of the retail English release (two CDs), before any
code. Where the survey ran Reluva's own readers or the game's DLLs over
the files, it says so; the rest is read from the files' first bytes and
the executable's strings, and is to be confirmed as each phase reaches it.

## Goal

And Then There Were None playable in `reluva` from a new game to its end,
on the AWE engine that runs Murder on the Orient Express: one engine,
with what differs between the games in a table of its own (the engine's
[README](../../README.md)), and a data viewer in `reluva-viewer` as
Orient Express has.

### What the install holds

The CDs hold an InstallShield setup: `data1.hdr`, `data1.cab` and
`data2.cab` on disc 1, `data3.cab` on disc 2. Their 5,903 files, unpacked
(`unshield x data1.cab`, with the three cabinets side by side), are the
installed game: 5,863 under `Program Executable Files`, the program
`ac.exe` and `readme.txt` under `Txt Files`, and the setup's own. 1.4 GB.
Everything is a loose file, as in Orient Express.

| Where | What | Format, as surveyed |
|---|---|---|
| `ac.exe` (593,920 bytes, built 10 October 2005) | the game | PE, Visual C++; the ANSI Windows calls (`RegisterClassExA`), Direct3D 8 |
| `granny2.dll` | Granny | 2.5.0.10 (Orient Express has 2.6.0.14) |
| `binkw32.dll` | Bink | byte for byte Orient Express's |
| `mss32.dll`, `mssmp3.asi` | Miles Sound System | another build than Orient Express's |
| `Locations/<Area>/` (49 areas) | 438 scenes, in `Base/` and in `Act<NN><x>/` for an act's own version (`%s\%s\Act%02d%c\*.ac1` in the executable) | `.ac1` |
| `Cutscenes/` (27 folders) | 95 scenes: the chapters' title cards and the still-picture cutscenes, with their backgrounds and cutouts | `.ac1` |
| `Interface/Scenes/` | the in-game menu, load, save and options screens: 4 scenes (the main menu is a location, `Locations/Mainmenu`) | `.ac1` |
| `People/<Character>/` (20 folders) | 43 `<name>_model.gr2` models and their animations: 726 Granny files in all | Granny 2 |
| `FMV/` (61), `Elements/` (12), `Inventory/` (1) | movies, video loops, an examine close-up | Bink, 800×600 or less |
| `Sound/` | 2,124 sounds, `VO/` the voices | `.acv`: WAV, 2,063 IMA ADPCM, 61 PCM; music `.mp3` |
| backgrounds | 531 pictures, 328 of them 800×600 | `.acg`: JPEG |
| cutouts, cursors, inventory, interface | 820 pictures | `.aca`: TGA, 32-bit, uncompressed |
| `Text/`, `Dialogue/` | the chapters' tables (`Chapter01` to `10`), `itemlist`, `uselist`, `charlist`, `menu`, `interface`, `errors`, the journal, 38 scripts, 421 dialogue tables and event files | `.act`: scrambled text (below) |
| `Interface/Fonts/` | Arial, Arial Narrow and four display fonts | TrueType |
| `*.scc` (544) | Visual SourceSafe's own files, left by the developers | not the game's: ignored |

### What the survey found

- **The game's frame is 800×600**, not 1024×768: the backgrounds, the
  full-screen cutouts and the movies are all that size.
- **Four extensions stand in for ordinary ones.** The executable names its
  files `.txt`, `.tga`, `.jpg` and `.wav` (`dialogue\locations\%s.txt`,
  `inventory\%s\inv%s.tga`); the install has `.act`, `.aca`, `.acg` and
  `.acv`. Its file opening (`0x4028f0`) swaps in the game's extension
  when that file is there, and turns the text's unscrambling on
  (`[0x485f20]`). `.acg` is a JPEG, `.aca` a TGA and `.acv` a WAV, as they
  are; only the text is scrambled.
- **The scrambling of `.act`** is: a u16 count of lines, then each line
  as a u16 length and its bytes XORed with 0x30. All 479 `.act` files
  read so, each ending at its last line. Inside are tab-separated tables
  in the family of Orient Express's, in 8-bit text, not UTF-16.
- **`.ac1` scenes are `.ac2`'s older form.** The same records, with the
  same tags stored back to front (`NECS` for `SCEN`), and fixed text
  buffers padded with 0xCD, but the text is 8-bit in 256-byte buffers
  where `.ac2` has UTF-16 in 512. Every tag in the 537 scenes is one
  Orient Express has, but `OLDS` (in 2); the counted records should take
  their older, shorter field lists as Orient Express's reader already
  does.
- **The tables' columns differ a little.** The dialogue tables add Tone
  and Location and have no Target; a chapter's table has Act, Event, Time
  of Day, Weather, Characters in Act, Completion Conditions, Event, Music
  and What to do, where Orient Express's `acts.txt` names a location to
  load.
- **The script language is Orient Express's, mostly.** Of the commands in
  the tables, the scripts and the event files, 49 verbs are ones Orient
  Express has; new are `cutscene` (171 uses), `inc` (79, a counter),
  `suspicion` (8), `rand` and `if`, and the condition `chapter`. Orient
  Express's `turn`, `look`, `dooropen`, `lock` and a few more aren't
  used. (A first count, from the text; phase 3 counts them properly.)
- **Systems Orient Express doesn't have:** the cutscenes as still
  pictures (`cutscene`), the journal (`interface\journalbook.tga`), the
  suspicion of each guest, chapters over acts.
- **Saves** are `.ac1` too: `Saved Games\slot%d.ac1` with a
  `slot%d.bmp` picture, `last.ac1`.
- **The window's title** is the executable's own,
  "Agatha Christie - And Then There Were None", and its settings are
  under `HKCU\Software\DreamCatcher\Agatha Christie\Agatha Christie - And
  Then There Were None\Settings`.

### What Reluva already reads, checked

Run on the unpacked install with Orient Express's code as it is:

| What | Check | Result |
|---|---|---|
| Granny files | `reluva-tool awe files-check -gr2` | all 726 read: 62 models, 665 animations; no compressed curves |
| Granny's Oodle1 | the same with `-oodle`, against the game's `granny2.dll` 2.5 in the x86 interpreter | 2,682 sections, every byte the same |
| Bink | `files-check -bik` | all 74 decoded, 44,184 frames |
| Bink, against the game's DLL | `bink-check`, three movies, 60 frames each | every pixel the same |
| Sounds | IMA ADPCM and PCM WAV | already decoded by `src/audio/` |
| Pictures | JPEG and uncompressed TGA | already decoded by `src/image/` |

## Phases

Each phase ends with its tests and its pages, as Orient Express's did.
Phases 1 and 2 are done ([README.md](README.md#where-it-stands)).

1. **Recognised, and its files read** (small; done).
   - Detection: `ac.exe` (`24bc962a1f2acfb788132d0ec198063d`, 593,920)
     and `Text/itemlist.act` (`b9f27db08f64b7340ccbaaef131bf9b8`,
     12,306), the first `kDetectionHashBytes` of each as `reluva --hash`
     gives them, and the same files as a fallback for other releases.
   - The file layer: a name asked for as `.txt`, `.tga`, `.jpg` or `.wav`
     found as `.act`, `.aca`, `.acg` or `.acv`, and `.act` unscrambled,
     as the game's `0x4028f0` does. One place in the engine, under the
     readers, so they don't know.
   - The tables read from `.act`, as 8-bit text.
   - The data viewer's Models, Movies, Pictures, Text and Sounds pages on
     this game, and `files-check` over every kind of file.
   - Tests: detection and the unscrambling, on made-up files.
2. **Scenes** (medium; done).
   - The scene reader takes `.ac1`: 8-bit text in 256-byte buffers, and
     each record's fields as this game has them (read from `ac.exe`'s
     loader, as `.ac2`'s were from `motoe.exe`'s at `0x435990`); `OLDS`.
   - Sweep all 537 scenes (`ac2-dump`, `files-check -ac2`): no record
     unread, no field past an end.
   - The layout: `Base/` and `Act<NN><x>/`, the screens in
     `Interface/Scenes/`, the cutscenes in `Cutscenes/`.
   - The data viewer's Scenes page: every scene drawn at 800×600 with its
     characters, against the game's own camera (`camera-check`, with this
     game's `granny2.dll`).
3. **What differs between the games, in a table** (medium).
   - A per-game description: the frame's size, the file names
     (`people\%s\%s_model.gr2`, `Saved Games\slot%d.ac1`), the tables'
     names and columns, the fonts, the window's title.
   - The script language: `script-check` over the whole install, then
     `cutscene`, `inc`, `suspicion`, `rand`, `if` and `chapter` from
     `ac.exe`.
   - The parts the two executables share (the dialogue manager, the
     path-finder, lighting, shadows, the interface) compared function by
     function against Orient Express's pages: where `ac.exe` does the
     same, nothing to do; where not, a branch in the table. The
     path-finder against `ac.exe`'s own in the x86 interpreter
     (`path-check`), as Orient Express's was.
4. **The game's own systems** (large).
   - Chapters, acts and the chapter tables.
   - The still-picture cutscenes and the title cards.
   - The journal, the suspicion of each guest.
   - Saves and loads in the game's `.ac1` format, with their pictures.
   - The main menu, and the in-game menu, load, save and options screens
     (`Interface/Scenes/`).
5. **From a new game to the end** (large).
   - The route found by `awe game-run -solve` on this game, then
     `game-check`: by the mouse, by the hand, saved and loaded after every
     action, wandering off it.
   - The game-files test: `awe.gamedata` steps for `attwn`, records for
     the English release.
   - Compared with recordings of the original, as Orient Express was.

## Decisions

- **One engine.** `attwn` is a game of `awe`, not an engine of its own:
  what's shared stays shared, and what differs goes in the per-game
  table (phase 3) rather than in branches across the code.
- **Orient Express's code stays as it is** for that game. Every phase runs
  its checks (`awe.engine`, the game-files steps) so nothing of it
  changes.
- **The installed game is what Reluva reads.** A player installs it with
  its own setup, or unpacks the cabinets; reading the CDs' cabinets
  directly is left for later.
- **`ac.exe` is read afresh.** It's an older build than `motoe.exe`, so
  no address carries over; Orient Express's pages say what to look for.
- **What a fact rests on is written down** as for Orient Express: the
  address in `ac.exe`, the file, or a recording (CONTRIBUTING.md).
