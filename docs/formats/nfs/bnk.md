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
| 12 | 4 × n | each slot's offset to its PT header, counted from the slot itself |

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
option drops the 50s to 0. Only that one write to `0x6fbbe0` was found,
so what sets it to 0 (a saved setting copied in whole?) stays unknown.

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
the non-MMX routines. Whether the MMX integer routines round or clip
differently from the float ones was not transcribed.

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
