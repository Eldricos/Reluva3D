# Page templates

Every page in `docs/` starts from one of these. The layout they fit into is
described in [docs/README.md](../README.md#adding-docs). Guidance is in
HTML comments (`<!-- -->`), which show while editing and not on the page;
delete each once its section is written.

| Template | Becomes | Made by |
|---|---|---|
| [engine/README.md](engine/README.md) | `docs/engines/<engine>/README.md` | `cmake/NewEngine.cmake` |
| [engine/KNOWN_DIFFERENCES.md](engine/KNOWN_DIFFERENCES.md), [UNCONFIRMED.md](engine/UNCONFIRMED.md), [TESTING.md](engine/TESTING.md), [REVERSE_ENGINEERING.md](engine/REVERSE_ENGINEERING.md) | The same pages beside it (or in a game's folder) | `cmake/NewEngine.cmake` |
| [game/README.md](game/README.md) | `docs/engines/<engine>/games/<game id>/README.md`, for an engine meant for several games | Copied by hand |
| [format.md](format.md) | A page in `formats/`: a file's layout | Copied by hand |
| [system.md](system.md) | A page in `systems/`: how part of the game behaves | Copied by hand |
| [common.md](common.md) | A page in `docs/common/`: a shared component or format | Copied by hand |
| [plan.md](plan.md) | `PLAN.md`, optional, while an engine or game is being built | Copied by hand |

**Placeholders.** In the engine templates, `@ENGINE@` and `@TITLE@` are
the engine's id and name, filled in by `cmake/NewEngine.cmake`, so those
templates must hold no other `@word@`. In the others, `<...>` marks what
to replace.
