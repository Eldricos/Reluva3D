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
those LEGO Loco leans on most: `windows` (windows, input, scroll bars),
`gdi`, `dsound`, `mci`, `dplay` (DirectPlay, two objects meeting on a
network) and `dplay_session` (its sessions over UDP: loss, routers, a
meeting server).

### The basic engine test

`legoloco.engine` (`tests/engines/legoloco/engine.cpp`), without the
game's files: a small install of its own (`tests/engines/legoloco/install.h`:
an executable with a string table, and a resource file with a picture,
its definition, a sound and boards), checked not to be taken for the
game, and taken through the data viewer's pages with keys pressed on
each; its record (`data/engine-record.txt`) holds what was drawn and the
pages' titles.

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

Reading the real game's files: until there's a game-files test,
`reluva-tool legoloco rf-list <game folder>` reads and unpacks every
entry of a copy's resource file, and should end with none failed;
`reluva-tool legoloco dat <game folder>` reads every object definition,
`reluva-tool legoloco sav <game folder>` every board,
`reluva-tool legoloco run <game folder> -save <folder>` how far the
program gets (with `-seconds`, `-click` and `-shot`, clicked through to
its first layout and the screen saved:
[systems/running.md](systems/running.md#how-far-it-gets)),
`reluva-tool legoloco ee <game folder>` `EE.INI`,
`reluva-tool legoloco lay <game folder>` the area arrangements and
`reluva-tool legoloco crd <game folder>` the postcards, likewise.

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

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | CD, 1.0.18.2 | Linux | Every reader over the CD's files; the viewer's pages drawn with `reluva-tool legoloco view` |
| November 2026 | CD, 1.0.18.2 | Linux | The playthrough with `reluva-tool legoloco run`; the network game with `pair` (in memory and over UDP) and `run -udp` in two processes on one computer, but for one copy leaving; the postcards with `crd` |
