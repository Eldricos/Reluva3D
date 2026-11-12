# Star Trek: Generations: how it was worked out

Reluva runs Star Trek: Generations from the player's own copy, with none of
the original's code in it. This page says how the facts behind that were
found, so they can be checked again or extended. The facts themselves are in
[formats/](formats/README.md), each page with what was decoded and where it
is in the code. The engine came to Reluva from
[STG-Engine](https://github.com/Eldricos/STG-Engine), where this work was
done; [PORTING.md](PORTING.md) says how the code came across.

## Ground rules

- **All of Reluva's code is new.** The game's file formats and behaviour were
  worked out from the game itself; nothing is taken from its code.
- **The retail game is studied as a legally owned copy, for
  interoperability.** The game's code, text and assets stay in the player's
  copy: nothing of them is in this repository.
- **Published format write-ups are read for facts only.** Cinepak and AVI,
  shared with other games, follow the descriptions named in
  [docs/common/video/cinepak.md](../../common/video/cinepak.md).
- See [CONTRIBUTING.md](../../../CONTRIBUTING.md) for what may and may not be
  used.

## Sources

All of it was worked out on the 2 CD Windows release:

- **The game's files**, read directly, for the formats: the archives, fonts,
  overlays, sprite sets, mission scripts, levels and the data files of the
  story, the captain's menu, the bridge and space combat.
- **The executable, `sgens.exe`, disassembled with Ghidra**, and searched
  with small Python scripts on the Capstone disassembler (every instruction
  that reads or writes a given field, the callers of a function, the
  references to an address or constant, a table dumped), for the
  behaviour: how the scripts run, how the levels are drawn, the enemies' AI.
  The addresses in the code's comments (like `0x4A7C91`) are the original's
  functions each piece follows.
- **A few details are inferred** rather than read: from the game's pixels
  (the sky's height, `formats/level.cpp`, `guessSkyHeight`) or from the
  scripts alone. [UNCONFIRMED.md](UNCONFIRMED.md) lists them.

## Methods

### The notes

The notes read off `sgens.exe`, a note per part of the game, are in
[research/](research/README.md), as they were written. STG-Engine kept
what they found in one file, `doc/formats.md`. In Reluva it is
[formats/](formats/README.md), a page per format. "Decoded" there means a
decoder exists in `src/engines/stgenerations` and has been checked against
the game's own files; "partial" and "unknown" are working notes.

### Checking the work

- **The game's own files:** all 18 archives, every picture, level, sound
  and movie of the 2 CD English release read; one movie checked against
  FFmpeg to the pixel ([TESTING.md](TESTING.md#checked-on-the-games-own-files)).
- **Against STG-Engine:** a save made in STG-Engine loads in Reluva and
  shows the same 3D view, pixel for pixel
  ([TESTING.md](TESTING.md#checked-on-the-games-own-files)).
- **Against the original:** what hasn't been compared with the running game
  yet is in [UNCONFIRMED.md](UNCONFIRMED.md).

### What went wrong, and what it taught

- **Notes can be wrong.** An early note said clicking the combat
  viewscreen enlarged it; reading the handler properly showed it steers.
  The notes are marked by confidence, and anything marked M or L is
  checked again before it's built on ([research/](research/README.md#corrected-since)
  lists what was corrected).
- **A guess shows up as a difference.** The overlays' frame heights were
  guessed at first. Tracing them showed the view overlays are one frame,
  and that a value read as a frame number was a drawing mode; that value
  had been hiding the helmet and other overlays.
- **Dead code exists.** The routine that starts a ship cloaking is never
  called, and the TACTICS scripts in COMBAT.DAT are never loaded. Finding
  where something is called, or that it isn't, matters as much as reading
  the function.
- **Checked across all the data.** A reading is tested against every
  mission, not one: reading the combat headings as "90 faces -x" makes 66
  of the 68 first-wave enemies face the Enterprise, against 26 the other
  way, which settled it.
- **Some things can't be settled from the code alone.** When the code
  leaves a choice open, the engine takes the reading that fits the
  evidence, and [UNCONFIRMED.md](UNCONFIRMED.md) lists it.

## The tools

`reluva-tool stgenerations <command>` (`reluva-tool help stgenerations` lists
them):

| Command | What it does |
|---|---|
| `files <game folder>` | Every file in a game's folder, with its size and detection signature |
| `parts <folder>` | The parts of the game a folder holds: the installed game and each CD |
| `probe <file or folder> [bytes]` | What each file holds: archives and their members, pictures, sounds, movies, scripts; a hex dump of the rest |
| `extract <archive> <folder> [pattern]` | Files taken out of one of the game's archives (e.g. `FONTS/*`) |
| `script <GAME.BIN>` | A mission script's objects, states and resources, and its disassembly |
| `archives`, `pictures`, `levels`, `sounds`, `movies` | Every one of the game's archives, pictures, levels, sounds or movies read or decoded, with checksums |
| `play <game folder> [options]` | The game run without a window: keys and clicks at given times, pictures of the screen, each frame's checksum |

## Working files not included

- **The disassembly dumps** the notes were read from (listings of
  `sgens.exe`'s functions), which are the original's code. The notes
  themselves are in [research/](research/README.md).
- **The search scripts** (Python, on Capstone) and Ghidra's project. To make
  them again, load `sgens.exe` (1571328 bytes, MD5
  `5044cd4e8cd2ef067dc79d86b38cc44d`) into Ghidra, or disassemble its code
  section with Capstone in 32-bit x86 mode, and search it for the
  addresses the pages give.
- **The game's own files** used to check each format, which stay in the
  player's copy ([CONTRIBUTING.md](../../../CONTRIBUTING.md)).
