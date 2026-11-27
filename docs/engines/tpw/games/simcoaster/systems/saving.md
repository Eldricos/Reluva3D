# SimCoaster: saving and loading

> **Status:** partial · **Code:** `src/engines/tpw/ui/info_windows.cpp`, `src/engines/tpw/ui/park_screen.cpp`, `src/engines/tpw/tpw.cpp` · **Checked against:** the string tables · **Confidence:** guessed unless marked

## Overview

The game's menu over the park (Esc) has "Load Game" and "Save Game"
(`UITEXT` 768, 769), the Personnel File "Load A Game" (35). Their
windows' strings: "Save Game" and "Load Game" (777, 778), the list's
"Save name" and "Date" (779, 780), "New Save" (782), "FILE ALREADY
EXISTS ... A saved game called '{31}' already exists. Do you want to
replace it?" (781), and "Delete Savegame", "Are you sure you want to
delete this savegame?" (4, 5; "Delete selected savegame", `UIHELPTEXT`
357). "Select A New Player" warns "Your game WILL be saved" (775). The
recording opens none of them.

## How it works

- **Saving:** the Save Game window lists the player's saves, newest
  first, under "New Save". New Save, then the tick, asks for a name,
  typed; an old save, then the tick, asks whether to replace it, and the
  tick again does. The game's written, and goes on; the park's clock
    stands while the window's up.
- **Deleting:** a save chosen, the bin (`b_delete`) asks "Delete
  Savegame: Are you sure you want to delete this savegame?", and the
  tick deletes it, in either window.
- **Loading:** the Load Game window lists them likewise; one chosen, the
  tick leaves the park for the save's, in its own world, with what the
  player had then: their rank, golden tickets, shares and challenges, as
  the save keeps the park's copy of them. It opens on the park, with
  neither the fly-in nor the briefing.
- **From the Personnel File**, "Load A Game", lit once the player shown
  has a game saved, opens Load Game over the file; the save chosen goes
  on into its park, past the globe.

A ride's riders are let off as a saved park opens: the rides' scripts
start afresh, without them.

Saves are Reluva's own files ([reluva-saves.md](../formats/reluva-saves.md)),
a player's listed by their name.

## In Reluva

`SaveLoadWindow` (`src/engines/tpw/ui/info_windows.cpp`), laid out by
guess in the other windows' style; `ParkScreen::openSaveLoad`,
`saveChosen`; `playWorld` in `src/engines/tpw/tpw.cpp` loads a save in
a park's place. `reluva-tool tpw window-shot <game folder> <world
folder> <out.png> save` (or `load`) draws the window with two saves.
The unit tests (`tpw.unit.ui`) name a new save, replace an old one and
load one.

## Sources and checks

- **Where the facts come from:** `UITEXT.str`, `UIHELPTEXT.str`.
- **How it was checked:** `window-shot save` and `load` read over;
  `save-check` ([reluva-saves.md](../formats/reluva-saves.md#sources-and-checks)).

## Open questions

- How the game's windows look, and where its delete button is.
- Whether "Select A New Player" saves the game as it says, and where it
  comes back to.
- What the game's own saves hold besides the grid ([tpws.md](../formats/tpws.md)).
