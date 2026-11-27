# SimCoaster: saved parks (`.TPWS`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/saved_park.cpp` · **Checked on:** the US release's one save, `Data/PreBuilt/prebuilt.TPWS` · **Confidence:** inferred unless marked

## Overview

A park saved by the game is a `.TPWS` file. The game ships one,
`Data/PreBuilt/prebuilt.TPWS` (37,718 bytes), a Land of Invention park
with a network of path and a queue, which is the path a new park there
starts with ([paths.md](../systems/paths.md#where-path-can-go)). After a
short header the park's whole state is one zlib stream, 1,692,641 bytes
inflated: much of it the game's structures written as they lay in
memory, padding and all (runs of 0xCD, a debugging build's fill for
memory never set). Of the state, the park's grid of cells is worked
out, and of each cell a few fields; of the records after it, the
objects', the bank's, the challenges' and the research lab's in part.

## Layout

All numbers little-endian.

### The file

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | 500 |
| 0x04 | 4 | 0x1301 |
| 0x08 | 1 | 0 |
| 0x09 | | "Version: Beta_21", then CR LF and two zeros |
| 0x1D | | A copyright notice in UTF-16, to its two zero bytes, then zeros |
| 0x61D | 8 | Not worked out |
| 0x625 | | The state's chunk |

The state's chunk:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | `BILZ` |
| 0x04 | 4 | The state's size, inflated *(confirmed)* |
| 0x08 | 4 | The chunk's size, from its tag: to the file's end *(confirmed)* |
| 0x0C | 16 | 15, 9, 0, 0: not worked out |
| 0x1C | | A zlib stream (`78 9C`), which inflates to the state's size *(confirmed)* |

### The state

The first 7 KB or so aren't worked out: lists of small records, some
holding park cells (`0F 2F 16` then `14 2F 16`: cell (47, 22)). Then
the grid, then records each with a number and a four-letter tag stored
backwards, most a second tag after it, also backwards
([the records](#the-records)).

### The grid

128 × 128 cells, column by column of the park's cells: cell (x, y) is
the record y × 128 + x, (x, y) as the settings number them
*(confirmed)*: the tiles below are the world's attribute map's, every
one. The grid
has no tag; it ends where the first tagged section (`EATC`, with its
size) begins, and starts at the first byte from which 16,384 whole
records run on to there.

A record is 86 bytes of the cell's own, then −1 (`FF FF`). Thirteen of
them have an entry of 10 bytes before the 86: (1, −1), (1, 0) or
(3, −2) and zeros, heights perhaps *(guessed)*; those cells are kind 7,
the rest kind 3.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Its kind: 3, or 7 with an entry before |
| 1 | 1 | Flags, not worked out (0x01, 0x10, 0x40 in some) |
| 2 | 1 | Bits, 0x40 or 0xC0 outside the park: not worked out |
| 8 | 2 | On path: nine bits, the cell and its eight neighbours, which are path, it seems (0x1FF in a path's middle) *(inferred)* |
| 4 | 1 | On a queue: its cells' order, counting up from the ride (0x36 to 0x3A) |
| 11 | 2 | Whose cell it is: the handle of the object it's part of, as the object's record has it ([the objects](#the-objects)), 0 for none. On a ride's, a feature's and an obstacle's cells, its entrance and exit, and an obstacle's margin *(confirmed)*: thirteen objects' handles, each on its own cells; on 53 cells of path, 1110's (the ride at (17, 17)), and on five the robot's, not worked out |
| 13 | 1 | What's built: 0 nothing, 1 path, 2 a queue; 1 on the entrance's path and on the recording's, 2 on a short run to a ride beside the path *(inferred)* |
| 17 | 1 | The piece it's drawn as. On a queue: 2 straight, 4 a bend, 5 where it meets the path. On path, the game's own tile: 9 a corner, 10 and 20 an edge, 11 and 12 a middle *(inferred)* |
| 21 | 2 | That piece's angle, degrees: 0, 90, 180 or 270. At 0 a path's corner opens east and south, at 90 south and west: an angle turns east (+x) towards south (+y), as every corner of the prebuilt park's does *(inferred)* |
| 25 | 1 | What fills it: 0 bare land, 1 path, 3 a queue, 4 an object, 9 and 10 an attraction's entrance and exit (its shape's `2` and `N`), 5 a margin kept clear about the lake, the river, the park's edges and the attractions behind hoardings, 7 outside the park, as they lie drawn as a picture of the park *(inferred)*; 8 an obstacle's cells, its handle on them (the lake's 211, the fallen trees' 64, the gates' 16), and the bridge's, under a handle no object has *(inferred)*; 30 the river, tiles 0x90 and 0x94, no one's *(inferred)*; 21 three cells of the log mill's beside it, west and east of it, not worked out |
| 46 | 1 | The cell's tile of the world's attribute map *(confirmed)* |
| 51 | 1 | On an object's first cell, a number of its, not worked out (0x21 to 0x25 for the robot, fountain and rocks) |
| 73 | 1 | 1 on the park's own land, it seems *(guessed)* |

The rest vary but aren't worked out: some trace the river, the lake and
the obstacles.

The prebuilt park's cells: 76 path, 5 queue, its tiles Land of
Invention's. Its path runs two cells wide from the entrance's two cells
up to y 42, with branches, as the recording's new park has it as its
briefing closes (2:34), the same shape *(confirmed)*. Its queue leads
from the path at (47, 24) along to (43, 24), turns, and ends at the
entrance of the Giant Robot Swing, a ride the recording shows standing
there at the start.

### The records

After the grid, each record starts with a number (4 bytes), then a
tag, backwards, and most a second tag, also backwards; their sizes
aren't given, a record running on to the next one's start. The
number isn't a size: `PARK`'s is 13 and it runs 76,621 bytes
*(confirmed)*. In order:

| Number | Tags | What |
|---|---|---|
| 37 | `EATC` | Not worked out |
| 3 | `LDMT` `EJBO` ("TMDL", "OBJE") | An object, 24 of them ([the objects](#the-objects)) |
| 21, 22 | `TMDL` with `OBJH`, `CUTS` | Not worked out |
| 19 | `TMDL` `CHHQ` | The challenges ([below](#the-challenges)) |
| 17 | `TMDL` `UIMS` | Not worked out |
| 16 | `TMDL` `BANK` | The bank ([below](#the-bank)) |
| 15 | `TMDL` `WEAT` | The weather, it seems |
| 23 | `TMDL` `TRNR` | Not worked out |
| 14 | `TMDL` `RLAB` | The research lab ([below](#the-research-lab)) |
| 13 | `TMDL` `PARK` | The park ([below](#the-park)) |
| 12 | `TMDL` `TAGS` | Not worked out |
| 11 | `TMDL` `ADVI` | The advisor, it seems |
| 10, 9 | `TMDL` with `MEHQ`, `STAF` | `STAF` the staff, it seems |
| 0 | `DONE` | The end of the `TMDL` records |
| 7 | `WRLD` `SCPT` | Not worked out |
| 0 | `SPSC` `PTCL` | The particle effects, by name (`BigSmokePuff`, `Firework1`, `Splash`, `GoldenTicket` and the rest) |
| 0 | `PART`, `MESS`, `KAME`, `ADVS`, `SOUN` | Not worked out |

Between `MESS` and `KAME`, among more not worked out: `SYSR` with the
game's build date, "17:55:58Dec 14 2000"; the bytes `KARTFLY_`, then
1, 400 and 400 bytes of zeros, a go-kart's or water ride's track, it
seems, none laid *(guessed)*; the world's kinds of object with their
names and folders (`Giant Waterwheel`,
`data\levels\science\Rides\wateride\`); and each ride's script by
name (`EventMap.rse`). Then `COAS` `ADV_` (34) and the sound banks'
folders (`\data\global\sound\cat_rides`). None for visitors: the
prebuilt park has none.

The prebuilt park has no track laid: the grid has none, the
waterwheel's cells only its own and its margin's, and its record's
slots are empty *(inferred)*.

### The records' heads

Every `TMDL` record but an object's goes on, after its tags, as an
object's does: 0x80 and 0x80 where an object has its place (0.5, 0.5,
"kept aside"), then two numbers a step apart either side of one between
(`OBJH` 12 and 14, `CUTS` 11 and 13, `UIMS` 9 and 11, down to `MEHQ` 1
and 3), the record's neighbours in a list, it seems *(guessed)*.

### The bank

`BANK`, after its head:

| Offset | Size | Field |
|---|---|---|
| 0x14 | 4 | 25, not worked out |
| 0x18 | 4 | 47,375 |
| 0x20 | 4 | 1 |
| 0x24 | 4 | 47,375 again |
| 0x2C | 4 | −2,625: 47,375 less 50,000, the settings' money |
| 0x30 | 32 × 8 | The loans on offer, below |
| 0x130 | 4 | 7, not worked out |

A loan, 32 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | 0, but 1 on the fourth (§ 10,000): taken, perhaps *(guessed)* |
| 4 | 4 | Its amount |
| 8 | 4 | Its APR, percent |
| 12 | 4 | Its months |
| 16 | 4 | Its monthly repayment |
| 20 | 8 | Zeros |
| 28 | 4 | Its lender, in `LOANNAMES.str` |

The eight are the settings' `LoanInfo[0]` to `[7]` in order, their
amounts, rates and months the general settings', their lenders Land of
Invention's own (0, 3, 2, 10, 6, 5, 11, 7) *(confirmed)*. Each
repayment is its amount × (1 + APR)^(months ÷ 24) ÷ months, rounded
down: § 100,000 at 20% over 36 months § 3,651, § 18,000 at 23% over 24
§ 922 *(confirmed)*: all eight. A new park's money is the settings'
50,000 whatever the save's, as the recording's is.

### The challenges

`CHHQ`, after its head, holds the world's ten challenges in its
settings' order (`ChallengesInThisLevel`, Land of Invention's 2, 4, 5,
14 and the rest), 125 bytes each, packed without padding; each its
settings' in `Data/Challenges.sam` *(confirmed)*, all ten, every field
below:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `Type` |
| 4 | 4 | `TargetTime` |
| 8 | 4 | `TargetVal` |
| 12 | 4 | `TargetVal2` |
| 16 | 4 | `TargetObj` |
| 24 | 4 | `CashPrize` |
| 32 | 1 | `CheckAtEndOnly` |
| 43 | 4 | `DaysDelay` |
| 51 | 4 | `MaxDaysTilTrigger` |
| 55 | 4 | `GracePeriodLimit` |
| 88 | 4 | `TriggerData` |
| 92 | 4 | `GTPrize` |
| 96 | 4 | `SharePrize` |
| 100 | 4 | `SharePriceEffectWon` |
| 104 | 4 | `SharePriceEffectLost` |
| 108 | 4 | `Sender` |
| 112 | 4 | Its number (`ScriptIndex`) |
| 116 | 4 | `MissionNeededToActivate` |
| 120 | 4 | `PromoGiven` |

The rest are zeros in all ten; none is offered yet.

### The research lab

`RLAB`, after its head, lists the world's research projects, 50 of
them, 32 bytes each; then 6,000 bytes or so unset, and a few numbers,
85 twice among them, the settings' `Research.StartingWorkLoad`, it
seems *(guessed)*. A project:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | The attraction's number |
| 2 | 1 | Its kind: 0 a ride, 1 a shop, 2 a sideshow, 3 a feature, 4 an upgrade or add-on, 6 a facility (the Info Kiosk, the Security Camera); the rides on sale have 4 *(inferred)* |
| 6 | 1 | Its `Research.Group` *(confirmed)* |
| 7 | 1 | On a ride on sale: 1 where it has an `Upgrades[1]`, else 2, it seems *(guessed)* |
| 8 | 4 | Its research cost, a float: its `Upgrades[0].CostOfResearch`, 300 for a ride without (the Bungee's), and for a ride on sale 100, its `Upgrades[1].CostOfResearch` *(confirmed)* |
| 12 | 4 | 1 or 2, not worked out |
| 20 | 4 | Its `Research.Type` (unset on a ride on sale) *(confirmed)* |
| 24 | 4 | Its consultant's kind, `RStaff[0].SType` *(confirmed)* |

The 50 are Land of Invention's rides, features, shops, sideshows,
facilities and add-ons, the rides first; the special rides among those
on sale.

### The park

`PARK`, after its head: 4, 32, 32, 128, 128, then a grid of 32 × 32
squares of four cells, 61 bytes each: the square's first cell's x and
y, its size (4, 4), then two floats and seven numbers, each after a
zero byte, every one −999 in the prebuilt park, unset, it seems
(statistics of the visitors there, perhaps) *(guessed)*. Then empty
lists, and an 8-byte number most of the records have, 0x0001BF53EB256D40
(a time, perhaps).

### The objects

Each object the park has is a record of the section tagged `LDMT` then
`EJBO` ("TMDL", "OBJE"), after the grid. From the `LDMT`:

| Offset | Size | Field |
|---|---|---|
| 8 | 2 | Its first cell's x, 8.8 fixed point: the cell and a half |
| 10 | 2 | Its first cell's y, likewise |
| 16 | 4 | Its angle, degrees |
| 20 | 2 | Its number: its settings' `Info.Id` (`rides/robot/robot.sam`'s 1101), as `Data/Objects.txt` allots them (1100s rides, 1400s features, 1700s obstacles), each an object of the world's *(confirmed)* |
| 22 | 2 | 2000 in all |
| 0xCC | 2 | Its handle, which its cells have ([the grid](#the-grid)) *(confirmed)* |
| 0xCE | 2 | The same, but for a ride's: another, near it (the robot's 0xD2C after 0xD2B) |
| 0xEC | 1 | A ride's capacity: its `Upgrades[0].InitCapacity` (the robot's 8, the waterwheel's 3) *(confirmed)*: the five rides |
| 0xED | 1 | A ride's duration: its `Upgrades[0].InitDuration` where it has one (40), else 3 *(confirmed)*: the five rides |
| 0xEE | 1 | 60 on every ride, not worked out |
| 0x100 | 4 | A ride's price: its `UsageInfo.InitPricePerUse` (the robot's 10, the tour ride's 35) *(confirmed)*: the five rides |
| 0x118 | 4 | Its working life, a float: its `Info.WorkingLife`, else 100 (the robot's 75, the log mill's 95) *(confirmed)* |

Then its name, at 0x3C: UTF-16 characters four bytes apart, with older
text between them where the buffer was written twice ("Giant " and
"Swing" alternating, then "Robot"), so not to be read as it is; and
much more, not worked out. A ride's record is 0x15D7 bytes from its
number, anything else's 0x148: a ride's has ten slots of 30 bytes and
six of 144 more, each starting −1 and the rest unset (0xCD) in the
prebuilt park's. The prebuilt park has 24: 14 placed,
among them the Giant Robot Swing at (42, 26), the Electric Fountain at
(49, 23), three rocks, the mission's fence (1700) at (41, 48) and four
more obstacles, and four rides in the park's other areas; ten kept
aside at (0.5, 0.5), the gates (1601) and others the game places
itself, it seems *(inferred)*.

An object's first cell is the corner its model is laid from: its model
covers x and z from 0 to 10 a cell of its shape (the robot's 40 by 50,
its `Info.Shape` four cells by five) *(confirmed)*. The robot's cells
in the grid are its shape turned end over end, its last row, `*2N*`,
at y 26: its first cell is that row's first, its entrance `2` at
(43, 26) by the queue's end.

## In Reluva

`SavedPark` (`src/engines/tpw/formats/saved_park.cpp`) reads a save's
version, inflates its state and reads the grid (each cell's kind,
flags, tile, what's built on it and fills it, its piece and angle) and
the objects. `Park` lays the prebuilt park's path, queue and objects
over a new park of the world it matches, an obstacle's cells taken as
an object's are
([paths.md](../systems/paths.md)), and `ParkScreen` draws them.

Reluva's own saved games aren't `.TPWS`: this format can't be written
while most of it is unknown ([reluva-saves.md](reluva-saves.md)).

`reluva-tool tpw tpws-check <game folder> [save] [-objects]` reads the
prebuilt park, or another save, and says what's in its grid, how many
objects it has (and, with `-objects`, lists them) and which world's it
is; the game-files test runs it (`tpw.gamedata.saves`). The unit tests
(`tpw.unit.formats`) read a save built in the test, and fail a cut one.

## Sources and checks

- **Where the facts come from:** the one save, `prebuilt.TPWS` of the US
  release (November 2026): its chunk inflated; the grid found by the
  period of its records; its fields by drawing each byte of every cell as
  a picture of the park, and setting them beside the world's attribute
    map and the recording; the objects by their numbers, against the
  world's objects' settings and models' sizes; the bank's, challenges'
  and research projects' numbers set beside the general settings',
  `Challenges.sam`'s and each attraction's, field by field.
- **How it was checked:** `tpws-check`: 16,384 cells read to the first
  section, every tile the attribute map's; the path, queue and objects
  laid in a new park drawn beside the recording at 2:35: the robot left
  of the path beyond its cross-piece, the fountain right, the rocks where
  the recording's are.

## Open questions

- Everything else in the state: the records before the grid, the
  records' fields (the bank's other numbers, whether its fourth loan's
  taken, the park's, the staff's), the objects' other fields, the
  cells' other fields and their entries; the rides' slots; what
  `KARTFLY_` holds with a track laid.
- A save the game wrote of a park played, with visitors and money
  spent, to set the records against.
- Whether every new park of Land of Invention starts from it, and what
  the other worlds start from.
- Saving a park.
