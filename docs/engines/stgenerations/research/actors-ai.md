# Actor AI, movement and animation timing (sgens.exe)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Confidence: **H** read directly from code, **M** code read but meaning inferred, **L** guess.
Scripts used: `re/ai/dump.py` (range disassembly), `re/ai/grepins.py`, `re/ai/actors.py`
(dumps verid1 actor records). Note: `exe.all_insns()` stops at 0x401888 (linear sweep hits data);
use `re/sweep.py` (skipdata) for greps.

## 0. Which record, where (H)

* The engine actor record **is the GAME.LV2 type-6 record itself (276 = 0x114 bytes)**, loaded raw:
  array `*0x57A18C`, count `*0x57A188` (0x4BD2C9/0x4BD350). The 196-byte records at `*0x5743A0`
  (count `*0x57439C`) are LV2 **type 7** (empty in verid1); an actor whose +0xFE != -1 drags the
  type-7 record with that index along (0x4C3A50: copies x/y, z+rec7+0x90, heading offset, light,
  zones). So the "196-byte actor record" in the brief is the wrong table; all offsets below are in
  the 0x114 record, and they agree with the brief's offsets (+0x1C.. +0x108).
* Active actors: doubly linked list head `0x57A17C` (next +4, prev +8); actors created in mode 8
  go in a second list `0x57A174` (level init 0x4BE2D7). Mode 8 unlinks (0x4B7790), others link
  (0x4B7730), flag 0x100 in +0x6C = linked.
* Multi-part actors: parts chained through +0 (all with the same id); every setter loops `rec=rec->+0`.
* Fields used by the AI (16.16 unless noted):

| off | meaning | verid1 values |
|---|---|---|
| +0x0C | floor flat under the actor (or dummy 0x57A700) | |
| +0x14 | id (word) | |
| +0x1C/+0x20/+0x24/+0x28.. | x, z (height), y; +0x28 = **reach** for mode 7 | reach = radius in file |
| +0x30 | z offset (stored z = logic z + this) | 0 / -1 |
| +0x3C/+0x40 | zone mask = the zone of its floor flat (copied each update, 0x4C3E65) | |
| +0x54 | collision radius | 1.0–5.0 |
| +0x58 | body height (sweep "height", obstacle height, ceiling clamp) | 2.5–20 |
| +0x6C | flags: 0x100 linked, 0x400000 update even in inactive zones, 0x1000000 flyer, 0x8000000 "sees player", 0x10000000 "player near", 0x40000000 "target near" | 0x206, 0x1000206 |
| +0x74 | target record (actor / billboard / point) for modes 2/3/7 | |
| +0x70 | guard target for mode 6 when +0x10D != 0 | |
| +0x7C | projectile spawner (mode 3) | default `*0x5522BC` |
| +0x80 | horizontal speed factor (flyers: vertical factor = 1−this) | 1.0 everywhere |
| +0x84/+0x88/+0x8C | muzzle offset (side, up, forward) | 0,5,0 |
| +0x90 R3, +0x94 R1, +0x98 R2 | mode-6 distance bands | 10–25 / 0 / 20 |
| +0x9C/+0xA0 | min/max distance walked between random turns | 0 / 10–30 |
| +0xA4/+0xA8 | current turn threshold / distance walked so far | 0 |
| +0xAC | step height | 3.0 |
| +0xB0..+0xBC | home x,z,y,heading (file copy of the start position; never rewritten by SET_POSITION) | |
| +0xC4+e*8 | animation entry e = min(mode,6): +0 word **speed**, +2 **rate**, +3 **frame count**, +4 **loop-start frame**, +5 directions, +6 mirror flag, +7 sequence base | see §4 |
| +0xFC | class (word, 0..15) | 1..10 |
| +0xFE | attached type-7 record index or -1 | |
| +0x100/+0x102 | random turn range min/max (heading units, 65536 = 360°) | 0 / 16384 or 32768 |
| +0x104 | turn rate (word) | 0, 16384, 32768, 49152 |
| +0x106 | animation clock, int16 8.8 frames | |
| +0x108 | heading (word); +0x10A wanted heading; +0x10C side byte (0/−1/1) | |
| +0x10D | mode-6 centre: 0 home, 1 actor (+0x70), 2 billboard; +0x70 null → the player | |
| +0x10E | **AI mode (byte)** | file: 0, 6, 7 |
| +0x10F | AI sub-state (byte) | |
| +0x111 | displayed sprite frame (computed each update; drawing) | |

