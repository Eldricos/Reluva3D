# LEGO Alpha Team: `.pac` archives

Source: the eight archives of the English retail install (2,462 files in
all), October 2026. The layout was worked out by hand on `Bhvr.pac` and
`Etc.pac`. A short script then walked every archive's directory and checked
it against the file. In all eight, every entry lies inside the file and the
entries follow one another with no gaps, apart from the placeholders below.

All numbers are little-endian.

## Header (20 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `PACK` |
| 4 | 4 | the archive's size in bytes (equal to the file's size in both) |
| 8 | 4 | 0 in both |
| 12 | 4 | where the first file's data starts (the directory's end) |
| 16 | 4 | number of files |

## Directory

It comes right after the header: one entry per file, in the order their
data comes in.

| Size | Field |
|---|---|
| variable | the name, zero-terminated, upper case, with no folders (`B0001.DLL`, `ABADI MT CONDENSED.DDF`) |
| 4 | 0 in every entry seen; *guess*: flags (no data is compressed in these two archives) |
| 4 | where the data starts, from the start of the archive |
| 4 | the data's size |
| 8 | a Windows FILETIME, the file's date (17 to 22 August 2000 in `Bhvr.pac`; `Etc.pac`'s go back to March 1999) |

The directory ends 4 bytes before the header's data start in every
archive. Those 4 bytes are zero.

**Placeholders.** `Ambient.pac` and `LMap.pac` each list one entry named after
the archive itself (`AMBIENT.PAC`, first; `LMAP.PAC`, last) with offset,
size and date all 0. The packing tool probably listed its own output file
while writing it. They're read as empty files.

## Reading them

`src/engines/netimmerse/pac_archive.{h,cpp}` reads them as an `Archive`.
Names are looked up ignoring case. It checks the header's size against the
file's, and checks that every entry's name and fields lie inside the
directory and its data inside the archive.
Entries whose flags aren't 0 are listed but not read, until an archive
shows what the flags mean. `reluva-tool pac-list <file.pac>` lists an
archive (offset, size, date, name), and `reluva-tool pac-extract <file.pac>
<dir> [filter]` writes its files out.

## Contents seen

| Archive | Files | What they are |
|---|---|---|
| `Ambient.pac` | 27 `.DDS` (+1 placeholder) | stereo sound, one per area (`E0000`, `E1010`...): ambience or music |
| `Audio.pac` | 169 `.DDS` | sound effects (`BEEP004`, `BIGDRILL`, `B0002_FIDGET`...) |
| `Bhvr.pac` | 74 `.DLL` | the puzzle parts' code (below) |
| `Etc.pac` | 349 `.TGA`, 12 `.DDF`, 111 `.PUZ`, 3 `.TXT` | the interface, fonts and puzzles (below) |
| `Fin.pac` | 147 `.FIN` | 3D models, one per part (`B0001.FIN`...) |
| `LMap.pac` | 582 `.TGA` (+1 placeholder) | lightmaps, by area (`E0000LM0036RC.TGA`) |
| `Locale_en.pac` | 302 `.DDS`, 6 `.TXT` | English voice, and the text |
| `Map.pac` | 678 `.TGA` | textures |

- **`.DDS` is sound, not DirectDraw Surface images.** Each file is an 18-byte
  Windows `WAVEFORMATEX` (format 1 = PCM, channels, rate, bytes per second,
  block align, bits, then an extra size of 0) followed by the samples, with
  no RIFF wrapper. All 498 follow it exactly: 425 mono and 72 stereo at
  22,050 Hz, and one mono at 44,100 Hz, all 16-bit. Their sizes are whole
  samples. That's 1.2 hours in all.
- **`.FIN` files are NetImmerse model streams.** All 147 start with the line
  `Dweezil 23` instead of NetImmerse's own header text. The objects follow,
  each named by a length-prefixed class name (`NiTriShape`...). The DLLs
  call themselves "NetImmerse Version 7.0" and know the header "NetImmerse
  File Format, Version 7.0". See [../netimmerse/fin.md](../netimmerse/fin.md).
- **`.TGA`**: uncompressed true colour (type 2), greyscale (type 3) and RLE
  true colour (type 10).
- **Locale `.TXT`**: `==key==` lines, each followed by its text
  (`BRIEFING_EN`, `CREDITS_EN`, `SPACES_EN`, `UNITS_EN`), and `ETC_EN`, a
  commented list of interface strings.

- `Bhvr.pac`: 74 Windows DLLs (4.5 MB), one per kind of puzzle part,
  named by the part's code (`T0001.DLL`, `U0188.DLL`, `CU0003.DLL`...). Each
  exports `getBehavior`, `getBehaviorByName` and `getNumBehaviors`.
  `DDPackedDirFileLib.dll` has its own DLL loader (its message is "DLL Load:
  Import error"), so these are run from the archive without being written
  to disk.
- `Etc.pac`:
  - 349 TGA images: the interface, cursors and briefing pictures;
  - 12 fonts: a `.DDF` (magic `FNT2`, a name, then the glyphs' boxes) plus
    its `_0.TGA`, `_1.TGA` pages. See [../netimmerse/ddf.md](../netimmerse/ddf.md);
  - 111 `.PUZ` puzzles. These are text, starting `PUZVER 1`, with one line per
    placed part: the part's code, an actor name, its position and angles, then
    its properties (`Locked`, `ElectricSink`, `Momentum 6207.67`). Cameras
    have lines of their own. Most missions have a `_SOLVED.PUZ` beside them;
  - 3 `.TXT` files, tab-separated lists of the parts by category
    (`CATEGORYUNITS.TXT`).
