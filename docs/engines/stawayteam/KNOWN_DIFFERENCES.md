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
- **A developer setting for the first mission.** In a build with
  developer keys, "Developer: first mission" starts a new game at a later
  mission, to reach its team selection
  ([systems/team-selection.md](systems/team-selection.md#in-reluva)).
- **The camera's and zoom's steps at 30 a second.** The original takes
  one each frame it draws, as fast as the machine draws; Reluva takes 30
  a second, the original's longest step, whatever its frame rate
  ([systems/maps.md](systems/maps.md#in-reluva)).
- **Opening movies as a setting.** The original always plays them;
  Reluva's "Opening movies" setting can leave them out
  ([systems/startup.md](systems/startup.md)).

## Bugs fixed

<!-- The original's own bugs that Reluva doesn't reproduce. -->
None yet.

## Not yet as the original

<!-- Known gaps: behaviour not done yet, or done differently for now. -->
- **Network games:** Reluva's own protocol between copies of Reluva, not
  the program's DirectPlay, so the original can't be met; there's no IPX;
  and the missions aren't played over the network yet: the lobby's
  countdown runs out in it ([systems/network.md](systems/network.md)).
- **Saved games** are Reluva's own, not the game's, so neither reads the
  other's; a voice-over playing as the game's saved isn't heard again
  as it's loaded ([systems/menus.md](systems/menus.md#saved-games)).
- **Keys' names** are Reluva's table of the US keyboard's, not what
  Windows gives for the player's own keyboard. A joystick's buttons work
  as bound, but its sticks do nothing
  ([systems/menus.md](systems/menus.md)).

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
