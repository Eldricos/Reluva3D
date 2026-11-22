# SimCoaster

Bullfrog's theme park game of 2001, published by Electronic Arts: build
parks in three themed worlds (Arabian Nights, New Science, Water World)
around roller coasters of the player's own design, meet the challenges
of its missions, and keep the guests happy. Released in Europe as Theme
Park Inc. Game id `simcoaster`, on the
[Theme Park World engine](../../README.md); its code is in
`src/engines/tpw/`.

## Releases

| Release | Recognised by | State |
|---|---|---|
| US, English, Windows (data version `Beta_21`) | `Data/ui.wad` (MD5 `f8caf97175f18ef3675b3609b03413f4`, 2,817,221 bytes) and `Data/Language/American/OBJECT_NAMES.str` (MD5 `04d5bf6ef8a6d85f15a25a33716fd895`, 10,200 bytes), with `Game.exe` | Recognised; shows an empty screen |
| Any other (Theme Park Inc, other languages) | `Data/ui.wad`, `Data/Challenges.sam` and `Game.exe` by name | Shown as "unknown release"; not checked |

An installed folder is what's recognised: it holds every file the game
reads, speech included. The CD's own files are in an InstallShield 6
cabinet, which Reluva doesn't read yet, so the CD can't be imported
([PLAN.md](PLAN.md#phases)).

## Where it stands

| Part | State |
|---|---|
| Detection | The US release by its data files' hashes; other releases by file names, as "unknown release" |
| The executable | Surveyed: SafeDisc 2, its code encrypted, so the game is to be rebuilt from its data, not run ([executable.md](formats/executable.md)) |
| Everything else | Not started: see [PLAN.md](PLAN.md) |

## Using it

Install the game from its CD with its own setup program (on Windows, or
in a virtual machine), and add the folder it was installed to in the
launcher (*Add Game...*). Reluva shows an empty screen for now; Esc
leaves it.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| `formats/` | The game's own files, a page each: [the executable](formats/executable.md) |
| `systems/` | How the game behaves, a page per part |
| `research/` | Working notes |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Terms

- **World:** one of the game's three themed lands (`arabian`, `science`,
  `water` in `Data/levels/`), each with its own rides, shops and scenery;
  `wIntro` is the opening's.
- **Challenge:** a goal set by the game's advisor, from
  `Data/Challenges.sam`.
