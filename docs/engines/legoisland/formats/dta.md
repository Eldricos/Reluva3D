# LEGO Island: the worlds' animation lists (`*INF.DTA`)

> **Status:** decoded · **Code:** `src/engines/legoisland/formats/dta.cpp` · **Checked on:** the English 1.0 release's 19 files, each read to its end, and each animation found in its world's SI file (November 2026) · **Confidence:** layout confirmed; what the fields mean inferred unless marked

## Overview

`lego\data\` holds a file named after each world, `<world>INF.DTA`
(`ACT1INF.DTA`, `HOSPINF.DTA`...), 19 in all, 6 bytes to 65 KB
([systems/game-files.md](../systems/game-files.md)). Each lists the
scripted animations of its world's SI file: the little scenes the
characters play (Infomaniac's lines in the Information Center, a bird
landing on a palm, the Brickster's escape), and the models and
characters each uses, with where they start.

| File | Animations | In |
|---|---|---|
| `ACT1INF.DTA` | 369 | `ISLE\ISLE.SI` |
| `ACT2INF.DTA` | 83 | `ACT2\ACT2MAIN.SI` |
| `ACT3INF.DTA` | 3 | `ACT3\ACT3.SI` |
| `BLDDINF.DTA`, `BLDHINF.DTA`, `BLDJINF.DTA`, `BLDRINF.DTA` | 6, 8, 7, 6 | `BUILD\DUNECAR.SI`, `COPTER.SI`, `JETSKI.SI`, `RACECAR.SI` |
| `GMAININF.DTA` | 17 | `GARAGE\GARAGE.SI` |
| `HOSPINF.DTA` | 16 | `HOSPITAL\HOSPITAL.SI` |
| `ICUBEINF.DTA` | 1 | `INFOCNTR\INFOSCOR.SI` |
| `IELEVINF.DTA`, `IISLEINF.DTA` | none | |
| `IMAININF.DTA` | 45 | `INFOCNTR\INFOMAIN.SI` |
| `IREGINF.DTA` | 4 | `INFOCNTR\REGBOOK.SI` |
| `OBSTINF.DTA` | 30 | `ISLE\ISLE.SI`, at other ids |
| `PMAININF.DTA` | 2 | `POLICE\POLICE.SI` |
| `RACCINF.DTA` | 61 | `RACE\CARRACE.SI` |
| `RACJINF.DTA`, `TESTINF.DTA` | 3 | `RACE\JETRACE.SI`; the two files are the same, byte for byte |

## Layout

All values little-endian; floats are 32-bit. A name is a byte, its
length, and that many letters, with no NUL.

| Size | Field |
|---|---|
| 4 | 3 in every file: a version, presumably *(guessed)* |
| 2 | animations |
| each | an animation, below |

### An animation

| Size | Field |
|---|---|
| name | 8 letters in all 664: `sjs012in`, `nin001pr`, `wns050p1` |
| 4 | its SI object's id: 500, 501, 502... in order in every file |
| 2 | 0xFFFF; in 322 of the 522 animations of `ACT1`, `ACT2`, `ACT3`, `RACC`, `RACJ` and `TEST`, 1 to 68 |
| 1 | 0 (430) or 1 (234) |
| 1 | 0 (415) or 1 (249) |
| 1 | a mask of 5 bits: 31 (278), one bit (350), or a few others |
| 1 | 0 (158) or 1 (506) |
| 12 | a point: x, y, z |
| 4 | a distance |
| 1 | models |
| each | a model, below |

The animation is the SI object `<name>_RunAnim` in its world's SI file
(a container whose children play together, its 3D animation
`<name>_Anim` among them), at the id given: every one of the 634
animations of 16 files is found so. `OBSTINF.DTA`'s 30 are in
`ISLE.SI` 100 lower, at 400 to 429 (`wns050p1_RunAnim` is object 400).

The point and distance are 0 in 49 animations, nearly all of them in
the rooms (all the hospital's and the police station's). Elsewhere they
say where the animation plays *(inferred)*: in the island's files most
of an animation's models start within the distance of the point, across
the ground (1,051 of `ACT1INF.DTA`'s 1,183, 156 of `OBSTINF.DTA`'s 161),
and for 364 animations they are exactly the ones its `.ani` starts with
([obj.md](obj.md#an-animation-ani)).

Five bits would be one for each of the five characters the player can
be (Pepper, Mama, Papa, Nick and Laura) *(guessed)*; which characters
an animation's models include doesn't follow them.

### A model

| Size | Field |
|---|---|
| name | `infoman`, `2x4red`, `*capdb01`, `flwryel1` |
| 1 | what it is, below |
| 12 | where it starts: x, y, z |
| 12 | which way it faces: x, y, z, of length 1 in nearly all |
| 12 | which way is up: x, y, z |
| 1 | 1 (1,452) or 0 (448) |

| Kind | Models | What its name is |
|---|---|---|
| 1 | 714 | a model in `WORLD.WDB` ([wdb.md](wdb.md)): its name (676), or its name and digits (38, `bird01`) |
| 2 | 867 | a character, in no world's list of models: `infoman`, `mama`, `papa`, `pepper`, `nick`, `laura`, `brickstr`, and extras named in two letters (`nu`, `en`, `rd`, `bd`) |
| 3 | 281 | `*` and a model's or part's name and digits: `*capdb01`, `*bike05`, `*gurney03` |
| 4 | 18 | `*` and a name, as 3: `*actor_ha`, `*pplane`, `*palm` |
| 5 | 20 | a model's or part's name and digits, with no `*`: `flwryel1`, `fenc05`, `capdb` |

## In Reluva

`DtaFile` (`src/engines/legoisland/formats/dta.cpp`) reads a file; its
models' kinds are `DtaModelKind`. The data viewer doesn't show them
yet.

- `reluva-tool legoisland dta-list <file.dta>`: its animations, and
  their models and where they start.
- `reluva-tool legoisland dta-check <game folder>`: every `*INF.DTA`
  read, each one's animations found in the SI files, and how many of
  their models start within their distance of their point.

## Sources and checks

- **Where the facts come from:** the files alone. Their layout was
  worked out by reading the smallest by hand (`ICUBEINF.DTA`, 89 bytes)
  and then reading all 19 with a script, which every file fitted to its
  last byte; each field's values were then tabulated over all 664
  animations and 1,900 models, and the animations' names and ids looked
  for among the SI files' objects (with `reluva-tool legoisland
  si-list`). The models' kinds were compared with `WORLD.WDB`'s names.
- **How it was checked:** `DtaFile` reads all 19 to their ends, and
  `reluva-tool legoisland dta-check` finds every animation in its SI
  file. `legoisland.unit.dta` reads a file built in the test, and fails
  on ones cut short, too long, or of another version.

## Open questions

- What the 16-bit value after the id, the four bytes after it, and the
  model's last byte mean to the game.
- What tells kinds 3 and 4 apart, and 5 from 1.
- Why `OBSTINF.DTA`'s ids are 100 more than its objects', and what
  `TESTINF.DTA` is for.
