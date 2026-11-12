# Music, speech subtitles, engine sounds and the mixer (sgens.exe)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Confidence: H = read directly from code, M = code read but meaning inferred, L = guess.
Working scripts: `re/mus/dump.py` (function dumper), `re/mus/sndtab.py` (dumps a mission's sound
table from its .DAT), dumps in `re/mus/*.txt`. Not covered here (see sound-table.md): the SGEN sound/cue
table layout in detail and the PLAY_SOUND / PLAY_SOUND_CUE natives.

There are **three separate audio users** in the exe, all on top of one low-level library
(`C:\stgens\gens\sndmgr\src\*.c`, functions 0x4D40C0..0x4DE393, DirectSound):

1. the **3D-level sound manager** `asndmgr` (object `*0x53FE00`, vtable 0x530394, code 0x41C034..0x41F9B1),
   created per level at 0x4338AA and fed with the SGEN sound table — this is everything in the
   action levels, *including their music*;
2. the **bridge / strategic-layer audio queue** (0x478B51..0x479A6D, item array 0x563278, count
   0x563318), driven by `Design\Universe.INI` commands `QueAudio` / `QueMusic`;
3. a few screens that stream one WAV themselves (credits 0x42D8AD, text screens 0x42E549, in-game menu
   0x460E3B, options preview 0x485A4A, space combat 0x49xxxx/0x4Axxxx).

## 0. Low-level library (sndmgr) — names from its assert strings (H)

| addr | name | notes |
|---|---|---|
| 0x4D74C0 | SoundBegin(hwnd, channels, items) | called once at 0x48A64A with (hwnd, **8**, **128**); max allowed 8 / 128 |
| 0x4D40C0 | AudioCreate | 0x4D4186 AudioDestroy |
| 0x4D43F9 | AudioLoad(path) | whole file in memory; 0x4D439E AudioUnload |
| 0x4D4A8E | AudioStream(path, -1) | streamed from disk |
| 0x4D4C22 | AudioClone | second voice sharing a loaded buffer |
| 0x4D4EFE / 0x4D5169 | AudioLink / AudioLinkStream | chain: next item starts when the previous ends (gapless) |
| 0x4D498A | AudioPlay | 0x4D5703 AudioStop, 0x4D57ED stop all |
| 0x4D5C11 | AudioSetDelay(ms) | start delay |
| 0x4D5D5B | AudioSetLoop(n) | -1 / 0x7FFFFFFF = endless |
| 0x4D5E9B | AudioSetPriority | 0x4D5F6B AudioSetPan (−10000..10000, DirectSound units) |
| 0x4D62E6 | AudioSetVolume(hundredths of dB, −10000..0) | 0x4D642E AudioSetVolumeRamp(target, ms) |
| 0x4D6962 | AudioSetCallback(h, event, fn) | event 0 = started, 1 = finished (as used) |
| 0x4D54F1 | length/position query (h, 0, mode) | mode 2 used as "duration in ms" |
| 0x4D6B16 | is-playing test | 0x4D7462 = millisecond clock |

**Voice limit = 8 mixing channels** (H). Channel request 0x4D9ED0: take a free channel, else steal the
playing item with the **lowest priority** (item+0x98), ties → **oldest start time** (+0x9C), skipping
locked items (+0x74 != 0). The new sound's own priority is *not* compared, so a new sound always
steals (H for the loop, M for field names). 128 = max loaded/cloned audio items.

## 1. 3D-level sound manager (asndmgr)

### 1.1 Sound definition (56 bytes, SGEN sound table; copied into each voice at voice+0x20) (H)

