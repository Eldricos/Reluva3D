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
  rules, drawn by their sprites at a guessed pace, under its HUD, the
  camera moved by Reluva's own keys and the compass; path can be laid, the
  bottom buttons' menus open, and all their items do something, the Overview
  Map and Visitor Opinion by guess, the Park Map's layers too; the game's
  menu loads, saves, sets the options Reluva follows (speed, volumes,
  advisor: [options.md](systems/options.md)), resumes and quits, nothing
  else; attractions', staff's and the gates' windows
  and menus have only some of their controls, and the windows the
  recording doesn't show are laid out by guess
  ([park-windows.md](systems/park-windows.md)); a new park has the prebuilt park's
  path, queue, ride and other objects ([paths.md](systems/paths.md)),
  standing still; attractions can be bought and placed, their
  queues laid, and they run their scripts; visitors choose, queue for
  and use rides, shops and toilets by guessed rules; litter, sickness,
  pranks, thoughts and opinions of prices by guessed rules; staff hired
  in the Hire Staff window and at work by guessed rules
  ([staff.md](systems/staff.md)); research and upgrades by guessed
  rates; the money's accounts, loans, share price and bankruptcy by
  guessed rules, their windows laid out by guess
  ([finance.md](systems/finance.md)); the missions played as far as the park
  awards allow, four of the eight measured by guessed rules and the
  rides' never held, so the last mission can't be done
  ([park-awards.md](systems/park-awards.md)); challenges by guessed rules, their
  triggers partly ([challenges.md](systems/challenges.md)); special rides
  activated in a window laid out by guess, abilities discovered and
  learnt by guessed rules, their hoardings not drawn
  ([special-rides.md](systems/special-rides.md)); an obstacle cleared at once by the first staff
  trained, and each park set up anew as the missions done leave it
  ([missions.md](systems/missions.md)); the Assign staff, financial summary and naming windows laid out
  by guess, and a ride's view only the camera brought close
  ([research.md](systems/research.md), [building.md](systems/building.md#its-window-and-menu)); the
  weather's rolls and look guessed ([visitors.md](systems/visitors.md),
  [weather.md](systems/weather.md)); their
  signs are lettered by guessed rules ([sgn.md](formats/sgn.md)); some show their settings' names, or
  another world's, the game's own not being found for them
  ([building.md](systems/building.md#their-names)), and the Buy window
  has no descriptions; no building but attractions and path yet, its
  fixed items' cells marked; the fly-in under the world's sky, where the
  recording's is black, and ended by a click or a key, a guess
  ([fly-in.md](systems/fly-in.md)); the advisor bare, with no lip-sync,
  saying seven of his lines, each once a park
  ([advisor.md](systems/advisor.md))
  ([PLAN.md](PLAN.md#phases), [park.md](systems/park.md)).
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
- **Saved games** are files of Reluva's own (`saves/save1.rsv` in the
  game's save folder), as the game's `.TPWS` can't be written; their
  windows are laid out by guess, a loaded game opens without the fly-in
  or briefing, its riders let off
  ([saving.md](systems/saving.md), [reluva-saves.md](formats/reluva-saves.md)).
- **The Personnel File** keeps its players in a file of Reluva's own,
  `players.txt` in the game's save folder, as the game's own aren't
  known ([players.md](systems/players.md)); the Coaster Design Kit does
  nothing; there are no
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
