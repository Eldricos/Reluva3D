# SGEN natives, batch B (sgens.exe)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Natives analysed: B1 47 72 45 7B 6E 49 48 4F 6C 85 F3 B7 36 55 9D 82 BA 53 86 B2 87 EC 56 5A 44.
The handler addresses come from a dump of the interpreter's case table. Arguments came from the verid1 script
(disassembled with a working script, not kept).

## Conventions used below

* **Push order.** Arguments are listed in push order: the first one listed is pushed first, the
  last one is pushed last and sits on top. Natives pop the top first. A **dword** argument takes
  2 slots. It is what `PUSH32` (op 6) pushes, low word first, and the native reads it as one
  little-endian 32-bit value (`sub [ebp-54h],4; mov eax,[eax]`).
* **Stack-effect fixes for optable.txt.** The generator counted only `sub [ebp-54h],2`, so it
  missed the dword pops. The correct counts are:
  * `44`, `4F`, `5A` pop **3** slots (id + dword), not 1.
  * `B2` pops **10** (4 words + 3 dwords), not 4.
  * `BA` pops **11** (5 words + 3 dwords), not 5.
* **id dispatch.** Many handlers use the same pattern: `if (id == obj->id /*obj+4*/) obj->vt[X]\(args);
  else game->vt[Y]\(id, args)`. `game.vt[Y]` is a one-line forwarder:
  `o = game->vt[0x148](id); o->vt[X](args)`. `game.vt[0x148]` (0x437DB8) is `CMap::Lookup` on
  game+0x9C (id -> script object). It returns NULL for an unknown id, and the forwarders do not
  check for NULL. `game.vt[0x15C]` (0x437EFD) is the same lookup with `id |= 0xA000` (actors only).
* **Classes.** Script type 1 is 0x533A50 (logic; most actor methods are empty `ret`). The others
  are type 2 0x533C88 (wall), 4 0x534100 (actor), 7 0x534A28 (floor/flat), 8 0x5345B0 (billboard).
  "Actor record" means the 3D engine's 276-byte actor record (`0x4B5070(id & 0x1FFF)`), linked
  by record[0] to its other parts. Actor setters write every part.
* **Inheritance (important, see B1).** An object can name a parent object. Event handlers it lacks
  are taken from the parent's script, but **they run with self = the inheriting object**. This is
  why verid1's type-1 "template" objects (0xE0DD/E0DE/E050/E05A/E193/E194) contain actor and
  billboard natives on `PUSH_SELF`. That code really runs on actors and billboards.

---

## Per-opcode results

### B1 — SET_PARENT (inherit event handlers) — high
* Args: `parentId`. Self only.
* Effect: `obj+0x48 = parentId ? game.vt[0x148](parentId) : NULL` (0x4539B4).
* Used by the event dispatcher `obj.vt[0x180]` = 0x44F4E1:
  * It looks up key `(event<<8)|self.state` in `cur->vt[0x230]()`'s handler map.
  * If the key is missing, it sets `cur = cur->+0x48` and repeats.
  * When it finds a handler, it runs it with `this = self` (`mov ecx,[ebp-30h]; call 0x44F9D4`).
* verid1: actors call B1(0xE05A/0xE0DE/0xE050/0xE0DD); billboards call B1(0xE193/0xE194).
* Returns nothing.

### 47 — SHOW / ACTIVATE (actor: idle mode 0) — high
* Args: `id` (self or other).
* What it does depends on the target class:
  * wall: `0x4B5F30(idx,1)` sets 3D flag 0x100 (visible) on the wall and its linked parts.
  * flat: `0x4B6070(idx,1)`, the same for floor/ceiling flats.
  * billboard: `obj+0x68=1`, then marks the object dirty (vt[0x28]); the update applies visibility.
  * actor (0x456BBC): AI mode `obj+0x64 = 0` (idle/stand, animation entry 0), clears the target,
    then marks dirty.
  * logic objects: no-op.
