# SGEN natives, batch F (used by amargos1/2, arvada1, enterp, galorn1/2, halee1)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Same conventions as natives-d.md:
* **Push order** is first-pushed to last-pushed. The handler pops the last-pushed value first.
* **d** = a 32-bit slot pair (16.16 unless said otherwise); **w** = a word.
* Slot counts come from the handlers' `sub [ebp-0x54]` sizes (re/ops.txt).
* **"self or id"**: if the id is the running object, the handler calls the object's own method
  (obj.vt[x]); otherwise it calls game.vt[y], which looks the id up first.
* Confidence: **H** = read from code, **M** = code plus inference, **L** = guess.

## Results

### 0x15 GAME_INIT → ALLOC_GLOBALS (1: w count)
* **Effect.** 0x44F10A(count): if count > the current size (0x545EAC), it Global(Re)Allocs the
  script globals array (0x545EB0) to count words.
* **Samples.** Every mission's first object, on enter: `40 GAME_INIT`, `36`, `26` ...
* **Implement.** No-op: we always have 32768 globals.
* **Confidence.** H.

### 0x88 SET_ENVIRONMENT_DAMAGE (1: w period)
* **Effect.** game.vt350 0x43A566(period, selfId): game+0x1F4 = period. 0 also clears the
  accumulator +0x1F8. Otherwise game+0x1FC = the calling object's id.
* **Per frame** (0x43B310, in game.vtDC):
  * +0x1F8 += frame ms (0x546D68).
  * While +0x1F8 > +0x1F4: health (+0x200) −1 and +0x1F8 −= period.
  * Health is clamped to at least 1. Then vt35C(health) updates the bar.
  * Then it broadcasts **event 9** with (+0x1FC, 1, 1, 13), i.e. damage kind 13.
