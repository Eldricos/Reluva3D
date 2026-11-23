# Star Trek: Starfleet Command Volume II: Empires at War

Game id `sfc2`, on the [Starfleet Command engine](../../README.md); its
code is in `src/engines/sfc/`. A real-time starship combat game built on
the Star Fleet Battles board game's rules, with single-player campaigns
on a strategic map (the Dynaverse) and multiplayer.

## Releases

| Release | Recognised by | State |
|---|---|---|
| English retail 1.0 (`StarFleet2.exe` version 1.0.0.2, built 27 November 2000), two CDs, and an install from them | `StarFleet2.exe` and `Assets/Sprites/sprites.q3` (`kVariants`, `src/engines/sfc/sfc.cpp`); on the CDs, by their sizes in the first CD's cabinet | Recognised; imported from its CDs |
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
- **Running the game's own program:** starting the game runs
  `StarFleet2.exe` in the x86 emulator, in Reluva's window, through its
  start-up, its 3D test on Reluva's Direct3D 6 and its loading to its
  title screen and its CD key dialog, with the mouse and keys passed to
  it: a key can be typed, and the game checks it and remembers it. Its
  sound plays through Reluva's Miles: past its title screen its menu
  music starts, and its main menu shows, its buttons working. A
  skirmish runs, the Klingon AI coming in; a campaign begins, its
  Dynaverse map moved on, its first mission played into and left, its
  auto-save loaded again, a save made by name and deleted; and the six
  tutorials run,
  their orders spoken and following what's done, to their debriefing
  ([systems/hosting.md](systems/hosting.md#its-missions)).
- **Importing from the CDs:** the launcher's import takes the game from
  its two CDs, as their setup installs it
  ([InstallShield cabinets](../../../../common/installshield.md)).
  `reluva-tool sfc host` runs it headless
  ([systems/hosting.md](systems/hosting.md)).
- **Multiplayer**, started: a game hosted alone with AI players, started
  into its mission ([systems/hosting.md](systems/hosting.md#its-multiplayer)).
- **Not yet:** a skirmish, a tutorial or a campaign's mission won (a
  mission forfeited returns to the Dynaverse, which goes on);
  a multiplayer game played with another machine; the 2.0.3.6 patch applied as it's imported; the
  `.gf`, `.mct` and `.mvm`
  settings, campaign and map files; the objects in
  `sprites.q3`.

The plan is in [PLAN.md](PLAN.md).

## Using it

Not playable yet. Either import it from its CDs: *Import Game...* in the
launcher with the first CD (or a copy of it: `data1.hdr` and
`data1.cab`), then *Add other disc...* with the second (`data2.cab`);
the game's files, 3,356 of them (900 MB), are taken out of the CDs'
cabinets as their setup installs them, but for Taldren's script kit,
which isn't used (`reluva-tool import <first CD> <folder> -patch
<second CD>` does the same). Or *Add Game...* with the folder of an
install made on Windows. Starting it plays the opening movie, out of `SFC2Intro.exe` (a key or
click skips it; [formats/intro.md](formats/intro.md)), then runs the game's own program, which takes
11 to 18 seconds to load to its title screen; its CD key dialog takes
the copy's CD key, typed as it's printed (Shift for capitals), once:
it's kept, with whatever else the game writes, in Reluva's folder for
the game (its registry values in `registry.txt`), not the install.
Closing the window or Ctrl+F5 asks before stopping it. In a mission the
game's own keys work, as `Assets/specs/bindings.ini` lists them: A and S
speed, Y select an enemy, R red alert, Z fire, the keypad the helm,
Tab the map, Esc to leave; the panels take clicks. A multiplayer game
against the computer: Multiplayer, TCP/IP, HOST, ANNOUNCE GAME; set a
slot to AI, click the icon beside its BPV and SELECT its ship, tick its
READY, then click the icon beside your own, SELECT yours, tick READY and
START MULTIPLAYER GAME.

`reluva-viewer` with the game opens its data viewer;
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
| [systems/](systems/) | How the game behaves: [hosting.md](systems/hosting.md) (running its own program) |
| [formats/](formats/) | A page per file format: [sprites-q3.md](formats/sprites-q3.md), [bft.md](formats/bft.md), [mod.md](formats/mod.md), [text-tables.md](formats/text-tables.md), [sound-zips.md](formats/sound-zips.md), [intro.md](formats/intro.md) |
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
