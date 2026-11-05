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
reports a Pentium without MMX by default (`common/x86/cpu.cpp`, CPUID),
so it runs the non-MMX routines; `nfs-host -mmx` reports MMX (below).

**The MMX path is integer throughout** (transcribed October 2026). With
MMX `0x4ffda2` replaces the mixers (`0x51f580` 8-bit mono, `0x51f680`,
`0x51f750` 16-bit mono, `0x51f850`), the resamplers and the mix down
(`0x520920`) too, and the mix bus holds 16-bit words, not floats:
- **Levels.** The set-up stores each level L (−128 to 127) as the word
  L × 256 (`0x51f5b0`, `0x51f780`); the mixer multiplies with `pmulhw`,
  so a 16-bit sample s gives ⌊L × s / 256⌋ (`0x51f7dc`), rounded down.
  An 8-bit sample goes in as the high byte of a word whose low byte
  is left from the previous pass (`punpcklbw mm6, mm5`, `0x51f60c`).
  That low byte is a byte of the samples before, so the error is up
  to one 8-bit step (255 in 16 bits), not 1/256 of one: about half a
  step too high on average (measured below).
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
then a clip to −32768..32767 (`0x521d34`). So the two differ (predicted; measured under Run side by side below):
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

**Run on the interpreter** (November 2026). Until then Reluva's x86 core
had no MMX, so the comparison above stayed numerical, from the
transcription (checked against the disassembly again at `0x51f774`,
`0x52092d`). Extended to mixes, with the Corvette's `car.bnk` 0 and 1
and `Gen.bnk` 29, 32 and 41 (a scratch script, not in the repo):
- **Rounding adds up.** With the gain taken out, 1, 2, 3 and 5 voices at
  L = 32 differ from the float mix by a mean of −0.8, −1.8, −2.6 and
  −4.4 steps (RMS 1.1, 2.0, 2.8, 4.6): about −0.9 a voice. One voice at
  L from −19 to 127 gives −0.75 to −1.0.
- **Clipping at the end** is the same: 2 to 5 voices at 127 clip 11-53%
  of their samples in both, within 1% of each other, and differ by RMS
  96-134 steps, the 127/128 gain again.
- **The bus.** One voice can't saturate it (at most 32,767 × 127 / 256,
  16,256). Two loud voices of one sign can. Three copies of a voice
  made loud (× 4), then two of it negated: the float mix gives the
  voice, clipped; the MMX bus stops at 32,767 after the first two
  copies and the two negated ones take it to about 0, RMS 24,700 steps
  apart. In the other order the two agree (RMS 211, the gain). So only
  then does the order of the voices matter, and only with MMX.

The interpreter now decodes MMX ([x86.md](../../engines/x86.md)), and
`nfs-host -mmx` has CPUID report it. Every MMX instruction in
`nfs3.exe` was found by a scan of the code database (`tools/nfsre`,
358 instructions in 19 routines; a linear sweep of `.text` with
capstone found no others). They are all behind the flag CPUID sets
(`0x5643b4`, written at `0x4f1dd4`):
- the sound mixer: the mixers `0x51f5dd`-`0x51f897` (8 and 16 bits,
  mono and stereo), the mix downs, one per output format (`0x520830`
  8-bit mono, `0x520870` 8-bit stereo, `0x520a10` 16-bit mono,
  `0x520920` 16-bit stereo; set at `0x4ffe05`-`0x4ffe36`, chosen by
  `iSNDmixreinit`), the bus clear `0x5209a0` (and
  `emms` at `0x520a00`), the resamplers `0x5270f0`, `0x527170`
  (8 bits), `0x527210`, `0x527290` (16 bits);
- the EA-XA decoders `0x52d1a0` (mono) and `0x52d250` (stereo), chosen
  at `0x521fc0` (`pmaddwd` with the nibble's coefficients, table
  `0x56ad70`);
- a copy of 8 bytes at a time (`0x5033e5`, all `movq`), used by the
  `memmove` at `0x4ea4f0`;
- `emms` after the FPU state is set (`0x4f0a06`, in `0x4f09c0`).

