# Star Trek: Away Team: testing

## Automated checks

### Unit tests

`tests/engines/stawayteam/unit/`, a file per topic, each a suite
`stawayteam.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `stawayteam.unit.detection` | Recognising the game: an empty folder and the program alone aren't the game; its two files, whatever they hold, are an unknown release. Importing it: from a CD's `Setup` folder, the files Reluva uses and the movies in their folders, and not the rest; from an install without movies, with a note |

| `stawayteam.unit.menus` | Fonts: a line's width with nothing between characters, lower case drawn as capitals in a font without it, control characters left out, others it hasn't got drawn as its first; the credits' lines (comments left out, blank lines kept, long ones cut); buttons: the `&` left out of the label and its shortcut kept, chosen by the shortcut (not with Ctrl), by a click, and not when let go off it |
| `stawayteam.unit.formats` | Frames (raw, each kind of run-length code, blended pixels with their alpha, damaged ones), objects in binary (a sequence and its frames, a waypoint map, objects in properties, another version, bytes left over, a class the game doesn't have), objects in text (arrays with and without counts, keys more than once, spaces at values' ends, LF and CRLF, written back as they were, text that isn't an object) and `.RDS` scripts (comments, labels with spaces, paragraphs) |

Reading `DATA.dat` is the common ZIP reader's, tested in
`common.unit.archives`, with an end record signed as the game signs it.

### The basic engine test

`stawayteam.engine` (`tests/engines/stawayteam/engine.cpp`), without the
game's files: a small install of its own (the program, and a `DATA.dat`
signed as the game's with a `Rules.txt`, two pictures, a sequence, a
model, a font, a sound and a script, the menus' pictures and fonts, all
made in the test), found as an unknown release, started (its movies not
there, so the main menu, its picture and buttons placed and its song),
C showing the credits and Esc closing them, Esc asking to quit, N closing the dialog, Q (Quit's key) asking again
and Y quitting, a checksum of the screen at each; the data viewer run
on it: its pictures shown on each background, zoomed, by folder and without the
status line, then each page in turn (the sequence stepped, the model
turned, the font, the sound played, the objects), a checksum of each
screen; then the game with rules that
aren't an object, and with none, each saying what's wrong. Against its record
(`data/engine-record.txt`).

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

That the real release is recognised: its signatures need the real
files, which the tests never hold. That the real `DATA.dat` is read
whole: `reluva-tool stawayteam data-extract` on it; and its objects and
scripts: `reluva-tool stawayteam data-check`, which should find 753
objects, all written back the same, 48 scripts with 1,378 entries, the
11 other text files, and no problems; its objects in binary:
`reluva-tool stawayteam object-check`, 3,738 models, 179 sequences, 19
fonts and 33 waypoint maps, none failed; and its pictures:
`reluva-tool stawayteam frame-check`, 671 frames read and one file not
a frame. How the pictures look: the data viewer, or `reluva-tool
stawayteam frame-png`.

## A playthrough by hand

1. Add the install's folder in the launcher: it's found as "Star Trek:
   Away Team".
2. Start it: the four opening movies, each skipped by a key or a click,
   with their sound; then the main menu's screen: the starry background
   moving, the title, the Reflexive logo bottom left, the startup song,
   and the six buttons, each lit under the mouse and pressed while held
   down, a sound as one is clicked; Credits rolls the credits in a window
   the menu shows through, fading in at the bottom and out at the top,
   headings underlined red, roles green, starting again after the last,
   RESUME closing it; Quit (or Esc, or Q) asks, No closes the dialog, Yes
   goes back to the launcher. With "Opening movies" off, the main menu's screen at once.
   With `DATA.dat` missing or damaged, it says so instead.
3. `reluva-viewer` with the game: the pictures, their colours and
   blended edges right on each background; sequences playing; a
   character's model (`Characters/Klingon.mdl16`) turning through its
   directions in each animation; the fonts' characters; sounds playing;
   an object of each kind of file.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US English retail | Linux | Detection of the install (`reluva-tool detect`); the import's plan (`reluva-tool import`) from the install, and from a CD's layout made of its files; all 6,949 files of `DATA.dat` read, their CRC-32s matching; `data-check`: all 753 objects and 48 scripts read; `frame-check`: all 671 frames read; `object-check`: all 3,969 objects in binary read; the viewer's pages on the real data, by their screens (the Klingon turning through `Idle`'s 12 directions); the opening movies and the main menu's screen, by their screens (sound not heard: no output here) |
