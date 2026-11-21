# Jedi Outcast: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **It starts in a window**: `r_fullscreen` defaults to 0, where
  `jk2mp.exe` starts fullscreen. Every Reluva game starts in a window;
  the Video page's Full Screen line changes it.
- **Light styles in the viewer**: the World and Maps pages hold every
  style full, showing a level's light as compiled. In a match they change
  as the client game sets them, as the original's do
  ([rbsp.md](../../formats/rbsp.md), In Reluva).
- **The Internet list** asks the community's masters after Raven's,
  which has long been gone: the masters are `sv_master1` to `sv_master5`,
  Raven's first ([systems/network.md](systems/network.md)).
- **The single-player menus' renderer description** (owner draw 249)
  says Reluva's renderer's GPU, driver and version; it isn't OpenGL, so
  there's no vendor, pixel format or extensions to show
  ([systems/sp-menus.md](systems/sp-menus.md)).
- **No CD key check**: a server lets every player in, and a client sends
  no key to be checked. The original's server asks Raven's authorize
  server about each player's key, but that server has long been gone,
  so there's nothing left to ask, and every server has let everyone in
  since ([systems/network.md](systems/network.md)).

## Bugs fixed

- **Pasting a ^V** (Ctrl+V's character) into the console is dropped;
  the original's paste pastes again for it, within itself, without end
  ([systems/console.md](systems/console.md)).

- **A bolted effect** (the client game's call 261) starts at the origin
  it's given, where the original starts its parts at, and culls them by,
  a place it never sets ([efx.md](../../formats/efx.md), Bolted effects).
- **A wind from `r_we snow wind`** blows on the snow here. The original
  never makes its list of counts and would read one that isn't there
  once the eye came within 300 of it *(inferred)*
  ([systems/weather.md](systems/weather.md)).

## Not yet as the original

- **The light amplification goggles' look.** Single player's client game
  calls `R_LAGoggles` (its call 64) each frame they're worn; the original
  then draws everything in a fog of its own with a flickering density,
  clears the screen to its colour and changes some images and lighting
  (jk2sp.exe 0x45fa60, and the paths reading 0x12e4020). Reluva ignores
  the call, so the view looks as without them.
- **Fog on sprites and beams.** The original picks a fog for each sprite
  entity the way it does for models (0x472990); Reluva fogs models but
  draws sprites, beams and other effects unfogged.

Left for after the rest is done and tested (its phase 11):

- **EAX environmental sound.** The original sets each level's EAX
  environments (`eagle/<map>.eal`) on sound cards that have it; Reluva will
  play the sound without them.
- **Force feedback.** The original plays `forcecfg/*.fcf` effects on force
  feedback joysticks (through `FFC10.dll`); Reluva won't.

## Additions outside the game

None yet.
