# Star Trek: Deep Space Nine - Harbinger: scripts (`CC.ALL`, `CCB.ALL` and their lists)

> **Status:** decoded · **Code:** `src/engines/stharbinger/formats/script.cpp`, `src/engines/stharbinger/formats/resources.cpp`, `src/engines/stharbinger/formats/dialogue.cpp` · **Checked on:** disc 1's 31 scene scripts and 72 dialogue scripts: all 32,974 scene directives and every dialogue read; every scene argument named, and 18,657 of the dialogues' 18,697 (the tool names a dialogue's arguments from one scene's lists, and finds no name for the other 40) (`reluva-tool stharbinger script`, November 2026) · **Confidence:** layout confirmed; behaviour from the code unless marked

## Overview

The scene scripts and their name lists, the linker files, and the dialogue
scripts and text, installed from disc 1's `SSE/SSE.ALL`.

## Layout

### Directive lists

```
u16 count
directive:  u8 command (0x32..0xC8), then ARGC[command] argument slots
slot:       u8 type, then data:
              00                 none (unused slot)
              01, 04             1 byte
              02, 08, 10, 18, 40 2 bytes
              20                 4 bytes (the second word is ignored)
```

`ARGC` is the table at `0x55140`. Type `02` is a symbol: an index into one of the
scene's name lists, a label number, or a keyword, depending on the command and slot
(see [../systems/script-reference.md](../systems/script-reference.md)). For commands that read values, a type `02` variable
goes through a per-scene map of 300 slots into the global variable array. Type `40`
is a constant.

### Scene scripts and name lists

`CC.ALL`, `CA.ALL` … `CV.ALL`, `CCS.ALL` and `CCZ.ALL` each have 32 entries (with
`.LST`), one per scene slot (slot 23 is empty in every file):

| Archive | Entry contents |
|---|---|
| `CC` | the scene's directive list; its first command is `SCENE` |
| `CA` `CB` `CS` `CD` `CT` `CH` `CV` | name lists: animations, backgrounds, sounds, dialogues, timers, hotspots, variables. Each entry is `name\0 source-file\0 flag` |
| `CCS` | linker cross-reference: `symbol\0 command slot` for each distinct use |
| `CCZ` | `u16` number of cross-reference entries |

Name-list indexes are positions in the list. Labels are numbered in the order they
are defined (`LABEL`, command `C8`). The source file of most entries is `NAME.CCG`,
which gives the scene's name.

### Linker files (`DS9.LCC`, `.LCV`, `.LVM`, `.LCX`, `.LDM`)

Read by DEEP.EXE at start-up, from the installed folder (`deep.exe ds9`):

| File | Contents |
|---|---|
| `DS9.LCC` | text, one scene name per line, in link order; the game starts with the first (`opening`) |
| `DS9.LCV` | per scene in link order: `u16 n`, then `u16[n]`, the global variable number of each entry of the scene's `CV` list |
| `DS9.LVM` | `u16 n`, then n NUL-terminated global variable names |
| `DS9.LCX` | `u16 n`, then `u16[n]`: link index of each scene symbol (the argument of `SCENE`/`NEWSCENE`) |
| `DS9.LDM` | dialogue names |

The variable map is the same as matching each scene's `CV` names against
`DS9.LVM`, ignoring case, which is what the engine does.

### Dialogue scripts (`CCB.ALL` + `.LST`)

72 entries, in the order of the dialogue-name table in `DEEP.EXE` (`COM010` …
`YAR020`):

```
directive list A
directive list B
u8 n, tree of NPC lines         (n non-empty groups)
u8 n, tree of player choices
tree group: u8 k, then k items:
              u16 line, u16 choice (FFFF for NPC lines), directive list,
              u8 m, then m sub-items: u16, u16, directive list
```

Empty groups (`k = 0`) do not count toward `n`. Line numbers index the dialogue's
lines in `CCQ`/`CCF`.

### Dialogue text

| Archive | Contents |
|---|---|
| `CCQ` | NUL-separated line text. `[label]` = menu label of a player choice, `{…}` = recording notes |
| `CCF` | CRLF-separated clip names, one per line: `<conversation 3><speaker 3><index 2>` |
| `CCR` | the menu labels again |

Each has 72 entries, in the same order as `CCB`.

## Sources and checks

*Star Trek: Deep Space Nine – Harbinger* (Viacom New Media, 1996), DOS, two CDs.
Worked out from the data and from `DEEP.EXE`. All values are little-endian.
Addresses like `0x1DD32` are virtual addresses in `DEEP.EXE`'s LE image (code
object at `0x10000`, data at `0x50000`); see
[REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).

- **How it was checked:** the unit tests (`stharbinger.unit.formats`, [TESTING.md](../TESTING.md)) cover scripts and name lists, and dialogue records.
