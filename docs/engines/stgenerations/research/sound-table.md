# SGEN sound table, cue table and sound-manager rules (sgens.exe)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Working scripts (not kept): `snd/decode.py <game.bin> [s|c|stat|all]` decoded sections 2/3 (output for
verid1: `snd/verid1_sounds.txt`). `snd/dump.py`, `snd/fn.sh`, `snd/mgrc.txt` = cleaned
disassembly of the sound manager 0x41BF30..0x41FA60 (source file asndmgr.cpp; low level = sndmgr\src\Sound.c).

Confidence: H = read from code, M = inferred from code + data, L = guess.

## 1. Loader and the six sections

`0x45F160(path)` reads "SGEN", u16 22, u16 2, u32 size, then `size` bytes into one buffer and
builds a 7-pointer header in place (H):

| ptr | what | stored at (game = CGame, filled by 0x4337F9..0x433893) |
|---|---|---|
| p[0] | code (buf+0x1C) | local |
| p[1] | section 0 ((i16,i16) pairs, −1 end) | local → 0x4B5220 |
| p[2] | section 1, resource table (12-byte recs; +8 patched to point to the names) | game+0x64 |
| p[3] | **section 2 = sound cue table** | game+0x68 → `mgr.0x41EB62` (mgr+0x10) |
| p[4] | **section 3 = sound definition table** | game+0x6C → `mgr.0x41EA4B(game, sec3)` (mgr+4) |
| p[5] | section 4 = 100-byte projectile/weapon records (13 in verid1, zero record ends) | game+0x70, used by 0x43FE9C |
| p[6] | section 5 (96 bytes, not traced; tested at 0x4327F2) | game+0x74 |

(The formats.md statement "sections 2 and 3 go to a level sub-object (0x41EA4B/0x41EB62)" is right
but the order is: 0x41EA4B gets section **3**, 0x41EB62 gets section **2**.)

Sound manager: `new(0x11C)` → ctor 0x41E9C6, vtable 0x530394, stored at game+0x80 and in the
global `*0x53FE00`. Fields: +4 def table, +8 game, +0xC subtitle window (0x41EB41), +0x10 cue table,
+0x14 def count, +0x18 path "<mission dir>sounds\\".

