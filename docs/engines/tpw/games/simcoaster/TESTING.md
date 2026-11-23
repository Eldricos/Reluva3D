# SimCoaster: testing

## Automated checks

### Unit tests

`tests/engines/tpw/unit/`, a file per topic, each a suite
`tpw.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `tpw.unit.import` | Importing from a small CD laid out as the US one: the cabinet's `Data` group in `Data/`, the movie and `Game.exe` at the top, the language's folders over the cabinet's files, the DLLs left out, then found by detection; a cabinet without `Game.exe` isn't the game's; a folder missing is said |
| `tpw.unit.formats` | The game's files: archives with folders, stored and RefPack files, damage; the `Data` folder mounted, each archive as its folder, loose files over them; settings files, every kind of line, broken ones turned away; a character set and a string table with text and parameters, damaged ones turned away; a font of both storages read, and one not; a sound bank's entries, PCM decoded, MPEG's header, one without a header; a particle library's effects, cut and broken ones turned away |
| `tpw.unit.ui` | The opening, headless, on an install built in the test: the logo movie stretched to 640 × 480, then the splash held 3 seconds; neither, without their files. A world's sky; the loading screen from pieces built in the test; a flat model's mesh stretched into a rectangle, its alpha blended; each of the Personnel File's controls where it says it is, clickable only when its state shows it; players written to a save folder and read back; particles burst, drawn and let go |
| `tpw.unit.detection` | Recognising the game: a release not in the table found by its files' names, in any case, as an unknown release; not without `Data/Challenges.sam` |

### The basic engine test

`tpw.engine` (`tests/engines/tpw/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`tests/engines/tpw/data/engine-record.txt`).
So far: stand-in files found as an unknown release of SimCoaster, its
`Data` folder (an archive and a loose file) mounted and every file read,
and the data viewer run on it, Tab through its pages and Esc: its
titles and a checksum of its screens.

### The game-files test

With your own copy, installed or imported from the CD:
`RELUVA_SIMCOASTER=<the game's folder> ctest --test-dir build -L gamedata`
(`tests/engines/tpw/gamedata.cmake`), against the records in
`tests/engines/tpw/data/gamedata/us/`:

| Step | What it runs |
|---|---|
| `release` | What the game is (`reluva-tool detect`) |
| `data` | Every file of the `Data` folder read, every archive's unpacked (`reluva-tool tpw data-check`) |
| `settings` | Every settings file read (`reluva-tool tpw sam-check`) |
| `text` | Every string table read in the language's character set, with a checksum of every string (`reluva-tool tpw str-check`) |
| `fonts` | Every font read, with a checksum of every glyph (`reluva-tool tpw bf4-check`) |
| `pictures` | Every picture decoded: `.fsh`, `.tga`, `.png`, `.jpg` (`reluva-tool tpw picture-check`) |
| `movies` | Every `.tgq` movie decoded, frames and sound (`reluva-tool tpw movie-check`) |
| `loading` | The loading screen put together, with a checksum of it (`reluva-tool tpw loading-screen`) |
| `sounds` | Every sound bank decoded, those that can't listed (`reluva-tool tpw sdt-check`) |
| `park` | A new park in Land of Invention played headless: its briefing turned to the details and back, Proceed, the camera moved by keys, a visitor come in, then Esc; the screens shown, the date, money and visitors, with a checksum (`reluva-tool tpw park-run`) |
| `soundmaps` | Every sound category read, each sound found in its bank and its length set against the bank's, with a checksum (`reluva-tool tpw sfx-check`) |
| `models` | Every model read, with a checksum of every mesh (`reluva-tool tpw md2-check`) |
| `animations` | Every model animation read and set against its model (`reluva-tool tpw anim-check`) |
| `sprites` | Every sprite sheet and animation set read, each animation's frames within its sheet, with a checksum (`reluva-tool tpw sprite-check`) |
| `heights` | Every height map read, with a checksum of them all (`reluva-tool tpw hmp-check`) |
| `terrain` | Every world's ground, attribute map and placed objects read, with a checksum of them all (`reluva-tool tpw terrain-check`) |
| `scripts` | Every ride and shop script read, and run for a simulated minute with guests; a checksum of their disassembly and of what each asked of the game (`reluva-tool tpw rse-check`) |
| `viewer` | The data viewer, headless: every page, a few keys on each, then Esc; its titles and a checksum of its screens (`reluva-tool tpw viewer-run`) |
| `opening` | The opening run headless, by a clock that moves only as it waits: the screens shown and when it ended (`reluva-tool tpw opening-run`) |
| `frontend` | The front end run headless: its opening skipped 5 s in by its button, then a quarter of a second apart play, a player named "Tester", made, "Enter Game", and clicks until Land of Invention turns under them; the screens shown, the world chosen, with a checksum of the screens (`reluva-tool tpw front-end-run`) |

The installed folder and one imported from the CD pass against the same
records. A baseline of your own is made as
[docs/DEVELOPMENT.md](../../../../DEVELOPMENT.md#the-game-files-test-with-your-own-copy-of-the-game)
says.

### What the automated checks don't cover

The US release's detection by its files' hashes, and importing the real
CD: the tests can't hold the game's files. Checked by hand.

## A playthrough by hand

1. Import the CD in the launcher (*Import Game...*): it's listed as
   "SimCoaster (US)", and the folder made holds the game's files as setup
   installs them ([cd.md](formats/cd.md)). Adding an installed folder
   (*Add Game...*) finds it the same.
2. Start it: the EA GAMES logo with its sound, filling the window, then
   the Bullfrog logo for 3 seconds (a key or a click skips either), then
   the loading screen, the SimCoaster logo top left and the legal text
   bottom right, which Esc leaves.
3. Open it in `reluva-viewer`: each page shows its files; a picture fits
   the window and Z shows it at its size; text in the game's font (F) on
   the Text page; a model turns with A and D, in its textures' colours
   (the Arabian shops' sand, the ATM desk's brown wood); on the Worlds
   page, each world's park lies in its surroundings, sand, grass or ice,
   its placed objects' cyan rings along its river, under its sky: an
   Arabian sunset, a pale blue, a deep blue; a sound plays with Space; the logo and the training
   video play on the Movies page.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US | Linux | `reluva-tool detect` on the install: "SimCoaster (US)"; on the CD: nothing found, as expected |
| November 2026 | US | Linux | `reluva-tool import` from the CD: 1,276 files, byte for byte the install's; found as "SimCoaster (US)" |
| November 2026 | US | Linux | The game-files test on the install and on the CD imported: 303 archives, 16,225 files read |
| November 2026 | US | Linux | The game-files test's six steps on the install and on the CD imported: 10,344 pictures decoded |
| November 2026 | US | Linux | The game-files test's eight steps on both: the logo decoded, the opening run (83 screens, 8.3 s) |
