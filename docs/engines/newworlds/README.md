# The Star Trek: New Worlds engine

Engine id `newworlds`, in `src/engines/newworlds/`. It runs Star Trek: New
Worlds (Interplay, 2000) from the player's own install.

It isn't part of the default build yet: two of its source files hold
string literals longer than Visual Studio accepts. To build it with GCC or
Clang, set `newworlds=enabled` in `engines.cfg` (or in your
`engines.local.cfg`). See the main
[README](../../../README.md#choosing-engines).

## Where it stands

It opens as a data viewer of the game's archives and their files: models,
maps, textures, sounds, strings, fonts and missions. Its first playable
layer plays a mission on its map with the game's own camera and controls,
the mission's script running. Figures the recordings of play can't give
are named placeholders for now. See
[game.md](game.md).

## Using it

Add the game's install folder with *Add Game...* in the launcher. **Tab**
switches between the pages:

- **Missions** (`level.dat`'s list) show their map with the briefing over
  it:
  - **Up/Down** turn the briefing's pages, **B** hides it, and **J** picks
    the side of a joint mission;
  - **Space** plays the mission's voice clip, **N/P** the next or previous
    one, and **M** the planet's music;
  - **Enter** plays the briefing step by step (**Enter** again skips a step,
    **Backspace** stops);
  - **X** steps through the mission's script: its events, messages and
    objectives (**Z** stops);
  - **I** shows the game's interface over the map, in the side's style,
    with the script's messages and objectives;
  - **C** shows or hides the colony, its hub where the recordings put it,
    growing as the script builds; **V** looks at it;
  - **F3** plays on the map, with the game's camera and controls. Select
    and move the colony's starting units, and **B** builds. The mission's
    script runs. See [game.md](game.md).

  The map page's keys work too.
- **Models:**
  - drag (or **W/A/S/D**) rotates, the wheel (or **+/-**) zooms, **Space**
    spins, and **R** resets the view;
  - **P** shows one part at a time, **N** selects the next, and **K** plays
    its animations (or sweeps its turret);
  - **L** shows outlines, **U** texture-coordinate checks, **T** turns
    textures off, **C** colours faces by texture number, and **B** shows
    back faces;
  - **G** changes how alpha layers are used: by default glow in `_g`
    textures and transparency in the others;
  - **H** switches smooth or flat shading, and **M** shows the model's
    points (from its `.mdl`).
- **Maps**, built from their ground tiles:
  - drag rotates, the wheel zooms, **W/A/S/D** move, **Q/E** turn, and
    **Left/Right** picks the next map;
  - **T** shows textures, **G** colours the ground by its terrain codes or
    shows resource deposits, and **O** shows the placed trees and rocks;
  - **H** switches smooth or flat shading, **K** shows the walls between
    levels, and **L** shows outlines.
- **Sounds:**
  - **Space** (or **Enter**) plays or stops the selected one;
  - **A** plays each as it's selected;
  - **F** lists every copy of the sounds the archives store more than
    once.
- **Strings:**
  - **Left/Right** picks a table;
  - **Up/Down**, **PageUp/PageDown** and **Home/End** scroll, and **+/-**
    change the text size;
  - **F** switches to the game's fonts.
- **Fonts:** **Left/Right** picks one, **+/-** change the size, and **B**
  outlines the glyphs' boxes.

Everywhere, **Left/Right** step through the page's items and **F** cycles
its filters.

`reluva-tool` has commands for the game's files: `cat-list`, `cat-probe`
and `cat-extract` for its archives, and `geo-*`, `gmf-*`, `tex-*`,
`stb-dump`, `fif-info`, `mdl-info`, `lbp-info`, `ibi-dump` and `level-info`
for what's in them.

## The docs

**The install and its archives:**

- [install.md](install.md): the install's layout.
- [cat.md](cat.md): `.cat` archives.
- [gui.md](gui.md): the GUI archives.
- [media.md](media.md): `Media/`.

**Files:**

- [geo.md](geo.md): `.geo` models.
- [mdl.md](mdl.md): `.mdl` metamodels.
- [arf.md](arf.md): `.arf` animation frames.
- [gmf.md](gmf.md): `.gmf` maps.
- [lbp.md](lbp.md): textures (`.lbp`, `.mp0`-`.mp4`, `.eb0`, `.eb1`,
  `.fbp`).
- [fif.md](fif.md): `.fif` fonts.
- [stb.md](stb.md): `.stb` string tables.
- [wav.md](wav.md): `.wav` sounds.
- [level.md](level.md): `level.dat`, the missions.

**The game:**

- [game.md](game.md): playing on the map.
- [colony.md](colony.md): starting colonies.
- [mission-scripts.md](mission-scripts.md): Reluva's mission scripts.

**Watched in play** (recordings of the original, for how the game behaves):

- [play.md](play.md): what the recordings show, and
  [vm-checklist.md](vm-checklist.md), what to collect.
- [play-tutorials.md](play-tutorials.md): the training missions.
- **Federation:** [F1](play-f1.md), [F2](play-f2.md), [F3](play-f3.md),
  [F8](play-f8.md), [F12](play-f12.md), [F14](play-f14.md).
- **Klingon:** [K1](play-k1.md), [K2](play-k2.md), [K7](play-k7.md),
  [K8](play-k8.md), [K12](play-k12.md), [K14](play-k14.md).
- **Romulan:** [R1](play-r1.md), [R2](play-r2.md), [R9](play-r9.md),
  [R12](play-r12.md), [R14](play-r14.md).
- **Joint missions** (Federation, Klingon and Romulan sides):
  - J1: [F](play-j1f.md), [K](play-j1k.md), [R](play-j1r.md);
  - J2: [F](play-j2f.md), [K](play-j2k.md), [R](play-j2r.md);
  - J3: [F](play-j3f.md), [K](play-j3k.md), [R](play-j3r.md);
  - J4: [F](play-j4f.md), [K](play-j4k.md), [R](play-j4r.md);
  - J6: [F](play-j6f.md), [K](play-j6k.md), [R](play-j6r.md);
  - J7: [F](play-j7f.md), [K](play-j7k.md), [R](play-j7r.md);
  - J8: [F](play-j8f.md), [K](play-j8k.md), [R](play-j8r.md);
  - J9: [F](play-j9f.md), [K](play-j9k.md), [R](play-j9r.md).
- **Bonus missions:** [C9](play-c9.md), [C10](play-c10.md),
  [C11](play-c11.md).
