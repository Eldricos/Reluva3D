# Need for Speed III: music (`.mus`, `.map`, `.lin`)

Status: **implemented** (`src/engines/nfs/mus.cpp`; the stereo EA-XA
block decoder in `src/common/audio/ea_audio.cpp`):

- all 19 music streams on the CD (`gamedata/audio/pc`) read, and their
  1,729 segments decode;
- all 29 Pathfinder maps read, and their offsets match their stream's
  segments (`reluva-tool nfs-survey`).

## How it was worked out

October 2026, from hex dumps and survey scripts:

- **The streams.** Chunks read off the data:
  - every segment's block count equals its `SCCl`;
  - its blocks' samples add up to its header's count;
  - every EA-XA block's size is 20 + 30 × groups bytes, with 0 or 2
    bytes of padding.
- **Stereo EA-XA.** Its details were settled by decoding:
  - with the history read as (previous, before previous) and the shift
    meaning what it does in banks, blocks join seamlessly (the jump at a
    join is the size of an ordinary step) and nothing clips;
  - every other reading jumps by a hundred times that at each join;
  - the first seconds' spectrogram is music: held notes, drum hits, a
    new section with guitar bends.
- **The maps.**
  - The table at the end equals the segments' offsets in the `.mus`.
  - The record sizes divide the rest exactly.
  - Every section's routes cover 0 to 100 without gaps and lead to
    sections that exist (1,698 sections with routes in the retail maps).

## Streams (`.mus`)

A stream is segments one after another. Each segment is four kinds of
chunk, each a 4-character tag and a u32 size (little-endian, the 8 bytes
included):

| Chunk | Contents |
|---|---|
| `SCHl` | a PT header ([bnk.md](bnk.md)): channels 2, compression 7 (EA-XA) or none (16-bit PCM), the number of samples |
| `SCCl` | u32: the number of data blocks |
| `SCDl` | a data block (below) |
| `SCEl` | the segment's end |

Data blocks:

- **EA-XA:**
  - u32 samples in the block;
  - for each channel, s16 previous and s16 before-previous sample;
  - per 28 samples, 30 bytes:
    - a filter byte (left in the high nibble);
    - a shift byte (likewise);
    - 28 bytes, each holding the left channel's residual in the high
      nibble and the right's in the low.

  Decoding is as for mono EA-XA ([bnk.md](bnk.md)), each channel with its
  own history.
- **PCM:** u32 samples, then interleaved 16-bit stereo.

Only the four-segment showcase tracks (`show*.mus`) are PCM.

## Pathfinder maps (`.map`, `.lin`)

A map says which segment follows which. The executable's strings call it
the "Pathfinder map".

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | `PFDx` |
| 4 | u8 | 0 in all (a version?) |
| 5 | u8 | 0 in most; 148-190 in six; unknown |
| 6 | u8 | number of sections, one per segment |
| 7 | u8 | 16: the size of an event record |
| 8 | u32 **big-endian** | number of events |
| 12 | 28 × sections | sections |
| | 16 × events | events |
| | 4 × sections | each section's segment, as an offset in the `.mus`, **big-endian** |

Sections, 28 bytes:

| Offset | Size | Meaning |
|---|---|---|
| 0 | u8 | unknown (0 up to about 20) |
| 1 | u8 | number of routes, at most 8 |
| 2 | 2 | `FF FF` in all |
| 4 | 3 × routes | each route: low, high, next section |

The next section is the one whose route's range holds a control value
from 0 to 100. A track's interactive map has mostly three routes per
section (Hometown's rock track: values 0-30 continue, 31-74 and 75-100
jump to more intense parts), so the value is presumably the race's
intensity. What the game feeds it is still to be found. Sections with no
route (31 of them) end the music, or leave the choice to the game.

The `.lin` maps have one route per section: a fixed order, perhaps for
when the music doesn't follow the race.

Events, 16 bytes: four values, then six section numbers in bytes 4-9
(Hometown: 1, 23, 47, 69, 109, 129), with zeros after in most. They look
like the places the game can jump in at; their use is to be found.

## Tools

- `reluva-tool mus-info <file.mus> [file.map | file.lin]` lists the
  segments, with their routes when given a map, and checks the map's
  offsets.
- `reluva-tool mus-wav <file.mus> <segment | all> <dir>` writes segments
  as WAV files.
