# SGEN natives, batch E (used by amargos1, arvada1, enterp, galorn1/2, halee1)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Same conventions as natives-c/natives-d: H = read from code, M = code plus inference, L = guess.
Push order is first-pushed to last-pushed; the handler pops the last-pushed value first.
"Self or id": the handler compares the popped id with SELF and calls the object's method directly,
otherwise game.vt[...] (id) looks the object up (vt148, exact id) and calls the same method.

## Script object classes (factory 0x43519F, switch on the script's object type)

| type | ids | class vtable | what |
|---|---|---|---|
| 1 | 0xExxx | 0x533A50 | logic |
| 2 | 0x4xxx | 0x533C88 | walls |
| 3 | 0x8xxx | 0x533EC8 | **movers** (doors; engine records via 0x4B7960, map `*0x55207C`) |
| 4 | 0xAxxx | 0x534100 | actors |
| 5 | 0x8xxx | 0x534378 | **lifts** (move a flat group between stored heights) |
| 6 | 0x6xxx | 0x5347F0 | **light groups** (engine records via 0x4B7B80, map `*0x5522B0`) |
| 7 | 0xCxxx | 0x534A28 | flats |
| 8 | 0x2xxx | 0x5345B0 | billboards |

Base class 0x533818: every method below is a no-op unless a class overrides it. Movers are
built at level load (0x4B7CA0) from walls whose flags have 0x2000 and that carry a mover id
(36-byte records; state +0x1C, position +0x18, flags +0x20). The engine port has no movers yet.

## Results

### 0x27 DO_LOG (1: w value)
* printf "DO_LOG %d" to the debug log (0x47A8AF; only when logging is on). H.
* Sample: galorn2 14683 `1 N27`.
* Implement: pop and ignore (or debugC).

### 0x28 DO_LOG_STACK (1: w n) — **slot count bug**
* Pops n, then logs the top n stack words **without popping them** (it saves and restores the stack
  pointer, 0x450291). "stack is empty" if nothing is there. H.
* opcodes.cpp says 2 pops: wrong, it must be **1**. Sample: galorn2 25211
  `GET_LOCAL 11, PUSH 2, N28, SUB` — the SUB uses the two words N28 only logged.
* Implement: pop n only; fix the table entry to 1 pop.

### 0x29 DO_LOG_LOCAL (1: w index)
* Logs "DO_LOG_LOCAL %s %d (%d) == %d" (object name, id, index, local[index]). H.
* Implement: pop and ignore.

### 0x2A DO_LOG_GLOBAL (1: w index)
* Logs "DO_LOG_GLOBAL (%d) == %d" with global[index] (`*0x545EB0`). H.
* Implement: pop and ignore.

### 0x37 RESET_TEXTURE (1: w id) — self or id
* vt4C. Walls, flats and billboards (0x45B2D0): vt48(+0x5A, 1, 0) = SET_SURFACE_TEXTURE with the
  texture saved at construction (+0x5A = the file's), 1 frame, speed 0. RESET_DEFAULT (0x2D, vt34)
  reaches the same method for these classes. Others: no-op. H.
* Sample: amargos1/halee1 `<id> N37` after texture animations.
* Implement: same as 0x2D (restoreTexture(id)).

### 0x39 MOVER_OPEN (1: w id) — self or id
* Type 3 only (0x45B440): obj+0x5C = 2, queue sync (vt28); the sync (vt18 0x45643E) writes it to
  the mover record's state +0x1C: **2 = start opening**. H (code), M (meaning from events.md: 2/6
  start opening, 1/5 open, 4/7 start closing, 3 closed).
* Sample: `0x80xx N39` in many missions (most-used of this batch with 3A).
* Implement: needs the mover (door) system; until then ignore.

### 0x3A MOVER_CLOSE (1: w id) — self or id
* Type 3 (0x45B470): state 4 = start closing. H/M as 39.
* Implement: with movers.

