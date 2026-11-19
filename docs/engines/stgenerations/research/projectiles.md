# Enemy shots and projectiles (sgens.exe)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

H = read directly from code, M = code plus inference, L = guess. Coordinates: records keep
**x at +0x1C, z (height) at +0x20, y at +0x24** (16.16). Angles are u16, 65536 = 360°.
Trig table `*0x55215C` = 512 × (a, b) = (sin, cos) 16.16, index `angle >> 7` (byte offset `(angle>>4)&0xFFF8`).
dt = `*0x57A030` ms (integer, clamped 8..1000). Scratch tools: `re/sec4.py` (dumps section 4), `re/coll.txt`
(disassembly of the sweep 0x4BA9E0).

## 1. SGEN section 4: weapon/projectile records (100 bytes)

Built at level start by **0x43FE9C** (game method): it counts records until `+0 == 0` (game+0x84 = count),
allocates `game+0x78` = array of count type pointers, and per record makes a **projectile type** ("T", 0x54 bytes).

| off | name | how it is used | conf |
|---|---|---|---|
| +0x00 | kind | 0 = end of table; 1 = beam (head + trail, T in 0x552074 array, 0xFC stride = 3 types); 2 = bolt (plain projectile, T in game+0x7C array); 3 = burst (particle spray, T in 0x552070 array); 4 = none (spawn fn 0x4403CC returns NULL, no move/hit fns) | H |
| +0x04 | anim rate | T+0x40 u16: frame = `(age_ms * rate) >> 16` (+phase), mod frames (0x4C789D) | H |
| +0x08 | lifetime ms | T+0x14; projectile expires when `age > lifetime` (0x4C4CA4) | H |
| +0x0C | first texture | T+0x3C i16: LV2 chunk-13 id; textures `first .. first + (dirs+dirflag)*frames - 1` are loaded at build (0x4B6EF0 → 0x4C4EC0) | H |
| +0x10 | half width | T+0x2C → proj+0x34 (sprite half width, like billboard +0x34) **and** proj+0x54 (collision radius r) | H |
| +0x14 | height | T+0x30 → proj+0x38 (sprite height) and proj+0x58 = height/2; the sprite is drawn with its base at z − height/2 (centred on the point) | H |
| +0x18 | mirror-dirs flag | T+0x45 (byte, set to 0xFF if non-zero): 2·dirs view sectors with mirroring (see §4) | H |
| +0x1C | keyed | non-zero → T+0x48 bit0 → proj+0x6C flag **0x4** (palette 255 transparent, draw method 1) | H |
| +0x20 | translucent | non-zero → T+0x48 bit1 → proj+0x6C flag **0x8** (0xF0..0xFE blended, needs 0x4 too) | H |
| +0x24 | u start | T+0x1C → proj+0x44 (sprite u start, 16.16 in 128ths of the texture) | H(copy)/M(meaning) |
| +0x28 | v start | T+0x24 → proj+0x4C | H/M |
| +0x2C | u length | T+0x20 → proj+0x48 (0x7FFFFF ≈ 128 = whole width) | H/M |
| +0x30 | v length | T+0x28 → proj+0x50 | H/M |
| +0x34 | frame count | T+0x3E u16 | H |
| +0x38 | directions | T+0x42 byte (1 everywhere in verid1) | H |
| +0x3C | speed | T+0x34, 16.16 value S: move per frame = `v·dt_ms >> 16`, so **units per second = S · 1000 / 65536** (S as a real number); 7000.0 (0x1B580000) → 106.8 units/s | H |
| +0x40 | fire sound | section-3 sound index, played by the spawn wrapper 0x440334 at the spawn point; −1 none | H |
| +0x44 | impact sound | played by game.vt170 at the hit object (x,y,z = +0x1C,+0x24,+0x20) or at the player (game+0xD4/D8/DC) | H |
| +0x48 | damage | event 16/59/9 payload, player damage amount; player weapon items rewrite it from their power setting (0x44A0DC) | H |
| +0x4C | team | attacker team: actors with `+0xD4 == team` take no default damage; passed as the player-damage "kind"; SET_ACTOR_WEAPON copies it to actor obj+0x84 | H |
| +0x50 | – | never read by the builder (0 in verid1) | H |
| +0x54 | N | kind 1: number of trail segments; kind 3: particle count (T+0x4C) | H |
| +0x58 | spacing / spread | kind 1: segment spacing 16.16 (T+0x50 = value<<8); kind 3: angular spread, low u16 (T+0x50 word) | H |
| +0x5C | impact weapon | index into game+0x78 (T+0x0C = sub type spawned at the impact point), −1 none | H |
| +0x60 | runtime | overwritten with the original spawn fn (the wrapper chains to it); file value 0 | H |

