# Star Trek: Starfleet Command II: campaign maps (`.mvm`)

> **Status:** decoded · **Code:** `src/engines/sfc/formats/settings.cpp` (`MetaverseMap`) · **Checked on:** English retail 1.0 (4 maps) and 2.0.3.6 (9) · **Confidence:** confirmed unless marked

## Overview

The Dynaverse's hex maps, in `MetaAssets/`: `EarlyMap.mvm`,
`MiddleMap.mvm` and `LateMap.mvm`, one an era, as a campaign's `.mct`
names them ([settings.md](settings.md)), and `conquest.mvm`. Each hex
has its empire, terrain, planet, base, and three numbers (economic,
impedence, strength).

## Layout

A text header in the settings syntax ([settings.md](settings.md)), then
the data from the offset it gives, little-endian:

- **The header** says what the file is (`[Version]`
  `FileType="StarfleetCommand.Map.Metaverse"`, `CellType="hex"`), where
  its data starts (`[Version/Data/Offsets] Data=1804`) and its end
  (`Text=13576`, the file's size), each type's size (4 bytes: string,
  int, float, index), and declares the classes: lists of names
  (`[Classes/Regions]`: Neutral, Federation, Klingon, Romulan, Gorn,
  Lyran, Hydran, Mirak, ISC; `Terrain`: black holes, asteroids, nebulae,
  a shipping lane; `Planets`: homeworlds, core worlds, colonies, asteroid
  bases; `Bases`: Starbase, Battle Station, Base Station, Weapons
  Platform, Listening Post), the `Cell` class (`Economic` int,
  `Impedence` float, `Strength` int, then `Region`, `Terrain`, `Planet`
  and `Base`, indexes in those lists, each with its range), and the
  `Map` (`Width`, `Height`, `Cells`, an array of Cell), and declares
  `Galaxy` a Map. Zero bytes pad it to the data.
- **The data,** the Galaxy: its width and height (32 bits each), the
  array's length (their product), then each cell's seven fields, 28
  bytes, row by row, the file ending with the last.

| Map | Size | Bases | Planets |
|---|---|---|---|
| Early | 28 × 15 | 8 starbases, 51 base stations | 11 homeworlds, 42 core worlds |
| Middle | 28 × 15 | 8 starbases, 24 battle stations, 37 base stations | 11 homeworlds, 41 core worlds |
| Late | 28 × 15 | 16 starbases, 63 battle stations | 11 homeworlds, 41 core worlds |
| Conquest | 18 × 10 | 8 starbases, 45 base stations | 8 homeworlds |

### What the values do

A campaign copies each hex into its database (`MetaAssets/db/Sfc2Spd.sds`,
a `tMetaMapHex` record each, its fields named in the program: `X`, `Y`,
`PoliticalControl`, `TerrainType`, `PlanetType`, `StarbaseType`, then a
base and a current value of each of `EconomicPoints`, `VictoryPoints`
and `SpeedPoints`), and the three numbers are those:

- **Economic** is the hex's economic points. An empire's economy is the
  sum over the hexes it controls: the Middle map's sums (Federation 945,
  Gorn 770, Klingon 720, ISC 625, Mirak 560, Romulan 535, Lyran 535,
  Hydran 415) are a new campaign's database's, and the news reports
  them as "ranks at 1 with 940 total economic points", moving as hexes
  change hands. The game's help: "The stronger an economy that an empire
  has, the more ships it can produce", and `MetaMap.gf`'s
  `TargetPopulationToEconomicRatio` sets the AI's ships at 0.02 of it.
  Most hexes have 5 or 10; homeworlds 80 to 100.
- **Strength** is the hex's victory points: its defence, the map's blue
  shield ("When this value drops, that area becomes disputed. If it
  continues to lose strength, it will eventually fall under the invading
  empire's control"). The current value is what battles wear down: in a
  campaign's saves after a mission, 28 and 47 hexes had dropped by 3 or
  5 from their base, their economy and speed unchanged *(inferred: by
  battles, which the news reports in those sectors)*. Bases add to it
  (`Score.gf`: a starbase 20 to its hex and 7 to those beside it, a
  battle station 14 and 4); without a base a hex's strength is its
  economic value in 353 of the Early map's 361.
- **Impedence** is the hex's speed points (a float, 1.0 for open space):
  the map's yellow arrow, "the ease with which a ship may pass through a
  sector". It's highest where the terrain slows ships, black holes 2.0,
  nebulae 1.7, asteroids 1.3, and lowest in some open hexes, 0.1 and
  0.2. How a move's time follows from it isn't traced: `MetaMap.gf`
  gives a move 750 ms (`MovementDelayInMilliseconds`).

`HexValues.gf` divides each into ten levels (economy and victory 10 to
100, speed 0.2 to 2.0), by the comments in it "the 10 levels" of each;
how bright the map draws a symbol, perhaps *(guessed)*.

## In Reluva

- **Reading it:** `MetaverseMap` (`src/engines/sfc/formats/settings.h`):
  the size, the cells, and the header's names for each index.
- **Commands:** `reluva-tool sfc maps <game folder>` reads every `.mvm`
  and counts its bases and planets by kind.
- **The data viewer:** the Maps page draws each map's hexes, coloured by
  empire, terrain, planet, base or economic value, with the names the
  header gives them, and a hex's fields under the mouse. The hexes stand
  in columns, every other one half a hex lower, as the game's map shows
  them; which columns are the lower ones is guessed.

## Sources and checks

- **What the values do:** the field names in `StarFleet2.exe`'s database
  procedures (`BaseEconomicPoints`, `BaseVictoryPoints`,
  `BaseSpeedPoints`...), the hexes of new and played campaigns' databases
  compared with the maps' cells, the settings files' comments and the
  game's help text, and the economic totals in its news.
- **Where the facts come from:** the header declares the layout itself;
  the data matched it: 12 bytes and 420 cells of 28 bytes make the Early
  map's 11,772, ending at the file's end, and the conquest map's 180 its
  5,052.
- **How it was checked:** all four of 1.0's maps and 2.0.3.6's nine
  read, each ending exactly at its last cell; every index within its
  list.
- **Unit test:** `sfc.unit.formats` (map): a 2 × 1 map built in the
  test, its header's names, a map cut short.

## Open questions

- How impedence sets a move's time, and what the ten levels of
  `HexValues.gf` are used for.
- Which of the bases has a shipyard, and where the player's ship can
  buy one ([hosting.md](../systems/hosting.md#its-missions)).
