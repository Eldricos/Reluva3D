# SGEN natives, batch C (less frequent natives used by verid1)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Source: sgens.exe interpreter at 0x44F9D4, case table 0x455A45. Script evidence comes from
verid1/game.bin, parsed with sgen.py `parse()`.

## Conventions

* **Push order**: arguments are listed in the order the script pushes them. The last one
  listed is on top of the stack and is popped first. `id` is always the last value pushed.
* `w` is a 16-bit slot. `d` is a 32-bit value: two slots, normally pushed with PUSH32 (op 6),
  usually 16.16 fixed point. `D(lo,hi)` in the samples is the PUSH32 operand pair, value
  `hi<<16|lo`.
* **"self-or-id" pattern**: many cases pop an object id. If it equals the running object's id
  (`[obj+4]`), they call `obj.vt[x]` directly. Otherwise they call `game.vt[y](id, ...)`, which
  looks the object up with `game.vt[0x148]` and calls the same `vt[x]`. Some natives look the
  id up only as an actor (`game.vt[0x15c]`, which ORs in 0xA000).
* **Object classes** (file type, then class vtable): 1 logic 0x533A50, 2 wall 0x533C88, 3
  0x533EC8, 4 actor 0x534100, 5 0x534378, 6 0x5347F0, 7 floor 0x534A28, 8 billboard 0x5345B0.
  Most actor natives are no-ops (`ret n`) on non-actor classes.
* **Behaviour templates**: the logic objects e0dd, e0de, e050 and e05a use actor natives on
  SELF. These scripts are behaviour templates whose handlers run as an actor, so SELF is the
  actor there.
* **Position block**: DB pushes a 7-slot block, and the PUSH_BLOCK (op 5) literals use the same
  layout. In push order it is `w heading_deg, d z(height), d y, d x`, with x on top. 4D, 75 and
  77 consume exactly this layout. 3D actor record fields: +0x1C x, +0x20 z (height), +0x24 y,
  +0x108 heading.

## Stack-effect corrections for optable.txt / opcodes.cpp

The generator counted pop operations, not 16-bit slots: each dword pop is 2 slots. It also
mis-counted some pushes. The real slot counts below come from the `sub/add [ebp-0x54]` sizes on
one execution path.

| op | table pops/pushes | real pops/pushes |
|----|----|----|
| 4D | 2/0 | 8/0 |
| 4E | 5/0 | 15/0 |
| 50 | 1/0 | 3/0 |
| 43 | 2/0 | 12/0 |
| 75 | 1/0 | 7/0 |
| 77 | 4/0 | 12/0 |
| BB | 4/0 | 10/0 |
| A2 | 0/0 | 4/0 |
| B3 | 0/0 | 2/0 |
| D9 | 0/0 | 2/0 |
| D0 | 1/0 | 3/0 |
| CA | 0/0 | 12/0 |
| DB | 1/1 | 1/7 |
| C7 | 1/2 | 1/1 |

The other natives in this batch match the table.

## Results

### 0x9D PANEL_ADD_BUTTON (6 slots, no result)
* **Push order**: `h, w, y, x, image, button`.
* **Target**: self, which must own an open close-up panel window created earlier by 0x6C.
* **Implementation**: obj.vt[0x148] goes to 0x44950D. That finds the child window of the main
  view (`*0x545EA0`) whose owner (`vt[0x104]`) is obj, then calls panel.vt[0x110] = 0x4487C6.
* **Effect**: creates a button window (0x1A4 bytes, ctor 0x426122) at rect
  (x, y, x+w-1, y+h-1). Its command id is `button | 0x4000 | panel<<16`. Its images are the
  bitmap resource `image`, plus `image+1` as the alternate state, loaded by 0x44959D.
* **Sample**: wall 43a9 opens a panel (`0000 0024 SELF N6C`), then adds buttons 1..5, e.g.
  `002b 002b 0049 007e 0023 0001 N9D` (43x43 at (126,73), image 0x23).
* **Confidence**: high.