| def | voice | meaning | verid1 values |
|---|---|---|---|
| +0x00 | +0x20 | id: bits 0-15 number, bits 16-23 **prefix index**, bit 24 **COMMON** | |
| +0x04 | +0x24 | volume 0..64 (linear amplitude) | 64 |
| +0x08 | +0x28 | priority → AudioSetPriority (flag 0x20 forces 4) | 0,1,2 (−1 once) |
| +0x0C | +0x2C | position mode: 0 none, 1 fixed point, 2 follow billboard, 3 follow actor | 0/1/3 |
| +0x10 | +0x30 | object id for modes 2/3 (`id & 0x1FFF`) | 1 (= Soran actor) |
| +0x14/18/1C | +0x34/38/3C | x, y, z (16.16) for mode 1; for modes 2/3 refreshed every frame from the record (+0x1C, +0x24, +0x20) | |
| +0x20 | +0x40 | maximum distance (16.16) | 80..750 |
| +0x24 | +0x44 | ms: start delay, or fade time when flag 0x40/0x80 | |
| +0x28 | +0x48 | loop count (0 = once, 0x7FFFFFFF = forever) | |
| +0x2C/+0x30 | +0x4C/+0x50 | if non-zero: re-trigger after random(min..max) ms instead of a native loop | |
| +0x34 | +0x54 | flags: 1 has subtitle, 4 stream from disk, 8 pan hard left, 0x10 hard right, 0x20 high priority, 0x40 fade-in, 0x80 fade-out, 0x100 muted | MUSI → 4; speech → 1 |

Section numbers here are 0-based as in formats.md; verid1 sizes are 8, 444, 4616, 19208, 1300, 96,
so the 56-byte sound table is section 3 (343 entries) and section 2 (4616 bytes) is the cue table.
Table ends at id == −1 (0x41EA4B). On load every def's +0x24 is cleared, and +0x28 too if flag 4.

