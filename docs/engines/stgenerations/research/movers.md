# Movers (doors) in sgens.exe

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

H = read from code, M = code plus inference, L = guess. "Wall" = the 152-byte chunk-4 record,
"flat" = the 292-byte chunk-3 record (engines/stg/level.cpp names). 16.16 unless said.

## 1. Where they come from (GAME.LV2) — H

Movers are not a chunk of their own: they are built at level load by **0x4B7CA0** (called from the
loader at 0x4B56BB) from fields of the wall and flat records. level.cpp reads none of these yet.

Wall record (152 bytes):

| off | type | meaning |
|---|---|---|
| 0x69 | byte | record kind: **1** for every wall (flats: 2 floor, 3 ceiling) — the collision/pick code switches on it |
| 0x6C | u32 | flags: **0x2000 = follower part** (no 0x2000 = leader part); **0x80000 = reverse when blocked** (mover flag 4); 0x40 / 0x80 / 0x10000 are cleared while the door moves and restored when it closes (see §2.5) |
| 0x8A | s16 | **mover id** (0 = none) |
| 0x8C | s16 | **mode**: how the part moves (§2.3); also the key that ties followers to a leader |
| 0x8E | u16 | speed: if ≠ 0, mover rate = speed · 64 / 100 (last leader with a non-zero value wins) |
| 0x90 | u16 | wait: if ≠ 0, the mover closes by itself after wait · 4 ms (sets mover flag 1) |
| 0x92 | s16 | **bump target**: walking into this wall activates mover `+0x92` (any wall, not only door parts) |
| 0x94 | s16 | **bump mode** 0..3 (§3) — 0 in every file checked |

Flat record (292 bytes): **0x106** s16 mover id, **0x108** s16 mode (6 or 7), flags 0x6C bit 0x2000 =
follower. Flats **without** 0x2000 are ignored (they are never attached). Only followers.

Build (0x4B7CA0):
1. Every wall with a mover id and **no** 0x2000 is a **leader**. The first leader of an id creates
   the mover (36-byte record, map `*0x55207C` keyed by id); later leaders of the same id are chained
   after it (part+4). Each leader may set speed/wait (+0x8E, +0x90) and flag 4 (wall flag 0x80000).
2. Every wall with a mover id **and** 0x2000 is a **follower**: it is attached (part+8 chain) to the
   leader of the same mover whose **mode (+0x8C) equals its own**.
3. Every flat with a mover id and 0x2000: same, its +0x108 matched against the leaders' +0x8C.

Mover record (36 bytes, array `*0x552078`):

| off | meaning |
|---|---|
| +0 / +4 | next / prev in the active list `*0x552098` |
| +8 | first leader part (68-byte, `*0x552090`) |
| +0xC | wait counter (reset on every state change) |
| +0x10 | mover id |
| +0x14 | u16 rate, default 0x4000 |
| +0x16 | u16 wait, default 0x100 |
| +0x18 | s16 **position**: 0 closed, +0x4000 open one way, −0x4000 open the other way |
| +0x1C / +0x1E | s16 state / last state seen |
| +0x20 | flags: 1 closes by itself, 2 **locked** (natives 3C/3D), 4 reverse when blocked, 8 "last opened the negative way" |

Wall part (68 bytes): +0 wall, +4 next leader, +8 next follower, +0xC bit0 follower,
+0x10..+0x1C **original** vertex 1 and 2 (x, y), +0x20..+0x2C original wall +0x1C..+0x28
(normal x, angle, normal y, distance), +0x30 original bottom (wall +0x30), +0x34 original top
(wall +0x2C), +0x38 original vStart (+0x4C), +0x3C original vLength (+0x50), +0x40 saved flag bits
(1 = 0x40, 2 = 0x80, 4 = 0x10000).

Flat part (108 bytes): +0 flat, +4/+8 links, +0x10.. original vertices (count +0x64), +0x50
original height (+0x20), +0x54..+0x60 u/v offsets/scales, +0x66 rotation, +0x68 bit0 ceiling.

Counts (movers / leaders by mode / followers / flat followers / bump walls):

