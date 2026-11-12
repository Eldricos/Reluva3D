# How the engine and main loop drive SGEN script events (sgens.exe)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Event numbers are decimal. "H" = high confidence (read directly from code), "M" = medium
(code read, meaning inferred), "L" = low (meaning is a guess).
Working files for this analysis are in `re/ev/` (scripts and disassembly dumps).

## 0. Delivery mechanics

### SendEvent(event, a1, a2) = vtable slot 0x180 = 0x44F4E1 (H)
1. `this->vt[0x10]()` must return non-zero (object enabled), otherwise the event is dropped (returns 0).
2. If `this+0x10` (current state) != -1: look up key `((event&0xFF)<<8) | (state&0xFF)` in the handler
   map returned by `vt[0x230]` of `this`, then of `this+0x48` (parent / "brain" object), then its parent, ...
   The **state used is always the receiving object's** and the block runs with `this` = receiver.
   (op 0xB1 most likely sets +0x48: e.g. billboards/actors do `PUSH 0xE193; NATIVE_B1` in state 0. M)
3. Handler found: run interpreter `0x44F9D4(code, a1, a2)`; afterwards if `this+0x14` (pending state from
   GOTO_STATE/CASE) != -1, clear it and setState(it), else if block's next-state != -1 setState(next).
   Return value = block result.
4. No handler (or state == -1): native default, `switch(event)` at 0x44F8EB (table 0x44F996/0x44F90A):
   1→vt184, 2→vt18C, 3→vt190, 5→vt194(arg), 14→vt198, 15→vt19C, 16→vt1A0(x,y,z), 17→vt1A4(arg),
   18..26→vt1A8..vt1C8, 27→1CC(arg), 28→1D0(arg), 29→1D4, 30→1D8(arg), 31→1DC, 32→1E0(arg), 33→1E4,
   34→1E8(arg), 35→1EC, 36→1F0, 37→1F4, 43→20C, 44→210, 45→214, 46→218, 47→21C(arg), 48→220;
   4, 6..13, 38..42, >48: nothing. ("arg" = the 1-word argument when a1 == 1.)
   **Consequence:** events sent to an object still in state -1 never reach script code.

### setState(s) = vt 0x228 = 0x455F6B (H)
If s != current: SendEvent(3,0,0) (leave, in old state), state = s, SendEvent(2,0,0) (enter).

### Event arguments in the VM (H) — interpreter prologue 0x44F9F8
`a1` is a word count, `a2` a value or pointer:
- a1 == 0: nothing pushed.
- a1 == 1: `(int16)a2` pushed.
- a1 == n > 1: `a2` points to n int16 words; pushed `p[n-1]` first ... `p[0]` last, so **p[0] is on top**.
There is no special "get arg" opcode: blocks POP / SET_LOCAL the arguments off the stack
(e.g. actor `ev 4: PUSH 1; IF_EQ ...` compares the event-4 value).

## 1. Engine event queue → game.vt[0x16C] (0x435CD8)

Engine queues `(code, id, record)` with 0x4B8120 (200-entry FIFO at 0x57A040, count 0x57A03C; if it fills it
is flushed at once). 0x4B80D0 flushes it in FIFO order through the callback pointer 0x57BEDC (= 0x4357E5,
set at 0x43508E after level init). Events queued during the flush are processed in the same flush.

Object lookup helpers (key = `(id & 0xFFFF) | typebits`, so an id that already has type bits keeps them:
e.g. a flat whose +0x14 is 0xE0nn reaches the type-1 object 0xE0nn) (H):

| game vt | adds bits | object type |
|---|---|---|
| 0x148 | none (exact id) | any |
| 0x14C | 0xE000 | 1 |
| 0x150 | 0x4000 | 2 (walls) |
| 0x154 / 0x158 | 0x8000 | 5/6 (0x8000 type) |
| 0x15C | 0xA000 | 4 (actors) |
| 0x160 | 0x2000 | 8 (billboards) |
| 0x164 | 0x6000 | 3 |
| 0x168 | 0xC000 | 7 (flats) |

All lookups go through the CMapWordToPtr at game+0x9C (Lookup 0x51CA7A). A missing object = no event.

Game state fields (int16, -1 = none): +0xB8 wall touched, +0xBA flat "standing on", +0xBC billboard,
+0xBE actor touched, +0xC0 floor flat (player over), +0xC2 ceiling flat (player under).

