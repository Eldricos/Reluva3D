# Levels (GAME.LV2)

> **Status:** partial · **Code:** `src/engines/stgenerations/formats/level.h`, `src/engines/stgenerations/game/render3d.h` · **Confidence:** confirmed unless marked

## Overview

A location's level, `GAME.LV2`: its palette, vertices, flats, walls, billboards, actors, sky and textures, as chunks. It's decoded enough to
render a level.

Loaded by the routine at `0x4BD130` in `sgens.exe`. All values are little endian.

## Layout

| Offset | Type | Meaning |
|--------|------|---------|
| 0 | u32 | version, 1 |
| 4 | u32[9] | record sizes: 1024, 8, 292, 152, 140, 276, 196, 64, 24. The original refuses the file if any differs |
| 40 | u32 | number of directory entries |
| 44 | u32 | directory size in bytes (entries × 16) |
| 48 | entries | u32 size, u32 file offset, u16 type, u16 id, u16 count A, u16 count B |

The chunks follow the directory. In verid1's file (448 entries), every byte
belongs to the header, the directory or exactly one chunk. The game loads a
chunk by (type, id). The tables use id 1, and count B is the number of
records.

| Type | Record | Contents |
|------|--------|----------|
| 1 | 4 | palette, 256 Windows RGBQUADs (B, G, R, 0); the console bitmaps use the same indices |
| 2 | 8 | vertices: int32 x, y in 16.16 fixed point. The floor plan, y up |
| 3 | 292 | flats: floor and ceiling polygons (see below) |
| 4 | 152 | walls (see below) |
| 5 | 140 | billboards: sprites that always face the camera |
| 6 | 276 | actors (see Actors below) |
| 7 | 196 | empty in verid1 |
| 12 | – | the sky (see below) |
| 13 | – | textures, one chunk per texture, id = texture number |

World coordinates are 16.16 fixed point. x and y are the floor plan, and
z is height, growing upwards. In verid1, walls are mostly 12 units high and
a texture usually covers 10 or 15 units.

Several records share a layout for animated textures: a frame count and a
speed, with the current frame being the base texture id plus an offset
that advances with time.

**Flats** (type 3) are horizontal convex polygons. Each one is a floor or a
ceiling at a single height; a level's floors and ceilings are stacked to
make rooms over rooms.

| Offset | Meaning |
|--------|---------|
| 0x14 | u32 group: flats with the same value are linked in one list (count A groups) |
| 0x20 | height |
| 0x3C | u64 zone the flat is in (one bit set); see Collision and zones |
| 0x44, 0x48 | u offset and scale: u = x · scale + offset, in texels |
| 0x4C, 0x50 | v offset and scale: v = y · scale + offset |
| 0x5C | maximum drawing distance, 15000 |
| 0x60, 0x62 | i16 u and v scroll speeds |
| 0x64, 0x66, 0x68 | lighting: i16 light group, u16 brightness, byte light level (see Lighting) |
| 0x69 | byte record kind: 2 on floors and 3 on ceilings |
| 0x6C | u32 flags: 0x100 visible, 0x100000 ceiling, 0x4000 extra pass (`0x4C8120`), 0x1 left out of the zone lists, 0x1000 pass-through: nothing rests on it. The player (`0x4B4904`), actors (`0x4B6AB0`) and items (`0x4B7AA0`) go through to the floor below; the player's script events still fire. These are trigger floors and water surfaces; as ceilings, they don't stop the head |
| 0x74 | u64 zones in play while standing on this floor |
| 0x7C | 16 bytes per vertex: an edge plane a, c, d (a·x + c·y + d = 0) at +0, +8 and +12; +4 receives the transformed vertex's address at load time |
| 0xFC | animation start time (run time) |
| 0x100, 0x102, 0x104 | u16 animation frame count, speed and current frame |
| 0x10A | u16 vertex count, 3–8 |
| 0x10C | u16 vertex indices |
| 0x11C | u16 texture id |
| 0x11E | u16 texture rotation angle, a full turn being 65536 |

A floor is drawn only when the camera is above it, and a ceiling only when
the camera is below it.

**Walls** (type 4) are vertical quads.

