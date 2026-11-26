# Star Trek: Armada: plan

## Goal

The English retail release played natively from the player's own
install: the front end, the four races' campaigns and the finale with
their cut scenes, skirmish against the computer, and network play, each
matching the original. Then the 1.1 and 1.2 patches and other languages,
and Armada II on the same engine.

## Phases

Readers and a data viewer first, then the original run in the emulator
as a reference, then the game rebuilt from the scene outwards: the map,
the front end, the rules, the campaign. What the install holds, which
these phases start from, is in [formats/install.md](formats/install.md).

1. **Start** (done): the engine from the template, detection of the
   English release ([formats/install.md](formats/install.md)), and its
   pages.
2. **Archives and text** (done): the ZFS archives
   ([formats/zfs.md](formats/zfs.md)), the object definitions with their
   includes ([formats/odf.md](formats/odf.md)), the settings in C
   ([formats/config.md](formats/config.md)), `Input.map`
   ([formats/input-map.md](formats/input-map.md)) and the `.ldl` files,
   which are the installer's scripts, not the game's strings
   ([formats/ldl.md](formats/ldl.md)). The archives need no LZO. Left
   for the phases that use them: `events.odf` (the announcements, with the
   sounds), the `gui_*.cfg` layouts and `shell.set` (with the front end),
   the tech trees (`.tt`, with the rules), and `Gamekey.map`.
3. **Data viewer** (done): pages for the pictures and their mipmap levels
   ([formats/textures.md](formats/textures.md)), the models in 3D
   ([formats/sod.md](formats/sod.md)), the sprite tables and fonts
   ([formats/spr.md](formats/spr.md)), the sounds
   ([formats/sounds.md](formats/sounds.md)), the Bink movies
   ([formats/movies.md](formats/movies.md)), and the text files. The
   fonts are sprites: `BZONE.FNT` isn't used. Left for later: the models'
   lights and emitters, which way their faces wind, and comparing the
   movies with Armada's own `binkw32.dll`.
4. **The original as a reference** (in progress): `Armada.exe` runs in
   Reluva's x86 emulator through its start-up (its DLLs, the
   configuration, the display mode, the sprite tables and their
   textures, `events.odf`) to its main menu, which shows as it should: a
   modal dialog painted with GDI, its Bink panels playing through
   DirectDraw ([systems/hosting.md](systems/hosting.md)). It traces the
   files it opens and the frames it shows (`reluva-tool storm3d host`).
   Its exports and classes are in [research/](research/README.md).
   Clicks given it (`-input`) take it through the single-player screen
   and the mission panel, past the game's CD check (MCI, with the disc's
   table of contents from its `.mds`), into "Premonitions", the mission
   first on the Federation's panel (its map `bzn\fed1.bzn`), which runs
   in 3D. Its text is drawn in fonts like the ones it asks for
   (`-fonts`). With a silent sound card that keeps time, the mission
   goes past its opening scenes to its interface, and its pointer, which
   hears the mouse's motion (`motion` in the input), closes the
   objectives and plays on. Next: the files it reads in play, for phase
   5; where its pointer starts; input that waits for what's shown.
