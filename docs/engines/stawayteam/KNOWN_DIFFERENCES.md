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
- **The main menu:** Start New Game goes as far as the first mission's
  briefing and team selection, not the mission; Load Save Game and Play Network Game play their sound and
  nothing else; in Settings, the sound's stereo and mixing
  choices, and the keyboard's bindings, are kept but don't change
  anything yet
  ([systems/menus.md](systems/menus.md)).
- **Maps:** the ground's values spread straight across a cell, not by
  the program's fractal; water's warping isn't there; triggers set off
  by enemies, passing through or colliding aren't there, nor some of the
  AIs' messages; enemies patrol, see, shoot, go to look at the team's
  noise and where they're called to, pursue, retreat and stun, but don't
  run away, recharge or look at the fallen yet; footsteps make no sound;
  shots show their flashes but no beam and always hit; there are no
  cursors; saving and loading from the escape menu aren't there
  ([systems/maps.md](systems/maps.md#in-reluva)).
- **Objectives:** there's no score, and
  LOAD GAME is greyed out; rescue and need-item objectives are only done
  as the triggers say; parts other than the team aren't moved between a
  mission's maps
  ([systems/objectives.md](systems/objectives.md#in-reluva)).
- **Items:** only weapons, grenades and the hypospray can be used;
  grenades fly straight, without an arc or bounces; there are no item
  cursors, no scrolling of the bar or full inventory, and the phaser
  can't be set to stun ([systems/items.md](systems/items.md#in-reluva)).
- **The briefing:** the items found in earlier missions aren't
  described after the lines, the text has no scroll bar (the mouse wheel
  scrolls it), the random numbers aren't there, and EXIT doesn't fade out
  ([systems/briefing.md](systems/briefing.md#in-reluva)).
- **The team selection:** START MISSION and EXIT come back to the main
  menu; the transporter's random numbers, the officers' body figures,
  the item lists' scrolling and their band's pulsing aren't there yet
  ([systems/team-selection.md](systems/team-selection.md#in-reluva)).
- **Buttons** don't draw the line under their shortcut's letter, nor
  brighten their label under the mouse; the toolkit's toggles make no
  sound
  ([systems/menus.md](systems/menus.md#buttons)).
- **The pointer** is the system's, not the game's.
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
