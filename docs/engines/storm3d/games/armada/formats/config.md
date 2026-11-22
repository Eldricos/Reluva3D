# Star Trek: Armada: settings in C (`.h`, `.cfg`, `.aip`)

> **Status:** partial · **Code:** `src/engines/storm3d/formats/config_file.cpp` · **Checked on:** the English retail 1.0 install's 94 files in this language · **Confidence:** layout confirmed; how the game reads it inferred

## Overview

Much of the game's tuning is in text files written in a part of C, as
variables with their values. The developers' comments call `RTS_CFG.h` a
"runtime configuration file".

| Files | What they set |
|---|---|
| `RTS_CFG.h`, which includes `ART_CFG.h` | The camera, the interface, the starfield, each detail level |
| `game_cfg.h`, `shell_cfg.h` | Folders, the front end's buttons |
| `Dynamic_Localized_Strings.h` | Pairs of an interface text's key and its English words (`"GUI_CP_OFFICERS_RES", "officers"`) |
| `CCA.cfg`, `Collision.cfg` | Collision avoidance, `Collision.cfg` as `#define`s alone |
| `woncfg.cfg` | The online service's servers |
| `AI/strategic_AI_config.txt` | The computer player's strategic maps |
| `AI/AIPs/*.aip` (85), each including `Aipdef.h` | The computer player's personalities: priorities and build lists |
| `misc/usrmsn01.aip` | A personality naming Battlezone's units (`avtank`, `svrecy`): left over from that game *(inferred)* |

Other `.cfg` files aren't this language: the race interfaces'
`misc/gui_*.cfg` are `key = value` lines like the object definitions
([odf.md](odf.md)), and `Logfile.cfg` has its own.

## Layout

Text in Windows-1252, with `\r\n` line ends.

### Lines starting with `#`

| Directive | Seen | Meaning |
|---|---|---|
| `#include "<file>"` | 87 files | The file's text in its place; found beside the including file |
| `#define NAME text` | 72 lines | A named value, which the game reads as a setting too (`#define GRID_X_SIZE 256.0` in `Collision.cfg`); none has arguments |
| `#DATA` ... `#END_DATA` | `misc/usrmsn01.aip` only | Rows of values for the array declared before it |

### Comments

`//` to the line's end, and `/* ... */`, in `CCA.cfg`, `Aipdef.h` and
`misc/usrmsn01.aip`.

### Declarations

- **A variable:** `<type> <name> = <value>;`. The types are `int`,
  `float`, `double` and `char` (a string, as `char NAME[256]`).
- **A variable with no type:** `DETAIL_1_cinematic_show_grid = 0;`, twice
  in `ART_CFG.h`. The executable names these settings
  (`DETAIL_%d_cinematic_show_grid`), so they're read.
- **A structure:** `struct Build_List_Element { char unit_type_name[80];
  double units_to_build; };`, then arrays of it with their values as one
  flat list (`"bbase", 1, "bdestroy", 1, ...`), taken a field at a time.
  Array sizes can be defines (`translations[MAX_STRINGS]`).

### Values

- Numbers, with or without C's `f`, and negative.
- Expressions of numbers and settings set before, with `+ - * /` and
  brackets (`STAR_FAR_CLIP_PLANE*0.02`).
- Strings, which may run over several lines (the tooltips in
  `Dynamic_Localized_Strings.h`) and have no escapes: `".\missions"` is a
  path with one backslash *(inferred)*.
- Brace lists for arrays, with an optional comma before the `}`.

## Behaviour

- **A setting used before it's set.** `ART_CFG.h`'s line 22,
  `float STARFIELD_GEOMETRY_SCALE = STAR_FAR_CLIP_PLANE*0.02;`, uses a
  variable set at line 34. C wouldn't accept it. The executable holds both
  names, so it likely has its own default for `STAR_FAR_CLIP_PLANE`, which
  the line would use *(guessed)*.
- **Which settings the game reads.** It looks them up by name (the
  executable holds the names, as `DETAIL_%d_cinematic_show_grid` shows),
  so a name it doesn't know is presumably ignored *(inferred)*.

## In Reluva

`ConfigFile` (`src/engines/storm3d/formats/config_file.h`) reads a file
and its includes, through a loader, into variables, defines and
structures. `find()` gives a variable, or else a define read as a value;
`number()` and `string()` read one. Arrays of a structure are lists of
records, each a list of its fields.

- A variable whose value uses a setting not set yet is left out, and
  reported, rather than given a guessed value: so far only
  `STARFIELD_GEOMETRY_SCALE`.
- A `#DATA` block is skipped, and reported.

Commands:

- `reluva-tool storm3d config <game folder> <file>`: a file's
  structures, defines and variables, with their values.
- `reluva-tool storm3d config-check <game folder>`: reads every file in
  the table, and every `.aip`.

## Sources and checks

- **Where the facts come from:** the 94 files, and the names in
  `Armada.exe`'s strings.
- **How it was checked:** `config-check` reads all 94, with the one
  setting left out and the two `#DATA` blocks skipped. The unit tests
  (`storm3d.unit.config`) cover each form above and the failures.

## Open questions

- `STAR_FAR_CLIP_PLANE`'s value when line 22 is read, and so
  `STARFIELD_GEOMETRY_SCALE`.
- What `#DATA` rows mean, if a file of Armada's own ever has them.