| level | movers | leaders by mode | wall followers | flat followers (mode) | bump walls |
|---|---|---|---|---|---|
| amargos1 | 42 | 6:77 7:2 1:6 0:5 | 62 | 6:18 7:1 | 29 |
| amargos2 | 54 | 6:69 7:14 0:11 1:10 | 75 | 6:16 7:1 | 51 |
| arvada1 | 12 | 7:66 6:13 0:2 1:1 | 39 | 7:42 6:1 | 9 |
| enterp | 111 | 0:132 1:132 | 361 | – | 625 |
| galorn1 | 62 | 6:84 0:20 1:20 7:8 2..5:3 each | 84 | 6:75 7:24 | 58 |
| galorn2 | 60 | 6:79 0:21 1:20 7:8 2..5:3 each | 111 | 6:75 7:24 | 58 |
| halee1 | 71 | 6:81 1:59 0:57 7:46 | 195 | 6:59 7:36 | 118 |
| verid1 | 0 | – | – | – | 0 |

Samples (amargos1): mover 77 = wall 11, mode 6 (slides up), speed 25600 (rate 16384), wait 256,
flags 0x9C300 (0x80000: reverses), top 7.0, bottom 0.5, vLength 128, bump → 77 mode 0 (the door is
its own button). Mover 81 = leaders 275/276/287 (mode 6, flags 0x103C0/0x10340) plus followers
639..641; speed 7680 from wall 276; no wait (stays open). halee1 mover 706: sliding pair, leaders
mode 0 and 1 (walls 483/547, speed 17920, wait 512), followers 524/548..550.

Rates seen: 25600 → 1.02 s to open; 12800 → 2.05 s; 51200 → 0.51 s (enterp: 51200/38400,
wait 1024 = 4.1 s). Waits: 256 → 1.0 s, 512 → 2.0 s, 1280 → 5.1 s.

## 2. How they move — H

### 2.1 Per frame: 0x4B82E0 (from the engine tick 0x4B5B10)

dt = frame ms (0x57A030, clamped 8..1000 — the engine's own clamp). For each mover in the active list:

```
saved = player block (0x57D9B8, 36 bytes); oldPos = pos; oldState = state
switch state:
 1 (open, +):   if flags&1: counter += dt>>2 (dt&~3); if counter >= wait: state = 4
                else: state = 0; flags &= ~8
 2 (opening +): clearOccluderFlags(); pos += rate*dt>>10; if pos >= 0x4000: pos = 0x4000, state = 1
                if !apply(): pos = oldPos; state = oldState; apply(); player = saved
 3 (closed):    restoreOccluderFlags(); state = 0
 4 (closing +): pos -= rate*dt>>10; if pos <= 0: pos = 0, state = 3, flags &= ~8
                if !apply(): revert as above; if flags&4: state = 2 (reopen)
 5 (open, −):   as 1 but → 7, and without auto-close: state = 0, flags |= 8
 6 (opening −): pos -= rate*dt>>10; if pos <= -0x4000: pos = -0x4000, state = 5
                if !apply(): revert; if flags&4: state = 7
 7 (closing −): pos += rate*dt>>10; if pos >= 0: pos = 0, state = 3, flags |= 8
                if !apply(): revert; if flags&4: state = 6
stateChanged(mover)   // 0x4B7980
```

(Only state 2 calls 0x4B9440 every frame; it is the flag clearing in §2.5.)

### 2.2 State changes and events: 0x4B7980

When state ≠ last state: state 0 → removed from the active list; from 0 → added; counter = 0;
an engine code is queued (0x4B8120) with id = mover id: state 2/6 → 12, 1/5 → 13, 4/7 → 14,
3 → 15, which the game turns into script **events to object `0x8000 | id`** (events.md):

| state | event | meaning |
|---|---|---|
| 2, 6 | **22** | started opening |
| 1, 5 | **23** | fully open |
| 4, 7 | **24** | started closing |
| 3 | **25** | closed |

The engine plays no sound: the type-3 script objects do it in their 22/24 handlers (amargos1
0x8042: `ev 22 → PLAY_SOUND 286, 63`, `ev 24 → PLAY_SOUND 287, 63`).

Natives (natives-e.md) write the state straight away through the object's sync (0x45643E:
`mover(id & 0x1FFF).state = v; 0x4B7980`), with no lock check: 39 → 2, 3A → 4, 3E → 1, 3F → 3,
3B toggles, 3C/3D set/clear flag 2. 3E/3F leave the position where it is; the next tick of
state 1/3 does not move anything (L: an instantly-open door keeps pos 0 until it closes again).

### 2.3 apply(): 0x4B85B0 — move every part to the position