| code | engine source (when) | 0x4357E5 / vt16C action | events (receiver) | conf |
|---|---|---|---|---|
| 1 | player move 0x4B472A/42: collision result (0x4B9870) changed and is a kind-1 (wall) record. Old one: queue(oldkind, -1, 0); new: queue(kind, rec+0x14, rec). Also calls mover-activate 0x4B94D0 on new wall. | game+0x1A8 = (id != -1); if +0xB8 != id && +0xB8 != -1: **15** → wall(+0xB8); +0xB8 = id; if id != -1: **14** → wall(id) (vt150) | 15 old wall, 14 new wall | H |
| 2 | 0x4B4B91: landed (airborne flag cleared); id = impact speed, rec = floor flat | flatobj = vt168(rec+0x14); unless flatobj && flatobj.vt244() (t7: returns obj+0x6C) : fall damage vt358(speed, rec.id|0xC000) then vt16C(2,…): +0xBA = +0xC0; if != -1 **14** → flat(+0xBA) | 14 floor flat | H |
| 3 | 0x4B4B27: hit ceiling; id = speed, rec = ceiling | same test, only fall/hit damage vt358; no script event | (none; damage → broadcast 9) | H |
| 4 | collision with a kind-4 record (billboard) | +0xBC: if != id && != -1 **15** → bb(+0xBC); +0xBC = id; if id != -1 **14** → bb(id) (vt160) | 15/14 billboards | H |
| 5 | 0x4B9B4F: player entered a billboard's box (|dx|,|dy| <= rec+0x54, vertical overlap rec+0x20..+0x58, zone active), once (flag 0x800000 in rec+0x6C); also kind-5 collision | → vt16C(4, id, 0) | as code 4 | H |
| 8 | 0x4B9B73: left that box (flag cleared) | → vt16C(4, -1, 0) | 15 to +0xBC | H |
| 6 | collision with a kind-6 record (actor) | +0xBE, same 15/14 pattern via vt15C | 15/14 actors | H |
| 7 | (kind 7 collision) | ignored | – | H |
| 9 | 0x4B4B55: ceiling above player changed (rec or -1) | if +0xC2 != id: **52** → flat(+0xC2) if != -1; **51** → flat(id) if != -1; +0xC2 = id | 52 old, 51 new ceiling | H |
| 10 | 0x4B47FB: floor flat under player changed | if +0xC0 != id: **15** → flat(+0xBA); **54** → flat(+0xC0); if id != -1: **53** → flat(id) and, if not airborne (0x57DA00 bit0 clear), +0xBA = id and **14** → flat(id); +0xC0 = id; +0xBA = airborne ? -1 : id | 15, 54, 53, 14 | H |
| 11 | 0x4B499F / 0x4B6831: became airborne (fell off / jumped) | if +0xBA != -1 **15** → flat(+0xBA); +0xBA = -1 | 15 standing flat | H |
| 12..15 | 0x4B7A0F: engine "mover" (door/lift, list 0x552098) state change; id = mover+0x10 | vt154(id) gets **22**(12), **23**(13), **24**(14), **25**(15) | see below | H (events), M (meaning) |
| 16 | actor engine (0x4C460B, 0x4C4A34): actor action/move finished; id = anim byte | if rec[0x10E] == id and actor(rec+0x14).vt234() == id: **36** | 36 actor | M |
| 17 | 0x4C4341: actor animation frame counter wrapped (cycle done) | same check → **37** | 37 actor | M |
| 18 | 0x4C4590: actor reached/turned to target | actor(rec+0x14) **36** directly (no check) | 36 actor | M |
| 19 | 0x4BC85C: result of a pick requested by game.vt3C4 (player attack/fire, sets game+0x25C=1) at screen centre; id = distance−1.0, rec = hit record (rec+0x69 kind) | range check (dist <= radius 0x57D998 + 0.5 [+rec+0x54]); target by kind: 1 wall, 2/3 flat, 4/5 billboard, 6 actor (also stores 0x542A04), 7 → id|0xE000 no object; **broadcast 59** (4 words: hit id with type bits or 0, x, y, z); target gets **16** (3 words: x, y, z) | 59 listeners, 16 hit object | H |
| 20 | 0x4C5059 etc.: engine projectile (obj+0x48 flag 0x10) hit something | game.vt170(id=projectile, rec): similar: sound, broadcast **59**, **16**(x,y,z) to hit object, or damage the player (vt34C) | 59, 16 | M |

Notes:
- Codes 1/4/5/6 come from one "currently colliding thing" slot (0x57D95C): only changes are queued.
  14/15 for walls and billboards are therefore sent once per contact start/end (H).
