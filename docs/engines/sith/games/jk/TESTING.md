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

Drawing on the graphics card against the software drawing, which needs
a GPU (or Mesa's software Vulkan, under `xvfb-run` without a display):
`reluva-tool sith host <install> -d3d -gpu <folder>` with the same
`-keys` as the game-files test's `level` step, plus `26:esc`, and
`-seconds 40`. It runs the game with its scenes drawn both ways, and
prints how many pixels differ by more than a step of the game's 16-bit
colours where the scenes draw. Software, GPU and difference pictures
of every 20th scene and the worst go into the folder. `-gpuplay
<folder>` draws the frames as play does, the 2D over the scene, one in
60 written there. `-gpuscale N` draws them N times the size; `-gpu`
then averages each N by N back to one pixel to compare. `-gpuaa N`
draws with N samples a pixel (2 or 4, as the device has).

The frame rate: `-fps N` shows at most N frames a second, as the player
does, and `-everyframe` has the world move at every frame. The run ends
with how many frames differed from the one before: the world's steps
shown.

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

### On the graphics card, and the frame rate

On a machine with a graphics card, in the launcher *3D drawing* on the
graphics card:

- With the game's *Enable 3D Acceleration* off in *Setup*, it's on when
  the game starts, and the 3D is drawn on the card.

- The launcher shows *3D drawing*, *Picture size*, *Anti-aliasing* and
  *Frame rate* for the game, and keeps what's chosen when Reluva is
  started again.
- Nar Shaddaa looks as in software, at 1x; the gauges, text and the
  automap (Tab) lie over the 3D; the menus look as before.
- At 2x to 4x the 3D is finer and the picture the same; the menus'
  pointer lands where it's pointed, and the mouse turns the view as
  fast as at 1x. With *Anti-aliasing* 4x the polygons' edges are smooth
  and nothing else changes; off, they're stepped.
- At *Frame rate* 100 (on a display of 100 Hz or more) and 60, play is
  smoother than *As the game*, at the same speed: a door, a lift or
  the walk across the bar takes as long timed by hand at each; a fight
  with the bar's thugs and the level's first troopers plays as at *As
  the game*; music and sound stay with the picture.
- A multiplayer game between two copies at *Frame rate* 100, and at
  one 100 and one *As the game*: players move at the same speed on
  both screens.
- On a display slower than the frame rate, and with vsync on and off:
  no tearing beyond the display's own, no slowing down.
- A level or two played through at length at 100, to find anything
  that runs differently (scripted events, enemies, the Force powers).

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US retail | Linux | *3D drawing* on the graphics card with *Setup*'s 3D acceleration off in the game's registry (`reluva` on a virtual display): turned on as the game started (`b3DAccel` 1 after), Nar Shaddaa drawn through Direct3D on the GPU; `host -d3d`, now the same code, reached the level drawing through Direct3D |
| November 2026 | US retail | Linux | The new settings through `reluva` itself on a virtual display (Xvfb, llvmpipe), driven by `xdotool`, with *3D drawing* on the graphics card, 2x, 4x anti-aliasing and *Frame rate* 100: the opening skipped, *Players*, *Single Player* and *New Game* by mouse clicks at 2x, the cut scene skipped, Nar Shaddaa walked, turned by the mouse and jumped in, the automap (Tab) over the 3D, the game's menu (Esc) and back; the same route with the defaults (software, *As the game*). The whole light side skimmed (`host -skim -fps 100 -everyframe`, on the GPU with `-gpuplay -gpuscale 2 -gpuaa 4` and in software): every level and cut scene reached; but in 3 of 12 skims Reluva crashed as the ending's cut scene (`52-53a.smk`) started, in the JIT's native code (gdb): the first three, run within the same hour; the nine since (three of them with a build reporting the crash, two with the JIT's buffer clears logged, of which there were none, two keeping a core, and two with the build of the first three rebuilt) ended normally. Not seen in a skim at the game's own rate, nor in one with the JIT off (`RELUVA_NO_JIT`), one run each. The *Load Game* list leaves out autosaves as the game means it to ([systems/hosting.md](systems/hosting.md), step 23). Level 21 (the light side's) loaded from an autosave given a listed title (`x~...`) and skimmed into the ending: 24 times without a crash, so the crash needs the long run before |
| November 2026 | US retail | Linux | The game's timing: `reluva-tool sith host -fps N [-everyframe]` in Nar Shaddaa, turning for 12 seconds: as the game, 335 new pictures in 618 frames at 60; every frame, 624 of 626 at 60, 988 of 1,043 at 100. With the player logged at each step (temporary code), the same walking speed, stop and jump at about 42 and 79 steps a second |
| November 2026 | US retail | Linux | Anti-aliasing on the graphics card (llvmpipe, 4 samples a pixel, 640x480): `-gpu -gpuaa 4` into Nar Shaddaa, 349 scenes against the emulator's drawing, 2.1% of pixels more than a 16-bit step apart, along the polygons' edges; `-gpuplay -gpuaa 4`: the level and gauges as without, the edges smoothed |
| November 2026 | US retail | Linux | Direct3D on the graphics card at twice the size (llvmpipe, 1280x960): `-gpu -gpuscale 2` into Nar Shaddaa, 107 scenes averaged back to 640x480 against the emulator's drawing, 3.2% of pixels more than a 16-bit step apart, at edges and fine detail; `-gpuplay -gpuscale 2`: the same picture, finer, the gauges enlarged over it. `sith.unit.render`'s *scene scaled*: viewports, pixel centres and the 2D at twice the size |
| November 2026 | US retail | Linux | Direct3D on the graphics card (Mesa's llvmpipe Vulkan, 640x480): `reluva-tool sith host -d3d -gpu` into Nar Shaddaa, 440 scenes drawn on the GPU against the emulator's drawing: 0.0001% of the pixels where they draw more than a 16-bit step apart, the worst scene 11 pixels, where triangles meet at the same depth ([systems/hosting.md](systems/hosting.md), step 21); `-gpuplay`: the gauges over the level. `common.unit.windows`: a scene kept instead of drawn, its triangle and Z clear as given, the 2D after it on top |
| November 2026 | US retail | Linux | Windowed: `reluva-tool sith host -args -windowGUI`: the opening, the logo and loading screen, *New Player* typed and clicked, *Single Player*, *New Game*, the cut scene and Nar Shaddaa, each frame blitted through GDI and shown, the first level reached at 26 seconds as full screen. `common.unit.windows`'s *window drawing* test: a DIB blitted into a window's device context reaches the engine, and again once the window grows |
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
