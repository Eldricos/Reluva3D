# The LEGO Island engine

LEGO Island (Mindscape, 1997), an open-world adventure on a LEGO island
for young players: an engine for this one game. Its code is almost all in
`LEGO1.DLL`, the "LegoOmni Library", which is to be run in Reluva's x86
interpreter with Windows and DirectX stood in for ([PLAN.md](PLAN.md)).
Engine id `legoisland`, in `src/engines/legoisland/`.

## Games

| Game | Game id | Publisher, year | Platform | Releases recognised | State |
|---|---|---|---|---|---|
| LEGO Island | `legoisland` | Mindscape / LEGO Media, 1997 | Windows | None yet; the English 1.0 release is next | Not yet recognised |

## Where it stands

Started from the template: it recognises nothing yet, and shows an empty
screen. The plan is in [PLAN.md](PLAN.md).

## Using it

Nothing to use yet: the game isn't recognised.

## This folder

| Page | What's in it |
|---|---|
| This page | The games, where the engine stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| `formats/` | A page per file format, as it's worked out |
| `systems/` | A page per part of how the game behaves (scripts, dialogue, saves, AI...) |
| `research/` | Working notes: address maps, raw findings, notes on recordings |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Code

| Path | What |
|---|---|
| `legoisland.cpp` | The `MetaEngine` (name, detection table, making an `Engine`) and the `Engine` |
| `tools/` | `reluva-tool legoisland` commands (`reluva-tool help legoisland` lists them) |

## Tests

- `reluva-tests legoisland` runs its unit tests and its engine test
  (`tests/engines/legoisland/`).
- A game-files test, when it has one: see
  [docs/DEVELOPMENT.md](../../DEVELOPMENT.md#engine-tests).
- What they cover, and what to check by hand: [TESTING.md](TESTING.md).

## Terms

- **Omni:** Mindscape's engine under the game; `LEGO1.DLL` names its
  classes `Mx...` (the engine) and `Lego...` (the game).
- **SI (Interleaf):** the archives on the CD holding the game's scripts
  and the media they play, laid out to be streamed from the CD.
- **Presenter:** the class an SI object names to play it (a sound, an
  animation, a model).
