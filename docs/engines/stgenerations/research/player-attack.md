# Player attack, picking and line of sight (sgens.exe)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Confidence: H = read from code, M = code + data/inference, L = guess.
"game" = CGame (vtable 0x531768), "view" = 3D view window game+0x274 (vtable 0x531610).
Fixed point is 16.16 unless noted. Angles are 16-bit (0x10000 = 360 deg).

**Corrections to earlier notes.**
* events.md said the code-19 broadcast 59 carries (id, x, y, z) and that event 16 carries (x, y, z).
  Both are wrong. Event 59 carries **(hitId, damage, power, team)** and event 16 carries
  **(damage, power, team)** (0x436845..0x436884). The same holds for projectile hits (code 20,
  0x436BC5).
* events.md said vt3A0 is a "look/examine" pick that sends broadcast 50. It is actually **use the
  active item / fire the weapon** (§3).

## 1. Input → action (H)

* Key down: game.vt118 = 0x43E8FB(vk). Key up: game.vt11C = 0x43F2BC(vk).
  `action = keymap[vk]` (byte table 0x545500, 256 entries). The defaults are static data. At start-up
  and on options changes the map is copied in from the config object *0x565AB0 (0x488913, called
  from 0x433CC3 and 0x436E9B), so it is user-remappable (M).
* Mouse: the view's button handlers 0x4420A7 / 0x4420E5 (L down/up) call game.vt118/vt11C(**1**),
  and 0x442123 / 0x442161 (R down/up) call them with **2**. They go through the key map with vk 1/2
  and only run when vt134() (3-D mode) is true.
* Default map entries relevant here:
  * vk 0x11 (**Ctrl**) → action 19 → `game+0x254 = 1`.
  * vk 0x01 (**left mouse button**) → action 20 → `game+0x258 = 1`.
  * vk 0x02 (right button) → action 52 → game.vt120(1), something else.
  Others: arrows 0x25..0x28 → 6, 1, 7, 2; numpad keys; Space and Num0 → 34; and so on.
* Key up of action 19 or 20 → `game+0x2E8 = 0` (the fire latch is released). 0x43F552 (focus lost
  and similar) clears every input flag, including 0x254, 0x258 and 0x2E8.
* Per frame, in game.vtDC 0x43AD04 at 0x43CF95 (the game update, before the engine tick and render):
  ```
  if ((g254 || g258) && !g2E8):
      if active item (game+0x2DC):
          if vt3AC() (= game+0x2D4 && item.vt34() "enough charge"):
              if !game+0xF8: 0x57D970 = 0     (player speed handed to the projectile)
              vt3B4()                          → fire the item (§3)
          else: g2E8 = 1; g254 = g258 = 0      (press swallowed, nothing happens)
      else:
          view.vt12C(0)                        → start the punch animation (§2)
          g2E8 = 1; g254 = g258 = 0
  ```
  So you get **one action per press**. Keyboard auto-repeat sets the flag again, but the latch stays
  set until key up. A punch press while the punch animation is still running is swallowed:
  vt12C only starts when the overlay is idle (state 1).
* game+0x2D4 = "item usable": set by game.vt3B8 / cleared by vt3BC from SET_ACTIVE_ITEM 0x439F9E
  (1 when an item is made active, 0 for bare hands).

## 2. The punch (bare hands) (H unless noted)

### 2.1 SET_PUNCH (native 0xC4 → game.vt460 0x44179C)
Script push order is a9 first and a1 last (a1 is on top):
`a9 hitVol, a8 hitSound, a7 swingVol, a6 swingSound, a5 power, a4 damage, a3 hitFrame, a2 msPerFrame, a1 image`.

