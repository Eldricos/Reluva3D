# SimCoaster: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **The online services are left out.** The original's chat, mail,
  uploads, news and voting (`wea*.dll`) talked to EA's servers, which
  have been shut down ([PLAN.md](PLAN.md#decisions)).
- **Importing the CD copies less than setup installs:** not the DLLs,
  `ImportCoaster.exe`, SafeDisc's files, the icons or EA's registration
  program, none of which Reluva uses ([cd.md](formats/cd.md#in-reluva)).

## Bugs fixed

None yet.

## Not yet as the original

- **The postcard font's packed glyphs aren't read** (`POSTCARD.bf4`, 160
  of its 250): their boxes are, not their pixels
  ([bf4.md](formats/bf4.md#the-pixels)).
- **Little of the game runs yet:** Reluva recognises the install, plays
  its opening and its front end to a world chosen, shows its loading
  screen, then the new park: its world, money and date, its clock
  running, the mission's briefing over it, visitors coming by guessed
  rules and drawn as squares, under a camera of Reluva's own keys; no
  fly-in, toolbar, gates or building yet, its fixed items marked
  ([PLAN.md](PLAN.md#phases), [park.md](systems/park.md)). Every park
  is its world's first mission's.
- **The front end's opening**: its sparkles are drawn by guessed
  numbers, size and speed, without the emitters' flares; the white burst
  in the well isn't drawn; the advisor's portrait shows by the
  recording's times; the skip button jumps to the
  globe, a guess, and a key presses it too
  ([front-end.md](systems/front-end.md#the-portrait-and-the-button));
  its sounds play at the script's own times, up to 4.7 s before the
  recording's, whose computer slowed under the sparkles, and from no
  place; its music turns between its groups by values Reluva sets to
  match the recording, and the advisor says his lines at the
  recording's times ([front-end.md](systems/front-end.md#the-music-and-the-speech)).
- **The Personnel File** keeps its players in a file of Reluva's own,
  `players.txt` in the game's save folder, as the game's own aren't
  known ([players.md](systems/players.md)); "Load A Game" is always
  disabled and the Coaster Design Kit does nothing; there are no
  tooltips; the portrait always shows; its blue buttons are lighter than
  the game's ([personnel-file.md](systems/personnel-file.md#in-reluva)).
- **On the globe**, a world under the pointer shows nothing, and the
  chosen world turns to the camera the shorter way round, guesses
  ([globe.md](systems/globe.md#in-reluva)).
- **The opening's logo movie** plays to its last frame (5.4 s, its last
  2 s silent), where the game may stop it with its sound; its colours
  are BT.601's, brighter than the game's; the splash after it stays 3
  seconds unless skipped, a guess ([opening.md](systems/opening.md#in-reluva)).

## Additions outside the game

- **The data viewer** (`reluva-viewer`), showing what the game's files
  hold ([README.md](README.md#using-it)); it isn't in `reluva`, the
  program players get.