5. **Maps and the scene** (done): `.bzn` and `.mdf`, the
   starfield and nebulae, planets, moons and asteroids, the camera
   (`RTS_CFG.h`'s limits), the ships. A skirmish map shown and moved
   around, checked against the original in the emulator. Done: the maps
   read, their settings, objects and paths
   ([formats/bzn.md](formats/bzn.md)), and the viewer's Maps page placing
   each object's model over the background, with the sprites the nebulae
   are made of and the asteroid fields, the way round the maps' pictures
   show them (the game's coordinates are left-handed), and the game's
   camera from its memory, so a map drawn from it matches the original's
   frame, and moves in play: on a parabola as it zooms, turning, and
   scrolling ([systems/camera.md](systems/camera.md)), with the planets'
   glows, the emitters' particles, and the maps' own lights as the
   game's draws show them; the original's skirmish runs in the emulator,
   and its frame matches the Maps page's, the players' starting units
   too, made where the game's code puts them
   ([formats/bzn.md](formats/bzn.md#starting-units)), in their team's
   colour; the game's grid and the shroud over what the player's units
   haven't seen; and the models lit as the game's code lights them,
   to within 0.002 of its draws. Phase 5 is done but for what needs
   play: the fog over what's been seen but isn't now, once ships move
   (phase 7), and the ships' specular highlights, which the renderer
   doesn't draw yet.
6. **The front end** (done): the shell's screens, the opening movie,
   game setup, options, loading and saving. The game itself starts with
   the opening movie, then the main menu as the original draws it, its
   panels' movies, its labels lit under the pointer, its music and
   sounds ([systems/front-end.md](systems/front-end.md)), the movies'
   colours as the game shows them; the single-player screen and its
   mission panel, the campaigns and missions open as the player's
   progress says (`shell.set`); the loading screen, in 3D, as the
   mission's files are read; the Options screen and its settings
   screens, their values the game's own preferences (`ARMADA.PRF`); the
   credits, in 3D; the Multiplayer Connection, Game Setup and Load Game
   screens. What needs play waits: the mission itself and Instant Action (phase
   7), saved games and the Load Game screen's list (phase 8), the ways
   to play over the network (phase 9).
7. **Rules** (in progress): units from their object definitions: movement and
   collision avoidance, weapons, shields, crews and boarding, officers,
   mining dilithium, building and research, special weapons, sight and
   the fog over what's been seen but isn't now, the ships' specular
   highlights as they turn in the light; then
   skirmish against the computer player (its scheduler, the `.aip`
   personalities, its DLLs). The largest phase, checked against the
   original and recordings, with the manual for the rules. Done: a
   mission shown in play after its loading screen, the map from the
   overview camera, which the keys move
   ([systems/play.md](systems/play.md)); a skirmish launched from the
   Game Setup screen, each player's starting units in their colour; the
   interface over it, its panels, counters and minimap; selecting a
   unit with the game's cursor, its ring and health bar, its display,
   orders and special weapons; selecting with a box, a group's display;
   ships sent with the right button, flying as the original's
   ([systems/movement.md](systems/movement.md)); the crew's growth; the minimap's units and view.
8. **The campaign:** the functions `Armada.exe` exports for the missions,
   the missions themselves ([Decisions](#decisions)), the cut scenes and
   the bridge scenes, and saved games.
9. **Network play:** on a local network, through `src/common/net.h`.
10. **Other releases and Armada II:** the patches and other languages
    hashed and added; then Armada II's files on the shared code.

## Decisions

| Date | Decision | Why |
|---|---|---|
| November 2026 | An engine for a family of games, `storm3d`, with Armada its first game, `armada` | `Armada.exe` names its renderer Storm3D, and Armada II shares its files' kinds; as `idtech3` and `netimmerse` are laid out |
| November 2026 | Rebuilt natively; the retail executable is run in the emulator only to study and check against | A native engine is the goal. The emulator already has most of what the executable imports (Direct3D 7, DirectInput, `MSVCRT.dll`, `MSVCP60.dll`), so a reference that can be traced costs little |
| November 2026 | Detection keys on `Armada.exe` and the two archives together | Which files a patch changes isn't known yet |
| November 2026 | Published tools are read for layouts only, every field checked against the game's files: stau (MIT) and SomaZ's Blender tools (GPL-2.0) for SOD and ODF, Open76 (GPL-3.0) for ZFS | Public documentation, as CONTRIBUTING.md allows; no code is copied |
| November 2026 | Not used: any source said to be Activision's, a ModDB download described as Battlezone II's source, and Dark Reign 2's source | Whether they may be used isn't clear, or they're other programs; leaving them alone keeps the work clean-room |
| November 2026 | The reference has a sound card that's silent but keeps time (the emulator's DirectSound), not one that fails | With DirectSoundCreate failing, the missions wait for their speech for ever; the reference is for what the program does, so its sounds need to end, not be heard |
| November 2026 | What the reference needs of Windows goes into the shared emulator, each part tested on its own | Any other hosted game can use it, as LEGO Alpha Team's and Need for Speed III's needs went there before |
| November 2026 | The online service (WON) isn't rebuilt | It no longer runs; network play goes through Reluva's own |

### The missions

The campaign's 26 missions are DLLs calling 213 of the functions and
variables `Armada.exe` exports by name
([formats/install.md](formats/install.md#missions)). Reluva will provide
those under the same names and meanings. How the missions then run is
to be decided at phase 8:

- **The shipped DLLs in the emulator,** calling Reluva's native
  functions, as Elite Force's single-player DLLs run on `idtech3`:
  faithful by construction, and little code.
- **Each mission rewritten natively,** from its code and from playing it:
  no emulator, but far more work.
