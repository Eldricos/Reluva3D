# The Clue: Murder at Boddy Mansion engine

Hasbro Interactive's Clue, developed by EAI. Engine id `clue`, in
`src/engines/clue/`. This page is the overview: what the game is made of,
where it stands and how to use it. How Reluva goes about it is in
[PLAN.md](PLAN.md).

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| Clue: Murder at Boddy Mansion (in the UK, Cluedo: Murder at Blackwell Grange) | `clue`, `clue-uk` | Hasbro Interactive (developed by EAI), 1998 | Windows | 2.0 (CD) and 2.2 (patch), US or UK data, an install or the CD | Playable, being checked against the original |

### What the game is made of

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

## Where it stands

Every phase of the [plan](PLAN.md) is done: detection and a data viewer,
the mansion, video, the interface, the game, the computer players, the
rest (hot spots, weather, saved games, preferences) and network play.
Still to do: the mansion, the game and the rest to be checked against
the original, the computer players to be compared with more of the
original's games, and network play to be tried between real machines.

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

## This folder

| Page | What's in it |
|---|---|
| This page | The game's files and releases, where it stands, the data viewer |
| [formats/](formats/) | The game's files: [resource files](formats/res.md), [`dialogInfo.res`](formats/resi.md), [objects and the mansion](formats/level.md), [the board](formats/board.md), [videos](formats/videos.md) |
| [systems/](systems/) | How the game behaves, and how Reluva does it: [playing](systems/playing.md), [the screen](systems/screen.md), [the computer players](systems/ai.md), [hot spots, weather and sounds](systems/sounds.md), [music](systems/music.md), [preferences](systems/preferences.md), [saved games](systems/saves.md), [network play](systems/network.md) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check in a playthrough |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the recordings of the original |
| [PLAN.md](PLAN.md) | The approach, and the plan's phases with what each did |

## Tests

The unit tests, the basic engine test and the game-files test, what they
cover, and what to check by hand in a playthrough: [TESTING.md](TESTING.md).