Game object side (H): `obj+0x64` = requested mode (natives write it), `obj+0x60` = last applied,
`obj+0x70` target. The object's sync (vt18 = 0x456907, run from the deferred-sync list 0x44E3A4 at the
end of the game update, i.e. before the engine tick) writes rec+0x10E (and resets +0x106 if the mode
changed), rec+0x74 (if a target was set), then **always** calls 0x4B5090 on every part:
`+0x106 = 0, +0x10F = 1`, clears flags 0x40000000|0x20000000 if no target, links/unlinks.
So *any* sync of the object (mode change or e.g. SET_ACTOR_COMBAT_PARAMS, which also marks dirty)
restarts the animation and the sub-state (H).

## 1. Clock and per-frame order (H)

* dt = `0x57A030` ms = frame time clamped 8..1000 (0x4B5B60).
* Engine tick 0x4B5B10: … movers 0x4B82E0, player 0x4B46A0, projectiles 0x4C4C70,
  **actors 0x4C3C10**, light 0x4B7A50, billboards 0x4BBEA0, …, flush queued events 0x4B80D0.
* Heading units: 16-bit, 65536 = 360°. Trig table `*0x55215C` = 512 × (sin, cos) 16.16, index h>>7
  (built 0x4BC650). Moving with heading h: `x -= d·sin h, y += d·cos h`; facing a vector (dx,dy):
  `h = −atan(dx,dy)` (0x4BC560), so heading 0 = +y, 0x4000 = −x.
* Distances: to the player = `max + (other two)/4` of |dx|,|dz|,|dy| (0x4B4630, 3-D, from the
  **player's eye** 0x57D9B8 to the actor's feet); to a target and mode-6 centre = 2-D
  `max + 3/4·min` (0x4C57F0).

### Actor update 0x4C3C10, for each actor in list 0x57A17C (H)
1. If no flag 0x400000 and its zones (+0x3C/+0x40) ∩ active zones (0x57D9A4/A8) = ∅: frozen — no
   animation, no movement, no events; only if "sees player" was set it is cleared and event 31 sent.
2. z −= +0x30. 0x4C3F70: build context (old pos, new pos = old, player rel, target rel — mode 6:
   home or +0x70/player; others: +0x74 or player), d3 = distance to player, d2 = 2-D distance to
   target; then the r3/r4 proximity tests (§3).
3. If new z < floor height and floor lacks flag 0x1000, z = floor.
4. 0x4C4230: animation advance + mode behaviour (§2, §4).
5. If the mode asked for it (ctx+0x5C bit 0): floor snap 0x4C3AC0 (§2.3). z += +0x30.
6. Sprite direction/frame (+0x111, drawing), zones/light copied from the floor.
7. "Sees player" r1/r2 test with line of sight (§3). Attached type-7 record follows (0x4C3A50).

## 2. Modes (switch 0x4C43B4, table 0x4C4448 for modes 2..7; others → default 0x4C43BB) (H)

Movement amount per tick (modes 6/7): `adv = floor(dt·rate/4)` (1/256 frames, possibly capped, §4),
**step = speed·adv** (16.16) → **units/s = speed·rate/262.144**, i.e. speed/256 units per animation
frame. Both modes use entry 6 (walk). Default speed when the file has 0: 0x40 (0x4BE3B3).
verid1 walk entries: speed 256 rate 5 (class 7, 4.9 u/s), 256@7 (class 8, 6.8), 128@7 (class 9, 3.4),
256@10 (class 6, 9.8), 1536@6 (class 10, 35 u/s). Scripts change rate with N53 (rate 0 = stand still,
200 = fast).

