# Path sets (`.pathset`)

Binary lists of paths, each a model name and its points. A city's
moving things use them: `race/<city>/<city>_ferry.pathset`,
`_sailboat` (boats and ducks), `_bridge` (drawbridges and a gate),
`_parkedcar` and `_train`. Some races and Crash Course lessons have
their own (`<city>_bridge_race0.pathset`, `london_ferry_crash9.pathset`,
...), and the cities' and races' props, decals and checkpoint layouts
use the same form (`city/<city>/props.pathset`, `race/<city>/circuit0.pathset`,
...). Read by `src/engines/age/pathset.cpp`. Worked out from the files:
86 of them in the archives, plus 15 in backup folders (`city/sf/bak/`).

## Layout

Little-endian throughout.

| Bytes | Contents |
|---|---|
| 4 | `PTH1` |
| 4 | number of paths |
| 4 | not known (0-125; 0 in 25 files) |

Then each path:

| Bytes | Contents |
|---|---|
| 32 | name, zero-padded |
| 4 | number of points, *n* |
| 4 | "start": below *n* in 2,592 of 2,697 paths, mostly 0 |
| 16 × *n* | each point: a 32-bit word, then x, y, z (floats, the city's coordinates) |
| 4 | end word |

The name is a model (`geometry/<name>.pkg`), sometimes after a prefix:
`open:` (6 paths, and `OPEN:` once) or `inactive:` (5). Other paths are
named only `PATH<n>` (not always different): four in
`sf_sailboat.pathset`, and every parked-car line and train path in the
cities' own files.

The end word's low byte is 1 or 2 (and 0 in props files); its next byte
is 8-240. In the moving things' files, 2 goes with loops and 1 with
paths that are two points or run out and back, so Reluva reads 1 as
"back and forth" and 2 as "round". The second byte is 8 for the ducks,
20 for the ferries, tugs and water taxis and 80-120 for the sailboards,
so Reluva reads it as a speed in tenths of a metre a second (0.8, 2 and
8-12 m/s). That is a guess; the props and decals files, which don't
move, have 20 too, and so do the trains, for which 2 m/s is far too
slow. In London's parked-car lines it varies (20-40, once 240) where
San Francisco's are nearly all 20; Reluva reads it there as the gap
between cars, in tenths of a metre (2-4 m, and 24 m on a 26 m line
that then has room for one car).

What the points' first words are isn't known: values like PSDL block
numbers, some with high bits set. The start word looks like the point a
path starts at (the San Francisco ferry's is 29 of its 30 points, and
two of London's are 2 and 3); Reluva starts boats there. The header's
third word isn't used.

Three files stop short: London's `blitz10.pathset` (in `giz_pcar01_l`'s
points), `blitz11.pathset` (whose second path also has a nonsense end
word) and `london_bridge_blitz10.pathset`. Reluva keeps the whole paths
before the break.

## What Reluva does with them

The ferry, sailboat, bridge, parked-car and train files, for the Drive
and Race pages
([age-traffic.md](../../engines/age-traffic.md)):

- **Boats**: the model goes round its path (closed back to the first
  point) or back and forth along it, at the speed above (at least 0.3
  m/s), bobbing a little. The `PATH<n>` paths get the city's sailboat
  (`giz_sailboat01_f`, `_l` in London), which no path names.
- **Bridges** (names with `bridge`, `waterloo` or `gate`): each stretch
  between points is a leaf, as long as it is (London's Tower Bridge path
  is 60 m with a point in the middle; `giz_bridge01_l` is 30 m long);
  the leaves are hinged at the path's ends. `inactive:` ones stay down
  (San Francisco's Chinatown gate); `open:` ones start raised, which is
  how Reluva reads the prefix.
- **Races**: `<city>_bridge_<race>.pathset` (`circuit0`, `race0`,
  `blitz10`, `crash3`, `crash5`, `multi`; and the same for ferries) is
  read over the city's file: a path for the same model whose first point
  is within 5 m of the city's replaces it, the rest are added.
- **Parked cars** (`<city>_parkedcar.pathset`; 55 lines in San
  Francisco, 841 m in all, and 91 in London, 5,116 m): lines of two or
  three points. London's run along the streets 2-4 m from the
  traffic's lanes, by the curb; San Francisco's lie in its car parks,
  across their marked bays, 11 m and more from any lane. So Reluva parks
  cars nose to tail along a line within 5 m of a lane and parallel to it,
  facing from its first point to its last, and side by side (a metre
  apart) across any other, as the bays are; it leaves one place in six
  empty. A line's own model where it names one (`sp_pcar01_l` in the
  Crash Course files); else in London one of `sp_pcar01_l`-`sp_pcar03_l`
  (single-mesh cars with 9-10 paint jobs, sized by their meshes), in San
  Francisco, which has no such models, one of the traffic's cars up to 3
  tonnes. Which way the game faces them, and how it spaces them, isn't
  known; the start word (0 on all but 13 lines) doesn't follow which way
  the lines run.
- **Trains** (`london_train.pathset`): 8 paths of 6-12 points, 300-860 m
  long, along the Underground's tunnel floors (13 m down, 6.5 m under
  their roofs). They name no model; London's aimap has a commented-out
  `[Subway]` section, `va_ug_l 3`, so Reluva runs three `va_ug_l`
  carriages (15 m each) along each. San Francisco has no train paths:
  its `city/sf/audio_pathsets/trolley.pathset`, a 4.2 km line over the
  hills, is where the street cable is heard
  (`aud/ambient/trolleycable.csv` lists the same points), and the cable
  car model (`va_cablecar_f`) isn't placed by any file Reluva reads.

Not read yet: the props, decals and race layouts, and the audio path
sets.

## Counts

| File | Paths |
|---|---|
| `sf_ferry` | 1 (a car ferry, 30 points) |
| `sf_sailboat` | 16: sailboards, four sailboat loops, 7 ducks |
| `sf_bridge` | 1 (`inactive:giz_chinagate_f`) |
| `london_ferry` | 6 car ferries |
| `london_sailboat` | 16: tugs, water taxis, 9 ducks |
| `london_bridge` | 4: Tower Bridge (`giz_bridge01_l`), two Waterloo spans, `open:giz_bridge02_l` |
| `sf_parkedcar` | 55 lines |
| `london_parkedcar` | 91 lines |
| `london_train` | 8 paths |
