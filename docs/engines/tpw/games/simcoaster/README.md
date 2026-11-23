# SimCoaster

Bullfrog's theme park game of 2001, published by Electronic Arts: build
parks in three themed worlds (Land of Invention, Polar Zone, Arabian
Nights)
around roller coasters of the player's own design, meet the challenges
of its missions, and keep the guests happy. Released in Europe as Theme
Park Inc. Game id `simcoaster`, on the
[Theme Park World engine](../../README.md); its code is in
`src/engines/tpw/`.

## Releases

| Release | Recognised by | State |
|---|---|---|
| US, English, Windows (data version `Beta_21`) | `Data/ui.wad` (MD5 `f8caf97175f18ef3675b3609b03413f4`, 2,817,221 bytes) and `Data/Language/American/OBJECT_NAMES.str` (MD5 `04d5bf6ef8a6d85f15a25a33716fd895`, 10,200 bytes), with `Game.exe` | Recognised; plays its opening, then an empty screen |
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
| Text | done: string tables (`.str`) and the character set ([str.md](formats/str.md)); all 37 tables' 3,775 strings read; `reluva-tool tpw str-check` |
| Fonts | `.bf4` ([bf4.md](formats/bf4.md)): all 35 fonts' 8,750 glyphs read, but for the postcard font's 160 of a storage not worked out; `reluva-tool tpw bf4-check` and `bf4-show` |
| Pictures | done: `.fsh` banks (compressed pictures among them), `.tga`, `.png` and `.jpg` ([fsh.md](formats/fsh.md)); all 10,344 decoded; `reluva-tool tpw picture-check` and `picture-dump` |
| Movies | done: the EA GAMES logo (`.tgq`, TQI video, [tgq.md](formats/tgq.md)), decoded and played; `reluva-tool tpw tgq-check` and `movie-check` |
| The opening | the logo, then the Bullfrog splash, as a recording shows them ([opening.md](systems/opening.md)); the front end's 3D opening not yet |
| Sounds | the banks (`.sdt`, [sdt.md](formats/sdt.md)): 5,315 of their sounds decode, MPEG and PCM; the rest are damaged in the game's own files; `reluva-tool tpw sdt-check`. The `.map` files beside them not yet |
| Models | `.MD2` ([md2.md](formats/md2.md)): all 698 models' 6,981 meshes read, placed by their node trees; their 1,209 animations not yet; `reluva-tool tpw md2-check` |
| Viewer | `reluva-viewer`: pictures, fonts, text, settings, sounds, movies and files (pages above) |
| The executable | Surveyed: SafeDisc 2, its code encrypted, so the game is to be rebuilt from its data, not run ([executable.md](formats/executable.md)) |
| Everything else | Not started: see [PLAN.md](PLAN.md) |

## Using it

In the launcher, *Import Game...* and choose the CD (or a copy of its
files): Reluva copies what the game needs into a folder of its own, as
its setup would install it. A folder the game's own setup installed can
be added instead (*Add Game...*). Reluva plays the game's opening (a
key or a click skips each part), then shows an empty screen for now;
Esc leaves it.

The data viewer, `reluva-viewer` (`reluva-viewer <its name in the
launcher>` or `reluva-viewer <the game's folder>`), shows what the game's
files hold, at 640 × 480. Tab and Shift+Tab move between its pages, Esc
leaves it. On every page Left and Right go to the previous and next
thing, PageUp and PageDown 10 at a time, Home and End to the first and
last; each page has its own keys besides:

| Page | Shows | Its own keys |
|---|---|---|
| Pictures | Every `.fsh`, `.tga`, `.png` and `.jpg`, zoomed to fit, over a check where it's clear | Z: its own size |
| Fonts | Sample text in the font, then each glyph with its code | |
| Text | A string table's strings by number, the one picked whole below | Up/Down: the string; F: in the game's font (`GAME12.bf4`) |
| Settings | A settings file's settings, values and grids | Up/Down: scroll |
| Sounds | A bank's sounds, their kind and length | Up/Down, PageUp/PageDown: the sound; Space or Enter: play or stop |
| Movies | The logo and the training video | Space or Enter: play (a key or a click stops it) |
| Files | Every file of `Data/`, where it is, its start as text or bytes | Up/Down, PageUp/PageDown: the file |

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases and the decisions taken |
| `formats/` | The game's own files, a page each: [the executable](formats/executable.md), [the CD](formats/cd.md), [archives](formats/wad.md), [settings](formats/sam.md), [text](formats/str.md), [fonts](formats/bf4.md), [pictures](formats/fsh.md), [movies](formats/tgq.md), [sound banks](formats/sdt.md), [models](formats/md2.md) |
| `systems/` | How the game behaves, a page per part: [the opening](systems/opening.md) |
| `research/` | Working notes: [what a recording of the game shows](research/footage.md) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Terms

- **World:** one of the game's three themed lands, each with its own
  rides, shops and scenery: Land of Invention, Polar Zone and Arabian
  Nights (`science`, `water` and `arabian` in `Data/levels/`), and the
  Intro World (`wIntro`) ([research/footage.md](research/footage.md#the-worlds-names-and-folders)).
- **Challenge:** a goal set by the game's advisor, from
  `Data/Challenges.sam`.
