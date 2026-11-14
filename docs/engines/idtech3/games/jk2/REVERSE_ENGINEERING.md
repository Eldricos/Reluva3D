# Jedi Outcast: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and
  may not be used.
- **Jedi Outcast's GPL source** (Raven's 2013 release) may be read as a
  guide to shapes and names, never copied. It isn't 1.02a's code, so
  every table, number and layout is checked against the retail files.

## Sources

- The English Windows 1.02a files ([README.md](README.md#releases)): the
  paks' listings and file headers, `version.inf`, the executables' version
  strings, and the DLL's exports and imports.

## Methods

The paks were listed with `unzip -l`; each file type's first bytes were
read for its magic and version (RBSP 1, `2LGM`/`2LGA` 6, `IBI`, `ROFF` 2,
Quake III's QVM magic `0x12721444`), and the RBSP header's lump table for
its count (18). `objdump -p` gave `jk2gamex86.dll`'s exports and imported
DLLs.

## The tools

`reluva-tool help idtech3` lists the engine's commands
(`src/engines/idtech3/tools/`); Elite Force's
[REVERSE_ENGINEERING.md](../stvef/REVERSE_ENGINEERING.md#the-tools) says
what each does. They work on any id Tech 3 game's QVMs and DLLs.

## Working files not included

None yet.