- Mover states (record +0x1C, position +0x18, 0..0x4000 or 0..−0x4000): 2/6 = start opening
  (→22), 1/5 = fully open (→23), 4/7 = start closing (→24), 3 = closed (→25); the update 0x4B8300
  advances them, bumping a wall with +0x92 (mover id) / +0x94 (mode) activates them (0x4B94D0). (M)
  The receiver key is `mover+0x10 | 0x8000`: ids 0x8xxx/0xAxxx/0xCxxx/0xExxx survive, but a 0x2xxx id
  would be turned into 0xAxxx (actor). Many verid1 actors/billboards and their type-1 parents have
  `ev 25 → next state` chains with empty code; who owns their mover records is not confirmed (L).

## 2. Who sends the other events

| event | sender | args | conf |
|---|---|---|---|
| 1 | **Timer.** WM_TIMER handler 0x4425FC (MFC message map 0x531550). Timer id = object id: object found → SendEvent(1); not found → KillTimer. Periodic until killed. id\|0x10000 → native obj.vt188 (type-6 objects); id\|0x20000 → KillTimer + event 46. | none | H |
| — | timers are set by ops **0x32** (pop id, pop ms), **0x33** (pop id, pop n: n·1000 ms), **0x34** (pop id, pop lo, pop hi: random(lo..hi)·100 ms) → game.vt178 (SetTimer, 0x45FF6D; callback 0x45FFFC posts WM_TIMER); op **0x35** pop id → vt17C KillTimer. id == self → obj.vt2C. Same id replaces the timer. | | H |
| 2 / 3 | setState (above); level init (below) | none | H |
| 4 | op **0x09** GAME_CALL_2: pop target id (top), pop value → game.vt18C: vt148(target).SendEvent(4, 1, value) (no null check) | 1: value | H |
| 5 | op **0x2C**: pop target, pop value → self: vt30 = SendEvent(5,1,v); else vt194 → target.vt30 | 1: value | H |
| 6,7,8,9,10,49,50,56,57,58,59,61 | **broadcasts** 0x4357B9 → 0x44E405: sent to every object in the listener list 0x545EB8 (op **0x31** subscribes (vt44), op **0x30** unsubscribes (vt3C); list cleared at level start, newest subscriber first) | see below | H |
| 14/15/22-25/36/37/51-54/16/59 | engine codes (section 1) | | H |
| 17, 26..35 | actor AI callback 0x44F29E(actor, other, kind) registered by 0x4576C2: kind0: other → 27(other) to actor + 17(actor) to other, else 26; kind1 28(other)/29; kind2 30/31; kind3 32/33; kind4 34/35 | 1: id or none | H (mapping), L (meaning) |
| 35 | game.vt188 every frame (from 0x43B2E4) when game+0xC4 (engaged object, set by op 0x73 → vt180, which also sends 35 to the previous one) and player moved > 3.0 units (0x30000) from where it was engaged; menu panel 0x448628 when player walks > +0x1B8 away; AI kind4 | none | H |
| 38..42 | actor damage t4 vt24C(dmg, severity) 0x45747E, self-sent: severity > +0x90: health(+0xA8)−=dmg, stun(+0xA0)−=dmg → 42 dead / 41 stunned / 40 hurt; > +0x94: stun−=dmg → 41 / 38; > +0x98: 39 | none | H (sends), M (names) |
| 43 / 44 | inventory (game+0x2B4, 12 slots): 0x446DD0 add item → 43 to item (replaced item first gets 44); 0x4472CC remove → 44; also t4 0x457E5F/0x457EFC | 1 (t4 ones) | M |
| 45 | op **0x72** "activate": pop id; self → vt138 else game.vt26C → obj.vt138 = SendEvent(45); inventory "use selected item" key 0x44789E | none | H |
| 46 | game.vt174(id) from sound/animation end 0x41D812 (sound record +0x60 = requesting object); one-shot timer (id\|0x20000) | none | M |
| 47 | menu panel click 0x448164 → owner(+0x1A8): 47 with item id (vt38 & 0x3FFF) or 0 | 1 | M |
| 48 | game.vt34C damage: worn armour item (+0x2E0) used up → 48 to it | none | M |
| 55 | screen effect slot finished (0x43199C, 4 slots at game+0x4A4) → its owner | none | M |
| 60, 63 | UI classes 0x449069 / 0x44763A | | L |
| 62 | UI hotspot click (mouse up inside rect, 0x446BBB / 0x448388) → slot's object | none | M |

