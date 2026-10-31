# Need for Speed III: track numbers

Which `trkNNN` folder is which track, and how the game picks a track's
files. Worked out in October 2026 from the retail `nfs3.exe` (its
disassembly) and `FeData/text/text.eng`.

## The tracks

| Number | Folder | Track | Front-end image | Length (miles) |
|---|---|---|---|---|
| 0 | `trk000` | Hometown | `home` | 3.6 |
| 1 | `trk001` | Redrock Ridge | `redr` | 5.4 |
| 2 | `trk002` | Atlantica | `atla` | 4.9 |
| 3 | `trk003` | Rocky Pass | `rock` | 5.5 |
| 4 | `trk004` | Country Woods | `coun` | 5.2 |
| 5 | `trk005` | Lost Canyons | `lost` | 6.3 |
| 6 | `trk006` | Aquatica | `aqua` | 5.3 |
| 7 | `trk007` | The Summit (text: "Summit") | `summ` | 6.4 |
| 8 | `trk008` | Empire City (the bonus track) | `empi` | 4.6 |

The order is the one Electronic Arts' own lists use, and the alphabetical
order of the `FeData/art/track/*.qfs` files is not it.

## How it was found

- **One variable holds the track number**: `0x6fd4ac`. The same value
  makes every track path and indexes every per-track table:
  - `0x41d8a0` formats `%sTRK%03x\Tr%02x%s` with it (the `trNN?.qfs`
    texture sets); `0x41e307` formats `%sTRK%03x\simhts.dat`;
    `0x4c83a4` formats `%sTrk%03x\tut%02x`. The folder number is
    printed in **hex** (`%03x`), so a 10th track would be `trk00a`.
  - `0x456919` and `0x4a4ae0` take `[0x6fd4ac*4 + 0x559620]`, a table
    of 9 pointers to `home redr atla rock coun lost aqua summ empi`
    (strings at `0x539938`, 8 bytes apart), and format `%s%s.qfs`: the
    track's front-end picture (`FeData/art/track/home.qfs`...).
  - `0x4565f0` takes `[0x6fd4ac*4 + 0x559644]`, 9 pointers to `3.6`,
    `5.4`, `4.9`, `5.5`, `5.2`, `6.3`, `5.3`, `6.4`, `4.6`, and prints
    it with text string 6 (`Miles`): the track info screen's length.
  - `0x460228` takes `[0x6fd4ac*4 + 0x559fb0]`, shape names `homt redr
    atln rokp cwod losc aqua sumt ecit eegg eegg` (11 entries, at
    `0x53a50c`): the Location screen's track picture.
- **The names are text strings 102 + number.** `text.eng` strings
  102-110 are Hometown, Redrock Ridge, Atlantica, Rocky Pass, Country
  Woods, Lost Canyons, Aquatica, Summit, Empire City. The exe's text
  symbol list (`kText...` strings from `0x540bcc`, 1,142 of them, in
  string order) names 102-117 `kTextTrack1` to `kTextTrack16`.
  - `0x44649d` (`lea eax,[ebx+0x66]`, then the text lookup `0x4d1850`)
    turns a saved record's track number (checked 0..8) into its name.
  - The Location screen's track list is 10 text numbers at `0x55636e`,
    retail initial values `102 103 104 105 106 107 108 109 484 0`, with
    10 "selectable" words at `0x556382`: `1 1 1 1 1 1 1 1 0 0`. Slot 8
    shows string 484, `(Bonus)`, and can't be picked.
  - `0x442b60(n)` sets slot n to text `0x66 + n` and makes it
    selectable. It is called with 8 when Empire City is won
    (`0x44e235`, `0x4527af`) and when a saved game has it (`0x44c2c3`,
    flag `0x6fd2ac`). Text 437/439 is "Empire City (and El Niño)
    Awarded"; 383 says the Knockout unlocks it.

## Bonus and hidden tracks

- Only 9 track folders ship, and only 9 entries are in each per-track
  table. **There is no Caverns, Scorpio-7 or Autocross** in this
  install: none of those names is in `nfs3.exe`, in `text.eng` or in
  any file of the install. (They were add-ons, not part of the retail
  data; not checked further.)
