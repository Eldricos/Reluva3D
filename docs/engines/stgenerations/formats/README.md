# Star Trek: Generations: file formats

Notes from reverse engineering the 2 CD Windows release. "Decoded" means a
decoder exists in `src/engines/stgenerations` and has been checked against real files.
"Partial" and "unknown" are working notes. All values are little-endian.

They came to Reluva with the engine, from STG-Engine's `doc/formats.md`, a page per format here; the notes they were drawn from are in [research/](../research/README.md). Everything was worked out from the original: the formats by reading the game's files, the behaviour by disassembling `sgens.exe` (with Ghidra, and Capstone for searches). The addresses in the code's comments (like `0x4A7C91`) are the original's functions each piece follows.

- [containers.md](containers.md): Archives and standard files
- [fonts.md](fonts.md): Fonts (.FNT)
- [overlays.md](overlays.md): Overlays (.OVR)
- [sprites.md](sprites.md): Sprite sets (.GS2, .GSF)
- [scripts.md](scripts.md): Mission scripts (GAME.BIN)
- [levels.md](levels.md): Levels (GAME.LV2, an away mission's 3D map: its walls, floors, textures and actors)
- [story.md](story.md): The story (STATES.INI)
- [menu.md](menu.md): The captain's menu (OPTIONS.DAT)
- [bridge.md](bridge.md): The bridge (STELLAR.DAT)
- [combat.md](combat.md): Space combat (COMBAT.DAT)