### 0x9F PANEL_ANIMATE_BUTTON (5 slots)
* **Push order**: `frameMs, loops, lastFrame, firstFrame, button`. Target is self's panel.
* **Implementation**: 0x449707 finds the panel (0x44960C), then the button (0x4488BD).
  * If `lastFrame == 0` or `lastFrame == firstFrame`, it shows the static frame `firstFrame`
    (vt[0xa8]) and forces `frameMs = 0`.
  * Otherwise it sets the frame range with vt[0xd0], at button +0x178/+0x17C.
  * vt[0xe8]\(loops) sets the frame count `(last-first+1)*loops`; 0 means forever.
  * vt[0xcc]\(frameMs) starts the frame timer at +0x170; 0 stops it.
* **Sample**: `0064 0000 000b 0000 0002 N9F`: button 2 cycles frames 0..11 forever at 100 ms.
* **Confidence**: high.

### 0x31 SUBSCRIBE_BROADCASTS (1: id)
* **Target**: self-or-id. obj.vt[0x44] = 0x44E45D, the same for all classes.
* **Effect**: once only (flag `obj+0xC`), it links obj at the head of the global list
  `*0x545EB8`. Game-wide events (0x4357B9 → 0x44E405 → obj.vt[0x180]) are delivered only to
  objects on this list. Examples of such events: 0xA "health changed", 0x31 "active item
  changed".
* **Sample**: almost every object calls `SELF N31` in its init (ev 2).
* **Confidence**: medium-high.

### Actor behaviour-state setters: 0x54, 0x57, 0x58 (1: id)
* **Target**: self-or-id, actors only. Each one:
  * writes `actor+0x64 = state`;
  * drops any pending target (0x456AD9);
  * marks the actor for update (vt[0x28] queues it on `*0x545EB4`).
* The update (0x456907) copies the state into the 3D actor record byte +0x10E. That state picks
  the AI routine (switch at 0x4C43B4) and the animation slot (+0xC4 + state*8, capped at 6).
* States 1, 4 and 5 use the plain movement/collision code (0x4BBEF0) with no steering. They
  differ only in animation slot.
* **0x54**: state 1, obj.vt[0x9c] = 0x456D1F. The templates use it on "hit" (ev 0x27) and in
  one death handler. Read it as the hit/pain (one-shot) animation. Medium.
* **0x57**: state 4, vt[0xac] = 0x456E22. Used in a death handler (e05a h5: `SELF N57 SELF N7C`;
  ev 0x25 then clears collision). Read it as death animation A. Medium.
* **0x58**: state 5, vt[0xdc] = 0x456E5F. Death handler (e050 h10). Read it as death animation
  B. Medium.
* Related states, for reference:
  * 0 normal (vt[0x84])
  * 2/3 turn toward a target (0x55, 0x56)
  * 6 combat chase (params from 0x4E)
  * 7 go to a point (0x53)
  * 8 hidden (vt[0x80], used by 0x70)

### 0x4E SET_ACTOR_COMBAT_PARAMS (15 slots)
* **Push order**: `d tMax, d tMin, d d3, d d2, d d1, w angMax, w angMin, w target, w mode, id`.
* **Target**: self-or-id, actors only. Actor vt[0xe4] = 0x457703 stores the values; the value
  0x8000 (word) or 0x80000000 (dword) means "leave unchanged". The update calls 0x4B5DF0, which
  writes the 3D record:
  * mode → +0x10D: 1 = target is an actor, 2 = target is a billboard.
  * target index → pointer at +0x70.
  * angMin/angMax → +0x100/+0x102.
  * d1 → +0x94, d2 → +0x98, d3 → +0x90.
  * tMin/tMax → +0x9C/+0xA0.
* Used by the state-6 chase AI (0x4C4670): the distance thresholds decide approach, back off or
  stop. The AI also picks a random strafe angle in [angMin, angMax] and a random time in
  [tMin, tMax] (LCG at 0x57AA68).
