# Star Trek: Starfleet Command II: testing

## Automated checks

### Unit tests

`tests/engines/sfc/unit/`, a file per topic, each a suite
`sfc.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `sfc.unit.detection` | The engine is registered; an empty folder isn't a game; the game's files with other contents are an unknown release, whatever their names' case |
| `sfc.unit.formats` | sprites.q3's images (16- and 8-bit, each kind of run, a name from the objects) and a run past a row's end; a .bft font, measured and drawn; a .mod model, and a triangle with a material that isn't there; a tab table with quotes and comments; a string table with continuation lines and a heading |

### The basic engine test

`sfc.engine` (`tests/engines/sfc/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`data/engine-record.txt`): it isn't taken for
the game, starting the game without `StarFleet2.exe` says so, and a
registry value the program writes is kept in its save folder's
`registry.txt` and read back, and two CDs laid out as the game's
(InstallShield 5, the setup's groups) are imported: nothing planned from
the first alone, all of it once the second is added, the result
recognised. `common.unit.installshield 5` checks version 5's cabinets
themselves.

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### Reading every retail file

With your own copy, these read every file of a kind and say what fails:

- `reluva-tool sfc sprites <game folder> -q`
- `reluva-tool sfc fonts <game folder>`
- `reluva-tool sfc models <game folder>`
- `reluva-tool sfc sounds <game folder>`
- `reluva-tool sfc table <file>`, for each table

### What the automated checks don't cover

Whether the data viewer shows the pictures, fonts and models as the game
does: by putting it beside the running game.

## A playthrough by hand

So far, to its CD key dialog: start the game from the launcher; after
its "Loading..." the title screen and the dialog show, the pointer
following the mouse with no box around it. Click the text box, type
(capitals with Shift, and `-`), click ENTER: a wrong key gets the
game's "ERROR! ... incorrect" message; the copy's own key closes the
dialog, the full title screen shows, its menu music starts, and its
main menu shows; Single Player opens Tutorials, Skirmish and Campaign;
Skirmish, then PLAY GAME, starts "Hostile Skirmish": S to speed up, Y
to select the Klingon, R for red alert, Z to fire once it's in range.
Tutorials, then START on a course: Captain Wells' orders, spoken; T on
the first, a container targeted, brings the next.
Started again, it asks for no key. Ctrl+F5, and Return
to launcher, stop it. Headless, the same with `reluva-tool sfc host <game folder>
-click 600 395 370 -type 620 <key> -click 700 395 405 -shot 900 out.png`.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 23 November 2026 | English retail 1.0 | Linux | The tutorials, headless with `-fixed-clock`: all six started from their screen and run; Command 190 followed (T targets a container, the next order comes) and, its containers not destroyed, ended "incomplete" with its debriefing; Command 290 to its debriefing; the voice-over captured with `-sound` |
| 23 November 2026 | English retail 1.0 | Linux | A skirmish played: headless with `-fixed-clock` (the same frames each run) and in the window with `xdotool`, the game imported from the CDs: S for speed, Y to target, R red alert, the helm's turn arrow, Z and the FIRE button hitting the Klingon, keypad 5's high-energy turn |
| 23 November 2026 | English retail 1.0 | Linux | Importing from the two CDs (`reluva-tool import`): 3,356 files in 20 seconds, every MD5 the header's, byte for byte the owner's install by setup but for `sfc2.isu`; recognised as 1.0. Every reader over it: 126 fonts, 317 models (6 with 4-cornered polygons), the sounds and `sprites.q3`, none failing |
| 23 November 2026 | English retail 1.0 | Linux | A skirmish: with a copy of the install short of files, "No missions available"; with the CD's program files added (taken with `unshield`), "Hostile Skirmish" listed and started, the Klingon coming in, 35 billion instructions without failing |
| 23 November 2026 | English retail 1.0 | Linux | DirectPlay's lobby: `reluva-tool sfc host` past the title screen to the main menu ("Select Game"), its lobby asked each frame; Single Player clicked, its Tutorials, Skirmish and Campaign screen shown; 6 billion instructions without failing |
| 23 November 2026 | English retail 1.0 | Linux | Miles: `reluva-tool sfc host -sound` past the title screen, the music captured byte for byte `MNU_A1.WAV` then `MNU_A2.WAV`; in the launcher's window (SDL's dummy audio driver) to the same point, stopping for want of DirectPlay's lobby |
| 23 November 2026 | English retail 1.0 | Linux | `reluva-tool sfc host`, headless: the owner's CD key typed and taken, the full title screen, `registry.txt` written with the `WON\CDKeys` value; a second run with the same save folder went past the dialog with no input, both stopping at `AIL_set_redist_directory` |
| 22 November 2026 | English retail 1.0 | Linux | In the launcher's window (a virtual X display, the mouse and keys sent by `xdotool`): the title screen and CD key dialog, text typed with capitals and `-`, ENTER answered "incorrect" for a made-up key, the pointer drawn without its background, Ctrl+F5 stopping it. The same headless with `-click`, `-type` and `-shot` |
| 22 November 2026 | English retail 1.0 | Linux | `reluva --detect` on the install's `StarFleet2.exe` and `sprites.q3`: recognised as 1.0 |
| 22 November 2026 | English retail 1.0 | Linux | `reluva-tool sfc host` with exceptions dispatched: four raised and caught, the title screen and its CD key dialog between 0.6 and 1 billion instructions; 2 billion run without failing (1,086 frames) |
| 22 November 2026 | English retail 1.0 | Linux | `reluva-tool sfc host` with Direct3D 6: the 3D test passed, an 800x600 mode, the first frame ("Loading...") after 45.08 million instructions; stops at a C++ exception it expects to catch, after 48.12 million |
| 22 November 2026 | English retail 1.0 | Linux | `reluva-tool sfc host`: start-up to the 3D test, 44,063,247 instructions, stopping for want of Direct3D 3 |
| 22 November 2026 | English retail 1.0 | Linux | Every image, font, model, table and sound read by the commands above, none failing; the data viewer's pages seen in screenshots (Xvfb, Mesa's software Vulkan), a model with its textures |
