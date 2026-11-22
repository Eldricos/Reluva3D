# LEGO Loco: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../CONTRIBUTING.md) for what may and may not
  be used.

## Sources

- **The game's files:** the CD (version 1.0.18.2) and an English install
  made from it, November 2026; what they hold is in
  [the first survey](research/first-survey.md), with the hashes of the
  files detection uses.
- **The executable:** `loco.exe`'s headers, sections, imports and
  resources, read with a PE parser. Its code is to be read with Reluva's
  disassembler and followed running in the x86 emulator
  (`reluva-tool x86-run`).
- **Published notes:** a Python unpacker of the resource file
  (neofelis2X's GitHub gist) and the Rock Raiders United forum's threads
  on the resource file and its compression, read for the layout only. No
  code is taken from them, and each fact from them is checked against the
  CD's files.
- **Public documentation:** Microsoft's Windows API and DirectX 5
  documentation, for what the executable calls.

## Methods

- **The data files:** their layouts found by comparing entries of the
  same kind, and checked by reading every entry of the CD's resource file.
- **The executable:** read where the data alone can't say (a field's
  meaning, the compression), each fact given with its address in
  `loco.exe`.

## The tools

`reluva-tool help legoloco` lists the engine's commands
(`src/engines/legoloco/tools/`).

| Command | What it does |
|---|---|
| `reluva-tool legoloco files <game folder>` | Lists every file in a game's folder, with its size |
| `reluva-tool legoloco rf-list <folder>` | Lists the resource file's entries, unpacking each to check it |
| `reluva-tool legoloco dat <folder> [name.dat]` | Prints an object definition, or reads every one and lists any that fail |
| `reluva-tool legoloco sav <file.sav>` | Prints a board; given a game folder, a board in the resource file, or reads every board of the copy |
| `reluva-tool legoloco ee <folder>` | Prints `EE.INI`'s date-based surprises |
| `reluva-tool legoloco lay <folder>` | Prints the area arrangements |
| `reluva-tool legoloco run <folder> -save <folder> [-log] [-limit n]` | Runs `loco.exe` in the emulator and reports how far it gets |
| `reluva-tool legoloco view <folder> <page> <item> <out.png> [keys]` | Draws a data viewer page into a PNG, without a window |
| `reluva-tool legoloco rf-extract <folder> <output folder> [name...]` | Writes the resource file's entries out, unpacked |

`loco.exe` itself was disassembled with GNU `objdump` (`objdump -d -M
intel`), which reads 32-bit Windows programs.

## Working files not included

- **The disassembly of `loco.exe`:** made again from your own copy with
  `objdump -d -M intel Exe/Loco.exe`.
- **The resource file's entries, unpacked:** made again with
  `reluva-tool legoloco rf-extract <game folder> <output folder>`.