Every type gets T+0x48 bit 0x8 ("collides", sweep test); kinds 1 and 2 also get **0x10** ("reports hits":
engine code 20). Kind 3 has no 0x10 so particle hits are silent.

### Projectile type T (0x54 bytes)
`+0 move(proj)`, `+4 spawn(T, pos*, ang*)`, `+8 hit(proj, hit) → next proj`, `+0xC impact sub-type`, `+0x14 lifetime`,
`+0x18 → section-4 record`, `+0x1C..+0x28 u/v`, `+0x2C half width`, `+0x30 height`, `+0x34 speed`, `+0x3C tex`,
`+0x3E frames`, `+0x40 anim rate`, `+0x42 dirs`, `+0x43 = log2(dirs)`, `+0x44 = 16 − log2(dirs) − dirflag`,
`+0x45 dirflag`, `+0x48 flags`, `+0x4C N/count`, `+0x50 spacing<<8 / spread`.
Defaults (0x4B6EF0): move 0x4C5170, hit 0x4C5030, spawn 0x4B6DD0. Then for kinds ≠ 4: spawn = **0x440334**
(play +0x40 sound at pos, then call the saved spawn +0x60).

| kind | move | spawn | hit |
|---|---|---|---|
| 1 beam head | 0x4C5170 | 0x4B7430 | 0x4B7170 |
| 1 trail "dormant" (T+0x54) | 0x4B7930 (none) | – | 0x4B72B0 (→ active), lifetime 0x8000 |
| 1 trail "active" (T+0xA8) | 0x4B75C0 (follow) | – | 0x4B7290 (hide), lifetime 0x7FFF |
| 2 bolt | 0x4C5170 | 0x4B6DD0 | 0x4C5030 |
| 3 burst | 0x4C5170 | 0x4B6C50 (returns NULL) | 0x4C5110 (bounce) |
| 4 none | 0 | 0x4403CC (NULL) | 0 |

