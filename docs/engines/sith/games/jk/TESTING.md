# Jedi Knight: testing

## Automated checks

### Unit tests

`tests/engines/sith/unit/`, a file per topic, each a suite
`sith.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `sith.unit.detection` | Recognising the game's releases: the fallback table, names in any case, a partial install or a CD not taken for one |
| `sith.unit.formats` | Reading colormaps, 8- and 16-bit pictures, materials, fonts, string tables, episodes, sound classes, sprites and particle clouds, and refusing broken ones |
| `sith.unit.models` | Reading a model with each of the game's quirks, placing its nodes, an animation's values between keyframes, and a puppet's modes and joints |
| `sith.unit.levels` | Reading a level with the game's quirks (lists shorter than their counts, `5;`), its geometry, sectors, templates and things, a thing's settings through its template's bases, and refusing broken ones |
| `sith.unit.gob` | Reading `.gob` archives (names with either separator and in any case), refusing broken ones, and the install's set of them |
| `sith.unit.import` | Importing from CD 1 and adding CD 2, what's copied and where, what isn't imported from, and that the result is recognised and finds its CDs |
| `sith.unit.folders` | Telling the CDs apart by `JK_.CD`, and finding them in and beside an install and by the settings |

### The basic engine test

`sith.engine` (`tests/engines/sith/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`tests/engines/sith/data/engine-record.txt`).
So far: an empty folder isn't the game, the install is recognised (as an
unknown release, its files being empty), a CD is found beside it and
another in it, and each of the data viewer's pages is drawn from archives
the test makes and a Smacker video on its CD copies, before and after a
key (the Colormaps page has one colormap, and the Sounds page one sound,
so their keys change nothing), the cut scene also played on by the
clock. The Models, Sprites and Levels pages are recorded as drawn or not: its pixels' edges
differ between compilers. Played, the engine stops with an error naming
`JK.EXE`, the test's being an empty file, having set up its window.

### The game-files test

With your own copy, `RELUVA_JK=<the install> ctest --test-dir build -L
gamedata`, both CDs' copies in the install (`cd1`, `cd2`) or beside it,
with their music for the `release` step's. Without the variable, the
launcher's entry for the game is used.
`tests/engines/sith/gamedata.cmake` runs `reluva-tool sith` commands on
the game, one step each:

| Step | What it runs |
|---|---|
| `release` | What the game is (`reluva-tool detect`), and the CDs and the music found on each (`folders`) |
| `formats` | Every file of the kinds Reluva reads in the archives decoded: colormaps, pictures, materials, fonts, models, animations, puppets, sprites, particles, levels, string tables, sounds, sound classes and episodes, each kind as a checksum (`formats -hash`) |
| `movies` | Every cut scene's frames, palettes, sound and subtitle numbers, each cut scene as a checksum (`movies -all`) |
| `start` | The retail game run from a new state until it waits at *New Player* (30 seconds): the files it opens, its registry writes and its debug output, as a checksum |
| `level` | A player made, *Single Player*, *New Game*, by clicks at set seconds; cut scenes skipped and Nar Shaddaa ended by its cheat (`host -skim`, 75 seconds): the cut scenes and levels reached, and every file opened up to the second level, as a checksum |

The game runs on the machine's clock, not an instruction clock, so the
hosted steps keep only what the run decides itself, up to a point any
machine that plays the game reaches in time: two runs of each opened the
same files in the same order. A machine busy with other work can run
the game too slowly to get there in time, and the step then differs
from its record: run them on a machine left idle. The records, in
`tests/engines/sith/data/gamedata/<release>/`, hold only counts, names of
the game's files and checksums. They were made on Linux with the US
retail CDs (`us-1.0`).

### What the automated checks don't cover

