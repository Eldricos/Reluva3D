# Jedi Knight: cut scenes (`.smk`)

> **Status:** decoded · **Code:** `src/video/smacker.cpp` · **Checked on:** US retail, all 25 cut scenes on both CDs · **Confidence:** confirmed unless marked

## Overview

The cut scenes are Smacker videos ([Smacker](../../../../../common/video/smacker.md))
on the CDs, in `GAMEDATA/RESOURCE/VIDEO`: 8 on CD 1, 17 on CD 2; the
install has none. The episode names which plays where
([episode-jk.md](episode-jk.md)); `01-02A.SMK`, the opening, is named by
`JK.EXE`. `JK.EXE` plays them through `SMACKW32.DLL`, RAD's player,
which it imports by number.

## Layout

All 25 alike:

| | |
|---|---|
| Kind | `SMK2`, no flags |
| Picture | 640x300, 8-bit, its palette in the video |
| Speed | 15 frames a second |
| Sound | One track: 16-bit stereo, 22,050 Hz, packed |
| Subtitles | A second track in 20 of them: a number a frame ([Subtitles](#subtitles)) |
| Length | 6.8 s (`41DA.SMK`) to 229.7 s (`01-02A.SMK`); 29,493 frames, about 33 minutes |

### Subtitles

20 of the cut scenes have a second track, 4 bytes in the header's table
of track sizes and no sound format; 5 (`25A`, `41DA`, `41DSA`, `46A`,
`50A`) have none. Most frames carry it: a 32-bit number, little-endian.
Its low 31 bits are a string's place in `ui\cutstrings.uni`
([uni.md](uni.md)), counted from 0 in the file's order, where `CUT_n` is
the n-th; 0 is none (`0x4214f2`) *(from the code)*. Each starts with 1
(`CUT_1`, empty), and the numbers that follow name the lines as
they're spoken, as `01-02A`'s follow its dialogue *(inferred)*.
Its top bit says the string is shown even without full subtitles: only
`03-04A.SMK` has one, 186 (`TITLE_186`, "Nar Shaddaa - The Vertical
City") in frames 30 to 59.

## Behaviour

In `JK.EXE`: `0x421310` (the start), `0x421450` (each frame), `0x426730`
and `0x4269c0` (the calls to `SMACKW32.DLL`).

- **The picture:** in a 640x480 mode, at (0, 50): rows 50 to 349, black
  above and below; the rectangle (0, 50, 640, 300) is the game's own,
  whatever the video's size (`0x4213a0`). Seen in Reluva's emulator, in
  `01-02A` and `03-04A`.
- **The subtitles:** each frame the game reads the second track's
  number (`SmackGetTrackData`, ordinal 26, `0x426b56`); when it changes,
  it clears the area (10, 360, 620, 120), below the picture, and draws
  the string there with `ui\sft\subtitlefont.sft`, centred, when the
  number's top bit is set or *Enable full subtitles on cutscenes* is on
  (`0x4214b8`) *(from the code)*. They aren't drawn over the picture.
  That setting is off for a new player (`0x401487`) *(from the code)*,
  so by default only the Nar Shaddaa title shows, as it does in the
  emulator at 2 seconds into `03-04A`. With the setting on, every line
  shows: in `03-04A` the first spoken line at about 76 seconds. The text
  is drawn by `0x4349d0`, broken into lines by `0x4352c0` *(confirmed:
  read in the code and seen in Reluva's emulator)*:
  - a word runs to the next white space (the C library's, so a line
    break is white space too, U+2028 and U+2029 among it), and takes
    the white space after it with it;
  - a line ends before a word that would take it to the area's width
    or past (620: `>=`), unless that word is the line's first;
  - each line is centred by its width, the white space it ends with
    counted (the first line of `03-04A`'s first spoken line 2 pixels
    further left than without it), at x = 10 + (620 - width) / 2,
    rounded towards 0;
  - the lines are the font's strip's height and its gap apart: 17 rows
    for `subtitlefont.sft` (17 and 0); a line past the area's bottom is
    cut there (`0x434cca`). No subtitle has more than three lines.
- **Keys:** space pauses and goes on, "Paused" (`GUI_PAUSED`) in the
  strip at the top (0, 10, 640, 40); Escape ends the cut scene
  (`0x4215c0`) *(from the code)*.

## In Reluva

Read by the shared decoder (`smacker::Decoder`, `src/video/smacker.h`).
`findCutScenes` (`src/engines/sith/folders.h`) lists the cut scenes on
the CDs found for an install. The data viewer's Cut scenes page plays
them with their sound, the picture following the clock, placed as on the
game's 640x480 screen, with the subtitles below it as the game draws
them: the titles, or every line with S, as the game's full subtitles,
broken into lines by the game's rules above (`subtitleLines`,
`src/engines/sith/pages/pages.h`) and spaced and centred as it does: the
same frame of `03-04A`, the game's and the viewer's, is the same to the
pixel below the picture, and each of the 376 subtitles breaks where the
earlier, seen-only rules broke it. A line past the area's bottom, which
no subtitle reaches, the viewer leaves out rather than cutting.
`reluva-tool sith movies <install> -all`
decodes every frame and its sound.

## Sources and checks

- **Where the facts come from:** the 25 files' headers, and their
  second tracks read frame by frame; `JK.EXE`'s imports and strings,
  `SMACKW32.DLL`'s exports (the names of the ordinals it imports), and
  its machine code ([Behaviour](#behaviour)); `ui\cutstrings.uni`.
- **How it was checked:** every frame and the sound of all 25 decoded by
  `reluva-tool sith movies -all`, the sound as long as the picture in
  each; a new game run by `reluva-tool sith host` up to `03-04A`, its
  frames saved each half second: the picture in rows 50 to 349, the
  title below it; then again with *Enable full subtitles on cutscenes*
  ticked in *Setup*, *General*: every line below the picture, the long
  ones on two lines, compared with the viewer's.

## Open questions

None.
