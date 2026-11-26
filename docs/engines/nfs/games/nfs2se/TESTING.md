# Need for Speed II SE: testing

## Automated checks

### Unit tests

In `tests/engines/nfs/unit/`, beside NFS III's, on installs and CDs the
tests build (`makeNfs2seInstall`, `makeNfs2seCd` in
`tests/engines/nfs/fixtures.h`; never the game's files):

| Suite | What it checks |
|---|---|
| `nfs.unit.nfs2se` | NFS II's own formats on files built in the tests: a two-block track (vertices made absolute, the next block's included; levels and their second groups; meshes and placements; a polygon past its level, and a cut file, refused), a `.col` (textures, meshes, placements, the road, told from NFS III's), both kinds of sky and the lights, a car model (the 6 bytes after an odd vertex count; a vertex past its part, bytes left over, a cut file refused), the 3dfx program's fog by track and road point and its sky's ring colours; the paint (the colours' file; the program's paint tables read from a program built in the test, a car's run past the table and another program refused; colours turned to RGB, a palette entry recoloured below and above the threshold and whitened, other colours left); a movie (two frames around its sound; one without sound; frames of two sizes, a cut file and no frames refused) |
| `nfs.unit.host` (its `windows` case) | The host's Windows as `nfs2sen.exe` needs it, called as the program would: a key given to the keyboard hook first, and dropped when the hook keeps it; drawing into a flipping pair's front surface shown at its Unlock; `joyGetNumDevs` |
| `common.unit.mdec` (`tests/common/mdec.cpp`) | MDEC frames built in the test: the macroblocks' order down the columns, the DC's scale and the colours, an escaped AC term, and bad frames refused |
| `nfs.unit.formats` (`shapes nfs2se`, and in `text table` and `music`) | A bank's palette entry given to its pictures without one, a picture's own 5:6:5 palette winning over it, 4-bit pictures' order; the front end's text records with their places; zero padding between music streams, and other bytes there refused |
| `nfs.unit.detection` (its `nfs2se` cases) | The install and the CD recognised by the fallback table, the CD once though both its programs match, an install with only the 3dfx program, and not without the cars' data; the CD's copy found beside the install, by `cdpath`, and missing; the tracks (once each, from the install and the CD), the car models in `cardata.viv`, which folder each program is in, the string tables found, and the race's palette from the loose `hud.fsh` |

### The basic engine test

`nfs.engine`'s `nfs2se` case (`tests/engines/nfs/engine.cpp`), against
its record `tests/engines/nfs/data/engine-record-nfs2se.txt`: a small
install and a CD's copy beside it, recognised and found; the viewer
through its pages (Status, Textures with a pause picture in the HUD's
palette, Strings with their places, Sounds, Music, Movies with a one-macroblock movie); the game, whose program is a
stand-in, which says it can't start it; and the game without the CD's copy, which says where to put it.

### The game-files test

`tests/engines/nfs/gamedata-nfs2se.cmake`, registered as
`nfs.nfs2se.gamedata.<step>`, run on the install named by
`RELUVA_NFS2SE`, with a copy of the CD's files in a folder beside it
(docs/DEVELOPMENT.md, Engine tests):

```bash
RELUVA_NFS2SE=<the install> ctest --test-dir build -L gamedata -R nfs2se
```

On Windows, in PowerShell, with Visual Studio's x64 Release preset
(docs/DEVELOPMENT.md, The game-files test):

```powershell
$env:RELUVA_NFS2SE = "<the install>"
ctest --test-dir out\build\x64-release -L gamedata -R nfs2se
```

