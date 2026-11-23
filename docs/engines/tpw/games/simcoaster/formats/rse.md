# SimCoaster: ride and shop scripts (`.RSE`)

> **Status:** partial · **Code:** `src/engines/tpw/formats/rse.cpp` · **Checked on:** the US release's 288 `.RSE` files and the five sources shipped with them · **Confidence:** confirmed unless marked

## Overview

Every ride, shop, facility and feature has a script beside its model
(`levels/arabian/rides/wateride/Wateride.RSE`): code for the game's
script machine, which runs the ride, letting guests on and off, playing
its animations, breaking down. There are 288. They are compiled from an
assembly language; five of its sources ship with the game, each beside
its compiled script, in `Data/TestScript/` (`Shop.rss`, `test.rss`,
`Feature.rss`, `TestAnim.rss`, `Null.rss`). They show the language:
directives (`#setstack 5`), declared variables (`variable VAR_LETMEON`),
labels (`.loop`), and one instruction a line, its mnemonic then its
operands (`BRANCH_NZ closed`).

OpenTPW's written pages on the format (opentpw.gu3.me/formats/rsse and
rsse-vm-instructions, March 2022) name the header's fields and the
instructions; everything they say that the files can show was checked
against them, and the five sources confirm the names they use.

## Layout

All numbers little-endian.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 8 | `RSSEQ`, then 0F 01 00 |
| 0x08 | 4 | How many variables |
| 0x0C | 4 | The stack's size: `#setstack` |
| 0x10 | 4 | 50 in all: a time slice, OpenTPW's page says *(inferred)* |
| 0x14 | 4 | The limbo's size: `#setlimbo` |
| 0x18 | 4 | The bounce's size: `#setbounce` |
| 0x1C | 4 | The walk's size: `#setwalk` |
| 0x20 | 16 | `Pad Pad Pad Pad ` |
| 0x30 | 4 | How many words of code |
| 0x34 | 4 each | The code |

Then entries of a length (4 bytes) and that many bytes: first the
script's strings, back to back, each ending in a zero byte; then each
variable's name, by number. The files end there.

The five sources' directives are their scripts' header: `Shop.rss`'s
`#setstack 5` and `#setlimbo 10` are `Shop.RSE`'s 5 and 10.

### The code

Each word is a 16-bit value, then 16 bits of flags:

| Flags | The word |
|---|---|
| 0x8000 | An instruction: the value is its opcode |
| 0x0000 | A number, signed (65,535 is −1: `ADD VAR_COUNT -1`) |
| 0x1000 | A string: the value is its byte offset into the strings |
| 0x2000 | An address: the value is a word of the code, a branch's target |
| 0x4000 | A variable: the value is its number |

An instruction's operands follow it; how many depends only on its
opcode, the same in all 288 scripts (below). A label in the source is
the address of the instruction after it.

### The opcodes

72 of the 106 are used. Where a name's operand count comes from: the
five sources, compiled; its uses in the 288 scripts, every one parsing
to its end; or, for those never used, only OpenTPW's page (`?`: not
given there either). `CRIT_UNLOCK` is missing from OpenTPW's list; the
sources give it, and that it is 2, with 0 `NOP` and 1 `CRIT_LOCK`,
puts the list's names on the right opcodes from `COPY` (3) on.