- The exe leaves room for more:
  - text 111 `Easter Egg` (`kTextTrack10`), 112-115 `BonusA` to
    `BonusD`, 116 `Drag Strip`, 117 `Oval`;
  - the shape name table's 10th and 11th entry `eegg`;
  - range checks on `0x6fd4ac` up to 10 or 11: `0x40ed4d` (`cmp edx,
    0xa`, an 11-way jump table at `0x40ed14`), `0x40ec7a` (`< 0xb`,
    32-byte rows from `0x56ef80`), `0x4129ec` (`< 0xa`, 8-byte rows at
    `0x552864`), `0x414368` (`< 9`: the `tr3d%02x` sound bank, else
    another name). What they hold is below.
  - The track list's 10th slot is text 0 and not selectable. Nothing
    found sets it.

## Reverse, mirror, night and weather

These are flags, not separate track numbers:

| Flag | Meaning | Evidence |
|---|---|---|
| `0x6fd4c0` | backward | `0x40e007`: `speedsR.bin` if set, else `speedsF.bin` |
| `0x6fd4c4` | mirrored | `trNN1.qfs` instead of `trNN0.qfs` (`0x41dbc0`, [hrz.md](hrz.md)) |
| `0x6fd4c8` | night | `0x478352`: `3TrNNN.hrz` / `NW` |
| `0x6fd4cc` | weather | `0x47835f`: `3TrNNW.hrz` / `NW` |

The four horizon files, the `tr3dNN{,n,w,nw}.bnk` sound banks
(`GameData/Audio/SFX`) and the 8-bit texture sets (`trNN0_8.qfs`,
`trNN1_8.qfs`) follow the same flags.

## Speed limits (`0x40ed40`)