f = pos · 4 (16.16: −1.0 .. 1.0). For each leader L (chain +4), for L and each follower P of it
(chain +8): walls (kind byte 1) → 0x4B8810(L, P, f); flats kind 2 or 3 → 0x4B8660(L, P, f).
Everything is computed from the parts' **original** values (part copies), never accumulated.

Walls, by the part's own mode (wall +0x8C), with L's original vertices A = L.v1, B = L.v2 and
H = L.top − L.bottom (originals):

| mode | motion | vertices of P | heights / texture |
|---|---|---|---|
| 0 | slide towards A | P.v − f·(B − A) | – |
| 1 | slide towards B | P.v + f·(B − A) | – |
| 2 | swing about A, +θ | A + R(+θ)(P.v − A) | – |
| 3 | swing about A, −θ | A + R(−θ)(P.v − A) | – |
| 4 | swing about B, −θ | B + R(−θ)(P.v − B) | – |
| 5 | swing about B, +θ | B + R(+θ)(P.v − B) | – |
| 6 | **slides up** | – | leader: bottom = P.bottom + f·H, vLength = P.vLength·(1 − f); follower: top = P.top + f·H |
| 7 | **slides down** | – | leader: top = P.top − f·H, vStart = P.vStart + f·P.vLength, vLength = P.vLength·(1 − f); follower: bottom = P.bottom − f·H |

θ = f · 90° (sin/cos table `*0x55215C`, 512 entries a turn, entry = ((f>>2) & 0xFFFF) >> 7;
each entry is (sin, cos)); R(θ)(x, y) = (x cos − y sin, x sin + y cos). For the leader itself
P = L, so slides move it by its own length and swings pivot about its own end. Negative f (states
5..7) slides/swings the other way. The new vertices are **written into the shared vertex table**
(`*0x57A584`), so anything else using those vertices moves too.

After moving a wall (0x4B76F0, 0x4B7670): angle a = atan2(v2 − v1) → wall +0x20; normal
(+0x1C, +0x24) = (sin a, −cos a); distance +0x28 = −(n · v2). (Same as the file's convention.)
Checked on amargos1 walls 5, 11, 218, 483: (dy/len, −dx/len) and −(n · v2) reproduce the file's
+0x1C, +0x24 and +0x28 (level.cpp's normalX/normalY/distance). Texture u is not touched (a slid door keeps its texture), v as in the table (v counts up from the
bottom edge, as the port already assumes: the texture rides with the moving edge).

Flats (0x4B8660), by the flat's mode (+0x108), with L's H as above:
* 6: height = P.height + f·H (rises with a door sliding up — floors of lift-like doors, raised slabs);
* 7: height = P.height − f·H;
* other: unchanged.
If the height changed: flat +0x20 = height; its zones in play (+0x74/+0x78) are looked up from the
flat's height→zones table (+0x70: {threshold, zones lo, zones hi} entries in descending order);
if the player stands on it (`*0x57D954`) and isn't airborne, the footing is re-checked (0x4B68A0,
the player rides it).

### 2.4 Blocking — apply() returns 0 to undo the step

* Walls: 0x4B9180(old, new): distance from the player (0x57D9B8: x, z, y; body) to the part's
  segment before and after (0x4B91C0, including how far above/below its bottom/top the player is);
  **blocked if the new distance is smaller than the old and below 1.0**. A part may move away from
  the player or past them at ≥ 1.0 units, never into them.
* Flats: 0x4B8760: a ceiling (flag 0x100000) moving down onto the player's head (player z + eye
  between the new and old heights) is checked with 0x4B69F0 → blocked if it would hit; a floor
  is never blocked (its result is forced to 1, kind 2), the player rides it.
* Any blocked part undoes the whole mover's step (all parts back to the previous position) and the
  player's position block is restored; closing movers with flag 4 reopen (states 4 → 2, 7 → 6,
  6 → 7).
* Actors are not checked (no reference to actor records in these functions).

### 2.5 Occluder flags

While opening (state 2, every frame, 0x4B9440), for wall parts with mode 2..7: modes 2..5 clear
wall flags 0x40, 0x80 and 0x10000 on every part; mode 6 clears 0x40 on the leader and 0x10000 on
all; mode 7 clears 0x80 on the leader and 0x10000 on all. On state 3 (closed, 0x4B93D0) the saved
bits come back. 0x40/0x80 are the renderer's "nothing visible below/above beyond this wall" portal
hints (billboard-draw.md); 0x10000 is likely another visibility hint (L). The port renders with a
depth buffer and doesn't use them: nothing to do.

