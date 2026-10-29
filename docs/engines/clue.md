# Clue: Murder at Boddy Mansion (1998)

Hasbro Interactive's Clue, developed by EAI. A plan for supporting it,
and what's known so far. Formats: [formats/clue/res.md](../formats/clue/res.md).

## What the game is made of

Source: the retail CD, a clean install from it and the 2.2 patch
(October 2026).

- **`Clue.exe`**: the game, Visual C++ 5 with MFC. It uses DirectDraw
  (16-bit colour; the title screen is 640×480), DirectInput, DirectShow for its videos
  (`AviPlayer.cpp`), the Miles Sound System (`Mss32.dll`), CD audio
  through Miles' "redbook" functions, DirectPlay and the MSN Gaming Zone
  for networked games, and SmartHeap (`SHW32.dll`).
- **`!EagleM.dll`**: EAI's "Eagle" library, used by the game for
  everything below its rules: the data files (`EGDataDrive`,
  `EGDataFile`), images and animations (`EGfxImage`, `EGfxRLEImage`,
  `EGfxAnimation`), fonts, cursors, the screen, and a window system
  (`EGuiWindow`, `EGuiButton`, `EGuiListCtrl`, `EGuiDialogBox`, ...). It
  exports 1,280 C++ functions under their decorated names, so the names
  of its classes and methods are known.
- **`ImageRLE16.dll`**: draws the run-length images, clipped, flipped,
  scaled and sheared.

Data (`Data\Classic\` once installed: the CD's `LangIndepend`,
`ClueSpecific` and `US` folders together, file for file):

| Folder | |
|---|---|
| `Resource` | the mansion's rooms, floors, walls, objects and the six characters: `.res` images, `.a` animations, `.o` objects |
| `Levels/clue.lvl` | the mansion, put together from them |
| `UI` | menus, dialogs, fonts, cursors, the notepad, the ticker |
| `dialogInfo.res` (install folder) | the layout of the menus and dialogs (`RESI`) |
| `Sounds` | WAV, all PCM: effects, and each suspect's spoken lines |
| `Text` | the How to Play pages, the ticker's sentences, credits |
| `Video` | AVI, Cinepak with PCM sound: the intro, the deal, a clip for each suspect with each weapon (`CM_candle.avi` ...) and a `_prison` clip for each suspect |

The CD also has `UK` data: the same sounds and text in British English
("Miss Scarlett", "Rev. Green", "dagger" for "Miss Scarlet", "Mr. Green",
"knife"). The patch replaces `Clue.exe`,
`!EagleM.dll`, `clue.lvl`, `dialogInfo.res` and `small`/`ticker` in `UI`.

The readme describes options for translucency, dynamic lighting,
walk cycles and walls, clickable animated objects in the rooms, hot-seat
play for several people at one computer, and a "no die roll" variant
played with credits.

### Identification

| File | Version | Size | MD5 of first 5000 bytes |
|---|---|---|---|
| `Clue.exe` | 2.0 (CD) | 386,048 | `7ddb4f359cfd8827b4f7b054e95b1cba` |
| `Clue.exe` | 2.2 (patch) | 391,168 | `cb43d1d845b786193353ac221e5f0ac1` |
| `!EagleM.dll` | 2.0 | 248,832 | `b512233497bbc18ed4dd875b0b6ec556` |
| `!EagleM.dll` | 2.2 | 242,688 | `a36d7d82e6ad515aca6307cc5968fc4c` |

## Approach

Reimplement the game, as for New Worlds, rather than run `Clue.exe` in
the x86 interpreter as Elite Force's DLLs are. Elite Force's DLLs only
ask a small engine interface of their host; `Clue.exe` is a whole Windows
program built on MFC, DirectDraw, DirectShow, DirectInput, DirectPlay,
Miles and SmartHeap, which would all need standing in for. The game's
rules are the board game's, which are public, and its look and content
are data-driven: the mansion, the menus and dialogs, the text and the
videos are all in files.

Where behaviour isn't in the data (the computer players' play, timings,
which sound plays when), it's found by watching the original run and,
where needed, by reading `Clue.exe`, as CONTRIBUTING.md allows. Eagle's
exported names give the structure.

The engine goes in `src/engines/clue/`, with the decoders likely to be
of use to other games (AVI, Cinepak) in `src/common/`.

## Plan

Each step can be seen working before the next.

1. **Detection and a data viewer.** Find the game in an install folder
   or on the CD (US or UK data; 2.0 or 2.2). Readers for `PACK`, `DDRV`,
   both image layouts and `ANIM`. A viewer as New Worlds has: images,
   animations playing, sounds, text. `reluva-tool` commands to list and
   extract them. Unit tests on made-up files.
2. **The mansion.** Read `.o` objects and `clue.lvl`, and draw the
   rooms: floor tiles, walls, objects in depth order, characters standing
   in them. Scroll around the house. The options for translucency,
   lighting and walls.
3. **Video.** An AVI reader and a Cinepak decoder, with the sound
   through the mixer. Play the intro, the deal and the suggestion clips.
4. **The interface.** The `RESI` layouts: windows, buttons, lists, text
   boxes, the bitmap fonts and cursors. The main menu, game setup,
   preferences, How to Play, credits, the notepad, the cards and the
   ticker.
5. **The game.** The board: squares, doors, secret passages, from the
   level. Turns, the die or credits, moving (with walk cycles),
   suggestions (moving the suspect and weapon into the room), disproving,
   accusations and the end. Hot-seat play with hidden cards. The spoken
   lines and the ticker's sentences.
6. **Computer players.** They note what they learn and deduce from it as
   the originals do; their choices compared with the original's over
   recorded games.
7. **The rest.** The clickable objects' animations, thunder and other
   ambient sounds, music, saved games (`SaveGames`) and `Prefs.ini`.
8. **Network play** (later, if wanted). DirectPlay's protocol between
   Reluva and Reluva at least; the Gaming Zone is gone.

## Open questions

- **Music:** `Clue.exe` plays CD audio tracks. If the CD has them, they
  would need to be ripped (to WAV, FLAC or MP3, say `track02.flac`),
  as ScummVM asks for some games.
- **Reference recordings** of the original running (on Windows 98 or
  later in a virtual machine, or under Wine) are the best source for
  timings and the computer players' behaviour.
