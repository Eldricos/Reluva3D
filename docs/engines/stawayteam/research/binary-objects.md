# Star Trek: Away Team: objects saved in binary

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

November 2026, from `Star Trek.exe` disassembled with `objdump`, and the
retail `DATA.dat`. For the sequences (`.seq16`), models (`.mdl16`) and the
maps' `.way` files, which all start with the float 26.0 (`00 00 d0 41`).
Everything here is from the code unless said, and checked by decoding
every such file ([Checks](#checks)).

## The classes

The game's objects are of some 550 classes, each described by an object
made as the program starts:

- `0x40c8a0(name, version, parent, size)` fills a description: name at
  `+0x10`, version (a float) at `+0x14`, parent's description at `+0x1c`,
  the object's size at `+0x2c`. `0x411370` does the same through more
  arguments (the description's global, then these, then three functions:
  create, ?, register the properties).
- A description is kept in a global, which a getter returns
  (`mov eax,[global]`), making it on first use. A getter that's
  `xor eax,eax` is a class with no parent.
- `0x40cc40` starts the class's properties as copies of its parent's
  (`+0x08` the list, `+0x20` the count, `+0x24` the parent's count);
  `0x40ccb0` adds one. Inherited properties come first.
- `0x40cd30` then works out the version: the class's own, plus 47 for each
  property it adds, plus 51 times its parent's, rounded to a 32-bit float
  at each step (`0x40cd65`, `0x40cd75`). A file's objects carry this
  version, so it checks that a class's properties have been found right:
  `CSequence`'s 116 + 47 x 10 = 586 is the 586.0 in every `.seq16`.
- The compiler copied some classes' creation, registrations and all, into
  every place that uses the getter: `CMultipleActionsAction`'s `Action` is
  registered from 22 places. Only the first copy to run does anything.
- Some classes add their properties through a helper of their own,
  `push property; call helper`, which makes the class if need be and adds
  it (`0x5cfaf0` for `CStarTrekHandWeaponInventoryItem`).
- A property with an empty name (the string at `0x651000`) is saved in
  binary, not in text: the text writer skips them (`0x40d94c` tests the
  name's first character).

`tools/stawayteam-re/stat_classes.py` reads all of this out of the
disassembly: [classes.txt](classes.txt) is the result, 553 classes and
2,084 properties.

## An object in binary

Written by `0x40dbd0`, read by `0x40dc60`:

1. The float 26.0: the format's version. The reader refuses one under
   4.0 (`0x6152ac`), and takes 24.0 (`0x6152a8`) as 25.0 after a dword
   more.
2. When the stream keeps no table of objects yet, the object's pointer
   (4 bytes): never in the game's files, which are read with one.
3. The class's version (see above). The reader reads on only when it's
   the class's own (`0x40dd05`), and the format 26.0.
4. Each property, in the class's order, by its own writer (the property's
   vtable `+0x34`; read by `+0x3c`): no names, no sizes.
5. The object's own data, by the object's vtable `+0x34` (read `+0x3c`);
   `0x401040` for most classes, which writes nothing.

### Properties, by their writers

| Writer | Kind | Saved as |
|---|---|---|
| `0x548280` | whole number | 4 bytes |
| `0x51b4b0` | an object of the same file | 4 bytes, its number in the stream's table |
| `0x4134a0` | number | a 32-bit float |
| `0x548740` | true or false | 1 byte |
| `0x548920` | one bit of a flags word | 1 byte, 0 or 1 |
| `0x4b2cd0` | point | 8 bytes |
| `0x43b210` | double | 8 bytes |
| `0x483860` | colour | 3 bytes |
| `0x548b70` | a choice from a list | a string: the choice's name |
| `0x547f50`, `0x40d3c0` | a name, a string | a string |
| `0x40c720` | a value saved as its text | a string |
| `0x40ec20` | an AI | a string: its class's name |
| `0x40eff0` | an object | a string, its class's name (empty for none), then the object |
| `0x548e80` | an object held in place | the object, without its class's name |
| `0x40e630` | objects held in place | a count, then the objects |
| `0x40f920` | an array | a count, then each item: a string first if the array has a list of names, then the item by its element's writer |
| `0x53df30` | a polygon | `POLY2D`, 0, 7, a 4-byte count, then 8 bytes for each point |

A string is a 4-byte length, then its bytes, with nothing after them
(`0x4ae120`).

- **An array's element** is a property of its own, given to the array's
  constructor: its 11th argument for `0x40f420`, its 8th for `0x40f300`
  (one fewer three of offsets), with the list of names two after it.
- **Objects held in place** (`0x40e630`) name their class in their
  constructor's arguments (`CRotationSequence`'s `Sort/Cutting Lines` are
  `CVerticalCut`s).
- **An object held in place on its own** (`0x548e80`, made by
  `0x548d50`) doesn't: its owner's constructor makes it. Its size is in
  the arguments, but several classes have the same version and size. The
  text files name it, though (`BehaviorStack=CEntityBehaviorStack`), the
  same for each class and key, and that's what's used.

### Data of their own

| Class | Writer | After the properties |
|---|---|---|
| `CSequence` | `0x55b640` | a box: 4 numbers of 4 bytes; then for each frame (the count is the class's first property, with no name, at `+0x24`) a byte, 1 if it's there, and the frame ([formats/frm16.md](../formats/frm16.md), by `0x4b3d20`) |
| `CRotationSequence` | `0x550ff0` | `CSequence`'s, then 8 bytes for each frame |
| `CModel` | `0x50f6e0` | a count, a string, then for each a byte, 1 if it's there, and then a name, a 0 byte and a `CRotationSequence` (the reader makes one with `0x550e50`) |
| `CWayPointMap` | `0x52dad0` | a count and that many `CPathWayPoint`s, then a count and that many `CPathLineSegment`s (the reader makes them with `0x530560`, `0x52ff80`) |
| `CPathWayPoint` | `0x5305e0` | a count, then that many 4-byte numbers: the lines it's on |

The classes of objects made by a reader were found by their
constructors' vtables: the vtable's `+0x08` returns the class's
description.

## The files

| Files | Class at the top | Version |
|---|---|---|
| `.mdl16`, all 3,738 | `CModel` | 1603.0 |
| `.seq16`, 179 | `CSequence` | 586.0 |
| `.seq16`, the other 19 (`Cache/Fonts`) | `CFont`, a kind of `CSequence` | 30079.0 |
| `.way`, all 33 | `CWayPointMap` | 101.0 |

A model's `Default Entity` holds the whole entity it makes, with its AI,
its inventory and what happens when it's destroyed: the Klingon's is a
`CEntityWalking` with a `CBorgAI`, a Klingon Disruptor in its
`CInventory`, and a `CCreateDefaultEntityAction` that leaves the
disruptor behind.

## Checks

- `tools/stawayteam-re/stat_objects.py` decodes all 3,738 models, 198
  sequences and 33 waypoint maps with the schema, each to its file's last
  byte, every object's version its class's: 565,765 objects of 147
  classes, the 427,413 `CPathLineSegment`s and 118,856 `CPathWayPoint`s of
  the waypoint maps among them.
- The text files, written from the same lists: of their 129,248 objects,
  127,867 have exactly the schema's keys in its order, and 1,259 have them
  but for properties added after the file was saved (`CProcess`'s
  `Always Use Target Under Cursor` isn't in `Process Types/`). 121 don't
  match, most of them the maps' `CSortList2D`, `CPlasmaTileMap` and
  `CLayerSaveData`, which write more of their own.

## Still open

- Five properties of the schema aren't identified, nor two arrays'
  elements (`unknown` in [classes.txt](classes.txt)), and two
  registrations aren't placed; none of them is in the binary files.
- The maps' own classes' text, and the `.zax` maps' objects in full.
