# Star Trek: Armada: testing

## Automated checks

### Unit tests

`tests/engines/storm3d/unit/`, a file per topic, each a suite
`storm3d.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `storm3d.unit.detection` | An empty folder, and the executable alone, aren't the game; Armada's three files by name are found as an unknown release |
| `storm3d.unit.shell` | The main menu's buttons: each place's, a label's and its panel's, Quit's over a panel's edge |
| `storm3d.unit.sod` | Models of 1.0, 1.2, 1.6 and 1.8 written and read; damaged ones refused; nodes placed in their parents; levels of detail; the parts an assimilated ship has |
| `storm3d.unit.spr` | Sprite tables: each directive, sprites, an include, each kind of keyframe, an emitter; a font's and a flipbook's frames |
| `storm3d.unit.zfs` | ZFS archives over two blocks: names, a name filling its field, decoding with an unencoded tail; damaged archives refused |
| `storm3d.unit.odf` | Object definitions: includes overridden, lists over lines, sections, the value forms, a stray line, failing includes |
| `storm3d.unit.config` | Settings in C: variables, defines, expressions, structures grouped, strings over lines, what's skipped, failures |
| `storm3d.unit.ldl` | The `.ldl` encoding, with a key made in the test; no key from a file that isn't the game's |
| `storm3d.unit.images` | The Images page's order (each texture before its mipmap levels) and which files it shows |
| `storm3d.unit.input_map` | `Input.map`'s blocks and conditions, a stray line, a block left open |

### The basic engine test

`storm3d.engine` (`tests/engines/storm3d/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`data/engine-record.txt`). So far its install
is Armada's three files by name, found as an unknown release, and a few
textures, a model, a sprite table, a sound and a text file made in the
test,
which it then shows in the data viewer, page by page, with keys pressed at set times: the record keeps a
checksum of every screen, of the sound played, of what the Models page
gives the 3D renderer (a recording one, `tests/support/recording_renderer.h`)
and each window title. No
movie: there's no Bink encoder to make one.

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

