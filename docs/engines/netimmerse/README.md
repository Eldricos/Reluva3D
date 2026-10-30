# The NetImmerse engine

Engine id `netimmerse`, in `src/engines/netimmerse/`, for games built on
NetImmerse (Numerical Design, later Gamebryo). First game: LEGO Alpha Team
(LEGO Media, 2000), game id `legoalphateam`, in `alphateam/`.

## Where it stands

LEGO Alpha Team is played by running the game's own program in Reluva's
x86 interpreter ([../../common/x86.md](../../common/x86.md)). Reluva
supplies the parts of Windows, DirectX and Aureal A3D it calls. Its menus,
saving, options, sound (in 3D, with music) and every mission work. All 51
Adventure missions are solved by the game's own solutions in automated
runs, and Mission One has also been played through by hand.

It's slow for now: the game draws in software in the interpreter. Loading
takes about 22 seconds, and the menus run at about 14 frames a second on 4
cores.

A data viewer of the game's files comes with it: archives, models (with
their animation), textures, fonts, sounds and text.

How it was built, measured and checked is in
[alphateam.md](alphateam.md).

## Using it

Add the game's install folder in the launcher (*Add Game...*): the one
holding `LEGOATeam.exe` and the `.pac` files. If `lmiintro.avi` is copied
from the CD into that folder, its intro movie plays first (a key or click
skips it).

While playing:

- the mouse is held by the window (the game draws its own pointer);
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

## Developer tools

`reluva-tool` has commands for this game, built with the engine:

- `pac-list`, `pac-extract`: the `.pac` archives;
- `fin-check`: reads every model, and checks the links and the animation
  tracks;
- `at-run`: runs the game headless, with scripted clicks and keys, frame
  captures, profiling and tracing. `-solved <mission>` plays a mission's
  own solution;
- `at-sweep`: runs every mission so, several at a time (`-solved` to solve
  them).

Run `reluva-tool` without arguments for each one's options; `at-run`'s are
described in [alphateam.md](alphateam.md).

## Documents

| Page | Covers |
|---|---|
| [alphateam.md](alphateam.md) | the plan and record: how the game runs, each part, and how it was checked |
| [formats/install.md](formats/install.md) | the install and the CD: its files and how releases are told apart |
| [formats/pac.md](formats/pac.md) | `.pac` archives |
| [formats/fin.md](formats/fin.md) | `.FIN` models, with their animation, morphs, lights and glows |
| [formats/ddf.md](formats/ddf.md) | `.DDF` fonts |

The intro movie's formats are shared with any engine:
[AVI](../../common/video/avi.md) and
[Indeo 5](../../common/video/indeo5.md).
