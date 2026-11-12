# Actors: sprite sets, animation and drawing (sgens.exe)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Scripts for this note (not kept): `stats6.py` (field statistics), `anim6.py` (per-actor
animation entries), `gs2.py` (GS2 dump), `render.py` (frame sheets to PNG).
Confidence: **H** = read directly from code, **M** = inferred from code plus data, **L** = guess.

## 0. Correction to the starting assumptions

- **The type 6 records ARE the runtime actor records.** The loader keeps the chunk in memory
  (`*0x57A18C`, stride 0x114) and the engine works on those 276-byte records in place: list links,
  floor pointer, current frame and so on are all written into them (0x4BE260). Nothing is copied. H
- **Type 7 (196 bytes, `*0x5743A0`) is something else.** It is a 3D box object. Its projector
  0x4C6980 transforms the 8 corners (it picks min/max per axis from bits 1/2/4) and puts the
  result in draw list type 2 (0x4C79C0, 0x4C8EF0). An actor can have a type-7 record **attached**:
  at load, `actor+0xFE` = index of the type-7 record whose u16 +0xAE == actor GS2 number and whose
  +0x14 == actor id, else -1 (0x4BE3DD..0x4BE41D). Each frame the update calls 0x4C3A50 on it. All
  verid1 actors have -1, and the verid1 type-7 table is empty. H (mechanism) / L (purpose)
- **"Linked parts"** are all the type-6 records that share the same id (+0x14). At load, 0x4BC880 finds
  or creates a per-id group in the hash `*0x57A190`. The record is pushed on the group's chain
  (`rec+0 = group+0x14; group+0x14 = rec; ++group+0x1C`). Mode changes (0x4B5CB0) and anim-rate
  changes (0x4B5DB0) walk `rec+0`. In verid1 all 40 ids are distinct, so every actor has one part. H

## 1. Sprite-set lookup and loading

- Format string `"%s%03d.gs2"` at 0x552308. Its only user is the loader **0x4C01B0** (ecx = number,
  edx = prefix). H
- The number is **u16 actor+0xFC**, which is also the actor's "class" for the AI tables. H
  (0x4BFF6F, 0x4B657E)
- The prefix is the buffer 0x551F28, filled at 0x42FC5F (`strcpy(0x551F28, mission+0x44)`). The
  level loader opens prefix + `"game.lv2"` (0x552054, 0x4B55B6..0x4B5630). So GS2 files are found
  next to GAME.LV2: `<mission dir>\NNN.gs2`. The open goes through the callback table at
  0x552038.. (0x430310 → 0x48D015 → CRT open, mode "rb"). H
- **One GS2 per actor**, but sets are shared. The table at 0x570280 has 64 slots of 20 bytes, indexed
  by number (so numbers are 0..63) (H):
  `+0 seq array (5-byte entries {u8 *frames; u8 n})`, `+4 frame array (8-byte entries
  {texture *; u32 width})`, `+8 refcount`, `+0xC frame count (0 = not loaded)`, `+0x10 seq count`.
  - 0x4C01B0 increments the refcount and returns at once if the set is already loaded.
  - Unload is 0x4C0540: decrement, free at 0 (0x4C0570).
- When the set is loaded:
  - At level start, 0x4BFF40 (called from the loader at 0x4BE488) loads the set of every actor whose
    **byte +0x112 == 0**. It sets flag 0x2000000 ("sprite set loaded") in +0x6C. H
  - Script natives **A8 (0x4B6560)** and **A9 (0x4B65C0)** take a GS2 number. They load or release
    that set for every actor with that +0xFC, and set or clear 0x2000000. H
  - Caveat: the loader also sets 0x2000000 unconditionally at 0x4BE324. An actor with +0x112 != 0
    would therefore be "drawable" without a loaded set. In verid1, +0x112 is 0 everywhere. M
- verid1 uses the GS2 numbers 1,2,3,4,5,6,7,8,9,10,15: 11 numbers, matching the 11 GS2 files in
  VERID1.DAT. M