* Every mode other than 8 links the actor into the world (`0x4B7730`, flag 0x100). Mode 8 is set
  by 0x46 (HIDE) and calls `0x4B7790`.

### Actor AI modes (needed for 47/48/49/55/56)
* How the mode is applied:
  * `obj+0x64` holds the mode. The actor-object update (vt[0x18], 0x456907) copies it into
    record+0x10E (byte) and resets the animation timer +0x106.
  * The target pointer `obj+0x70` goes into record+0x74.
  * `0x4B5090` re-links the actor into the world, or unlinks it for mode 8.
* Animation:
  * Each record has 7 animation entries of 8 bytes at +0xC4+mode*8, one per mode 0..6.
    Mode 7 uses entry 6.
  * Entry fields: +2 rate (0xC6), +3 last frame, +4 first frame.
  * Playback is in 0x4C4230: `timer(8.8 frames) += rate*dt_ms/4`, about rate x 0.98 frames/s.
* Per-mode behaviour (switch at 0x4C43B4, table 0x4C4448, cases for modes 2..7):
  * 0, 1, 4, 5: stand in place with the mode's animation (gravity/collision only, 0x4BBEF0).
  * 2: turn toward the target (0x4C45C0). Turn speed is record+0x104.
  * 3: turn toward the target and fire at the end of each animation cycle (0x4C4460). The
    projectile comes from the weapon object at record+0x7C (`vt[4]`), and engine event 0x12 is posted.
  * 6: autonomous wander (0x4C4670). It moves about its home point record+0xB0 (or a guard target
    when record+0x10D is set). It uses the distance bands record+0x90/0x94/0x98 and random heading
    changes (record+0x100/0x102 angle range, +0xA4 interval). Most actors start in mode 6.
  * 7: walk to the target (0x4C4A10). The target is an actor, a billboard or a point. With no target
    it walks to the **player** (0x4C3F70 falls back to the camera position 0x57D9B8). It posts engine
    event 0x10 when `distance < record+0x28`.
  * 8: hidden (removed from the world).
* Engine events reach the game through the callback 0x4357E5, then game.vt[0x16C] (0x435CD8):
  * 0x10 becomes script event **36** (0x24, "arrived/facing done").
  * 0x11 becomes script event **37** (0x25, "animation cycle done").
  * Both are sent only while the actor is still in the mode that raised them.

### 48 — ACTOR_WANDER (mode 6) — high (mechanism) / medium (name)
* Args: `id`. Actor only; no-op for other classes (0x456BF9).
* Effect: mode 6, target cleared.

### 49 — ACTOR_APPROACH_PLAYER (mode 7, no target) — high
* Args: `id`. Actor only (0x456C36).
* Effect: mode 7 with target cleared, so the actor walks to the player and raises event 36 when
  within record+0x28.
* Related setters for the same mode: 0x4A (target actor), 0x4B (target billboard) and 0x4C (point).

### 55 — ACTOR_ATTACK — high
* Args: `targetId, id`. targetId is an actor id; 0 means the player.
* Actor only (0x456D5C).
* Effect: mode 3. `targetId != 0` gives `obj+0x70 = actor record(targetId & 0x1FFF)`.
* verid1 always uses `0, SELF`, i.e. shoot the player.

### 56 — ACTOR_FACE — high
* Args: `targetId, id`. Same argument handling as 55.
* Actor only (0x456DBF).
* Effect: mode 2, turn toward the target or the player. Event 36 fires when it faces the target.

### 45 — SET_ACTOR_WEAPON — medium
* Args: `weaponType, id`. Actor lookup via game.vt[0x15C], so a bare actor number or an 0xA000 id
  both work.
* Effect (0x4575F1):
  * `w = (weaponType == -1) ? NULL : game->+0x78[weaponType]` (game.vt[0x46C] = 0x4404A0).
  * Every part's record+0x7C is set to w. That is the projectile spawner used by mode 3.
  * `obj+0x84 = w->+0x18->+0x4C`, a weapon property (probably damage), or 0.
