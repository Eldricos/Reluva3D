# Jedi Outcast: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

None yet.

## Bugs fixed

None yet.

## Not yet as the original

The game isn't playable yet ([PLAN.md](PLAN.md)). In the maps:

- **Light styles** ([rbsp.md](../../formats/rbsp.md), In Reluva): each
  surface's styles are added into one lightmap, scaled by their colours,
  where the original draws a pass for each. On 7 shaders whose lightmap
  pass isn't their first, that lights the extra styles through the
  texture, where the original adds them on top. A vertex's styles are
  scaled by colour / 255, the original's by 256. Outside a match (the
  World and Maps pages) every style is held full.
- **Surface sprites** (grass, ferns, vines) stand where Reluva's own
  random draws put them, not where the original's do: the same density
  and look, other spots. Wind and weather sprites wait for the game's
  code; until then they sway as with no wind, as the original's do when
  it starts, and weather ones don't show. Their fade distances don't
  lengthen with a narrower field of view yet
  ([shader.md](../../formats/shader.md), In Reluva).
- **Video textures** whose video the paks don't hold (9 of 13
  `videoMap` stages) aren't drawn; the rest of their shaders is.

In the multiplayer game:

- **Movers on recorded paths** (ROFF) stay put until those calls are
  answered ([systems/game-calls.md](systems/game-calls.md)).
- **Ghoul2 animations change at once**, without blending into each other
  as the original's do when asked (0x80), and the bone-angle mode that
  replaces a bone's turn is taken as turning after it
  ([ghoul2.md](../../formats/ghoul2.md), Animation).

In the multiplayer menus:

- **Network play** ([systems/network.md](systems/network.md)): a server
  Reluva hosts doesn't announce itself to the masters, and the client
  doesn't pass the server's level change (`svc_mapchange`) to its client
  game.
- **Effects** ([efx.md](../../formats/efx.md)): electricity is drawn
  without its taper and branches, and effects bolted to a model aren't
  played (1.02a's client game doesn't ask). The second triangle of a
  saber's trail is given its corners' own alpha, where Raven's code
  leaves two of them unset.
- **The favourite servers** are kept in `favorites.txt` (address and
  name a line) in the game's save folder, not in the
  original's `servercache.dat`; the other lists aren't kept between runs
  but searched again, as the menus do on opening.
- **The Internet list** asks the community's masters after Raven's,
  which has long been gone.
- **`R_RemapShader`** isn't answered yet, and no menu plays a video
  ([systems/syscalls.md](systems/syscalls.md)).

Left for after the rest is done and tested (its phase 11):

- **EAX environmental sound.** The original sets each level's EAX
  environments (`eagle/<map>.eal`) on sound cards that have it; Reluva will
  play the sound without them.
- **Force feedback.** The original plays `forcecfg/*.fcf` effects on force
  feedback joysticks (through `FFC10.dll`); Reluva won't.

## Additions outside the game

None yet.