### verid1 section 4 (12 records + terminator)
| # | type | anim rate | life ms | tex | half-w | height | dirflag | key/trans | u0,v0,us,vs | frames | dirs | speed (u/s) | fire snd | hit snd | dmg | team | +50 | N/count | spacing/spread | impact |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 1 beam | 500 | 10000 | 156 | 0.125 | 0.25 | 0 | 1/0 | 0,0,128,128 | 1 | 1 | 7e+03 (106.8) | 86 | -1 | 4 | 3 | 0 | 100 | 0.1 | 3 |
| 1 | 2 bolt | 150 | 5000 | 152 | 0.25 | 0.5 | 0 | 1/0 | 0,0,128,128 | 4 | 1 | 3e+03 (45.8) | 39 | -1 | 6 | 6 | 0 | 0 | 0 | -1 |
| 2 | 1 beam | 500 | 10000 | 162 | 0.125 | 0.25 | 0 | 1/0 | 0,0,128,128 | 1 | 1 | 6e+03 (91.6) | 82 | -1 | 4 | 5 | 0 | 50 | 0.15 | 4 |
| 3 | 3 burst | 320 | 1000 | 156 | 0.075 | 0.15 | 0 | 1/0 | 0,0,128,128 | 6 | 1 | 150 (2.3) | -1 | -1 | -1 | 7 | 0 | 40 | 5461 (30.0 deg) | -1 |
| 4 | 3 burst | 320 | 1000 | 161 | 0.075 | 0.15 | 0 | 1/0 | 0,0,128,128 | 6 | 1 | 150 (2.3) | -1 | -1 | -1 | 11 | 0 | 40 | 5461 (30.0 deg) | -1 |
| 5 | 1 beam | 500 | 10000 | 162 | 0.125 | 0.25 | 0 | 1/0 | 0,0,128,128 | 1 | 1 | 6e+03 (91.6) | 82 | -1 | 5 | 4 | 0 | 80 | 0.15 | 4 |
| 6 | 2 bolt | 320 | 35000 | 204 | 30 | 30 | 0 | 1/0 | 0,0,128,128 | 4 | 1 | 750 (11.4) | -1 | -1 | -1 | 10 | 0 | 0 | 0 | -1 |
| 7 | 1 beam | 500 | 5000 | 156 | 0.125 | 0.25 | 0 | 1/0 | 0,0,128,128 | 1 | 1 | 4.5e+03 (68.7) | 86 | -1 | 4 | 0 | 0 | 100 | 0.1 | 3 |
| 8 | 2 bolt | 150 | 5000 | 152 | 0.25 | 0.5 | 0 | 1/0 | 0,0,128,128 | 4 | 1 | 3e+03 (45.8) | 39 | -1 | 4 | 6 | 0 | 0 | 0 | -1 |
| 9 | 2 bolt | 150 | 5000 | 152 | 0.25 | 0.5 | 0 | 1/0 | 0,0,128,128 | 4 | 1 | 3e+03 (45.8) | 39 | -1 | 8 | 6 | 0 | 0 | 0 | -1 |
| 10 | 1 beam | 500 | 10000 | 162 | 0.125 | 0.25 | 0 | 1/0 | 0,0,128,128 | 1 | 1 | 6e+03 (91.6) | 82 | -1 | 2 | 5 | 0 | 80 | 0.15 | 4 |
| 11 | 1 beam | 500 | 10000 | 162 | 0.125 | 0.25 | 0 | 1/0 | 0,0,128,128 | 1 | 1 | 6e+03 (91.6) | 82 | -1 | 6 | 5 | 0 | 80 | 0.15 | 4 |
| 12 | end | | | | | | | | | | | | | | | | | | | |

speed column: raw 16.16 value (units/s). verid1 actors use weapons 1, 2, 8, 9, 10, 11 (SET_ACTOR_WEAPON).
Records 0 and 7 (team 3/0, sound 86) are presumably the player's phaser types (item +0x2C index; M).
Record 6 (30×30, damage −1, 35 s) would heal the player by 1 if it hit (L: probably unused).
Textures 152..167 are 64×64; 204..207 are 256×128.

## 2. The weapon object (actor +0x7C) and SET_ACTOR_WEAPON

* The "weapon" **is the projectile type T** (`game+0x78[i]`). Default `*0x5522BC` = static 0x579F20 whose spawn
  (+4) = 0x4BF3C0 = `return NULL` (so an unarmed mode-3 actor animates but fires nothing). H
* SET_ACTOR_WEAPON (0x4575F1): `w = (type == -1) ? NULL : game+0x78[type]`; all parts' record+0x7C = w;
  `obj+0x84 = w->+0x18->+0x4C` (record team) or 0. obj+0x84 is returned by actor vt258 (the team used by the
  actor-vs-actor hit path vt1A4). NB a −1 weapon sets record+0x7C = NULL, and the fire routine calls
  `w->vt[4]` without a NULL check (crash in the original if such an actor enters mode 3). H
* Fire (actor mode 3, 0x4C4460; H):
  * `h = rec+0x108` (heading), `(s,c) = T[h]`; muzzle offset (ox,oz,oy) = (+0x84, +0x88, +0x8C):
    `pos.x = x + ox·c − oy·s`, `pos.y = y + oy·c + ox·s`, `pos.z = z + rec+0x30 + oz`, pos[3] = rec+0x28 (copied).
  * Aim at the context target point P (x,z,y at ctx+0x44/48/4C): `dx,dy,dz = P − pos`,
    `heading = atan2(dy, dx)` (0x4BC560(ecx=y, edx=x)), `dist = 0x4C8B90(dx,dy)` (2-D), `pitch = atan2(dz, dist)`;
    angles = {heading, pitch, ?, extra speed 0}.
  * Posts engine code **18** (id 0, rec = actor) → event **36** to the actor ("fired"; events.md calls this
    "reached/turned to target"; the call site 0x4C4590 is in the fire routine).
  * `p = w->vt[4](w, &pos, &angles)`; if p: `p+0x18 = actor record` (shooter, skipped by the sweep).