* verid1 values: 1, 2, 8, 9, 10, 11, picked at random per actor.

### 44 — SET_ACTOR_STEP_HEIGHT — medium-high
* Args: `dword height (16.16), id`. Actor only (0x457830).
* Effect: record+0xAC = height on all parts.
* The collision code (0x4BBF4C) copies it into 0x57D994, the maximum step-up height; the player's
  value is 2.0. The test is at 0x43D255: blocked if `floor rise > 0x57D994`.
* File default 3.0. verid1 uses 1.5, 2.5, 3.0, 0, 0.5.

### 4F — SET_ACTOR_WIDTH — medium-high
* Args: `dword width (16.16), id`. Actor only (0x457881).
* Effect: record+0x54 = width/2, the collision radius passed to the collision routine (0x4BBF8E,
  together with height +0x58).
* verid1 uses 3.0, 2.5, 3.5, 0.

### 5A — SET_ACTOR_REACH_RADIUS — medium
* Args: `dword dist (16.16), id`. Actor only (0x457DCB).
* Effect: record+0x28 = dist, the "arrived" distance for mode 7 (0x4C4A1E). File default is the
  same as the radius.
* verid1 uses 6.0, 4.0, 500.0.

### 53 — SET_ACTOR_ANIM_RATE — high
* Args: `rate, mode`. Self only; no id argument.
* Effect: `obj.vt[0x238](mode, rate)` gives (actor, 0x457E19) `0x4B5DB0`. That sets
  record+0xC6+mode*8 = rate (byte) on all parts.
* verid1: (10,6), (5,3), (4,3), (0,6), (200,6). File default is 10.
* **Only valid for actors.** The logic, wall, floor and billboard vtables have no slot 0x238
  (0x533A50 ends at +0x234), so on those classes the call reads the next class's destructor.
  verid1 only runs it through inheritance from actors. A reimplementation should treat it as a
  no-op for non-actors.

### EC — SET_FULLBRIGHT — medium
* Args: `on (0/1), id`. Self or a looked-up object. There is no NULL check on the lookup.
* Effect: 3D object flags +0x6C, bit 0x10 = on&1, on every part. Implementations:
  * wall 0x456336 (`0x4B7940`)
  * actor 0x457A67
  * flat 0x4593DA (`0x4B7600`)
  * billboard 0x45871A (`0x4B5100`)
  * logic: no-op
* The only reader found, the renderer at 0x4C5A0A, uses light level 0 (full bright) instead of the
  surface's fixed level +0x68 when the bit is set.
* verid1 toggles it on enemy actors via the E0DD/E0DE templates (glow while hit/firing?).

### 36 — SET_SURFACE_TEXTURE (texture animation) — high
* Args: `period, frames, texture, id`. Self or other.
* Wall, flat and billboard classes only (0x45B260); no-op for logic and actors.
* Effect:
  * Stores obj+0x5C/5E/60, then on update (wall 0x456075 calls `0x4B60B0`):
    * texture (+0x96 on walls) = texture
    * frames (+0x82) = frames (0 is treated as 1)
    * speed (+0x80) = 0x4000/period (0 is treated as 1)
    * phase reset, start time = engine clock 0x57A034 (ms)
  * The frame is `((now-start)*speed>>16 + phase) % frames`, so one frame lasts 4*period ms.
  * File walls store speed 128 (period 128, 512 ms/frame). Note: this contradicts the repo's fixed
    125 ms per frame.
  * Floors use `0x4B61D0`, billboards `0x4B6140`/`0x4B5FC0`.
* verid1: `0,1,513,SELF` (set a static texture); `128,2,508,0x43A5` (animate 2 frames).

### 6C — AUX_SHOW_IMAGE — high (mechanism) / medium (name "aux panel")
* Args: `frame, image, id`. image is an index into the script's resource list, section 1.
  verid1 frame is always 0.