| Opcode | Name | Operands | Uses | Checked by |
|---|---|---|---|---|
| 0 (0x00) | `NOP` | 0 | 24 | its uses |
| 1 (0x01) | `CRIT_LOCK` | 0 | 145 | the sources |
| 2 (0x02) | `CRIT_UNLOCK` | 0 | 232 | the sources |
| 3 (0x03) | `COPY` | 2 | 1483 | the sources |
| 4 (0x04) | `SETLV` | ? | 0 | — |
| 5 (0x05) | `SUB` | 3 | 60 | the sources |
| 6 (0x06) | `ENDSLICE` | 0 | 398 | the sources |
| 7 (0x07) | `GETTIME` | 1 | 155 | the sources |
| 8 (0x08) | `ADDOBJ` | 4 | 1024 | its uses |
| 9 (0x09) | `ADDOBJ_EXT` | 5 | 0 | — |
| 10 (0x0A) | `KILLOBJ` | 1 | 242 | its uses |
| 11 (0x0B) | `FADEOBJ` | 1 | 149 | its uses |
| 12 (0x0C) | `SETOBJPARAM` | 3 | 20 | its uses |
| 13 (0x0D) | `EVENT` | 3 | 1449 | its uses |
| 14 (0x0E) | `EVENT_EXT` | 4 | 0 | — |
| 15 (0x0F) | `FLUSHANIM` | 0 | 15 | its uses |
| 16 (0x10) | `TRIGANIM` | 3 | 135 | its uses |
| 17 (0x11) | `WAITANIM` | 2 | 595 | the sources |
| 18 (0x12) | `LOOPANIM` | 2 | 271 | its uses |
| 19 (0x13) | `TRIGWAITANIM` | 3 | 37 | its uses |
| 20 (0x14) | `GETANIM` | 1 | 0 | — |
| 21 (0x15) | `TRIGANIMSPEED` | 4 | 0 | — |
| 22 (0x16) | `FLUSHANIM_CH` | 1 | 0 | — |
| 23 (0x17) | `TRIGANIM_CH` | 4 | 18 | its uses |
| 24 (0x18) | `WAITANIM_CH` | 3 | 0 | — |
| 25 (0x19) | `LOOPANIM_CH` | 3 | 0 | — |
| 26 (0x1A) | `TRIGWAITANIM_CH` | 4 | 0 | — |
| 27 (0x1B) | `GETANIM_CH` | 2 | 9 | its uses |
| 28 (0x1C) | `RAND` | 2 | 20 | its uses |
| 29 (0x1D) | `JSR` | 1 | 40 | its uses |
| 30 (0x1E) | `RETURN` | 0 | 16 | its uses |
| 31 (0x1F) | `BRANCH` | 1 | 868 | the sources |
| 32 (0x20) | `BRANCH_Z` | 1 | 877 | the sources |
| 33 (0x21) | `BRANCH_NZ` | 1 | 1013 | the sources |
| 34 (0x22) | `BRANCH_NV` | 1 | 48 | the sources |
| 35 (0x23) | `BRANCH_PV` | 1 | 69 | the sources |
| 36 (0x24) | `DBGMSG` | 1 | 0 | — |
| 37 (0x25) | `NAME` | 1 | 241 | the sources |
| 38 (0x26) | `TEST` | 1 | 1416 | the sources |
| 39 (0x27) | `CMP` | 2 | 150 | its uses |
| 40 (0x28) | `PUSH` | 1 | 0 | — |
| 41 (0x29) | `POP` | 0 | 0 | — |
| 42 (0x2A) | `HUSH` | 1 | 33 | the sources |
| 43 (0x2B) | `HOP` | 1 | 33 | the sources |
| 44 (0x2C) | `WAIT` | 1 | 812 | its uses |
| 45 (0x2D) | `WAITABS` | 1 | 34 | its uses |
| 46 (0x2E) | `WAIT4ANIM` | 0 | 155 | its uses |
| 47 (0x2F) | `ADD` | 2 | 502 | the sources |
| 48 (0x30) | `MULT` | 3 | 1 | its uses |
| 49 (0x31) | `DIV` | 3 | 3 | its uses |
| 50 (0x32) | `MOD` | 3 | 3 | its uses |
| 51 (0x33) | `TURBO` | 1 | 0 | — |
| 52 (0x34) | `END` | 0 | 0 | — |
| 53 (0x35) | `TOUR` | 2 | 57 | its uses |
| 54 (0x36) | `BUMP` | 2 | 159 | its uses |
| 55 (0x37) | `COAST` | 2 | 216 | its uses |
| 56 (0x38) | `ADDHEAD` | 1 | 31 | its uses |
| 57 (0x39) | `DELHEAD` | 1 | 31 | its uses |
| 58 (0x3A) | `LIMBO` | 2 | 23 | the sources |
| 59 (0x3B) | `UNLIMBO` | 1 | 23 | the sources |
| 60 (0x3C) | `FORCEUNLIMBO` | 1 | 23 | the sources |
| 61 (0x3D) | `INLIMBO` | 1 | 4 | the sources |
| 62 (0x3E) | `LIMBOSPACE` | 1 | 22 | its uses |
| 63 (0x3F) | `SPAWNCHILD` | 1 | 4 | its uses |
| 64 (0x40) | `SPAWNSOUND` | 1 | 31 | its uses |
| 65 (0x41) | `REMOVECHILD` | 0 | 3 | its uses |
| 66 (0x42) | `SETVARINCHILD` | 2 | 0 | — |
| 67 (0x43) | `GETVARINCHILD` | 2 | 0 | — |
| 68 (0x44) | `SETVARINPARENT` | 2 | 0 | — |
| 69 (0x45) | `GETVARINPARENT` | 2 | 0 | — |
| 70 (0x46) | `BOUNCESETNODE` | 1 | 1 | its uses |
| 71 (0x47) | `BOUNCESETBASE` | 1 | 4 | its uses |
| 72 (0x48) | `BOUNCE` | 2 | 4 | its uses |
| 73 (0x49) | `UNBOUNCE` | 1 | 4 | its uses |
| 74 (0x4A) | `FORCEUNBOUNCE` | 1 | 8 | its uses |
| 75 (0x4B) | `BOUNCING` | 1 | 24 | its uses |
| 76 (0x4C) | `WALKON` | 7 | 77 | its uses |
| 77 (0x4D) | `WALKOFF` | 1 | 86 | its uses |
| 78 (0x4E) | `WALKGET` | 1 | 80 | its uses |
| 79 (0x4F) | `WALKST_FLOAT` | 3 | 0 | — |
| 80 (0x50) | `WALKFLOATSTAT` | 1 | 0 | — |
| 81 (0x51) | `WALKFLOATSTOP` | 0 | 0 | — |
| 82 (0x52) | `ENABLELIGHT` | 1 | 0 | — |
| 83 (0x53) | `DISABLELIGHT` | 1 | 0 | — |
| 84 (0x54) | `SETLIGHT` | 1 | 0 | — |
| 85 (0x55) | `COLOURLIGHT` | 4 | 0 | — |
| 86 (0x56) | `STARTSCREAM` | 2 | 32 | its uses |
| 87 (0x57) | `STOPSCREAM` | 0 | 64 | its uses |
| 88 (0x58) | `SINGLESCREAM` | 2 | 35 | its uses |
| 89 (0x59) | `SCREAMLEVEL` | 1 | 43 | its uses |
| 90 (0x5A) | `FINDSCRIPTRAND` | 2 | 1 | its uses |
| 91 (0x5B) | `GETREMOTEVAR` | 3 | 0 | — |
| 92 (0x5C) | `SETREMOTEVAR` | 3 | 2 | its uses |
| 93 (0x5D) | `REPAIREFFECT` | 1 | 132 | its uses |
| 94 (0x5E) | `GETCUSTPTCLCODE` | 2 | 0 | — |
| 95 (0x5F) | `SETTIMER` | 1 | 87 | its uses |
| 96 (0x60) | `GETTIMER` | 1 | 42 | its uses |
| 97 (0x61) | `YEAR` | 1 | 0 | — |
| 98 (0x62) | `MONTH` | 1 | 0 | — |
| 99 (0x63) | `DAY` | 1 | 0 | — |
| 100 (0x64) | `HOUR` | 1 | 0 | — |
| 101 (0x65) | `MIN` | 1 | 0 | — |
| 102 (0x66) | `SEC` | 1 | 0 | — |
| 103 (0x67) | `SETREVERB` | 1 | 6 | its uses |
| 104 (0x68) | `DIPMUSIC` | 1 | 11 | its uses |
| 105 (0x69) | `SPARK` | 4 | 0 | — |

