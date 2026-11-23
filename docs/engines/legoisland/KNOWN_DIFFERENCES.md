# LEGO Island: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

<!-- Each item: what the original does, what Reluva does, and why, with a
link to the page that has the details. Say whether a setting gives back
the original's way. -->

## By choice

<!-- Deliberate: conveniences, and what the original can't do on today's
systems (a CD check, a mode the hardware no longer has). -->
- **Turning:** the original turns the player a set amount each frame,
  so on today's machines, drawing many more frames a second, far too
  fast (known among its players). Reluva turns it by time, as fast as
  at 20 frames a second, however many it draws; the *Turning* setting
  gives back the original's way, with the *Frame rate* setting at 20 to
  match ([systems/running.md](systems/running.md#in-reluva)).

## Bugs fixed

<!-- The original's own bugs that Reluva doesn't reproduce. -->
None yet.

## Not yet as the original

<!-- Known gaps: behaviour not done yet, or done differently for now. -->
- **The 3D is drawn by Direct3D's rules,** not Retained Mode's own: its
  lights and shading are Direct3D's
  ([d3drm.md](../../common/x86/d3drm.md#drawing)). It matches a
  screenshot of the original at the pizzeria; elsewhere it hasn't been
  compared. Decals and frames' textures aren't drawn, but the game
  hasn't been seen to use them: on the way to the island it blends its
  translucent faces (render mode 1) unsorted, sets no fog, and no
  texture of its has a colour key.
- **The setup dialog's** (`CONFIG.EXE`) video memory, palette and device
  choices aren't offered: they're the stand-ins' to decide. Its others
  are the engine's settings, the island's model at High unless set (the
  game's own default is Medium); its joystick is the first one
  connected.

## Additions outside the game

<!-- What Reluva adds around the game: viewers, tools, settings. -->
- **Importing** from the CD into one folder, so the game runs without
  it ([systems/game-files.md](systems/game-files.md#importing-it)).
- **The data viewer** and `reluva-tool legoisland` commands, for the SI
  files ([formats/si.md](formats/si.md#in-reluva)), the world
  database ([formats/wdb.md](formats/wdb.md#in-reluva)) and the worlds'
  animation lists ([formats/dta.md](formats/dta.md#in-reluva)).