| Offset | Meaning |
|--------|---------|
| 0x1C, 0x24, 0x28 | line nx·x + ny·y + d = 0 with a unit normal, used to cull walls facing away |
| 0x20 | the heading of vertex 1 to vertex 2 in turns (0x10000 = 360°); scripts that turn objects rebuild the line from it (`0x4B7670`) |
| 0x2C, 0x30 | heights of the top and bottom edges |
| 0x44, 0x48 | u at the first vertex, and u length along the wall, in 128ths of the texture's width |
| 0x4C, 0x50 | v at the bottom edge, and v length up the wall, in 128ths of the texture's height; v counts rows from the bottom of the texture. Walls stacked on one line continue each other this way (verid1 and halee1 have 17 such pairs, none the other way round) |
| 0x62 | i16 v scroll speed |
| 0x64, 0x66, 0x68 | lighting, as for flats |
| 0x69 | byte record kind, 1 |
| 0x3C | u64 zones the wall is in |
| 0x6C | u32 flags: 0x100 visible; a wall faces the side its normal points to (the right of vertex 1 to vertex 2) and is drawn only from there, unless 0x8000, which also blocks from both sides (0x4000 alone: unseen from behind but hides what is past it); 0x20 mirrors the texture; seen from behind, u still runs from vertex 1 to vertex 2 if exactly one of 0x20 and 0x40000 is set, and the other way otherwise (`0x4C9393`; halee1's 55 rock-edge caps keep their dark edge on the end joining the rock only this way); 0x2 draws columns that are one run of texels over that run alone, so a silhouette isn't repeated above itself (`0x4C0AD0`); 0x40, 0x80 hide what is past the wall below or above it; 0x8 masked (has transparent texels), 0x4 unknown, 0x2000 set aside for script handling at `0x4B7DB5` |
| 0x60 | i16 u scroll speed |
| 0x70, 0x74 | u offsets of the ends for a texture running on along a strip of walls: u0 − (+0x74) at vertex 1 and u0 − (+0x70) at vertex 2, added rather than taken away when mirrored as above |
| 0x78 | extra thickness, added to the radius in one kind of collision test |
| 0x7C, 0x80, 0x82, 0x84 | animation start time, speed, frame count, current frame |
| 0x86, 0x88 | u16 vertex indices |
| 0x8A | u16 scripted object id |
| 0x96 | u16 texture id |

Texture coordinates on walls and floors are in 128ths of the texture: a u
or v length of 128 shows a texture once, whether it is 32, 64, 128 or 256
texels across. 128 is by far the most common length in all four levels
checked, for every texture size. A 256-texel texture can hold two pictures,
one above the other, and walls pick either half; arvada1's texture 10 has a
cliff over a misty mountain range.

**Billboards** (type 5) stand at x +0x1C, z +0x20, y +0x24. They are
2 × (+0x34) wide and +0x38 high, with the texture id at u16 +0x8A and the
frame count at +0x86. Lighting is at +0x64..+0x68, as for flats (record kind 4 or 5).

**Sky** (type 12, id 0) is a 64-byte header followed by an image stored
row by row. The header holds the data size at +16 and the height at +24;
the width is size / height. verid1's sky is 640 × 252 and wraps seamlessly
from left to right. halee1's header has the same fields apart from the size,
65536, which 252 doesn't divide: its sky is 512 × 128. Where the height
doesn't fit, the reimplementation takes the width at which neighbouring rows
match best. The header also holds 504 at +20, which the drawing
routine at `0x4C2F50` uses as its row stride. That doesn't fit verid1's
image, so this field isn't understood yet. Nor is how the sky maps to the
view angle; the reimplementation tiles it at one sky pixel per screen pixel.

**Textures** have a 20-byte header: 12 zero bytes (pointers filled in at
run time), then u16 id, width, height and flags. Then come width × height
pixels stored column by column, each column bottom first (billboard pictures that stand on the ground fill the first stored row; in verid1, halee1, amargos1 and arvada1, 85 do and 11 fill the last instead), and 3 × width bytes
of per-column hints like GS2's. Flag 1 means the texture has no hints. Palette index 255
(magenta) is transparent.

## Behaviour

The renderer was traced through `sgens.exe`:
- **Camera.** `0x4C3600` transforms the vertices with the camera at
  `0x57D9B8` (x, z, y). Yaw cos/sin are at `0x57D9CC`/`0x57D9D0`, and a pitch
  shear is at `0x57D9D8`. Looking up and down shears the view instead of
  rotating it.
- **Projection.** screen x = centre + x/depth · W/2 and screen y = centre −
  y/depth · W/2, set up at `0x4B58B0`. That is a 90° horizontal field of view.
- **Drawing.** `0x4C13F0` draws flats and `0x4C1A50` texture-maps them;
  `0x4C7D00` draws walls.

How walls, billboards and actors are drawn depends on their flags
(`0x4C5820`, `0x4C0AD0`, `0x4C80B4`):

- Index 255 is skipped. Without flag 0x4 the original draws billboards and actors with a routine
  that doesn't skip it, but none in verid1 lack the flag.
- Flag 0x8 makes them **see-through**: walls with 0x8 alone, billboards and actors with 0x8 and
  0x4. Texels 0xF0..0xFE are then averaged with the picture behind them through a 15 × 256 table
  built at level load (`0x4BD620`): entry [s − 0xF0][d] is the palette entry nearest
  ((pal[s] + pal[d]) >> 1), the first one winning a tie. Those texels aren't shaded; the rest are
  drawn as usual. verid1's steam (textures 143..147, flags 0x30C) is drawn this way.
- Without 0x8, texels 0xF0..0xFE are ordinary colours.
- Flag 0x2 (trim columns to their opaque run), and 0x40 and 0x80 (hide what is above or below),
  only save work. Flag 0x10 is fullbright and 0x20 mirrors.

Billboards with flag 0x400000 are **items** (record kind 5). The level keeps
the ones not yet in play in a storage room off the map, in a zone of their
own. At the start (`0x4BDD70`) and whenever a script puts one somewhere,
`0x4B7AA0` drops it onto the highest floor no more than 2 units above its
base, passing over floors with flag 0x1000. It takes that floor's zones and
light, so it is drawn wherever it lands. Flag 0x1000000 keeps its height.

At load time the game builds a 64 × 64 grid of 32-byte cells over the map
(around `0x4BEE07`), presumably for collision and visibility.

### Lighting

Every wall, flat, billboard and actor is drawn at one of 8 light levels.
- **Shade tables.** Each level has a colour table, built at `0x4BFC60` and
  held as 8 pointers at `0x575D80`. Table n maps each palette entry to the
  entry nearest to it blended n/8 of the way toward a fog colour; table 0 is
  the identity, drawn by faster routines. The builder is only called when a
  level starts, and then with black, so the levels are darkness. There is
  no distance fog. Where there is no sky, the background is filled with the
  entry nearest the fog colour, kept at `0x575670`.
- **Choosing the level.** A surface's level comes from offsets 0x64-0x68:
  - If the i16 light group at 0x64 is 0, the level is the byte at 0x68
    (0 for every such surface in verid1).
  - Otherwise, on load (`0x4BE0E0`) the level is 7 − min(7, brightness ÷
    3200), with the u16 brightness at 0x66.
  - A script can set a group's brightness percentage (the native at
    `0x4B7BA0`). The level then becomes 7 − min(7, percent × brightness ÷
    320000).
