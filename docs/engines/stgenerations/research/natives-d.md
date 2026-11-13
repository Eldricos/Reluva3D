# SGEN natives, batch D (the rest used by verid1; mission flow)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Same conventions as natives-c.md. "Push order" is the order the script pushes; the native pops
the last one first. "d" = dword (2 slots, 16.16 unless noted), "w" = word. Handlers call
game.vt[x]\(p1, p2, ...) with **p1 = the value popped first (the last pushed)**. Slot counts match
opcodes.cpp. Confidence: H read from code, M code plus inference, L guess.

## Mission flow summary

* **Event 6** (game.vtEC 0x43E742, no args) is broadcast when the mission starts; game+0x8C = 1. (H)
* verid1 e001 on 6: TELEPORT_PLAYER to the start, **91(1000,1000,1000)** (briefing → game
  transition), a sound. When the transition ends the game broadcasts **event 7** (no args) and
  e001 on 7 starts the mission proper (signal 666 to e032, stop sound 48). (H for the calls)
* **Objectives:** 99(v) sets the goal mask game+0x94 (default −1 = none), 9A(v) ORs done bits into
  game+0x90, 9B(v) clears bits. The per-frame update game.vtDC (0x43AD04) checks
  `+0x90 == +0x94 && +0x98 == 0`: then it broadcasts **event 8** (no args), sets +0x98 = 1 and
  **host+0x10 = 1** (host = game+0x4C, the away-mission module 0x532578). So host+0x10 is the
  "mission accomplished" flag the campaign reads after the module ends (H for code; M for meaning).
  verid1: `2 N99` at start (one goal, bit 2), `2 N9A` at 14217 (the goal done).
* **Ending the mission:** game.vt114 = 0x43F652 → host.vt18 = 0x443384: hide the cursor,
  host+0x50 = 1 (0x460DC2) — the module stops and returns to its owner. Reached by:
  * **BF** directly (no fade) — verid1's ending (object 126 at 14649, after a 4 s fade to black
    with BA and its event 55).
  * **92** (transition to black, then vt114) — verid1 e001 on event 46 (after stopping the player
    and locking controls) and e00b's sequence (red tint, eye height 4 = collapse, then 92 after
    1750 ms: reads as the death path) (H for the calls, L for what each sequence means).
  * Success vs failure is not passed to vt18; it is host+0x10 (set only via the objective mask).
* The engine never kills the player: DAMAGE_PLAYER clamps health at 1 (natives-b.md); scripts
  decide death (e004 on event 9 at health 1).
* The word array 0x56F844 (F2/F3 GET/SET_ARRAY) is a static, outside the game object, so it
  outlives the mission: e001 on 46 stores health (`C6 0 F3`) and the phaser charge or 666 in
  slot 2 before ending (M: campaign state carried between missions).

## Results

### 0x91 BRIEFING_TRANSITION (6 slots: d t3, d t2, d t1)
* **Effect**: game.vtF4 0x43D8FF(p1 = t1, t2, t3): +0x320 = t1, +0x324 = t1+t2, +0x328 = t1+t2+t3
  (phase ends, ms). Builds the "from" picture +0x2F4 from the mission's **briefing.bmp**
  (mission folder, game+0x5C → +0x44) with **proceed.bmp** drawn on it, and the "to" picture
  +0x2F8 by rendering the game screen (vt474(0)). Sets +0x2FC = 1 (busy), +0x304 = 0 (clock),
  **+0x308 = 0**, +0x31C = 0.
* The per-frame transition (game.vt10C 0x43E13A, run while +0x2FC): on the first frame it plays
  sound +0x334 if ≠ −1 (0x41F1A4, set by 0x96) and sets +0x31C = 1; then by time t:
  * t < t1: blend of picture +0x2F4 with colour +0x32C, weight 256·t/t1 (screen vt64);
  * t < t1+t2: cross-fade +0x2F4 → +0x2F8, weight 1024·(t−t1)/t2 (vt6C);
  * t < t1+t2+t3: +0x2F8 with colour +0x32C, weight 256 − 256·(t−t1−t2)/t3;
  * then (after 4 more frames) +0x2FC = 0 and: if +0x308 == 1 → game.vt114 (end the mission);
    else broadcast **event 7** and repaint the console.
* Which way each blend runs (towards or away from the colour) is not checked. (L)
* **Sample**: e001 on 6: `3e8 3e8 3e8 N91`.
* **Confidence**: H for fields, timing and the end; M for the pictures; L for the blend directions.
  Nothing found waits for a click on proceed.bmp.

### 0x92 END_TRANSITION (6 slots: d t3, d t2, d t1)
* **Effect**: game.vtF8 0x43DC37: only if no transition is running (+0x2FC == 0). Same phase
  times as 0x91, "from" = the current screen, "to" = cleared (vt60), **+0x308 = 1**: when the
  transition ends it calls game.vt114 → **the mission ends** (see summary).
