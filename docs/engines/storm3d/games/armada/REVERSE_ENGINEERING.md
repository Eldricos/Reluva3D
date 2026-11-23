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
- **The game's files:** each kind's layout worked out from the files
  themselves, then a reader checked against every file of that kind;
  each format page says how.
- **The executables:** `reluva-tool pe-info` and `objdump -p` for their
  headers, imports and exports. Later, Reluva's disassembler
  (`src/emulation/x86/disasm.h`) for the code, and the original run in
  Reluva's x86 emulator to trace what it does ([PLAN.md](PLAN.md)).
- **The viewer's pages, seen:** `reluva-tool storm3d viewer-shot` saves a
  2D page as PNG. The Models page needs a 3D renderer: `reluva-viewer` was
  run under Xvfb on Mesa's software Vulkan driver, keys sent with
  `xdotool` and the screen taken with ImageMagick's `import`.

## The tools

`reluva-tool help storm3d` lists the engine's commands
(`src/engines/storm3d/tools/`).

| Command | What it does |
|---|---|
| `reluva-tool storm3d files <game folder>` | Lists every file in the game's folder, with its size |
| `reluva-tool storm3d zfs-list <file.zfs>` | A ZFS archive's tables ([formats/zfs.md](formats/zfs.md)) |
| `reluva-tool storm3d zfs-extract <file.zfs> <output dir> [name filter]` | Its files, decoded |
| `reluva-tool storm3d odf <game folder> <name.odf>` | An object definition with its includes ([formats/odf.md](formats/odf.md)) |
| `reluva-tool storm3d odf-check <game folder>` | Reads every object definition |
| `reluva-tool storm3d config <game folder> <file>` | A settings file's values ([formats/config.md](formats/config.md)) |
| `reluva-tool storm3d config-check <game folder>` | Reads every settings file |
| `reluva-tool storm3d ldl <game folder> <file.ldl> [output file]` | An `.ldl` file decoded ([formats/ldl.md](formats/ldl.md)) |
| `reluva-tool storm3d input-map <Input.map>` | The key and mouse bindings ([formats/input-map.md](formats/input-map.md)) |
| `reluva-tool storm3d image-check <game folder>` | Decodes every picture ([formats/textures.md](formats/textures.md)) |
| `reluva-tool storm3d image <game folder> <name> <out.png>` | Writes one picture as PNG |
| `reluva-tool storm3d sound-check <game folder>` | Decodes every sound ([formats/sounds.md](formats/sounds.md)) |
| `reluva-tool storm3d movie-check <game folder> [name filter]` | Decodes every movie ([formats/movies.md](formats/movies.md)) |
| `reluva-tool storm3d movie-frame <game folder> <name> <frame> <out.png>` | Writes one frame of a movie as PNG |
| `reluva-tool storm3d spr <game folder> <table.spr>` | A sprite table ([formats/spr.md](formats/spr.md)) |
| `reluva-tool storm3d spr-check <game folder>` | Reads every sprite table |
| `reluva-tool storm3d sod <game folder> <name.sod>` | A model's nodes, meshes and animations ([formats/sod.md](formats/sod.md)) |
| `reluva-tool storm3d sod-check <game folder>` | Reads every model |
| `reluva-tool storm3d viewer-shot <game folder> <out.png> [key...]` | The data viewer run without a window, keys pressed, its last screen as PNG |

## Working files not included

None yet.