That the US release's signatures match a real install: checked by hand
([Last checked](#last-checked)).

## A playthrough by hand

- *Add Game...* on the install finds "Jedi Knight: Dark Forces II (US,
  1.0)".
- Playing it: the opening's title and crawl (Esc skips the crawl), *New
  Player* (typing a name), the main menu by the mouse, Windows' pointer
  shown over it; *Single Player*, *New Game*: the first cut scene, then
  Nar Shaddaa. In play the mouse turns Kyle, held by the window, and the
  arrows move him; Esc brings up the game's menu with the pointer
  again. The opening's soundtrack, the menus' clicks, the cut scene's
  sound and the level's are heard; with CD 1's audio tracks beside its
  files, the music fades in as Kyle leaves the bar. A step on CD 2 (level
  9 on, or its cut scenes in *Cutscenes*) starts without the game asking
  for CD 2, and plays CD 2's music. Ctrl+F5 asks before going back to the launcher.
- `reluva-tool sith folders <install>` finds both CDs where they are.
- *Import Game...* on CD 1, with CD 2 added, makes an install that's
  recognised, with both CDs' cut scenes in it, and their music when it was beside them.
- In `reluva-viewer`: the menus' backgrounds look as in the game; the
  Force powers' icons look right with `bkforce.bm`'s palette; textures
  look right through `dflt.cmp`; the fonts' sample text is evenly spaced;
  the levels look as in the game from the player's start, their things
  in place;
  the models' parts meet at their joints, and move together when
  animated;
  sounds and cut scenes play with their sound, the cut scenes' picture
  keeping pace with it.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US retail | Linux | Force Protection: in the first level, `imayoda` and `yodajammies` typed, then `activate 29`; the shield's points around Kyle in third person, and its orientation read at 44, 46, 48 and 50 seconds: turning about (1, 1, 1) at about 104.5 degrees a second, as 60 a second about each axis gives |
| November 2026 | US retail | Linux | A death watched: Kyle `warp`ed into the first level's gunfire and killed; `-peek` read his puppet's tracks, his death held at its last frame (86) at full weight from 65 to 75 seconds, and his 17 nodes' matrices, each the death's last frame though another track played at full weight below it |
| November 2026 | US retail | Linux | The game's software drawing against the viewer's: Nar Shaddaa's first frame from the player's start, the same view with the game's 90 degrees, colours within 2 of 255; under the water in Baron's Hed (`warp` typed), the tinted palette the formula's to the pixel; a full subtitle the same to the pixel; the HUD's gauge and pieces rebuilt from the files through the level's palette, the same but for its numbers |
| November 2026 | US retail | Linux | The game-files test: its five steps made from an install with both CDs' copies beside it and run again, also two at a time, as recorded. An install imported from the CDs' images, their music copied with them (7 and 11 tracks), passes `release`, `formats` and `start` |
| November 2026 | US retail | Linux | Import with the music: `sith.unit.import`'s *music* test copies CD 1's cue sheet and its audio track's image (not the data track's, nor a cue sheet of data alone) and CD 2's ripped tracks into `cd1` and `cd2`; the imported CD 1's music is read from the copies. The retail CDs' images too, in the game-files test's row above |
| November 2026 | US retail | Linux | Full subtitles: *Setup*, *General*, *Enable full subtitles on cutscenes*, *Ok*, through `reluva-tool sith host`; the setting kept for the player, and in `03-04A` every spoken line shown below the picture, the first at about 76 seconds, long ones on two centred lines as the viewer breaks them |
| November 2026 | US retail | Linux | Small differences: run through the menus, a level and a cut scene, the game makes its Arial fonts and never calls `TextOutA`. A second wheel's pedals named by `wheel2pedals`: `common.unit.sdl_controller`'s *second* test sets them the other way about and reads the pedal pressed as the brake |
| November 2026 | US retail | Linux | *Setup*'s 3D choice through the game's own screens: *Display*, *Enable 3D Acceleration*, *Ok*; *Quit* writes `b3DAccel` 1, the primary display, the HAL device and mode 3, as `-d3d` does, and the next new game draws through Direct3D. Closing the window in the menus (`-close`) ends the game with its settings saved; stopped in play, its settings are saved for it and the next run draws through Direct3D from the start |
| November 2026 | US retail | Linux | Forces for the second controller: `common.unit.sdl_controller`'s *second's forces* test, with two virtual pads, rumbles each by its own forces alone. Jedi Knight gives none: it has no force feedback ([systems/hosting.md](systems/hosting.md)). A wheel's motor, through SDL's haptics, not yet tried on a real wheel |
| November 2026 | US retail | Linux | A racing wheel as the second joystick: `common.unit.sdl_controller`'s *second* test, with SDL's virtual pad and wheel, reads the pad as the first controller and the wheel as the second, its wheel and pedals; the game reads the second as in the two-joystick check above. Not yet with a real wheel |
| November 2026 | US retail | Linux | Two joysticks: `reluva-tool sith host -pad` with two controllers: *Setup*, *Controls*, *Joystick* lists joystick 2's axes too; its X axis bound to *Turn Left/Right* there, the second controller alone turned the view in Nar Shaddaa |
| November 2026 | US retail | Linux | Joystick: `reluva-tool sith host -pad` with a new player: *Setup*, *Controls*, *Joystick* lists the controller's five axes and buttons once *Disable joystick* is unticked (X turns, Y moves, buttons 1 and 2 fire); in Nar Shaddaa the left stick turned the view and walked Kyle forward. `ctest`'s *joysticks* test reads caps, axes, buttons and the hat |
| November 2026 | US retail | Linux | The whole single-player game, skimmed with `reluva-tool sith host -skim` from *New Game*: the light side's 21 levels and the dark side's (with `-cheat sithlord`, from the Maw on), every cut scene the episode names started, CD 2 put in at level 9, the tallies between levels, both endings and the credits; each level drawn and played a few seconds, nothing stopped. A game saved in Nar Shaddaa (*Save*) and loaded from the main menu (*Single Player*, *Load*) |
| November 2026 | US retail | Linux | Multiplayer: two `reluva-tool sith host -net` on one machine, over its UDP sockets: one hosted *The Duel* (Capture the Flag); the other listed it with its episode, level and players, joined, and saw the host's Kyle in the level; each was told of the other's player, the host of the joiner's leaving. `ctest`'s *directplay* test hosts, joins and passes messages on the loopback network |
| November 2026 | US retail | Linux | Direct3D: `reluva-tool sith host -d3d` with keys into Nar Shaddaa: the level drawn through Direct3D 1's execute buffers in 16-bit colour, textured and depth-tested, the menus still 8-bit; no call left unanswered. `ctest`'s *direct3d 1* test draws through an execute buffer and checks the pixels |
| November 2026 | US retail | Linux | Changing CDs: with CD 2's `23a.smk` among a player's cut scenes seen, *Cutscenes* lists "9 - 8t88's Payment"; played, the game checks for CD 2, drive D becomes CD 2's copy, and the cut scene (only on CD 2) plays, without the game asking for the CD |
| November 2026 | US retail | Linux | CD 1's music from the image's cue sheet and audio tracks beside CD 1's files: `reluva-tool sith folders` finds 7 tracks on CD 1 and 11 on CD 2; `reluva-tool sith host -wav` into Nar Shaddaa: the game plays tracks 2 and 3, and what's heard is track 2 sample for sample (with the script's volume set aside, the bar being quiet) |
| November 2026 | US retail | Linux | Sound: `reluva-tool sith host -wav` through the opening, menus, first cut scene and Nar Shaddaa; what SMACKW32 wrote into its buffer the same as Reluva's decode of `01-02A.SMK`, and what the card played the same as what was written, in order. `reluva` under Xvfb with SDL's disk audio driver: the same sounds reach the output; not listened to |
| November 2026 | US retail | Linux | Played in `reluva`'s window (under Xvfb, driven with xdotool): the crawl, a new player typed, the main menu, *Single Player* and *New Game* clicked, the cut scene skipped, Nar Shaddaa played: walking back with the arrows, turning with the mouse; the game's writes only its player and autosave in the save folder |
| November 2026 | US retail | Linux | `reluva-tool sith host` with keys and clicks: the opening, the menus, the first cut scene and Nar Shaddaa, drawn by the game's own software renderer |
| November 2026 | US retail | Linux | The things placed in Nar Shaddaa, Katarn's house and the fuel station drawn on the Levels page: crates on the ground, characters in place |
| November 2026 | US retail | Linux | `reluva-tool sith formats`: all 35 levels read. The Levels page drawn for Nar Shaddaa, Katarn's house, the fuel station, the tower and level 19B, from their players' starts |
| November 2026 | US retail | Linux | `reluva-tool sith formats`: all 20 sprites and 3 particle clouds read; an explosion's cels and a cloud drawn on the Sprites page |
| November 2026 | US retail | Linux | `reluva-tool sith formats`: all 504 animations and 49 puppets read, every puppet's animations found. Kyle drawn in four animations on the Models page |
| November 2026 | US retail | Linux | `reluva-tool sith formats`: all 477 models read. The Models page drawn for Kyle, a stormtrooper and a crate: joints meeting, textures in place |
| November 2026 | US retail | Linux | The Sounds, Sound classes, Episodes and Cut scenes pages drawn with `reluva-tool sith page`, and a cut scene stepped through 450 frames; without a sound device, so not listened to |
| November 2026 | US retail | Linux | `reluva-tool sith formats`: all 644 sounds, 102 sound classes and 3 episodes read. `reluva-tool sith episode`: every cut scene a step names found on its CD. `reluva-tool sith movies -all`: every frame and the sound of the 25 cut scenes decoded |
| November 2026 | US retail | Linux | The data viewer's pages drawn with `reluva-tool sith page` on the install, and looked over: pictures, materials, colormaps, fonts and strings |
| November 2026 | US retail | Linux | `reluva-tool sith formats` on the install: all 146 pictures, 21 colormaps, 1,983 materials, 27 fonts and 7 string tables read |
| November 2026 | US retail | Linux | `reluva-tool sith folders` on the install, with copies of both CDs beside it: recognised as the US release, both CDs found. `reluva-tool import` from the copy of CD 1 with CD 2 added: recognised as the US release, both CDs found in it |