* Effect: obj.vt[0x124] (all classes 0x44EC8A) calls `0x448C90(mgr=*0x545EA0, obj, image, frame)`.
  * It creates or reuses this object's window in the AUX viewer manager. The window is 0x1CC bytes,
    class 0x532928.
  * It loads `dir + resname[image]`, or the name after a leading `*` (0x44959D), and calls
    `vt[0xA8](frame)`.
  * It makes that window the visible one and hides the others.
  * When two or more entries exist it enables the cycle button (mgr+0x60, `vt[0xCC](0x21,300)`).
* verid1 images: 1 HYPSPRAY, 2 PHASER, 3 TRICRDR, 5 AUXCLOAK, 12 RSORAN, 24 DISRPAUX, 25 LBATAUX,
  27 KMDEAD, 28 PHARBATT, 29 KIRKDIES, 30 BLANK, 36 auxhench.
* An image index with bit 0x8000 set skips creating the window.

### 6E — AUX_REMOVE — high
* Args: `id`. obj.vt[0x12C] (0x44ECF2) calls `0x449138(mgr, obj)`.
* Effect: destroys this object's AUX window and decrements the count, then updates the
  visible/cycle state.

### 9D — AUX_ADD_BUTTON (hotspot) — high
* Args: `h, w, y, x, image, index`. Self only; it uses self's AUX window.
* Effect: `0x44950D`, then window `vt[0x110]` (0x4487C6).
  * Creates a child button (0x1A4 bytes) with rect (x, y, x+w-1, y+h-1) and id
    `(owner<<16)|(index|0x4000)`.
  * Its image is resource `image`, loaded in mode 3.
* verid1 examples:
  * wall 0x43A9: index 1..5 with images 35..31 (auxh05..auxh01).
  * actors: indices 100..102 with images 13/14/15 (BLUE/GREN/REDTIME).

### 72 — TRIGGER (send event 45) — high
* Args: `id`. Self or other.
* Effect: obj.vt[0x138] (all classes 0x44EE23) calls `vt[0x180](45, 0, 0)`, a **synchronous**
  dispatch of event 45 (0x2D) to the target. Handlers are inherited (see B1).
* Event 45 is the generic "activate" message; it has handlers on every object type in verid1.

### 7B — SET_MAP_MARKER — medium-high
* Args: `style (0..7), id`. Pops: id on top, then style.
* Effect: game.vt[0x3D4] (0x44067A) calls `0x444433` on the automap/tricorder-map subsystem
  (game+0x2A0).
  * `map258[id] = &style[style]`. The 0x24-byte style records sit at +0xF0, 0x138, 0x114, 0x1A4,
    0x1C8, 0x1EC, 0x210, 0x234 for styles 0..7.
  * Other style values are ignored.
  * The map renderer 0x444A04 draws each object in map258 (0x444E6E) with its style.
* verid1: actors use 1/3/4/7; wall 0x43A9 sets actor 0xA06C to 4.

### 86 — SET_ZONE_ON_MAP — high (mechanism)
* Args: `on, zone`. zone is on top; 0 means all.
* Effect: `0x4B5220(zone, on)`. For each wall (0x57A3C4, stride 0x98) whose zone mask
  (+0x3C/+0x40) contains bit `zone` (bit 0 always included), it sets or clears wall flag **0x800**.
* The automap (0x444C34/0x444C6B) draws only walls with 0x800 whose height range covers the
  player. So this reveals or hides map areas.
* verid1: floor trigger 0xC039 clears zones 2,3,4,6,13,18..21,25..27,45,52,53.

