# SimCoaster: sound categories (`cat_*.map`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/sound_map.cpp` · **Checked on:** the US release's 17 categories, every sound · **Confidence:** inferred unless marked

## Overview

Beside the sound banks ([sdt.md](sdt.md)) are pairs of `.map` files, one
pair a category of sounds: `cat_ridesBANK.map` and `cat_ridesSFX.map` in
`Data/global/sound/`, for the rides' sounds. The BANK file names the
banks the category draws on; the SFX file lists its effects, each a
number and the sounds it may play. Scripts start an effect by its number
(`EVENT 5 <where> <effect>`, [rse.md](rse.md)): the front end's script
starts 242, `intro-doorclose`, as its camera passes the doors
([front-end.md](../systems/front-end.md#the-sounds)).

The categories: `global/sound/` has `ambient`, `globallobbysfx`, `kids`,
`rides`, `staff` and `ui`; `global/Speech/` has `speech`, the advisor's
1,136 lines; each world has `Music/cat_music`, and Land of Invention,
Polar Zone and Arabian Nights `Sound/cat_ambient` and `Sound/cat_rides`.
Their effects' numbers are one series, 1 to 322, across the global
categories, which share none but a few of `globallobbysfx`'s; a world's
own categories reuse some of them for its own sounds (224 is the global
`barrier_up3`, Land of Invention's an ambient sound) *(inferred: a
world's would stand in for the global ones there)*.

## Layout

All numbers little-endian. Every file starts with 16 bytes: a 0 for
effects or a 1 for banks, then 15 bytes the same in every file of its
kind, the two kinds' differing in one byte (0xB0 for effects, 0xA0 for
banks, the 12th): an id of the format, it seems *(guessed)*.

### The banks (`cat_*BANK.map`)

| Offset | Size | Field |
|---|---|---|
| 0x10 | 8 | Not worked out: 0, 1 or 0x8001 |
| 0x18 | 4 | How many banks |
| 0x1C | 11 each | A record a bank, not worked out: the same bytes in many files, perhaps the tool's memory saved with them *(guessed)* |
| | | Then each bank's name: 4 bytes, its length with its ending 0, then the name, as `Sound\Ride` |

A bank's name leads from the category's folder's parent: `Sound\Ride`
in `global/sound/` is `global/Sound/RideHD.sdt`, `Music\Music` in
`levels/science/Music/` is `levels/science/Music/MusicHD.sdt`: every
sound is found there, of the length the map gives *(confirmed)*.

### The effects (`cat_*SFX.map`)

| Offset | Size | Field |
|---|---|---|
| 0x10 | 12 | Not worked out |
| 0x1C | 4 | How many effects |
| 0x20 | 20 | Not worked out |
| 0x34 | 20 each | The table: an effect a row |
| | | Then each effect's groups, in the table's order |

A row of the table:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | The effect's number |
| 0x04 | 4 | How many groups it has |
| 0x08 | 4 | 0 |
| 0x0C | 4 | Not worked out: 6,000 in most, 1,000 to 6,000 in the rest (milliseconds, perhaps) |
| 0x10 | 4 | Flags, not worked out |

An effect's groups are their headers, 42 bytes each, then each group's
sounds and links in turn. A group's header:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | How many sounds it has |
| 0x04 | 4 | How many links it has |
| 0x0C | 2 | Its volume, percent, lowest and highest: 100 for most, 12 for the lobby's quiet space ambience, 80 for its thunder *(inferred)* |
| 0x14 | 2 | 50 and 50 but in the speech's: not worked out (the pan, perhaps) |
| 0x1E | 2 | Its share of the effect's plays, of 0xFFFF in all its groups' |
| 0x22 | 4 | A float, 100 in most, 0.1 to 170 in others: not worked out |
| 0x26 | 4 | Most often `47 04 6C F0`, sometimes other bytes: perhaps memory saved with it *(guessed)* |

The rest is 0 in most, other numbers in some: not worked out.

A sound, 16 bytes:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | Its entry in the bank, from 1 |
| 0x04 | 2 | The highest roll, 0 to 0xFFFF, that picks it, above the sound before's |
| 0x06 | 2 | 0 |
| 0x08 | 4 | How long it is, in milliseconds: every one within 60 ms of its bank's length *(confirmed)* |
| 0x0C | 4 | Its bank, from 1, in the BANK file's order |

A link, 8 bytes: another group's number, from 1, and a word whose top
byte is 100. The global ambience's space sounds (`globallobbysfx`'s 3)
link each of its four groups to the other three: one plays after
another, it seems *(guessed)*.

The 230 groups of more than one sound all give rolls rising to 0xFFFF
or nearly. Of the 51 effects of more than one group, 49 give shares
adding up to 0xFFFF or nearly; the lobby's space sounds give shares
rising as the rolls do, and the global rides' 93 two shares of 4,172
and 93. So playing an effect picks a group by the shares, then a sound
in it by a roll *(inferred)*.

## In Reluva

`SoundMap` (`src/engines/tpw/formats/sound_map.cpp`) reads a category:
its banks' names, and its effects' groups, volumes, shares and sounds,
not the links. `SoundEffects` (`src/engines/tpw/sound_effects.cpp`) holds
the global categories, picks an effect's sound as above, reads its bank
the first time and plays it on the mixer at the group's volume. The
front end plays its script's sounds so ([front-end.md](../systems/front-end.md#the-sounds)).

`reluva-tool tpw sfx-check <game folder> [cat_*SFX.map]` reads every
category, finds each sound in its bank and sets its length against the
bank's; with a category, it lists its effects and their sounds. The
game-files test runs it (`tpw.gamedata.soundmaps`). The unit tests
(`tpw.unit.formats`) read a category built in the test and play its
sound into a mixer.

## Sources and checks

- **Where the facts come from:** the US release's 17 categories
  (November 2026): the table by its count, the groups by reading them
  through to each file's last byte; the sounds' fields against the
  banks (the lobby's four thunderclaps, `Thunder2` 4,876 ms, the
  bank's 4.89 s); the effects' numbers against the sounds' names (the
  front end's script's 242, 245, 250 are `intro-doorclose`,
  `intro-flyin`, `intro-trapdoor`).
- **How it was checked:** `sfx-check`: all 17 read to their ends; of
  their 3,977 sounds, 3,922 found in their banks, every one within 60 ms
  of its length there; the other 55 are the two music banks' last
  entries, which aren't in the files ([sdt.md](sdt.md#what-doesnt-decode)).

## Open questions

- The fields not worked out: the table's 4th word and flags; the
  groups' other numbers (the pan, a pitch, a delay, a distance?); the
  speech's groups, laid out otherwise.
- What the links do, and how the game chains a group's sounds by them.
- Whether a world's categories stand in for the global ones.
- What the 16 bytes the files start with stand for.