A race with `-mmx` (the race of `nfs.md`, 5 billion instructions,
`-deterministic 50000`, 100 seconds of the game's clock; `-count`
counted the routines' entries) ran without a fault, the same in three
runs. The MMX routines ran: the 16-bit mixers (`0x51f7ad` 44,405 times,
`0x51f897` 47,030), the 16-bit resamplers (`0x527210` 29,531,
`0x527290` 26,783), the bus clear (22,522), the EA-XA decoders (30,519
and 47,387) and the copy (141,833). The 8-bit routines did not (no
sound in the game is 8-bit, below), nor did any float mixer or resampler. The
recording (`-wav`, by the game's clock) against the float path's:
- **Level**: −0.03 dB over the run. The spectra agree within 0.5 dB in
  each band (0-250 Hz up to 9-11 kHz), in the menus and in the race.
  So nothing is added: no noise, no lost highs.
- **Resolution**: 99.96% of the MMX run's non-zero samples are even
  (the float run's 49.9%), as the doubling at the mix down predicts.
- **Clipping**: 0.045% of samples at full scale, 0.043% with floats.
- **Not sample by sample.** The two recordings part at the first sound
  (7.15 s): it starts 44 frames (2 ms) apart, and windows aligned by
  their best lag still correlate only 0.3-0.6, though their 10 ms
  envelopes follow each other. The clock counts instructions, and the
  MMX mixer runs in the interpreter (the float one natively), so the
  voices start at slightly different moments. So the predicted rounding,
  about −0.9 steps a voice, could not be seen in a race: it is far below
  the content's own offset (means of −15 to +180 steps). A check of that
  needs the two mixers run on the same voices: that is the next part.

**Run side by side** (November 2026). `reluva-tool nfs-mmx-check <game>
[trials]` loads the game twice, both in the interpreter with no native
routines. It sets the mixer up (`0x4fff18`, for 16-bit stereo output)
with the MMX flag (`0x5643b4`) clear in one and set in the other. Then
it starts the same voices in both with `iSNDmixstart` (`0x5000b8`) and
mixes 4,096 frames with `0x500864`, as the game does. So each side runs
the game's whole chain: decoder, resampler, mixer, bus and mix down.
The voices are `Gen.bnk` 29, 32, 41 (PCM), 33, 35 (EA-XA) and the
Corvette's `car.bnk` 0 and 1, and noise; at levels, pans and pitches
picked at random or set. The numbers below are from 40 trials, in
output steps, MMX less float. "Gain out" is MMX less float × 127/128.
- **Gain.** A least-squares fit of MMX on float gives 0.99214-0.99221
  in every case: 127/128 (0.99219). So it is L/128 against L/127.
- **Rounding, one voice.** With the gain taken out, one voice is about
  1 step low. Pitched voices are −1.00 (resampled samples have every
  fraction). At the output's rate it depends on the level: the bus gets
  ⌊L × s / 256⌋, and if L is a multiple of 2^k only 256 / 2^k fractions
  occur. Measured: L 127 −0.98, 100 −0.96, 64 −0.73, 32 −0.84, 8 −0.94,
  −19 −0.97 (RMS 0.96-1.17). The bus rounds down by half a bus step on
  average, and the mix down doubles that.
- **Rounding adds up.** At L = 32, 1, 2, 3 and 5 voices are −0.83,
  −1.67, −2.49, −4.23 at the output's rate, and −1.00, −2.00, −3.00,
  −4.98 pitched (0.602 or 1.375). So it is −1 a voice, not −0.9; the
  earlier −0.9 was L = 32 at the output's rate. With 5 voices, 42% of
  samples are 5 or more steps low, 28% 4, 20% 3, 0.1% within 0.5.
- **Random voices.** 1-6 real voices at random pans and pitches: gain
  out −3.49, RMS 16.9; clipped 0.40% (float 0.42%).
- **The mix down** (`0x520920`) doubles each bus word with saturation,
  exactly: checked on 4,096 random words. It adds no offset of its own.
- **The lowest bit.** Every non-zero MMX sample below 32,767 is even, in
  every case (float: 49-55%). Only the top, 32,767, is odd.
- **The bus saturates.** A voice made loud (× 4) at L = 127, step 1:
  three copies, then two negated. The float mix is the voice, clipped
  (output RMS 25,281). The MMX bus stops at 32,767 after the first
  copies, and the two negated ones take it toward 0 (output RMS 9,721;
  apart by RMS 22,020). The other way round (two negated, then three)
  the two agree: gain out −4.73, RMS 4.8. One voice can't reach full
  scale with MMX: at most 2 × ⌊32,767 × 127 / 256⌋ = 32,510. So with
  that voice float clips 40.6% of samples, MMX none.
- **EA-XA.** The MMX decoder (`0x52d1a0`) decodes lower. At L = 127, the
  MMX mix of the bank's EA-XA is 12.2 steps under the MMX mix of the
  same sound decoded by Reluva (`audio::decodeEaXa`) as PCM (RMS 16.6,
  max 40). The float path's decoder is within 0.18 of it (RMS 2.0,
  max 12). So with EA-XA the MMX path is about 13 steps low in all.
- **8-bit sources.** No retail sound is 8-bit (below), so the check makes
  some: the high bytes of the real sounds. The float path reads an
  8-bit sample as the signed byte × 256 (its table at `0xa0c2a0`: 0x7f
  32,512, 0x80 −32,768). The MMX mixer's low byte (above) makes it high
  by about L steps: one mono voice at step 1, +121.7 at L 127, +62.3 at
  64, +29.9 at 32 (RMS 148, 75, 37). Pitched, the 8-bit resampler
  (`0x5270f0`, `0x527170`, its edge `0x51f9de`) rounds down again, so
  it is less: +61 and +63 for mono at 0.602 and 1.375, +97 and +94 for
  stereo. The gain stays 0.988-0.991. The sound is otherwise sane: the
  same wave, a little noise and a small DC offset.
- **Other output formats.** The game asks for 16-bit stereo only. The
  others were run too. 16-bit mono (`0x520a10`) adds left and right with
  saturation and doesn't double, so its lowest bit is even half the
  time; gain out −2.41. 8-bit mono (`0x520830`, the same sum's high
  byte) is −0.51 of an 8-bit step; 8-bit stereo (`0x520870`) −0.01.

The check counts the MMX routines' entries as they run. The 8-bit
mixers (`0x51f5dd` 768 times, `0x51f6c7` 432), resamplers (`0x5270f0`
288, `0x527170` 288, `0x51f9de` 144) and the three mix downs the game
never asks for (`0x520830`, `0x520870`, `0x520a10`, 160 each) all ran,
without a fault, as did the 16-bit routines and `0x52d1a0`.

**No 8-bit sounds.** A sound is 8-bit only with a PT tag `0x81` of 8
(`iSNDplatformresolve`; 16 by default, `0x522557`). A scan of every
file of the install and the CD for PT headers (13,919: the banks, those
in `.viv` files, the `.mus` streams and the movies) found no `0x81` at
all: 9,717 MicroTalk, 3,333 EA-XA and 869 PCM, all 16-bit. `nfs3.exe`
holds no PT header of its own. The front end's sounds (`fesfx.bnk`),
the speech (MicroTalk) and the music are all 16-bit.
So the 8-bit MMX routines never run in the retail game; only the
side-by-side check runs them.

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

and at least 0, the redline from its `carp.txt` ("engine redline in
rpm"). The raw value is worked out at `0x4119bb`; the Doppler factor
multiplies it and the cap is applied after, at `0x41749e`. The 1000 is
the gearbox: the car's set-up record (car +0x220; player 1's is
`0x6fd52c`) has +0x1C 1 for the automatic (`0x6fd548`), and with the
manual it is 2000. 500 comes off while the car's +0x164 is above 5:
that is the time in the air ([physics.md](physics.md)), not the gear.
The Doppler factor (`0x4124d0`, 16.16) is 1 for the player's own car:
the idle figures below match with it so.
So a Corvette (root key 74, fine tune 28, range 15, redline 6000) idling
at 900 rpm is at bend 16, −2497 cents: what a memory dump of a race
showed for its engine voice. Other
sounds use the bend too: the track's sound sources (Hometown's biplane)
are bent by their Doppler shift alone, from 78 as they come to 54 as
they go.

**The cap holds in play** (November 2026). Logged with `-watch 0x4119af`
(the rpm, the divisor) and `-watch 0x4174b5` (the bend that is set,
`-watchread ebp-8`), a Corvette on Hometown, the player's engine at 32
Hz:
- Automatic (`sn-p2`, gas held from the grid, 70 seconds): all 2,213
  updates fit the formula exactly. Revving on the grid, in neutral, the
  rpm goes to the redline (6,000), the raw value to 108 and the bend
  stays at 100. In first the bend holds at 100 down to 5,513 rpm, and in
  the air (+0x164 6 and 10) the divisor is 6,500 and the rpm reaches
  6,250 (raw 122). 459 of 2,192 updates after the gas went on were
  capped, most of them on the grid.
- Manual (`sn-p1`, `0x6fd548` poked to 0): the divisor is 8,000. The
  rpm stops at the redline, so the bend tops out at 95 and the cap is
  never reached (2,532 updates).

So with the automatic the pitch stops rising at 5,512 rpm (the
Corvette's) while the revs go on to the redline: at 6,000 rpm it is 8
bends, 188 cents, under the uncapped value. The automatic changes up at R − R/8 (5,250 rpm),
so in a race the cap shows on the grid, in the air and in top gear.

Footage agrees. `Summit_Night` (the DB7, range 14, automatic) revs on
the grid at 0:17.2-0:21.7: its brightest partial holds at 328-331 Hz.
Before the changes up it holds at 305-308 Hz (Seen in footage, below):
bend 95, at the first tick past 5,250. The cap gives 5 bends more, 109
cents, 327 Hz. Without it the bend would be 108, 361 Hz; a pitch in
proportion to the revs, 350 Hz.

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
| 15 on | in a Hot Pursuit, one per racer (`0x56ee28`[car]): `IP<colour>spch.bnk` from the car's `IPspch<language>.viv` (colours bk, bl, br, dk, gr, lt, or, pu, rd, sv, wt, yl), or `IPncspch.bnk` (no colour) about one time in six; the car named on the radio, slot 0x00 for the dispatcher and 0x10, 0x20, 0x30 for the officers' voices ([physics.md](physics.md), The radio) | `0x40a010`, `0x413750` |

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
from the banks). What the game does about it (November 2026):
- **Hometown** never tries. `0x41bc20`, tested before anything else in
  `0x410e30`, is 0 on track 0 with night or weather on (`g_night`,
  `g_weather`), so the objects aren't looked at. In `sq-hn` (Hometown
  by night, 115 s, the car driven by the computer) `0x4113d7` was
  never reached.