* **Result.** 1 health per `period` ms until 0 is set (poison gas, vacuum, radiation ...).
* **Samples.** galorn1 (10 uses), arvada1 (13), galorn2 (15) ...
* **Implement.** Mission fields `_envPeriod`, `_envSource`, `_envElapsed`.
  * In update(): accumulate ms.
  * Per elapsed period: `_health = max(1, _health − 1)` and broadcast 9 {source, 1, 1, 13}.
  * Note this bypasses the shield (it doesn't go through vt34C).
* **Confidence.** H.

### 0x8A SET_HEALTH (1: w value)
* **Effect.** game.vt35C 0x43A6C0(v): v is clamped to at most the maximum (+0x204), with no
  lower clamp. Health bar +0x9C = v (repainted); game+0x200 = v. No event.
* **Sample.** enterp start: `GET_ARRAY(i) > 0 → SET_HEALTH(GET_ARRAY(i))`. This restores the
  health a previous mission stored in the carried array.
* **Implement.** `_health = std::min<int>(v, _maxHealth)`.
* **Confidence.** H.

### 0x8B SET_WORN_ITEM (1: w id; self, or 0 for none)
* **Effect.** game.vt3C8 0x43A0FD(obj): game+0x2E0 = the item.
  * The old item's controller gets vt34 (detach display).
  * The new one gets vt30(leftPanel, rect (165,17)-(189,130), id 0x13, visible = (left mode == 3)).
    That is a gauge in the life panel at screen (165,270)-(189,383).
* **DAMAGE_PLAYER (vt34C)**, for damage kinds other than 12 and when +0x2E0 has charge
  (ctl.vt38):
  * The damage becomes ctl.vt3C(damage).
  * For kind 9 (0x44B182) that is: the charge (+0x10) −= damage, or 0 if damage ≥ charge, and
    the returned remainder is always 0. **The shield soaks the whole hit.**
  * If the charge is now gone, the item gets event 0x30 (48).
* **Worn items are ATTACH_ITEM_BEHAVIOUR kind 9** (ctor 0x44AE8B):
  * max +0xC = 30, charge +0x10 = 30, +0x14 = 5 (the gauge's colour/style).
* **Gauge draw** (0x445D74): ⌈charge/step⌉ bars, each 20×2 px, starting at (x+2, top+2) and
  3 px apart going down.
* **Samples.**
  * galorn1 2034: `ATTACH(… kind 9) ; 150 150 SELF NCB`; on event 4 `SELF N8B` or `0 N8B`.
  * amargos2 uses it with CC.
* **Implement.**
  * `_wornItem` id; ADD charge for kind 9 (30/30).
  * In damagePlayer: if kind != 12 and the worn item's charge > 0, charge = max(0, charge − dmg),
    dmg = 0; send 48 if it reached 0.
  * Optional gauge.
* **Confidence.** H (absorb rule), M (display geometry).

### 0xE7 SET_WORN_GAUGE (1: w id; self, or 0)
* **Effect.** game.vt3D0 0x43A20F: the same as 8B but for game+0x2E4, with the same display rect.
  * Nothing else reads +0x2E4; it is display only.
  * Used for **kind 14** items (ctor 0x44B2AA: 30/30, style 2), which scripts drive with E6.
* **Sample.** halee1 200A: `ATTACH(0,20,14)`, `20 20 SELF NE6`.
  * Every 30 s it sets `local local 8202 NE6` and calls `8202 NE7`: a countdown gauge (air?).
* **Implement.** Store the id. Draw it like 8B's gauge if wanted, else no effect.
* **Confidence.** H (code), L (what it shows).

### 0xCB SET_ITEM_CHARGE_MAX (3: w max, w charge, w id)
* **Effect.** obj.vtD0 0x44EACC(charge, max) on the item controller:
  * if max ≠ 0x8000: ctl.vt2C(max), which for kinds 9/14 (0x44AF18) sets +0xC and clamps the
    charge;
  * if charge ≠ 0x8000: ctl.vt24(charge), which for 9/14 (0x44AF57) sets +0x10 clamped to the
    max and refreshes the gauge.
* Only meaningful for kinds 9/14. On charge kinds 2–6, vt24/vt2C are other functions, so scripts
  only use it on 9/14.
* **Sample.** galorn1/2: `150 150 SELF NCB` (max 150, charge 150).
* **Implement.** `item.maxCharge = max; item.charge = min(charge, maxCharge)`; 0x8000 keeps.
* **Confidence.** H.

### 0xE6 SET_ITEM_CHARGE_MAX (alias) (3: w max, w charge, w id)
* **Effect.** obj.vtD8 0x44EB65: the same two calls as CB, with the words masked to 16 bits
  unsigned.
* **Samples.** halee1 `20 20 SELF NE6`; `local local 8202 NE6`.
* **Implement.** Treat like CB.
* **Confidence.** H.

### 0xCC GET_ITEM_VALUE (1: w id; pushes 1)
* **Real stack effect: 1 pop, 1 push.** Both branches push exactly one word. ops.txt adds the
  two branches together to get 2.
  * amargos2 confirms it: `SELF NCC ; PUSH 0 ; SET_LOCAL` stores the result in local 0.
* **Effect.**
  * Self: obj.vtD4 0x44EB3B → ctl.vt20().
  * Other id: game.vt298 0x4393EC → same, result via pointer.
  * ctl.vt20 for kinds 9/14 (0x45A470) is the charge +0x10. For weapon kinds it is the item's
    "team" constant (kind 2 → 3).
* **Sample.** amargos2 worn-item objects on 45/4: `SELF NCC → local 0`, then `0 N8B`.
* **Implement.** Push the item's charge for kinds 9/14, else its team value.
* **Confidence.** H.

### 0x93 TRANSITION_TELEPORT (13 slots: d t3, d t2, d t1, position block)
* **Pops in this order:**
  * the position block (x, y, z dwords, heading word; as TELEPORT_PLAYER 0x75);
  * then t1, t2, t3.
* **Effect.** game.vtFC 0x43DE6F: like 92 (from the current screen) but with +0x308 = 2. The
  saved position +0x30C..+0x318 is passed to vt13C (TELEPORT_PLAYER).
* **Timing.** natives-d places the teleport at the start of the transition (not re-checked).
  * Visually it is hidden if done at the darkest point.
  * The transition ends with **event 7** like 0x91, not with ending the mission.
* **Sample.** enterp: `PUSH32 500, PUSH32 1000, PUSH32 500, BLK(0,0,0x10,0,ffe3,0,ffe1) N93`.
* **Implement.**
  * Start a non-ending transition (fade out over t1, back in over t2+t3).
  * Teleport (setPosition(0, block)) when brightness reaches 0.
  * Broadcast 7 at the end, as for 91.
* **Confidence.** M.

### 0x94 SET_TRANSITION_COLOUR (3: w b, w g, w r)
* **Effect.** game.vt100 0x43E084(r, g, b): +0x32C = GetNearestPaletteIndex(palette, RGB(r,g,b)).
  * This is the colour the transitions (91/92/93) blend through. The default is presumably
    black.
* **Samples.** enterp on 7: `255 255 107 N94` (r 107, g 255, b 255: cyan). Before a 93:
  `0 255 100 N94` (r 100, g 255, b 0).
* **Implement.** Store an RGB. TransitionView fades towards it instead of black.
* **Confidence.** H.

### 0x97 SET_MISSION_OUTCOME (2: d code)
* **Effect.** game.vt3F4 0x4408DD: host(game+0x4C).vt24(code) = 0x461251. This puts code into
  the first empty slot of host+0x14[5] (0x461251; vt28 0x4612AA removes a code).
  * The controller's owner reads them after the module ends (reader not traced).
  * They look like story branch codes for the campaign: follow-up movies or missions.
* **Samples.**
  * amargos1 after `92`: `if global26 == 1: PUSH32 11 N97`.
  * enterp `if global20 == 1: PUSH32 1 N97`.
* **Implement.** Keep a list of up to 5 codes on Mission (getOutcomeCodes()). Report them when
  the mission ends and keep them for the campaign; there is no visible effect.
* **Confidence.** H (storage), M (meaning).

### 0xA8 LOAD_ACTOR_CLASS (1: w class)
* **Effect.** 0x4B6560(class): for every actor record of that class (+0xFC), sets flag 0x2000000
  and loads its sprites (0x4C01B0). It is the opposite of 0xA9.
* **Samples.** arvada1 `1 NA8` (×2), enterp.
* **Implement.** No-op: we load every sprite set at the start.
* **Confidence.** H.

### 0xAB SET_TEXTURE_SCROLL (3: w vSpeed, w uSpeed, w id)
* **Effect.** obj.vt50 on walls (0x4561BF), flats (0x458F9A) and billboards (0x4588BB); actors
  ignore it. For every record of the object, 0x4B78B0(rec, uSpeed, vSpeed):
  * re-bases the texture offsets +0x44 (u) and +0x4C (v) so the texture doesn't jump;
  * stores +0x60 = uSpeed and +0x62 = vSpeed.
  * The renderer then offsets textures by speed · t, with t the global time 0x57A034:
    `(speed·t & 0x1FFFF) << 6`, wrapping every 2^17 / speed.
* **Sample.** arvada1: `local 0 16584 NAB` (u 0, v = local), `local 0 -16184 NAB`. These are
  scrolling textures (falls, conveyors).
* **Implement.**
  * Per wall/flat/billboard: scroll speeds (u, v), with an offset that grows by speed·ms.
  * Units, by the formula: (speed · ms · 64) in 16.16 texture-offset units, i.e. speed/1024
    units per ms. Check against a sample visually.
* **Confidence.** H (fields), M (units).

### 0xAD SET_FLAT_FLAG_68 (1: w id)
* **Effect.**
  * Flats (0x45BA00): the script object's +0x68 = 1. The only reader found is the flat's vt23C
    getter (0x45BA20), used by no native in these scripts.
  * Walls, billboards and actors: no-op (base 0x44ED4C).
* **Sample.** arvada1 flats C002/C003/C005/C006 on enter: `SELF NAD`.
* **Implement.** No-op (meaning unknown).
* **Confidence.** H (write), L (meaning).

### 0xAF SET_PLAYER_SPIN (1: w degrees per second)
* **Effect.** game.vt418 0x440B90: game+0x1EC = v. Each frame (0x43B6F0):
  `heading += MulDiv(v, frameMs, 1000) · 32768/180`, i.e. v degrees per second.
* **Sample.** galorn1/2 on a timer: `0 SET_PLAYER_CONTROLS ; 180 NAF` (the player is spun
  helplessly); `100 NAF` in amargos1.
* **Implement.** `_spin` (deg/s). The engine adds `spin · ms/1000` (radians conversion) to the
  camera yaw each frame (the sign isn't checked).
* **Confidence.** H.

### 0xB6 SET_SKY (1: w id)
* **Effect.** game.vt444 → view vt120 0x431729(id):
  * finds the sky texture record whose id is `id` in the list 0x579FB0 (0x4B5440);
  * copies it into the current sky (0x4B5520);
  * turns the sky on (0x57D834 = 1); view+0x47C = id.
* **Samples.** Always `0 NB6`: galorn1/2 (×17), arvada1.
* **Implement.** The same as SET_SKY_ENABLED(1). A non-zero id would pick another sky chunk.
* **Confidence.** H.

### 0xB8 SHOW_VIEW_OVERLAY (2: w frame, w image)
* **Effect.** game.vt44C 0x4415A7(image, frame):
  * image −1: view.vt104(0, 0) removes it.
  * Otherwise: the script resource's file (0x44959D) is loaded into the view's second overlay
    (+0x480), and +0x49C = frame.
  * Each frame (0x430BA4) it is drawn like the punch: horizontally centred at the top of the
    view, frame `frame` (surface vt94).
* **Samples.** halee1: `1 81 NB8` (image 81, frame 1) when wearing something; `0 -1 NB8` removes
  it.
* **Implement.** An overlay of the resource image's .OVR at that frame (frames as tall as the
  view, like the punch), drawn after the 3D view.
* **Confidence.** H.

### 0xC0 SET_FLAT_ZONES (5 slots: d high, d low, w id)
* **Effect.** obj.vt168 on flats (0x459050). For every flat record of the object:
  * +0x74 = low, +0x78 = high: the 64-bit "zones in play" mask (our Sector.activeZones).
  * If the player stands on one of them (0x57D954), 0x4B68A0(low, high) applies it at once.
  * Other classes: no-op.
* **Sample.** amargos1: `PUSH32 0, PUSH32 0x400, SELF NC0` (only zone 10 active).
* **Implement.** For sectors with that objectId: `activeZones = (uint64)high << 32 | (uint32)low`.
  If the walker's floor is one of them, refresh its zones.
* **Confidence.** H.

### 0xC1 ADD_FLAT_ZONE_BAND (7 slots: d high, d low, d height, w id)
* **Effect.** obj.vt16C on flats (0x4590D9).
  * On first use, the object allocates a table of up to 8 entries {height, low, high}, starting
    with {0x80000000, the flat's current +0x74, +0x78}.
  * Each call inserts {height, low, high}, keeping the table sorted by descending height.
  * Every flat record of the object points +0x70 at the table.
* **Reader** (0x4B49C5): while the player is on such a flat, the zones in play are those of the
  first entry whose height ≤ the flat's current height (+0x20).
  * **Result: a lift whose visible zones change with its height.**
* **Sample.** arvada1 flat C00A: (high 0x32000AED, low 0x94400000, height −10), (0xC, 0, −90),
  (0xC, 0x401C0, −120).
* **Implement.** Per sector, a sorted list of {height, zones}. When the walker's floor has one,
  use the first entry with height ≤ the floor's current height. This needs moving flats; use the
  sector height.
* **Confidence.** H.

### 0xC9 MAP_HEIGHT_BAND (4 slots: d above, d below)
* **Effect.** game.vt3EC → automap 0x4442DF(below, above): the band of heights the automap shows
  relative to the player.
  * +0x64 = +0x60 = below (0x80000000 keeps; 0x80000001 → −6.0).
  * +0x6C = +0x68 = above (default 6.0).
  * +0x5C = 0x80000000, +0x70 = 0x7FFFFFFF, then a refresh (0x44420F).
* **Sample.** galorn2: `PUSH32 0xffce0000 (−50), PUSH32 0xffc40000 (−60) NC9`, giving below −60
  and above −50.
* **Implement.** Hud::setMapLevels-like: set the current band's low/high to below/above.
* **Confidence.** H (fields), M (exact band semantics).

### 0xE3 FIRE_PROJECTILE_AT_OBJECT (9 slots: w targetId, d fromH, d fromY, d fromX, w type)
* **Effect.** It pops type, fromX, fromY, fromH, then the target id.
  * 0x4B5070(id) finds the actor's (first) record. Its +0x1C x, +0x24 y and +0x20 height become
    the "to" point of game.vt468, the same as 0xE2 FIRE_PROJECTILE_AT. No shooter.
* **Sample.** arvada1 (9 uses).
* **Implement.** Like 0xE2, with the target's position (actor feet; the original aims at the
  record's height).
* **Confidence.** H.

### 0xE5 SET_PIXEL_PICK (2: w on, w id)
* **Effect.** obj.vt174 on walls, flats and billboards (0x4562D2): every record's flags +0x6C bit
  0x200 = on. The pick test then hits only non-0xFF texels (player-attack.md §4). Actors:
  no-op.
* **Sample.** galorn2: `1 SELF NE5` after HIDE.
* **Implement.** No-op: our pick already only records drawn (opaque) pixels.
* **Confidence.** H.

### 0xEF GET_ALERTED_COUNT (pushes 1)
* **Effect.** Pushes the word at 0x57A178: the number of actors that currently see the player.
  * Per actor (0x4C3E90): when it is within its class's notice distance (0x57BEF4 table) and has
    line of sight (0x4BB900), flag 0x8000000 is set and the count goes up; when it loses either,
    the count goes down.
  * The class callback 0x57AA70 is called with 1 (seen) or 2 (lost).
  * The count is reset at level load (0x4BE2BC).
* **Sample.** galorn1 (74 uses): `NEF 1 IF_GT ...` (stealth: being seen by more than one guard).
* **Implement.** Count the actors whose proximity state says "sees the player" (our Actors near
  flag with line of sight).
* **Confidence.** H.

### 0xCD SET_FLAT_GROUPS: readers
* game+0x140/+0x144 (the OR of group b's zones) and the flat +0x18 marks have no readers in the
  game, the automap or the renderer/physics (0x430000–0x446000, 0x4B0000–0x4C9000).
  * Searched for loads of +0x18 followed by compares or masks, and for game+0x140/+0x144 loads.
* The only side effect found: if the player stands on a group-c flat, 0x4B6800(0) runs. That
  re-runs the player's support with no impulse.
* **Implement.** Keep the no-op.
* **Confidence.** M.

### 0xAE SET_PLAYER_CONTROLS: bits
* **The mask in effect.** Each frame (0x43AF50) the effective mask +0x1E4 is:
  * mask (+0x1E8), unless the controls are locked (+0x1A4) and +0x148 is clear;
  * in that case it is forced to **6** (turn and look only).
* **Bit tests** in the per-frame input/motion code 0x43AD04 (default mask 0x5F):

| bit | where | what it gates | conf |
|---|---|---|---|
| 0x01 | 0x43B86D | walking forward/back (speed +0x164) | H |
| 0x02 | 0x43B0D0, 0x43C22A | turning: mouse-x steering in the view and the turn keys (+0x230, rate +0x170) | H |
| 0x04 | 0x43B1B1, 0x43CAD5 | looking up/down: mouse-y and the look keys (+0x220, rate +0x174) | H |
| 0x08 | 0x43B7AD | jumping (+0x244 key, not mid-air: +0x14C timer = 1000) | H |
| 0x10 | 0x43C518 | running: +0x240 doubles the speed | M |
| 0x20 | 0x43BD22 | step rate from distance moved per frame (+0x180): footsteps/bob, off by default | L |
| 0x40 | 0x43B8C9, 0x43CD5A.. | in low gravity/swimming (+0x148): vertical movement; without it a locked player gets the fixed speed 0x542A18 | M |

* **Samples.** 0x1F (no swimming), 0x06 (cutscene: turn and look only), 0x17 (no jump),
  0x5F (default), 0 (frozen).
* **Implement.** In updateWalk:
  * forward/back and strafe need bit 1;
  * turn needs 2; look needs 4; run needs 0x10; jump needs 8.