Turning (0x4C4620): `max = rate104·dt >> 10` heading units per tick (rate104 per 1.024 s:
16384 ≈ 88°/s, 32768 ≈ 176°/s); shortest way (16-bit wrap), clamps exactly onto the goal, returns
the applied delta (0 = already facing). Turn rate 0 = never turns.

* **0, 1, 4, 5 (and any ≥ 9)**: stand. Only the animation runs. Movement only if the floor is a
  conveyor (floor flag 0x200000, 0x4BBFF0, then swept with 0x4B9870) and snap to floor if it
  moved or the floor flat is a moving lift (+0x106 != 0) (0x4BBEF0, 0x4C43BB). No turning.
* **2 face** and **3 attack** (0x4C45C0): turn toward the target (or player). When heading == goal
  and (it turned this tick or +0x10F != 0) → engine code 16 → **event 36**, then +0x10F = 0.
  Since every sync sets +0x10F = 1, entering mode 2 while already facing still gives 36 on the
  first tick. No movement.
  Mode 3 additionally **fires** (0x4C4460) when the anim clock is 0: on the first tick after the
  mode was set, and on the tick after each cycle wrap. Shot: from pos + rotated muzzle offset
  (+0x84,+0x88(+0x30),+0x8C), aimed at the target point, spawner `+0x7C->vt[4]`, projectile+0x18 =
  shooter. Every shot also queues code 18 → **event 36** (delivered without the mode check).
* **6 wander** (0x4C4670): first moves `step·k80` along the current heading with full collision
  (0x4C4AE0, §2.2); D = 2-D distance from the new position to the centre (home +0xB0; or the player
  / +0x70 target if +0x10D != 0), Dprev = distance before the move. Sub-state S = +0x10F:
  * S ≥ 4 (returning): if blocked → (S=5: finish turning to +0x10A first) set +0x10A = heading ±
    (90°..135°: 0x6000 − rand&0x1FFF, side from the wall direction), S=5; else turn toward the
    centre, S=4. When D < R3 → S=0.
  * S < 4, D ≥ R3: (after finishing any turn) +0x10A = heading ±90° toward the centre side;
    then S = 1 if D < R2, else 3 if Dprev < R2, else 4 (head home).
  * S < 4, D < R3: if D < R1 ≤ Dprev → +0x10A = heading+90°, S=3; else if blocked →
    +0x10A = heading ±90° (side from the wall), S=2; else turn toward +0x10A, S=0.
  * Random turns (S < 3): +0xA8 += step; when +0xA8 ≥ +0xA4 and S == 0: +0x10A = heading +
    (angMin + rand·(angMax−angMin)) — always the same rotational sense (side byte is 1 unless
    the LCG hits 0; quirk); new threshold +0xA4 = tMin + rand·(tMax−tMin) whole units; +0xA8 = 0.
    So the "time" parameters of N4E are **distances walked**, not time. First tick picks at once.
  * LCG `0x57AA68: s = s·0x41C64E6D + 0x3039`, seeded from the system clock (0x42FDD3).
  * N4E passes angMin/angMax raw (e.g. 45, 180) with no degree conversion (0x450F8A → 0x457703 →
    0x4B5DF0), so scripted wanderers turn only 0.25°–1° at random; file defaults are 0..90°/180°.
  * No events from mode 6 itself.
* **7 walk to target** (0x4C4A10): moves first (0x4C4AE0). If Dprev (2-D distance to target or
  player, computed before the move) < reach +0x28 → code 16 → **event 36, every tick while within
  reach**; the actor does **not** stop (scripts change the mode on 36). Steering: not blocked →
  +0x10A = bearing to the target, turn toward it (S=4); blocked → set +0x10A = heading ±90° (side
  from the wall direction, 0x4C49C0/0x4C49F0), S=5, and keep turning to it before re-aiming. No
  pathfinding: straight line + 90° wall-follow kick (H for the code, M for "wall-follow").
