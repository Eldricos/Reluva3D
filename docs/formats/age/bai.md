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
u16 flags              0, 1, 2, 5, 8, 9, 10: not known
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
u16 kind               0-3: not known
f32 distance[lanes + 1][n]   along each lane and the sidewalk
f32 laneOffset[lanes]        each lane's distance from the middle line
44 bytes               widths and offsets not used yet, then room for
                       eight floats (cd where unused)
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
u16 control            0, 1 or 3: not known (stop signs, lights?)
u16                    0
u32 slot               the road's place in the junction's list
point a, b             two points near the end; zero in most second ends
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

## Not known yet

The flags, the sides' kind, the ends' control and two points, the
widths after the lane offsets, and the table after the junctions (a
count, then for each PSDL block a count and its roads, it seems).