### GS2 as the engine reads it (0x4C01B0, 0x4C0380, 0x4C0420)

- **Header.** The engine reads 24 bytes but uses only dword 1 (frame count) and dword 3 (sequence
  count). The version and the "size hint" are ignored. H
- **Sequences.** Each is `u8 n, u8 frameIndex[n]`. An empty sequence keeps a NULL pointer. H
- **Frames.** Each frame is (H):
  - `u32 width`. This is **16.16 world half-width at scale 1.0**, not a 24.8 full width. See §3.
    Numerically it is cols/128 for 256-row frames and cols/64 for 128-row frames.
  - `u32 cols`, `u32 rows`.
  - The column-major pixels, bottom row first.
  - The 3×cols table bytes.
  - The engine builds a texture header (0x14 bytes) plus a descriptor (0x28 bytes) like the level
    textures. Its shifts come from log2(rows) and log2(cols), so rows are powers of two (128 / 256).
- **The three column tables** are a column-drawing optimisation (0x4C0B59..0x4C0D73) (M):
  - t2 = column class: 0 = empty (skipped), 1 = solid, 2 = has holes, 3 = special.
  - t0 and t1 = first and last opaque row × 2.
  - Drawing every pixel except 0xFF gives the same image. H for the 0xFF test in 0x4C6F80,
    0x4C7040, 0x4C7130 and 0x4C7280.
- Accessors (H):
  - 0x4C0690(set, seq) gives n.
  - 0x4C06B0(set, seq, i) gives the texture of `frames[seq.idx[i]]`.
  - 0x4C0500(set, seq, i) gives the frame-array entry, used for the width.

## 2. Sequence and frame selection (per-frame update 0x4C3C10, timer 0x4C4230)

### Animation entries
There are 7 entries of 8 bytes at **+0xC4 + m*8**, with m = min(mode +0x10E, 6). Mode 7 uses entry 6;
mode 8 (hidden) is not drawn.

