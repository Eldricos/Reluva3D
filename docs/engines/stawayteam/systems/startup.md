# Star Trek: Away Team: the game's start

> **Status:** partial · **Code:** `src/engines/stawayteam/ui/movies.cpp`, `src/engines/stawayteam/ui/main_menu.cpp` · **Checked against:** the code only; Reluva's run on the US English retail install's files · **Confidence:** from the code unless marked

## Overview

As the game starts, it plays four movies, the publishers' and the
developer's logos and then the introduction, each of which a key
skips; then it shows the main menu, its starry background moving
behind the title and its buttons, with the startup song.

## How it works

### The opening movies

In `Star Trek.exe`: the game's object is made with a table of the
movies' names at `0x63f860` (4 of them, given at `0x4f4b5c`), and the
startup code plays each in turn (`0x4d51b6`):

| Movie | File | Picture | Length |
|---|---|---|---|
| Activision | `DATA/Movies/Activision.bik` | 640x480, 15 frames a second | 10.1 s |
| Paramount | `DATA/Movies/Paramount.bik` | 640x480, 15 | 16.4 s |
| Reflexive | `DATA/Movies/Reflexive.bik` | 640x400, 22 | 10.4 s |
| Intro | `DATA/Movies/Intro.bik` | 640x480, 30 | 103.4 s |

The movies are loose files in the install, not in `DATA.dat`: `0x4d4da0`
makes each name `Movies/<name>.bik` (`0x63e83c`) and opens it, and when
it can't, goes on to the next (`0x4d4e14`).

**Skipping.** While a movie plays, the player looks every frame
(`0x4d4e83`) for a key newly pressed in the program's table of keys
(`0x4e8de0`: a key's "pressed" bit, set as Windows reports it down,
`0x4e8ec0`); one ends the movie, the table's pressed bits cleared
(`0x4e8c80`). The mouse's buttons only change the mouse's state
(`0x5fa9ea`), so a click doesn't skip it.
While the game isn't the window in front (the flag at the
player's `+0x64`) the movie is paused *(inferred)*.

**The startup sound.** After the movies the code plays `Startup.wav`
(`0x63f98c`, by `0x4e40e0` at `0x4d51e1`), but there's no such file in
the install or in `DATA.dat`: nothing is heard.

### The main menu's screen

`Rules.txt`'s main menu (`Custom Rules`, `Menu`, the one with `Menu
Name=Main Menu`) lists what's on its screen, its "nernies", each at a
position:

| Nernie | Position | What it shows |
|---|---|---|
| `CMovieNernie` | 0,91 | `Main Screen Background.bik`: the starry background, 640x280, 26.7 s |
| `CMovieNernie` | 127,30 | `STAT Logo for Main.bik`: the title, 389x61, 5.3 s |
| `CImageNernie` | 8,439 | `Main Menu/Reflexive Logo`: a picture (`Cache/Interface/Screens/Main Menu/Reflexive Logo.frm16`) |

The menu's buttons, over it, are in [menus.md](menus.md#the-main-menu).
Each movie nernie plays its movie over and over: it opens it with the
player's loop flag set (`0x51139d`), so the player goes on from its last
frame to its first rather than stopping (`0x44a6de`). Neither has a
sound track (their Bink headers give none), so they're silent. The
song is `Rules.txt`'s `Startup Song`, `Startup.ogg` in `DATA.dat`'s
`Music/`, played once: as it ends the first mission's Random Songs
follow ([maps.md](maps.md#the-music)).

## In Reluva

- **The opening movies:** `playMovie` (`src/engines/stawayteam/ui/movies.cpp`)
  plays each of `kOpeningMovieNames` from the install's `DATA/Movies/`
  through the shared `video::MoviePlayer`, its picture in the middle of
  the screen on black, its sound through the mixer, until it ends or a key
  is pressed; a movie that isn't there is passed over. There's no pointer
  over them: the game's pointer starts hidden (`SetCursor` with none,
  `0x510a44`), and only the game's own shows once a screen turns it on
  (`0x510bf0`). The engine's setting "Opening movies" leaves them out.
- **The main menu's screen:** `MainMenu`
  (`src/engines/stawayteam/ui/main_menu.cpp`) reads the main menu's nernies
  from `Rules.txt` and shows each at its place, the movies over and over
  without sound, with the startup song, and its buttons over them
  ([menus.md](menus.md)).

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`, at the addresses given; `Rules.txt`; the movies' Bink
  headers.
- **How it was checked:** Reluva run on the retail files, the screen
  looked at during each movie and on the main menu's screen: the movies'
  pictures and colours, and the title, background and logo where
  `Rules.txt` puts them. Not yet against the original running. The engine
  test starts the game on an install of its own, without movies, and
  keeps a checksum of the main menu's screen.

## Open questions

None now.