## 3. Bumping — 0x4B94D0 (H)

Called from the player move (0x4B4751) when the wall the player collides with **changes** to a
wall (kind 1) — i.e. once per new contact, the same moment events 14/15 are queued. Only the
player; no use key. If the wall's +0x92 (bump target) ≠ 0, mover = +0x92; by its state and the
wall's +0x94 (bump mode):

| mover state | mode 0 | mode 1 | mode 2 | mode 3 (toggle) |
|---|---|---|---|---|
| 0 idle | → 2 unless locked | → 6 unless locked | → 7 if flag 8 else 4 | pos ≠ 0: → 7 if flag 8 else 4; pos = 0: unless locked → 6 if flag 8 else 2 |
| 1, 2, 5, 6 (open/opening) | – | – | state 1/2 → 4, 5/6 → 7 | same as mode 2 |
| 3, 4, 7 (closed/closing) | → 2 unless locked | → 6 unless locked | – | → 2 unless locked |

Every file checked uses mode 0 only. Door leaders usually bump themselves (+0x92 = own mover id);
enterp has 625 bump walls (frames and panels round the doors too).

## 4. Flats as movers

Only as followers of a wall leader (mode 6/7 heights). Lifts (moving a flat group between stored
heights) are the separate type-5 objects (natives 5E/64, natives-e.md); type-6 objects are light
groups. Flats never move horizontally.

## 5. Implementation plan for engines/stg

1. **LevelFile**: read wall +0x69 (ignore), +0x8A moverId, +0x8C moverMode, +0x8E moverSpeed,
   +0x90 moverWait, +0x92 bumpMover, +0x94 bumpMode; flat +0x106 moverId, +0x108 moverMode.
   Add a non-const `getVertices()` (movers rewrite vertices) and a helper
   `updateWallGeometry(int index)` that recomputes normalX/normalY/distance from the vertices:
   d = v2 − v1, len = |d|; normal = (dy/len, −dx/len) in 16.16; distance = −(n · v2)
   (verified against the file's values, §2.3).
2. **Movers class** (new engines/stg/movers.{h,cpp}, owned by Mission like Projectiles):
   build from the level as in §1 (leaders, followers matched by mode; keep originals: vertices of
   both ends, top, bottom, vStart, vLength, flat heights). State, pos (int, ±0x4000), rate, wait,
   counter, flags 1/2/4/8. `update(ms)` per §2.1 with apply() per §2.3 and the block test of §2.4
   (player only: distance from the player's (x, y) to the moved segment < radius-ish 1.0 and
   getting smaller while the player's feet..head overlaps the part's bottom..top). Queue events
   22..25 to `0x8000 | id` on state changes (§2.2) — Mission sends them after update.
   `setState(id, s)`, `lock(id, bool)`, `bump(wallIndex)` (§3).
3. **Walker / projectiles caches**: Walker keeps `std::shared_ptr<const std::vector<WallLine>>`
   built once; give it a way to refresh moved walls (e.g. make the cache rebuildable per wall index
   or rebuild the whole vector when a mover moved this frame — a few thousand walls, cheap enough
   to do only on frames where something moved). Projectiles' `_walls` (Line) needs the same.
   Actors' walkers share the cache pointer via the prototype copy, so rebuild through one shared
   object (or give Mission a "level geometry changed" hook that refreshes all three).
4. **Bump**: Walker already reports contacts (`_contactWall`); Mission's checkPlayerContacts sends
   14/15 on contact change — call `movers.bump(wall)` there when the new contact is a wall with
   bumpMover ≠ 0.
5. **Natives**: 39 → setState 2, 3A → 4, 3E → 1, 3F → 3, 3B toggle (open if pos == 0 / closed,
   else close), 3C/3D lock. Object id → mover id = id & 0x1FFF.
6. **Flats**: modes 6/7 change Sector.height; if the walker stands on that flat, re-place it
   (it rides up/down); keep Sector.activeZones as loaded (the height→zones table +0x70 isn't parsed
   yet — L: may matter for lifts/trapdoors changing visibility).
7. **Renderer**: reads walls/vertices/flats live each frame, so nothing else; the occluder flags
   (§2.5) can be ignored.
8. **Save/load**: per mover pos, state, last state, counter, flags; on load re-apply() every mover
   with pos ≠ 0 (geometry is derived from originals + pos, so it's restored exactly).
