# The Star Trek: New Worlds engine

Engine id `newworlds`, in `src/engines/newworlds/`. Game: Star Trek: New
Worlds (Interplay, 2000), the English Windows release.

## Where it stands

New Worlds opens as a data viewer: its missions, models, textures, maps,
sounds, strings and fonts, all read from the game's own files. The missions
page also has a first playable layer (**F3**): the mission's map with the
game's camera and controls, the colony's starting units, building, and the
mission's script running ([formats/game.md](formats/game.md)).

The engine is **off by default** in the build: two of its files hold string
literals longer than Visual Studio accepts. To build it with GCC or Clang:

```sh
cmake -S . -B build -DRELUVA_ENGINE_NEWWORLDS=ON
```

## Using it

Add the game's install folder in the launcher (*Add Game...*); the files
that identify it are listed in [formats/install.md](formats/install.md).

**Tab** switches between missions, models, textures, maps, sounds, strings
and fonts. **Left/Right** step through them and **F** cycles filters.

**Missions** (`level.dat`'s list) show their map with the briefing over it:

- **Up/Down** turn its pages; **B** hides it; **J** picks the side of a
  joint mission;
- **Space** plays the mission's voice clip, **N/P** the next or previous
  one, **M** the planet's music;
- **Enter** plays the briefing step by step (**Enter** again skips a step,
  **Backspace** stops);
- **X** steps through the mission's script: its events, messages and
  objectives (**Z** stops);
- **I** shows the game's interface over the map in the side's style, with
  the script's messages and objectives in it;
- **C** shows or hides the colony (its hub where the recordings put it,
  growing as the script builds) and **V** looks at it;
- **F3** plays on the map with the game's own camera and controls: select
  and move the colony's starting units, **B** to build, the mission's
  script running ([formats/game.md](formats/game.md)). The map's keys work
  too.

**Models**:

- drag with the mouse (or **W/A/S/D**) to rotate; the wheel (or **+/-**)
  zooms; **Space** spins; **R** resets the view;
- **P** shows one part at a time; **N** selects the next one;
- **K** plays its animations (or sweeps its turret);
- **L** outlines; **U** checks texture coordinates; **T** turns textures
  off;
- **G** changes how alpha layers are used: by default glow in `_g`
  textures and transparency in the others; or ignored, or transparency;
- **H** smooth or flat shading;
- **M** the model's points (from its `.mdl`);
- **C** colours faces by texture number; **B** shows back faces.

**Maps** are built from their ground tiles:

- drag to rotate, the wheel to zoom, **W/A/S/D** to move, **Q/E** to turn,
  **Left/Right** for the next map;
- **T** textures; **G** colours the ground by its terrain codes or shows
  resource deposits;
- **O** the placed trees and rocks;
- **H** smooth or flat shading;
- **K** the walls between levels;
- **L** outlines.

**Sounds**: **Space** (or **Enter**) plays or stops the selected one, **A**
plays each as it's selected, and **F** lists every copy of the sounds the
archives store more than once.

**Strings**: **Left/Right** picks a table; **Up/Down**, **PageUp/PageDown**
and **Home/End** scroll; **+/-** change the text size; **F** switches to the
game's fonts.

**Fonts**: **Left/Right** picks one, **+/-** change the size and **B**
outlines the glyphs' boxes.

## Developer tools

`reluva-tool` has commands for this game's files, built with the engine:

- archives: `cat-probe`, `cat-list`, `cat-extract`;
- strings, textures and models: `stb-dump`, `lbp-info`, `tex-survey`,
  `tex-keys`, `geo-info`, `geo-survey`, `geo-textures`, `geo-slots`,
  `geo-sheet`, `geo-faces`, `mdl-info`;
- maps and missions: `gmf-info`, `gmf-fit`, `gmf-dump`, `ground-textures`,
  `level-info`;
- sounds and fonts: `wav-info`, `wav-decode`, `fif-info`.

Run `reluva-tool` without arguments for each one's options.

## Documents

**File formats** ([formats/](formats/)): how each was worked out is
recorded in its page.

| Page | Covers |
|---|---|
| [install.md](formats/install.md) | the install: its files and how releases are told apart |
| [cat.md](formats/cat.md) | `.cat` archives |
| [stb.md](formats/stb.md) | `.stb` string tables |
| [lbp.md](formats/lbp.md) | textures (`.lbp`, `.mp0`–`.mp4`, `.eb0`, `.eb1`, `.fbp`) |
| [geo.md](formats/geo.md) | `.geo` models |
| [mdl.md](formats/mdl.md) | `.mdl` metamodels |
| [arf.md](formats/arf.md) | `.arf` animation frames |
| [gmf.md](formats/gmf.md) | `.gmf` maps |
| [level.md](formats/level.md) | `level.dat` |
| [gui.md](formats/gui.md) | the GUI archives |
| [fif.md](formats/fif.md) | `.fif` fonts |
| [wav.md](formats/wav.md) | `.wav` sounds |
| [media.md](formats/media.md) | the `Media/` folder |
| [colony.md](formats/colony.md) | starting colonies |
| [game.md](formats/game.md) | Reluva's play layer on the map |
| [mission-scripts.md](formats/mission-scripts.md) | Reluva's mission scripts (the original's are in its program) |

**Watched in play** ([play/](play/)): what recordings of the original show,
the base for the play layer and the mission scripts.
[play.md](play/play.md) is the overview, [play-tutorials.md](play/play-tutorials.md)
the training missions, and `play-<mission>.md` one page per mission.
[vm-checklist.md](play/vm-checklist.md) lists what's still to collect from
the game.
