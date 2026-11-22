# Star Trek: Armada: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and may not
  be used.

## Sources

- **The game's files:** the English retail CD and an install from it
  ([formats/install.md](formats/install.md)).
- **The executables:** `Armada.exe` (2,383,917 bytes, of 10 March 2000)
  and the missions' and the AI's DLLs: their headers, imports, exports and
  strings so far. Its exports name the functions the missions call, and
  its run-time type information names 459 of its classes.
- **The game's own documents:** the manual (`help/Armada.pdf`), the readme,
  and the notes the developers left in the install (`Scheduler_README.txt`,
  the comments in `RTS_CFG.h` and the `.aip` files).
- **Published work, read for layouts only, every field checked against
  the game's files:** stau (MIT), a reader of SOD models and ODFs;
  SomaZ's Blender_ST_Armada_Tools (GPL-2.0), SOD models for Armada and
  Armada II; Open76 (GPL-3.0), Interstate '76's ZFS archives. No code is
  taken from them.
- **Not used:** any source code said to be Activision's, for Armada,
  Battlezone, Interstate '76 or Storm3D. A download on ModDB described as
  Battlezone II's source, and Dark Reign 2's source, are left alone too:
  whether they may be used isn't clear, and they're other programs.

## Methods

- **Listing:** every file's size and kind, over the whole install and CD.
- **The executables:** `reluva-tool pe-info` and `objdump -p` for their
  headers, imports and exports. Later, Reluva's disassembler
  (`src/emulation/x86/disasm.h`) for the code, and the original run in
  Reluva's x86 emulator to trace what it does.

## The tools

`reluva-tool help storm3d` lists the engine's commands
(`src/engines/storm3d/tools/`).

| Command | What it does |
|---|---|
| `reluva-tool storm3d files <game folder>` | Lists every file in the game's folder, with its size |

## Working files not included

None yet.
