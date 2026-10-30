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
| u8 each | a second byte for each block: 205 for block 0, 1-20 for 801 road blocks and 9 junctions, 0 for the rest: the street furniture rule (below) |
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
| 7 | facade bound | | 4 | its direction, a height, left and right vertices (below) |
| 8 | divided road | yes | 6 x count + 2 | the divider's texture and kind, its height (below), then sections: outer edge, curb, divider, divider, curb, outer edge |
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
  probably for collisions. Their first word is the wall's direction in
  64ths of a turn: floor(atan2(dz, dx) x 64 / 2 pi) + 32, from the left
  vertex to the right (all 64 values are used; exact for 99.7% of London's
  9,586 and 86% of San Francisco's 12,491, the rest one off).
- **Divided roads' dividers**: the first word's high byte is the
  divider's texture (numbered as the texture attribute's: 150 is
  `s_jersey_rail`, the freeway's concrete barrier; 138 and 144
  `swalk_f`), its low two bits its kind (1 flat, 2 a kerbed island, 3 a
  barrier; bits 6 and 7 vary, not known), and the second word its height
  in 256ths of a metre (0x26, 0.15 m, on the islands; 0x100, 1 m, on the
  barrier, which is 1 m wide; 0x500 on the flat ones). Reluva raises the
  islands and barriers to their height, sides and ends, in their texture;
  as part of the city's mesh they're solid.
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
masked edges have no road beyond them in all 37 (paired with the
neighbour stored at the edge's first vertex, 76 have nothing beyond and
33 border ground blocks, as round the railed junctions).
Words 6-7 and 8-9 are two more edge masks the same way, identical to
each other in all 37, set in only 4 (San Francisco's blocks 962, 964,
966 and London's 1168), each within the wall mask. All six edges they
mark are of no length (a corner point repeated) with a different road
block on either side, as where two roads' mouths meet; every other
walled edge of no length has the same road, or none, on one side. Not
exact (four such corners aren't marked), so perhaps a wall's end post;
not drawn. Word 3 is less than the outline's edge count every time and 0 in
7 of London's 10 Underground junctions; it isn't a walled edge nor the
longest, and isn't known.

| Flag | Meaning | How it was settled |
|---|---|---|
| 0x1, 0x2 | a wall on the left, on the right | railings along one side of a bridge have one |
| 0x8 | a flat ceiling | only on tunnel textures (11, mostly London's Underground junctions) |
| 0x10, 0x20 | the left half of a curved ceiling closed at the start, at the end | see below |
| 0x40, 0x80 | the same for the right half | |
| 0x100 | a curved ceiling | only on tunnel textures (95) |
| 0x200-0x1000 | four more flags with the closing ones, "offset" in what's remembered of the names | not used |
| 0x4 | goes with a second height of exactly 2 m (all 135 with it, none without); 116 of San Francisco's 120 and London's one are in elevated (0x40) blocks, and two have no walls at all, so probably a 2 m fascia under a raised deck | not drawn by Reluva |
| 0x4000 | on all London's railings (`s_conc_rail`, `tl_bridgerail_l`), San Francisco's 15 `s_conc_rail`, and every `s_wall_veg` wall of its road 4 (blocks 423-441); second height 1 or 3; always in 0x80 blocks | not known: not the texture's alpha (`pl_ggbridge_rail_f` is cut out too, without it), the strip, the block's height, nor what lies beyond the wall |

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
| 0x40 | 129, 214 | off the ground's level: the freeway, bridges, the Underground. 120 and 202 of them have another block over or under their middle (more than 3 m away); of the other blocks, 4% and 5% |
| 0x20, 0x80 | 77, 27; 138, 90 | 0x20 on road blocks that curve and slope (below); 0x80 with the freeway, plazas, parks and London's Embankment: not settled (below) |


**0x20** is only on road blocks, never junctions (77 in San Francisco,
27 in London), and they curve and slope: 76 of the 77 and 25 of the 27
do both. In San Francisco all 77 have road quads out of plane by more
than 2 cm, against 49 of the 508 others, so it looks like a mark for a
twisted road surface. It doesn't separate exactly: 65 and 78 curving,
sloping blocks lack it. Ruled out: negatively listed blocks, open edges,
the neighbours' 0x40, slope or curve alone. The best single measure, a
strip quad out of plane by more than 2.44 cm, is wrong for 10 of San
Francisco's 556 road blocks and fails in London (straight two-section
Underground ramps flagged, others like them not). It isn't a road's: 12
and 15 roads have blocks with and without it. Roads never bank (each
section's two edges are level across), so the twist is only the inner
and outer edges sloping differently. San Francisco's older `sfai.psdl`
has no 0x20 at all, so it was worked out by a later tool, perhaps from
something not in the file.

**0x80** marks a block with more to collide with than its floor, it
seems:
- On road blocks it's having walls or railings: a road block has 0x80
  exactly when it has a tunnel attribute, but for 4 in San Francisco
  (561, 647, 651, 652) and 1 in London (857) of 1,039 (127 and 23 with
  both, 412 and 472 with neither). That's San Francisco's elevated
  freeway (112 of its 149): every raised road block that isn't a tunnel
  has `s_wall_veg` walls.
- The others (15 and 22 blocks, all 0x84) are ground under ramp, tunnel
  mouth and bridge models with a `.ter` (10 and 5); the Underground's and
  Broadway tunnel's entrances (San Francisco's 131, 133, London's 10),
  whose models carry the instance flag 0x2000, only ever found in 0x80
  blocks (15 instances); and London's 32 Underground junctions with
  station models (`subway_a`, `_c`, `_f`) and no 10-word tunnel.
- Not exact: some junctions with walls lack it (San Francisco 971, 973,
  1092), some without have it (London 1021-1030, `r1_stone256`), and
  empty Embankment ground has it (San Francisco 122, 126, 312; London
  245-298, seven blocks). A model's `.ter` alone isn't it: 89 San
  Francisco blocks without 0x80 hold `.ter` bounds.

In the older `sfai.psdl` 0x80 is only on 144 building blocks and the
freeway is plain 0x48, so the shipped cities' 0x20 and 0x80 were
recomputed by a later tool. The visibility files don't single these
blocks out ([pvs.md](pvs.md)). The city folders also hold `<city>.rid`,
each block's editor number one per line (1,171 and 1,341 lines), and
`<city>.extra`, pairs of those numbers and a model, each the block the
`.inst` puts that model in (15 in San Francisco; London's Underground
models and its river wall).

## The roads

After the bounds: a u32 count (379 in San Francisco, 540 in London: as
many as the `.bai` road map's roads, in the same order), then each road:

| Field | Contents |
|---|---|
| u16 | flags (0x440 and 0x441 commonest): below |
| u16 | more flags: below |
| u8, u8 | lanes on each side: the `.bai`'s side 1, then side 0 (all 540 in London, 358 of 379 in San Francisco) |
| float each | where each lane is across the road, 0-1 (0.125, 0.375, 0.625, 0.875 for four) |
| u8, u8 | a byte for each end, as the `.bai` orders them: bits 0 and 7. Bit 7 is set on every end where traffic gives way (the `.bai`'s control 0, 81 ends) and on 207 others; bit 0 goes mostly with traffic lights (463 of its 548 ends) |
| 8 u16 | vertices: 0 and 1 the road's curb corners at its first point (each its curb distance across), 4 and 5 at its last; 2, 3 and 6, 7 points 20-35 m out beyond each end, across the junction |
| u8 | blocks |
| u16 each | the road's blocks, as the `.bai` lists them (375 of 379 and 532 of 540 the same; the `.bai` adds a junction's block now and then); 23 of San Francisco's roads list theirs negative (-408 for block 408) |

Read this way both cities' files end exactly with the last road.

The road's flags repeat what the `.bai` says of it (compared over all
919 roads): 0x2 is set exactly when its side 1's sidewalk kind has bit 0
set (kinds 1 and 3), 0x4 exactly when it has bit 1 set (2 and 3), 0x8
exactly when the `.bai`'s road flag 0x1 is, 0x10 exactly when its 0x2 is;
0x1 is on 271 of the 369 one-way roads and 13 others; 0x40 on the roads
with sidewalks (not on 154 roads with kinds 1 or 3 both sides).

A second look, comparing each bit with everything the `.bai` and the
blocks say of the same road, settled more of them:

| Bits | Meaning | How it was settled |
|---|---|---|
| 0x20 | the freeway's loop | set exactly on the 4 roads with the `.bai`'s flag 0x4 |
| 0x100 | cable-car tracks | set exactly on the 21 roads whose `.bai` sides have trams (all `r4_track_f`) |
| 0x200 | a train line | set exactly on London's 28 roads with the Underground's track (`r_sub_l`) |
| 0x400, 0x800 | one two-bit field: the control at the road's first end (the `.bai`'s end 1), numbered as the `.bai`'s: 0 give way, 1 traffic lights, 3 none | 0x800 is never without 0x400; where lanes arrive at that end it equals the `.bai`'s control at all 559 ends; where none arrive the `.bai` has 3 and the PSDL keeps 0 or 1 (on 244 roads). 0 is on 45 roads, nearly all the stem of a T |
| second word, bits 0-1 | the control at the other end (the `.bai`'s end 0), the same way | equal on all 919 roads |
| second word, 0x80, 0x100 | the road's end 1, end 0 leads under the Underground | in London an end with a block over it always has the bit; seven 0x100 roads have no cover there, and in San Francisco the match is weak |

The second word's 0x40 is on 47 of the 48 roads with kind 1 both sides,
its 0x2 mostly with no sidewalk. Reluva takes the controls and the
trams from the `.bai`, which says the same.

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

## Street furniture rules

A road block's second byte is which of `city/<city>/proprules.csv`'s
rules line its road's sidewalks: 1-20 in San Francisco, which has rules
`n01left`... `n20right`, and 1-16 in London, which has `n01`-`n16`. Every
`.bai` road whose blocks have one has the same in all of them (332 of San
Francisco's 379 roads, 407 of London's 540; the rest, ramps and the
like, none). See [pathset.md](pathset.md).

## Not known yet

Block flags 0x80 and 0x20 exactly; bits 6 and 7 of the divider's
kind; a junction tunnel's word 3 and what its masks in words 6-9 mark;
the tunnels' flag 0x4000; the roads' flag 0x1 and the rest of the
second word; the end bytes' bit 0 exactly. The program's
own handling of these is encrypted (see ../../engines/age.md), so they're
for observation of the game running.