* Spawn wrapper 0x440334(T, pos, ang): if record+0x40 != −1: sound.SetPos(snd, pos.x, pos.y, pos.z)
  (0x41EE5B) + Play (0x41F1A4); then `return record+0x60(T, pos, ang)`.
* Player fire (0x4B4E10, from the pick code 0x4BC863): weapon `*0x57D960` (= game+0x78[selectedItem.vt60()],
  set at 0x43FE1B); muzzle = camera + (0x57D974 lateral, 0x57D978 up, 0x57D97C forward) rotated by
  (0x57D9D0, 0x57D9CC); aimed at the crosshair point, extra speed from float 0x57D970; `p+0x18 = −1`
  (never hits the player), then an immediate sweep from the player centre to the muzzle catches point-blank hits.

## 3. Projectile runtime record (0xA0 bytes) and update 0x4C4C70

Pool: blocks of 32 × 0xA0 (0x4C4E30, calloc → zeroed), free list per block, more blocks on demand.
Active list head `*0x575654` (+4 next, +8 prev; new ones are pushed at the head).

| off | meaning |
|---|---|
| +0x00 | chain: trail segment behind this one (beam); freed with it (0x4B7150) |
| +0x04/+0x08 | active list next / prev |
| +0x10 | current texture pointer (`*0x5744B0[tex]`) |
| +0x18 | shooter: actor record, −1 = player, 0 = none |
| +0x1C/+0x20/+0x24 | x, z, y |
| +0x28 | pos[3] (copied from the spawn position, unused) |
| +0x34 / +0x38 | sprite half width / height |
| +0x44..+0x50 | u start, u length, v start, v length |
| +0x54 / +0x58 | collision radius r / half height |
| +0x6C | flags: 0x100 visible+drawn, 0x200, 0x400 set at spawn; 0x4/0x8 from record +0x1C/+0x20; 0x20 mirror (set per frame) |
| +0x70 | type T |
| +0x74/+0x78/+0x7C | velocity x, z, y (16.16 per 65536 ms) |
| +0x80 | speed = ang[3] + T.speed |
| +0x84..+0x8C | beam: trail offset D |
| +0x90 | beam head: ms·256 per segment |
| +0x94 | age, ms |
| +0x98 / +0x9A | heading / pitch |
| +0x9C | animation phase |

Spawn 0x4B6DD0(T, pos, ang): sets the fields above, then velocity (0x4B6F80):
```
vx = S·cos(pitch)·cos(heading)    vy = S·cos(pitch)·sin(heading)    vz = S·sin(pitch)      (S = +0x80)
```
Per frame, per projectile (0x4C4C70; H):
1. `age += dt`; if `age > T.lifetime` → `T.hit(p, 0)` (expiry), continue with the returned next.
2. old = (x, z, y, r); `T.move(p)`; default move 0x4C5170: `pos += (v · dt) >> 16` (each axis). **No gravity, no drag.**
3. If `T.flags & 8`: `hit = sweep(p, &old)` (0x4BA9E0); hit != 0 → `T.hit(p, hit)`.

