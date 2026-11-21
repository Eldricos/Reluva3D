# Star Trek: Birth of the Federation: the game state

> **Status:** partial · **Code:** `src/engines/botf/game/game_state.cpp` · **Checked against:** the user's saved games of three games, from their first turn to their forty-first · **Confidence:** confirmed unless marked

## Overview

A game in progress, as Reluva holds it: everything a saved game holds,
read into one place ([formats/sav.md](../formats/sav.md)), with the
questions the screens ask of it, and that the rules will. The galaxy
screen draws from it ([galaxy-screen.md](galaxy-screen.md)); turn
processing, the system screen and saving will work on it.

## How it works

A game state is a saved game's contents: the summary (the player's empire
and the turn), the galaxy (its sectors, systems with their planets and
buildings, stellar objects, outposts), the five empires (their figures,
their access to each other's territory, the races they've met, their
systems), every race's diplomatic state, the treaties, the ships, the
task forces and the last turn's results.

On top of them, the lookups:

- **What's in a sector:** its system, its stellar object, its task
  forces and its outposts.
- **A ship by its number,** as a task force lists its ships.
- **Whether an empire may go into a sector:** not where an empire whose
  territory it has no access to claims it. The galaxy screen's ranges
  follow it.
- **An empire's events:** how many of the last turn's results count, as
  the galaxy screen's panel shows them.

## In Reluva

`GameState` (`src/engines/botf/game/game_state.cpp`) is built from a
saved game, read whole by `GameState::load`. The engine opens a save into
one and hands it to the galaxy screen. The unit tests
(`botf.unit.archives`: "game state", and the save's parts) cover the
lookups and the readers.

## Sources and checks

- **Where the facts come from:** the save's files, as the Ultimate
  Editor's source lays out those not worked out from the saves
  themselves; see [formats/sav.md](../formats/sav.md).
- **How it was checked:** the galaxy screen, drawn from it, matches the
  user's screenshots as before; the buildings, provinces, races and
  territory access read from the saves agree with what the screenshots
  and the other files show.

## Open questions

- What turn processing will need beyond this: production, research,
  movement and combat each read more of the save.
- Writing a game state back to a saved game.
