# Clue: Murder at Boddy Mansion (1998)

Hasbro Interactive's Clue, developed by EAI. Engine id `clue`, in
`src/engines/clue/`. This page is the overview: what the game is made of,
how Reluva goes about it, where it stands and how to use it.

## This folder

| Page | What's in it |
|---|---|
| This page | The game's files and releases, the approach, where it stands, the data viewer |
| [formats/](formats/) | The game's files: [resource files](formats/res.md), [`dialogInfo.res`](formats/resi.md), [objects and the mansion](formats/level.md), [the board](formats/board.md), [videos](formats/videos.md) |
| [systems/](systems/) | How the game behaves, and how Reluva does it: [playing](systems/playing.md), [the screen](systems/screen.md), [the computer players](systems/ai.md), [hot spots, weather and sounds](systems/sounds.md), [music](systems/music.md), [preferences](systems/preferences.md), [saved games](systems/saves.md), [network play](systems/network.md) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check in a playthrough |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the recordings of the original |

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

`Clue.exe` gives the version; the language's files give the language. The
UK's have one file the US's don't: `Sounds/end turn.wav` (175,220 bytes,
MD5 of its first 5000 bytes `95881a34ccc9579a3d7bafa45ccaa8b6`), a spoken
"end turn" that both versions of `Clue.exe` name in their table of voices
(the string at 0x45ECE0) but never play (see the voices in [systems/playing.md](systems/playing.md#in-the-game)). So an install (`Data/Classic`) with it is the
UK's, without it the US's; the CD (`Data/US`, `Data/UK`) is found as both,
to play either. A `Clue.exe` not in the table is still found, as an
unknown release, in the language its files are.

The UK release is Cluedo: its own files say so ("Are you sure you want to
exit Cluedo?" in its `dialogInfo.res`, "Cluedo" on its menus) and
`Clue.exe` names itself "Clue / Cluedo". Reluva names it as it was sold,
Cluedo: Murder at Blackwell Grange, in the launcher and the window's title.

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

## Where it stands

Each step can be seen working before the next.

1. **Detection and a data viewer** (done). Finds the game in an install
   folder or on the CD (US or UK data; 2.0 or 2.2). Readers for `PACK`,
   `DDRV`, both image versions and `ANIM`. Viewers for the animations
   (playing), images, sounds and text, described below. `reluva-tool`
   commands to list, extract and check the files. Unit tests on made-up
   files.
2. **The mansion** (done, to be checked against the original). Reads
   the objects (`.o`) and `clue.lvl` ([formats/level.md](formats/level.md))
   and draws the house: floors, walls, furniture and the suspects at their
   starting places, sorted by the boxes they fill, the rooms' animations
   playing. Scrolls; an overview of the whole house. The near walls
   see-through, solid or hidden.
3. **Video** (done, [formats/videos.md](formats/videos.md)). An AVI reader and a Cinepak decoder in
   `src/common/video` ([common/cinepak.md](../../common/cinepak.md)); the
   videos page plays all 44 with their sound.
4. **The interface** (done, but for the in-game panels). `dialogInfo.res`
   read ([formats/resi.md](formats/resi.md)); the game's
   fonts, dialogs, buttons, sliders and pointer. The game now starts as
   the original: the intro, the main menu over the storm, Start Game
   (locally), the six suspects' players (human with a name, or computer
   at a difficulty), the game variants, Preferences (saved), How to Play
   with its texts, Credits, Exit; then "Entering the mansion", the deal,
   and the mansion with the game's bar and the Esc menu. The in-game panels
   (cards, notepad, suggestion and accusation, disproving) come with the
   rules, as they need them.
5. **The game** (done, to be checked against the original). The board
   read from the level ([formats/board.md](formats/board.md)):
   squares, doors, starting squares, secret passages. The rules as the
   game's own texts give them (`rules.h`): the deal, turns, the die or No
   Die Roll's nine moves, moving, suggestions bringing the suspect in,
   disproving in turn, accusations, being out, the end. Played as
   described in [systems/playing.md](systems/playing.md#in-the-game): moves planned square by square
   and walked, the suggestion and accusation window, the suggestions'
   videos, disproving, the card shown, the solution, the notepad and the
   cards, the board from above, the ticker's sentences and the spoken
   lines. Hot seat: each human sees only their own cards and notes.
   Weapons are cards only: the mansion has no weapon tokens. Computer
   players at three levels (step 6). Unit tests for the board, the
   rules and whole games of computer players.
6. **Computer players** (done, to be compared with more of the
   original's games). Each keeps a notebook of who holds which card
   (`ai.h`) and deduces from what it sees; the set-up screen's three
   levels see and deduce more or less ([systems/ai.md](systems/ai.md)). Unit tests for the
   deductions; `reluva-tool clue board <clue.lvl> <games>` plays mixed
   games of the three levels on the real board. The levels are matched
   to recordings of the original at each.
7. **The rest** (done, to be checked against the original). The rooms'
   hot spots ([systems/sounds.md](systems/sounds.md)), thunder and gusts of wind with the drapes blowing,
   the fire and the fountain heard near them, thunder over the menus'
   storm; saved games ([systems/saves.md](systems/saves.md)). Saved games are Reluva's own, in eight slots as
   the original's Save window and Load Game screen have: the original's
   needn't load. The original's `Prefs.ini` is read the first time
   ([systems/preferences.md](systems/preferences.md)).
8. **Network play** (done, to be tried between real machines). Games
   on the local network and over the internet by address, in the
   original's network screens. Reluva's own protocol, not DirectPlay's:
   Reluva and the original can't play each other
   ([systems/network.md](systems/network.md)).

## Using it

The game itself, its menus and its keys: [systems/playing.md](systems/playing.md).

**F9** switches between the game and a viewer of its data (and back;
**Esc** also goes back). **Tab** switches between the viewer's pages.

- **The mansion**, first: **arrows** or a mouse drag scroll (**Shift**
  faster), **Home** goes back to the middle, **M** shows the whole house
  (click to go to a place there), **W** makes the walls nearest the camera
  see-through, solid or hidden, **O** shows or hides the black outside,
  **N** the move numbers' templates the level keeps, **F** every object's box,
  **Space** pauses the animations. The info line names what's under the
  mouse pointer, with its layer, place and box.

- **Videos**, playing with their sound: **Left/Right** the previous or
  next one, **Space** or **Enter** plays it again or stops it, **Z** the
  size.
- **Animations**, playing: **Left/Right** the previous or next one,
  **Up/Down** the previous or next file, **Space** pauses, **,/.** step a
  frame, **Z** zooms, **A** shows the anchor point (a character's feet),
  **L** one layer at a time.
- **Images**: **Left/Right** step (**PageUp/PageDown** by 10,
  **Home/End**), **Up/Down** the file, **Z** zooms, **B** changes the
  background.
- **Sounds**: the game's WAV files and the ripped music, if any.
  **Left/Right** step, **Space** or **Enter** plays or stops, **A** plays
  each as it's selected.
- **Text**: **Left/Right** the file, **Up/Down**, **PageUp/PageDown** and
  **Home/End** scroll.

`reluva --detect <folder>` lists what's found in a folder.
