# Star Trek: Birth of the Federation: saved games (`.sav`)

> **Status:** partial · **Code:** `src/engines/botf/formats/save_game.cpp` · **Checked on:** the English retail install's four tutorials, `game0.sav` to `game3.sav`: every file decoded, `gameInfo`'s fields and the galaxy's files read (November 2026) · **Confidence:** confirmed unless marked

## Overview

A saved game is an archive of the game's state, one file per part of it
(`gameInfo`, `techInfo`, the empires, the map, the AI ...). The install
keeps one per Load Game slot, `game0.sav` to `game11.sav`, top down (a
fresh install has the four tutorials as `game0.sav` to `game3.sav`), with
`Auto.sav`, the autosave the main menu's Continue loads, and `Last.sav`,
a copy of it, beside them. A fresh install has every one of them: the four tutorials, and
the other ten files 0 bytes long (`Game4.sav` to `Game9.sav`, `Auto.sav`
dated December 1998, `game10.sav` and `game11.sav` August 1998,
`Last.sav` April 1999). An empty slot on the Load Game screen is one of
those. Reluva reads only what the load and save screens show: each
game's description, and its player's empire, whose colour the slot's text
takes.

## Layout

The archive is [alt.res's](alt-res.md): a 24-byte header (a 16-byte name,
the directory's offset, the number of files), each file's LZSS data, and a
directory of 40-byte entries. Two differences:

- **The header's name** is the save's file name, `game0.sav` in
  `game0.sav`, up to 15 characters (the Ultimate Editor refuses longer
  ones), not "alt.res".
- **The LZSS window** starts filled with spaces (`0x20`) for every file but
  `techInfo`, whose window starts filled with zeros as `alt.res`'s does
  *(published)*. Decoded with the wrong fill, any reference into the window
  before it's been written gives the wrong bytes. In `game0.sav` no file
  but `result.lst` has such a reference, so the save doesn't show which
  fill is right; `result.lst` is stored in 61 pieces (its entry's sixth
  field), which Reluva doesn't read yet.

`auto.sav`, an autosave at turn 1, holds 539 files; there too only
`result.lst` decodes differently with the two fills. `game0.sav` is 81,944 bytes: 249 files, the directory at 55,924, every
file decoding to its stated size. Some files are empty (`starBaseInfo`,
`GTFStasis`: offset 0 or the next file's, both sizes 0).

The Ultimate Editor's decoder writes its window from 4,079 and reads a
reference's position plus 4,078, which comes to `alt.res`'s: writing from
1, references read where they say. It fills all but the window's last 17
bytes; those are the first 17 written, so no reference reads them before
they are, and Reluva fills the whole window *(published)*.

### `gameInfo`

The parts the screens use:

| Offset | Type | Field |
|---|---|---|
| 0x00 | `char[13]` | the save's file name again (`game0.sav`) |
| 0x0D | `char[51]` | the game's description, zero-terminated, Windows-1252: "Tutorial #1", what the Load Game screen shows |
| 0x40 | `i32` | the turn, 0 the first (0 in Tutorial #1) *(published)* |
| 0x4C | `i32` | 1 for a multiplayer game (0 in both saves) *(published)* |

Further on is the game's setup, which Reluva doesn't read yet. The
offsets are the Ultimate Editor's; what the values mean was checked on two
saves: the first tutorial's `game0.sav`, and an `auto.sav` at turn 1 of a
game the user set up as the Federation, every empire Early, a ring galaxy
of medium size, many minor races, Normal difficulty and random events on:

| Offset | Type | Field | Tutorial #1 | `auto.sav` |
|---|---|---|---|---|
| 0x252 | `u8` | the player's empire, 0 Cardassians to 4 Romulans (`race.rst`'s order) | 4: the Romulans, as the manual says the tutorials are | 1: the Federation |
| 0x254 | `u8[5]` | each major empire's civilization level, 0 Beginning to 4 Advanced | 1 1 1 1 2 | 1 1 1 1 1: all Early |
| 0x260 | `i32` | galaxy shape: 0 irregular, 1 elliptical, 2 ring, 3 spiral | 0 | 2: ring |
| 0x264 | `i32` | galaxy size: 0 small, 1 medium, 2 large | 1 | 1: medium |
| 0x268 | `i32` | minor races: 0 none, 1 few, 2 some, 3 many | 3 | 3: many |
| 0x26C | `i16` | difficulty: 1 simple to 5 impossible, one more than the setup screen's order | 1 | 3: normal |
| 0x26E | `i16` | random events: 1 on | 0 | 1: on |
| 0x290 | `i32` | tactical combat: 0 manual, 2 automatic (`last.sav`, beside `Stbof.ini`'s `TACTICAL=ABSTRACT`) | 0 | 0 |
| 0x2A4, 0x2A8 | `i32` | the strategic and tactical timers: 0 none *(published)* | 0, 0 | 0, 0 |

A third save, the user's `last.sav` (664 files, at turn 318, the
Federation), holds 2 for tactical combat, 1 (elliptical) for the shape, 0
(small) for the size, no minor races, difficulty 1, every empire at 0
and random events off: the setup `Stbof.ini` keeps
([systems/game-files.md](../systems/game-files.md)). Its header's name is
`auto.sav`, so it's presumably an autosave copied.

Each value checked is one of those the saves show; the others in each
field (a small or large galaxy, a spiral one ...) are the setup screen's
order, which the ones seen fit *(inferred)*. The timers weren't set away
from none in any of them, so their other values are the editor's. `auto.sav`'s description is empty, and its header's name is
`auto.sav`.

### The files

What each save holds, by name; `#` is a number. The four tutorials hold 249
to 614 files, the same kinds in each but for the AI's tasks, which only
the later three have. What each is for is the Ultimate Editor's
(`ue/edit/sav/files/`) *(published)*; the galaxy's four are worked out
below.

| Files | What they hold |
|---|---|
| `version`, `SeedInfo`, `gameInfo` | the save's version, the random seeds, the game's setup and state ([`gameInfo`](#gameinfo)) |
| `galInfo`, `sector.lst`, `systInfo`, `stellInfo` | the galaxy: its size, its sectors, its star systems and its other objects ([below](#the-galaxy)) |
| `starBaseInfo`, `monster.lst` | outposts and starbases; space monsters |
| `empsInfo`, `alienInfo`, `RToSInfo`, `intelInfo`, `techinfo`, `treaty`, `incident.dat` | the empires, the minor races, their relations, intelligence, research, treaties, incidents |
| `provInfo#`, `trdeInfo#`, `trdeInfo#.cnt`, `SphOfIn#`, `alienTFInfo#` | each empire's provinces, trade routes and sphere of influence; each minor race's task forces |
| `strcInfo`, `ordInfo`, `result.lst` | the systems' buildings, the empires' orders, the last turn's results |
| `GShipList`, `GShipHead`, `GTForceList`, `GTForceHd`, `GWTForce`, `GWTForceHd`, `GTFStasis`, `GTFStasisHd`, `ShipCntr`, `TForceCntr`, `shipname.dat` | ships and task forces, and the names given to ships |
| `AIAgent#`, `AINumAgents`, `AIAgtIdCtr`, `AISysUnt`, `AIShpUnt`, `Agt*#`, `AITask#`, `AINumTasks`, `AITaskIdCtr`, `TskSh#`, `TskSy#` (and their `.cnt`) | the computer players' state: 19 agents, one for each race that plays, and their tasks |

### The galaxy

Four files, which the other files' numbers refer to. The layouts are the
Ultimate Editor's (`GalInfo.java`, `Sector.java`, `SystemEntry.java`,
`StellEntry.java`) *(published)*; what's listed was checked on the four
tutorials, which share a medium galaxy of 16 by 12 sectors, 45 star
systems and 12 other objects. Numbers are little-endian.

**`galInfo`**, 48 bytes:

| Offset | Type | Field |
|---|---|---|
| 0x08 | `i32` | the galaxy's size: 0 small, 1 medium, 2 large (1 in the tutorials, medium as their setup says) |
| 0x0C | `i16` | star systems: `systInfo`'s count |
| 0x0E | `i16` | other objects: `stellInfo`'s count |
| 0x10 | `i16` | outposts and starbases: 0, 4, 6 and 7 in the four tutorials, as the player builds them |
| 0x14 | `i16` | sectors: `sector.lst`'s count, 192 |

The first 8 bytes are the same in the four tutorials and not known; the
rest are zeros.

**`sector.lst`**, 104 bytes a sector, row by row from the top left:

| Offset | Type | Field |
|---|---|---|
| 0x00 | `i32` | its row, from 0 |
| 0x04 | `i32` | its column, from 0: the last sector's row and column give the grid's size |
| 0x08 | `i16` | its outpost or starbase, or -1 |
| 0x0A | `i16` | its other object, an index into `stellInfo`, or -1 |
| 0x0C | `i16` | its star system, an index into `systInfo`, or -1 |
| 0x0E | `u16` | the empires claiming it, a bit each from the Cardassians: the Ferengi home system's sector has 4 |
| 0x28 | `u64` | the races that have explored it, a bit each *(published)* |

In each tutorial the sectors with a system are as many as `galInfo` says,
and so are those with an object. The rest of the entry (the races'
agreements on who owns it, task forces there, each empire's scanning) is
the editor's and not read yet.

**`systInfo`**, the systems one after another, each a fixed part of 0x328
bytes and then what that part counts: its planets (76 bytes each), a
32-bit word for each planet, its specials (64 bytes each), a word for each
trade route, and, for the player's systems, its production queue (108
bytes). Read so, each tutorial's file comes out to its last byte. The
fixed part's fields read:

| Offset | Type | Field |
|---|---|---|
| 0x00 | `i32` | its number, from 0 in order |
| 0x04 | `i32` | 2 for the player's systems, 1 for the other inhabited ones in a new game; in the tutorials the computer's home systems have 0 and their colonies 1, so what sets 0 isn't known |
| 0x08 | `char[40]` | its name: "Acamar" |
| 0x31 | `u8` | the star's type *(published)* |
| 0x32 | `char[5]` | the star's pictures' name: "s-o-" |
| 0x40 | `i32` | its population, as the system screen shows it: Sol 310, then 320 a turn later |
| 0x44 | `i16` | the race living there, by `race.rst`'s number (5 and on for the minor races), or -1 |
| 0x4C | `i32` | the race holding it, the same; 0x23 for no one |
| 0x54 | `i32[5]` | the population working in industry, energy, food, research and intelligence: 10 for each building manned (Sol's 15 replicators, 4 reactors, 10 farms, 2 universities and no databanks manned: 150, 40, 100, 20, 0, adding up to its 310) |
| 0x6A | `i16` | its planets |
| 0x6C | `i16` | its specials: 0 in every system seen |
| 0x6E | `i16` | its natural dilithium *(published)* |
| 0x80 | `i32` | what it's building: 5 trade goods, 1 a building (Sol's after a Type 2 Databank was queued), 3 a ship (after a destroyer was) |
| 0x88 | `i16` | with 1 there, the building's `edifice.bst` number: 154, "Type 2 Databank" |
| 0x90 | `i16` | with 3 there, the ship's `shiplist.sst` number: 30, the Miranda class Destroyer |
| 0xBC | `i32` | its maximum population, ten times what the screen shows: Sol's 3,350 for 335 |
| 0x78, 0x7C | `i32` | where it's drawn on the galaxy's small map, 5 pixels a sector: Acamar, in column 6 and row 0, at 32, 3 |
| 0x308, 0x309 | `u8` | its trade routes, coming in and its own |

A planet, 76 bytes: its name (`char[40]`, ending in a line break:
"Acamar I\n"), its picture (`char[14]` at 0x28: "vsal.ani"), its number
in the system (`i16` at 0x36), its size (`i16` at 0x38, 0 to 2), and its
type and atmosphere (`u8` at 0x48 and 0x49) *(published)*. The fixed
part's other fields (production, morale, buildings, trade) are the
editor's and not read yet. Saved at a new game's first turn, before one
has passed, the fields the turn works out (output, consumption, the
workers' shares) are all 0: the system screen works them out itself
then, and the save a turn later has the screen's numbers (Sol's food 357,
energy 265 less 55, industry 234, research 34). The player's production
queue, at the end, starts with the first item's build order (as at 0x80),
8 bytes in its building and 16 bytes in its ship: they changed with the
databank and the destroyer queued. Its last word is 1 when the first item
is bought this turn (the editor's "buy first"): it was after the user
bought the databank.

**`stellInfo`**, 88 bytes an object:

| Offset | Type | Field |
|---|---|---|
| 0x00 | `i32` | its type: 0 black hole, 1 X-ray pulsar, 2 nebula, 10 wormhole, 11 radio pulsar, by the names the tutorials give them |
| 0x04 | `char[40]` | its name, by its sector's column letter and row from 1: "Black Hole C4" |
| 0x2C | `char[20]` | its picture: empty in the tutorials |
| 0x44, 0x48 | `i32` | where it's drawn on the small map, as a system: "Black Hole C4" at 12, 17 |

### The empires

`empsInfo` holds the five major empires, Cardassians first, in 444-byte
entries. Read as the editor gives them (`EmpsInfo.java`), checked against
the galaxy screen's empire panel in five of the saves:

| Offset | Type | Field |
|---|---|---|
| 0x00 | `char[40]` | its name: "the United Federation of Planets" |
| 0x28 | `char[40]` | its leader's: "the Federation President" |
| 0x54 | `i32` | its credits now: 2,000 at the start, 1,835 once the databank was bought, 2,086 a turn later, 2,449 five turns later, as the panel's total credits |
| 0x58 | `i32` | its credits a turn: 86, 70 with the databank or a destroyer in the queue instead of trade goods, 88, 92 |
| 0x60 | `i32` | its dilithium a turn: 1. The panel shows 0 with the destroyer queued, while the file still has 1 |
| 0x68 | `i32` | its research points a turn: 34, as Sol's research; 0 in the save at the very start, before a turn has worked them out |
| 0x6C | `i32` | its population, all its systems': 310, 320, 335 |
| 0x98 to 0xB0 | `i32[7]` | the same seven figures as they were when the turn began: buying lowers the credits at 0x54 and not those at 0x98 |

At the start the empires' credits as the turn began were 800, 2,000,
1,600, 1,000 and 1,200, Cardassians to Romulans, on Simple difficulty.

### Research

`techinfo` starts with a count of the races in the game, a 16-bit index
of every race (35), and then a 20-byte entry for each major empire,
Cardassians first. Its 6 bytes in are the empire's shares of research,
`i16` percentages for biotech, computers, construction, energy,
propulsion and weapons, as the research screen orders them: the
Federation's 17, 17, 17, 17, 16, 16 at the start, and 20, 17, 15, 15, 16,
17 once the user changed them, as the screenshots before and after show.
The rest of the file, each race's research, is the editor's
(`TechInfo.java`) and not read yet. Its window starts full of zeros
([above](#layout)).

### Task forces

`GWTForce` holds the task forces, a ship alone being one, in 108-byte
entries; `GWTForceHd`, 40 bytes, says each entry's size (`i32` at 0x10:
108), how many the list has room for (0x14: 32) and how many are in use
(0x18: 11 at the start, two for each empire and one more). An entry, as
the editor gives it (`TaskForce.java`), checked as noted:

| Offset | Type | Field |
|---|---|---|
| 0x00 | `i16` | its number, as `TForceCntr` counts them |
| 0x04 | `i32` | its ships |
| 0x08 | `i32[9]` | the ships' numbers, as [`GShipList`](#ships) numbers them |
| 0x2C | `i32` | its mission: each empire's two start with 6 and 5, engage and evade by the editor's names *(published)* |
| 0x30 | `i32` | the sector it's ordered to, row by row, or -1: 113 for column 9 of row 8 on a small galaxy's 13 columns, ordered a sector west of Sol |
| 0x34 | `i32` | the race it belongs to |
| 0x38, 0x3C | `i32` | its sector's row and column: Sol's 8, 10, then 8, 9 the turn after the order |

Ordered to move, a task force's entry in `GWTForce` changes (the
destination, and the order's number at 0x54); a turn later it's in the
new sector and `GTForceList`, which is otherwise the same list, has it
too.

### Ships

`GShipList` holds the ships, in 116-byte entries; `GShipHead` is laid out
as `GWTForceHd`, with 116 for the size, and says how many are in use. The
layout is the editor's (`Ship.java`); checked on the tutorials and the
user's new game, whose 11 ships at the start are each empire's two, as
the galaxy screen's empire panel lists the Federation's ("Science
Vessel", "Colony Ship"), and the Edo Guardian:

| Offset | Type | Field |
|---|---|---|
| 0x00 | `char[40]` | its name: "USS Martynov A" |
| 0x28 | `i16` | the race it belongs to, by `race.rst`'s number (42 and 43 for space monsters' races) |
| 0x2A | `i16` | its number. `ShipCntr` holds the next one; ships lost leave gaps: Tutorial #4 numbers its 81 ships up to 108, its `ShipCntr` 109 |
| 0x38 | `i32` | its shields: its class's (`shiplist.sst`), 110 for the Oberth class |
| 0x3C | `i32` | its hit points: its class's when whole, 19 |
| 0x40 | `i32` | its crew's experience: 350 for every Klingon ship at the start, 0 for the others' |
| 0x50 | `i16` | the task force it's in, whose entry lists it: so in all 15 saves |
| 0x52 | `i32` | its class, by `shiplist.sst`'s number: 35, the Oberth class Science Vessel |

The Edo Guardian's shields are 0 at the very start and its class's 6,350
a turn later. The rest of the entry (accuracy and defense bonuses,
cloaking, agility, what it can do) is the editor's and not read yet, as
is `shipname.dat`.

### What a turn and an order change

Saves from one game (Federation, small galaxy, few minor races, Simple,
random events off), each made from the first: as it started; a turn
later; with a building queued at Sol; with a ship ordered to move, and a
turn after that; with the research shares changed; with a worker moved;
with the databank bought; with a destroyer queued; five turns later.
Saved again without a change, a game's `SeedInfo`, `gameInfo`,
`empsInfo`, `result.lst`, `AIAgent0` to `AIAgent6` and the four list
heads (`GShipHead`, `GTForceHd`, `GWTForceHd`, `GTFStasisHd`) change
anyway. Each change the player made is an order in `ordInfo`
([below](#orders)); beyond that, queuing a building, buying it and
queuing a ship each changed `systInfo`, a move `GWTForce`, the research
shares `techinfo`, and moving workers only a flag in `systInfo` (0x314,
the editor's "update worker assignments"). A turn changes most files,
and adds the AI's tasks (`AITask#`, `TskSh#`, `TskSy#`).

### Orders

`ordInfo` holds the turn's orders, the player's and the computer's,
carried out when the turn ends; read so, each save's file comes out to
its last byte. Each order is a 32-byte head and then its own part, as
long as the head says. The layouts are the editor's (`Order.java`,
`EconomicOrder.java`, `MilitaryOrder.java`) *(published)*, checked on the
orders the user gave:

| Offset | Type | Field |
|---|---|---|
| 0x00 | `u32` | its number |
| 0x04 | `i32` | the turn it was given: 0 to 4 in the save five turns on |
| 0x08 | `i16` | the race giving it, by `race.rst`'s number |
| 0x10 | `i32` | its type: 0 economy, 1 research, 2 military, 5 diplomacy, 7 labor (the editor names 1 "economic result"; here it holds the research shares) |
| 0x1C | `i32` | how long its own part is: 52, 12, 52, 1,164 and 24 bytes |

Each empire's home system has an economy order from the start, building
trade goods. Its own part, from its start:

- **Economy:** whether to build (0) or buy (1) (`i32`), the system's
  index in `systInfo` (`i16` at 4: Sol's 27), the building (`i16` at 8,
  0x7FFF for none: 154 with the databank queued), what's built (`i32` at
  16, as `systInfo`'s 0x80: 5, 1, 3) and the ship (`i16` at 20, -1 for
  none: 30 with the destroyer queued). Buying the databank added a second
  economy order for Sol with 1 in its first word.
- **Research:** the six shares, `i16` each: 20, 17, 15, 15, 16, 17, as
  the research screen after the change.
- **Military:** the destination's row and column (`i32` at 4 and 8: 8,
  9, a sector west of Sol), where the task force started (12 and 16), its
  mission (20: 0, a move) and the task force's number (`i16` at 28: 7).
- **Labor:** the system's index (`i32`), then its workers in industry,
  energy, food, research and intelligence (`i32` each): 140, 40, 100, 20,
  0 with one replicator's workers taken away. Loaded, the save shows
  industry 218 and one worker spare, as when it was saved (the user, in
  the game): `systInfo`'s workers are the turn's start, the order the
  change. The first try at it saved 150, 40, 90, 30, 0, food to research,
  though its screenshot doesn't show that change.

## In Reluva

`readSaveSummary` (`src/engines/botf/formats/save_game.cpp`) opens a save
with `AltResArchive`, takes its name from the header, and decodes
`gameInfo` with the window full of spaces for the description, turn and
player's empire; on `game0.sav` it gives "game0.sav", "Tutorial #1", turn
0, the Romulans.
`decompressLzss` takes the window's fill. The unit tests
(`botf.unit.archives`: "lzss" and "saved games") cover both.

`readSavedOrders` reads the turn's orders, and decodes the economy,
research, military and labor orders; on the user's saves it gives each
order as the screens showed it given. The unit tests ("saved orders")
cover it.

`readSavedShips` reads the ships, and `readSavedTaskForces` lists each
task force's ships; on all 15 saves every ship is in the task force it
names, and that task force lists it. The unit tests ("saved ships") cover
it.

`readSavedEmpires` reads the five empires' figures; on the user's saves
they're the empire panel's. `readSavedTaskForces` reads the task forces
in use, and `readSavedResearch` each empire's research shares; on the user's saves
they give the Federation's ship ordered west and the shares as the
research screen shows them. The unit tests ("saved empires, fleets and
research") cover the three.

`readSavedGalaxy` reads the galaxy's four files, and refuses a save whose
files don't agree: counts that differ from `galInfo`'s, a sector out of
order or pointing past a list, a system cut short. On the four tutorials
it gives each one's 45 systems and 12 objects on a 16 by 12 grid, the
five empires' home systems where the tutorials put them. The unit tests
(`botf.unit.archives`: "saved galaxy") cover it on a galaxy built to this
layout.

## Sources and checks

- **Where the facts come from:** the Ultimate Editor's source
  (`ue/util/file/LzssFile.java` for the archive and its header's name,
  `ue/util/file/LzssInputStream.java` for the window,
  `ue/edit/sav/tools/SavTools.java` for which files are text,
  `ue/edit/sav/files/GameInfo.java` for `gameInfo`'s fields, and the files
  under `ue/edit/sav/files/` for what each file holds and the galaxy's
  layouts; GPL-2.0, read for the layout only; see
  [tables.md](tables.md#other-sources)), November 2026. The slots' file
  names are from a listing of the user's install.
- **How it was checked:** on the user's `game0.sav` (Tutorial #1, from a
  fresh install) and `auto.sav` (539 files, at turn 1), November 2026: every file decoded with each window fill,
  and `readSaveSummary` run on it, its description matching the Load Game
  screenshot's. The galaxy's files on the four tutorials, `game0.sav` to
  `game3.sav`: each file read whole to its last byte, the counts agreeing,
  the systems' and objects' sectors agreeing with their map positions and
  their names' column letters and rows. The fields since, on seven saves
  of one new game the user made a change apart, with screenshots of the
  system, research and galaxy screens for each, and four more (a
  worker moved, the databank bought, a destroyer queued, five turns
  passed), November 2026 (the pairs are listed above). The unit tests use files built to
  this layout, not the game's. The window's fill is from the editor alone.

## Open questions

- Whether the Load Game screen's name field shows the file's name or the
  header's: they're the same in `game0.sav`.
- Which fill each file's window starts with: settled by a save with a
  reference into the unwritten window, or by `result.lst` once its pieces
  are read.
- The timers' other values, from a game set up with them.
- What `Last.sav` is for. `Auto.sav` is the autosave the main menu's
  Continue goes on from; `Last.sav` is a copy of it (its header names it
  `auto.sav`): the game writes both, with the same time, once a game
  started or loaded has passed a turn, but moving `Last.sav` away changes
  nothing that shows (the user, in the game).
- The diplomacy orders' layout (the computer's gave one by the fifth
  turn), and the other kinds of economy and military order.
- What `systInfo`'s value at 0x04 means for the computer's systems, and
  `galInfo`'s first 8 bytes.
- The rest of `gameInfo`, of the galaxy's files, and every other file in
  the archive: settled by saves taken a turn apart, or with one change
  between them.
