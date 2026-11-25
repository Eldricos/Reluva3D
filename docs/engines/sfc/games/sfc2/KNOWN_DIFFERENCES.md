# Star Trek: Starfleet Command II: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

<!-- Each item: what the original does, what Reluva does, and why, with a
link to the page that has the details. Say whether a setting gives back
the original's way. -->

## By choice

<!-- Deliberate: conveniences, and what the original can't do on today's
systems (a CD check, a mode the hardware no longer has). -->
- **What the game writes goes to a folder of Reluva's.** The original
  writes its logs, its settings and its campaigns' saves in its own
  folder; hosted, those go to a save folder, read before the install,
  and the install is left as it is
  ([systems/hosting.md](systems/hosting.md#what-it-writes)).
- **What the game writes to the registry goes to a file.** The original
  keeps its CD key in Windows' registry; hosted, the values it writes
  are kept in `registry.txt` in its save folder, so they're kept on
  every system Reluva runs on, and nothing is written to Windows' own
  registry ([systems/hosting.md](systems/hosting.md#what-it-writes)).
- **Imported, the script kit is left out.** The CDs' setup installs
  Taldren's mission-script kit (`API\`) with the game; an import from
  the CDs doesn't, as Reluva doesn't use it
  ([PLAN.md](PLAN.md#decisions)), nor setup's uninstall log
  (`sfc2.isu`). The game is otherwise as setup installs it
  ([README.md](README.md#using-it)).
- **The opening movie is played by Reluva.** The original's launcher
  runs `SFC2Intro.exe`, a small player with the movie inside; Reluva
  takes the movie out of it and plays it itself before the game, a key
  or click skipping it ([formats/intro.md](formats/intro.md)).
- **Multiplayer is between copies of Reluva.** The original's DirectPlay
  speaks Microsoft's protocol; Reluva's speaks its own, on port 28541,
  so it plays other copies of Reluva, not the game running on Windows
  ([dplay.md](../../../../common/x86/dplay.md#the-network)).
- **The machine is named RELUVA.** Hosting or joining a multiplayer
  game, the original names its player by the computer's name; hosted,
  it's told the name Reluva gives every program, and the machine's own
  address, 127.0.0.1 ([systems/hosting.md](systems/hosting.md#its-multiplayer)).

## Bugs fixed

<!-- The original's own bugs that Reluva doesn't reproduce. -->
None yet.

## Not yet as the original

<!-- Known gaps: behaviour not done yet, or done differently for now. -->
- **Not everything has been played.** The front end, skirmishes, the
  tutorials, a campaign's first missions with its saves, and
  multiplayer (against AI players, and between copies of Reluva to a
  game's end) run, and a skirmish and a campaign's Convoy Escort have
  been won, and the Command 190 tutorial passed; the other tutorials
haven't been passed, and the Film
  Room, Credits, Hot Keys and Vessel Library screens haven't been
  opened ([systems/hosting.md](systems/hosting.md)).
- **Multiplayer sessions don't pass on to a new host.** When the host
  leaves, the others' session is lost, where DirectPlay could make
  another player the host; groups and chat messages aren't there
  ([dplay.md](../../../../common/x86/dplay.md)).
- **A loaded campaign stops starting missions.** Once a mission has been
  played in a campaign that was loaded, no later mission starts; whether
  the game or Reluva is at fault isn't known yet ([systems/hosting.md](systems/hosting.md#open-questions)).
- **The 2.0.3.6 patch's own changes are barely played.** It applies,
  and the patched game runs to its main menu; its missions and its
  multiplayer haven't been played ([systems/patching.md](systems/patching.md)).
- **Online play isn't there.** Dynaverse II and MPlayer were services
  that have closed; the Winsock functions the game imports, perhaps
  theirs, aren't stood in for.

## Additions outside the game

<!-- What Reluva adds around the game: viewers, tools, settings. -->
- **The data viewer** (`reluva-viewer`): pages of the game's pictures,
  fonts, models, tables and sounds, which the game has no way to show.
- **`reluva-tool sfc` commands** that read every file of a kind
  ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)).