- **Actors.** An actor takes the light level of the flat it stands on
  (`0x4B7A50`).

In verid1, about half the walls and flats are in light groups, at
brightness 5120 to 20480. That leaves the caves under the ice noticeably
darker than the open canyon.

### Actors

Type 6 records are the actors themselves: the engine updates them in
place (`0x4C3C10`). Records sharing an id (+0x14) are parts of one actor,
chained at +0, and change mode together. verid1 has 40 actors, each with
one part.

| Offset | Meaning |
|--------|---------|
| 0x14 | id; the script object is 0xA000 \| id |
| 0x1C, 0x20, 0x24 | x, z (feet), y |
| 0x28 | reach: "walk to" has arrived within this distance |
| 0x30 | added to z for the sprite's bottom edge |
| 0x34 | sprite scale (at run time, the current half width) |
| 0x38 | sprite height in world units |
| 0x54, 0x58 | collision radius and height |
| 0x64..0x68 | light, as walls; at run time the floor's light is copied in |
| 0x6C | flags: 0x1 not drawn, 0x10 fullbright, 0x400000 never frozen, 0x1000000 flyer (keeps its height) |
| 0x90, 0x94, 0x98 | wander: outer, inner and home radius |
| 0x9C, 0xA0 | wander: walk between this and this far before a random turn |
| 0xAC | step height |
| 0xB0 | home point (the start position) |
| 0xC4 | 7 animation entries of 8 bytes (below) |
| 0xFC | u16 sprite set number: the actor's sprites are `NNN.gs2` next to GAME.LV2, and the number is also its class |
| 0x100, 0x102 | wander: random turn range, 65536 to a turn |
| 0x104 | turning speed, heading units per 1024 ms |
| 0x108 | heading: 65536 to a turn, 0 faces +y, 16384 faces −x |
| 0x10D | wander around the player rather than home |
| 0x10E | mode (below) |

