# Known differences from the original

Where Reluva deliberately behaves differently from Star Trek: Voyager -
Elite Force (`stvoy.exe`, `stvoyHM.exe` and the game's own code), leaves
something out, or adds something. Anything not listed here is meant to
behave as the original does; a difference that isn't listed is a bug.

Each entry says what the original does, what Reluva does and why, and
where it's described in more detail. Points that are assumed rather than
confirmed are in [UNCONFIRMED.md](UNCONFIRMED.md).

## Installing, files and saved games

- **No installer.** The original is installed by its setup program, which
  also writes registry entries and Start menu items.
  - Reluva copies only the files it needs from the CD (or an install) into
    its own library folder (`<data>/library/stvef`).
  - It takes the 1.2 patch's files out of its Wise installer without
    running it. It recognises files by their contents and doesn't read the
    installer's script.
  - This works the same on every system.
  - See [idtech3.md](engines/idtech3.md) (Import) and
    [wise.md](formats/wise.md).
- **The Expansion Pack, switchable.** Installing the original's Expansion
  replaces the single-player DLLs.
  - Reluva keeps the Expansion's DLLs in `expansion/`, beside 1.2's.
  - Each game in the launcher has a *Use the Expansion Pack* setting.
    With it off, `pak3.pk3` isn't loaded and 1.2's DLLs are used.
  - The same folder added twice gives both versions.
  - See [idtech3.md](engines/idtech3.md) (Detection).
- **Saves kept apart by version.** The original keeps 1.x and Expansion
  saves in one `saves/` folder, although neither version's game can load
  the other's: the Expansion's player chunk is longer.
  - Reluva keeps the Expansion's in `expansion/saves/` and moves any save
    found in the wrong folder.
  - Its Load menu leaves out the other version's saves, and loading one is
    refused with a message.
  - See [eliteforce-sp-interface.md](formats/idtech3/eliteforce-sp-interface.md)
    ("The Expansion Pack's saves").
- **Where the player's files go.** The original writes into its install
  folder. Reluva writes into its own save folder for each game:
  - settings and key bindings in `reluva.cfg`, not
    `efconfig.cfg`/`hmconfig.cfg`;
  - saved games and screenshots;
  - the CD key in `cdkey.txt`;
  - paks downloaded from servers, in `<game folder>/` there, searched above
    the install's;
  - `menus-stopped.txt`, a report Reluva adds when the menus stop on an
    error.
- **Mods' own files.** The original writes a mod's files into the mod's
  folder.
  - Reluva puts a mod's saves, settings and screenshots in the save
    folder's `mods/<mod>/`. Its settings start as a copy of the game's.
  - The CD key is shared between mods.
  - See [idtech3.md](engines/idtech3.md) (Mods).
- **Mods in single player switch at once.** The original single-player
  engine reads `fs_game` only as it starts, so its Mods screen takes effect
  at the next launch. Reluva restarts the game's files at once, as
  Holomatch's engine does. The launcher's *Mod* setting stands in for the
  original's `+set fs_game` command line.
- **One file order on every system.** Wherever a folder's listing reaches
  the game (loose files, the mods list, which pak overrides which), Reluva
  sorts names as Windows lists an NTFS folder: `a`-`z` compared as capitals,
  so `_` comes after the letters. Linux and macOS then give the game the
  same order as Windows. See
  [eliteforce-sp-interface.md](formats/idtech3/eliteforce-sp-interface.md)
  ("Listing files").
- **Pak archives** are checked against each file's CRC. Encrypted, Zip64
  and split archives aren't read (none are expected in game data). See
  [pk3.md](formats/idtech3/pk3.md).
- **Detection.** Releases are recognised by the contents of their files, not
  by the registry. Only the English Windows release and its patches are
  known. Others are shown as "unknown release".

## Rendering

- **Frame size.** The frame is drawn at the chosen resolution and
  letterboxed in the window. Fullscreen uses the desktop's display mode
  rather than switching modes. Until a mode is chosen, the size is 960x720.
- **Brightness** (`r_gamma`) is a final pass on the GPU, standing in for
  the original's hardware gamma ramp. It affects only the game's frame,
  not the whole screen.
- **Anisotropic filtering**, when turned on, is always 16x.
- **Lightmaps** are always brightened twice over. The original doubles or
  quadruples them depending on its gamma mode; doubling matches its look in
  a window. See [shader.md](formats/idtech3/shader.md).
- **Curved surfaces** are always drawn at full detail. The original lowers
  their detail with distance (`r_lodCurveError`). See
  [bsp.md](formats/idtech3/bsp.md).
- **Collision with curved surfaces** is Reluva's own method: the curves are
  cut into small triangles that collide like thin brushes. It's close to the
  original but not identical. See [bsp.md](formats/idtech3/bsp.md)
  ("Collision").
- **Animated models in Elite Force's MDR format** are posed at load and
  blended on the GPU. The original blends the bones. It differs only in
  how a blend between two very different frames bends. See
  [mdr.md](formats/idtech3/mdr.md).
- **Unreadable map surfaces.** A surface of an unknown type is skipped,
  where Quake III refuses the map.
- **Not drawn yet:**
  - the engine's shadow modes 2 and 3 (`cg_shadows`, which Elite Force's
    renderer reads as Quake III's `r_shadows`): stencil and projected
    shadows, and the `projectionShadow` deform. Elite Force's menus don't
    offer them; mode 1, the blob under each player, is drawn;
  - single player's type 3 beam entity;
  - `deformVertexes text*` and `GL_SRC_ALPHA_SATURATE` blending;
  - autosprites on models (no model uses them).
- **Drawn approximately:** `rgbGen lightingDiffuse` uses vertex colours, and
  `alphaGen portal`, `lightingSpecular` and `entity` are opaque. See
  [shader.md](formats/idtech3/shader.md).
- **Ignored:** single player's `R_Scissor` call, which nothing in the game
  calls.

## Sound

- **No limit of copies per sound.** Quake III caps one sound at 5 copies per
  entity. Reluva leaves that out, because it silenced rapid fire. It keeps
  the rule that the same sound on the same entity within 50 ms is a
  repeat, and is skipped.
- **Menu sounds** play at the sound volume. A new sound on a channel other
  than 0 replaces the one playing there.
- **Force feedback** (`ff_usejoy`, the client game's calls 34-37, through
  `FFC10.dll`) is ignored.

## Single player

- **The Single Player page's own keys:**
  - Ctrl+R restarts the level;
  - Ctrl+PgUp and Ctrl+PgDn change map;
  - Ctrl+I hides the info line;
  - Esc returns to the menus.
- **Cinematics** play under their plain names. The original also looks for
  `_lo` versions on slow machines, and has a check for the German release.
- **The `STRP` save chunk**, an engine table of the client's, is written
  empty and skipped when read.
- **The game's code runs in an x86 interpreter,** not natively:
  - the processor reports itself as a Pentium, and Windows as XP;
  - a null pointer is caught;
  - Windows' structured exceptions end the run;
  - there's no MMX or SSE, which the game doesn't use.

  See [x86.md](engines/x86.md).

## Holomatch and network play

- **Master servers.** Raven's master server is gone. When the menus ask for
  it, Reluva asks every master set (`sv_master1`-`5`), which includes the
  community's `efmaster.tjps.eu`. It always asks for empty and full servers
  too. See [eliteforce-network.md](formats/idtech3/eliteforce-network.md).
- **Every level is open to others.** The Play page hosts each level it
  starts on port 27960 (or the next free of 27961-27963), when the game
  code is 1.2's. `reluva-tool net-run` hosts without a local player.
- **Pure servers:**
  - Reluva's server lists the client game's and menus' paks among the
    level's even when it isn't pure.
  - It isn't pure, with a note, if its own client game or menus aren't in a
    pak.
  - Reluva's client lists every pak of its own the server names, so it
    passes the server's check.
- **User info** is sent on connecting. The original also sends it whenever a
  player setting changes.
- **Smaller details:**
  - packets carrying an earlier level's id after a restart are ignored
    quietly;
  - the server's frame runs before the local player's command is stamped;
  - a player-state word the protocol doesn't send is zeroed.

  See [eliteforce-syscalls.md](formats/idtech3/eliteforce-syscalls.md).

## Menus and input

- **The CD key.** The original checked keys with an online service that no
  longer runs. Reluva applies only the menus' own rule, and treats a valid
  key as accepted.
- **The 1.2 mode buttons.** The original shows them only when the
  Expansion's key file differs from the game's. Reluva shows them anyway,
  unless `reluva_modeButtons` is 0.
- **The menus' pointer** follows the system mouse.
- **The joystick** is read through SDL rather than Windows' joystick API.
  `in_joystick` takes effect at once; the original waits for `in_restart`.
  See [eliteforce-input.md](formats/idtech3/eliteforce-input.md).

## Bots

- **Routing** finds the quickest route out from the goal over the whole map,
  and keeps it until an area opens or closes. The original speeds this up
  with clusters, portals and caches. The routes are the same kind, not
  proven identical. See [aas.md](formats/idtech3/aas.md).
- **Bots don't answer chat.** The game has no reply-chat file
  (`rchat.c`). The code to answer is there, but the data isn't.

## Game code (QVMs)

- **Safety checks.** The original wraps addresses within the program's
  memory. Reluva checks every access, jump and call, and stops the program
  with an error instead. It also:
  - limits how long a call may run;
  - treats division by zero as an error;
  - keeps `switch` tables read-only.

  See [qvm.md](formats/idtech3/qvm.md).

## Videos

- **Bink sound** is clipped before overlapping blocks are blended, which
  differs from FFmpeg's decoder on a few peaks. Colours are converted as
  BT.601 studio range. See [bink.md](formats/bink.md).
