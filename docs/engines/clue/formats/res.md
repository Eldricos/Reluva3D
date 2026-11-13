# Clue: Murder at Boddy Mansion: resource files

> **Status:** decoded · **Code:** `src/engines/clue/formats/resources.h` · **Checked on:** the retail CD (2.0, US and UK data), a clean install of it and the 2.2 patch: every file · **Confidence:** confirmed unless marked

## Overview

The game's resource files: `.res` images, `.a` animations, `.o` objects
and `clue.lvl`, all `DDRV` archives, the images and animations packed;
resources are named by drive and kind ([Names](#names)).

## Layout

All numbers are little-endian.

### `PACK`: a compressed file

`.res` (images) and `.a` (animations) are stored compressed:

| Offset | Size | |
|---|---|---|
| 0 | 4 | `PACK` |
| 4 | 4 | size of the compressed data |
| 8 | 4 | size once uncompressed |
| 12 | | a zlib stream (RFC 1950: its 2-byte header, deflate data, an Adler-32) |

Each file is exactly 12 bytes plus the compressed size, and each unpacks
to the size given (all 153). `.o` (objects) and `.lvl` (the mansion) aren't
packed. `dialogInfo.res` isn't either, and isn't a `DDRV`: it starts with
`RESI`, the menus' and dialogs' layout ([resi.md](resi.md)).

### `DDRV`: an archive ("data drive")

Eagle's library calls these `EGDataDrive`, with entries `dd_FileEntry` (its
exported names). All 336 `.res`, `.a`, `.o` and `.lvl` files are one.

| Offset | Size | |
|---|---|---|
| 0 | 4 | `DDRV` |
| 4 | 4 | offset of the directory |
| 8 | 4 | number of entries |
| 12 | | the files |
| directory | 148 each | the entries |
| end − 4 | 4 | number of entries again |

An entry:

| Offset | Size | |
|---|---|---|
| 0 | 4 | 0 in every file |
| 4 | 8 | a Windows FILETIME (1998) |
| 12 | 4 | offset of the file |
| 16 | 4 | its size |
| 20 | 128 | its name, NUL-padded |

The directory is in name order, not the files' order. An `ANIM` entry's
size is 4 bytes more than its record holds (it takes in the next record's
tag, or the directory's first 4 bytes), so animations are read by their
own structure. `clue.lvl`'s entries are `/Info.dat`, `/Objects.dat`,
`/Workspace.dat` and `/World.dat`.

### Names

Resources are named `drive:Xname`: the drive is a file's name without its
extension, `X` the kind and also the first letter of the entry's name in
it. `I` is an image in `drive.res`, `A` an animation in `drive.a`, `O` an
object in `drive.o`. For example `c_scarlet:Imsidlee_01` is the entry
`Imsidlee_01` of `c_scarlet.res`, the first frame of Miss Scarlet's idle
animation facing east. Animations name images in other drives too: the
conservatory's fountain uses `wall:Iwindow_fntn`.

### `EIMG`: an image

Two versions, both run-length coded 16-bit images. Version `0x200` holds
the mansion, its objects, the characters and most of the interface;
version `0x100` the fonts and part of the interface (`18ptup`, `largeu`,
`main_anim`, `ticker` ...). The 2.2 patch's `small` and `ticker` are
`0x200` where the CD's are `0x100`.

Version `0x200`:

| Offset | Size | |
|---|---|---|
| 0 | 4 | `EIMG` |
| 4 | 4 | `0x200` |
| 8 | 4 | the size of the rest |
| 12 | 4 | 2 |
| 16 | 4 | width |
| 20 | 4 | height |
| 24 | 1 | 3 |
| 25 | 4 | bytes per row of the original's surface: the width rounded up to even, × 2 |
| 29 | 12 | zeros |
| 41 | 4 | width again |
| 45 | 4 | height again |
| 49 | 4 | `00 FF 00 00` |
| 53 | 4 | size of the pixel data |
| 57 | | the pixel data |

Version `0x100`:

| Offset | Size | |
|---|---|---|
| 0 | 4 | `EIMG` |
| 4 | 4 | `0x100` |
| 8 | 4 | the size of the rest |
| 12 | 2 | width |
| 14 | 2 | height |
| 16 | 4 | 16 (bits per pixel) |
| 20 | 4 | 2 |
| 24 | 12 | zeros |
| 36 | 2 | x of the pixels in their cell |
| 38 | 2 | y |
| 40 | 2 | the cell's width |
| 42 | 2 | the cell's height |
| 44 | 4 | `00 FF 00 00` |
| 48 | 4 | size of the pixel data |
| 52 | | the pixel data |

A font's glyph is stored without its blank edges: `18ptup:I18ptup100` is
10×11 pixels at (1, 8) in a 12×24 cell. Every glyph fits its cell.

The pixels are RGB565, coded row by row as a series of codes:

- 1-127: that many pixels follow, 2 bytes each;
- `0x80 | n`: skip n transparent pixels;
- the end of the row: `0x80` in version `0x200`, `0x00` in version `0x100`.

This reads each of the 17,026 images in all the copies to the last byte of
its data, without a row running past its width. The executable's
`ImageRLE16.dll` draws these images (its exports: `DecodeByteBlit16` and
clipped, flipped, scaled and sheared variants).

### `ANIM`: an animation

| Size | |
|---|---|
| 4 | `ANIM` |
| 2 | `0x200` |
| 1 + n | its own name, n bytes. Most are 128 bytes of `0xCD`, the fill Visual C++'s debug heap gives new memory: unnamed |
| 1 | frames a second (`FPS`; 0 in most of the interface) |
| 1 | how it plays (`Flags`): 0 once, 1 round and round, 2 to the end and back |
| 1 | number of layers |
| 2 | number of frames |
| 2 | number of images |
| 2 | width |
| 2 | height |
| 4 | a part's size in bytes: 10 in every record |
| 4 | EagleM's size for an entry of its table of images in memory (8 + a 128-byte name): 136 in every record, no bearing on the file |
| 4 | 4 + layers × frames × 10 |
| | the images: each 1 + n bytes, a name `drive:Iname` |
| | the layers: each a 4-byte number (0, 1, ...) and a 10-byte part for each frame |
| | `ISOA` and its block, in most |

A part is the layer's image in that frame:

| Offset | Size | |
|---|---|---|
| 0 | 2 | the image's number in the list, or `0xFFFF` for none in that frame |
| 2 | 2 | x |
| 4 | 2 | y |
| 6 | 4 | its own draw flags (`Flags`), 0 in all but a few (below) |

So a frame is its layers' images drawn at their places. A character has
one layer; a floor of 4×4 tiles one frame of 16 layers, a tile each; the
conservatory's fountain 4 layers: the window behind it, the fountain, and
two of water (`Icnwater_`, `Icnwaterb_`), each with frames of its own.

`ISOA` (isometric animation): a 4-byte size, then one of two forms. The
short one, 10 bytes a frame, is in the fonts and some of the interface;
the size is then frames × 10. The long one has 24 bytes a frame and
extras:

| Offset | Size | |
|---|---|---|
| 0 | 2 | x of the frame's top left from the anchor point (signed; −44 for Miss Scarlet) |
| 2 | 2 | y (−106 for her: the anchor is at her feet) |
| 4 | 3 | `RelativeWorld`: a world offset, x, y and height, signed (0 in every frame) |
| 7 | 1 | the number of boxes: 1 (0 in some) |
| 8 | 2 | the box's number |
| 10 | 2 | its x from the anchor (signed; mostly 0, −25 for a wall lamp) |
| 12 | 2 | its y |
| 14 | 2 | its height off the floor: 1 for things that stand, −1 for flat ones (floors, the black outside), 0 for frames without a box |
| 16 | 2 | the box's size along the world's x |
| 18 | 2 | its size along y |
| 20 | 2 | its height |
| 22 | 2 | number of 7-byte extras that follow |

As `Clue.exe` reads it (0x4491DB on): the frame's x and y, three bytes,
the number of boxes, then each box as a number and six signed values,
its offset (x, y, height) and size (x, y, height; 0 taken as 1), then
the extras. The box is the space the object fills in the world, and the anchor point
is its back corner on the floor (see [level.md](level.md) for the world's
axes). Walls along x are 64 × 16 × 161, along y 16 × 64 × 161; the
characters 16 × 16 × 100; the pool table 168 × 92 × 48; a 4 × 4 floor block
64 × 64 × 1, flat. The image follows from it: a wall along x is 80 = 64 +
16 pixels wide and its frame's top left is at (−16, −161) from the anchor.

An earlier version of this page took bytes 20-21 for the frame's time: the
characters' 100 fitted that, but walls' 161 and the armour's 140 show it's
the height. Another took the header's 10 for the frames' rate; it is the
parts' size (EagleM.dll reads each part with it, 0x1000F0B8), and the
rate is the byte after the name.

How the fields were named: EagleM.dll writes an animation as text too
(0x1000F500): `Animation`, `Flags`, `FPS`, `FrameSize`, then each layer's
`Frame x y "image"` with its `Flags`; Clue.exe writes the ISOA block so
(0x4499B0): `Relative2D x y`, `RelativeWorld x y z`, `BBox` and its seven
numbers, and a `KeyList` of the frame's keys.

**Playing.** EagleM.dll plays the interface's (0x1000BFF0): a frame each
1000 / FPS ms (integer), round and round whatever the flags, still at 0
FPS. Clue.exe plays the house's (0x427E60 on each 66 ms tick, 0x446E30): a
frame each 1000 / FPS ms, 10 FPS when it has none, by the flags: once (the
last frame held), round and round, or to the end and back. In the game:
the suspects' walks 13-16 FPS, round; their idles 4-8, to the end and
back; the fires 8, round; the storm over the menus 15; the hot spots'
reactions 2-100, once.

**A part's flags**, when not 0, are what Clue.exe draws that image of the
house with in place of the object's own (0x4285E8): their high 16 bits a
pixel function of `ImageRLE16.dll`'s (`GetBlittersTable`, 0x10061030): 2 a
quarter of the picture over three quarters of what is behind, 3 half and
half, 4 three quarters of the picture. The conservatory's fountain's
water (`Acnfntn`, 2, 3 and 4) and `specialfx:Abad` (2) have them. EagleM
draws the interface's without them.

**The extras** are the frame's keys, 7 bytes each as the objects'
([level.md](level.md)): a 2-byte key, a type byte (0, a whole number) and
a 4-byte value. The game has one: key 5000 (value 0), two in each of the
six characters' eight `mult` (walking) animations, on the frames a foot
comes down: Clue.exe plays a footstep there (0x426660), one of
`male_step1`-`5` for the men (secondary type 1, 3, 5) or `female_step1`-`7`
for the women, by chance.

All 1,034 animations of every copy read this way, and all 7,262 images
they name are found and decode. In the short form the first 4 bytes are
the anchor offsets too; the rest are zeros.

## In Reluva

`reluva-tool clue check <game folder>` repeats the checks with Reluva's
readers; `clue list` and `clue extract` show and extract a file's contents.

## Sources and checks

- **Where the facts come from:** the files of the retail CD (version 2.0, US and UK data), a clean
  install of it and the 2.2 patch, studied byte by byte (October 2026). Each
  layout above was first guessed from a few files, then checked by a program
  against every file of the CD, the install and the patch; the counts given
  are from those checks. Images were also compared with the game's look:
  Miss Scarlet's walk, the ballroom's couch, the credits' team photo, the
  title screen. Nothing here comes from the executables, except where said.