| arg | stored | meaning |
|---|---|---|
| a1 | bitmap resource #a1 (section-1 table, 0-based) → view sprite +0x948 | first-person overlay (.OVR). verid1: 6 = KIRKHIT.OVR, 7 = PICHOP.OVR |
| a2 | view+0x96C | ms per overlay frame |
| a3 | view+0x974 | frame on which the pick is made |
| a4 | game+0x260 | damage |
| a5 | game+0x264 | power (compared with the actor thresholds) |
| a6/a7 | view+0x978; `SET_SOUND_VOLUME(a6, a7)` (0x41ED07, which permanently sets the sample's default volume) | swing sound, played when the animation starts (-1 = none) |
| a8/a9 | game+0x268; `SET_SOUND_VOLUME(a8, a9)` if a8 != -1 | sound played on a hit |

verid1 calls:
* e001 / e007: (7, 100, 3, 3, 9, 0x15, 63, -1, 0).
* e002: (7, 64, 2, 3, 10, -1, 0, -1, 0) and (6, 64, 3, 3, 9, 0x0F, 64, -1, 0).

Frame height = the 3-D viewport height (view word +0x7A; 0x4029A5). Frame count =
OVR rows / height (view+0x95C). KIRKHIT: 1260 rows, so 5 frames of 504×252.

### 2.2 Overlay state machine (view+0x964; update view.vt130 = 0x431E57(dt))
* It is called every frame from view.vtF0 0x4309C5 at 0x430C2C, **after** the 3-D render 0x4B5A90.
  dt = frame ms [0x546D68] (0 when paused).
* vt12C 0x431DC5(rev) starts it only if the sprite is loaded and state == 1:
  state = 2, elapsed (+0x968) = 0, +0x970 = rev (always 0 here), plays sound +0x978 (0x41F1A4(s,0,0)).
* State 2: if rev, state 4 and frame = count−1; else state 3 and frame 0. +0x97C (picked) = 0.
* State 3: `elapsed += dt; f = min(floor(elapsed/msPerFrame), cur+1)`. So at most one frame per tick
  and no frames are skipped. If `f >= count` → state 1 (idle, not drawn); else frame = f.
* State 4 is the mirror image (counting down). State 5 → 1.
* In states 2..4 the overlay is drawn (+0x68.vt94(sprite, 1, y = view.vt118())). After that:
  `if (!+0x97C && cur == hitFrame) { game.vt3C4(); +0x97C = 1; }`.
* So the pick is requested on the first tick that shows frame `hitFrame`, at ≈ hitFrame·msPerFrame
  ms (and at least hitFrame+1 ticks) after the press. The cooldown is the whole animation: about
  count·msPerFrame ms. KIRKHIT with e001's values: pick at ~300 ms, done at ~500 ms.

### 2.3 game.vt3C4 0x43A39F: request the pick
`game+0x25C = 1` (the "punch pick" flag). GetClientRect(view) gives the point
`x = right/2, y = bottom/2 + view.vt118()`. vt118 = (view+0x7C − word view+0x7A)/2, the offset of
the 3-D viewport inside the window. That point is the **centre of the 3-D view**.
0x4B6660(x, y): if no pick is pending (0x552164 == 0), set 0x552164 = 1 (mode 1) and call 0x4BC740:
pick point 0x579FD0/D4 = (x, y), best key 0x579FC4 = 0x80000000, best record 0x579FC0 = 0.

The request is made after this frame's render, so it is resolved during the **next** frame's render,
and the resulting code 19 is delivered at the following tick's flush.

## 3. Weapon/item fire (active item) (H unless noted)

game.vt3B4 0x43A2FD: the point is view.vt114() = the **cursor point** view+0xD8/+0xDC
(y += view.vt118()). Then game.vt3A0 0x43FDC4(x, y):
```
ctl = item+0x44 (item controller)
ctl.vt30()                      use: if charge >= cost, charge -= cost (0x449F20); item-specific effects
broadcast 50 (1 word: ctl.vt38())  (= the cost/damage for charge items)
0x57D960 = game+0x78[ctl.vt60()]   weapon stage object (section-4 record index = ctl+0x2C)
view.vt134(ctl.vt2C())         sets the muzzle offsets 0x57D974/78/7C and view+0xAC = flash ms (§3.2)
0x4B6870(x, y)                 pick request, mode 2
if ctl.vt5C() == 0: g2E8 = 1; g254 = g258 = 0     (single shot; vt5C is 0 for kinds 2..13)
```
This runs in the game update **before** the render, so the pick resolves in the same frame.

### 3.1 Item controllers (native 0x82 ATTACH_ITEM_BEHAVIOUR; factory 0x449918)
* The pops are id (top), kind, b, c (the deepest, pushed first). Kinds 2..6 and 11..13 are charge
  items (vtables 0x532BA0.., base 0x449D19):
  * +0x2C = **c = weapon record index**, +0x0C max, +0x14 charge, +0x24 setting (native E9),
    +0x18/+0x1C cached cost.
* vt38 = 0x449E4D: `cost = damage = max(1, section4[c].+0x48)`.
* vt58 = vt38 (power), vt34 = (vt38() <= charge), vt5C = 0 (no auto-fire), vt60 = c, vt2C = setting.
* vt20 = "team" of the item: kind 2 → 3, kinds 3/4/5 → 4, kind 6 → 5, kinds 11/12 → 7, kind 13 → 8.
* verid1:
  * phaser 2002: `82(kind 2, b 3, c 0)`, which gives record 0: damage 4, team(+0x4C) 3.
  * 212C..: `82(6, 5, 5)` → record 5 (damage 5, team 4). `E9 0x12C` = 300 ms flash.

### 3.2 Muzzle offsets and flash (view.vt134 = 0x4320D1(setting))
* view+0xAC = setting. While it is > 0, the held-item sprite (+0x80) shows its "firing" frame
  (+0xC8 − 6); it counts down by the frame ms (0x430C58). So the setting is a muzzle-flash display
  time, not a power level (M).
* The offsets, with (base) = view+0x454.. (or +0x460.. if view+0x478):
  * forward `o74 = base0`.
  * up `o78 = base1 + pitch(0x57D9D4)·0x18000/(−0x2000) − 0.5 + crossY(+0xDC)·(−0x6000)/view+0x474`.
  * side `o7c = (W/2 − crossX(+0xD8))·base2/(W/2)` with W = word view+0x78.
  * The muldiv is integer a·b/c (0x4321DE).
  * base0..2 are copied by 0x430E06 from per-item tables when the held-item image is set. So the
    muzzle follows the gun sprite and the cursor (M).

### 3.3 Mode-2 resolve: 0x4B4E10 spawns a projectile (H)
After the render (0x4BC820), with `z = 2^44/bestKey − 1.0` (the camera depth of the picked pixel):
```
if z <= R (0x57D998, player radius): z = W/2 in 16.16 (0x57D840)   (also when nothing was hit:
                                                                    the key 0x80000000 gives z < 0)
s   = (z + 1.0) · (2/W)            (0x57BEC0 = 2/W; focal length = W/2, 90 deg horizontal FOV)
camX = (W/2 − px)·s                (left positive), with px, py the pick pixel
camY = (H/2 − py)·s − z·shear      (shear 0x57D9D8 = the look up/down y-shear)
f = z − o74;  u = camY − o78;  l = camX − o7c
yaw   = atan2(l, f) + heading(0x57D9C8)      (0x4BC560 table atan2)
pitch = atan2(u, f / cos(yaw_offset))        (0x4BC5F0 gives the horizontal length)
speed = int(0x57D970 · 65536)                (player forward speed; 0 unless +0xF8)
origin = eye + o74·fwd + o7c·left + (0, 0, o78)
        (fwd = (0x57D9D0, 0x57D9CC); x = ex + o74·c − o7c·s, y = ey + o7c·c + o74·s, z = ez + o78)
proj = weaponStage(0x57D960).vt4(stage, &origin, &{yaw, pitch, speed})
proj+0x18 = −1                                (owner = player)
if 0x4BA9E0(proj, from = eye − fwd·1.0): proj.stage.vt8(proj, hit)   (muzzle inside a wall → immediate hit)
```
* The stage vt4 is wrapped (0x440334) to play section-4 record +0x40 at the spawn point: the fire
  sound, 86 in verid1 record 0.
* So **the player's phaser is a real projectile** that flies from the muzzle toward the 3-D point
  under the cursor. It is not an instant hit, and code 19 is not used.

### 3.4 Section-4 weapon record (100 bytes, game+0x70 → stage objects game+0x78[i]; 0x43FE9C) (M)
* +0 type:
  * 1: bolt (class 0x4B7350, hits reported, flag 0x10).
  * 2: (0x4B6EF0, hits reported).
  * 3: effect (0x4B6C00; vt170 ignores its hits).
  * 4: no spawner.
* Fields: +4 → stage+0x40 (word), +8 → +0x14, +0xC → +0x3C (image), +0x10/+0x14 → +0x2C/+0x30,
  +0x1C/+0x20 → flag bits 0/1, +0x24..+0x30 → +0x1C..+0x28, +0x34 → +0x3E, +0x38 → +0x42,
  +0x3C → +0x34 (speed?), +0x54/+0x58 → class init.
* +0x40 = fire sound, +0x44 = impact sound (at the hit), **+0x48 = damage (also the power)**,
  **+0x4C = team**, +0x5C = next stage index (impact effect), and stage+0x18 points at the record.
* verid1 record 0 (phaser):
  `type 1, 500, 10000, img 0x9C, 0.125, 0.25, …, +0x3C 7000.0, snd 86, -1, dmg 4, team 3, 0, 100, 0.1, next 3`.
* Hit (code 20 → game.vt170 0x43696F(stage, rec)):
  * Plays impact sound +0x44 at the hit.
  * Non-player rec: hitId = rec+0x14 | type bits (wall 0x4000, flat 0xC000, billboard 0x2000,
    actor/kind-7 0xA000), with no range test. Broadcasts **59(hitId, dmg, dmg, team)** and sends
    **16(dmg, dmg, team)** to the object.
  * rec == −1 (the player): vt34C(0, dmg, team).

## 4. The pick test (code 19 source) (H)

Picking happens inside the renderer, at one pixel (px, py). Candidates are compared by the sort key
`key = 2^44/(depth+1)`, where depth is the camera-space depth from the eye. The candidate with the
**largest key (nearest)** wins.
* **Walls and sprites** (billboards, actors, boxes: all column-list items): column clipper 0x4C0AD0
  at 0x4C1293. In column px, for every emitted visible span with `top <= py < bottom`
  (already clipped by nearer occluders), it calls 0x4BC770(item, span):
  ```
  if item.key > best:
     flags = item.rec+0x6C
     if flags & 0x400: no
     elif (flags & 0x200) && (byte [0x552168] & 1)      (0x552168 is static 0xFF: on)
          && item.method(+0x10) == 0:                   (keyed column)
          hit = texel(row py) != 0xFF
     else: hit = true                                   (span coverage only)
     if hit: best = item.key; bestRec = item.rec
  ```
  * Sprites are therefore hit by their visible per-column span. For flag-0x4 sprites the spans are
    already cut to the opaque rows (column class 2 with flag 2) and fully transparent columns
    (class 0) are skipped. Columns with holes are hit-tested per texel only when the column method
    is keyed (0).
  * Actors (verid1 flags 0x206) use method 1/3, so their hit area is the column span, not the exact
    pixels.
* **Floors and ceilings**: row renderers 0x4C84A0 / 0x4C87D0. On row py, for the span
  `[start, end]` containing px, 0x4BC7F0 compares flat+0x114 (key) and takes flat+0x104 (record).
  There is no flag test.
* Resolve 0x4BC820 (end of 0x4B5A90):
  `dist = 2^44/bestKey − 1.0` (16.16), i.e. the depth of the hit point (sprite: its centre plane).
  Mode 1 → queue code 19(id = dist, rec = bestRec or 0); mode 2 → 0x4B4E10. Then the request is
  cleared.

## 5. Code 19 handling: game.vt16C(19, dist, rec) at 0x4364B8 (H)

```
if game+0x25C:  dmg = game+0x260, power = game+0x264, team = 2       (punch)
else:           dmg = item.vt38(), power = item.vt58(), team = item.vt20()   (dead path: only
                                                                       vt3C4 requests mode 1)
kind = rec ? rec+0x69 : 0;  hitId = 0;  target = none
R = 0x57D998 (player radius, default 1.2 = 0x13333; native D9 SET_PLAYER_WIDTH sets width/2)
kind 0:    "miss" (debug log only)
kind 1:    if dist <= R + 0.5:          target = wall  vt150(rec+0x14), hitId = id|0x4000
kind 2,3:  if dist <= R + 0.5:          target = flat  vt168(id),       hitId = id|0xC000
kind 4,5:  if dist <= R + 0.5 + rec+0x54: target = billboard vt160(id), hitId = id|0x2000
kind 6:    if dist <= R + 0.5 + rec+0x54: target = actor vt15C(id),     hitId = id|0xA000
           (also 0x542A04 = id, 0x542A08 = 0: written, never read)
kind 7:    if dist <= R + 0.5 + rec+0x54:                               hitId = id|0xE000 (no object)
if kind != 0: broadcast 59 (hitId, dmg, power, team)     (hitId = 0 when out of range)
if target:    target.SendEvent(16, 3 words: dmg, power, team)
if game+0x25C: if hitId && game+0x268 != -1: play 0x41F1A4(game+0x268, 0, 0)  (non-positional)
               game+0x25C = 0
```
* rec+0x54 is the object's radius. Inside the 0x7FFFFFFF id test (if the object's id is 0) there is
  no target, but hitId is still set.