## Behaviour

How the game runs the scripts is for the interpreter (phase 7). What
the sources and OpenTPW's pages say:

- Every ride's first twelve variables are the same: `VAR_LETMEON`,
  `VAR_LETMEOFF`, `VAR_CAPACITY`, `VAR_DURATION`, `VAR_BREAKSTAT`,
  `VAR_ONRIDE`, `VAR_RIDECLOSED`, `VAR_BROKEN`, `VAR_WORN`,
  `VAR_RUNNING`, `VAR_SPEED` (`VAR_PAD` in the game's scripts) and
  `VAR_PARAM`: "Common variable set - All ride scripts must have
  these", and "All variables are initialised to zero by default", the
  sources say. The game writes some (a guest wanting on in
  `VAR_LETMEON`) and reads others.
- `TEST`, `CMP`, `SUB` and others set flags, zero and negative, which
  the `BRANCH_` instructions test.
- A script runs until `ENDSLICE` or a wait, then gives the others their
  turn *(inferred)*: every script's main loop has one.
- `CRIT_LOCK` and `CRIT_UNLOCK` bracket the parts that touch the
  queue: "do not disturb", `test.rss` says.

## In Reluva

`Script` (`src/engines/tpw/formats/rse.cpp`) reads a script into its
header, instructions, strings and variables, and writes it out as
source, in the shipped sources' language, a label `.a<word>` at each
branch's target. `reluva-tool tpw rse-dis <game folder> <script>`
prints one; `reluva-tool tpw rse-check <game folder>` reads them all
and counts each opcode's uses; the game-files test runs it
(`tpw.gamedata.scripts`). The viewer's Scripts page shows each script
as source.

## Sources and checks

- **Where the facts come from:** the US release's scripts (November
  2026); the five shipped sources set beside their compiled scripts;
  OpenTPW's written pages on the format, for the header's fields and the
  names of the instructions the sources don't use (their code wasn't
  read).
- **How it was checked:** all 288 read with `reluva-tool tpw
  rse-check`, every one's code and strings to its end; the five sources'
  instructions, in order, are their compiled scripts' (2, 39, 13, 12 and
  52 instructions), and their directives the headers'. The unit tests
  (`tpw.unit.formats`) read a script built in the test, its disassembly
  checked line by line, and fail broken ones.

## Open questions

- What most instructions do, and their operands: the interpreter's work.
- The header's 0F 01 00 and its time slice.
- The 34 opcodes no script uses, `SETLV`'s operands among them.
