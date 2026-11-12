# The NetImmerse engine

Engine id `netimmerse`, in `src/engines/netimmerse/`, for games built on
NetImmerse (Numerical Design, later Gamebryo). First game: LEGO Alpha Team
(LEGO Media, 2000), game id `legoalphateam`, in `alphateam/`.

## Games

| Game | Id | Docs |
|---|---|---|
| LEGO Alpha Team (LEGO Media, 2000) | `legoalphateam` | [games/legoalphateam/](games/legoalphateam/README.md): where it stands, using it, its formats, how it runs and each part of it, and its known differences, unconfirmed items, testing and how it was worked out |

A page goes in a game's folder when it was worked out from that game, and
moves up to this folder once another game is found to share it. So far
everything was read from LEGO Alpha Team.

## This folder

| Page | Covers |
|---|---|
| This page | The engine, where its code goes, and its tests |
| [games/](games/) | A folder per game, for what was worked out from it |

## Code

Following idtech3/eliteforce: an engine for the technology, a subfolder
for the game.

- Engine id `netimmerse`, in `src/engines/netimmerse/`: `.pac` archives,
  NIF reading (NetImmerse's stream format for a scene graph and its
  objects, which the game's `.FIN` models are, under a header of their
  own: [fin.md](games/legoalphateam/formats/fin.md)), the scene graph, rendering through `gfx::Renderer3D`, sound
  through `common/audio`. Built when `engines.cfg` has `netimmerse=enabled`, as it does by
  default (it was off at first, which left the launcher not recognising
  the game in a default build).
- Game id `legoalphateam`, with what is specific to it in
  `src/engines/netimmerse/alphateam/`.
- Formats documented under
  [games/legoalphateam/formats/](games/legoalphateam/formats/) (`.pac`,
  the game's own files, and NIF as this game uses it), with how each was
  worked out, as CONTRIBUTING.md asks.
- Tests in `tests/engines/netimmerse/unit/formats.cpp` (`netimmerse.unit.formats`), on small files built by the tests
  themselves (no game data in the repository).

If later LEGO games turn out to use NetImmerse too, they join this
engine; others get their own. Check each one's install for `Ni*.dll`
before deciding.

## Tests

As every engine's ([../../DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests)),
in `tests/engines/netimmerse/`:

- **Unit tests** (`reluva-tests netimmerse.unit.formats`): the `.pac`,
  `.DDS`, text, `.FIN` and `.DDF` readers, on files built in the test.
- **The basic engine test** (`reluva-tests netimmerse.engine`), without the
  game's files: a small install of its own, recognised, and shown page by
  page in the data viewer; its record is `data/engine-record.txt`.
- **The game-files test** (`gamedata.cmake`), with your own install: set
  `RELUVA_ALPHATEAM` to the game's folder and run
  `ctest -L gamedata -R netimmerse`. It lists the archives, reads every
  model, and runs the game's own code: its start, and Mission One played
  through at 640x480 and 1024x768. Records are in
  `data/gamedata/english/`. [games/legoalphateam/TESTING.md](games/legoalphateam/TESTING.md) has the details.

The emulator, the Windows stand-ins, DirectX, A3D and the Indeo video it
uses are tested with the shared code (`reluva-tests common.unit.jit`,
`common.unit.windows`, `common.unit.indeo`).
