# Need for Speed III: music (`.mus`, `.map`, `.lin`)

> **Status:** decoded · **Code:** `src/engines/nfs/formats/mus.cpp` · **Checked on:** every stream and map · **Confidence:** confirmed unless marked

Implemented (`src/engines/nfs/formats/mus.cpp`; the stereo EA-XA
block decoder in `src/audio/ea_audio.cpp`):

- all 19 music streams on the CD (`gamedata/audio/pc`) read, and their
  1,729 segments decode;
- all 29 Pathfinder maps read, and their offsets match their stream's
  segments (`reluva-tool nfs survey`).

## Sources and checks

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
    ([frd.md](frd.md)). The game reads it at the car's own slice
    (car +0x1C) and sends it when it changes. `0x4211c0` works out a
    look-ahead from the car's velocity ((|x| + |z|) × 65536, +0xA4 and +0xAC) and then
    replaces it with 0 (`0x421227`); the ±16 slices left after it
    (`0x42122b`, `0x421232`, the sign by the wrong-way flag +0x998)
    are reached by no jump (November 2026, read again; before then
    this page said 16 ahead). In the runs of
    [tracks.md](tracks.md) (Music laps) each zone change came with
    +0x1C on the zone's first slice (driven backward, on the slice
    just below it: the lookup and the slice numbers are the same both
    ways; the 186 seen for 191 on Hometown and Country Woods backward
    was the car put 5 slices on by the computer driver's progress
    check, `0x409b70` → `0x429100`, at that moment, because the test's
    placing left the car owing a lap so its race distance stayed 0;
    every backward run's car was moved at the same two steps: tracks.md,
    Music laps, November 2026). Hometown's laps run zones 4, 5, 6, 7;
    the other tracks use 4-9 in four to six runs a lap.
  - **1, a pursuit starting** ("Start Cop Music"), sent as urgent.
    When the pursuit ends ("Back to Normal") the game sends the zone
    again.
  - **0, at once** (`SNDpatheventinterrupt`): when the car's reset
    timer (car +0x8A0) passes 600 ticks (`0x413553`), and from
    `0x418c60`. That one is called when a cop's stop begins
    (`0x401e40` → `0x401580` → `0x412880` → `0x4c8b30`) and after a
    hit's sound louder than 125 that isn't a skid (`0x411b06`). The
    zone follows as an ordinary event.
  - **2 and 3 are never sent by the game's own code.** The only calls
    to `SNDpathevent` and `SNDpatheventinterrupt` are at `0x41356e`,
    `0x41358e`, `0x4136c6`, `0x418a37`, `0x418aac`, `0x418b2e`,
    `0x418c1c`, `0x418c93` and `0x418cb3`. Each sends the constant 0 or
    1, or the current zone (`0x552834`, 4 at a race's start, `0x412ab9`;
    then the FRD zone, `0x4211c0`). So 2 or 3 would come only from a
    track whose zone byte is 2 or 3, and the retail tracks have none.
- **The control value** (`SNDpathcontrol`), set when it changes:
  - in a pursuit: 0 at the start and at the lower level, 100 at the
    higher ("Low Cop Music", "Hi Cop Music");
  - else, by the lap (`0x4133ad`; laps counted from 0, of n): 100 on
    the last lap (or past lap 5 of an 8-lap race); 0 on lap 0 of a race
    of other than two laps, and on lap 1 of an 8-lap race; else 50;
  - then the number of computer racers, `0x6fd520` (`0x413403`;
    [physics.md](../systems/physics.md)), changes it:
    - 0: no change;
    - 1: tables by lap (0-7, `0x412d14` and `0x412d34`), one for the car
      in the lead and one for the rest:

      | Lap | Leading | Not leading |
      |---|---|---|
      | 0 | 50 in a 2-lap race, else 0 | 0 |
      | 1 | 100 in a 2-lap race, else 50 | 50 in a 2-lap race, else 0 |
      | 2 | 100 in a 4-lap race, else 50 | 50 in a 4-lap race, else 0 |
      | 3 | 100 in a 4-lap race, else 50 | 100 in a 4-lap race, else 0 |
      | 4-6 | 100 | 50 |
      | 7 | 100 | 100 |

    - 2 or more: +100 in first place, +50 in second or third, at most
      100.

    The place is the driver record's +0x2BC (`0x412d85`), 1 for the
    leader: the race order, numbered from 1 after sorting the cars
    (`0x4553f2`). So with one computer racer the music goes by laps,
    and with two or more by the player's place;
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