Broadcast payloads (all via 0x4357B9):
| ev | when | args | conf |
|---|---|---|---|
| 6 | game.vt0xEC 0x43E742 | none | L |
| 7 | game.vt0x10C 0x43E13A (after a modal screen, when +0x308 != 1) | none | L |
| 8 | per-frame update 0x43AD53, once when game+0x90 == +0x94 and +0x98 == 0 (then +0x98 = 1) | none | L |
| 9 | player damaged: game.vt34C(src, dmg, kind) (ops 0x87, falls via vt358 kind 1, projectiles), 0x43B419 environment (src=+0x1FC, 1, 1, 13) | 4: src, dmg, dmg, kind | H |
| 10 | player healed game.vt354(delta) | 1: new health (+0x200) | H |
| 49 | game.vt3A4 (op at 0x452F06/20) | 1 | L |
| 50 | game.vt3A0 0x43FDC4: pick mode 2 ("look/examine") result | 1: object id | M |
| 56/57/58 | game.vt0xE0 0x43D0C9 after each engine tick: player medium (+0x158, from floor flat +0x18) changed to 2 / 1 / 0 | none | M |
| 59 | hit report (codes 19, 20) | 4: id, x, y, z | H |
| 61 | 0x43AF1B per frame: airborne bit (0x57DA00&1) changed | 1: 1 = left ground, 0 = landed | H |

## 3. Timing

### Level start (0x4336F0 = game.vt0xD8) (H)
1. Load SGEN (0x45F160), create every object (factory 0x43519F; base ctor 0x44E1A0 sets state −1,
   registers id in game+0x9C via vt144). No events yet.
2. **0x4356FF**: clear listener and dirty lists (0x44E340); if the map is not empty, iterate it with
   CMap GetNextAssoc (0x51CB4D) and call `setState(0)` on each object, pumping messages (vt490) after each.
   → each object gets **3** (in state −1: native only) then **2 in state 0**.
   Iteration order: buckets `(id >> 4) % 17` ascending (17 = default hash size, 0x51C884); inside a
   bucket, last-inserted first (insert at 0x51CAA7 prepends). Objects not yet visited are still in
   state −1, so events they receive from earlier objects' init blocks go to native defaults only.
3. 0x44E3A4: run deferred per-object sync (obj.vt18 for objects marked by vt28).
4. Install engine callback (0x57BEDC = 0x4357E5), apply section-0 pairs (0x4B5220), etc.
   There is **no "event 1 to everyone" at start**: event 1 is purely the timer tick.

### Each frame — game.vt0xE8 0x43A77E → game.vt474(1) 0x436C49 (M for the outer loop, H inside)
1. game.vt0xDC **0x43AD04** (player/game update): broadcast 8 (once), 61 on airborne change,
   environment damage (9), disengage check vt188 (35), …, ends with 0x44E3A4 deferred sync.
2. view.vtF0(1) **0x4309C5**:
   a. advance engine clock by frame time [0x546D68] ms, clamped 8..1000 (0x4B5B60);
   b. engine tick 0x4B5B10: movers 0x4B82E0 (codes 12-15), player move 0x4B46A0 (1,2,3,4,6,9,10,11),
      actors 0x4C4C70 (16,17,18,20), billboards 0x4BBEA0 (5,8), then **flush → scripts** (0x4B80D0);
   c. game.vt0xE0 0x43D0C9 (medium broadcasts 56/57/58);
   d. render 0x4B5A90; a pending pick (game.vt3C4 attack / vt3A0 look) is resolved here and queues
      code 19 (delivered at the next tick's flush) or runs mode 2 directly;
   e. screen effects 0x43199C (event 55).
3. WM_TIMER (event 1), keyboard/mouse (inventory 43/44/45, UI 47/62) arrive through the Windows
   message loop between frames (and wherever vt490 pumps messages).

No script event is sent unconditionally every frame; "per frame" behaviour in scripts is built from
timers (ops 0x32-0x34).

## 4. verid1 handler counts (blocks per type), for prioritising
1:{t1 44,t7 18,t2 5,t8 15,t4 17}  2:{t1 37,t7 26,t2 6,t8 38,t4 50}  4:{t1 40,t8 15,t4 23,t7 1,t2 1}
14:{t7 15,t1 8,t2 5,t8 10,t4 1}  16:{t2 4,t4 1}  24:{t4 64}  25:{t1 4,t8 58,t4 167}  45:{t1 23,t8 19,t4 8,t7 5,t2 4}
53:{t7 40}  54:{t7 12}  51/52:{t7 3/2}  62:{t1 6,t8 14,t2 3,t4 3}  35:{t1 8,t8 8,t2 3,t4 4}
Ops used: 0x09 ×269, 0x35 ×210, 0x32 ×66, 0x33 ×37, 0x34 ×32, 0x72 ×39, 0x31 ×14, 0x2C/0x73 ×0.