- **Summit** plays anyway, and gets nothing. Its night and wet banks
  have 7 slots, slot 0 empty, so `iSNDplaytaggedpatch` (`0x501de8`)
  is given no patch and returns −8. A sound number past the bank's
  slots gives the same from `0x4e9bf8`. `0x4182b0` passes it on, and
  `0x417080` stores −8 as the slot's voice (`0x570de8` + slot × 8) and
  the sound as its number (`0x570dec`). Each later step the number is
  the same, so it only sets the volume, pitch and pan of voice −8,
  which match no voice (−8 again, `0x4e9540`). No other sound is put
  in its place. In `sq-sn` (Summit by night, 262 s) slot 21 played
  sound 0 from 233.1 s, 300 updates, its voice −8 throughout, and the
  −3 row started sound 0 on slot 19 at ticks 4992, 5632, 5760 and
  5888 (multiples of 128), each giving voice −8.

**Checked in runs** (November 2026). In these runs the computer drove
the player's car, so it went round the whole track: the car's task
slots were poked once they were filled (`-poke at=0x42c4c4#1`: car
+0x600 `0x409c00`, +0x608 `0x4088f0`, +0x614 `0x4a2f10`, as
`0x42c400` sets them for a computer car). Logged with `-watch` at
`0x4113d7` (the main play: slot in eax, sound in edx, volume at
[esp]), `0x411286` and `0x4112d7` (the specials):
- **Summit** by day (`sn-m1`, 4 laps, 800 s, stereo): slot 21 played
  sound 0, 713 updates, the volume up to 113, from 232.8 s (the end of
  the first lap) each lap. Only slot 21: the two objects were never in
  reach together. The −3 row started sound 0 on slot 19 nine times:
  234.6 s, then 244.6, 246.6 and 248.6 s (2 s apart, 128 ticks), then
  496.2 and 518.0, 753.5, 755.6 and 773.5. Its volume is the main
  sound's at that moment (24 to 98).
