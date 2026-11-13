# Need for Speed III: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **Where it writes.** The game writes its saves and settings to Reluva's
  save folder for the game, never into the install (the emulator's drive C:
  keeps what's written apart, `Win32Process::Drive::writable`).
- **The CD.** The music and movies are read from a copy of the CD's files
  (in the install, beside it, or at `cdpath=`); no disc is needed.
- **Drawn larger.** With a GPU the game is drawn at twice its 640x480
  (`renderscale=` 1 to 4), its textures filtered as its renderers ask
  (`texturefilter=point` point-samples them).
- **The renderer it's told of.** The game is told it has a Voodoo 2 and
  draws as for one, with fog; `renderer=direct3d` tells it of its Direct3D
  renderer instead, `renderer=glide` runs its own Voodoo 2 renderer on the
  Glide stand-in.
- **Movies' black lines** between the picture's lines are filled in
  (`movielines=keep` keeps them).
- **Exit Game** goes back to the launcher.
- **Network play** uses the machine's network: TCP on port 1030, IPX on the
  local network, and serial and modem links as TCP connections between two
  machines (`serial=`, `modem=`).

## Bugs fixed

None yet.

## Not yet as the original

- **Floating point.** Since the merge into main's emulator, the x87 unit's
  transcendental instructions (`FSIN`, `FCOS`, `FPTAN`, `FPATAN`, `F2XM1`,
  `FYL2X`...) aren't rounded to the precision control's 24 bits, as Intel
  documents; before, the branch rounded them. The game's results can
  differ from the earlier runs' in the last bits (the hosted runs' end
  states changed with the merge).
- The computer cars' model on the Tracks page (not the hosted game, which
  runs the game's own) still parts from the game's after about a minute
  ([PLAN.md](PLAN.md), Open questions).

## Additions outside the game

- The data viewer's pages: the folders, tracks and cars in 3D, textures,
  strings, fonts, sounds, music and movies.
- `surround=prologic` (and `passive`, `headphones`) decodes the game's
  Dolby Surround mode with a model of a Pro Logic decoder, to a 4.0 or 5.1
  device.
- Game controllers offered to the game as a joystick (a racing wheel as a
  wheel), with force feedback to a wheel's motor or a pad's rumble.
- `positionalupdates=on`: network races with the game's positional
  updates, meant for slow links.
- `reluva-tool nfs` commands for studying the game's files and running
  the game without a window ([REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md)).