* **Sample**: `D(15) D(5) D(30) D(40) D(0) 00b4 002d 0000 0001 SELF N4E`, then N49 or N48.
* **Confidence**: medium (exact meaning of each distance is low).

### 0x4D SET_POSITION (8 slots)
* **Push order**: `w heading_deg, d z, d y, d x, id`, i.e. a position block followed by the id.
* **Target**: self-or-id.
  * Actor: vt[0xec] = 0x457C8A calls `0x4B64C0(idx, x, z, y, heading*65536/360)`. That finds
    the sector at the point, clamps z to it, and sets +0x1C/+0x20/+0x24/+0x108 for the actor
    and all its linked parts.
  * Billboard: 0x4586D0 calls 0x4B6300.
  * Other classes: no-op.
* **Sample**: `002e D(35.1) D(-138) D(-75) SELF N4D` (a001 init). Scripts also do
  `DB(other) SELF N4D` to copy another object's position.
* **Confidence**: high.

### 0x59 SET_HEADING (2: angle_deg, id)
* **Target**: self-or-id, actors. Actor vt[0xf0] = 0x457CEB.
* **Effect**: keeps the current position and re-places the actor with the new heading through
  the same placement routine.
* **Confidence**: high.

### 0x43 SET_PROXIMITY_RANGES (12 slots)
* **Push order**: `d r4, d r3, d r2, d r1, d r0, w targetClass, w class`. No id.
* **Target**: runs on self, and only actors do anything (actor vt[0xe0] = 0x4576C2).
* **Effect**: calls `0x4B6620(class, targetClass, &{r0..r4}, cb=0x44F29E)`. This fills the
  global table 0x57BEF0[class*17 + targetClass] and callback table 0x57AA70.
  * `class` is the actor kind (actor record +0xFC). targetClass 0 is the player; otherwise it
    is another actor's kind.
  * The engine compares distances against these ranges with hysteresis. For example, r3 is the
    "enter" range and r4 the "leave" range (0x4C412D/0x4C40F7). Crossing a range posts events
    3/4, which the callback turns into script events (0x1B, ...).
* **Sample**: a001 `D(100) D(90) D(100) D(90) D(80) 0000 0001 N43`.
* **Confidence**: medium.

### 0x50 SET_COLLISION_RADIUS (3: d radius, id)
* **Target**: self-or-id, actors.
* **Effect**: actor vt[0xfc] = 0x4578D2 writes 3D record +0x58 (all parts). The level loader
  initialises it to width/2 (0x4B6E6B), and it is passed to the collision routine (0x4BBF91).
* **Sample**: the death handlers do `D(0) SELF N50`, so the corpse no longer blocks.
* **Confidence**: medium.

### 0x70 HIDE_OBJECT (2: v, id)
* **Target**: self-or-id.
* **Effect**: actor and billboard vt[0x130] call vt[0x80], which sets state 8. The 3D engine
  (0x4BE2F7) moves the record out of the visible list. `v` is ignored.
* **Sample**: an item billboard hides itself when picked up (`SELF N6E 0000 SELF N70`).
* **Confidence**: medium-high.

### 0x71 DROP_AT (2: where, id)
* **Target**: self-or-id, actors or billboards (vt[0x134] = 0x4567C3 / 0x45858C).
* **Effect**: calls vt[0x84] (state 0, visible/normal). It then moves the object to the
  player's position (`where = 0`: camera 0x57D9B8 minus eye height 0x57D98C) or to actor
  `where`'s position and heading, and relinks its sector (0x4B7AA0).
* **Sample**: inventory items drop themselves at the player (`0000 SELF N71`).
* **Confidence**: medium-high.

### 0xDB GET_POSITION (1: id; pushes 7)
* **Pushes**: `w heading_deg, d z, d y, d x` (x on top).
* **Target**:
  * `id 0`: the player, from camera globals 0x57D9B8/0x57D9BC (minus eye height)/0x57D9C0,
    with the heading from 0x57D9C8.
  * Otherwise self-or-id via obj.vt[0x164]: actor 0x4579CE, wall 0x456276, etc. Logic objects
    leave the values unset.
