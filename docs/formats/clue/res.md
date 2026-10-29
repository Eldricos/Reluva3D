# Clue: Murder at Boddy Mansion: resource files

Source: the files of the retail CD (version 2.0, US and UK data), a clean
install of it and the 2.2 patch, studied byte by byte (October 2026). Each
layout below was first guessed from a few files, then checked by a program
against every file of the CD, the install and the patch; the counts given
are from those checks. Images were also compared with the game's look:
Miss Scarlet's walk, the ballroom's couch, the credits' team photo, the
title screen. Nothing here comes from the executables, except where said.

`reluva-tool clue-check <game folder>` repeats the checks with Reluva's
readers; `clue-list` and `clue-extract` show and extract a file's contents.

All numbers are little-endian.

## `PACK`: a compressed file

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
`RESI` (the menus' and dialogs' layout, still to be worked out).

## `DDRV`: an archive ("data drive")

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

## Names

Resources are named `drive:Xname`: the drive is a file's name without its
extension, `X` the kind and also the first letter of the entry's name in
it. `I` is an image in `drive.res`, `A` an animation in `drive.a`, `O` an
object in `drive.o`. For example `c_scarlet:Imsidlee_01` is the entry
`Imsidlee_01` of `c_scarlet.res`, the first frame of Miss Scarlet's idle
animation facing east. Animations name images in other drives too: the
conservatory's fountain uses `wall:Iwindow_fntn`.

## `EIMG`: an image

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

## `ANIM`: an animation

| Size | |
|---|---|
| 4 | `ANIM` |
| 2 | `0x200` |
| 1 + n | its own name, n bytes. Most are 128 bytes of `0xCD`, the fill Visual C++'s debug heap gives new memory: unnamed |
| 2 | not understood (two bytes, e.g. 6 and 2 in Miss Scarlet's idle animations) |
| 1 | number of layers |
| 2 | number of frames |
| 2 | number of images |
| 2 | width |
| 2 | height |
| 4 | 10 in every record |
| 4 | 136 in every record |
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
| 6 | 4 | not understood (zero in most) |

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
| 4 | 16 | not understood: 0, then `0x100` or 0, two signed values, a 0/1/−1, two sizes (16 × 16 for the characters) |
| 20 | 2 | the frame's time in milliseconds (100 for the characters; 0 for still frames) |
| 22 | 2 | number of 7-byte extras that follow |

The extras are rare: 96 in an install, each `88 13 00 00 00 00 00`
(5000), two in each of the six characters' eight `mult` animations (one a
direction); perhaps a pause of up to 5 s.

All 1,034 animations of every copy read this way, and all 7,262 images
they name are found and decode. In the short form the first 4 bytes are
the anchor offsets too; the rest are zeros.