* So the **max punch reach** is depth ≤ 1.7 for walls/flats, or 1.7 + radius for sprites (depth
  measured from the eye along the view axis).
* There is no event 19 or 45 to the hit object: walls and billboards get **16** like actors. A
  script "use by punch" is a 16 handler (verid1 has 16 handlers on t2 ×4 and t4 ×1).

### 5.1 Event 16 default (no script handler for 16 in the current state)
* SendEvent 0x44F4E1 tries the script first. Otherwise native case 16 calls
  `vt1A0(dmg, power, team)`.
* Actors: 0x457B29: `if actor+0xD4 (team, native F1) != team: TakeHit vt24C(dmg, power)` (0x45747E,
  thresholds +0x90/+0x94/+0x98 as in natives-a.md, events 38..42). Other classes: no-op.
* Example: verid1 actor (full 11, stun 6, notice 40, team 5) punched with (3, 9, team 2):
  9 > 6 → stun −= 3 → event 38, or 41 when stun reaches 0. A punch never kills such an actor
  (power 9 < 11).
* Phaser bolt (4, 4, team 3): power 4 < 6 → no damage, no event, unless the thresholds are lower
  (scripts may handle 16 themselves).

## 6. Crosshair / aim (H for data flow, M for the cursor visuals)
* view.vtD8 0x430756 (per frame): GetCursorPos (or 0x420E36 when 0x56FA2C) → ScreenToClient.
  * If it is outside the 3-D rect (+0x50..+0x5C) and game.vt124() is false, crosshair =
    viewport centre (word +0x78 / 2, word +0x7A / 2).
  * Otherwise clamp to the rect and MapWindowPoints, giving **view+0xD8/+0xDC**.
  * Then view.vtFC(x, y) (hotspot test) chooses cursor shape 1 or 2 (0x4323D0 / 0x432489, SetCursor).