* **Confidence**: high.

### 0xBE DISTANCE_TO_PLAYER (1: id; pushes 1)
* **Target**: self-or-id.
* **Effect**: obj.vt[0x160] gets the object's x, y, z, and game.vt[0x45C] = 0x4416EF returns
  the rounded Euclidean distance (world units) to the player position game+0xD4/D8/DC. Logic
  objects return 0.
* **Sample**: e05a `SELF NBE 0014 IF_GT`.
* **Confidence**: high.

### 0xF1 SET_ACTOR_TEAM (2: team, id)
* **Target**: self-or-id. The non-self path looks the id up as an actor (game.vt[0x15c]).
* **Effect**: actor vt[0x264] = 0x45769E writes `actor+0xD4 = team`. The hit handlers
  vt[0x1a0]/vt[0x1a4] (0x457B29/0x457B6B) skip damage (vt[0x24c]) when the attacker's team
  equals this one.
  * Logic objects forward to game.vt[0x178]\(self, team).
  * Walls and billboards: vt[0x264] is a getter; meaningless here.
* **Sample**: actors init with `0005 SELF NF1`.
* **Confidence**: medium-high.

### 0x7C AUTOMAP_REMOVE (1: id)
* **Target**: explicit id.
* **Effect**: game.vt[0x3d8] → automap/scanner panel (game+0x2A0, ctor 0x443739) →
  `CMapWordToPtr::RemoveKey(id)` (0x51CB01) on its map +0x258.
* The counterpart 0x7B(category, id) adds the object under one of 8 icon categories.
* The panel (0x444A04) draws mapped objects relative to the player with an 8-way compass.
* **Sample**: dying actors do `SELF N7C`.
* **Confidence**: medium-high.

### 0xCA AUTOMAP_SET_LEVELS (12 slots)
* **Push order**: `d hi2, d lo2, d hi1, d lo1, d hi0, d lo0` (lo0 on top).
* **Target**: none (global).
* **Effect**: game.vt[0x3ec] (automap) then 0x4443D6 stores +0x5C..+0x70. The automap shows
  sectors whose height is in [lo,hi] of the current level index +0x78 (0x44420F). This gives 3
  height bands, i.e. 3 deck levels.
* **Sample**: e001 `D(25) D(12) D(10) D(-2) D(-7) D(-15) NCA`.
* **Confidence**: medium-high.

### 0x84 INVENTORY_ADD (2: slot, id)
* **Target**: explicit id.
* **Effect**: game.vt[0x27c] → game.vt[0x278] → inventory panel (game+0x2B4) 0x446DD0(slot, obj).
  * `slot = -1` takes the first free slot of 1..11.
  * It replaces any previous occupant and draws the item icon (obj+0x44 controller vt[0x18]).
  * The object needs an item controller, created by 0x82(type, a, b, id).
* **Sample**: billboard 2002 `0002 SELF N84 0001 N8C`.
* **Confidence**: high.

### 0x8C SET_ACTIVE_ITEM (1: on)
* **Target**: self, always the running object.
* **Effect**: game.vt[0x3a4]\(on ? obj : 0) = 0x439F9E.
  * It deselects the old active item (game+0x2DC), selects obj, and draws its icon into the
    view panel.
  * It broadcasts event 0x31 with the item.
  * `on = 0` means no item (bare hands).
* **Confidence**: high.

### 0xE9 SET_ITEM_SETTING (2: v, id)
* **Target**: self-or-id (generic lookup).
* **Effect**: obj.vt[0xc4] calls `obj+0x44 controller.vt[0x28](v)`.
  * For most controller types that is 0x449E2C, which stores +0x24.
  * vt[0x2c] reads it back; the game passes it to the view panel (vt[0x134], view +0xAC) when
    the item is used. The value looks like a power/setting level, not the charge.
* **Sample**: 12 billboards do `82(6,5,5)` then `012c SELF NE9`.
* **Confidence**: low-medium.