* **8 hidden**: unlinked from the list, nothing runs.
* No mode ever changes by itself; when the mode-1/4/5 animation ends nothing happens except event 37
  (H: the engine never writes +0x10E; actor defaults for 36/37, vt1F0/1F4 = 0x45ADD0/0x45ADF0, do
  nothing). verid1 templates switch mode/state on 37 (e.g. death: 37 → collision height 0 → hide).

### 2.2 Move with collision, modes 6/7 (0x4C4AE0) (H)
* Flyers (flag 0x1000000): z moves toward target z − height/2 by at most step·(1−k80) (k80 = 1.0
  in verid1, so 0).
* New xy = old + step·k80 along heading. Conveyor offset added (0x4BBFF0).
* Sweep **0x4B9870(from = old pos + radius +0x54, to = new pos + height +0x58, flag 1)** with the
  step-height global 0x57D994 = +0xAC and the active zone mask 0x57D9A4/A8 = the zones listed by
  the actor's floor (+0x74/+0x78), z raised by +0x30 during the test; globals restored after.
  Same routine as the player (player passes flag 0): on a hit it slides; if the slide also hits,
  flag 1 puts the actor **back at its start** (player stops at the first hit) (0x4B98E1).
* Obstacles: walls (as for the player) and other actors with radius != 0 in active zones whose
  [z, z+height] overlaps (0x4B9C37). The player is not an obstacle for actors (not in the list) (M).
  No explicit self-skip in the actor loop (M: presumably harmless because the circle test only
  counts approach; skip self in a reimplementation).
* Returns the hit record (non-zero = blocked); the hit's wall direction (result+0x20/+0x28) picks
  the turn side. Always requests the floor snap.
