# Star Trek: Away Team: objects in text (`.txt`, `.zax`, `.MDS`, `.STY`)

> **Status:** decoded · **Code:** `src/engines/stawayteam/formats/text_object.cpp` · **Checked on:** the US English retail `DATA.dat`: all 753 objects read and written back byte for byte (November 2026) · **Confidence:** confirmed unless marked

## Overview

The game keeps most of its data as C++ objects written out as text: each
file is one object, named by its class, holding fields that are values or
other objects. In `DATA.dat`:

| Files | Class at the top | Count |
|---|---|---|
| `Rules.txt` | `CStarTrekRules`: the first map, the screen's layout, the menus | 1 |
| `Levels/Mission NN/*.zax` (and `.zax.BAK` copies) | `CLayerSaveData`: a map, its objects and scripts | 33 (and 16) |
| `Levels/Mission NN/Mission NN.MDS` | `CMissionDescription`: a mission's name, briefing, map | 18 |
| `Surface Types/*.STY` | `CStarTrekSurfaceType` | 8 |
| the other `.txt`: roster, items, damage and death types... | one class each | 677 |

The `.zax` files are 1-2 MB each, the largest objects; the deepest
nesting is 49 levels.

Not every `.txt` is an object: `Credits.txt` and the ten
`Levels/Mission NN/NN Voice Script.txt` are text of their own kinds
([below](#text-files-that-arent-objects)).

## Layout

Text in Windows-1252, lines ending CRLF.

### An object

```text
CMissionDescription
{
	Mission Name=Klingon Base on Rigel 4
	Show Briefing=1
	Script=CBriefingScript
	{
		Script=Array
		{
			Array Count=2
			Array Item=CBriefingScriptLine
			{
				Text=01 Mission Briefing
			}
			...
		}
	}
}
```

- The file starts with the class name on a line of its own, then `{`,
  then the fields, then `}`. Nothing follows the last `}` but, in 736 of
  the 753 files, a line end.
- A field is `Key=Value`, split at the first `=`. Keys have spaces
  (`Mission Name`); values are text as written, with any spaces at their
  end, which matter: 41 values end in a space, and they're names other
  fields refer to the same way (`Name=spawn K6 hiding ` and
  `Relay Name=spawn K6 hiding `). No value holds a `=` or a brace.
- When a field's value is a class name and a `{` line follows, the field
  is an object: that block, of that class.
- The game writes a tab before a line for each level it's nested in. It
  reads lines with any tabs, spaces, CR and LF before them.

### Arrays

An object of class `Array`. Its first field is usually a count, `Item
Count` (96,519 arrays) or `Array Count` (1,646), which is always the
number of items after it; 19 arrays have no count (11 in `Death
Behaviors/`, 8 in `Character/`), and their items run to the `}`. The items are fields under
one key, objects or plain values (761 arrays of values, as `Actor=Ty
Mijoral` in `Rules.txt`).

### Keys more than once

A key can come more than once outside arrays too: 49 objects have one
(`Level Part` in `CSortList2D`, in maps). The fields' order is kept.

## Behaviour

In `Star Trek.exe`: `0x40d9f0` reads an object, `0x40d8c0` writes one.

- **Reading a line** (`0x4adfa0`) takes up to 1,024 bytes, without its
  line end, as a bare `{` compares equal *(inferred)*. Leading tabs, spaces,
  CR and LF are skipped (`0x5dfc60`, its set at `0x64ab20`); nothing at a
  line's end is.
- **The object's `{`:** the reader reads a line; if it isn't `{` (the
  class name, when that wasn't read before), it reads one more, taken for
  the `{` whatever it holds (`0x40da65`).
- **Each line then:** one starting `}` ends the object (`0x40dab6`); one
  without `=` is skipped (`0x40dabe`, through `strchr` at `0x6077f0`).
- **Keys:** each is looked for among the object's own properties
  (`0x40cae0`: a list of names, searched from the last one found), and
  the property found reads the value itself (its slot `0x24`,
  `0x40db31`): one holding objects reads the lines after it as one
  (objects held in place, `0x40e508`, through `0x40d9c0` to `0x40d9f0`).
  So a field is an object because its class says so, not because of the
  lines after it; one whose object is empty has no lines at all.
- **Other markings the code knows:** values holding `::` (checked at
  `0x40db36`) and numbers written `%s=0x%08X` (`0x6362b0`). Neither is in
  any of the game's files.

### Text files that aren't objects

- **`Credits.txt`** has `//` comments and lines to centre: it's read by
  the credits' own reader ([menus.md](../systems/menus.md)). Each `NN
  Voice Script.txt` has `*Speaker*` lines then `:Label` and text, as
  `.RDS` files do, and the game never reads them: under `Levels` it
  looks only for maps (`.zax`, `0x4f7648`), missions (`.MDS`,
  `0x507830`), scripts (`.RDS`, `0x4f60b3`), sounds (`0x4f609d`) and
  minimaps (`0x5c5df6`), and no name of a voice script is in the
  program. They look like the script the voices were recorded from.

## In Reluva

- **Reading:** `readTextObject` into a `TextObject`: its class, and its
  fields in order, each a `TextField` with its key, its value and, for an
  object, the object. `TextObject` finds a field by key, reads one as a
  number, and gives an array's items without its count. Lines are read as
  the game reads them, but for objects: Reluva takes a field for an
  object when a `{` line follows. On the game's files that's the same:
  of their 1,838,132 fields with a value and a class property, every one
  the game's property table makes an object, and no other, has a `{`
  after it (`tools/stawayteam-re/stat_text_kinds.py`); 464,914 more are
  empty, and 52,468 are keys of classes that read their text themselves.
  A file edited by hand could still tell the two apart.
- **Writing:** `writeTextObject` writes as the game does: the game's
  files come back byte for byte, but for the 17 without a line end at
  their end.
- **The engine** reads `Rules.txt` as it starts.
- **Commands:** `reluva-tool stawayteam data-check` reads every object
  (and script) in `DATA.dat`, writing each object back to compare with
  its file; `reluva-tool stawayteam text-show` prints one file's fields.

## Sources and checks

- **Where the facts come from:** every text file in the retail `DATA.dat`,
  surveyed with scripts: the lines' kinds, the arrays' counts and items,
  keys more than once, spaces at lines' ends. The reader in `Star
  Trek.exe`, disassembled with `objdump` from where it uses its `{`, `}`
  and `=` strings, at the addresses given.
- **How it was checked:** `reluva-tool stawayteam data-check` on the
  retail `DATA.dat` reads all 753 objects, and writes each back the same
  (736 byte for byte, 17 but for the line end at their end); unit tests
  (`stawayteam.unit.formats`) on objects made in the test: arrays with
  and without counts, spaces at values' ends, lines without `=`, LF and
  CRLF, and text that isn't an object.

## Open questions

None now.
