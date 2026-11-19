# LEGO Alpha Team

Game id `legoalphateam` (LEGO Media, 2000), the first game on the
[NetImmerse engine](../../README.md); its own code is in
`src/engines/netimmerse/alphateam/`.

A contraption-puzzle game: the player places parts in a room, then gives
the GO signal, and Dash, the one Alpha Team agent still free, works
through them into Ogel's bases (Ogel is the villain). So the game's
briefing has it (`BRIEFING_EN.TXT` in `Locale_en.pac`).

## Releases

| Release | Recognised by | State |
|---|---|---|
| English, Windows retail CD (`LEGOATEN`) | `Locale_en.pac`'s hash, with `LEGOATeam.exe` ([formats/install.md](formats/install.md)) | Every mission works: see [Where it stands](#where-it-stands) |
| Other releases | `LEGOATeam.exe` and `Bhvr.pac`, found as "unknown release" | Not seen: only the English version has been |

### What the install shows

- **It's a NetImmerse game.** `NiMain`, `NiAnimation`, `NiCollision`,
  `NiDirect3d` and `NiA3dSound` are NetImmerse's DLLs (Numerical Design
  Ltd.; the engine later became Gamebryo). The DLLs call themselves
  "NetImmerse Version 7.0". The models (`Fin.pac`'s `.FIN` files) are its
  streams under a header of the game's own (`Dweezil 23`).
  NIF is well documented in public by the NifTools project (`nif.xml`),
  which can be read as a format reference, as we used published specs for
  PE/COFF and x86.
- **The game's own code is in four DLLs.** `LEGOATeam.exe` is 60 KB;
  `BaseClassComp`, `BehaviorComp`, `LegoLogicComp` and `LoadComp` (about
  1 MB together) hold the game. They call NetImmerse through its C++
  classes (mangled exports, virtual functions), not a C table like Elite
  Force's.
- **Data is in `.pac` archives** read by `DDPackedDirFileLib.dll`, a
  format of the developer's own. One archive per kind of data, and one
  per language (`Locale_en.pac`).
- **Sound** went through Aureal A3D (`NiA3dSound`), which no current
  machine has; Reluva's mixer replaces it.

## Where it stands

LEGO Alpha Team is played by running the game's own program in Reluva's
x86 interpreter ([../../../../common/x86/README.md](../../../../common/x86/README.md)). Reluva
supplies the parts of Windows, DirectX and Aureal A3D it calls. Its menus,
saving, options, sound (in 3D, with music) and every mission work. All 51
Adventure missions are solved by the game's own solutions in automated
runs, and Mission One has also been played through by hand.

The game's code runs in an x86 emulator that turns it into native code
(a JIT) on x86-64 Windows and Linux, and its drawing is done in software
on the processor. At 1024x768x32 on a current desktop the menus run at
about 35 frames a second and missions at about 45, lower in wide shots
with much on screen.

A data viewer of the game's files comes with it: archives, models (with
their animation), textures, fonts, sounds and text.

How it was built, measured and checked is in [systems/](systems/) and
[research/](research/), a page per part (see [This folder](#this-folder)). Also:

- [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md): where it behaves
  differently from the original, and why;
- [UNCONFIRMED.md](UNCONFIRMED.md): what hasn't been checked against the
  original yet, and the format fields still guessed;
- [TESTING.md](TESTING.md): the playthrough checklist, how to report a
  problem, and the automated checks;
- [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md): how it was all worked
  out, the rules followed and the tools.

## Using it

Add the game's install folder in the launcher (*Add Game...*): the one
holding `LEGOATeam.exe` and the `.pac` files. If `lmiintro.avi` is copied
from the CD into that folder, its intro movie plays first (a key or click
skips it).

While playing:

- the mouse is held by the window (the game draws its own pointer);
- the window's title shows the game's frames a second (at most 60, as
  its flips wait for the display);
- **Ctrl+F5** goes back to the launcher;
- **F12** pauses the game and opens the data viewer; **Esc** there goes
  back to the game.

Without `LEGOATeam.exe` in the folder, the engine opens the data viewer
alone.

**In the data viewer**, **Tab** switches between the pages:

- **Archives**: what each `.pac` archive holds.
- **Models**: every `.FIN` model in 3D.
  - **Left/Right** step (**PageUp/PageDown** by 10); drag to turn; the
    wheel (or **+/-**) zooms; **W/A/S/D/Q/E** move; **R** resets the view;
    **I** hides the info.
  - **T**, **L** and **B** turn textures, lightmaps and a level's sky on or
    off.
  - **H** shows the hidden effects (sparks, a stunned agent's stars) and
    the characters' far-away stand-ins.
  - Animated models play their clips: **C** and **V** pick the clip (with
    its sound), **Space** pauses.
  - **G** marks the lights; pressed again, it lights the models with them.
  - **F** shows a part's footprint on the board.
- **Textures**: **Left/Right** step (**PageUp/PageDown** by 10,
  **Home/End**); **F** jumps to the next archive.
- **Fonts**: **Left/Right** step; **P** shows the font's pages instead of
  the sample.
- **Sounds**: **Left/Right** (or **Up/Down**) step; **Space** (or
  **Enter**) plays or stops; **A** plays each as it's selected; **F** jumps
  to the next archive.
- **Text**: the language's text files and the puzzles. **Left/Right** pick
  a file; **Up/Down**, **PageUp/PageDown** and **Home/End** scroll.

### Developer tools

`reluva-tool netimmerse <command>` has commands for this game, built with
the engine (`src/engines/netimmerse/tools/`):

- `pac-list`, `pac-extract`: the `.pac` archives;
- `fin-check`: reads every model, and checks the links and the animation
  tracks;
- `at-run`: runs the game's own program headless (no window or sound), with scripted clicks and keys, frame
  captures, profiling and tracing. `-solved <mission>` plays a mission's
  own solution;
- `at-sweep`: runs every mission so, several at a time (`-solved` to solve
  them).

`reluva-tool help netimmerse` lists them; run one without arguments for
its options. `at-run`'s are described in
[systems/running.md](systems/running.md#loading-and-starting-the-program)
and [systems/training.md](systems/training.md).

## This folder

| Page | Covers |
|---|---|
| This page | What the install shows, where it stands, how to use it, the developer tools |
| [formats/](formats/) | The game's files: [the install and the CD](formats/install.md), [`.pac` archives](formats/pac.md), [`.FIN` models](formats/fin.md) (with their animation, morphs, lights and glows), [`.DDF` fonts](formats/ddf.md), [the intro movie](formats/intro-movie.md) |
| [systems/](systems/) | How the game runs, and each part of it: [running the game's own code](systems/running.md), [detection, the archives and the data viewer](systems/data-viewer.md), [input](systems/input.md), [sound](systems/sound.md), [options](systems/options.md), [saving](systems/saves.md), [into a mission and the training tour](systems/training.md), [every mission](systems/missions.md), [Mission One and the camera](systems/camera.md) |
| [research/](research/) | Working notes: [speed, as it went](research/speed.md) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where it behaves differently from the original, and why |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What hasn't been checked against the original yet |
| [TESTING.md](TESTING.md) | The playthrough checklist, how to report a problem, and the automated checks |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was all worked out, the rules followed and the tools |
| [PLAN.md](PLAN.md) | The ways weighed for running the game, the decision, and the order of work |

The intro movie's formats are shared with any engine:
[AVI](../../../../common/video/avi.md) and
[Indeo 5](../../../../common/video/indeo5.md).
