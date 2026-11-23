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
3. **Data viewer** (next): pictures (BMP, TGA and their smaller copies,
   `Armada.pal`), fonts, sounds and music, the Bink movies, text, object
   definitions, and the SOD models on a Models page drawn through
   `src/graphics/gfx3d.h`.
4. **The original as a reference:** `Armada.exe` run in Reluva's x86
   emulator far enough to reach its front end and load a map, with traces
   of its calls and files (a `reluva-tool storm3d` command). Its exports
   and class names go into `research/`.
5. **Maps and the scene:** `.bzn` and `.mdf`, the starfield and nebulae,
   planets, moons and asteroids, the camera (`RTS_CFG.h`'s limits), the
   ships. A skirmish map shown and moved around, checked against the
   original in the emulator.
6. **The front end:** the shell's screens, the opening movie, game setup,
   options, loading and saving.
7. **Rules:** units from their object definitions: movement and
   collision avoidance, weapons, shields, crews and boarding, officers,
   mining dilithium, building and research, special weapons; then
   skirmish against the computer player (its scheduler, the `.aip`
   personalities, its DLLs). The largest phase, checked against the
   original and recordings, with the manual for the rules.
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