### 82 — ATTACH_ITEM_BEHAVIOUR — medium
* Args: `c, b, kind, id`. Self or other.
* Effect: `obj+0x44 = 0x449918(kind, b, c, obj)`. The old handler is not freed.
* It is a factory switch on kind (table 0x449C33):
  * kind 1: simple item (0x449CBE, item code b at +4).
  * kinds 2-6 and 11-13: items with an energy charge, created via `0x449DCD(cur,cost,max,rate,-1)`:
    * kind 2: 320/16/320/4
    * kind 3: 200/12/200/4
    * kind 4: 400/16/400/8
    * kind 5: 1200/18/1200/18
    * kind 6: 240/12/240/4
    * kinds 11/12: 100/10/100/10
    * kind 13: 360/9/360/3
  * kinds 9, 10, 14: other classes.
  * kinds 7 and 8: none.
* obj+0x44 is the object's inventory-item handler. Natives B4/C7/B9/CB/CC/E6/E9 (obj.vt[0xBC..0xD8])
  read or modify it, e.g. B4 adds to the charge clamped to [0,max].
* verid1 billboards: `5,5,6` (kind 6) and `0,b,1` (kind 1, b = 0..7).

### 85 — INVENTORY_REMOVE — high
* Args: `id`.
* Effect: game.vt[0x280] (0x4391EF) looks up the object. In the inventory panel (game+0x2B4):
  * It finds the slot whose object is this one (12 slots at +0x1D0, 0x447BCF).
  * `0x447013(slot)` clears the slot (+0x170 = -1, +0x1D0 = 0).
  * It deselects the slot if it was selected or in use (+0x200/+0x204/+0x44C).
  * It redraws the slot.
* verid1's start-up removes items 0x2001.., 0x2009, 0x212C.. .

### 87 — DAMAGE_PLAYER — high
* Args: `type, amount`. The attacker is always self.
* Effect: game.vt[0x34C] (0x43A42B):
  * If 0x5429FC (damage enabled) and `amount != 0`:
    * When `type != 12` and the worn shield item (game+0x2E0, its +0x44 handler) has charge, the
      shield absorbs part of the damage (`vt[0x3C]`). Event 48 goes to the shield object when it
      runs out.
    * Health game+0x200 -= the remainder, clamped to at least 1.
    * `vt[0x35C]` updates the health bar (game+0x2A8, max +0x204).
  * Then, if amount != 0, it broadcasts script **event 9** with args (attackerId, amount, amount,
    type) (`0x4357B9(9,4,..)`).
* verid1: type 13 with amounts 0/1/2/3/10/200; actors use `0, local`.

### B2 — SET_PLAYER_MOTION — high (fields) / medium (exact roles)
* Args (push order):
  1. `dword jump`
  2. `w176`
  3. `w172 turnAccel`
  4. `dword accel`
  5. `w174`
  6. `w170 turnRate`
  7. `dword speed` (pushed last)
* Effect (game.vt[0x42C] = 0x440F23) writes game fields:
  * +0x164 = speed: max walk speed (x2 when running, /2 when slow)
  * +0x170 = turnRate
  * +0x174 = w174: pitch/look rate (guess)
  * +0x168 = accel: per 100 ms
  * +0x172 = turnAccel
  * +0x176 = w176: pitch accel (guess)
  * +0x16C = jump: impulse used by 0x4B6800
* For each value, 0x80000000/0x8000 keeps the current setting and 0x80000001/0x8001 restores the
  default from 0x545334..0x54534C.
* All are read by the player movement update 0x43AD04 (e.g. 0x43B894, 0x43B9CC, 0x43C272, 0x43C2EC).
* verid1:
  * normal: (1.5,15,10,1.0,10,10,20.0) or (1.0,12,10,0.5,10,10,5.6)
  * frozen for cutscenes: (0,0,0,0.49,5000,5000,0.49)

### B7 — SET_SKY_ENABLED — medium-high
* Args: `on`.
* Effect: game.vt[0x448] (0x441587) sets 0x57D834 = on.
* The frame renderer (0x4B5AC2) draws the sky (0x4C2F50) only if 0x57D834 is set and a sky exists.
  The level loader sets the flag to 1 (0x4BFBC0).