### 0x3E MOVER_SET_OPEN (1: w id) — self or id
* Type 3 (0x45B4A0): state 1 = fully open at once. H/M.

### 0x3F MOVER_SET_CLOSED (1: w id) — self or id
* Type 3 (0x45B4D0): state 3 = closed at once. H/M.
* (0x3B, vt60 0x456586, not in this batch: toggles — opens a closed mover, closes an open one.)

### 0x3C MOVER_LOCK (1: w id) — self or id
* Type 3 (0x456602): 0x4B5BA0(id & 0x1FFF, 1) sets bit 0x2 of the mover record's +0x20. Bumping a
  wall that belongs to a mover (0x4B94D0) only starts it opening when that bit is clear. H.
* Implement: with movers (a locked flag).

### 0x3D MOVER_UNLOCK (1: w id) — self or id
* Type 3 (0x45663D): the same with 0: clears bit 0x2. H.

### 0x40 SET_ACTOR_MAP_MARKER (2: w value, w x) — self only
* Pops x (top, unused by actors) then value; obj.vt74(x, value). Actors (0x457163): +0x58 = value,
  and for value 1..9: +0x5C = style and game.vt3D4(id, style) — the same call as SET_MAP_MARKER
  (0x7B). Styles: value 1 or 2 → 0, value k → k − 2 for 3..9 (0..7: romu, sorn, injr, klng, chod,
  fish, fuzy, pred). Other values only store +0x58. Other classes: no-op. H.
* Samples: halee1 actors `9 6 N40` (style 7), many `5 1`/`5 2`/`5 3` (style 3), `1 4`/`3 1`.
* Implement: on an actor self, `_hud.setMarker(self id, kMarkerLife, style)` for value 1..9.

### 0x4A ACTOR_GO_TO_ACTOR (2: w target, w id) — self or id
* Actors (0x456C73): mode 7 (+0x64 = 7), target = the actor record of `target` (0x4B5070, looked up
  without type bits, so 50 means actor 0xA032), +0x68 = 1, sync. Arrival is event 37 as for mode 7.
  Others: no-op. H.
* Sample: galorn1 1321 `50 SELF N4A`, 1402 `54 SELF N4A` (actors 0xA032, 0xA036 exist).
* Implement: `_actors->setMode(id, kModeWalkTo, target | 0xA000)`.

### 0x5B SET_ACTOR_TARGET (2: w target, w id) — actor (self, or id via game.vt15C = actor lookup)
* Actors (0x45705C): target record = actor `target & 0x1FFF` (0x4B5070), +0x68 = 1, sync; the
  mode is not changed (unlike 4A). H.
