# LEGO Island: Interleaf archives (`.SI`)

> **Status:** decoded · **Code:** `src/engines/legoisland/formats/si.cpp` · **Checked on:** the English 1.0 CD's 26 SI files, every object and chunk read, and all 6,259 media rebuilt as standard files and checked (November 2026) · **Confidence:** confirmed unless marked

## Overview

An SI ("Interleaf") file holds a tree of objects, the game's scripts, and
the data of the media they play, interleaved for streaming from a CD: a
second of speech beside the animation frames it goes with. The CD has 26
in `LEGO\SCRIPTS\` (570 MB), one or a few per part of the game
(`ISLE\ISLE.SI`, `RACE\CARRACE.SI`...); the installer copies `NOCD.SI`
too ([systems/game-files.md](../systems/game-files.md)).

The files are RIFF files of Mindscape's own chunks. Each object is an
`MxOb` record; a medium's data is in `MxCh` chunks, each a piece of it (a
header, a frame, a second of sound) stamped with the time it plays.

## Layout

All values little-endian. Every chunk is a RIFF chunk: a four-letter tag,
the size of what follows, and that, padded to an even size.

```text
RIFF "OMNI"
  MxHd                  the header
  MxOf                  where each stream starts
  LIST "MxSt"
    MxSt                a stream: one object and its data
      MxOb              the object, its children inside it
      LIST "MxDa"
        MxCh ...        its data, and its children's
        pad             filler, to the end of a buffer
    MxSt ...