What needs the game's own files, checked by hand with `reluva-tool
storm3d` ([Last checked](#last-checked)): recognising the English
release by its hashes, and reading every file of each kind
(`odf-check`, `config-check`, `image-check`, `sound-check`,
`movie-check`, `spr-check`, `sod-check`, `bzn-check`, and `zfs-extract`, `ldl` and `input-map` on
each file); `viewer-shot` shows any page on the install as a PNG, the
3D ones drawn in software.
How the Models page looks: the recording renderer draws nothing (see
[A playthrough by hand](#a-playthrough-by-hand)). And whether the sounds
and movies sound right: the checks only show they
decode, and last as long as they should.

## A playthrough by hand

1. Add the install in the launcher: it's found as "Star Trek: Armada".
2. Start it: the opening movie if the install has it (a key or a click
   ends it), then the main menu: its four panels playing, the label
   under the pointer lit, its music; Quit or Esc goes back to the
   launcher.
3. The data viewer's Models page: ships, stations and planets drawn
   whole, with their textures, turning; L shows less detailed levels.
   Without a display, run `reluva-viewer` under Xvfb on Mesa's software
   Vulkan driver, as the render check is run in CI.
4. The Maps page: each map's ships, stations and planets where the map
   places them, over its starfield; N goes from ship to ship. In
   "Premonitions" (`bzn/fed1.bzn`), the Enterprise between two Akira
   class ships. T from above: the map as its picture (`bzn/<map>.bmp`)
   shows it, nebulae and asteroid fields where its colours are.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | English retail 1.0 | Linux | `reluva-tool detect` on the install finds it as the English release |
| November 2026 | English retail 1.0 | Linux | Both ZFS archives extract whole (596 files); all 488 object definitions, 94 settings files and 3 `Input.map`s read; all six `.ldl` files decode |
| November 2026 | English retail 1.0 | Linux | All 2,320 pictures decode (`image-check`); four looked at as PNGs |
| November 2026 | English retail 1.0 | Linux | All 1,168 sounds decode (`sound-check`); all 69 movies decode whole, with the CD's (`movie-check`); three frames looked at as PNGs. Not listened to: no sound output here |
| November 2026 | English retail 1.0 | Linux | All 24 sprite tables read (`spr-check`); the Sprites page's pointer, flipbook, interface panel and font, and the Text page, looked at with `viewer-shot` |
| November 2026 | English retail 1.0 | Linux | The original in the emulator (`reluva-tool storm3d host`): its start-up and main menu, 20 seconds of its clock without stopping, 1,152 frames shown; the menu looked at as PNGs |
| November 2026 | English retail 1.0 | Linux | The original in the emulator with clicks (`-input`): the single-player screen and the mission panel looked at as PNGs; the first mission starts loading, up to the CD check |
| November 2026 | English retail 1.0, its disc's `.mds` | Linux | The original with the CD (`-cd`, `-toc`): "Track verification successful.", then "Premonitions", the mission first on the Federation's panel (its map `bzn\fed1.bzn`), 90 seconds of its clock without a stop, its 3D scene looked at as a PNG |
| November 2026 | English retail 1.0 | Linux, Mesa's software Vulkan under Xvfb | All 273 models read whole (`sod-check`); the Models page in `reluva-viewer`: a Borg starbase, a Federation and a Klingon battleship, the Kahless sword at both its levels, and a 1.2 model |
| November 2026 | English retail 1.0 | Linux | All 49 maps read whole (`bzn-check`: 17,586 objects, 1,238 paths); the Maps page with `viewer-shot`: "Blue Passages" from above, and "Premonitions"' starting fleet up close; the Models page's Borg starbase again, through the shared drawing |
| November 2026 | English retail 1.0 | Linux | All 183 sprite nodes read (`spr-check`); the Maps page from above (T) set beside the maps' pictures for "Blue Passages" and "The Very First Map": nebulae, asteroid fields and the central ring where the pictures have them, the way round they are |
| November 2026 | English retail 1.0, its disc's `.mds` | Linux | The original's skirmish (Multiplayer, Instant Action) on "The Very First Map", its frame at 59 seconds of its clock set beside `map-shot` from the camera in its memory: the lava moon lit and the background's nebula over its stars as the game's are; the same for "Premonitions"' planets |
| November 2026 | English retail 1.0, its disc's `.mds` | Linux | The skirmish's starting units: the game's memory read at 26 to 42 seconds, and the starbase's position watched as it was written (`-watch`); `map-shot` with R set beside its frame: two construction ships and a starbase where the game has them, in the Federation's grey, without the assimilated parts or the crew's fires |
| November 2026 | English retail 1.0, its disc's `.mds` | Linux | The skirmish's frame 4,100: its grid's lines placed back in the world (a line each 100 units at y = 0) and `map-shot` set beside it, the lines where the game's are; the starbase's vertices matched to its model, to see how ships are lit (not the maps' formula) |
| November 2026 | English retail 1.0, its disc's `.mds` | Linux | The skirmish scrolled to the area's edge (right arrow held 4 seconds): its shroud and fog textures read from memory, their draws placed on the plane y = 0; `map-shot` with R set beside both frames, the start's view clear and the shroud grey up to x = 6400 |
| November 2026 | English retail 1.0, its disc's `.mds` | Linux | The lighting code found by watching a vertex's colour written (`-watch`, with registers) and read; the formula checked against the planet Barisa's 198 vertices in "Premonitions" (0.002 on average) and the starbase's in the skirmish (median 0.001); `map-shot` of both set beside the game's frames |
| November 2026 | English retail 1.0, its disc's `.mds` | Linux | The game's main menu with `engine-shot`, set beside the original's frames at rest and with the pointer over Single Player: the background, the panels' movies (their colours, against the background), the labels and their looks where the game has them |
