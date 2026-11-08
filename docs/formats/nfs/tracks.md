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

**What the pairs are for** (October 2026). The music's lap is also set
from the car's own lap count (+0x204) when that changes, in the lap
handler `0x412d60` (called from `0x4173c3`; the copy at
`0x4132f4`-`0x4132ff`, when +0x204 differs from the car's last, kept
at `0x5710c8`). So the zone pair only moves the music on early, at the
last zone change before the line, rather than at the line itself; a
track without a pair still gets its music laps, at the line. The zones
at each slice (byte 3 of the FRD's position records, from the start
line on) show why Atlantica and Aquatica have none:

| Track | Zones in order (first slice) | Last change before the line | Pair |
|---|---|---|---|
| 0 Hometown | 4, 5 (191), 6 (477), 7 (641), 4 (823) | 7 to 4 | 4, 7 |
| 1 Redrock Ridge | 4, 5, 6, 7 (1033), 4 (1398) | 7 to 4 | 4, 7 |
| 2 Atlantica | 4, 5 (380), 6 (920), 4 (1287) | 6 to 4 | none |
| 3 Rocky Pass | 4, 5, 6, 7 (972), 4 (1322) | 7 to 4 | 4, 7 |
| 4 Country Woods | 4, 5, 8, 9 (641), 7 (1081), 4 (1263) | 7 to 4 | 4, 9 |
| 5 Lost Canyons | 4, 5, 8, 9 (902), 6 (1085), 4 (1572) | 6 to 4 | 4, 9 |
| 6 Aquatica | 7, 8 (548), 9 (747), 6 (1029) | 6 to 7 | none |
| 7 Summit | 4, 5, 8, 9 (1143), 4 (1549) | 9 to 4 | 4, 9 |
| 8 Empire City | 4, 7 (367, one slice), 5, 6, 7 (945), 4 (1202) | 7 to 4 | 4, 7 |

