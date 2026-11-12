# How Reluva's Star Trek: Generations support was worked out

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
  [docs/common/cinepak.md](../../common/cinepak.md).
- See [CONTRIBUTING.md](../../../CONTRIBUTING.md) for what may and may not be
  used.

## Sources

All of it was worked out on the 2 CD Windows release:

- **The game's files**, read directly, for the formats: the archives, fonts,
  overlays, sprite sets, mission scripts, levels and the data files of the
  story, the captain's menu, the bridge and space combat.
- **The executable, `sgens.exe`, disassembled with Ghidra**, for the
  behaviour: how the scripts run, how the levels are drawn, the enemies' AI.
  The addresses in the code's comments (like `0x4A7C91`) are the original's
  functions each piece follows.
- **A few details are inferred** rather than read: from the game's pixels
  (the sky's height, `formats/level.cpp`, `guessSkyHeight`) or from the
  scripts alone. [UNCONFIRMED.md](UNCONFIRMED.md) lists them.

## The notes

STG-Engine kept its notes in one file, `doc/formats.md`. In Reluva it is
[formats/](formats/README.md), a page per format. "Decoded" there means a
decoder exists in `src/engines/stgenerations` and has been checked against
the game's own files; "partial" and "unknown" are working notes.

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

## Checking the work

- **The game's own files:** all 18 archives, every picture, level, sound
  and movie of the 2 CD English release read; one movie checked against
  FFmpeg to the pixel ([TESTING.md](TESTING.md#checked-on-the-games-own-files)).
- **Against STG-Engine:** a save made in STG-Engine loads in Reluva and
  shows the same 3D view, pixel for pixel
  ([TESTING.md](TESTING.md#checked-on-the-games-own-files)).
- **Against the original:** what hasn't been compared with the running game
  yet is in [UNCONFIRMED.md](UNCONFIRMED.md).
