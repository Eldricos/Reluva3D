# Testing (Star Trek: New Worlds)

## Automated checks

### Unit tests

`tests/engines/newworlds/unit.cpp`, without the game's files:

| Suite | What it checks |
|---|---|
| `newworlds.unit.archives` | `.cat` names, reading, and rejecting damaged archives |
| `newworlds.unit.files` | String tables, textures, `.geo`, `.gmf`, `.mdl`, `.arf`, `.fif`, `level.dat`, model textures |
| `newworlds.unit.game` | Briefings, the mission scripts (every recorded mission's walkthrough), the colony, star systems, model poses, the economy, the world |
| `newworlds.unit.render` | The rasterizer |

### Not there yet

- **The basic engine test** (`newworlds.engine`) and **the game-files
  test**: see [DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests) for what
  they are.

### With the game's files, by hand

- `reluva-tool newworlds cat-probe` on the install: all 24 archives of the
  retail install pass ([cat.md](cat.md#verified)).

## Against the original

What to play and record in the original to check Reluva against it, and
fill what's still unknown: [vm-checklist.md](vm-checklist.md). What's
unconfirmed so far: [UNCONFIRMED.md](UNCONFIRMED.md).
