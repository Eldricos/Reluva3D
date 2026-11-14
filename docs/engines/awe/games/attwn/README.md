# And Then There Were None

Game id `attwn`, the second game on the [AWE engine](../../README.md),
after [Murder on the Orient Express](../motoe/README.md); its own code is
in `src/engines/awe/attwn/`.

## Releases

| Release | Recognised by | State |
|---|---|---|
| English (retail, Windows, two CDs, 2005) | `ac.exe` and `Text/itemlist.act` | Its files read; not playable yet |
| Other releases | The same files and `granny2.dll`, whatever their contents (a fallback) | Not tried |

## Where it stands

Phases 1 and 2 of [PLAN.md](PLAN.md) are done:

- **Its files:** the game is recognised, and every file of the install
  is read as the game opens it, its renamed files by their ordinary names
  and its text unscrambled ([formats/files.md](formats/files.md)).
- **Its scenes:** all 537 (`.ac1`) are read, their cameras the same as
  the game's Granny builds them, and the data viewer draws them at the
  game's 800×600 with their characters ([formats/ac1.md](formats/ac1.md)).

Phase 3, what differs from Orient Express in their shared code, is
under way: its script words ([systems/script.md](systems/script.md)),
and the path-finder, the same code in both
([formats/ac1.md](formats/ac1.md#the-walk-grid-and-the-path-finder)).
Nothing is played yet: `reluva` says so.

## Using it

Add the installed game's folder (the one holding `ac.exe`) in the
launcher. From the two CDs without installing: `unshield x data1.cab`
with `data1.hdr` and the three cabinets in one folder; the game is
`Program Executable Files` with `ac.exe` (from `Txt Files`) beside it.

- **The game:** not playable yet.
- **The data viewer:** `reluva-viewer <entry>` or `reluva-viewer <game
  folder>`: Scenes (the scenes' files, an act's version over its base;
  their keys at the foot of the screen), Models, Movies, Pictures, Text
  and Sounds; Tab moves between them.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [formats/](formats/) | The game's files: [names and scrambled text](formats/files.md), [scenes (`.ac1`)](formats/ac1.md) |
| [systems/](systems/) | How the game behaves: [the script runner](systems/script.md) |
| [PLAN.md](PLAN.md) | The survey of the install, the phases, the decisions |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

What the AWE games share is in the engine's folder:
[Granny 2](../../formats/gr2.md). Orient Express's pages
([../motoe/](../motoe/README.md)) describe the rest of the engine as that
game has it, and are the map for this one.
