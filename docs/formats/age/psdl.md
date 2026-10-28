# .psdl cities

A city's streets, sidewalks, ground and buildings: `city/sf.psdl` and
`city/london.psdl` in `mm2core.ar` ("procedural street description", as
the developers' file names have it). The geometry isn't stored as
triangles: each block of the city is a list of attributes (a road strip,
a building front, a roof) that the triangles are generated from. Read by
`src/engines/age/psdl.cpp`, drawn by `city_mesh.cpp`; `reluva-tool
psdl-info` shows a city's.

## How this was worked out

From the two retail cities. The header gave counts followed by runs of
12-byte points (vertices) and floats (heights), then length-prefixed
names, which were texture names. One more than the stored number of
names was in the count, and the same turned out true of the blocks.

Each block was then read as two counts (perimeter edges, then 16-bit
words) and the walk ended exactly at the next block every time. The
words' first value in a block was always a header whose bits 3-6 took 13
values; trying a length for each kind until every block of both cities
(2,418 that aren't empty) ended exactly where its count says gave the
rules below. Exactly one header in each block has bit 7 set; after it,
in 98 blocks, only texture attributes follow.

Which texture a texture attribute picks was settled by the names: read
as 1-based, road strips get `r2_f`, `r4_f`..., building tops get
`sliver` and roofs get roof textures; read as 0-based they get sidewalk
and window textures. What each attribute draws, and how heights and
texture coordinates work, was settled by drawing it and looking (see the
attributes below). The attributes' names (sliver, facade bound, road
fan...) and some first guesses at their layouts came from what's
remembered of the Midtown Madness modding community's public
descriptions of this format; every rule here was then checked against
the two cities as above, and no code from anywhere was used.

## Layout

Little-endian throughout.

| Field | Contents |
|---|---|
| `PSD0` | |
| u32 | version: 2 |
| u32 + 12 bytes each | vertices: x, y, z floats (Y up) |
| u32 + 4 bytes each | heights: floats, absolute |
| u32 | textures, plus one |
| | each: a length byte, then the name and a zero (some are empty) |
| u32 | blocks, plus one (block 0 isn't stored) |
| u32 | the first road block (402 in San Francisco, 367 in London): the blocks before it have no roads |
| | the blocks |
| u8 each | a byte of flags for each block, block 0 included (see below) |
| u8 each | a second byte for each block: 205 for block 0, 1-20 for 801 road blocks and 9 junctions, 0 for the rest |
| 6 floats | the city's bounds, minimum then maximum |
| 4 floats | its centre and radius |
| | the roads (below), to the end of the file |

A block:

| Field | Contents |
|---|---|
| u32 | perimeter edges |
| u32 | attribute words |
| u16 pairs | each edge: a vertex, and the block on the other side of it (0: none). Of the 23,410 edges that name a block, 99% are named back by that block |
| u16 words | the attributes |

An attribute is a header word, then its words. In the header, bits 0-2
are a subtype, bits 3-6 the type and bit 7 marks the block's last
attribute. For the types marked *counted*, a subtype of 0 means the
count is the next word; otherwise the subtype is the count.

| Type | Name | Counted | Words | What they are |
|---|---|---|---|---|
| 0 | road strip | yes | 4 x count | sections across the road: sidewalk's outer edge, curb, curb, outer edge |
| 1 | sidewalk strip | yes | 2 x count | pairs: curb, outer edge |
| 2 | rectangle strip | yes | 2 x count | pairs across a strip |
| 3 | sliver | | 4 | top height, a texture scale (heights), left and right vertices |
| 4 | crosswalk | | 4 | vertices |
| 5 | road fan | yes | count + 2 | a triangle fan of road (intersections) |
| 6 | fan | yes | count + 2 | a triangle fan (ground, grass, plazas) |
| 7 | facade bound | | 4 | a value (an angle, perhaps), a height, left and right vertices |
| 8 | divided road | yes | 6 x count + 2 | two words not known, then sections: outer edge, curb, divider, divider, curb, outer edge |
| 9 | tunnel | yes | count | walls, railings and ceilings: flags, two heights, and for a junction an outline's edges (below) |
| 10 | texture | | 1 | the texture is subtype x 256 + the word, 1-based |
| 11 | facade | | 6 | bottom and top heights, repeats across and up, left and right vertices |
| 12 | roof | yes | count + 2 | a height, then a triangle fan of count + 1 vertices |

| Type | San Francisco | London |
|---|---|---|
| road strip | 457 | 434 |
| sidewalk strip | 795 | 979 |
| rectangle strip | 29 | 242 |
| sliver | 6,824 | 5,205 |
| crosswalk | 648 | 697 |
| road fan | 1,539 | 1,793 |
| fan | 1,849 | 1,592 |
| facade bound | 12,539 | 9,620 |
| divided road | 99 | 29 |
| tunnel | 159 | 127 |
| texture | 27,930 | 33,155 |
| facade | 11,533 | 19,454 |
| roof | 4,820 | 2,953 |

## What the attributes draw

Vertex heights are the ground's; heights in the heights table are
absolute (a roof at 52.3 over ground at 36).

- **Textures come in threes.** A road's texture is followed in the list by
  its sidewalk's and then its crosswalk's or a low-detail road's:
  `r2_f, swalk_f, r2_lo_f`; `rinter_f, swalk_inter_f, rxwalk_f`. Road
  strips and divided roads draw the road with the current texture and
  their sidewalks with the next; sidewalk strips use the next; crosswalks
  the one after. Checked by listing, for every road attribute of both
  cities, the three names from its texture on.
- **Road textures are half a road**, the centre line along v = 0 and the
  curb along v = 1: San Francisco's `r2_f` has its yellow centre line at
  the top and its white edge line at the bottom; London's `r4_nopark_l`
  has its double yellow no-parking lines at the bottom. So each half of a
  road is drawn from its curb (v = 1) to the middle (v = 0), with u along
  the road. A divided road's halves run from each curb to the divider.
- **Sidewalk strips' markers**: 188 strips of two pairs (72 in San
  Francisco, 116 in London) start with a pair of equal values, (0, 0) or
  (1, 1). A sidewalk can't have one vertex for both edges, so these
  aren't vertices; read as vertices they make triangles across the city
  (to the origin and to a hill in San Francisco), which the car on the
  Drive page drove onto. Reluva skips such pairs, so these strips draw
  nothing. What they mean isn't known.
- **Sidewalks** are 0.15 m above the road: the curb vertices are at the
  road's height and the outer edges at the sidewalk's. Reluva draws the
  sidewalk at its outer height and a curb face down to the road.
- **Facades** are walls from the left vertex to the right, from the
  bottom height to the top, the texture repeated across and up as many
  times as the two counts say.
- **Slivers** are walls from the vertices' own heights (the ground) up to
  their height: the strip of shop fronts under a facade, and building
  edges. Reluva repeats the texture along them by length times the scale
  over height.
- **Roofs** are fans at their height over their vertices.
- **Fans and road fans** are drawn at their vertices' heights.
- **Facade bounds** aren't drawn: they follow the facades (same vertices),
  probably for collisions.
- **Tunnels** (below).

Texture coordinates for fans, roofs and road strips' lengths are
Reluva's choice (a 10 m tile; a texture's height per strip width) until
compared with the game.