**Animation entries.** Each entry has eight bytes:
- u16 speed, 8.8 units walked per frame (0 means 0x40);
- rate;
- frames per view direction (0 means 1);
- loop start, clamped to frames − 1;
- number of view directions (8 or 1);
- mirror flag;
- the sequence's number (unused).

The entry for mode m is min(m, 6). Its GS2 sequence is given by the table
at `0x552338`:

| Entry | Animation | GS2 sequence |
|-------|-----------|--------------|
| 0 | stand | 0 |
| 1 | pain | 2 |
| 2 | stand | 0 |
| 3 | attack | 3 |
| 4 | death A | 4 |
| 5 | death B | 5 |
| 6 | walk | 1 |

A sequence holds each view direction's frames in turn.

**Timing** (`0x4C4230`):
- Each tick advances the animation by rate × dt / 1024 frames (dt in ms),
  which is about `rate` frames a second.
- The first pass plays every frame; after that the animation loops from
  loop start. A loop start of frames − 1 plays the animation once and holds
  the last frame.
- Each cycle raises script event 37, unless the animation has only one frame.
- Walking moves speed × frames advanced.

**Drawing** (`0x4C3C10`):
- **Facing:** let A be the angle from the camera to the actor, measured like a heading.
  The octant is `(u16)(−A − heading + 0x1000) >> 13`, and direction =
  octant × directions / 8.
  - Direction 0 shows the actor's back, 2 its right side and 4 its front.
  - With the mirror flag, directions 5–7 are drawn as 3–1 flipped.
  - The frame is the sequence's entry at frame + direction × frames, or entry 0 if that is past the end.
- **Size:** the sprite is centred on x, y. It is 2 × the frame's half width
  × scale wide and 0x38 high, and stands on z + 0x30.
- **Light:** it is lit like the floor it stands on.

**Modes**. Scripts set them, and nothing changes mode by itself:

| Mode | Behaviour |
|------|-----------|
| 0 | idle |
| 1 | pain animation |
| 2 | turn to face the target or the player; event 36 once facing |
| 3 | face and fire at the start of each cycle (each shot raises 36) |
| 4, 5 | death animations |
| 6 | wander (below) |
| 7 | walk straight at the target or the player; event 36 every tick while within reach. Blocked, it turns a quarter and tries again (no path finding) |
| 8 | hidden: neither drawn nor updated |

**Wander** (mode 6):
- Walks its heading and turns at the turning speed.
- After walking a random distance, it turns by a random angle in the
  turn range, always in the same direction.
- Beyond the outer radius from its centre it turns a quarter towards the
  centre. Beyond the home radius it walks home until it is inside the outer
  radius again. Crossing inwards past the inner radius turns it a quarter.
- A wall turns it a quarter.
- Its centre (`0x4C4015`) is its home point, or with a guard mode (record
  +0x10D) what it guards: SET_ACTOR_COMBAT_PARAMS id, mode, target, ... sets
  mode 1 for an actor or 2 for a billboard, found by number (`0x4B5DF0`), and
  target 0 means the player. arvada1 and bersus have actors guard others
  (10, 12, 18, 38-47); the rest wander about home or the player.
- SET_ACTOR_COMBAT_PARAMS (`0x457703`) keeps any value given as 0x8000 (or
  0x80000000 for the distances). It stores its angles as given. verid1 gives
  degrees, so its wanderers turn by under a degree at a time, as in the
  original.

**Movement:** actors move with the player's sweep and slide, using their own
radius, height and step height. They snap onto floors and never fall.

**Frozen actors:** an actor whose floor is in a zone the player can't see
is frozen.

**Proximity events.** SET_PROXIMITY_RANGES sets four distances, r1 to r4, for
an actor class and a target class (0 is the player). Each is measured from
the player's eye to the actor's feet:
- 29 when the player comes within r1 (with a line of sight in the original);
- 31 when the player goes beyond r2;
- 33 when the player comes within r3;
- 35 when the player goes beyond r4.