* **Item fire aims at the mouse cursor** (free aim inside the view). **The punch always uses the
  view centre.**
* Vertical aim: there is no separate pitch ray. The view is y-sheared by 0x57D9D8, and screen
  picking automatically includes it. The projectile pitch comes from the unprojection with the
  −z·shear term (§3.3).

## 7. Line of sight 0x4BB900(actor rec) → 1 visible / 0 blocked (H)

Users: only the actor "sees player" test (0x4C3E9A while seen: keep only if d3 < r2 and LOS;
0x4C3EE9 while not seen: enter if d3 < r1 and LOS). The edx argument is ignored. Projectiles and
picking use other code (0x4BA9E0, renderer).
```
mask = actor.floor(+0xC).+0x74/+0x78  |  player zones 0x57D99C/0x57D9A0
A = (rec+0x1C, rec+0x24, z = rec+0x20 + rec+0x88 + (rec+0x38 − rec+0x88)/2)
    = midway between muzzle height (+0x88, verid1 5.2) and the top (+0x38) of the sprite
P = eye (0x57D9B8 x, 0x57D9C0 y, 0x57D9BC z)       (the camera point, not the feet)
d = P − A;  if d == 0: return 0
if |dz| > 2·approxDist2D(dx, dy) (0x4C57F0): return 0          (steeper than ~63 deg)
line through A along d: n = (cos a, −sin a) from the table, c = −n·A  (0x4BA160)
zlo, zhi = min/max(A.z, P.z)
walk the uniform map grid cells crossed by A→P (0x4BC320, 0x4C53B0, 0x4C5460):
  walls in cell (cell+0, count u16 +0x14):
     need zone (w+0x3C & mask.lo || w+0x40 & mask.hi) and (w flags & 0x100)
     need zhi >= bottom(+0x30) − 0.25 and zlo <= top(+0x2C) + 0.25
     t = 0x4BBCD0:  A and P strictly on opposite sides of the wall line (nx,ny,d at +0x1C/+0x24/+0x28;
         the side of A is made positive; P must be < 0)
         and the wall's endpoints, each extended 0.25 along the wall, on opposite sides of line AP
         t = sA/(sA − sP) (4.28 fixed)
     z = A.z + t·dz;  if bottom − 0.25 <= z <= top + 0.25: return 0
  if zlo != zhi: flats in cell (cell+4, count +0x16 + +0x18 = floors + ceilings):
     need zone match and flags & 0x100, and zlo <= h(+0x20) <= zhi
     t = (h − A.z)/dz;  Q = A + t·d (x, y)
     if 0x4B69F0(flat, Q, margin 0.5): return 0     (bbox, then every convex edge n·Q + d + 0.5 >= 0,
                                                     i.e. inside the polygon grown by 0.5)
return 1
```
* Wall flags other than 0x100 are ignored. Windows/masked walls, 0x4000 invisible occluders and
  one-sidedness all block. Doors block or not only through their current geometry.
* Billboards, actors and boxes never block LOS.

## 8. Other notes
* In mode 2 (item fire) the projectile inherits 0x57D970 (player forward speed) unless +0xF8 == 0,
  in which case it is zeroed first (M: the meaning of +0xF8 is not traced).
* 0x4BA9E0 (actor/wall sweep used by projectiles) is also called by 0x4C4CF5 (projectile flight).
* Not traced: projectile flight and lifetime fields; what the item-specific part of ctl.vt30 does
  (the classes 2/3 path calls 0x4B5EF0 and game.vt164(id).vt30(3)); the game.vt120 right-button
  action.
