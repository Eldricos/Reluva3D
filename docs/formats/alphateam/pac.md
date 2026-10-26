# LEGO Alpha Team: `.pac` archives

Source: `Bhvr.pac` (74 files) and `Etc.pac` (475 files) from the English
retail install, read by hand and then with a short script that walked the
directory and checked it against the file (October 2026). Every entry in
both archives lies inside the file, and the entries follow one another
with no gaps. The other archives haven't been checked yet.

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

The directory ends 4 bytes before the header's data start in both
archives. Those 4 bytes are zero.

## Contents seen

- `Bhvr.pac`: 74 Windows DLLs (4.5 MB), one per kind of puzzle part,
  named by the part's code (`T0001.DLL`, `U0188.DLL`, `CU0003.DLL`...). Each
  exports `getBehavior`, `getBehaviorByName` and `getNumBehaviors`.
  `DDPackedDirFileLib.dll` has its own DLL loader (its message is "DLL Load:
  Import error"), so these are run from the archive without being written
  to disk.
- `Etc.pac`:
  - 349 TGA images: the interface, cursors and briefing pictures;
  - 12 fonts: a `.DDF` (magic `FNT2`, a name, then the glyphs' boxes) plus
    its `_0.TGA`, `_1.TGA` pages;
  - 111 `.PUZ` puzzles. These are text, starting `PUZVER 1`, with one line per
    placed part: the part's code, an actor name, its position and angles, then
    its properties (`Locked`, `ElectricSink`, `Momentum 6207.67`). Cameras
    have lines of their own. Most missions have a `_SOLVED.PUZ` beside them;
  - 3 `.TXT` files, tab-separated lists of the parts by category
    (`CATEGORYUNITS.TXT`).
