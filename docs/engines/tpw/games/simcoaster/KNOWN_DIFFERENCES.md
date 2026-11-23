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
  its opening and its front end to a world chosen, then shows its
  loading screen: the park after it isn't built ([PLAN.md](PLAN.md#phases),
  [loading-screen.md](systems/loading-screen.md)).
- **The front end's opening** lacks its effects (the sparkles, the beam,
  the ring of light, the lit hole in the table), the advisor's portrait
  and the skip button; a key or a click skips it
  ([front-end.md](systems/front-end.md#in-reluva)).
- **The Personnel File** starts with four new players every time and
  keeps none; "Load A Game", "Delete Player" and the Coaster Design Kit
  do nothing; its blue buttons are lighter than the game's
  ([personnel-file.md](systems/personnel-file.md#in-reluva)).
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
