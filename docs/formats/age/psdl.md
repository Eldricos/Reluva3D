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
| u32 | not known (402 in San Francisco, 367 in London) |
| | the blocks |
| u8 each | a byte of flags for each block, block 0 included |
| u8 each | a second byte for each block: 205 for block 0, 0 for the rest |
| 6 floats | the city's bounds, minimum then maximum |
| 4 floats | its centre and radius |
| | not worked out yet (18 KB in San Francisco, 23 KB in London) |

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
| 9 | tunnel | yes | count | not worked out |
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
- **Tunnels** aren't drawn yet.

Texture coordinates for fans, roofs and road strips' lengths are
Reluva's choice (a 10 m tile; a texture's height per strip width) until
compared with the game.

## Not known yet

The second header count; the block flags and second bytes; the data
after the bounds; the divided road's two words; tunnels; facade bounds'
first value; whether the game draws the low-detail textures (the third of
a road's three) far away. The program's own handling of these is
encrypted (see ../../engines/age.md), so they're for observation of the
game running.
