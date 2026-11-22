# LEGO Loco: the first survey (working note)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

A working note from November 2026: the first look at the CD and at an
English install of it, before any reader was written. Facts move to
`formats/` and `systems/` pages as they're
worked out there. Marks: **seen** (in the files), **guess**.

## The copy studied

One CD and an English install made from it. The installed `Loco.exe` and
`resource.RFH`/`.RFD` are byte for byte the CD's `Exe/loco.exe` and
`art-res/resource.RFH`/`.RFD`. Detection's hashes (MD5 of the first 5000
bytes, and the size):

| File | MD5 of the start | Size |
|---|---|---|
| `Exe/loco.exe` | `695dd274531337b1b600fd55f8f51482` | 1,117,696 |
| `art-res/resource.RFH` | `e7f73852368fbd30ca42b6d527b25242` | 85,647 |
| `art-res/resource.RFD` | `9a5566cc1f1ff68e828b660b2ba14cee` | 57,305,835 |

## The CD

- **Root:** an InstallShield 5 setup (`SETUP.EXE`, `setup.ins`,
  `data1.cab`, `_user1.cab`, `_sys1.cab`, `layout.bin`), `DATA.TAG`
  (LEGO Media, LEGO LOCO, version 1.18.003), `lang.dat` (the setup's
  language choice), and `ws2setup.exe` (Winsock 2's installer).
  `c-dilla1.bmp` and `c-dilla2.bmp` are pictures for C-Dilla, but nothing
  of C-Dilla was found in `loco.exe` (no section of its own, no import, no
  string) **seen**.
- **`setupdir/`:** the setup in nine languages, by Windows language id:
  Danish, German, English, Spanish, Italian, Dutch, Norwegian, Swedish,
  French.
- **`DIRECTX/`:** DirectX's own installer and drivers; not the game's.
- **`Exe/`:** `loco.exe`, the screen saver `LEGO LOCO.scr`, `LEGO.INI`.
- **`Video/locoIntr.avi`:** the intro movie.
- **`art-res/`:** the game's data, the same layout as an install's.

The CD's layout is the install's: `Exe/` and `art-res/` side by side,
`Video/` beside them. The CD's `LEGO.INI` points at `d:\loco\...`; the
install's at its own folder, with a `[Locale]` section the CD's lacks
(`Language=ENGLISH`) **seen**. `loco.exe` holds the names `GERMAN`,
`FRENCH`, `ENGLISH`, `DUTCH`, `DANISH` beside `Locale` and `Language`,
so it reads the language from there **guess**.

## `loco.exe`

- Built 6 October 1998 (PE time stamp), linker 5.10 (Visual C++ 5).
  Version 1.0.18.2, `SpecialBuild` "FINAL", internal name "LOCO Desktop
  Railway".
- Image base 0x400000, no relocations: it must be loaded there.
- Sections `.text` (0x757e0 bytes), `.rdata`, `.data` (0x828f0 bytes in
  memory, 0x7200 in the file), `.rsrc` (0x8d720 bytes). Nothing packed
  or encrypted (`.text`'s entropy is that of ordinary code).
- The C runtime is linked in: no MSVCRT import, and `RtlUnwind`,
  `RaiseException`, `SetUnhandledExceptionFilter`, TLS and the heap from
  KERNEL32, as Visual C++ 5's static runtime uses them.
- Resources: 4 bitmaps, 5 dialogs, 173 string-table blocks, one icon, the
  version. All with the neutral language: the translations are in the one
  table, chosen at run time **guess**.

### What it imports

| DLL | Count | What |
|---|---|---|
| KERNEL32 | 108 | files, heap, threads (`CreateThread`, events, critical sections), INI files, the C runtime's needs |
| USER32 | 82 | windows, messages, dialogs (`DialogBoxParamA`, `CreateDialogParamA`), scroll bars, timers, cursors (`LoadCursorFromFileA`), `DrawTextA`, `wsprintfA` |
| GDI32 | 19 | fonts (`CreateFontA`), pens and brushes, `Ellipse`, `MoveToEx`/`LineTo`, `SetPixel`, `StretchBlt` |
| WINMM | 6 | the multimedia timer (`timeSetEvent`), `PlaySoundA`, `mciSendCommandA` |
| ADVAPI32 | 6 | the registry (`SOFTWARE\Intelligent Games\LEGO LOCO\Path`), `GetUserNameA` |
| DDRAW | 1 | `DirectDrawCreate`: DirectDraw only, no Direct3D |
| DSOUND | 2 | by ordinal: 1 (`DirectSoundCreate`) and 2 **guess** |
| DPLAYX | 2 | by ordinal 1 and 4: DirectPlay, for network play (its messages speak of modems and of `SetNetwork`) |
| ole32 | 3 | `CoInitializeEx`, `CoCreateInstance` (DirectPlay's lobby or address objects **guess**) |
| comdlg32 | 2 | `GetOpenFileNameA`, `GetSaveFileNameA` |
| VERSION | 3 | its own version |
| MSVFW32 | 1 | `MCIWndRegisterClass`: the movies play in an MCI window |
| SHELL32 | 1 | `ShellExecuteA` |

Its strings hold DirectDraw's error texts for 8-bit and 4-bit palettes,
so the screen is palettized **guess**.

## `art-res/`

- **`resource.RFH` and `resource.RFD`:** the resource file, 2,522
  entries. The index is a list of entries, each a 32-bit name length
  (counting its zero), the name (a path with `\`), a 32-bit size and a
  32-bit flag. The data are in the `.RFD` one after another in the
  index's order: the sizes add up to the `.RFD`'s size exactly **seen**.
  The flag is 1 for 1,375 entries, which start with a 32-bit size larger
  than the entry, a 32-bit count, a zeroed table and then a list of
  16-bit symbols: compressed, by Huffman coding as community notes say
  **guess**. Entries with flag 0 are plain: BMPs start `BM`, `.dat` files
  are text.
- **Its entries by kind:** 1,338 `.bmp` (912 compressed), 627 `.dat`,
  355 `.wav` (269 compressed), 173 `.but`, 17 `.sav`, 6 `.lay`, 3 `.ani`,
  `ee.ini` and an icon. By folder: `clipart` 724, `sounds` 371,
  `scenery` 232, `zooms` 166, `post` 155, `people` 148, `building` 116,
  `track` 104, `roads` 91, `toybox` 87, `startup` 74, `misc` 73, `paths`
  34, `map` 28, `tutorial` 25, `layouts` 23, `trains` 21, `cursors` 16,
  `animals` 16, `vehicles` 10, `backdrop` 5, `credits` 2.
- **The pictures:** the plain ones are 8-bit BMPs with a 256-colour
  palette **seen**.
- **`.dat`:** text, one per piece (a road, a building), with sections
  `physical_occupancy` (a size and grids of the squares it fills),
  `bitmap_occupancy`, `entry_exit`, `RMBSeq`, `MaxMinifigForResource`,
  `PossibleMinifigs` (resource numbers), `Shifts`, `InsertSeq`,
  `MobileSeq` and `EasterEgg` lines, `Hotspot`, then its frames
  (`total_number_of_frames`, `number_of_frame_sets`, named frame sets),
  `-9` ending a part, and `//` comments explaining the `EasterEgg` and
  `Minifig` lines' fields **seen**.
- **`EE.INI`:** `[TimeEvents]`, lines of dates and times (from, to),
  a resource number, a frame set, a duration and a letter (`S`, `P`);
  `[LoadEvents]`, dates with a pair of resource numbers: the surprises
  that come on certain days **guess**.
- **`SAVEGAME/*.sav`:** the eight ready-made layouts. Each starts with
  16-bit words 8, 64, 48, 0, a count (443 in `SNOWBALL.SAV`, 1927 in
  `FOREST.SAV`), 0, 1, then a name (`WINTER`); after the header, rows of
  64 bytes of small numbers: a board of 64 by 48 squares **guess**. The
  minifigures' names are in it as text.
- **`ScrSaver/SS800.sav`...`SS1280.sav`:** the same format, the screen
  saver's layouts for 800, 1024, 1152 and 1280 pixels wide; the header's
  seventh word is 2 **seen**.
- **`POSTBAG/`:** postcards (`.crd`), 1,296 on the CD under `Easter/` in
  nine languages' folders; an install adds `ALBUM`, `AlbIndex`, `ATT_IN`,
  `ATT_OUT`, `SORT/BAG`, `SORT/IN`, `SORT/OUT`.
- **`backdrop/`:** 19 BMPs, the desktop behind the board.
- **`cursors/`:** `busy.ani`, `LEGO.ICO`.
- **`video/`:** `IgSpin.avi` and `legospin.avi` (the publishers' logos),
  and `music.wav`.
- **`~curr`:** the size of `FOREST.SAV` (250,048 bytes): the layout last
  played **guess**.

## The movies

Every AVI is 640 by 480, Cinepak (`cvid`) video with PCM sound (44.1
kHz, 16-bit stereo in `IgSpin.avi`) **seen**. `locoIntr.avi` is about 12
frames a second, `legospin.avi` 15. Reluva's AVI reader and Cinepak
decoder ([avi.md](../../../common/video/avi.md)) read this kind.

## Published notes found

- A Python unpacker of the resource file, published as a GitHub gist
  (neofelis2X), and threads on the Rock Raiders United forum about the
  resource file and its compression (topics 5851, 8340 and 4037). To be
  read for the layout only, each fact checked against the files.
- No decompilation of LEGO Loco is known. OpenLoco is a different game.
