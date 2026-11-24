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
- **The 3D is drawn by Direct3D's pipeline,** with Retained Mode's
  documented lighting rules (attenuation, range, parallel point lights)
  ([d3drm.md](../../common/x86/d3drm.md#drawing)). It matches a
  screenshot of the original at the pizzeria; elsewhere it hasn't been
  compared. On the way to the island it blends its translucent faces
  (render mode 1) unsorted, sets no fog, and uses no decals, frame
  textures, material modes or colour keys; those are drawn as the
  documentation describes, should a later part use them.
- **The setup dialog's** (`CONFIG.EXE`) video memory and palette
  choices aren't offered: they're the stand-ins' to decide, and written
  as the setup's defaults. Its others are the engine's settings, each as
  the setup's default unless set, but for the 3D device: the hardware
  one, its textures smoothed, where the setup chose RGB Emulation on a
  player's machine. Its joystick is the first one connected.
- **3D drawn on the GPU** (the *3D drawn by* setting, the GPU unless
  set; the processor where there's none): the same pictures as drawn by
  the processor but for a little rounding, about 1 pixel in 130 a colour
  step off on the island.
- **Each frame is shown a frame late,** when the program gives the next,
  so its drawing goes on while the program works that one out: the
  island's frames are 40 to 54% quicker for it, for a frame's delay
  (17 ms at 60 frames a second) between the player's input and what's
  shown.

## Additions outside the game

<!-- What Reluva adds around the game: viewers, tools, settings. -->
- **Importing** from the CD into one folder, so the game runs without
  it ([systems/game-files.md](systems/game-files.md#importing-it)).
- **The data viewer** and `reluva-tool legoisland` commands, for the SI
  files ([formats/si.md](formats/si.md#in-reluva)), the world
  database ([formats/wdb.md](formats/wdb.md#in-reluva)) and the worlds'
  animation lists ([formats/dta.md](formats/dta.md#in-reluva)).