* Sample: arvada1 16053 `3 SELF INC N5B` (the next actor's target becomes actor 3).
* Implement: set the target of the actor's current mode (Actors needs a setTarget(id, target)).

### 0x5C SET_ACTOR_ANIM_SPEED (4: d speed, w anim, w id) — actor
* Actors (0x4570A8): for every part, animation[anim].speed (record +0xC4 + anim·8, the first word)
  = speed >> 8, i.e. a 16.16 value stored as 8.8. H.
* Samples: galorn1 `PUSH32 0x40000, 6, SELF N5C` (4.0 for animation 6, after SET_ACTOR_ANIM_RATE),
  `PUSH32 0x6666, 6` (0.4).
* Implement: LevelFile::Actor animations[anim].speed = (uint16)(raw >> 8) on the actor's parts.

### 0x5E LIFT_SETUP (18: d h8 … d h1, w count, w flat) — self only
* Pops flat (top), count, then 8 dwords; obj.vt234. Lifts (type 5, 0x4581AD): +0x58 = the flat
  object (game.vt168 = lookup with 0xC000), +0x64 = count, +0x68.. = heights h1..h8 (16.16, the
  first popped dword is h1). Other classes' slot 0x234 is an unrelated getter; only type-5 objects
  call it. H (fields), M (names).
* Sample: halee1 0x8328 on enter: `0 0 0 0 0 0 0x240000 0x141999 2 -15577 N5E` → flat 0xC327,
  2 levels, h1 = 20.1, h2 = 36.0.
* Implement: store per lift id: flat id, heights.

### 0x64 LIFT_GO_TO (2: w level, w id) — self or id
* Lifts (0x4583EA): game.vt440(keep, keep, 0x7FFFFFFF) (fall block +0x10 = 0x7FFFFFFF and
  game+0x13C = 1: re-check the player's footing), then flat.vt118(h[level − 1]): **every flat whose
  group (+0x14) is that flat id gets height (+0x20) = h at once** (0x458FF8, sets the level-dirty
  flag 0x55208C); +0x5C = +0x60 = −1. Others: no-op. H (code), M (no animation found here).
* Samples: amargos1 `1 -32525 N64` / `2 -32525 N64` (lift 0x80F3 to level 1 / 2).
* Implement: set Sector.height of the flats with that objectId, then re-place the walker if it
  stands on one (walker.place(x, y, feet) or a re-check).

### 0x66 SET_LIGHT_LEVEL (2: w percent, w id) — self or id
* Light groups (type 6, 0x45B880): if an effect is running (+0x1C ≠ 0) the value waits in +0x5E;
  otherwise +0x5A = percent and the sync (0x458AD8) writes it to the light record's byte +8 and
  recomputes the levels (0x4B7BA0). Others: no-op. H.
* The group is `id & 0x1FFF` (records `*0x5522B0`); type-6 objects' effects (+0x1C: vt188 pulses
  between 10 and 90 in steps of 10, or flickers 10/90 at random) are set by natives not in this
  batch.
* Samples: amargos1 `30 0x6102 N66` (three groups to 30%), values 30..100 common.
* Implement: `_renderer.setGroupBrightness(id & 0x1FFF, percent)` — the existing API.

### 0x67 RESET_LIGHT_LEVEL (1: w id) — self or id
* Light groups (0x45B8D0): vt11C(+0x5C) = 0x66 with the brightness the record had at load. H.
* Sample: arvada1 `0x6032 N67`, `0x6033 N67`.
* Implement: setGroupBrightness(group, 100) if 100 is the load value (the record's +8; check the
  loader), else the stored initial value.

### 0x73 ENGAGE (1: w id)
* game.vt180 (0x4380FE): if another object is engaged (game+0xC4), it gets event 35 now; then
  game+0xC4 = the object and +0xC8..+0xD0 = the player's position. Every frame (game.vt188 from
  0x43B2E4) the engaged object gets **event 35** once the player is more than 3.0 units from there.
  H (events.md).
* Sample: amargos1 10315 `SELF PANEL_SHOW_IMAGE SELF N73` (a panel that closes when you walk off).
* Implement: Mission keeps engaged id + position; per update send 35 at > 3 units (and clear);
  replacing sends 35 to the old one. (Separate from the Hud's per-panel 2-unit check, which
  is the menu panel's own rule.)

### 0x76 TELEPORT_PLAYER_RELATIVE (14: block to, block from)
* Pops two position blocks: first `from` (pushed last), then `to`. Moves the player by
  (to − from) and turns them by (to.heading − from.heading) degrees, keeping the pitch: the new
  position is P + (to − from) with z taken at the feet (0x452703 → vt13C). H.
* Sample: halee1 45470 on event 14: `BLK(0, z −4, y 134, x 707) BLK(0, z 41, y 94, x 215) N76` —
  steps into a copy of the room 492 units away.
* Implement: from = popPosition, to = popPosition; walker.place(x + dx, y + dy, feet + dz);
  setPlayerHeading(current degrees + to.heading − from.heading).

## Notes
* opcodes.cpp slot counts match the handlers for this batch, except **0x28 (should pop 1)**.
* Movers (39/3A/3C..3F, and 3B) need a door system that isn't in the engine: records from walls with
  flag 0x2000 and a mover id (0x4B7CA0), states advanced by 0x4B8300, events 22..25 (events.md).
