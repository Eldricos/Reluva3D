# SimCoaster: the particle library (`.plb`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/plb.cpp` · **Checked on:** the US release's one library, `Data/Particle/Tp2.plb` · **Confidence:** inferred unless marked

## Overview

The game's particle effects (sparkles, smoke, fireworks, music notes,
fumes) are described in one file, `Particle/Tp2.plb`, 35,704 bytes: 105
effects, each a record of parameters with its name. Scripts and settings
start them by their number: a speaker's script plays 36, "Notes"
(`EVENT 2 1 36`); a cleared obstacle's 92, "BigSmokePuff"; the swamp's
settings name 81, "Create3", and 77, "Destroy3"
(`Info.CreateParticleEffect`, `Info.DestroyParticleEffect`).

## Layout

All numbers little-endian.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | How many effects: 105 |
| 0x04 | 4 | A record's size: 320 |
| 0x08 | 320 each | The effects, by number from 0 |

Then 2,096 bytes not worked out, with names in them (`Coll`, `Coll2`,
`UIAttr`, `UIRepel`, `KeyAttr`, `UiBigRepel`, `Tornado`): colliders and
attractors, it seems *(guessed)*.

### An effect

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 × 54 | Its parameters: not worked out, but for word 29 |
| 0x74 | 4 | Word 29: how long a particle lasts, in milliseconds *(inferred)*: the front end's sparkles, 3,000, last about 3 seconds in the recording |
| 0xD8 | 4 × 16 | A particle's colours over its life, first to last: A, R, G, B from the top byte down |
| 0x118 | 40 | Its name, as "FW1explosion" |

Effect 0 is "NULL"; 101 to 104 have no name. Among them: 1 "Sparks",
19 "BeamUp", 36 "Notes", 60 "FW1explosion", 67 "GoldSparkles", 83
"Twinkle", 89 "Upgrade", 92 "BigSmokePuff".

## In Reluva

`ParticleLibrary` (`src/engines/tpw/formats/plb.cpp`) reads the effects'
names, colours, the particles' lifetimes and their other words, for the
front end's effects ([front-end.md](../systems/front-end.md#the-effects)).
The unit tests (`tpw.unit.formats`) read a library built in the test.

## Sources and checks

- **Where the facts come from:** the file; the effects' numbers set
  beside the scripts' `EVENT 2` instructions and the settings that name
  them (the speakers' 36 "Notes", the obstacles' 92 "BigSmokePuff").
- **How it was checked:** the front end's sparkles drawn from it, set
  beside the recording.

## Open questions

- The parameters: how many particles, their speed, spread, size and
  sprite; which words are which.
- What the bytes after the records are.
- Where the particles' pictures are, if they have any: no file in
  `Data` looks like one.
