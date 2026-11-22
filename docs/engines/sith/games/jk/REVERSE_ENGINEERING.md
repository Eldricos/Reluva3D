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
- **Comparing every file of a kind:** for each format, a layout taken
  from a few files, then checked on all of them, each file's size
  accounted for to the byte; fields that never change, or change with
  no pattern, noted as such. A field's meaning is then taken from what
  it does across the files (colour 0 in transparent textures, the
  pictures' x and y on the screen), and marked as inferred.
- **Drawing it:** pictures drawn with each palette they might take, and
  placed where their x and y say on a menu's background, to see which
  fits; fonts drawn with and without a field as their spacing.

## The tools

`reluva-tool help sith` lists the engine's commands
(`src/engines/sith/tools/`).

| Command | What it does |
|---|---|
| `reluva-tool sith files <game folder>` | Every file in a folder, with its size |
| `reluva-tool sith gob <archive.gob>` | A `.gob` archive's index |
| `reluva-tool sith formats <install> [-q]` | Every colormap, picture, material, font, string table, sound, sound class and episode in the install's archives read, a line each, and a count of each kind |
| `reluva-tool sith strings <install> <file.uni>` | A string table's keys and strings |
| `reluva-tool sith episode <install>` | Each episode's steps, and the CD each cut scene was found on |
| `reluva-tool sith movies <install> [-all]` | The cut scenes on the CDs found; with `-all` every frame and its sound decoded |
| `reluva-tool sith page <install> <page> [keys...] <out.png>` | One of the data viewer's pages drawn after the keys given, as a PNG |
| `reluva-tool sith folders <install>` | The game recognised in an install, and where each CD was found |

## Working files not included

None yet.