- **Aquatica** by day (`sn-a1`, 300 s): slot 21 played sound 13 (object
  1) from 152.2 s, 178 updates, and sound 6 (object 0) from 179.0 s,
  1,025, both up to 121-122. The −2 row started sound 7 on slot 19
  once, at 181.3 s, at volume 43.

Neither special stands out in the recordings: sound 7 against
`sn-a1`'s sound at 181.3 s correlates 0.13, no better than elsewhere
(0.15). The engines cover it, as in the footage (below).

More runs (November 2026, by day, 4 laps, 420 s, the same watches;
the tick in the cycle and L read at `0x4113d7` as esi+0x14 and +0x18,
esi being the kept object's record):
- **Redrock Ridge** (`sq-rr`): object 0 (sound 6) on slot 20 and
  object 1 (sound 7) on slot 21, together, four times a lap's worth
  (93.3 s, 206.4 s, 299.5 s, 391.0 s), up to volume 107. Object 1's
  cycle L is 2560 ticks, but it was in reach only at ticks 830-1040,
  384-664, 1218-1340 and 1960-2150: never at 670 or 1822 (+12 is −1,
  never), so sound 8 didn't start. With +8 poked to 600 (`sq-rr2`,
  `-poke ms=1000:0x55263c:600:2`; the run is otherwise the same) it
  started on slot 19 at 209.75 s, tick 600, and got a voice of its own.
  Even with the music and engines muted (`0x6fbbf0`, `0x6fbbf8` poked
  to 0) it can't be told from sounds 6 and 7 in the recording
  (correlation 0.15 at 210.9 s, 0.10-0.15 elsewhere).
- **Lost Canyons** (`sq-lc`): slot 21 played sound 7 (object 1) at
  67.4 s and 293.5 s, up to volume 125, and sound 6 (object 0) three
  times. Object 1's cycle is 576 ticks, so its −2 row can never fire:
  the tick never reaches 670 or 1822. The two rows are one in the
  table (`0x552622`), but the objects differ.
