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
reads, speech included. The CD can be imported instead, laid out as its
setup installs it ([cd.md](formats/cd.md)).

## Where it stands

| Part | State |
|---|---|
| Detection | The US release by its data files' hashes; other releases by file names, as "unknown release" |
| Importing | From the CD: its InstallShield 6 cabinet and its language's folders, as setup installs them; checked byte for byte against an install ([cd.md](formats/cd.md)) |
| Archives | done: `.wad` ([wad.md](formats/wad.md)); all 303 archives' 15,007 files read; the `Data` folder mounted, archives and loose files together; `reluva-tool tpw wad-list`, `wad-extract` and `data-check` |
| Settings | done: `.sam` ([sam.md](formats/sam.md)); all 369 files' 15,825 settings read; `reluva-tool tpw sam-check` |
| The executable | Surveyed: SafeDisc 2, its code encrypted, so the game is to be rebuilt from its data, not run ([executable.md](formats/executable.md)) |
| Everything else | Not started: see [PLAN.md](PLAN.md) |

## Using it

In the launcher, *Import Game...* and choose the CD (or a copy of its
files): Reluva copies what the game needs into a folder of its own, as
its setup would install it. A folder the game's own setup installed can
be added instead (*Add Game...*). Reluva shows an empty screen for now;
Esc leaves it.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| `formats/` | The game's own files, a page each: [the executable](formats/executable.md), [the CD](formats/cd.md), [archives](formats/wad.md), [settings](formats/sam.md) |
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