```

### Buffers

The game reads a file a buffer at a time: 64 KiB in most, 128 KiB in
`ISLE\JUKEBOX.SI`, 352 KiB in `SNDANIM.SI` (the header's buffer size),
counted from the start of the file. No chunk crosses from one buffer to
the next. A piece of data that would is split across two or more `MxCh`
chunks, the first ending at the buffer's end; the rest of a buffer too
small for another chunk is a `pad ` chunk, or, where even a chunk's 8
bytes of header won't fit, bytes of filler (4 in `ACT2\ACT2MAIN.SI` at
`0x1CBFFFC`) with the next chunk at the next buffer's start.

### `MxHd`: the header

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | minor version: 2 |
| 2 | 2 | major version: 2 (the game wants 2.2 *(published)*) |
| 4 | 4 | buffer size, in bytes: 65,536, 131,072 or 360,448 |
| 8 | 4 | buffer count: 1 to 20 |

### `MxOf`: the streams

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | a count: more than the streams listed (22 in `JUKEBOXW.SI`, which has one) |
| 4 | 4 each | each stream's `MxSt` chunk's offset in the file; 0 for an unused entry |

### `MxOb`: an object

The record's fields, in order:

| Size | Field |
|---|---|
| 2 | type, below |
| string | the class that plays it (`LegoWorldPresenter`, `MxControlPresenter`...), or empty for its type's own |
| 4 | 0 in every record |
| string | name |
| 4 | id: its chunks' object id, unique within the file |
| 4 | flags: `0x20` always; `0x02` in most; `0x08` a still drawn with a transparent colour *(inferred)*; `0x01`, `0x04` in some sounds and containers |
| 4 | start time, in milliseconds after its container starts |
| 4 | duration, in milliseconds; -1 for as long as it's wanted |
| 4 | times it plays: 1, or 100 to 10,000 for a loop |
| 24 | location: 3 doubles (a still's position on the screen, x, y and depth) |
| 24 | direction: 3 doubles, (0, 0, 1) in all but 143 |
| 24 | up: 3 doubles, (0, 1, 0) in all |
| 2 | the extra string's length, its NUL included |
| that | the extra string: settings as `key:value` pairs, below |

Strings end with a NUL. Then a medium has more fields, and a container its
children.

| Type | What it is | On the CD |
|---|---|---|
| 3 | an animation: `FLC` or `SMK` | 841 |
| 4 | a sound: `WAV` | 3,536 |
| 6 | a container whose children play one after another *(inferred)* | 7 |
| 7 | a container whose children play together | 1,095 |
| 8 | an event: `EVT` | 33 |
| 9 | a container playing one of its children, chosen by a variable | 4 |
| 10 | a still picture: `STL` | 688 |
| 11 | a 3D object: `OBJ` (a model, animation, texture or path) | 1,161 |

### A medium's fields

| Size | Field |
|---|---|
| string | the file it was made from, on the developers' machines (`Q:\Lego\Media\JukeBox\backgrnd.bmp`); `inline` for some 3D objects |
| 4 | 0 in every record |
| 4 | 0 in every record |
| 4 | frames a second: 10, or 15 for one movie, in animations (as their FLC and Smacker headers say); 1 in the rest |
| 4 | format, a FourCC with a leading space: `" FLC"`, `" SMK"`, `" WAV"`, `" STL"`, `" EVT"`, `" OBJ"` |
| 4 | 1, or 0 for some stills and animations: whether the game manages its palette *(inferred)* |
| 4 | 0 in most; -1, 1,000 and up to 54,100 in a few |
| 4 | a sound's only: its volume, 1 to 100 |

### A container's children

After the extra string, a `LIST` chunk of type `MxCh` holds the children:
a count, then each child's `MxOb` chunk. A type 9 container has, before
them, the name of the variable choosing among them (`RANDOM_3`), the
count, and each child's value (`0`, `1`, `2`). The variables on the CD
are `RANDOM_2` and `RANDOM_3`.

### The extra string

Settings for the class playing the object, `key:value` pairs between
commas, spaces, tabs, line breaks or colons, the keys in any case:
`World:current`, `Visibility:FALSE`, `BMP_ISMAP`, `Style:Map`,
`DB_CREATE:ambul`, `PTATCAM:HEAD;INFOHAT`... The
[legoisland.org wiki](https://legoisland.org/wiki/Interleaf_File#Extra_string)
lists what's known of them *(published)*.

### `MxCh`: a chunk of an object's data

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | flags: `0x02` the object's last chunk, with no data; `0x10` part of a split piece |
| 2 | 4 | the object's id |
| 6 | 4 | time, in milliseconds from the object's start; -1 for its header |
| 10 | 4 | size of the piece's data from this chunk on: the whole piece in its first chunk, what's left in each after |
| 14 | ... | data |

An object's chunks are in the file in the order they play, interleaved
with other objects'. Its first is its header, timed -1, and its last an
end chunk timed when it ends; a container has only the end chunk.

### Each format's pieces

| Format | Header | Then |
|---|---|---|
| `STL` | `BITMAPINFOHEADER` and a 256-colour palette (1,064 bytes) | one piece: the pixels, bottom row first. All 8-bit |
| `WAV` | `WAVEFORMAT`'s 16 bytes (PCM, mono; 11,025 Hz 8- or 16-bit, or 22,050 Hz 8-bit), the source file's data size, and 44 (the size of the source's header, presumably *(guessed)*) | a piece a second of sound. Together they hold up to 22 bytes less than the source's data size, and a few sounds much less, cut to their duration |
| `FLC` | the FLIC file's 128-byte header (frames, size, 100 ms a frame) | a piece a frame: a count of rectangles, each 4 `u32`s (left, top, right, bottom: what the frame changes), then the FLIC frame (`0xF1FA`). No loop frame |
| `SMK` | the Smacker file's header and tables, up to its first frame | a piece a frame, as in the file |
| `OBJ` | one piece in most: the animation, model, texture or path it was made from, and a 0 byte ([obj.md](obj.md)) | |
| `EVT` | 16 bytes, the same in all 33: `01 00 00 00 02` and 3 bytes of `cd`, twice | one piece: a setting's name and its value, [below](#evt-an-event) |

### `EVT`: an event

An event sets something for the scenes around it, by name. Its data
piece is two strings, each ending with a NUL, and nothing after them:
the setting's name and its value. The CD's 33, in five SI files:

| Name | Value | Events | Where |
|---|---|---|---|
| `patch` | `patch` | 23 | `RACE\CARRACER.SI` (`NRT100DR_Anim`...) |
| `HIT_WALL_SOUND`, `HIT_ACTOR_SOUND` | `HitWall`, `HitActor` | 2 each | the car and jet ski races; the values are their sound objects' names without `Sound` (`HitWallSound`, beside them) |
| `tempBackgroundColor` | `set 0 0 0`, `set 67 10 100` | 3 | `RACE\CARRACER.SI`, its skies (`TRS100_BlackSky`, `TRS200_DkBlueSky`) |
| `backgroundColor` | `reset` | 1 | `RACE\CARRACER.SI` (`TRS400_BlueSky`) |
| `ACT2_ANIMS_FILE` | `l:\lego\scripts\Act2\Act2Main` | 1 | `ACT2\ACT2MAIN.SI` |
| `CUSTOMIZE_ANIM_FILE` | `\lego\scripts\sndanim` | 1 | `SNDANIM.SI` |

The header's `cd` bytes are the fill a Microsoft compiler's debug build
leaves in memory never written, presumably *(guessed)*.

## In Reluva

The data viewer's page shows each SI file's objects and plays their
media. `SiFile` (`src/engines/legoisland/formats/si.cpp`) reads the header,
the tree of objects, and an index of every chunk, and reads an object's
pieces, split chunks joined, when asked; `rebuildMediaFile` makes a
medium's pieces the `.bmp`, `.wav`, `.flc` or `.smk` file it was made
from.

- `reluva-tool legoisland si-info <file.si>`: the header, and counts.
- `reluva-tool legoisland si-list <file.si>`: the objects, as a tree,
  with where each is put (a still's place on the screen).
- `reluva-tool legoisland si-chunks <file.si> <object id>`: an object's
  chunks.
- `reluva-tool legoisland si-events <file.si>`: the events, each its
  setting's name and value (`parseEvent`).
- `reluva-tool legoisland si-extract <file.si> <out folder> [object id]`:
  the media as files; 3D objects as `.ani`, `.mod`, `.tex` and `.gph`
  files ([obj.md](obj.md)); `EVT` objects as their pieces joined, in
  `.bin` files. The FLC animations decode with the shared FLIC decoder
  ([flic.md](../../../common/video/flic.md)): `reluva-tool flic-frames`.

## Sources and checks

- **Where the facts come from:** the
  [legoisland.org wiki](https://legoisland.org/wiki/Interleaf_File) for
  the file's purpose, `MxHd`, `MxOf`, `MxCh` and its flags, buffers, the
  sounds' formats and the extra string *(published)*, each checked on the
  CD's files. `MxOb`, the media's headers and pieces, the filler and the
  select containers were worked out from the files: every record laid
  side by side, and `JUKEBOXW.SI` compared with its source script
  (`JUKEBOXW.SS`, which shipped by mistake with the Korean release and is
  quoted on the [wiki](https://legoisland.org/wiki/Weaver)), whose
  `location=(145,20,8)`, `duration=INDEFINITE`, `transparency=YES` and
  `paletteManagement=NONE` are the records' values.
- **How it was checked:** every file read to its end by `SiFile` and by
  a separate script, both finding the same 7,365 objects and 6,259 media;
  every medium rebuilt and checked: each `.wav`'s length, each `.bmp`'s
  pixels against its size, each `.flc`'s frames walked to its end, each
  `.smk`'s frame sizes adding up to its length; every event read by
  `reluva-tool legoisland si-events`, all 33 alike. `legoisland.unit.si`
  reads a file built in the test, with split pieces, a pad chunk and
  filler, and an event.

## Open questions

- `MxOf`'s first value, and the medium's 0, 0 and last fields.
- The flags `0x01`, `0x02` and `0x04`; whether `0x08` is transparency
  and the medium's 1 or 0 palette management, or the other way round
  (the jukebox's script sets both together).
- What an event's header and the race's `patch` events mean to the
  game, and what `tempBackgroundColor`'s three numbers are (`67 10 100`
  for the dark blue sky).
