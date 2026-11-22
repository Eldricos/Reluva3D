# Star Trek: Starfleet Command Volume II: Empires at War

Game id `sfc2`, on the [Starfleet Command engine](../../README.md); its
code is in `src/engines/sfc/`. A real-time starship combat game built on
the Star Fleet Battles board game's rules, with single-player campaigns
on a strategic map (the Dynaverse) and multiplayer.

## Releases

| Release | Recognised by | State |
|---|---|---|
| English retail 1.0 (`StarFleet2.exe` version 1.0.0.2, built 27 November 2000), two CDs, and an install from them | `StarFleet2.exe` and `Assets/Sprites/sprites.q3` (`kVariants`, `src/engines/sfc/sfc.cpp`) | Recognised |
| Patch 2.0.3.6 (October 2002), over the retail install | The same files' names (`kFallback`): "unknown release", as the patch rewrites both | Surveyed; its patched files not yet hashed |
| Other releases | The same files' names: "unknown release" | Not checked |

Detection signatures (MD5 of each file's start, and its size, as
`reluva --hash` prints them):

| File | MD5 (start) | Size |
|---|---|---|
| `StarFleet2.exe` 1.0 | `e78f2d2c9c9c51f2077bb5c10cbaf617` | 4149248 |
| `Assets/Sprites/sprites.q3` 1.0 | `7eaeae9272f5d82ac6523a3d6191a45a` | 78253026 |

## Where it stands

- **Recognising it:** the launcher recognises the 1.0 install, and any
  other as an unknown release.
- **Reading its files:** the interface's pictures (`sprites.q3`), the
  fonts, the models and their textures, the ship, fighter, sound and
  music tables, the string tables, and the sounds; every retail file of
  each kind is read ([formats/](#this-folder)).
- **The data viewer** (`reluva-viewer`) shows all of them.
- **Not yet:** the game itself (starting it shows an empty screen); the
  `.gf`, `.mct` and `.mvm` settings, campaign and map files; the opening
  movie; the objects in `sprites.q3`.

The plan is in [PLAN.md](PLAN.md).

## Using it

Not playable yet. *Add Game...* in the launcher, with the install's
folder, finds it. `reluva-viewer` with the game opens its data viewer;
Tab moves between its pages, Esc leaves:

- **Images:** sprites.q3's images, then every BMP under `Assets`.
  Left/Right step, PageUp/PageDown by 10, Home/End; S switches between
  sprites.q3 and the BMPs, N goes to the next named image, B changes the
  background (a checkerboard shows what's transparent), Z shows a big
  picture whole-size or fitted.
- **Fonts:** Left/Right pick a font; B shows each glyph's box.
- **Models:** Left/Right, PageUp/PageDown pick a model; L the next level
  of detail, T textures on or off, P the named points, R resets the view,
  I the details; drag with the left button to turn it, the wheel or +/-
  to zoom.
- **Tables:** Left/Right pick a table; Up/Down, PageUp/PageDown,
  Home/End scroll; [ and ] the first column shown; C the comment rows.
- **Sounds:** Up/Down pick a sound, Left/Right a zip; Space plays or
  stops it, A plays each as it's picked.

## This folder

| Page | What's in it |
|---|---|
| This page | Where it stands, how to use it |
| [PLAN.md](PLAN.md) | The plan, its phases, what the install holds, and the decisions taken |
| [formats/](formats/) | A page per file format: [sprites-q3.md](formats/sprites-q3.md), [bft.md](formats/bft.md), [mod.md](formats/mod.md), [text-tables.md](formats/text-tables.md), [sound-zips.md](formats/sound-zips.md) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva knowingly differs from the original |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been confirmed against the original |
| [TESTING.md](TESTING.md) | The tests, and what to check by hand |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was worked out, and the tools |

## Terms

- **Dynaverse:** the campaign's strategic map of hexes, held by the
  empires, on which missions are offered ([PLAN.md](PLAN.md#what-the-install-and-the-cds-hold)).
- **BPV:** a ship's Basic Point Value, its cost and strength in Star
  Fleet Battles' rules.
- **Hardpoint:** a named point on a model where a weapon or system is
  ([formats/mod.md](formats/mod.md)).
- **Illumination map:** a texture's second picture, of the parts that
  glow, named after it with an `i` ([formats/mod.md](formats/mod.md)).
