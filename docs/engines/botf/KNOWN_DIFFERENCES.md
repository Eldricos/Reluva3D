# Star Trek: Birth of the Federation: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **No CD check, no SafeDisc.** The original won't start without its CD
  and the SafeDisc driver; Reluva reads the game's files from its folder,
  and from the CD's files wherever they are
  ([systems/game-files.md](systems/game-files.md)).

## Bugs fixed

None yet.

## Not yet as the original

- **The game starts at the game setup screen,** not the opening movie and
  the main menu; the setup's Cancel goes back to the launcher rather than
  to the main menu.
- **Accepting an empire comes back to the game setup screen:** there's no
  game to start yet ([systems/screens.md](systems/screens.md)).
- **The timers offer only None,** as their other choices aren't known.
- **The main menu behind the panels is drawn as it is,** where the
  recording shows its buttons dimmed.

## Additions outside the game

- **The data viewer** (`reluva-viewer`): the pictures, fonts, sounds, text
  and data tables, and window layouts ([README.md](README.md#using-it)).
- **`reluva-tool botf` commands:** `alt-list` and `snd-list` list and
  extract the two archives ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)).