* verid1 floor triggers toggle it when you move between indoor and outdoor areas.

### BA — SCREEN_TINT / FLASH — high
* Args (push order):
  1. `dword fadeOutMs`
  2. `dword holdMs`
  3. `dword fadeInMs`
  4. `percent`
  5. `B`
  6. `G`
  7. `R`
  8. `notifyId` (pushed last; 0 = none, self, or an object)
* Effect: `game.vt[0x450]()` returns the 3D view (game+0x274, class 0x531610). Its `vt[0x124]`
  (0x431772) sets palette-effect slot 0:
  * colour (R,G,B), intensity = percent*128/100
  * an envelope: ramp up over fadeIn, hold, ramp down over fadeOut
  * fadeIn = fadeOut = hold = 0 makes the tint permanent; percent 0 clears it
* Each frame 0x43199A blends every palette entry `c = (c*(128-k) + col*k) >> 7`.
* At the end it sends **event 55** to the notify object.
* verid1: white flash (100%, 400/2000/500 ms), dim to black (50%, 4000/18000/1000), orange
  (207,159,26) 75% flash.

### F3 — SET_STATIC_VAR — high (mechanism)
* Args: `value, index`.
* Effect: `word[0x56F844 + index*2] = value`. 0xF2 reads it back (0x455935).
* Unlike GET/SET_GLOBAL (the per-mission array at *0x545EB0), this is a static array in the
  executable, so it keeps its values across missions (no reset was found).
* verid1 indexes 0..4.

---

## Summary table
| op | name | args (push order, last = top) | target | conf |
|---|---|---|---|---|
| B1 | SET_PARENT (inherit handlers) | parentId (0 = none) | self | high |
| 47 | SHOW / actor IDLE (mode 0) | id | id | high |
| 72 | TRIGGER (sync event 45) | id | id | high |
| 45 | SET_ACTOR_WEAPON | weaponType (-1 none), id | actor id | medium |
| 7B | SET_MAP_MARKER | style 0..7, id | id | med-high |
| 6E | AUX_REMOVE | id | id | high |
| 49 | ACTOR_APPROACH_PLAYER (mode 7) | id | actor | high |
| 48 | ACTOR_WANDER (mode 6) | id | actor | high/med |
| 4F | SET_ACTOR_WIDTH (radius = w/2) | dword w, id | actor | med-high |
| 6C | AUX_SHOW_IMAGE | frame, image, id | id | high |
| 85 | INVENTORY_REMOVE | id | item id | high |
| F3 | SET_STATIC_VAR | value, index | — | high |
| B7 | SET_SKY_ENABLED | on | — | med-high |
| 36 | SET_SURFACE_TEXTURE | period, frames, texture, id | wall/flat/billboard | high |
| 55 | ACTOR_ATTACK (mode 3) | targetId (0 = player), id | actor | high |
| 9D | AUX_ADD_BUTTON | h, w, y, x, image, index | self | high |
| 82 | ATTACH_ITEM_BEHAVIOUR | c, b, kind, id | id | medium |
| BA | SCREEN_TINT/FLASH | dword out, dword hold, dword in, pct, B, G, R, notifyId | — | high |
| 53 | SET_ACTOR_ANIM_RATE | rate, mode | self (actor) | high |
| 86 | SET_ZONE_ON_MAP | on, zone | walls | high |
| B2 | SET_PLAYER_MOTION | dword jump, w176, turnAccel, dword accel, w174, turnRate, dword speed | player | high/med |
| 87 | DAMAGE_PLAYER | type, amount | player (attacker = self) | high |
| EC | SET_FULLBRIGHT | on, id | id | medium |
| 56 | ACTOR_FACE (mode 2) | targetId (0 = player), id | actor | high |
| 5A | SET_ACTOR_REACH_RADIUS | dword dist, id | actor | medium |
| 44 | SET_ACTOR_STEP_HEIGHT | dword h, id | actor | med-high |