### 0xC7 GET_ITEM_CHARGE (1: id; pushes 1)
* **Target**: self-or-id.
* **Effect**: obj.vt[0xc0] returns controller.vt[0x3c], the current charge +0x14. Using the
  item subtracts its per-use cost from it (0x449F5A).
* **Sample**: `2002 NC7 00f0 IF_LT`.
* **Confidence**: medium-high.

### 0xB4 ADD_ITEM_CHARGE (2: delta, id)
* **Target**: self-or-id.
* **Effect**: obj.vt[0xbc] = 0x44E952 sets `charge = clamp(charge + delta, 0, max)`, with max
  from vt[0x44] and the write through vt[0x40].
* **Confidence**: high.

### 0xC6 GET_HEALTH (pushes 1)
* **Effect**: returns game+0x200, the player health (16 bits).
* **Confidence**: high.

### 0x89 ADD_HEALTH (1: amount)
* **Effect**: game.vt[0x354] = 0x43A5BA.
  * health += amount, clamped to [1, max health game+0x204].
  * Updates the health bar (vt[0x35c] → game+0x2A8 widget).
  * If `amount != 0`, broadcasts event 0xA(1, health).
* **Sample**: `0064 N89` (heal 100).
* **Confidence**: high.

### 0xC3 SET_BODY_IMAGE (2: frame, image)
* **Effect**: game.vt[0x458] = 0x441639. It loads bitmap resource `image` (0x44959D); `-1`
  means the default "BODY.BMP". It shows it in the HUD body/status window (game+0x2A4) via
  vt[0xa4]\(image+2, bmp, 3), then selects `frame` (vt[0xa8]).
* **Sample**: e004 picks image 0xA/0xB/8/-1 from the health value (C6).
* **Confidence**: medium-high.

### 0x77 PUSH_PLAYER (shockwave/knockback, 12 slots)
* **Push order**: `w tEnd, w tHold, w tRamp, d strength, w (ignored), d z, d y, d x`. The last 4
  are a position block; its heading word is dropped.
* **Effect**: game.vt[0x344] stores the values at game+0x1C0..0x1D8 and arms +0x1E0. Every
  frame (0x43B447) it computes an envelope from the elapsed time:
  * ramps up until tRamp ms, holds until tHold, ramps down until tEnd;
  * the amplitude is `strength` (negative means away from the point).
  * Then it turns the camera toward (x, y) (0x4B6790), moves the player along the heading by
    the amount (0x4B6690), and turns back.
* 0x78 (game.vt[0x348]) cancels it.
* **Sample**: wall 4005 `00fa 0064 0000 D(-8) BLK(pos) N77` (force-field push).
* **Confidence**: medium-high.

### 0x75 TELEPORT_PLAYER (7 slots)
* **Push order**: `w heading_deg, d z, d y, d x`, i.e. a position block.
* **Effect**: game.vt[0x13c]\(x, y, z, heading, pitch = 0x7FFF, meaning unchanged) = 0x43E791.
  It sets game+0xD4/D8/DC and +0xE0, then the camera (0x4B6340, 0x4B6400).
* **Sample**: `BLK(009f,0,0,0,0x31,0,-16) N75` (heading 159, z 0, y 49, x -16).
* **Confidence**: high.

### 0xB3 SET_EYE_HEIGHT (2: d h)
* **Effect**: game.vt[0x430]. It stores game+0x18C = h. When not crouched (+0x8C == 0) it also
  sets +0x188 = h, +0x190 = h + headroom, and the engine eye offset 0x57D98C = h.
* **Sample**: `D(6.0) NB3`.
* **Confidence**: high.

### 0xA2 SET_PLAYER_STEP_HEADROOM (4: d step, d headroom)
* headroom is on top.
* **Effect**: game.vt[0x434]: game+0x104 = headroom (added to the eye height to give the head
  height), game+0x108 = step. `0x4B5EE0` sets 0x57D994 = step, the maximum step-up height used
  by collision (default 2.0).
* **Sample**: `D(2.5) D(1.0) NA2`; floor trigger c008 sets the step to 0.5.
* **Confidence**: medium-high.