- **Empire City** (`sq-ec`): object 2 (sound 6, L 4375) and object 4
  (sound 3, L 2575), each alone on slot 21 on lap 1 (on lap 2 object
  4 came in while object 2 played and took slot 20). The fade in shows: object 2
  came in at tick 33 (109.75 s) at volume 1 and rose by 12 every 6
  ticks to 124 at tick 99, then held (the distance's share). Neither
  fade out was reached (object 2 in reach to tick 3133, its fade from
  4195; object 4 to 1898, its fade from 2335). With the fades poked
  into reach (`sq-ec2`: object 2's +10, +12 to 1400, 1340; object 4's
  to 975, 575) the volume fell from 64 at tick 2973 to 5 at 3029 and
  the sound stopped at 3033 (L − 1340), and object 4's fell from 123 at
  tick 1562 to 0 by 1898. Muted (`sq-ec3`, as `sq-rr2`) the race is
  silent from 95 to 105 s, 118 to 128 s and so on; it sounds from 110
  to 117.4 s and from 129 to 149.6 s, the times object 2 and object 4
  play, so they are heard. The sound at 105-109 s is a placed sound
  (`s6-ec1`, the same run with `-watch` at `0x417080` and `0x4182b0`
  and `-voices`): slot 19, the `.frd`'s one-shots (kind 1,
  `0x41d210`), playing the track bank's sound 5 (the same in `tram08`
  and `tr3d08`), a 1-second sound at 11025 Hz,
  started 15 times from 101.0 to 111.9 s, 0.2 to 1.4 s apart. Its
  volume rose as the car came near (2 at 101.0 s, 22 at 105.2 s, 50
  at 105.9 s, 127 from 106.6 s), and the mixer's sum goes from
  silence to sound at 105.6 s. The only other voice then is slot 18,
  the track's loop, which asks for sound 12 of the 12-slot bank (0-11;
  [frd.md](frd.md)) at volume 1-3 from 98.0 to 100.0 s: nothing plays.
  Slot 21 (object 2) starts at 109.76 s.

**When they come naturally** (November 2026). The cycle runs from the
race's set-up: the tick is ((`0x7d3684` & ~1) + phase) mod L
(`0x410f0d`-`0x410f33`; `0x40f6d0` takes the key as (t / delay) mod
(keys − 1) and the tick as t mod (delay × (keys − 1))), and the step
count is 0 at the set-up and 512 at the green light, so it is the same
in every race. Worked out from the `.col` keys and road points (block
15) and checked against the positions `0x40f6d0` gave in a run
(`r2-rr-scout`, `-watch 0x410f38`, ebp+0x2e/+0x32/+0x36: exact):
- **Redrock Ridge, the −2 row.** Object 1 (41 keys, delay 64, phase
  32) is at tick 670 on steps 638 + 2560k, 2.0 s after the green light
  and every 40 s after (race clock 2.0, 42.0, 82.0 s...), and at 1822
  on steps 1790 + 2560k (20.0, 60.0, 100.0 s...). It is then at
  (278, 50, 280) and (278, 51, 181), above the lap's last road points:
  in reach (275) of points 1375-1462 and 0-1 at 670, 1371-1460 at
  1822 (the lap has 1463). So it is heard by a listener in the last 90
  slices before the line at those moments, or still on the grid 2 s
  after the start. The listener is the camera (`0x570a78`, from
  `0x4229c0`), not the car. A car driven by the computer
  (`r2-rr-scout`) is at slice 3 at step 638, 282 away, and at slices
  297 and 121 at steps 3198 and 1790 (552 and 965 away), which is why `sq-rr`
  never heard it.
  - On the grid (`r2-rr-grid`, the player not driving): object 1 came
    into reach at tick 656 (volume 1-4, slot 21) and at tick 670, step
    638, 50.84 s, the −2 row started sound 8 on slot 19 (`0x411286`) at
    volume 4.
  - Placed (`r2-rr-place`: the car's position, a still velocity, +0x1C
    and +0x4 poked to road point 1419 every 100 ms of steps 3150-3260
    and 4300-4410): sound 8 started on slot 19 at tick 670 (step 3198,
    90.84 s, volume 121) and at tick 1822 (step 4350, 108.84 s, volume
    120), with sounds 7 and 6 on slots 20 and 21 (up to 124). The
    camera was at (280, 8.6, 232) both times. In `-voices` the mixer's
    front L+R energy doubles from 91.1 s to 92.6 s and from 109.1 s,
    about the 2.34 s of sound 8 (a one-shot, `tram01` slot 8).
- **Empire City, the fade-outs** (phase 0). Object 2's fade (ticks
  4195-4255) comes at steps 4195-4255 + 4375k, race clock 57.5-58.5 s,
  with the object near road point 615 (85-98 away; in reach 452-656);
  object 4's (2335-2575) at steps 2335-2575 + 2575k, 28.5-32.2 s and
  68.7-72.4 s, near point 733-742 (in reach 642-770). Placed the same
  way (`r2-ec-place`, point 738 for steps 2290-2600, 615 for
  4150-4300): object 4 on slot 21 held 102-104 to tick 2336, fell
  evenly (100 at 2342, 73 at 2408, 26 at 2516) to 1 at 2572 and stopped
  at L; object 2 went from 112 at tick 4194 by 7-8 a step to 1 at 4254
  and stopped at 4256 (L − 120; `0x4113de`). Each time the mixer's call
  count fell by about a quarter when the sound stopped (95 to 66 at
  81.1 s, 93 to 71 at 107.3 s, in `-voices`).

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
- **The grid.** Revving before the start (0:17.2-0:21.7) the partial
  holds at 328-331 Hz, where the cap puts it (above, The cap holds in
  play).

