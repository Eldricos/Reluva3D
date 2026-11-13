# Clue reverse-engineering tools

The working tools used to find out how Clue: Murder at Boddy Mansion works,
kept so the findings in [docs/engines/clue/](../../docs/engines/clue/) can
be checked and extended. How they were used:
[REVERSE_ENGINEERING.md](../../docs/engines/clue/REVERSE_ENGINEERING.md).

They are research scripts, not part of Reluva: not built by CMake, not
tested, written for one question at a time. They read a copy of the game
you own; nothing of the game is in this folder, and nothing they write
(disassembly caches, extracted pictures, frames of recordings) belongs in
the repository.

## Setting up

- Python 3 with `pefile`, `capstone`, `unicorn`, `numpy`, `opencv-python`
  and `pillow` (`pip install pefile capstone unicorn numpy opencv-python pillow`).
- `ffmpeg`, for frames and sound from recordings.
- Two environment variables:
  - `CLUE_GAME`: the installed game (the 2.2 patch applied): `Clue.exe`,
    `!EagleM.dll`, `ImageRLE16.dll`, `Data/Classic/`. Default `game`.
  - `CLUE_WORK`: where caches and outputs go. Default the current folder.
- Run the Python with `-I` (`python3 -I py/rx.py ...`).

The C++ programs in `harness/` are built against Reluva's libraries, from
the repository's root after a build:

    g++ -std=c++20 -O1 -Isrc -I. tools/clue-re/harness/NAME.cpp \
        build/engines/clue/libreluva_engine_clue.a build/libreluva_common.a -lpthread -o NAME

`harness.cpp` and `netgame.cpp` also need `src/backends/networking/sockets/socket_net.cpp`.

## The executables (`py/`)

| Tool | What it does |
|---|---|
| `rx.py` | `Clue.exe`: `str <regex>` strings and their addresses; `ref <hex>` instructions using a value; `dis <hex> <n>` disassembly; `calls`, `func`, `dword`. Builds `insns.pkl` (every instruction) in `CLUE_WORK` the first time. |
| `edisasm.py`, `edis.py` | `!EagleM.dll`: build `eagle.pkl`, then `edis.py <hex> <n>` disassembles from an address. |
| `dll.py` | `ImageRLE16.dll`: `dll.py <hex> <n>`. |
| `near.py` | For each `UI:` resource name in `Clue.exe`, the small numbers pushed near where it's used (positions, ids). |
| `callers.py`, `xref.py` | Who calls a function; calls into imports. |
| `peimp.py` | An executable's imports, by library. |

## Running the code (`emu/`)

| Tool | What it does |
|---|---|
| `run.py` | Runs `Clue.exe`'s game-screen set-up (0x435050) and layout in Unicorn, with EagleM's window calls, MFC's `CString`, SmartHeap and USER32 stood in for, and prints every window's place. Needs `sizes.txt` (the pictures' sizes: `harness/uisizes` writes it) in `CLUE_WORK`. |

## The data files (`py/` and `harness/`)

| Tool | What it does |
|---|---|
| `ddrv.py`, `unpack.py` | Read the `DDRV` archives (packed or not). |
| `anim.py`, `eimg.py`, `eimg2.py`, `resi.py`, `lvl.py`, `avi.py` | Early readers of the animation, image, dialog, level and video formats (Reluva's own readers replaced them). |
| `board.py` | The board's rooms from the level's floors. |
| `proto.py`, `proto2.py`, `proto3.py` | Early renderings of the house, before Reluva drew it. |
| `harness/uisizes` | The sizes of named pictures (for `emu/run.py`). |
| `harness/animoff`, `fpsflags`, `partflags`, `fkeys` | Animations: parts' offsets; every animation's speed and flags; parts' flags; frames' keys. |
| `harness/timings`, `objunk`, `objinfo`, `types`, `states` | Objects: their keys ("timings"), the record's starting state and frame, types, states. |
| `harness/spots`, `spots2` | The hot spots: rooms, boxes, how their outline covers their picture. |
| `harness/resiraw`, `resictl`, `dlg`, `dlgctl` | `dialogInfo.res`: dialogs' headers, controls' fields. |
| `harness/dumpimg` | Writes a resource's picture as a PPM (for matching recordings). |
| `harness/tickerlines` | Every ticker sentence drawn in the ticker's font (for reading recordings). |
| `harness/fonts`, `glyph*`, `sheet` | The fonts and their glyphs. |
| `harness/doors*`, `bd`, `near`, `rel`, `boxoff`, `fr`, `tim`, `ftim`, `levels*`, `wholehouse`, `lookrender`, `sameview`, `idbuf*`, `windows`, `tickone` | Questions about the board, the drawing order, boxes, lighting and the house's look, one each. |
| `harness/harness` | Drives Reluva headless by a script of keys, clicks and waits, saving screenshots: `harness clue <game> <out> wait:2 key:x click:320,96 shot:name ...` |
| `harness/fullgame`, `netgame` | Whole games, alone and over the network, without a display. |
| `harness/roomwalk` | Walks about a room: the ways to its cells from where the suspect stands, and the walk the `RoomWalk` command plays (`roomwalk <game> <room>`). |

## Recordings (`py/`)

| Tool | What it does |
|---|---|
| `soundfind.py` | Where each of the game's sounds plays in a recording: the recording's sound as 8 kHz mono 16-bit (`ffmpeg -i rec.mp4 -ac 1 -ar 8000 -f s16le rec.raw`) cross-correlated with each WAV (likewise, in `wav8/`). |
| `ratefit.py` | A sound's playback rate at a time in a recording (the game plays its sounds at 65/64). |
| `phat.py` | Whitened cross-correlation, for sounds under music. |
| `escfind.py`, `winfind.py` | Where the Esc menu and the windows over the game are in a recording's frames. |
| `locate.py`, `locate2.py`, `anchors.py`, `drawer.py`, `hudlocate.py`, `mappawns.py` | Positions of the house, the bar's pieces, the drawer and the pawns on the board from above, in frames. |
| `lightfit.py` | Fitting the lighting's shades to frames. |
| `tickmatch.py`, `tickmatch2.py`, `tsheets.py`, `sheet.py` | Reading the ticker: matching its strip against `harness/tickerlines`' sentences, or stacking strips into sheets to read by eye. |

Frames are cut to the game's 640 × 480 first: for a 1280 × 720 recording
of the game at 1.5 times, `ffmpeg -i rec.mp4 -vf "fps=1,crop=960:720:160:0,scale=640:480" f_%05d.png`.
Check each recording's crop by where the bar's ornaments fall (left at
0, 413; right at 594, 413).