* **Sample**: e001 on 46 and e00b: `3e8 3e8 3e8 N92`.
* **Confidence**: H for flow, M for the pictures.
* Note: game.vtFC 0x43DE6F (native **0x93**, 13 slots, not used by verid1) is a third variant with
  +0x308 = 2: at the start of the transition it calls vt13C (TELEPORT_PLAYER) with the saved
  +0x30C..+0x318: a teleport hidden by a fade.

### 0x96 SET_TRANSITION_SOUND (2: w volume, w sound)
* **Effect**: game.vt108 0x43E0FE(sound, volume): +0x334 = sound; 0x41ED07(sound, volume)
  (the sound manager's load/prepare, as NATIVE_68 uses before playing). The transitions play
  +0x334 when they start.
* **Sample**: e001 init `64 48 N96` (sound 48, volume 64); e001 on 7 stops sound 48 (6B).
* **Confidence**: H.

### 0x99 SET_OBJECTIVES (2: d mask)
* **Effect**: game.vt3FC 0x459920: game+0x94 = mask (default −1). See summary: event 8 and
  host+0x10 = 1 once game+0x90 equals it.
* **Sample**: e001 init `PUSH32 2 N99`.
* **Confidence**: H (code), M (name).

### 0x9A OBJECTIVE_DONE (2: d bits)
* **Effect**: game.vt400 0x459950: game+0x90 |= bits. (0x9B, game.vt404: +0x90 &= ~bits.)
* **Sample**: 14217 `PUSH32 2 N9A`, in the ending sequence.
* **Confidence**: H (code), M (name).

### 0xA3 SET_GRAVITY (6 slots: d c, d maxFall, d gravity)
* **Effect**: game.vt438 0x441257(gravity, maxFall, c). Each value 0x80000000 keeps, 0x80000001
  restores the default (from +0x10C, +0x110, +0x11C):
  * +0x124 = gravity · 2^-16 (float), +0x128 = maxFall · 2^-16 (float), +0x134 = c (raw);
  * then copies the 24-byte block +0x124.. to `*0x57D950` (the fall physics block that
    LOW_GRAVITY 0xB5 also writes) and +0x13C = 0.
* **Sample**: e001 init `PUSH32 0x40000, PUSH32 0x61a80000, PUSH32 0x500000 NA3`:
  gravity 80.0, max fall 25000.0, c = 0x40000. The defaults are 64.4 and 200.0 (natives-c.md B5).
* **Confidence**: H for fields; M for names; c's role unknown.

### 0xEE SET_FALL_DAMAGE (4 slots: d divisor, d multiplier)
* **Effect**: game.vt484 0x436F35(multiplier, divisor): +0x270 = multiplier (default 1),
  +0x26C = divisor (default 0x200000 = 32.0). Used by 0x43A662(x, src): if x > 0,
  `damage = x · +0x270 / +0x26C`; if > 0, DAMAGE_PLAYER(src, damage, kind 1) (kind 1 = falls,
  events.md). x is the fall's speed or height on landing, 16.16. (x's source not traced.)
* **Sample**: e001 init `PUSH32 0x2f0000, PUSH32 1 NEE` → divisor 47.0, multiplier 1.
* **Confidence**: H for the formula, M for "falling", L for x.

### 0xCD SET_FLAT_GROUPS (3: w c, w b, w a)
* **Effect**: game.vt424 0x440D3D(a, b, c), on flat groups (0x4B7600(id) = the head of the flats
  whose u32 at +0x14 is id; linked through +0):
  * game+0x140/+0x144 = 0, then OR of every group-b flat's +0x74/+0x78 (its "zones in play");
    group b flats: +0x18 = 1;
  * group a flats: +0x18 = 2;
  * group c flats: +0x18 = (b << 16) | 3;
  * if the player's current flat (`*0x57D954`) is in group c: 0x4B6800(0) (re-evaluates the
    player's support; refreshes zones).
* **Sample**: `101 150 100 NCD` then `103 151 102 NCD` (a = 100, b = 150, c = 101 ...).
* **Confidence**: H for the writes; L for the meaning (flat +0x18 roles, likely a lift or a
  floor that switches zones in play). Readers of flat +0x18 not traced.

### 0xD5 GET_DIFFICULTY (pushes 1)
* **Effect**: pushes config (`*0x565AB0`) +0x340, default 1 (0x4870BC).
* **Sample**: e000 init stores it in global 22; actors use `global22 + random(10) <= 7` to pick
  the tougher behaviour (6014).
* **Confidence**: H for the value, M for "difficulty".

### 0xD6 SET_INVENTORY_ENABLED (1: w on)
* **Effect**: game.vt388 → inventory 0x4470F3: +0x448 = on (repaint if changed). Off greys out
  every slot (grayout.bmp, hud.md §2.1) and blocks clicks.
* **Sample**: `0 ND6` with a teleport (e0??, 608); `1 ND6` at 7296.
* **Confidence**: H.

### 0xD7 SET_ITEM_ENABLED (2: w on, w id)
* **Effect**: game.vt38C 0x4407D2(id, on): looks the object up (vt148) and inventory 0x447137:
  the slot holding it gets +0x1A0[i] = (on == 0) (disabled = greyed out), repaint if changed.