**Wall hits and skids.** Not settled. The `Gen.bnk` hits (29-37) and
skids (41-47) give no clear match in the races: most peaks are in the
menus, and the few in races don't hold over the sound's whole length.
The one a frame bears out is `Country_Woods_Day` 2:39.4, the car
sliding into a snow bank: slot 30 at −280 cents (correlation 0.46 over
0.4 s), where a code-5 bank should give slot 29
([col.md](col.md)). One match isn't enough to say which.

**The moving objects.** None heard, though they play (runs above, The
moving objects' sounds). Sounds 6, 7 and 8 (`Redrock_Ridge`,
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
- **In nfs-host** (`c3-radio`, Hometown, a racer's pursuit): the
  radio's lines start at `0x4c85f4` from `off00A`-`C`, `disp00`, the
  racers' `IP..spch` banks and clicks of `siren.bnk` slots 0x20-0x28;
  `off00C` slot 17 and `disp00` slot 16 are heard where the log puts
  them. At the player's ticket (`c3-spch`) `copspch` slot 18 plays,
  with the radio muted. Which slot is said when is in
  [physics.md](physics.md) (The radio), and what each says in What
  the radio lines say, below.

## What the radio lines say

Worked out in November 2026 (`c5-speech`): the English `off00A`-`C`,
`off02A`, `disp00`, `disp02`, `copspch` and one car's `IP..spch` banks
were decoded with `bnk-wav` and run through a local speech-to-text
model (faster-whisper `small.en`, on the CPU), then matched to the
slots the code asks for (`0x40ab20` and the call routines,
[physics.md](physics.md), The radio). The words aren't kept here, only
what each line is for. Slot numbers are hex. A range means the code
picks one at random.

Re-checked in November 2026 (`s6-speech`): the lines that came out
unclear were run again through the larger `medium.en` model, and
compared by ear-proxy:
- **`off02A` 0x3F says 160, not 180.** Its 0x3C-0x3E are the very
  samples of `off00A`'s (the same voice: identical, sample for
  sample), but 0x3F is another take (correlation −0.01, 1.71 s against
  1.72 s). Both models hear 160 in it (`small.en` 0.98 sure of the
  word) and 180 in `off00A`'s. Its spectrum has a fricative (most of
  the energy over 3.5 kHz, 40 ms) at 1.20 s, in the number: the s of
  "sixty"; `off00A`'s 0x3F has none after "he's". So voice A on
  Atlantica's and Aquatica's banks says 160 for the line the code says
  above 80.5 m/s (180 mph).
- The dispatcher's 0x20 is "ten-four" and 0x13 "all units" twice
  (`medium.en`); his 0x16 is "unit" and the number, swallowed.
- **The dispatcher's 0x11 is "all units"** (`s7-speech`). It is one
  sample, the same in `disp00`-`03` and `disp08` (identical files).
  `large-v3` (from Hugging Face, int8 on the CPU) still can't make it
  out unaided: raw, band-passed (250-3,600 Hz), noise-gated, slowed and
  normalised it gives three different guesses and one blank, its
  first word 0.44 sure at best.
  With a short police-radio prompt it hears "all units" every time,
  but unsure of the words (0.01-0.97). Two checks settle it. The
  Spanish dispatcher's 0x11 is heard the same in all five forms (each
  word 0.8 sure or more) as "to all units", in a set that matches the
  English: 0x10 the notice to all units, 0x12 all available units,
  0x13 all units twice. And by spectral distance (DTW over log bands)
  the English 0x11 is nearest to each half of 0x13 (0.14), not to 0x10,
  0x12 or other short lines (0.17-0.34). The code agrees: it is the
  opener before "unit n" when a cop joins a chase (`0x40b330`) or takes
  it up again (`0x40b530`).
- The rest that differ between the models (186 lines compared, of
  `off00A`-`C`) differ in a word that doesn't change what the line is
  for (a dispatch name, a unit number's wording; 0x2C-0x2F give the
  police code for "situation in hand", the suspect caught).

**The officer banks** (`off%02xA`-`C`, entries 11-13). The three
voices say the same things in their own words; the track's banks
differ only in the unit numbers, the dispatch name and the places.

