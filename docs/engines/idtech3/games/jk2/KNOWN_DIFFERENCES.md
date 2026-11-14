# Jedi Outcast: known differences from the original

Where Reluva knowingly differs from the original game. What may differ but
hasn't been settled goes in [UNCONFIRMED.md](UNCONFIRMED.md).

## By choice

None yet.

## Bugs fixed

None yet.

## Not yet as the original

The game isn't playable yet ([PLAN.md](PLAN.md)). In the maps:

- **Light styles.** The original animates each light style as the game's
  code sets it (flickering, switched lights); Reluva holds them all full,
  as the original starts them, and adds each surface's styles into one
  lightmap. On 7 shaders whose lightmap pass isn't their first, that
  lights the extra styles through the texture, where the original adds
  them on top ([rbsp.md](../../formats/rbsp.md), In Reluva). Until the
  game's code runs (phase 6).
- **Surface sprites** (grass, ferns, vines) stand where Reluva's own
  random draws put them, not where the original's do: the same density
  and look, other spots. Wind and weather sprites wait for the game's
  code; until then they sway as with no wind, as the original's do when
  it starts, and weather ones don't show. Their fade distances don't
  lengthen with a narrower field of view yet
  ([shader.md](../../formats/shader.md), In Reluva).
- **Video textures** whose video the paks don't hold (9 of 13
  `videoMap` stages) aren't drawn; the rest of their shaders is.

Left for after the rest is done and tested (its phase 11):

- **EAX environmental sound.** The original sets each level's EAX
  environments (`eagle/<map>.eal`) on sound cards that have it; Reluva will
  play the sound without them.
- **Force feedback.** The original plays `forcecfg/*.fcf` effects on force
  feedback joysticks (through `FFC10.dll`); Reluva won't.

## Additions outside the game

None yet.
