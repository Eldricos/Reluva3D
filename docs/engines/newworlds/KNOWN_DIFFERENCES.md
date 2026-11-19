# Known differences from the original (Star Trek: New Worlds)

Reluva can't play the game yet: it can be looked at in the data viewer
(`reluva-viewer`), which has a first playable layer on a mission's map
([README](README.md#where-it-stands)).
These are the differences known so far.

## By choice

- **The mission scripts are Reluva's own.** The original keeps its
  missions' logic in `STNW.exe`; Reluva's scripts are written from what the
  recordings showed. Their messages are Reluva's own words for what the
  game's messages say; the objectives and fail conditions are the game's
  own strings ([mission-scripts.md](mission-scripts.md)).
- **The build menu is a numbered list,** not the game's row of buttons
  ([game.md](game.md#controls)).

## Not yet as the original

- **Not modelled yet:** the playbook (a double click with units selected),
  selecting buildings, units defending themselves, and units building: a
  building goes up by itself once placed ([game.md](game.md#controls)).
- **Figures the recordings can't give are placeholders:** speeds, camera
  distances, build times, costs, power, mining, crew growth and more. The
  full list, with where each value came from, is in
  [game.md](game.md#placeholders).
- **Not scripted:** where the starting colonies, units and enemies are, the
  timing of attacks, how close "approach" is, and exact amounts. R8 has no
  script, as it has no recording. Where a Romulan recording's beat had no
  event of its own, the script uses a timer
  ([mission-scripts.md](mission-scripts.md#how-far-they-go)).
- **The starting colonies are approximations,** placed where the recordings
  put them ([colony.md](colony.md)).

## Additions outside the game

- **The data viewer** (`reluva-viewer`): pages for the game's missions, models, maps, sounds,
  strings and fonts, with their own keys ([README](README.md#using-it)).
