# Star Trek: Away Team: objects in binary (`.mdl16`, `.seq16`, `.way`)

> **Status:** decoded · **Code:** `src/engines/stawayteam/formats/binary_object.cpp` · **Checked on:** the US English retail `DATA.dat`: all 3,738 models, 198 sequences and 33 waypoint maps, each read to its last byte (November 2026) · **Confidence:** from the code unless marked

## Overview

The game's models, sequences and waypoint maps are its objects saved by
its binary writer: the same objects, of the same classes, as the text
files hold ([text-object.md](text-object.md)), but with no names. Each
value follows the one before, in the order its class lists its
properties, so reading one needs every class's list: Reluva keeps them in
a table read out of `Star Trek.exe`
([research/classes.txt](../research/classes.txt), and
[research/binary-objects.md](../research/binary-objects.md) for how).

| Files | Class at the top | Its version | What it holds |
|---|---|---|---|
| `Cache/Models/**/*.mdl16`, 3,738 | `CModel` | 1603.0 | A thing on the map: its name and description, sounds, damage, movement, the entity it makes (with its AI and inventory), and its animations, each a `CRotationSequence` of frames for each direction |
| `Cache/**/*.seq16`, 179 | `CSequence` | 586.0 | An animation of frames: animated textures, effects, the interface's |
| `Cache/Fonts/*.seq16`, 19 | `CFont` (a kind of `CSequence`) | 30079.0 | A font: a frame for each character |
| `Levels/Mission NN/*.way`, 33 | `CWayPointMap` | 101.0 | A map's waypoints and the lines between them, for finding paths |

## Layout

All numbers little-endian.

### An object

In `Star Trek.exe`: `0x40dbd0` writes one, `0x40dc60` reads one.

| Size | Field |
|---|---|
| 4 | 26.0, a float: the format's version |
| 4 | The class's version, a float |
| | Each property, by its kind ([below](#properties)) |
| | The class's own data, if it has any ([below](#data-of-a-class-of-its-own)) |

A class's version is worked out as the program starts, from its own
version, the number of properties it adds (47 each) and its parent's (51
times), as 32-bit floats (`0x40cd30`). The game reads no object whose
version isn't its class's (`0x40dd05`), so a file says which version of a
class's list it was written with.

A property's group (`Damage/` in `Damage/Hit Points`) isn't part of its
key in text; neither is in binary, where there are no keys.

### Properties

| Kind | Saved as |
|---|---|
| Whole number, an object of the same file (by number) | 4 bytes |
| Number | A 32-bit float |
| True or false, one bit of the object's flags | 1 byte |
| Point | 8 bytes: two 4-byte numbers, shown as whole numbers *(guessed)* |
| Double | 8 bytes |
| Colour | 3 bytes |
| String, name, choice from a list (by its name), AI (by its class's name) | A 4-byte length, then its bytes, nothing after them |
| Object | Its class's name (empty for none), then the object |
| Object held in place | The object, without its class's name |
| Objects held in place | A count, then the objects, of the property's class |
| Array | A count, then the items, each saved by the array's element |
| Polygon | `POLY2D`, 0, 7, a 4-byte count, then 8 bytes for each point |

An object held in place doesn't name its class, nor does the program's
registration of it: its owner's constructor makes it. The game's text
files name it (`BehaviorStack=CEntityBehaviorStack`), the same for each
class and key, and that's where the table has it from, for 55 of the
79. The other 24, in no text file, are told by their version, and by
their size where versions are shared; none is in the game's binary files.

An array's items could each have a name before them (`0x40f920`), but
none of the game's arrays does: each one's list of names is null.

### Data of a class of its own

What a class's own writer adds after its properties, for the five that
have one in these files:

- **`CSequence`** (`0x55b640`): its bounds, 4 numbers of 4 bytes (left,
  top, right, bottom); then for each frame (as many as its first
  property, which has no name), a byte, 1 if the frame is there, and the
  frame ([frm16.md](frm16.md), written by `0x4b3d20`).
- **`CRotationSequence`** (`0x550ff0`): `CSequence`'s, then 8 bytes for
  each frame, what they hold not known.
- **`CModel`** (`0x50f6e0`): a count, a string, then that many entries:
  a byte, 1 if there's a sequence; if there is, its name, a 0 byte and a
  `CRotationSequence`.
- **`CWayPointMap`** (`0x52dad0`): a count and that many `CPathWayPoint`s
  (flags, x, y), then a count and that many `CPathLineSegment`s (flags,
  the width that can pass, length, angle, and the two waypoints' numbers).
- **`CPathWayPoint`** (`0x5305e0`): a count, then that many 4-byte
  numbers: the lines it's on.

## Behaviour

What a model's entity holds shows what the game makes of it: the
Klingon's `Default Entity` is a `CEntityWalking` with the Borg's kind of
AI (`CBorgAI` and its states), a Klingon Disruptor in its `CInventory`,
and, when it's destroyed, a `CCreateDefaultEntityAction` that leaves the
disruptor where it fell.

## In Reluva

- **Reading:** `readBinaryObject` into a `BinaryObject`: its properties
  as a `TextObject`, as a text file would have them (every value as
  text, objects nested), so they're read alike from either kind of file;
  and what its class saves of its own: a sequence's bounds and frames
  (`SequenceData`), a model's named rotation sequences, a waypoint map's
  waypoints and lines.
- **The classes:** `formats/object_schema.h`, and the table
  `formats/class_table.cpp`, generated by
  `tools/stawayteam-re/stat_table.py` from
  [research/classes.txt](../research/classes.txt).
- **Commands:** `reluva-tool stawayteam object-check` reads every
  model, sequence and waypoint map in `DATA.dat`; `reluva-tool
  stawayteam object-show` prints one.

## Sources and checks

- **Where the facts come from:** `Star Trek.exe`, disassembled with
  `objdump`: the writer and reader of objects, each kind of property's
  writer, the five own writers, and every class's registration, read out
  by `tools/stawayteam-re/stat_classes.py`
  ([research/binary-objects.md](../research/binary-objects.md) has the
  addresses). The classes of objects held in place from the text files in
  `DATA.dat`.
- **How it was checked:** every model, sequence and waypoint map in the
  retail `DATA.dat` read to its last byte, 565,765 objects of 147 classes,
  every one of the version its class's table gives: by
  `tools/stawayteam-re/stat_objects.py`, and by `reluva-tool stawayteam
  object-check`, the same with Reluva's reader. The text files, saved
  from the same lists, have the table's properties in its order, but
  where a property was added after they were saved. Unit tests
  (`stawayteam.unit.formats`) on objects made in the test: a sequence
  and its frames, a waypoint map, objects in properties, and what isn't
  the game's.

## Open questions

- **`CRotationSequence`'s 8 bytes a frame,** and the 8 bytes of a point
  or a polygon's point: integers or floats.
- **Five properties** of the table aren't identified, nor two arrays'
  items (`unknown` in [research/classes.txt](../research/classes.txt));
  none of them is in the game's binary files.