### 1.2 File names (0x41F8A8) (H)
`prefix[ (id>>16)&0xFF ] + "%04d" % (id&0xFFFF) + ".wav"` (or ".txt" for the subtitle), prefixes from
the 4-char table at 0x53FDA8:
`0 MISC 1 PICA 2 TROI 3 GEOR 4 RIKE 5 DATA 6 WORF 7 CRUS 8 KIRK 9 ROMU 10 KLIN 11 SORA 12 CHOD
13 HANU 14 EXTR 15 KRAU 16 LURS 17 BETO 18 MUSI 19 TEMP 20 COMP`.
Directory: bit 24 set → `<ACTION dir>COMMON\` (COMMON.DAT, cfg+0xBAD); prefix 18 (MUSI) → the music
directory cfg+0xAA9 = `<install>\DATA\MUSIC\`, which is the ZIP `MUSIC.<disc>` (disc number cfg+8,
1 or 2) mounted at 0x487A19, or the CD folder `%s\MUSIC\` if the archive is missing (0x487627);
otherwise `<mission dir>\sounds\` (cfg+0xCB1 + "sounds\", set at 0x41EAF8). cfg = `*0x565AB0`.

### 1.3 Voices (H)
Pool of 0x84-byte voices (16 at start, +10 when empty; 0x41D9A5/0x41DA4E) — no game-side limit.
Handle = voice+0x5C (counter from 1, 0x53FC7C). Natives return **handle+1** (−1 = failed).
A "sound reference" (32-bit, used by STOP/volume/fade natives): high word 0 → sound index (changes the
def), high word −1 → all voices playing sound `low word`, high word h > 0 → voice with handle h−1.
Same sample playing twice shares the buffer (AudioClone, ring at voice+0x14/+0x18, 0x41D0CD).
Play = 0x41C619: copy def → **cull test 0x41E320** → load/clone → start.

Cull test 0x41E320: loop count > 0 → always plays; mode 0 → plays; modes 1-3: plays only if
`dist(player, source) < maxdist` **and** volume ≥ 10. A culled voice is not started but still runs its
completion path (owner still gets event 46, no subtitle).

Per-frame update 0x41C27F (from game frame 0x43A7B5) → 0x41DF0E per voice:
- start callback (event 0, 0x41E823) → 0x41D62E: record duration (`0x41E987`: sndmgr length mode 2,
  ×2 for MUSI) and start time; **show subtitle** if def flag 1 and the voice was audible;
- finished callback (event 1, 0x41E8B6) → loops with delay (+0x4C) re-trigger, linked chain next,
  else free (0x41D812): if voice+0x60 (owner object, set only by PLAY_SOUND_CUE chains) → game.vt174 →
  **event 46** to that object;
- watchdog: a non-looping voice still "playing" more than `max(duration, 3000)` ms after it should
  have ended is force-stopped;
- fades: linear in 0..64 volume units from +0x70 to +0x74 over +0x78 ms; at the end of a fade to 0 the
  voice is stopped, otherwise flags 0x40/0x80 are cleared;
- positional voices (mode ≠ 0) recompute volume and pan every frame (0x41E481/0x41E583).

### 1.4 Volume curve and master volumes (H)
0x41E90B: `v = clamp(vol,0,64); v = v * master / 64; return dB[v]` with `dB[i] = 2000·log10(i/64)`
(table 0x53FCA0: −10000, −6021, −5021 … −44, 0) — i.e. **linear amplitude vol/64**.
`master` = 0x53FC84 (music) for MUSI sounds (voice+0x80 bit 0), else 0x53FC88 (effects + speech).
Both = options slider × 64 / 29 (0x434CDC, re-applied when the options dirty flags cfg+0x374/+0x378 are
set, 0x436EC1). Sliders cfg+0x338 (music) / cfg+0x33C (sound), **0..29, default 20** (0x4870A0).
Streams outside the manager use 0x4892BD: `0 → −10000, else 2000·(v+1)/30 − 2000` (hundredths dB).
Global mute 0x41C302 (game.vt47C) sets every voice to −10000 while the in-game menu is open (0x44341A).

### 1.5 Positional sound 0x41E583 (H)
- `dx = x − px, dy = y − py, dz = z − pz` with player at 0x57D9B8 (x), 0x57D9C0 (y), 0x57D9BC (z, height).
- distance (0x4B4630) = `max + (other two)/4` of |dx|,|dy|,|dz| (cheap approximation).
- `dist ≥ maxdist` → silent (voice keeps running). Otherwise
  `v = vol − 10; if v ≤ 0 silent; v = 10 + v·(maxdist − dist)/maxdist` → curve above (linear fall-off
  from full volume at the source to volume 10 at maxdist).
- pan: if `2·(|dx|+|dy|) ≤ |dz|` (mostly above/below) → centre. Else a 16-bit angle from an octant
  approximation of atan2(dy,dx) (`0x2000·minor/major` + octant), `+0x4000 + heading(0x57D9C8)`,
  folded front/back into −0x4000..0x4000, `p = angle·64/0x4000` (−64..64), then scaled by distance
  `p = p·dist/maxdist` (close sounds are centred). Pan = `−dB[64+p]` for p<0, `dB[64−p]` for p≥0
  (so |pan| = attenuation of the far ear). Sign convention (which side is left) not verified (M).
- Flags 8/0x10 instead force hard left/right.

### 1.6 Script natives that reach the manager (push order first → last; top of stack last) (H)
Not yet named in `opcodes.cpp`:

| op | suggested name | pushes | effect |
|---|---|---|---|
| 69 | PLAY_SOUND_DELAYED | d delayMs, w (ignored), w sound | def+0x24 = delay, play; returns handle+1 |
| CF | STOP_SOUND_REF | d ref | stop one voice (0x41C842/0x41C889) — 6B stops all clones |
| D1 | SET_SOUND_LOOP | w maxDelay, w minDelay, d loops, w sound | def+0x28/+0x2C/+0x30 |
| D2 | SET_SOUND_PRIORITY | w priority, w ref | def+8 / voices |
| D3 | SET_SOUND_POSITION | d z, d y, d x, d ref | def+0x14/18/1C (16.16); e.g. `24,-44,102,27` |
| DC | PLAY_SOUND_FADE_IN | w ms, d sound | play from 0 to its volume over ms; returns handle+1 |
| DD | FADE_OUT_SOUND | w ms, d ref | fade to 0 over ms then stop (starts it first if not playing) |
| DE | MUTE_SOUND | w on, d ref | flag 0x100: volume −10000 while set, voices keep playing |
| DF, E0 | SET_SOUND_OBJECT | w objectId, d ref | def+0x10 (object followed in modes 2/3) |
| E1 | SET_SOUND_MAX_DISTANCE | d dist, d ref | def+0x20 |
| D5 | GET_OPTION_340 | – → w | pushes cfg+0x340 (default 1; probably difficulty; not sound) (L) |
| 96 | SET_LEVEL_ENTRY_SOUND | w volume, w sound | game+0x334 = sound (+ its volume); played by game.vt10C (0x43E13A) after a modal screen / level entry; default = index of COMMON MISC0048 (M) |

(68 PLAY_SOUND, 6A PRELOAD, 6B STOP, 8D CUE, D0 SET_SOUND_VOLUME already named.) The 32-bit refs
returned by DC are stored by scripts as `(handle+1) << 16 | sound`.

### 1.7 How a level gets music (H for mechanism, M for intent)
There is **no music system in the 3D game**: music tracks are ordinary sound-table entries with prefix
18 (MUSInnnn, normally flag 4 = streamed, priority 2), played by scripts with PLAY_SOUND / cues /
DC / DD, scaled by the music slider. Nothing in GAME.BIN sections other than the sound table or the
mission table selects music, and the engine never starts any. All voices stop when the level ends
(0x41C1D0). verid1 examples: object 0xE001 on event 6 plays sound 136 (MUSI7501, vol 50) as the
opening cue; object 0xE028 is a **zone cross-fader**: event 4 with a sound index (666 = none) →
`DD(old, 500 ms)` then `DC(new, 500 ms)`; flats send it 45/98/99/105/324 (MISC75xx/6507 ambient
loops, loop 0x7FFFFFFF) on event 14 (player stepped on). verid1 has 36 MUSI entries (9006…9090,
6500-6502, 7501, 7503), mostly reached through cues.

## 2. Speech subtitles

### 2.1 In the 3D levels (H)
- Shown when a voice whose def has **flag 1** actually starts and was audible (0x41D6DD → 0x41F9B1).
  The .txt name is built like the .wav name (same prefix/number/directory, extension `.txt`).
- Window: game+0x2C8, created at level start (0x43775E) only if the subtitle option is on
  (cfg+0x348 == 0, default on). Class vtable 0x530F28 (ctor 0x42B23E): a child of HUD window 3,
  rect **(10, 209)–(639, 225)** = one 16-px text line across the bottom of the view, font
  `Fonts\Action.fnt`, colour PALETTEINDEX-form (0x01000000 | index) (M).
- `vt[0xA4](file, delay=0, duration)` (0x42B51F) loads the file and **first clears the current
  text** (0x42B3B5): a new subtitle replaces the old one immediately — no waiting, no queue.
  duration = the sample's length in ms.
- Text layout 0x42B697: one line (up to CR/LF) at a time, leading blank lines skipped. Escapes:
  `\T<hex RRGGBB>\` text colour (GetNearestPaletteIndex), `\W<hex>\` delay before showing,
  `\B<hex>\` vt9C (font/style, L), `\\` literal. A line's time is the given duration for the first
  line; later lines (or when no duration) get `words × 1000 ms`, minimum 2000 (+0x54/+0x58).
  If the line is taller than the window it scrolls up during the last part: starts after
  delay+1000 ms, lasts duration−2000 ms. After the last line the window is hidden (callback +0xAC).
- If the .txt is missing nothing is shown (the bridge path prints `File: %s has no subtitle`).
- Debug flag 0x542A2C (always 0) would show the .wav name for ≥3 s instead.

### 2.2 Speech queue
- 3D levels: **none**. PLAY_SOUND starts at once and mixes with whatever plays (8 channels). Only a
  cue with flag 0x20 plays its members in sequence (AudioLink chain, member < 0 = gap in ms), and
  event 46 to the owner lets scripts chain lines themselves. (H)
- Bridge layer: a FIFO (0x563278): `QueAudio` items wait for the previous item; queuing speech while
  music is the head fades the music out (AudioSetVolumeRamp to −10000 over ≤1000 ms) and stops it
  after half that time; `QueMusic` is only accepted when the queue is empty; each item's subtitle
  uses the same window class (0x47976A), cleared when the item ends (0x479646). (H)

## 3. Engine-driven sounds in the 3D levels
The 3D engine (0x4B0000..0x4D0000) **never calls the sound manager** (no reference to *0x53FE00 or
asndmgr functions; H). LV2 walls, flats, billboards, movers and actors have **no sound field**
used by code. All of these come from scripts or from game-side tables:

| sound | where | conf |
|---|---|---|
| footsteps | none exist | H |
| doors / lifts | none in code; scripts may react to mover events 22-25 (verid1 doesn't) | H |
| step on flat, wall hit, actor pain/death, item use, splash | script handlers: ev 14 (flats), 16 (wall hit), 39/41/42 (actors via parent 0xE0DD), 45, 47, 56/57/58 (medium) | H |
| player hurt | script on broadcast 9 (verid1 object 0xE005) | H |
| bare-hand/weapon swing | SET_PUNCH a6/a7 → view+0x978, played when the first-person action animation starts (0x431DC5) | H |
| punch hit | SET_PUNCH a8/a9 → game+0x268, played on a pick hit (vt16C code 19, 0x4368D8) | H |
| projectile impact | SGEN **section 4** records (100 bytes, game+0x70): +0x40 = sound, positioned at the hit point (0x440334, via D3-style set position then play); +0x44 = second sound positioned at the hit record or the player (vt170 0x43696F). verid1 +0x40 = 86/39/82 (82 = MISC7515, mode 1, maxd 140) | M |
| UI beep | game+0x2EC = index of COMMON MISC0050 (0x1000032), played on HUD/menu changes (0x43A77E, 0x43F679…) | H |
| item setting change | item behaviour objects play their +0x20 sound (0x44A03A; −1 by default) | L |
| ambient loops | scripts (looping MISC defs, positional modes 1-3) | H |

## 4. Music outside the levels
- **Bridge (strategic layer)**: `QueMusic <file>` (Universe.INI command class vtable 0x5302F8,
  0x41299B → 0x478116) queues a file from the music directory (MUSIC.n). Idle music 0x476E9E: after
  **60 s** (0x54703C) with nothing queued it queues a random `Loop1..4.wav` (`Loop%d.wav`), every other
  interval (so every 120 s); queuing anything resets the timer. Kinds: 1 sfx volume, 2 music volume,
  3 sfx volume (0x479552). `WIN1`, `LOSE1-4`, `GOVER2`, `TIMEOUT4`, `GAMESTAR` do not appear in the exe,
  so they must be named by data (Universe.INI / States.ini `QueMusic`) (M).
- **Credits** 0x42D8AD: streams `Credits.WAV`, `MUSI9022`, `MUSI5003`, `MUSI9024`, `MUSI9020`
  (table 0x540198) from the music dir in order as the scroll advances, music volume.
- **Text screens** (class 0x531128, 0x42E549, `Text%d.txt`): streams a music file named in its data.
- **In-game menu** 0x460E3B (game.vt47C mutes the level first): plays `<OPTIONS dir><A|C>LOOP<1-4>.WAV`
  looped, and on exit `<A|C>TAG<1-2>.WAV`, waiting for it to finish ('A' if the menu's type is 2,
  'C' if 3). (M; letters/types not confirmed with data.)
- Space combat (0x49xxxx: `scloop%d`, `scwin`, `sclose`, …) not analysed.

## 5. Options / globals summary
| what | where | default |
|---|---|---|
| music slider | cfg+0x338 (0..29) | 20 |
| sound slider | cfg+0x33C (0..29) | 20 |
| subtitles | cfg+0x348 (0 = on) | on |
| music / sfx dirty flags | cfg+0x374 / +0x378 | |
| manager master music / sfx | 0x53FC84 / 0x53FC88 (0..64) | 64 |
| manager mute | 0x53FC80 | 0 |
| options screen | 0x482EAC (sliders +0xD8/+0xDC; preview `%sM_%4d.WAV` / `%sX_%4d.WAV`, 0x485A4A) | |
No `sgens.ini` keys for sound were found (no profile strings); how cfg is persisted was not traced (L).

## 6. Implementation checklist
1. One mixer with 8 voices; steal lowest priority / oldest. Volume = vol/64 × master/64 (linear
   amplitude); MUSI uses the music master, everything else the sound master.
2. Sound table entry → file path by prefix/number/COMMON/MUSI rules (§1.2); stream flag 4.
3. Per-frame: positional volume/pan (§1.5), linear fades, loops with random re-trigger delays,
   watchdog, event 46 to the cue owner on completion.
4. Subtitles: on start of a flag-1 sound, replace the subtitle line with the .txt (escapes, per-line
   timing, scroll), one line at (10,209)-(639,225), Action.fnt.
5. Natives in §1.6.
