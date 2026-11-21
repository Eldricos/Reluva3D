# LEGO Island: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../CONTRIBUTING.md) for what may and may not
  be used.

## Sources

- **The game's files:** the English 1.0 CD and a full install made from
  it by the game's own installer.
- **The executables:** `ISLE.EXE` and `LEGO1.DLL`, their headers, import
  and export tables and version resources. `LEGO1.DLL` exports its classes
  by their C++ names, which these pages use.
- **Published write-ups:** the [legoisland.org wiki](https://legoisland.org/wiki/)
  (the SI format, the releases) and the notes of the SI editor
  [SIEdit](https://github.com/itsmattkc/SIEdit), for file layouts only:
  each fact is checked against the game's files before it's used. The
  public decompilation of the game's code isn't read
  ([PLAN.md](PLAN.md#decisions)).

## Methods

- **The executables' tables** read with a PE reader (Python's `pefile`):
  which DLLs and functions the game calls, which tells the stand-ins it
  needs.
- **File layouts** worked out by reading every record of a kind side by
  side with short scripts, tabulating the values each field takes, and
  comparing a file with its source where one is known (`JUKEBOXW.SS`, on
  the wiki). A reader is then written in Reluva, run over every file on
  the CD, and its counts compared with the script's.
- **Media checked** by rebuilding each as a standard file and checking it
  against its own format's rules (a WAV's length, a FLIC's frames, a
  Smacker file's frame sizes).

## The tools

`reluva-tool help legoisland` lists the engine's commands
(`src/engines/legoisland/tools/`).

| Command | What it does |
|---|---|
| `reluva-tool legoisland files <game folder>` | Every file in a game's folder, with its size |
| `reluva-tool legoisland si-info <file.si>` | An SI file's header, and how many objects and chunks it has |
| `reluva-tool legoisland si-list <file.si>` | Its objects, as a tree |
| `reluva-tool legoisland si-chunks <file.si> <object id>` | One object's chunks |
| `reluva-tool legoisland si-extract <file.si> <out folder> [object id]` | Its media as the files they were made from |

<!-- A table: command, what it does. Research scripts that aren't part of
Reluva go in tools/legoisland-re/, with a README. -->

## Working files not included

<!-- What the notes mention but the repository leaves out (disassembly
dumps, extracted assets), and how to make each again from your own copy. -->
