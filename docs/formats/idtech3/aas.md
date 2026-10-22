# AAS files (the bots' maps) and bot scripts

## `.aas`

`maps/<name>.aas` is what Quake III's bot library (and Elite Force's,
the same library) knows of a level: the space a player can stand, crouch
or swim in, cut into convex areas, and the ways between them. The layout
and what the library answers from it were read from the GPL ioquake3 bot
library (`aasfile.h`, `be_aas_file.c`, `be_aas_sample.c`,
`be_aas_route.c`); Reluva's reader, `src/engines/idtech3/botlib/aas.cpp`,
is new code from this description.

Everything is little-endian.

### Header (124 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `"EAAS"` |
| 4 | 4 | version: 4, or 5 |
| 8 | 4 | the checksum of the BSP it was made from |
| 12 | 14 x 8 | the lumps: where each starts and its length |

In version 5 the header from offset 8 on is scrambled: each byte XORed
with its position counted from offset 8, times 119, kept to a byte.

### Lumps

| # | Lump | Record |
|---|---|---|
| 0 | bounding boxes | 32 bytes: presence type, flags, mins, maxs |
| 1 | vertexes | 12: a point |
| 2 | planes | 20: normal, distance, type |
| 3 | edges | 8: two vertexes |
| 4 | edge index | 4: an edge, negative if reversed |
| 5 | faces | 24: plane, flags, edges, first edge, front area, back area |
| 6 | face index | 4: a face, negative if its back faces the area |
| 7 | areas | 48: its number, faces, first face, mins, maxs, centre |
| 8 | area settings | 28: contents, flags, presence type, cluster, number in the cluster, reachabilities, first reachability |
| 9 | reachabilities | 44: area reached, face, edge, start, end, travel type, travel time (16 bits, hundredths of a second), 2 bytes unused |
| 10 | nodes | 12: plane, front child, back child |
| 11 | portals | 20 |
| 12 | portal index | 4 |
| 13 | clusters | 16 |

Record 0 of edges, faces, areas, nodes and reachabilities is unused.

- **Finding an area.** From node 1, take the front child when the point
  is in front of the node's plane (distance > 0), else the back one. A
  negative child is minus an area's number; 0 is solid.
- **Area settings.** Contents: water 1, lava 2, slime 4, cluster portal 8,
  teleporter 64, jump pad 128, do not enter 256, mover 1024. Flags:
  grounded 1, ladder 2, liquid 4, disabled 8, bridge 16. Presence types:
  standing 2, crouching 4. A standing player's box is -15 -15 -24 to 15 15
  32; a crouching one's top is 8.
- **Travel types and flags.** The types: walk 2, crouch 3, barrier jump 4,
  jump 5, ladder 6, walk off a ledge 7, swim 8, water jump 9, teleport 10,
  elevator 11, rocket jump 12, BFG jump 13, grapple 14, double jump 15, ramp
  jump 16, strafe jump 17, jump pad 18, func_bob 19. Each type has a travel
  flag. Walk is `0x2`, crouch `0x4`, barrier jump `0x8`, jump `0x10`,
  ladder `0x20`, walk off a ledge `0x80`, swim `0x100`, water jump `0x200`,
  teleport `0x400`, elevator `0x800`, jump pad `0x40000`, func_bob
  `0x1000000`. An area to be entered needs a flag for what fills it: air
  `0x80000`, water `0x100000`, slime `0x200000` or lava `0x400000`. It also
  needs do not enter `0x800000` if marked so, and bridge `0x4000000` if on a
  bridge. A route may only use reachabilities whose flags are all in the
  flags the game gives.

### Routing

A route's time is in hundredths of a second. It counts the
reachabilities' times plus the time to cross each area between them, from
where one arrives to where the next leaves. Crossing is the straight
distance times 0.33 (walking at 300 units a second), times 1.3 in an area
only crouched through, times 1 in liquid, and at least 1. From a point,
the time from there to the first reachability's start is added. Within
the goal area the time is 1.

The original speeds this up with clusters and portals and caches. Reluva
finds the quickest route out from the goal area over the whole map, which
gives the same kind of answer. It keeps the result per goal area and set
of travel flags, and forgets it when an area is turned on or off.

## Bot scripts (`botfiles/`)

Characters (`bots/<name>_c.c`), item and weapon weights, item and weapon
descriptions and chats are text in a C-like form. They go through the bot
library's preprocessor (read from `l_precomp.c` and `l_script.c`):

- `#include "file"` or `<file>`, under `botfiles/`;
- `#define` with or without parameters (`#` and `##` in their bodies),
  and `#undef`;
- `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else` and `#endif`, with C's
  operators and `defined`;
- `$evalint(...)` and `$evalfloat(...)` in the text, which evaluate an
  expression;
- strings with only white space between them are joined, with C's
  escapes; `//` and `/* */` comments.

A character file is blocks of `skill N { index value ... }`. The value is
an integer, a float or a string, and the index is usually a define from
`chars.h`. Loading a character clamps the skill to 1–5. Skills 1, 4 and 5
are read as they are. Others blend the floats between 1 and 4, or between
4 and 5, and take the integers and strings from the lower. A missing skill
falls back to the default character, `bots/default_c.c`, then to the
file's first skill. Characteristics the file leaves out come from the
default character. Reluva's side: `bot_script.cpp`, `character.cpp`.
