# Jedi Outcast: single player's saved games

> **Status:** partial · **Code:** `src/engines/idtech3/jk2/sp_savegame.cpp`, `src/engines/idtech3/raven/savegame.cpp`, `src/engines/idtech3/client/sp_play_page.cpp` · **Checked against:** `jk2sp.exe` 1.02a's code; saved and loaded with `jk2gamex86.dll` in Reluva · **Confidence:** from the code unless marked

## Overview

A saved game is `saves/<name>.sav`: Raven's chunks, as Elite Force's
([../../stvef/systems/sp-interface.md](../../stvef/systems/sp-interface.md),
Saved games), written by `jk2sp.exe` in two steps: what it needs before
the level starts (the version, the comment, the picture, the map, the
cvars, the player carried over), then the level (its time, area
portals, configstrings, then the game's own chunks through `WriteLevel`).
Two things are new since Elite Force: a chunk can be compressed, and the
picture is a JPEG. The game saves its Ghoul2 instances through four
new entries of the Ghoul2 table ([sp-ghoul2.md](sp-ghoul2.md)).

## How it works

### A chunk (writer 0x4394e0, reader 0x4396d0)

Nothing is written while `sv_testsave` is set. Each chunk is its id
(four letters), then either:

- **Stored:** its length, the checksum of its data, the data, and the
  magic word `0x1234abcd`.
- **Compressed**, when `sv_compress_saved_games` is set (1 unless
  changed) and compressing makes it shorter: its length negated, the
  length compressed, the checksum (of the data before compressing), the
  compressed data, and the magic word.

The compression is a run-length code. The decoder (0x4392f0) reads a
control byte: n above 0 repeats the next byte n times; n below 0 copies
the next -n bytes as they are. The encoder (0x439220) writes runs of up
to 127 equal bytes, and between them literals of up to 127 bytes,
ending a literal before three equal bytes in a row.

### Saving (0x438aa0, `save(name, autosave)`)

Any save but an autosave is checked first (0x438a20), silently:
`cl_running` set, no in-game cinematic playing (the flag
`CL_PlayCinematic` keeps, 0x4051e0), the server's level running (its
state 2, set as the level spawns, 0x4373c3), the map not one starting
with `_`, the player's health above 0, and the game's
`GameAllowedToSaveHere` (export 5), which refuses while the game's own
camera scenes play. The save command (0x438390) also
refuses `current`, names with `..`, `/` or `\`, and the dead ("Can't
savegame while dead!"). For the menus' quick save and autosave names,
`quik*` and `auto*`, it takes the screen (0x40b600, 512 x 512), clears
the comment and saves as `quik` or `auto`; for any other name, the
picture is the one the save menu took.

The file is written as `saves/current.sav`, then renamed. Its chunks:

| Chunk | What |
|---|---|
| `_VER` | the version, 1 |
| `COMM` | the comment, 64 bytes: the save menu's, or `---> <map>` for an autosave or when there's none |
| `CMTM` | when it was saved (`time()`) |
| `SHLN`, `SHOT` | the picture's length, then the picture: a JPEG at quality 95 (0x46ff50), 512 x 512; an autosave's is `levelshots/<map>`, scaled |
| `MPCM` | the map, 1024 bytes |
| `CVCN`, then `CVAR` and `VALU` each | the cvars flagged 2 (the test at 0x438552): how many, then each name and value with its NUL |
| `GAME` | 1 for an autosave |
| `CVSV`, `AMMO`, `IVTY`, `FPLV` | an autosave's only: the player carried over (below), from the cvars `playersave`, `playerammo`, `playerinv` and `playerfplvl`, 1024 bytes each |
| `TIME`, `TIMR` | a save the player makes: the level's time, and the time left over from the last frame |
| `PRTS` | the area portals: how many times each pair of areas is joined, areas x areas ints |
| `CSCN`, then `CSIN` and `CSDA` each | the configstrings with text (but the system info, 1): how many, then each index and text |
| the game's | `WriteLevel(autosave)` (export 3) |

An autosave's player (0x4350f0) goes into the cvars as text first:
`playersave` holds sixteen numbers from the player's state (health and
the like, at 0xc4, 0xd0, 0xcc, 0xc8, 0x94, 0x98, 0x9c; the view angles
as floats, 0xa0 to 0xa8; then 0x2e8, 0x344, 0x270, 0x2d4, 0x2e0,
0x2dc), `playerammo` ten counts from 0x184, `playerinv` seven from
0x1ac, `playerfplvl` the eleven force powers' levels from 0x350.

### Loading (0x438fa0)

The version must be 1. The comment, time, picture (0x438950: decoded,
it must be 512 x 512, and is kept for the loading screen) and map are
read; the cvars are set; then `GAME`, and for an autosave the player's
four cvars. The level is started with load mode 2 for an autosave, 1
otherwise; then a save the player made gives back its time, its area
portals (the areas flooded again) and its configstrings; then the game
reads its own, `ReadLevel(autosave, loadTransition)`.

### Automatic saves and level changes

- **A level started** (0x43621e), but a map starting with `_`: a new
  level is saved as `auto`, then as `auto_<map>`, both autosaves. One
  loaded by `loadtransition` from its hub save (the load mode,
  0x12c34a4, not 0, and the level-change flag 0x126b6d8 set) is saved as
  `hub/<map>`, then `auto`, as saves the player made, their picture
  the screen `loadtransition` took.
- **`map`** (0x435510) clears `playersave` and the spawn target.
- **`maptransition <map> <target>`** (0x4354d0) takes the screen, stores
  the player (above), sets the spawn target, and (0x435310) deletes
  `auto` but on a `_` map before starting the level. The game's `Init`
  is told of a level change (its last argument, 0x126b6d8) only by
  `loadtransition`, not by this.
- **`loadtransition <map> [<target>]`** (0x435280), for hubs: sets the
  level-change flag (0x126b6d8), takes the screen, stores the player,
  saves the level left as `hub/<map>` (a save the player makes, so
  checked as above), sets the spawn target, then loads `hub/<next map>`
  (0x4381f0: "Restoring game", without the load command's checks of
  the name) or, if that fails, starts the map as `maptransition` does
  (0x435310); the flag is cleared after. The game sends it from its
  level changer (`target_level_change`, 0x200b3e00 in
  `jk2gamex86.dll`) when its spawn flag 1 is set; no 1.02a map sets
  it (`valley`'s has 2), so in the game as shipped it's only typed.
  And the shipped game can't load a hub save: told of a level change,
  its `ReadLevel` (0x200af940) reads the objectives (`OBJT`) before the
  level's own (`LVLC`), the reverse of the order its `WriteLevel` writes
  them in (0x200af901, 0x200af908), so the load stops with an error.

### After a death: `load *respawn` (0x438230)

`auto` if nothing has been loaded. Otherwise, on `_brig`, `auto` if it
can be read; else `auto` if it's of the level being played (its `MPCM`),
or the saved game last loaded.

### The Ghoul2 instances

The game hands each entity's Ghoul2 instances to entry 95 of the Ghoul2
table, `SaveGhoul2Models(instances, &buffer, &size)` (0x458d10), and
writes the buffer as its own `GHL2` chunk; entry 98 frees it. The buffer
is the count of models, then for each its record from the file index on
(112 bytes), then its surface overrides (a count, 24 bytes each), bone
overrides (148 bytes each) and bolts (16 bytes each). An instance that
isn't valid is a count of 0. Loading, entry 96, `LoadGhoul2Models
(instances, buffer)` (0x4592e0), sizes the instances and copies the
records back, then sets each model with a file up again (its index
found again, 0x453a40). Entry 97 (0x453900) deletes an instance and
zeroes its handle.

## In Reluva

`jk2/sp_savegame` writes and reads both steps, and `raven/savegame`
the chunks, compressed as above. The single-player Play page saves as
the original: `auto` and `auto_<map>` as a new level starts, and `save
<name>` (`F12`, `save quik*`, in `default.cfg`), which takes the next
frame drawn as its picture, scaled to 512 x 512. `load <name>` (`F9`,
`load quik`) and `load *respawn` load as above, the picture shown on the
loading screen. A key's commands run as typed at the original's
console: the engine's own first (save, load, map), then the client
game's and the game's.

`loadtransition` is as above: the screen taken as the next frame is
drawn, the level left saved as `hub/<map>`, the next map's hub save
loaded with the game told of a level change (`Init`'s and
`ReadLevel`'s last argument), or the map started; the level reached
from a hub save is saved again as `hub/<map>` and `auto`. Seen on the
Play page: `loadtransition` from `kejim_base` to `kejim_post` saved
`hub/kejim_base` and started `kejim_post`; back again, it saved
`hub/kejim_post`, and loading `hub/kejim_base` stopped with the game's
error above, as the original's would. A save is refused while an in-game cinematic plays (`ingamecinematic`,
the client game's call), but not under a plain `cinematic` typed over a
running level. `CL_PlayCinematic_f` (0x404920) keeps its third argument
as the flag the check reads, which `cinematic` (0x404c00) passes as 0
and `ingamecinematic` (0x404c20) as 1, and it doesn't stop the server
*(confirmed)*.
The `single-player` tool's level changes don't take `loadtransition`.

## Sources and checks

- **Where the facts come from:** `jk2sp.exe` 1.02a: the chunk writer
  and reader (0x4394e0, 0x4396d0), the run-length code (0x439220,
  0x4392f0), the save checks and command (0x438a20, 0x438390), the
  saver (0x438aa0) and loader (0x438fa0), the picture (0x46ff50,
  0x438950, 0x40b600), the player's cvars (0x4350f0), the level
  commands (0x435510, 0x4354d0, 0x435310, 0x435280), the autosaves
  (0x43621e), `load *respawn` (0x438230); the Ghoul2 entries (0x458d10,
  0x4592e0, 0x453900, 0x453a40). Strings: `default.cfg`'s `F9` and
  `F12` bindings.
- **How it was checked:** `sp-run kejim_post -saveload` (a gamedata
  test) saves the level after 100 frames, 1.2 MB with every Ghoul2
  instance, and loads it into a new game, which reads every chunk back
  and plays on from the saved time. On the Play page, the autosaves are
  written as `kejim_post` starts, and `F12` and `F9` save and load.
