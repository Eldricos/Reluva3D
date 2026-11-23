# LEGO Loco: testing

## Automated checks

### Unit tests

`tests/engines/legoloco/unit/`, a file per topic, each a suite
`legoloco.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `legoloco.unit.boards` | Boards: the header, squares, pieces with their minifigures, and trains; a board holding fewer pieces than it counts; refusing a short one |
| `legoloco.unit.definitions` | Object definitions: their parts, occupancy grids and frame-set table, the game's corrections, and refusing damaged ones |
| `legoloco.unit.detection` | Recognising the game: an empty folder, and the right names with other contents, aren't it |
| `legoloco.unit.easter_eggs` | `EE.INI`'s two sections, read key by key until one is missing; the area arrangements and their index |
| `legoloco.unit.game_files` | A copy's files: the resources' names from an executable's string table, finding a resource by number, and the frames' width; the save folder's `LEGO.INI` made from the copy's, then kept with what was written into it |
| `legoloco.unit.postcards` | Postcards: a card read back with its names, message, stamp, background and stickers; refusing a damaged one; the clip art's names in each category |
| `legoloco.unit.resources` | The resource file: reading its index and entries by name, unpacking a compressed entry, and refusing damaged ones |

The shared code that runs the game has its own tests, in `common.unit.*`;
those LEGO Loco leans on most: `windows` (windows, input, scroll bars,
the pointer), `cursors` (the busy pointer's file format), `gdi`,
`dsound`, `mci`, `dplay` (DirectPlay, two objects meeting on a network)
and `dplay_session` (its sessions over UDP: loss, routers, a meeting
server, a new host taking over).

### The basic engine test

`legoloco.engine` (`tests/engines/legoloco/engine.cpp`), without the
game's files: a small install of its own (`tests/engines/legoloco/install.h`:
an executable with a string table, and a resource file with a picture,
its definition, a sound and boards), checked not to be taken for the
game, and taken through the data viewer's pages with keys pressed on
each; its record (`data/engine-record.txt`) holds what was drawn and the
pages' titles.

### The game-files test

With your own copy of the game: point `RELUVA_LEGOLOCO` at its install
(the folder holding `Exe` and `art-res`), or add it in the launcher, then
run the steps (`tests/engines/legoloco/gamedata.cmake`, ctest
`legoloco.gamedata.<step>`):

```sh
RELUVA_LEGOLOCO=/path/to/LEGO\ Loco ctest --test-dir build -L gamedata -R legoloco
```

| Step | What it runs |
|---|---|
| `release` | What the game is: its version and language |
| `files` | The files it came with, by name, size and MD5, as one checksum; not those its own games change (`LEGO.INI`, the layout being played, the post bag's own folders) |
| `resources` | Every entry of the resource file unpacked (`rf-list`) |
| `formats` | Every object definition, board, area arrangement, date-based surprise and postcard read (`dat`, `sav`, `lay`, `ee`, `crd`) |
| `run` | `loco.exe` at 1024 by 768 on a clock of instructions (`run -deterministic 50000`), the movies skipped: one player's game started from the front screen, the welcome bubble closed and the Toybox opened; the screen at four moments (loading, the welcome bubble with Windows' arrow over it, the board, the Toybox), as checksums with their frames' numbers |

- **Records:** in `tests/engines/legoloco/data/gamedata/<version>-<language>/`,
  one file a step, holding counts, summary lines and checksums, none of
  the game's own content. So far: `1.0.18.2-en`, the English release,
  made on Linux.
- **A copy as installed:** the steps expect the files the game came
  with. A copy played on Windows may hold the player's own layouts in
  `art-res\SAVEGAME`, which the `files` and `formats` steps then count;
  Reluva writes its own to the save folder, so a copy played only in
  Reluva stays as installed.
- **When a step fails:** it prints the recorded and the new lines; each
  command's output is kept in the build, under `gamedata/legoloco/<step>/`
  (the run's screens as `.ppm` files, its report as `run.txt`), with the
  previous run's beside it. If the change is meant, run again with
  `RELUVA_GAMEDATA_UPDATE=1` and review the records' diff.
- **The same every time:** the run's clock counts instructions, moved
  on a millisecond whenever every thread waits, and the run ends where
  the steps are taken, not by a clock of the tool's own. The date it's
  given is fixed too, noon on 1 March 2026 in UTC, moving with that
  clock: the game reads the date, and its minifigures walked otherwise
  on another day. Checked by running it again and again, and in another
  time zone. It was checked to catch a change: the
  text drawn a little narrower changes the welcome bubble's screen.
- **Your own baseline:** where your results differ from the committed
  records (another system, say), keep your own before making changes,
  as [DEVELOPMENT.md](../../DEVELOPMENT.md#the-game-files-test-with-your-own-copy-of-the-game)
  says (`RELUVA_GAMEDATA_RECORDS`, or **Make my baseline** in the Tests
  window).

### What the automated checks don't cover

Beyond the game-files test's run, how far the program gets with other
steps: `reluva-tool legoloco run <game folder> -save <folder>` (with
`-seconds`, `-click` and `-shot`, clicked through and the screen saved:
[systems/running.md](systems/running.md#how-far-it-gets)).

Playing the game over a network isn't covered by an automated test
either: it needs the game's own files. `reluva-tool legoloco pair`
runs two copies on one network, in memory or (`-udp`) over the
system's UDP, and `run -udp` one copy each in two processes or on two
computers ([systems/running.md](systems/running.md#in-reluva)).

## A playthrough by hand

The game, from the launcher:

- The LEGO logo, the Intelligent Games logo and the intro movie play,
  with their sound; then the front screen, with music. The game's hand
  follows the mouse.
- Choosing the one-player case, then the tick: the first layout, and the
  station master's welcome in a bubble, its text legible; its train
  runs.
- The bubble closed: the hand pointer follows the mouse. The LEGO case
  opens the Toybox; each of its buttons opens its panel (a tip first).
- A track picked up and placed on bare ground stays there. Four curves
  (turned with right clicks while held) close into a loop; with points
  in one corner and a depot under them, apron towards the loop, a click
  on the depot (the Toybox closed) starts a train that runs round. A
  right click opens its zoombox; a train built there, an engine and
  three carriages, runs out and round as built.
- The post office (a right click, the Toybox closed) shows its tip over
  the sorting room. The pencils open the editor: a background and a
  sticker put on a card, turned over by the post box, stamped and posted
  (a file in `PostBag\Sort\Out`). A card put in `PostBag\Sort\In` is
  held up by the postman, and filed in the album with a click on it and
  on the book.
- Saved under a name typed in the save panel, the layout loads again
  from the load panel, as it was.
- The windowed mode's button makes a framed window, and back.
- The front screen's arrow leaves the game; closing Reluva's window asks
  first.

A network game, with two copies (two computers, or two runs of Reluva on
one), each with a name of its own (`LEGO.INI`'s `[USER]` `Name`):

- On both, the front screen's case of two heads; on one, the grid with
  a pen (hosting), on the other the one with binoculars (joining); the
  tick, then TCP. The joiner's list shows the host's game; on the grid
  of places each sees both players' names; the tick takes both into
  the game.
- Each board is bigger than the screen: the scroll bars move it, a
  line at a time on an arrow, a page on the track, steadily while
  held, or to where the thumb's dragged.
- A right click on a gate shows the map: both players' places lit and
  their names listed.
- A train started from a depot runs to a gate, shows on both maps as it
  goes, and comes into the other player's board through its gate.
- In the Postcard Editor, the back of a card lists the other player; a
  card sent to them (picked with a double click, then posted) is taken
  by a train with a mail wagon from the sender's post office and
  arrives at the other's (`PostBag\Sort\In`).
- One copy leaving: the other is told and plays on.

In the data viewer (`reluva-viewer <folder>`):

- Boards: every layout drawn whole on its backdrop, no magenta squares
  (pieces without a picture); S shows the squares' values.
- Pictures: a minifigure's walks and moods play (Up/Down, Space); a
  building's frames line up with the marks on its strip.
- Definitions and Sounds: each opens; the sounds play.

Against the original: a recording of it played, its frames set beside
Reluva's at the same screen size (`run -desktop WxH`), screen by screen.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | CD, 1.0.18.2 | Linux | Every reader over the CD's files; the viewer's pages drawn with `reluva-tool legoloco view` |
| November 2026 | CD, 1.0.18.2 | Linux | Against a published recording of the original (34 minutes of single-player play at 1024 by 768): its intro, front screen, first layout, Toybox and palettes, Postcard Editor, a minifigure's card and a zoombox, beside Reluva's with `run -desktop 1024x768`. The same, but for the screen's size, the name box and the text's font, then put right: the name box shown and typed into, the bubbles' lines breaking as there; left, the letters' shapes and the selection's colour, Windows XP's in the recording ([KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md)) |
| November 2026 | CD, 1.0.18.2 | Linux | The playthrough with `reluva-tool legoloco run`; the network game with `pair` (in memory and over UDP) and `run -udp` in two processes on one computer, but for one copy leaving; the postcards with `crd` |
