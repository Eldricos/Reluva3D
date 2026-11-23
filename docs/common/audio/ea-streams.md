# EA's sound headers, streams and EA-XA

## What it is

How Electronic Arts' games of the late 1990s and after describe and keep
their sound: a "PT" header of tagged values giving a sound's channels,
rate, length and coding; streams of chunks (`SCHl`, `SCCl`, `SCDl`,
`SCEl`) holding a header and blocks of audio, as Need for Speed III's
music and the sound of EA's movies are; and EA-XA, their ADPCM coding.
The code is `src/audio/ea_stream.cpp` (`parseSoundHeader`,
`EaSoundStream`) and `src/audio/ea_audio.cpp` (the codecs).

## What Reluva supports

- PT headers: the audio's tags read into fields (defaults for those not
  given), every tag kept by number for a game's own code.
- Streams: their segments found, other chunks between them passed over;
  a segment decoded when its sound is stereo EA-XA or 16-bit PCM.
- EA-XA in mono frames (as in Need for Speed III's banks) and in stereo
  stream blocks. EA's MicroTalk speech coding is decoded too
  (`src/audio/ea_audio.cpp`); Need for Speed III's page describes it
  ([bnk.md](../../engines/nfs/games/nfs3/formats/bnk.md#microtalk)).

## The format

### PT headers

`PT\0\0`, then tags:

- a tag byte, a length byte, then the value in that many bytes,
  **big-endian**;
- `0xFC`, `0xFD` and `0xFE` open sections and have no value;
- `0xFF` ends the header.

| Tag | Meaning | Default |
|---|---|---|
| `0x81` | Bits per sample (8 or 16) | 16 |
| `0x82` | Channels | 1 |
| `0x83` | Compression: 7 EA-XA, 9 MicroTalk; absent, 16-bit PCM | PCM |
| `0x84` | Sample rate | 22050 |
| `0x85` | Number of samples | |
| `0x86`, `0x87` | A loop: its first sample and its end | |
| `0x88` | In a bank, the data's offset from the bank's start | |
| `0x8A` | 0 in the files seen | |

Tags below `0x80` and the rest above it are a game's own: Need for Speed
III's sound code reads its "patch" from those below `0x26`
([bnk.md](../../engines/nfs/games/nfs3/formats/bnk.md#pt-headers)).

### Streams

A stream is segments one after another. Each segment is four kinds of
chunk, each a 4-character tag and a u32 size (little-endian, the 8 bytes
included):

| Chunk | Contents |
|---|---|
| `SCHl` | A PT header |
| `SCCl` | u32: the number of data blocks |
| `SCDl` | A data block (below) |
| `SCEl` | The segment's end |

A movie's stream has its pictures' chunks between these (a TGQ movie's
`pIQT`), which belong to the video.

Data blocks:

- **EA-XA, stereo:**
  - u32 samples in the block;
  - for each channel, s16 previous and s16 before-previous sample;
  - per 28 samples, 30 bytes:
    - a filter byte (left in the high nibble);
    - a shift byte (likewise);
    - 28 bytes, each holding the left channel's residual in the high
      nibble and the right's in the low.
- **PCM:** u32 samples, then interleaved 16-bit stereo.

### EA-XA ADPCM

- Mono, in banks: 28 samples in each 15-byte frame. The frame's first
  byte holds the filter (high nibble, 0-3) and the shift (low nibble,
  0-12); 14 bytes of 4-bit residuals follow, high nibble first.
- Stereo, in streams: as above, each channel with its own history.

Decoding a residual:

    sample = residual × 2^(12 − shift) + (c1 × previous + c2 × before_previous + 128) >> 8

clamped to 16 bits. The filters (c1, c2) are CD-XA's, in 1/256ths:
(0, 0), (240, 0), (460, −208), (392, −220).

Filter numbers above 3 appear only in final, partly used frames (fewer
than 0.1% of frames); Reluva uses no prediction for them.

## Sources and checks

- **Where the facts come from:** worked out on Need for Speed III's
  banks and music (October 2026), from hex dumps and survey scripts:
  every tag's values over all its banks; every segment's block count
  equal to its `SCCl`, its blocks' samples adding up to its header's
  count. EA-XA from CD-XA ADPCM, which it derives from: CD-XA's filters
  fit, the shift's range is CD-XA's, high nibble first gives 20 times
  less clipping than the other order. Stereo EA-XA's history order and
  shift by decoding: read so, blocks join seamlessly and nothing clips;
  every other reading jumps at each join. Need for Speed III's
  [bnk.md](../../engines/nfs/games/nfs3/formats/bnk.md) and
  [mus.md](../../engines/nfs/games/nfs3/formats/mus.md) have the details.
  SimCoaster's movies' stream (`bf.tgq`) has the same chunks, with
  `pIQT` between them.
- **How it was checked:** all of Need for Speed III's 976 banks (11,775
  sounds) and 19 music streams (1,729 segments) read and decoded. The
  unit tests (`common.unit.ea_audio`, `tests/common/ea_audio.cpp`) read
  PT headers and a stream built in the test, and decode EA-XA frames
  written by hand.
- **Confidence:** confirmed unless marked.

## Used by

- **`nfs`**: Need for Speed III's sound banks, music and movies
  ([bnk.md](../../engines/nfs/games/nfs3/formats/bnk.md),
  [mus.md](../../engines/nfs/games/nfs3/formats/mus.md)).
- **`tpw`**: SimCoaster's logo movie's sound
  ([tgq.md](../../engines/tpw/games/simcoaster/formats/tgq.md)).