| Step | What it records |
|---|---|
| `release` | The release detection finds, and whether the CD's copy is beside the install |
| `survey` | The game's files in the install and on the CD by name, size and MD5, as one checksum: not the CD's `REDIST` and `UTILITY` (DirectX's installers and tools, which a copy may leave out), nor the folders the game writes as it's played (`FeData/pc/config`, `stats`, `save`, `GameData/Prefs`, `TmpTrk`); the listing is left in the step's output folder as `files.txt`. And every file the engine reads, read and counted (`reluva-tool nfs survey`) |
| `formats` | Every archive, picture, string table, font, track, model, sound, music section and movie frame decoded, each kind as a checksum (`nfs survey -hash`) |
| `host_glide` | The same run with the 3dfx program (`nfs2sea.exe`, the install's or else the CD's) on Reluva's Glide stand-in as a Voodoo Graphics, drawn by its software rasterizer: about five minutes |
| `drive` | `nfs2sen.exe` on the same clock, a knockout's first round at Proving Grounds (2 laps, 9 cars) driven to its end by the road driver (`-roaddrive`; [research/road-driver.md](research/road-driver.md)): each lap's place and the finishing place, its end state and its last frame: about four minutes |
| `host` | `nfs2sen.exe` on a clock of instructions (`nfs host -deterministic 300000`): the title movie skipped with Enter, the menu taken to RACE with four Downs, a race at Proving Grounds with Up held; its end state, its system calls and its last frame, as checksums |

The records, `tests/engines/nfs/data/gamedata/nfs2se-en-windows/`, are
the US CD's English install's, made on Linux and the same on Windows.
Its file listing has 1,809 files, among them the set-up's `patch.exe`
(RTPatch's updater, copied from the CD's `UTILITY/PATCH.BIN`), which
virus scanners may flag: a copy without it fails the `survey` step. On the instruction clock
the date is fixed (1 November 1997, every file dated so), so the run is
the same each time.

### What the automated checks don't cover

That the real release is recognised by its hashes: checked by hand
([Last checked](#last-checked)).

## A playthrough by hand

1. Add the install in the launcher, with a copy of the CD beside it: it's
   found as "Need for Speed II SE".
2. Start it: the EA logo and the title movie (Enter skips it), then the
   main menu. Up and Down move along it, every press; the mouse moves its
   cursor. RACE starts a race at Proving Grounds: Up accelerates.
   Ctrl+F5 goes back. With `renderer=glide` the same, on the 3dfx
   program. With two copies (on two machines, or one with
   `reluva-tool nfs host -net -netaddr`): *game setup*'s game type
   *network*, CONNECT, *create game* on one and *join game* on the
   other; both press RACE in the lobby, the host's last.
3. In `reluva-viewer`, the Status page lists the install, the CD's copy
   (beside the install), 19 music streams, 15 movies, `nfs2sen.exe` in
   the install and `NFS2SEA.EXE` on the CD, 8 tracks (`TR00`, `TR02` to
   `TR08`) and 53 car models.
4. Move the CD's copy away and start it again: it says the CD's files
   weren't found.
5. In `reluva-viewer`, the Tracks page: each track (PageUp/PageDown)
   drawn whole, its trees and fences see-through; Up/Down along the
   road. The Cars page: each model, L through its levels. The Movies
   page: each movie (Up/Down) with its sound in step, Left/Right 5
   seconds.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US CD and an install from it | Linux | The host delivering input before the main thread's functions only: 63 Downs and Enter on two copies on the real clock, none lost (4 of 64 before); late connects on the serial link, 15-25 s apart, all to the lobby with the copies started 3 s apart; a software and a 3dfx copy linked and racing; the game-files host records made again ([research/network-play.md](research/network-play.md)) |
| November 2026 | US CD and an install from it | Linux | Two copies on the serial link, both driven (`-roaddrive`): Proving Grounds, 4 laps, to the race summary, the same on both; again with `-netdrop 10`; one copy killed mid-race (the other's race *aborted*); two copies of the 3dfx program linked and racing ([research/network-play.md](research/network-play.md)) |
| November 2026 | US CD and an install from it | Linux | The road driver (`-roaddrive`) on the host, deterministic: Proving Grounds, a one-lap race 2nd of 8; the knockout's first round 7th of 9, the player advancing on the knockout standings ([research/road-driver.md](research/road-driver.md)) |
| November 2026 | US CD and an install from it | Linux | The road driver at the computer cars' speeds: a 4-lap single race won on Proving Grounds, Last Resort, North Country and Mediterraneo; the knockout's first round 4th of 9 ([research/road-driver.md](research/road-driver.md)) |
| November 2026 | US CD and an install from it | Linux | A knockout to its end on the host: round 1 (Proving Grounds, 2 laps, 9 cars) quit from the pause menu, the player eliminated, the knockout standings, then the main menu back to a single race (PLAN.md, Phase 6) |
| November 2026 | US CD and an install from it | Linux | A tournament to its end on the host (`-keysfile`): seven races, the first driven with Up held (8th, 8:19.46), the rest quit from the pause menu; each race's summary and standings, then the tournament summary and the main menu (PLAN.md, Phase 6) |
| November 2026 | US CD and an install from it | Linux | A race's presents: one whole picture every 23-29 ms; a wheel with force feedback (`-joystick Wheel -wheel -ff`) mapped in the game's controls setup, then a race on it (accelerator `joy:4`, steering `joy:0`): 224 force changes, a spring following the road and two full pushes (`-forcelog`) |
| November 2026 | US CD and an install from it | Linux | Two copies on the host (`-keysfile`), raced at Proving Grounds over the network (IPX over UDP, 127.0.0.2 and .3), a serial link and a modem call, each from its connect screen through the lobby ([research/network-play.md](research/network-play.md)) |
| November 2026 | US CD and an install from it | Linux, Xvfb | The 3dfx program (`renderer=glide`) from `reluva-viewer`'s Play page: the title movie, the menu by the keys, a race at Proving Grounds with Up held (about 20 frames a second, Glide's GPU backend on Mesa's software Vulkan); on the host (`-program`), its movies, front end and race on the software rasterizer, `grChromakeyValue(0x00FF00)` in the race; the `host_glide` step twice, the same record |
| November 2026 | US CD and an install from it | Linux | The game-files test (`RELUVA_NFS2SE`), its four steps twice with the same records; on the host: tournament and knockout races started, the attract mode's movies, the sound recorded (`-wav`) through the movie, menus and a race, the save folder's files, a wheel with force feedback offered (`-joystick -wheel -ff`) and taken |
| November 2026 | US CD and an install from it | Linux, Xvfb | The game from `reluva-viewer`'s Play page: the title movie, Enter to the menu, Down four times to RACE (each press taken), the race at Proving Grounds, Up held to 132 mph |
| November 2026 | US CD and an install from it | Linux | `nfs2sen.exe` on the host (`reluva-tool nfs host -deterministic 300000`): the EA logo and title movie, the front end, a race at Proving Grounds driven with the keys, at about 39 frames a second ([research/hosted-spike.md](research/hosted-spike.md)) |
| November 2026 | US CD and an install from it | Linux | `reluva-tool nfs survey` over the CD: 15 movies, 9,340 frames and 15 soundtracks read, none failing; frame 100 of every movie and every 50th of `F355AV.DCT` (`dct-frames`) against FFmpeg's decoding: within 8 in 255 per colour; the Movies page drawn (`reluva-tool nfs page`) |
| November 2026 | US CD and an install from it | Linux, Xvfb, Mesa's software Vulkan (llvmpipe) | The Tracks page on Proving Grounds and Monolithic Studios (`RELUVA_NFS2SE_TRACK_VIEW`): the road, barriers, floodlights, the stranded ship, trees see-through once `0x07C0` is keyed out, the studio's streets, sci-fi set and neon section; the Cars page on `futr` and the F355 (`RELUVA_NFS2SE_CARS_VIEW`): its tail lights, grille and plates |
| November 2026 | US CD and an install from it | Linux | `reluva-tool nfs survey` over the CD and the install with NFS II's own readers: all 8 tracks, 8 `.col` files, 34 skies, 8 light files and 53 car models read, nothing failing; Proving Grounds and the F355 and T. rex exported (`trk-obj`, `geo-obj`) and drawn elsewhere |
| November 2026 | US CD and an install from it | Linux | `reluva-tool nfs survey` over the CD and the install: every archive, picture bank, picture, text table, font, sound bank and sound, music stream, segment and map, and `.qda` file read, none failing but NFS II's own `.col` and `.hrz`; the Textures page's pause screen (`PAUSEEN.QFS`'s `head`) in the HUD's palette beside its 3dfx copy, and the Strings and Music pages, drawn (`reluva-tool nfs page`) |
| November 2026 | US CD and an install from it | Linux | The install and the CD recognised as the release (`reluva-tool detect`), and an install with only `NFS2SEA.EXE` as an unknown release; the Status page drawn from the install with the CD's copy beside it (`reluva-tool nfs page`), with the counts in [A playthrough by hand](#a-playthrough-by-hand) |
| November 2026 | US CD and an install from it | Linux | NFS III's readers over every file (`reluva-tool nfs survey`), and `nfs2sen.exe` loaded in the x86 emulator ([research/first-survey.md](research/first-survey.md)) |