| off | type | meaning | conf |
|---|---|---|---|
| +0 (0xC4) | u16 | move speed, world units per animation frame in 8.8 (walk distance = speed × frame delta, 0x4C4409/0x4C4427). The loader defaults 0 to 0x40 | M |
| +2 (0xC6) | u8 | rate. Native 53 sets it on all parts (0x4B5DD0) | H |
| +3 (0xC7) | u8 | **N = frames per direction**. The loader defaults 0 to 1 | H |
| +4 (0xC8) | u8 | **loop start frame**. The loader clamps it to ≤ N−1 (0x4BE3B3..0x4BE3DB) | H |
| +5 (0xC9) | u8 | **direction count** (8, or 1 / 0 = no directions) | H |
| +6 (0xCA) | u8 | **mirror flag**: draw directions 5..7 as mirrored 3..1 | H |
| +7 (0xCB) | u8 | sequence number (the editor's copy; equals the engine table, unused by the engine) | M |

### Mode to sequence
The table is the u16 array at **0x552338**: `{0, 2, 0, 3, 4, 5, 1}` for m = 0..6. H

| seq | modes | meaning |
|---|---|---|
| 0 | 0 (idle), 2 (face target) | stand |
| 1 | 6 (wander), 7 (walk to) | walk |
| 2 | 1 | pain / hit. For crew GS2 001 this is the transporter beam-out |
| 3 | 3 | attack |
| 4 | 4 | death A |
| 5 | 5 | death B |

Sequences 6..15 are never used.

### Timer (0x4C4230)
`t` = u16 at +0x106 (8.8 frames), and `frame` = byte +0x107 = t>>8. Mode changes set t = 0
(0x4B5CF8, 0x4B5095).

```
delta = (rate*256 * dt_ms) >> 10        dt = *0x57A030, clamped to 8..1000 ms (0x4B5B60)
                                        i.e. about rate frames per 1.024 s
lim   = (N - start) << 8;  if (delta >= lim) delta = lim - 1     (never skip a whole loop)
if t == 0:
    if mode == 3: fire weapon (0x4C4460), t = 1
    else t = delta
else:
    t += delta
    if (t>>8) >= N:
        if mode == 3: t = 0                        (fire again next tick)
        if N > 1: post engine event 0x11 (script event 37 "cycle done")
        while (t>>8) >= N: t += (start - N) << 8   (wrap back to the loop start)
        if t == 0 and mode != 3: t = 1
```

The effect (H):
- The first pass plays frames 0..N−1, then it loops start..N−1.
- **Start = N−1 means "play once and hold the last frame"**. Event 37 then repeats each
  frame-time. All pain, death and most attack entries in verid1 are written start == N, and the
  loader clamps them to N−1.

### Direction (0x4C3D16..0x4C3DCF)
```
A   = atan2i(actor.x - cam.x, actor.y - cam.y)      0x4BC560: 16-bit angle, 0 = +y, 0x4000 = +x
oct = (u16)(-A - heading(+0x108) + 0x1000) >> 13   0..7, 45° sectors centred on the axes
d   = (oct * dirs(+0xC9)) >> 3
if mirror(+0xCA) && d > 4:  d = 8 - d, set flag 0x20 in +0x6C (flip horizontally); else clear 0x20
```
Heading convention: an actor with heading h faces the world vector (−sin h, cos h). This holds for
the facing routine 0x4C45C0 (`heading = -atan2(dx, dy)`) and for the muzzle offset in 0x4C4460.

So **d = 0 is the back view** (the actor faces away from the camera), **2 is the profile facing
screen-right**, **4 is the front**, and 6 is the mirrored 2. This is confirmed visually: 001.gs2
seq 0 frames 0..4 = back, back-¾ right, profile right, front-¾ right, front. H

### Frame
```
idx = (u8)(frame + d*N)          stored at +0x111
if idx >= seq.n: idx = 0
tex = frames[seq.idx[idx]]       → +0x10
halfWidth(+0x34) = frameWidth * scale(+0xC0) >> 16
uRange(+0x48)    = (cols-1) << 16
```
The sequence layout is therefore direction-major: all N frames of d = 0, then d = 1, and so on. Its
length is N × (number of distinct d), e.g. 5 × N for 8 mirrored directions.

### Check against verid1
- **Actor id 1 uses GS2 001** (crew, scale 1.75):
  - mode 0: N=1, dirs 8, mirror → seq 0 = 5 frames. ✓
  - mode 1: N=15, dirs 1 → seq 2 = 15 frames (beam-out, held at frame 14). ✓
  - mode 3: N=5, start 0 → seq 3 = 5 frames (firing loop). ✓
  - Seq 1 (walk) is empty, and this actor starts in mode 0 and never needs it.
- **samples3/008.gs2 does not fit the gs-8 records.**
  - The file has one 11-frame sequence (a flame).
  - The four gs-8 actors (ids 81..84) start in **mode 6**. Mode 6 needs seq 1 with ≥ 40 frames
    (N=8, 5 mirrored directions).
  - With an empty sequence, 0x4C06B0 dereferences NULL.
  - So that sample is probably not verid1's 008, or those actors are never updated. **Open.**

## 3. Drawing

### List
0x4C7AB0 walks the active actor list (`*0x57A17C`, next = +4, prev = +8; mode-8 actors sit on a
separate list). An actor is drawn when all of these hold:
- `flags(+0x6C) & 0x100` (linked/visible).
- `flags & 0x2000000` (set loaded).
- `!(flags & 1)`.
- `(+0x3C & *0x57D99C) || (+0x40 & *0x57D9A0)`. The masks are the player's floor +0x74/+0x78, the
  same zone test as walls and billboards.
- `depth < +0x5C` (unsigned compare, so anything behind the camera is dropped; +0x5C = 15000.0 in
  the file).

The actor's zone words +0x3C/+0x40 are **copied every update from its floor flat** (+0xC → flat
+0x3C/+0x40, at 0x4C3E65). H

### Projection
The sprite path is 0x4B5370 then 0x4C5820, and billboards share 0x4C5820.
- Camera space: x' = dx·cos − dy·sin; depth = dx·sin + dy·cos; vertical = dz + depth·pitch
  (*0x57D9D8).