### Sweep 0x4BA9E0 (segment old → new, thickness r; returns the nearest hit and moves p to the hit point)
* No movement → 0. Otherwise every test only shortens the segment, so the nearest hit wins (H):
  1. **Blockmap cells** along the 2-D path (0x4C53B0 / 0x4C5460), objects whose zones (+0x3C/+0x40) meet the
     active zone mask (0x57D9A4/A8 = the player's current floor zones during the projectile pass) and with +0x6C & 0x100:
     * cell+0xC list (count +0x1A): **billboards** — vertical slab `[z0 − r, z0 + h + r]` (z0 = +0x20, h = +0x58)
       and 2-D circle radius `+0x54 + r` (0x4BA1E0).
     * cell+0 list (count +0x14): **walls** — `[bottom +0x30 − r, top +0x2C + r]`, plane test 0x4BA670 with
       radius 0: the start must be on the wall's front side (normal +0x1C/+0x24, d +0x28) unless wall flag
       0x8000 (two-sided), the end behind it, and the crossing between the two vertices. So a shot passes
       through one-sided walls from behind.
     * cell+4 list: **flats** (not 0x1000): moving down (dz<0) → floors (entries [0, +0x16)), up → ceilings
       ([+0x16, +0x16+0x18)), skipped if dz = 0; plane z = +0x20 (no r), point-in-polygon 0x4B69F0.
  2. **Actors** (list 0x57A17C, all zones) with radius +0x54 != 0, slab `[z − r, z + h + r]`, circle `+0x54 + r`,
     **skipping p+0x18 (the shooter)**.
  3. **Type-7 boxes** (0x5743A0 × 0xC4, count 0x57439C) with +0x6C & 0x100 and radius != 0, same test.
  4. **Player**, unless p+0x18 == −1: circle centre `(X − [0x57D9D0], Y − [0x57D9CC])` (camera 0x57D9B8 x,
     0x57D9C0 y; the two globals are the 16.16 pair of the view heading), radius **0x57D998** (half width, default
     1.2, no + r), slab `[Zeye − 0x57D98C, Zeye + 0x57D990]` (eye height default 6.0, headroom 0.51).
     Plus a top cap when moving down: crossing `Zeye + 0x57D990` within the square |Δx|,|Δy| ≤ R of (X, Y). Result −1.
* Hit records carry their kind at +0x69: 1 wall, 2/3 flat, 4/5 billboard, 6 actor, 7 box.

### Hit handlers
* **Bolt 0x4C5030**: free chain; if T&0x10 post engine **code 20** (id = T, rec = hit, also on expiry with 0);
  if T.sub: unless expiry, reflect the angles (0x4B70C0: wall → `heading = 2·wallAngle(+0x20) − heading` and
  nudge 1/64 unit along it; flat → `pitch = −pitch`; billboard/actor/box → `heading += 0x8000`; player (−1) →
  `heading += 0x8000`), then `T.sub.spawn(T.sub, &p.pos, {heading, pitch, ?, p.speed})` (so the impact effect is spawned
  even on expiry, at the end point); free p.
* **Beam head 0x4B7170**: expiry → free chain + head. Hit → code 20, sub-spawn (as above), then the head becomes the
  static "spent" type 0x57A590 (moves on, no collision, hidden: +0x6C &= ~0x100), age so that it expires after
  `(N+2)·dt_seg` ms; active trail segments are hidden one per `dt_seg` from the head backwards (they keep
  following the invisible head through the wall), dormant ones get a short life. Then everything is freed.
* **Burst particle 0x4C5110**: hit on wall/flat/billboard/box → reflect (0x4B70C0), recompute velocity, keep
  going (bounces). Expiry, actor or player hit → free. No events, no damage.

### Beam (kind 1) spawn 0x4B7430 (H)
`D = spacing · dir(heading, pitch)`; `dt_seg = fixdiv(spacing<<8, speed+ang[3])` (= ms·256 per spacing).
Creates N+1 dormant segments (type T+0x54, all at the muzzle, +0x84 = D, ages staggered so segment k (counted from
the head) activates after k·dt_seg/256 ms), then the head (type T, +0x90 = dt_seg, +0 → last segment). An active
segment (T+0xA8) sits at `(the segment in front).pos − D` every frame. Only the head collides. With verid1 numbers
(speed 7000, spacing 0.1, N = 100) the beam is a 10-unit streak of 101 sprites, formed in ≈ 94 ms.

### Burst (kind 3) spawn 0x4B6C50 (H)
For i < count: `m = (spread · rand17) >> 17` (0..spread), θ random; angles = `(heading + m·sin θ, pitch + m·cos θ)`;
extra speed `e = (T.speed or ang[3]/4) · (1 + rand17/65536)`, final speed = T.speed + e (so 2..4 × T.speed);
initial age = `lifetime · rand15 >> 16` (0..lifetime/2). Spawned from the impact reflection, so the spray bounces back
off the surface. Returns NULL.

### What a hit does: game.vt170 = 0x43696F(T, hit) via code 20 (H)
1. hit == 0 (expiry) → nothing. hit == −1 → player. record = T+0x18; record kind 3 → nothing.
2. Impact sound record+0x44 (if != −1) at the player position (game+0xD4/D8/DC) or at the hit object.
3. Object hit: id/target by kind: 1 wall → vt150(+0x14), id |0x4000; 2/3 flat → vt168 then **falls through**
   to the billboard case (target = vt160(+0x14), id |0x2000 — original bug); 4/5 billboard → vt160, |0x2000;
   6 actor → vt15C, |0xA000; 7 box → id |0xA000, no target.
   * **broadcast 59** with 4 words **(id, damage, damage, team)** (record +0x48, +0x48, +0x4C);
   * target gets **event 16** with 3 words (damage, damage, team) (obj.vt180).
   * Actors without a script handler for 16 (all verid1 actors except a001 state 6) run the native default
     vt1A0 = 0x457B29: `if actor+0xD4 != team: TakeHit(damage, power = damage)` (0x45747E: thresholds +0x90/+0x94/+0x98,
     events 38..42). Walls/billboards: scripts (e.g. wall-hit sound handlers).
4. Player hit: **game.vt34C(src 0, damage, team)**. No event 59/16.

## 4. Drawing (0x4C77E0, list builder for the projectile list; H)
* Each projectile with +0x6C & 0x100 becomes a **sprite item (type 4) through the billboard projector 0x4C5820**:
  a camera-facing quad 2·(+0x34) wide, +0x38 high, base at z − +0x58 (centred), u/v from +0x44..+0x50.
  No distance limit test (unlike billboards). Light: item+0x54 = +0x68 byte = 0 (never set) → shade table 0,
  i.e. effectively full-bright; flag 0x10 never set.
* Method: flag 0x4 (record +0x1C) → keyed (255 transparent); + 0x8 (record +0x20) → translucent 0xF0..0xFE;
  without 0x4 → fully opaque (magenta drawn). verid1: all keyed, none translucent.
* Animation: if frames > 1: `f = (age·rate >> 16) + phase`, wrapped mod frames (phase re-based).
* Direction: `a = atan2` from the viewer point (X − 2·[0x57D9D0], Y − 2·[0x57D9CC]) to the projectile;
  `d = (heading + a) >> (16 − log2 dirs − dirflag)`; if d > dirs: `d = 2·dirs − d` and set mirror flag 0x20.
  Texture = `first + d + (f << log2 dirs) + (dirflag & f)` (so dirs+dirflag images per frame).
  verid1: dirs 1, dirflag 0 → texture = first + f.
* Type-7 boxes / method 2 (0x4C8EF0) are not involved.

## 5. Player damage game.vt34C = 0x43A42B(src, dmg, kind) (H)
* If `*0x5429FC` (damage enabled; initialised 1, never written by code) and dmg != 0:
  * if kind != 12 and a shield is worn (game+0x2E0, handler +0x44, item kinds 9/10, vt38 = +0x18 active):
    `dmg = shield.vt3C(dmg)` = 0x44B182: if charge (+0x10) > 0 the **whole hit is absorbed** (charge −= dmg,
    or charge = 0 and vt34 deactivate when charge <= dmg; the excess is lost); returns 0. If the shield is no longer
    active afterwards → event **48** to the shield item.
  * if dmg != 0: `health(game+0x200) −= dmg`, **clamped to ≥ 1**; vt35C(health): clamp to max game+0x204, set the
    health bar (game+0x2A8 +0x9C) and redraw.
* If dmg != 0 (after the shield): **broadcast event 9 (src, dmg, dmg, kind)** — projectile hits: (0, damage, damage, team).
* **No invulnerability timer**, no knock-back, no engine death: health can never go below 1 here.
  Death is scripted: verid1 object 0xE004 handles event 9: reads GET_HEALTH (C6); ≤ 16/15 and ≥ 6 → hurt
  overlay C3(0,10|8) + signal; ≤ 5 → C3(0,11|9); **== 1 → death**: trigger 0xE00B/0xE007, set global 6 = 666 or
  signal 666 to 0x2003, disable objects, and (state 2 handler) a 2 s / 1.5 s / 1 s fade to black (BA 2000,1500,1000,
  100%, 0,0,0, notify self). M (script reading).
