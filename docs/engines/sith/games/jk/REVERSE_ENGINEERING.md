# Jedi Knight: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and may not
  be used.

## Sources

- **The game's files:** the US retail release, both CDs and an install
  from them (`JK.EXE` 1,387,520 bytes, whole-file MD5
  `1cbc984348811f40d0d520a1d43e9106`).
- **`JK.EXE`:** its imports and strings so far; no disassembly yet.
- **The readme** (`README.TXT`, 9 September 1997) and the troubleshooting
  guide (`Jedi.doc`).
- **Published write-ups** of the formats by the game's editing community,
  for facts only, each checked against the game's files.
- **Not used:** decompilation-based rebuilds of the game (OpenJKDF2 and
  the like), for facts or code ([PLAN.md](PLAN.md#decisions)).

## Methods

- **Comparing files:** the install's archives with the CDs', by
  whole-file MD5, to learn what the installer copies; the two CDs' files
  with each other.
- **Reading the executable's headers:** its sections and imports
  (DirectX, `smackw32.dll`, WINMM), with a PE reader.

## The tools

`reluva-tool help sith` lists the engine's commands
(`src/engines/sith/tools/`).

| Command | What it does |
|---|---|
| `reluva-tool sith files <game folder>` | Every file in a folder, with its size |
| `reluva-tool sith folders <install>` | The game recognised in an install, and where each CD was found |

## Working files not included

None yet.