### Section 3: sound definitions, 56 bytes each, ended by a record whose +0 == −1 (H)
verid1: 342 records + terminator = 19208 bytes (the "u16 342" seen at the start is just the low
word of record 0's key 0x00010156).

| off | field | notes |
|---|---|---|
| +0x00 | key (file id) | bits 0-15 number, bits 16-23 speaker/prefix index, bit 24 = COMMON archive (§2) |
| +0x04 | volume 0..64 | default 64. PLAY_SOUND/SET_SOUND_VOLUME overwrite it permanently |
| +0x08 | priority | AudioSetPriority; data 0,1,2 (one −1). Forced to 4 when flag 0x20 |
| +0x0C | position mode | 0 none, 1 fixed point (+0x14..), 2 billboard `0x4B5100(id)`, 3 actor `0x4B5070(id)` |
| +0x10 | source object id | for modes 2/3, used `& 0x1FFF`. Natives DF/E0 set it |
| +0x14,+0x18,+0x1C | x, z, y (16.16) | mode 1 position: x vs camera 0x57D9B8, +0x18 vs 0x57D9C0 (horizontal), +0x1C vs 0x57D9BC (height). Native D3 sets it |
| +0x20 | radius (16.16) | inaudible at distance >= radius. Native E1 sets it |
| +0x24 | start delay, ms | AudioSetDelay. Zeroed at load. Set by op 0x69 and by cues (persists in the def!) |
| +0x28 | repeat count | 0x7FFFFFFF = loop forever (ambient loops), 4 on MUSI7503. Cues reset it to 0 |
| +0x2C | repeat min gap, ms | 0 → hardware loop `AudioSetLoop(count)`. ≠0 → replay `count` times with a random gap |
| +0x30 | repeat max gap, ms | gap = min + RANDOM(max−min) (0x40E1D0) |
| +0x34 | flags | 1 speech (show subtitle), 4 stream from disk (music; no preload/clone, repeat cleared at load), 8 pan full left, 0x10 pan full right, 0x20 "loop/high priority" (prio 4), 0x40 fade-in, 0x80 fade-out, 0x100 muted (−10000) |

Native D1 sets +0x28/+0x2C/+0x30; D2 sets +0x08; DE sets/clears 0x100. A `voice` (0x84 bytes,
0x41DA4E) holds a copy of the def at voice+0x20, so def offset = voice offset − 0x20.

### Section 2: cue (sound group) table (H)
A list of variable-length entries, ended by a dword −1:
```
u32 0x12345678          ; entry magic
i32 key                 ; cue number used by PLAY_SOUND_CUE
u32 flags
u32 count               ; 0 in the file, filled at load (0x41EB62)
{ i32 member; u32 used; } [count]   ; member >= 0: section-3 index; < 0: delay of -member ms
u32 0x87654321          ; entry end
```
`used` is zeroed at load and counts how often each member was picked (per mission load).
verid1: 121 cues, keys 3..0x7C. Flags seen: 0x20, 0x21, 0x02, 0x01.

## 2. Sound number → file (0x41F8A8, H)
Script sound numbers are **indices into section 3** (def = table + 56·n); the file comes from the def key.
```
pre = (key >> 16) & 0xFF;  num = key & 0xFFFF
dir = (key & 0x01000000) ? app+0xBAD  ("<data>\COMMON\", 0x4877E6: "%sCOMMON\\")
    : pre == 0x12        ? app+0xAA9  (music dir: "<data>\DATA\MUSIC\" if a MUSIC.<disc> archive
                                       is found, else "<cd>\MUSIC\", 0x4879EE..0x487AFA)
    :                      mgr+0x18   (app+0xCB1 mission dir, e.g. "VERID1\", + "sounds\\" 0x53FF24)
name = dir + PREFIX[pre] + sprintf("%04d", num) + (txt ? ".txt" : ".wav")
```
PREFIX table 0x53FDA8 (4 chars each): 0 MISC, 1 PICA, 2 TROI, 3 GEOR, 4 RIKE, 5 DATA, 6 WORF,
7 CRUS, 8 KIRK, 9 ROMU, 10 KLIN, 11 SORA, 12 CHOD, 13 HANU, 14 EXTR, 15 KRAU, 16 LURS, 17 BETO,
18 MUSI, 19 TEMP. The mission dir comes from the mission table 0x545600 (0x84-byte recs, +0x44
dir name, e.g. index 14 "VERID1\"). In archive terms: mission → `<MISSION>.DAT:sounds/XXXX####.WAV`;
COMMON bit → `COMMON.DAT:XXXX####.WAV` (no sounds\ subfolder); MUSI → `MUSIC.1/2:MUSI####.WAV` (M for
the archive mapping, H for the strings).

verid1 decoded examples (index → file):
- 0 → VERID1\sounds\PICA0342 (speech), 1 → MISC0010, 2 → MISC7530, 9 → MISC7531 (fixed position, r=750)
- 18 → MUSIC\MUSI7503 (repeat 4), 21 → COMMON\PICA1005, 36 → SORA0027 (actor-attached, r=80)
- 57 → **RIKE0107**, 59 → RIKE0108, 61 → RIKE0109, 63 → RIKE0110; 56/58/60/62 → PICA0110..0113;
  cue 10 = [56,57,58,59,60,61,62,63], a Picard/Riker exchange (subtitle file rike0107.txt).
- 136 → MUSIC\MUSI7501 (stream), 226..254 → MUSI90xx/65xx (stream), 266..304 → COMMON\PICA100x,
  EXTR100x, KLIN10xx (random barks, cues 12/47/99/100/101/102), 43/45/98/99/105/324 = looping
  MISC75xx/6507 ambience (repeat 0x7FFFFFFF).
- 224/225/309..315 → COMMON\MISC0010, apparently placeholder records.
Speech records (flag 1) are always speaker prefixes; SFX are MISC; verid1 is a Kirk mission (89 KIRK).

Engine-side lookups by key (0x41ECA4): game+0x334 = index of key 0x01000030 (COMMON MISC0048),
game+0x2EC = index of 0x01000032 (COMMON MISC0050), −1 if the mission does not list them.

### Subtitles (H)
When a voice really starts (0x41D62E), if def flag 1 (or the debug global 0x542A2C, always 0) and
the subtitle window exists (game+0x2C8, created by 0x43775E only if the subtitle option is on,
app+0x348 == 0; rect (10,209)-(639,225)), the manager calls 0x41F9B1(key, duration):
it builds the **.txt** name with the same rule and calls `win.vt[0xA4](txtName, 0, durationMs)`
(0x42B51F: loads the whole file as text, shows it for `duration` ms; vtable 0x530F28). The debug
branch (0x542A2C ≠ 0) shows the .wav name for max(duration, 3000) ms instead. Each line of a cue
gets its subtitle when that line starts. Duration = AudioCount(h,0,2) (×2 for music voices), 0x41E987.

## 3. Playback rules

### Volume (H)
`0x41E90B(v, isMusic)`: clamp 0..64, `v = v·master/64` (master 0x53FC84 music / 0x53FC88 SFX, both
64 by default, setters 0x41C458/0x41C51B), then dB table 0x53FCA0 (DirectSound hundredths of dB):
`tbl[0] = −10000, tbl[64] = 0, tbl[i] = −(1000·log2(64/i) + 21)` (i.e. −10 dB per halving, e.g.
32 → −1021, 16 → −2021). Music voices = key prefix 0x12 (voice flag 1). Global mute 0x53FC80 (0x41C302).

### Channels, voices, priorities (H)
- SoundBegin(hwnd, **8 channels**, 128 buffers) at 0x48A64A. Voice structs are pooled (16 preallocated,
  grows by 10), so the manager itself has no limit.
- Channel request 0x4D9ED0: use a free channel; otherwise **steal** the channel with the lowest
  priority (item+0x98), ties → oldest start time (+0x9C), skipping locked ones (+0x74). The new
  sound's priority is not compared, so a new sound always gets a channel.
- A second voice of an already-loaded sample clones the buffer (AudioClone, 0x41D0CD); voices sharing
  a buffer form a ring (voice+0x14/+0x18). Flag-4 defs stream (AudioStream) instead.
- Speech gets no special treatment: a new line does not stop a playing one (they overlap); only the
  subtitle text is replaced. (M, no code checks flag 1 except the subtitle.)

### Natives (script value → manager call)
"ref" = 32-bit sound reference: high word 0 → def index (low word); high word −1 → every voice
playing def[low]'s key; high word h > 0 → the voice with serial h−1 (= handle returned by DC/68).
Natives that pop a 16-bit value sign-extend it, so they can only reach the "index" form.
- **0x68 PLAY_SOUND (vol, sound)**: 0x41ED07(sound, vol) sets def.volume = vol *permanently*,
  then 0x41F1A4(sound, 0, 0): new top-level voice (no requester), start. Pushes `serial+1`, or
  −1 (stays −1). Serials come from 0x53FC7C, start at 1, never reused (16-bit in the script).
  0x41F1A4's 3rd arg: 1 sets def flag 0x20, 2 clears it (used by engine callers, not scripts).
- **0x69 (delay:dword, word ignored, sound)**: def.delay = delay ms (persistent), then play as 0x68.
  verid1: `PUSH32 500; PUSH 63; PUSH 161; N69` (MISC7543 after 0.5 s).
- **0x6A PRELOAD_SOUND (n)**: 0x41C5DE → copy + 0x41D0CD (AudioLoad / clone); the voice is kept
  loaded but not started.
- **0x6B STOP_SOUND (n, 16-bit)**: 0x41F463 → for h>0 stop the voice and every voice sharing its
  buffer (0x41C8D0), for −1 all of def[low] (0x41C93A); **index form: no-op** (verid1 uses 48, 37,
  27, 17, which do nothing in the original).
- **0xCF (ref:dword)**: stop that one voice (0x41C842/0x41C889; index form no-op).
- **0xD0 SET_SOUND_VOLUME (ref, vol)**: index → def.volume; voice → +0x24 and AudioSetVolume.
- **0xD1 (idx, count:dword, min, max)**: def repeat fields +0x28/+0x2C/+0x30.
- **0xD2 (ref, prio)**, **0xD3 (ref, x, z, y dwords)** (verid1: def 27 at (0x66,−0x2C,0x18).0),
  **0xDF/0xE0 (ref, objId)** source object, **0xE1 (ref, radius)**, **0xDE (ref, mute)**.
- **0xDC (ref, ms)**: play with fade-in (flag 0x40: ramp 0 → volume over ms), pushes serial+1.
- **0xDD (ref, ms)**: fade a playing voice to 0 over ms, then stop it; index form returns −1.
  verid1 object 42 (music player) does `DD((h<<16)|idx, 500)` then `h = DC(newIdx, 500)`: a
  0.5 s cross-fade.

### Voice life cycle and event 46 (H)
- Start 0x41D25E: check audibility 0x41E320 (repeat > 0 → yes; mode 0 → yes; modes 1-3: distance
  < radius and def volume >= 10). An inaudible voice is never loaded and finishes at once.
- Per frame (0x41C27F from game 0x43A7B5 via 0x41EC1D, dt = time delta): channel callback 0
  ("really started", after the delay) → start time, duration, subtitle; callback 1 → finished.
  Fades are linear over ms. Watchdog: a non-repeating voice is killed when
  `elapsed − duration > max(duration, 3000)`.
- Finished: if repeat > 0 and min gap ≠ 0 → count−1, replay after the random gap. Otherwise free
  (0x41D812): unload/destroy the buffer, then **if voice+0x60 (requester) ≠ 0 →
  game.vt[0x174]\(id) = 0x437CF9 → obj.vt[0x180]\(46, 0, 0) = event 46 with no arguments**.
- Only cue chains have a requester: PLAY_SOUND, 0x69 and DC voices never send 46. A cue's parent
  voice carries the object id; it is freed (and sends 46) when its last child finishes, when it is
  stopped, when the cue picked no sound, or immediately for an exhausted flag-8 cue. Shutdown
  (0x41C1D0) clears requesters first.

## 4. PLAY_SOUND_CUE (0x8D, pops id then cue) → obj.vt[0x13C] = 0x44EE4D → 0x41F4E1(cue, obj.id) (H)
Find the entry with key == cue (none → −1, nothing sent). Selection, on `members[0..n)`:
1. **0x20 sequence**: play all members in order. With flag 1 as well, only the first time
   (members[0].used marks it).
2. Otherwise:
   - **0x10**: members[0] is always played first, then a pick from the rest.
   - **4**: the first time, play only members[0]; afterwards pick from members[1..].
   - **8**: the last member is the "final" one, kept out of the normal pick. When the pool is
     exhausted it is played once; after that every call sends event 46 at once and returns −1.
   - **1 no repeat**: only members with used == 0 can be picked; never reset (with no flag 8 an
     exhausted cue plays nothing and sends 46).
   - **2 random**: pick RANDOM(n) among the candidates; without 2 the first candidate is taken
     (so 1 alone = play in order, once each).
   The picked member gets used++.
3. Build a chain: parent voice 0x41C705(objId) (requester = object), then for each selected
   member: a negative value adds −v ms to a pending delay; a sound index gets def.delay = pending
   delay (or 0), def.repeat = 0 (both written into the shared def) and becomes a child voice
   (0x41C779). A trailing delay is ignored. Children are AudioLinked so they play back to back
   (gaps = their delays), skipping inaudible ones. Returns the chain handle (discarded by 8D).
- RANDOM = LCG at 0x546488: `seed = seed·189 + 2749; r = (seed·n) >> 32`.
- Cues are **not positional by themselves**: the object id is used only for event 46. A member is
  positional only if its own def has a position mode (verid1: SORA lines are mode 3, object 1,
  radius 80, probably attached to Soran's actor record 1).

### Positional sounds (0x41E481 / 0x41E583, every frame; H for the math, M for left/right)
- Source: mode 1 the def point; mode 2/3 record +0x1C/+0x24/+0x20 of billboard/actor `id & 0x1FFF`
  (looked up once, then followed every frame).
- d = approximate 3-D distance (0x4B4630: max + (sum of the other two)/4) to the camera.
- d >= radius or volume <= 10 → volume −10000. Else `vol = 10 + (vol−10)·(radius−d)/radius`
  → master/dB curve.
- Pan: 0 if `2·(|dx|+|dz|) <= |dy|` (source mostly above/below). Else an approximate atan2 angle
  (0x2000·minor/major + quadrant) + 0x4000 + player heading 0x57D9C8, folded to ±0x4000;
  `p = angle·64/0x4000 · d/radius` (centred when near); `pan = p<0 ? −tbl[64+p] : tbl[64−p]`
  (DirectSound −10000..10000). Static flags 8/0x10 force full left/right instead.
- Section-4 projectile records (+0x40 = impact sound index; verid1 86/39/82) are played at the hit
  point: 0x440334 sets the def position (0x41EE5B) and plays it (0x41F1A4).

## Open points
- Exact meaning of callback 0 vs 1 (start/end) is inferred from how the flags are used (M).
- def+0x0C == 1 test 0x41F123 has no callers. Section 5 and section-4 fields other than +0x40 not decoded.
- The handle encoding (h<<16 | idx) relies on scripts building a dword; verid1 object 42 does it.
