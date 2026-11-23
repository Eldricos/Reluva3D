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
- **The main menu:** in Settings, the sound's stereo and mixing
  choices, and the keyboard's bindings, are kept but don't change
  anything yet
  ([systems/menus.md](systems/menus.md)).
- **Maps:** projectiles don't set off touching triggers, and the animations' and
  particles' messages aren't there; the camera scripts don't pause the
  characters, and one taken off the team stays on its bar; walkers keep
  apart by Reluva's own rule; the transporter's pictures aren't drawn
  as enemies beam out; the 5 polygons' processes and the Out Of
  Range cursor aren't there
  ([systems/maps.md](systems/maps.md#in-reluva)).
- **Saved games** are Reluva's own, not the game's: a save keeps the
  mission as it began, and loading one starts that mission again from
  its briefing; the save screens' picture and random numbers aren't
  shown ([systems/menus.md](systems/menus.md#saved-games)).
- **Objectives:** rescue and need-item objectives are only done
  as the triggers say; parts other than the team aren't moved between a
  mission's maps
  ([systems/objectives.md](systems/objectives.md#in-reluva)).
- **Items:** the Psychic Rack, the enhancements and the Psyche
  Displacement Controller aren't there; the
  gadgets' sparks and the transporter's pictures aren't drawn, nor the
  band on the team bar; there's no full inventory, and the phaser's
  overload isn't there ([systems/items.md](systems/items.md#in-reluva)).
- **The briefing:** the text has no scroll bar (the mouse wheel
  scrolls it), the random numbers aren't there, and EXIT doesn't fade out
  ([systems/briefing.md](systems/briefing.md#in-reluva)).
- **The team selection:** the transporter's random numbers and the
  effect the figures are drawn through aren't there yet
  ([systems/team-selection.md](systems/team-selection.md#in-reluva)).
- **Buttons** don't draw the line under their shortcut's letter, nor
  brighten their label under the mouse; the toolkit's toggles make no
  sound
  ([systems/menus.md](systems/menus.md#buttons)).
- **The pointer** is the system's during the opening movies; from the
  main menu on it's the game's.
- **Keys' names** are Reluva's table of the US keyboard's, not what
  Windows gives for the player's own keyboard; joysticks aren't there.

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