* Conveyor-only path 0x4BBEF0 (modes 0/1/4/5) sets 0x57D99C/A0 instead of A4/A8 (so it sweeps
  with the player's zones) — original quirk (H).

### 2.3 Ground (0x4C3AC0) (H)
* Walkers: z' = z + step(+0xAC); take the highest floor flat under (x,y) with height ≤ z'
  (0x4B6AB0, grid lists sorted by height, flats with flag 0x1000 skipped); **z = its height**
  instantly. No floor → z unchanged. **No gravity, no falling speed**: an actor walking off a ledge
  drops to the lower floor in one tick if one exists; a rise > step is not climbable (the sweep
  blocks walls above the step, and the floor search won't take a floor above z+step).
* Flyers: keep z, only clamped: going up, head (z+height) stays below the lowest ceiling; going
  down, not below the floor.
* +0x0C = that floor (or dummy); its zone becomes the actor's zone and light.
* SET_POSITION (0x4B64C0) also drops z to the floor below (walkers).

## 3. Proximity (N43) and the AI callback (H unless noted)

* Tables: `0x57BEF0 + (class·17 + target)·20` = r0..r4 (16.16), callback `0x57AA70[class·17+target]`;
  16 classes × 17 targets: target 0 = player, 1..15 = actor class, 16 = non-actor target.
  Defaults (0x4BE483): r0 1000, r1 INT_MIN, r2 0, r3 INT_MIN, r4 0, callback no-op 0x4B7800 → no
  events until a script sets them. Global per class pair, not per actor.
* N43 pops `class, targetClass, r0, r1, r2, r3, r4` (sample `D(100)D(90)D(100)D(90)D(80) 0000 0001`
  = class 1/player: r0 80, r1 90, r2 100, r3 90, r4 100); 0x4576C2 installs cb 0x44F29E.
* **r0 is never read** (only refs: init and setter).
* Player "near" (start of update, actor id != 0): flag 0x10000000; enter when d3 < r3, leave when
  d3 > r4 (d3 = 3-D approx from the player's eye). Counter 0x57A194 (native 0xF0 pushes it).
* Target "near" (if +0x74 set): flag 0x40000000; d2 (2-D) vs r3/r4 of [class][targetClass]
  (targetClass = target actor's class, or 16).
* Player "seen" (end of update): flag 0x8000000; enter when d3 < r1 **and** line of sight
  (0x4BB900: from the actor at mid-body to the player's eye, fails if |dz| > 2·horizontal, walls/
  flats in the actor's floor zones ∪ 0x57D99C/A0 block); leave when d3 > r2 or LOS lost.
  Counter 0x57A178 (native 0xEF).
* Callback `cb(actorId, other, kind)` is called **directly** (events delivered at once, inside the
  engine actor loop, not via the end-of-tick queue). 0x44F29E sends to the actor (vt15C):

| kind | engine meaning | other = 0 (player) | other != 0 |
|---|---|---|---|
| 1 | starts seeing the player (r1 + LOS) | **29** | 28(other) |
| 2 | stops seeing (beyond r2, LOS lost, or frozen) | **31** | 30(other) |
| 3 | came within r3 | **33** | **32**(targetClass) |
| 4 | went beyond r4 | **35** | **34**(targetClass) |
| 0 | never raised by the engine | 26 | 27(other) to actor + 17(actor) to the other actor |

  The engine only raises kinds 1/2 with other=0 and 3/4 with other = 0 or the target's class.
  verid1 handles 29, 31, 35 (35 is also the game's "disengage" event), not 33/32/34.
* verid1 N43 settings: class 1 (80,90,100,90,100), 2 (25,25,30,15,20), 4 (0,0,0,1,4), 7
  (100,80,100,5,10), 8 (95,20,35,20,35), 9 (50,45,50,10,20), 15 (100,80,100,80,100), and a
  second class-1 setting (100,100,120,100,120) — listed as r0..r4, all with target 0 (player).
* **N4E SET_ACTOR_COMBAT_PARAMS** (pops id, mode→+0x10D, target→+0x70, angMin→+0x100,
  angMax→+0x102, d→+0x94 R1, d→+0x98 R2, d→+0x90 R3, d→+0x9C, d→+0xA0): only mode 6 uses them.
  verid1 `D15 D5 D30 D40 D0 180 45 0 1`: R1 0, R2 40, R3 30, walk 5..15 units between turns of
  45..180 heading units, centre = the player (mode 1, target 0 → +0x70 null → player); the `…0 0`
  variant wanders around the home point. The 0x8000/0x80000000 values mean "keep".

## 4. Animation clock and event 37 (0x4C4230) (H)

* Entry e = min(mode, 6) (mode 7 uses entry 6). Load-time fixes (0x4BE3B3): speed 0 → 64,
  count 0 → 1, loopStart ≥ count → count−1.
* `adv = floor(dt·rate·256/1024)` 1/256 frames → **rate·1000/1024 ≈ 0.977·rate frames/s**
  (one frame = 1024/rate ms). Capped at `(count−loopStart)·256 − 1` per tick (this cap also caps
  the walking step).
* Clock +0x106 (8.8). Mode change/sync sets it to 0. If 0 at the start of a tick: (mode 3: fire,
  clock = 1) else clock = adv, no wrap test. Otherwise clock += adv; if frame (clock>>8) ≥ count:
  mode 3 sets clock = 0; if count > 1 queue code 17 (id = mode); then clock −= (count−loopStart)·256
  until frame < count; a result of exactly 0 becomes 1 (except mode 3).
* Code 17 → **event 37** only if rec+0x10E and obj+0x64 still equal the mode that raised it
  (0x435CD8). **No 37 when count == 1.**
* So: first 37 at ≈ count·1024/rate ms after the mode was (re)set, then every
  (count−loopStart)·1024/rate ms. One-shot entries (loopStart = count−1, e.g. mode 1 count 10 →
  9) hold the last frame and repeat 37 every frame-time (1024/rate ms) until the script reacts.
  Mode 3: shot at entry, 37 at the end of each cycle, next shot (and 36) the following tick.
* verid1 examples (count, loop, rate): class 7 mode 0 (1,0,7) no 37; mode 1 pain (10,10→9,10)
  37 after ~1.0 s; mode 3 attack (4,4→3,5) ~0.82 s per shot; mode 4 death (2,2→1,10);
  mode 5 (11,11→10,10) ~1.1 s; walk (5,0,5).