The player collides with actors as with wall ends. A bump sends 14 and 15,
as walls do.

`src/engines/stgenerations/game/actors.h` implements all this, with shots (`projectiles.h`),
damage and the line-of-sight check.

### Collision and zones

The player update at `0x4B46A0` moves the player with these settings, set
at `0x4BE150`:

| Address | Value | Meaning |
|---------|-------|---------|
| `0x57D98C` | 6.0 | eye height above the feet |
| `0x57D990` | 0.51 | headroom: the body reaches this far above the eye |
| `0x57D994` | 2.0 | step height |
| `0x57D998` | 1.2 | radius |

A move goes from the previous position to the new one (`0x4B9870`):
- **Sweep.** `0x4B9940` sweeps a circle of the player's radius and finds
  the first wall or actor touched. Walls come from a grid along the path.
- **Which walls count.** A wall is tested only if it is visible, in an
  active zone, and its bottom-to-top range overlaps the player's body above
  the step height. Walls below that are stepped over.
- **Wall tests.** Each wall is tested as a line (`0x4BA670`) and as its two
  end points (`0x4BA1E0`). The line only blocks from the front, the side
  its normal points to, unless flag 0x8000 is set.
- **On a hit.** The player stops at the wall. The result includes the wall's
  direction, and what is left of the move slides along it. If the slide
  hits something too, the move ends there.
- **Script event.** A wall the player touches sends an event to the scripts,
  with the record kind at +0x69 and the wall's +0x14.

After moving, the player stands on the highest floor under them that is no
more than the step height above the feet (`0x4B4C50`). Under a ceiling lower
than the eye plus the headroom, the eye comes down to fit (`0x4B46A0`); the
upper doorways in AMARGOS1 are only that tall. A wall whose bottom the head
just touches doesn't block. This search uses the
64 × 64 grid of flats built at `0x4BEDE0`, where each flat's outline is
pushed out by 3 units. With no floor underneath the player falls; the
original's falling physics (`0x4B4800`) is not decoded yet.

**Zones** are 64 bits that partition the level. Every flat is in one zone,
and walls and billboards are in one or more. The floor the player stands
on lists the zones in play at 0x74 and makes them active (`0x4B68A0`).
Only walls and floors in active zones are collided with, and only
surfaces in active zones are drawn. That is how a level keeps rooms stacked
above one another, or caves under a canyon, apart. With no floor
underfoot, every zone is active. A script can add zones of its own; these
are kept at `0x57D9AC`.

### Water

SET_FLAT_GROUPS (0xCD, `0x440D3D`) a, b, c makes a pool from three flat ids
(the flats' own +0x14, not script ids): a is the surface seen from below (a
pass-through ceiling), b the surface seen from above (a pass-through floor)
and c the pool's bottom. It marks them 2, 1 and 3 at the flat's +0x18 (the
bottom also records b). antilios has six pools, verid1 two, and arvada1,
bersus, galorn1/2 and halee1 more.

Every frame `0x43D0C9` looks at the floor under the player. On a bottom, it
measures the eye's depth under the surface above it (the eye height, if
there's no surface overhead): 0.6 or more is under water (2), more than 0.6
above is wading or afloat (1), and in between the player goes under if
sinking and floats up if not. A change is broadcast: 56 under water, 57
wading, 58 out of the water. Gravity follows, unless control 0x40 is off:
afloat, it is (height of the eye over the surface − 1) / 6 of normal
(buoyancy), under water none. With 0x40 off, under water it is half, with a
fastest fall of 100. Afloat, the zones above the surface are shown too
(`0x43D765`).

The scripts do the rest. antilios's pools, on 56, turn LOW_GRAVITY on, tint
the screen, show a view overlay, slow the player (SET_PLAYER_MOTION) and take
control 0x40 away, so the player sinks slowly and walks along the bottom; 58
undoes it.

## In Reluva

Opening a `GAME.LV2` in the viewer draws its floor plan. `]` switches to a
3D view of the level that you can walk through (arrows or WASD, PgUp/PgDn
to look up and down, End to level the view, L to switch lighting off and on,
Shift+C to walk through walls, X to jump, C to crouch). Pressing `]` again steps
through the textures.

## Sources and checks

From STG-Engine's notes on the 2 CD Windows release ([README](README.md) says how they were made). All values are little-endian.
