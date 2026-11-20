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
- **The console key**: `~` (or `` ` ``) opens the console on its own. The
  original's multiplayer engine opens it only with Shift held (0x4099dc:
  the key alone, with the console shut, goes on to the bindings and
  menus). Its default config binds nothing to the key, so only a
  player's own binding on it is lost.

## Bugs fixed

None yet.

## Not yet as the original

In the maps:

- **Surface sprites** (grass, ferns, vines) on far curves stay where
  they stand up close, where the original's move as it drops the curve's
  detail; the wind's gusts come at Reluva's own random times; and
  multiplayer's rain and snow system (`r_we`), which would also set the
  wind and the weather sprites, isn't there ([shader.md](../../formats/shader.md),
  In Reluva). Single player's rain is there, its drops and gusts
  Reluva's own random draws ([systems/sp-weather.md](systems/sp-weather.md)).

In the multiplayer game:

- **The console** is drawn as the original's, but for small details:
  its typing line's scroll, a colour carried from one printed line to
  the next, and how far a key scrolls back
  ([systems/console.md](systems/console.md), In Reluva).
- **Network play** ([systems/network.md](systems/network.md)): a server's
  map change notices all go at once, where the original's go at steps of
  its loading; it looks its masters' names up once, where the original
  looks again when one's cvar changes.
- **Effects** ([efx.md](../../formats/efx.md)): effects bolted to a
  model aren't played (1.02a's client game doesn't ask). The second
  triangle of a saber's trail is given its corners' own alpha, where
  Raven's code leaves two of them unset. Electricity's small kinks are
  Reluva's own random draws, not the original's, as surface sprites'
  spots are.

In the 1.03 and 1.04 patches' multiplayer:

- **Ghoul2's new bolt call** (the client game's 285, the game module's
  588) is answered as the one before it, so a bolt it asks for keeps
  its turn where the original's may leave it out
  ([systems/cgame-calls.md](systems/cgame-calls.md), Patches).
- **The text calls' punctuation**: whether a character may end a line
  is answered from Reluva's own list (`.,!?;:`), not the original's
  ([systems/syscalls.md](systems/syscalls.md), Patches).
- **Keys the patches have and Quake III hasn't** (`CONSOLE`, `EURO`,
  `SCROLLLOCK` and others) never reach the menus: Reluva's keys are Quake
  III's.

In the multiplayer menus:

- **The favourite servers** are kept in `favorites.txt` (address and
  name a line) in the game's save folder, not in the
  original's `servercache.dat`; the other lists aren't kept between runs
  but searched again, as the menus do on opening.
- **The Internet list** asks the community's masters after Raven's,
  which has long been gone.
- **The menus' `R_RemapShader`** isn't answered (no 1.02a menu uses the
  script command that calls it), and no menu plays a video
  ([systems/syscalls.md](systems/syscalls.md)).

Left for after the rest is done and tested (its phase 11):

- **EAX environmental sound.** The original sets each level's EAX
  environments (`eagle/<map>.eal`) on sound cards that have it; Reluva will
  play the sound without them.
- **Force feedback.** The original plays `forcecfg/*.fcf` effects on force
  feedback joysticks (through `FFC10.dll`); Reluva won't.

## Additions outside the game

None yet.