| Slot | What it is | Said by |
|---|---|---|
| 0x10-0x17 | the officer's call sign: his unit number "to" the dispatch (County on Hometown's banks, Central on Atlantica's); slot 0x10 + the cop's index | `0x40aa20`, first in every call unless he spoke last |
| 0x1A-0x1B | the suspect has crashed / wrecked | kind 0x23, `0x40bc30`, once a chase record (+0x54 bit 8, `0x403910`) |
| 0x1D | asks for a wrecker (tow truck) | kind 0x1A, after 0x1E-0x1F half the time |
| 0x1E-0x1F | his car is disabled, he can't go on | kind 0x19, `0x40bba0`, once a cop (+0x711 bit 0x10, `0x403950`) |
| 0x20-0x23 | he has lost the suspect | kind 0xC, `0x40b480` (a human target lost) |
| 0x24-0x27 | he has the suspect again | kind 0xB, `0x40b530`, before the car's line, unless a strip or roadblock line was just said (`0x5524f4`) |
| 0x28-0x2B | asks for backup | kind 0xF, `0x40b720` (the call for help, `0x4043a0`) |
| 0x2C-0x2F | the suspect is done / caught / in custody | kind 0x13 (0x2C-0x2E) or 0x14, arrested (0x2C-0x2F), `0x40b670`, a computer racer ticketed |
| 0x31 | the suspect missed the spike strip | kind 0x1D, `0x40bca0`, the strip's racer near (`0x403df0`) |
| 0x32 | the suspect ran the roadblock | kind 0x1E, `0x40bd00` (`0x403ed0`) |
| 0x33-0x34 | asks for / recommends a roadblock | kind 0x1C, `0x40b860` (`0x403b20`) |
| 0x35-0x36 | the roadblock is in place | kind 0x22, `0x40bac0` (state 10) |
| 0x37-0x38 | asks for / recommends a spike strip | kind 0x1B, `0x40b910` (`0x4039c0`) |
| 0x39-0x3B | the strip is laid; 0x3A and 0x3B add the side, left and right (voice C says only that it's laid) | kinds 0x1F, 0x20, 0x21, `0x40b9c0` |
| 0x3C-0x3F | how fast the suspect is going: over 100, 120, 140 and 180 mph; `off02A` says 160 for the last (below) | `0x40b190` (above 44.7, 53.6, 62.6, 80.5 m/s) |
| 0x40-0x5F | a place: "at", "by" or "on" a landmark | `0x40aeb0` |
| 0x60-0x7F | the same places with "near" or "from" | `0x40aeb0` (place + 0x20) |
| 0x7E, 0x7F | "a quarter mile", "a half mile", said before a "from" place | `0x40aeb0`: 0x7F when the nearest place is over 100 slices off, 0x7E over 50 |

Heard in runs ([physics.md](physics.md), Radio runs; the cars' roofs
poked): a disabled cop said 0x1F then the wrecker 0x1D and a place, a
crash 0x1B and a place, and the places along Hometown came from its
rows as read, by the speaking cop's slice. The other tracks' places,
and 0x7E and 0x7F, are under The places, below (`s7-` runs).

Slots 0x18-0x19, 0x1C and 0x30 are empty, and kind 0x18 (slots
0x0C-0x0D, `0x40b7f9`, which has no caller) has no line in any bank.
The strip's variants fit the lanes (physics.md): an even cop's strip
lies right of the middle and says 0x3B (variant 2, kind 0x21), an odd
one's left, 0x3A (variant 0, kind 0x20), and variant 1 the plain 0x39.

**The places** come from the track's table at `0x560aa0` (one per
track, 0-8): rows of 16 bytes, the first slice, the slice past the
last, a kind and the slot; −1 ends it. `0x4c8550` takes the row the
speaking cop's slice (a little ahead, by its speed and the queue's
length) falls in; out
of all rows `0x4c8470` takes the nearest. In a row the "at" form is
said (kind 2: either form at random). Off the rows, kind 0 says the
"near/from" form, kind 1 the "at" form, or a quarter or half mile and
the "from" form when over 50 or 100 slices off, and kind 2 either. Hometown has 11 rows
(slots 0x40-0x49), Atlantica 15. Tracks 4-7 have tables of their own
but take tracks 0-3's banks (track modulo 4), so a bank holds the
places of both tracks: in `off00` Hometown's table uses 0x40-0x49 and
Country Woods' some of those and 0x4B-0x51 (0x4A and 0x52 are in the
bank but in neither table); in `off02` Atlantica's uses 0x40-0x4F and
Aquatica's goes to 0x58.

The nearest row isn't looked for round the lap: a slice before the
first row takes the first, one past the last row takes the last
(`0x4c8470`). The distance is to the row's near end. So the quarter
and half mile come only in long gaps that end in a kind 1 row:
Rocky Pass and The Summit (tracks 3 and 7, bank `off03`) have kind 1
rows at 0-42 and 591-651 and none between, and Hometown has them
before slice 171 and past 911.