### 0xD9 SET_PLAYER_WIDTH (2: d width)
* **Effect**: 0x57D998 = width/2, the player collision radius (default 1.2, 0x4BE222). It is
  copied into the collision box at 0x57D9E8 (0x4B46F6).
* **Sample**: c008 `D(3.5) ND9`.
* **Confidence**: medium-high.

### 0xB5 LOW_GRAVITY (1: on)
* **Effect**: game.vt[0x420] = 0x440C72.
  * `on`: the vertical-physics block `*0x57D950` becomes {0, 10.0, 0, 0, 0}.
  * `off`: restores the defaults from game+0x10C, which are {64.4 (gravity, 2x32.2), 200.0
    (max fall speed), ...}. The block is used by the fall code at 0x4B4824.
  * Also sets game+0x148 = on, which keeps the control mask active during cutscene lock (see
    AE).
* **Sample**: e009.
* **Confidence**: medium.

### 0xAE SET_PLAYER_CONTROLS (1: mask)
* **Effect**: game.vt[0x414]: game+0x1E8 = mask. The effective mask +0x1E4 = mask, unless
  (+0x148 == 0 and +0x1A4 != 0), in which case it is 0.
* The default is 0x5F (0x432FD3). Bits 1, 2, 4, 8, 0x10, 0x20 and 0x40 gate player
  input/actions in the input handler (0x43B0D0..0x43D558).
* **Sample**: values used are 0x1F, 0x06, 0x17, 0x5F.
* **Confidence**: medium (individual bit meanings unknown).

### 0xA5 GAME_MODE_CMD (1: cmd)
* **Effect**: game.vt[0x40c] = 0x440937, a switch on cmd:
  * 2: game.vt[0x370]. Restores the normal HUD (main window 0x288 to state 0, re-enables HUD
    panels).
  * 3: game.vt[0x364]\(0). Puts window 0x288 in state 3 and disables the other HUD panels; also
    plays a sound.
  * 4 / 5: automap +0x9C = 1 / 0 (automap enable).
  * 6 / 7: game+0x160 = 1 / 0. When set, player velocity is zeroed on event 0x38 (0x43D6E8).
  * 8: stop the player (+0xF8, +0x100, +0xFC = 0; +0xE4/+0xE6 = 0x8000).
  * 9 / 10: automap +0x2F4 = 3.0 / 0.
* **Sample**: e001 `0009 NA5` and `0008 NA5`; billboard 2001 uses 2/3.
* **Confidence**: medium for the dispatch, low for the meaning of 2, 3, 9 and 10.

### 0x8E VIEW_SHAKE (1: mode)
* **Effect**: game.vt[0x3f0] → 3D view window (game+0x274, class 0x531610) vt[0x108] =
  0x4313DD stores mode +0x414. Per frame (0x4314A0):
  * 0: no shake; offsets are reset.
  * 1: random ±4 px jitter of the view (+0x404/+0x40C, random 0x40E1D0).
  * 2: ±8 px.
  * 3: nothing.
  * 4: a decaying vertical bounce, a jolt (+0x418 = 9999, then 20, then damped).
* **Sample**: e009 `0004 N8E`.
* **Confidence**: medium-high.

### 0xB0 VIEW_OVERLAY (1: on)
* **Effect**: game.vt[0x41c] → view vt[0x11c]: view+0x41C = on. While on, the view blits a
  vertically scrolling overlay surface each frame (+0x420 scrolls by the frame time and wraps
  at 0x2A00; 0x430B03).
* Looks like a scan-line or interference effect.
* **Confidence**: low-medium.

### 0xBB SCREEN_FLASH (10 slots)
* **Push order**: `d fadeOut_ms, d hold_ms, d fadeIn_ms, w percent, w b, w g, w r`.
* **Effect**: view = game.vt[0x450] (game+0x274); `view.vt[0x124]\(1, r, g, b, percent, fadeIn,
  hold, fadeOut, 0)` = 0x431772. This sets colour-overlay channel 1 (records at +0x4A4,
  stride 40) with level = percent*128/100 and the envelope.