`0x40ed40(slice)` gives the speed limit, in m/s, at a slice of the
current track (the car's slice is its +0x1C). It jumps by the track
number through the 11-way table at `0x40ed14`; each case compares the
slice with one range. Tracks 9 and 10 and any slice outside a range
not listed take the default, 45 mph.

| Track | Limit | In slices |
|---|---|---|
| 0 Hometown | 30 mph | 489-630 |
| | 50 mph | the rest |
| 1 Redrock Ridge | 65 mph | 171-790 |
| | 70 mph | 791-1000 |
| | 55 mph | the rest |
| 2 Atlantica, 5 Lost Canyons, 6 Aquatica | 50 mph | all |
| 3 Rocky Pass | 50 km/h | 1321-1390 |
| | 80 km/h | the rest |
| 4 Country Woods | 20 mph | 296-339 |
| | 50 mph | the rest |
| 7 Summit | 50 km/h | 1550-1630 |
| | 80 km/h | the rest |
| 8 Empire City | 35 mph | 401-1000 |
| | 45 mph | the rest |

The values are exact: 13.411 m/s is 30 mph, 22.222 m/s is 80 km/h,
and so on. Who uses it (its 8 callers):

- traffic drives at the limit + 1 m/s (`0x40c27d`, into the car's
  target speed +0x7B0);
- the computer's cars' code at `0x40929b` puts it in the target speed
  (negated when the track is run backward);
- a cop goes after a car going faster than the limit + 15 mph (6.705
  m/s, `0x403ffd`, against the car's speed +0x7C4).

## Traffic and AI settings (rows from `0x56ef80`)

Rows of 32 bytes, one per track, at `0x56ef80` + track × 0x20, in
`.bss`. So "rows at `0x56ef8c`" is the field +0xC of these. They are
filled for every track at each race's set-up (`0x40e410`, through
`0x40e3c0`), with one set of values for each race type (`0x6fd3b8`,
jump table `0x40e400`). Readers check the track number < 11 and use a
default past it (rows 9 and 10 are never filled).

| Field | Read by | What it is |
|---|---|---|
| +0x0 int | `0x40ef10` | how many traffic cars may be on the road at once: a traffic car comes back only while fewer are out (`0x40c245`) |
| +0x4 int | `0x40eed0` | the traffic's return delay, in 32 Hz ticks, scaled by a random 0-1 (`0x405d7f`); +200 with weather, +400 at night |
| +0x8 int | `0x40eca0` | if set, no oncoming traffic; else about 70 in 256 traffic cars drive the other way (`0x40c5dc`). Past track 10: 1 if the track header's nibble (byte 0x37 >> 4) is 0 |
| +0xC int | `0x40ec70` | 1 or −1: which side the traffic keeps to, with the direction (`0x40c31a`, `0x40cefe`); past track 10: 1 |
| +0x14 float | `0x40ee80` | the AI racers' speed factor (`0x40933f`) |
| +0x18 float | `0x40ee80` | the same, used instead in race types 1 and 2 (Tournament, Knockout), or with the aggressive AI (`0x6fd4f0`) |
| +0x1C float | `0x40ece0` | 30 mph in every row, default 60 mph. Nothing calls `0x40ece0` |
| +0x10 | - | never written |

The values, by race type:

| Race type | +0x0 | +0x4 | +0x14 | +0x18 |
|---|---|---|---|---|
| 0 | Hometown 8, Redrock 6, Atlantica 9, Rocky Pass 5, Country Woods 7, Lost Canyons 6, Aquatica 5, Summit 5, Empire City 12 | 40, 120, 80, 100, 40, 120, 80, 80, 20 | 0.82 or 0.84 | 0.94 or 0.96 |
| 3 (the pursuit) | as type 0 | as type 0 | 0.78-0.82 | 0.83-0.90 |
| 1, 2 | 0 (no traffic) | 0 | 0 | 0.92-0.94; or, with `0x6fd470` = 1, 0.98-1.0 less 0.005 for each of night, weather and mirrored |

+0x8 is 0 and +0xC is 1 in every row. A flag not identified
(`0x55eb2e` bit 0x20) then gives Rocky Pass's row 1, 100, 0.9, 1.0
(`0x40ebcd`). With the `RUSHHOUR` code
(`0x6fd4fc` bit 8, set at `0x452825`) every row gets +0x0 = 15 and
+0x4 = 0 (`0x40e3c3`). The codes are in a table at `0x5594e4`, each
letter + 9; the others that set `0x6fd4fc` bits are `GOFAST` 1,
`TDRIVE` 2 (with a number, the traffic car to drive, `0x6fd500`),
`MONKEY` 4, `BULLHORN` 0x10 and `MADLAND` 0x20.

## Music laps (rows at `0x552864`)

Two tables of 10 pairs of ints, for forward (`0x552864`) and backward
(`0x5528b4`, when `0x6fd4c0`), read at a race's sound set-up
(`0x4129c0`) into `0x571240` (first) and `0x571238` (second). They are
**music zones** ([mus.md](mus.md)): the zone the lap starts in and the
one it ends in.

| Track | Forward | Backward |
|---|---|---|
| 0, 1, 3 | 4, 7 | 4, 5 |
| 4, 5, 7 | 4, 9 | 4, 5 |
| 8 | 4, 7 | none |
| 2, 6 | none | none |
| 9 | 0, 0 | 0, 0 |

"None" is −1. Past 9, both are −1. When the zone changes after the
first 600 steps (`0x4135c6`), going from the second to the first counts
a lap on, and from the first to the second, when driving the wrong way
(+0x998), a lap back (`0x4135d6`-`0x413685`). The count (`0x55285c`)
gives the music's own lap number (`0x552860`, 0 to laps − 1), which
sets the music's control value by the lap (`0x4133ad`). A left-over
debug print says it: `acutal lap=%d false lap=%d actual counter=%d`
(`0x5369e8`, to an empty function `0x42d510`). Hometown's zones 4, 5,
6, 7 ([mus.md](mus.md)) fit its pair 4, 7.

## What isn't known

- Answered in [physics.md](physics.md): the race types `0x6fd3b8` are
  0 Single Race, 1 Tournament, 2 Knockout, 3 Hot Pursuit (so the
  tournament and knockout have no traffic); `0x6fd470` is their level
  (0 Beginner, 1 Expert) and `0x6fd4f0` the `ai` option (0 Normal, 1
  Aggressive).
- Why Atlantica and Aquatica have no music lap pair.
- Not checked at run time.
