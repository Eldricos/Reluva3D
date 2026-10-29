# Road maps (`.bai`)

`city/sf.bai` and `city/london.bai` in `mm2core.ar`: each city's roads as
the computer-driven cars see them, with their lanes, sidewalks and tram
lines, and the junctions joining them. Read by `src/engines/age/bai.cpp`.

Worked out from the two files alone, with a few lines of throwaway
Python: guessing at a field's size, parsing every road with that guess,
and checking that the next road's number came out one more than the last
and that the coordinates landed in the city. Both files then parse to
their last byte but a table at the end that isn't needed (379 roads and
214 junctions in San Francisco, 540 and 328 in London). Much of the file
is structures written straight from memory: unused room is filled with
`cd` and `fd` bytes, which is what a debug build's allocator leaves.

All numbers are little-endian; floats are 32-bit; points are three floats
(x, y, z) in the city's coordinates, as the PSDL's.

## Layout

```
"CAI1"
u16 junctions
u16 roads
road[roads]
junction[junctions]
...                    a table of each PSDL block's roads (not read)
```

### A road

```
u16 id                 0, 1, 2... in order
u16 n                  points along it
u16 flags              see "Road flags and kinds" below
u16 blocks
u16 block[blocks]      the PSDL blocks it runs through
f32 halfWidth          from its middle line to the sidewalks' outer edge
f32 speedLimit         15 throughout (as the aimaps' default, m/s)
side[2]                see below: side 0, then side 1
f32 distance[n]        along its middle line
point middle[n]        its middle line
point axes[4][n]       at each point: across (to side 0), up, back, ahead
end[2]                 see below
```

A side:

```
u16 lanes
u16 trams              tram lines (San Francisco's cable cars)
u16 trains             train lines (London)
u16                    1 throughout
u16 kind               its sidewalk: see below
f32 distance[lanes + 1][n]   along each lane and the sidewalk
f32 laneOffset[lanes]        each lane's distance from the middle line
f32 widths[11]         the sidewalk middle's distance from the middle
                       line; minus the half width; each lane's pair
                       (below); the half width; the rest unset (cd)
point lane[lanes][n]   each lane's middle line
point sidewalk[n]      the sidewalk's middle
point track[trams + trains][n]
point curb[n]          the sidewalk's edge by the road
point outerEdge[n]     its other edge
```

The order of the rows was found by their distances from the middle line:
for a side with one lane and a tram line they come out 7.5, 12.5, 2.5,
10 and 15 m, so the tram rows come between the sidewalk's middle and its
edges.

An end, 38 bytes:

```
u32 junction
u16                    cd cd
u16 control            1 traffic lights, 0 give way, 3 none (as Reluva reads it)
u16                    0
u32 slot               the road's place in the junction's list
point a, b             where traffic arriving there stops: see below
```

`end[0]` is the junction at the road's last point and `end[1]` the one
at its first: true for all but 5 of the 919 roads, measured by distance.
`slot` checks out against the junction's list for 1,834 of the 1,838
ends.

### A junction

```
u16 id
u16 block              its PSDL block
point center
u16 roads
u32 road[roads]        in order round it (3 or 4, a few 2 or 5)
```

## Sides and which way the lanes run

Side 0 is on the left going from a road's first point to its last, side
1 on the right: worked out from the axes (`across` points to side 0's
sidewalk). A one-way road has all its lanes on side 1 and none on side 0
(68 roads in San Francisco have four lanes on side 1 and none on side 0).

The lanes are stored the way traffic keeping right drives them: side 1's
rows from the first point to the last, but side 0's lanes (only its
lanes, not its sidewalk) from the last point to the first. Measured
across all 2,616 lanes: every side 0 lane starts at the road's far end,
and every two-way road's lanes lie on the right of the way they're
stored in, in both cities (a one-way road's spread across its middle). London's aimap says its traffic keeps left
([aimap.md](aimap.md)), so there the cars drive every lane the other way.

## The ends' control and points

An end has its two points exactly when lanes arrive at it (by the
right-hand reading above): 1,478 of the 1,838 ends, every one of them;
the other 360 have zeros and control 3. Where there are points, `a`
stands on the curb on the right of the way in, level with the road's
last (or first) point and as far out as the road's half width, and `b`
is a metre from it straight across the road. No traffic lights or stop
signs are placed in the cities' instance files, so the game puts them up
itself, and these are where (see
[age-traffic.md](../../engines/age-traffic.md)).

The control, by how it falls:

| Control | Ends with points (San Francisco, London) | Where |
|---|---|---|
| 1 | 533, 519 | most junctions of three or more roads |
| 0 | 36, 45 | only at junctions of three roads |
| 3 | 81, 264 | the rest; every end without points |

Reluva reads 1 as traffic lights (they're common, on crossroads in both
cities), 0 as giving way (minor roads into T-junctions) and 3 as no
control (the main road through a T-junction, ends nothing arrives at).
That reading is a guess from where they fall.

## Road flags and kinds

Worked out by comparing every road of both cities (919) with what's
known of it (its lanes, tracks, sidewalks, the blocks it's in, the PSDL's
own flags for it) and by drawing the roads that have each value on a map
of the city.

The sides' kinds are their sidewalks:

| Kind | Sides | What they are |
|---|---|---|
| 0 | 1,205 | an ordinary sidewalk (3-5 m) |
| 1 | 353 | the lane-less side of a one-way road (257), and both sides of 48 roads in central London with wide pavements (7.6 m) |
| 2 | 12 | a narrow walkway (1.5 m): the Golden Gate Bridge and San Francisco's freeway |
| 3 | 261 | none: 114 of the 128 roads with kind 3 on both sides have no sidewalk at all; London's Underground (77 of them under a roof), San Francisco's ramps and alleys |

Reluva's pedestrians don't walk kind 3 sides.

The flags:

| Flag | Roads | Where |
|---|---|---|
| 0x1 | 10 | San Francisco's freeway (the PSDL road flags 0x8 on the same roads) |
| 0x4 | 4 | four of those, its loop |
| 0x2 | 99 | London's Underground and some of San Francisco's ramps and alleys: all have no sidewalk (kind 3); the PSDL road flags 0x10 on exactly the same roads |
| 0x8 | 536 | 469 of London's 540 roads, 67 of San Francisco's 379: nothing found goes with it (not lanes, lengths, junction controls, furniture rules, tunnels) |

Reluva reads 0x2 as a road not for traffic: its traffic doesn't start on
them nor turn into them while there's another way (the game's aimaps
give no exceptions to say otherwise, so without it Reluva's cars drove
into the Underground).

The widths: for a side with lanes, after the sidewalk's middle and the
negative half width, a pair of equal values for each lane: its edge on
the right as it's stored, as a distance from the middle line (5, 10 for
two lanes 2.5 and 7.5 out; a one-way road's lanes across the middle are
irregular), then the half width. A side with no lanes has the second
value unset and the half width third.

## Not known yet

Flag 0x8, and the table after the junctions (a count, then for each PSDL
block a count and its roads, it seems).
