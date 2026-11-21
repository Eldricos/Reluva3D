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

In the maps:

- **Surface sprites**' wind gusts come at Reluva's own random times
  ([shader.md](../../formats/shader.md), In Reluva).
- **The weather**'s drops, flakes and gusts are Reluva's own random
  draws, and its snowflakes squares as large as the original's points
  ([systems/weather.md](systems/weather.md)).

In the multiplayer game:

- **Effects** ([efx.md](../../formats/efx.md)): the second
  triangle of a saber's trail is given its corners' own alpha, where
  Raven's code leaves two of them unset. Electricity's small kinks are
  Reluva's own random draws, not the original's, as surface sprites'
  spots are.

In the multiplayer menus:

- **The favourite servers** are kept in `favorites.txt` (address and
  name a line) in the game's save folder, not in the
  original's `servercache.dat`; the other lists aren't kept between runs
  but searched again, as the menus do on opening.
- **The Internet list** asks the community's masters after Raven's,
  which has long been gone.
- **The menus' `R_RemapShader`** changes only the menus' drawing, where
  the original's renderer changes the shader for everything it draws
  ([systems/syscalls.md](systems/syscalls.md)).
- **The menus' videos** never play their sound (every video the shipped
  menus start is silent anyway), and a 17th at once is refused, where
  the original ends the game with an error
  ([systems/syscalls.md](systems/syscalls.md), Videos).

Left for after the rest is done and tested (its phase 11):

- **EAX environmental sound.** The original sets each level's EAX
  environments (`eagle/<map>.eal`) on sound cards that have it; Reluva will
  play the sound without them.
- **Force feedback.** The original plays `forcecfg/*.fcf` effects on force
  feedback joysticks (through `FFC10.dll`); Reluva won't.

## Additions outside the game

None yet.
