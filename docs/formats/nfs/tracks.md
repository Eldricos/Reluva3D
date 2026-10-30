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
    32-byte rows at `0x56ef8c`), `0x4129ec` (`< 0xa`, 8-byte rows at
    `0x552864`), `0x414368` (`< 9`: the `tr3d%02x` sound bank, else
    another name).
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

## What isn't known

- What the per-track rows at `0x56ef8c` and `0x552864` and the jump
  table at `0x40ed14` hold.
