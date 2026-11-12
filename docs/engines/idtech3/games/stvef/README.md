# Star Trek: Voyager - Elite Force

Game id `stvef`, on the [id Tech 3 engine](../../README.md); its own code
is in `src/engines/idtech3/eliteforce/`. The engine's file formats, which
Elite Force shares with every Quake III engine game, are in the engine's
[formats/](../../formats/); where the engine stands with this game is on
the [engine's page](../../README.md#where-it-stands).

## This folder

| Doc | What's in it |
|---|---|
| This page | Using it: adding the game, the patch, the Expansion Pack, mods, and the pages |
| [formats/](formats/) | [Map entities](formats/entities.md), as Elite Force's maps use them, and [the 1.2 patch](formats/patch.md) |
| [systems/](systems/) | Its game code's interfaces ([single player's DLLs](systems/sp-interface.md), [the QVMs' system calls](systems/syscalls.md)), [the network protocol](systems/network.md), [joystick input](systems/input.md), [running single player's DLLs](systems/single-player.md) and [cinematics](systems/cinematics.md) |
| [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md) | Where Reluva deliberately differs from the original game |
| [UNCONFIRMED.md](UNCONFIRMED.md) | What's assumed rather than confirmed |
| [TESTING.md](TESTING.md) | What still needs playing |
| [REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) | How it was all worked out, and the rules followed |

## Using it

You need the game: the English Windows CD (or a disc image), or an install
made by its own setup. The 1.2 patch installer (`eliteforcepatch1_2.exe`)
and the Expansion Pack's CD are optional.

- **Adding the game.**
  - *Add Game...* in the launcher plays it from an existing install.
  - *Import Game...* copies it from the CD (or an install) into Reluva's
    library folder. It takes only the files Reluva needs, so nothing has to
    be installed, on any system.
- **The patch.** *Add patch...* during an import, or *Apply Patch...* for a
  game already added, takes 1.2's files out of the patch installer without
  running it.
- **The Expansion Pack.** *Add expansion...* during an import, or *Add
  Expansion...* for a game already added, copies it from its CD. Each game
  has a *Use the Expansion Pack* setting, so the same folder can be added
  twice to play with and without it.
- **Mods** (folders beside `baseEF` with their own paks) are chosen with a
  game's *Mod* setting, or from the game's own Mods screens.
- **From a terminal:** `reluva-tool detect <folder>` and `reluva-tool import
  <source> [destination] [-patch <installer or Expansion CD>]...` do the
  same as the launcher.

A game starts on a page to choose single player, Holomatch or the
developer tools:

- **Single player and Holomatch** are the game's own menus and play, with
  its own keys.
- **The developer tools** are pages for the game's data. **Ctrl+Tab** steps
  through them; plain **Tab** is the game's.
  - **3D world:**
    - **Left/Right** picks a map; drag to look;
    - **W/A/S/D** move, **Space/C** (or **E/Q**) go up and down, **Shift**
      goes faster;
    - **R** returns to the start, and **I** shows or hides the info.

    Models placed by the map's entities show too.
  - **Models**, players put together:
    - **Left/Right** step through them; drag to turn, and the wheel zooms;
    - **A** animates, and **,/.** step frames.
  - **Wireframe maps:**
    - **Left/Right** picks one;
    - drag (or **Shift+arrows**) rotates, the wheel (or **+/-**) zooms, and
      **W/A/S/D/Q/E** move;
    - **P** shows or hides patches, **I** the info, and **R** resets the
      view.
  - **Textures:** **Left/Right** step through them (**PageUp/PageDown** by
    10).
  - **Files:** the search path, and which archive each file comes from.

3D needs a GPU with Vulkan (Linux, Windows) or Metal (macOS).
