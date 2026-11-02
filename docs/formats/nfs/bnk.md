# Need for Speed III: `.bnk` sound banks and EA's audio codecs

Status: **implemented**:

- banks and PT headers: `src/engines/nfs/bnk.cpp`;
- the codecs: `src/common/audio/ea_audio.cpp`.

On the install and the CD, all 976 banks read and all 11,775 sounds
decode (`reluva-tool nfs-survey`).

## How it was worked out

October 2026:

- **Banks and PT headers.** Read off hex dumps, then checked with a
  survey script over every bank:
  - tag frequencies and values;
  - each sound's bytes per sample from the spacing of the data offsets,
    which separates three codings.
- **EA-XA.** From CD-XA ADPCM, which it derives from:
  - CD-XA's filters fit, and the shift nibble's range (0-12, mostly 3-5)
    is CD-XA's;
  - high nibble first gives 20 times less clipping than the other order;
  - decoded, the sounds are as smooth as the PCM ones;
  - a car-description narration's spectrogram is plainly speech.
- **MicroTalk.** Compression 9 is EA MicroTalk, as in Beasts & Bumpkins'
  PT files. Reluva's decoder is written from the algorithm as described
  by Andrew D'Addesio's public-domain utkencode
  (github.com/daddesio/utkencode), read as a reference:
  - its constant tables are the format's, and were compared value for
    value;
  - on all 1,869 English speech sounds (52 million samples), Reluva's
    output equals that decoder's, sample for sample;
  - a decoded police radio call's spectrogram is speech over radio hiss.

## Bank layout

All values are little-endian unless noted.

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | `BNKl` |
| 4 | u16 | version: 2 for most banks, 4 for the car engines, 3D sounds and track ambiences |
| 6 | u16 | number of slots |
| 8 | u32 | size of the headers' area (slots and PT headers); the audio follows |
| 12 | 4 × n | version 2: each slot's offset to its PT header, counted from the slot itself |
| 12 | u32 | version 4: the size of the audio (the file's size less the headers' area) |
| 16 | u32 | version 4: −1 in all 74 |
| 20 | 4 × n | version 4: the slots, as above |

The game's parser (`0x502620`) takes the slots from +0x14 for version 4
and +0x0C for version 2 (`0x50265b`-`0x5026d9`; other versions get a
warning and are read as 2). Checked in October 2026: so read,
`Gen3d.bnk` (version 4) has its sounds in the same slots as `Gen.bnk`
(0, 1, 5, 10, 11, 15, 18, 24, 25, 29-35, 37, 40-47). Reluva's
`parseSoundBank` reads them so too. Until October 2026 it read every
bank's slots from 12, so for the 74 version-4 banks (car engines,
`gen3d`, `siren3d`, `tr3d*`, `tram*`) its slot numbers were two too high
and the last two slots were lost; the Tracks page's placed sounds,
which name slots of `tr3d*`, played the wrong ones.

A slot of 0 or −1 is empty. A few slots (a couple of hundred) point
into the audio data, at no header: leftovers by the look of them, and
taken as empty.

## PT headers

`PT\0\0`, then tags:

- a tag byte, a length byte, then the value in that many bytes,
  **big-endian**;
- `0xFC`, `0xFD` and `0xFE` open sections and have no value;
- `0xFF` ends the header.

| Tag | Meaning | Default |
|---|---|---|
| `0x82` | channels | 1 |
| `0x83` | compression: 7 EA-XA, 9 MicroTalk; absent, 16-bit PCM | PCM |
| `0x84` | sample rate (11025, 16000, 44100 in the retail banks) | 22050 |
| `0x85` | number of samples | |
| `0x81` | bits per sample (8 or 16; none of the retail banks has it) | 16 |
| `0x86`, `0x87` | the loop: its first sample and its end, the last sample (in 48 sounds the count instead, one past the last) | |
| `0x88` | the data's offset from the bank's start | |
| `0x8A` | 0 in all | |
| `0x92` | the 3D method: 1 software (iSND mixes it), 2 DirectSound 3D | 1 |
| `0x93` | room for the DirectSound 3D buffer's pointer, 0 in the files | |
| `0x00`-`0x25` | the patch's parameters, below | |
| `0x91` | the mixing quality, 0-100: 50 (182 sounds) or 100 (25, in the traffic cars' and `FeData/Plac`'s `car.viv`); below | 0 |

**How these were worked out** (October 2026). Each tag was surveyed over
all 408 banks. The game's sound code (iSND, linked into `nfs3.exe`) was
then read where it parses PT headers:

- `iSNDplatformresolve` (`0x522a80`) takes the sound's format:
  - `0x81` (8 or 16), `0x82`-`0x88`, `0x8A`;
  - `0x92` and `0x93`. With `0x92` = 2, it makes a DirectSound 3D buffer
    for the sound (its message: "3D METHOD SET TO DIRECTSOUND, BUT NO 3D
    HARDWARE POINTER PRESENT"), keeps the pointer in `0x93`'s four
    bytes, and rewrites `0x92` as 3, resolved.

  `0x92` = 2 is in the positioned sounds' banks (`tr3dNN`, `gen3d`,
  `siren3d`); the rest have 1.
- The loop: with `0x87` as a length, 416 loops would run past their
  sound's end; as the last sample, all fit but 48, which give the count.
- The routine at `0x501de8` reads the tags below `0x26` into a table of
  38 parameters (the "patch"; iSND's messages speak of tagged patches). `FD` and `FE` mark the patch's
  alternatives. An alternative is used when two values (the call's) fall
  within the ranges held in parameters 1-2 and 3-4.
- `0x91` is read at `0x522658`, by the routine that hands a voice to
  iSND's software mixer (`0x522510`, called from `0x501d65`); see The
  mixing quality below.
- The routine that starts a voice (`0x501978`) uses them, and the ones
  that update it (`0x502cb0`, `0x502d30`, and the 100 Hz tick at
  `0x500c80`). Those name most of them; see Patch parameters below.

The values, over all the banks (most common first):

| Tag | Values | In |
|---|---|---|
| `0x06` | 25 (5,450), 50, 80, 20, 100 | nearly every sound |
| `0x07` | 59, 68, 38, 45, 50... | ambiences and positioned sounds |
| `0x0A` | 4 (4,047), 6, 5, 12 | nearly every sound |
| `0x0C` | 40, 65, 73, 88, 7 | ambiences, positioned |
| `0x0E` | 100, 51, 90, 30, 115 | ambiences, positioned |
| `0x0F` | 10, 50, 30 | ambiences, positioned |
| `0x11` | 200, 300, 100, 150 | ambiences, positioned, rain |
| `0x13` | 127, 100, 110 | ambiences, positioned, sirens |
| `0x19`-`0x23` | 0 to 1,826 | a few ambiences |
| `0x08` | 0 (54), 2, 3 | 58 sounds: `putt`, `sputt`, positioned sounds |
| `0x09` | 3, 4 | the 4 sounds with a `0x19` envelope (`tr3d00`, `tr3d04`, `tram00`, `tram04`) |
| `0x12` | 147 to 191 | 25 sounds in cars' banks |
| `0x1C` | 0 | the 2 envelopes of `tr3d04` and `tram04` |

None of the install's 4,426 sounds has `0x00`, `0x14`, `0x16`, `0x18`,
`0x1A`, `0x1B` or `0x25` (October 2026, a survey of the 408 banks on the
install, alternatives included).

## Patch parameters

From the routines above (random amounts come from `0x5224e0`, which
gives −x to x):

| Parameter | Meaning |
|---|---|
| `0x01`-`0x04` | the key and velocity ranges an alternative (`FD`/`FE`) is for |
| `0x05` | the voices the sound may take, a bit each (default −1, any) |
| `0x06` | the priority (default 0). When no voice is free, a sound takes the allowed voice of lowest priority below 102, the oldest of equals, whatever its own priority (`0x4ff86c`) |
| `0x0B` | the number of voices it takes (default 1) |
| `0x07` | the root key: played at key k, the sound is (k − root) semitones up |
| `0x0A` | the pitch bend's range, in semitones each way (4 on most sounds; 10-16 on the engines) |
| `0x0C` | the pan (0-127), with `0x0D` a random amount either side |
| `0x0E` | the volume (0-127), with `0x0F` a random amount either side |
| `0x10` | the fine tune, in cents, a signed byte (249 is −7) |
| `0x11` | a random amount of pitch either side, in cents |
| `0x15` | a table the pan is read through, after the caller's pan is added (`0x501bcb`, and `0x4e9a43` when the pan changes) |
| `0x17` | a table the bend is read through (none on the retail sounds' engines) |
| `0x1D`, `0x1E`, `0x1F` | a volume wobble: its table (the volume is × value / 127, `0x5014fa`) and period (100 Hz ticks), started at a random point if `0x1F` is set |
| `0x20`, `0x21`, `0x22`, `0x23` | a pitch wobble: its table, its period in 100 Hz ticks, its depth in cents, and (if set) a random starting point |
| `0x24` | a random detune either side, in cents: drawn once per call (`0x50204c`), the same for all the call's voices, and added when `0x24` is set (`0x501b38`) |
| `0x25` | how much of the caller's pan is used (default 1): the caller's pan p becomes `0x25` × (p − 64) + 64 (`0x501b8b`, `0x4e9a13`); 1 leaves it, 0 centres it |
| `0x09`, `0x19`, `0x1C` | a volume envelope (below): its number of segments (default 1), its table, and its starting level (default 127) |
| `0x12` | a table the voice's final volume (0-127) is read through (`0x501526`) |
| `0x13` | the send to iSND's effects bus 0 (0-127, default 0), below |
| `0x00`, `0x08` | copied into the voice (+0x0D, +0x36; `0x501b6d`, `0x501c4e`) and never read back. `0x00` defaults to 1 (`0x5017c0`), `0x08` to −1 (`0x501821`). The music streams' headers carry `0x00` = 2 (and `0x06` = 101) in all 1,733 of them, so their voices get 2. See Unread voice fields below |
| `0x1B` | the stream's chunks per second (default 15, `0x5017d0`). Copied into the voice (+0x0B, `0x501ce7`); only the audio streamer (`sstream.c`) reads it back, below |
| `0x14`, `0x16`, `0x18`, `0x1A` | stored by `0x501de8` but not read when a voice starts |

Tables (`0x12`, `0x15`, `0x17`, `0x19`, `0x1D`, `0x20`) are given as
offsets from the tag; `0x5018dc` turns them into pointers.

**The volume** of a voice, 0-127, is computed at `0x5014a0` as

caller's volume × (patch volume × velocity / 127) × envelope level ×
master / 127³,

then multiplied by the `0x1D` wobble's table value / 127 when there is
one, then read through `0x12`'s table when there is one.

**The envelope** (`0x19`): pairs of u32, a length in 100 Hz ticks and a
level (0-127). The level starts at `0x1C` and moves in a straight line to
each pair's level over its length (`0x501c32`; the tick at `0x500d4c`). A
negative length holds forever. After the last of `0x09`'s segments the
voice stops (`0x500d72`). The default table (`0x5678a8`) is one pair,
(forever, 127): a sound plays at full level until it ends. `tr3d04` and
`tram04` start theirs at level 0, so they fade in.

**The effects send** (`0x13`): the voice's level into effects bus 0 is
`0x13` × the caller's send × the bus's master level / 127² (`0x501d26`,
`0x50319a`); iSND's mixer then sends (left + right) / 2 × that / 128 of
the voice to the bus (`0x500654`). The game sets the bus up once
(`SNDfxinitbus` at `0x4e9c70`, called from `0x418759` with 127 and 20),
and turns its master level to 127 (`0x418770`) when a test of the
player's car at `0x4174dd` (`0x420bf0`) holds, and back to 0 otherwise.
What the bus does (an echo or reverb, by its use) and what `0x420bf0`
tests (tunnels?) weren't followed.

**The mixing quality** (`0x91`). At `0x522658`, `0x91` (0 if absent)
plus a game-wide offset (`0x9ef616`) is clamped to 0-100 and passed to
the mixer's voice setup (`0x5000b8`). There it picks one of two sets of
routines for the voice's sample format (`0x500100` and on): 25 or less,
the set at `0x9f6aec`; above, the set at `0x9f6afc`. With MMX (CPUID bit
23, `0x4f1dc2`) both sets hold the same routines (`0x4ffda2`). Without
it they differ: `0x5211c0`/`0x521310` below, a resampler that repeats
samples by a 16.16 step (`0x52125b`), and `0x521bc4`/`0x521800` above,
which choose separate routines for rates above, below and at the
output's (`0x52175c`). The offset is set at `0x40fcde`: 0, or −50 when
the audio option at `0x6fbbe0` is 0 (it is 1 by default, `0x4725f2`).
So the retail sounds (50 or 100) get the better routines unless that
option drops the 50s to 0. The only code write to `0x6fbbe0` is that
default. It is the **`enginesound`** option (October 2026): the options
table (`0x5563e8`; entry name at `0x556bcc`, value address at
`0x556bdc`) names it, with the list `0x55631a`, text 159 "Standard" and
160 "Optimal", so 0 Standard and 1 Optimal (the default). No menu has
`listname=enginesound` (none of the install's or the CD's `.mnu`
files), so nothing in the game sets it: only `config.dat` does, saved
and loaded whole, at offset 0xA0 (the install's has 1). A
`config.dat` with byte 0xA0 at 0 gives the nearest-sample sounds
below.

What a set is. The set holds one routine per sample format: 8-bit mono,
8-bit stereo, 16-bit mono, 16-bit stereo (`0x5000b8`'s flags 1, 2, 4,
8, from the bits and channels at `0x5226f8`-`0x522981`). Each is the
setup of the voice's resampler: it fills in the resampler's three
routines (set the step, count the input wanted, resample) at voice
+0xDB0, +0xDB4, +0xDB8 (`0x5001d3`). The step is the 16.16 ratio of
input to output rate, at most 8 (`0x5001d8`). Everything else is the
same in both sets: the decoders that turn 8-bit, 16-bit or EA-XA data
into float samples (table `0x9f6a6c`, chosen by format and compression),
the mixers that apply the levels (`0x9f6adc`-`0x9f6ae8`: `0x520a40`
mono, `0x520e00` stereo, the same for 8 and 16 bits), and the mix down
to 16 bits with its clipping (`0x521d00`). So the only difference is
how the samples are resampled. Read from the code (`0x4ffe61`, the
non-MMX table):

- **25 or less** (`0x5211c0` mono, `0x521310` stereo): nearest sample,
  rounded down. Each output frame copies input frame ⌊position⌋ as it
  is, and the position steps by the 16.16 step (`0x5212cb`,
  `0x521430`); the fraction carries to the next block (`0x5212e6`).
  There is a branch that repeats the previous block's last sample
  (`0x5212aa`), but the test that turns it on (`0x5212e8`, the step's
  integer part against the whole step) never holds.
- **Above 25** (`0x521bc4` mono, `0x521800` stereo): linear
  interpolation. Setting the step picks one of three pairs of routines
  (`0x521b20`, `0x52175c`): above 1, below 1, or exactly 1. Each output
  is a × (1 − t) + b × t, t the fraction × 1/65536 (`0x521972`,
  `0x521a9c`; the bulk in `0x5273c0` mono and `0x527330` stereo, the
  routines Reluva runs natively). The last input sample is kept
  (+0x10) so the first output of the next block interpolates across
  the join. When the step crosses 1 the position is reset
  (`0x521b7a`, `0x5217b6`). There is no filter beyond the linear one.

At a step of exactly 1 both sets pass the input through untouched
(`0x52125b`, `0x521a10`), so a sound played at the output's rate sounds
the same either way. Otherwise the low set gives a stepped wave: its
images and aliasing are heard as a grainy, metallic edge, worst when a
sound is pitched down a lot (an engine at low revs). The high set
smooths that but still aliases somewhat when pitched up.

With MMX (`0x4ffda2`) both quality bands get the same routines
(`0x51fc38` 8-bit mono and so on). They are built like the high set
(the same three-way choice of step, `0x51fb94`) and interpolate on
integers (`0x51f9de`: two 8-bit samples times their weights; the bulk
in MMX at `0x527170`). On an MMX machine `0x91` changes nothing.

In practice: on a machine without MMX, with the option at its default,
every retail sound (50 or 100) gets linear interpolation and the
nearest-sample routines are never used. With the option at 0, the 182
sounds at 50 drop to nearest-sample and the 25 at 100 (the traffic
cars' and `FeData/Plac`'s `car.viv`) stay linear. Reluva's x86 core
reports a Pentium without MMX (`common/x86/cpu.cpp`, CPUID), so it runs
the non-MMX routines.

**The MMX path is integer throughout** (transcribed October 2026). With
MMX `0x4ffda2` replaces the mixers (`0x51f580` 8-bit mono, `0x51f680`,
`0x51f750` 16-bit mono, `0x51f850`), the resamplers and the mix down
(`0x520920`) too, and the mix bus holds 16-bit words, not floats:
- **Levels.** The set-up stores each level L (−128 to 127) as the word
  L × 256 (`0x51f5b0`, `0x51f780`); the mixer multiplies with `pmulhw`,
  so a 16-bit sample s gives ⌊L × s / 256⌋ (`0x51f7dc`), rounded down.
  An 8-bit sample goes in as the high byte of a word whose low byte
  is left from the previous pass (`punpcklbw mm6, mm5`, `0x51f60c`):
  an error under 1/256 of a step.
- **The bus.** Each voice is added with signed saturation (`paddsw`,
  `0x51f7eb`), so the bus clips at ±32767, half scale, voice by voice,
  in the order the voices are mixed.
- **The mix down** doubles each word with saturation (`paddsw mm0,
  mm0`, `0x520958`). So the output is always even, and clips at
  −32768 and 32767 (32766 from below).
- **Resampling** (`0x527170`, the 8-bit bulk): the weights are
  (0xFFFF − frac) / 2 and frac / 2, 15 bits, applied with `pmaddwd`
  to the samples × 256 and shifted down 23: rounded down again, then
  saturated to 8 bits (`packsswb`). The scalar edge (`0x51f9de`)
  does the same in 32 bits.

The float path (`0x520a40` and on, `0x521d00`): gain L × 1/127
(`0x56ad10`; at 127 exactly 1, with no multiply), sums kept in float,
and one rounding at the end, to nearest (adding 1.5 × 2²³, `0x55188c`),
then a clip to −32768..32767 (`0x521d34`). So the two differ:
- the gain: L/128 with MMX, L/127 without, 0.07 dB quieter;
- rounding: down at every step with MMX, about −1 step per voice on
  average, against one rounding to nearest;
- resolution: the MMX output's lowest bit is always 0;
- clipping: the same limits at the end, but MMX saturates a half-scale
  bus as each voice is added, so a loud voice clips there and later
  ones of the other sign subtract from the clipped value.

Worked through numerically on `Gen.bnk` slot 32 (October 2026, from the
transcriptions above, not by running the routines): against the float
result, MMX at L = 127 differs by RMS 78 steps (max 255), nearly all
the 127/128 gain (−42 dB); with the gain taken out, by a mean of −1.0
and RMS 1.15 steps (max 2), at L = 64 −0.75 and 0.93.

**Unread voice fields** (`0x00`, `0x08`). The voices are one static
array, 16 of 0x60 bytes at `0xa09b3c`, and voice pointers are only ever
made as `0xa09b3c` + n × 0x60. A search of the whole of `nfs3.exe` for
accesses at +0x0D and +0x36 (byte, word or dword, and the absolute
`0xa09b49`/`0xa09b72` forms) finds only the two writes. The dword reads
at +0x34, +0x35, +0x36 (`0x501b90`, `0x501d26`, `0x501d35`, `0x50319a`,
`0x4e9e91`) are all shifted right 24, so they read the bytes at +0x37 to
+0x39. The other reads at +0x0D in iSND (`0x4e9305`, `0x502771`,
`0x5029d0` and others) are on the stream buffer records, which keep
flags at +0x0C/+0x0D, not on voices. `eacsnd.dll` is the DirectSound
driver (`iSNDdirectplay3d` and the like), loaded by name rather than
imported. Its calls take buffers, rates and positions, and it cannot
see the voice array. So `0x00` and `0x08` have no effect in this build.
Their meaning for EA's other games stays unknown.

**The streamer's chunk rate** (`0x1B`). `iSNDparseheader` copies the
stream's `SCHl` PT header into `0x5646a0` (`0x4e8144`) and starts a
voice from it (`0x4e8a54`). It then reads the voice's +0x0B
(`0x4e8a8d`) into `0x5646ff`. That value is used three ways:

- `SNDstreambuftime` (`0x4e7ce0`): the chunks still queued (from
  `SNDstreamchunksleft`, `0x4e7c58`) × 1000 / the value
  (`0x4e7d95`-`0x4e7dbd`), the buffered time in milliseconds. A value
  of 0 asserts "THIS FUNCTION REQUIRES A NEWER AUDIO STREAM FORMAT"
  (`0x4e7d5a`).
- `SNDstreaminterrupt` (`0x4e7a14`): in state 2 (`0x5646fd`, set as
  the stream's voice starts, `0x4e8a98`), it keeps the next
  3 × value chunks of the queue (about 3 seconds) and frees the rest
  (`0x4e7ab8`-`0x4e7afc`), so new data takes over soon.
- It is passed to `0x5017b4`, a stub that returns 0 (`0x4e7c86`,
  `0x4e7e71`).

No retail stream has `0x1B`, so it is 15. The `.mus` files fit that:
their `SCDl` blocks hold 1,456 or 1,484 samples in turn, 1,470 on
average, which is 22,050 / 15 (`atlarock.mus`).

**The pitch** is in cents from the sound's own rate: the rate is
× 2^(cents / 1200), from a table of 2^(2^i cents) (`0x522e90`, the
table at `0x56adec`). A voice's cents are

fine tune − (root − key) × 100 + (bend − 64) × range × 100 / 64,

the bend (0-127, 64 for none) set when it starts and by `SNDpitchbend`
as it plays. Checked against the voices in a memory dump of a race: a
range-6 sound at bend 77, 78 and 74 was at 121, 131 and 93 cents.

**An engine's pitch from its revs** (`nfs3.exe` `0x4119a9`,
`0x417390`): the game plays the car's engine (`car.bnk` in its
`car.viv`) at key 60 and bends it each frame by

bend = min(100, 127 × rpm / (redline + 1000) × Doppler),

the redline from its `carp.txt` ("engine redline in rpm"). The 1000 is
2000 for cars whose driver record (+0x220) has a flag other than 1 (the
player's car had 1000), and 500 comes off while the car's field at
+0x164, its gear by the look of it, is above 5. The Doppler factor
(`0x4124d0`, 16.16) is 1 for the player's own car: the idle figures below
match with it so.
So a Corvette (root key 74, fine tune 28, range 15, redline 6000) idling
at 900 rpm is at bend 16, −2497 cents: what a memory dump of a race
showed for its engine voice. At the redline the bend reaches 100. Other
sounds use the bend too: the track's sound sources (Hometown's biplane)
are bent by their Doppler shift alone, from 78 as they come to 54 as
they go.

Which sounds use which coding:

- **MicroTalk:** all the speech (police radio, the dispatcher, the voice
  assistant, the co-driver's lines).
- **EA-XA:** track ambiences, rain, car and manufacturer descriptions,
  podium speech.
- **16-bit PCM:** engines, sirens, crashes and the front end's sounds.

Some PCM sounds run past the next sound's data or the end of the bank.
Reluva uses as much as is there.

## Which bank a race sound comes from

Read from `nfs3.exe` (November 2026). At a race's start `0x413bc0`
loads the banks into a table of 12-byte entries at `0x571124`
(`0x410b50`: +0 the bank, +8 its memory):

| Entry | Bank | Loaded at |
|---|---|---|
| 0 | the traffic car's (`strex`, `scrate`, `struck`, `slimo`; without 3D sound `trex`...) | `0x413cd4` |
| 2 | `Gen3d` with 3D sound (`0x5525a4`), else `Gen` | `0x414246` |
| 3 | the track's: `tr3dNN` (3D) or `tramNN`, + `n`, `w` or `nw` by sky; `trambNN` from track 9 | `0x414425` |
| 4 | `rain` | `0x41447d` |
| 5 | `siren3d` or `siren` | `0x414178` |
| 6, 7 | `cnt`, `lap` | `0x4142d2`, `0x414358` |
| 8 | `copspch` | `0x4141d0` |
| 9-14 | `tutor` (9), `vocasst` (10), the `off%02xA`-`C` (11-13) and `disp%02x` (14) speech, numbered by the track modulo 4 (Seen in footage, below) | `0x4144f3`-`0x4147e1` |
| 22 | another traffic bank (`s...`) | `0x413e0a` |

`0x4138e0` loads the cars' banks (`scar.bnk`, `car.bnk`, `ocar.bnk`) at
an index it is passed (+0 the bank, +8 its memory, as `0x410b50`).
Its four calls (November 2026):

| Call | Entry | Bank | Which car |
|---|---|---|---|
| `0x413d6d` | 0 | `scar.bnk` (`genscar` if missing) | split screen (`0x6fd3b0` = 1): set-up 0 |
| `0x413ea6` | 22 | `scar.bnk` | split screen: set-up 1, the second player |
| `0x41402c` | 0 | `car.bnk` (`gencar` if missing) | otherwise: the local player's (`g_setups[0x6fd4f4]`), unless the `TDRIVE` cheat (`0x6fd4fc` bit 2) is on (another bank, not followed) |
| `0x4140f5` | 1 | `ocard.bnk` with 3D sound (`0x5525a4`), else `ocar.bnk`; `genocar` if missing | one car for all the others: set-up 3 in split screen (car 0 with no computer cars), else set-up 1, or 0 when the local player is 1 |

So **entry 1 is the other cars' bank**, `ocar.bnk` from one car's
`car.viv` (path + `car.viv`, `0x536ac0`), so the other cars share one
car's sounds. `0x417080` takes entry 1 for sound number 0 and for
slots 1, 3 and 27-30 (`0x552798`, below). The front end also plays from entries 1 and 2
(`0x4c9990`: entry 1 for sounds 0-4, else 2; freed at `0x4c9a00`).

`0x417080` plays a sound on a voice slot. Its bank: entry 3 for slots
18-21, 26, 32-36 (the track's sounds and the player's road loops);
else by sound number (0 entry 1, 2 entry 0, 10 and 11 entry 2, 12
entry 5 as sound 1); else the byte for the slot in `0x552798` (4-byte
stride): slots 0-3 0, 1, 0, 1; 4-15 2; 16 0; 17 22; 18-21 3; 22-25 2;
26 3; 27-30 1; 31 2; 32-35 3; 36 0; 37 22. So the other cars' skids (slots 22-25) and the
player's (8-11) are `Gen.bnk` slots 41-47 (`0x414d70`), and the moving
objects' sounds (slots 20, 21) are slots of the track's bank. The voice
slots are listed in [nfs.md](../../engines/nfs.md) (Dolby, which voices
are behind).

## The moving objects' sounds

Read from `nfs3.exe` (November 2026), `0x410e30`, each sound step.

**Which objects.** Only the `.col` file's objects block (id 7) counts:
`0x41dcc0` keeps its address at `0x5dd840`, the one entry of the list
at `0x552600` (the next entry is 0 and nothing writes it). Id 18,
Atlantica's second block, is not read here, nor are the `.frd`
objects. The n-th type-3 object of block 7 takes row n of the track's
table (`0x552744`, one pointer a track, copied to `0x570dd0` at
`0x4116af`). The tables overlap: no row count is stored, a track uses
as many rows as it has objects.

**A row** is nine s16 (18 bytes):

| Offset | Meaning |
|---|---|
| 0 | range r, in world units: heard within r (x and z, then y, each by the larger plus a quarter of the smaller) |
| 2 | phase: ticks added to the time before the object's place is worked out |
| 4 | the main sound; below 0 the object is skipped |
| 6, 8 | fade in: silent up to tick +6 of the object's cycle, full at +8; +6 below 0 means always on |
| 10, 12 | fade out: from L − +10 to L − +12 (L the cycle, keys − 1 times the key delay; `0x40f6d0` gives the tick in the cycle and L) |
| 14 | a sound played instead during the fade in; or −2 or −3, special (below) |
| 16 | a sound played instead during the fade out; for −2/−3 rows, minus the special sound's number |

The two nearest objects in range are kept (`0x410ec5`-`0x411025`) and
played on slots 20 and 21. For the second player's listener the stop
and the playing-sound check use 0x22 + n, but the play itself still
adds 0x14 (`0x4113d4`); the call's last argument is the player × 32. The volume is 127 × (r² − d²) / r², scaled by the fades.
The slots are swapped when the new first is the old second or the new
second the old first (`0x411047`-`0x411094`, the old pair at
`0x570dd4`, `0x570dd8`). Both start at 0 (bss), and with one object in
reach the second is −1, so a lone object always lands on **slot 21**.
A sound already playing that is row +14 or +16 runs to its end before
the main one comes back (`0x4111f6`-`0x41122d`).

**Special rows.** With +14 = −2 the main sound plays as usual, and when
the cycle's tick equals +8, +10 or +12 sound −(+16) is also started on
slot 19 (`0x411232`-`0x411286`). With +14 = −3 the same happens on
every tick that is a multiple of +8 (`0x411290`-`0x4112d7`). The time
used is the step count rounded down to even, plus the phase. Slot 19 is
also one of the `.frd` placed sounds' slots (`0x41d210`).

The tables, by track and object (rows past a track's objects omitted):

| Track | Object | Range | Phase | Sound | Fades and specials |
|---|---|---|---|---|---|
| 0 Hometown | 0, the 141-key loop | 300 | 0 | 31 | none |
| 1 Redrock Ridge, 5 Lost Canyons | 0 | 250 | −32 | 6 | none |
| 1, 5 | 1 | 275 | 32 | 7 | −2: sound 8 at ticks 670 and 1822 |
| 1 | 2, 3 | | | none | |
| 5 | 2 | | | none | |
| 2 Atlantica | 0 | 400 | 0 | 6 | none |
| 6 Aquatica | 0 | 500 | 0 | 6 | −2: sound 7 at ticks 900, 2560, 8988 |
| 6 | 1 | 175 | 0 | 13 | none |
| 7 Summit | 0, 1 | 100 | 0 | 0 | −3: sound 0 every 128 ticks (2 s) |
| 8 Empire City | 0, 1, 3 | | | none | |
| 8 | 2 | 250 | 0 | 6 | in 32-96, out L−180 to L−120 |
| 8 | 4 | 250 | 0 | 3 | in 32-180, out L−240 to L |
| 3, 4 | | | | no table (0) | |

So Hometown's slot 21 is its one `.col` object, the loop flown 52 m
up (`frd.md`), sound 31 of `tr3d00`/`tram00` (a 0.42 s loop). Its
17-key spinner is only in the `.frd` and makes no sound here. Checked
in a run (`gh2-run1`, a single race on Hometown, `-watch 0x4113d7`):
all 40 plays logged were slot 0x15, sound 0x1f, the volume rising
0, 1, 3, 4, 5 as it came into range at 42 s.

Not every sky has these sounds. The night and wet banks of Hometown
(`tram00n`, `tram00w`, `tram00nw` and the `tr3d` ones) have no sound
31, and Summit's (`tram07n`, `w`, `nw`) no sound 0. So only by a dry
day does Hometown's loop or Summit's −3 row have a sound to play (read
from the banks; what the game does with a missing sound wasn't
followed).

## Seen in footage (November 2026)

From 18 YouTube recordings of the original game (`Hometown_Day` and so
on, one race per track by day and night; AAC, so good for what plays
when, not sample for sample). Times are clip times, m:ss. Sounds were
looked for by cross-correlating the decoded sounds, at a range of
pitches, with the clips' sound.

**The engine's pitch.** `Summit_Night`'s car is an Aston Martin DB7
(`Amd7`: root 68, fine tune −7, range 14, redline 6000) with the
automatic (N1234R). Its partials rise with the tach in each gear and
fall at each change; the brightest goes 161 to 305 Hz in first
(0:24.3-0:27.3). What the footage shows:
- **The changes glide.** The pitch falls over about 0.5 s, not at
  once: 1-2 at 0:27.6-0:28.2, 440 cents; 2-3 at 0:31.0-0:31.6, 640
  cents. The tach falls as slowly.
- **The shift points sound alike.** Before both changes the pitch
  holds at 305-308 Hz for 0.3-0.5 s, with the tach still at about
  5,300 rpm. That is below 5,512, where the formula's bend reaches its
  cap of 100, so the cap isn't what holds it.
- **Against the formula.** Read off the tach (about ±300 rpm), the
  changes went 5,400 to 4,100 and 5,300 to 3,700 rpm. The formula
  gives 520 and 640 cents; a pitch in proportion to the revs, 480 and
  620. Over the revs seen (3,600-5,400) the pitch rises 0.37 cents an
  rpm; the formula's is 0.40, and a proportional pitch's 0.32-0.48. So
  the footage fits the formula, but it can't tell it from a pitch in
  proportion to the revs: the two differ by under 10% in this range.
  They part at low revs, which the clips only show at the launch,
  with wheelspin. If the brightest partial is the 8th of sound 0's
  43.07 Hz cycle, the formula puts the revs 200-400 below the tach's
  reading, within what the tach can be read to.

**Wall hits and skids.** Not settled. The `Gen.bnk` hits (29-37) and
skids (41-47) give no clear match in the races: most peaks are in the
menus, and the few in races don't hold over the sound's whole length.
The one a frame bears out is `Country_Woods_Day` 2:39.4, the car
sliding into a snow bank: slot 30 at −280 cents (correlation 0.46 over
0.4 s), where a code-5 bank should give slot 29
([col.md](col.md)). One match isn't enough to say which.

**The moving objects.** None heard. Sounds 6, 7 and 8 (`Redrock_Ridge`,
`Lost_Canyons`), 6, 7 and 13 (`Aquatica`), 0 (`Summit_Day`), 3 and 6
(`Empire_City`) and 31 (`Hometown_Day`) give no peak above the clips'
noise at ±200 cents. That fits Reluva's own Hometown run, where the
loop played at volume 5 of 127 or less as it came into reach. The
night clips have no sound 31 or 0 to find (above).

**Sirens and speech** (the Hot Pursuit clips):
- `Summit_Night` (a racer's pursuit, [physics.md](physics.md)) has a
  wail at 7:20-7:27.5, the shape of `siren.bnk` slot 0 (up to about
  1.2 kHz and down), then a yelp, slot 1 (7:27.7, correlation 0.48).
  Slot 1 also plays in `Aquatica_Day` (4:29.8, 4:37.8) and
  `Hometown_Night` (1:01.3).
- The two ticket scenes of `Summit_Night` (3:06.3, 5:07.1) carry
  `copspch` slots 17 and 19.
- The dispatcher speaks throughout, and his lines are `disp03`'s:
  slots 36, 38-40, 65, 78, 81 and 99, which differ in the other
  `disp` banks, are heard. So Summit uses Rocky Pass's speech. The
  code says why (`0x4146c6`-`0x4147c6`): in a Hot Pursuit each bank's
  number (`%soff%02xA`, `B`, `C`, `%sdisp%02x`) is the track modulo 4, or the
  track itself past 7. So tracks 4-7 take the speech of 0-3, and only
  0-3 and 8 have banks. `Aquatica_Night` (track 6) likewise plays
  `disp02` lines (slot 36, the same in `disp08`), not `disp00` or
  `disp03` ones.

## EA-XA ADPCM

- Mono in banks: 28 samples in each 15-byte frame.
- The frame's first byte holds the filter (high nibble, 0-3) and the
  shift (low nibble, 0-12).
- 14 bytes of 4-bit residuals follow, high nibble first.

Decoding a residual:

    sample = residual × 2^(12 − shift) + (c1 × previous + c2 × before_previous + 128) >> 8

clamped to 16 bits. The filters (c1, c2) are CD-XA's, in 1/256ths:
(0, 0), (240, 0), (460, −208), (392, −220).

Filter numbers above 3 appear only in final, partly used frames (fewer
than 0.1% of frames); Reluva uses no prediction for them.

## MicroTalk

Mono, 22,050 Hz, 432 samples a frame, about 1.7 bits a sample here.

The stream:

- starts with header bits: reduced bandwidth, the multi-pulse threshold,
  the fixed gains;
- then holds frames back to back, bits read lowest first.

Each frame holds:

- 12 reflection coefficients, interpolated over the frame;
- four 108-sample subframes. Each has a pitch lag and gain, a fixed gain,
  and an excitation: multi-pulse (pulses and runs of zeros, coded by two
  models) or a coarse residual, at full or half bandwidth.

Synthesis is a 12th-order LPC filter. See the reference above for the
details.

Two details of the original's memory layout matter for exact output, and
Reluva's decoder keeps them:

- the adaptive codebook is read on into the frame being decoded;
- a pitch lag over 216 in the first subframe reads into the decoder's
  state before the codebook.

## Tools

- `reluva-tool bnk-info <file.bnk | file.viv name>` lists the sounds and
  their tags.
- `reluva-tool bnk-wav ... <dir>` writes each sound as a WAV file.

## On the Sounds page

A sound with a loop (the engines, ambiences) plays it over and over until
stopped, the loop's ends marked on its waveform. A sound with a bend
range is played as the game plays it: Left and Right move its bend, 4 at
a time. In a car's banks (beside its `carp.txt`) they move its revs
instead, 250 rpm at a time from idle ("engine minimum rpm") to the
redline, at key 60 and the game's bend for those revs; the header shows
the revs, bend and cents. Elsewhere a sound is played at its own root
key, as the key a caller would play isn't known. A sound without a bend
range moves a semitone at a time.
