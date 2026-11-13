# Need for Speed III: testing

## Automated checks

### Unit tests

`tests/engines/nfs/unit/`, a file per topic, each a suite
`nfs.unit.<topic>`, on data built in the tests (never the game's files;
the builders are in `tests/engines/nfs/fixtures.h`):

| Suite | What it checks |
|---|---|
| `nfs.unit.detection` | Recognising the game, finding the CD's files, the listings, the game's files in archives |
| `nfs.unit.formats` | RefPack, `.viv`, `.fsh` and the cockpit's, text, fonts, `.fce`, the sky, collision, sound banks, music, movies |
| `nfs.unit.track` | The Tracks page's models: lighting, weather, animation, reflections, placed sounds, headlights, the road frame, particles, the cars' effects and paint |
| `nfs.unit.computer` | The computer cars' gears, air, driving, catching up, pursuits, hits and the cops' calls |
| `nfs.unit.host` | The hosted game's force effects, 3D sound and THRASH's fog, Direct3D look and blends |

The shared code this game needs is tested with the rest of it:
`common.unit.x86_more` (exact FILD/FISTP, segment registers, the
readable first page, pokes), `common.unit.mmx`, `common.unit.drives`
(drives, a serial port), `common.unit.mixer`, `common.unit.ea_audio`,
`common.unit.prologic`, and the Glide stand-in's `common.unit.glide`,
`glide_more`, `glide_boards` and `glide_gpu`.

### The basic engine test

`nfs.engine` (`tests/engines/nfs/engine.cpp`), without the game's files:
an install and a copy of the CD made in code (a track, a car's archive,
pictures, strings, a sound bank, a movie, and a stand-in for the
executable), found as the launcher finds them, every page shown in turn
with keys, and the format readers on the same files; against its record
(`tests/engines/nfs/data/engine-record.txt`).

### The game-files test

With your own copy, `RELUVA_NFS3=<the install> ctest --test-dir build -L
gamedata`, the CD's files beside the install as `NFS3-CD`.
`tests/engines/nfs/gamedata.cmake` runs `reluva-tool nfs` commands on the
game, one step each:

| Step | What it runs |
|---|---|
| `release` | What the game is (`reluva-tool detect`), and whether the CD's files are beside it |
| `survey` | Every file of the install and the CD by name, size and MD5, as one checksum; every file the engine reads, read and counted (`survey`) |
| `formats` | Every archive, picture, string table, font, model, track, collision file, sound, music section, movie frame and movie's sound decoded, each kind as a checksum (`survey -hash`) |
| `host` | The retail game run from the menus into a Hometown race by clicks, on an instruction clock (`host -deterministic 50000 -nomovie -native`, 3.5 billion instructions): its end state, its system calls and the last frame written (in the race), as checksums |
| `host_glide` | The same with its own Voodoo 2 renderer on the Glide stand-in (`-glide`) |

The records, in `tests/engines/nfs/data/gamedata/<release>/`, hold only
counts and checksums. They were made on Linux with the US Classics CD's
install (`classics`); the hosted steps' records depend on the system's
maths library (docs/DEVELOPMENT.md, Engine tests).

### What the automated checks don't cover

The sound as heard, the GPU's pictures, live input, force feedback and
playing over a real network.

## A playthrough by hand

1. Add the install (with the CD's files beside it) in the launcher; it's
   found as "Need for Speed III: Hot Pursuit (Classics)".
2. Tab through the pages: the tracks and cars in 3D, textures, strings,
   fonts, sounds, music and movies.
3. On the Play page: the intro movie (Esc skips it), the menus with the
   mouse, a Single Race on Hometown; the HUD, the sound, the cars.
4. A Hot Pursuit race, and a race on another track by night and in the
   rain.
5. Two copies on two machines (or two addresses on one): a network race
   over TCP.
6. `renderer=glide` and `renderer=direct3d`: the same race, its look.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| 13 November 2026 | Classics (`27, 2.0`) | Linux, after the merge into main | The automated checks; `reluva-tool nfs host` into a Hometown race natively and with `-glide`; a two-copy TCP race; the Tracks page |
