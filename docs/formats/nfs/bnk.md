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
| 4 | u16 | version: 2 (838 banks) or 4 (212: the car engines, 3D sounds, track ambiences) |
| 6 | u16 | number of slots |
| 8 | u32 | size of the headers' area (slots and PT headers); the audio follows |
| 12 | 4 × n | each slot's offset to its PT header, counted from the slot itself |

A slot of 0 or −1 is empty. A few slots (230 in the retail banks) point
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
| `0x86`, `0x87` | loop start and length, in samples (the engine sounds loop) | |
| `0x88` | the data's offset from the bank's start | |
| `0x8A` | 0 in all | |
| `0x92` | 1 or 2; not bytes per sample (PCM sounds take 2 either way) | |
| `0x06`, `0x07`, `0x0A`-`0x23`, `0x91`, `0x93` | unknown (0x06 is 25 on most sounds) | |

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