**Heard on other tracks** (`s7-t2`-`t4`, `s7-L3`-`L7`, November 2026:
the racer's pursuit of the Radio runs on tracks 2-5 and 7; physics.md).
Each place said matched the table read above, by the speaking cop's
slice a little ahead:
- **Atlantica** (2, `off02`): 671 → 0x42 (in row 654-697); 745 →
  0x44 (733-756); off the rows, all kind 0, the "near" form: 226 →
  0x63, 436 and 523 → 0x61, 712 → 0x62.
- **Rocky Pass** (3): 804 → 0x43 one time and 0x63 another (row
  781-818, kind 2); 734 → 0x43; 237 and 272 → **0x7F, 0x61** (195 and
  230 slices past row 0-42); 364-416 → **0x7F, 0x64** (175-227 before
  row 591-651).
- **Country Woods** (4, `off00`): 385 → 0x4D; 283 → 0x42 (12 past
  row 264-271, kind 1: "at", under 50); 409-429 → 0x6E (row 400-406,
  kind 0); 654 → 0x50 (45 past row 595-609, kind 1); 1364 → 0x44
  (past the last row, kind 1).
- **Lost Canyons** (5, `off01`): 268-280 → 0x4C, 595-604 → 0x58,
  795 → 0x59, 1412-1417 → 0x5C, each in its row.
- **The Summit** (7, `off03`): **496 → 0x7E, 0x64** (the quarter
  mile: 95 before row 591-651); 163, 272 → 0x7F, 0x61; 326 → 0x7F,
  0x64; 555 → 0x44 (36 off); 74 → 0x41; 1512 → 0x51; 1518 and 1529 →
  0x62 (row 1551-1632, kind 0, nearer than 1463-1480).

Each time the dispatcher's reply repeated the distance and the place
(0x7E or 0x7F, then the "from" slot). On Hometown the look-ahead
wraps past the lap's end: in `s7-x1` cop car 6 at slice 944 looked at
slice 57, 114 before row 171-224 (kind 1), and said 0x7F (151.05 s and
again 187.59 s, voice C, after "joining pursuit"). Both times the
"from" place (0x63, which `off00C` has) wasn't started after it; why
is not known.

**The dispatcher banks** (`disp%02x`, entry 14):

| Slot | What it is | Said by |
|---|---|---|
| 0x10 | "all units be advised" | `0x40aa70(0x10)`: a new chase (`0x40b220`), a strip or roadblock in place when he last spoke over 1,000 sound steps ago |
| 0x11 | "all units" (a short opener; `s7-speech`, above) | `0x40aa70(0x11)`: a cop joins (`0x40b330`), a chase taken again (`0x40b530`) |
| 0x12 | calling the available units | `0x40aa70(0x12)`, the call for help (`0x40b720`) |
| 0x13 | "all units" said twice | no caller found |
| 0x14-0x1B | "unit" and the officer's number; slot 0x14 + the cop's index | `0x40aa90`, and `0x40a970` half the time |
| 0x20 | "ten-four" | `0x40a970` with a form (after the officer's message) |
| 0x21 | acknowledged | not asked for by the code read |
| 0x22 | go ahead | `0x40a970` form 0, half the time |
| 0x24-0x2B | "go ahead" and the unit's number; 0x24 + the index | `0x40a970` form 0, otherwise |
| 0x30-0x33 | the unit is requesting backup / assistance | kind 0x10, the call for help |
| 0x34-0x35 | go ahead with / cleared for a spike strip | kind 0x1B |
| 0x37 | the strip has been laid | kinds 0x1F-0x21 (0x36 says the same, unused) |
| 0x3A-0x3B | go ahead with / cleared for a roadblock | kind 0x1C |
| 0x3C | the roadblock is set up | kind 0x22 |
| 0x40-0x7F | the places, as in the officer banks; the dispatcher repeats the officer's last place (`0x5524f8`), with the quarter or half mile first when the officer gave one (`0x552520`) | `0x40aeb0(0, ...)` |

So a call goes: officer's call sign, the dispatcher's "go ahead
(unit)" (`0x40a970`), the message, a place or speed, a click, then the
dispatcher's reply: his opener (0x10-0x12), "unit" and number, the
message's reply, perhaps a place; or only "ten-four".

**The car's name** (`IP<colour>spch.bnk`, entries 15 on, all three
voices and the dispatcher in one bank): 0x00 the dispatcher's "in
pursuit of a" and the colour and car; 0x10-0x12 voice A (cop index mod
8 0, 3, 6), 0x20-0x22 voice B (2, 5, 7), 0x30-0x32 voice C (1, 4) (by
`0x40a580`): +0 "in pursuit of" the car (kind 8, a new chase), +1
"joining pursuit of" it (kind 9, and kind 0xB, taken again), +2 "I
have it in sight" (kind 10, also kind 9's other form). `IPncspch` names
the car with no colour.

**`copspch`** (entry 8, the officer at the car, not over the radio):

| Slot | What it is | Said by |
|---|---|---|
| 0x00-0x09 | shouts through the loudspeaker: stop, pull over, police, slow down | `0x4c89b0` (one at random, not the last), from `0x4127c0`: `0x401520` with cheat bit 0x10, every 384 steps at a chased target over 8.94 m/s |
| 0x10-0x12 | a warning, let off lightly | the ticket scene (`0x401580` → `0x4c8b30`): 4 or more tickets left 0x10-0x11, 3 left 0x11-0x12 |
| 0x13 | the final warning: next time he's arrested | 2 tickets left |
| 0x20-0x22 | out of the car, you're under arrest | 1 left (the arrest) |

In `c3-radio`'s first call (cop 1, voice C, 71.10 s) that reads: unit
46's call sign (0x11), "in pursuit of" the car (car bank 0x30), by the
covered bridges (0x43, Hometown's row for slices 171-224), over 100
mph (0x3C); then the dispatcher: all units be advised (0x10), unit 46
(0x15), "in pursuit of" the car (0x00), the same place (0x43).

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
