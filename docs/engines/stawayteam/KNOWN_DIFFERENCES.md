# Star Trek: Away Team: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

<!-- Each item: what the original does, what Reluva does, and why, with a
link to the page that has the details. Say whether a setting gives back
the original's way. -->

## By choice

<!-- Deliberate: conveniences, and what the original can't do on today's
systems (a CD check, a mode the hardware no longer has). -->
- **No CD check.** The original asks for its CD while it runs (*"Please
  insert the original Star Trek Away Team CD now."*,
  [research/file-survey.md](research/file-survey.md#the-install)); Reluva
  plays from the install alone.
- **Opening movies as a setting.** The original always plays them;
  Reluva's "Opening movies" setting can leave them out
  ([systems/startup.md](systems/startup.md)).

## Bugs fixed

<!-- The original's own bugs that Reluva doesn't reproduce. -->
None yet.

## Not yet as the original

<!-- Known gaps: behaviour not done yet, or done differently for now. -->
- **The main menu:** Start New Game's difficulty window doesn't start a
  game; Load Save Game and Play Network Game play their sound and
  nothing else; in Settings, CONFIGURE KEYBOARD and CONFIGURE SOUND
  don't lead anywhere, and the sliders don't answer their keys
  ([systems/menus.md](systems/menus.md)).
- **Buttons** don't draw the line under their shortcut's letter, nor
  brighten their label under the mouse; the toolkit's toggles make no
  sound
  ([systems/menus.md](systems/menus.md#buttons)).
- **The pointer** is the system's, not the game's.

## Additions outside the game

<!-- What Reluva adds around the game: viewers, tools, settings. -->
- **The game's options** are kept in `options.ini` where Reluva keeps
  the game's files, not where the game keeps them
  ([systems/menus.md](systems/menus.md#the-settings-window)).
- **The data viewer** (`reluva-viewer`): the game's pictures, sequences,
  models, fonts, sounds and objects, a page each
  ([README.md](README.md#using-it)).
- **`reluva-tool stawayteam` commands** for studying the game's files
  ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)).