- **The divisor is depth + 1.00001.** Walls do the same by moving the eye back 1.0 unit along the
  view direction (0x4C7D03..0x4C7D33). H
- **Horizontal:** left/right = (x' ∓ halfWidth(+0x34)) / (depth+1). Screen x = cx(*0x57BED4) +
  that × focal(*0x57D840). Columns are ceil'd. The sprite is centred on the actor's x,y. H
- **Vertical:**
  - bottom = vertical / (depth+1), and top = (vertical + height(+0x38)) / (depth+1).
  - Screen y = cy(*0x57BEC4) − v × focal.
  - The world z used is actor +0x20. It already includes the draw offset +0x30: the loader adds it
    (0x4BE391), and the update subtracts it before physics and adds it back after (0x4C3C97,
    0x4C3CEF). H
  - So **the bottom edge of the frame (its stored first row) sits at physical z + off(+0x30)**.
    Physical z is clamped to the floor height, so the bottom edge stands on the floor (0x4C3CB0).
- **Size:**
  - Quad width = 2 × frameWidth × scale. Scale is file +0x34, copied to +0xC0 at load (0x4BE33E).
    +0x34 is then overwritten every frame with the half-width.
  - Quad height = **+0x38, a fixed per-actor value from the level file**, not derived from the frame.
  - The whole frame is stretched over that box: u over cols−1, v as a 0.32 fraction of the frame
    height (`dv = (+0x50=0x7FFFFF / pixelHeight) << 9`).
  - In verid1 the result is usually square pixels (e.g. scale 2.0 / height 8.0 with 256-row frames
    = 1/32 unit per pixel). Not always: gs-8 is 2.5 / 3.0. H
- **Mirror:** when flag 0x20 is set, u starts at the right and du is negated (0x4C5A5F). H
- **Texture scroll** +0x60/+0x62 (u/v speeds × global time) and offsets +0x44/+0x4C exist. They are
  0 for all verid1 actors. H

### Lighting
- Each update copies the light byte of the floor flat into actor +0x68: `actor+0x68 = floor+0x68`
  (0x4C3E56).
- The draw item takes **shadeTable[*0x575D80 + light]**, or **0** (no shading) when the actor flag
  0x10 is set (0x4C5A0A, 0x4C1102). H
- The file's light words at +0x64..+0x69 are therefore overwritten.

### Transparency
Index 0xFF is skipped. H

### Sorting and occlusion
- All draw items go into per-screen-column lists. Wall items are 0x7C bytes; sprites go in the
  0x575EFC array.
- The lists are sorted by the key **2^44 / (depth+1)** (0x4C8BF0, 0x4C0940). Each column is then
  painted far to near from the clipped segment list (0x4C1320, 0x4C7420). Walls in front therefore
  cover sprites column by column. H for the structure, M for the details.
- The sprite item also stores its world top/bottom z (item+4 = z+height, item+8 = z + 1.5). These
  are presumably for clipping against floors and ceilings: the +1.5 keeps the feet from being cut by
  the floor the actor stands on. M
- **A depth buffer with per-pixel transparency reproduces this.**
- Draw method: item+0x10 = 1 when actor flag 0x4 is set, else 3. Actor flags 0x40/0x80 go to item
  bits 1/2. Their meanings are unresolved (L). All verid1 actors have flags 0x206 (bits 1, 2, 9).

## 4. Type 6 / runtime actor record (276 bytes)

The values are from verid1 (40 records).

| off | type | meaning | conf |
|---|---|---|---|
| 0x00 | ptr | runtime: next linked part (same id) | H |
| 0x04 | ptr | runtime: next in active/hidden list | H |
| 0x08 | ptr | runtime: prev in list | H |
| 0x0C | ptr | runtime: floor flat the actor stands on | H |
| 0x10 | ptr | runtime: current frame texture | H |
| 0x14 | u32 | id (script object 0xA000\|id) | H |
| 0x18 | – | 0 | |
| 0x1C,0x20,0x24 | 16.16 | x, z (height), y | H |
| 0x28 | 16.16 | reach distance (= radius in the data) | H |
| 0x2C | – | 0 | |
| 0x30 | 16.16 | vertical draw offset, added to z (−1.0 for gs 3/5) | H |
| 0x34 | 16.16 | **file: sprite scale**; runtime: current half-width | H |
| 0x38 | 16.16 | **sprite height** in world units | H |
| 0x3C,0x40 | u64 | zones (file all-ones; runtime copied from the floor) | H |
| 0x44 | 16.16 | u offset (0) | H |
| 0x48 | 16.16 | u range; runtime (cols−1)<<16 | H |
| 0x4C | 16.16 | v offset (0) | H |
| 0x50 | | v range 0x7FFFFF (full frame) | H |
| 0x54 | 16.16 | collision radius | H |
| 0x58 | 16.16 | collision height | H |
| 0x5C | 16.16 | max draw depth (15000.0) | H |
| 0x60,0x62 | i16 | u/v scroll speed (0) | H |
| 0x64..0x69 | | light words (as walls); +0x68 is overwritten with the floor light | H |
| 0x6C | u32 | flags: 0x1 not drawn, 0x4 draw method, 0x10 fullbright, 0x20 mirrored (runtime), 0x100 linked/visible, 0x800 drawn this frame, 0x1000000 movement variant (in 0x4C3AC0; 2 actors), 0x2000000 set loaded, 0x4000000 set at load, 0x8000000 "near player" (AI callback) | H/M |
| 0x70 | ptr | runtime: guard target | M |
| 0x74 | ptr | runtime: AI target (game obj+0x70) | H |
| 0x7C | ptr | weapon object (default *0x5522BC) | H |
| 0x80 | 16.16 | 1.0, unknown | L |
| 0x84,0x88,0x8C | 16.16 | muzzle offset: side, up, forward (e.g. 0.9, 5.2, 0) | H |
| 0x90,0x94,0x98 | 16.16 | wander distance bands | M |
| 0xA0 | 16.16 | 10/20/30, unknown (wander) | L |
| 0xA4 | | wander interval | M |
| 0xAC | 16.16 | step height (3.0) | H |
| 0xB0..0xBC | | home point x,z,y (= initial position) | H |
| 0xC0 | 16.16 | runtime: scale (copied from 0x34 at load); 0 in the file | H |
| 0xC4..0xFB | 7×8 | animation entries (§2) | H |
| 0xFC | u16 | **GS2 number / actor class** | H |
| 0xFE | i16 | attached type-7 index or −1 (set at load) | H |
| 0x100,0x102 | u16 | wander turn angle range | M |
| 0x104 | u16 | turn speed | H |
| 0x106 | u16 | animation timer 8.8 (byte 0x107 = frame) | H |
| 0x108 | u16 | heading (65536 = 360°) | H |
| 0x10A | – | 0 | |
| 0x10C | u8 | 1 in all records, unknown | L |
| 0x10D | u8 | guard mode flag | M |
| 0x10E | u8 | mode (initial: 0, 6 or 7 in verid1) | H |
| 0x10F | u8 | "turn finished" latch | M |
| 0x111 | u8 | current index into the sequence | H |
| 0x112 | u8 | don't preload the sprite set | H |

### Level-start conversion (0x4BE260..0x4BE441, then 0x4BFF40)
For each record:
1. Link it into its id group.
2. Put it on the hidden list (mode 8) or the active list.
3. Set flags |= 0x4000000 | 0x2000000 | 0x100.
4. Set weapon = default.
5. Copy scale +0xC0 = +0x34.
6. Find the floor (0x4C3AC0).
7. Set z += +0x30.
8. Normalise the 7 animation entries.
9. Resolve +0xFE.
10. Load the GS2 sets.

A small per-class table (0x57BEF0 / 0x57AA70, indexed by class × 17 + target class) holds the
"near player" distances and callbacks registered by native 0x4576C2. It has no effect on drawing.
