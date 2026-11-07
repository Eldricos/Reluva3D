# ACTIONSEQ special functions (dispatcher 0x25027..0x25E46)

Sources: full.asm / cv_aseq.asm, listings2/scenes (BRIDGE, BRIDGE2, HABITAT, HABRM1, MAZEA1..3, MAZEB1,
MAZEC1, PROMBAR). Confidence H/M/L per item; guesses are marked (guess).

## 0. Dispatcher, full type map — H

Entry: result (ebp-0xC) = 999; 0x714C8 = 0 (arcade death flag); 0x71454 = 0 (music-stop flag).
0x7142C = id >= 0x100 ? id >> 8 : id (**always written**, not only on the default path). Param = id & 0xFF.

| type | target | what |
|---|---|---|
| 0, 1, 0x1C, 0x1D | 0x251D7 / 0x2529D | plain INTRO/MOVIES videos (cursor_video_notes §5) |
| 2..6, 0x19..0x1B, 0x1E, 0x1F | 0x25347/0x2533D | arcade |
| 7..0x0E, 0x10..0x18 | 0x256AB | TRANS videos |
| 0x0F | default | (defdead is not reachable) |
| 0x80 / 0x81 | 0x2588C / 0x2589B | 0x714E4 = 1 / 0 (tricorder icon) |
| 0xA0 | 0x258AA | 0x714CC = param |
| **0xC0, 0xC1** | 0x258BC | player phaser beam to the cursor (identical; param ignored) |
| **0xC2** | 0x25B0E | enemy shot from the SETRECT hotspot at the player, random hit |
| 0xC3 | 0x25DB5 | no-op (999) |
| **0xF0** | 0x25DBA | 0x71144 = 0xFFFF (tick off), 999 |
| **0xF1..0xF5** | 0x25DC6 | tick metronome + countdown duration (ALL of F1..F5, not just F5) |
| else (0x20..0x7F, 0x82..0x9F, 0xA1..0xBF, 0xC4..0xEF, 0xF6..0xFF) | 0x25E2C | 0x7142C = 0xFFFF, result 999 |

Range tests at 0x25080..0x251D1: `>=0xF0 && <=0xF5` with ==0xF0 split off, so 0xF1..0xF4 share the F5 code.

## 1. 0xC0 / 0xC1 — player phaser shot (0x258BC..0x25B09) — H

1. memset(overlay 0x71020, 1, 640*480) (0x33E80; 1 = transparent).
2. Aim point = cursor centre: ax = [0x559B0] + [0x559CA]>>1, ay = [0x559B2] + [0x559C8]>>1.
3. Draw_Phaser_Beam 0x27215 from (320, 479) (bottom centre of screen) to (ax, ay) into 0x71020,
   colour [0x71EB5] (nearest-white UI colour), pitch 640. Beam = Bresenham, a vertical run per point,
   width 13 px at the start tapering to 1 px at the aim point (arcade_notes §6).
4. Bounding box: left = min(ax,320), right = max(ax,320), top = min(ay,479), bottom = max(ay,479); if
   right-left < 2 widen by 1 each side; same for y.
5. Cursor hidden (0x559C6 = 0); 0x2D562 blits that rectangle of 0x71020 straight to the screen with
   key colour 1; cursor flag back to 1.
6. Append to the "beam overlay" list (index i = [0x71004], u16 arrays, 6 slots, no bound check):
   0x70FE8[i]=320, 0x70FDC[i]=479 (from), 0x71014[i]=ax, 0x71008[i]=ay (to), 0x70FF8[i]=0 (kind phaser),
   0x70FD0[i]=left, 0x70FC4[i]=top, 0x70FB8[i]=w, 0x70FAC[i]=h; [0x71004]++.
   (0x70FE8+12 = 0x70FF4 = SETRECT hotspot: a 7th entry would overwrite it — never happens in practice.)
7. If 0x70FA8 == 0: 0x70FA8 = 3 (life of the overlay in renderer steps; an existing count is not reset).
8. Result 999. **Non-blocking**, no click wait, mouse not moved, palette untouched, parameter ignored.

C0 and C1 are byte-identical in effect. Scripts use C0 (#192) for a shot that **hits** (clicked the target
hotspot: zap/temlyk/barlyk/hablyk/homebank...) and C1 (#193) for a **miss** (the full-screen `shoot`
hotspot 10,10,620,460) — always right after `SOUND phasor`. The script itself does the consequence.

### Renderer side (0x22AE3 step) — H
* 0x22BD4: if 0x70FA8 != 0: --0x70FA8; when it reaches 0, every list entry's rect is restored from
  the clean background 0x71038 into the work buffer 0x71028 (x-3, y, w+6, h; 0x2CD1E) and marked dirty;
  [0x71004] = 0 (0x22DB5).
