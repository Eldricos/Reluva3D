# And Then There Were None: scene files (`.ac1`)

> **Status:** decoded · **Code:** `src/engines/awe/formats/ac2.cpp` (`parseAc1`) · **Checked on:** the English release: all 537 scenes (`awe files-check -ac2`), each read to the end of its `DONE` · **Confidence:** from the code unless marked

## Overview

The game's scenes: 438 in `Locations/<area>/`, 95 in `Cutscenes/` (the
chapters' title cards and the still-picture cutscenes), and the in-game
menu, load, save and options screens in `Interface/Scenes/`. Each is
Orient Express's `.ac2` in an older form
([../../motoe/formats/ac2.md](../../motoe/formats/ac2.md)): the same
records, tags and counted fields, read by the same code. This page has
what differs.

## Layout

### What differs from `.ac2`

- **Text is a byte a character** (Windows-1252), in buffers of the same
  number of characters: 256 bytes where `.ac2` has 512, 32 for its short
  names. The buffers are padded with 0xCD as `.ac2`'s are.
- **No `LOAD` record.**
- **`BASE`** (`ac.exe` `0x42c79e`), a 256-byte path: the scene this one
  is built on. The loader reads that scene first (`0x42ca40`), then the
  rest of this file. While a base is loaded, this file's `FAKE`, `OUTD`
  and `NVNT` are read and dropped (`0x42c84f`, `0x42c901`, `0x42c92c`):
  the base's stay.
- **The camera** (`0x4085e0`) has the same fields, its walk grid's size
  and bytes being two of them, then only `1240`: no whiteout. Its records
  have 21 fields.
- **`OLDS`** (2 scenes), a picture of its own class (constructor
  `0x42f130`, load `0x430910`): the common object fields, then a `SPRT`
  of four fields (scroll, scroll speed, filter, `dc8`), without the
  rotation.

The other scene settings read as in `.ac2` (`0x42c740` onwards): `MUSC`
and `EVNT` as pairs of 256-byte texts, `SCEN` a 256-byte path, `SHOW`,
`FAKE`, `OUTD` and `NVNT` a u16 each, `FOOT` and `PUZZ` 32-byte names.
Any other tag is an object, made by the factory (`0x423bc0`) and read by
its class.

### Where the scenes are

| Folder | What |
|---|---|
| `Locations/<Area>/Base/` | a place's scenes, each with its camera and walk grid |
| `Locations/<Area>/Act<NN><x>/` | an act's version of a scene: its `BASE` names the scene in `Base/`, its own records are added; 221 of them, none with a camera |
| `Locations/Mainmenu/Base/` | the main menu |
| `Cutscenes/<Name>/Act<NN><x>/` | still-picture cutscenes and the chapters' title cards |
| `Interface/Scenes/` | `ingame`, `load`, `save`, `options` |

## In Reluva

`parseAc1` is `.ac2`'s reader with its text a byte a character, the two
records above, and the camera's fields counted as the game counts them
(as Orient Express's camera does too). The data viewer's Scenes page lists
the scenes of both kinds, merges an act's version over the scene its
`BASE` names, and draws them at the game's 800×600, its characters too.

## Sources and checks

- **Where the facts come from:** `ac.exe`'s scene loader (`0x42c740` to
  `0x42ca13`), its camera's (`0x4085e0`), and `OLDS`'s (`0x430910`).
  The objects' fields are read by Orient Express's lists: that every file
  reads to its end with them says their layout is the same, but each
  class's loader in `ac.exe` isn't read yet, so their meanings are
  Orient Express's *(inferred)*.
- **How it's checked:** `reluva-tool awe files-check <game folder> -ac2`:
  all 537 scenes read, none with bytes after `DONE`; all 316 cameras the
  same as the game's `granny2.dll` builds them (in the x86 interpreter);
  every act's version finds its base's camera; Orient Express's
  path-finder plans every reachable `walk` of the scripts (445). Of the
  1,825 files the scenes' objects name, 12 (10 files: `TemporaryBG.jpg`,
  `adiningcut1 copy.jpg`...) aren't in the install. `awe.unit.attwn`
  reads a scene made in the test. The Scenes page was looked at on the
  main menu, the entry hall and the kitchen in act 1B, with its
  characters.
