# Star Trek: Armada: saving and loading a game

> **Status:** partial · **Code:** `src/engines/storm3d/play/saving.cpp`, `src/engines/storm3d/shell/saved_games_screen.cpp` · **Checked against:** Federation 1 saved and loaded in Reluva, carried on to its end · **Confidence:** the screens and the scripts' part from the code; what Reluva keeps of a mission Reluva's own

## Overview

A campaign mission can be saved from its game menu, outside a cut
scene, and loaded again from that menu or from the single-player
screen's Load Game. The screens are the program's; the file's head is
its own ([formats/saved-game.md](../formats/saved-game.md)), but what
follows is Reluva's: the program's form wants every kind of object's
fields, and there was no saved game of the program's to check one on.

## How it works

*(from the code)*:

- **The game menu** has Save Game only outside a cut scene (0x537020).
  Save Game opens the Save Game screen; Load Game the Load Game screen,
  whose Previous Menu goes back to the menu.
- **Save Game** (0x53eac0): `bitmaps\misc\loadbg.bmp`, "SAVE GAME"
  (`label.map`'s `save_game`) at (64, 6), ten rows of the saves there
  from (100, 61), 394 by 32 each, each showing its description; the
  scroll bar at x 614; the description's box, `wonlogin\wlstext.bmp`, at
  (100, 385), its text from (107, 392), 40 characters at most, at first
  the mission's name; Previous Menu, Save Game and Delete Game on the
  bottom bar. Save Game with a row chosen asks "Overwrite game?"
  (`save_over`) and, on Yes, saves over it; with none, it saves a new
  `s<n>.sav`. The screen stays, its list read again. Delete Game takes
  the save away, and the `.bmp` and `.trk` of its name if there are any.
- **Load Game** (0x53b8b0): the same screen with eleven rows and Load
  Game in the middle; from the single-player screen and from play
  alike. Load Game loads the save chosen as a mission is started, its
  loading screen first.
- **Loading** starts the mission's scripts afresh, then gives their
  variables back through their own load functions; the rules and the
  scripts that were running aren't saved, and every variable given back
  is marked changed, so all its rules are tested again (0x5431d0). The
  campaign's and mission's numbers are kept, so a mission loaded and
  won goes on to the next.

## In Reluva

- **The screens** as above (`SavedGamesScreen`). The saves are in
  Reluva's save folder for the game, `save\`, beside its `shell.set`;
  the install's own `save` folder isn't read.
- **What a save keeps**, Reluva's own choice: the map and the player's
  slot; each unit as it was made, in its order (its definition, player,
  colour and place), so that each is made again with the same handle;
  then everything that changes in play: the ships' moves and orders,
  every unit's shields, hull, systems, crew, weapons and special
  weapons, targets, cloaks, effects, holds and boarding parties, the
  builders' queues and sites, the freighters, the players' counters,
  tallies and research, the computer players, the relations, the
  wormholes and conduits, the people aboard, the markers, titles and
  objectives, the shots in flight, the clock, and the scripts' variables
  as their own save functions write them. The camera and the selection
  are kept too. Not kept: what's only drawn (explosions, debris,
  shields' glows), a voice part way through, and anything in a cut
  scene, which can't be saved in.
- **Loading** starts the mission on its map, makes again the units made
  since, puts all that back, then gives the scripts their variables, as
  above; the clock goes on from where it was saved.

## Sources and checks

- The program's screens and its saving and loading, read in the
  disassembly at the addresses given.
- **Checked:** Federation 1 played to 107 seconds of its clock, saved,
  and loaded in another run: from the pass after loading, the scripts'
  every call, 224,000 of them, many counting units by where they are,
  was the same as in the run that went on unsaved. Saved again in the
  fight with the Borg at 503 seconds and loaded, it played on the same,
  77,000 calls, to the same win at 635.39.

## Open questions

- The program's own form, to load its saves and write saves it can
  load.
- Whether the program keeps a voice or a fade going on across a save.