* 0x23008 (pass 2, before priority-4 animations): if 0x70FA8 > 1 and count > 0, re-draw every entry
  into the work buffer 0x71028: kind 0 → 0x27215 colour [0x71EB5]; kind 1 → 0x2759D (new random jitter).
* So a beam lives for 3 renderer steps (~40-60 ms at 50 Hz): drawn directly by ACTIONSEQ, redrawn on
  step 1 (3→2), left on step 2 (2→1), erased on step 3 (1→0). Level-4 animations draw over it.
* 0x71004 / 0x70FA8 are also zeroed at init (0x10188) and on load (0x1FA04/0x1FA0E).

## 2. 0xC2 — enemy shot at the player (0x25B0E..0x25DB0) — H

1. memset(0x71020, **6**, 640*480) (key colour 6 here, because the beam uses colours 1..3).
2. Source = SETRECT hotspot h = [0x70FF4], record [0x6F3D0 + 4h] (u16 x,y,w,h at +0,+2,+4,+6):
   sx = x + w/2, sy = y + h - 5 (bottom centre of the hotspot, e.g. the drone's `zap` rect).
3. Target: tx = random(620) + 10 (0x1380D, Watcom LCG; 10..629), ty = 460 (0x1CC).
4. **Hit test: 213 <= tx <= 426 (0xD5..0x1AA, the middle third) → result 0xFFFE (65534)**, else 999.
   Hit chance 214/620 = 34.5 %. No dependence on difficulty, mouse or anything else.
5. 0x711C0 = 460 (bottom clip used by the beam; same global as the arcade's video bottom).
6. Enemy beam 0x2759D from (sx,sy) to (tx,460) into 0x71020, pitch 640: Bresenham along the major
   axis, i = 1..len: colour 1 / width 1; after len/3 colour 2 / width 3; after 2len/3 colour 3 / width 5;
   x jitter random(width) - width/2; at each point a vertical streak of 8 px downward, stopped at
   y >= [0x711C0]; colour indices 1..3 of the scene palette.
7. Same bbox/blit (key 6)/overlay-list append as C0 but kind 0x70FF8[i] = 1; 0x70FA8 = 3 if 0.
8. Non-blocking. Result 65534 = player hit, 999 = missed.

Scripts (BRIDGE, BRIDGE2, MAZEA1/A2/A3, MAZEB1, MAZEC1 drone fights): `SOUND phasor|laser; ACTIONSEQ #194,
$ret; IFNE $ret, #65534, >survived; HOTOFF shoot, zap; FADEOUT #4; SOUND death; TIMER deadtime,#10,
>aredead` → DEATH. The drone's `zap` hotspot is moved with SETRECT zap,... (SetZap), which is what makes
0x70FF4 point at the drone.

## 3. 0xC3 — H
Jumps straight to the exit: result 999, nothing else. Not used in data.

## 4. 0xF0..0xF5 — "red alert" countdown metronome (PROMBAR, HABITAT) — H (code), M (script meaning)

### 4.1 Code
* **0xF0** (0x25DBA): 0x71144 = 0xFFFF → tick off. Result 999. Param ignored.
* **0xFn, n = 1..5** (0x25DC6):
  1. If the tick sample 0x711B0 is not loaded: Load_One_Sound("tick", &0x711B0, 16) (0x1D084;
     `\ds9\sound\tick.raw`, 22050 Hz, 16-bit).
  2. t = byte[0x54F13 + param*5 + n] (n = type & 0xF).
  3. d = t + 1 + 2*(2 - difficulty)  (difficulty 0x714E8: 0..2; +5 / +3 / +1).
  4. **0x71144 (period) = d * 64** (ms). Phase 0x71140 is NOT reset.
  5. **Result = d * 10** (= d seconds in 100 ms script-timer ticks).
* Interrupt side (AIL 250 Hz callback 0x2B409 → 0x2B43D): if 0x71144 != 0xFFFF: 0x71140 += 4;
  if 0x71140 >= 0x71144: 0x71140 = 0; Start_Sound(0x711B0, "tick.raw") (0x143A0).
  So a tick every d*64 ms, ~15.6 ticks per countdown whatever d is; it runs in the interrupt, so it
  ticks everywhere (scene, conversations, menus, videos) until 0xF0, a load, or exit. It does not speed
  up by itself: the period changes only when the script re-issues 0xFn with a new location (closer
  location → smaller d → faster tick). **Nothing is drawn, nothing happens at "zero"** in the exe —
  the expiry is the script timer that the script arms with the returned value.
* Nothing else reads 0x71144/0x71140 (xrefs: init 0x10088/0x10092, save 0x1EB80/0x1EB97, load
  0x1F941/0x1FA8E/0x1FAA5, 0x2B44B.., dispatcher).
* Save (0x1EB80, 0x1EB97): u32 period 0x71144, u32 phase 0x71140 written right after 0x714CC.
  Load: first 0x71144 = 0xFFFF (0x1F941) and everything is freed (0x1E50E, which also frees 0x711B0);
  after reading 0x714CC it loads "tick" if 0x711B0 == 0 (0x1FA64), then reads period and phase — so the
  metronome resumes at once with the saved phase. 0x1E50E is also called at shutdown (0x10950).
* Init (0x10088..): 0x71144 = 0xFFFF, 0x71140 = 0, 0x711B0 = 0, 0x7142C = 0xFFFF.

### 4.2 Table 0x54F13 — H (bytes), M (meaning)
Effective layout: T(loc, n) = byte[0x54F13 + 5*loc + n], loc = param 0..81, n = 1..5 (0x54F13 itself
= 0xFF is never read for a valid n; column 5 of row loc is physically column 0 of row loc+1).
Table spans 0x54F14..0x550AD (82*5 = 410 bytes); after that zeros. No bounds check (param up to 255
would read past into other data). Values = "distance" (seconds before difficulty bonus) from deck
location `loc` to the stage-n objective; zeros (stage target locations, guess) are:
F1 locs 30-33, F3 loc 73, F4 loc 37, F5 loc 9; F2 has none.

| loc | F1 | F2 | F3 | F4 | F5 |
|---|---|---|---|---|---|
| 0 | 25 | 37 | 37 | 26 | 9 |
| 1 | 24 | 36 | 36 | 25 | 8 |
| 2 | 23 | 35 | 35 | 24 | 7 |
| 3 | 22 | 34 | 34 | 23 | 6 |
| 4 | 21 | 33 | 33 | 22 | 5 |
| 5 | 20 | 32 | 32 | 21 | 4 |
| 6 | 19 | 31 | 31 | 20 | 3 |
| 7 | 18 | 30 | 30 | 19 | 2 |
| 8 | 17 | 29 | 29 | 18 | 1 |
| 9 | 16 | 28 | 28 | 17 | 0 |
| 10 | 15 | 27 | 27 | 16 | 1 |
| 11 | 14 | 26 | 26 | 15 | 2 |
| 12 | 13 | 25 | 25 | 14 | 3 |
| 13 | 12 | 24 | 24 | 13 | 4 |
| 14 | 11 | 23 | 23 | 12 | 5 |
| 15 | 10 | 22 | 22 | 11 | 6 |
| 16 | 9 | 21 | 21 | 10 | 7 |
| 17 | 10 | 22 | 22 | 11 | 8 |
| 18 | 11 | 23 | 23 | 12 | 9 |
| 19 | 12 | 24 | 24 | 13 | 10 |
| 20 | 13 | 25 | 25 | 14 | 11 |
| 21 | 14 | 26 | 26 | 15 | 12 |
| 22 | 8 | 20 | 20 | 9 | 8 |
| 23 | 7 | 19 | 19 | 8 | 9 |
| 24 | 6 | 18 | 18 | 7 | 10 |
| 25 | 5 | 17 | 17 | 6 | 11 |
| 26 | 4 | 16 | 16 | 5 | 12 |
| 27 | 3 | 15 | 15 | 4 | 13 |
| 28 | 2 | 14 | 14 | 3 | 14 |
| 29 | 1 | 13 | 13 | 2 | 15 |
| 30 | 0 | 14 | 14 | 3 | 16 |
| 31 | 0 | 15 | 15 | 4 | 17 |
| 32 | 0 | 16 | 16 | 5 | 18 |
| 33 | 0 | 17 | 17 | 6 | 19 |
| 34 | 4 | 18 | 18 | 7 | 20 |
| 35 | 5 | 19 | 19 | 8 | 21 |
| 36 | 2 | 12 | 12 | 1 | 16 |
| 37 | 3 | 11 | 11 | 0 | 17 |
| 38 | 4 | 10 | 10 | 1 | 18 |
| 39 | 5 | 9 | 9 | 2 | 19 |
| 40 | 6 | 8 | 8 | 3 | 20 |
| 41 | 7 | 7 | 7 | 4 | 21 |
| 42 | 8 | 6 | 6 | 5 | 22 |
| 43 | 9 | 5 | 5 | 6 | 23 |
| 44 | 10 | 4 | 5 | 7 | 24 |
| 45 | 11 | 5 | 6 | 8 | 25 |
| 46 | 11 | 3 | 4 | 8 | 25 |
| 47 | 12 | 2 | 3 | 9 | 26 |
| 48 | 13 | 1 | 2 | 10 | 27 |
| 49 | 14 | 1 | 3 | 11 | 28 |
| 50 | 15 | 2 | 4 | 12 | 29 |
| 51 | 16 | 3 | 5 | 13 | 30 |
| 52 | 17 | 4 | 6 | 14 | 31 |
| 53 | 18 | 5 | 7 | 15 | 32 |
| 54 | 19 | 6 | 8 | 16 | 33 |
| 55 | 20 | 7 | 9 | 17 | 34 |
| 56 | 21 | 8 | 10 | 18 | 35 |
| 57 | 22 | 9 | 11 | 19 | 36 |
| 58 | 22 | 10 | 10 | 18 | 36 |
| 59 | 21 | 9 | 9 | 17 | 35 |
| 60 | 20 | 8 | 8 | 16 | 34 |
| 61 | 19 | 7 | 7 | 15 | 33 |
| 62 | 18 | 6 | 6 | 14 | 32 |
| 63 | 17 | 5 | 5 | 13 | 31 |
| 64 | 16 | 4 | 4 | 12 | 30 |
| 65 | 15 | 3 | 3 | 11 | 29 |
| 66 | 14 | 2 | 2 | 10 | 28 |
| 67 | 13 | 2 | 2 | 9 | 27 |
| 68 | 12 | 3 | 3 | 8 | 26 |
| 69 | 11 | 4 | 4 | 7 | 25 |
| 70 | 10 | 5 | 5 | 8 | 24 |
| 71 | 11 | 6 | 6 | 9 | 25 |
| 72 | 14 | 3 | 1 | 10 | 28 |
| 73 | 15 | 4 | 0 | 12 | 29 |
| 74 | 16 | 5 | 1 | 11 | 30 |
| 75 | 17 | 6 | 2 | 12 | 31 |
| 76 | 16 | 5 | 1 | 12 | 30 |
| 77 | 16 | 4 | 2 | 13 | 30 |
| 78 | 15 | 3 | 1 | 12 | 29 |
| 79 | 16 | 4 | 3 | 13 | 30 |
| 80 | 15 | 3 | 2 | 12 | 29 |
| 81 | 14 | 2 | 1 | 11 | 28 |

Seconds = T + 1 + 2*(2 - difficulty); tick period = 64 ms * that; script timer = 10 * that.

### 4.3 Script usage (no dialogue script uses ACTIONSEQ ids >= 0xC0; scenes use it only through variables)
The listing shows `ACTIONSEQ $Mdist, $Mdist` (220x) and `ACTIONSEQ $Mred, $Mdist` — no literal #61440..
* Scene start (PROMBAR, HABITAT): `IFNE $Mred,#0,>skip; LET #61440,$Mred; ACTIONSEQ $Mred,$Mdist`
  (0xF000 → tick off), then `TIMER reddead, #2000, >quarkfr|>hb0110, scene; TIMEROFF reddead`.
* Each room view: `ACTIONSEQ #41xxx,$ret` (0xA0xx map location), `LET #loc,$Mdist; ADD $Mred,$Mdist;
  ACTIONSEQ $Mdist,$Mdist; TIMERSET reddead,$Mdist`. So id = 0xF000 + stage*0x100 + loc: stage 0 stops
  the tick and returns 999 (the timer is off anyway); stage n re-arms the tick period and reddead to
  d seconds **from this room**. Every room change restarts the countdown with the new room's value.
* Stage changes: HABITAT `LET #61696,$Mred; TIMERON reddead` (stage 1), `nextred: TIMERON reddead;
  ADD #256,$Mred; ...` after each Lyko phaser kill (temlyk/barlyk/hablyk, which first do ACTIONSEQ
  #192 + TIMEROFF reddead). A 6th stage (0xF6xx) would fall to the default: 999 returned, the old tick
  period keeps running (guess: never reached).
* Expiry: reddead fires → PROMBAR `quarkfr:` MAPOFF, HOTOFFs, LET #61440,$Mred; ACTIONSEQ (tick off),
  ldie animation, SOUND death, FADEOUT → death; HABITAT `hb0110:` HOTOFFs then branches on $redlyko.
  The countdown is a scene timer (type 0), so it pauses in conversations but the tick sound does not.

## 5. 0x7142C — H
Written on every call (= type), 0xFFFF on the default branch and at init (0x100F6). Read only inside the
dispatcher and its video/arcade branches (0x251EC..0x257CE: positions, pathsets, per-type arcade
options, name lookup 0x24E8F). **No reader outside 0x25027..0x25E46; not saved.** The 0xFFFF marker
has no consumer (dead store).