* **Sample**: e005 `D(400) D(0) D(0) 0064 0000 0000 00b4 NBB` (red damage flash);
  e009 `... 003c 00ff 00ff 00ff NBB` (white 60%).
* The colour component order (r, g, b) is assumed.
* **Confidence**: medium-high.

### 0xBC SCREEN_TINT (4 slots)
* **Push order**: `percent, b, g, r`.
* **Effect**: the same routine on channel 3, with all times 0, so the hold is -1 (permanent).
  `0 0 0 0` clears it.
* **Sample**: e003 `0064 00ff 00ff 00ff NBC` (white-out), later `0000 0000 0000 0000 NBC`.
* **Confidence**: medium-high.

### 0xC4 SET_PUNCH (bare-hand action, 9 slots)
* **Push order**: `a9 hitVol, a8 hitSound, a7 swingVol, a6 swingSound, a5, a4, a3, a2, a1 image`.
* **Effect**: game.vt[0x460] = 0x44179C.
  * Loads bitmap `image`.
  * view.vt[0x128] = 0x431D25 sets up the first-person action overlay animation: bitmap, a2,
    a3 (timing/frames, +0x96C/+0x974), and the swing sound a6 at volume a7 (+0x978).
  * game+0x260 = a4 and +0x264 = a5 replace the item's vt[0x38]/vt[0x58] parameters when no
    item is held (+0x25C, set by the punch action game.vt[0x3c4]).
  * game+0x268 = a8, the sound played on a hit, at volume a9.
* **Sample**: e001 `0000 ffff 003f 0015 0009 0003 0003 0064 0007 NC4`.
* **Confidence**: medium (the roles of a2..a5 are low).

### Sound natives (sound manager `*0x53FE00`, asndmgr.cpp)
* **0x6A PRELOAD_SOUND (1: n)**: 0x41F16A → 0x41C5DE. Creates the audio buffer for sample
  table entry n (AudioCreate); nothing is played. e001 preloads a list at init.
  Confidence: medium-high.
* **0x6B STOP_SOUND (1: h)**: 0x41F463. It takes a 32-bit sound reference: high word -1 means
  "all instances of sample low word" (0x41C93A); a high word greater than 0 means voice
  handle-1 (0x41C8D0, which calls AudioStop).
  * The script value is sign-extended from 16 bits. Ids 0..0x7FFF (all the uses in verid1:
    0x25, 0x1B, 0x11, 0x30) have high word 0, and the original then does nothing.
  * Confidence: high for the code path.
* **0xD0 SET_SOUND_VOLUME (3: w volume, d sound)**: 0x41ED07.
  * For a plain sample index (high word 0) it sets that sample's default volume (entry +4,
    0..64).
  * Otherwise it sets the playing voice's volume (AudioSetVolume, curve at 0x41E90B).
  * Sample: `0032 D(0x56) ND0`. Confidence: high.

### 0xF2 GET_ARRAY (1: i; pushes 1)
* **Effect**: pushes the word at 0x56F844[i]. This is a second global word array, separate from
  the GET_GLOBAL table `*0x545EB0`. The setter is 0xF3 `(value, i)`, with i on top.
* **Sample**: e001 `0002 NF2`.
* **Confidence**: high.

## Other findings
* The case-dispatch style is uniform: if `id == [obj+4]`, call obj.vt[x]\(args); otherwise call
  game.vt[y]\(id, args), which looks up with game.vt[0x148] or game.vt[0x15c] (actor-only).
* Game sub-objects:
  * +0x274: 3D view window (vtable 0x531610).
  * +0x2A0: automap/scanner.
  * +0x2A4: body image window.
  * +0x2A8: health bar.
  * +0x2B4: inventory (12 slots).
  * +0x2C0: bitmap resource container.
  * +0x2DC: active item.
  * Player position: game+0xD4 (x) / +0xD8 (y) / +0xDC (z); heading +0xE0.
