# Star Trek: Away Team: objects saved in binary

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

November 2026, from `Star Trek.exe` disassembled with `objdump`. For the
sequences (`.seq16`), models (`.mdl16`) and the maps' `.way` files, which
all start with the float 26.0 (`00 00 d0 41`).

## Where

- `0x40dbd0` writes an object in binary; `0x40dc60` reads one. They sit
  beside the text writer (`0x40d8c0`) and reader (`0x40d9f0`) of the same
  objects ([formats/text-object.md](../formats/text-object.md)).
- The constant 26.0 is pushed at `0x40dbda`. The reader refuses a
  version under 4.0 (`0x6152ac`); one of 24.0 (`0x6152a8`) it takes as
  25.0 after reading a dword more (`0x40dce3`, `mov [esp+8],
  0x41c80000`). 26.0 itself is at `0x6152a4`.

## What the writer writes

1. The float 26.0: the format's version.
2. If the stream keeps no table of objects yet (`[stream+0x20]+0x18` is
   0): it starts one (`0x54d710`), adds the object (`0x54da80`) and writes
   the object's pointer (4 bytes). Not seen in the files' starts: their
   second dword is a float (586.0 in `!None.seq16`).
3. The class's version, a float (`0x40c9e0` on the class's description,
   from the object's vtable `+0x08`).
4. Each property, in the order the class lists them (`[class+0x08]`,
   `[class+0x20]` of them), by the property's own vtable `+0x34`: no
   names, no sizes.
5. The object's own data, by its vtable `+0x34`.

So the files can't be read without each class's list of properties and
how each kind of property writes itself. The text writer walks the same
list, so a class's text files give its properties' names and order
(guessed); the classes of `.seq16` and `.mdl16` aren't in any text file.

## Seen in `!None.seq16` (380 bytes)

```text
00 00 d0 41            26.0
00 80 12 44            586.0: the class's version?
01 00 00 00            1: frames?
00 00 f0 41            30.0: frames a second?
01                     ?
10 00 00 00 "Transparent Mask"   a string: its length, then its bytes
01 01 00 00 ...        ?
fb ff ff ff fb ff ff ff 06 00 00 00 06 00 00 00   -5, -5, 6, 6: a box
01                     ?
2e 10 ...              a frame (formats/frm16.md): 11x11, origin 5,5
```

## Next

Find the classes' property lists: where classes register (the object
factory behind `0x40cae0`'s list) and each kind of property's binary
writer and reader.
