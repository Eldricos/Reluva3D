# Jedi Outcast: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

- **Light styles in the viewer**: the World and Maps pages hold every
  style full, showing a level's light as compiled. In a match they change
  as the client game sets them, as the original's do
  ([rbsp.md](../../formats/rbsp.md), In Reluva).

## Bugs fixed

None yet.

## Not yet as the original

The game isn't playable yet ([PLAN.md](PLAN.md)). In the maps:

- **Surface sprites** (grass, ferns, vines) on far curves stay where
  they stand up close, where the original's move as it drops the curve's
  detail; the wind's gusts come at Reluva's own random times; and the
  rain and snow system (`r_we`), which would also set the wind and the
  weather sprites, isn't there ([shader.md](../../formats/shader.md),
  In Reluva).

In the multiplayer game:

- **Ghoul2's bone-angle mode** that replaces a bone's turn is taken as
  turning after it ([ghoul2.md](../../formats/ghoul2.md), Animation).

In the multiplayer menus:

- **Recorded paths** ([roff.md](../../formats/roff.md)): the client
  game's side follows its own clock where the original's follows the
  server's.
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
