# Star Trek: Birth of the Federation: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **No CD check, no SafeDisc.** The original won't start without its CD
  and the SafeDisc driver; Reluva reads the game's files from its folder,
  and from the CD's files wherever they are
  ([systems/game-files.md](systems/game-files.md)).

- **Esc on the main menu leaves the game,** as Quit Game does.

## Bugs fixed

None yet.

## Not yet as the original

- **The game starts at the main menu,** without the opening movie.
- **On the main menu,** only New Single Player Game and Quit Game do
  anything yet; Continue, Save Game and Retire can't be used (as in the
  game with nothing in progress), and New Multiplayer Game, Load Game,
  Options and Hall of Fame do nothing. The version ("1.0.2" in the game)
  isn't drawn.
- **Accepting an empire comes back to the main menu:** there's no game to
  start yet ([systems/screens.md](systems/screens.md)).
- **The timers offer only None,** as their other choices aren't known.

## Additions outside the game

- **The data viewer** (`reluva-viewer`): the pictures, fonts, sounds, text
  and data tables, and window layouts ([README.md](README.md#using-it)).
- **`reluva-tool botf` commands:** `alt-list` and `snd-list` list and
  extract the two archives ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)).