## Tunnels, walls and railings

The tunnel attribute (286 in the two cities) puts walls along a road and
sometimes a roof over it: London's Underground (`sub_tunnel`), San
Francisco's road tunnels (`s_tun_sw`), the walls along its freeway
(`s_wall_veg`), and the railings of bridges (`s_conc_rail`,
`pl_ggbridge_rail_f`, Tower Bridge's `tl_bridgerail_l`). It's drawn in
the current texture, the one set before it.

Its first word is flags, its next two heights in 256ths of a metre. With
a count of 3 (249 of them) it walls the one strip that follows it in the
block (a road strip, a divided road or a rectangle strip): along the
strip's first and last vertices of each section. With a count of 10 (37,
in junctions) it walls the block's own outline: words 4 and 5 are a mask
of its edges, bit *i* for the edge ending at outline vertex *i*, and the
masked edges are exactly those with no block beyond them in all 37.
Words 3 and 6-9 aren't known.

| Flag | Meaning | How it was settled |
|---|---|---|
| 0x1, 0x2 | a wall on the left, on the right | railings along one side of a bridge have one |
| 0x8 | a flat ceiling | only on tunnel textures (11, mostly London's Underground junctions) |
| 0x10, 0x20 | the left half of a curved ceiling closed at the start, at the end | see below |
| 0x40, 0x80 | the same for the right half | |
| 0x100 | a curved ceiling | only on tunnel textures (95) |
| 0x200-0x1000 | four more flags with the closing ones, "offset" in what's remembered of the names | not used |
| 0x4 | only with `s_wall_veg` and the Golden Gate's railing | not known |
| 0x4000 | on railings and some walls | not known |

The names of these flags (sides, flat and curved ceilings, closed and
offset ends) are what's remembered of the modding community's public
descriptions; each was kept only where the cities bore it out as above.

The heights: walls of the first height (railings 1-2 m, the freeway's
walls 5, the Underground's 5.5). A curved ceiling springs from the lower
of the two heights and reaches the higher at its crown: London's
Underground has 5.5 and 7, and a crown 7 m over the floor is what fits
under the streets (measured from every section of every Underground
tunnel up to the first surface above it, the least room is exactly 7.0
m, where the floor slopes under a surface at -4; a crown at 5.5 + 7 =
12.5 m would come up through the streets); San Francisco's road tunnels
have 9 or 10 then 7, so walls of 7 and a crown of 9 or 10. A flat ceiling
is at the first height. The Underground's tunnels come into its junctions
(flat ceilings at 5.5) with their ends closed (0x0b53 at a start, 0x15a3
at an end, both alternating along London's lines), so a closed end fills
the arch's end above the first height. Reluva draws the arch as a sine
across the strip in eight pieces.

## Block flags

| Flag | Blocks | What they are |
|---|---|---|
| 0x4 | 401 in San Francisco, 366 in London: blocks 1 to the first road block, exactly | no roads: ground, buildings, roofs |
| 0x8 | 556, 647 | roads: 98% of London's are in the road map's roads |
| 0x10 | 214, 328 | the road map's junctions, every one |
| 0x2 | 14, 209 | under a roof: San Francisco's road tunnels, London's Underground |
| 0x20, 0x40, 0x80 | | with walls and railings mostly (every San Francisco block with 0x40 has a tunnel attribute); not settled |

## The roads

After the bounds: a u32 count (379 in San Francisco, 540 in London: as
many as the `.bai` road map's roads, in the same order), then each road:

| Field | Contents |
|---|---|
| u16 | flags, not known (0x440 and 0x441 commonest) |
| u16 | not known |
| u8, u8 | lanes on each side: the `.bai`'s side 1, then side 0 (all 540 in London, 358 of 379 in San Francisco) |
| float each | where each lane is across the road, 0-1 (0.125, 0.375, 0.625, 0.875 for four) |
| u8, u8 | a byte for each end: bits 0 and 7; bit 0 goes mostly with traffic lights there, not settled |
| 8 u16 | vertices, four near each end; how they're used isn't known |
| u8 | blocks |
| u16 each | the road's blocks, as the `.bai` lists them (375 of 379 and 532 of 540 the same; the `.bai` adds a junction's block now and then); 23 of San Francisco's roads list theirs negative (-408 for block 408) |

Read this way both cities' files end exactly with the last road.

## Water

`city/<map>.water` is text: the water's height (-1.9 in San Francisco,
-3.8 in London), then three blocks: empty blocks at that height under the
Golden Gate, Tower Bridge and Waterloo Bridge. Reluva draws the water
over their outlines in the first animated water texture the city's
texture list names (`s_ocean-0007`, `s_thames-0009`; see
[tex.md](tex.md)), and doesn't collide with it.

## Low-detail roads

A road strip's third texture is a low-detail version of its road
(`r2_lo_f`) for 962 of the 1,019 road strips and divided roads. Reluva draws a block's roads in
that texture when the block is more than 150 m from the eye (worked out
again when the eye moves 60 m or into another block). Whether the game
does is still to be seen; divided roads keep their own.

## Not known yet

The second byte of the road blocks; block flags 0x20-0x80; the divided
road's two words; the tunnel attribute's other words and flags; facade
bounds' first value; the roads' flags, ends and vertices. The program's
own handling of these is encrypted (see ../../engines/age.md), so they're
for observation of the game running.
