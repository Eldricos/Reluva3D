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

## Bugs fixed

<!-- The original's own bugs that Reluva doesn't reproduce. -->
None yet.

## Not yet as the original

<!-- Known gaps: behaviour not done yet, or done differently for now. -->
The game runs as far as its main menus; nothing past them has been
tried, and it has no network yet
([systems/hosting.md](systems/hosting.md)).

## Additions outside the game

<!-- What Reluva adds around the game: viewers, tools, settings. -->
- **The data viewer** (`reluva-viewer`): pages of the game's pictures,
  fonts, models, tables and sounds, which the game has no way to show.
- **`reluva-tool sfc` commands** that read every file of a kind
  ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md#the-tools)).