The pairs are all (4, 7) or (4, 9): the lap ends in zone 7 or 9 and
starts in 4. Aquatica never enters zone 4 at all (it runs 7, 8, 9, 6,
one FRD for both directions), so no pair of that shape can fit it.
Atlantica's lap ends 6 to 4, so (4, 6) would have served it forward,
and backward its 5 to 4 at slice 380 fits the (4, 5) the others have.
**It is just absent data** (November 2026): the tables' only reader is
`0x4129c0` (`0x412a02`-`0x412a22`, a plain index by `0x6fd4ac`, any
track under 10), and the copies `0x571240`/`0x571238` are read only by
the lap handler's zone test (`0x4135e1`-`0x413620`). The zone comes
from `0x4211c0` (byte 3 of the slice's FRD record) with no track test,
and the current zone `0x552834` is written only at `0x412ade` and
`0x4136b5`. The music code has no case for track 2; the exe's
Atlantica-only code (`0x415210`, slices 920-1066) plays track sounds,
not music. So rows 2 hold −1 by the data alone (an earlier zone
layout, perhaps; that part can't be read from the exe).
Country Woods and Lost Canyons have (4, 9), but in the shipped FRDs
their zone 9 is followed by 7 or 6, not 4, so their forward pair never
matches either: of the forward pairs only tracks 0, 1, 3, 7 and 8 ever
count. Either way the music's lap still follows the real one at the
line.

**Checked at run time** (November 2026, `nfs-host`, a 4-lap single
race on each of tracks 0-8, the player's car driven by the computer).
The car was put 54-80 slices before its lap's last zone change (the
position, a still velocity, +0x1C and +0x4 poked every 100 ms of steps
700-760, as the moving objects' runs in [bnk.md](bnk.md)), so it drove
through the change on its own after the first 600 steps. Watched at
`0x4135c6` (a zone change: dl the new zone, dh the old), `0x413645`
(a lap counted, eax +1 or −1) and `0x4132f4` (the car's own lap), with
the pair (`0x571240`, `0x571238`), the count `0x55285c`, the music's
lap `0x552860`, the car's lap +0x204 and +0x1C read:

| Track | Pair read | Last change (slice) | Counted |
|---|---|---|---|
| 0 Hometown (`e3-lap0`) | 4, 7 | 7 to 4 (823), 67.6 s | yes, +1 |
| 1 Redrock Ridge (`e3-lap1`) | 4, 7 | 7 to 4 (1398), 65.6 s | yes; at the line (73.7 s) the music's lap was already 1 |
| 2 Atlantica (`e3-lap2`) | −1, −1 | 6 to 4 (1287), 64.8 s | no; the music's lap became 1 from the car's at the line (71.0 s) |
| 3 Rocky Pass (`e3-lap3`) | 4, 7 | 7 to 4 (1322), 64.7 s | yes |
| 4 Country Woods (`e3-lap4`) | 4, 9 | 7 to 4 (1263), 66.2 s | no |
| 5 Lost Canyons (`e3-lap5`) | 4, 9 | 6 to 4 (1572), 66.2 s | no |
| 6 Aquatica (`e3-lap6b`) | −1, −1 | 6 to 7 at the line (slice 0), 65.5 s | no; the car's lap set it in the same step |
| 7 Summit (`e3-lap7`) | 4, 9 | 9 to 4 (1549), 67.4 s | yes |
| 8 Empire City (`e3-lap8`) | 4, 7 | 7 to 4 (1202), 65.2 s | yes; at the line (69.9 s) already 1 |
| 0 backward (`e3-lap0r`, `0x6fd4c0` = 1) | 4, 5 | 5 to 4 (slice 186 after a reset from 191, below), 71.2 s | yes |
| 1 backward (`e5-lap1r`) | 4, 5 | 5 to 4 (189), 71.1 s | yes |
| 3 backward (`e5-lap3r`) | 4, 5 | 5 to 4 (171), 69.3 s | yes |
| 4 backward (`e5-lap4r`) | 4, 5 | 5 to 4 (186 after a reset from 191), 71.2 s | yes |
| 5 backward (`e5-lap5r`) | 4, 5 | 5 to 4 (197), 70.7 s | yes |
| 7 backward (`e5-lap7r`) | 4, 5 | 5 to 4 (170), 69.2 s | yes |

So the zones count a lap early on Hometown, Redrock Ridge, Rocky Pass,
Summit and Empire City forward, and on every track with a backward pair
backward (0, 1, 3, 4, 5, 7; November 2026, `e5-lap1r` and the rest:
`e5-runs/lapr.sh`, the same set-up with `0x6fd4c0` = 1, the car put on
the road point 70 slices up from zone 5's first slice, `lapplace.py`);
forward on Atlantica, Country Woods, Lost Canyons and Aquatica only the
line does. Backward the change came at the slice just below zone 5's
first (189, 171, 197, 170 for its 190, 172, 198, 171), on Country Woods
and Hometown with +0x1C at 186 for 191. The zone
changed with +0x1C on the new zone's first slice each time, with no
look-ahead ([mus.md](mus.md)). Two runs show the wrong-way branch
too, as a side effect of the poke: on Summit and on every backward run
the jump onto the far slice came with the wrong-way flag (+0x998) set
(the car still facing the grid's way), and the jump's zone change (4 to
9; 4 to 5) matched the pair the other way round, so the count went to
−1 (the music's lap held at 0, `0x41365b`-`0x41366e`) before the real
change put it back to 0.

**Why 186 on Hometown and Country Woods** (November 2026, settled):
the car was put 5 slices on by a reset just then; the zone lookup
had nothing to do with it. From the code, `0x4211c0` reads the zone
at the car's own slice +0x1C the same way in both directions: block
+0x1C >> 3 of the track's blocks (`0x571370`, 0x5C0 bytes each), the
record +0x1C − the block's first slice (+0x584) of its positions
(+0x58C), byte 3 (`0x421246`-`0x421293`). The slices keep the FRD's
numbering when the track is run backward (nothing there tests
`0x6fd4c0`), and the wrap at the table's ends (`0x421251`-`0x421260`)
only serves the look-ahead, which is always 0 (`0x421227`). Both FRDs
have zone 5 from slice 191 (block 23: 4 4 4 4 4 4 4 5; Redrock Ridge's
from 190), and both tracks share these blocks. Watched: `e8-zr4`
(Country Woods backward, `lapr.sh`'s set-up, `-watch 0x4135b6`, every
call of the lookup, with +0x1C and the car's position) shows +0x1C
192, 191, 191, 191 and then 186 at 71.152 s, the car's x, z jumping
from (698.6, −720.0) to (724.8, −706.7) and standing there, and the
same jump 209 to 204 at 65.01 s, still in zone 5. `e8-zq0` (Hometown,
the same, `-watch 0x429100 -watch 0x428f90`) caught the reset:
`0x429100` called from `0x409bbf` at 65.006 s (+0x1C 208) and 71.145
s (+0x1C 191), the zone change following at 71.157. `0x409bbf` is in
`0x409b70`, the computer driver's progress check (the car is driven
by the computer's tasks in these runs): after step 0x480, when the
car's race distance (+0x8) hasn't passed its best (+0x708) for 0x180
steps (6 s), it calls `0x429100`, which puts the car `0x5efdcc` (5)
slices on, −5 when backward (`0x42910d`-`0x4291af`), still. The
saved steps (+0x704, read as the call's edx − 0x180) were 1152 and
then 1544: up to step 0x480 (58.9 s here) the check only records the
distance and the step (`0x409bc4`), and from then on this car's race
distance never passed what it was at step 1152, so the check moved it
at 1544 (65.0 s) and again 384 steps later at 1928 (71.1 s); the
driver task runs every 8 steps, so 1544 is the first check past
1152 + 0x180.

**Why that race distance stood still** (November 2026, settled): it
was 0 throughout, because the placing pokes left the car owing a
lap. +0x8 is worked out each step by `0x42ce10` (from `0x42ce70`):
0 while the laps owed (+0x208) are over 0, else lap (+0x204) × the
track's slices (`0x5dd940`) + the slice, or slices − slice backward.
A backward race starts every car owing one lap (`0x42be2f`), paid
off by `0x42cc20` when the car crosses the line from the grid: with
`0x6fd4c0` set, +0x4 (the slice the step before) 50 or under and
+0x1C over 500 takes 1 off (`0x42cca7`-`0x42ccd8`); with +0x4 50 or
under and +0x1C 500 or under it only copies +0x1C to +0x4
(`0x42ccb6`). The pokes (`lapNr.place`, 51.9-52.8 s) wrote the
same far slice (260 and so on) to both +0x1C and +0x4 while the
player's car was still on the grid side of the line, so `0x42cc20`
saw no crossing (+0x4 equal to +0x1C returns at once, `0x42cc2f`),
the lap stayed owed, and +0x8 stayed 0. The progress check
`0x409b70` (called by the computer driver `0x409c00` at `0x409c58`,
`0x409d51` and `0x409e0a`) keeps the best +0x8 at +0x708 and its step
at +0x704 while +0x8 is over the best, the car's chase record
(+0x628) has cops on it (+0x2C), the step `0x7d3684` is 0x480 or
under, or positional updates (`0x6fd50c`) are on; otherwise, 0x180
steps after the saved step, it saves the step and calls `0x429100`.
With +0x8 and +0x708 both 0, nothing ever passed the best. Forward,
no lap is owed at the start (+0x208 0) and the grid is on the first
slices past the line (the computer cars' first crossing backward was
from slice 0), so the same pokes leave +0x8 = the new slice,
which grows as the car drives on.

Run on every track with a lap set-up (November 2026, `e10-rs`:
`e10-runs/rs.sh`, `lap.sh`'s and `lapr.sh`'s set-ups with the same
`.place` files, 74 s; `-watch` at `0x429100` with `[esp]`, and at
each change of the laps owed or lap in `0x42cc20`, `0x42cc4a`,
`0x42cc80`, `0x42cc8e`, `0x42cccc`, `0x42cce1`, reading +0x1C, +0x4,
+0x8, +0x204, +0x208, +0x704, +0x708 and the step):

| Run | `0x429100` calls (from `0x409bbf`, car `0x5e1120`) | Laps owed |
|---|---|---|
| 0 Hometown backward (`e10-rs0r`) | 65.006 s (+0x1C 208), 71.145 s (191) | 1 throughout; the seven computer cars paid theirs at 50.7-55.0 s (`0x42cccc`, +0x4 0 and +0x1C the track's last slice, 958) |
| 1 Redrock Ridge backward (`e10-rs1r`) | 65.014 s (207), 71.139 s (189) | 1 throughout |
| 3 Rocky Pass backward (`e10-rs3r`) | 65.026 s (186), 71.148 s (160) | 1 throughout |
| 4 Country Woods backward (`e10-rs4r`) | 65.011 s (209), 71.128 s (191) | 1 throughout |
| 5 Lost Canyons backward (`e10-rs5r`) | 65.020 s (214), 71.156 s (195) | 1 throughout |
| 7 Summit backward (`e10-rs7r`) | 64.991 s (185), 71.110 s (159) | 1 throughout |
| 0-8 forward (`e10-rs0f`-`e10-rs8f`) | none | 0; the player's lap counted at the line (`0x42cc8e`) on Redrock Ridge (73.7 s), Atlantica (71.0 s), Aquatica (65.5 s) and Empire City (69.8 s), +0x8 then the last slice before it (1462, 1324, 1433, 1229) |

At each call +0x8, +0x204 and +0x708 read 0, +0x208 1, and the saved
step 1544 then 1928 (`0x608`, `0x790`). So every backward run's car
was moved 5 slices at the same two steps, not only Hometown's and
Country Woods'. On the other tracks the first move came well before
zone 5 and the second after the car had driven through the change
by itself (Redrock Ridge's at slice 189, Rocky Pass's at 171 at
69.3 s, Lost Canyons' at 197 at 70.7 s, Summit's at 170 at 69.2 s;
the second moves were at 189, 160, 195 and 159), so neither crossed a boundary; on Hometown and Country
Woods the second came at slice 191, one before the change. It is a
side effect of the test's placing: a car that crossed the line from
the grid (as every car does in a real race) pays the owed lap, and a
car driving through gets the change at 190, as on the other tracks.
A placing that wants a moving race distance backward has to poke
+0x208 to 0 as well (a slice of 500 or under, such as these 260s,
never pays the lap even when +0x4 is left on the grid's side).

## What isn't known

- Answered in [physics.md](physics.md): the race types `0x6fd3b8` are
  0 Single Race, 1 Tournament, 2 Knockout, 3 Hot Pursuit (so the
  tournament and knockout have no traffic); `0x6fd470` is their level
  (0 Beginner, 1 Expert) and `0x6fd4f0` the `ai` option (0 Normal, 1
  Aggressive).
- Answered above (Music laps): Aquatica never enters zone 4, so no
  (4, x) pair fits it; Atlantica would fit (4, 6) forward and (4, 5)
  backward: no code treats track 2 apart, its −1s are only data.
  Without a pair the music's lap still follows the real lap, at the
  line. In runs the pairs count on tracks 0, 1, 3, 7 and 8 forward
  and backward on all six tracks with the pair (4, 5) (0, 1, 3, 4, 5,
  7), each with zone 5 second from the line, so driven backward its
  last change is 5 to 4, as run (November 2026, Music laps).
  Atlantica would count too, but its row is −1, and Empire City's
  second zone is its one-slice 7.
