# SimCoaster: keeping the players

> **Status:** partial · **Code:** `src/engines/tpw/players.cpp` · **Checked against:** a recording · **Confidence:** inferred unless marked

## Overview

The Personnel File ([personnel-file.md](personnel-file.md)) has four
slots for players, kept from one game to the next: the recording starts
with a player who has played before, deletes them, and makes another.
Where the game keeps them isn't known, so Reluva keeps them in a file of
its own.

## How it works

What the game keeps of a player, as the Personnel File shows it: their
name, rank, golden tickets, shares owned and the share price, each
world's challenges won and whether it's open, and their current
objective (none until their first game). It must keep their parks too,
for "Load A Game" and "Resume this game"; how isn't known.

Neither the installed game nor its CD has a player's file to read: the
install's `Data` holds no file written by the game (`PreBuilt/prebuilt.TPWS`
is a park the game ships). The game may write one when a player is
made, or keep players in the Windows registry. A copy of the game that
has been played would settle it.

## In Reluva

The players are kept in `players.txt` in the game's save folder (the
launcher's for the game), written whenever one is made, deleted, or
given their first objective (`src/engines/tpw/players.cpp`), and as a
park closes, with its objective, golden tickets, worlds opened and share
price ([missions.md](missions.md), [finance.md](finance.md)). Its lines:

```
# Reluva's SimCoaster players: docs/engines/tpw/games/simcoaster/systems/players.md
player 0
name No Self
rank 1
golden-tickets 0
shares-owned 1000
share-price 100
world 0 1 0
world 1 0 0
world 3 0 0
objective 0 0
```

- `player <slot>` (0 to 3) starts a player; the lines after it are
  theirs. A slot not in the file is empty.
- `name`: the rest of the line, as typed (UTF-8).
- `rank`: in `PROMOTIONLEVELS.str`; `golden-tickets`, `shares-owned`,
  `share-price`: numbers.
- `world <theme> <open> <won>`: a world by its `THEMENAMES.str` number
  (0 Land of Invention, 1 Polar Zone, 3 Arabian Nights), 1 if open, and
  its challenges won.
- `objective <mission> <theme>`: in `OPERATION.str` (−1 none) and where.

Lines it doesn't know are passed over, and what a player's lines leave
out is a new player's, so a later Reluva can add to them. The shares
issued and each world's number of challenges come from the settings,
not the file. Without a save folder, the players last only while the
game runs.

`reluva-tool tpw front-end-run <game folder> [seconds] -save <folder>`
runs the front end headless keeping its players in a folder. The unit
tests (`tpw.unit.ui`) write players and read them back.

## Sources and checks

- **Where the facts come from:** the recording (1:04 to 1:22): a player
  who had played, deleted; a new one made, with no objective; the
  installed game's files.
- **How it was checked:** `front-end-run` twice on one save folder: the
  first makes "Tester" and gives them "Operation Expand!", the second
  shows them in the first slot, as the recording shows its player.

## Open questions

- Where and how the game keeps its players and their parks.
- Whether a player's objective is set when they enter their first game
  (as Reluva does), or later.