**Checked in runs** (November 2026). Logged with `-watch` at
`SNDpathevent`, `SNDpatheventinterrupt`, `SNDpathcontrol` and
`0x4e6c87` (a section queued; the section is the word at `0x5645c4`),
each with the music's lap (`0x552860`), the car's lap (car +0x204),
its place (+0x2BC) and speed (+0x4F4), by `-watchread`. A section is
queued as the one before it starts, so it plays a section later.
- **Event 0** (`sn-h1`, Hometown, `hometech`): the reset timer poked
  past 600 just before it is read (`-poke at=0x41354a#700`). At 62.75 s
  the game sent interrupt 0 and then zone 4 as an event. From section
  23 (part 2) the music went at once to section 115, `events[2][0]`
  (part 11, 3 s), and after it to 16, part 11's event 4. So event 0
  jumps to its own section and the zone takes the music back.
- **The routes** (`sn-m1`, Summit by day, 4 laps, 7 computer racers,
  the player's car driven by the computer, 800 s): 313 sections. 301
  follow a route, each the route whose range holds the control value
  then; the other 12 are the zones' events (5, 8, 9, 4 each lap) and
  one at the start.
- **The control value by lap** (`sn-m1`; the race ended in its third
  lap): on lap 0 of the 4-lap race it was 0 the whole lap (no call was
  needed), in 6th to 8th place. On laps 1 and 2 it was 50, falling to 0
  each time the speed went under 25 m/s and back at 25. The music's lap
  went up 33 s before the line, at Summit's zones 9 to 4 (291.0 s; the
  car's lap at 324.4 s), as [tracks.md](tracks.md) says.
- **The 2-lap rule** (`sn-h1`, a 2-lap race): 50 on lap 0, in 8th
  place.
- **Leading** (`sn-h2`, 4 laps, the car's lap poked to 1 and then 3):
  the poke put the car first in the race order (place 1), and the value
  went to 100 on lap 1 (50 + 100, at most 100) and 100 on lap 3.

- **2nd and 3rd place** (`sq-pl`, Hometown, 4 laps, 7 computer
  racers; the place the music reads poked at `0x412d85`): on lap 0
  (0) the value went to 50 in 2nd place (53.1 s) and stayed 50 in 3rd
  (127.7 s, after a slow corner). When the music's lap went to 1 at
  160.7 s it went to 100 in 3rd (50 + 50), and to 50 when the place was
  made 4th (160.9 s). So 2nd and 3rd add 50 and 4th adds nothing.
- **8 laps** (`sq-8`, Hometown, 7 racers, 5th place held, the car's
  lap poked to 1, 2, 5, 6, 7): 0 on laps 0 and 1 (no call was needed),
  50 from lap 2 (80.0 s), 50 on lap 5, 100 from lap 6 (110.0 s).
- **One racer** (`oppnumber` One, `0x6fd4d0` = 1 poked at
  `0x474ca0`, so `0x6fd520` = 1; Hometown; the place switched between 1
  and 2 within each lap). The values match the tables above:
  - `sq-1b`, 2 laps: lap 0 50 leading, 0 not; lap 1 100 and 50.
  - `sq-1a`, 4 laps: lap 0 0 both; lap 1 50 and 0; lap 2 100 and 50;
    lap 3 100 both.
  - `sq-1c`, 8 laps: lap 0 0 both; laps 1-3 50 and 0; laps 4 and 5 100
    and 50; lap 7 100 both. Lap 6 not leading wasn't seen there (the
    music's lap went to 7 at the zones before the place changed).
  - `s6-l6`, 8 laps, the place held at 2 from the start
    (`-pokeevery at=0x412d85:0x5e13dc:2`) and the car's lap poked to 4,
    6 and 7 (55, 65, 78 s). The lap the tables take is the greater of
    the music's lap and the car's (`0x4133ad`-`0x4133c3`), so the poke
    counts at once. Lap 4 set 50; at lap 6 the value stayed 50 (no call
    needed; the word at `0x5645c6`, which follows the calls, 0 before
    the first and 50 after it, still read 50 at 65.0 s); lap 7 set 100.
    So not leading, lap 6 is 50, as laps 4 and 5: the table sends laps
    4-6 to one case (`0x4134b2`, `0x412d34`).
- **The sections** follow the routes for the value in all these runs
  (`hometech`: 60 of 63 in `sq-8`, 81 of 86 in `sq-1c`; `homerock`: 36
  of 41 in `sq-pl`); the others are the zones' events.

### Which stream

`0x410030` (copying the stream to `temp.mus`, from `0x477fd0` with the
track) and `0x4107c0` (opening the map and starting the stream) name
the file as the track's prefix (`0x5525a8`) and `rock` or `tech`.
The pick is a setting: `0x6fbbe4`, byte 0xA4 of
`FeData/config/config.dat` (`tunenames` in the option table at
`0x556bf0`; its texts 463-466 and 162-172 of `text.eng`):

| `0x6fbbe4` | Text | Stream |
|---|---|---|
| 0 | Track default (Rock) | `rock` |
| 1 | Track default (Techno) | `tech` |
| 2 | Track default (Rock or Techno), the default (`0x472600`) | `tech` when `0x552854` is 1 |
| 3-12 | a tune by name: "Little Sweaty Sow" ... "Warped" | the prefix by pairs (`0x40ffd0`: 3-4 `home`, 5-6 `lost`, 7-8 `atla`, 9-10 `alpi`, 11-12 `empr`), even `tech`, odd `rock`; 11 ("Flimsy") is tech 51 times in 100 (`0x410163`) |
| 13 | Random | `0x40ffd0`'s random 0-4 as the prefix's index: `home`, `lost`, `atla`, `alpi`, or `home` again (entry 4 is Country Woods'); `rock` |

`0x552854` flips at each race's start (`0x4a36c6`, in `0x4a3690`,
before the race loads) and is 0 when the game starts. So by default
the first race plays tech, the next rock, and so on. The command-line flags `-e3demo`, `-msdemo` and `-demoaudio`
(`0x55eb2c` bits 20, 21 and 24, set by `0x4a5080`) force rock. With a
tune chosen (3-13) the race plays its `.lin` order: `0x4107c0` takes
`.lin` when its third argument, `0x6fbbe4` > 2, is set (`0x4a37c1`).
Tech plays quieter: when `0x6fbbe4` is 1 or `0x552854` is 1 the race
music's volume (`0x6fbbf0`) is lowered by `0x5525d0`[track]: 15 for
Hometown, Atlantica, Country Woods and Aquatica, 6 for Redrock Ridge
and Lost Canyons, 0 for the rest.

Checked in runs: with byte 0xA4 at 0 `sq-pl` (Hometown) played
`homerock` (its sections follow `homerock`'s routes); at 2, the first
race of `sn-m1`, `sn-h1`, `sq-8` and `sq-1a` played tech (`alpitech`,
`hometech`; at `0x4108a6` esi is `0x5368e0`, `tech`). So `sn-m1`'s
`alpitech` against the footage's `Summit_Day` `alpirock` is the
alternation (or the setting), not day or night.

Two races in one session (`s6-t6`, `s6-t11`, November 2026; byte
0xA4 at 2, Hometown, `-watch` at `0x4a36c6` and at `0x4108a6`, where
esi names the stream): the first race flipped `0x552854` at 37.4 s
and played `tech` (41.0 s). Restarting it from the pause menu (Esc,
Restart, Yes) doesn't go back through `0x4a3690`: no flip, and the
race opened `tech` again (76.0 s). Quitting to the main menu (Esc,
Quit, Main Menu, Yes) and clicking Race started a new one: the flip
at 96.4 s and `rock` at 100.0 s (esi `0x5368d8`). So the alternation
is by race started from the front end; a restart keeps the stream.

A tune chosen (`e3-t5b`, `e3-t11`, `e3-t13`, November 2026; byte 0xA4
at 5, 11 or 13, a first race on Hometown, `-watch` at `0x4108cb` with
the name built at ebp+0x7E read, and `0x552854`): 5 (the `lost` pair's
odd tune) opened `D:\GameData\Audio\pc\lostrock` with
`.lin` (ebx 1 at `0x4107c0`) and `0x552854` 0; 11 ("Flimsy") opened
`emprtech.lin`, `0x552854` 1 (its 51-in-100 draw came out tech); 13
(Random) `homerock.lin`, 0. So `0x410030` sets `0x552854` from the
tune after the race start's flip, and the stream follows it, not the
alternation; Random is always rock. Hometown's own stream is not
used: the prefix is the tune's.

## Seen in footage (November 2026)

From 18 YouTube recordings of the original game, one race per track by
day and by night (`Hometown_Day` and so on; times are clip times,
m:ss). Each segment's first 2 seconds were cross-correlated with the
clips' sound, and the path through each map followed from section to
section, the next one picked among the routes and events of the one
before. Matches are clear (correlation 0.2-0.7 against 0.06 for the
wrong segments); where the music is quiet under the engines, a few
sections are uncertain. Places and laps come from the HUD.

**Which stream.** Every clip plays its track's own pair, as the table
of prefixes at `0x5525a8` says (track 0-8: `home`, `lost`, `atla`,
`alpi`, `home`, `lost`, `atla`, `alpi`, `empr`):

| Track | Day | Night |
|---|---|---|
| 0 Hometown | `homerock` | `homerock` |
| 1 Redrock Ridge | `losttech` | `lostrock` |
| 2 Atlantica | `atlatech` | `atlatech` |
| 3 Rocky Pass | `alpirock` | `alpitech` |
| 4 Country Woods | `hometech` | `hometech` |
| 5 Lost Canyons | `lostrock` | `lostrock` |
| 6 Aquatica | `atlarock` | `atlatech` |
| 7 Summit | `alpirock` | `alpitech` |
| 8 Empire City | `emprrock` | `emprrock` |

Rock or tech isn't tied to day or night: by default it alternates
from race to race (Which stream, above). No clip plays a `.lin` order,
so none had a tune chosen.

**The start.** `Hometown_Day` and `Hometown_Night` play section 170
during the loading and the grid fly-by (0:12.4, 0:11.4), as header byte
5 says; `Summit_Day` and `Lost_Canyons_Day` start at section 0, and
`Summit_Night` and `Rocky_Pass_Night` play `alpitech`'s 190.

**The zones.** Each lap's events come in the order of the FRD's zones
([tracks.md](tracks.md), Music laps): Hometown, Redrock Ridge and
Rocky Pass 4, 5, 6, 7; Country Woods 4, 5, 8, 9, 7; Lost Canyons 4, 5,
8, 9, 6; Summit 4, 5, 8, 9; Atlantica 4, 5, 6; Empire City 4, 5, 6, 7.
Empire City's one-slice zone 7 (slice 367) leaves no trace: the next
section is zone 5's. The new part starts at a section's end, a little
after the zone's slice: `Hometown_Day`'s lap 1 (1:41.7 long) has zone
5 at 0:47.5, 6 at 1:19.6, 7 at 1:35.6 and 4 at 1:54.8, the line at
2:01.9 (27, 58, 74 and 93% of the lap, against slices 191, 477, 641
and 823 of 960).

**The control value.** The rules above hold where the footage can test
them. The clips are 2-lap races against 7 computer cars, so the value
goes by the place, from 50 on the first lap and 100 on the last:
- **Place.** On lap 1, 4th to 8th place plays the middle routes:
  `Hometown_Day` 0:28-1:26 (5th-8th), `Atlantica_Day` 1:00.9-1:10.1
  (6th, 4th), `Summit_Day` and `Lost_Canyons_Day` all lap (6th-8th).
  Reaching 3rd turns it high at the next section decided:
  `Atlantica_Day` is 3rd from 1:11.5 and high from 1:13.8;
  `Hometown_Day` 3rd from 1:27, high from 1:29.2.
- **The last lap** is high whatever the place: `Summit_Day` in 8th,
  `Lost_Canyons_Day` in 6th.
- **The music's own lap.** `Summit_Day` turns high at 4:26.2, after
  zone 9 to 4 (4:18.6), 20 s before the lap line (4:46.9): Summit's
  pair (4, 9) moves the music on early. `Lost_Canyons_Day` turns high
  at 4:20.2, the first section after its line (4:16.6): its pair never
  matches, so the lap counts at the line. Both as tracks.md says.
- **Speed.** `Summit_Day` crashes and stops at 2:52 (speedometer at 0);
  the next two sections (2:54.1, 2:57.9) take the low routes, and the
  middle ones come back at 3:01.8 when it is moving. Low routes also
  follow two slow corners where the needle drops to about 55 mph
  (2:40-2:42, low from 2:42.5; 6:56-6:57, low at 6:57.7): 25 m/s is 56
  mph, so this fits a speed of 25 in m/s. (The dial is mph on the
  results screen, [physics.md](../systems/physics.md).)
- **After the finish** the music drops to the low routes within a
  section: `Hometown_Day` 3:34.0 (finish 3:31.2), `Atlantica_Day`
  5:11.9 (5:08.5), `Lost_Canyons_Day` 7:49.0, `Summit_Day` 9:04.4.

**Pursuits.** In the Hot Pursuit clips event 1 leads to the pursuit's
part (`homerock` section 84, part 12; `alpitech` 80, part 11), which
then switches between its high and low routes, and the zone's music
comes back after: `Hometown_Night` pursuit at 0:43.4, 2:27.0 and
3:53.4, back to zone 5 at 1:20.1; `Summit_Night` from about 1:50, at
4:35.5, 5:40.8, 6:07.2 and 9:27.2.

**Event 0.** Both candidates fit what sends it: at `Aquatica_Day`
4:31.4 a cop stops the player ("TICKETED" on screen by 4:31.5), and
at `Atlantica_Night` 0:56.9 the player, a cop, is closing on Ram Rod,
which slows by the wall (0:56.5-1:00): the start of a stop, by the look
of it. Neither car is reset there. The races of other lengths aren't in the footage (runs above).

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
