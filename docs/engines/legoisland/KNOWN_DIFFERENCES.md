# LEGO Island: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

<!-- Each item: what the original does, what Reluva does, and why, with a
link to the page that has the details. Say whether a setting gives back
the original's way. -->

## By choice

<!-- Deliberate: conveniences, and what the original can't do on today's
systems (a CD check, a mode the hardware no longer has). -->
None yet.

## Bugs fixed

<!-- The original's own bugs that Reluva doesn't reproduce. -->
None yet.

## Not yet as the original

<!-- Known gaps: behaviour not done yet, or done differently for now. -->
- **The 3D is drawn by Direct3D's rules,** not Retained Mode's own: its
  lights and shading are Direct3D's, and it doesn't yet sort, fog, or
  draw decals or frames' textures
  ([d3drm.md](../../common/x86/d3drm.md#drawing)). How near it looks to
  the original's hasn't been compared.
- **Turning is too fast** on the island with the arrow keys, as the
  original's is on today's machines, it's known among its players: a
  fix, to turn at the speed it was made for, is still to do
  ([PLAN.md](PLAN.md)).
- **The setup dialog's settings** (`CONFIG.EXE`) aren't offered: the game
  uses its own defaults, but for where it saves and the island's model,
  the detailed one.

## Additions outside the game

<!-- What Reluva adds around the game: viewers, tools, settings. -->
- **Importing** from the CD into one folder, so the game runs without
  it ([systems/game-files.md](systems/game-files.md#importing-it)).
- **The data viewer** and `reluva-tool legoisland` commands, for the SI
  files ([formats/si.md](formats/si.md#in-reluva)), the world
  database ([formats/wdb.md](formats/wdb.md#in-reluva)) and the worlds'
  animation lists ([formats/dta.md](formats/dta.md#in-reluva)).