* **Sample**: `1 8193 ND7` (enable the tricorder 0x2001).
* **Confidence**: H.

### 0xE2 FIRE_PROJECTILE_AT (13 slots: d toH, d toY, d toX, d fromH, d fromY, d fromX, w type)
* **Effect**: game.vt468 0x4403DE(type, fromX, fromY, fromH, toX, toY, toH): stage =
  game+0x78[type] (section-4 record, projectiles.md); origin (fromX, fromH, fromY) in the
  engine's (x, height, y) order; direction from 0x4B7050 on (to − from) gives heading and pitch;
  speed = stage+0x34; spawns with stage.vt4 like any projectile. No shooter is set.
* **Sample**: e063 on 4: `0x1b0000 0x230000 0x10000 0xa00000 0xff260000 0xffc00000 6 NE2`:
  type 6 from (−64, −218) at height 160 to (1, 35) at height 27.
* **Confidence**: H for the call; M for the axis order (taken from the record layout).

### 0x4C ACTOR_GO_TO (7 slots: d h, d y, d x, w id)
* **Effect**: actor vt98 0x456F96(x, y, h): obj+0x64 = 7 (mode 7: go to a point), 0x456A9C
  allocates a point record (0x4B7810) into obj+0x70; point +0x1C = x, +0x24 = y, +0x20 = h;
  obj+0x68 = 1; vt28 (sync). Arrival raises code 17 → event 37 (actors-ai.md).
* **Sample**: actor a00a on 37: `0x50000 0x610000 0xd0000 SELF N4C` (x 13, y 97, h 5) —
  a chain of waypoints; a006: `0x4000000 0xfd930000 0x660000` (h 1024: flies off upwards).
* **Confidence**: H.

### 0x5D SET_ACTOR_ALWAYS_ACTIVE (2: w on, w id)
* **Effect**: actor vt244 0x457102: every part (0x4B5070 list): flags +0x6C bit 0x400000 = on
  (actors-ai.md: updated even in inactive zones).
* **Sample**: a00a on enter: `1 SELF N5D`.
* **Confidence**: H.

### 0xEA SET_ACTOR_FLYER (2: w on, w id)
* **Effect**: actor vtB4 0x456F35: every part: flags +0x6C bit 0x1000000 = on (flyer).
* **Sample**: a006, a00a: `1 SELF NEA`.
* **Confidence**: H.

### 0xE8 SET_ACTOR_CLIMB_ANGLE (2: w degrees, w id)
* **Effect**: actor vtB0 0x456E9C: every part: +0x80 = int(f(|deg| · π/180) · 65536), f = the
  C runtime trig at 0x502F3A (sin or cos; not identified). +0x80 is the horizontal speed factor
  (flyers: vertical factor = 1 − this), 1.0 by default.
* **Sample**: a006 `60 SELF NE8`, a00a `48 SELF NE8`.
* **Confidence**: H for the field; M for the name; f unidentified (cos would give 0.5 / 0.67).

### 0xF4 SET_BILLBOARD_UNPICKABLE (2: w on, w id)
* **Effect**: billboard vt17C 0x45877B: every record (0x4B5100 list): flags +0x6C bit 0x400 = on.
  The pick test ignores records with 0x400 (player-attack.md §4).
* **Sample**: 20c8 on enter: `1 SELF NF4`.
* **Confidence**: H for the bit; M for the name (0x400 may have other readers).

### 0x83 SET_ITEM_ICON (2: w icon, w id)
* **Effect**: self: obj+0x44 (item controller).vt14 = 0x449C9D: ctl+4 = icon (the INVENT.BMP
  frame, hud.md §2.1); then game.vt394 → inventory 0x447C28(obj) repaints its slot.
  Other id: game.vt274 does the same on the looked-up object.
* **Sample**: hypospray 2003: `8 SELF N83` (state 2), `1 SELF N83` (state 3).
* **Confidence**: H.

### 0xA9 RELEASE_ACTOR_CLASS (1: w class)
* **Effect**: 0x4B65C0(class): for every actor record (`*0x57A18C`, `*0x57A188` of them, 0x114
  bytes) whose class +0xFC == class: 0x4C0540(class) (decrements a per-class use count at
  0x570288 + class·20, and when it reaches 0 frees it, 0x4C0570) and clears flag 0x2000000.
  Returns 1 if any matched (ignored).
* **Sample**: after removing an actor: `-24566 N46 10 NA9`, `-24574 N46 2 NA9`.
* **Confidence**: H for the loop; M for "frees the class's sprites".

### 0xC8 MAP_ZOOM_LEVELS (6 words)
* Known (natives-b / hud notes): `shift2, scale2, shift1, scale1, shift0, scale0` → 0x44555F, the
  automap's three zoom levels (0x8000 keep, 0x8001 default). e001: `15 2 15 1 16 1`.
* **Confidence**: M.

## Not covered here
BF, 77, AE, B2, B5 (parent). 0x93/0x95/0x9B are not used by verid1 (summarised above).
