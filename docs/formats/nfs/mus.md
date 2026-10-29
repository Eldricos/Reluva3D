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

The PCM streams are six of the eight showcase tracks (`show1`, `3`, `4`, `5`, `7`, `8`) and `srv2.mus`; `show2` and `show6` are EA-XA.

## Pathfinder maps (`.map`, `.lin`)

A map says which segment follows which. The executable's strings call it
the "Pathfinder map".

| Offset | Size | Meaning |
|---|---|---|
| 0 | 4 | `PFDx` |
| 4 | u8 | 0 in all (a version?) |
| 5 | u8 | the section the music starts at |
| 6 | u8 | number of sections, one per segment |
| 7 | u8 | 16: the number of events (a row's length) |
| 8 | u32 **big-endian** | number of rows of events, one per part |
| 12 | 28 × sections | sections |
| | 16 × rows | events |
| | 4 × sections | each section's segment, as an offset in the `.mus`, **big-endian** |

Sections, 28 bytes:

| Offset | Size | Meaning |
|---|---|---|
| 0 | u8 | the part: the row of events used from this section |
| 1 | u8 | number of routes, at most 8 |
| 2 | 2 | `FF FF` in all |
| 4 | 3 × routes | each route: low, high, next section |

The `.lin` maps have one route per section: a fixed order, perhaps for
when the music doesn't follow the race.

Events: a row of 16 section numbers for each part. Event e from a
section of part p leads to section `events[p][e]`.

### How the game plays a map

From `nfs3.exe`'s Pathfinder code (iSND's `SNDpathcontrol` at
`0x4e705c`, `SNDpathevent` `0x4e70fc`, `SNDpatheventinterrupt`
`0x4e7208`, and the choice at a section's end, `0x4e6be3`) and the
game's calls to it (`0x4131xx`-`0x4136c6`, `0x418990`):

- **At a section's end** the next is, in order:
  - the section a pending event leads to, for this section's part; the
    event is then used up;
  - else the first route whose range holds the control value (0-127);
  - else, no route holding it, the same section again.

  If the section reached has no routes and no event is pending, the
  music ends after it.
- **Events** wait for the section's end (`SNDpathevent`). One sent as
  urgent (the pursuit's) stays first: an ordinary one sent meanwhile
  waits behind it. `SNDpatheventinterrupt` jumps at once instead.
- **The events the game sends:**
  - **4-9, the track's zones.** Every track has a music zone at each
    slice: byte 3 of the FRD block's 8-byte position records
    ([frd.md](frd.md)). The game reads it 16 slices ahead of the car
    and sends it when it changes. Hometown's laps run zones 4, 5, 6, 7;
    the other tracks use 4-9 in four to six runs a lap.
  - **1, a pursuit starting** ("Start Cop Music"), sent as urgent.
    When the pursuit ends ("Back to Normal") the game sends the zone
    again.
  - **0, at once** (`SNDpatheventinterrupt`): when the car's reset
    timer passes 600 ticks, and from `0x418c60`. The zone follows as
    an ordinary event.
  - 2 and 3 weren't seen sent.
- **The control value** (`SNDpathcontrol`), set when it changes:
  - in a pursuit: 0 at the start and at the lower level, 100 at the
    higher ("Low Cop Music", "Hi Cop Music");
  - else 100 on the last lap (or past lap 5 of an 8-lap race), 50 on
    the laps between, 0 on the first lap of a race of more than two
    laps; in some race modes (a setting at `0x6fd520`) raised by 100 or
    50 by a value of the race passed in;
  - and 0 whenever the car is slower than 25 (its speed field, × 2,
    under 50): standing still, the music stays calm.

So Hometown's rock map (`homerock.map`): parts 1-11 and 14-22 are the
zones' music, mostly eight sections each, with three routes each for
values 0-28, 29-72 and 73-100; part 12 (sections 76-91) is the
pursuit's, low for 0-50 and high for 51-100; part 13 (92-97) bridges back into a zone after event 0; and
170 and 171, 6.4 seconds each, are the race's intro (the start, header
byte 5) and the section an event leads to where a part has nothing of
its own.

**What the game plays** (October 2026, from the reads `nfs3.exe` makes
of its copy of the stream, `C:\GameData\temp.mus`, in a scripted race on
`trk000` with `hometech`):

- As the race loads, it plays section 189: the map's header byte 5
  (`0xBD`), a 12-second section of its own kind (21), the race's intro.
  The other maps' byte 5 is 0, or another such section: `alpitech`
  190, `atlatech` 185, `emprrock` 148, `homerock` 170, `losttech` 189.
  The `.lin` maps have their own (`hometech.lin` 16).
- After it, at the race's start, section 16, then 17, 18 ... 23 and back
  to 16: the map's routes at low intensity (0-28), with the car standing
  still. So the intensity is low for a car at rest; what raises it was
  not seen (the car didn't move in that race).
- The game reads 16 KB at a time, a section's second half while its
  first plays.

So byte 5 is the start section: the Music page starts there. The car
standing still kept the control value at 0, as the game's rules above
say.

## On the Music page

The segments play as one stream (`audio::SoundQueue`, `common/audio`):
the page keeps the next section decoded and queued behind the one
playing, chosen when it's queued, and each segment's last sample leads
straight into the next one's first, with no gap. The choice is the
game's, above: an event from the keys 0-9 if one is pending (1 a
pursuit, 4-9 the zones; it applies to the next section decided), else
the route for the intensity (Left/Right), else the same section. The
page shows the part, where its events lead, and the event pending.

## Tools

- `reluva-tool mus-info <file.mus> [file.map | file.lin]` lists the
  segments, with their routes when given a map, and checks the map's
  offsets.
- `reluva-tool mus-wav <file.mus> <segment | all> <dir>` writes segments
  as WAV files.
